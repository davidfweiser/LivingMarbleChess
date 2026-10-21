// The Metal view that shows the board and turns touches into taps on squares,
// piece drags and camera moves. On iPad a pointer or Pencil also hovers,
// trackpads and mice scroll to zoom, and a keyboard works as on the Mac.
//
//   one finger    tap: select a piece, then tap a glowing square to move it
//                 drag a piece onto a glowing square to move it there
//                 drag anywhere else: turn the board (from above, the side or underneath)
//   two fingers   pinch: zoom   twist: turn the board   slide up/down: tilt
import MetalKit
import UIKit

@MainActor
final class GameView: MTKView {
  weak var controller: GameController?

  /// What hovers over the scene, in points with a bottom-left origin (like the Mac version):
  /// the finger carrying a piece, or an iPad pointer or Pencil. Lights up the square below.
  private(set) var mouse: CGPoint?

  private enum Gesture {
    case idle
    case pending    // one finger down, not yet a tap or a drag
    case orbit      // one finger turns the board
    case pieceDrag  // one finger carries a piece
    case twoFinger  // pinch, twist and tilt
  }
  private var gesture = Gesture.idle
  private var fingers: [UITouch] = []  // at most two are followed
  private var pressPoint = CGPoint.zero, lastPoint = CGPoint.zero
  private var lastSpan: CGFloat = 0, lastAngle: CGFloat = 0, lastCenter = CGPoint.zero

  /// How far (points) a finger may wander before a tap becomes a drag.
  private static let slop: CGFloat = 10

  override init(frame: CGRect, device: MTLDevice?) {
    super.init(frame: frame, device: device)
    isMultipleTouchEnabled = true
    addGestureRecognizer(UIHoverGestureRecognizer(target: self, action: #selector(hovered(_:))))
    // trackpad and mouse-wheel scrolling zoom; fingers are left to the touch handlers below
    let scroll = UIPanGestureRecognizer(target: self, action: #selector(scrolled(_:)))
    scroll.allowedScrollTypesMask = .all
    scroll.allowedTouchTypes = []
    scroll.cancelsTouchesInView = false
    addGestureRecognizer(scroll)
  }

  required init(coder: NSCoder) {
    fatalError("GameView is created in code")
  }

  /// UIKit points (top-left origin) to the controller's convention (bottom-left origin).
  private func flipped(_ p: CGPoint) -> CGPoint { CGPoint(x: p.x, y: bounds.height - p.y) }

  /// Radians per point of finger travel: a drag across the short side of the view is half a turn.
  private var orbitRate: Float { kPI / Float(max(200, min(bounds.width, bounds.height))) }

  // ---------------------------------------------------------------------------
  // Touches
  // ---------------------------------------------------------------------------
  override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
    if !isFirstResponder { becomeFirstResponder() }
    for t in touches where fingers.count < 2 { fingers.append(t) }
    if fingers.count == 2 {
      if gesture != .twoFinger {
        // a second finger turns any one-finger gesture into pinch/twist/tilt (a carried piece
        // stays selected but is not moved)
        gesture = .twoFinger
        mouse = nil
        let (span, angle, center) = twoFingerShape()
        (lastSpan, lastAngle, lastCenter) = (span, angle, center)
      }
    } else if fingers.count == 1 && gesture == .idle {
      gesture = .pending
      pressPoint = fingers[0].location(in: self)
      lastPoint = pressPoint
      mouse = nil
    }
  }

  override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
    switch gesture {
    case .pending:
      let p = fingers[0].location(in: self)
      guard hypot(p.x - pressPoint.x, p.y - pressPoint.y) > Self.slop else { return }
      if let c = controller, c.beginPieceDrag(at: flipped(pressPoint), in: bounds.size) {
        gesture = .pieceDrag
        mouse = flipped(p)
      } else {
        gesture = .orbit
        orbit(to: p)
      }
    case .orbit:
      orbit(to: fingers[0].location(in: self))
    case .pieceDrag:
      mouse = flipped(fingers[0].location(in: self))
    case .twoFinger:
      twoFingerMoved()
    case .idle:
      break
    }
  }

  override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
    finish(touches, cancelled: false)
  }

  override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
    finish(touches, cancelled: true)
  }

  private func finish(_ touches: Set<UITouch>, cancelled: Bool) {
    let lifted = fingers.filter { touches.contains($0) }
    guard let first = lifted.first else { return }  // an extra finger we never followed
    switch gesture {
    case .pending where !cancelled:
      // a tap: where the finger came down is where the player aimed
      controller?.click(at: flipped(pressPoint), in: bounds.size)
    case .pieceDrag:
      mouse = nil
      if !cancelled { controller?.endPieceDrag(at: flipped(first.location(in: self)), in: bounds.size) }
    default:
      break
    }
    fingers.removeAll { touches.contains($0) }
    if fingers.isEmpty {
      gesture = .idle
    } else if gesture == .twoFinger {
      // one finger left after a pinch keeps turning the board
      gesture = .orbit
      lastPoint = fingers[0].location(in: self)
    }
  }

  /// One finger: dragging sideways turns the board, dragging down raises the camera (as on the Mac).
  private func orbit(to p: CGPoint) {
    let k = orbitRate
    controller?.world.cam.orbit(-Float(p.x - lastPoint.x) * k, Float(p.y - lastPoint.y) * k * 0.67, 0)
    lastPoint = p
  }

  private func twoFingerShape() -> (span: CGFloat, angle: CGFloat, center: CGPoint) {
    let a = fingers[0].location(in: self), b = fingers[1].location(in: self)
    return (hypot(b.x - a.x, b.y - a.y), atan2(b.y - a.y, b.x - a.x), CGPoint(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2))
  }

  /// Two fingers: spreading them zooms in, twisting turns the board with them, and sliding
  /// both down raises the camera.
  private func twoFingerMoved() {
    let (span, angle, center) = twoFingerShape()
    let zoom = span > 1 && lastSpan > 1 ? -Float(log(span / lastSpan)) : 0
    // screen y points down, so a clockwise twist grows the angle and should turn the board clockwise
    let twist = wrapAngle(Float(angle - lastAngle))
    controller?.world.cam.orbit(twist, Float(center.y - lastCenter.y) * orbitRate * 0.67, zoom)
    (lastSpan, lastAngle, lastCenter) = (span, angle, center)
  }

  // ---------------------------------------------------------------------------
  // iPad pointer, Pencil hover and scrolling
  // ---------------------------------------------------------------------------
  @objc private func hovered(_ g: UIHoverGestureRecognizer) {
    guard gesture == .idle else { return }
    switch g.state {
    case .began, .changed: mouse = flipped(g.location(in: self))
    default: mouse = nil
    }
  }

  @objc private func scrolled(_ g: UIPanGestureRecognizer) {
    let dy = g.translation(in: self).y
    g.setTranslation(.zero, in: self)
    // content follows the fingers: scrolling down zooms in, as on the Mac
    if g.state == .began || g.state == .changed { controller?.world.cam.orbit(0, 0, -Float(dy) * 0.008) }
  }

  // ---------------------------------------------------------------------------
  // Hardware keyboard: the Mac's keys (arrows, N, U, F, R, B, C, H, ?, Esc, F1, F12, ⌘Z, ⌘N)
  // ---------------------------------------------------------------------------
  override var canBecomeFirstResponder: Bool { true }

  override func didMoveToWindow() {
    super.didMoveToWindow()
    if window != nil { becomeFirstResponder() }
  }

  override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
    let rest = presses.filter { !handle($0, down: true) }
    if !rest.isEmpty { super.pressesBegan(rest, with: event) }
  }

  override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
    let rest = presses.filter { !handle($0, down: false) }
    if !rest.isEmpty { super.pressesEnded(rest, with: event) }
  }

  override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
    let rest = presses.filter { !handle($0, down: false) }
    if !rest.isEmpty { super.pressesCancelled(rest, with: event) }
  }

  /// Returns true if the key is one of the game's.
  private func handle(_ press: UIPress, down: Bool) -> Bool {
    guard let key = press.key, let c = controller else { return false }
    switch key.keyCode {
    case .keyboardLeftArrow: c.setArrow(.left, down: down)
    case .keyboardRightArrow: c.setArrow(.right, down: down)
    case .keyboardUpArrow: c.setArrow(.up, down: down)
    case .keyboardDownArrow: c.setArrow(.down, down: down)
    default:
      guard down else { return false }
      let mods = key.modifierFlags.intersection([.command, .control, .alternate])
      let ch = key.charactersIgnoringModifiers.lowercased()
      if mods == .command { return ch == "z" ? c.letterKey("u") : ch == "n" ? c.letterKey("n") : false }
      if !mods.isEmpty { return false }
      switch key.keyCode {
      case .keyboardEscape: c.escape()
      case .keyboardF1: c.model.showHelp.toggle()
      case .keyboardF12: c.requestScreenshot()
      default: return c.letterKey(ch)
      }
    }
    return true
  }
}
