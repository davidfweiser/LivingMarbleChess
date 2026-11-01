// Heads-up display (Dear ImGui): status, controls, move list, banners.
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

#include "app.h"
#include "imgui.h"

namespace {
const ImVec4 kGold(0.98f, 0.80f, 0.45f, 1.0f);
const ImU32 kGoldU = IM_COL32(250, 205, 115, 255);

// The name under "Living Marble" (the web page has a name of its own)
#ifdef __EMSCRIPTEN__
const char* const kSubtitle = "3D Fighting Chess";
const char* const kSubtitleSpaced = "3 D   F I G H T I N G   C H E S S";
#else
const char* const kSubtitle = "3D Battle Chess";
const char* const kSubtitleSpaced = "3 D   B A T T L E   C H E S S";
#endif

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
               kSubtitleSpaced, false);
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

// A name for the leaderboard takes letters, digits, _ and -
static int nameChars(ImGuiInputTextCallbackData* d) {
  ImWchar c = d->EventChar;
  bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  return ok ? 0 : 1;
}

// The online part of the panel: the friend's link, or the players and their clocks; offering a
// draw and resigning; how the game ended
void App::drawOnline(float S) {
  const bool match = online.kind == OnlineGame::Kind::Match;
  const float half = (300 * S - 32 * S - 8 * S) * 0.5f;
  const double now = glfwGetTime();
  const ImVec4 kRed(1.0f, 0.45f, 0.35f, 1.0f);
  if (!online.error.empty() || online.net == OnlineGame::Net::Closed) {
    if (ImGui::Button(match ? "Look for an opponent again" : "Start a new online game", ImVec2(-1, 0))) {
      if (match) startMatch();
      else startOnline("");
    }
  } else if (online.needName) {
    static bool focused = false;
    ImGui::TextWrapped("Choose a name for the leaderboard (3 to 16 letters, digits, _ or -):");
    ImGui::SetNextItemWidth(-1);
    if (!focused) ImGui::SetKeyboardFocusHere();
    focused = true;
    bool enter = ImGui::InputText("##name", online.nameBuf, sizeof(online.nameBuf),
                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter, nameChars);
    if (ImGui::Button("Play", ImVec2(-1, 0)) || enter) registerName();
  } else if (match && online.seeking) {
    if (me.known) ImGui::TextDisabled("Playing as %s (%d)", me.name.c_str(), me.rating);
    if (ImGui::Button("Cancel", ImVec2(-1, 0))) setMode(Mode::HumanWhite);
  } else if (match && online.started) {
    // the players and their clocks: the opponent above, this player below
    for (int k = 0; k < 2; ++k) {
      int side = k == 0 ? online.side ^ 1 : online.side;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p = ImGui::GetCursorScreenPos();
      float r = 6.0f * S;
      ImVec2 c(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f + 1);
      dl->AddCircleFilled(c, r, side == chess::WHITE ? IM_COL32(236, 230, 218, 255) : IM_COL32(30, 28, 28, 255));
      dl->AddCircle(c, r, IM_COL32(200, 160, 100, 200), 0, 1.5f);
      ImGui::Dummy(ImVec2(r * 2 + 6 * S, 0));
      ImGui::SameLine();
      ImGui::Text("%s (%d)%s", online.names[side].c_str(), online.ratings[side], side == online.side ? "  - you" : "");
      if (online.clocks) {
        int ms = onlineClock(side);
        char clk[16];
        if (ms < 10000) std::snprintf(clk, sizeof(clk), "%d.%d", ms / 1000, (ms % 1000) / 100);
        else std::snprintf(clk, sizeof(clk), "%d:%02d", ms / 60000, (ms / 1000) % 60);
        bool running = online.clockRun == side && online.endText.empty();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(clk).x);
        ImGui::TextColored(ms < 30000 && running ? kRed : running ? kGold : ImVec4(0.6f, 0.56f, 0.5f, 1.0f), "%s", clk);
      }
    }
    if (online.awaySeconds >= 0 && online.endText.empty()) {
      int left = std::max(0, online.awaySeconds - (int)(now - online.awayAt));
      ImGui::TextWrapped("%s left: they lose in %d s unless they come back.", online.names[online.side ^ 1].c_str(), left);
    }
  } else if (!match && !online.room.empty()) {
    // the game's link, for the friend (and for coming back to the game)
    const char* you = online.side == chess::WHITE ? "White" : "Black";
    if (!online.started) ImGui::TextWrapped("Send this link to a friend. You play %s; the game starts when they open it.", you);
    else ImGui::TextDisabled("You play %s against your friend", you);
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextWrapped("%s", online.link.c_str());
    ImGui::PopStyleColor();
    if (ImGui::Button("Copy link", ImVec2(-1, 0))) copyOnlineLink();
    if (online.copied > 0) ImGui::TextDisabled("%s", online.copyOk ? "Copied" : "Copy it from the address bar instead");
  }

  if (onlineGameOn() && online.net == OnlineGame::Net::Open) {
    if (online.drawOffered) {
      ImGui::TextColored(kGold, "%s offers a draw", match ? online.names[online.side ^ 1].c_str() : "Your friend");
      if (ImGui::Button("Accept draw", ImVec2(half, 0))) offerDraw(true);
      ImGui::SameLine();
      if (ImGui::Button("Decline", ImVec2(half, 0))) declineDraw();
    } else {
      ImGui::BeginDisabled(online.myDrawOffer);
      if (ImGui::Button(online.myDrawOffer ? "Draw offered" : "Offer draw", ImVec2(half, 0))) offerDraw(false);
      ImGui::EndDisabled();
      ImGui::SameLine();
      bool asking = resignArmed();
      if (asking) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.12f, 0.9f));
      if (ImGui::Button(asking ? "Resign? Click" : "Resign", ImVec2(half, 0))) resignOnline();
      if (asking) ImGui::PopStyleColor();
    }
  }
  if (!online.endText.empty()) {
    ImGui::TextColored(kGold, "%s", online.endText.c_str());
    if (!online.ratingText.empty()) ImGui::TextDisabled("%s", online.ratingText.c_str());
    if (match && ImGui::Button("Find another opponent", ImVec2(-1, 0))) startMatch();
  }
  if (match && ImGui::Button("Leaderboard", ImVec2(-1, 0))) openLeaderboard();
  if (noticeTime > 0 && !notice.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kRed);
    ImGui::TextWrapped("%s", notice.c_str());
    ImGui::PopStyleColor();
  }
}

// The best players, and this one
void App::drawLeaderboard(float S) {
  if (!leaders.open) return;
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::Begin("Leaderboard", &leaders.open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
    if (leaders.loading) {
      ImGui::TextDisabled("Loading...");
    } else if (leaders.rows.empty()) {
      ImGui::TextDisabled("Nobody yet: players appear after %d rated games.", leaders.minGames);
    } else if (ImGui::BeginTable("##top", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30 * S);
      ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 170 * S);
      ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthFixed, 64 * S);
      ImGui::TableSetupColumn("Games", ImGuiTableColumnFlags_WidthFixed, 56 * S);
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < leaders.rows.size(); ++i) {
        const Leaderboard::Row& r = leaders.rows[i];
        bool mine = me.known && r.name == me.name;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%d", (int)i + 1);
        ImGui::TableSetColumnIndex(1);
        if (mine) ImGui::TextColored(kGold, "%s", r.name.c_str());
        else ImGui::TextUnformatted(r.name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%d", r.rating);
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled("%d", r.games);
      }
      ImGui::EndTable();
    }
    ImGui::Separator();
    if (!me.known) ImGui::TextDisabled("Play someone online to get a rating.");
    else if (leaders.myRank > 0) ImGui::Text("You: %s, #%d with %d", me.name.c_str(), leaders.myRank, me.rating);
    else
      ImGui::TextDisabled("You: %s, %d - on the board after %d rated games (%d so far)", me.name.c_str(), me.rating,
                          leaders.minGames, me.games);
    if (ImGui::Button("Refresh")) openLeaderboard();
  }
  ImGui::End();
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
  ImGui::TextDisabled("%s", kSubtitle);
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
  ImGui::BeginDisabled(animating() || game.plyCount() == 0 || mode == Mode::Online);
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
  const char* modes[] = {"You play White", "You play Black",       "Two players",
                         "Watch AI vs AI", "Play a friend online", "Play someone online"};
  bool match = mode == Mode::Online && online.kind == OnlineGame::Kind::Match;
  int mi = mode == Mode::Online ? (match ? 5 : 4) : (int)mode, was = mi;
  ImGui::SetNextItemWidth(-90 * S);
  ImGui::BeginDisabled(match && onlineGameOn());  // (a rated game: finish it, or resign)
  if (ImGui::Combo("Mode", &mi, modes, onlineAvailable() ? 6 : 4) && mi != was) {
    if (mi == 4) startOnline("");
    else if (mi == 5) startMatch();
    else setMode((Mode)mi);
    if (mode == Mode::Watch) world.cam.autoSpin = 0.05f;
  }
  ImGui::EndDisabled();
  if (match && onlineGameOn() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Finish the game, or resign, first");
  if (mode == Mode::Online) drawOnline(S);
  const char* levels[5];
  for (int i = 0; i < 5; ++i) levels[i] = chess::kAILevels[i].name;
  ImGui::SetNextItemWidth(-90 * S);
  ImGui::BeginDisabled(mode == Mode::TwoPlayer || mode == Mode::Online);
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
    ImGui::Checkbox("Save power", &savePower);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("30 frames a second while the board is at rest and in battery saver");
    ImGui::SetNextItemWidth(-90 * S);
    ImGui::SliderFloat("Exposure", &exposure, 0.4f, 2.0f, "%.2f");
    if (*paceNote) ImGui::TextDisabled("%.0f fps (%s)", fps, paceNote);
    else ImGui::TextDisabled("%.0f fps", fps);
  }
  ImGui::End();

  drawLeaderboard(S);

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
#ifdef __EMSCRIPTEN__
    ImGui::BulletText("H hide the interface, M music on/off, F11 full screen");
#else
    ImGui::BulletText("H hide the interface, M music on/off, F11 full screen, F12 screenshot, Ctrl+Q quit");
#endif
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
