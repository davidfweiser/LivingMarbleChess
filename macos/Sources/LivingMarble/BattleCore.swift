// Choreography: quiet moves, castling, promotion and the battle framework.
// Every attacking piece type has several battle styles; each capture picks one.
// The battles themselves live in the Battles*.swift files.
import Foundation
import simd

struct MovePlan {
  var move = Move()
  var mover = -1       // actor id
  var victim = -1      // actor id (captures)
  var rook = -1        // actor id (castling)
  var rookTo = -1      // square
  var promoKind = -1   // meshgen kind for promotion
  var promoSquare = -1
  var fast = false     // skip battle theatrics
  var style = -1       // battle style for captures (-1 = pick one at random)
}

let kBattleStyles = 3  // per piece type

enum MoveStyle { case hop, stomp, leap, glide, regal }

func styleFor(_ kind: Int) -> MoveStyle {
  switch kind {
  case Kind.pawn: return .hop
  case Kind.knight: return .leap
  case Kind.bishop: return .glide
  case Kind.rook: return .stomp
  case Kind.queen: return .regal
  default: return .hop
  }
}

func flat(_ v: vec3) -> vec3 { vec3(v.x, 0, v.z) }
func flatDir(_ a: vec3, _ b: vec3) -> vec3 {
  let d = flat(b - a)
  let l = length(d)
  return l > 1e-4 ? d / l : vec3(0, 0, 1)
}

/// Moves one actor between two points with a per-piece personality.
struct Mover {
  var actor = -1
  var from = vec3(0), to = vec3(0)
  var style = MoveStyle.hop
  var t: Float = 0, dur: Float = 1, hopH: Float = 0.1, leapH: Float = 0
  var hops = 1, lastLanded = -1
  var begun = false, faceTravel = true

  init() {}
  init(_ a: Int, _ dest: vec3, _ s: MoveStyle, _ leapHeight: Float = 0) {
    actor = a
    to = dest
    style = s
    leapH = leapHeight
  }

  mutating func begin(_ w: World) {
    let a = w.actors[actor]
    from = a.pos
    from.y = 0
    let dist = length(flat(to - from))
    if faceTravel && dist > 0.05 { a.yawTarget = faceAngle(to - from) }
    let kingSlow: Float = a.kind == Kind.king ? 1.2 : 1.0
    switch style {
    case .hop:
      hops = max(1, Int((dist / 0.62).rounded()))
      dur = Float(hops) * 0.30 * kingSlow
      hopH = a.kind == Kind.king ? 0.10 : 0.13
    case .stomp:
      hops = max(1, Int((dist / 0.78).rounded()))
      dur = Float(hops) * 0.42
      hopH = 0.09
    case .leap:
      hops = 1
      dur = 0.78 + 0.10 * dist
      hopH = leapH > 0 ? leapH : 0.8 + 0.16 * dist
    default:
      hops = 1
      dur = 0.45 + 0.21 * dist
      hopH = 0.07
    }
    if dist < 0.02 { dur = 0.05 }
    begun = true
  }

  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if !begun { begin(w) }
    let a = w.actors[actor]
    t += dt
    let u = clamp(t / dur, 0, 1)
    switch style {
    case .hop, .stomp:
      let hp = u * Float(hops)
      let hi = min(hops - 1, Int(floor(hp)))
      let lu = hp - Float(hi)
      let air = clamp((lu - 0.16) / 0.72, 0, 1)
      let prog = (Float(hi) + easeInOut(air)) / Float(hops)
      a.pos = mix(from, to, prog)
      a.pos.y = 4 * air * (1 - air) * hopH
      a.poseSquash = lu < 0.16 ? mix(1, style == .stomp ? 0.8 : 0.86, lu / 0.16) : 1
      var landed = air >= 1 ? hi : hi - 1
      if u >= 1 { landed = hops - 1 }
      if landed > lastLanded {
        lastLanded = landed
        let p = a.pos
        if style == .stomp {
          w.sound(.thud, 0.9, p, w.fx.rnd(0.9, 1.05))
          w.fx.dust(p, 8, a.radius(), 0.8)
          w.shake(0.10)
        } else {
          w.sound(.step, 0.55, p, a.kind == Kind.pawn ? w.fx.rnd(1.15, 1.3) : w.fx.rnd(0.85, 0.95))
          w.fx.dust(p, 3, a.radius() * 0.8, 0.4)
        }
      }
    case .leap:
      let crouch: Float = 0.2, land: Float = 0.9
      if u < crouch {
        let c = u / crouch
        a.pos = from
        a.poseSquash = mix(1, 0.72, easeOut(c))
        a.poseBend = vec2(0, -0.25 * c)
      } else {
        let air = clamp((u - crouch) / (land - crouch), 0, 1)
        let prog = easeInOut(air) * 0.85 + air * 0.15
        a.pos = mix(from, to, prog)
        a.pos.y = 4 * air * (1 - air) * hopH
        a.poseSquash = 1
        a.poseBend = vec2(0, mix(-0.35, 0.25, air))
        if air >= 1 && lastLanded < 0 {
          lastLanded = 0
          w.sound(.thud, 0.8, a.pos, 1.15)
          w.fx.dust(a.pos, 10, a.radius(), 1.0)
          w.shake(0.08)
        }
        if u >= land { a.poseBend = vec2(0) }
      }
    case .glide, .regal:
      let e = easeInOut(u)
      a.pos = mix(from, to, e)
      a.pos.y = hopH * sin(kPI * u)
      if style == .regal { a.poseTwist = sin(kPI * u) * 0.9 }
      if u >= 1 && lastLanded < 0 {
        lastLanded = 0
        w.sound(.step, 0.45, a.pos, 0.8)
      }
    }
    if u >= 1 {
      a.pos = to
      a.pos.y = 0
      a.poseSquash = 1
      a.poseBend = vec2(0)
      a.poseTwist = 0
      return true
    }
    return false
  }
}

/// Closest distance between segments p1-q1 and p2-q2.
func segSegDist(_ p1: vec3, _ q1: vec3, _ p2: vec3, _ q2: vec3) -> Float {
  let d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2
  let a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r)
  var s: Float = 0, t: Float = 0
  if a <= 1e-8 && e <= 1e-8 { return length(r) }
  if a <= 1e-8 {
    t = clamp(f / e, 0, 1)
  } else {
    let c = dot(d1, r)
    if e <= 1e-8 {
      s = clamp(-c / a, 0, 1)
    } else {
      let b = dot(d1, d2), den = a * e - b * b
      s = den > 1e-8 ? clamp((b * f - c * e) / den, 0, 1) : 0
      t = (b * s + f) / e
      if t < 0 {
        t = 0
        s = clamp(-c / a, 0, 1)
      } else if t > 1 {
        t = 1
        s = clamp((b - c) / a, 0, 1)
      }
    }
  }
  return length((p1 + d1 * s) - (p2 + d2 * t))
}

/// Picks a cinematic camera that keeps both fighters in clear view.
func battleShot(_ w: World, _ A: vec3, _ V: vec3, _ heightBias: Float = 0.55, _ extra: Float = 0,
                _ attacker: Int = -1, _ victim: Int = -1) {
  if !w.cinematic { return }
  let mid = A * 0.42 + V * 0.58
  let dir = flatDir(A, V)
  let L = length(flat(V - A))
  let side = vec3(-dir.z, 0, dir.x)
  let orbitCam = w.cam.orbitPose().pos
  let toViewer = normalize(flat(orbitCam - mid) + vec3(1e-4))
  let target = mid + vec3(0, heightBias, 0)
  let dist = 3.3 + L * 0.45 + extra
  let baseH = 1.0 + L * 0.15 + extra * 0.3
  let look = [A + vec3(0, 0.6, 0), V + vec3(0, 0.5, 0), target]
  var bestScore: Float = 1e9
  var best = CamPose()
  for sgn in stride(from: -1, through: 1, by: 2) {
    for k in -4...4 {
      let ang = radians(18 * Float(k))
      let sd = side * Float(sgn)
      let offDir = vec3(cos(ang) * sd.x - sin(ang) * sd.z, 0, sin(ang) * sd.x + cos(ang) * sd.z)
      for e in 0..<3 {
        let cam = target + offDir * dist * (1 - 0.08 * Float(e)) + vec3(0, baseH + 0.75 * Float(e), 0)
        var occ: Float = 0
        for a in w.actors {
          if !a.alive || !a.visible || a.id == attacker || a.id == victim { continue }
          let b0 = a.pos, b1 = a.pos + vec3(0, a.height(), 0)
          let r = a.radius() * 1.05
          if length(flat(cam - a.pos)) < r + 0.35 && cam.y < b1.y + 0.3 { occ += 6 }
          for p in look {
            let d = segSegDist(cam, p, b0, b1)
            if d < r {
              // pieces close to the lens hide much more of the frame
              let near = length(flat(a.pos - cam)) / max(0.1, length(flat(p - cam)))
              occ += near < 0.5 ? 2.5 : 1.0
            }
          }
        }
        var score = occ * 4
        score += (1 - dot(offDir, toViewer)) * 1.6  // stay on the player's side
        score += abs(dot(offDir, dir)) * 1.2         // prefer a side-on view
        score += Float(e) * 0.6                      // prefer lower, more dramatic angles
        if score < bestScore {
          bestScore = score
          best.pos = cam
        }
      }
    }
  }
  best.target = target
  best.fov = 34
  best.focus = length(best.pos - best.target)
  best.aperture = 7
  w.cam.setShot(best, 0.75)
}

// ---------------------------------------------------------------------------
// Timeline-based helper base
// ---------------------------------------------------------------------------
class Staged: Anim {
  var phase = 0
  var pt: Float = 0, total: Float = 0
  var first = true, entered = false

  func update(_ w: World, _ dt: Float) -> Bool {
    if dt <= 0 { return false }
    pt += dt
    total += dt
    let done = step(w, dt)
    first = false
    return done
  }

  /// Override in subclasses; returns true when finished.
  func step(_ w: World, _ dt: Float) -> Bool { true }

  func next() {
    phase += 1
    pt = 0
    first = true
    entered = false
  }
  func enter() -> Bool {
    if !entered {
      entered = true
      return true
    }
    return false
  }
}

// ---------------------------------------------------------------------------
final class QuietMove: Staged {
  let plan: MovePlan
  var mover = Mover(), rookMover = Mover()
  init(_ p: MovePlan) { plan = p }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    switch phase {
    case 0:
      if enter() {
        mover = Mover(plan.mover, squareCenter(Int(plan.move.to)), styleFor(a.kind))
        if plan.rook >= 0 {
          rookMover = Mover(plan.rook, squareCenter(plan.rookTo), .leap, 0.95)
        }
      }
      let d1 = mover.update(w, dt)
      var d2 = true
      if plan.rook >= 0 {
        d2 = pt > 0.25 ? rookMover.update(w, dt) : false
      }
      if d1 && d2 { next() }
    case 1:
      if enter() {
        a.yawTarget = a.baseYaw
        if plan.rook >= 0 { w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw }
      }
      if pt > 0.12 { return true }
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Shared battle prelude / epilogue
// ---------------------------------------------------------------------------
class BattleBase: Staged {
  let plan: MovePlan
  var A = vec3(0), V = vec3(0), dir = vec3(0)
  var L: Float = 0
  var released = false
  var advance = Mover()
  var shotHeight: Float = 0.55, shotExtra: Float = 0

  init(_ p: MovePlan) { plan = p }

  func setup(_ w: World) {
    A = w.actors[plan.mover].pos
    A.y = 0
    V = w.actors[plan.victim].pos
    V.y = 0
    dir = flatDir(A, V)
    L = length(flat(V - A))
  }
  /// Returns true when the face-off is over.
  func faceOff(_ w: World, _ duration: Float = 0.45) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    if enter() {
      setup(w)
      a.yawTarget = faceAngle(V - A)
      v.yawTarget = faceAngle(A - V)
      battleShot(w, A, V, shotHeight, shotExtra, plan.mover, plan.victim)
    }
    v.tremble = clamp(pt / duration, 0, 1) * 0.6
    v.idleWeight = 0
    return pt >= duration
  }
  func moveIn(_ w: World, _ dt: Float) -> Bool {
    if enter() {
      let s = styleFor(w.actors[plan.mover].kind)
      advance = Mover(plan.mover, squareCenter(Int(plan.move.to)), s)
    }
    return advance.update(w, dt)
  }
  func finish(_ w: World) -> Bool {
    let a = w.actors[plan.mover]
    if enter() {
      a.yawTarget = a.baseYaw
      if w.cinematic { w.cam.release(1.0) }
      a.glow = vec4(0)
      a.tilt = quat.identity
      a.tiltPivot = vec3(0)
      a.poseTwist = 0
      a.idleWeight = 1
    }
    return pt > 0.25
  }
  func victimCenter(_ w: World) -> vec3 {
    let v = w.actors[plan.victim]
    return v.pos + vec3(0, v.height() * 0.5, 0)
  }
}

// ===========================================================================
// Helpers shared by the battle styles
// ===========================================================================

/// Centre distance at which two pieces standing face to face just touch.
func touchDist(_ a: Actor, _ b: Actor) -> Float { (a.radius() + b.radius()) * 0.92 }
func overBoard(_ p: vec3) -> Bool { abs(p.x) < 4.5 && abs(p.z) < 4.5 }

/// A victim knocked into the air: it tumbles backwards and bursts where it
/// lands - or keeps falling if it has been knocked clean off the floating board.
struct Flight {
  var vel = vec3(0)
  var spin: Float = 0, angle: Float = 0, t: Float = 0
  mutating func launch(_ v: Actor, _ velocity: vec3, _ spinRate: Float) {
    vel = velocity
    spin = spinRate
    angle = 0
    t = 0
    v.tremble = 0
    v.idleWeight = 0
    v.tiltPivot = vec3(0, MeshGen.pieceHeight(v.kind) * 0.5, 0)
  }
  /// True once it hits the board (or has dropped well below it).
  mutating func update(_ v: Actor, _ dt: Float) -> Bool {
    t += dt
    vel.y -= 14 * dt
    v.pos += vel * dt
    angle += spin * dt
    v.tilt = angleAxis(-angle, vec3(1, 0, 0))
    let hh = v.height() * 0.5
    let low = v.pos.y + hh - (abs(cos(angle)) * hh + abs(sin(angle)) * v.radius())
    if overBoard(v.pos) && low <= 0 && vel.y < 0 { return true }
    return v.pos.y < -1.5 || t > 3
  }
}

/// Shatters a flying victim where it came down.
func crashLanding(_ w: World, _ v: Actor, _ dir: vec3) {
  let c = v.pos + vec3(0, v.height() * 0.5, 0)
  let onBoard = overBoard(v.pos) && v.pos.y > -0.5
  w.shatter(v, c - vec3(0, 0.2, 0), dir * 1.5 + vec3(0, -1, 0), 1.5, 0.8, 9)
  if onBoard {
    let g = flat(v.pos)
    w.fx.dust(g, 20, 0.6, 1.8)
    w.groundRing(g, vec3(0.8, 0.7, 0.55), 2.8, 0.5, 0.07)
    w.sound(.thud, 1, c, 0.8)
    w.shake(0.35)
  }
}

/// Dust whipped round in a little whirlwind.
func whirlDust(_ w: World, _ c: vec3, _ radius: Float) {
  for _ in 0..<2 {
    var p = Particle()
    let ang = w.fx.rnd(0, 6.2831853)
    let d = vec3(cos(ang), 0, sin(ang))
    p.pos = c + d * radius * w.fx.rnd(0.6, 1.0) + vec3(0, w.fx.rnd(0, 0.1), 0)
    p.vel = vec3(-d.z, 0, d.x) * w.fx.rnd(1.5, 2.5) + vec3(0, w.fx.rnd(0.3, 0.9), 0)
    p.size = w.fx.rnd(0.05, 0.11)
    p.grow = 0.25
    p.color = vec3(0.55, 0.5, 0.45)
    p.colorEnd = p.color * 0.9
    p.opacity = w.fx.rnd(0.12, 0.25)
    p.maxLife = w.fx.rnd(0.6, 1.1)
    p.type = .smoke
    p.drag = 2
    p.rot = w.fx.rnd(0, 6.28)
    p.rotVel = 3
    p.seed = w.fx.rnd()
    p.fadeIn = 0.08
    w.fx.add(p)
  }
}

/// Glowing motes streaming from one point towards another.
func stream(_ w: World, _ from: vec3, _ to: vec3, _ col: vec3, _ n: Int, _ speed: Float) {
  for _ in 0..<n {
    var p = Particle()
    p.pos = from + w.fx.rndDir() * 0.06
    p.vel = (to - from) * speed * w.fx.rnd(0.8, 1.2) + w.fx.rndDir() * 0.25
    p.size = w.fx.rnd(0.008, 0.02)
    p.color = col * w.fx.rnd(0.6, 1.4)
    p.colorEnd = col * 0.2
    p.opacity = 0
    p.maxLife = min(0.9, 1 / speed) * w.fx.rnd(0.8, 1.05)
    p.type = .glow
    p.drag = 0.2
    w.fx.add(p)
  }
}

/// Orientation that points a prop's +y axis along `along`.
func aimY(_ along: vec3, _ hint: vec3 = vec3(0, 1, 0)) -> quat {
  let y = normalize(along)
  let h = abs(dot(y, hint)) > 0.95 ? vec3(1, 0, 0) : hint
  let x = normalize(cross(y, h))
  let z = cross(x, y)
  return quat_cast(mat3(x, y, z))
}

// ---------------------------------------------------------------------------
// Promotion: the pawn dissolves in a pillar of light and returns as its new self
// ---------------------------------------------------------------------------
final class PromotionAnim: Staged {
  let pawn: Int, sq: Int, kind: Int
  var newId = -1
  init(_ pawn: Int, _ square: Int, _ kind: Int) {
    self.pawn = pawn
    self.sq = square
    self.kind = kind
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let c = squareCenter(sq)
    let gold = vec3(1.0, 0.84, 0.5)
    let u = total
    w.frameLights.append(PointLight(c + vec3(0, 1.2, 0), 4, gold * 14 * sin(clamp(u / 2, 0, 1) * kPI)))
    if u < 1.4 {
      for _ in 0..<3 {
        var p = Particle()
        let a = w.fx.rnd(0, 6.28)
        p.pos = c + vec3(cos(a) * 0.35, w.fx.rnd(0, 0.2), sin(a) * 0.35)
        p.vel = vec3(0, w.fx.rnd(1.0, 2.5), 0)
        p.size = w.fx.rnd(0.01, 0.03)
        p.color = gold * w.fx.rnd(2, 5)
        p.colorEnd = gold * 0.3
        p.opacity = 0
        p.maxLife = w.fx.rnd(0.6, 1.2)
        p.type = .spark
        p.stretch = 0.04
        p.drag = 0.5
        w.fx.add(p)
      }
    }
    if first { w.sound(.chime, 1, c) }
    let p = w.actors[pawn]
    if p.alive {
      p.glow = vec4(gold, min(1.5, u * 2))
      if u > 0.5 { p.dissolve = clamp((u - 0.5) / 0.7, 0, 1) }
      p.dissolveColor = gold
      if p.dissolve >= 1 { p.alive = false }
    }
    if u > 0.85 && newId < 0 {
      let n = p.copy()
      n.id = w.actors.count
      n.kind = kind
      n.alive = true
      n.visible = true
      n.dissolve = 1
      n.dissolveVel = -1.1
      n.dissolveColor = gold
      n.first = true
      n.glow = vec4(gold, 1.5)
      n.square = sq
      newId = n.id
      w.actors.append(n)
      w.actorAt[sq] = newId
      w.fx.magicBurst(c + vec3(0, 0.6, 0), gold * 4, 60, 2)
    }
    if newId >= 0 {
      w.actors[newId].glow = vec4(gold, max(0, 1.5 - (u - 0.85) * 1.2))
    }
    if u > 2.4 {
      if newId >= 0 { w.actors[newId].glow = vec4(0) }
      w.actors[pawn].alive = false
      return true
    }
    return false
  }
}

final class SeqAnim: Anim {
  private var list: [Anim] = []
  private var i = 0
  func add(_ a: Anim?) {
    if let a { list.append(a) }
  }
  func update(_ w: World, _ dt: Float) -> Bool {
    var dt = dt
    while i < list.count {
      if !list[i].update(w, dt) { return false }
      i += 1
      dt = 1e-5
    }
    return true
  }
}

// ---------------------------------------------------------------------------
// Fast capture (theatrics disabled): quick strike + shatter
// ---------------------------------------------------------------------------
final class QuickCapture: BattleBase {
  override func step(_ w: World, _ dt: Float) -> Bool {
    let v = w.actors[plan.victim]
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
      if moveIn(w, dt) { next() }
    case 2:
      if enter() { w.actors[plan.mover].yawTarget = w.actors[plan.mover].baseYaw }
      return pt > 0.1
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
final class CheckmateAnim: Staged {
  let king: Int, winner: Int
  var theta: Float = 0, omega: Float = 0
  var tdir = vec3(0, 0, 1)
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
        if w.cinematic {
          var p = CamPose()
          p.target = k.pos + vec3(0, 0.8, 0)
          let back = normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4))
          p.pos = p.target + back * 3.6 + vec3(0, 1.3, 0)
          p.fov = 32
          p.focus = length(p.pos - p.target)
          p.aperture = 7
          w.cam.setShot(p, 0.9)
        }
      }
      let u = clamp(pt / 1.5, 0, 1)
      k.tremble = 0.4 + u
      k.crackColor = red
      k.crack = easeIn(u) * 0.9
      k.glow = vec4(red, 0.6 * u)
      k.idleWeight = 0
      w.frameLights.append(PointLight(k.pos + vec3(0, 0.8, 0), 3, red * (3 + 10 * u)))
      if u >= 1 {
        tdir = normalize(vec3(sin(k.yaw), 0, cos(k.yaw)))
        k.tiltPivot = vec3(0, 0, -k.radius() / k.scale * 0.9)
        omega = 1.5
        w.sound(.rumble, 0.8, k.pos, 0.8)
        next()
      }
    case 1:
      k.tremble = 0.3
      omega += 9 * sin(theta + 0.2) * dt
      theta += omega * dt
      k.tilt = angleAxis(-theta, vec3(1, 0, 0))
      if theta > 1.25 {
        let top = k.pos - tdir * k.height() * 0.7 + vec3(0, 0.3, 0)
        w.shatter(k, top, -tdir * omega * 0.7, 1.2, 0.7, 6)
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

final class StalemateAnim: Staged {
  override func step(_ w: World, _ dt: Float) -> Bool {
    for a in w.actors where a.alive {
      let u = clamp(pt / 2, 0, 1)
      a.poseTwist = sin(pt * 7 + a.phase) * 0.35 * sin(u * kPI)
    }
    if pt > 2 {
      for a in w.actors { a.poseTwist = 0 }
      return true
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------
private let kStyleNames: [[String]] = [
  ["Head-butt", "Bull charge", "Whirlwind"],
  ["Trample", "Buck kick", "Joust"],
  ["Holy bolt", "Judgment beam", "Seraph orbs"],
  ["Cannon", "Timber!", "Earthquake"],
  ["Telekinesis", "Frost nova", "Vortex"],
  ["Royal blade", "Royal decree", "Rain of blades"],
]

private var lastStyle = [Int](repeating: -1, count: Kind.count)

/// A random style for this piece type - never the same one twice in a row.
private func pickStyle(_ kind: Int, _ forced: Int) -> Int {
  var s = forced >= 0 ? forced % kBattleStyles : Int.random(in: 0..<kBattleStyles)
  if forced < 0 && s == lastStyle[kind] { s = (s + 1 + Int.random(in: 0..<(kBattleStyles - 1))) % kBattleStyles }
  lastStyle[kind] = s
  return s
}

private func makeBattle(_ kind: Int, _ style: Int, _ plan: MovePlan) -> Anim {
  switch kind * kBattleStyles + style {
  case Kind.pawn * kBattleStyles + 1: return PawnCharge(plan)
  case Kind.pawn * kBattleStyles + 2: return PawnWhirl(plan)
  case Kind.knight * kBattleStyles + 0: return KnightBattle(plan)
  case Kind.knight * kBattleStyles + 1: return KnightKick(plan)
  case Kind.knight * kBattleStyles + 2: return KnightJoust(plan)
  case Kind.bishop * kBattleStyles + 0: return BishopBattle(plan)
  case Kind.bishop * kBattleStyles + 1: return BishopBeam(plan)
  case Kind.bishop * kBattleStyles + 2: return BishopOrbs(plan)
  case Kind.rook * kBattleStyles + 0: return RookBattle(plan)
  case Kind.rook * kBattleStyles + 1: return RookTopple(plan)
  case Kind.rook * kBattleStyles + 2: return RookQuake(plan)
  case Kind.queen * kBattleStyles + 0: return QueenBattle(plan)
  case Kind.queen * kBattleStyles + 1: return QueenFrost(plan)
  case Kind.queen * kBattleStyles + 2: return QueenVortex(plan)
  case Kind.king * kBattleStyles + 0: return KingBattle(plan)
  case Kind.king * kBattleStyles + 1: return KingDecree(plan)
  case Kind.king * kBattleStyles + 2: return KingBlades(plan)
  default: return PawnBattle(plan)
  }
}

func battleStyleName(_ kind: Int, _ style: Int) -> String {
  if kind < 0 || kind >= Kind.count || style < 0 || style >= kBattleStyles { return "" }
  return kStyleNames[kind][style]
}

func makeMoveAnim(_ w: World, _ plan: MovePlan) -> Anim {
  let seq = SeqAnim()
  if plan.victim >= 0 {
    if plan.fast {
      seq.add(QuickCapture(plan))
    } else {
      let kind = w.actors[plan.mover].kind
      seq.add(makeBattle(kind, pickStyle(kind, plan.style), plan))
    }
  } else {
    seq.add(QuietMove(plan))
  }
  if plan.promoKind >= 0 { seq.add(PromotionAnim(plan.mover, plan.promoSquare, plan.promoKind)) }
  return seq
}

func makeCheckmateAnim(_ w: World, _ kingId: Int, _ winnerColor: Int) -> Anim {
  CheckmateAnim(kingId, winnerColor)
}

func makeStalemateAnim(_ w: World) -> Anim { StalemateAnim() }

/// Staged demonstration of one attacker type (used by --battle for screenshots).
func makeDemoBattle(_ w: World, _ attackerId: Int, _ victimId: Int) -> Anim {
  var p = MovePlan()
  p.mover = attackerId
  p.victim = victimId
  p.move.from = UInt8(w.actors[attackerId].square)
  p.move.to = UInt8(w.actors[victimId].square)
  p.move.flags = Move.fCapture
  return makeMoveAnim(w, p)
}
