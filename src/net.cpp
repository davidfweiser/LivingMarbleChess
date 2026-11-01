#include "net.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// window.lmNet is the page's: it holds the WebSocket, reconnects and turns the relay's
// JSON into the plain lines above.
EM_JS(int, lm_net_present, (), { return (typeof window !== 'undefined' && window.lmNet) ? 1 : 0; });
EM_JS(void, lm_net_command, (const char* cmd), { if (window.lmNet) window.lmNet.cmd(UTF8ToString(cmd)); });
// The next message into buf: its length, -1 if there is none, or (larger than cap) the
// buffer it needs.
EM_JS(int, lm_net_next, (char* buf, int cap), {
  var q = window.lmNet ? window.lmNet.inbox : null;
  if (!q || !q.length) return -1;
  var s = q[0], n = lengthBytesUTF8(s);
  if (n + 1 > cap) return n + 1;
  stringToUTF8(s, buf, cap);
  q.shift();
  return n;
});

namespace net {
bool available() { return lm_net_present() != 0; }
void command(const std::string& cmd) { lm_net_command(cmd.c_str()); }
bool next(std::string& msg) {
  msg.resize(256);
  for (;;) {
    int n = lm_net_next(&msg[0], (int)msg.size());
    if (n < 0) return false;
    if (n < (int)msg.size()) {
      msg.resize((size_t)n);
      return true;
    }
    msg.resize((size_t)n);  // too long for the buffer: again, with room for it
  }
}
}  // namespace net

#else

namespace net {
bool available() { return false; }
void command(const std::string&) {}
bool next(std::string&) { return false; }
}  // namespace net

#endif
