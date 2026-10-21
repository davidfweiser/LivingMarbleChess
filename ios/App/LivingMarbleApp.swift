// Living Marble - 3D Battle Chess, iPhone and iPad edition.
// A photoreal chess game where carved marble pieces come alive and fight.
// The game itself is shared with the Mac app (macos/Sources/LivingMarble);
// this folder adds the touch view, the touch interface and the app shell.
import SwiftUI
import UIKit

@main @MainActor
struct LivingMarbleApp: App {
  private let controller: GameController?
  @Environment(\.scenePhase) private var scenePhase

  init() {
    // the Mac's command-line options also work here (Xcode: Edit Scheme > Arguments)
    let options = Options.parse(Array(CommandLine.arguments.dropFirst()))
    if options.selftest {
      var b = Board()
      b.setStart()
      let n = Chess.perft(b, 5)
      print("perft(5) = \(n) (\(n == 4_865_609 ? "OK" : "FAIL"))")
      exit(n == 4_865_609 ? 0 : 1)
    }
    controller = GameController(options: options)
  }

  var body: some Scene {
    WindowGroup {
      Group {
        if let controller {
          ContentView(controller: controller)
            .onChange(of: controller.model.mode, initial: true) { _, mode in
              // nobody touches the screen while the AI plays itself: keep it awake
              UIApplication.shared.isIdleTimerDisabled = mode == .watch
            }
        } else {
          NoMetalView()
        }
      }
      .statusBarHidden()
      .persistentSystemOverlays(.hidden)
      // dragging near the screen edges turns the board instead of opening Notification Center
      .defersSystemGestures(on: .vertical)
    }
    .onChange(of: scenePhase) { _, phase in
      guard let controller else { return }
      // no GPU work in the background; sound picks up again on return
      controller.view.isPaused = phase == .background
      if phase == .active { controller.audio.resume() }
    }
  }
}

private struct NoMetalView: View {
  var body: some View {
    VStack(spacing: 10) {
      Text("Living Marble needs Metal").font(.title2.weight(.semibold))
      Text("The graphics could not be set up on this device.").foregroundStyle(.secondary)
    }
    .multilineTextAlignment(.center)
    .padding(32)
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .background(Color.black)
    .preferredColorScheme(.dark)
  }
}
