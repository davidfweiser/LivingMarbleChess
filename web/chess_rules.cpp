// Living Marble - the game's chess rules (src/chess.cpp) for the server (web/relay.py),
// which loads them as a shared library, so the server and the browsers agree exactly on
// what is legal and how a game ends.
//
//   g++ -O2 -std=c++17 -shared -fPIC -Isrc -o liblmrules.so src/chess.cpp web/chess_rules.cpp
#include <cstring>
#include <string>

#include "chess.h"

extern "C" {

// Plays a game from the start: its moves in long algebraic notation, separated by spaces
// ("e2e4 e7e5 g1f3"). Returns -(k + 1) if move k (from 0) isn't legal; otherwise how the
// game stands: 0 going on, 1 White has mated, 2 Black has mated, 3 stalemate, 4 fifty-move
// rule, 5 threefold repetition, 6 insufficient material.
int lm_rules_play(const char* moves) {
  chess::Game game;
  const char* p = moves ? moves : "";
  int k = 0;
  while (*p) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char* e = std::strchr(p, ' ');
    std::string u = e ? std::string(p, e - p) : std::string(p);
    chess::Move m;
    if (game.result() != chess::Result::Ongoing || !chess::findLegalUci(game.board(), u, m)) return -(k + 1);
    game.play(m);
    ++k;
    p = e ? e : p + u.size();
  }
  return (int)game.result();
}

// A game kept by the server, one move at a time.
void* lm_game_new() { return new chess::Game(); }
void lm_game_free(void* g) { delete static_cast<chess::Game*>(g); }
// Plays a move: -1 if it isn't legal (the game is unchanged), otherwise how the game stands (as above).
int lm_game_play(void* g, const char* uci) {
  chess::Game& game = *static_cast<chess::Game*>(g);
  chess::Move m;
  if (!uci || game.result() != chess::Result::Ongoing || !chess::findLegalUci(game.board(), uci, m)) return -1;
  game.play(m);
  return (int)game.result();
}

}  // extern "C"
