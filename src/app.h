// The game application: window, game flow, input, UI and capture tools.
#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ai.h"
#include "animation.h"
#include "audio.h"
#include "battle.h"
#include "chess.h"
#include "meshgen.h"
#include "renderer.h"
#include "textures.h"

struct GLFWwindow;
struct ImFont;
struct ImVec2;

#ifndef LM_VERSION  // set from project(VERSION) in CMakeLists.txt
#define LM_VERSION "dev"
#endif

struct Options {
  int width = 1600, height = 900;
#ifdef __EMSCRIPTEN__
  // the web page, like iPhone and iPad: one sculpting level less (it loads quicker) and High graphics
  int quality = 2;      // mesh/texture quality 0..3
  int gfxPreset = 2;    // 0 low .. 3 ultra
#else
  int quality = 3;      // mesh/texture quality 0..3
  int gfxPreset = 3;    // 0 low .. 3 ultra
#endif
  bool fullscreen = false;
  // capture / testing
  std::string shotPath;           // single screenshot then exit
  std::string recordDir;          // frame sequence then exit
  int frames = 40;
  int battle = -1;                // stage a battle demo (attacker kind)
  int style = -1;                 // force one battle style (default: random)
  std::vector<float> shotTimes;   // with --battle: screenshot times
  std::string shotPrefix = "battle";
  std::string fen;
  bool demo = false;              // AI vs AI
  bool noUI = false;
  bool hasCam = false;
  bool noCinematic = false;       // keep the camera still (no battle shots, no turning to the side to move)
  bool dof = false;               // depth of field (off: every piece stays sharp)
  float camYaw = 0, camPitch = 0.8f, camDist = 12.6f;
  float camTX = 0, camTY = 0.15f, camTZ = 0;
  bool noAudio = false;
  bool noMusic = false;
  bool selftest = false;
  bool noRender = false;
  int level = -1;
  std::string forceMove;
  std::string selectSq;
  std::string loadingShot;        // capture the loading screen           // e.g. e2, pre-select a piece (screenshots)          // e.g. e5d6, played right after setup          // simulate without drawing (stress tests)
  int scenario = 0;               // canned positions for screenshots
  int pieces = 0;                 // 0 Living Marble, 1 medieval, 2 aliens, 3 Romans, 4 dinosaurs
  std::string join;               // the web page: join a friend's online game (its code)
  bool lobby = false;             // the web page: the lobby (Play someone online) as soon as the game is ready
  bool touch = false;             // the web page on a touch screen (iPhone, iPad): fingers, and a toolbar
};

// Fingers on the web page's canvas. On the board one finger taps a square or turns the board,
// two pinch to zoom, twist to turn and slide to tilt (as in the iPhone and iPad app); on the
// interface one finger presses its buttons and scrolls its windows.
struct TouchInput {
  enum class G { Idle, Pending, Orbit, Pinch, UI, UIScroll };
  G g = G::Idle;
  int n = 0;                           // the fingers followed (two at most)
  long id[2] = {0, 0};
  float x[2] = {0, 0}, y[2] = {0, 0};  // where they are, in canvas pixels
  float pressX = 0, pressY = 0;        // where the first came down
  float lastX = 0, lastY = 0;          // one finger, at the last move
  float span = 0, angle = 0, cy = 0;   // two fingers, at the last move
  void* scrollWindow = nullptr;        // the interface window a finger scrolls (an ImGuiWindow)
  float sentX = 0, sentY = 0;          // where the interface last heard the finger is
  bool tap = false;                    // a tap on the board, for handleInput ...
  float tapX = 0, tapY = 0;            // ... and where
  bool used = false;                   // the last pointer was a finger (nothing hovers)
};

// Online: against a friend in another browser, through the relay (the web page only)
enum class Mode { HumanWhite = 0, HumanBlack, TwoPlayer, Watch, Online };

// An online game's state (Mode::Online): with a friend (by link), or against another player
// from the page's lobby (rated, with clocks)
struct OnlineGame {
  enum class Net { Connecting, Open, Lost, Closed };
  enum class Kind { Friend, Match };
  Kind kind = Kind::Friend;
  Net net = Net::Connecting;
  std::string room, link;  // the game's code and the link to it
  int side = chess::WHITE; // this player's
  bool started = false;    // the opponent has joined (the game can begin)
  bool peer = false;       // the opponent is here now
  bool rated = false;
  std::string error;       // why the server said no
  std::deque<std::string> moves;  // the opponent's moves ("PLY UCI"), played when the board is at rest
  float copied = 0;        // seconds left of "Copied"
  int copyHow = 0;         // 0 not copied, 1 copied, 2 shared (iPhone and iPad: the share sheet)
  // matched games
  std::string unrated;     // why this one doesn't change ratings ("" if it does)
  std::string names[2];    // by side
  int ratings[2] = {0, 0};
  bool clocks = false;
  int clockMs[2] = {0, 0}; // at clockAt
  int clockRun = -1;       // the side whose clock runs
  double clockAt = 0;
  int awaySeconds = -1;    // the opponent left: seconds until they lose (at awayAt)
  double awayAt = 0;
  bool drawOffered = false;  // by the opponent
  bool myDrawOffer = false;
  double resignAsked = -1;   // "Resign?" asked at (a second click resigns) ...
  int resignFrame = 0;       // ... and in which frame (slow devices: a few frames at least)
  // the end, as the server has it (resignation, time, a draw agreed ...)
  std::string endText, ratingText;
  bool endShown = false;
};

// This browser's player, as the server knows them (names and ratings)
struct OnlinePlayer {
  bool known = false;
  std::string name;
  int rating = 0, games = 0;
};

// The leaderboard window
struct Leaderboard {
  bool open = false, loading = false;
  int minGames = 5, myRank = 0;
  struct Row {
    std::string name;
    int rating, games;
  };
  std::vector<Row> rows;
};

class App {
 public:
  explicit App(const Options& o);
  ~App();
  int run();

  // ---- state used by the UI ----
  Options opt;
  Mode mode = Mode::HumanWhite;
  int aiLevel = 2;
  bool battles = true;
  bool autoRotate = true;
  bool showUI = true;
  bool showHelp = false;
  bool fullscreen = false;
  int gfxPreset = 3;
  float exposure = 1.0f;
  bool depthOfField = false;  // blur what the camera is not focused on (High and Ultra only)
  bool savePower = true;      // fewer frames while the board is at rest and in battery saver
  const char* paceNote = "";  // why the frame rate is lowered ("at rest", "battery saver"), or ""
  float volume = 0.8f;
  bool music = true;         // the background music
  float musicVolume = 0.7f;
  float fps = 0;
  chess::Game game;
  World world;
  Renderer renderer;
  Audio audio;
  chess::AIEngine ai;
  bool loading = true;
  float loadProgress = 0;
  std::string loadStage;
  std::string bannerText;
  float bannerTime = 0;
  bool promoPending = false;
  int promoFrom = -1, promoTo = -1;
  bool gameOverShown = false;
  ImFont* fontUI = nullptr;
  ImFont* fontTitle = nullptr;
  ImFont* fontBanner = nullptr;

  void newGame(bool tellFriend = true);
  bool canStartNew() const;  // whether New would start a game now (not during a rated game, or while it connects)
  void undo();
  // the web page's Home page button: back to the front page, with the game kept as it is (the
  // front page's "Back to the game" comes back to it). Not during a rated game.
  bool canGoHome() const;
  void goHome();
  void backFromHome();
  bool atHome = false;  // the front page or the lobby is over the game, which waits (paused, silent)
  // online games (the web page): a new one (this player is White) or a friend's by its code
  bool onlineAvailable() const;
  void startOnline(const std::string& code);
  // the web page's lobby (Play someone online): the players there, and rated games against them.
  // The game waits behind it as for the front page; enterMatch() is a game the lobby started.
  void openLobby();
  void enterMatch();
  bool pendingMatch = false;  // (a rated game started while the game was loading: entered once it's ready)
  void pause();               // (the front page or the lobby over the game)
  void leaveOnline();
  void copyOnlineLink();
  void resignOnline();
  void offerDraw(bool accept);
  void declineDraw();
  void openLeaderboard();
  bool onlineGameOn() const;  // a game against someone is going on (not over)
  int onlineClock(int side) const;  // milliseconds left
  bool resignArmed() const;  // "Resign?": the next click resigns
  const char* onlineEndText() const { return online.endText.c_str(); }
  OnlineGame online;
  OnlinePlayer me;
  Leaderboard leaders;
  std::string notice;  // a passing message from the server (a name taken ...)
  float noticeTime = 0;
  void flipCamera();
  void resetView();
  void setFullscreen(bool on);
  void setMode(Mode m);
  void applyGraphicsPreset(int p);
  // 0 Living Marble, 1 medieval, 2 aliens, 3 Romans, 4 dinosaurs (the figure sets are built in the background on first use)
  void setPieceSet(int s);
  int pieceSet = 0;
  int pendingSet = -1;
  bool armyBuilding(int set) const { return set > 0 && set < 5 && army[set].running; }
  float armyProgress(int set) const;
  bool isHumanTurn() const;
  bool animating() const { return !anims.empty(); }
  // A hint: the AI looks for the best move for the side to move and selects it.
  bool canHint() const;
  void requestHint();
  bool hintPending = false;
  const char* statusText() const;
  // the touch interface (the web page on iPhone and iPad)
  bool showMenu = false, showMoves = false;
  bool fullscreenAvailable = true;  // (an iPhone's browser has no full screen for pages)
  float bannerTop = 0;              // banners stay below this (the touch interface's status)
#ifdef __EMSCRIPTEN__
  void onTouch(int type, const struct EmscriptenTouchEvent* e);
#endif

 private:
  bool initWindow();
  void initUI();
  void loadAssets();
  bool tick();  // one pass of the main loop
  double prevTime = 0;
  void frame(float dtReal);
  void pace(float dt, double frameStart);
  void updateGame(float dt);
  void handleInput();
  void commitMove(const chess::Move& m);
  void showHint(const chess::Move& m);
  void cancelHint();
  void select(int sq);
  void deselect();
  int pickSquare(double mx, double my, bool piecesFirst) const;
  void buildScene(SceneView& sv, int fbW, int fbH);
  void drawUI();
  void drawTouchUI();
  void drawTouchShowButton();
  void drawSettings(float S);
  void drawHomeButton();
  void drawMoves(const ImVec2& pos, const ImVec2& pivot, float width, float maxHeight, float S);
  void drawHelp(float S);
  void drawClocks(float S);
  void drawOnline(float S);
  void drawLeaderboard(float S);
  void drawBanner();
  void drawLoading();
  void saveScreenshot(const std::string& path, int w, int h);
  void startAIIfNeeded();
  void pollOnline(float dt);
  void playOnlineMoves();
  void syncOnline(const std::vector<std::string>& uci);
  void rebuildWorld();
  void setupBattleDemo();
  void setupScenario();

  GLFWwindow* win = nullptr;
  int winX = 0, winY = 0, winW = 0, winH = 0;  // the windowed placement to return to from full screen
  std::deque<std::unique_ptr<Anim>> anims;
  meshgen::Library lib;
  meshgen::BuildProgress progress;
  medieval::Library mlib;
  alien::Library alib;
  roman::Library rlib;
  dino::Library dlib;
  struct ArmyBuild {  // background build of a figure set (index 1 medieval, 2 aliens, 3 Romans, 4 dinosaurs)
    meshgen::BuildProgress progress;
    std::thread thread;
    std::atomic<bool> ready{false};
    bool running = false;
    bool uploaded = false;
  } army[5];
  void startArmyBuild(int set);
  void buildArmy(int set, meshgen::BuildProgress* progress);
  void uploadArmy(int set);
  void applyPieceSet(int s);
  tex::Image boardAlbedo, boardDetail;
  std::thread loader;
  std::atomic<bool> assetsReady{false};
  bool announcePending = false;
  double scrollAccum = 0;
  bool quitRequested = false;
  bool assetsUploaded = false;
  int selected = -1;
  std::vector<chess::Move> selMoves;
  int hover = -1;
  chess::Move lastMove;
  bool hasLastMove = false;
  bool aiPending = false;
  float aiDelay = 0;
  bool checkAnnounced = false;
  bool rightDrag = false;
  TouchInput touch;
  bool askNew = false;  // the touch toolbar's New: ask before a game in progress ends
  // the camera at rest: tall (portrait) views look down more steeply from closer in, so the
  // board fills their width (as on the iPhone and iPad)
  int tallView = -1;  // the view is portrait-shaped (1) or not (0); -1 before the first frame
  float homePitch() const { return tallView == 1 ? 1.1f : 0.80f; }
  float homeDist() const { return tallView == 1 ? 9.8f : 12.6f; }
  void* uiWindowAt(float x, float y) const;  // the interface window there (an ImGuiWindow), or none
  void touchOrbit(float x, float y);
  void touchPinch();
  void releaseNow();
  bool leftHeld = false, leftDrag = false;
  double lastMX = 0, lastMY = 0, pressMX = 0, pressMY = 0;
  glm::mat4 view{1}, proj{1};
  glm::vec3 camPos{0};
  int frameCount = 0;
  bool fixedStep = false;
  float battleTime = 0;
  size_t nextShot = 0;
  int victimDemo = -1;
  // frame rate (pace())
  float restTime = 0;          // seconds the board has been at rest
  CamPose pacedCam;            // the camera, cursor and selection at the last frame
  double pacedMX = -1, pacedMY = -1;
  int pacedSelected = -1;
  float powerCheck = 0;        // seconds to the next look at the power state and the display
  bool powerSaver = false;
  int refreshHz = 60;
  int vsyncs = 1;              // display refreshes per frame (2 or more: a lowered rate)
  bool intervalsWork = true;   // the driver keeps to swap intervals above 1 ...
  int slowFrames = 0;          // ... (checked over the first frames at a lowered rate)
  double slowSince = 0;
  double frameDue = 0;         // ... if it doesn't: when the next frame is due
};
