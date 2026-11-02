// The web page's connection to the server for online games (web/relay.py), through the
// page's JavaScript (window.lmNet in web/index.html). The desktop versions have none.
#pragma once
#include <string>

namespace net {

// Whether online games are possible (the web page).
bool available();

// To the server:  "create" (a game with a friend: this player is White), "join CODE", "top" (the
// leaderboard), "move PLY UCI", "resign", "draw" (offer, or accept the opponent's), "decline",
// "new" (games with a friend), "sync" (send the game as it stands), "copy" (the invite link to
// the clipboard), "hello" (connect), "close" (leave the game).
void command(const std::string& cmd);

// From the server, one at a time:
//   "status connecting|open|lost|closed"
//   "room CODE SEAT FULL PEER RATED [UCI ...]"   SEAT w or b; FULL, PEER, RATED 0 or 1
//   "players WNAME WRATING BNAME BRATING"        "clock WMS BMS RUN" (RUN w, b or -)
//   "unrated TEXT" (why this rated game's ratings won't change: after "room")
//   "move PLY UCI"  "peer 0|1"  "away SECONDS" (the opponent loses unless they come back)
//   "draw" (offered)  "declined"  "end RESULT WHY [WBEFORE WAFTER BBEFORE BAFTER]"  "new"
//   "me NAME RATING GAMES" or "me -" (the front page logs in)
//   "top MINGAMES MYRANK MYRATING MYGAMES [NAME RATING GAMES ...]"
//   "link URL"  "notice TEXT"  "error TEXT"  "copied 0|1"
// (Rated games are found in the page's lobby, which calls lm_online_match when one starts.)
bool next(std::string& msg);

}  // namespace net
