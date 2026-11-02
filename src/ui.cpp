// Heads-up display (Dear ImGui): status, controls, move list, banners.
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

#include "app.h"
#include "imgui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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

const ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

// A list to choose from (ImGui::Combo); on touch screens its choices are finger-sized
bool pick(const char* label, int* current, const char* const items[], int count, bool touch) {
  if (!touch) return ImGui::Combo(label, current, items, count);
  bool changed = false;
  // (tall enough for every choice, as far as the screen goes)
  const ImGuiStyle& st = ImGui::GetStyle();
  const float h = ImGui::GetFontSize() * 2.4f;
  float all = count * h + (count - 1) * st.ItemSpacing.y + 2 * st.WindowPadding.y;
  ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::CalcItemWidth(), 0),
                                      ImVec2(FLT_MAX, std::min(all, ImGui::GetIO().DisplaySize.y * 0.7f)));
  if (ImGui::BeginCombo(label, items[*current])) {
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    for (int i = 0; i < count; ++i) {
      bool selected = i == *current;
      if (ImGui::Selectable(items[i], selected, 0, ImVec2(0, h))) {
        *current = i;
        changed = true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::PopStyleVar();
    ImGui::EndCombo();
  }
  return changed;
}

// A text's size, made smaller if it would be wider than maxWidth (a phone's screen)
float fitted(ImFont* f, float size, const char* t, float maxWidth) {
  float w = f->CalcTextSizeA(size, FLT_MAX, 0.0f, t).x;
  return w > maxWidth && w > 0 ? size * maxWidth / w : size;
}

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
  float u = ImGui::GetFontSize() / 16.0f;  // the interface's scale: the spacing grows with the text (high-DPI screens)
  const float fit = io.DisplaySize.x * 0.9f;  // (a phone: the titles shrink to fit)
  textCentered(dl, fontBanner, fitted(fontBanner, fontBanner->FontSize * gs, "Living Marble", fit), ImVec2(c.x, c.y - 70 * u),
               IM_COL32(240, 226, 200, 255), "Living Marble", true);
  textCentered(dl, fontTitle, fitted(fontTitle, fontTitle->FontSize * gs * 0.7f, kSubtitleSpaced, fit), ImVec2(c.x, c.y - 18 * u),
               IM_COL32(200, 170, 120, 255), kSubtitleSpaced, false);
  int total = std::max(1, progress.total.load() + 1);
  int done = progress.done.load();
  float target = (float)done / total;
  loadProgress += (target - loadProgress) * 0.15f;
  float w = std::min(380.0f * u, io.DisplaySize.x * 0.8f), h = 4.0f * u;
  ImVec2 p0(c.x - w * 0.5f, c.y + 30 * u), p1(c.x + w * 0.5f, c.y + 30 * u + h);
  dl->AddRectFilled(p0, p1, IM_COL32(60, 48, 36, 255), 2.0f);
  dl->AddRectFilled(p0, ImVec2(p0.x + w * loadProgress, p1.y), kGoldU, 2.0f);
  const char* st = progress.stage.load();
  std::string stage = (st && *st) ? std::string(st) : std::string("Quarrying marble");
  stage += "...";
  textCentered(dl, fontUI, fontUI->FontSize * gs, ImVec2(c.x, c.y + 58 * u), IM_COL32(170, 150, 120, 255), stage.c_str(),
               false);
}

void App::drawBanner() {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  float gs = io.FontGlobalScale;
  if (!bannerText.empty() && bannerTime > 0) {
    float a = std::min(1.0f, bannerTime);
    bool permanent = bannerTime > 1e8f;
    float size = fitted(fontBanner, fontBanner->FontSize * gs * (permanent ? 0.85f : 1.0f), bannerText.c_str(),
                        io.DisplaySize.x * 0.94f);
    ImU32 col = IM_COL32(250, 220, 160, (int)(255 * a));
    // (the touch interface: below the status bar, as in the iPhone and iPad app)
    const float S = ImGui::GetFontSize() / 16.0f;
    bool roomy = std::min(io.DisplaySize.x, io.DisplaySize.y) >= 600.0f * S;
    float y = io.DisplaySize.y * (!opt.touch || roomy ? 0.14f : io.DisplaySize.x > io.DisplaySize.y ? 0.24f : 0.2f);
    if (opt.touch && showUI && !opt.noUI) y = std::max(y, bannerTop + size * 0.62f);  // (clear of a tall status)
    textCentered(fg, fontBanner, size, ImVec2(io.DisplaySize.x * 0.5f, y), col, bannerText.c_str(), true);
    if (permanent && showUI && !opt.noUI) {
      textCentered(fg, fontUI, fontUI->FontSize * gs, ImVec2(io.DisplaySize.x * 0.5f, y + size * 0.75f),
                   IM_COL32(220, 200, 160, 220), opt.touch ? "Tap New to play again" : "Press N or 'New game' to play again",
                   false);
    }
  }
}

// The online part of the panel: the friend's link, or the players and their clocks; offering a
// draw and resigning; how the game ended
void App::drawOnline(float S) {
  const bool match = online.kind == OnlineGame::Kind::Match;
  const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  const double now = glfwGetTime();
  const ImVec4 kRed(1.0f, 0.45f, 0.35f, 1.0f);
  if (!online.error.empty() || online.net == OnlineGame::Net::Closed) {
    if (ImGui::Button(match ? "Find an opponent" : "Start a new online game", ImVec2(-1, 0))) {
      if (match) openLobby();
      else startOnline("");
    }
  } else if (match && online.started) {
    drawClocks(S);
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
    if (ImGui::Button(opt.touch ? "Share link" : "Copy link", ImVec2(-1, 0))) copyOnlineLink();
    if (online.copied > 0)
      ImGui::TextDisabled("%s", online.copyHow == 2 ? "Shared" : online.copyHow ? "Copied" : "Copy it from the address bar instead");
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
    if (match && ImGui::Button("Find another opponent", ImVec2(-1, 0))) openLobby();
  }
  if (match && ImGui::Button("Leaderboard", ImVec2(-1, 0))) openLeaderboard();
  if (noticeTime > 0 && !notice.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, kRed);
    ImGui::TextWrapped("%s", notice.c_str());
    ImGui::PopStyleColor();
  }
}

// The players of a rated game and their clocks: the opponent above, this player below
void App::drawClocks(float S) {
  const ImVec4 kRed(1.0f, 0.45f, 0.35f, 1.0f);
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
      ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, io.DisplaySize.x < 480 * S ? 120 * S : 170 * S);
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
    if (!me.known) ImGui::TextDisabled("Log in on the front page to get a rating.");
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
  if (opt.touch) {
    drawTouchUI();
  } else {
    const float pad = 18.0f * S;

    // ---------------- left panel ----------------
    ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300 * S, 0), ImGuiCond_Always);
    // (in a short window it stops above the hint line, and scrolls)
    ImGui::SetNextWindowSizeConstraints(ImVec2(300 * S, 0),
                                        ImVec2(300 * S, std::max(200 * S, io.DisplaySize.y - 2 * pad - 24 * S)));
    ImGui::Begin("##main", nullptr, kPanel | ImGuiWindowFlags_AlwaysAutoResize);
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
    ImGui::BeginDisabled(!canStartNew());
    if (ImGui::Button("New game", ImVec2(bw, 0))) newGame();
    ImGui::EndDisabled();
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
      ImGui::SetTooltip("%s", mode == Mode::TwoPlayer || mode == Mode::Online
                                  ? "No hints against another person"
                                  : "Selects the best move for you: click the glowing square to play it");
    ImGui::Spacing();
    drawSettings(S);
#ifdef __EMSCRIPTEN__
    ImGui::Spacing();
    drawHomeButton();
#endif
    ImGui::End();

    drawLeaderboard(S);
    drawMoves(ImVec2(io.DisplaySize.x - pad, pad), ImVec2(1, 0), 236 * S, std::min(io.DisplaySize.y * 0.62f, 480 * S), S);
    drawBanner();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float gs = io.FontGlobalScale;
    // ---------------- hint line ----------------
    const char* hint = "Click: select & move    Drag: rotate board    Scroll: zoom    T: hint    R: reset view    F1: help    H: hide UI    F11: full screen";
    fg->AddText(fontUI, fontUI->FontSize * gs * 0.9f, ImVec2(pad, io.DisplaySize.y - pad - fontUI->FontSize * gs),
                IM_COL32(220, 200, 170, 140), hint);
  }

  if (showHelp) drawHelp(S);

  // ---------------- promotion ----------------
  if (promoPending) ImGui::OpenPopup("Promote pawn");
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Promote pawn", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::TextColored(kGold, "Your pawn reached the last rank!");
    ImGui::Text("Choose what it becomes:");
    const char* names[4] = {"Queen", "Rook", "Bishop", "Knight"};
    const int types[4] = {chess::QUEEN, chess::ROOK, chess::BISHOP, chess::KNIGHT};
    const bool narrow = io.DisplaySize.x < 480 * S;  // (a phone: two by two)
    const float bw = narrow ? 120 * S : 86 * S;
    for (int i = 0; i < 4; ++i) {
      if (i && !(narrow && i == 2)) ImGui::SameLine();
      if (ImGui::Button(names[i], ImVec2(bw, 40 * S))) {
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

// The modes, the opponent, the pieces, the sound and the graphics: the left panel's (the
// Menu, on touch screens)
void App::drawSettings(float S) {
  const char* modes[] = {"You play White", "You play Black",       "Two players",
                         "Watch AI vs AI", "Play a friend online", "Play someone online"};
  bool match = mode == Mode::Online && online.kind == OnlineGame::Kind::Match;
  int mi = mode == Mode::Online ? (match ? 5 : 4) : (int)mode, was = mi;
  ImGui::SetNextItemWidth(-90 * S);
  ImGui::BeginDisabled(match && onlineGameOn());  // (a rated game: finish it, or resign)
  if (pick("Mode", &mi, modes, onlineAvailable() ? 6 : 4, opt.touch) && mi != was) {
    if (mi == 4) startOnline("");
    else if (mi == 5) openLobby();  // (the players online: the mode changes once a game starts)
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
  pick("Opponent", &aiLevel, levels, 5, opt.touch);
  ImGui::EndDisabled();
  {
    const char* sets[] = {"Living Marble", "Medieval", "Aliens"};
    int ps = pendingSet >= 0 ? pendingSet : pieceSet;
    ImGui::SetNextItemWidth(-90 * S);
    if (pick("Pieces", &ps, sets, 3, opt.touch)) setPieceSet(ps);
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
    if (ImGui::Checkbox(opt.touch ? "Play music" : "Play music (M)", &music)) audio.setMusic(music);
    std::string playing = music ? audio.nowPlaying() : std::string();
    if (!playing.empty()) ImGui::TextDisabled("Playing: %s", playing.c_str());
  }
  if (fullscreenAvailable) {  // (not on an iPhone: Add to Home Screen instead)
    bool fs = fullscreen;
    if (ImGui::Checkbox(opt.touch ? "Full screen" : "Full screen (F11)", &fs)) setFullscreen(fs);
  }
  if (ImGui::CollapsingHeader("Graphics")) {
    const char* presets[] = {"Low", "Medium", "High", "Ultra"};
    int gp = gfxPreset;
    ImGui::SetNextItemWidth(-90 * S);
    if (pick("Quality", &gp, presets, 4, opt.touch)) applyGraphicsPreset(gp);
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
}

// The web page's way back to its front page (the game waits there as it is)
void App::drawHomeButton() {
#ifdef __EMSCRIPTEN__
  const bool can = canGoHome();
  ImGui::BeginDisabled(!can);
  if (ImGui::Button("Home page", ImVec2(-1, 0))) goHome();
  ImGui::EndDisabled();
  if (!can) ImGui::TextDisabled("Finish the game, or resign, to leave it");
  else if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the front page; the game waits for you there");
#endif
}

// The moves so far, in a window at pos (pivot: which of its corners that is)
void App::drawMoves(const ImVec2& pos, const ImVec2& pivot, float width, float maxHeight, float S) {
  const auto& san = game.san();
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
  float rows = (float)((san.size() + 1) / 2);
  float listH = std::max(1.0f, rows) * ImGui::GetTextLineHeightWithSpacing() + 70 * S;
  ImGui::SetNextWindowSize(ImVec2(width, std::max(80 * S, std::min(maxHeight, listH))), ImGuiCond_Always);
  ImGui::Begin("##moves", nullptr, kPanel);
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
}

void App::drawHelp(float S) {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (opt.touch) {  // (no wider than the screen, and it scrolls)
    float w = std::min(io.DisplaySize.x - 16 * S, 560 * S);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, io.DisplaySize.y - 16 * S));
  }
  ImGui::Begin("Help", &showHelp, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
  ImGui::TextColored(kGold, "Controls");
  if (opt.touch) {
    auto bullet = [](const char* t) {
      ImGui::Bullet();
      ImGui::TextWrapped("%s", t);
    };
    bullet("Tap a piece, then a glowing square to move it");
    bullet("Drag with one finger: turn the board - even look underneath");
    bullet("Two fingers: pinch to zoom, twist to turn the board, slide up or down to tilt");
    bullet("Hint selects the best move for you: tap the glowing square to play it");
    bullet("New starts a new game (it asks first if a game is going on)");
    bullet("Menu: the mode (online games too), the opponent, the pieces, the sound and the graphics");
    bullet("Menu, Home page: back to the front page; Back to the game there returns to it as it was");
    if (!fullscreenAvailable) bullet("The whole screen: Safari's Share button, then Add to Home Screen");
  } else {
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
  }
  ImGui::Spacing();
  ImGui::TextColored(kGold, "The battles (each capture picks one at random)");
  static const char* const kPieces[6] = {"Pawn", "Knight", "Bishop", "Rook", "Queen", "King"};
  for (int k = 0; k < 6; ++k) {
    if (opt.touch) {
      ImGui::Bullet();
      ImGui::TextWrapped("%s: %s, %s, %s", kPieces[k], battleStyleName(k, 0, pieceSet), battleStyleName(k, 1, pieceSet),
                         battleStyleName(k, 2, pieceSet));
    } else {
      ImGui::BulletText("%s: %s, %s, %s", kPieces[k], battleStyleName(k, 0, pieceSet), battleStyleName(k, 1, pieceSet),
                        battleStyleName(k, 2, pieceSet));
    }
  }
  ImGui::Spacing();
  ImGui::TextDisabled("Living Marble %s", LM_VERSION);
  ImGui::End();
}

// ---------------------------------------------------------------------------
// The touch interface (the web page on iPhone and iPad), as in the iPhone and iPad app: the
// status at the top, a toolbar at the bottom (down the right edge of a phone on its side), the
// settings in a menu, and the move list beside the board on an iPad (behind a button on a phone).
void App::drawTouchUI() {
  ImGuiIO& io = ImGui::GetIO();
  const ImGuiStyle& st = ImGui::GetStyle();
  const float S = ImGui::GetFontSize() / 16.0f;
  const ImVec2 D = io.DisplaySize;
  const float pad = 8.0f * S;
  const bool roomy = std::min(D.x, D.y) >= 600.0f * S;  // an iPad: room for the move list beside the board
  const bool side = !roomy && D.x > D.y;               // a phone on its side
  const bool match = mode == Mode::Online && online.kind == OnlineGame::Kind::Match;
  const bool live = onlineGameOn() && online.net == OnlineGame::Net::Open;  // an online game: draw, resign
  const double now = glfwGetTime();
  const ImVec4 kRed(1.0f, 0.45f, 0.35f, 1.0f);

  // ---------------- the toolbar ----------------
  enum What { NEW, UNDO, HINT, FLIP, DRAW, RESIGN, MOVES, MENU };
  struct Tool {
    const char* label;
    bool enabled;
    What what;
  } tools[7];
  int n = 0;
  // a stray tap mustn't throw a game away: New asks first, if one is going on
  const bool playing = game.plyCount() > 0 && game.result() == chess::Result::Ongoing &&
                       (mode != Mode::Online || online.endText.empty());
  tools[n++] = {"New", canStartNew(), NEW};
  if (live) {
    tools[n++] = {online.myDrawOffer ? "Offered" : "Draw", !online.myDrawOffer && !online.drawOffered, DRAW};
    tools[n++] = {resignArmed() ? "Sure?" : "Resign", true, RESIGN};
  } else {
    tools[n++] = {"Undo", !animating() && game.plyCount() > 0 && mode != Mode::Online, UNDO};
    tools[n++] = {"Hint", canHint(), HINT};
  }
  tools[n++] = {"Flip", true, FLIP};
  if (!roomy) tools[n++] = {"Moves", true, MOVES};
  tools[n++] = {"Menu", true, MENU};

  const float gap = 4.0f * S;
  float bw, bh = 44.0f * S;
  ImVec2 tpos, tpivot;
  if (side) {
    bw = 72.0f * S;
    bh = std::min(bh, (D.y - 2 * pad - 2 * gap - (n - 1) * gap) / n);
    tpos = ImVec2(D.x - pad, D.y * 0.5f);
    tpivot = ImVec2(1, 0.5f);
  } else {
    bw = roomy ? 80.0f * S : (D.x - 2 * pad - 2 * gap - (n - 1) * gap) / n;
    tpos = ImVec2(D.x * 0.5f, D.y - pad);
    tpivot = ImVec2(0.5f, 1);
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(gap, gap));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * S, 2 * S));
  ImGui::SetNextWindowPos(tpos, ImGuiCond_Always, tpivot);
  ImGui::Begin("##tools", nullptr, kPanel | ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::SetWindowFontScale(0.92f);
  for (int i = 0; i < n; ++i) {
    const Tool& t = tools[i];
    if (i && !side) ImGui::SameLine();
    bool armed = t.what == RESIGN && resignArmed();
    bool on = (t.what == MENU && showMenu) || (t.what == MOVES && showMoves);
    if (armed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.12f, 0.9f));
    else if (on) ImGui::PushStyleColor(ImGuiCol_Button, st.Colors[ImGuiCol_ButtonActive]);
    ImGui::BeginDisabled(!t.enabled);
    bool pressed = ImGui::Button(t.label, ImVec2(bw, bh));
    ImGui::EndDisabled();
    if (armed || on) ImGui::PopStyleColor();
    if (!pressed) continue;
    switch (t.what) {
      case NEW:
        if (playing && mode != Mode::Watch) askNew = true;
        else newGame();
        break;
      case UNDO: undo(); break;
      case HINT: requestHint(); break;
      case FLIP: flipCamera(); break;
      case DRAW: offerDraw(false); break;
      case RESIGN: resignOnline(); break;
      case MOVES: showMoves = !showMoves; break;
      case MENU: showMenu = !showMenu; break;
    }
  }
  const ImVec2 toolPos = ImGui::GetWindowPos();
  ImGui::End();
  ImGui::PopStyleVar(3);
  const float toolTop = side ? D.y : toolPos.y;    // (the room above the toolbar ...
  const float toolLeft = side ? toolPos.x : D.x;  // ... and to its left)

  // ---------------- the status, top left (taps go through it, to the board) ----------------
  const float statusW = side ? std::min(D.x * 0.42f, 320 * S) : roomy ? 380 * S : D.x - 2 * pad;
  const bool clocks = match && online.started;
  ImGui::SetNextWindowPos(ImVec2(pad, pad), ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(ImVec2(clocks ? statusW : 0, 0), ImVec2(statusW, D.y * 0.5f));
  ImGui::Begin("##status", nullptr, kPanel | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
  ImGui::PushTextWrapPos(statusW - st.WindowPadding.x);
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = 6.0f * S;
    bool white = game.board().side == chess::WHITE;
    ImVec2 c(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f + 1);
    dl->AddCircleFilled(c, r, white ? IM_COL32(236, 230, 218, 255) : IM_COL32(30, 28, 28, 255));
    dl->AddCircle(c, r, IM_COL32(200, 160, 100, 200), 0, 1.5f);
    ImGui::Dummy(ImVec2(r * 2 + 6 * S, 0));
    ImGui::SameLine();
    ImGui::TextWrapped("%s", statusText());
  }
  if (clocks) drawClocks(S);
  if (mode == Mode::Online && online.awaySeconds >= 0 && online.endText.empty()) {
    int left = std::max(0, online.awaySeconds - (int)(now - online.awayAt));
    ImGui::TextWrapped("%s left: they lose in %d s unless they come back.",
                       match ? online.names[online.side ^ 1].c_str() : "Your friend", left);
  }
  if (mode == Mode::Online && !online.ratingText.empty()) ImGui::TextDisabled("%s", online.ratingText.c_str());
  if (armyBuilding(1)) ImGui::TextDisabled("Raising the medieval army... %d%%", (int)(armyProgress(1) * 100));
  if (armyBuilding(2)) ImGui::TextDisabled("The aliens are landing... %d%%", (int)(armyProgress(2) * 100));
  if (noticeTime > 0 && !notice.empty()) ImGui::TextColored(kRed, "%s", notice.c_str());
  ImGui::PopTextWrapPos();
  const ImVec2 statusPos = ImGui::GetWindowPos(), statusSize = ImGui::GetWindowSize();
  ImGui::End();
  const float statusBottom = statusPos.y + statusSize.y;

  // ---------------- a draw offered, above the toolbar ----------------
  if (live && online.drawOffered) {
    ImGui::SetNextWindowPos(side ? ImVec2(toolLeft - pad, D.y - pad) : ImVec2(D.x * 0.5f, toolTop - pad), ImGuiCond_Always,
                            side ? ImVec2(1, 1) : ImVec2(0.5f, 1));
    ImGui::Begin("##offer", nullptr, kPanel | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(kGold, "%s offers a draw", match ? online.names[online.side ^ 1].c_str() : "Your friend");
    if (ImGui::Button("Accept", ImVec2(110 * S, 0))) offerDraw(true);
    ImGui::SameLine();
    if (ImGui::Button("Decline", ImVec2(110 * S, 0))) declineDraw();
    ImGui::End();
  }

  // ---------------- the move list ----------------
  if (roomy || showMoves) {
    float w = roomy ? 236 * S : std::min(220 * S, D.x * 0.5f);
    ImVec2 pos(side ? toolLeft - pad : D.x - pad, roomy || side ? pad : statusBottom + pad);
    float maxH = roomy ? std::min(D.y * 0.5f, 480 * S) : (side ? D.y - pad : toolTop - pad) - pos.y;
    drawMoves(pos, ImVec2(1, 0), w, maxH, S);
  }

  // ---------------- the menu: everything else ----------------
  if (showMenu) {
    ImVec2 pos(pad, statusBottom + pad);
    float w = roomy ? 380 * S : D.x - 2 * pad, maxH = toolTop - pad - pos.y;
    if (side) {  // beside the status, if there is room
      pos = ImVec2(statusPos.x + statusSize.x + pad, pad);
      w = std::min(400 * S, toolLeft - pad - pos.x);
      if (w < 260 * S) {
        pos = ImVec2(pad, statusBottom + pad);
        w = std::min(400 * S, toolLeft - 2 * pad);
      }
      maxH = D.y - pad - pos.y;
    }
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, std::max(120 * S, maxH)));
    ImGui::Begin("##menu", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
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
    drawHomeButton();
    drawSettings(S);
    ImGui::Separator();
#ifdef __EMSCRIPTEN__
    // the account (the page's window: a new password, a new recovery code, logging out)
    std::string account = me.known ? "Account: " + me.name : std::string("Log in");
    if (ImGui::Button(account.c_str(), ImVec2(-1, 0))) EM_ASM({ if (window.lmAccount) window.lmAccount(); });
#endif
    const float third = (ImGui::GetContentRegionAvail().x - 2 * st.ItemSpacing.x) / 3.0f;
    if (ImGui::Button("Reset view", ImVec2(third, 0))) resetView();
    ImGui::SameLine();
    if (ImGui::Button("Help", ImVec2(third, 0))) showHelp = !showHelp;
    ImGui::SameLine();
    if (ImGui::Button("Hide", ImVec2(third, 0))) {  // (a clear view of the board: Show brings it back)
      showUI = false;
      showMenu = false;
    }
    ImGui::End();
  }

  // ---------------- New, during a game: are you sure? ----------------
  if (askNew) {
    ImGui::OpenPopup("##new");
    askNew = false;
  }
  ImGui::SetNextWindowPos(ImVec2(D.x * 0.5f, D.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(D.x - 32 * S, 340 * S), 0), ImGuiCond_Always);
  if (ImGui::BeginPopupModal("##new", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
    ImGui::PushFont(fontTitle);
    ImGui::TextColored(kGold, "Start a new game?");
    ImGui::PopFont();
    ImGui::TextWrapped("%s", mode == Mode::Online ? "The game starts again for your friend too."
                                                  : "The game you are playing will end.");
    ImGui::Spacing();
    const float half = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.12f, 0.9f));
    if (ImGui::Button("New game", ImVec2(half, 44 * S))) {
      newGame();
      ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Keep playing", ImVec2(half, 44 * S))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  drawLeaderboard(S);
  bannerTop = statusBottom;
  drawBanner();
}

// With the interface hidden (for a clear view of the board): one button brings it back.
void App::drawTouchShowButton() {
  ImGuiIO& io = ImGui::GetIO();
  const float S = ImGui::GetFontSize() / 16.0f;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 8 * S, io.DisplaySize.y - 8 * S), ImGuiCond_Always, ImVec2(1, 1));
  ImGui::SetNextWindowBgAlpha(0.5f);
  ImGui::Begin("##show", nullptr, kPanel | ImGuiWindowFlags_AlwaysAutoResize);
  if (ImGui::Button("Show", ImVec2(64 * S, 40 * S))) showUI = true;
  ImGui::End();
}
