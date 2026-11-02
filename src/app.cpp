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
#include "imgui_internal.h"  // (the touch interface: which window a finger is on, and scrolling it)
#include "net.h"
#include "power.h"

#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

using glm::vec3;

namespace {
#ifdef __EMSCRIPTEN__
void* getProc(const char*) { return nullptr; }  // (WebGL is linked in directly)
#else
void* getProc(const char* n) { return reinterpret_cast<void*>(glfwGetProcAddress(n)); }
#endif
void glfwError(int code, const char* desc) { std::fprintf(stderr, "[glfw] error %d: %s\n", code, desc); }
void scrollCb(GLFWwindow* w, double, double dy) {
  App* app = static_cast<App*>(glfwGetWindowUserPointer(w));
  if (app && !ImGui::GetIO().WantCaptureMouse) {
    app->world.cam.orbit(0, 0, (float)(-dy * 0.08));
  }
}

#ifdef __EMSCRIPTEN__
// The page's mouse wheel and touchpad, in fractions of a wheel notch (Emscripten's GLFW
// rounds every touchpad nudge up to a whole notch): ImGui's scrolling and the camera's zoom.
EM_BOOL webWheelCb(int, const EmscriptenWheelEvent* ev, void* user) {
  float perUnit = ev->deltaMode == DOM_DELTA_PIXEL ? 1.0f / 100.0f : (ev->deltaMode == DOM_DELTA_LINE ? 1.0f / 3.0f : 1.0f);
  float wx = (float)ev->deltaX * -perUnit, wy = (float)ev->deltaY * -perUnit;
  ImGui::GetIO().AddMouseWheelEvent(wx, wy);
  App* app = static_cast<App*>(user);
  if (!ImGui::GetIO().WantCaptureMouse) app->world.cam.orbit(0, 0, -wy * 0.08f);
  return EM_TRUE;  // the page itself doesn't scroll or zoom
}

// Fingers on the canvas (App::onTouch)
EM_BOOL webTouchCb(int type, const EmscriptenTouchEvent* ev, void* user) {
  static_cast<App*>(user)->onTouch(type, ev);
  return EM_TRUE;  // the page doesn't scroll or zoom, and the browser makes no mouse clicks of them
}

// Keeps the canvas's drawing buffer at the size it is shown at, in device pixels (sharp on
// high-DPI screens). The page's style sheet sizes the canvas to fill the browser window.
// (Not through glfwSetWindowSize: Emscripten's asks for full screen when the size is the
// screen's, and sizes a full-screen canvas in CSS pixels. GLFW's window just takes the size.)
void fitCanvas(GLFWwindow* win) {
  double cw = 0, ch = 0;
  emscripten_get_element_css_size("#canvas", &cw, &ch);
  double dpr = emscripten_get_device_pixel_ratio();
  int w = std::max(1, (int)std::lround(cw * dpr)), h = std::max(1, (int)std::lround(ch * dpr));
  int ww, wh;
  glfwGetWindowSize(win, &ww, &wh);
  int bw = 0, bh = 0;
  emscripten_get_canvas_element_size("#canvas", &bw, &bh);
  if (w != ww || h != wh || w != bw || h != bh) {
    EM_ASM({
      var c = Module['canvas'];
      if (c.width != $0) c.width = $0;
      if (c.height != $1) c.height = $1;
      if (GLFW.active) { GLFW.active.width = $0; GLFW.active.height = $1; }
    }, w, h);
  }
}
#endif

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
  depthOfField = o.dof;
  if (o.demo) mode = Mode::Watch;
  if (o.level >= 0) aiLevel = std::min(4, o.level);
  fixedStep = !o.shotPath.empty() || !o.recordDir.empty() || o.battle >= 0 || !o.shotTimes.empty();
}

App::~App() {
  if (loader.joinable()) loader.join();
  for (ArmyBuild& b : army)
    if (b.thread.joinable()) b.thread.join();
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
#ifdef __EMSCRIPTEN__
  // WebGL 2 on the page's canvas; opaque, so the page never shows through
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_ALPHA_BITS, 0);
  glfwWindowHint(GLFW_SAMPLES, 0);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_SAMPLES, 0);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#endif
#ifdef __APPLE__
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif
  GLFWmonitor* mon = nullptr;
  int w = opt.width, h = opt.height;
#ifndef __EMSCRIPTEN__  // (the page goes full screen through the browser: setFullscreen())
  if (opt.fullscreen) {
    mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    w = vm->width;
    h = vm->height;
    // leaving full screen gives back a centred window of the requested size
    int mx, my;
    glfwGetMonitorPos(mon, &mx, &my);
    winW = std::min(opt.width, vm->width);
    winH = std::min(opt.height, vm->height);
    winX = mx + (vm->width - winW) / 2;
    winY = my + (vm->height - winH) / 2;
  }
#endif
  win = glfwCreateWindow(w, h, "Living Marble - 3D Battle Chess", mon, nullptr);
  if (!win) return false;
  fullscreen = mon != nullptr;
  glfwMakeContextCurrent(win);
#ifdef __EMSCRIPTEN__
  fitCanvas(win);  // (the browser paces the frames: the main loop's swap interval is set in run())
#else
  glfwSwapInterval(fixedStep ? 0 : 1);
#endif
  const char* missing = nullptr;
  if (!lmLoadGL(getProc, &missing)) {
    std::fprintf(stderr, "OpenGL 3.3 function missing: %s\n", missing ? missing : "?");
    return false;
  }
  glfwSetWindowUserPointer(win, this);
#ifdef __EMSCRIPTEN__
  emscripten_set_wheel_callback("#canvas", this, false, webWheelCb);
  emscripten_set_touchstart_callback("#canvas", this, true, webTouchCb);
  emscripten_set_touchmove_callback("#canvas", this, true, webTouchCb);
  emscripten_set_touchend_callback("#canvas", this, true, webTouchCb);
  emscripten_set_touchcancel_callback("#canvas", this, true, webTouchCb);
  // Emscripten's GLFW takes a finger for a click of the mouse where the mouse last was (and
  // doesn't follow it): the game reads the fingers itself
  // (no commas outside brackets in EM_ASM's code: the preprocessor would split it there)
  EM_ASM({
    var c = Module['canvas'];
    if (typeof GLFW === 'undefined' || !c) return;
    if (GLFW.onMousemove) c.removeEventListener('touchmove', GLFW.onMousemove, true);
    if (GLFW.onMouseButtonDown) c.removeEventListener('touchstart', GLFW.onMouseButtonDown, true);
    if (GLFW.onMouseButtonUp) c.removeEventListener('touchcancel', GLFW.onMouseButtonUp, true);
    if (GLFW.onMouseButtonUp) c.removeEventListener('touchend', GLFW.onMouseButtonUp, true);
  });
  fullscreenAvailable = EM_ASM_INT({ return (document.fullscreenEnabled || document.webkitFullscreenEnabled) ? 1 : 0; }) != 0;
#else
  glfwSetScrollCallback(win, scrollCb);
#endif
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
#ifdef __EMSCRIPTEN__
  xs = ys = (float)emscripten_get_device_pixel_ratio();  // the canvas is in device pixels
#else
  glfwGetWindowContentScale(win, &xs, &ys);
#endif
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
  if (opt.touch) {  // fingers: larger targets, a little room around them, slim scroll bars
    st.FramePadding = ImVec2(12, 10);
    st.ItemSpacing = ImVec2(10, 10);
    st.TouchExtraPadding = ImVec2(3, 3);
    st.GrabMinSize = 22.0f;
    st.ScrollbarSize = 6.0f;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
  }
  if (uiScale > 1.0f) st.ScaleAllSizes(uiScale);

  ImGui_ImplGlfw_InitForOpenGL(win, true);
#ifdef __EMSCRIPTEN__
  ImGui_ImplOpenGL3_Init("#version 300 es");  // (imgui is built with IMGUI_IMPL_OPENGL_ES3)
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
    if (opt.pieces == 1 || opt.pieces == 2) {
      // starting with another army: make it before the game begins
      progress.stage = opt.pieces == 1 ? "Raising the medieval army" : "Landing the aliens";
      buildArmy(opt.pieces, nullptr);
      army[opt.pieces].ready = true;
    }
    assetsReady = true;
  });
}

void App::buildArmy(int set, meshgen::BuildProgress* prog) {
  if (set == 1) medieval::buildLibrary(mlib, opt.quality, prog);
  else alien::buildLibrary(alib, opt.quality, prog);
}

void App::uploadArmy(int set) {
  if (set == 1) renderer.uploadMedieval(mlib);
  else renderer.uploadAlien(alib);
  army[set].uploaded = true;
}

void App::startArmyBuild(int set) {
  ArmyBuild& b = army[set];
  if (b.running || b.ready) return;
  b.running = true;
  b.thread = std::thread([this, set]() {
    buildArmy(set, &army[set].progress);
    army[set].ready = true;
  });
}

float App::armyProgress(int set) const {
  const meshgen::BuildProgress& p = army[set].progress;
  int t = std::max(1, p.total.load());
  return std::min(1.0f, (float)p.done.load() / t);
}

void App::setPieceSet(int s) {
  s = (s == 1 || s == 2) ? s : 0;
  if (s == pieceSet && pendingSet < 0) return;
  if (s != 0 && !army[s].uploaded) startArmyBuild(s);
  pendingSet = s;
}

// Swaps every piece for its counterpart in the other army; they materialise in place.
void App::applyPieceSet(int s) {
  pieceSet = s;
  pendingSet = -1;
  world.pieceSet = s;
  world.stopFidgets();
  for (Actor& a : world.actors) {
    if (!a.alive) continue;
    a.set = s;
    a.tilt = glm::quat(1, 0, 0, 0);
    a.resetControls();
    a.dissolve = 1.0f;
    a.dissolveVel = -world.fx.rnd(1.1f, 1.6f);
    a.dissolveColor = s == 1 ? vec3(1.0f, 0.62f, 0.25f) : (s == 2 ? vec3(0.4f, 1.0f, 0.5f) : vec3(1.0f, 0.75f, 0.35f));
  }
  world.sound(SFX_CHIME, 0.6f, vec3(0), s == 1 ? 0.75f : (s == 2 ? 1.4f : 1.0f));
  audio.setMusicArmy(s);  // and its music
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
  music = !opt.noMusic;
  audio.setMusic(music);
  audio.setMusicVolume(musicVolume);
  audio.setMusicArmy(opt.pieces);
  if (!opt.noAudio && audio.init()) {
    world.audio = &audio;
    audio.setVolume(volume);
    audio.resumeMusic();
  } else {
    std::printf("[audio] no audio device - running silent\n");
  }
  world.lib = &lib;
  world.mlib = &mlib;
  world.alib = &alib;
  world.reset();
  loadAssets();
  prevTime = glfwGetTime();
#ifdef __EMSCRIPTEN__
  // The browser runs the loop: it calls tick() for every frame it shows, and run() returns
  // with the game still going. (A swap interval of n draws every n-th of the display's frames.)
  emscripten_set_main_loop_arg([](void* app) { static_cast<App*>(app)->tick(); }, this, 0, false);
  glfwSwapInterval(1);
  return 0;
#else
  while (tick()) {
  }
#endif
  if (fixedStep) {
    std::printf("moves:");
    for (size_t i = 0; i < game.san().size(); ++i) std::printf(" %s", game.san()[i].c_str());
    std::printf("\nresult: %s (%d plies, %d actors, %zu chunks)\n", chess::resultText(game.result()), game.plyCount(),
                (int)world.actors.size(), world.chunks.size());
  }
  return 0;
}

// One pass of the main loop; false once the game is over (the window closed, or Quit).
bool App::tick() {
  if (glfwWindowShouldClose(win) || quitRequested) return false;
#ifdef __EMSCRIPTEN__
  // a lowered frame rate the swap interval can't keep to: skip the browser's frame (the canvas keeps the last one)
  if (frameDue > 0 && glfwGetTime() < frameDue) return true;
  fitCanvas(win);
  fullscreen = EM_ASM_INT({ return (document.fullscreenElement || document.webkitFullscreenElement) ? 1 : 0; }) != 0;  // (Esc leaves it too)
#else
  // minimised: nothing to draw, and the game waits (the music plays on)
  if (!fixedStep && glfwGetWindowAttrib(win, GLFW_ICONIFIED)) {
    glfwWaitEventsTimeout(0.25);
    prevTime = glfwGetTime();
    return true;
  }
  // a lowered frame rate the driver doesn't keep to: wait for the frame's time (any input ends it)
  double wait = frameDue - glfwGetTime();
  if (wait > 0) glfwWaitEventsTimeout(wait);
  else glfwPollEvents();
#endif
  double now = glfwGetTime();
  float dt = (float)(now - prevTime);
  prevTime = now;
  if (dt > 0) fps = fps * 0.95f + 0.05f / std::max(dt, 1e-4f);
  dt = std::min(dt, 0.05f);
  if (fixedStep && !loading) dt = 1.0f / 30.0f;
  frame(dt);
  pace(dt, now);
  glfwSwapBuffers(win);
  return true;
}

// ---------------------------------------------------------------------------
void App::newGame(bool tellFriend) {
  if (mode == Mode::Online) {
    if (tellFriend) {
      if (online.kind == OnlineGame::Kind::Match) {  // a matched game: another opponent, once it is over
        if (!onlineGameOn() && !online.seeking) startMatch();
        return;
      }
      if (online.net != OnlineGame::Net::Open) return;  // (the friend has to hear of it)
      net::command("new");
    }
    online.moves.clear();
    online.endText.clear();
    online.ratingText.clear();
    online.endShown = false;
    online.drawOffered = online.myDrawOffer = false;
    online.resignAsked = -1;
  }
  anims.clear();
  cancelHint();
  ai.cancel();
  aiPending = false;
  promoPending = false;
  gameOverShown = false;
  checkAnnounced = false;
  announcePending = false;
  hasLastMove = false;
  deselect();
  if (!opt.fen.empty() && mode != Mode::Online) game.setFEN(opt.fen);
  else game.reset();
  world.reset();
  world.cam.release(0.6f);
  world.spawnFromBoard(game.board().sq, true);
  world.sound(SFX_CHIME, 0.6f);
  audio.resumeMusic();
  world.cam.setSide(mode == Mode::HumanBlack ? 1 : mode == Mode::Online ? online.side : 0);
  bannerText = "";
}

// Sets out the pieces, the ghosts of the captured ones and the last move's marks as the game
// stands (after an undo, or an online game brought up to date).
void App::rebuildWorld() {
  anims.clear();
  gameOverShown = false;
  deselect();
  hasLastMove = game.plyCount() > 0;
  if (hasLastMove) lastMove = game.moves().back();
  world.chunks.clear();
  world.props.clear();
  world.cam.release(0.6f);
  world.spawnFromBoard(game.board().sq, true);
  world.ghosts.clear();
  for (const chess::Move& m : game.moves())
    if (m.isCapture() && m.captured) world.addGhost((m.captured & 7) - 1, m.captured >> 3, 0.0f, true);
}

void App::undo() {
  if (mode == Mode::Online) return;  // (both players see the same game)
  if (animating() || promoPending) return;
  if (game.plyCount() == 0) return;
  cancelHint();
  ai.cancel();
  aiPending = false;
  int n = (mode == Mode::TwoPlayer || mode == Mode::Watch) ? 1 : 2;
  if (game.result() != chess::Result::Ongoing && !isHumanTurn()) n = 1;
  for (int i = 0; i < n && game.plyCount() > 0; ++i) game.undo();
  // make sure the human is to move again
  if (!isHumanTurn() && game.plyCount() > 0 && mode != Mode::Watch) game.undo();
  rebuildWorld();
  world.sound(SFX_CHIME, 0.5f, vec3(0), 0.8f);
  audio.resumeMusic();  // in case the game had ended
}

void App::flipCamera() { world.cam.yaw = wrapAngle(world.cam.yaw + kPI); }

void App::resetView() {
  world.cam.setSide(mode == Mode::HumanBlack ? 1
                    : mode == Mode::TwoPlayer ? game.board().side
                    : mode == Mode::Online    ? online.side
                                              : 0);
  world.cam.pitch = homePitch();
  world.cam.dist = homeDist();
}

void App::setMode(Mode m) {
  if (mode == Mode::Online && m != Mode::Online) leaveOnline();
  mode = m;
  cancelHint();
  ai.cancel();
  aiPending = false;
  world.cam.autoSpin = 0;
  if (m == Mode::HumanWhite) world.cam.setSide(0);
  if (m == Mode::HumanBlack) world.cam.setSide(1);
  deselect();
}

// The monitor holding most of the window (the primary one if it is off-screen).
static GLFWmonitor* monitorOf(GLFWwindow* w) {
  int wx, wy, ww, wh;
  glfwGetWindowPos(w, &wx, &wy);
  glfwGetWindowSize(w, &ww, &wh);
  int n = 0;
  GLFWmonitor** mons = glfwGetMonitors(&n);
  GLFWmonitor* best = glfwGetPrimaryMonitor();
  long bestArea = 0;
  for (int i = 0; i < n; ++i) {
    int mx, my;
    glfwGetMonitorPos(mons[i], &mx, &my);
    const GLFWvidmode* vm = glfwGetVideoMode(mons[i]);
    long ox = std::max(0, std::min(wx + ww, mx + vm->width) - std::max(wx, mx));
    long oy = std::max(0, std::min(wy + wh, my + vm->height) - std::max(wy, my));
    if (ox * oy > bestArea) {
      bestArea = ox * oy;
      best = mons[i];
    }
  }
  return best;
}

void App::setFullscreen(bool on) {
  if (on == fullscreen || !win) return;
#ifdef __EMSCRIPTEN__
  // the browser's full screen, for the whole page (the canvas follows it in tick())
  // (an iPad's Safari has the webkit names)
  if (on) EM_ASM({
    var e = document.documentElement;
    if (e.requestFullscreen) e.requestFullscreen().catch(function() {});
    else if (e.webkitRequestFullscreen) e.webkitRequestFullscreen();
  });
  else EM_ASM({
    var d = document;
    if (d.fullscreenElement && d.exitFullscreen) d.exitFullscreen().catch(function() {});
    else if (d.webkitFullscreenElement && d.webkitExitFullscreen) d.webkitExitFullscreen();
  });
  return;
#endif
  if (on) {
    glfwGetWindowPos(win, &winX, &winY);
    glfwGetWindowSize(win, &winW, &winH);
    GLFWmonitor* mon = monitorOf(win);
    const GLFWvidmode* vm = glfwGetVideoMode(mon);
    glfwSetWindowMonitor(win, mon, 0, 0, vm->width, vm->height, vm->refreshRate);
  } else {
    glfwSetWindowMonitor(win, nullptr, winX, winY, winW, winH, 0);
  }
  fullscreen = on;
  glfwSwapInterval(fixedStep ? 0 : 1);  // some drivers drop the swap interval on a mode switch
  vsyncs = 1;
  powerCheck = 0;  // another monitor, perhaps another refresh rate
}

// Picks the frame rate for what is on screen (with Save power on). A move, a battle, flying
// debris, a turning camera or a player at the controls get every refresh of the display. Once
// the board has been at rest for a second (the pieces only breathe and fidget, the ghosts bob)
// it draws 30 frames a second, which look the same and take a fraction of the power; so does
// everything while battery saver is on. A lowered rate is a whole number of refreshes per frame
// (the swap interval), so the frames stay evenly spaced; where the driver ignores swap
// intervals, a timer waits instead.
void App::pace(float dt, double frameStart) {
  if (fixedStep) return;
  powerCheck -= dt;
  if (powerCheck <= 0) {
    powerCheck = 3;
    powerSaver = power::saverOn();
#ifdef __EMSCRIPTEN__
    GLFWmonitor* mon = glfwGetPrimaryMonitor();  // (the browser's screen)
#else
    GLFWmonitor* mon = glfwGetWindowMonitor(win);
    // (Wayland doesn't tell where a window is)
    if (!mon) mon = glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ? glfwGetPrimaryMonitor() : monitorOf(win);
#endif
    const GLFWvidmode* vm = mon ? glfwGetVideoMode(mon) : nullptr;
    refreshHz = vm && vm->refreshRate > 0 ? vm->refreshRate : 60;
  }
  // how fast the view turns or travels, in camera distances per second (the slow orbit: 0.05)
  const CamPose& cp = world.cam.current;
  float moved = glm::length(cp.pos - pacedCam.pos) + glm::length(cp.target - pacedCam.target);
  float camRate = moved / (std::max(1.0f, glm::length(cp.pos - cp.target)) * std::max(dt, 1e-3f));
  pacedCam = cp;
  double mx, my;
  glfwGetCursorPos(win, &mx, &my);
  bool arrows = ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_RightArrow) ||
                ImGui::IsKeyDown(ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_DownArrow);
  bool input = mx != pacedMX || my != pacedMY || selected != pacedSelected || arrows || ImGui::IsAnyMouseDown() ||
               touch.n > 0;
  pacedMX = mx;
  pacedMY = my;
  pacedSelected = selected;
  // debris still tumbling, ghosts jumping for joy, an army changing into another
  bool settling = false;
  for (const Chunk& c : world.chunks) settling = settling || !c.sleeping;
  for (const Ghost& g : world.ghosts) settling = settling || g.cheer > 0;
  for (const Actor& a : world.actors) settling = settling || a.dissolveVel != 0;
  bool shaken = world.trauma > 0 || world.timeScale < 0.99f;  // camera shake, slow motion
  bool lively = !loading && (input || !anims.empty() || camRate > 0.06f || shaken || settling);
  restTime = lively ? 0 : restTime + dt;

  int want = 1;  // refreshes per frame
  paceNote = "";
  if (savePower && (powerSaver || restTime > 1)) {
    want = std::max(1, (int)std::floor(refreshHz / 30.0 + 0.1));  // 30 fps or a little more
    if (want > 1) paceNote = powerSaver ? "battery saver" : "at rest";
  }
  if (intervalsWork) {
    if (want != vsyncs) {
      glfwSwapInterval(want);
      vsyncs = want;
      slowFrames = 0;
      slowSince = frameStart;
    } else if (want > 1 && ++slowFrames == 30) {
      // 30 frames should take 30 x `want` refreshes; sooner, and the driver ignores the interval
      // (or vsync is off): from now on a timer waits for each frame's time
      if (frameStart - slowSince < 0.75 * 30 * want / refreshHz) {
        intervalsWork = false;
        glfwSwapInterval(1);
        vsyncs = 1;
      }
    }
  }
  frameDue = !intervalsWork && want > 1 ? frameStart + (double)want / refreshHz : 0;
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
    case Mode::Online:
      return online.started && online.net == OnlineGame::Net::Open && side == online.side && online.endText.empty();
    default: return false;
  }
}

const char* App::statusText() const {
  static std::string s;
  chess::Result r = game.result();
  if (mode == Mode::Online) {
    bool match = online.kind == OnlineGame::Kind::Match;
    if (!online.error.empty()) return online.error.c_str();
    if (online.net == OnlineGame::Net::Connecting) return "Connecting to the server...";
    if (online.net == OnlineGame::Net::Lost) return "Connection lost - reconnecting...";
    if (online.net == OnlineGame::Net::Closed) return "Not connected";
    if (online.seeking) {
      int t = (int)(glfwGetTime() - online.seekSince);
      char buf[64];
      std::snprintf(buf, sizeof(buf), "Looking for an opponent... %d:%02d", t / 60, t % 60);
      s = buf;
      return s.c_str();
    }
    if (!online.endText.empty()) return online.endText.c_str();
    if (r != chess::Result::Ongoing) return chess::resultText(r);
    if (!online.started) return match ? "Setting up the game..." : "Waiting for your friend to join...";
    std::string them = match && !online.names[online.side ^ 1].empty() ? online.names[online.side ^ 1] : "Your friend";
    if (isHumanTurn()) s = "Your move";
    else s = them + "'s move" + (online.peer ? "" : " (they're away)");
    if (game.board().inCheck()) s += "  -  Check!";
    return s.c_str();
  }
  if (r != chess::Result::Ongoing) return chess::resultText(r);
  bool white = game.board().side == chess::WHITE;
  if (!isHumanTurn() && aiPending) s = std::string(white ? "White" : "Black") + " is thinking...";
  else if (hintPending) s = "Looking for a good move...";
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
  if (mode == Mode::Online) return;  // (the friend moves)
  if (aiPending || game.result() != chess::Result::Ongoing || isHumanTurn()) return;
  ai.start(game.board(), game.hashHistory(), aiLevel);
  aiPending = true;
  aiDelay = mode == Mode::Watch ? 0.7f : 0.15f;
}

// The hint searches at full strength ("King": about 2.5 s), whatever the opponent's level.
constexpr int kHintLevel = 3;

bool App::canHint() const {
  return !loading && anims.empty() && isHumanTurn() && game.result() == chess::Result::Ongoing && !promoPending &&
         !hintPending && !aiPending &&
         mode != Mode::TwoPlayer && mode != Mode::Online;  // (no help from the computer against another person)
}

void App::requestHint() {
  if (!canHint()) return;
  ai.start(game.board(), game.hashHistory(), kHintLevel);
  hintPending = true;
}

void App::cancelHint() {
  if (!hintPending) return;
  ai.cancel();
  hintPending = false;
}

// Selects the suggested piece with only the suggested square glowing: a click there plays it.
void App::showHint(const chess::Move& m) {
  chess::Move legal;
  if (!chess::findLegal(game.board(), m.from, m.to, m.promo, legal)) return;
  selected = legal.from;
  selMoves.assign(1, legal);
  bannerText = "Hint: " + chess::toSAN(game.board(), legal);
  bannerTime = 3.0f;
  world.sound(SFX_CHIME, 0.35f, squareCenter(legal.from), 1.5f);
}

void App::commitMove(const chess::Move& m) {
  cancelHint();
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
    if (plan.victim >= 0) world.actors[plan.victim].ghostOnDeath = true;  // it will watch from the side
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
  // the music of the piece that moves (a promoted pawn: of what it becomes)
  audio.playMusic((m.flags & chess::MF_PROMO) ? m.promo - 1 : chess::pieceType(game.board().sq[m.from]) - 1);
  if (mode == Mode::Online && game.board().side == online.side)  // this player's move: to the opponent
    net::command("move " + std::to_string(game.plyCount()) + " " + m.uci());
  game.play(m);
  lastMove = m;
  hasLastMove = true;
  deselect();
  anims.push_back(makeMoveAnim(world, plan));
  announcePending = true;
  startAIIfNeeded();
}

// ---------------------------------------------------------------------------
// Online games (the web page): the moves go through the server (web/relay.py) to the other
// player's browser, and theirs come back the same way. With a friend (by a link), or matched
// by the server with another player looking for an opponent (rated, with clocks).

bool App::onlineAvailable() const { return net::available(); }

// A new online game with a friend (code empty: this player is White and gets a link for the
// friend), or a game by its code (a friend's link, or coming back after a reload). The board
// starts afresh; the server's moves follow.
void App::startOnline(const std::string& code) {
  if (!net::available()) return;
  if (mode == Mode::Online) leaveOnline();
  setMode(Mode::Online);
  online = OnlineGame();
  newGame(false);
  net::command(code.empty() ? std::string("create") : "join " + code);
}

// Looks for an opponent: the server pairs this player with someone of a similar rating.
void App::startMatch() {
  if (!net::available()) return;
  if (mode == Mode::Online) leaveOnline();
  setMode(Mode::Online);
  online = OnlineGame();
  online.kind = OnlineGame::Kind::Match;
  online.seeking = true;
  online.seekSince = glfwGetTime();
  newGame(false);
  net::command("seek");
}

void App::leaveOnline() {
  net::command("close");
  online = OnlineGame();
  opt.join.clear();
}

void App::copyOnlineLink() { net::command("copy"); }

// Resigning takes a second click within 3 seconds (or a few frames, on a slow device).
bool App::resignArmed() const {
  return online.resignAsked >= 0 && (glfwGetTime() - online.resignAsked < 3.0 || frameCount - online.resignFrame < 6);
}

void App::resignOnline() {
  if (!onlineGameOn()) return;
  if (resignArmed()) {
    net::command("resign");
    online.resignAsked = -1;
  } else {
    online.resignAsked = glfwGetTime();
    online.resignFrame = frameCount;
  }
}

// Offers a draw, or (accept) takes the opponent's offer.
void App::offerDraw(bool accept) {
  if (!onlineGameOn() || (!accept && (online.myDrawOffer || online.drawOffered))) return;
  net::command("draw");
  if (accept) online.drawOffered = false;
  else online.myDrawOffer = true;
}

void App::declineDraw() {
  if (!online.drawOffered) return;
  net::command("decline");
  online.drawOffered = false;
}

void App::openLeaderboard() {
  leaders.open = true;
  leaders.loading = true;
  net::command("top");
}

bool App::onlineGameOn() const {
  return mode == Mode::Online && online.started && !online.seeking && online.endText.empty() &&
         game.result() == chess::Result::Ongoing;
}

int App::onlineClock(int side) const {
  int ms = online.clockMs[side];
  if (online.clockRun == side && online.endText.empty()) ms -= (int)((glfwGetTime() - online.clockAt) * 1000);
  return std::max(0, ms);
}

// How a game ended, for this player: "You win by resignation", "Draw by agreement" ...
static std::string endDescription(const std::string& result, const std::string& why, int side) {
  if (why == "aborted") return "Game aborted - not rated";
  bool draw = result == "1/2-1/2";
  bool won = (result == "1-0" && side == chess::WHITE) || (result == "0-1" && side == chess::BLACK);
  std::string reason = why == "checkmate"     ? "by checkmate"
                       : why == "resignation" ? (won ? "- your opponent resigned" : "- you resigned")
                       : why == "timeout"     ? (won ? "on time" : "- your time ran out")
                       : why == "abandoned"   ? (won ? "- your opponent left" : "- you were away too long")
                       : why == "agreement"   ? "by agreement"
                       : why == "stalemate"   ? "by stalemate"
                       : why == "fifty"       ? "by the fifty-move rule"
                       : why == "repetition"  ? "by threefold repetition"
                       : why == "material"    ? "- insufficient material"
                                              : "";
  return std::string(draw ? "Draw " : won ? "You win " : "You lose ") + reason;
}

// What the page has heard from the server since the last frame
void App::pollOnline(float dt) {
  if (online.copied > 0) online.copied -= dt;
  if (noticeTime > 0) noticeTime -= dt;
  std::string msg;
  while (net::next(msg)) {
    std::istringstream in(msg);
    std::string kind;
    in >> kind;
    // about this player, and the leaderboard: whatever the mode
    if (kind == "me") {
      std::string name;
      in >> name;
      me = OnlinePlayer();
      if (name != "-") {
        me.known = true;
        me.name = name;
        in >> me.rating >> me.games;
      }
      continue;
    }
    if (kind == "top") {
      int myRating = 0, myGames = 0;
      in >> leaders.minGames >> leaders.myRank >> myRating >> myGames;
      leaders.rows.clear();
      Leaderboard::Row r;
      while (in >> r.name >> r.rating >> r.games) leaders.rows.push_back(r);
      leaders.loading = false;
      continue;
    }
    if (kind == "notice") {
      std::getline(in >> std::ws, notice);
      noticeTime = 5;
      continue;
    }
    if (mode != Mode::Online) continue;  // (still on its way when this player left)
    bool match = online.kind == OnlineGame::Kind::Match;
    if (kind == "status") {
      std::string s;
      in >> s;
      online.net = s == "open"     ? OnlineGame::Net::Open
                   : s == "lost"   ? OnlineGame::Net::Lost
                   : s == "closed" ? OnlineGame::Net::Closed
                                   : OnlineGame::Net::Connecting;
    } else if (kind == "needlogin") {  // (the login has run out: the front page logs in again)
      online.error = "You're not logged in. Reload the page to log in.";
      online.seeking = false;
    } else if (kind == "seeking") {
      online.seeking = true;
    } else if (kind == "room") {
      std::string code, seat;
      int full = 0, peer = 0, rated = 0;
      in >> code >> seat >> full >> peer >> rated;
      std::vector<std::string> uci;
      for (std::string u; in >> u;) uci.push_back(u);
      int side = seat == "b" ? chess::BLACK : chess::WHITE;
      bool turn = online.room.empty() || side != online.side;
      if (code != online.room) {  // another game (a new opponent): its own end, offers and clocks
        online.endText.clear();
        online.ratingText.clear();
        online.endShown = false;
        online.clocks = false;
      }
      online.room = code;
      online.side = side;
      online.started = full != 0;
      online.peer = peer != 0;
      online.rated = rated != 0;
      online.kind = rated ? OnlineGame::Kind::Match : OnlineGame::Kind::Friend;
      online.seeking = false;
      online.drawOffered = online.myDrawOffer = false;
      online.awaySeconds = -1;
      online.net = OnlineGame::Net::Open;
      online.error.clear();
      if (turn) world.cam.setSide(side);  // (the first time: from this player's side of the board)
      if (rated && turn && uci.empty()) {
        bannerText = "Your opponent is here";
        bannerTime = 2.5f;
        world.sound(SFX_CHIME, 0.6f, vec3(0), 1.2f);
      }
      syncOnline(uci);
    } else if (kind == "players") {
      in >> online.names[0] >> online.ratings[0] >> online.names[1] >> online.ratings[1];
    } else if (kind == "clock") {
      std::string run;
      in >> online.clockMs[0] >> online.clockMs[1] >> run;
      online.clockRun = run == "w" ? chess::WHITE : run == "b" ? chess::BLACK : -1;
      online.clockAt = glfwGetTime();
      online.clocks = true;
    } else if (kind == "link") {
      std::getline(in >> std::ws, online.link);
    } else if (kind == "move") {
      std::string rest;
      std::getline(in >> std::ws, rest);
      online.moves.push_back(rest);
      online.drawOffered = online.myDrawOffer = false;  // (a move answers a draw offer)
    } else if (kind == "peer") {
      int here = 0;
      in >> here;
      std::string them = match ? "Your opponent" : "Your friend";
      if (here && !online.peer) {
        bannerText = online.started ? them + " is back" : them + " is here";
        bannerTime = 2.5f;
        world.sound(SFX_CHIME, 0.6f, vec3(0), 1.2f);
      } else if (!here && online.peer && online.endText.empty()) {
        bannerText = them + " left";
        bannerTime = 2.5f;
      }
      online.peer = here != 0;
      if (here) {
        online.started = true;
        online.awaySeconds = -1;
      }
    } else if (kind == "away") {
      in >> online.awaySeconds;
      online.awayAt = glfwGetTime();
    } else if (kind == "draw") {
      online.drawOffered = true;
      bannerText = "Your opponent offers a draw";
      bannerTime = 2.5f;
    } else if (kind == "declined") {
      online.myDrawOffer = false;
      notice = "Your draw offer was declined.";
      noticeTime = 4;
    } else if (kind == "end") {
      std::string result, why;
      in >> result >> why;
      online.endText = endDescription(result, why, online.side);
      online.endShown = false;
      online.drawOffered = online.myDrawOffer = false;
      online.awaySeconds = -1;
      online.clockRun = -1;
      int wb = 0, wa = 0, bb = 0, ba = 0;
      if (in >> wb >> wa >> bb >> ba) {
        int before = online.side == chess::WHITE ? wb : bb, after = online.side == chess::WHITE ? wa : ba;
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Your rating: %d -> %d (%+d)", before, after, after - before);
        online.ratingText = buf;
        if (me.known) {
          me.rating = after;
          me.games++;
        }
      }
    } else if (kind == "new") {
      newGame(false);
      bannerText = "Your friend started a new game";
      bannerTime = 2.5f;
    } else if (kind == "error") {
      std::getline(in >> std::ws, online.error);
      online.seeking = false;
    } else if (kind == "copied") {
      int how = 0;
      in >> how;
      online.copyHow = how;
      online.copied = 2.5f;
    }
  }
}

// The opponent's moves, one at a time while the board is at rest; and the end of the game as
// the server has it, once the last move has been shown
void App::playOnlineMoves() {
  if (!online.endText.empty() && !online.endShown && anims.empty() && online.moves.empty()) {
    online.endShown = true;
    if (game.result() == chess::Result::Ongoing) {  // (resigned, out of time ...: the board can't tell)
      bannerText = online.endText;
      bannerTime = 1e9f;
      audio.stopMusic();
      world.sound(SFX_CHIME, 0.7f, vec3(0), 0.7f);
    }
  }
  if (!anims.empty() || promoPending || online.moves.empty() || game.result() != chess::Result::Ongoing) return;
  std::string m = online.moves.front();
  online.moves.pop_front();
  int ply = std::atoi(m.c_str());
  std::string u = m.substr(std::min(m.size(), m.find(' ') + 1));
  if (ply < game.plyCount()) return;  // (already played)
  chess::Move legal;
  if (ply == game.plyCount() && game.board().side != online.side && chess::findLegalUci(game.board(), u, legal)) {
    commitMove(legal);
  } else {
    online.moves.clear();
    net::command("sync");  // out of step: the server sends the game as it stands
  }
}

// The server's record of the game (on joining, and coming back after a reload or a lost
// connection): the friend's one missed move is played as usual; further off, the board is
// set out afresh.
void App::syncOnline(const std::vector<std::string>& uci) {
  const std::vector<chess::Move>& mine = game.moves();
  size_t same = 0;
  while (same < mine.size() && same < uci.size() && mine[same].uci() == uci[same]) ++same;
  online.moves.clear();
  if (same == mine.size() && same == uci.size()) return;
  if (same == mine.size() && uci.size() == same + 1) {
    online.moves.push_back(std::to_string(same) + " " + uci[same]);
    return;
  }
  cancelHint();
  promoPending = false;
  announcePending = false;
  game.reset();
  for (const std::string& u : uci) {
    chess::Move m;
    if (!chess::findLegalUci(game.board(), u, m)) break;
    game.play(m);
  }
  rebuildWorld();
  world.sound(SFX_CHIME, 0.5f, vec3(0), 0.8f);
  audio.resumeMusic();
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

// ---------------------------------------------------------------------------
// Fingers (the web page on iPhone, iPad and other touch screens), as in the iPhone and iPad app:
//   one finger    tap: select a piece, then tap a glowing square to move it
//                 drag: turn the board (from above, the side or underneath)
//   two fingers   pinch: zoom   twist: turn the board   slide up or down: tilt
// On the interface a finger is the mouse: it presses buttons and drags sliders, and dragged
// up or down it scrolls the window under it.

// The interface window at a point (in canvas pixels), or none: the one in front, as last drawn.
void* App::uiWindowAt(float x, float y) const {
  ImGuiContext* g = ImGui::GetCurrentContext();
  if (!g || opt.noUI) return nullptr;
  for (int i = g->Windows.Size - 1; i >= 0; --i) {
    ImGuiWindow* w = g->Windows[i];
    if (!w->Active || w->Hidden || (w->Flags & ImGuiWindowFlags_NoMouseInputs)) continue;
    ImRect bb = w->Rect();
    bb.Expand(g->Style.TouchExtraPadding);
    if (bb.Contains(ImVec2(x, y))) return w;
  }
  return nullptr;
}

// One finger turns the board: across the short side of the screen is half a turn.
void App::touchOrbit(float x, float y) {
  int ww, wh;
  glfwGetWindowSize(win, &ww, &wh);
  float k = kPI / (float)std::max(200, std::min(ww, wh));
  world.cam.orbit(-(x - touch.lastX) * k, (y - touch.lastY) * k * 0.67f, 0);
  touch.lastX = x;
  touch.lastY = y;
}

// Two fingers: spreading them zooms in, twisting turns the board with them, sliding both down
// raises the camera.
void App::touchPinch() {
  TouchInput& t = touch;
  float dx = t.x[1] - t.x[0], dy = t.y[1] - t.y[0];
  float span = std::hypot(dx, dy), angle = std::atan2(dy, dx), cy = (t.y[0] + t.y[1]) * 0.5f;
  if (t.span > 1 && span > 1) {
    int ww, wh;
    glfwGetWindowSize(win, &ww, &wh);
    float k = kPI / (float)std::max(200, std::min(ww, wh));
    // (screen y points down: a clockwise twist grows the angle, and turns the board clockwise)
    world.cam.orbit(wrapAngle(angle - t.angle), (cy - t.cy) * k * 0.67f, -std::log(span / t.span));
  }
  t.span = span;
  t.angle = angle;
  t.cy = cy;
}

// A finger lifted from the interface: the frames that see it run now, in the browser's touch
// event, so that a button which copies a link or goes full screen may (browsers allow those
// only in answer to the player's touch).
void App::releaseNow() {
  ImGuiContext* g = ImGui::GetCurrentContext();
  auto pending = [g]() {
    for (const ImGuiInputEvent& e : g->InputEventsQueue)
      if (e.Type == ImGuiInputEventType_MouseButton) return true;
    return false;
  };
  for (int i = 0; i < 3 && pending(); ++i) {
    frameDue = 0;
    tick();
  }
}

#ifdef __EMSCRIPTEN__
void App::onTouch(int type, const EmscriptenTouchEvent* e) {
  if (!ImGui::GetCurrentContext() || !win) return;
  using G = TouchInput::G;
  TouchInput& t = touch;
  ImGuiIO& io = ImGui::GetIO();
  t.used = true;
  // the event's points are in CSS pixels; the canvas, the interface and the camera in canvas pixels
  double cw = 0, ch = 0;
  emscripten_get_element_css_size("#canvas", &cw, &ch);
  int ww, wh;
  glfwGetWindowSize(win, &ww, &wh);
  const float r = cw > 0 ? (float)(ww / cw) : 1.0f;
  const float slop = 10.0f * r;  // how far a finger may wander before a tap becomes a drag
  auto followed = [&](long id) {
    for (int i = 0; i < t.n; ++i)
      if (t.id[i] == id) return i;
    return -1;
  };
  for (int k = 0; k < e->numTouches; ++k) {  // where the followed fingers are now
    const EmscriptenTouchPoint& p = e->touches[k];
    int i = followed(p.identifier);
    if (i >= 0) {
      t.x[i] = p.targetX * r;
      t.y[i] = p.targetY * r;
    }
  }

  if (type == EMSCRIPTEN_EVENT_TOUCHSTART) {
    for (int k = 0; k < e->numTouches; ++k) {
      const EmscriptenTouchPoint& p = e->touches[k];
      if (!p.isChanged || t.n >= 2 || followed(p.identifier) >= 0) continue;
      if (t.g == G::UI || t.g == G::UIScroll) continue;  // (the interface: one finger at a time)
      t.id[t.n] = p.identifier;
      t.x[t.n] = p.targetX * r;
      t.y[t.n] = p.targetY * r;
      t.n++;
    }
    if (t.n == 1 && t.g == G::Idle) {
      t.pressX = t.lastX = t.x[0];
      t.pressY = t.lastY = t.y[0];
      ImGuiContext* g = ImGui::GetCurrentContext();
      if (g->OpenPopupStack.Size > 0 || uiWindowAt(t.x[0], t.y[0])) {  // (an open list or question takes every touch)
        t.g = G::UI;
        ImGui_ImplGlfw_CursorEnterCallback(win, GLFW_TRUE);  // (the backend then leaves the mouse where the finger is)
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(t.sentX = t.x[0], t.sentY = t.y[0]);
        io.AddMouseButtonEvent(0, true);
      } else {
        t.g = G::Pending;
      }
    } else if (t.n == 2 && (t.g == G::Pending || t.g == G::Orbit)) {
      // a second finger turns a tap or a turn into pinch, twist and tilt
      t.g = G::Pinch;
      t.span = 0;
      touchPinch();
    }
    return;
  }

  if (type == EMSCRIPTEN_EVENT_TOUCHMOVE) {
    if (t.n == 0) return;
    switch (t.g) {
      case G::Pending:
        if (std::hypot(t.x[0] - t.pressX, t.y[0] - t.pressY) > slop) {
          t.g = G::Orbit;
          touchOrbit(t.x[0], t.y[0]);
        }
        break;
      case G::Orbit:
        touchOrbit(t.x[0], t.y[0]);
        break;
      case G::Pinch:
        if (t.n == 2) touchPinch();
        break;
      case G::UI: {
        // up or down, on a window that scrolls: the finger scrolls it (and presses nothing);
        // sideways it stays the mouse (a slider)
        float dx = t.x[0] - t.pressX, dy = t.y[0] - t.pressY;
        ImGuiWindow* w = static_cast<ImGuiWindow*>(uiWindowAt(t.pressX, t.pressY));
        while (w && w->ScrollMax.y <= 0 && (w->Flags & ImGuiWindowFlags_ChildWindow)) w = w->ParentWindow;
        if (w && w->ScrollMax.y > 0 && std::fabs(dy) > slop && std::fabs(dy) > std::fabs(dx)) {
          t.g = G::UIScroll;
          t.scrollWindow = w;
          t.lastY = t.y[0];
          io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);  // (released away from everything: no click)
          io.AddMouseButtonEvent(0, false);
        } else if (t.x[0] != t.sentX || t.y[0] != t.sentY) {
          io.AddMousePosEvent(t.sentX = t.x[0], t.sentY = t.y[0]);
        }
        break;
      }
      case G::UIScroll: {
        ImGuiWindow* w = static_cast<ImGuiWindow*>(t.scrollWindow);
        float from = w->ScrollTarget.y < FLT_MAX ? w->ScrollTarget.y : w->Scroll.y;  // (moves since the last frame add up)
        ImGui::SetScrollY(w, std::clamp(from - (t.y[0] - t.lastY), 0.0f, w->ScrollMax.y));
        t.lastY = t.y[0];
        break;
      }
      case G::Idle:
        break;
    }
    return;
  }

  // a finger lifted (or the browser took the touch away)
  const bool cancelled = type == EMSCRIPTEN_EVENT_TOUCHCANCEL;
  bool lifted[2] = {false, false};
  for (int k = 0; k < e->numTouches; ++k) {
    const EmscriptenTouchPoint& p = e->touches[k];
    int i = followed(p.identifier);
    if (p.isChanged && i >= 0) lifted[i] = true;
  }
  if (!lifted[0] && !lifted[1]) return;  // (a finger we didn't follow)
  switch (t.g) {
    case G::Pending:  // a tap: where the finger came down is where the player aimed
      if (!cancelled) {
        t.tap = true;
        t.tapX = t.pressX;
        t.tapY = t.pressY;
      }
      break;
    case G::UI:
      // (no needless move first: each one holds the release back a frame)
      if (cancelled) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
      else if (t.x[0] != t.sentX || t.y[0] != t.sentY) io.AddMousePosEvent(t.x[0], t.y[0]);
      io.AddMouseButtonEvent(0, false);
      if (!cancelled) releaseNow();
      io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);  // (a finger lifted hovers over nothing)
      break;
    default:
      break;
  }
  int kept = 0;
  for (int i = 0; i < t.n; ++i) {
    if (lifted[i]) continue;
    t.id[kept] = t.id[i];
    t.x[kept] = t.x[i];
    t.y[kept] = t.y[i];
    kept++;
  }
  t.n = kept;
  if (t.n == 0) {
    t.g = G::Idle;
  } else if (t.g == G::Pinch) {  // one finger left after a pinch goes on turning the board
    t.g = G::Orbit;
    t.lastX = t.x[0];
    t.lastY = t.y[0];
  }
}
#endif

void App::handleInput() {
  ImGuiIO& io = ImGui::GetIO();
  double mx, my;
  glfwGetCursorPos(win, &mx, &my);
  if (touch.used && touch.n == 0 && (mx != lastMX || my != lastMY)) {  // a mouse or trackpad again (an iPad's)
    touch.used = false;
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
  }
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
  const bool finger = io.MouseSource == ImGuiMouseSource_TouchScreen;  // (fingers on the board: onTouch)
  if (finger) {
    leftHeld = false;
  } else if (ImGui::IsMouseClicked(0) && !io.WantCaptureMouse) {
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
    if (ImGui::IsKeyPressed(ImGuiKey_T, false)) requestHint();
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
      music = !music;
      audio.setMusic(music);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) showHelp = !showHelp;
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false) || (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
      setFullscreen(!fullscreen);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) deselect();
#ifndef __EMSCRIPTEN__  // (in a browser Ctrl+Q and F12 belong to the browser)
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) quitRequested = true;
    if (ImGui::IsKeyPressed(ImGuiKey_F12)) {
      char name[64];
      std::snprintf(name, sizeof(name), "living_marble_%d.png", (int)(glfwGetTime() * 10));
      int fw, fh;
      glfwGetFramebufferSize(win, &fw, &fh);
      saveScreenshot(name, fw, fh);
    }
#endif
  }
  hover = -1;
  if (touch.tap) {  // a finger tapped the board
    touch.tap = false;
    mx = touch.tapX;
    my = touch.tapY;
    leftClick = true;
  } else if (touch.used || io.WantCaptureMouse) {
    return;  // (a finger doesn't hover)
  }
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
      world.cheerGhosts(b.side ^ 1, 0.7f, 1.8f);
    }
    if (mode == Mode::TwoPlayer && autoRotate && game.result() == chess::Result::Ongoing)
      world.cam.setSide(b.side);
  }
  chess::Result r = game.result();
  if (r != chess::Result::Ongoing) {
    if (!gameOverShown) {
      gameOverShown = true;
      audio.stopMusic();
      bannerText = chess::resultText(r);
      bannerTime = 1e9f;
      if (r == chess::Result::WhiteMates || r == chess::Result::BlackMates) {
        int loser = r == chess::Result::WhiteMates ? chess::BLACK : chess::WHITE;
        int ks = game.board().kingSq[loser];
        int kid = world.actorAt[ks];
        if (kid >= 0) anims.push_back(makeCheckmateAnim(world, kid, loser ^ 1));
        world.cheerGhosts(loser ^ 1, 1.3f, 8.0f);
        world.droopGhosts(loser, 10.0f);
      } else {
        anims.push_back(makeStalemateAnim(world));
      }
    }
    return;
  }
  if (hintPending && ai.done()) {
    hintPending = false;
    showHint(ai.result());
  }
  if (mode == Mode::Online) {
    playOnlineMoves();
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
  // views narrower than 16:10 (portrait phones and tablets, tall windows) keep the horizontal
  // field of view of a 16:10 one, so the board still fits across (as on the Mac, iPhone and iPad)
  float fovY = glm::radians(cp.fov);
  if (aspect < 1.6f) fovY = 2.0f * std::atan(std::tan(fovY * 0.5f) * 1.6f / aspect);
  proj = glm::perspective(fovY, aspect, sv.nearZ, sv.farZ);
  view = glm::lookAt(cp.pos, cp.target, vec3(0, 1, 0));
  camPos = cp.pos;
  sv.view = view;
  sv.proj = proj;
  sv.camPos = cp.pos;
  sv.focusDist = cp.focus;
  sv.aperture = depthOfField ? cp.aperture : 0.0f;
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
  if (opt.scenario == 4) {
    // ghost gallery: a row of captured pieces on each side
    static const int kinds[7] = {0, 0, 1, 2, 3, 4, 0};
    for (int c = 0; c < 2; ++c)
      for (int k : kinds) world.addGhost(k, c, 0.0f, true);
  }
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
      if (opt.pieces != 0 && army[opt.pieces].ready) {
        uploadArmy(opt.pieces);
        pieceSet = world.pieceSet = opt.pieces;
      }
      if (opt.hasCam) {
        world.cam.yaw = opt.camYaw;
        world.cam.pitch = opt.camPitch;
        world.cam.dist = opt.camDist;
        world.cam.target = glm::vec3(opt.camTX, opt.camTY, opt.camTZ);
      }
      if (opt.noCinematic) {
        world.cinematic = false;
        autoRotate = false;
      }
      if (opt.battle >= 0) setupBattleDemo();
      else if (!opt.shotPath.empty() || !opt.recordDir.empty() || !opt.shotTimes.empty()) {
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
      if (!opt.join.empty()) startOnline(opt.join);  // the page was opened with a friend's link
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

  pollOnline(dt);

  // the medieval army finished building in the background
  for (int s = 1; s < 3; ++s) {
    ArmyBuild& b = army[s];
    if (b.running && b.ready) {
      b.thread.join();
      b.running = false;
      uploadArmy(s);
    }
  }
  if (pendingSet >= 0 && anims.empty() && (pendingSet == 0 || army[pendingSet].uploaded)) applyPieceSet(pendingSet);

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
  world.fidgetAllowed = anims.empty() && !promoPending && opt.battle < 0 && opt.shotPath.empty() && opt.shotTimes.empty();
  world.excludeFidget = selected >= 0 ? world.actorAt[selected] : -1;
  world.simulate(sdt);
  world.cam.evaluate(dt, world.trauma, (float)glfwGetTime());
  if (bannerTime > 0 && bannerTime < 1e8f) bannerTime -= dt;

  // frame the board again when the view turns between landscape and portrait (a phone turned,
  // a window resized); a --camera pose holds until then
  if ((fbH > fbW ? 1 : 0) != tallView) {
    bool first = tallView < 0;
    tallView = fbH > fbW ? 1 : 0;
    if (!(first && opt.hasCam)) {
      world.cam.pitch = homePitch();
      world.cam.dist = homeDist();
    }
  }

  // keep the internal resolution sane on very high-DPI screens (Ultra renders native)
  renderer.setRenderScale(gfxPreset >= 3 ? 1.0f : std::min(1.0f, 1440.0f / std::max(1, fbH)));
  SceneView sv;
  buildScene(sv, fbW, fbH);
  if (!opt.noRender) renderer.render(sv, fbW, fbH, 0);
  if (showUI && !opt.noUI) {
    drawUI();
  } else {
    drawBanner();
    if (opt.touch && !opt.noUI) drawTouchShowButton();  // (with the interface hidden: one button brings it back)
  }
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  frameCount++;

  // ---- capture tools ----
  if (opt.battle >= 0 || !opt.shotTimes.empty()) {
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
    // a staged battle ends with its animation; timed shots of a move wait for every shot
    bool idleQuit = opt.battle >= 0 && idleAfter > (opt.recordDir.empty() ? 0.0f : 1.3f);
    if ((shotsDone && opt.recordDir.empty()) || idleQuit || frameCount > 3000) quitRequested = true;
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
    std::printf("[sim] frame %d ply %d anims %zu chunks %zu particles %zu %s, music: %s\n", frameCount, game.plyCount(),
                anims.size(), world.chunks.size(), world.fx.size(), chess::resultText(game.result()),
                audio.nowPlaying().c_str());
}
