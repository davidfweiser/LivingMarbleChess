// Medieval battles of the gatehouse (siege stomp, dragon fire, dragon's jaws)
// and of the royals on their thrones (throne charge / ram, sceptre smite and
// sweep, royal cleave, sweeping blow).
import Foundation
import simd

// ===========================================================================
// ROOK - the gatehouse
// ===========================================================================
/// Siege stomp: stand up on stone legs, walk over, and stamp on the victim.
final class RookStomp: MBattle {
  private var start = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 0.8
      shotExtra = 1.0
      if faceOff(w) { next() }
    case 1:  // the gatehouse stands up
      if enter() {
        w.sound(.grind, 0.9, a.pos, 0.7)
        w.sound(.rumble, 0.8, a.pos, 0.8)
      }
      let u = clamp(pt / 0.85, 0, 1)
      a.ctl.legs = easeInOut(u)
      a.tremble = 0.4 * (1 - u)
      if w.fx.rnd() < 0.5 { w.fx.dust(a.pos, 2, 0.35, 0.5) }
      v.tremble = 0.6
      if u >= 1 {
        w.sound(.stomp, 0.8, a.pos, 1.0)
        w.shake(0.15)
        next()
      }
    case 2:  // stomp over
      if enter() { walker = Walker(plan.mover, V - dir * min(L, 0.62), .castle, 1.0) }
      v.tremble = 0.9
      v.ctl.headPitch = -0.2
      if walker.update(w, dt) { next() }
    case 3:  // lift a foot high...
      if enter() {
        a.yawTarget = faceAngle(V - a.pos)
        w.sound(.grind, 0.5, a.pos, 1.3)
        if L > 1.5 { battleShot(w, flat(a.pos), V, 0.8, 0.9, plan.mover, plan.victim) }
      }
      let u = easeOut(pt / 0.45)
      a.ctl.lift = u
      a.ctl.lean = -0.1 * u
      v.tremble = 1.3
      v.ctl.headPitch = -0.4 * u
      if pt > 0.6 {
        start = flat(a.pos)
        next()
      }
    case 4:  // ...and STAMP
      let u = clamp(pt / 0.12, 0, 1)
      a.ctl.lift = 1 - easeIn(u)
      a.ctl.lean = mix(-0.1, 0.12, u)
      a.pos = start + dir * 0.14 * u
      if u >= 1 {
        let c = V + vec3(0, v.height() * 0.3, 0)
        w.shatter(v, c, vec3(0, -3.5, 0) + dir * 0.8, 2.4, 0.25, 8)
        w.groundRing(V, vec3(1.3, 1.0, 0.7), 4.0, 0.8, 0.12)
        w.fx.dust(V, 34, 0.8, 2.6)
        w.fx.chips(V, 30, 3.0, v.color == 1)
        w.sound(.stomp, 1, V, 0.8)
        w.sound(.boom, 0.7, V, 1.4)
        w.shake(0.8)
        w.slowmo(0.28, 0.45)
        // everyone nearby jumps
        for o in w.actors where o.alive && o.visible && o.id != a.id && length(flat(o.pos - V)) < 2.2 {
          o.sq.v -= 2
        }
        next()
      }
    case 5:
      a.ctl.lean = 0.12 * (1 - clamp(pt / 0.4, 0, 1))
      if pt > 0.45 { next() }
    case 6:
      if moveInM(w, dt, .castle, 0.9) { next() }
    case 7:  // sit back down on its foundations
      if enter() {
        a.yawTarget = a.baseYaw
        w.sound(.grind, 0.7, a.pos, 0.8)
      }
      let u = clamp(pt / 0.7, 0, 1)
      a.ctl.legs = 1 - easeInOut(u)
      if u >= 1 {
        w.sound(.thud, 0.9, a.pos, 0.7)
        w.fx.dust(a.pos, 14, a.radius(), 0.9)
        w.shake(0.2)
        next()
      }
    case 8:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

/// Dragon: the drawbridge drops, a dragon crawls out of the gate - fire or jaws.
final class RookDragon: MBattle {
  private let fire: Bool
  private var dragon = -1
  private var gate = vec3(0), out = vec3(0), lunge0 = vec3(0)
  private var growl: Float = 0, roarT: Float = 0, burnT: Float = 0
  private var bitten = false
  private var closing = false
  private let fullScale: Float = 0.68

  init(_ p: MovePlan, _ fire: Bool) {
    self.fire = fire
    super.init(p)
  }

  private func mouth(_ w: World) -> vec3 {
    guard let d = w.puppet(dragon) else { return vec3(0) }
    return puppetPoint(d, DB.head, vec3(0, 0.665, 0.72))
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    var d = w.puppet(dragon)
    switch phase {
    case 0:
      shotHeight = 0.7
      shotExtra = 1.6
      if faceOff(w) { next() }
    case 1:  // slide within reach of the dragon
      if enter() {
        let keep: Float = fire ? 1.65 : 1.45
        walker = Walker(plan.mover, L > keep ? V - dir * keep : flat(a.pos), .slide, 1.5)
        walker.face = false
      }
      a.yawTarget = faceAngle(V - a.pos)
      a.yawRate = 4
      v.tremble = 0.6
      if walker.update(w, dt) && pt > 0.4 { next() }
    case 2:  // the drawbridge drops, the portcullis rises
      if enter() { w.sound(.chain, 1, a.pos) }
      let u = clamp(pt / 0.8, 0, 1)
      a.ctl.bridge = easeIn(u)
      a.ctl.gate = easeInOut(clamp((pt - 0.3) / 0.7, 0, 1))
      if u >= 1 && pt < 0.8 + dt {
        let lip = a.pos + a.forward() * 0.53
        w.sound(.thud, 1, lip, 0.9)
        w.fx.dust(lip, 14, 0.25, 1.0)
        w.shake(0.2)
      }
      if pt > 0.3 && pt - dt <= 0.3 { w.sound(.grind, 0.6, a.pos, 1.4) }
      if pt > 1.05 { next() }
    case 3:  // eyes glow in the dark, a growl
      if enter() { w.sound(.roar, 0.5, a.pos, 0.6) }
      gate = a.pos + a.forward() * 0.12 + vec3(0, 0.14, 0)
      let side = vec3(-a.forward().z, 0, a.forward().x)
      let blink: Float = pt < 0.3 ? pt / 0.3 : 1
      for s in stride(from: -1, through: 1, by: 2) {
        w.fx.flash(gate + side * (0.028 * Float(s)), 0.04 * blink, vec3(6.0, 3.0, 0.5), 0.05)
      }
      w.frameLights.append(PointLight(gate + a.forward() * 0.1, 1.2, vec3(3.0, 1.0, 0.2) * blink))
      v.tremble = 0.9
      if pt > 0.6 { next() }
    case 4:  // the dragon crawls out over the bridge, growing to full size
      if enter() {
        dragon = w.addPuppet(MAsset.dragon, a.color)
        d = w.puppet(dragon)
        gate = flat(a.pos) + a.forward() * 0.05
        out = flat(a.pos) + a.forward() * 0.95
        if let d {
          d.pos = gate
          d.yaw = a.yaw
          d.scale = 0.08
        }
        w.sound(.roar, 0.6, a.pos, 0.85)
        // frame the dragon and its prey from the side, keeping the gatehouse out of the way
        battleShot(w, out, V, 0.75, 1.5, -1, plan.victim)
      }
      let u = clamp(pt / 1.2, 0, 1)
      if let d {
        let prev = d.pos
        d.pos = mix(gate, out, easeInOut(u))
        d.pos.y = 0.02 * sin(u * kPI)
        d.scale = mix(0.08, fullScale, easeOut(u))
        d.ctl.gait += length(d.pos - prev) / 0.25 * kTau
        d.ctl.stride = sin(u * kPI) + 0.2
        d.ctl.wings = 0
        if w.fx.rnd() < 0.3 { w.fx.smoke(d.pos + vec3(0, 0.2, 0), 1, 0.15, 0.3, 0.6) }
        d.yaw = faceAngle(V - d.pos)
      }
      v.tremble = 1
      if u >= 1 { next() }
    case 5:  // rear up, spread the wings, ROAR
      if enter() { w.sound(.roar, 1, d?.pos ?? a.pos, 1) }
      let u = clamp(pt / 1.05, 0, 1)
      if let d {
        d.ctl.stride = approach(d.ctl.stride, 0, dt * 3)
        d.ctl.neck = sin(u * kPI * 0.8) * 1
        d.ctl.jaw = sin(clamp(u * 1.4, 0, 1) * kPI) * 0.9 + 0.1
        d.ctl.wings = easeOut(clamp(u * 1.6, 0, 1))
        d.ctl.flap += dt * 9
        if u > 0.2 && u < 0.7 { w.shake(dt * 1.2) }
      }
      v.tremble = 1.3
      if u >= 1 { next() }
    case 6:
      if fire {  // breathe fire
        if enter() {
          w.sound(.fire, 1, d?.pos ?? a.pos, 1)
          burnT = 0
        }
        if let d {
          d.ctl.neck = approach(d.ctl.neck, -0.35, dt * 3)
          d.ctl.jaw = approach(d.ctl.jaw, 1, dt * 6)
          d.ctl.flap += dt * 6
          d.ctl.wings = 1
          let m = mouth(w)
          let tgt = victimCenter(w)
          let fd = normalize(tgt - m + vec3(1e-4))
          let len = length(tgt - m)
          if pt > 0.15 {
            fireJet(w, m, fd, len, dt)
            w.frameLights.append(PointLight(m + fd * len * 0.5, 3, vec3(18.0, 7.0, 1.5) * (0.8 + 0.4 * w.fx.rnd())))
            w.frameLights.append(PointLight(tgt, 2.5, vec3(14.0, 5.0, 1.0)))
          }
        }
        if v.alive {
          let u = clamp((pt - 0.25) / 1.25, 0, 1)
          v.heat = u
          v.crackColor = vec3(1.0, 0.45, 0.1)
          v.crack = u * 0.9
          v.tremble = 1.5
          if w.fx.rnd() < 0.4 { w.fx.embers(victimCenter(w), 2, 1.2) }
          if u >= 1 {
            let c = victimCenter(w)
            w.shatter(v, c, vec3(0, 0.5, 0) + dir * 1.5, 2.2, 1.2, 9)
            w.fx.fireball(c, 1.0)
            w.light(c, vec3(40.0, 16.0, 4.0), 5, 0.5)
            w.groundRing(V, vec3(2.4, 1.0, 0.3), 3.4, 0.6, 0.1)
            w.sound(.explode, 1, c, 0.9)
            w.shake(0.5)
            w.slowmo(0.3, 0.45)
            // the debris keeps burning for a moment
            for ch in w.chunks where length(flat(ch.pos - V)) < 0.8 && ch.age < 0.05 {
              ch.glow = 1
              ch.glowColor = vec3(1.0, 0.4, 0.1)
            }
          }
        }
        if !v.alive && pt > 1.85 { next() }
      } else {  // lunge and bite
        if enter() {
          lunge0 = d?.pos ?? vec3(0)
          bitten = false
        }
        let u = clamp(pt / 0.5, 0, 1)
        if let d, !bitten {
          let toV = flat(V - lunge0)
          let reach = max(0, length(toV) - 0.62)
          d.pos = lunge0 + normalize(toV + vec3(1e-4)) * reach * easeIn(u)
          d.ctl.neck = mix(1.0, -0.9, easeIn(u))
          d.ctl.jaw = u < 0.8 ? 1 : 1 - (u - 0.8) / 0.2
          d.ctl.wings = 1
          d.ctl.flap += dt * 10
          if u >= 1 {
            bitten = true
            w.sound(.bite, 1, V, 1.0)
            w.fx.chips(victimCenter(w), 20, 2.0, v.color == 1)
            w.shake(0.35)
            v.crackColor = vec3(1.0, 0.6, 0.3)
            v.crack = 0.6
          }
        }
        if bitten, let d {
          // lift the victim in its jaws and shake it
          let t2 = pt - 0.5
          d.ctl.neck = mix(-0.9, 0.5, easeInOut(t2 / 0.6))
          d.ctl.headYaw = 0.5 * sin(t2 * 22) * clamp(t2 / 0.3, 0, 1)
          d.ctl.jaw = 0.15
          if v.alive {
            let m = mouth(w)
            v.pos = m - vec3(0, v.height() * 0.65, 0)
            v.tilt = angleAxis(0.6 * sin(t2 * 22), dir)
            v.tiltPivot = vec3(0, v.height() * 0.7, 0)
            v.tremble = 1.5
            v.idleWeight = 0
            if t2 > 1.0 {
              let c = victimCenter(w)
              w.sound(.bite, 1, c, 0.8)
              w.sound(.shatter, 0.8, c, 0.9)
              w.shatter(v, c, vec3(0, -1.0, 0) - dir * 0.5, 1.6, 0.3, 9)
              w.fx.dust(c, 14, 0.3, 1.0)
              w.shake(0.45)
              w.slowmo(0.3, 0.4)
            }
          }
          if !v.alive && t2 > 1.6 { next() }
        }
      }
    case 7:  // fold the wings and go back in, shrinking into the dark
      if enter() {
        lunge0 = d?.pos ?? out
        if let d { d.yaw = faceAngle(gate - d.pos) }
        w.sound(.roar, 0.4, lunge0, 1.3)
      }
      let u = clamp(pt / 1.15, 0, 1)
      if let d {
        let prev = d.pos
        d.ctl.wings = approach(d.ctl.wings, 0, dt * 2.5)
        d.ctl.neck = approach(d.ctl.neck, 0, dt * 2)
        d.ctl.headYaw = approach(d.ctl.headYaw, 0, dt * 3)
        d.ctl.jaw = approach(d.ctl.jaw, 0, dt * 3)
        d.pos = mix(lunge0, gate, easeInOut(u))
        d.pos.y = 0
        d.scale = mix(fullScale, 0.05, easeIn(u))
        d.ctl.gait += length(d.pos - prev) / 0.25 * kTau
        d.ctl.stride = sin(u * kPI) + 0.2
        if u > 0.85 { d.dissolve = (u - 0.85) / 0.15 }
        if u >= 1 { d.alive = false }
      }
      if u >= 1 { next() }
    case 8:  // portcullis down and drawbridge up as the gatehouse glides onto the square
      // (moveInM() needs this phase's enter() to start the glide, so the sounds use a flag)
      if !closing {
        closing = true
        w.sound(.clang, 0.6, a.pos, 0.6)
        w.sound(.chain, 0.9, a.pos, 1.05)
      }
      let u = clamp(pt / 0.9, 0, 1)
      a.ctl.gate = 1 - easeIn(clamp(u * 2, 0, 1))
      a.ctl.bridge = 1 - easeInOut(u)
      if u >= 1 && pt - dt < 0.9 { w.sound(.thud, 0.6, a.pos, 1.3) }
      let arrived = moveInM(w, dt, .slide, 1.3)
      if arrived && u >= 1 { next() }
    case 9:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// QUEEN and KING - royals on their thrones
// ===========================================================================
/// Throne charge / ram: the throne rockets across the board into the victim.
final class ThroneRam: MBattle {
  private let shatterIt: Bool
  private var start = vec3(0), back = vec3(0), stop = vec3(0)
  private var dur: Float = 0.5
  private var flight = Flight()

  init(_ p: MovePlan, _ shatterIt: Bool) {
    self.shatterIt = shatterIt
    super.init(p)
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.9
      if faceOff(w) { next() }
    case 1:  // draw back, grip the armrests
      if enter() {
        start = flat(a.pos)
        back = start - dir * 0.28
        w.sound(.slide, 0.6, a.pos, 0.8)
        a.yawTarget = faceAngle(V - A)
      }
      let u = easeInOut(pt / 0.6)
      a.pos = mix(start, back, u)
      a.ctl.lean = 0.3 * u
      a.ctl.armL = vec3(0.3 * u, 0, 0)
      a.ctl.armR = vec3(0.3 * u, 0, 0)
      v.tremble = 0.9
      if pt > 0.75 { next() }
    case 2:  // charge
      if enter() {
        stop = V - dir * touchDist(a, v) * 0.95
        dur = clamp(0.2 + 0.12 * length(stop - back), 0.25, 0.6)
        w.sound(.slide, 1, a.pos, 1.3)
        w.sound(.whoosh, 0.8, a.pos, 0.8)
      }
      let u = clamp(pt / dur, 0, 1)
      a.pos = mix(back, stop, easeIn(u))
      w.fx.dust(flat(a.pos), 1, 0.2, 0.4)
      v.tremble = 1.2
      if u >= 1 {
        let hit = V - dir * v.radius() * 0.7 + vec3(0, 0.35, 0)
        w.fx.sparks(hit, dir + vec3(0, 0.6, 0), 34, 4.0, 0.7, vec3(3.5, 2.8, 1.8))
        w.fx.chips(hit, 24, 2.8, v.color == 1)
        w.light(hit, vec3(8.0, 6.0, 4.0), 3, 0.2)
        w.sound(.clack, 1, hit, 0.7)
        w.sound(.thud, 1, hit, 0.9)
        w.shake(0.5)
        w.slowmo(0.28, 0.4)
        if shatterIt {
          w.shatter(v, hit, dir * 5 + vec3(0, 1.2, 0), 1.8, 1.0, 10)
          w.groundRing(V, vec3(1.2, 1.0, 0.7), 3.0, 0.5, 0.08)
        } else {
          flight.launch(v, dir * 4.2 + vec3(0, 4.6, 0), 9)
        }
        start = flat(a.pos)
        next()
      }
    case 3:  // rebound and settle
      let u = clamp(pt / 0.4, 0, 1)
      a.pos = start - dir * 0.08 * sin(u * kPI)
      a.ctl.lean = 0.3 * (1 - u)
      a.ctl.armL = vec3(0.3 * (1 - u), 0, 0)
      a.ctl.armR = a.ctl.armL
      if v.alive && !shatterIt && flight.update(v, dt) { crashLanding(w, v, dir) }
      if !v.alive && pt > 0.5 { next() }
    case 4:
      if moveInM(w, dt, .slide, 0.9) { next() }
    case 5:
      return finishM(w)
    default:
      break
    }
    return false
  }
}

/// The royal gets up, walks to the victim and strikes it (style decides how),
/// then returns to the throne.
final class RoyalStrike: MBattle {
  enum Strike { case smite, sweep, cleave, blow }
  private let strike: Strike
  private var flight = Flight()
  private var struck = false
  private var sub = 0

  init(_ p: MovePlan, _ s: Strike) {
    strike = s
    super.init(p)
  }

  private func reach() -> Float { strike == .cleave ? 0.55 : 0.42 }

  /// The arm pose during the strike: u 0 = wound up, 1 = follow-through.
  private func strikePose(_ a: Actor, _ wind: Float, _ u: Float) {
    switch strike {
    case .smite:  // sceptre raised high and brought down
      a.ctl.armR = vec3(mix(0, 2.2, wind) - 2 * u, 0.1, -0.3 * wind)
      a.ctl.lean = -0.1 * wind + 0.35 * u
    case .sweep:  // backhanded sweep across the body
      a.ctl.armR = vec3(1.1 * wind, mix(0, 1, wind) - 1.5 * u, -0.2)
      a.ctl.twist = 0.6 * wind - 1.1 * u
      a.ctl.lean = 0.15 * u
    case .cleave:  // the sword overhead, then straight down
      a.ctl.itemYaw = -kPI * 0.5 * min(1, wind * 2 + u)
      a.ctl.armR = vec3(mix(0, 2.6, wind) - 2.5 * u, 0.15 * wind, -0.4 * wind + 0.2 * u)
      a.ctl.armL = vec3(mix(0, 2.3, wind) - 2.2 * u, -0.1, -0.4 * wind)
      a.ctl.lean = -0.12 * wind + 0.4 * u
    case .blow:  // a level sweep of the sword
      a.ctl.itemYaw = -kPI * 0.5 * min(1, wind * 2 + u)
      a.ctl.armR = vec3(1.3 * max(wind, u * 0.8), mix(0, 1.1, wind) - 1.6 * u, -0.2)
      a.ctl.twist = 0.7 * wind - 1.2 * u
      a.ctl.itemPitch = 0.6 * wind
      a.ctl.lean = 0.15 * u
    }
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.0
      shotHeight = 0.75
      if faceOff(w) { next() }
    case 1:  // glide most of the way on the throne, then rise from it
      if sub == 0 {
        if enter() {
          let glide = max(0, L - 1.35)
          walker = Walker(plan.mover, A + dir * glide, .slide, 1.2)
          walker.face = false
          a.yawTarget = faceAngle(V - A)
        }
        if walker.update(w, dt) {
          sub = 1
          entered = false
          pt = 0
        }
      } else if standUp(w, 0.85) {
        next()
      }
      v.tremble = 0.7
    case 2:  // walk to the victim
      if enter() {
        if L > 1.6 { battleShot(w, flat(a.pos), V, shotHeight, 0.6, plan.mover, plan.victim) }
        let off = Medieval.standOffset(a.kind) * a.scale
        let stopAt = v.radius() + reach() + off
        walker = Walker(plan.mover, V - dir * min(stopAt, L), .foot, 0.75)
        if strike == .cleave { w.sound(.clang, 0.3, a.pos, 1.6) }
      }
      if strike == .cleave { strikePose(a, easeInOut(pt / 0.8) * 0, 0) }
      v.tremble = 0.9
      if walker.update(w, dt) { next() }
    case 3:  // wind up
      if enter() { a.yawTarget = faceAngle(V - a.pos) }
      let wnd = easeInOut(pt / 0.55)
      strikePose(a, wnd, 0)
      if strike == .smite {
        let orb = bonePoint(a, HB.itemR, Medieval.itemTip(a.kind))
        w.frameLights.append(PointLight(orb, 2, kGold * 6 * wnd))
        if w.fx.rnd() < 0.5 { w.fx.magicTrail(orb, vec3(0), kGold * 2.5) }
      }
      v.tremble = 1.2
      v.ctl.headPitch = -0.3 * wnd
      if pt > 0.7 {
        w.sound(.whoosh, 0.9, a.pos, strike == .sweep ? 0.9 : 1.05)
        next()
      }
    case 4:  // strike
      let u = easeIn(pt / 0.16)
      strikePose(a, 1 - u * 0.6, u)
      if pt >= 0.12 && !struck {
        struck = true
        let c = victimCenter(w)
        let hit = c - dir * v.radius() * 0.5 + vec3(0, v.height() * 0.2, 0)
        switch strike {
        case .smite:
          w.fx.flash(hit, 1.0, kGold * 6, 0.15)
          w.fx.magicBurst(hit, kGold * 4, 60, 2.5)
          w.light(hit, kGold * 30, 4, 0.4)
          v.crackColor = vec3(1.0, 0.85, 0.5)
          w.sound(.clang, 0.9, hit, 0.8)
          w.sound(.zap, 0.6, hit, 0.8)
          w.shatter(v, hit + vec3(0, 0.2, 0), vec3(0, -2.0, 0) + dir, 2.3, 0.8, 10)
        case .sweep:
          let side = vec3(-dir.z, 0, dir.x)
          w.fx.sparks(hit, side + vec3(0, 0.5, 0), 30, 3.5, 0.6, vec3(3.0, 2.6, 2.0))
          w.sound(.clack, 1, hit, 0.75)
          flight.launch(v, dir * 2.5 + side * 3 + vec3(0, 4.4, 0), 9)
        case .cleave:
          w.fx.sparks(c, vec3(0, 1, 0), 40, 4.0, 1.0, vec3(4.0, 3.0, 1.5))
          w.fx.flash(c, 0.9, kGold * 5, 0.12)
          w.light(c, kGold * 25, 4, 0.3)
          w.sound(.clang, 1, c)
          w.sound(.shatter, 0.6, c, 1.2)
          w.slice(v, 0.9)
        case .blow:
          let side = vec3(-dir.z, 0, dir.x)
          w.fx.sparks(hit, side + vec3(0, 0.3, 0), 40, 4.5, 0.6, vec3(4.0, 3.0, 1.5))
          w.fx.flash(hit, 0.7, kGold * 4, 0.1)
          w.sound(.clang, 1, hit, 0.9)
          w.shatter(v, hit, side * 4 + dir * 1.5 + vec3(0, 1.0, 0), 1.6, 0.8, 12)
        }
        w.shake(0.45)
        w.slowmo(0.25, 0.5)
      }
      if pt > 0.3 { next() }
    case 5:  // follow-through, recover
      let u = easeInOut(pt / 0.7)
      strikePose(a, 0.4 * (1 - u), 1 - u)
      if strike == .cleave || strike == .blow { a.ctl.itemYaw = -kPI * 0.5 * (1 - u) }
      if v.alive && strike == .sweep && flight.update(v, dt) { crashLanding(w, v, dir) }
      if pt > 0.8 && !v.alive {
        a.ctl.armR = vec3(0)
        a.ctl.armL = vec3(0)
        a.ctl.twist = 0
        a.ctl.lean = 0
        a.ctl.itemPitch = 0
        a.ctl.itemYaw = 0
        next()
      }
    case 6:  // walk onto the square; the throne follows; sit down
      if ret.update(w, a, squareCenter(Int(plan.move.to)), dt) { next() }
    case 7:
      return finishM(w)
    default:
      break
    }
    return false
  }
}
