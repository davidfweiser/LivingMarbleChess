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
const char* battleStyleName(int kind, int style, int set = 0);  // set: 0 Living Marble, 1 medieval, 2 aliens, 3 Romans, 4 dinosaurs

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

// the Romans (battle_roman.cpp)
const char* romanBattleStyleName(int kind, int style);
std::unique_ptr<Anim> makeRomanBattle(World& w, int kind, int style, const MovePlan& plan);
std::unique_ptr<Anim> makeRomanQuiet(const MovePlan& plan);
std::unique_ptr<Anim> makeRomanQuickCapture(const MovePlan& plan);
std::unique_ptr<Anim> makeRomanCheckmate(int kingId, int winnerColor);

// the dinosaurs (battle_dino.cpp)
const char* dinoBattleStyleName(int kind, int style);
std::unique_ptr<Anim> makeDinoBattle(World& w, int kind, int style, const MovePlan& plan);
std::unique_ptr<Anim> makeDinoQuiet(const MovePlan& plan);
std::unique_ptr<Anim> makeDinoQuickCapture(const MovePlan& plan);
std::unique_ptr<Anim> makeDinoCheckmate(int kingId, int winnerColor);
