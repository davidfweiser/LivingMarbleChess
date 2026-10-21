// The native interface laid over the 3D scene (SwiftUI): status, controls,
// move list, banners, help, promotion and the loading screen. On iPhone and
// iPad the touch layout in ios/App/TouchInterface.swift replaces the desktop
// panels; the banners, move list, promotion and loading screen are shared.
import MetalKit
import Observation
import SwiftUI

/// Interface state. The controller mirrors the game into it; the views edit the settings.
@Observable @MainActor
final class GameModel {
  // settings
  var mode = Mode.humanWhite
  var aiLevel = 2
  var battles = true
  var cinematic = true
  var autoRotate = true
  var slowOrbit = false
  var volume: Float = 0.8
  var gfxPreset = 2
  var pieceSet = 0  // 0 Living Marble, 1 medieval, 2 aliens (what the picker shows: the set being switched to)
  var exposure: Float = 1
  var showUI = true
  var showHelp = false
  var noUI = false
  // mirrored game state
  var loading = true
  var loadProgress: Double = 0
  var loadStage = "Quarrying marble..."
  var status = ""
  var whiteToMove = true
  var san: [String] = []
  var material = "even"
  var canUndo = false
  var bannerText = ""
  var bannerAlpha: Double = 0
  var bannerPermanent = false
  var promoPending = false
  var fps = 0
  var audioOK = false
  var medievalBuilding = false  // the medieval army is being sculpted in the background
  var medievalProgress: Double = 0
  var alienBuilding = false  // ... and the aliens
  var alienProgress: Double = 0
  var screenshotsSaved = 0  // counts saved screenshots (the touch interface confirms each)
}

enum Palette {
  static let gold = Color(red: 0.98, green: 0.80, blue: 0.45)
  static let text = Color(red: 0.93, green: 0.90, blue: 0.84)
  static let dim = Color(red: 0.60, green: 0.56, blue: 0.50)
  static let panel = Color(red: 0.055, green: 0.045, blue: 0.04)
  static let banner = Color(red: 0.98, green: 0.86, blue: 0.63)
}

extension Font {
  static func serif(_ size: CGFloat) -> Font { .custom("Georgia", size: size) }
}

/// Dark, slightly translucent card used by every panel.
private struct Panel: ViewModifier {
  func body(content: Content) -> some View {
    content
      .padding(.horizontal, 16)
      .padding(.vertical, 14)
      .background {
        RoundedRectangle(cornerRadius: 12, style: .continuous).fill(.ultraThinMaterial)
        RoundedRectangle(cornerRadius: 12, style: .continuous).fill(Palette.panel.opacity(0.62))
        RoundedRectangle(cornerRadius: 12, style: .continuous).strokeBorder(Palette.gold.opacity(0.14))
      }
      .foregroundStyle(Palette.text)
  }
}

extension View {
  func panel() -> some View { modifier(Panel()) }
}

// ---------------------------------------------------------------------------
struct ContentView: View {
  let controller: GameController

  var body: some View {
    ZStack {
      MetalSceneView(view: controller.view)
#if os(iOS)
        .ignoresSafeArea()
#endif
      if controller.model.loading {
        LoadingView(model: controller.model)
      } else {
#if os(macOS)
        HUDView(controller: controller, model: controller.model)
#else
        TouchHUDView(controller: controller, model: controller.model)
#endif
      }
    }
    .background(Color.black)
    .preferredColorScheme(.dark)
    .tint(Palette.gold)
  }
}

#if os(macOS)
private struct MetalSceneView: NSViewRepresentable {
  let view: MTKView
  func makeNSView(context: Context) -> MTKView { view }
  func updateNSView(_ nsView: MTKView, context: Context) {}
}
#else
private struct MetalSceneView: UIViewRepresentable {
  let view: MTKView
  func makeUIView(context: Context) -> MTKView { view }
  func updateUIView(_ uiView: MTKView, context: Context) {}
}
#endif

// ---------------------------------------------------------------------------
private struct LoadingView: View {
  let model: GameModel

  var body: some View {
    VStack(spacing: 0) {
      Text("Living Marble")
        .font(.serif(64))
        .lineLimit(1)
        .minimumScaleFactor(0.5)
        .foregroundStyle(Color(red: 0.94, green: 0.89, blue: 0.78))
        .shadow(color: .black.opacity(0.4), radius: 2, x: 1, y: 2)
      Text("3 D   B A T T L E   C H E S S")
        .font(.serif(21))
        .lineLimit(1)
        .minimumScaleFactor(0.5)
        .foregroundStyle(Color(red: 0.78, green: 0.67, blue: 0.47))
        .padding(.top, 6)
      Capsule()
        .fill(Color(red: 0.24, green: 0.19, blue: 0.14))
        .overlay(alignment: .leading) {
          GeometryReader { g in
            Capsule().fill(Palette.gold).frame(width: g.size.width * max(0, min(1, model.loadProgress)))
          }
        }
        .frame(maxWidth: 380)
        .frame(height: 4)
        .padding(.top, 34)
      Text(model.loadStage)
        .font(.system(size: 15))
        .foregroundStyle(Color(red: 0.67, green: 0.59, blue: 0.47))
        .padding(.top, 18)
    }
    .padding(.horizontal, 32)
    .allowsHitTesting(false)
  }
}

// ---------------------------------------------------------------------------
#if os(macOS)
private struct HUDView: View {
  let controller: GameController
  @Bindable var model: GameModel

  var body: some View {
    GeometryReader { geo in
      ZStack {
        if model.showUI && !model.noUI {
          VStack(alignment: .leading, spacing: 0) {
            HStack(alignment: .top, spacing: 0) {
              ControlPanel(controller: controller, model: model)
              Spacer(minLength: 0)
              MovesPanel(model: model, maxHeight: min(geo.size.height * 0.62, 480))
            }
            Spacer(minLength: 0)
            Text("Click: select & move     Drag: turn the board     Scroll or pinch: zoom     R: reset view     ?: help     H: hide interface")
              .font(.system(size: 13))
              .foregroundStyle(Color(red: 0.86, green: 0.78, blue: 0.67).opacity(0.6))
              .allowsHitTesting(false)
          }
          .padding(18)
        }
        BannerView(model: model, size: geo.size,
                   y: geo.size.height * 0.14 + (model.bannerPermanent && model.showUI && !model.noUI ? 14 : 0),
                   hint: "Press N or choose New Game to play again")
        if model.showHelp {
          HelpPanel(model: model)
        }
        if model.promoPending {
          PromotionPanel(controller: controller)
        }
      }
      .frame(width: geo.size.width, height: geo.size.height)
    }
  }
}
#endif

/// "Check!" and the game result, centred at height y; game over adds the hint below.
struct BannerView: View {
  let model: GameModel
  let size: CGSize
  let y: CGFloat
  let hint: String

  var body: some View {
    if !model.bannerText.isEmpty {
      let fontSize: CGFloat = model.bannerPermanent ? 54 : 64
      VStack(spacing: 6) {
        Text(model.bannerText)
          .font(.serif(fontSize))
          .lineLimit(1)
          .minimumScaleFactor(0.4)  // phones
          .foregroundStyle(Palette.banner)
          .shadow(color: .black.opacity(0.55), radius: 3, x: 1.5, y: 2)
        if model.bannerPermanent && model.showUI && !model.noUI {
          Text(hint)
            .font(.system(size: 15))
            .lineLimit(1)
            .minimumScaleFactor(0.6)
            .foregroundStyle(Color(red: 0.86, green: 0.78, blue: 0.63).opacity(0.86))
            .shadow(color: .black.opacity(0.5), radius: 2)
        }
      }
      .opacity(model.bannerAlpha)
      .frame(maxWidth: max(0, size.width - 32))
      .position(x: size.width / 2, y: y)
      .allowsHitTesting(false)
    }
  }
}

// ---------------------------------------------------------------------------
#if os(macOS)
private struct ControlPanel: View {
  let controller: GameController
  @Bindable var model: GameModel

  var body: some View {
    VStack(alignment: .leading, spacing: 9) {
      VStack(alignment: .leading, spacing: 1) {
        Text("Living Marble").font(.serif(30)).foregroundStyle(Palette.gold)
        Text("3D Battle Chess").font(.system(size: 13)).foregroundStyle(Palette.dim)
      }
      Divider().overlay(Palette.gold.opacity(0.3))
      HStack(alignment: .firstTextBaseline, spacing: 8) {
        Circle()
          .fill(model.whiteToMove ? Color(red: 0.93, green: 0.90, blue: 0.85) : Color(red: 0.12, green: 0.11, blue: 0.11))
          .overlay(Circle().strokeBorder(Color(red: 0.78, green: 0.63, blue: 0.39).opacity(0.8), lineWidth: 1.5))
          .frame(width: 12, height: 12)
          .alignmentGuide(.firstTextBaseline) { $0[.bottom] - 1 }
        Text(model.status).font(.system(size: 14, weight: .medium)).fixedSize(horizontal: false, vertical: true)
      }
      HStack(spacing: 8) {
        Button { controller.newGame() } label: { Text("New Game").frame(maxWidth: .infinity) }
        Button { controller.undo() } label: { Text("Undo").frame(maxWidth: .infinity) }
          .disabled(!model.canUndo)
        Button { controller.flipCamera() } label: { Text("Flip View").frame(maxWidth: .infinity) }
      }
      .controlSize(.large)
      .padding(.vertical, 2)

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
        Text("Raising the medieval army... \(Int(model.medievalProgress * 100))%")
          .font(.system(size: 12))
          .foregroundStyle(Palette.dim)
      }
      if model.alienBuilding {
        Text("The aliens are landing... \(Int(model.alienProgress * 100))%")
          .font(.system(size: 12))
          .foregroundStyle(Palette.dim)
      }

      Toggle("Battle animations", isOn: $model.battles)
      Toggle("Cinematic camera", isOn: $model.cinematic)
      if model.mode == .twoPlayer {
        Toggle("Turn board each move", isOn: $model.autoRotate)
      }
      if model.mode == .watch {
        Toggle("Slow orbit", isOn: Binding(get: { model.slowOrbit }, set: { controller.setSlowOrbit($0) }))
      }
      if model.audioOK {
        LabeledContent("Volume") {
          Slider(value: Binding(get: { Double(model.volume) }, set: { controller.setVolume(Float($0)) }), in: 0...1)
        }
      }
      DisclosureGroup("Graphics") {
        VStack(alignment: .leading, spacing: 8) {
          Picker("Quality", selection: Binding(get: { model.gfxPreset }, set: { controller.applyGraphicsPreset($0) })) {
            ForEach(Array(["Low", "Medium", "High", "Ultra"].enumerated()), id: \.offset) { Text($0.element).tag($0.offset) }
          }
          LabeledContent("Exposure") {
            Slider(value: Binding(get: { Double(model.exposure) }, set: { model.exposure = Float($0) }), in: 0.4...2.0)
          }
          Text("\(model.fps) fps").font(.system(size: 12).monospacedDigit()).foregroundStyle(Palette.dim)
        }
        .padding(.top, 6)
      }
    }
    .font(.system(size: 13))
    .frame(width: 268)
    .panel()
  }
}
#endif

// ---------------------------------------------------------------------------
struct MovesPanel: View {
  let model: GameModel
  let maxHeight: CGFloat

  var body: some View {
    let rows = (model.san.count + 1) / 2
    let listHeight = CGFloat(max(1, rows)) * 22 + 44
    VStack(alignment: .leading, spacing: 8) {
      HStack(alignment: .firstTextBaseline) {
        Text("Moves").font(.system(size: 14, weight: .semibold)).foregroundStyle(Palette.gold)
        Spacer()
        Text(model.material).font(.system(size: 12)).foregroundStyle(Palette.dim)
      }
      Divider().overlay(Palette.gold.opacity(0.3))
      ScrollViewReader { proxy in
        ScrollView(.vertical) {
          LazyVStack(spacing: 0) {
            ForEach(0..<rows, id: \.self) { r in
              MoveRow(number: r + 1, white: model.san[r * 2], black: r * 2 + 1 < model.san.count ? model.san[r * 2 + 1] : nil,
                      lastIsWhite: r * 2 == model.san.count - 1, lastIsBlack: r * 2 + 1 == model.san.count - 1)
                .background(r % 2 == 1 ? Color.white.opacity(0.03) : Color.clear)
                .id(r)
            }
          }
        }
        .scrollIndicators(.automatic)
        .onChange(of: model.san.count) {
          if rows > 0 { withAnimation(.easeOut(duration: 0.2)) { proxy.scrollTo(rows - 1, anchor: .bottom) } }
        }
      }
    }
    .frame(width: 204)
    .frame(height: min(maxHeight, listHeight))
    .panel()
  }
}

struct MoveRow: View {
  let number: Int
  let white: String
  let black: String?
  let lastIsWhite: Bool
  let lastIsBlack: Bool
  var fontSize: CGFloat = 13

  var body: some View {
    HStack(spacing: 0) {
      Text("\(number).").foregroundStyle(Palette.dim).frame(width: 34, alignment: .leading)
      Text(white).foregroundStyle(lastIsWhite ? Palette.gold : Palette.text).frame(maxWidth: .infinity, alignment: .leading)
      Text(black ?? "").foregroundStyle(lastIsBlack ? Palette.gold : Palette.text).frame(maxWidth: .infinity, alignment: .leading)
    }
    .font(.system(size: fontSize).monospacedDigit())
    .frame(height: fontSize + 9)
    .padding(.horizontal, 4)
  }
}

// ---------------------------------------------------------------------------
#if os(macOS)
private struct HelpPanel: View {
  let model: GameModel

  private let controls = [
    "Click a piece, then a glowing square to move",
    "Drag (any button), arrow keys: turn the board, even look underneath",
    "Scroll or pinch: zoom",
    "N new game, U or ⌘Z undo, F flip view, R reset view",
    "B battle animations on/off, C cinematic camera on/off",
    "H hide the interface, F12 or ⇧⌘S screenshot, ⌘Q quit",
  ]
  private let pieces = ["Pawn", "Knight", "Bishop", "Rook", "Queen", "King"]

  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      HStack {
        Text("Controls").font(.system(size: 15, weight: .semibold)).foregroundStyle(Palette.gold)
        Spacer()
        Button("Close") { model.showHelp = false }
          .keyboardShortcut(.cancelAction)
      }
      ForEach(controls, id: \.self) { bullet($0) }
      Text("The battles (each capture picks one at random)")
        .font(.system(size: 15, weight: .semibold))
        .foregroundStyle(Palette.gold)
        .padding(.top, 10)
      ForEach(0..<Kind.count, id: \.self) { k in
        let s = model.pieceSet
        bullet("\(pieces[k]): \(battleStyleName(k, 0, s)), \(battleStyleName(k, 1, s)), \(battleStyleName(k, 2, s))")
      }
    }
    .font(.system(size: 13))
    .frame(width: 480)
    .panel()
  }

  private func bullet(_ s: String) -> some View {
    HStack(alignment: .firstTextBaseline, spacing: 8) {
      Text("•").foregroundStyle(Palette.gold)
      Text(s).fixedSize(horizontal: false, vertical: true)
    }
  }
}
#endif

struct PromotionPanel: View {
  let controller: GameController

  private let choices: [(String, String, Int)] = [
    ("♛", "Queen", Chess.queen), ("♜", "Rook", Chess.rook), ("♝", "Bishop", Chess.bishop), ("♞", "Knight", Chess.knight),
  ]

  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Text("Your pawn reached the last rank!").font(.system(size: 15, weight: .semibold)).foregroundStyle(Palette.gold)
      Text("Choose what it becomes:")
      HStack(spacing: 8) {
        ForEach(choices, id: \.1) { c in
          Button { controller.choosePromotion(c.2) } label: {
            VStack(spacing: 2) {
              Text(c.0).font(.system(size: 24))
              Text(c.1).font(.system(size: 12))
            }
            .frame(width: 74, height: 52)
#if os(iOS)
            .modifier(TouchKey())
#endif
          }
        }
      }
      Button { controller.cancelPromotion() } label: {
        Text("Cancel")
          .frame(maxWidth: .infinity)
#if os(iOS)
          .frame(height: 44)
          .modifier(TouchKey())
#endif
      }
      .keyboardShortcut(.cancelAction)
    }
    .font(.system(size: 13))
    .fixedSize()
    .panel()
  }
}

#if os(iOS)
/// iOS buttons have no bezel of their own: give the choices a visible, finger-sized shape.
private struct TouchKey: ViewModifier {
  func body(content: Content) -> some View {
    content
      .background(RoundedRectangle(cornerRadius: 10, style: .continuous).fill(Color.white.opacity(0.08)))
      .contentShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
  }
}
#endif
