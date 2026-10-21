// Chess rules: board representation, legal move generation, SAN, FEN.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace chess {

enum Color : int { WHITE = 0, BLACK = 1 };
enum PieceType : int { NONE = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6 };

// A piece is stored as (type | color << 3); 0 means empty.
inline int makePiece(int type, int color) { return type | (color << 3); }
inline int pieceType(int p) { return p & 7; }
inline int pieceColor(int p) { return p >> 3; }

inline int sqFile(int sq) { return sq & 7; }
inline int sqRank(int sq) { return sq >> 3; }
inline int makeSq(int file, int rank) { return rank * 8 + file; }
std::string sqName(int sq);

enum CastleRights : int { WK = 1, WQ = 2, BK = 4, BQ = 8 };

enum MoveFlag : uint8_t {
  MF_NONE = 0,
  MF_CAPTURE = 1,
  MF_EP = 2,
  MF_CASTLE = 4,
  MF_DOUBLE = 8,
  MF_PROMO = 16,
};

struct Move {
  uint8_t from = 0, to = 0, promo = 0, flags = 0;
  uint8_t piece = 0, captured = 0;  // filled at generation
  bool operator==(const Move& o) const { return from == o.from && to == o.to && promo == o.promo; }
  bool operator!=(const Move& o) const { return !(*this == o); }
  bool isNull() const { return from == 0 && to == 0; }
  bool isCapture() const { return flags & MF_CAPTURE; }
  // Square of the piece that gets captured (differs from 'to' for en passant).
  int captureSquare() const { return (flags & MF_EP) ? (to ^ 8) : to; }
  std::string uci() const;
};

struct MoveList {
  Move m[256];
  int n = 0;
  void push(const Move& mv) { m[n++] = mv; }
  Move* begin() { return m; }
  Move* end() { return m + n; }
  const Move* begin() const { return m; }
  const Move* end() const { return m + n; }
};

struct Board {
  uint8_t sq[64] = {};
  int side = WHITE;
  int castling = 0;
  int ep = -1;
  int halfmove = 0;
  int fullmove = 1;
  int kingSq[2] = {4, 60};
  uint64_t hash = 0;

  void setStart();
  bool setFEN(const std::string& fen);
  std::string fen() const;

  bool attacked(int square, int byColor) const;
  bool inCheck() const { return attacked(kingSq[side], side ^ 1); }
  bool inCheck(int color) const { return attacked(kingSq[color], color ^ 1); }

  void genPseudo(MoveList& out, bool capturesOnly = false) const;
  void genLegal(MoveList& out) const;
  // Applies a pseudo-legal move; returns false if it leaves own king in check
  // (board is then in an undefined state - use on a copy).
  bool make(const Move& m);
  void makeNull();

  uint64_t computeHash() const;
  bool insufficientMaterial() const;
  int nonPawnMaterial(int color) const;
};

// SAN for a legal move in the given position (includes +/#).
std::string toSAN(const Board& b, const Move& m);
// Find the legal move matching from/to/promo (promo 0 = any -> queen).
bool findLegal(const Board& b, int from, int to, int promo, Move& out);

uint64_t perft(const Board& b, int depth);

enum class Result { Ongoing, WhiteMates, BlackMates, Stalemate, FiftyMove, Repetition, Material };
const char* resultText(Result r);

// Full game record with history for undo/repetition.
class Game {
 public:
  Game() { reset(); }
  void reset();
  bool setFEN(const std::string& fen);
  const Board& board() const { return cur_; }
  bool play(const Move& m);  // m must be legal
  bool undo();
  Result result() const { return result_; }
  const std::vector<std::string>& san() const { return san_; }
  const std::vector<Move>& moves() const { return moves_; }
  const std::vector<uint64_t>& hashHistory() const { return hashes_; }
  int plyCount() const { return (int)moves_.size(); }

 private:
  void updateResult();
  Board cur_;
  std::vector<Board> boards_;
  std::vector<Move> moves_;
  std::vector<std::string> san_;
  std::vector<uint64_t> hashes_;
  Result result_ = Result::Ongoing;
};

}  // namespace chess
