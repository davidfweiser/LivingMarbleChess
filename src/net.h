// The web page's connection to the relay for online games (web/relay.py), through the
// page's JavaScript (window.lmNet in web/index.html). The desktop versions have none.
#pragma once
#include <string>

namespace net {

// Whether online games are possible (the web page).
bool available();

// To the relay:  "create" (a new game: this player is White), "join CODE", "move PLY UCI",
// "new", "sync" (send the game as it stands), "copy" (the invite link to the clipboard), "close".
void command(const std::string& cmd);

// From the relay, one at a time:  "status connecting|open|lost|closed",
// "room CODE SEAT FULL PEER [UCI ...]" (SEAT w or b; FULL, PEER 0 or 1), "link URL",
// "move PLY UCI", "peer 0|1", "new", "error TEXT", "copied 0|1".
bool next(std::string& msg);

}  // namespace net
