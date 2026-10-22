// Heads-up display (Dear ImGui): status, controls, move list, banners.
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>

#include "app.h"
#include "imgui.h"

namespace {
const ImVec4 kGold(0.98f, 0.80f, 0.45f, 1.0f);
const ImU32 kGoldU = IM_COL32(250, 205, 115, 255);

void textCentered(ImDrawList* dl, ImFont* f, float size, ImVec2 center, ImU32 col, const char* t, bool shadow) {
  ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, 0.0f, t);
  ImVec2 p(center.x - sz.x * 0.5f, center.y - sz.y * 0.5f);
  if (shadow) {
    for (int i = 1; i <= 3; ++i)
      dl->AddText(f, size, ImVec2(p.x + i * 0.7f, p.y + i * 0.9f), IM_COL32(0, 0, 0, 90 - i * 20), t);
  }
  dl->AddText(f, size, p, col, t);
}
}  // namespace

void App::drawLoading() {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImVec2 c(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  float gs = io.FontGlobalScale;
  textCentered(dl, fontBanner, fontBanner->FontSize * gs, ImVec2(c.x, c.y - 70), IM_COL32(240, 226, 200, 255),
               "Living Marble", true);
  textCentered(dl, fontTitle, fontTitle->FontSize * gs * 0.7f, ImVec2(c.x, c.y - 18), IM_COL32(200, 170, 120, 255),
               "3 D   B A T T L E   C H E S S", false);
  int total = std::max(1, progress.total.load() + 1);
  int done = progress.done.load();
  float target = (float)done / total;
  loadProgress += (target - loadProgress) * 0.15f;
  float w = 380.0f * ImGui::GetFontSize() / 16.0f, h = 4.0f;
  ImVec2 p0(c.x - w * 0.5f, c.y + 30), p1(c.x + w * 0.5f, c.y + 30 + h);
  dl->AddRectFilled(p0, p1, IM_COL32(60, 48, 36, 255), 2.0f);
  dl->AddRectFilled(p0, ImVec2(p0.x + w * loadProgress, p1.y), kGoldU, 2.0f);
  const char* st = progress.stage.load();
  std::string stage = (st && *st) ? std::string(st) : std::string("Quarrying marble");
  stage += "...";
  textCentered(dl, fontUI, fontUI->FontSize * gs, ImVec2(c.x, c.y + 58), IM_COL32(170, 150, 120, 255), stage.c_str(),
               false);
}

void App::drawBanner() {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  float gs = io.FontGlobalScale;
  if (!bannerText.empty() && bannerTime > 0) {
    float a = std::min(1.0f, bannerTime);
    bool permanent = bannerTime > 1e8f;
    float size = fontBanner->FontSize * gs * (permanent ? 0.85f : 1.0f);
    ImU32 col = IM_COL32(250, 220, 160, (int)(255 * a));
    textCentered(fg, fontBanner, size, ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.14f), col,
                 bannerText.c_str(), true);
    if (permanent && showUI && !opt.noUI) {
      textCentered(fg, fontUI, fontUI->FontSize * gs, ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.14f + size * 0.75f),
                   IM_COL32(220, 200, 160, 220), "Press N or 'New game' to play again", false);
    }
  }
}

void App::drawUI() {
  ImGuiIO& io = ImGui::GetIO();
  const float S = ImGui::GetFontSize() / 16.0f;
  const float pad = 18.0f * S;
  const ImGuiWindowFlags panel = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

  // ---------------- left panel ----------------
  ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(300 * S, 0), ImGuiCond_Always);
  ImGui::Begin("##main", nullptr, panel | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PushFont(fontTitle);
  ImGui::TextColored(kGold, "Living Marble");
  ImGui::PopFont();
  ImGui::TextDisabled("3D Battle Chess");
  {
    const char* ver = "v" LM_VERSION;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(ver).x);
    ImGui::TextDisabled("%s", ver);
  }
  ImGui::Separator();
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = 6.0f * S;
    bool white = game.board().side == chess::WHITE;
    dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f + 1), r,
                        white ? IM_COL32(236, 230, 218, 255) : IM_COL32(30, 28, 28, 255));
    dl->AddCircle(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f + 1), r, IM_COL32(200, 160, 100, 200), 0,
                  1.5f);
    ImGui::Dummy(ImVec2(r * 2 + 6 * S, 0));
    ImGui::SameLine();
    ImGui::TextWrapped("%s", statusText());
  }
  ImGui::Spacing();
  float bw = (300 * S - 32 * S - 16 * S) / 3.0f;
  if (ImGui::Button("New game", ImVec2(bw, 0))) newGame();
  ImGui::SameLine();
  ImGui::BeginDisabled(animating() || game.plyCount() == 0);
  if (ImGui::Button("Undo", ImVec2(bw, 0))) undo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Flip view", ImVec2(bw, 0))) flipCamera();
  ImGui::BeginDisabled(!canHint());
  if (ImGui::Button("Hint (T)", ImVec2(-1, 0))) requestHint();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Selects the best move for you: click the glowing square to play it");
  ImGui::Spacing();
  const char* modes[] = {"You play White", "You play Black", "Two players", "Watch AI vs AI"};
  int mi = (int)mode;
  ImGui::SetNextItemWidth(-90 * S);
  if (ImGui::Combo("Mode", &mi, modes, 4)) {
    setMode((Mode)mi);
    if (mode == Mode::Watch) world.cam.autoSpin = 0.05f;
  }
  const char* levels[5];
  for (int i = 0; i < 5; ++i) levels[i] = chess::kAILevels[i].name;
  ImGui::SetNextItemWidth(-90 * S);
  ImGui::BeginDisabled(mode == Mode::TwoPlayer);
  ImGui::Combo("Opponent", &aiLevel, levels, 5);
  ImGui::EndDisabled();
  {
    const char* sets[] = {"Living Marble", "Medieval", "Aliens"};
    int ps = pendingSet >= 0 ? pendingSet : pieceSet;
    ImGui::SetNextItemWidth(-90 * S);
    if (ImGui::Combo("Pieces", &ps, sets, 3)) setPieceSet(ps);
    if (armyBuilding(1)) ImGui::TextDisabled("Raising the medieval army... %d%%", (int)(armyProgress(1) * 100));
    if (armyBuilding(2)) ImGui::TextDisabled("The aliens are landing... %d%%", (int)(armyProgress(2) * 100));
  }
  ImGui::Checkbox("Battle animations", &battles);
  ImGui::Checkbox("Cinematic camera", &world.cinematic);
  if (mode == Mode::TwoPlayer) ImGui::Checkbox("Turn board each move", &autoRotate);
  if (mode == Mode::Watch) {
    bool spin = world.cam.autoSpin != 0;
    if (ImGui::Checkbox("Slow orbit", &spin)) world.cam.autoSpin = spin ? 0.05f : 0.0f;
  }
  if (audio.ok()) {
    ImGui::SetNextItemWidth(-90 * S);
    if (ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f")) audio.setVolume(volume);
    ImGui::BeginDisabled(!music);
    ImGui::SetNextItemWidth(-90 * S);
    if (ImGui::SliderFloat("Music", &musicVolume, 0.0f, 1.0f, "%.2f")) audio.setMusicVolume(musicVolume);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The music's own volume");
    if (ImGui::Checkbox("Play music (M)", &music)) audio.setMusic(music);
    std::string playing = music ? audio.nowPlaying() : std::string();
    if (!playing.empty()) ImGui::TextDisabled("Playing: %s", playing.c_str());
  }
  {
    bool fs = fullscreen;
    if (ImGui::Checkbox("Full screen (F11)", &fs)) setFullscreen(fs);
  }
  if (ImGui::CollapsingHeader("Graphics")) {
    const char* presets[] = {"Low", "Medium", "High", "Ultra"};
    int gp = gfxPreset;
    ImGui::SetNextItemWidth(-90 * S);
    if (ImGui::Combo("Quality", &gp, presets, 4)) applyGraphicsPreset(gp);
    ImGui::BeginDisabled(!renderer.settings().dof);
    ImGui::Checkbox("Depth of field (blur)", &depthOfField);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(-90 * S);
    ImGui::SliderFloat("Exposure", &exposure, 0.4f, 2.0f, "%.2f");
    ImGui::TextDisabled("%.0f fps", fps);
  }
  ImGui::End();

  // ---------------- move list ----------------
  const auto& san = game.san();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, pad), ImGuiCond_Always, ImVec2(1, 0));
  float rows = (float)((san.size() + 1) / 2);
  float listH = std::max(1.0f, rows) * ImGui::GetTextLineHeightWithSpacing() + 70 * S;
  ImGui::SetNextWindowSize(ImVec2(236 * S, std::min(std::min(io.DisplaySize.y * 0.62f, 480 * S), listH)), ImGuiCond_Always);
  ImGui::Begin("##moves", nullptr, panel);
  ImGui::TextColored(kGold, "Moves");
  {
    static const int val[7] = {0, 1, 3, 3, 5, 9, 0};
    int mat[2] = {0, 0};
    for (int s = 0; s < 64; ++s) {
      int p = game.board().sq[s];
      if (p) mat[chess::pieceColor(p)] += val[chess::pieceType(p)];
    }
    int d = mat[0] - mat[1];
    ImGui::SameLine();
    if (d != 0) ImGui::TextDisabled("  %s +%d", d > 0 ? "White" : "Black", std::abs(d));
    else ImGui::TextDisabled("  even");
  }
  ImGui::Separator();
  ImGui::BeginChild("##list", ImVec2(0, 0), false);
  static size_t lastCount = 0;
  if (ImGui::BeginTable("##mt", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 30 * S);
    ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch);
    for (size_t i = 0; i < san.size(); i += 2) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextDisabled("%d.", (int)(i / 2 + 1));
      ImGui::TableSetColumnIndex(1);
      bool lastW = i + 1 == san.size();
      if (lastW) ImGui::TextColored(kGold, "%s", san[i].c_str());
      else ImGui::TextUnformatted(san[i].c_str());
      if (i + 1 < san.size()) {
        ImGui::TableSetColumnIndex(2);
        bool lastB = i + 2 == san.size();
        if (lastB) ImGui::TextColored(kGold, "%s", san[i + 1].c_str());
        else ImGui::TextUnformatted(san[i + 1].c_str());
      }
    }
    ImGui::EndTable();
  }
  if (san.size() != lastCount) {
    ImGui::SetScrollHereY(1.0f);
    lastCount = san.size();
  }
  ImGui::EndChild();
  ImGui::End();

  drawBanner();
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  float gs = io.FontGlobalScale;
  // ---------------- hint line ----------------
  const char* hint = "Click: select & move    Drag: rotate board    Scroll: zoom    T: hint    R: reset view    F1: help    H: hide UI    F11: full screen";
  fg->AddText(fontUI, fontUI->FontSize * gs * 0.9f, ImVec2(pad, io.DisplaySize.y - pad - fontUI->FontSize * gs),
              IM_COL32(220, 200, 170, 140), hint);

  if (showHelp) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Help", &showHelp, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored(kGold, "Controls");
    ImGui::BulletText("Left-click a piece, then a glowing square to move");
    ImGui::BulletText("Drag (any button) / arrow keys: rotate the board - even look underneath");
    ImGui::BulletText("Mouse wheel: zoom");
    ImGui::BulletText("N new game, U undo, T hint, F flip view, R reset view");
    ImGui::BulletText("A hint selects the best move for you: click the glowing square to play it");
    ImGui::BulletText("B toggle battle animations, C toggle cinematic camera");
    ImGui::BulletText("H hide the interface, M music on/off, F11 full screen, F12 screenshot, Ctrl+Q quit");
    ImGui::Spacing();
    ImGui::TextColored(kGold, "The battles (each capture picks one at random)");
    static const char* const kPieces[6] = {"Pawn", "Knight", "Bishop", "Rook", "Queen", "King"};
    for (int k = 0; k < 6; ++k)
      ImGui::BulletText("%s: %s, %s, %s", kPieces[k], battleStyleName(k, 0, pieceSet), battleStyleName(k, 1, pieceSet),
                        battleStyleName(k, 2, pieceSet));
    ImGui::Spacing();
    ImGui::TextDisabled("Living Marble %s", LM_VERSION);
    ImGui::End();
  }

  // ---------------- promotion ----------------
  if (promoPending) ImGui::OpenPopup("Promote pawn");
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Promote pawn", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::TextColored(kGold, "Your pawn reached the last rank!");
    ImGui::Text("Choose what it becomes:");
    const char* names[4] = {"Queen", "Rook", "Bishop", "Knight"};
    const int types[4] = {chess::QUEEN, chess::ROOK, chess::BISHOP, chess::KNIGHT};
    for (int i = 0; i < 4; ++i) {
      if (i) ImGui::SameLine();
      if (ImGui::Button(names[i], ImVec2(86 * S, 40 * S))) {
        chess::Move m;
        if (chess::findLegal(game.board(), promoFrom, promoTo, types[i], m)) commitMove(m);
        promoPending = false;
        ImGui::CloseCurrentPopup();
      }
    }
    if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
      promoPending = false;
      deselect();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
