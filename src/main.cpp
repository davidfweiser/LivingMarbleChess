// Living Marble - 3D Battle Chess
// A photoreal chess game where carved marble pieces come alive and fight.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include "app.h"
#include "audio.h"

static void usage() {
  std::printf(
      "Living Marble - 3D Battle Chess\n\n"
      "  --fullscreen            run fullscreen\n"
      "  --size WxH              window size (default 1600x900)\n"
      "  --quality 0|1|2         sculpting/texture detail (default 1)\n"
      "  --graphics 0..3         Low / Medium / High / Ultra (default High)\n"
      "  --demo                  watch the AI play itself\n"
      "  --fen \"<fen>\"           start from a position\n"
      "  --pieces marble|medieval|alien  which army to play with (default marble)\n"
      "  --mute                  no sound\n"
      "  --selftest              verify the move generator (perft)\n"
      "\n capture tools:\n"
      "  --shot file.png [--frames N] [--scenario 0|1|2]\n"
      "  --battle K --shots t1,t2,... [--prefix name]   (K: 0 pawn .. 5 king)\n"
      "  --fen \"<fen>\" --move e2e4 --shots t1,t2,...   (screenshots of any move)\n"
      "  --style S               force battle style S (0..2) instead of a random one\n"
      "  --record dir [--frames N]\n"
      "  --camera yaw,pitch,dist\n"
      "  --no-ui\n");
}

int main(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--help" || a == "-h") {
      usage();
      return 0;
    } else if (a == "--fullscreen") o.fullscreen = true;
    else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
    else if (a == "--quality") o.quality = std::atoi(next().c_str());
    else if (a == "--graphics") o.gfxPreset = std::atoi(next().c_str());
    else if (a == "--demo") o.demo = true;
    else if (a == "--fen") o.fen = next();
    else if (a == "--mute") o.noAudio = true;
    else if (a == "--pieces") {
      std::string v = next();
      o.pieces = (v == "medieval" || v == "1") ? 1 : ((v == "alien" || v == "aliens" || v == "2") ? 2 : 0);
    }
    else if (a == "--selftest") o.selftest = true;
    else if (a == "--shot") o.shotPath = next();
    else if (a == "--frames") o.frames = std::atoi(next().c_str());
    else if (a == "--scenario") o.scenario = std::atoi(next().c_str());
    else if (a == "--battle") o.battle = std::atoi(next().c_str());
    else if (a == "--style") o.style = std::atoi(next().c_str());
    else if (a == "--prefix") o.shotPrefix = next();
    else if (a == "--record") o.recordDir = next();
    else if (a == "--no-ui") o.noUI = true;
    else if (a == "--no-render") o.noRender = true;
    else if (a == "--level") o.level = std::atoi(next().c_str());
    else if (a == "--move") o.forceMove = next();
    else if (a == "--select") o.selectSq = next();
    else if (a == "--loading-shot") o.loadingShot = next();
    else if (a == "--dump-sfx") return Audio::dumpWav(next().c_str()) ? 0 : 1;
    else if (a == "--camera") {
      o.hasCam = std::sscanf(next().c_str(), "%f,%f,%f,%f,%f,%f", &o.camYaw, &o.camPitch, &o.camDist, &o.camTX,
                             &o.camTY, &o.camTZ) >= 3;
    } else if (a == "--shots") {
      std::stringstream ss(next());
      std::string tok;
      while (std::getline(ss, tok, ',')) o.shotTimes.push_back((float)std::atof(tok.c_str()));
    } else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      usage();
      return 1;
    }
  }
  App app(o);
  return app.run();
}
