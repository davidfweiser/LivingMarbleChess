// Medieval battles of the man-at-arms (spear thrust, shield bash, javelin),
// the mounted knight (joust, trample, sword sweep) and the bishop on his
// cathedra (wrath of heaven, brimstone, pillar of salt).
import Foundation
import simd

// ===========================================================================
// PAWN - the man-at-arms
// ===========================================================================
/// Spear thrust: march up, level the spear, lunge.
final class PawnThrust: MBattle {
  private var start = vec3(0)
  private var hit = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // march within a spear's reach
      if enter() { walker = Walker(plan.mover, V - dir * min(L, 0.98), .foot, 0.8) }
      v.tremble = 0.7
      if walker.update(w, dt) { next() }
    case 2:  // lower the spear
      if enter() { a.yawTarget = faceAngle(V - a.pos) }
      let u = easeInOut(pt / 0.45)
      a.ctl.itemPitch = 1.5 * u
      a.ctl.armR = vec3(0.45 * u, 0, 0)
      a.ctl.lean = -0.08 * u
      a.ctl.crouch = 0.25 * u
      v.tremble = 1
      if pt > 0.55 {
        start = flat(a.pos)
        w.sound(.whoosh, 0.8, a.pos, 1.1)
        next()
      }
    case 3:  // lunge!
      let u = clamp(pt / 0.16, 0, 1)
      a.pos = start + dir * 0.22 * easeOut(u)
      a.ctl.lean = mix(-0.08, 0.32, u)
      a.ctl.armR = vec3(mix(0.45, 0.65, u), 0, -0.35 * u)
      a.ctl.itemPitch = 1.5 + 0.1 * u
      a.ctl.gait = kPI * 0.5
      a.ctl.stride = 0.6 * u
      if u >= 1 && !hit {
        hit = true
        let tip = victimCenter(w) - dir * v.radius() * 0.6
        w.fx.sparks(tip, -dir + vec3(0, 0.3, 0), 26, 3.5, 0.6, vec3(3.0, 2.6, 2.0))
        w.fx.flash(tip, 0.4, vec3(2.0, 1.8, 1.5), 0.08)
        w.light(tip, vec3(6.0, 5.0, 4.0), 2.5, 0.15)
        w.sound(.clang, 0.8, tip, 1.3)
        w.shatter(v, tip, dir * 3.2 + vec3(0, 0.6, 0), 1.2, 0.7, 8)
        w.shake(0.35)
        w.slowmo(0.3, 0.35)
        next()
      }
    case 4:  // recover, spear upright again
      let u = easeInOut(pt / 0.6)
      a.ctl.lean = 0.32 * (1 - u)
      a.ctl.armR = vec3(0.65 * (1 - u), 0, -0.35 * (1 - u))
      a.ctl.itemPitch = 1.6 * (1 - u)
      a.ctl.stride = 0.6 * (1 - u)
      a.ctl.crouch = 0.25 * (1 - u)
      if pt > 0.65 { next() }
    case 5:
      if moveInM(w, dt, .foot, 0.8) { next() }
    case 6:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

/// Shield bash: raise the shield, charge, and knock the victim flying.
final class PawnBash: MBattle {
  private var start = vec3(0), stop = vec3(0)
  private var dur: Float = 0.5
  private var flight = Flight()

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.6
      if faceOff(w) { next() }
    case 1:  // shield up, spear back, a battle cry
      let u = easeInOut(pt / 0.5)
      a.ctl.armL = vec3(0.55 * u, 0.15 * u, -0.3 * u)
      a.ctl.lean = 0.18 * u
      a.ctl.crouch = 0.3 * u
      v.tremble = 0.9
      if pt > 0.6 { next() }
    case 2:  // charge
      if enter() {
        start = flat(a.pos)
        stop = V - dir * touchDist(a, v)
        dur = clamp(0.25 + 0.22 * length(stop - start), 0.3, 0.85)
        w.sound(.whoosh, 0.7, a.pos, 0.9)
      }
      let u = clamp(pt / dur, 0, 1)
      let prevS = length(flat(a.pos) - start)
      a.pos = mix(start, stop, easeIn(u))
      a.ctl.gait += (length(flat(a.pos) - start) - prevS) / 0.3 * kTau
      a.ctl.stride = 1
      a.ctl.lean = 0.3
      if w.fx.rnd() < 0.3 { w.fx.dust(flat(a.pos), 1, 0.1, 0.4) }
      v.tremble = 1
      if u >= 1 {
        let hit = V - dir * v.radius() * 0.8 + vec3(0, min(a.height(), v.height()) * 0.5, 0)
        w.fx.sparks(hit, dir + vec3(0, 0.5, 0), 30, 3.5, 0.7, vec3(3.0, 2.6, 2.0))
        w.fx.chips(hit, 20, 2.5, v.color == 1)
        w.light(hit, vec3(6.0, 5.0, 4.0), 2.5, 0.15)
        w.sound(.clang, 0.9, hit, 0.75)
        w.sound(.thud, 0.8, hit, 1.2)
        w.shake(0.35)
        w.slowmo(0.3, 0.35)
        flight.launch(v, dir * 3.6 + vec3(0, 4.4, 0), 8)
        next()
      }
    case 3:  // the victim flies; the soldier stands his ground
      let u = easeInOut(pt / 0.5)
      a.ctl.stride = 1 - u
      a.ctl.lean = 0.3 * (1 - u)
      a.ctl.armL = vec3(0.55, 0.15, -0.3) * (1 - u)
      a.ctl.crouch = 0.3 * (1 - u)
      if v.alive && flight.update(v, dt) { crashLanding(w, v, dir) }
      if !v.alive && pt > 0.55 { next() }
    case 4:
      if moveInM(w, dt, .foot, 0.8) { next() }
    case 5:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

/// Javelin: hurl the spear from afar, then walk over and pull it out of the rubble.
final class PawnJavelin: MBattle {
  private var spear = -1
  private var p0 = vec3(0), v0 = vec3(0)
  private var T: Float = 0.6
  private let g: Float = 9.8

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.6
      if faceOff(w) { next() }
    case 1:  // draw back
      let u = easeInOut(pt / 0.55)
      a.ctl.armR = vec3(2.3 * u, 0.15 * u, -0.6 * u)
      a.ctl.itemPitch = -0.55 * u
      a.ctl.twist = 0.55 * u
      a.ctl.lean = -0.12 * u
      a.ctl.armL = vec3(0.5 * u, 0, 0)
      v.tremble = 0.8
      if pt > 0.65 { next() }
    case 2:  // throw
      let u = easeIn(pt / 0.14)
      a.ctl.armR = vec3(mix(2.3, 1.4, u), 0.15, -0.6 * (1 - u))
      a.ctl.twist = mix(0.55, -0.3, u)
      a.ctl.lean = mix(-0.12, 0.25, u)
      if pt >= 0.14 {
        a.ctl.itemR = 0
        spear = w.addPuppet(MAsset.spear, a.color)
        p0 = bonePoint(a, HB.itemR, Medieval.handR(MAsset.pawn)) + vec3(0, 0.05, 0)
        let target = victimCenter(w)
        T = 0.32 + 0.07 * L
        v0 = (target - p0) / T + vec3(0, 0.5 * g * T, 0)
        w.sound(.whoosh, 0.9, p0, 1.25)
        next()
      }
    case 3:  // flight
      let u = easeOut(pt / 0.4)
      a.ctl.armR = vec3(mix(1.4, 0.4, u), 0.15 * (1 - u), 0)
      a.ctl.twist = -0.3 * (1 - u)
      a.ctl.lean = 0.25 * (1 - u)
      a.ctl.armL = vec3(0.5 * (1 - u), 0, 0)
      let s = w.puppet(spear)
      let tt = min(pt, T)
      if let s {
        s.pos = p0 + v0 * tt + vec3(0, -0.5 * g * tt * tt, 0)
        let vel = v0 + vec3(0, -g * tt, 0)
        s.yaw = faceAngle(vel)
        let pitch = atan2(length(flat(vel)), vel.y)  // from straight up
        s.tilt = angleAxis(pitch, vec3(1, 0, 0))
        // the spear flies tip first: shift so the shaft trails behind
        s.pos -= normalize(vel) * 0.55
      }
      v.tremble = 1
      if pt >= T {
        let c = victimCenter(w)
        let vb = normalize(v0 + vec3(0, -g * T, 0))
        w.fx.sparks(c, -vb, 24, 3.0, 0.6, vec3(3.0, 2.6, 2.0))
        w.sound(.clack, 1, c, 0.8)
        w.shatter(v, c - vb * 0.1, vb * 3 + vec3(0, 0.8, 0), 1.5, 0.9, 9)
        w.shake(0.35)
        w.slowmo(0.3, 0.4)
        // the spear sticks in the board, quivering
        if let s {
          s.pos = flat(V) + vec3(0, -0.12, 0) - vb * 0.2
          s.pos = flat(V) - normalize(flat(vb) + vec3(1e-4)) * 0
          s.tilt = angleAxis(0.5, vec3(1, 0, 0))
          s.yaw = faceAngle(vb)
          s.pos.y = -0.08
        }
        next()
      }
    case 4:  // the spear quivers in the board
      if let s = w.puppet(spear) {
        let q = 0.05 * exp(-pt * 5) * sin(pt * 45)
        s.tilt = angleAxis(0.5 + q, vec3(1, 0, 0))
      }
      if pt > 0.6 { next() }
    case 5:  // walk over to it
      if enter() { walker = Walker(plan.mover, V - dir * 0.28, .foot, 0.8) }
      if walker.update(w, dt) { next() }
    case 6:  // stoop and pull it out
      let u = sin(clamp(pt / 0.8, 0, 1) * kPI)
      a.ctl.crouch = 0.55 * u
      a.ctl.lean = 0.35 * u
      a.ctl.armR = vec3(0.6 * u, 0, -0.3 * u)
      if pt > 0.4 && a.ctl.itemR < 0.5 {
        a.ctl.itemR = 1
        if let s = w.puppet(spear) { s.alive = false }
        w.sound(.clack, 0.4, a.pos, 1.4)
        w.fx.dust(V, 6, 0.2, 0.6)
      }
      if pt > 0.85 { next() }
    case 7:
      if moveInM(w, dt, .foot, 0.8) { next() }
    case 8:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// KNIGHT - the mounted knight
// ===========================================================================
/// Joust: couch the lance and charge; the lance shatters on impact.
final class KnightJoustM: MBattle {
  private var hit = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.0
      shotHeight = 0.7
      if faceOff(w) { next() }
    case 1:  // couch the lance
      if enter() {
        w.sound(.neigh, 0.6, a.pos, 1.05)
        a.yawTarget = faceAngle(V - A)
      }
      let u = easeInOut(pt / 0.6)
      a.ctl.itemPitch = 1.42 * u
      a.ctl.armR = vec3(0.2 * u, 0, 0)
      a.ctl.lean = 0.12 * u
      a.ctl.rear = 0.2 * sin(u * kPI)
      a.ctl.gait += dt * 6
      a.ctl.stride = 0.3
      v.tremble = 0.9
      if pt > 0.65 { next() }
    case 2:  // gallop
      if enter() {
        walker = Walker(plan.mover, V, .horse, 2.4)
        walker.run = 1
        walker.face = false
      }
      walker.update(w, dt)
      a.ctl.stride = max(a.ctl.stride, 0.4)
      let tip = bonePoint(a, KB.lance, Medieval.itemTip(MAsset.knight))
      v.tremble = 1
      if !hit && dot(flat(tip - V), dir) > -v.radius() * 0.5 {
        hit = true
        let c = victimCenter(w)
        w.fx.sparks(c - dir * 0.2, -dir + vec3(0, 0.4, 0), 30, 4.0, 0.6, vec3(4.0, 3.0, 1.5))
        w.fx.flash(c, 0.8, vec3(4.0, 3.5, 2.5), 0.12)
        w.light(c, vec3(18.0, 14.0, 9.0), 4, 0.3)
        splinters(w, tip, -dir, 45)
        a.ctl.itemR = 0  // the lance shatters
        w.sound(.splinter, 1, tip)
        w.sound(.clack, 0.9, c, 0.75)
        w.shatter(v, c - dir * 0.2, dir * 5 + vec3(0, 1.0, 0), 1.4, 0.9, 10)
        w.shake(0.5)
        w.slowmo(0.25, 0.5)
      }
      if walker.s >= walker.len - 1e-3 { next() }
    case 3:  // pull up and rear in triumph
      if enter() { w.sound(.neigh, 0.9, a.pos, 0.95) }
      let u = clamp(pt / 1.3, 0, 1)
      walker.update(w, dt)
      a.ctl.rear = 0.85 * sin(u * kPI)
      a.ctl.itemPitch = 1.42 * (1 - easeInOut(u))
      a.ctl.armR = vec3(0.9 * sin(u * kPI), 0, 0)
      a.ctl.lean = -0.15 * sin(u * kPI)
      if u >= 1 {
        a.ctl.rear = 0
        w.sound(.hoof, 0.8, a.pos, 0.9)
        w.fx.dust(a.pos, 8, 0.3, 0.8)
        next()
      }
    case 4:  // a fresh lance appears in his hand
      if enter() {
        a.ctl.itemR = 1
        w.sound(.chime, 0.45, a.pos, 1.3)
        w.fx.magicBurst(bonePoint(a, KB.lance, Medieval.itemTip(MAsset.knight)), kGold * 2, 20, 1)
      }
      a.ctl.stride = approach(a.ctl.stride, 0, dt * 3)
      if pt > 0.4 { next() }
    case 5:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

/// Trample: rear up over the victim and crash down on it.
final class KnightTrampleM: MBattle {
  private var start = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.1
      shotHeight = 0.8
      if faceOff(w) { next() }
    case 1:  // canter up
      if enter() {
        walker = Walker(plan.mover, V - dir * 0.62, .horse, 1.5)
        walker.run = 0.6
      }
      v.tremble = 0.8
      if walker.update(w, dt) { next() }
    case 2:  // rear up, screaming
      if enter() {
        w.sound(.neigh, 1, a.pos, 0.9)
        a.yawTarget = faceAngle(V - a.pos)
      }
      let u = clamp(pt / 0.7, 0, 1)
      a.ctl.rear = easeOut(u)
      a.ctl.lean = -0.25 * u
      a.ctl.armR = vec3(0.6 * u, 0, 0)
      v.tremble = 1.2
      v.ctl.headPitch = -0.3 * u
      if pt > 1.0 {
        start = flat(a.pos)
        next()
      }
    case 3:  // crash down
      let u = clamp(pt / 0.16, 0, 1)
      a.ctl.rear = 1 - easeIn(u)
      a.pos = start + dir * 0.3 * u
      a.ctl.lean = mix(-0.25, 0.2, u)
      if u >= 1 {
        let c = V + vec3(0, v.height() * 0.4, 0)
        w.shatter(v, c, vec3(0, -3.0, 0) + dir * 1.0, 2.2, 0.3, 8)
        w.groundRing(V, vec3(1.4, 1.1, 0.8), 3.2, 0.7, 0.1)
        w.fx.dust(V, 26, 0.7, 2.2)
        w.light(c, vec3(6.0, 5.0, 4.0), 3, 0.2)
        w.sound(.stomp, 1, c, 1.1)
        w.sound(.explode, 0.6, c, 1.3)
        w.shake(0.6)
        w.slowmo(0.28, 0.4)
        next()
      }
    case 4:  // stamp and snort
      a.ctl.lean = 0.2 * (1 - clamp(pt / 0.5, 0, 1))
      a.ctl.armR = vec3(0.6, 0, 0) * (1 - clamp(pt / 0.5, 0, 1))
      if pt > 0.6 { next() }
    case 5:
      if moveInM(w, dt, .horse, 0.9) { next() }
    case 6:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

/// Sword sweep: the rider draws his sword and cuts the victim in two as he gallops past.
final class KnightSweepM: MBattle {
  private var sword = -1
  private var sliced = false
  private var side = vec3(0), P1 = vec3(0)

  private func placeSword(_ w: World, _ raise: Float) {
    let a = w.actors[plan.mover]
    guard let s = w.prop(sword) else { return }
    let grip = bonePoint(a, KB.rFore, Medieval.handR(MAsset.knight))
    let elbow = bonePoint(a, KB.rFore, Medieval.skeleton(MAsset.knight).joint[KB.rFore])
    let fore = normalize(grip - elbow + vec3(1e-4))
    let up = normalize(mix(fore, vec3(0, 1, 0), raise) + vec3(1e-4))
    s.rot = aimY(up, a.forward())
    s.scale = 0.6
    s.pos = grip - up * 0.12 * s.scale
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.2
      if faceOff(w) { next() }
    case 1:  // the lance vanishes, a sword appears
      if enter() {
        sword = w.addProp(.sword)
        w.sound(.chime, 0.6, a.pos, 1.15)
        w.fx.magicBurst(bonePoint(a, KB.lance, Medieval.itemTip(MAsset.knight)), kGold * 2, 20, 1)
      }
      let u = clamp(pt / 0.6, 0, 1)
      a.ctl.itemR = 0
      a.ctl.armR = vec3(1.8 * easeInOut(u), 0.3 * u, -0.4 * u)
      placeSword(w, 0.6)
      if let s = w.prop(sword) {
        s.dissolve = 1 - u
        s.glow = vec4(kGold, 1.2 - u)
      }
      v.tremble = 0.8
      if u >= 1 { next() }
    case 2:  // gallop past the victim, sword raised
      if enter() {
        side = vec3(-dir.z, 0, dir.x)
        // pass with the victim on the rider's sword (right) side
        let right = -side
        P1 = V - right * 0.42 + dir * 0.9
        walker = Walker(plan.mover, P1, .horse, 2.2)
        walker.run = 1
        w.sound(.neigh, 0.7, a.pos, 1.0)
      }
      walker.update(w, dt)
      placeSword(w, 0.6)
      let rel = flat(V - a.pos)
      let along = dot(rel, normalize(flat(P1 - A) + vec3(1e-4)))
      if !sliced && along < 0.45 {
        // swing
        let k = clamp((0.45 - along) / 0.3, 0, 1)
        a.ctl.armR = vec3(mix(1.8, -0.4, k), 0.3 + 0.4 * k, -0.4 * (1 - k))
        a.ctl.twist = -0.5 * k
        if k >= 1 || along < 0.15 {
          sliced = true
          let c = victimCenter(w)
          w.slice(v, 0.9)
          w.fx.sparks(c, vec3(0, 1, 0), 40, 4.0, 1.0, vec3(4.0, 3.0, 1.5))
          w.fx.flash(c, 0.8, kGold * 5, 0.12)
          w.light(c, kGold * 22, 4, 0.3)
          w.sound(.clang, 1, c, 1.1)
          w.sound(.shatter, 0.6, c, 1.2)
          w.shake(0.4)
          w.slowmo(0.25, 0.5)
        }
      } else if sliced {
        a.ctl.armR = vec3(approach(a.ctl.armR.x, 0.3, dt * 3), 0.3, 0)
        a.ctl.twist = approach(a.ctl.twist, 0, dt * 2)
      }
      v.tremble = 1
      if walker.s >= walker.len - 1e-3 { next() }
    case 3:  // wheel round
      walker.update(w, dt)
      a.ctl.armR = vec3(approach(a.ctl.armR.x, 0, dt * 2), approach(a.ctl.armR.y, 0, dt), 0)
      placeSword(w, 0.6 * (1 - clamp(pt / 0.8, 0, 1)))
      if let s = w.prop(sword) {
        let u = clamp((pt - 0.4) / 0.5, 0, 1)
        s.dissolve = u
        s.glow = vec4(kGold, 0.8 * u)
        if u >= 1 { s.alive = false }
      }
      if pt > 0.9 { next() }
    case 4:
      // (the fresh lance is set every frame: moveInM() needs this phase's enter()
      // to start the ride back onto the square)
      a.ctl.itemR = 1
      if moveInM(w, dt, .horse, 1.0) { next() }
    case 5:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// BISHOP - seated on his cathedra
// ===========================================================================
/// Raises crozier and hand to heaven (u: 0..1).
func bishopInvoke(_ a: Actor, _ u: Float, _ t: Float) {
  a.ctl.armL = vec3(0.9 * u, 0.1 * u, 0.4 * u)
  a.ctl.armR = vec3(0.7 * u, 0.0, -0.2 * u)
  a.ctl.headPitch = -0.35 * u
  a.ctl.lean = -0.08 * u + 0.02 * sin(t * 2) * u
}

/// Wrath of heaven: the Dies irae on the organ, a storm over the victim, lightning.
final class BishopWrath: MBattle {
  private var cloud = vec3(0)
  private var flick: Float = 0, rumble: Float = 0
  private var strikes = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let stormTint = vec3(0.05, 0.05, 0.06)
    cloud = V + vec3(0, 1.9, 0)
    switch phase {
    case 0:
      shotHeight = 1.0
      shotExtra = 1.4
      if faceOff(w) { next() }
    case 1:  // the hymn begins; the bishop raises his crozier; a storm gathers
      if enter() {
        w.sound(.hymn, 0.95, a.pos)
        w.sound(.wind, 0.6, cloud, 0.9)
      }
      let u = clamp(pt / 3.6, 0, 1)
      bishopInvoke(a, easeInOut(pt / 1.0), w.time)
      a.glow = vec4(kGold, 0.35 * easeInOut(pt / 1.0))
      let crozier = bonePoint(a, HB.itemL, Medieval.skeleton(MAsset.bishop).joint[HB.itemL]) + vec3(0, 0.7, 0)
      w.frameLights.append(PointLight(crozier, 2, kGold * 3 * easeInOut(pt / 1.0)))
      cloudPuffs(w, cloud, 0.3 + 0.45 * easeOut(u), 70 + 150 * u, dt, stormTint, 0)
      if u > 0.35 { rain(w, cloud, 0.55, 160 * (u - 0.35), dt) }
      flick -= dt
      if u > 0.3 && flick <= 0 {  // lightning inside the cloud
        flick = w.fx.rnd(0.25, 0.6)
        let p = cloud + w.fx.rndDir() * 0.3
        w.fx.bolt(p, p + w.fx.rndDir() * 0.35, vec3(2.0, 2.4, 3.5), 0.04, 5)
        w.light(p, vec3(8.0, 9.0, 14.0), 3, 0.12)
        var g = Particle()
        g.pos = p
        g.size = 0.6
        g.color = vec3(0.8, 0.9, 1.3)
        g.colorEnd = g.color * 0.1
        g.opacity = 0
        g.maxLife = 0.15
        g.type = .glow
        g.fadeIn = 0
        g.still = true
        w.fx.add(g)
      }
      rumble -= dt
      if u > 0.4 && rumble <= 0 {
        rumble = 1.4
        w.sound(.rumble, 0.6, cloud, 0.7)
      }
      v.tremble = 0.4 + 0.8 * u
      v.ctl.headPitch = -0.35 * u
      if u >= 1 { next() }
    case 2:  // strike!
      cloudPuffs(w, cloud, 0.75, 150, dt, stormTint, 0)
      rain(w, cloud, 0.55, 110, dt)
      bishopInvoke(a, 1, w.time)
      if enter() || (strikes == 1 && pt > 0.32) {
        strikes += 1
        let top = V + vec3(0, v.alive ? v.height() * 0.95 : 0.1, 0)
        lightning(w, cloud - vec3(0, 0.2, 0), top, strikes == 1 ? 1.4 : 0.8)
        w.sound(.thunder, 1, top, strikes == 1 ? 1.0 : 1.15)
        w.shake(strikes == 1 ? 0.55 : 0.3)
        if strikes == 1 {
          v.crackColor = vec3(0.7, 0.85, 1.0)
          v.crack = 0.8
          v.heat = 0.6
          v.glow = vec4(0.6, 0.75, 1.0, 1.5)
          w.slowmo(0.3, 0.5)
        }
      }
      if strikes == 1 && pt > 0.18 && v.alive {
        let c = victimCenter(w)
        w.shatter(v, c, vec3(0, 0.5, 0), 2.6, 1.6, 11)
        w.fx.flash(c, 1.4, vec3(5.0, 6.0, 9.0), 0.2)
        w.fx.sparks(c, vec3(0, 1, 0), 50, 5.0, 1.0, vec3(3.0, 3.5, 5.0))
        w.fx.smoke(c, 12, 0.3, 0.8, 0.9)
        w.groundRing(V, vec3(1.2, 1.5, 2.4), 3.4, 0.7, 0.1)
        w.sound(.explode, 0.8, c, 1.2)
      }
      if pt > 0.9 { next() }
    case 3:  // the storm passes, the bishop lowers his arms
      let u = clamp(pt / 1.2, 0, 1)
      bishopInvoke(a, 1 - easeInOut(u), w.time)
      a.glow = vec4(kGold, 0.35 * (1 - u))
      if u < 0.5 {
        cloudPuffs(w, cloud, 0.75, 50 * (1 - u * 2), dt, stormTint, 0)
        rain(w, cloud, 0.55, 100 * (1 - u * 2), dt)
      }
      if u >= 1 { next() }
    case 4:
      if moveInM(w, dt, .slide, 1.2) { next() }
    case 5:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

/// Brimstone: an organ chord, a red cloud, and burning stones raining down.
final class BishopBrimstone: MBattle {
  private struct Rock {
    var prop = -1
    var from = vec3(0), to = vec3(0)
    var t: Float = -1, dur: Float = 0.5
    var lethal = false
  }
  private var cloud = vec3(0)
  private var rocks = [Rock](repeating: Rock(), count: 4)
  private var launched = 0, landed = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let fireTint = vec3(0.16, 0.05, 0.03)
    cloud = V + vec3(0, 2.0, 0)
    switch phase {
    case 0:
      shotHeight = 1.0
      shotExtra = 1.4
      if faceOff(w) { next() }
    case 1:  // the chord; a red cloud boils up over the victim
      if enter() {
        w.sound(.organ, 1, a.pos)
        w.sound(.rumble, 0.7, cloud, 0.6)
      }
      let u = clamp(pt / 2.0, 0, 1)
      bishopInvoke(a, easeInOut(pt / 0.8), w.time)
      cloudPuffs(w, cloud, 0.25 + 0.4 * easeOut(u), 60 + 100 * u, dt, fireTint, u)
      w.frameLights.append(PointLight(cloud - vec3(0, 0.3, 0), 3.5, vec3(6.0, 1.8, 0.4) * u))
      v.tremble = 0.5 + 0.7 * u
      v.ctl.headPitch = -0.35 * u
      if u >= 1 { next() }
    case 2:  // fire and brimstone
      cloudPuffs(w, cloud, 0.65, 140, dt, fireTint, 1)
      w.frameLights.append(PointLight(cloud - vec3(0, 0.3, 0), 3.5, vec3(6.0, 1.8, 0.4)))
      bishopInvoke(a, 1, w.time)
      if launched < 4 && pt > Float(launched) * 0.38 {
        let i = launched
        rocks[i].prop = w.addProp(.cannonball)
        rocks[i].from = cloud + vec3(w.fx.rnd(-0.3, 0.3), -0.15, w.fx.rnd(-0.3, 0.3))
        rocks[i].lethal = launched == 2
        let side = vec3(-dir.z, 0, dir.x)
        let off: [Float] = [0.55, -0.6, 0.0, 0.45]
        if rocks[i].lethal {
          rocks[i].to = V + vec3(0, v.height() * 0.6, 0)
        } else {
          rocks[i].to = V + side * off[launched] + dir * w.fx.rnd(-0.4, 0.4)
        }
        rocks[i].t = 0
        rocks[i].dur = 0.42
        if let p = w.prop(rocks[i].prop) {
          p.heat = 1
          p.scale = rocks[i].lethal ? 1.25 : w.fx.rnd(0.8, 1.0)
          p.pos = rocks[i].from
        }
        w.sound(.whoosh, 0.6, rocks[i].from, 0.6)
        launched += 1
      }
      for i in rocks.indices {
        if rocks[i].t < 0 { continue }
        rocks[i].t += dt
        let r = rocks[i]
        let p = w.prop(r.prop)
        let u = clamp(r.t / r.dur, 0, 1)
        if let p {
          p.pos = mix(r.from, r.to, easeIn(u))
          // glm::angleAxis(dt * 9, (1, 0, 0.3)): the axis is not normalised
          let h = dt * 9 * 0.5
          p.rot = (quat(cos(h), sin(h), 0, 0.3 * sin(h)) * p.rot).normalized
          w.frameLights.append(PointLight(p.pos, 1.8, vec3(5.0, 1.8, 0.4)))
          w.fx.embers(p.pos, 2, 0.6)
          if w.fx.rnd() < 0.7 { w.fx.smoke(p.pos, 1, 0.03, 0.2, 0.8) }
        }
        if u >= 1 {
          p?.alive = false
          rocks[i].t = -1
          landed += 1
          let at = r.lethal ? r.to : flat(r.to)
          w.fx.fireball(at, r.lethal ? 1.1 : 0.6)
          w.groundRing(flat(at), vec3(2.2, 0.9, 0.3), 3.0, 0.5, 0.08)
          w.light(at, vec3(30.0, 12.0, 3.0), 4, 0.4)
          w.sound(.explode, r.lethal ? 1.0 : 0.6, at, r.lethal ? 0.9 : 1.2)
          w.shake(r.lethal ? 0.55 : 0.25)
          if r.lethal && v.alive {
            v.crackColor = vec3(1.0, 0.5, 0.15)
            v.crack = 0.9
            w.shatter(v, at + vec3(0, 0.2, 0), vec3(0, -1.0, 0), 2.5, 1.4, 10)
            w.slowmo(0.3, 0.45)
          } else if v.alive {
            v.heat = min(1, v.heat + 0.25)
          }
        }
      }
      v.tremble = 1.2
      if landed >= 4 && pt > 2.0 { next() }
    case 3:
      let u = clamp(pt / 1.0, 0, 1)
      bishopInvoke(a, 1 - easeInOut(u), w.time)
      if u >= 1 { next() }
    case 4:
      if moveInM(w, dt, .slide, 1.2) { next() }
    case 5:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

/// Pillar of salt: a bell tolls three times and the victim turns to salt and crumbles away.
final class BishopSalt: MBattle {
  private var tolls = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let pale = vec3(1.0, 0.96, 0.88)
    switch phase {
    case 0:
      shotHeight = 0.8
      shotExtra = 1.0
      if faceOff(w) { next() }
    case 1:  // the bell tolls; a shaft of pale light falls on the victim, which turns to salt
      if tolls < 3 && pt >= Float(tolls) * 1.35 {
        w.sound(.bell, 0.9 - 0.1 * Float(tolls), a.pos, 1.0)
        tolls += 1
      }
      let u = clamp(pt / 3.6, 0, 1)
      bishopInvoke(a, easeInOut(pt / 0.9) * 0.8, w.time)
      // light from above
      for _ in 0..<3 {
        var p = Particle()
        let an = w.fx.rnd(0, kTau)
        let r = w.fx.rnd(0, v.radius() * 1.3)
        p.pos = V + vec3(cos(an) * r, w.fx.rnd(0, 2.2), sin(an) * r)
        p.vel = vec3(0, -w.fx.rnd(0.2, 0.6), 0)
        p.size = w.fx.rnd(0.008, 0.02)
        p.color = pale * w.fx.rnd(1.5, 3.0)
        p.colorEnd = pale * 0.2
        p.opacity = 0
        p.maxLife = w.fx.rnd(0.6, 1.2)
        p.type = .glow
        p.drag = 0.2
        w.fx.add(p)
      }
      w.frameLights.append(PointLight(V + vec3(0, 1.4, 0), 3, pale * (4 + 10 * min(1, pt))))
      v.stone = easeInOut(u)
      v.tremble = 0.8 * (1 - u)
      v.idleWeight = 1 - u
      v.glow = vec4(pale, 0.4 * u)
      if u >= 1 { next() }
    case 2:  // and crumbles to dust from the top down
      if enter() {
        v.dissolveColor = pale * 0.8
        w.sound(.shatter, 0.7, victimCenter(w), 0.7)
        w.sound(.rumble, 0.5, V, 1.4)
      }
      let u = clamp(pt / 1.6, 0, 1)
      if v.alive {
        v.dissolve = easeIn(u)
        v.stone = 1
        let h = v.height() * (1 - u)
        w.fx.dust(V + vec3(0, h, 0), 2, v.radius() * 0.8, 0.3, vec3(0.86, 0.84, 0.8))
        if w.fx.rnd() < 0.6 { w.fx.chips(V + vec3(0, h, 0), 3, 0.8, false) }
        if u >= 1 {
          v.alive = false
          v.visible = false
          w.fx.dust(V, 24, 0.5, 0.8, vec3(0.86, 0.84, 0.8))
        }
      }
      bishopInvoke(a, 0.8 * (1 - easeInOut(u)), w.time)
      if u >= 1 && pt > 1.8 { next() }
    case 3:
      if moveInM(w, dt, .slide, 1.2) { next() }
    case 4:
      return finishM(w)
    default:
      break
    }
    return false
  }
}
