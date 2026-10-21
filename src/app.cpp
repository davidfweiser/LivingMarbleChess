#include "app.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <iterator>

#include "gl.h"
#include "image_io.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

using glm::vec3;

namespace {
void* getProc(const char* n) { return reinterpret_cast<void*>(glfwGetProcAddress(n)); }
void glfwError(int code, const char* desc) { std::fprintf(stderr, "[glfw] error %d: %s\n", code, desc); }
void scrollCb(GLFWwindow* w, double, double dy) {
  App* app = static_cast<App*>(glfwGetWindowUserPointer(w));
  if (app && !ImGui::GetIO().WantCaptureMouse) {
    app->world.cam.orbit(0, 0, (float)(-dy * 0.08));
  }
}

RenderSettings presetSettings(int p) {
  RenderSettings s;
  switch (p) {
    case 0:
      s.shadowRes = 1024;
      s.msaa = 0;
      s.ssao = false;
      s.reflections = false;
      s.dof = false;
      s.bloom = true;
      break;
    case 1:
      s.shadowRes = 2048;
      s.msaa = 2;
      s.ssao = true;
      s.reflections = true;
      s.dof = false;
      break;
    case 2:
      s.shadowRes = 4096;
      s.msaa = 4;
      break;
    default:
      s.shadowRes = 4096;
      s.msaa = 8;
      break;
  }
  return s;
}
}  // namespace

App::App(const Options& o) : opt(o) {
  gfxPreset = o.gfxPreset;
  if (o.demo) mode = Mode::Watch;
  if (o.level >= 0) aiLevel = std::min(4, o.level);
  fixedStep = !o.shotPath.empty() || !o.recordDir.empty() || o.battle >= 0;
}

App::~App() {
  if (loader.joinable()) loader.join();
  ai.cancel();
  audio.shutdown();
  if (ImGui::GetCurrentContext()) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
  if (win) glfwDestroyWindow(win);
  glfwTerminate();
}

bool App::initWindow() {
  glfwSetErrorCallback(glfwError);
  if (!glfwInit()) return false;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_SAMPLES, 0);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif
  GLFWmonitor* mon = nullptr;
  int w = opt.width, h = opt.height;
  if (opt.fullscreen) {
    mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    w = vm->width;
    h = vm->height;
  }
  win = glfwCreateWindow(w, h, "Living Marble - 3D Battle Chess", mon, nullptr);
  if (!win) return false;
  glfwMakeContextCurrent(win);
  glfwSwapInterval(fixedStep ? 0 : 1);
  const char* missing = nullptr;
  if (!lmLoadGL(getProc, &missing)) {
    std::fprintf(stderr, "OpenGL 3.3 function missing: %s\n", missing ? missing : "?");
    return false;
  }
  glfwSetWindowUserPointer(win, this);
  glfwSetScrollCallback(win, scrollCb);
  return true;
}

void App::initUI() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  int ww, wh, fw, fh;
  glfwGetWindowSize(win, &ww, &wh);
  glfwGetFramebufferSize(win, &fw, &fh);
  float xs = 1, ys = 1;
  glfwGetWindowContentScale(win, &xs, &ys);
  bool pointsCoords = fw > ww * 1.2f;  // macOS style (window in points)
  float px = pointsCoords ? xs : 1.0f;   // oversample factor for crisp text
  float uiScale = pointsCoords ? 1.0f : std::max(1.0f, xs);
  const char* sans[] = {"/System/Library/Fonts/Supplemental/Arial.ttf",
                        "/Library/Fonts/Arial.ttf",
                        "C:/Windows/Fonts/segoeui.ttf",
                        "/usr/share/fonts/opentype/inter/Inter-Regular.otf",
                        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                        "/usr/share/fonts/TTF/DejaVuSans.ttf",
                        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};
  const char* serif[] = {"/System/Library/Fonts/Supplemental/Georgia.ttf",
                         "/Library/Fonts/Georgia.ttf",
                         "C:/Windows/Fonts/georgia.ttf",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
                         "/usr/share/fonts/TTF/DejaVuSerif.ttf",
                         "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf"};
  auto tryLoad = [&](const char* const* list, size_t n, float size) -> ImFont* {
    for (size_t i = 0; i < n; ++i) {
      FILE* f = std::fopen(list[i], "rb");
      if (!f) continue;
      std::fclose(f);
      ImFontConfig cfg;
      cfg.OversampleH = 2;
      ImFont* font = io.Fonts->AddFontFromFileTTF(list[i], size * px * uiScale, &cfg);
      if (font) return font;
    }
    return nullptr;
  };
  fontUI = tryLoad(sans, sizeof(sans) / sizeof(sans[0]), 16.0f);
  if (!fontUI) {
    ImFontConfig cfg;
    cfg.SizePixels = 15.0f * px * uiScale;
    fontUI = io.Fonts->AddFontDefault(&cfg);
  }
  fontTitle = tryLoad(serif, sizeof(serif) / sizeof(serif[0]), 30.0f);
  if (!fontTitle) fontTitle = fontUI;
  fontBanner = tryLoad(serif, sizeof(serif) / sizeof(serif[0]), 64.0f);
  if (!fontBanner) fontBanner = fontTitle;
  io.FontGlobalScale = 1.0f / px;
  io.FontDefault = fontUI;

  ImGuiStyle& st = ImGui::GetStyle();
  ImGui::StyleColorsDark();
  st.WindowRounding = 10.0f;
  st.FrameRounding = 6.0f;
  st.GrabRounding = 6.0f;
  st.PopupRounding = 8.0f;
  st.WindowBorderSize = 0.0f;
  st.FramePadding = ImVec2(10, 6);
  st.ItemSpacing = ImVec2(8, 7);
  st.WindowPadding = ImVec2(16, 14);
  ImVec4* c = st.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.045f, 0.04f, 0.78f);
  c[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.06f, 0.05f, 0.95f);
  c[ImGuiCol_Text] = ImVec4(0.93f, 0.90f, 0.84f, 1.0f);
  c[ImGuiCol_TextDisabled] = ImVec4(0.6f, 0.56f, 0.5f, 1.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.14f, 0.11f, 0.85f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.21f, 0.15f, 0.9f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.34f, 0.26f, 0.18f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.34f, 0.25f, 0.15f, 0.85f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.40f, 0.22f, 0.95f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.52f, 0.28f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.34f, 0.25f, 0.15f, 0.7f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.5f, 0.37f, 0.2f, 0.85f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.62f, 0.46f, 0.25f, 1.0f);
  c[ImGuiCol_CheckMark] = ImVec4(0.98f, 0.80f, 0.45f, 1.0f);
  c[ImGuiCol_SliderGrab] = ImVec4(0.85f, 0.65f, 0.35f, 1.0f);
  c[ImGuiCol_SliderGrabActive] = ImVec4(0.98f, 0.80f, 0.45f, 1.0f);
  c[ImGuiCol_Separator] = ImVec4(0.45f, 0.35f, 0.22f, 0.5f);
  c[ImGuiCol_TitleBg] = ImVec4(0.1f, 0.08f, 0.06f, 1.0f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.12f, 0.08f, 1.0f);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.4f, 0.3f, 0.2f, 0.8f);
  if (uiScale > 1.0f) st.ScaleAllSizes(uiScale);

  ImGui_ImplGlfw_InitForOpenGL(win, true);
#ifdef __APPLE__
  ImGui_ImplOpenGL3_Init("#version 330 core");
#else
  ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
}

void App::loadAssets() {
  loader = std::thread([this]() {
    meshgen::buildLibrary(lib, opt.quality, &progress);
    progress.stage = "Inlaying the board";
    int res = opt.quality == 0 ? 1024 : (opt.quality == 1 ? 2048 : 4096);
    tex::bakeBoard(res, lib.boardHalf, boardAlbedo, boardDetail);
    assetsReady = true;
  });
}

int App::run() {
  if (opt.selftest) {
    chess::Board b;
    b.setStart();
    uint64_t n = chess::perft(b, 5);
    std::printf("perft(5) = %llu (%s)\n", (unsigned long long)n, n == 4865609ull ? "OK" : "FAIL");
    return n == 4865609ull ? 0 : 1;
  }
  if (!initWindow()) {
    std::fprintf(stderr, "Could not create an OpenGL 3.3 window.\n");
    return 1;
  }
  initUI();
  renderer.init(presetSettings(gfxPreset));
  std::printf("[gl] %s\n", renderer.glInfo().c_str());
  if (!opt.noAudio && audio.init()) {
    world.audio = &audio;
    audio.setVolume(volume);
  } else {
    std::printf("[audio] no audio device - running silent\n");
  }
  world.lib = &lib;
  world.reset();
  loadAssets();
  double prev = glfwGetTime();
  while (!glfwWindowShouldClose(win) && !quitRequested) {
    glfwPollEvents();
    double now = glfwGetTime();
    float dt = (float)(now - prev);
    prev = now;
    if (dt > 0) fps = fps * 0.95f + 0.05f / std::max(dt, 1e-4f);
    dt = std::min(dt, 0.05f);
    if (fixedStep && !loading) dt = 1.0f / 30.0f;
    frame(dt);
    glfwSwapBuffers(win);
  }
  if (fixedStep) {
    std::printf("moves:");
    for (size_t i = 0; i < game.san().size(); ++i) std::printf(" %s", game.san()[i].c_str());
    std::printf("\nresult: %s (%d plies, %d actors, %zu chunks)\n", chess::resultText(game.result()), game.plyCount(),
                (int)world.actors.size(), world.chunks.size());
  }
  return 0;
}

// ---------------------------------------------------------------------------
void App::newGame() {
  anims.clear();
  ai.cancel();
  aiPending = false;
  promoPending = false;
  gameOverShown = false;
  checkAnnounced = false;
  announcePending = false;
  hasLastMove = false;
  deselect();
  if (!opt.fen.empty()) game.setFEN(opt.fen);
  else game.reset();
  world.reset();
  world.cam.release(0.6f);
  world.spawnFromBoard(game.board().sq, true);
  world.sound(SFX_CHIME, 0.6f);
  world.cam.setSide(mode == Mode::HumanBlack ? 1 : 0);
  bannerText = "";
}

void App::undo() {
  if (animating() || promoPending) return;
  if (game.plyCount() == 0) return;
  ai.cancel();
  aiPending = false;
  int n = (mode == Mode::TwoPlayer || mode == Mode::Watch) ? 1 : 2;
  if (game.result() != chess::Result::Ongoing && !isHumanTurn()) n = 1;
  for (int i = 0; i < n && game.plyCount() > 0; ++i) game.undo();
  // make sure the human is to move again
  if (!isHumanTurn() && game.plyCount() > 0 && mode != Mode::Watch) game.undo();
  gameOverShown = false;
  deselect();
  hasLastMove = game.plyCount() > 0;
  if (hasLastMove) lastMove = game.moves().back();
  world.chunks.clear();
  world.props.clear();
  world.cam.release(0.6f);
  world.spawnFromBoard(game.board().sq, true);
  world.sound(SFX_CHIME, 0.5f, vec3(0), 0.8f);
}

void App::flipCamera() { world.cam.yaw = wrapAngle(world.cam.yaw + kPI); }

void App::resetView() {
  world.cam.setSide(mode == Mode::HumanBlack ? 1 : mode == Mode::TwoPlayer ? game.board().side : 0);
  world.cam.pitch = 0.80f;
  world.cam.dist = 12.6f;
}

void App::setMode(Mode m) {
  mode = m;
  ai.cancel();
  aiPending = false;
  world.cam.autoSpin = 0;
  if (m == Mode::HumanWhite) world.cam.setSide(0);
  if (m == Mode::HumanBlack) world.cam.setSide(1);
  deselect();
}

void App::applyGraphicsPreset(int p) {
  gfxPreset = p;
  renderer.applySettings(presetSettings(p));
}

bool App::isHumanTurn() const {
  int side = game.board().side;
  switch (mode) {
    case Mode::HumanWhite: return side == chess::WHITE;
    case Mode::HumanBlack: return side == chess::BLACK;
    case Mode::TwoPlayer: return true;
    default: return false;
  }
}

const char* App::statusText() const {
  static std::string s;
  chess::Result r = game.result();
  if (r != chess::Result::Ongoing) return chess::resultText(r);
  bool white = game.board().side == chess::WHITE;
  if (!isHumanTurn() && aiPending) s = std::string(white ? "White" : "Black") + " is thinking...";
  else if (isHumanTurn() && mode != Mode::TwoPlayer) s = "Your move";
  else s = std::string(white ? "White" : "Black") + " to move";
  if (game.board().inCheck()) s += "  -  Check!";
  return s.c_str();
}

void App::deselect() {
  selected = -1;
  selMoves.clear();
}

void App::select(int sq) {
  selected = sq;
  selMoves.clear();
  chess::MoveList ml;
  game.board().genLegal(ml);
  for (const chess::Move& m : ml)
    if (m.from == sq) selMoves.push_back(m);
  world.sound(SFX_SELECT, 0.7f, squareCenter(sq));
}

void App::startAIIfNeeded() {
  if (aiPending || game.result() != chess::Result::Ongoing || isHumanTurn()) return;
  ai.start(game.board(), game.hashHistory(), aiLevel);
  aiPending = true;
  aiDelay = mode == Mode::Watch ? 0.7f : 0.15f;
}

void App::commitMove(const chess::Move& m) {
  MovePlan plan;
  plan.move = m;
  plan.mover = world.actorAt[m.from];
  if (plan.mover < 0) {
    // visual state out of sync - rebuild
    game.play(m);
    world.spawnFromBoard(game.board().sq, false);
    return;
  }
  if (m.isCapture()) {
    int cs = m.captureSquare();
    plan.victim = world.actorAt[cs];
    world.actorAt[cs] = -1;
  }
  if (m.flags & chess::MF_CASTLE) {
    int rf = m.to > m.from ? m.from + 3 : m.from - 4;
    int rt = m.to > m.from ? m.from + 1 : m.from - 1;
    plan.rook = world.actorAt[rf];
    plan.rookTo = rt;
    world.actorAt[rf] = -1;
    world.actorAt[rt] = plan.rook;
    if (plan.rook >= 0) world.actors[plan.rook].square = rt;
  }
  world.actorAt[m.from] = -1;
  world.actorAt[m.to] = plan.mover;
  world.actors[plan.mover].square = m.to;
  if (m.flags & chess::MF_PROMO) {
    plan.promoKind = m.promo - 1;
    plan.promoSquare = m.to;
  }
  plan.fast = !battles;
  plan.style = opt.style;
  if (plan.victim < 0 && m.isCapture()) plan.fast = true;
  world.stopFidgets();
  game.play(m);
  lastMove = m;
  hasLastMove = true;
  deselect();
  anims.push_back(makeMoveAnim(world, plan));
  announcePending = true;
  startAIIfNeeded();
}

// ---------------------------------------------------------------------------
int App::pickSquare(double mx, double my, bool piecesFirst) const {
  int ww, wh;
  glfwGetWindowSize(win, &ww, &wh);
  if (ww <= 0 || wh <= 0) return -1;
  float x = (float)(2.0 * mx / ww - 1.0), y = (float)(1.0 - 2.0 * my / wh);
  glm::mat4 inv = glm::inverse(proj * view);
  glm::vec4 a = inv * glm::vec4(x, y, -1, 1), b = inv * glm::vec4(x, y, 1, 1);
  vec3 ro = vec3(a) / a.w, re = vec3(b) / b.w;
  if (ro.y <= 0.0f) return -1;  // looking at the underside of the board
  vec3 rd = glm::normalize(re - ro);
  float bestT = 1e9f;
  int best = -1;
  if (piecesFirst) {
    for (const Actor& ac : world.actors) {
      if (!ac.alive || !ac.visible || ac.square < 0) continue;
      float r = ac.radius() * 0.85f, H = ac.height();
      vec3 o = ro - ac.pos;
      float A = rd.x * rd.x + rd.z * rd.z;
      float B = 2 * (o.x * rd.x + o.z * rd.z);
      float C = o.x * o.x + o.z * o.z - r * r;
      float disc = B * B - 4 * A * C;
      if (disc < 0 || A < 1e-8f) continue;
      float t = (-B - std::sqrt(disc)) / (2 * A);
      float yy = o.y + rd.y * t;
      if (t > 0 && yy >= 0 && yy <= H) {
        if (t < bestT) {
          bestT = t;
          best = ac.square;
        }
      } else if (std::fabs(rd.y) > 1e-5f) {  // top cap
        float tc = (H - o.y) / rd.y;
        vec3 p = o + rd * tc;
        if (tc > 0 && p.x * p.x + p.z * p.z < r * r && tc < bestT) {
          bestT = tc;
          best = ac.square;
        }
      }
    }
  }
  if (std::fabs(rd.y) > 1e-5f) {
    float t = -ro.y / rd.y;
    vec3 p = ro + rd * t;
    if (t > 0 && t < bestT && std::fabs(p.x) < 4.0f && std::fabs(p.z) < 4.0f) {
      best = chess::makeSq(std::min(7, (int)std::floor(p.x + 4.0f)), std::min(7, (int)std::floor(4.0f - p.z)));
    }
  }
  return best;
}

void App::handleInput() {
  ImGuiIO& io = ImGui::GetIO();
  double mx, my;
  glfwGetCursorPos(win, &mx, &my);
  bool rdown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ||
               glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
  const float dYaw = (float)(-(mx - lastMX) * 0.006), dPitch = (float)((my - lastMY) * 0.004);
  if (rdown && (rightDrag || !io.WantCaptureMouse)) {
    if (rightDrag) world.cam.orbit(dYaw, dPitch, 0);
    rightDrag = true;
  } else {
    rightDrag = false;
  }
  // left-drag also turns the board; a left click that doesn't move selects / moves
  bool leftClick = false;
  if (ImGui::IsMouseClicked(0) && !io.WantCaptureMouse) {
    leftHeld = true;
    leftDrag = false;
    pressMX = mx;
    pressMY = my;
  } else if (leftHeld) {
    if (!ImGui::IsMouseDown(0)) {
      leftHeld = false;
      leftClick = !leftDrag;
    } else {
      if (!leftDrag && std::hypot(mx - pressMX, my - pressMY) > 5.0) leftDrag = true;
      if (leftDrag) world.cam.orbit(dYaw, dPitch, 0);
    }
  }
  lastMX = mx;
  lastMY = my;
  if (!io.WantCaptureKeyboard) {
    float k = 1.5f * io.DeltaTime;
    if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) world.cam.orbit(k, 0, 0);
    if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) world.cam.orbit(-k, 0, 0);
    if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) world.cam.orbit(0, k * 0.6f, 0);
    if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) world.cam.orbit(0, -k * 0.6f, 0);
    if (ImGui::IsKeyPressed(ImGuiKey_N)) newGame();
    if (ImGui::IsKeyPressed(ImGuiKey_U) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))) undo();
    if (ImGui::IsKeyPressed(ImGuiKey_F)) flipCamera();
    if (ImGui::IsKeyPressed(ImGuiKey_R)) resetView();
    if (ImGui::IsKeyPressed(ImGuiKey_C)) world.cinematic = !world.cinematic;
    if (ImGui::IsKeyPressed(ImGuiKey_B)) battles = !battles;
    if (ImGui::IsKeyPressed(ImGuiKey_H)) showUI = !showUI;
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) showHelp = !showHelp;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) deselect();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) quitRequested = true;
    if (ImGui::IsKeyPressed(ImGuiKey_F12)) {
      char name[64];
      std::snprintf(name, sizeof(name), "living_marble_%d.png", (int)(glfwGetTime() * 10));
      int fw, fh;
      glfwGetFramebufferSize(win, &fw, &fh);
      saveScreenshot(name, fw, fh);
    }
  }
  hover = -1;
  if (io.WantCaptureMouse) return;
  bool canAct = anims.empty() && isHumanTurn() && game.result() == chess::Result::Ongoing && !promoPending;
  int sq = pickSquare(mx, my, true);
  if (canAct) {
    const chess::Board& b = game.board();
    bool own = sq >= 0 && b.sq[sq] && chess::pieceColor(b.sq[sq]) == b.side;
    bool dest = false;
    for (const chess::Move& m : selMoves)
      if (m.to == sq) dest = true;
    if (own || dest) hover = sq;
  }
  if (leftClick && canAct) {
    if (sq < 0) {
      deselect();
      return;
    }
    const chess::Board& b = game.board();
    for (const chess::Move& m : selMoves) {
      if (m.to == sq) {
        if (m.flags & chess::MF_PROMO) {
          promoPending = true;
          promoFrom = m.from;
          promoTo = m.to;
        } else {
          commitMove(m);
        }
        return;
      }
    }
    if (b.sq[sq] && chess::pieceColor(b.sq[sq]) == b.side) {
      if (sq == selected) deselect();
      else select(sq);
    } else {
      deselect();
    }
  }
}

// ---------------------------------------------------------------------------
void App::updateGame(float dt) {
  if (!anims.empty()) {
    if (anims.front()->update(world, dt)) anims.pop_front();
    return;
  }
  if (announcePending) {
    announcePending = false;
    const chess::Board& b = game.board();
    if (game.result() == chess::Result::Ongoing && b.inCheck()) {
      world.sound(SFX_CHECK, 0.9f);
      bannerText = "Check!";
      bannerTime = 2.0f;
    }
    if (mode == Mode::TwoPlayer && autoRotate && game.result() == chess::Result::Ongoing)
      world.cam.setSide(b.side);
  }
  chess::Result r = game.result();
  if (r != chess::Result::Ongoing) {
    if (!gameOverShown) {
      gameOverShown = true;
      bannerText = chess::resultText(r);
      bannerTime = 1e9f;
      if (r == chess::Result::WhiteMates || r == chess::Result::BlackMates) {
        int loser = r == chess::Result::WhiteMates ? chess::BLACK : chess::WHITE;
        int ks = game.board().kingSq[loser];
        int kid = world.actorAt[ks];
        if (kid >= 0) anims.push_back(makeCheckmateAnim(world, kid, loser ^ 1));
      } else {
        anims.push_back(makeStalemateAnim(world));
      }
    }
    return;
  }
  if (!isHumanTurn()) {
    if (!aiPending) startAIIfNeeded();
    if (aiPending && ai.done()) {
      aiDelay -= dt;
      if (aiDelay <= 0) {
        chess::Move m = ai.result();
        aiPending = false;
        chess::Move legal;
        if (chess::findLegal(game.board(), m.from, m.to, m.promo, legal)) commitMove(legal);
      }
    }
  }
}

// ---------------------------------------------------------------------------
void App::buildScene(SceneView& sv, int fbW, int fbH) {
  CamPose cp = world.cam.current;
  float aspect = fbH > 0 ? (float)fbW / fbH : 1.6f;
  sv.nearZ = 0.08f;
  sv.farZ = 90.0f;
  proj = glm::perspective(glm::radians(cp.fov), aspect, sv.nearZ, sv.farZ);
  view = glm::lookAt(cp.pos, cp.target, vec3(0, 1, 0));
  camPos = cp.pos;
  sv.view = view;
  sv.proj = proj;
  sv.camPos = cp.pos;
  sv.focusDist = cp.focus;
  sv.aperture = cp.aperture;
  sv.time = world.time;
  sv.exposure = exposure;
  sv.flash = world.flash;
  world.buildDrawItems(renderer, sv.items);
  world.collectLights(sv.lights);
  world.fx.collect(sv.particles);
  world.collectOccluders(sv.occluders);
  world.collectRings(sv.rings, sv.ringColors);
  std::fill(std::begin(sv.boardState), std::end(sv.boardState), (uint8_t)0);
  for (const chess::Move& m : selMoves) sv.boardState[m.to * 4 + 0] = m.isCapture() ? 128 : 255;
  if (selected >= 0) sv.boardState[selected * 4 + 1] = 255;
  if (hasLastMove && anims.empty()) {
    sv.boardState[lastMove.from * 4 + 2] = 255;
    sv.boardState[lastMove.to * 4 + 2] = 255;
  }
  if (hover >= 0) sv.boardState[hover * 4 + 3] = 255;
  sv.check = glm::vec4(0);
  if (anims.empty() && game.result() == chess::Result::Ongoing && game.board().inCheck()) {
    vec3 k = squareCenter(game.board().kingSq[game.board().side]);
    sv.check = glm::vec4(k.x, k.z, 0, 1);
  }
}

void App::saveScreenshot(const std::string& path, int w, int h) {
  std::vector<uint8_t> px((size_t)w * h * 4);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glReadBuffer(GL_BACK);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  for (size_t i = 0; i < (size_t)w * h; ++i) px[i * 4 + 3] = 255;
  if (writePNG(path, w, h, 4, px.data(), true)) std::printf("saved %s\n", path.c_str());
}

void App::setupBattleDemo() {
  static const char* fens[6] = {"r1bqkb1r/ppp2ppp/2n5/3n4/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 5",
                                "rn1qkbnr/ppp2ppp/4p3/3b4/8/2N2N2/PPPPPPPP/R1BQKB1R w KQkq - 0 4",
                                "r1bqk2r/pppp1ppp/4n3/4p3/4P3/1B3N2/PPPP1PPP/RNBQK2R w KQkq - 0 6",
                                "rnb1kbnr/ppp2ppp/3q4/4p3/8/2N5/PPPR1PPP/2B1KBNR w Kkq - 0 8",
                                "1nb1kbnr/ppp2ppp/3r4/4p3/8/2N5/PPPQ1PPP/R1B1KBNR w KQk - 0 9",
                                "8/5pk1/6p1/3n4/4K3/6P1/5P2/8 w - - 0 40"};
  static const int from[6] = {28, 18, 17, 11, 11, 28};
  static const int to[6] = {35, 35, 44, 43, 43, 35};
  int k = std::max(0, std::min(5, opt.battle));
  mode = Mode::TwoPlayer;
  autoRotate = false;
  game.setFEN(fens[k]);
  world.reset();
  world.spawnFromBoard(game.board().sq, false);
  chess::Move m;
  if (chess::findLegal(game.board(), from[k], to[k], 0, m)) commitMove(m);
  battleTime = 0;
  nextShot = 0;
}

void App::setupScenario() {
  if (!opt.fen.empty()) {
    game.setFEN(opt.fen);
  } else if (opt.scenario == 1) {
    // a lively middlegame
    game.setFEN("r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP1B1PPP/R2QKB1R w KQ - 0 8");
  } else if (opt.scenario == 3) {
    // piece gallery for inspecting the sculpting
    game.setFEN("8/8/8/pnbrqk2/PNBRQK2/8/8/8 w - - 0 1");
  } else if (opt.scenario >= 10 && opt.scenario <= 15) {
    // single piece study: 10 pawn .. 15 king on d4 (white) and e5 (black)
    const char w[] = "PNBRQK", b[] = "pnbrqk";
    int k = opt.scenario - 10;
    std::string fen = std::string("k7/8/8/4") + b[k] + "3/3" + w[k] + "4/8/8/7K w - - 0 1";
    if (k == 5) fen = "8/8/8/4k3/3K4/8/8/8 w - - 0 1";
    game.setFEN(fen);
  } else if (opt.scenario == 2) {
    game.setFEN("r1b1k2r/ppppqppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 1");
  } else {
    game.reset();
  }
  world.reset();
  world.spawnFromBoard(game.board().sq, false);
}

// ---------------------------------------------------------------------------
void App::frame(float dt) {
  int fbW, fbH;
  glfwGetFramebufferSize(win, &fbW, &fbH);
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (loading) {
    if (assetsReady) {
      loader.join();
      renderer.uploadLibrary(lib);
      renderer.uploadBoardTextures(boardAlbedo, boardDetail, lib.boardHalf);
      boardAlbedo = tex::Image();
      boardDetail = tex::Image();
      loading = false;
      if (opt.hasCam) {
        world.cam.yaw = opt.camYaw;
        world.cam.pitch = opt.camPitch;
        world.cam.dist = opt.camDist;
        world.cam.target = glm::vec3(opt.camTX, opt.camTY, opt.camTZ);
      }
      if (opt.battle >= 0) setupBattleDemo();
      else if (!opt.shotPath.empty() || !opt.recordDir.empty()) {
        setupScenario();
        if (opt.demo) mode = Mode::Watch;
        else mode = Mode::TwoPlayer;
        if (opt.forceMove.size() >= 4) {
          const std::string& u = opt.forceMove;
          int f = chess::makeSq(u[0] - 'a', u[1] - '1'), t = chess::makeSq(u[2] - 'a', u[3] - '1');
          int pr = 0;
          if (u.size() > 4) pr = std::string(" pnbrqk").find(u[4]);
          chess::Move m;
          if (chess::findLegal(game.board(), f, t, pr, m)) commitMove(m);
          else std::printf("forced move %s is not legal\n", u.c_str());
        }
        if (opt.selectSq.size() == 2) select(chess::makeSq(opt.selectSq[0] - 'a', opt.selectSq[1] - '1'));
      } else newGame();
      if (mode == Mode::Watch) world.cam.autoSpin = 0.05f;
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, fbW, fbH);
      glClearColor(0.02f, 0.016f, 0.012f, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      drawLoading();
      ImGui::Render();
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      if (!opt.loadingShot.empty() && progress.done.load() >= progress.total.load() / 2) {
        saveScreenshot(opt.loadingShot, fbW, fbH);
        opt.loadingShot.clear();
      }
      return;
    }
  }

  handleInput();
  float sdt = world.beginFrame(dt);
  // per-frame visual flags (animations override them)
  for (Actor& a : world.actors) {
    a.tremble = 0;
    a.glow = glm::vec4(0);
    a.idleWeight = 1;
    a.selected = false;
    a.yawRate = 9.0f;
  }
  if (selected >= 0 && world.actorAt[selected] >= 0) world.actors[world.actorAt[selected]].selected = true;
  if (anims.empty() && game.result() == chess::Result::Ongoing && game.board().inCheck()) {
    int kid = world.actorAt[game.board().kingSq[game.board().side]];
    if (kid >= 0) {
      Actor& k = world.actors[kid];
      k.tremble = 0.35f;
      k.glow = glm::vec4(1.0f, 0.15f, 0.05f, 0.35f + 0.25f * std::sin(world.time * 7.0f));
    }
  }
  if (hover >= 0 && selected >= 0) {
    for (const chess::Move& m : selMoves)
      if (m.to == hover && m.isCapture()) {
        int vid = world.actorAt[m.captureSquare()];
        if (vid >= 0) world.actors[vid].tremble = 0.5f;
      }
  }
  updateGame(sdt);
  world.fidgetAllowed = anims.empty() && !promoPending && opt.battle < 0 && opt.shotPath.empty();
  world.excludeFidget = selected >= 0 ? world.actorAt[selected] : -1;
  world.simulate(sdt);
  world.cam.evaluate(dt, world.trauma, (float)glfwGetTime());
  if (bannerTime > 0 && bannerTime < 1e8f) bannerTime -= dt;

  // keep the internal resolution sane on very high-DPI screens (Ultra renders native)
  renderer.setRenderScale(gfxPreset >= 3 ? 1.0f : std::min(1.0f, 1440.0f / std::max(1, fbH)));
  SceneView sv;
  buildScene(sv, fbW, fbH);
  if (!opt.noRender) renderer.render(sv, fbW, fbH, 0);
  if (showUI && !opt.noUI) drawUI();
  else drawBanner();
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  frameCount++;

  // ---- capture tools ----
  if (opt.battle >= 0) {
    battleTime += sdt;
    while (nextShot < opt.shotTimes.size() && battleTime >= opt.shotTimes[nextShot]) {
      char name[512];
      std::snprintf(name, sizeof(name), "%s_%02d.png", opt.shotPrefix.c_str(), (int)nextShot);
      saveScreenshot(name, fbW, fbH);
      nextShot++;
    }
    if (!opt.recordDir.empty()) {
      char name[512];
      std::snprintf(name, sizeof(name), "%s/frame_%04d.png", opt.recordDir.c_str(), frameCount);
      saveScreenshot(name, fbW, fbH);
    }
    bool shotsDone = nextShot >= opt.shotTimes.size();
    static float idleAfter = 0;
    if (anims.empty() && battleTime > 1.0f) idleAfter += sdt;
    if ((shotsDone && opt.recordDir.empty()) || idleAfter > (opt.recordDir.empty() ? 0.0f : 1.3f) || frameCount > 3000)
      quitRequested = true;
  } else if (!opt.recordDir.empty()) {
    char name[512];
    std::snprintf(name, sizeof(name), "%s/frame_%04d.png", opt.recordDir.c_str(), frameCount);
    saveScreenshot(name, fbW, fbH);
    if (frameCount >= opt.frames) quitRequested = true;
  } else if (!opt.shotPath.empty() && frameCount >= opt.frames) {
    saveScreenshot(opt.shotPath, fbW, fbH);
    quitRequested = true;
  }
  if (opt.noRender && frameCount % 600 == 0)
    std::printf("[sim] frame %d ply %d chunks %zu particles %zu\n", frameCount, game.plyCount(), world.chunks.size(),
                world.fx.size());
}
