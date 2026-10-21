#include "ai.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace chess {

const AILevel kAILevels[5] = {
    {"Squire", 2, 0.4, 90},
    {"Knight", 3, 0.8, 30},
    {"Bishop", 5, 1.2, 8},
    {"King", 64, 2.5, 0},
    {"Grandmaster", 64, 6.0, 0},
};

namespace {

// Piece-square tables, written with rank 8 on the first row (white's view).
const int PST_P[64] = {0,  0,  0,  0,   0,   0,  0,  0,  50, 50, 50,  50, 50, 50,  50, 50,
                       10, 10, 20, 30,  30,  20, 10, 10, 5,  5,  10,  25, 25, 10,  5,  5,
                       0,  0,  0,  22,  22,  0,  0,  0,  5,  -5, -10, 0,  0,  -10, -5, 5,
                       5,  10, 10, -20, -20, 10, 10, 5,  0,  0,  0,   0,  0,  0,   0,  0};
const int PST_N[64] = {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   0,   0,   0,   -20, -40,
                       -30, 0,   10,  15,  15,  10,  0,   -30, -30, 5,   15,  20,  20,  15,  5,   -30,
                       -30, 0,   15,  20,  20,  15,  0,   -30, -30, 5,   10,  15,  15,  10,  5,   -30,
                       -40, -20, 0,   5,   5,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50};
const int PST_B[64] = {-20, -10, -10, -10, -10, -10, -10, -20, -10, 0,   0,   0,   0,   0,   0,   -10,
                       -10, 0,   5,   10,  10,  5,   0,   -10, -10, 5,   5,   10,  10,  5,   5,   -10,
                       -10, 0,   10,  10,  10,  10,  0,   -10, -10, 10,  10,  10,  10,  10,  10,  -10,
                       -10, 5,   0,   0,   0,   0,   5,   -10, -20, -10, -10, -10, -10, -10, -10, -20};
const int PST_R[64] = {0,  0, 0, 0, 0, 0, 0, 0,  5,  10, 10, 10, 10, 10, 10, 5,  -5, 0, 0, 0, 0, 0,
                       0,  -5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0, 0, 0,
                       0,  0, 0, -5, -5, 0,  0,  0,  0,  0,  0,  -5, 0,  0,  0,  5,  5,  0,  0,  0};
const int PST_Q[64] = {-20, -10, -10, -5, -5, -10, -10, -20, -10, 0,   0,   0,  0,  0,   0,   -10,
                       -10, 0,   5,   5,  5,  5,   0,   -10, -5,  0,   5,   5,  5,  5,   0,   -5,
                       0,   0,   5,   5,  5,  5,   0,   -5,  -10, 5,   5,   5,  5,  5,   0,   -10,
                       -10, 0,   5,   0,  0,  0,   0,   -10, -20, -10, -10, -5, -5, -10, -10, -20};
const int PST_K_MG[64] = {-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                          -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
                          -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10,
                          20,  20,  0,   0,   0,   0,   20,  20,  20,  30,  10,  0,   0,   10,  30,  20};
const int PST_K_EG[64] = {-50, -40, -30, -20, -20, -30, -40, -50, -30, -20, -10, 0,   0,   -10, -20, -30,
                          -30, -10, 20,  30,  30,  20,  -10, -30, -30, -10, 30,  40,  40,  30,  -10, -30,
                          -30, -10, 30,  40,  40,  30,  -10, -30, -30, -10, 20,  30,  30,  20,  -10, -30,
                          -30, -30, 0,   0,   0,   0,   -30, -30, -50, -30, -30, -30, -30, -30, -30, -50};

const int VAL[7] = {0, 100, 320, 330, 500, 900, 0};
const int PASSED[8] = {0, 5, 10, 20, 35, 60, 100, 0};

constexpr int INF = 32000;
constexpr int MATE = 30000;

inline int tableIndex(int s, int color) { return color == WHITE ? (s ^ 56) : s; }

}  // namespace

int evaluate(const Board& b) {
  int mg[2] = {0, 0}, eg[2] = {0, 0};
  int phase = 0;
  int bishops[2] = {0, 0};
  int pawnsOnFile[2][8] = {};
  int minPawnRank[2][8], maxPawnRank[2][8];
  for (int f = 0; f < 8; ++f) {
    minPawnRank[0][f] = minPawnRank[1][f] = 9;
    maxPawnRank[0][f] = maxPawnRank[1][f] = -1;
  }
  for (int s = 0; s < 64; ++s) {
    int p = b.sq[s];
    if (!p || pieceType(p) != PAWN) continue;
    int c = pieceColor(p), f = s & 7, r = s >> 3;
    pawnsOnFile[c][f]++;
    minPawnRank[c][f] = std::min(minPawnRank[c][f], r);
    maxPawnRank[c][f] = std::max(maxPawnRank[c][f], r);
  }
  for (int s = 0; s < 64; ++s) {
    int p = b.sq[s];
    if (!p) continue;
    int c = pieceColor(p), t = pieceType(p), idx = tableIndex(s, c);
    int f = s & 7, r = s >> 3;
    int v = VAL[t];
    mg[c] += v;
    eg[c] += v;
    switch (t) {
      case PAWN: {
        mg[c] += PST_P[idx];
        eg[c] += PST_P[idx] / 2;
        // passed pawn?
        bool passed = true;
        int them = c ^ 1;
        for (int df = -1; df <= 1 && passed; ++df) {
          int nf = f + df;
          if (nf < 0 || nf > 7) continue;
          if (c == WHITE ? (maxPawnRank[them][nf] > r) : (minPawnRank[them][nf] < r && minPawnRank[them][nf] < 9))
            passed = false;
        }
        if (passed) {
          int adv = c == WHITE ? r : 7 - r;
          mg[c] += PASSED[adv] / 2;
          eg[c] += PASSED[adv];
        }
        bool isolated = (f == 0 || !pawnsOnFile[c][f - 1]) && (f == 7 || !pawnsOnFile[c][f + 1]);
        if (isolated) {
          mg[c] -= 10;
          eg[c] -= 12;
        }
        break;
      }
      case KNIGHT:
        mg[c] += PST_N[idx];
        eg[c] += PST_N[idx];
        phase += 1;
        break;
      case BISHOP:
        mg[c] += PST_B[idx];
        eg[c] += PST_B[idx];
        bishops[c]++;
        phase += 1;
        break;
      case ROOK:
        mg[c] += PST_R[idx];
        eg[c] += PST_R[idx];
        if (!pawnsOnFile[c][f]) {
          int bonus = pawnsOnFile[c ^ 1][f] ? 8 : 18;
          mg[c] += bonus;
          eg[c] += bonus / 2;
        }
        phase += 2;
        break;
      case QUEEN:
        mg[c] += PST_Q[idx];
        eg[c] += PST_Q[idx];
        phase += 4;
        break;
      case KING: {
        mg[c] += PST_K_MG[idx];
        eg[c] += PST_K_EG[idx];
        // pawn shield
        int dir = c == WHITE ? 1 : -1;
        int shield = 0;
        for (int df = -1; df <= 1; ++df) {
          int nf = f + df, nr = r + dir;
          if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
          int q = b.sq[nr * 8 + nf];
          if (q == makePiece(PAWN, c)) shield++;
        }
        mg[c] += shield * 12;
        break;
      }
    }
  }
  for (int c = 0; c < 2; ++c) {
    if (bishops[c] >= 2) {
      mg[c] += 30;
      eg[c] += 45;
    }
    for (int f = 0; f < 8; ++f)
      if (pawnsOnFile[c][f] > 1) {
        mg[c] -= 12 * (pawnsOnFile[c][f] - 1);
        eg[c] -= 18 * (pawnsOnFile[c][f] - 1);
      }
  }
  if (phase > 24) phase = 24;
  int mgScore = mg[WHITE] - mg[BLACK];
  int egScore = eg[WHITE] - eg[BLACK];
  int score = (mgScore * phase + egScore * (24 - phase)) / 24;
  score += b.side == WHITE ? 10 : -10;  // tempo
  return b.side == WHITE ? score : -score;
}

// ---------------------------------------------------------------------------
struct TTEntry {
  uint64_t key;
  int16_t score;
  int8_t depth;
  uint8_t flag;  // 0 empty, 1 exact, 2 lower, 3 upper
  uint8_t from, to, promo, pad;
};

struct AIEngine::Impl {
  std::vector<TTEntry> tt;
  uint64_t ttMask = 0;
  std::atomic<bool>* stop = nullptr;
  std::chrono::steady_clock::time_point t0;
  double timeLimit = 1.0;
  int noise = 0;
  uint64_t nodes = 0;
  bool aborted = false;
  std::vector<uint64_t> path;  // game history + search path hashes
  Move killers[128][2];
  int history[16][64];
  Move rootBest;
  int rootScore = 0;

  Impl() {
    tt.resize(1 << 20);
    ttMask = tt.size() - 1;
  }

  bool timeUp() {
    if ((nodes & 2047) == 0) {
      double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      if (el > timeLimit || (stop && stop->load())) aborted = true;
    }
    return aborted;
  }

  int evalNoisy(const Board& b) {
    int e = evaluate(b);
    if (noise) {
      uint64_t h = b.hash * 0x9E3779B97F4A7C15ull;
      e += int((h >> 40) % (uint64_t)(2 * noise + 1)) - noise;
    }
    return e;
  }

  bool isRepetition(const Board& b) {
    int n = (int)path.size();
    int lim = std::min(n, b.halfmove + 1);
    for (int i = 2; i <= lim; i += 2) {
      if (path[n - i] == b.hash) return true;
    }
    return false;
  }

  int scoreMove(const Move& m, const Move& ttMove, int ply) {
    if (m == ttMove) return 2000000;
    if (m.flags & MF_PROMO) return 1500000 + VAL[m.promo];
    if (m.flags & MF_CAPTURE) return 1000000 + VAL[pieceType(m.captured)] * 10 - VAL[pieceType(m.piece)] / 10;
    if (ply < 128) {
      if (m == killers[ply][0]) return 900000;
      if (m == killers[ply][1]) return 800000;
    }
    return history[m.piece][m.to];
  }

  int qsearch(const Board& b, int alpha, int beta, int ply) {
    nodes++;
    if (timeUp()) return 0;
    int stand = evalNoisy(b);
    if (stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    if (ply > 60) return stand;
    MoveList ml;
    b.genPseudo(ml, true);
    int scores[256];
    for (int i = 0; i < ml.n; ++i) scores[i] = scoreMove(ml.m[i], Move(), 127);
    for (int i = 0; i < ml.n; ++i) {
      int bi = i;
      for (int j = i + 1; j < ml.n; ++j)
        if (scores[j] > scores[bi]) bi = j;
      std::swap(ml.m[i], ml.m[bi]);
      std::swap(scores[i], scores[bi]);
      const Move& m = ml.m[i];
      // delta pruning
      if (!(m.flags & MF_PROMO) && stand + VAL[pieceType(m.captured)] + 200 < alpha) continue;
      Board c = b;
      if (!c.make(m)) continue;
      int sc = -qsearch(c, -beta, -alpha, ply + 1);
      if (aborted) return 0;
      if (sc >= beta) return sc;
      if (sc > alpha) alpha = sc;
    }
    return alpha;
  }

  int search(const Board& b, int depth, int alpha, int beta, int ply, bool allowNull) {
    if (timeUp()) return 0;
    if (ply > 0) {
      if (b.halfmove >= 100 || isRepetition(b)) return 0;
      // mate distance pruning
      alpha = std::max(alpha, -MATE + ply);
      beta = std::min(beta, MATE - ply - 1);
      if (alpha >= beta) return alpha;
    }
    bool inCheck = b.inCheck();
    if (inCheck) depth++;
    if (depth <= 0) return qsearch(b, alpha, beta, ply);
    nodes++;

    TTEntry& e = tt[b.hash & ttMask];
    Move ttMove;
    if (e.key == b.hash && e.flag) {
      ttMove.from = e.from;
      ttMove.to = e.to;
      ttMove.promo = e.promo;
      if (ply > 0 && e.depth >= depth) {
        int s = e.score;
        if (s > MATE - 200) s -= ply;
        if (s < -MATE + 200) s += ply;
        if (e.flag == 1) return s;
        if (e.flag == 2 && s >= beta) return s;
        if (e.flag == 3 && s <= alpha) return s;
      }
    }

    bool pvNode = beta - alpha > 1;
    if (!pvNode && !inCheck && depth <= 3 && ply > 0) {
      int st = evalNoisy(b);
      if (st - 120 * depth >= beta) return st;  // reverse futility
    }
    if (allowNull && !inCheck && !pvNode && depth >= 3 && ply > 0 && b.nonPawnMaterial(b.side) > 0) {
      if (evalNoisy(b) >= beta) {
        Board c = b;
        c.makeNull();
        path.push_back(c.hash);
        int R = depth >= 6 ? 3 : 2;
        int sc = -search(c, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
        path.pop_back();
        if (aborted) return 0;
        if (sc >= beta && sc < MATE - 200) return beta;
      }
    }

    MoveList ml;
    b.genPseudo(ml);
    int scores[256];
    for (int i = 0; i < ml.n; ++i) scores[i] = scoreMove(ml.m[i], ttMove, ply);

    int legal = 0;
    int bestScore = -INF;
    Move best;
    int origAlpha = alpha;
    for (int i = 0; i < ml.n; ++i) {
      int bi = i;
      for (int j = i + 1; j < ml.n; ++j)
        if (scores[j] > scores[bi]) bi = j;
      std::swap(ml.m[i], ml.m[bi]);
      std::swap(scores[i], scores[bi]);
      const Move& m = ml.m[i];
      Board c = b;
      if (!c.make(m)) continue;
      legal++;
      path.push_back(c.hash);
      bool quiet = !(m.flags & (MF_CAPTURE | MF_PROMO));
      int sc;
      if (legal == 1) {
        sc = -search(c, depth - 1, -beta, -alpha, ply + 1, true);
      } else {
        int red = 0;
        if (depth >= 3 && legal > 3 && quiet && !inCheck && !c.inCheck()) {
          red = 1 + (legal > 10 ? 1 : 0);
          if (depth - 1 - red < 1) red = depth - 2;
        }
        sc = -search(c, depth - 1 - red, -alpha - 1, -alpha, ply + 1, true);
        if (sc > alpha && red) sc = -search(c, depth - 1, -alpha - 1, -alpha, ply + 1, true);
        if (sc > alpha && sc < beta) sc = -search(c, depth - 1, -beta, -alpha, ply + 1, true);
      }
      path.pop_back();
      if (aborted) return 0;
      if (sc > bestScore) {
        bestScore = sc;
        best = m;
        if (ply == 0) {
          rootBest = m;
          rootScore = sc;
        }
      }
      if (sc > alpha) alpha = sc;
      if (alpha >= beta) {
        if (quiet && ply < 128) {
          if (!(killers[ply][0] == m)) {
            killers[ply][1] = killers[ply][0];
            killers[ply][0] = m;
          }
          history[m.piece][m.to] += depth * depth;
          if (history[m.piece][m.to] > 700000) {
            for (auto& row : history)
              for (auto& v : row) v /= 2;
          }
        }
        break;
      }
    }
    if (legal == 0) return inCheck ? -MATE + ply : 0;

    int store = bestScore;
    if (store > MATE - 200) store += ply;
    if (store < -MATE + 200) store -= ply;
    if (e.key != b.hash || depth >= e.depth || bestScore >= beta) {
      e.key = b.hash;
      e.score = (int16_t)store;
      e.depth = (int8_t)std::min(depth, 120);
      e.flag = bestScore >= beta ? 2 : (bestScore > origAlpha ? 1 : 3);
      e.from = best.from;
      e.to = best.to;
      e.promo = best.promo;
    }
    return bestScore;
  }

  Move run(const Board& root, const std::vector<uint64_t>& hist, int level, int& outDepth, int& outScore) {
    const AILevel& L = kAILevels[std::max(0, std::min(4, level))];
    t0 = std::chrono::steady_clock::now();
    timeLimit = L.timeLimit;
    noise = L.evalNoise;
    nodes = 0;
    aborted = false;
    path = hist;
    if (path.empty() || path.back() != root.hash) path.push_back(root.hash);
    std::memset(killers, 0, sizeof(killers));
    std::memset(history, 0, sizeof(history));
    MoveList legal;
    root.genLegal(legal);
    if (legal.n == 0) return Move();
    Move best = legal.m[0];
    outDepth = 0;
    outScore = 0;
    if (legal.n == 1) return best;
    for (int d = 1; d <= L.maxDepth; ++d) {
      rootBest = Move();
      int sc = search(root, d, -INF, INF, 0, false);
      if (aborted) {
        // partial iteration: accept the best move found so far if it beat previous
        if (!rootBest.isNull() && d > 1) {
          for (const Move& m : legal)
            if (m == rootBest && rootScore > outScore - 50) best = m;
        }
        break;
      }
      for (const Move& m : legal)
        if (m == rootBest) best = m;
      outDepth = d;
      outScore = sc;
      if (sc > MATE - 100 || sc < -MATE + 100) break;
      double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      if (el > timeLimit * 0.55) break;
    }
    return best;
  }
};

AIEngine::AIEngine() : impl_(new Impl) {}
AIEngine::~AIEngine() {
  cancel();
  delete impl_;
}

void AIEngine::cancel() {
  stopFlag_ = true;
  if (thread_.joinable()) thread_.join();
  stopFlag_ = false;
  running_ = false;
}

void AIEngine::start(const Board& b, const std::vector<uint64_t>& history, int level) {
  cancel();
  done_ = false;
  running_ = true;
  impl_->stop = &stopFlag_;
  thread_ = std::thread([this, b, history, level]() {
    int d = 0, s = 0;
    Move m = impl_->run(b, history, level, d, s);
    best_ = m;
    depth_ = d;
    score_ = s;
    nodes_ = impl_->nodes;
    done_ = true;
    running_ = false;
  });
}

Move AIEngine::searchSync(const Board& b, const std::vector<uint64_t>& history, int level) {
  cancel();
  impl_->stop = &stopFlag_;
  int d = 0, s = 0;
  Move m = impl_->run(b, history, level, d, s);
  depth_ = d;
  score_ = s;
  nodes_ = impl_->nodes;
  return m;
}

}  // namespace chess
