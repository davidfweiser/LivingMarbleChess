// Alternative battle styles for the heavy pieces: the rook topples or quakes,
// the queen freezes or spins the victim away, the king decrees or calls down
// a rain of blades.
import Foundation
import simd

// ---------------------------------------------------------------------------
// Rook: timber! The tower topples onto the victim, then springs back up.
// ---------------------------------------------------------------------------
final class RookTopple: BattleBase {
  private var approach = Mover()
  private var phi: Float = 0, omega: Float = 0
  private var crushed = false
  private let kFlat: Float = kPI * 0.5 - 0.03
  private func crush(_ w: World, _ v: Actor, _ at: vec3) {
    crushed = true
    w.shatter(v, at, vec3(0, -3, 0) + dir * 1.2, 1.8, 0.4, 9)
    w.fx.dust(V, 16, 0.5, 1.6)
    w.sound(.clack, 1, at, 0.7)
    w.shake(0.35)
    w.slowmo(0.3, 0.35)
    omega *= 0.6
  }
  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // stomp within reach
      if enter() {
        let reach: Float = 1.12
        approach = Mover(plan.mover, L > reach ? V - dir * reach : A, .stomp)
      }
      v.tremble = 0.8
      if approach.update(w, dt) {
        a.yawTarget = faceAngle(dir)
        next()
      }
    case 2:  // teeter...
      if enter() { w.sound(.rumble, 0.7, a.pos, 0.55) }
      let u = clamp(pt / 0.7, 0, 1)
      a.poseBend = vec2(0.04 * sin(u * kPI * 3), -0.12 * sin(u * kPI * 2))
      v.tremble = 1
      v.poseBend = vec2(0, -0.25 * u)  // the victim looks up
      if u >= 1 {
        a.poseBend = vec2(0)
        a.tiltPivot = vec3(0, 0, a.radius() / a.scale * 0.95)
        phi = 0.02
        omega = 0.4
        w.sound(.whoosh, 0.7, a.pos, 0.6)
        next()
      }
    case 3:  // ...and fall
      omega += 9 * sin(phi + 0.15) * dt
      phi = min(kFlat, phi + omega * dt)
      a.tilt = angleAxis(phi, vec3(1, 0, 0))
      let top = vec3(a.model() * vec4(0, MeshGen.pieceHeight(a.kind) * 0.95, 0, 1))
      if !crushed && v.alive {
        let reached = top.y <= v.height() * 0.95 && length(flat(top - V)) < v.radius() + 0.2
        if reached || phi > 1.2 { crush(w, v, top) }
      }
      if phi >= kFlat {
        for i in 1...4 {
          w.fx.dust(flat(mix(a.pos, top, Float(i) / 4)), 6, 0.2, 1.2)
        }
        w.sound(.thud, 1, top, 0.65)
        w.sound(.boom, 0.4, top, 1.6)
        w.groundRing(flat(top), vec3(0.8, 0.7, 0.55), 2.5, 0.5, 0.08)
        w.shake(0.45)
        next()
      }
    case 4:  // lie there a moment
      if pt > 0.35 { next() }
    case 5:  // spring back up
      let u = clamp(pt / 0.55, 0, 1)
      phi = kFlat * (1 - easeInOut(u))
      a.tilt = angleAxis(phi, vec3(1, 0, 0))
      a.pos.y = 0.15 * sin(u * kPI)
      if u >= 1 {
        a.tilt = quat.identity
        a.tiltPivot = vec3(0)
        a.pos.y = 0
        a.sq.v -= 3
        w.fx.dust(a.pos, 8, a.radius(), 0.8)
        w.sound(.thud, 0.8, a.pos, 0.9)
        next()
      }
    case 6:
      if moveIn(w, dt) { next() }
    case 7:
      return finish(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Rook: earthquake. Leaps and slams down; a shockwave races across the board
// and erupts under the victim.
// ---------------------------------------------------------------------------
final class RookQuake: BattleBase {
  private var front: Float = 0, nextPuff: Float = 0, nextRing: Float = 0
  private var jolted: [Int] = []
  private var flight = Flight()
  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 0.8
      shotExtra = 0.7
      if faceOff(w) { next() }
    case 1:  // crouch
      if enter() { w.sound(.rumble, 0.7, a.pos, 0.8) }
      a.poseSquash = mix(1, 0.75, easeOut(pt / 0.3))
      v.tremble = 0.6
      if pt > 0.3 {
        w.sound(.whoosh, 0.7, a.pos, 0.7)
        w.fx.dust(a.pos, 8, a.radius(), 0.8)
        next()
      }
    case 2:  // leap
      let u = clamp(pt / 0.45, 0, 1)
      a.pos.y = 0.85 * easeOut(u)
      a.poseSquash = mix(1.12, 1, u)
      if u >= 1 { next() }
    case 3:  // SLAM
      let u = clamp(pt / 0.12, 0, 1)
      a.pos.y = 0.85 * (1 - easeIn(u))
      if u >= 1 {
        a.pos.y = 0
        a.sq.v -= 5
        w.sound(.boom, 1, A, 0.6)
        w.sound(.thud, 1, A, 0.7)
        w.fx.dust(A, 30, 0.6, 2.4)
        w.groundRing(A, vec3(1.2, 0.9, 0.6), 4, 0.8, 0.12)
        w.shake(0.7)
        front = a.radius()
        nextPuff = front
        nextRing = front + 0.6
        next()
      }
    case 4:  // the shockwave races across the board
      if enter() { w.sound(.rumble, 1, (A + V) * 0.5, 0.7) }
      front += 6.5 * dt
      let end = L - v.radius() * 0.5
      while nextPuff <= min(front, end) {
        let p = A + dir * nextPuff
        w.fx.dust(p, 4, 0.12, 1.2, vec3(0.5, 0.42, 0.34))
        w.fx.embers(p, 2, 1.5)
        w.fx.chips(p + vec3(0, 0.02, 0), 2, 1.8, w.fx.rnd() < 0.5)
        nextPuff += 0.22
      }
      if front >= nextRing && front < end {
        w.groundRing(A + dir * front, vec3(1.4, 0.7, 0.25), 2, 0.35, 0.06)
        nextRing += 0.6
      }
      // pieces near the fault line get jolted
      let wave = A + dir * min(front, end)
      for o in w.actors {
        if !o.alive || !o.visible || o.id == plan.mover || o.id == plan.victim { continue }
        if length(flat(o.pos - wave)) > 0.6 { continue }
        if jolted.contains(o.id) { continue }
        jolted.append(o.id)
        o.sq.v -= 2.5
      }
      v.tremble = 1.2
      if front >= end {
        // eruption!
        w.fx.dust(V, 30, 0.4, 2.5, vec3(0.5, 0.42, 0.34))
        w.fx.smoke(V + vec3(0, 0.2, 0), 10, 0.25, 1.2, 0.7)
        w.fx.embers(V, 30, 3)
        w.fx.fireball(V + vec3(0, 0.1, 0), 0.6)
        w.groundRing(V, vec3(2.0, 1.0, 0.35), 3.5, 0.6, 0.1)
        w.light(V + vec3(0, 0.3, 0), vec3(30, 14, 5), 4, 0.5)
        w.sound(.explode, 1, V, 0.8)
        w.shake(0.5)
        w.slowmo(0.3, 0.6)
        flight.launch(v, vec3(0, 4.2, 0), 6)
        next()
      }
    case 5:  // tossed into the air, bursts at the top
      if v.alive {
        let landed = flight.update(v, dt)
        if landed || flight.vel.y <= 0.3 {
          let c = victimCenter(w)
          w.shatter(v, c, vec3(0), 2.4, 0.5, 10)
          w.fx.flash(c, 0.8, vec3(6.0, 3.5, 1.5), 0.12)
          w.sound(.clack, 1, c, 0.7)
          w.shake(0.3)
        }
      }
      if !v.alive && pt > 0.4 { next() }
    case 6:
      if moveIn(w, dt) { next() }
    case 7:
      return finish(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Queen: frost nova. Freezes the victim solid, then shatters it like ice.
// ---------------------------------------------------------------------------
final class QueenFrost: BattleBase {
  private let ice = vec3(0.55, 0.85, 1.0)
  private var yaw0: Float = 0, crackleT: Float = 0
  private func crown(_ w: World) -> vec3 {
    let a = w.actors[plan.mover]
    return a.pos + vec3(0, a.height() * 0.93, 0)
  }
  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 0.9
      shotExtra = 0.6
      if faceOff(w) { next() }
    case 1:  // twirl, gathering frost
      if enter() {
        yaw0 = a.yaw
        w.sound(.charge, 0.8, a.pos, 1.4)
      }
      let u = clamp(pt / 0.9, 0, 1)
      a.yaw = wrapAngle(yaw0 + 2 * kPI * easeInOut(u))
      a.yawTarget = a.yaw
      a.pos.y = 0.15 * easeInOut(u)
      a.poseTwist = sin(u * kPI) * 1.0
      a.glow = vec4(ice, 0.5 * u)
      a.idleWeight = 0
      w.fx.swirl(a.pos + vec3(0, 0.7, 0), 0.45, ice * 2.5, w.time)
      w.frameLights.append(PointLight(crown(w), 3, ice * (1 + 3 * u)))
      v.tremble = 0.6
      if u >= 1 {
        a.yawTarget = faceAngle(V - A)
        next()
      }
    case 2:  // blizzard
      if enter() { w.groundRing(V, ice * 0.9, 1, 1.6, 0.12) }
      let u = clamp(pt / 1.2, 0, 1)
      a.pos.y = 0.15 + 0.02 * sin(w.time * 5)
      a.poseBend = vec2(0, 0.15)
      stream(w, crown(w), victimCenter(w) + w.fx.rndDir() * 0.15, ice * 3, 4, 2.2)
      if w.fx.rnd() < 0.3 { w.fx.dust(V, 1, 0.3, 0.3, vec3(0.7, 0.8, 0.9)) }
      w.frameLights.append(PointLight(crown(w), 3, ice * 4))
      w.frameLights.append(PointLight(victimCenter(w), 3, ice * (4 + 10 * u)))
      crackleT -= dt
      if crackleT <= 0 {
        crackleT = 0.2
        w.sound(.crackle, 0.45, V, w.fx.rnd(1.6, 1.9))
      }
      v.crackColor = ice * 1.2
      v.crack = 0.55 * u
      v.glow = vec4(ice, 0.8 * u)
      v.tremble = 1 - u  // frozen stiff
      v.idleWeight = 0
      if u >= 1 {
        v.tremble = 0
        w.sound(.chime, 0.7, V, 1.9)
        next()
      }
    case 3:  // frozen... wind up
      let u = clamp(pt / 0.45, 0, 1)
      v.glow = vec4(ice, 0.8 + 0.3 * sin(pt * 20))
      a.poseBend = vec2(0, mix(0.15, -0.2, easeOut(u)))
      if u >= 1 { next() }
    case 4:  // SNAP
      let u = clamp(pt / 0.12, 0, 1)
      a.poseBend = vec2(0, mix(-0.2, 0.3, u))
      a.poseTwist = -0.8 * u
      if u >= 1 {
        let c = victimCenter(w)
        w.shatter(v, c, dir * 1.0, 2.3, 0.9, 7)
        w.fx.magicBurst(c, ice * 4, 70, 2.5)
        w.fx.sparks(c, vec3(0, 1, 0), 40, 3.5, 1.0, ice * 3)
        w.fx.flash(c, 1.2, ice * 5, 0.15)
        w.groundRing(V, ice * 1.5, 3, 0.7, 0.08)
        w.light(c, ice * 30, 4.5, 0.45)
        w.addFlash(ice * 0.1)
        w.sound(.shatter, 0.8, c, 1.5)
        w.sound(.chime, 0.6, c, 2.0)
        w.shake(0.4)
        w.slowmo(0.3, 0.45)
        next()
      }
    case 5:  // descend
      let u = clamp(pt / 0.5, 0, 1)
      a.pos.y = 0.15 * (1 - easeInOut(u))
      a.glow = vec4(ice, 0.5 * (1 - u))
      a.poseBend = vec2(0, 0.3 * (1 - u))
      a.poseTwist = -0.8 * (1 - u)
      if u >= 1 {
        a.idleWeight = 1
        next()
      }
    case 6:
      if moveIn(w, dt) { next() }
    case 7:
      return finish(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Queen: vortex. Spins the victim down to a point - then it bursts back out.
// ---------------------------------------------------------------------------
final class QueenVortex: BattleBase {
  private let violet = vec3(0.75, 0.32, 1.0)
  private var spinRate: Float = 0, whooshT: Float = 0, boltT: Float = 0
  private func crown(_ w: World) -> vec3 {
    let a = w.actors[plan.mover]
    return a.pos + vec3(0, a.height() * 0.93, 0)
  }
  private func eye(_ w: World) -> vec3 {  // centre of the vortex
    let v = w.actors[plan.victim]
    return V + vec3(0, 0.25 + MeshGen.pieceHeight(v.kind) * 0.5, 0)
  }
  // shrink about the vortex eye rather than the piece's base
  private func setScale(_ w: World, _ s: Float, _ lift: Float) {
    let v = w.actors[plan.victim]
    v.scale = s
    v.pos.y = lift + (1 - s) * MeshGen.pieceHeight(v.kind) * 0.5
  }
  private func inflow(_ w: World, _ c: vec3) {
    for _ in 0..<3 {
      var p = Particle()
      let ang = w.fx.rnd(0, 6.2831853)
      let d = vec3(cos(ang), 0, sin(ang))
      let r = w.fx.rnd(0.8, 1.3)
      p.pos = c + d * r + vec3(0, w.fx.rnd(-0.4, 0.4), 0)
      p.vel = vec3(-d.z, 0, d.x) * 2.6 - d * 1.6
      p.size = w.fx.rnd(0.01, 0.025)
      p.color = violet * w.fx.rnd(1.5, 3.5)
      p.colorEnd = violet * 0.3
      p.opacity = 0
      p.maxLife = w.fx.rnd(0.4, 0.6)
      p.type = .spark
      p.stretch = 0.03
      p.drag = 0
      w.fx.add(p)
    }
    var s = Particle()
    let ang = w.fx.rnd(0, 6.2831853)
    let d = vec3(cos(ang), 0, sin(ang))
    s.pos = c + d * 1.1 + vec3(0, w.fx.rnd(-0.3, 0.3), 0)
    s.vel = vec3(-d.z, 0, d.x) * 1.8 - d * 1.2
    s.size = w.fx.rnd(0.1, 0.18)
    s.color = vec3(0.05, 0.03, 0.07)
    s.colorEnd = s.color
    s.opacity = 0.3
    s.maxLife = 0.8
    s.type = .smoke
    s.drag = 0.4
    s.rot = w.fx.rnd(0, 6.28)
    s.rotVel = 4
    s.seed = w.fx.rnd()
    s.fadeIn = 0.2
    w.fx.add(s)
  }
  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 0.95
      shotExtra = 0.9
      if faceOff(w) { next() }
    case 1:  // raise hands
      if enter() { w.sound(.charge, 0.8, a.pos, 0.6) }
      let u = clamp(pt / 0.6, 0, 1)
      a.pos.y = 0.18 * easeInOut(u)
      a.poseTwist = sin(u * kPI) * 1.4
      a.glow = vec4(violet, 1.1 * u)
      a.idleWeight = 0
      w.frameLights.append(PointLight(crown(w), 3, violet * (3 + 8 * u)))
      if u >= 1 { next() }
    case 2:  // the vortex opens
      if enter() {
        w.sound(.rumble, 0.9, V, 0.6)
        w.groundRing(V, violet * 1.2, 1.5, 1.5, 0.1)
      }
      let u = clamp(pt / 1.5, 0, 1)
      a.pos.y = 0.18 + 0.02 * sin(w.time * 5)
      a.poseBend = vec2(0, -0.15)
      spinRate = 26 * easeIn(u)
      v.yaw = wrapAngle(v.yaw + spinRate * dt)
      v.yawTarget = v.yaw
      setScale(w, mix(1, 0.3, easeIn(u)), 0.25 * easeInOut(u))
      v.glow = vec4(violet, 0.3 + 0.9 * u)
      v.tremble = 0.6
      v.idleWeight = 0
      inflow(w, eye(w))
      boltT -= dt
      if boltT <= 0 {
        boltT = w.fx.rnd(0.08, 0.16)
        w.fx.bolt(crown(w), eye(w) + w.fx.rndDir() * 0.1, violet * 5, 0.12, 14)
      }
      whooshT -= dt
      if whooshT <= 0 {
        whooshT = 0.35 - 0.2 * u
        w.sound(.whoosh, 0.5, V, 0.7 + 0.8 * u)
      }
      w.frameLights.append(PointLight(crown(w), 3, violet * 8))
      w.frameLights.append(PointLight(eye(w), 3.5, violet * (6 + 14 * u)))
      if u >= 1 { next() }
    case 3:  // collapse to a point...
      let u = clamp(pt / 0.15, 0, 1)
      v.yaw = wrapAngle(v.yaw + spinRate * dt)
      v.yawTarget = v.yaw
      setScale(w, mix(0.3, 0.05, easeIn(u)), 0.25)
      w.fx.flash(eye(w), 0.1 + 0.5 * u, violet * 6, 0.05)
      if u >= 1 {
        // ...and burst back out at full size
        setScale(w, 1, 0.25)
        v.crack = 0.6
        v.crackColor = violet
        let c = eye(w)
        w.shatter(v, c, vec3(0), 3.2, 0.8, 12)
        w.fx.magicBurst(c, violet * 5, 100, 3.5)
        w.fx.flash(c, 1.4, violet * 6, 0.2)
        w.fx.ring(c, 0.3, violet * 3, 0.4)
        w.groundRing(V, violet * 1.5, 3, 0.8, 0.08)
        w.light(c, violet * 40, 5, 0.5)
        w.addFlash(violet * 0.12)
        w.sound(.explode, 0.9, c, 0.7)
        w.shake(0.55)
        w.slowmo(0.3, 0.5)
        next()
      }
    case 4:  // descend
      let u = clamp(pt / 0.5, 0, 1)
      a.pos.y = 0.18 * (1 - easeInOut(u))
      a.glow = vec4(violet, 0.9 * (1 - u))
      a.poseBend = vec2(0, -0.15 * (1 - u))
      if u >= 1 {
        a.idleWeight = 1
        next()
      }
    case 5:
      if moveIn(w, dt) { next() }
    case 6:
      return finish(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// King: royal decree. The victim is made to bow - then crumbles to dust.
// ---------------------------------------------------------------------------
final class KingDecree: BattleBase {
  private let gold = vec3(1.0, 0.8, 0.42)
  private func crown(_ w: World) -> vec3 {
    let a = w.actors[plan.mover]
    return a.pos + vec3(0, a.height(), 0)
  }
  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // draw himself up to full height
      if enter() { w.sound(.chime, 0.9, a.pos, 0.55) }
      let u = clamp(pt / 0.8, 0, 1)
      a.poseSquash = 1 + 0.08 * easeOut(u)
      a.poseBend = vec2(0, -0.12 * easeOut(u))
      a.glow = vec4(gold, 0.45 * u)
      a.idleWeight = 0
      w.frameLights.append(PointLight(crown(w), 3, gold * (1 + 4 * u)))
      w.fx.swirl(crown(w) - vec3(0, 0.3, 0), 0.3, gold * 2, w.time)
      v.tremble = 0.6
      if u >= 1 { next() }
    case 2:  // "Kneel."
      if enter() {
        w.sound(.whoosh, 0.7, a.pos, 0.6)
        w.fx.ring(crown(w), 0.2, gold * 3, 0.4)
        w.groundRing(A, gold * 1.4, 4, 0.8, 0.08)
        w.addFlash(gold * 0.06)
      }
      let u = clamp(pt / 0.25, 0, 1)
      a.poseSquash = mix(1.08, 1, u)
      a.poseBend = vec2(0, mix(-0.12, 0.3, easeOut(u)))
      if u >= 1 {
        v.tiltPivot = vec3(0, 0, v.radius() / v.scale * 0.9)  // front edge, facing the king
        next()
      }
    case 3:  // the victim bows
      let u = clamp(pt / 1.0, 0, 1)
      a.poseBend = vec2(0, mix(0.3, 0.1, u))
      v.tilt = angleAxis(0.5 * easeInOut(u), vec3(1, 0, 0))
      v.poseBend = vec2(0, 0.3 * easeInOut(u))
      v.tremble = 0.5
      v.glow = vec4(gold, 0.25 * u)
      v.idleWeight = 0
      w.frameLights.append(PointLight(victimCenter(w), 2.5, gold * 4 * u))
      if u >= 1 {
        w.sound(.thud, 0.6, V, 1.3)
        w.fx.dust(V + dir * v.radius(), 6, 0.2, 0.6)
        next()
      }
    case 4:  // and crumbles away
      if enter() { w.sound(.rumble, 0.6, V, 1.2) }
      let u = clamp(pt / 0.9, 0, 1)
      v.crackColor = gold * 0.4
      v.crack = easeIn(u)
      v.tremble = 0.8 + u
      let tint = v.color == 0 ? vec3(0.62, 0.60, 0.57) : vec3(0.22, 0.21, 0.2)
      if w.fx.rnd() < 0.7 {
        w.fx.dust(V + w.fx.rndDir() * 0.2 + vec3(0, w.fx.rnd(0, v.height()), 0), 1, 0.05, 0.2, tint)
      }
      if u >= 1 {
        w.shatter(v, victimCenter(w), vec3(0, -1.5, 0), 0.5, 0.15, 3)
        w.fx.dust(V, 40, 0.8, 1.5, tint)
        w.fx.smoke(V + vec3(0, 0.3, 0), 10, 0.3, 0.4, v.color == 0 ? 0.1 : 0.6)
        w.groundRing(V, vec3(0.8, 0.7, 0.55), 2, 0.6, 0.08)
        w.sound(.thud, 0.8, V, 0.6)
        w.shake(0.25)
        next()
      }
    case 5:  // a satisfied nod
      let u = clamp(pt / 0.6, 0, 1)
      a.poseBend = vec2(0, 0.1 * (1 - u) + 0.15 * sin(u * kPI))
      a.glow = vec4(gold, 0.45 * (1 - u))
      if u >= 1 {
        a.poseBend = vec2(0)
        a.idleWeight = 1
        next()
      }
    case 6:
      if moveIn(w, dt) { next() }
    case 7:
      return finish(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// King: rain of blades. Three golden swords plunge into the victim from above.
// ---------------------------------------------------------------------------
final class KingBlades: BattleBase {
  private static let kBlades = 3
  private let gold = vec3(1.0, 0.8, 0.42)
  private let kScale: Float = 1.2
  private var blade = [Int](repeating: -1, count: KingBlades.kBlades)
  private var hover = [vec3](repeating: vec3(0), count: KingBlades.kBlades)
  private var plunge = [vec3](repeating: vec3(0), count: KingBlades.kBlades)
  private var aim = [quat](repeating: quat.identity, count: KingBlades.kBlades)
  private var struck = 0
  private func dropAt(_ i: Int) -> Float { 0.1 + 0.32 * Float(i) }
  override func step(_ w: World, _ dt: Float) -> Bool {
    let kBlades = KingBlades.kBlades
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 1.2
      shotExtra = 1.4
      if faceOff(w) { next() }
    case 1:  // the blades appear above the victim
      if enter() {
        w.sound(.chime, 0.9, V, 1.0)
        let a0 = w.fx.rnd(0, 6.28)
        for i in 0..<kBlades {
          blade[i] = w.addProp(.sword)
          let ang = a0 + Float(i) * 2 * kPI / Float(kBlades)
          let out = vec3(cos(ang), 0, sin(ang))
          let down = normalize(vec3(0, -1, 0) - out * 0.3)  // leaning in
          let tip = V + out * 0.08 + vec3(0, v.height() * 0.35, 0)
          plunge[i] = tip - down * kScale
          hover[i] = plunge[i] + vec3(0, 1, 0) + out * 0.3
          aim[i] = aimY(down, out)
        }
      }
      let u = clamp(pt / 0.9, 0, 1)
      a.poseBend = vec2(0, -0.2 * easeOut(u))
      a.glow = vec4(gold, 0.6 * u)
      a.idleWeight = 0
      for i in 0..<kBlades {
        guard let s = w.prop(blade[i]) else { continue }
        let ui = clamp((pt - 0.15 * Float(i)) / 0.5, 0, 1)
        s.pos = hover[i] + vec3(0, 0.04 * sin(w.time * 3 + Float(i)), 0)
        s.rot = aim[i]
        s.scale = kScale
        s.dissolve = 1 - ui
        s.glow = vec4(gold, 1.6 - ui)
        w.frameLights.append(PointLight(s.pos, 2, gold * 4))
      }
      v.tremble = 0.9
      if u >= 1 { next() }
    case 2:  // one after another they fall
      for i in 0..<kBlades {
        guard let s = w.prop(blade[i]) else { continue }
        let ud = clamp((pt - dropAt(i)) / 0.13, 0, 1)
        s.pos = mix(hover[i] + vec3(0, 0.04 * sin(w.time * 3 + Float(i)), 0), plunge[i], easeIn(ud))
        s.glow = vec4(gold, ud < 1 ? 0.6 : 0.9)
        if ud >= 1 && struck == i {
          struck += 1
          let hit = plunge[i] + (s.rot * vec3(0, 1, 0)) * kScale * 0.6
          w.fx.sparks(hit, vec3(0, 1, 0), 25, 3.5, 0.8, vec3(4.0, 3.0, 1.5))
          w.fx.chips(hit, 10, 2.0, v.color == 1)
          w.fx.flash(hit, 0.5, gold * 5, 0.1)
          w.light(hit, gold * 15, 3, 0.2)
          w.sound(.clang, 0.9, hit, 0.9 + 0.15 * Float(i))
          w.shake(0.2)
          v.crackColor = gold
          v.crack = 0.3 * Float(struck)
          v.sq.v -= 3
          a.sq.v -= 1.5
        }
      }
      v.tremble = struck > 0 ? 0 : 0.9  // pinned
      v.idleWeight = 0
      a.poseBend = vec2(0, mix(-0.2, 0.25, min(1, pt / dropAt(kBlades - 1))))
      if struck == kBlades && pt > dropAt(kBlades - 1) + 0.4 {
        let c = victimCenter(w)
        w.shatter(v, c, vec3(0), 2.2, 1.0, 9)
        w.fx.flash(c, 1.2, gold * 6, 0.2)
        w.fx.magicBurst(c, gold * 4, 60, 2.5)
        w.groundRing(V, gold * 1.5, 3, 0.7, 0.08)
        w.light(c, gold * 30, 4.5, 0.4)
        w.sound(.explode, 0.7, c, 1.2)
        w.shake(0.45)
        w.slowmo(0.3, 0.45)
        next()
      }
    case 3:  // the blades fade
      let u = clamp(pt / 0.8, 0, 1)
      for i in 0..<kBlades {
        guard let s = w.prop(blade[i]) else { continue }
        s.dissolve = easeIn(u)
        s.glow = vec4(gold, 0.9 * (1 - u))
        if u >= 1 { s.alive = false }
      }
      a.poseBend = vec2(0, 0.25 * (1 - u))
      a.glow = vec4(gold, 0.6 * (1 - u))
      if u >= 1 {
        a.idleWeight = 1
        next()
      }
    case 4:
      if moveIn(w, dt) { next() }
    case 5:
      return finish(w)
    default:
      break
    }
    return false
  }
}
