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

struct Options {
  int width = 1600, height = 900;
  int quality = 1;      // mesh/texture quality 0..2
  int gfxPreset = 2;    // 0 low .. 3 ultra
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
  float camYaw = 0, camPitch = 0.8f, camDist = 12.6f;
  float camTX = 0, camTY = 0.15f, camTZ = 0;
  bool noAudio = false;
  bool selftest = false;
  bool noRender = false;
  int level = -1;
  std::string forceMove;
  std::string selectSq;
  std::string loadingShot;        // capture the loading screen           // e.g. e2, pre-select a piece (screenshots)          // e.g. e5d6, played right after setup          // simulate without drawing (stress tests)
  int scenario = 0;               // canned positions for screenshots
  int pieces = 0;                 // 0 Living Marble, 1 medieval, 2 aliens
};

enum class Mode { HumanWhite = 0, HumanBlack, TwoPlayer, Watch };

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
  int gfxPreset = 2;
  float exposure = 1.0f;
  float volume = 0.8f;
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

  void newGame();
  void undo();
  void flipCamera();
  void resetView();
  void setMode(Mode m);
  void applyGraphicsPreset(int p);
  // 0 Living Marble, 1 medieval, 2 aliens (the figure sets are built in the background on first use)
  void setPieceSet(int s);
  int pieceSet = 0;
  int pendingSet = -1;
  bool armyBuilding(int set) const { return set > 0 && set < 3 && army[set].running; }
  float armyProgress(int set) const;
  bool isHumanTurn() const;
  bool animating() const { return !anims.empty(); }
  const char* statusText() const;

 private:
  bool initWindow();
  void initUI();
  void loadAssets();
  void frame(float dtReal);
  void updateGame(float dt);
  void handleInput();
  void commitMove(const chess::Move& m);
  void select(int sq);
  void deselect();
  int pickSquare(double mx, double my, bool piecesFirst) const;
  void buildScene(SceneView& sv, int fbW, int fbH);
  void drawUI();
  void drawBanner();
  void drawLoading();
  void saveScreenshot(const std::string& path, int w, int h);
  void startAIIfNeeded();
  void setupBattleDemo();
  void setupScenario();

  GLFWwindow* win = nullptr;
  std::deque<std::unique_ptr<Anim>> anims;
  meshgen::Library lib;
  meshgen::BuildProgress progress;
  medieval::Library mlib;
  alien::Library alib;
  struct ArmyBuild {  // background build of a figure set (index 1 medieval, 2 aliens)
    meshgen::BuildProgress progress;
    std::thread thread;
    std::atomic<bool> ready{false};
    bool running = false;
    bool uploaded = false;
  } army[3];
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
  bool leftHeld = false, leftDrag = false;
  double lastMX = 0, lastMY = 0, pressMX = 0, pressMY = 0;
  glm::mat4 view{1}, proj{1};
  glm::vec3 camPos{0};
  int frameCount = 0;
  bool fixedStep = false;
  float battleTime = 0;
  size_t nextShot = 0;
  int victimDemo = -1;
};
