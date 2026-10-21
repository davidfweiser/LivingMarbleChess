// The Metal view that shows the board and turns mouse/trackpad input into
// camera moves and clicks on squares.
import AppKit
import MetalKit

@MainActor
final class GameView: MTKView {
  weak var controller: GameController?

  /// Cursor position in view points (bottom-left origin) while it is over the 3D scene.
  private(set) var mouse: CGPoint?
  private var pressPoint = CGPoint.zero, lastPoint = CGPoint.zero
  private var leftHeld = false, leftDrag = false
  private var tracking: NSTrackingArea?

  override var acceptsFirstResponder: Bool { true }
  override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

  override func updateTrackingAreas() {
    super.updateTrackingAreas()
    if let tracking { removeTrackingArea(tracking) }
    let t = NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                           owner: self, userInfo: nil)
    addTrackingArea(t)
    tracking = t
  }

  private func point(_ e: NSEvent) -> CGPoint { convert(e.locationInWindow, from: nil) }

  /// True when the scene itself (not an interface panel above it) is under the cursor.
  private func sceneIsUnder(_ e: NSEvent) -> Bool {
    guard let root = window?.contentView else { return true }
    let hit = root.hitTest(root.convert(e.locationInWindow, from: nil))
    return hit === self || hit == nil
  }

  private func orbit(by p: CGPoint) {
    // dragging right turns the board one way, dragging down raises the camera
    let dx = Float(p.x - lastPoint.x), dyDown = Float(lastPoint.y - p.y)
    controller?.world.cam.orbit(-dx * 0.006, dyDown * 0.004, 0)
  }

  // ---- left button: click selects / moves, drag turns the board ----
  override func mouseDown(with e: NSEvent) {
    window?.makeFirstResponder(self)
    let p = point(e)
    leftHeld = true
    leftDrag = false
    pressPoint = p
    lastPoint = p
  }

  override func mouseDragged(with e: NSEvent) {
    guard leftHeld else { return }
    let p = point(e)
    if !leftDrag && hypot(p.x - pressPoint.x, p.y - pressPoint.y) > 5 { leftDrag = true }
    if leftDrag { orbit(by: p) }
    lastPoint = p
    mouse = p
  }

  override func mouseUp(with e: NSEvent) {
    guard leftHeld else { return }
    leftHeld = false
    if !leftDrag { controller?.click(at: point(e), in: bounds.size) }
  }

  // ---- right / middle button: always turns the board ----
  override func rightMouseDown(with e: NSEvent) { lastPoint = point(e) }
  override func rightMouseDragged(with e: NSEvent) {
    let p = point(e)
    orbit(by: p)
    lastPoint = p
  }
  override func otherMouseDown(with e: NSEvent) { lastPoint = point(e) }
  override func otherMouseDragged(with e: NSEvent) {
    let p = point(e)
    orbit(by: p)
    lastPoint = p
  }

  // ---- zoom: scroll wheel, two-finger scroll, pinch ----
  override func scrollWheel(with e: NSEvent) {
    let dy = Float(e.scrollingDeltaY) * (e.hasPreciseScrollingDeltas ? 0.1 : 1)
    controller?.world.cam.orbit(0, 0, -dy * 0.08)
  }

  override func magnify(with e: NSEvent) {
    controller?.world.cam.orbit(0, 0, -Float(e.magnification) * 1.2)
  }

  // ---- hover ----
  override func mouseMoved(with e: NSEvent) { mouse = sceneIsUnder(e) ? point(e) : nil }
  override func mouseEntered(with e: NSEvent) { mouse = sceneIsUnder(e) ? point(e) : nil }
  override func mouseExited(with e: NSEvent) { mouse = nil }
}
