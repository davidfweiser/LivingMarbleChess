// The game: flow of play, AI turns, input, scene building and capture tools.
// It drives the Metal view once per display refresh.
import AppKit
import MetalKit
import simd

enum Mode: Int, CaseIterable {
  case humanWhite = 0, humanBlack, twoPlayer, watch
  var title: String {
    switch self {
    case .humanWhite: return "You play White"
    case .humanBlack: return "You play Black"
    case .twoPlayer: return "Two players"
    case .watch: return "Watch AI vs AI"
    }
  }
}

@MainActor
final class GameController: NSObject, MTKViewDelegate, NSMenuItemValidation {
  let opt: Options
  let model = GameModel()
  let device: MTLDevice
  let renderer: Renderer
  let view: GameView
  let game = Game()
  let world = World()
  let audio = Audio()
  let ai = AIEngine()

  private let queue: MTLCommandQueue
  private let lib = Library()
  private let progress = BuildProgress()
  private var boardAlbedo = TexImage(), boardDetail = TexImage()
  private var assetsReady = false
  private var loading = true

  private var anims: [Anim] = []
  private var announcePending = false
  private var selected = -1
  private var selMoves: [Move] = []
  private var hover = -1
  private var lastMove = Move()
  private var hasLastMove = false
  private var aiPending = false
  private var aiDelay: Float = 0
  private var promoFrom = -1, promoTo = -1
  private var gameOverShown = false
  private var bannerText = ""
  private var bannerTime: Float = 0
  private var viewM = mat4.identity, projM = mat4.identity

  private let fixedStep: Bool
  private var frameCount = 0
  private var battleTime: Float = 0
  private var nextShot = 0
  private var idleAfter: Float = 0
  private let startTime = CACurrentMediaTime()
  private var prevTime: CFTimeInterval = 0
  private var fps: Float = 0
  private var keysDown = Set<UInt16>()
  private var keyMonitor: Any?
  private var screenshotRequested = false
  private var quitRequested = false
  private var loadingShotTaken = false

  init?(options o: Options) {
    opt = o
    guard let dev = MTLCreateSystemDefaultDevice(), let q = dev.makeCommandQueue(),
          let r = Renderer(device: dev, settings: RenderSettings.preset(clamp(o.gfxPreset, 0, 3)))
    else { return nil }
    device = dev
    queue = q
    renderer = r
    view = GameView(frame: CGRect(x: 0, y: 0, width: o.width, height: o.height), device: dev)
    fixedStep = !o.shotPath.isEmpty || !o.recordDir.isEmpty || o.battle >= 0
    super.init()

    model.gfxPreset = clamp(o.gfxPreset, 0, 3)
    if o.demo { model.mode = .watch }
    if o.level >= 0 { model.aiLevel = min(4, o.level) }
    model.noUI = o.noUI
    view.controller = self
    view.delegate = self
    view.colorPixelFormat = Renderer.outputPixelFormat
    view.colorspace = CGColorSpace(name: CGColorSpace.sRGB)
    view.framebufferOnly = false  // screenshots read the drawable back
    view.preferredFramesPerSecond = 120
    print("[metal] \(renderer.info)")
    if !o.noAudio && audio.start() {
      world.audio = audio
      audio.setVolume(model.volume)
      model.audioOK = true
    } else {
      print("[audio] no audio device - running silent")
    }
    world.lib = lib
    world.reset()
    loadAssets()
    keyMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp]) { [weak self] e in
      guard let self else { return e }
      return self.handleKey(e) ? nil : e
    }
  }

  func shutdown() {
    ai.cancel()
    audio.shutdown()
    if let keyMonitor { NSEvent.removeMonitor(keyMonitor) }
    keyMonitor = nil
  }

  private func loadAssets() {
    let lib = self.lib, progress = self.progress, quality = opt.quality
    DispatchQueue.global(qos: .userInitiated).async { [weak self] in
      MeshGen.buildLibrary(lib, quality: quality, progress: progress)
      progress.stage = "Inlaying the board"
      let res = quality == 0 ? 1024 : (quality == 1 ? 2048 : 4096)
      let board = Textures.bakeBoard(res: res, half: lib.boardHalf)
      DispatchQueue.main.async {
        guard let self else { return }
        self.boardAlbedo = board.albedo
        self.boardDetail = board.detail
        self.assetsReady = true
      }
    }
  }

  // ---------------------------------------------------------------------------
  // Game flow
  // ---------------------------------------------------------------------------
  var animating: Bool { !anims.isEmpty }

  var isHumanTurn: Bool {
    let side = game.board.side
    switch model.mode {
    case .humanWhite: return side == Chess.white
    case .humanBlack: return side == Chess.black
    case .twoPlayer: return true
    case .watch: return false
    }
  }

  func newGame() {
    anims.removeAll()
    ai.cancel()
    aiPending = false
    model.promoPending = false
    gameOverShown = false
    announcePending = false
    hasLastMove = false
    deselect()
    if !opt.fen.isEmpty {
      _ = game.setFEN(opt.fen)
    } else {
      game.reset()
    }
    world.reset()
    world.cam.release(0.6)
    world.spawnFromBoard(game.board.sq, true)
    world.sound(.chime, 0.6)
    world.cam.setSide(model.mode == .humanBlack ? 1 : 0)
    bannerText = ""
  }

  func undo() {
    if animating || model.promoPending { return }
    if game.plyCount == 0 { return }
    ai.cancel()
    aiPending = false
    var n = (model.mode == .twoPlayer || model.mode == .watch) ? 1 : 2
    if game.result != .ongoing && !isHumanTurn { n = 1 }
    var i = 0
    while i < n && game.plyCount > 0 {
      game.undo()
      i += 1
    }
    // make sure the human is to move again
    if !isHumanTurn && game.plyCount > 0 && model.mode != .watch { game.undo() }
    gameOverShown = false
    bannerText = ""
    deselect()
    hasLastMove = game.plyCount > 0
    if hasLastMove, let m = game.moves.last { lastMove = m }
    world.chunks.removeAll()
    world.props.removeAll()
    world.cam.release(0.6)
    world.spawnFromBoard(game.board.sq, true)
    world.sound(.chime, 0.5, vec3(0), 0.8)
  }

  func flipCamera() { world.cam.yaw = wrapAngle(world.cam.yaw + kPI) }

  func resetView() {
    world.cam.setSide(model.mode == .humanBlack ? 1 : model.mode == .twoPlayer ? game.board.side : 0)
    world.cam.pitch = 0.80
    world.cam.dist = 12.6
  }

  func setMode(_ m: Mode) {
    model.mode = m
    ai.cancel()
    aiPending = false
    setSlowOrbit(m == .watch)
    if m == .humanWhite { world.cam.setSide(0) }
    if m == .humanBlack { world.cam.setSide(1) }
    deselect()
  }

  func setSlowOrbit(_ on: Bool) {
    model.slowOrbit = on
    world.cam.autoSpin = on ? 0.05 : 0
  }

  func applyGraphicsPreset(_ p: Int) {
    model.gfxPreset = p
    renderer.applySettings(RenderSettings.preset(p))
  }

  func setVolume(_ v: Float) {
    model.volume = v
    audio.setVolume(v)
  }

  func choosePromotion(_ type: Int) {
    if let m = Chess.findLegal(game.board, promoFrom, promoTo, type) { commitMove(m) }
    model.promoPending = false
  }

  func cancelPromotion() {
    model.promoPending = false
    deselect()
  }

  private var statusText: String {
    let r = game.result
    if r != .ongoing { return r.text }
    let white = game.board.side == Chess.white
    var s: String
    if !isHumanTurn && aiPending {
      s = (white ? "White" : "Black") + " is thinking..."
    } else if isHumanTurn && model.mode != .twoPlayer {
      s = "Your move"
    } else {
      s = (white ? "White" : "Black") + " to move"
    }
    if game.board.inCheck() { s += "  -  Check!" }
    return s
  }

  private func deselect() {
    selected = -1
    selMoves.removeAll()
  }

  private func select(_ sq: Int) {
    selected = sq
    selMoves = game.board.genLegal().filter { Int($0.from) == sq }
    world.sound(.select, 0.7, squareCenter(sq))
  }

  private func startAIIfNeeded() {
    if aiPending || game.result != .ongoing || isHumanTurn { return }
    ai.start(game.board, game.hashHistory, model.aiLevel)
    aiPending = true
    aiDelay = model.mode == .watch ? 0.7 : 0.15
  }

  private func commitMove(_ m: Move) {
    var plan = MovePlan()
    plan.move = m
    plan.mover = world.actorAt[Int(m.from)]
    if plan.mover < 0 {
      // visual state out of sync - rebuild
      game.play(m)
      world.spawnFromBoard(game.board.sq, false)
      return
    }
    if m.isCapture {
      let cs = m.captureSquare
      plan.victim = world.actorAt[cs]
      world.actorAt[cs] = -1
    }
    if m.isCastle {
      let from = Int(m.from), to = Int(m.to)
      let rf = to > from ? from + 3 : from - 4
      let rt = to > from ? from + 1 : from - 1
      plan.rook = world.actorAt[rf]
      plan.rookTo = rt
      world.actorAt[rf] = -1
      world.actorAt[rt] = plan.rook
      if plan.rook >= 0 { world.actors[plan.rook].square = rt }
    }
    world.actorAt[Int(m.from)] = -1
    world.actorAt[Int(m.to)] = plan.mover
    world.actors[plan.mover].square = Int(m.to)
    if m.isPromo {
      plan.promoKind = Int(m.promo) - 1
      plan.promoSquare = Int(m.to)
    }
    plan.fast = !model.battles
    plan.style = opt.style
    if plan.victim < 0 && m.isCapture { plan.fast = true }
    world.stopFidgets()
    game.play(m)
    lastMove = m
    hasLastMove = true
    deselect()
    anims.append(makeMoveAnim(world, plan))
    announcePending = true
    startAIIfNeeded()
  }

  private func updateGame(_ dt: Float) {
    if let a = anims.first {
      if a.update(world, dt) { anims.removeFirst() }
      return
    }
    if announcePending {
      announcePending = false
      let b = game.board
      if game.result == .ongoing && b.inCheck() {
        world.sound(.check, 0.9)
        bannerText = "Check!"
        bannerTime = 2
      }
      if model.mode == .twoPlayer && model.autoRotate && game.result == .ongoing { world.cam.setSide(b.side) }
    }
    let r = game.result
    if r != .ongoing {
      if !gameOverShown {
        gameOverShown = true
        bannerText = r.text
        bannerTime = 1e9
        if r == .whiteMates || r == .blackMates {
          let loser = r == .whiteMates ? Chess.black : Chess.white
          let kid = world.actorAt[game.board.kingSq[loser]]
          if kid >= 0 { anims.append(makeCheckmateAnim(world, kid, loser ^ 1)) }
        } else {
          anims.append(makeStalemateAnim(world))
        }
      }
      return
    }
    if !isHumanTurn {
      if !aiPending { startAIIfNeeded() }
      if aiPending && ai.done {
        aiDelay -= dt
        if aiDelay <= 0 {
          let m = ai.result
          aiPending = false
          if let legal = Chess.findLegal(game.board, Int(m.from), Int(m.to), Int(m.promo)) { commitMove(legal) }
        }
      }
    }
  }

  // ---------------------------------------------------------------------------
  // Input
  // ---------------------------------------------------------------------------
  private var canAct: Bool {
    anims.isEmpty && isHumanTurn && game.result == .ongoing && !model.promoPending
  }

  /// Square under a point of the view (points, bottom-left origin), or -1.
  private func pickSquare(_ p: CGPoint, _ size: CGSize, _ piecesFirst: Bool) -> Int {
    if size.width <= 0 || size.height <= 0 { return -1 }
    let x = Float(2 * p.x / size.width - 1), y = Float(2 * p.y / size.height - 1)
    let inv = (projM * viewM).inverse
    let a = inv * vec4(x, y, 0, 1), b = inv * vec4(x, y, 1, 1)
    let ro = a.xyz / a.w, re = b.xyz / b.w
    if ro.y <= 0 { return -1 }  // looking at the underside of the board
    let rd = normalize(re - ro)
    var bestT: Float = 1e9
    var best = -1
    if piecesFirst {
      for ac in world.actors {
        if !ac.alive || !ac.visible || ac.square < 0 { continue }
        let r = ac.radius() * 0.85, H = ac.height()
        let o = ro - ac.pos
        let A = rd.x * rd.x + rd.z * rd.z
        let B = 2 * (o.x * rd.x + o.z * rd.z)
        let C = o.x * o.x + o.z * o.z - r * r
        let disc = B * B - 4 * A * C
        if disc < 0 || A < 1e-8 { continue }
        let t = (-B - sqrt(disc)) / (2 * A)
        let yy = o.y + rd.y * t
        if t > 0 && yy >= 0 && yy <= H {
          if t < bestT {
            bestT = t
            best = ac.square
          }
        } else if abs(rd.y) > 1e-5 {  // top cap
          let tc = (H - o.y) / rd.y
          let q = o + rd * tc
          if tc > 0 && q.x * q.x + q.z * q.z < r * r && tc < bestT {
            bestT = tc
            best = ac.square
          }
        }
      }
    }
    if abs(rd.y) > 1e-5 {
      let t = -ro.y / rd.y
      let q = ro + rd * t
      if t > 0 && t < bestT && abs(q.x) < 4 && abs(q.z) < 4 {
        best = Chess.makeSq(min(7, Int(floor(q.x + 4))), min(7, Int(floor(4 - q.z))))
      }
    }
    return best
  }

  /// A left click that didn't turn into a drag.
  func click(at p: CGPoint, in size: CGSize) {
    guard !loading, canAct else { return }
    let sq = pickSquare(p, size, true)
    if sq < 0 {
      deselect()
      return
    }
    let b = game.board
    for m in selMoves where Int(m.to) == sq {
      if m.isPromo {
        model.promoPending = true
        promoFrom = Int(m.from)
        promoTo = Int(m.to)
      } else {
        commitMove(m)
      }
      return
    }
    if b.sq[sq] != 0 && Chess.pieceColor(Int(b.sq[sq])) == b.side {
      if sq == selected {
        deselect()
      } else {
        select(sq)
      }
    } else {
      deselect()
    }
  }

  private static let arrowKeys: Set<UInt16> = [123, 124, 125, 126]

  /// Single-key game controls (command shortcuts are left to the menus). Returns true if handled.
  private func handleKey(_ e: NSEvent) -> Bool {
    if !e.modifierFlags.intersection([.command, .control, .option]).isEmpty { return false }
    if Self.arrowKeys.contains(e.keyCode) {
      if e.type == .keyDown {
        keysDown.insert(e.keyCode)
      } else {
        keysDown.remove(e.keyCode)
      }
      return true
    }
    guard e.type == .keyDown else { return false }
    if e.isARepeat { return true }
    switch e.keyCode {
    case 53:  // Esc
      if model.showHelp {
        model.showHelp = false
      } else if model.promoPending {
        cancelPromotion()
      } else {
        deselect()
      }
      return true
    case 122:  // F1
      model.showHelp.toggle()
      return true
    case 111:  // F12
      screenshotRequested = true
      return true
    default:
      break
    }
    if loading { return false }
    switch e.charactersIgnoringModifiers?.lowercased() {
    case "n": newGame()
    case "u": undo()
    case "f": flipCamera()
    case "r": resetView()
    case "c": model.cinematic.toggle()
    case "b": model.battles.toggle()
    case "h": model.showUI.toggle()
    case "?", "/": model.showHelp.toggle()
    default: return false
    }
    return true
  }

  private func handleInput(_ dt: Float) {
    if view.window?.isKeyWindow != true { keysDown.removeAll() }
    let k = 1.5 * dt
    if keysDown.contains(123) { world.cam.orbit(k, 0, 0) }         // left
    if keysDown.contains(124) { world.cam.orbit(-k, 0, 0) }        // right
    if keysDown.contains(126) { world.cam.orbit(0, k * 0.6, 0) }   // up
    if keysDown.contains(125) { world.cam.orbit(0, -k * 0.6, 0) }  // down
    hover = -1
    // captures must not depend on where the cursor happens to be
    guard canAct, !fixedStep, let m = view.mouse else { return }
    let sq = pickSquare(m, view.bounds.size, true)
    let b = game.board
    let own = sq >= 0 && b.sq[sq] != 0 && Chess.pieceColor(Int(b.sq[sq])) == b.side
    let dest = selMoves.contains { Int($0.to) == sq }
    if own || dest { hover = sq }
  }

  // ---------------------------------------------------------------------------
  // Frame
  // ---------------------------------------------------------------------------
  nonisolated func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

  nonisolated func draw(in view: MTKView) {
    MainActor.assumeIsolated { tick() }
  }

  private func tick() {
    let now = CACurrentMediaTime()
    var dt = prevTime == 0 ? Float(1.0 / 60.0) : Float(now - prevTime)
    prevTime = now
    if dt > 0 { fps = fps * 0.95 + 0.05 / max(dt, 1e-4) }
    dt = min(dt, 0.05)
    if fixedStep && !loading { dt = 1 / 30 }
    frame(dt)
    if quitRequested { finishAndQuit() }
  }

  private func finishLoading() {
    renderer.uploadLibrary(lib)
    renderer.uploadBoardTextures(albedo: boardAlbedo, detail: boardDetail, half: lib.boardHalf)
    boardAlbedo = TexImage()
    boardDetail = TexImage()
    loading = false
    model.loading = false
    if opt.hasCam {
      world.cam.yaw = opt.camYaw
      world.cam.pitch = opt.camPitch
      world.cam.dist = opt.camDist
      world.cam.target = vec3(opt.camTX, opt.camTY, opt.camTZ)
    }
    if opt.battle >= 0 {
      setupBattleDemo()
    } else if !opt.shotPath.isEmpty || !opt.recordDir.isEmpty {
      setupScenario()
      model.mode = opt.demo ? .watch : .twoPlayer
      if opt.forceMove.count >= 4 {
        let u = Array(opt.forceMove.utf8).map { Int($0) }
        let a = Int(UInt8(ascii: "a")), one = Int(UInt8(ascii: "1"))
        let f = Chess.makeSq(u[0] - a, u[1] - one), t = Chess.makeSq(u[2] - a, u[3] - one)
        var pr = 0
        if u.count > 4, let i = Array(" pnbrqk".utf8).firstIndex(of: UInt8(u[4])) { pr = i }
        if let m = Chess.findLegal(game.board, f, t, pr) {
          commitMove(m)
        } else {
          print("forced move \(opt.forceMove) is not legal")
        }
      }
      if opt.selectSq.count == 2 {
        let u = Array(opt.selectSq.utf8).map { Int($0) }
        select(Chess.makeSq(u[0] - Int(UInt8(ascii: "a")), u[1] - Int(UInt8(ascii: "1"))))
      }
    } else {
      newGame()
    }
    if model.mode == .watch { setSlowOrbit(true) }
  }

  private func frame(_ dt: Float) {
    guard let drawable = view.currentDrawable, let cb = queue.makeCommandBuffer() else { return }
    let target = drawable.texture
    let fbW = target.width, fbH = target.height

    if loading {
      if assetsReady {
        finishLoading()
      } else {
        clear(cb, target)
        let total = max(1, progress.total + 1)
        let goal = Double(progress.done) / Double(total)
        model.loadProgress += (goal - model.loadProgress) * 0.15
        let stage = progress.stage
        model.loadStage = (stage.isEmpty ? "Quarrying marble" : stage) + "..."
        if !opt.loadingShot.isEmpty && !loadingShotTaken && progress.done >= progress.total / 2 {
          loadingShotTaken = true
          print("note: the loading screen is drawn by the native interface; \(opt.loadingShot) holds only the 3D layer")
          capture(cb, target, [opt.loadingShot], drawable)
          return
        }
        cb.present(drawable)
        cb.commit()
        return
      }
    }

    handleInput(dt)
    world.cinematic = model.cinematic
    let sdt = world.beginFrame(dt)
    // per-frame visual flags (animations override them)
    for a in world.actors {
      a.tremble = 0
      a.glow = vec4(0)
      a.idleWeight = 1
      a.selected = false
      a.yawRate = 9
    }
    if selected >= 0 && world.actorAt[selected] >= 0 { world.actors[world.actorAt[selected]].selected = true }
    if anims.isEmpty && game.result == .ongoing && game.board.inCheck() {
      let kid = world.actorAt[game.board.kingSq[game.board.side]]
      if kid >= 0 {
        let k = world.actors[kid]
        k.tremble = 0.35
        k.glow = vec4(1.0, 0.15, 0.05, 0.35 + 0.25 * sin(world.time * 7))
      }
    }
    if hover >= 0 && selected >= 0 {
      for m in selMoves where Int(m.to) == hover && m.isCapture {
        let vid = world.actorAt[m.captureSquare]
        if vid >= 0 { world.actors[vid].tremble = 0.5 }
      }
    }
    updateGame(sdt)
    world.fidgetAllowed = anims.isEmpty && !model.promoPending && opt.battle < 0 && opt.shotPath.isEmpty
    world.excludeFidget = selected >= 0 ? world.actorAt[selected] : -1
    world.simulate(sdt)
    world.cam.evaluate(dt, world.trauma, Float(CACurrentMediaTime() - startTime))
    if bannerTime > 0 && bannerTime < 1e8 { bannerTime -= dt }

    // keep the internal resolution sane on very high-DPI screens (Ultra renders native)
    renderer.setRenderScale(model.gfxPreset >= 3 ? 1 : min(1, 1440 / Float(max(1, fbH))))
    var sv = SceneView()
    buildScene(&sv, fbW, fbH)
    if !opt.noRender { renderer.render(sv, commandBuffer: cb, target: target) }
    frameCount += 1
    updateModel()

    // ---- capture tools ----
    var shots: [String] = []
    if screenshotRequested {
      screenshotRequested = false
      shots.append(screenshotPath())
    }
    if opt.battle >= 0 {
      battleTime += sdt
      while nextShot < opt.shotTimes.count && battleTime >= opt.shotTimes[nextShot] {
        shots.append(String(format: "%@_%02d.png", opt.shotPrefix, nextShot))
        nextShot += 1
      }
      if !opt.recordDir.isEmpty { shots.append(String(format: "%@/frame_%04d.png", opt.recordDir, frameCount)) }
      let shotsDone = nextShot >= opt.shotTimes.count
      if anims.isEmpty && battleTime > 1 { idleAfter += sdt }
      if (shotsDone && opt.recordDir.isEmpty) || idleAfter > (opt.recordDir.isEmpty ? 0 : 1.3) || frameCount > 3000 {
        quitRequested = true
      }
    } else if !opt.recordDir.isEmpty {
      shots.append(String(format: "%@/frame_%04d.png", opt.recordDir, frameCount))
      if frameCount >= opt.frames { quitRequested = true }
    } else if !opt.shotPath.isEmpty && frameCount >= opt.frames {
      shots.append(opt.shotPath)
      quitRequested = true
    }
    if opt.noRender && frameCount % 600 == 0 {
      print("[sim] frame \(frameCount) ply \(game.plyCount) chunks \(world.chunks.count) particles \(world.fx.count)")
    }
    if shots.isEmpty {
      cb.present(drawable)
      cb.commit()
    } else {
      capture(cb, target, shots, drawable)
    }
  }

  private func buildScene(_ sv: inout SceneView, _ fbW: Int, _ fbH: Int) {
    let cp = world.cam.current
    let aspect = fbH > 0 ? Float(fbW) / Float(fbH) : 1.6
    sv.nearZ = 0.08
    sv.farZ = 90
    projM = perspective(radians(cp.fov), aspect, sv.nearZ, sv.farZ)
    viewM = lookAt(cp.pos, cp.target, vec3(0, 1, 0))
    sv.view = viewM
    sv.proj = projM
    sv.camPos = cp.pos
    sv.focusDist = cp.focus
    sv.aperture = cp.aperture
    sv.time = world.time
    sv.exposure = model.exposure
    sv.flash = world.flash
    world.buildDrawItems(renderer.pieces, renderer.sword, renderer.cannonball, &sv.items)
    world.collectLights(&sv.lights)
    world.fx.collect(&sv.particles)
    world.collectOccluders(&sv.occluders)
    world.collectRings(&sv.rings, &sv.ringColors)
    for m in selMoves { sv.boardState[Int(m.to) * 4 + 0] = m.isCapture ? 128 : 255 }
    if selected >= 0 { sv.boardState[selected * 4 + 1] = 255 }
    if hasLastMove && anims.isEmpty {
      sv.boardState[Int(lastMove.from) * 4 + 2] = 255
      sv.boardState[Int(lastMove.to) * 4 + 2] = 255
    }
    if hover >= 0 { sv.boardState[hover * 4 + 3] = 255 }
    sv.check = vec4(0)
    if anims.isEmpty && game.result == .ongoing && game.board.inCheck() {
      let k = squareCenter(game.board.kingSq[game.board.side])
      sv.check = vec4(k.x, k.z, 0, 1)
    }
  }

  /// Mirrors the game state into the interface model (only writing what changed).
  private func updateModel() {
    func set<T: Equatable>(_ kp: ReferenceWritableKeyPath<GameModel, T>, _ v: T) {
      if model[keyPath: kp] != v { model[keyPath: kp] = v }
    }
    set(\.status, statusText)
    set(\.whiteToMove, game.board.side == Chess.white)
    if model.san.count != game.san.count || model.san.last != game.san.last { model.san = game.san }
    set(\.canUndo, !animating && game.plyCount > 0)
    let val = [0, 1, 3, 3, 5, 9, 0]
    var mat = [0, 0]
    for s in 0..<64 {
      let p = Int(game.board.sq[s])
      if p != 0 { mat[Chess.pieceColor(p)] += val[Chess.pieceType(p)] }
    }
    let d = mat[0] - mat[1]
    set(\.material, d != 0 ? "\(d > 0 ? "White" : "Black") +\(abs(d))" : "even")
    let showBanner = !bannerText.isEmpty && bannerTime > 0
    set(\.bannerText, showBanner ? bannerText : "")
    set(\.bannerPermanent, bannerTime > 1e8)
    set(\.bannerAlpha, showBanner ? Double(min(1, bannerTime)) : 0)
    if frameCount % 30 == 0 { set(\.fps, Int(fps.rounded())) }
  }

  private func clear(_ cb: MTLCommandBuffer, _ target: MTLTexture) {
    let rp = MTLRenderPassDescriptor()
    rp.colorAttachments[0].texture = target
    rp.colorAttachments[0].loadAction = .clear
    rp.colorAttachments[0].storeAction = .store
    rp.colorAttachments[0].clearColor = MTLClearColor(red: 0.02, green: 0.016, blue: 0.012, alpha: 1)
    cb.makeRenderCommandEncoder(descriptor: rp)?.endEncoding()
  }

  /// Presents the frame and saves it to each path (BGRA read back from the drawable).
  private func capture(_ cb: MTLCommandBuffer, _ target: MTLTexture, _ paths: [String], _ drawable: CAMetalDrawable) {
    let w = target.width, h = target.height, row = w * 4
    guard let buf = device.makeBuffer(length: row * h, options: .storageModeShared),
          let blit = cb.makeBlitCommandEncoder()
    else {
      cb.present(drawable)
      cb.commit()
      return
    }
    blit.copy(from: target, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
              sourceSize: MTLSize(width: w, height: h, depth: 1), to: buf, destinationOffset: 0,
              destinationBytesPerRow: row, destinationBytesPerImage: row * h)
    blit.endEncoding()
    cb.present(drawable)
    cb.commit()
    cb.waitUntilCompleted()
    let px = [UInt8](UnsafeBufferPointer(start: buf.contents().assumingMemoryBound(to: UInt8.self), count: row * h))
    for p in paths {
      if writePNG(p, w, h, bgra: px) {
        print("saved \(p)")
      } else {
        print("could not write \(p)")
      }
    }
  }

  private func screenshotPath() -> String {
    let dir = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Pictures/Living Marble")
    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    let f = DateFormatter()
    f.dateFormat = "yyyy-MM-dd 'at' HH.mm.ss"
    return dir.appendingPathComponent("Living Marble \(f.string(from: Date())).png").path
  }

  private func finishAndQuit() {
    quitRequested = false
    if fixedStep {
      print("moves: " + game.san.joined(separator: " "))
      print("result: \(game.result.text) (\(game.plyCount) plies, \(world.actors.count) actors, \(world.chunks.count) chunks)")
    }
    NSApp.terminate(nil)
  }

  // ---------------------------------------------------------------------------
  // Canned setups for the capture tools
  // ---------------------------------------------------------------------------
  private func setupBattleDemo() {
    let fens = ["r1bqkb1r/ppp2ppp/2n5/3n4/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 0 5",
                "rn1qkbnr/ppp2ppp/4p3/3b4/8/2N2N2/PPPPPPPP/R1BQKB1R w KQkq - 0 4",
                "r1bqk2r/pppp1ppp/4n3/4p3/4P3/1B3N2/PPPP1PPP/RNBQK2R w KQkq - 0 6",
                "rnb1kbnr/ppp2ppp/3q4/4p3/8/2N5/PPPR1PPP/2B1KBNR w Kkq - 0 8",
                "1nb1kbnr/ppp2ppp/3r4/4p3/8/2N5/PPPQ1PPP/R1B1KBNR w KQk - 0 9",
                "8/5pk1/6p1/3n4/4K3/6P1/5P2/8 w - - 0 40"]
    let from = [28, 18, 17, 11, 11, 28]
    let to = [35, 35, 44, 43, 43, 35]
    let k = clamp(opt.battle, 0, 5)
    model.mode = .twoPlayer
    model.autoRotate = false
    _ = game.setFEN(fens[k])
    world.reset()
    world.spawnFromBoard(game.board.sq, false)
    if let m = Chess.findLegal(game.board, from[k], to[k], 0) { commitMove(m) }
    battleTime = 0
    nextShot = 0
  }

  private func setupScenario() {
    if !opt.fen.isEmpty {
      _ = game.setFEN(opt.fen)
    } else if opt.scenario == 1 {
      // a lively middlegame
      _ = game.setFEN("r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP1B1PPP/R2QKB1R w KQ - 0 8")
    } else if opt.scenario == 3 {
      // piece gallery for inspecting the sculpting
      _ = game.setFEN("8/8/8/pnbrqk2/PNBRQK2/8/8/8 w - - 0 1")
    } else if opt.scenario >= 10 && opt.scenario <= 15 {
      // single piece study: 10 pawn .. 15 king on d4 (white) and e5 (black)
      let w = Array("PNBRQK"), b = Array("pnbrqk")
      let k = opt.scenario - 10
      var fen = "k7/8/8/4\(b[k])3/3\(w[k])4/8/8/7K w - - 0 1"
      if k == 5 { fen = "8/8/8/4k3/3K4/8/8/8 w - - 0 1" }
      _ = game.setFEN(fen)
    } else if opt.scenario == 2 {
      _ = game.setFEN("r1b1k2r/ppppqppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 1")
    } else {
      game.reset()
    }
    world.reset()
    world.spawnFromBoard(game.board.sq, false)
  }

  // ---------------------------------------------------------------------------
  // Menu actions
  // ---------------------------------------------------------------------------
  @objc func newGameAction(_ sender: Any?) { if !loading { newGame() } }
  @objc func undoAction(_ sender: Any?) { if !loading { undo() } }
  @objc func flipAction(_ sender: Any?) { flipCamera() }
  @objc func resetViewAction(_ sender: Any?) { resetView() }
  @objc func toggleInterfaceAction(_ sender: Any?) { model.showUI.toggle() }
  @objc func toggleBattlesAction(_ sender: Any?) { model.battles.toggle() }
  @objc func toggleCinematicAction(_ sender: Any?) { model.cinematic.toggle() }
  @objc func helpAction(_ sender: Any?) { model.showHelp.toggle() }
  @objc func screenshotAction(_ sender: Any?) { screenshotRequested = true }
  @objc func modeAction(_ sender: NSMenuItem) {
    if let m = Mode(rawValue: sender.tag) { setMode(m) }
  }
  @objc func levelAction(_ sender: NSMenuItem) { model.aiLevel = sender.tag }
  @objc func qualityAction(_ sender: NSMenuItem) { applyGraphicsPreset(sender.tag) }

  func validateMenuItem(_ item: NSMenuItem) -> Bool {
    switch item.action {
    case #selector(undoAction(_:)):
      return !loading && !animating && game.plyCount > 0
    case #selector(newGameAction(_:)):
      return !loading
    case #selector(modeAction(_:)):
      item.state = model.mode.rawValue == item.tag ? .on : .off
    case #selector(levelAction(_:)):
      item.state = model.aiLevel == item.tag ? .on : .off
      return model.mode != .twoPlayer
    case #selector(qualityAction(_:)):
      item.state = model.gfxPreset == item.tag ? .on : .off
    case #selector(toggleBattlesAction(_:)):
      item.state = model.battles ? .on : .off
    case #selector(toggleCinematicAction(_:)):
      item.state = model.cinematic ? .on : .off
    case #selector(toggleInterfaceAction(_:)):
      item.title = model.showUI ? "Hide Interface" : "Show Interface"
    default:
      break
    }
    return true
  }
}
