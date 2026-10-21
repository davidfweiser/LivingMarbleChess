// Choreography: quiet moves, castling, promotion and the battles. Every
// attacking piece type has several battle styles; each capture picks one.
#pragma once
#include <memory>

#include "animation.h"
#include "chess.h"

struct MovePlan {
  chess::Move move;
  int mover = -1;      // actor id
  int victim = -1;     // actor id (captures)
  int rook = -1;       // actor id (castling)
  int rookTo = -1;     // square
  int promoKind = -1;  // meshgen kind for promotion
  int promoSquare = -1;
  bool fast = false;   // skip battle theatrics
  int style = -1;      // battle style for captures (-1 = pick one at random)
};

constexpr int kBattleStyles = 3;  // per piece type
const char* battleStyleName(int kind, int style, int set = 0);  // set: 0 Living Marble, 1 medieval, 2 aliens

std::unique_ptr<Anim> makeMoveAnim(World& w, const MovePlan& plan);
std::unique_ptr<Anim> makeCheckmateAnim(World& w, int kingId, int winnerColor);
std::unique_ptr<Anim> makeStalemateAnim(World& w);
// staged demonstration of one attacker type (used by --battle for screenshots)
std::unique_ptr<Anim> makeDemoBattle(World& w, int attackerId, int victimId);

// the medieval army (battle_medieval.cpp)
const char* medievalBattleStyleName(int kind, int style);
std::unique_ptr<Anim> makeMedievalBattle(int kind, int style, const MovePlan& plan);
std::unique_ptr<Anim> makeMedievalQuiet(const MovePlan& plan);
std::unique_ptr<Anim> makeMedievalQuickCapture(const MovePlan& plan);
std::unique_ptr<Anim> makeMedievalCheckmate(int kingId, int winnerColor);

// the aliens (battle_alien.cpp)
const char* alienBattleStyleName(int kind, int style);
std::unique_ptr<Anim> makeAlienBattle(int kind, int style, const MovePlan& plan);
std::unique_ptr<Anim> makeAlienQuiet(const MovePlan& plan);
std::unique_ptr<Anim> makeAlienQuickCapture(const MovePlan& plan);
std::unique_ptr<Anim> makeAlienCheckmate(int kingId, int winnerColor);
