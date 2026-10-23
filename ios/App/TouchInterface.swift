// The touch interface laid over the 3D scene on iPhone and iPad (SwiftUI): the
// status, a toolbar, and sheets for settings, the move list and help. The
// banners, move list panel, promotion choice and loading screen are shared
// with the Mac (Interface.swift).
import SwiftUI

struct TouchHUDView: View {
  let controller: GameController
  @Bindable var model: GameModel
  @State private var showSettings = false
  @State private var showMoves = false
  @State private var confirmNewGame = false
  @State private var shotSaved = false

  var body: some View {
    GeometryReader { geo in
      let wide = geo.size.width > geo.size.height
      let roomy = min(geo.size.width, geo.size.height) >= 600  // iPad: room for the move list
      let showing = model.showUI && !model.noUI
      ZStack {
        if showing {
          if roomy {
            VStack(spacing: 0) {
              HStack(alignment: .top, spacing: 12) {
                StatusPill(model: model, showMaterial: false)
                Spacer(minLength: 0)
                MovesPanel(model: model, maxHeight: geo.size.height * (wide ? 0.55 : 0.3))
              }
              Spacer(minLength: 0)
              toolbar(.horizontal, movesButton: false)
            }
            .padding(16)
          } else if wide {
            // phone on its side: the board fills the middle, the controls keep to the edges
            HStack(alignment: .top, spacing: 0) {
              StatusPill(model: model, showMaterial: true)
              Spacer(minLength: 0)
              toolbar(.vertical, movesButton: true)
                .frame(maxHeight: .infinity)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
          } else {
            VStack(spacing: 0) {
              StatusPill(model: model, showMaterial: true)
              Spacer(minLength: 0)
              toolbar(.horizontal, movesButton: true)
            }
            .padding(.horizontal, 12)
            .padding(.top, 6)
            .padding(.bottom, 4)
          }
        } else if !model.noUI {
          showInterfaceButton
        }
        if shotSaved {
          Notice(text: "Screenshot saved to Files")
            .frame(maxHeight: .infinity, alignment: .bottom)
            .padding(.bottom, showing && !(wide && !roomy) ? 72 : 16)
            .transition(.opacity)
        }
        // under the status: low enough to clear it on a phone on its side
        BannerView(model: model, size: geo.size,
                   y: geo.size.height * (roomy ? 0.14 : wide ? 0.24 : 0.2) + (model.bannerPermanent && showing ? 14 : 0),
                   hint: "Tap New to play again")
        if model.promoPending {
          PromotionPanel(controller: controller)
        }
      }
      .frame(width: geo.size.width, height: geo.size.height)
    }
    .sheet(isPresented: $showSettings) { SettingsSheet(controller: controller, model: model) }
    .sheet(isPresented: $showMoves) { MovesSheet(model: model) }
    .sheet(isPresented: $model.showHelp) { HelpSheet(pieceSet: model.pieceSet) }
    .task(id: model.screenshotsSaved) {
      guard model.screenshotsSaved > 0 else { return }
      withAnimation { shotSaved = true }
      try? await Task.sleep(for: .seconds(2))
      if !Task.isCancelled { withAnimation { shotSaved = false } }
    }
  }

  private func toolbar(_ axis: Axis, movesButton: Bool) -> some View {
    let layout = axis == .horizontal ? AnyLayout(HStackLayout(spacing: 0)) : AnyLayout(VStackLayout(spacing: 0))
    return layout {
      ToolButton(title: "New", icon: "plus.circle") {
        // a stray tap must not throw away a game in progress
        if model.san.isEmpty || model.bannerPermanent {
          controller.newGame()
        } else {
          confirmNewGame = true
        }
      }
      .confirmationDialog("Start a new game?", isPresented: $confirmNewGame, titleVisibility: .visible) {
        Button("New Game", role: .destructive) { controller.newGame() }
      }
      ToolButton(title: "Undo", icon: "arrow.uturn.backward") { controller.undo() }
        .disabled(!model.canUndo)
      ToolButton(title: "Hint", icon: model.hintPending ? "lightbulb.fill" : "lightbulb") { controller.requestHint() }
        .disabled(!model.canHint)
      ToolButton(title: "Flip", icon: "arrow.triangle.2.circlepath") { controller.flipCamera() }
      ToolButton(title: "Reset", icon: "camera.viewfinder") { controller.resetView() }
      if movesButton {
        ToolButton(title: "Moves", icon: "list.number") { showMoves = true }
      }
      ToolButton(title: "Settings", icon: "gearshape") { showSettings = true }
    }
    .padding(4)
    .background {
      RoundedRectangle(cornerRadius: 16, style: .continuous).fill(.ultraThinMaterial)
      RoundedRectangle(cornerRadius: 16, style: .continuous).fill(Palette.panel.opacity(0.62))
      RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(Palette.gold.opacity(0.14))
    }
  }

  /// With the interface hidden, one small button brings it back.
  private var showInterfaceButton: some View {
    Button { model.showUI = true } label: {
      Image(systemName: "eye")
        .font(.system(size: 17, weight: .medium))
        .frame(width: 44, height: 44)
        .background(Circle().fill(.ultraThinMaterial))
    }
    .buttonStyle(.plain)
    .foregroundStyle(Palette.gold.opacity(0.85))
    .accessibilityLabel("Show Interface")
    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing)
    .padding(12)
  }
}

// ---------------------------------------------------------------------------
private struct ToolButton: View {
  let title: String
  let icon: String
  let action: () -> Void
  @Environment(\.isEnabled) private var isEnabled

  var body: some View {
    Button(action: action) {
      VStack(spacing: 3) {
        Image(systemName: icon).font(.system(size: 19, weight: .medium)).frame(height: 22)
        Text(title).font(.system(size: 10, weight: .medium)).lineLimit(1).minimumScaleFactor(0.8)
      }
      .frame(width: 48, height: 46)  // above the 44 pt touch target, and seven fit a 375 pt wide phone
      .contentShape(Rectangle())
    }
    .buttonStyle(.plain)
    .foregroundStyle(Palette.gold.opacity(isEnabled ? 1 : 0.35))
  }
}

/// Whose move it is (or the result), plus the material balance.
private struct StatusPill: View {
  let model: GameModel
  let showMaterial: Bool

  var body: some View {
    HStack(spacing: 8) {
      Circle()
        .fill(model.whiteToMove ? Color(red: 0.93, green: 0.90, blue: 0.85) : Color(red: 0.12, green: 0.11, blue: 0.11))
        .overlay(Circle().strokeBorder(Color(red: 0.78, green: 0.63, blue: 0.39).opacity(0.8), lineWidth: 1.5))
        .frame(width: 12, height: 12)
      Text(model.status)
        .font(.system(size: 15, weight: .medium))
        .lineLimit(1)
        .minimumScaleFactor(0.7)
      if showMaterial && model.material != "even" {
        Text(model.material).font(.system(size: 12).monospacedDigit()).foregroundStyle(Palette.dim)
      }
    }
    .padding(.horizontal, 14)
    .padding(.vertical, 9)
    .background {
      Capsule().fill(.ultraThinMaterial)
      Capsule().fill(Palette.panel.opacity(0.62))
      Capsule().strokeBorder(Palette.gold.opacity(0.14))
    }
    .foregroundStyle(Palette.text)
    .allowsHitTesting(false)
  }
}

private struct Notice: View {
  let text: String

  var body: some View {
    Text(text)
      .font(.system(size: 14, weight: .medium))
      .padding(.horizontal, 14)
      .padding(.vertical, 8)
      .background {
        Capsule().fill(.ultraThinMaterial)
        Capsule().fill(Palette.panel.opacity(0.62))
      }
      .foregroundStyle(Palette.text)
      .allowsHitTesting(false)
  }
}

// ---------------------------------------------------------------------------
// Sheets. Settings and moves stop at half height with the board still live above
// them, so the camera can be turned while the graphics or the move list are open.
// ---------------------------------------------------------------------------
private struct SettingsSheet: View {
  let controller: GameController
  @Bindable var model: GameModel
  @Environment(\.dismiss) private var dismiss

  private static let qualities = ["Low", "Medium", "High", "Ultra"]

  var body: some View {
    NavigationStack {
      Form {
        Section("Game") {
          Picker("Mode", selection: Binding(get: { model.mode }, set: { controller.setMode($0) })) {
            ForEach(Mode.allCases, id: \.self) { Text($0.title).tag($0) }
          }
          Picker("Opponent", selection: $model.aiLevel) {
            ForEach(kAILevels.indices, id: \.self) { Text(kAILevels[$0].name).tag($0) }
          }
          .disabled(model.mode == .twoPlayer)
          Picker("Pieces", selection: Binding(get: { model.pieceSet }, set: { controller.setPieceSet($0) })) {
            Text("Living Marble").tag(0)
            Text("Medieval").tag(1)
            Text("Aliens").tag(2)
          }
          if model.medievalBuilding {
            ProgressView("Raising the medieval army...", value: model.medievalProgress)
          }
          if model.alienBuilding {
            ProgressView("The aliens are landing...", value: model.alienProgress)
          }
          Toggle("Battle animations", isOn: $model.battles)
          Toggle("Cinematic camera", isOn: $model.cinematic)
          if model.mode == .twoPlayer {
            Toggle("Turn board each move", isOn: $model.autoRotate)
          }
          if model.mode == .watch {
            Toggle("Slow orbit", isOn: Binding(get: { model.slowOrbit }, set: { controller.setSlowOrbit($0) }))
          }
        }
        if model.audioOK {
          Section {
            LabeledContent {
              Slider(value: Binding(get: { Double(model.volume) }, set: { controller.setVolume(Float($0)) }), in: 0...1)
            } label: {
              Label("Volume", systemImage: "speaker.wave.2.fill")
            }
            LabeledContent {
              Slider(value: Binding(get: { Double(model.musicVolume) }, set: { controller.setMusicVolume(Float($0)) }),
                     in: 0...1)
                .disabled(!model.music)
            } label: {
              Label("Music", systemImage: "music.note")
            }
            Toggle("Play music", isOn: Binding(get: { model.music }, set: { controller.setMusic($0) }))
          } header: {
            Text("Sound")
          } footer: {
            if model.music && !model.nowPlaying.isEmpty {
              Text("Playing: \(model.nowPlaying). In every army each piece has its own music, which plays when it moves.")
            }
          }
        }
        Section {
          Picker("Quality", selection: Binding(get: { model.gfxPreset }, set: { controller.applyGraphicsPreset($0) })) {
            ForEach(Self.qualities.indices, id: \.self) { Text(Self.qualities[$0]).tag($0) }
          }
          .pickerStyle(.segmented)
          Toggle("Depth of field (blur)", isOn: $model.depthOfField)
            .disabled(!RenderSettings.preset(model.gfxPreset).dof)
          Toggle("Save power", isOn: $model.savePower)
          LabeledContent("Exposure") {
            Slider(value: Binding(get: { Double(model.exposure) }, set: { model.exposure = Float($0) }), in: 0.4...2.0)
          }
        } header: {
          Text("Graphics")
        } footer: {
          Text(model.fpsText + ". Save power draws 30 frames a second while the board is at rest and in Low Power Mode. "
            + "If it stutters or the device gets warm, pick a lower quality.")
        }
        Section {
          Button("Hide Interface") {
            model.showUI = false
            dismiss()
          }
          Button("Save Screenshot") { controller.requestScreenshot() }
          NavigationLink("Controls and Battles") { HelpList(pieceSet: model.pieceSet) }
        } footer: {
          Text("Screenshots show just the board and go to the Files app, in Living Marble.")
        }
        Section {
          LabeledContent("Version", value: kVersion)
        }
      }
      .navigationTitle("Living Marble")
      .navigationBarTitleDisplayMode(.inline)
      .toolbar {
        ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
      }
    }
    .presentationDetents([.medium, .large])
    .presentationBackgroundInteraction(.enabled(upThrough: .medium))
  }
}

private struct MovesSheet: View {
  let model: GameModel
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    NavigationStack {
      Group {
        if model.san.isEmpty {
          Text("No moves yet")
            .foregroundStyle(.secondary)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        } else {
          MoveList(model: model)
        }
      }
      .navigationTitle("Moves")
      .navigationBarTitleDisplayMode(.inline)
      .toolbar {
        ToolbarItem(placement: .topBarLeading) {
          Text(model.material).font(.subheadline).foregroundStyle(.secondary)
        }
        ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } }
      }
    }
    .presentationDetents([.medium, .large])
    .presentationBackgroundInteraction(.enabled(upThrough: .medium))
  }
}

private struct MoveList: View {
  let model: GameModel

  var body: some View {
    let rows = (model.san.count + 1) / 2
    ScrollViewReader { proxy in
      ScrollView(.vertical) {
        LazyVStack(spacing: 0) {
          ForEach(0..<rows, id: \.self) { r in
            MoveRow(number: r + 1, white: model.san[r * 2], black: r * 2 + 1 < model.san.count ? model.san[r * 2 + 1] : nil,
                    lastIsWhite: r * 2 == model.san.count - 1, lastIsBlack: r * 2 + 1 == model.san.count - 1, fontSize: 16)
              .background(r % 2 == 1 ? Color.white.opacity(0.04) : Color.clear)
              .id(r)
          }
        }
        .padding(.horizontal, 16)
      }
      .onAppear { if rows > 0 { proxy.scrollTo(rows - 1, anchor: .bottom) } }
      .onChange(of: model.san.count) {
        if rows > 0 { withAnimation(.easeOut(duration: 0.2)) { proxy.scrollTo(rows - 1, anchor: .bottom) } }
      }
    }
  }
}

private struct HelpSheet: View {
  let pieceSet: Int
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    NavigationStack {
      HelpList(pieceSet: pieceSet)
        .toolbar {
          ToolbarItem(placement: .confirmationAction) {
            Button("Done") { dismiss() }
              .keyboardShortcut(.cancelAction)
          }
        }
    }
  }
}

private struct HelpList: View {
  let pieceSet: Int  // whose battles to list: 0 Living Marble, 1 medieval, 2 aliens
  private let touch = [
    "Tap a piece, then a glowing square to move it, or drag the piece onto the square",
    "Drag anywhere else to turn the board, even to look at it from underneath",
    "Pinch to zoom, twist two fingers to turn the board, slide two fingers up or down to tilt it",
    "Reset brings the camera back, Flip looks from the other side",
    "Hint selects the best move for you: tap the glowing square to play it",
  ]
  private let keyboard = [
    "Arrow keys turn the board",
    "N new game, U or ⌘Z undo, T hint, F flip view, R reset view",
    "B battle animations on/off, C cinematic camera on/off",
    "H hide the interface, M music on/off, F12 screenshot, ? help",
  ]
  private let pieces = ["Pawn", "Knight", "Bishop", "Rook", "Queen", "King"]

  var body: some View {
    List {
      Section("Touch") {
        ForEach(touch, id: \.self) { Text($0) }
      }
      Section("With a keyboard") {
        ForEach(keyboard, id: \.self) { Text($0) }
      }
      Section {
        ForEach(0..<Kind.count, id: \.self) { k in
          VStack(alignment: .leading, spacing: 3) {
            Text(pieces[k]).font(.headline).foregroundStyle(Palette.gold)
            Text("\(battleStyleName(k, 0, pieceSet)), \(battleStyleName(k, 1, pieceSet)), \(battleStyleName(k, 2, pieceSet))")
              .foregroundStyle(.secondary)
          }
        }
      } header: {
        Text("The battles")
      } footer: {
        Text("Each capture picks one of the attacker's three styles at random.")
      }
    }
    .navigationTitle("Controls and Battles")
    .navigationBarTitleDisplayMode(.inline)
  }
}
