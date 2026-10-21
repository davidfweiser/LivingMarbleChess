// The classic battle style (style 0) of every attacking piece type.
import Foundation
import simd

// ---------------------------------------------------------------------------
// Pawn: hop up, wind up, head-butt. The victim topples and bursts on the board.
// ---------------------------------------------------------------------------
final class PawnBattle: BattleBase {
  var approach = Mover()
  var theta: Float = 0, omega: Float = 0
  var victimStart = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // approach
      if enter() { approach = Mover(plan.mover, V - dir * 0.68, .hop) }
      v.tremble = 0.7
      if approach.update(w, dt) { next() }
    case 2:  // wind up
      if enter() { a.yawTarget = faceAngle(V - a.pos) }
      a.poseSquash = mix(1, 0.78, easeOut(pt / 0.35))
      a.poseBend = vec2(0, -0.38 * easeOut(pt / 0.35))
      v.tremble = 1
      if pt > 0.38 { next() }
    case 3:  // strike
      let u = clamp(pt / 0.11, 0, 1)
      a.poseSquash = mix(0.78, 1.12, u)
      a.poseBend = vec2(0, mix(-0.38, 0.8, easeIn(u)))
      a.pos = V - dir * mix(0.68, 0.56, u)
      if u >= 1 {
        let hit = V - dir * 0.24 + vec3(0, min(v.height(), a.height()) * 0.62, 0)
        w.fx.sparks(hit, -dir + vec3(0, 0.4, 0), 26, 3.5, 0.6, vec3(3.0, 2.6, 2.0))
        w.fx.chips(hit, 18, 2.5, v.color == 1)
        w.fx.flash(hit, 0.35, vec3(2.0, 1.8, 1.5), 0.08)
        w.light(hit, vec3(6.0, 5.0, 4.0), 2.5, 0.15)
        w.sound(.clack, 1.0, hit, 0.9)
        w.shake(0.28)
        w.slowmo(0.3, 0.3)
        v.tremble = 0
        v.tiltPivot = vec3(0, 0, -v.radius() / v.scale * 0.92)
        omega = 3.6
        theta = 0
        victimStart = v.pos
        next()
      }
    case 4:  // topple
      a.poseBend = vec2(0, 0.25)
      a.poseSquash = 1
      omega += 11 * sin(theta + 0.25) * dt
      theta += omega * dt
      v.tilt = angleAxis(-theta, vec3(1, 0, 0))
      v.pos = victimStart + dir * 0.18 * easeOut(pt / 0.4)
      if theta > 1.22 {
        let top = v.pos + dir * v.height() * 0.7 + vec3(0, v.height() * 0.3, 0)
        w.shatter(v, top, dir * (omega * 0.6) + vec3(0, -1, 0), 1.1, 0.6, 7)
        w.fx.dust(v.pos + dir * 0.6, 12, 0.5, 1.4)
        w.groundRing(v.pos + dir * 0.5, vec3(0.8, 0.7, 0.55), 2.5, 0.45, 0.06)
        w.shake(0.3)
        next()
      }
    case 5:
      a.poseBend = vec2(0)
      if pt > 0.55 { next() }
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
// Knight: leaps high over the board and tramples the victim from above.
// ---------------------------------------------------------------------------
final class KnightBattle: BattleBase {
  var vy: Float = 0, landH: Float = 0, dur: Float = 1

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 0.85
      shotExtra = 1.3
      if faceOff(w) { next() }
    case 1:  // crouch
      a.poseSquash = mix(1, 0.68, easeOut(pt / 0.3))
      a.poseBend = vec2(0, -0.3 * easeOut(pt / 0.3))
      v.tremble = 0.8
      if pt > 0.3 {
        w.sound(.whoosh, 0.6, a.pos, 0.8)
        w.fx.dust(a.pos, 8, 0.4, 1.0)
        next()
      }
    case 2:  // leap
      if enter() {
        dur = 0.85 + 0.08 * L
        landH = v.height() * 0.88
      }
      let u = clamp(pt / dur, 0, 1)
      let apex = 1.2 + 0.1 * L
      var p = mix(A, V, easeInOut(u) * 0.8 + u * 0.2)
      p.y = 4 * u * (1 - u) * apex + u * u * landH
      a.pos = p
      a.poseSquash = 1
      a.poseBend = vec2(0, mix(-0.25, -0.55, u))  // rearing up
      v.poseBend = vec2(0, -0.35 * u)              // victim looks up
      v.tremble = 1
      if u >= 1 {
        let c = V + vec3(0, landH, 0)
        w.shatter(v, c, vec3(0, -2.5, 0), 2.6, 0.25, 8)
        w.groundRing(V, vec3(1.6, 1.3, 0.9), 3.2, 0.7, 0.1)
        w.fx.dust(V, 26, 0.7, 2.4)
        w.fx.sparks(c, vec3(0, 1, 0), 20, 3.0, 1.0)
        w.light(c, vec3(6.0, 5.0, 4.0), 3.0, 0.2)
        w.sound(.explode, 0.7, c, 1.3)
        w.shake(0.55)
        w.slowmo(0.28, 0.4)
        vy = -1
        next()
      }
    case 3:  // drop to the board
      vy -= 14 * dt
      a.pos.y = max(0, a.pos.y + vy * dt)
      a.poseBend = vec2(0, 0.2)
      if a.pos.y <= 0 {
        w.sound(.thud, 1.0, a.pos, 0.85)
        w.fx.dust(a.pos, 14, 0.5, 1.6)
        w.shake(0.3)
        next()
      }
    case 4:  // triumphant rear
      a.poseBend = vec2(0, -0.45 * sin(kPI * clamp(pt / 0.6, 0, 1)))
      if pt > 0.7 {
        a.poseBend = vec2(0)
        next()
      }
    case 5:
      if moveIn(w, dt) { next() }  // already there; settles yaw
    case 6:
      return finish(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Bishop: rises, gathers holy light and smites; the victim cracks with light.
// ---------------------------------------------------------------------------
final class BishopBattle: BattleBase {
  var orb = vec3(0), orbFrom = vec3(0), orbTo = vec3(0)
  var orbDur: Float = 0.4
  let gold = vec3(1.0, 0.82, 0.48)

  func head(_ w: World) -> vec3 {
    let a = w.actors[plan.mover]
    return a.pos + vec3(0, a.height() * 0.97, 0) + a.forward() * 0.05
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // rise and charge
      if enter() { w.sound(.charge, 0.9, a.pos) }
      let u = clamp(pt / 1.0, 0, 1)
      a.pos.y = 0.34 * easeInOut(u) + 0.02 * sin(w.time * 6)
      a.glow = vec4(gold, 1.3 * u)
      a.poseBend = vec2(0, -0.12 * u)
      a.idleWeight = 0
      w.frameLights.append(PointLight(head(w), 3.0, gold * (2 + 14 * u)))
      w.fx.swirl(head(w), 0.22 * (1.2 - u), gold * 3, w.time)
      v.tremble = 0.5 + 0.5 * u
      if u >= 1 {
        orbFrom = head(w)
        orbTo = victimCenter(w)
        orbDur = clamp(length(orbTo - orbFrom) / 8, 0.18, 0.7)
        w.sound(.zap, 1.0, orbFrom)
        w.fx.magicBurst(orbFrom, gold * 4, 30, 2.0)
        next()
      }
    case 2:  // bolt of light
      a.poseBend = vec2(0, 0.22)
      a.pos.y = 0.34 + 0.02 * sin(w.time * 6)
      let u = clamp(pt / orbDur, 0, 1)
      let prev = orb
      orb = mix(orbFrom, orbTo, u)
      w.frameLights.append(PointLight(orb, 3.5, gold * 22))
      w.fx.flash(orb, 0.16, gold * 6, 0.06)
      w.fx.magicTrail(orb, (orb - prev) / max(dt, 1e-4), gold * 4)
      w.fx.bolt(orbFrom, orb, gold * 3, 0.03, 6)
      if u >= 1 {
        w.fx.magicBurst(orb, gold * 5, 40, 2.5)
        w.fx.flash(orb, 0.8, gold * 6, 0.15)
        w.sound(.crackle, 0.8, orb, 1.3)
        w.shake(0.2)
        v.crackColor = vec3(1.0, 0.86, 0.55)
        next()
      }
    case 3:  // light bursts through cracks
      let u = clamp(pt / 0.8, 0, 1)
      if enter() { w.sound(.rumble, 0.7, V, 1.4) }
      v.crack = easeIn(u)
      v.glow = vec4(gold, 0.8 * u)
      v.tremble = 1.4
      a.glow = vec4(gold, 1.3 * (1 - u * 0.6))
      w.frameLights.append(PointLight(victimCenter(w), 3.0, gold * (4 + 16 * u)))
      if u >= 1 {
        let c = victimCenter(w)
        w.shatter(v, c, vec3(0), 2.7, 1.3, 9)
        w.fx.flash(c, 1.6, gold * 8, 0.25)
        w.fx.magicBurst(c, gold * 5, 90, 3.0)
        w.groundRing(V, gold * 1.6, 3.0, 0.7, 0.08)
        w.light(c, gold * 40, 5.0, 0.5)
        w.addFlash(gold * 0.15)
        w.sound(.explode, 0.6, c, 1.5)
        w.shake(0.4)
        w.slowmo(0.33, 0.4)
        next()
      }
    case 4:  // descend
      let u = clamp(pt / 0.55, 0, 1)
      a.pos.y = 0.34 * (1 - easeInOut(u))
      a.glow = vec4(gold, 0.5 * (1 - u))
      a.poseBend = vec2(0, 0.22 * (1 - u))
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
// Rook: the tower is a cannon. Boom, iron ball, explosion.
// ---------------------------------------------------------------------------
final class RookBattle: BattleBase {
  var ball = -1
  var p0 = vec3(0), v0 = vec3(0)
  var T: Float = 0.6
  let g: Float = 9.8

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // rumble
      if enter() { w.sound(.rumble, 0.9, a.pos) }
      let u = clamp(pt / 0.6, 0, 1)
      a.tremble = 0.6 * u
      a.poseSquash = mix(1, 0.9, u)
      if w.fx.rnd() < 0.4 { w.fx.smoke(a.pos + vec3(0, a.height(), 0), 1, 0.05, 0.4, 0.4) }
      v.tremble = 0.8
      if u >= 1 {
        a.tremble = 0
        p0 = a.pos + vec3(0, a.height() * 1.02, 0) + dir * 0.05
        let target = victimCenter(w)
        T = 0.48 + 0.07 * L
        v0 = (target - p0) / T + vec3(0, 0.5 * g * T, 0)
        ball = w.addProp(.cannonball)
        if let b = w.prop(ball) {
          b.pos = p0
          b.heat = 1
        }
        // muzzle blast
        w.fx.flash(p0, 0.9, vec3(8.0, 4.5, 1.8), 0.12)
        w.fx.smoke(p0, 7, 0.12, 0.6, 0.65)
        w.fx.sparks(p0, normalize(v0), 30, 4.0, 0.5)
        w.fx.embers(p0, 12, 1.5)
        w.light(p0, vec3(30.0, 15.0, 5.0), 4.0, 0.2)
        w.sound(.boom, 1.0, p0)
        w.shake(0.35)
        a.sq.v -= 4  // recoil squash
        next()
      }
    case 2:  // flight
      let u = pt
      a.pos = A - dir * 0.12 * exp(-u * 4) * sin(min(u * 20, kPI))
      a.poseSquash = 1
      let b = w.prop(ball)
      if let b {
        let prev = b.pos
        let tt = min(u, T)
        b.pos = p0 + v0 * tt + vec3(0, -0.5 * g * tt * tt, 0)
        let vel = (b.pos - prev) / max(dt, 1e-4)
        b.rot = (angleAxis(dt * 12, normalize(cross(vel, vec3(0, 1, 0)) + vec3(1e-4))) * b.rot).normalized
        b.heat = mix(1, 0.55, u / T)
        w.frameLights.append(PointLight(b.pos, 1.5, vec3(3.0, 1.0, 0.25)))
        if w.fx.rnd() < 0.8 { w.fx.smoke(b.pos, 1, 0.02, 0.05, 0.7) }
        w.fx.embers(b.pos, 1, 0.3)
      }
      v.tremble = 1
      if u >= T {
        let c = victimCenter(w)
        let vb = normalize(v0 + vec3(0, -g * T, 0))
        if let b { b.alive = false }
        w.shatter(v, c - vb * 0.1, vb * 3.4 + vec3(0, 1, 0), 1.7, 1.5, 10)
        w.fx.fireball(c, 1.0)
        w.groundRing(V, vec3(2.4, 1.1, 0.35), 3.5, 0.6, 0.1)
        w.light(c, vec3(40.0, 18.0, 6.0), 5.0, 0.55)
        w.addFlash(vec3(0.25, 0.12, 0.04))
        w.sound(.explode, 1.0, c)
        w.shake(0.6)
        w.slowmo(0.3, 0.4)
        next()
      }
    case 3:
      a.pos = A
      if pt > 0.6 { next() }
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

// ---------------------------------------------------------------------------
// Queen: telekinesis. Lifts the victim in a storm of lightning and crushes it.
// ---------------------------------------------------------------------------
final class QueenBattle: BattleBase {
  let violet = vec3(0.72, 0.38, 1.0)
  var boltT: Float = 0, crackleT: Float = 0

  func crown(_ w: World) -> vec3 {
    let a = w.actors[plan.mover]
    return a.pos + vec3(0, a.height() * 0.93, 0)
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 0.95
      shotExtra = 0.9
      if faceOff(w) { next() }
    case 1:  // raise hands (rise + twirl)
      if enter() { w.sound(.charge, 0.8, a.pos, 0.8) }
      let u = clamp(pt / 0.6, 0, 1)
      a.pos.y = 0.18 * easeInOut(u)
      a.poseTwist = sin(u * kPI) * 1.4
      a.glow = vec4(violet, 1.1 * u)
      a.idleWeight = 0
      w.frameLights.append(PointLight(crown(w), 3.0, violet * (3 + 8 * u)))
      if u >= 1 { next() }
    case 2:  // levitate the victim
      let u = clamp(pt / 1.5, 0, 1)
      a.pos.y = 0.18 + 0.02 * sin(w.time * 5)
      a.poseBend = vec2(0, -0.15)
      v.pos.y = 1.05 * easeOut(u)
      v.yawTarget = v.yaw + 1.8
      v.yawRate = 3
      v.tremble = 0.6 + 0.8 * u
      v.glow = vec4(violet, 0.5 + 0.5 * u)
      v.idleWeight = 0
      v.tilt = angleAxis(0.25 * sin(w.time * 3) * u, normalize(vec3(1, 0, 0.4)))
      v.tiltPivot = vec3(0, v.height() * 0.5 / v.scale, 0)
      boltT -= dt
      if boltT <= 0 {
        boltT = w.fx.rnd(0.04, 0.09)
        w.fx.bolt(crown(w), victimCenter(w) + w.fx.rndDir() * 0.12, violet * 7, 0.16, 16)
        if w.fx.rnd() < 0.5 { w.fx.bolt(crown(w), victimCenter(w) + w.fx.rndDir() * 0.2, violet * 4, 0.22, 14) }
      }
      crackleT -= dt
      if crackleT <= 0 {
        crackleT = w.fx.rnd(0.25, 0.4)
        w.sound(.crackle, 0.6, victimCenter(w), w.fx.rnd(0.9, 1.2))
      }
      let flick = 0.6 + 0.4 * w.fx.rnd()
      w.frameLights.append(PointLight(crown(w), 3.5, violet * 10 * flick))
      w.frameLights.append(PointLight(victimCenter(w), 3.5, violet * 12 * flick))
      w.fx.swirl(victimCenter(w), v.radius() * 1.4, violet * 3, w.time)
      if u >= 1 {
        v.crackColor = vec3(0.85, 0.55, 1.0)
        next()
      }
    case 3:  // crush
      let u = clamp(pt / 0.2, 0, 1)
      v.poseSquash = mix(1, 0.55, easeIn(u))
      v.crack = u
      a.poseBend = vec2(0, 0.3 * u)
      w.frameLights.append(PointLight(victimCenter(w), 3.5, violet * 20))
      if u >= 1 {
        let c = victimCenter(w)
        v.poseSquash = 1
        w.shatter(v, c, vec3(0), 2.1, 0.5, 10)
        w.fx.flash(c, 1.3, violet * 6, 0.2)
        w.fx.magicBurst(c, violet * 5, 80, 3.0)
        w.groundRing(V, violet * 1.5, 3.0, 0.8, 0.08)
        w.light(c, violet * 35, 5.0, 0.5)
        w.addFlash(violet * 0.12)
        w.sound(.explode, 0.7, c, 1.2)
        w.shake(0.45)
        w.slowmo(0.33, 0.45)
        next()
      }
    case 4:  // descend
      let u = clamp(pt / 0.5, 0, 1)
      a.pos.y = 0.18 * (1 - easeInOut(u))
      a.glow = vec4(violet, 0.9 * (1 - u))
      a.poseBend = vec2(0, 0.3 * (1 - u))
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
// King: summons a golden blade and cleaves the victim clean in two.
// ---------------------------------------------------------------------------
final class KingBattle: BattleBase {
  var sword = -1
  var approach = Mover()
  var theta: Float = 0
  var sliced = false
  let gold = vec3(1.0, 0.8, 0.42)

  func placeSword(_ w: World, _ th: Float) {
    let v = w.actors[plan.victim]
    guard let s = w.prop(sword) else { return }
    let P = w.lib.piece[v.kind]
    let n = P.sliceN
    let t = normalize(vec3(-n.y, n.x, 0))
    let hm = P.slicePoint.y
    let b = cos(th) * vec3(0, 0, -1) + sin(th) * t
    let y = normalize(b), z = n, x = normalize(cross(y, z))
    let Rl = mat3(x, y, z)
    let Rv = angleAxis(v.yaw, vec3(0, 1, 0))
    let dz = max(0.62, length(flat(w.actors[plan.mover].pos - v.pos)) * 0.62)
    let hiltLocal = vec3(0, hm + 0.08, dz)
    s.pos = v.pos + Rv * (hiltLocal * v.scale)
    s.rot = Rv * quat_cast(Rl)
    s.scale = 1.35
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:
      if enter() { approach = Mover(plan.mover, A, .hop) }
      v.tremble = 0.8
      if approach.update(w, dt) {
        v.yawTarget = faceAngle(a.pos - v.pos)
        v.yaw = v.yawTarget
        next()
      }
    case 2:  // summon the blade
      if enter() {
        sword = w.addProp(.sword)
        w.sound(.chime, 0.8, a.pos, 0.9)
      }
      let u = clamp(pt / 0.7, 0, 1)
      theta = radians(95)
      placeSword(w, theta)
      if let s = w.prop(sword) {
        s.dissolve = 1 - u
        s.glow = vec4(gold, 1.6 - u)
        w.fx.magicTrail(s.pos + s.rot * vec3(0, 0.5, 0), vec3(0), gold * 3)
        w.frameLights.append(PointLight(s.pos + s.rot * vec3(0, 0.5, 0), 2.5, gold * (6 * (1.2 - u))))
      }
      a.glow = vec4(gold, 0.5 * u)
      v.tremble = 1
      if u >= 1 { next() }
    case 3:  // wind up
      let u = clamp(pt / 0.35, 0, 1)
      theta = radians(mix(95, 128, easeOut(u)))
      placeSword(w, theta)
      a.poseBend = vec2(0, -0.22 * u)
      if let s = w.prop(sword) { s.glow = vec4(gold, 0.6) }
      if u >= 1 {
        w.sound(.whoosh, 1.0, a.pos, 1.1)
        next()
      }
    case 4:  // swing
      let u = clamp(pt / 0.22, 0, 1)
      let prevTheta = theta
      theta = radians(mix(128, -105, easeIn(u)))
      placeSword(w, theta)
      a.poseBend = vec2(0, mix(-0.22, 0.35, u))
      if let s = w.prop(sword) {
        s.glow = vec4(gold, 0.9)
        w.fx.magicTrail(s.pos + s.rot * vec3(0, 0.85, 0), vec3(0), gold * 2)
      }
      if !sliced && prevTheta > radians(-4) && theta <= radians(-4) {
        sliced = true
        let c = victimCenter(w)
        w.slice(v, 0.9)
        w.fx.sparks(c, vec3(0, 1, 0), 40, 4.0, 1.0, vec3(4.0, 3.0, 1.5))
        w.fx.flash(c, 0.9, gold * 5, 0.12)
        w.light(c, gold * 25, 4.0, 0.3)
        w.sound(.clang, 1.0, c)
        w.sound(.shatter, 0.6, c, 1.2)
        w.shake(0.45)
        w.slowmo(0.22, 0.6)
      }
      if u >= 1 { next() }
    case 5:  // the blade fades away
      let u = clamp(pt / 0.8, 0, 1)
      if let s = w.prop(sword) {
        s.dissolve = easeIn(u)
        s.glow = vec4(gold, 0.9 * (1 - u))
        if u >= 1 { s.alive = false }
      }
      a.poseBend = vec2(0, 0.35 * (1 - u))
      a.glow = vec4(gold, 0.5 * (1 - u))
      if u >= 1 { next() }
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
