// Choreography for the medieval army. Men-at-arms march, knights canter and
// leap, thrones and the gatehouse glide over the board. Captures:
//
//   Pawn    spear thrust, shield bash, javelin
//   Knight  joust (the lance shatters), trample (the horse rears and crashes
//           down), sword sweep (the rider cuts the victim in two in passing)
//   Bishop  wrath of heaven (the organ plays the Dies irae, a storm gathers
//           over the victim and lightning strikes it), brimstone (burning
//           stones rain from a red cloud), pillar of salt (a bell tolls and
//           the victim turns to salt and crumbles)
//   Rook    siege stomp (the gatehouse stands up on stone legs, walks over and
//           stamps on the victim), dragon fire and dragon's jaws (the
//           drawbridge drops and a dragon crawls out)
//   Queen   throne charge, sceptre smite, sceptre sweep
//   King    royal cleave, throne ram, sweeping blow
//
// Kings and queens fight on foot: they get up from their thrones, walk to the
// victim, strike, and the empty throne glides after them so they can sit back down.
// The battles themselves live in BattlesMedievalPawnKnightBishop.swift and
// BattlesMedievalRookRoyal.swift.
import Foundation
import simd

let kTau: Float = 6.2831853
let kGold = vec3(1.0, 0.82, 0.45)

func approach(_ x: Float, _ target: Float, _ rate: Float) -> Float {
  x < target ? min(target, x + rate) : max(target, x - rate)
}
func approachAngle(_ a: Float, _ target: Float, _ rate: Float) -> Float {
  let d = wrapAngle(target - a)
  return wrapAngle(a + clamp(d, -rate, rate))
}
/// World position of a point that rides on a bone (rest-pose model coordinates).
func bonePoint(_ a: Actor, _ bone: Int, _ rest: vec3) -> vec3 {
  vec3(a.model() * Medieval.boneMatrix(a.pose, bone) * vec4(rest, 1))
}
func puppetPoint(_ p: Puppet, _ bone: Int, _ rest: vec3) -> vec3 {
  vec3(p.model() * Medieval.boneMatrix(p.pose, bone) * vec4(rest, 1))
}
/// Where a standing royal's body is (the actor's origin stays where the throne was).
func bodyPos(_ a: Actor) -> vec3 { a.pos + a.forward() * Medieval.standOffset(a.kind) * (1 - a.ctl.sit) * a.scale }

/// Which half-cycle of a gait we are in (a footstep falls on every change).
@inline(__always) private func halfCycle(_ gait: Float) -> Int {
  let x = floor(gait / kPI)
  return x.isFinite ? Int(x) : 0
}

func spark(_ p: vec3, _ v: vec3, _ col: vec3, _ size: Float, _ life: Float) -> Particle {
  var q = Particle()
  q.pos = p
  q.vel = v
  q.size = size
  q.color = col
  q.colorEnd = col * 0.2
  q.opacity = 0
  q.maxLife = life
  q.type = .spark
  q.stretch = 0.03
  q.drag = 1
  q.gravity = 6
  q.bounce = true
  q.fadeIn = 0
  return q
}

/// Wooden splinters (a lance shattering).
func splinters(_ w: World, _ at: vec3, _ dir: vec3, _ n: Int) {
  for _ in 0..<n {
    let d = normalize(dir + w.fx.rndDir() * 0.9 + vec3(0, 0.4, 0))
    let speed = w.fx.rnd(1.5, 4.5)
    let col = vec3(0.45, 0.3, 0.16) * w.fx.rnd(0.6, 1.2)
    let size = w.fx.rnd(0.006, 0.012)
    var p = spark(at, d * speed, col, size, w.fx.rnd(0.8, 1.6))
    p.colorEnd = p.color
    p.opacity = 1
    p.stretch = 0.02
    p.gravity = 9.8
    p.drag = 0.6
    w.fx.add(p)
  }
}

/// A jagged lightning bolt from 'a' to 'b' with side branches, a flash and thunder.
func lightning(_ w: World, _ a: vec3, _ b: vec3, _ power: Float) {
  let col = vec3(2.2, 2.6, 4.0)
  var prev = a
  let segs = 9
  for i in 1...segs {
    let t = Float(i) / Float(segs)
    var p = mix(a, b, t)
    if i < segs { p += vec3(w.fx.rnd(-1, 1), 0, w.fx.rnd(-1, 1)) * 0.09 * power }
    w.fx.bolt(prev, p, col * (2 + power), 0.025, 4)
    if i < segs - 1 && w.fx.rnd() < 0.45 {
      let br = p + vec3(w.fx.rnd(-1, 1), w.fx.rnd(-0.6, -0.1), w.fx.rnd(-1, 1)) * 0.35
      w.fx.bolt(p, br, col * 1.2, 0.03, 5)
    }
    prev = p
  }
  w.fx.flash(b, 0.7 * power, vec3(5.0, 6.0, 9.0), 0.12)
  w.light(b + vec3(0, 0.5, 0), vec3(30.0, 36.0, 55.0) * power, 6, 0.3)
  w.light((a + b) * 0.5, vec3(20.0, 24.0, 40.0) * power, 6, 0.18)
  w.addFlash(vec3(0.25, 0.28, 0.35) * power)
}

/// A dark storm cloud: billowing smoke that slowly turns about its centre.
func cloudPuffs(_ w: World, _ c: vec3, _ radius: Float, _ rate: Float, _ dt: Float, _ tint: vec3,
                _ glowAmt: Float) {
  let n = rate * dt
  let count = Int(n) + (w.fx.rnd() < n - floor(n) ? 1 : 0)
  for _ in 0..<max(0, count) {
    var p = Particle()
    let a = w.fx.rnd(0, kTau)
    let r = radius * sqrt(w.fx.rnd())
    let d = vec3(cos(a), 0, sin(a))
    // a flat-bottomed, billowing cloud: dark underneath, paler where the tops catch the light
    let h = w.fx.rnd(-0.1, 0.22) * (1 - 0.6 * r / max(radius, 1e-3))
    p.pos = c + d * r + vec3(0, h * radius / 0.6, 0)
    let swirl = vec3(-d.z, 0, d.x) * w.fx.rnd(0.12, 0.3)
    let spread = d * w.fx.rnd(-0.02, 0.06)
    p.vel = swirl + spread + vec3(0, w.fx.rnd(-0.01, 0.06), 0)
    p.size = w.fx.rnd(0.14, 0.34) * radius / 0.6
    p.grow = w.fx.rnd(0.04, 0.12)
    let lit = clamp((h + 0.1) / 0.32, 0, 1)
    p.color = tint * w.fx.rnd(0.6, 1.0) * (0.5 + 2.6 * lit * lit)
    p.colorEnd = p.color * 0.8
    p.opacity = w.fx.rnd(0.55, 0.85)
    p.maxLife = w.fx.rnd(2.2, 3.4)
    p.type = .smoke
    p.drag = 0.6
    p.rot = w.fx.rnd(0, 6.28)
    p.rotVel = w.fx.rnd(-0.3, 0.3)
    p.seed = w.fx.rnd()
    p.fadeIn = 0.5
    w.fx.add(p)
  }
  if glowAmt > 0 && w.fx.rnd() < 0.3 {
    var g = Particle()
    g.pos = c + w.fx.rndDir() * radius * 0.5
    g.vel = vec3(0)
    g.size = radius * w.fx.rnd(0.5, 0.9)
    g.color = vec3(1.6, 0.5, 0.12) * glowAmt
    g.colorEnd = g.color * 0.2
    g.opacity = 0
    g.maxLife = 0.4
    g.type = .glow
    g.fadeIn = 0.1
    w.fx.add(g)
  }
}

/// Rain streaking down from a cloud onto the board, with tiny splashes.
func rain(_ w: World, _ c: vec3, _ radius: Float, _ rate: Float, _ dt: Float) {
  let n = rate * dt
  let count = Int(n) + (w.fx.rnd() < n - floor(n) ? 1 : 0)
  for _ in 0..<max(0, count) {
    let a = w.fx.rnd(0, kTau), r = radius * sqrt(w.fx.rnd())
    var p = Particle()
    p.pos = c + vec3(cos(a) * r, -0.1, sin(a) * r)
    p.vel = vec3(0.3, -6.5, 0.1) * w.fx.rnd(0.85, 1.15)
    p.size = 0.004
    p.color = vec3(0.35, 0.4, 0.5) * w.fx.rnd(0.6, 1.2)
    p.colorEnd = p.color
    p.opacity = 0
    p.maxLife = (c.y - 0.1) / 6.5
    p.type = .spark
    p.stretch = 0.012
    p.drag = 0
    p.fadeIn = 0.02
    w.fx.add(p)
    if w.fx.rnd() < 0.3 {
      var s = p
      s.pos = vec3(p.pos.x, 0.01, p.pos.z)
      s.vel = vec3(w.fx.rnd(-0.3, 0.3), w.fx.rnd(0.3, 0.6), w.fx.rnd(-0.3, 0.3))
      s.maxLife = 0.15
      s.gravity = 6
      s.stretch = 0.004
      w.fx.add(s)
    }
  }
}

/// A jet of dragon fire from the mouth along 'dir'.
func fireJet(_ w: World, _ mouth: vec3, _ dir: vec3, _ len: Float, _ dt: Float) {
  let n = max(1, Int(dt * 260))
  for _ in 0..<n {
    var p = Particle()
    let d = normalize(dir + w.fx.rndDir() * 0.13)
    p.pos = mouth + d * w.fx.rnd(0, 0.05)
    p.vel = d * w.fx.rnd(2.6, 3.6) * len
    p.size = w.fx.rnd(0.03, 0.06)
    p.grow = w.fx.rnd(0.35, 0.6)
    p.color = vec3(7.0, 3.6, 1.1) * w.fx.rnd(0.7, 1.3)
    p.colorEnd = vec3(0.7, 0.1, 0.02)
    p.opacity = 0
    p.maxLife = w.fx.rnd(0.3, 0.45)
    p.type = .smoke
    p.drag = 2.2
    p.gravity = -0.8
    p.rot = w.fx.rnd(0, 6.28)
    p.rotVel = w.fx.rnd(-2, 2)
    p.seed = w.fx.rnd()
    p.fadeIn = 0.02
    w.fx.add(p)
  }
  if w.fx.rnd() < 0.5 { w.fx.embers(mouth + dir * 0.3, 2, 1.5) }
  if w.fx.rnd() < 0.25 { w.fx.smoke(mouth + dir * len * 0.9 + vec3(0, 0.2, 0), 1, 0.1, 0.6, 0.8) }
}

// ---------------------------------------------------------------------------
// Walker: moves a piece from where it stands to 'to' - on foot, on horseback,
// on stone legs, or gliding (thrones and the gatehouse).
// ---------------------------------------------------------------------------
struct Walker {
  enum Mode { case foot, horse, castle, slide }
  var actor = -1
  var from = vec3(0), to = vec3(0)
  var mode = Mode.foot
  var speed: Float = 0.8, accel: Float = 1.6, cycle: Float = 0.3, run: Float = 0
  var s: Float = 0, v: Float = 0, len: Float = 0, turnWait: Float = 0, trail: Float = 0
  var lastStep = 0
  var begun = false, face = true, soundOn = true

  init() {}
  init(_ a: Int, _ dest: vec3, _ m: Mode, _ spd: Float) {
    actor = a
    to = flat(dest)
    mode = m
    speed = spd
  }

  mutating func begin(_ w: World) {
    let a = w.actors[actor]
    from = flat(a.pos)
    len = length(to - from)
    cycle = Medieval.strideCycle(a.kind)
    accel = mode == .slide ? speed * 2.2 : speed * 2.0
    if face && len > 0.05 {
      a.yawTarget = faceAngle(to - from)
      turnWait = min(0.55, abs(wrapAngle(a.yawTarget - a.yaw)) * 0.3)
    }
    lastStep = halfCycle(a.ctl.gait)
    begun = true
    if mode == .slide && soundOn && len > 0.05 {
      if a.kind == Kind.rook {
        w.sound(.grind, 0.75, a.pos, w.fx.rnd(0.9, 1.05))
      } else {
        w.sound(.slide, 0.6, a.pos, a.kind == Kind.king ? 0.85 : w.fx.rnd(0.95, 1.1))
      }
    }
  }

  func footstep(_ w: World, _ a: Actor) {
    if !soundOn { return }
    let p = flat(a.pos)
    switch mode {
    case .foot:
      if a.kind == Kind.pawn {
        w.sound(.armor, 0.45, p, w.fx.rnd(0.9, 1.15))
      } else {
        w.sound(.step, 0.4, p, w.fx.rnd(0.85, 0.95))
      }
    case .horse:
      w.sound(.hoof, 0.5 + 0.3 * run, p, w.fx.rnd(0.9, 1.1))
      if run > 0.5 { w.fx.dust(p, 2, 0.15, 0.6) }
    case .castle:
      w.sound(.stomp, 0.8, p, w.fx.rnd(0.9, 1.1))
      w.fx.dust(p + a.forward() * 0.05, 4, 0.22, 0.7)
      w.shake(0.14)
    case .slide:
      break
    }
  }

  @discardableResult
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if !begun { begin(w) }
    let a = w.actors[actor]
    if turnWait > 0 {
      turnWait -= dt
      if mode == .foot || mode == .horse {  // shuffle round on the spot
        a.ctl.gait += dt * 4
        a.ctl.stride = approach(a.ctl.stride, 0.25, dt * 2)
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
    if mode == .slide {
      trail -= dt
      if trail <= 0 && v > 0.2 {
        trail = 0.06
        w.fx.dust(p, a.kind == Kind.rook ? 3 : 1, a.radius() * 0.8, 0.25,
                  a.kind == Kind.rook ? vec3(0.55, 0.5, 0.45) : vec3(0.5, 0.45, 0.4))
      }
    } else {
      a.ctl.gait += v * dt / cycle * kTau
      a.ctl.stride = clamp(v / max(0.1, speed * 0.6), 0, 1)
      if mode == .horse { a.ctl.run = run }
      let step = halfCycle(a.ctl.gait)
      if step != lastStep {
        lastStep = step
        footstep(w, a)
      }
    }
    if s >= len - 1e-4 {
      a.ctl.stride = approach(a.ctl.stride, 0, dt * 4)
      if mode == .horse { a.ctl.run = approach(a.ctl.run, 0, dt * 3) }
      return a.ctl.stride <= 0.001
    }
    return false
  }
}

/// The empty throne glides after its owner, who sits back down and turns to face the enemy.
struct ThroneReturn {
  var st = 0
  var t: Float = 0
  var seatFrom = vec3(0), dest = vec3(0)
  var yawFrom: Float = 0
  var walk = Walker()

  mutating func update(_ w: World, _ a: Actor, _ V: vec3, _ dt: Float) -> Bool {
    t += dt
    switch st {
    case 0:  // walk onto the square (if not there yet)
      if t <= dt + 1e-6 { walk = Walker(a.id, V, .foot, 0.7) }
      if walk.update(w, dt) {
        st = 1
        t = 0
        seatFrom = a.seatPos
        yawFrom = a.seatYaw
        dest = V
        if length(flat(seatFrom - dest)) > 0.05 { w.sound(.slide, 0.7, seatFrom, 0.9) }
      }
    case 1:  // the throne follows
      let d = length(flat(dest - seatFrom))
      let dur = clamp(d * 0.5, 0.35, 1.4)
      let u = clamp(t / dur, 0, 1)
      a.seatPos = mix(seatFrom, dest, easeInOut(u))
      a.seatPos.y = 0
      a.seatYaw = yawFrom + wrapAngle(a.yaw - yawFrom) * easeInOut(u)
      if w.fx.rnd() < 0.5 && u < 1 { w.fx.dust(a.seatPos, 1, 0.25, 0.2) }
      if u >= 1 {
        st = 2
        t = 0
      }
    case 2:  // and sits down
      a.ctl.sit = easeInOut(t / 0.75)
      a.ctl.stride = 0
      if t >= 0.75 {
        a.ctl.sit = 1
        a.seatFree = false
        a.yawTarget = a.baseYaw
        a.yawRate = 3.5
        w.sound(.thud, 0.35, a.pos, 1.3)
        st = 3
        t = 0
      }
    case 3:  // swivel round to face the enemy again
      a.yawRate = 3.5
      return t > 0.7
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Quiet moves (and castling)
// ---------------------------------------------------------------------------
final class MQuietMove: Staged {
  let plan: MovePlan
  var walker = Walker(), rookWalker = Walker()
  var A = vec3(0), B = vec3(0)
  var dur: Float = 1
  var slideSound = false
  init(_ p: MovePlan) { plan = p }

  /// Canter on the spot, gather, leap over whatever is in the way, land.
  private func knightLeap(_ w: World, _ a: Actor, _ dt: Float) -> Bool {
    switch phase {
    case 0:
      if enter() {
        A = flat(a.pos)
        B = squareCenter(Int(plan.move.to))
        a.yawTarget = faceAngle(B - A)
        w.sound(.hoof, 0.4, A, 1.0)
      }
      a.ctl.gait += dt * 7
      a.ctl.stride = approach(a.ctl.stride, 0.5, dt * 2)
      a.ctl.run = 0.4
      if pt > 0.45 { next() }
    case 1:  // gather
      let u = clamp(pt / 0.22, 0, 1)
      a.ctl.stride = approach(a.ctl.stride, 0, dt * 4)
      a.ctl.rear = 0.25 * sin(u * kPI * 0.5)
      a.ctl.crouch = u
      if u >= 1 {
        w.sound(.whoosh, 0.5, a.pos, 0.7)
        w.fx.dust(a.pos, 8, 0.3, 0.9)
        dur = 0.75 + 0.08 * length(B - A)
        next()
      }
    case 2:  // the leap
      let u = clamp(pt / dur, 0, 1)
      var p = mix(A, B, easeInOut(u) * 0.8 + u * 0.2)
      p.y = 4 * u * (1 - u) * 0.62
      a.pos = p
      a.ctl.rear = 0.25 * (1 - u)
      a.ctl.tuck = sin(u * kPI)
      a.ctl.crouch = 0
      if u >= 1 {
        a.pos = B
        a.ctl.tuck = 0
        w.sound(.hoof, 0.9, B, 0.85)
        w.sound(.thud, 0.7, B, 1.2)
        w.fx.dust(B, 12, 0.35, 1.1)
        w.shake(0.1)
        next()
      }
    case 3:  // settle, a shake of the head
      if enter() { a.yawTarget = a.baseYaw }
      a.ctl.headPitch = -0.25 * sin(clamp(pt / 0.5, 0, 1) * kPI)
      a.ctl.gait += dt * 5
      a.ctl.stride = 0.25 * (1 - clamp(pt / 0.5, 0, 1))
      if pt > 0.55 {
        a.ctl.headPitch = 0
        a.ctl.stride = 0
        return true
      }
    default:
      break
    }
    return false
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    if a.kind == Kind.knight { return knightLeap(w, a, dt) }
    switch phase {
    case 0:
      if enter() {
        var m = Walker.Mode.slide
        var spd: Float = 1.1
        if a.kind == Kind.pawn {
          m = .foot
          spd = 0.8
        } else if a.kind == Kind.rook {
          spd = 0.9
        } else if a.kind == Kind.king {
          spd = 0.8
        } else if a.kind == Kind.bishop {
          spd = 1.2
        }
        let dist = length(flat(squareCenter(Int(plan.move.to)) - a.pos))
        if m == .slide { spd = max(spd, dist / 1.5) }  // long glides go faster
        walker = Walker(plan.mover, squareCenter(Int(plan.move.to)), m, spd)
        if m == .slide { walker.face = false }
        A = flat(a.pos)
        B = squareCenter(Int(plan.move.to))
        if plan.rook >= 0 {
          rookWalker = Walker(plan.rook, squareCenter(plan.rookTo), .slide, 0.9)
          rookWalker.face = false
        }
      }
      var d1: Bool
      if plan.rook >= 0 {
        // castling: the king's throne swings out in an arc while the gatehouse slides past
        let u = clamp(pt / 1.6, 0, 1), e = easeInOut(u)
        let toCenter = vec3(0, 0, a.color == 0 ? -1 : 1)
        let W = (A + B) * 0.5 + toCenter * 0.8
        let pa = (1 - e) * (1 - e) * A
        let pw = 2 * (1 - e) * e * W
        let p = pa + pw + e * e * B
        a.pos = vec3(p.x, 0, p.z)
        if !slideSound {
          slideSound = true
          w.sound(.slide, 0.6, a.pos, 0.85)
        }
        d1 = u >= 1
      } else {
        d1 = walker.update(w, dt)
      }
      var d2 = true
      if plan.rook >= 0 { d2 = pt > 0.3 ? rookWalker.update(w, dt) : false }
      // a little life on the way: royals sway, bishops raise the crozier
      if Medieval.throned(a.kind) { a.ctl.lean = -0.06 * sin(clamp(pt / 1.0, 0, 1) * kPI) }
      if d1 && d2 { next() }
    case 1:
      if enter() {
        a.yawTarget = a.baseYaw
        a.ctl.lean = 0
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
// Shared prelude and epilogue for medieval battles
// ---------------------------------------------------------------------------
class MBattle: BattleBase {
  var walker = Walker()
  var ret = ThroneReturn()
  var standAt = vec3(0)

  /// Walk / glide / gallop onto the target square.
  func moveInM(_ w: World, _ dt: Float, _ m: Walker.Mode, _ speed: Float) -> Bool {
    if enter() {
      walker = Walker(plan.mover, squareCenter(Int(plan.move.to)), m, speed)
      if m == .slide { walker.face = false }
    }
    return walker.update(w, dt)
  }
  /// A royal gets up from its throne (which stays where it is).
  func standUp(_ w: World, _ dur: Float = 0.8) -> Bool {
    let a = w.actors[plan.mover]
    if enter() {
      a.seatFree = true
      a.seatPos = a.pos
      a.seatYaw = a.yaw
      w.sound(.slide, 0.3, a.pos, 1.5)
    }
    a.ctl.sit = 1 - easeInOut(pt / dur)
    return pt >= dur
  }
  func finishM(_ w: World) -> Bool {
    let a = w.actors[plan.mover]
    let done = finish(w)
    if done {
      a.pos = squareCenter(Int(plan.move.to))
      a.resetControls()
      a.heat = 0
    }
    return done
  }
}

// ---------------------------------------------------------------------------
// Fast capture (battle animations off)
// ---------------------------------------------------------------------------
final class MQuickCapture: MBattle {
  override func step(_ w: World, _ dt: Float) -> Bool {
    let v = w.actors[plan.victim]
    let a = w.actors[plan.mover]
    switch phase {
    case 0:
      if enter() { setup(w) }
      v.tremble = 1
      if pt > 0.15 {
        w.shatter(v, victimCenter(w), dir * 1.5, 1.4, 0.8)
        w.shake(0.2)
        next()
      }
    case 1:
      let m: Walker.Mode = a.kind == Kind.pawn ? .foot : (a.kind == Kind.knight ? .horse : .slide)
      if moveInM(w, dt, m, a.kind == Kind.pawn ? 1.2 : 1.8) { next() }
    case 2:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Checkmate: the king rises, staggers, and topples forward off his throne.
// ---------------------------------------------------------------------------
final class MCheckmate: Staged {
  let king: Int, winner: Int
  var theta: Float = 0, omega: Float = 0
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
        w.sound(.bell, 0.6, k.pos, 0.8)
        if w.cinematic {
          // in front of the king (a little to the player's side), so we see his face
          var p = CamPose()
          p.target = k.pos + vec3(0, 0.8, 0) + k.forward() * 0.3
          let player = normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4))
          let back = normalize(k.forward() * 1.0 + player * 0.45 + vec3(1e-4))
          p.pos = p.target + back * 3.4 + vec3(0, 1.1, 0)
          p.fov = 32
          p.focus = length(p.pos - p.target)
          p.aperture = 7
          w.cam.setShot(p, 0.9)
        }
        if k.hasSeat {
          k.seatFree = true
          k.seatPos = k.pos
          k.seatYaw = k.yaw
        }
      }
      let u = clamp(pt / 1.6, 0, 1)
      if k.hasSeat { k.ctl.sit = 1 - easeInOut(clamp(pt / 0.9, 0, 1)) }
      k.ctl.lean = 0.15 * sin(pt * 5) * u
      k.ctl.headPitch = 0.3 * u
      k.tremble = 0.3 + 0.6 * u
      k.crackColor = red
      k.crack = easeIn(u) * 0.9
      k.glow = vec4(red, 0.6 * u)
      k.idleWeight = 0
      w.frameLights.append(PointLight(k.pos + vec3(0, 0.8, 0), 3, red * (3 + 10 * u)))
      if u >= 1 {
        k.tiltPivot = vec3(0, 0, Medieval.standOffset(k.kind) + 0.12)
        omega = 1.2
        w.sound(.rumble, 0.8, k.pos, 0.8)
        next()
      }
    case 1:  // topple forward
      k.tremble = 0.2
      omega += 8 * sin(theta + 0.2) * dt
      theta += omega * dt
      k.tilt = angleAxis(theta, vec3(1, 0, 0))
      if theta > 1.2 {
        let f = k.forward()
        let top = k.pos + f * (k.height() * 0.7) + vec3(0, 0.3, 0)
        k.seatFree = false  // the throne breaks with him
        k.seatPos = k.pos
        k.seatYaw = k.yaw
        w.shatter(k, top, f * omega * 0.7, 1.2, 0.7, 6)
        w.fx.dust(k.pos, 30, 0.8, 2)
        w.groundRing(k.pos, red * 1.5, 3, 0.9, 0.1)
        w.light(k.pos + vec3(0, 0.5, 0), red * 20, 4, 0.5)
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
// Factories
// ---------------------------------------------------------------------------
private let kMedievalStyleNames: [[String]] = [
  ["Spear thrust", "Shield bash", "Javelin"],
  ["Joust", "Trample", "Sword sweep"],
  ["Wrath of heaven", "Brimstone", "Pillar of salt"],
  ["Siege stomp", "Dragon fire", "Dragon's jaws"],
  ["Throne charge", "Sceptre smite", "Sceptre sweep"],
  ["Royal cleave", "Throne ram", "Sweeping blow"],
]

func medievalBattleStyleName(_ kind: Int, _ style: Int) -> String {
  if kind < 0 || kind >= Kind.count || style < 0 || style >= kBattleStyles { return "" }
  return kMedievalStyleNames[kind][style]
}

func makeMedievalBattle(_ kind: Int, _ style: Int, _ plan: MovePlan) -> Anim {
  switch kind * kBattleStyles + style {
  case Kind.pawn * kBattleStyles + 0: return PawnThrust(plan)
  case Kind.pawn * kBattleStyles + 1: return PawnBash(plan)
  case Kind.pawn * kBattleStyles + 2: return PawnJavelin(plan)
  case Kind.knight * kBattleStyles + 0: return KnightJoustM(plan)
  case Kind.knight * kBattleStyles + 1: return KnightTrampleM(plan)
  case Kind.knight * kBattleStyles + 2: return KnightSweepM(plan)
  case Kind.bishop * kBattleStyles + 0: return BishopWrath(plan)
  case Kind.bishop * kBattleStyles + 1: return BishopBrimstone(plan)
  case Kind.bishop * kBattleStyles + 2: return BishopSalt(plan)
  case Kind.rook * kBattleStyles + 0: return RookStomp(plan)
  case Kind.rook * kBattleStyles + 1: return RookDragon(plan, true)
  case Kind.rook * kBattleStyles + 2: return RookDragon(plan, false)
  case Kind.queen * kBattleStyles + 0: return ThroneRam(plan, false)
  case Kind.queen * kBattleStyles + 1: return RoyalStrike(plan, .smite)
  case Kind.queen * kBattleStyles + 2: return RoyalStrike(plan, .sweep)
  case Kind.king * kBattleStyles + 0: return RoyalStrike(plan, .cleave)
  case Kind.king * kBattleStyles + 1: return ThroneRam(plan, true)
  default: return RoyalStrike(plan, .blow)
  }
}

func makeMedievalQuiet(_ plan: MovePlan) -> Anim { MQuietMove(plan) }
func makeMedievalQuickCapture(_ plan: MovePlan) -> Anim { MQuickCapture(plan) }
func makeMedievalCheckmate(_ kingId: Int, _ winnerColor: Int) -> Anim { MCheckmate(kingId, winnerColor) }
