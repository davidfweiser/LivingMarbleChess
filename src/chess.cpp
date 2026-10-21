#include "chess.h"

#include <cstring>
#include <sstream>

namespace chess {

// ---------------------------------------------------------------------------
// Precomputed attack tables
// ---------------------------------------------------------------------------
namespace {

struct Tables {
  int knightT[64][8], knightN[64];
  int kingT[64][8], kingN[64];
  int ray[64][8][8], rayN[64][8];
  uint64_t zPiece[16][64], zSide, zCastle[16], zEp[8];

  Tables() {
    const int kdf[8] = {1, 2, 2, 1, -1, -2, -2, -1};
    const int kdr[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    const int gdf[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int gdr[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    for (int s = 0; s < 64; ++s) {
      int f = s & 7, r = s >> 3;
      knightN[s] = kingN[s] = 0;
      for (int i = 0; i < 8; ++i) {
        int nf = f + kdf[i], nr = r + kdr[i];
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) knightT[s][knightN[s]++] = nr * 8 + nf;
        nf = f + gdf[i];
        nr = r + gdr[i];
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) kingT[s][kingN[s]++] = nr * 8 + nf;
      }
      // directions: 0 N, 1 S, 2 E, 3 W (orthogonal) / 4 NE, 5 NW, 6 SE, 7 SW (diagonal)
      for (int d = 0; d < 8; ++d) {
        rayN[s][d] = 0;
        int nf = f, nr = r;
        for (;;) {
          nf += gdf[d];
          nr += gdr[d];
          if (nf < 0 || nf > 7 || nr < 0 || nr > 7) break;
          ray[s][d][rayN[s][d]++] = nr * 8 + nf;
        }
      }
    }
    uint64_t x = 0x9E3779B97F4A7C15ull;
    auto rnd = [&x]() {
      x += 0x9E3779B97F4A7C15ull;
      uint64_t z = x;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      return z ^ (z >> 31);
    };
    for (auto& a : zPiece)
      for (auto& v : a) v = rnd();
    zSide = rnd();
    for (auto& v : zCastle) v = rnd();
    for (auto& v : zEp) v = rnd();
  }
};
const Tables T;

}  // namespace

std::string sqName(int sq) {
  std::string s;
  s += char('a' + sqFile(sq));
  s += char('1' + sqRank(sq));
  return s;
}

std::string Move::uci() const {
  std::string s = sqName(from) + sqName(to);
  if (promo) s += " nbrqk"[promo];
  return s;
}

// ---------------------------------------------------------------------------
// Board
// ---------------------------------------------------------------------------
void Board::setStart() { setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }

static int charToPiece(char c) {
  int color = (c >= 'a' && c <= 'z') ? BLACK : WHITE;
  switch (c | 32) {
    case 'p': return makePiece(PAWN, color);
    case 'n': return makePiece(KNIGHT, color);
    case 'b': return makePiece(BISHOP, color);
    case 'r': return makePiece(ROOK, color);
    case 'q': return makePiece(QUEEN, color);
    case 'k': return makePiece(KING, color);
  }
  return 0;
}

static char pieceToChar(int p) {
  static const char* s = ".PNBRQK";
  char c = s[pieceType(p)];
  return pieceColor(p) == BLACK ? char(c | 32) : c;
}

bool Board::setFEN(const std::string& fen) {
  std::istringstream ss(fen);
  std::string placement, stm = "w", cast = "-", eps = "-";
  int hm = 0, fm = 1;
  ss >> placement >> stm >> cast >> eps >> hm >> fm;
  std::memset(sq, 0, sizeof(sq));
  int r = 7, f = 0;
  int kings[2] = {-1, -1};
  for (char c : placement) {
    if (c == '/') {
      r--;
      f = 0;
    } else if (c >= '1' && c <= '8') {
      f += c - '0';
    } else {
      int p = charToPiece(c);
      if (!p || r < 0 || f > 7) return false;
      sq[r * 8 + f] = (uint8_t)p;
      if (pieceType(p) == KING) kings[pieceColor(p)] = r * 8 + f;
      f++;
    }
  }
  if (kings[0] < 0 || kings[1] < 0) return false;
  kingSq[0] = kings[0];
  kingSq[1] = kings[1];
  side = (stm == "b") ? BLACK : WHITE;
  castling = 0;
  for (char c : cast) {
    if (c == 'K') castling |= WK;
    if (c == 'Q') castling |= WQ;
    if (c == 'k') castling |= BK;
    if (c == 'q') castling |= BQ;
  }
  ep = -1;
  if (eps.size() == 2) ep = makeSq(eps[0] - 'a', eps[1] - '1');
  halfmove = hm;
  fullmove = fm;
  hash = computeHash();
  return true;
}

std::string Board::fen() const {
  std::string s;
  for (int r = 7; r >= 0; --r) {
    int empty = 0;
    for (int f = 0; f < 8; ++f) {
      int p = sq[r * 8 + f];
      if (!p) {
        empty++;
        continue;
      }
      if (empty) s += char('0' + empty), empty = 0;
      s += pieceToChar(p);
    }
    if (empty) s += char('0' + empty);
    if (r) s += '/';
  }
  s += side == WHITE ? " w " : " b ";
  std::string c;
  if (castling & WK) c += 'K';
  if (castling & WQ) c += 'Q';
  if (castling & BK) c += 'k';
  if (castling & BQ) c += 'q';
  s += c.empty() ? "-" : c;
  s += ' ';
  s += ep >= 0 ? sqName(ep) : "-";
  s += ' ' + std::to_string(halfmove) + ' ' + std::to_string(fullmove);
  return s;
}

uint64_t Board::computeHash() const {
  uint64_t h = 0;
  for (int s = 0; s < 64; ++s)
    if (sq[s]) h ^= T.zPiece[sq[s]][s];
  if (side == BLACK) h ^= T.zSide;
  h ^= T.zCastle[castling];
  if (ep >= 0) h ^= T.zEp[ep & 7];
  return h;
}

bool Board::attacked(int s, int by) const {
  // pawns
  int pawn = makePiece(PAWN, by);
  int f = s & 7;
  if (by == WHITE) {
    if (s >= 8) {
      if (f > 0 && sq[s - 9] == pawn) return true;
      if (f < 7 && sq[s - 7] == pawn) return true;
    }
  } else {
    if (s < 56) {
      if (f > 0 && sq[s + 7] == pawn) return true;
      if (f < 7 && sq[s + 9] == pawn) return true;
    }
  }
  int knight = makePiece(KNIGHT, by);
  for (int i = 0; i < T.knightN[s]; ++i)
    if (sq[T.knightT[s][i]] == knight) return true;
  int king = makePiece(KING, by);
  for (int i = 0; i < T.kingN[s]; ++i)
    if (sq[T.kingT[s][i]] == king) return true;
  int rook = makePiece(ROOK, by), bishop = makePiece(BISHOP, by), queen = makePiece(QUEEN, by);
  for (int d = 0; d < 8; ++d) {
    for (int i = 0; i < T.rayN[s][d]; ++i) {
      int p = sq[T.ray[s][d][i]];
      if (!p) continue;
      if (p == queen) return true;
      if (d < 4 ? p == rook : p == bishop) return true;
      break;
    }
  }
  return false;
}

static inline void addMove(MoveList& out, int from, int to, int piece, int captured, int flags, int promo = 0) {
  Move m;
  m.from = (uint8_t)from;
  m.to = (uint8_t)to;
  m.piece = (uint8_t)piece;
  m.captured = (uint8_t)captured;
  m.flags = (uint8_t)flags;
  m.promo = (uint8_t)promo;
  out.push(m);
}

void Board::genPseudo(MoveList& out, bool capturesOnly) const {
  const int us = side, them = side ^ 1;
  for (int s = 0; s < 64; ++s) {
    int p = sq[s];
    if (!p || pieceColor(p) != us) continue;
    int t = pieceType(p);
    if (t == PAWN) {
      int dir = us == WHITE ? 8 : -8;
      int startRank = us == WHITE ? 1 : 6;
      int promoRank = us == WHITE ? 7 : 0;
      int to = s + dir;
      int f = s & 7;
      auto pushPawn = [&](int target, int cap, int flags) {
        if ((target >> 3) == promoRank) {
          for (int pr = QUEEN; pr >= KNIGHT; --pr) addMove(out, s, target, p, cap, flags | MF_PROMO, pr);
        } else if (!capturesOnly || (flags & MF_CAPTURE)) {
          addMove(out, s, target, p, cap, flags);
        }
      };
      if (to >= 0 && to < 64 && !sq[to]) {
        pushPawn(to, 0, 0);
        if ((s >> 3) == startRank && !sq[to + dir] && !capturesOnly)
          addMove(out, s, to + dir, p, 0, MF_DOUBLE);
      }
      for (int df = -1; df <= 1; df += 2) {
        int nf = f + df;
        if (nf < 0 || nf > 7) continue;
        int c = s + dir + df;
        if (c < 0 || c > 63) continue;
        if (sq[c] && pieceColor(sq[c]) == them) pushPawn(c, sq[c], MF_CAPTURE);
        else if (c == ep) addMove(out, s, c, p, makePiece(PAWN, them), MF_CAPTURE | MF_EP);
      }
    } else if (t == KNIGHT || t == KING) {
      const int* tg = t == KNIGHT ? T.knightT[s] : T.kingT[s];
      int n = t == KNIGHT ? T.knightN[s] : T.kingN[s];
      for (int i = 0; i < n; ++i) {
        int to = tg[i];
        int q = sq[to];
        if (!q) {
          if (!capturesOnly) addMove(out, s, to, p, 0, 0);
        } else if (pieceColor(q) == them) {
          addMove(out, s, to, p, q, MF_CAPTURE);
        }
      }
      if (t == KING && !capturesOnly) {
        if (us == WHITE && s == 4) {
          if ((castling & WK) && !sq[5] && !sq[6] && sq[7] == makePiece(ROOK, WHITE) && !attacked(4, them) &&
              !attacked(5, them) && !attacked(6, them))
            addMove(out, 4, 6, p, 0, MF_CASTLE);
          if ((castling & WQ) && !sq[3] && !sq[2] && !sq[1] && sq[0] == makePiece(ROOK, WHITE) &&
              !attacked(4, them) && !attacked(3, them) && !attacked(2, them))
            addMove(out, 4, 2, p, 0, MF_CASTLE);
        } else if (us == BLACK && s == 60) {
          if ((castling & BK) && !sq[61] && !sq[62] && sq[63] == makePiece(ROOK, BLACK) && !attacked(60, them) &&
              !attacked(61, them) && !attacked(62, them))
            addMove(out, 60, 62, p, 0, MF_CASTLE);
          if ((castling & BQ) && !sq[59] && !sq[58] && !sq[57] && sq[56] == makePiece(ROOK, BLACK) &&
              !attacked(60, them) && !attacked(59, them) && !attacked(58, them))
            addMove(out, 60, 58, p, 0, MF_CASTLE);
        }
      }
    } else {
      int d0 = t == BISHOP ? 4 : 0;
      int d1 = t == ROOK ? 4 : 8;
      for (int d = d0; d < d1; ++d) {
        for (int i = 0; i < T.rayN[s][d]; ++i) {
          int to = T.ray[s][d][i];
          int q = sq[to];
          if (!q) {
            if (!capturesOnly) addMove(out, s, to, p, 0, 0);
            continue;
          }
          if (pieceColor(q) == them) addMove(out, s, to, p, q, MF_CAPTURE);
          break;
        }
      }
    }
  }
}

bool Board::make(const Move& m) {
  const int us = side, them = side ^ 1;
  int p = sq[m.from];
  // remove old castle/ep from hash
  hash ^= T.zCastle[castling];
  if (ep >= 0) hash ^= T.zEp[ep & 7];

  // capture
  if (m.flags & MF_CAPTURE) {
    int cs = m.captureSquare();
    hash ^= T.zPiece[sq[cs]][cs];
    sq[cs] = 0;
  }
  // move piece
  hash ^= T.zPiece[p][m.from];
  sq[m.from] = 0;
  int placed = (m.flags & MF_PROMO) ? makePiece(m.promo, us) : p;
  sq[m.to] = (uint8_t)placed;
  hash ^= T.zPiece[placed][m.to];

  if (pieceType(p) == KING) {
    kingSq[us] = m.to;
    castling &= us == WHITE ? ~(WK | WQ) : ~(BK | BQ);
    if (m.flags & MF_CASTLE) {
      int rf, rt;
      if (m.to > m.from) {
        rf = m.from + 3;
        rt = m.from + 1;
      } else {
        rf = m.from - 4;
        rt = m.from - 1;
      }
      int rook = sq[rf];
      hash ^= T.zPiece[rook][rf] ^ T.zPiece[rook][rt];
      sq[rt] = (uint8_t)rook;
      sq[rf] = 0;
    }
  }
  // castling rights from rook squares
  auto clearCorner = [&](int s) {
    if (s == 0) castling &= ~WQ;
    if (s == 7) castling &= ~WK;
    if (s == 56) castling &= ~BQ;
    if (s == 63) castling &= ~BK;
  };
  clearCorner(m.from);
  clearCorner(m.to);

  ep = (m.flags & MF_DOUBLE) ? (m.from + m.to) / 2 : -1;
  halfmove = (pieceType(p) == PAWN || (m.flags & MF_CAPTURE)) ? 0 : halfmove + 1;
  if (us == BLACK) fullmove++;
  side = them;
  hash ^= T.zSide;
  hash ^= T.zCastle[castling];
  if (ep >= 0) hash ^= T.zEp[ep & 7];
  return !attacked(kingSq[us], them);
}

void Board::makeNull() {
  if (ep >= 0) hash ^= T.zEp[ep & 7];
  ep = -1;
  side ^= 1;
  hash ^= T.zSide;
  halfmove++;
}

void Board::genLegal(MoveList& out) const {
  MoveList pl;
  genPseudo(pl);
  out.n = 0;
  for (const Move& m : pl) {
    Board c = *this;
    if (c.make(m)) out.push(m);
  }
}

bool Board::insufficientMaterial() const {
  int minors[2] = {0, 0};
  int bishopColors[2] = {0, 0};
  for (int s = 0; s < 64; ++s) {
    int p = sq[s];
    if (!p) continue;
    int t = pieceType(p);
    if (t == PAWN || t == ROOK || t == QUEEN) return false;
    if (t == KNIGHT || t == BISHOP) {
      minors[pieceColor(p)]++;
      if (t == BISHOP) bishopColors[pieceColor(p)] |= 1 << (((s >> 3) + (s & 7)) & 1);
    }
  }
  if (minors[0] + minors[1] <= 1) return true;
  return false;
}

int Board::nonPawnMaterial(int color) const {
  static const int v[7] = {0, 0, 3, 3, 5, 9, 0};
  int t = 0;
  for (int s = 0; s < 64; ++s)
    if (sq[s] && pieceColor(sq[s]) == color) t += v[pieceType(sq[s])];
  return t;
}

// ---------------------------------------------------------------------------
std::string toSAN(const Board& b, const Move& m) {
  std::string s;
  int t = pieceType(m.piece);
  if (m.flags & MF_CASTLE) {
    s = (m.to > m.from) ? "O-O" : "O-O-O";
  } else {
    if (t != PAWN) {
      s += " PNBRQK"[t];
      MoveList legal;
      b.genLegal(legal);
      bool clash = false, sameFile = false, sameRank = false;
      for (const Move& o : legal) {
        if (o.to == m.to && o.from != m.from && pieceType(o.piece) == t) {
          clash = true;
          if (sqFile(o.from) == sqFile(m.from)) sameFile = true;
          if (sqRank(o.from) == sqRank(m.from)) sameRank = true;
        }
      }
      if (clash) {
        if (!sameFile) s += char('a' + sqFile(m.from));
        else if (!sameRank) s += char('1' + sqRank(m.from));
        else s += sqName(m.from);
      }
      if (m.flags & MF_CAPTURE) s += 'x';
    } else if (m.flags & MF_CAPTURE) {
      s += char('a' + sqFile(m.from));
      s += 'x';
    }
    s += sqName(m.to);
    if (m.flags & MF_PROMO) {
      s += '=';
      s += " PNBRQK"[m.promo];
    }
  }
  Board c = b;
  c.make(m);
  if (c.inCheck()) {
    MoveList r;
    c.genLegal(r);
    s += r.n == 0 ? '#' : '+';
  }
  return s;
}

bool findLegal(const Board& b, int from, int to, int promo, Move& out) {
  MoveList legal;
  b.genLegal(legal);
  for (const Move& m : legal) {
    if (m.from == from && m.to == to) {
      if ((m.flags & MF_PROMO) && m.promo != (promo ? promo : QUEEN)) continue;
      out = m;
      return true;
    }
  }
  return false;
}

uint64_t perft(const Board& b, int depth) {
  MoveList pl;
  b.genPseudo(pl);
  uint64_t n = 0;
  for (const Move& m : pl) {
    Board c = b;
    if (!c.make(m)) continue;
    n += depth <= 1 ? 1 : perft(c, depth - 1);
  }
  return n;
}

const char* resultText(Result r) {
  switch (r) {
    case Result::WhiteMates: return "Checkmate - White wins";
    case Result::BlackMates: return "Checkmate - Black wins";
    case Result::Stalemate: return "Stalemate - draw";
    case Result::FiftyMove: return "Fifty-move rule - draw";
    case Result::Repetition: return "Threefold repetition - draw";
    case Result::Material: return "Insufficient material - draw";
    default: return "";
  }
}

// ---------------------------------------------------------------------------
void Game::reset() {
  cur_.setStart();
  boards_.clear();
  moves_.clear();
  san_.clear();
  hashes_.assign(1, cur_.hash);
  result_ = Result::Ongoing;
}

bool Game::setFEN(const std::string& fen) {
  Board b;
  if (!b.setFEN(fen)) return false;
  reset();
  cur_ = b;
  hashes_.assign(1, cur_.hash);
  updateResult();
  return true;
}

bool Game::play(const Move& m) {
  Board next = cur_;
  if (!next.make(m)) return false;
  san_.push_back(toSAN(cur_, m));
  boards_.push_back(cur_);
  moves_.push_back(m);
  cur_ = next;
  hashes_.push_back(cur_.hash);
  updateResult();
  return true;
}

bool Game::undo() {
  if (boards_.empty()) return false;
  cur_ = boards_.back();
  boards_.pop_back();
  moves_.pop_back();
  san_.pop_back();
  hashes_.pop_back();
  result_ = Result::Ongoing;
  updateResult();
  return true;
}

void Game::updateResult() {
  MoveList legal;
  cur_.genLegal(legal);
  if (legal.n == 0) {
    if (cur_.inCheck()) result_ = cur_.side == WHITE ? Result::BlackMates : Result::WhiteMates;
    else result_ = Result::Stalemate;
    return;
  }
  if (cur_.halfmove >= 100) {
    result_ = Result::FiftyMove;
    return;
  }
  int reps = 0;
  for (uint64_t h : hashes_)
    if (h == cur_.hash) reps++;
  if (reps >= 3) {
    result_ = Result::Repetition;
    return;
  }
  if (cur_.insufficientMaterial()) {
    result_ = Result::Material;
    return;
  }
  result_ = Result::Ongoing;
}

}  // namespace chess
