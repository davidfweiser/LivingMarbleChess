// Application lifecycle: the window and the menu bar.
import AppKit
import SwiftUI

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
  private let opt: Options
  private var controller: GameController?
  private var window: NSWindow?

  init(options: Options) {
    opt = options
  }

  func applicationDidFinishLaunching(_ notification: Notification) {
    guard let c = GameController(options: opt) else {
      let alert = NSAlert()
      alert.messageText = "Living Marble needs Metal"
      alert.informativeText = "No Metal-capable graphics device was found."
      alert.runModal()
      NSApp.terminate(nil)
      return
    }
    controller = c
    NSApp.mainMenu = buildMenu(c)

    let host = NSHostingView(rootView: ContentView(controller: c))
    host.sizingOptions = []
    let w = NSWindow(contentRect: NSRect(x: 0, y: 0, width: opt.width, height: opt.height),
                     styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
    w.title = "Living Marble"
    w.appearance = NSAppearance(named: .darkAqua)
    w.backgroundColor = .black
    w.contentMinSize = NSSize(width: 800, height: 500)
    w.collectionBehavior = [.fullScreenPrimary]
    w.contentView = host
    w.isReleasedWhenClosed = false
    w.delegate = self
    w.center()
    w.makeKeyAndOrderFront(nil)
    w.makeFirstResponder(c.view)
    window = w
    NSApp.activate()
    if opt.fullscreen { w.toggleFullScreen(nil) }
  }

  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

  func applicationWillTerminate(_ notification: Notification) {
    controller?.shutdown()
  }

  /// Minimised, hidden, covered or on another Space: nothing is drawn until it shows again.
  func windowDidChangeOcclusionState(_ notification: Notification) {
    guard let w = notification.object as? NSWindow else { return }
    controller?.setWindowVisible(w.occlusionState.contains(.visible))
  }

  // ---------------------------------------------------------------------------
  private func buildMenu(_ c: GameController) -> NSMenu {
    let main = NSMenu()

    @discardableResult
    func add(_ menu: NSMenu, _ title: String, _ action: Selector?, _ key: String = "",
             _ mods: NSEvent.ModifierFlags = .command, tag: Int = 0, target: AnyObject? = nil) -> NSMenuItem {
      let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
      item.keyEquivalentModifierMask = mods
      item.tag = tag
      item.target = target
      menu.addItem(item)
      return item
    }
    func submenu(_ title: String, _ menu: NSMenu) {
      let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
      item.submenu = menu
      main.addItem(item)
    }

    // application menu
    let app = NSMenu(title: "Living Marble")
    add(app, "About Living Marble", #selector(NSApplication.orderFrontStandardAboutPanel(_:)))
    app.addItem(.separator())
    add(app, "Hide Living Marble", #selector(NSApplication.hide(_:)), "h")
    add(app, "Hide Others", #selector(NSApplication.hideOtherApplications(_:)), "h", [.command, .option])
    add(app, "Show All", #selector(NSApplication.unhideAllApplications(_:)))
    app.addItem(.separator())
    add(app, "Quit Living Marble", #selector(NSApplication.terminate(_:)), "q")
    submenu("Living Marble", app)

    // game
    let game = NSMenu(title: "Game")
    add(game, "New Game", #selector(GameController.newGameAction(_:)), "n", target: c)
    add(game, "Undo Move", #selector(GameController.undoAction(_:)), "z", target: c)
    add(game, "Hint", #selector(GameController.hintAction(_:)), "t", [], target: c)
    game.addItem(.separator())
    for m in Mode.allCases {
      add(game, m.title, #selector(GameController.modeAction(_:)), "\(m.rawValue + 1)", tag: m.rawValue, target: c)
    }
    game.addItem(.separator())
    let opponent = NSMenu(title: "Opponent")
    for (i, level) in kAILevels.enumerated() {
      add(opponent, level.name, #selector(GameController.levelAction(_:)), tag: i, target: c)
    }
    let oppItem = NSMenuItem(title: "Opponent", action: nil, keyEquivalent: "")
    oppItem.submenu = opponent
    game.addItem(oppItem)
    let pieces = NSMenu(title: "Pieces")
    for (i, name) in ["Living Marble", "Medieval", "Aliens"].enumerated() {
      add(pieces, name, #selector(GameController.piecesAction(_:)), tag: i, target: c)
    }
    let piecesItem = NSMenuItem(title: "Pieces", action: nil, keyEquivalent: "")
    piecesItem.submenu = pieces
    game.addItem(piecesItem)
    game.addItem(.separator())
    // single-letter shortcuts are handled by the game itself; listing them documents them
    add(game, "Battle Animations", #selector(GameController.toggleBattlesAction(_:)), "b", [], target: c)
    add(game, "Cinematic Camera", #selector(GameController.toggleCinematicAction(_:)), "c", [], target: c)
    add(game, "Music", #selector(GameController.toggleMusicAction(_:)), "m", [], target: c)
    submenu("Game", game)

    // view
    let view = NSMenu(title: "View")
    add(view, "Flip View", #selector(GameController.flipAction(_:)), "f", [], target: c)
    add(view, "Reset View", #selector(GameController.resetViewAction(_:)), "r", [], target: c)
    add(view, "Hide Interface", #selector(GameController.toggleInterfaceAction(_:)), "h", [], target: c)
    view.addItem(.separator())
    let quality = NSMenu(title: "Graphics Quality")
    for (i, name) in ["Low", "Medium", "High", "Ultra"].enumerated() {
      add(quality, name, #selector(GameController.qualityAction(_:)), tag: i, target: c)
    }
    let qualityItem = NSMenuItem(title: "Graphics Quality", action: nil, keyEquivalent: "")
    qualityItem.submenu = quality
    view.addItem(qualityItem)
    add(view, "Depth of Field", #selector(GameController.toggleDepthOfFieldAction(_:)), target: c)
    add(view, "Save Power", #selector(GameController.toggleSavePowerAction(_:)), target: c)
    view.addItem(.separator())
    add(view, "Save Screenshot", #selector(GameController.screenshotAction(_:)), "s", [.command, .shift], target: c)
    view.addItem(.separator())
    add(view, "Enter Full Screen", #selector(NSWindow.toggleFullScreen(_:)), "f", [.command, .control])
    submenu("View", view)

    // window
    let window = NSMenu(title: "Window")
    add(window, "Minimize", #selector(NSWindow.performMiniaturize(_:)), "m")
    add(window, "Zoom", #selector(NSWindow.performZoom(_:)))
    submenu("Window", window)
    NSApp.windowsMenu = window

    // help
    let help = NSMenu(title: "Help")
    add(help, "Controls and Battles", #selector(GameController.helpAction(_:)), "?", target: c)
    submenu("Help", help)
    NSApp.helpMenu = help

    return main
  }
}
