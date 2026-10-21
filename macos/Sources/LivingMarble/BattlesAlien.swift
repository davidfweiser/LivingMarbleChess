// Choreography for the alien army. Little green men waddle, the saucer flies,
// the psychic, the queen and the overlord float, the tripod walks on its three
// legs. Captures:
//
//   Pawn    ray gun (pew, pew - two misses, then a hit), goo grenade (the victim
//           melts), tongue (shoots out, swallows it whole, burps up the pieces)
//   Knight  abduction (tractor beam: up it goes, spinning, and out it comes in
//           pieces), crop circle (lasers carve a ring, the victim falls in two),
//           cow drop
//   Bishop  mind blast (the victim swells and pops), hypnosis (it dances in a
//           daze, then blasts off like a rocket), telekinetic crush
//   Rook    heat ray, stomp (misses once), tentacle toss
//   Queen   acid spit, brood (an egg hatches a swarm), tail strike
//   King    shrink ray (and a squash), the mothership, disintegrator (it
//           sputters, he taps it, it fires far too hard)
//
// The battles themselves live in BattlesAlienPawnKnightBishop.swift and
// BattlesAlienRookQueenKing.swift (as battle_alien.cpp).
import Foundation
import simd

/// The army's light colour (cyan for white, magenta for black).
func armyLight(_ color: Int) -> vec3 { color == 0 ? vec3(0.25, 1.0, 0.92) : vec3(1.0, 0.18, 0.62) }
/// The colour of its goo.
func armyGoo(_ color: Int) -> vec3 { color == 0 ? vec3(0.35, 0.9, 0.08) : vec3(0.55, 0.12, 0.8) }

/// Which step of a gait we are in (a footstep falls on every change).
@inline(__always) private func gaitStep(_ gait: Float, _ per: Float) -> Int {
  let x = floor(gait / per)
  return x.isFinite ? Int(x) : 0
}

// ---------------------------------------------------------------------------
// AWalker: moves an alien to 'to' - waddling, hovering or striding on three legs.
// ---------------------------------------------------------------------------
struct AWalker {
  enum Mode { case waddle, hover, tripod }
  var actor = -1
  var from = vec3(0), to = vec3(0)
  var mode = Mode.hover
  var speed: Float = 0.8, accel: Float = 1.6, cycle: Float = 0.3, lift: Float = 0.04
  var s: Float = 0, v: Float = 0, len: Float = 0, turnWait: Float = 0
  var lastStep = 0
  var begun = false, face = true, soundOn = true

  init() {}
  init(_ a: Int, _ dest: vec3, _ m: Mode, _ spd: Float) {
    actor = a
    to = flat(dest)
    mode = m
    speed = spd
  }

  static func modeFor(_ kind: Int) -> Mode { kind == Kind.pawn ? .waddle : (kind == Kind.rook ? .tripod : .hover) }

  mutating func begin(_ w: World) {
    let a = w.actors[actor]
    from = flat(a.pos)
    len = length(to - from)
    cycle = Alien.strideCycle(a.kind)
    accel = speed * (mode == .hover ? 2.4 : 2.0)
    if face && len > 0.05 {
      a.yawTarget = faceAngle(to - from)
      turnWait = min(0.45, abs(wrapAngle(a.yawTarget - a.yaw)) * 0.25)
    }
    lastStep = gaitStep(a.ctl.gait, kPI)
    begun = true
    if mode == .hover && soundOn && len > 0.05 { w.sound(.whoosh, 0.35, a.pos, a.kind == Kind.king ? 0.6 : 0.85) }
  }

  func footstep(_ w: World, _ a: Actor) {
    if !soundOn { return }
    let p = flat(a.pos)
    if mode == .waddle {
      w.sound(.step, 0.4, p, w.fx.rnd(1.35, 1.6))
    } else if mode == .tripod {
      w.sound(.stomp, 0.45, p, w.fx.rnd(1.3, 1.5))
      w.sound(.clang, 0.12, p, w.fx.rnd(1.6, 1.9))
      w.fx.dust(p + a.forward() * 0.1, 3, 0.25, 0.5)
      w.shake(0.05)
    }
  }

  @discardableResult
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if !begun { begin(w) }
    let a = w.actors[actor]
    if turnWait > 0 {
      turnWait -= dt
      if mode != .hover {  // shuffle round on the spot
        a.ctl.gait += dt * 5
        a.ctl.stride = approach(a.ctl.stride, 0.3, dt * 2)
      }
      return false
    }
    let rem = len - s
    let vt = min(speed, sqrt(2 * accel * max(rem, 0)))
    v = v < vt ? min(vt, v + accel * dt) : max(vt, v - accel * 2.5 * dt)
    if rem > 1e-3 && v < 0.08 { v = 0.08 }
    s = min(len, s + v * dt)
    let p = len > 1e-4 ? mix(from, to, s / len) : to
    a.pos.x = p.x
    a.pos.z = p.z
    if mode == .hover {
      // rise a little while moving, tip into the direction of travel
      let u = len > 1e-4 ? s / len : 1
      a.ctl.hover = lift * sin(kPI * u)
      a.ctl.lean = approach(a.ctl.lean, 0.12 * clamp(v / max(speed, 0.1), 0, 1) * (u < 0.85 ? 1 : -0.5), dt * 0.8)
      if a.kind == Kind.knight { a.ctl.spin += dt * (2 + 10 * v) }
    } else {
      a.ctl.gait += v * dt / cycle * kTau
      a.ctl.stride = clamp(v / max(0.1, speed * 0.6), 0, 1)
      let step = gaitStep(a.ctl.gait, mode == .tripod ? kTau / 3 : kPI)
      if step != lastStep {
        lastStep = step
        footstep(w, a)
      }
    }
    if s >= len - 1e-4 {
      a.ctl.stride = approach(a.ctl.stride, 0, dt * 4)
      a.ctl.hover = approach(a.ctl.hover, 0, dt * 0.5)
      a.ctl.lean = approach(a.ctl.lean, 0, dt * 1)
      return a.ctl.stride <= 0.001 && abs(a.ctl.hover) < 1e-3 && abs(a.ctl.lean) < 1e-3
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Quiet moves (and castling)
// ---------------------------------------------------------------------------
final class AQuietMove: Staged {
  let plan: MovePlan
  var walker = AWalker(), rookWalker = AWalker()
  var A = vec3(0), B = vec3(0)
  var dur: Float = 1
  init(_ p: MovePlan) { plan = p }

  /// The saucer lifts off, zooms across in an arc (over anything in the way) and settles.
  private func saucerFlight(_ w: World, _ a: Actor, _ dt: Float) -> Bool {
    switch phase {
    case 0:
      if enter() {
        A = flat(a.pos)
        B = squareCenter(Int(plan.move.to))
        a.yawTarget = faceAngle(B - A)
        w.sound(.charge, 0.35, A, 1.6)
      }
      a.ctl.hover = 0.12 * easeInOut(pt / 0.35)
      a.ctl.spin += dt * 10
      if pt > 0.35 {
        dur = 0.55 + 0.12 * length(B - A)
        w.sound(.whoosh, 0.6, a.pos, 1.3)
        next()
      }
    case 1:
      let u = clamp(pt / dur, 0, 1)
      let p = mix(A, B, easeInOut(u))
      a.pos = vec3(p.x, 0, p.z)
      a.ctl.hover = 0.12 + 0.35 * sin(kPI * u)
      a.ctl.lean = 0.3 * sin(kPI * clamp(u * 1.4, 0, 1)) - 0.15 * max(0, u - 0.7) / 0.3
      a.ctl.twist = 0.15 * sin(kTau * u)
      a.ctl.spin += dt * 14
      if w.fx.rnd() < 0.6 {
        stream(w, a.pos + vec3(0, Alien.hoverHeight(a.kind) + a.ctl.hover - 0.07, 0), a.pos - vec3(0, 0.1, 0),
               armyLight(a.color), 1, 2.0)
      }
      if u >= 1 { next() }
    case 2:  // settle with a wobble
      let u = clamp(pt / 0.45, 0, 1)
      a.ctl.hover = 0.12 * (1 - easeInOut(u))
      a.ctl.lean = -0.08 * sin(u * kPI * 2) * (1 - u)
      a.ctl.twist = 0.06 * sin(u * kPI * 3) * (1 - u)
      if u >= 1 {
        a.ctl.hover = 0
        a.ctl.lean = 0
        a.ctl.twist = 0
        a.yawTarget = a.baseYaw
        w.sound(.chime, 0.2, a.pos, 2.0)
        return true
      }
    default:
      break
    }
    return false
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    if a.kind == Kind.knight { return saucerFlight(w, a, dt) }
    switch phase {
    case 0:
      if enter() {
        let m = AWalker.modeFor(a.kind)
        var spd: Float = m == .waddle ? 0.7 : (m == .tripod ? 0.75 : (a.kind == Kind.king ? 0.9 : 1.1))
        let dist = length(flat(squareCenter(Int(plan.move.to)) - a.pos))
        if m == .hover { spd = max(spd, dist / 1.5) }  // long glides go faster
        if m == .tripod { spd = clamp(dist / 3.0, spd, 1.3) }  // and long marches stride out
        walker = AWalker(plan.mover, squareCenter(Int(plan.move.to)), m, spd)
        if m == .hover {
          walker.face = a.kind == Kind.queen
          walker.lift = a.kind == Kind.king ? 0.06 : 0.05
        }
        A = flat(a.pos)
        B = squareCenter(Int(plan.move.to))
        if plan.rook >= 0 { rookWalker = AWalker(plan.rook, squareCenter(plan.rookTo), .tripod, 0.9) }
      }
      var d1: Bool
      if plan.rook >= 0 {
        // castling: the overlord swings out in an arc while the tripod strides past
        let u = clamp(pt / 1.6, 0, 1), e = easeInOut(u)
        let toCenter = vec3(0, 0, a.color == 0 ? -1 : 1)
        let W = (A + B) * 0.5 + toCenter * 1.6
        let pa = (1 - e) * (1 - e) * A
        let pw = 2 * (1 - e) * e * W
        let p = pa + pw + e * e * B
        a.pos = vec3(p.x, 0, p.z)
        a.ctl.hover = 0.08 * sin(kPI * u)
        if pt <= dt + 1e-6 { w.sound(.whoosh, 0.4, a.pos, 0.6) }
        d1 = u >= 1
      } else {
        d1 = walker.update(w, dt)
      }
      var d2 = true
      if plan.rook >= 0 { d2 = pt > 0.2 ? rookWalker.update(w, dt) : false }
      if a.kind == Kind.queen { a.ctl.tail += dt * 2 }
      if d1 && d2 { next() }
    case 1:
      if enter() {
        a.yawTarget = a.baseYaw
        a.ctl.hover = 0
        if plan.rook >= 0 { w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw }
      }
      if pt > 0.25 { return true }
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Shared prelude and epilogue for alien battles
// ---------------------------------------------------------------------------
class ABattle: BattleBase {
  var walker = AWalker()

  // the aliens are small: the camera comes in closer and lower
  override init(_ p: MovePlan) {
    super.init(p)
    shotExtra = -0.7
    shotHeight = 0.42
  }

  func moveInA(_ w: World, _ dt: Float, _ speed: Float = 1) -> Bool {
    let a = w.actors[plan.mover]
    if enter() {
      let m = AWalker.modeFor(a.kind)
      walker = AWalker(plan.mover, squareCenter(Int(plan.move.to)), m, speed)
      if m == .hover { walker.face = false }
    }
    return walker.update(w, dt)
  }
  func finishA(_ w: World) -> Bool {
    let a = w.actors[plan.mover]
    let done = finish(w)
    if done {
      a.pos = squareCenter(Int(plan.move.to))
      a.resetControls()
    }
    return done
  }
}

// ---------------------------------------------------------------------------
// Fast capture (battle animations off): a quick zap
// ---------------------------------------------------------------------------
final class AQuickCapture: ABattle {
  override func step(_ w: World, _ dt: Float) -> Bool {
    let v = w.actors[plan.victim]
    let a = w.actors[plan.mover]
    switch phase {
    case 0:
      if enter() {
        setup(w)
        w.sound(.zap, 0.6, v.pos, 1.5)
      }
      v.tremble = 1
      v.glow = vec4(armyLight(a.color), 1.5 * pt / 0.2)
      if pt > 0.2 {
        w.fx.magicBurst(victimCenter(w), armyLight(a.color) * 3, 30, 1.5)
        w.shatter(v, victimCenter(w), dir * 1.5, 1.4, 0.8)
        w.shake(0.2)
        next()
      }
    case 1:
      if moveInA(w, dt, a.kind == Kind.pawn ? 1.2 : 1.8) { next() }
    case 2:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Checkmate: the overlord's throne sputters and fails; he crashes and cracks.
// ---------------------------------------------------------------------------
final class ACheckmate: Staged {
  let king: Int, winner: Int
  var fall: Float = 0, vy: Float = 0
  init(_ king: Int, _ winner: Int) {
    self.king = king
    self.winner = winner
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let k = w.actors[king]
    let red = vec3(1.0, 0.18, 0.06)
    switch phase {
    case 0:
      if enter() {
        w.sound(.mate, 1, k.pos)
        w.sound(.charge, 0.5, k.pos, 0.5)
        if w.cinematic {
          var p = CamPose()
          p.target = k.pos + vec3(0, 0.7, 0) + k.forward() * 0.3
          let player = normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4))
          let back = normalize(k.forward() * 1.0 + player * 0.45 + vec3(1e-4))
          p.pos = p.target + back * 3.2 + vec3(0, 1.0, 0)
          p.fov = 32
          p.focus = length(p.pos - p.target)
          p.aperture = 7
          w.cam.setShot(p, 0.9)
        }
      }
      // the drive sputters: the throne bobs, sparks fly, smoke pours out
      let u = clamp(pt / 1.8, 0, 1)
      k.ctl.hover = 0.05 * sin(pt * 13) * (1 - u) - 0.03 * u
      k.ctl.lean = 0.08 * sin(pt * 7) * u
      k.ctl.headYaw = 0.5 * sin(pt * 4) * u  // looking about in panic
      k.ctl.perk = -u
      k.ctl.jaw = 0.6 * u
      k.tremble = 0.3 + 0.5 * u
      k.crackColor = red
      k.crack = easeIn(u) * 0.8
      k.glow = vec4(red, 0.5 * u)
      k.idleWeight = 0
      if w.fx.rnd() < dt * 12 {
        let sx = w.fx.rnd(-0.15, 0.15)
        let sz = w.fx.rnd(-0.15, 0.15)
        let s = k.pos + vec3(sx, 0.18, sz)
        w.fx.sparks(s, vec3(0, -0.5, 0), 8, 2.0, 0.8, vec3(4.0, 2.5, 1.0))
        w.sound(.zap, 0.25, s, w.fx.rnd(1.6, 2.2))
      }
      if w.fx.rnd() < dt * 8 { w.fx.smoke(k.pos + vec3(0, 0.15, 0), 1, 0.1, 0.4, 0.8) }
      w.frameLights.append(PointLight(k.pos + vec3(0, 0.6, 0), 3, red * (3 + 10 * u)))
      if u >= 1 {
        vy = 0
        w.sound(.rumble, 0.7, k.pos, 1.3)
        next()
      }
    case 1:  // the drive dies: down he drops
      vy -= 9 * dt
      fall += vy * dt
      k.ctl.hover = -0.03 + fall
      let floorH = -Alien.hoverHeight(Kind.king) + 0.02
      if k.ctl.hover <= floorH {
        k.ctl.hover = floorH
        let top = k.pos + vec3(0, 0.6, 0) + k.forward() * 0.1
        w.shatter(k, top, k.forward() * 1.5, 1.3, 0.8, 6)
        w.fx.dust(k.pos, 30, 0.8, 2)
        w.fx.fireball(k.pos + vec3(0, 0.2, 0), 0.6)
        w.groundRing(k.pos, red * 1.5, 3, 0.9, 0.1)
        w.light(k.pos + vec3(0, 0.5, 0), red * 20, 4, 0.5)
        w.sound(.explode, 0.9, k.pos, 1.1)
        w.shake(0.5)
        w.slowmo(0.35, 0.6)
        next()
      }
    case 2:  // winners celebrate
      for a in w.actors where a.alive && a.color == winner {
        a.selected = pt > (a.phase - floor(a.phase)) * 0.4 && pt < 2.4
      }
      if pt > 2.6 {
        for a in w.actors { a.selected = false }
        if w.cinematic { w.cam.release(1.2) }
        return true
      }
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------
/// Where a piece's weapon fires from (ray gun, saucer belly, brain, eye, sceptre).
func muzzleOf(_ a: Actor) -> vec3 {
  switch a.kind {
  case Kind.pawn: return bonePoint(a, AH.itemR, Alien.muzzle(Kind.pawn))
  case Kind.knight: return bonePoint(a, UB.hatch, Alien.muzzle(Kind.knight))
  case Kind.bishop: return bonePoint(a, AH.head, Alien.muzzle(Kind.bishop))
  case Kind.rook: return bonePoint(a, TB.hood, Alien.muzzle(Kind.rook))
  default: return bonePoint(a, AH.itemR, Alien.muzzle(a.kind))
  }
}
func headOf(_ a: Actor) -> vec3 {
  let bone = a.kind == Kind.knight ? UB.head : (a.kind == Kind.rook ? TB.body : AH.head)
  return bonePoint(a, bone, Alien.headPoint(a.kind))
}

/// A ray-gun bolt flying from a to b.
struct Bolt {
  var a = vec3(0), b = vec3(0), col = vec3(1)
  var t: Float = 0, dur: Float = 0.18
  var active = false
  mutating func fire(_ from: vec3, _ to: vec3, _ c: vec3) {
    a = from
    b = to
    col = c
    t = 0
    dur = max(0.08, length(to - from) / 9)
    active = true
  }
  /// True on the frame it arrives.
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if !active { return false }
    t += dt
    let u = min(1, t / dur)
    let p = mix(a, b, u), tail = mix(a, b, max(0, u - 0.25))
    w.fx.bolt(tail, p, col * 5, 0.004, 3)
    w.frameLights.append(PointLight(p, 1.6, col * 8))
    if u >= 1 {
      active = false
      return true
    }
    return false
  }
}

/// A blob lobbed in an arc (goo, acid): a puppet that wobbles as it flies.
struct Lob {
  var puppet = -1
  var a = vec3(0), b = vec3(0)
  var h: Float = 0.5, t: Float = 0, dur: Float = 0.6, spin: Float = 0
  var active = false
  mutating func throwAt(_ p: Int, _ from: vec3, _ to: vec3, _ height: Float, _ duration: Float) {
    puppet = p
    a = from
    b = to
    h = height
    dur = duration
    t = 0
    active = true
  }
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if !active { return false }
    t += dt
    let u = min(1, t / dur)
    if let p = w.puppet(puppet) {
      p.pos = mix(a, b, u) + vec3(0, 4 * h * u * (1 - u), 0) - vec3(0, 0.05, 0)
      p.yaw += dt * 9
      p.tilt = angleAxis(0.4 * sin(t * 17), vec3(1, 0, 0))
      if w.fx.rnd() < 0.5 {
        var q = Particle()
        q.pos = p.pos + vec3(0, 0.05, 0)
        q.vel = vec3(0, -0.3, 0)
        q.size = w.fx.rnd(0.008, 0.016)
        q.color = armyGoo(p.color) * 0.5
        q.colorEnd = q.color
        q.opacity = 0.9
        q.maxLife = 0.5
        q.type = .spark
        q.gravity = 6
        q.stretch = 0.01
        w.fx.add(q)
      }
    }
    if u >= 1 {
      active = false
      return true
    }
    return false
  }
}

/// A continuous beam (heat ray, disintegrator, lasers).
func beamLine(_ w: World, _ a: vec3, _ b: vec3, _ col: vec3, _ jitter: Float, _ power: Float) {
  w.fx.bolt(a, b, col * (3 + 3 * power), jitter, 7)
  w.fx.bolt(a, b, col * (1.5 + power), jitter * 2.5, 5)
  w.frameLights.append(PointLight(b + vec3(0, 0.15, 0), 2.5, col * (12 * power)))
  w.frameLights.append(PointLight(mix(a, b, 0.4), 2.0, col * (5 * power)))
}

/// A splash of goo.
func gooSplash(_ w: World, _ at: vec3, _ color: Int, _ n: Int) {
  let g = armyGoo(color)
  for _ in 0..<max(0, n) {
    var p = Particle()
    let d = normalize(w.fx.rndDir() + vec3(0, 0.8, 0))
    p.pos = at + d * 0.03
    p.vel = d * w.fx.rnd(0.8, 2.4)
    p.size = w.fx.rnd(0.01, 0.03)
    p.color = g * w.fx.rnd(0.35, 0.6)
    p.colorEnd = p.color * 0.8
    p.opacity = 0.95
    p.maxLife = w.fx.rnd(0.5, 1.0)
    p.type = .spark
    p.gravity = 9
    p.stretch = 0.012
    p.bounce = true
    p.fadeIn = 0
    w.fx.add(p)
  }
  w.fx.flash(at, 0.4, g * 2, 0.12)
}

/// The tractor beam: motes streaming up a column of light, a pool of light below.
func tractorBeam(_ w: World, _ top: vec3, _ bottom: vec3, _ radius: Float, _ col: vec3, _ dt: Float, _ power: Float) {
  let n = 90 * dt * power
  let count = Int(n) + (w.fx.rnd() < n - floor(n) ? 1 : 0)
  for _ in 0..<max(0, count) {
    var p = Particle()
    let a = w.fx.rnd(0, kTau), r = radius * sqrt(w.fx.rnd())
    let h = w.fx.rnd()
    p.pos = mix(bottom, top, h) + vec3(cos(a) * r * (0.6 + 0.4 * (1 - h)), 0, sin(a) * r)
    p.vel = vec3(0, w.fx.rnd(0.6, 1.4), 0)
    p.size = w.fx.rnd(0.008, 0.02)
    p.color = col * w.fx.rnd(1.0, 2.5)
    p.colorEnd = col * 0.3
    p.opacity = 0
    p.maxLife = w.fx.rnd(0.4, 0.8)
    p.type = .glow
    p.drag = 0
    w.fx.add(p)
  }
  // soft glowing haze filling the column
  if w.fx.rnd() < dt * 30 * power {
    var g = Particle()
    g.pos = mix(bottom, top, w.fx.rnd())
    g.vel = vec3(0, 0.3, 0)
    g.size = radius * w.fx.rnd(0.9, 1.4)
    g.color = col * 0.18 * power
    g.colorEnd = col * 0.02
    g.opacity = 0
    g.maxLife = 0.5
    g.type = .glow
    g.fadeIn = 0.15
    w.fx.add(g)
  }
  w.frameLights.append(PointLight(bottom + vec3(0, 0.25, 0), 2.2, col * 7 * power))
}

/// Psychic rings rolling from one point to another.
func psychicRings(_ w: World, _ from: vec3, _ to: vec3, _ col: vec3, _ dt: Float) {
  if w.fx.rnd() < dt * 7 {
    for i in 1...4 { w.fx.ring(mix(from, to, Float(i) / 5), 0.018 + 0.008 * Float(i), col * 1.4, 0.22) }
  }
  if w.fx.rnd() < dt * 20 { stream(w, from, to, col, 1, 1.6) }
}

/// A victim melting into a puddle (u: 0..1).
func melt(_ w: World, _ v: Actor, _ u: Float, _ goo: vec3, _ dt: Float) {
  v.idleWeight = 0
  v.poseSquash = 1 - 0.65 * easeIn(u)
  v.poseBend = vec2(0.12 * sin(u * 17), 0.1 * sin(u * 13)) * u
  v.glow = vec4(goo, 0.4 + 0.8 * u)
  v.dissolveColor = goo * 1.3
  v.dissolve = clamp((u - 0.55) / 0.45, 0, 1)
  if w.fx.rnd() < dt * 14 {
    let px = w.fx.rnd(-0.12, 0.12)
    let pz = w.fx.rnd(-0.12, 0.12)
    let p = v.pos + vec3(px, 0.02, pz)
    w.fx.smoke(p, 1, 0.05, 0.5, 0.0)
    gooSplash(w, p, v.color == 0 ? 1 : 0, 1)
  }
}

/// A saucer hop: from where it is to 'to', rising to 'height' above its resting hover.
struct SaucerHop {
  var from = vec3(0), to = vec3(0)
  var h0: Float = 0, h1: Float = 0, peak: Float = 0.3, t: Float = 0, dur: Float = 1
  var begun = false
  mutating func begin(_ a: Actor, _ dest: vec3, _ endHover: Float, _ arc: Float, _ duration: Float) {
    from = flat(a.pos)
    to = flat(dest)
    h0 = a.ctl.hover
    h1 = endHover
    peak = arc
    dur = duration
    t = 0
    begun = true
  }
  mutating func update(_ w: World, _ a: Actor, _ dt: Float) -> Bool {
    t += dt
    let u = clamp(t / dur, 0, 1), e = easeInOut(u)
    let p = mix(from, to, e)
    a.pos = vec3(p.x, 0, p.z)
    a.ctl.hover = mix(h0, h1, e) + peak * sin(kPI * u)
    a.ctl.lean = 0.25 * sin(kPI * clamp(u * 1.3, 0, 1)) * (length(to - from) > 0.1 ? 1 : 0)
    a.ctl.spin += dt * 12
    if w.fx.rnd() < 0.4 {
      stream(w, a.pos + vec3(0, Alien.hoverHeight(Kind.knight) + a.ctl.hover - 0.07, 0),
             a.pos + vec3(0, Alien.hoverHeight(Kind.knight) + a.ctl.hover - 0.3, 0), armyLight(a.color), 1, 2.0)
    }
    if u >= 1 {
      a.ctl.lean = 0
      return true
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------
private let kAlienStyleNames: [[String]] = [
  ["Ray gun", "Goo grenade", "Tongue"],
  ["Abduction", "Crop circle", "Cow drop"],
  ["Mind blast", "Hypnosis", "Telekinetic crush"],
  ["Heat ray", "Stomp", "Tentacle toss"],
  ["Acid spit", "Brood", "Tail strike"],
  ["Shrink ray", "Mothership", "Disintegrator"],
]

func alienBattleStyleName(_ kind: Int, _ style: Int) -> String {
  if kind < 0 || kind >= Kind.count || style < 0 || style >= kBattleStyles { return "" }
  return kAlienStyleNames[kind][style]
}

func makeAlienBattle(_ kind: Int, _ style: Int, _ plan: MovePlan) -> Anim {
  switch kind * kBattleStyles + style {
  case Kind.pawn * kBattleStyles + 0: return PawnRayGun(plan)
  case Kind.pawn * kBattleStyles + 1: return PawnGoo(plan)
  case Kind.pawn * kBattleStyles + 2: return PawnTongue(plan)
  case Kind.knight * kBattleStyles + 0: return KnightAbduction(plan)
  case Kind.knight * kBattleStyles + 1: return KnightCropCircle(plan)
  case Kind.knight * kBattleStyles + 2: return KnightCowDrop(plan)
  case Kind.bishop * kBattleStyles + 0: return BishopMindBlast(plan)
  case Kind.bishop * kBattleStyles + 1: return BishopHypnosis(plan)
  case Kind.bishop * kBattleStyles + 2: return BishopCrush(plan)
  case Kind.rook * kBattleStyles + 0: return RookHeatRay(plan)
  case Kind.rook * kBattleStyles + 1: return RookStompA(plan)
  case Kind.rook * kBattleStyles + 2: return RookToss(plan)
  case Kind.queen * kBattleStyles + 0: return QueenAcid(plan)
  case Kind.queen * kBattleStyles + 1: return QueenBrood(plan)
  case Kind.queen * kBattleStyles + 2: return QueenTail(plan)
  case Kind.king * kBattleStyles + 0: return KingShrink(plan)
  case Kind.king * kBattleStyles + 1: return KingMothership(plan)
  default: return KingDisintegrator(plan)
  }
}

func makeAlienQuiet(_ plan: MovePlan) -> Anim { AQuietMove(plan) }
func makeAlienQuickCapture(_ plan: MovePlan) -> Anim { AQuickCapture(plan) }
func makeAlienCheckmate(_ kingId: Int, _ winnerColor: Int) -> Anim { ACheckmate(kingId, winnerColor) }
