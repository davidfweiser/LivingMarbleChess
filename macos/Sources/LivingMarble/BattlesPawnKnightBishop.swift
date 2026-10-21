// Alternative battle styles for the pawn, knight and bishop (styles 1 and 2):
// bull charge, whirlwind, buck kick, joust, judgment beam and seraph orbs.
import Foundation
import simd

// ---------------------------------------------------------------------------
// Pawn: bull charge. Paws the ground, charges and sends the victim flying.
// ---------------------------------------------------------------------------
final class PawnCharge: BattleBase {
  private var start = vec3(0), stop = vec3(0)
  private var dur: Float = 0.5, dustT: Float = 0
  private var kicks = 0
  private var flight = Flight()

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.8
      if faceOff(w) { next() }
    case 1:  // head down, paw the ground
      if enter() { start = a.pos }
      a.poseBend = vec2(0, 0.3 * easeOut(pt / 0.3))
      a.poseSquash = 0.9
      let sp = fmod(pt, 0.3) / 0.3
      a.pos = start - dir * 0.07 * sin(sp * kPI)
      if kicks < 3 && pt > 0.15 + 0.3 * Float(kicks) {
        kicks += 1
        w.fx.dust(a.pos - dir * a.radius(), 5, 0.15, 0.9)
        w.sound(.step, 0.7, a.pos, 0.75)
      }
      v.tremble = 0.9
      if pt > 0.95 { next() }
    case 2:  // charge!
      if enter() {
        start = a.pos
        stop = V - dir * touchDist(a, v)
        dur = clamp(0.22 + 0.09 * length(flat(stop - start)), 0.25, 0.7)
        w.sound(.whoosh, 0.8, a.pos, 0.9)
      }
      let u = clamp(pt / dur, 0, 1)
      a.pos = mix(start, stop, easeIn(u))
      a.pos.y = abs(sin(u * kPI * 3)) * 0.05
      a.poseBend = vec2(0, 0.45)
      a.poseSquash = 1
      dustT -= dt
      if dustT <= 0 {
        dustT = 0.04
        w.fx.dust(flat(a.pos), 2, 0.1, 0.5)
      }
      v.tremble = 1
      if u >= 1 {
        a.pos.y = 0
        let hit = V - dir * v.radius() * 0.8 + vec3(0, min(a.height(), v.height()) * 0.5, 0)
        w.fx.sparks(hit, dir + vec3(0, 0.5, 0), 30, 3.5, 0.7, vec3(3.0, 2.6, 2.0))
        w.fx.chips(hit, 20, 2.5, v.color == 1)
        w.fx.flash(hit, 0.4, vec3(2.0, 1.8, 1.5), 0.08)
        w.light(hit, vec3(6.0, 5.0, 4.0), 2.5, 0.15)
        w.sound(.clack, 1.0, hit, 0.8)
        w.sound(.thud, 0.8, hit, 1.2)
        w.shake(0.35)
        w.slowmo(0.3, 0.35)
        flight.launch(v, dir * 4 + vec3(0, 4.2, 0), 9)
        a.sq.v -= 3
        next()
      }
    case 3:  // the victim flies, the pawn bounces back off the impact
      let u = clamp(pt / 0.3, 0, 1)
      a.pos = stop - dir * 0.1 * sin(u * kPI)
      a.poseBend = vec2(0, mix(-0.3, 0, u))
      if v.alive && flight.update(v, dt) { crashLanding(w, v, dir) }
      if !v.alive && pt > 0.5 { next() }
    case 4:  // stands proud
      a.poseBend = vec2(0, -0.15 * sin(kPI * clamp(pt / 0.5, 0, 1)))
      if pt > 0.5 { next() }
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
// Pawn: whirlwind. Spins up like a top and grinds the victim apart.
// ---------------------------------------------------------------------------
final class PawnWhirl: BattleBase {
  private var start = vec3(0), stop = vec3(0)
  private var spinRate: Float = 0, soundT: Float = 0
  private func spin(_ a: Actor, _ dt: Float) {
    a.yaw = wrapAngle(a.yaw + spinRate * dt)
    a.yawTarget = a.yaw
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // spin up
      let u = clamp(pt / 0.8, 0, 1)
      spinRate = 32 * easeIn(u)
      spin(a, dt)
      a.poseSquash = 0.92
      a.pos.y = 0.04 * u
      a.idleWeight = 0
      whirlDust(w, flat(a.pos), a.radius() * 1.3)
      soundT -= dt
      if soundT <= 0 {
        soundT = 0.32 - 0.15 * u
        w.sound(.whoosh, 0.35 + 0.3 * u, a.pos, 0.8 + 0.6 * u)
      }
      v.tremble = 0.8
      if u >= 1 { next() }
    case 2:  // drill in
      if enter() {
        start = a.pos
        stop = V - dir * touchDist(a, v) + vec3(0, 0.04, 0)
      }
      let u = clamp(pt / 0.45, 0, 1)
      spin(a, dt)
      a.pos = mix(start, stop, easeInOut(u))
      whirlDust(w, flat(a.pos), a.radius() * 1.3)
      soundT -= dt
      if soundT <= 0 {
        soundT = 0.17
        w.sound(.whoosh, 0.65, a.pos, 1.4)
      }
      v.tremble = 1
      if u >= 1 { next() }
    case 3:  // grind
      if enter() { w.sound(.rumble, 0.6, V, 1.5) }
      let u = clamp(pt / 0.7, 0, 1)
      spin(a, dt)
      a.pos = stop + dir * 0.05 * u
      let side = vec3(-dir.z, 0, dir.x)
      let contact = V - dir * v.radius() + vec3(0, min(a.height(), v.height()) * 0.45, 0)
      let flip: Float = w.fx.rnd() < 0.5 ? 1 : -1
      w.fx.sparks(contact, side * flip + vec3(0, 0.4, 0), 4, 4.0, 0.4)
      if w.fx.rnd() < 0.5 { w.fx.chips(contact, 2, 2.0, v.color == 1) }
      w.frameLights.append(PointLight(contact, 1.0, vec3(1.6, 0.9, 0.3)))
      soundT -= dt
      if soundT <= 0 {
        soundT = 0.07
        w.sound(.clack, 0.45, contact, w.fx.rnd(1.2, 1.6))
      }
      v.crack = 0.85 * u
      v.tremble = 1.5
      v.poseTwist = 1.2 * u
      v.idleWeight = 0
      if u >= 1 {
        let c = victimCenter(w)
        w.shatter(v, contact, side * 3.5 + dir * 1.5, 1.6, 1.0, 14)
        w.fx.dust(V, 18, 0.5, 1.8)
        w.groundRing(V, vec3(0.8, 0.7, 0.55), 3.0, 0.5, 0.07)
        w.light(c, vec3(10.0, 6.0, 3.0), 3.0, 0.25)
        w.sound(.clack, 1.0, c, 0.8)
        w.shake(0.4)
        w.slowmo(0.3, 0.35)
        next()
      }
    case 4:  // spin down... dizzy!
      let u = clamp(pt / 1.2, 0, 1)
      spinRate *= exp(-dt * 3.5)
      if spinRate > 1.5 {
        spin(a, dt)
      } else {
        a.yawTarget = faceAngle(dir)
      }
      a.pos.y = 0.04 * (1 - u)
      a.poseSquash = 1
      let wob = 0.18 * (1 - u) * min(1, pt * 3)
      a.poseBend = vec2(cos(pt * 9), sin(pt * 9)) * wob
      if u >= 1 {
        a.poseBend = vec2(0)
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
// Knight: buck kick. Turns its back, rocks forward and kicks the victim sky-high.
// ---------------------------------------------------------------------------
final class KnightKick: BattleBase {
  private var approach = Mover()
  private var flight = Flight()
  private var phi: Float = 0
  private var snorted = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 0.9
      shotExtra = 1.3
      if faceOff(w) { next() }
    case 1:  // trot up
      if enter() { approach = Mover(plan.mover, V - dir * touchDist(a, v) * 1.05, .hop) }
      v.tremble = 0.7
      if approach.update(w, dt) { next() }
    case 2:  // turn around, snort
      if enter() { a.yawTarget = faceAngle(-dir) }
      if !snorted && pt > 0.3 {
        snorted = true
        let nose = a.pos - dir * 0.35 + vec3(0, 0.85, 0)
        w.fx.smoke(nose, 3, 0.03, 0.2, 0.15)
        w.sound(.whoosh, 0.4, nose, 0.55)
      }
      v.tremble = 0.9
      if pt > 0.5 { next() }
    case 3:  // gather
      let u = clamp(pt / 0.25, 0, 1)
      a.poseSquash = mix(1, 0.85, easeOut(u))
      a.poseBend = vec2(0, -0.2 * easeOut(u))
      if u >= 1 {
        // rock forward over the front edge so the hind end swings up
        a.tiltPivot = vec3(0, 0, a.radius() / a.scale * 0.9)
        w.sound(.whoosh, 0.8, a.pos, 1.0)
        next()
      }
    case 4:  // KICK
      let u = clamp(pt / 0.1, 0, 1)
      phi = 0.55 * easeOut(u)
      a.tilt = angleAxis(phi, vec3(1, 0, 0))
      a.poseSquash = 1
      a.poseBend = vec2(0, 0.35 * u)
      if u >= 1 {
        let hit = V - dir * v.radius() + vec3(0, 0.35, 0)
        w.fx.sparks(hit, dir + vec3(0, 0.8, 0), 30, 3.5, 0.6, vec3(3.0, 2.6, 2.0))
        w.fx.chips(hit, 18, 2.5, v.color == 1)
        w.fx.flash(hit, 0.45, vec3(2.0, 1.8, 1.5), 0.08)
        w.light(hit, vec3(6.0, 5.0, 4.0), 2.5, 0.15)
        w.fx.dust(V, 10, 0.4, 1.2)
        w.sound(.clack, 1.0, hit, 0.75)
        w.sound(.thud, 0.9, hit, 0.7)
        w.shake(0.4)
        w.slowmo(0.25, 0.45)
        flight.launch(v, dir * 2 + vec3(0, 5.0, 0), 7)
        next()
      }
    case 5:  // settle back down and watch it fly
      let u = clamp(pt / 0.5, 0, 1)
      phi = 0.55 * (1 - easeInOut(u))
      a.tilt = angleAxis(phi, vec3(1, 0, 0))
      a.poseBend = vec2(0, 0.35 * (1 - u))
      if u >= 1 {
        a.tilt = quat.identity
        a.tiltPivot = vec3(0)
        a.yawTarget = faceAngle(dir)
      }
      if v.alive && flight.update(v, dt) { crashLanding(w, v, dir) }
      if !v.alive && u >= 1 { next() }
    case 6:  // triumphant rear
      a.poseBend = vec2(0, -0.45 * sin(kPI * clamp(pt / 0.6, 0, 1)))
      if pt > 0.7 {
        a.poseBend = vec2(0)
        next()
      }
    case 7:
      if moveIn(w, dt) { next() }
    case 8:
      return finish(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Knight: joust. Summons a golden lance and gallops straight through the victim.
// ---------------------------------------------------------------------------
final class KnightJoust: BattleBase {
  private var lance = -1
  private var start = vec3(0), stop = vec3(0)
  private var dur: Float = 0.55, dustT: Float = 0
  private let gold = vec3(1.0, 0.8, 0.42)
  private let kLanceScale: Float = 1.15, kHilt: Float = 0.1
  private func placeLance(_ w: World) {
    let a = w.actors[plan.mover]
    guard let s = w.prop(lance) else { return }
    s.rot = aimY(dir)
    s.pos = a.pos + vec3(0, a.height() * 0.48, 0) + dir * kHilt
    s.scale = kLanceScale
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.8
      if faceOff(w) { next() }
    case 1:  // summon the lance
      if enter() {
        lance = w.addProp(.sword)
        w.sound(.chime, 0.8, a.pos, 1.2)
      }
      let u = clamp(pt / 0.6, 0, 1)
      a.poseBend = vec2(0, -0.25 * sin(u * kPI))
      placeLance(w)
      if let s = w.prop(lance) {
        s.dissolve = 1 - u
        s.glow = vec4(gold, 1.5 - u)
        w.frameLights.append(PointLight(s.pos + dir * 0.6, 2.0, gold * (5 * (1.2 - u))))
      }
      v.tremble = 0.8
      if u >= 1 { next() }
    case 2:  // gallop
      if enter() {
        start = a.pos
        stop = V - dir * (kHilt + kLanceScale - v.radius() * 0.8)
        dur = clamp(0.3 + 0.15 * length(flat(stop - start)), 0.35, 0.75)
        w.sound(.whoosh, 0.8, a.pos, 0.8)
      }
      let u = clamp(pt / dur, 0, 1)
      a.pos = mix(start, stop, easeIn(u))
      a.pos.y = abs(sin(u * kPI * 4)) * 0.1
      a.poseBend = vec2(0, 0.15 * sin(u * kPI * 8))
      dustT -= dt
      if dustT <= 0 {
        dustT = 0.05
        w.fx.dust(flat(a.pos), 2, 0.15, 0.5)
      }
      placeLance(w)
      v.tremble = 1
      if u >= 1 {
        let c = victimCenter(w)
        w.fx.sparks(c - dir * 0.2, dir + vec3(0, 0.3, 0), 40, 4.5, 0.6, vec3(4.0, 3.0, 1.5))
        w.fx.flash(c, 0.9, gold * 5, 0.12)
        w.light(c, gold * 25, 4.0, 0.3)
        w.sound(.clang, 1.0, c, 0.9)
        w.shatter(v, c - dir * 0.2, dir * 4.5 + vec3(0, 0.8, 0), 1.3, 0.8, 10)
        w.shake(0.45)
        w.slowmo(0.25, 0.5)
        stop = a.pos
        next()
      }
    case 3:  // skid and rear
      let u = clamp(pt / 0.7, 0, 1)
      a.pos = stop + dir * 0.18 * easeOut(u)
      a.poseBend = vec2(0, -0.45 * sin(u * kPI))
      if enter() { w.fx.dust(flat(a.pos), 8, 0.3, 1.0) }
      placeLance(w)
      if let s = w.prop(lance) { s.glow = vec4(gold, 0.8 * (1 - u)) }
      if u >= 1 { next() }
    case 4:  // the lance fades away
      let u = clamp(pt / 0.5, 0, 1)
      placeLance(w)
      if let s = w.prop(lance) {
        s.dissolve = easeIn(u)
        if u >= 1 { s.alive = false }
      }
      if u >= 1 { next() }
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
// Bishop: judgment beam. A pillar of light from above dissolves the victim.
// ---------------------------------------------------------------------------
final class BishopBeam: BattleBase {
  private let gold = vec3(1.0, 0.86, 0.55)
  private var crackleT: Float = 0
  private func head(_ w: World) -> vec3 {
    let a = w.actors[plan.mover]
    return a.pos + vec3(0, a.height() * 0.97, 0)
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let sky = V + vec3(0, 3.2, 0)
    switch phase {
    case 0:
      shotHeight = 1.1
      shotExtra = 1.2
      if faceOff(w) { next() }
    case 1:  // prayer: light gathers above the victim
      if enter() { w.sound(.charge, 0.9, a.pos, 0.85) }
      let u = clamp(pt / 1.1, 0, 1)
      a.poseBend = vec2(0, 0.28 * sin(u * kPI))
      a.glow = vec4(gold, 1.2 * u)
      a.idleWeight = 0
      w.frameLights.append(PointLight(head(w), 2.5, gold * (2 + 8 * u)))
      w.frameLights.append(PointLight(sky, 5.0, gold * (2 + 18 * u)))
      w.fx.swirl(sky, 0.5 * (1.2 - u), gold * 3, w.time)
      stream(w, sky, victimCenter(w), gold * 2, 1, 1.4)
      v.tremble = 0.4 + 0.6 * u
      v.glow = vec4(gold, 0.3 * u)
      if u >= 1 {
        w.sound(.zap, 1.0, V, 0.7)
        w.sound(.explode, 0.5, V, 1.6)
        w.fx.flash(V + vec3(0, v.height(), 0), 1.0, gold * 6, 0.15)
        w.groundRing(V, gold * 1.4, 2.5, 0.8, 0.08)
        w.addFlash(gold * 0.12)
        w.shake(0.25)
        v.dissolveColor = gold
        next()
      }
    case 2:  // the beam
      let u = clamp(pt / 1.2, 0, 1)
      let base = V + vec3(0, 0.02, 0)
      w.fx.bolt(sky + vec3(0, 2.0, 0), base, gold * 10, 0.015, 10)
      for i in 0..<10 {
        w.fx.flash(base + vec3(0, Float(i) * 0.5 + w.fx.rnd(0, 0.5), 0), 0.2, gold * 1.3, 0.05)
      }
      w.frameLights.append(PointLight(victimCenter(w), 4.0, gold * 30))
      w.frameLights.append(PointLight(sky, 5.0, gold * 12))
      if w.fx.rnd() < 0.6 { w.fx.magicBurst(base + w.fx.rndDir() * 0.2, gold * 3, 3, 1.2) }
      crackleT -= dt
      if crackleT <= 0 {
        crackleT = 0.15
        w.sound(.crackle, 0.5, V, w.fx.rnd(1.2, 1.5))
      }
      a.poseBend = vec2(0, -0.15)
      a.glow = vec4(gold, 1.2)
      v.dissolve = easeIn(u)
      v.glow = vec4(gold, 0.6 + u)
      v.pos.y = 0.3 * easeIn(u)
      v.tremble = 1.2 * (1 - u)
      v.idleWeight = 0
      if u >= 1 {
        let c = victimCenter(w)
        v.alive = false
        v.visible = false
        w.fx.magicBurst(c, gold * 5, 80, 2.0)
        w.fx.flash(c, 1.2, gold * 6, 0.2)
        w.light(c, gold * 30, 4.0, 0.5)
        w.sound(.chime, 1.0, c, 1.4)
        next()
      }
    case 3:  // afterglow
      let u = clamp(pt / 0.6, 0, 1)
      a.glow = vec4(gold, 1.2 * (1 - u))
      a.poseBend = vec2(0, -0.15 * (1 - u))
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

// ---------------------------------------------------------------------------
// Bishop: seraph orbs. Five orbs of light circle the victim, then strike as one.
// ---------------------------------------------------------------------------
final class BishopOrbs: BattleBase {
  private static let kOrbs = 5
  private let gold = vec3(1.0, 0.82, 0.48)
  private var spinA: Float = 0, radius: Float = 0.75, crackleT: Float = 0
  private var orbsOut = 0  // orbs released so far (C++ `released`, shadowing BattleBase's flag)
  private func head(_ w: World) -> vec3 {
    let a = w.actors[plan.mover]
    return a.pos + vec3(0, a.height() * 0.97, 0) + a.forward() * 0.05
  }
  private func orbit(_ w: World, _ i: Int) -> vec3 {
    let ang = spinA + Float(i) * 2 * kPI / Float(Self.kOrbs)
    return victimCenter(w) + vec3(cos(ang) * radius, 0.12 * sin(ang * 2 + w.time * 3), sin(ang) * radius)
  }
  private func releaseAt(_ i: Int) -> Float { 0.25 + 0.15 * Float(i) }
  private func drawOrb(_ w: World, _ p: vec3) {
    w.fx.flash(p, 0.16, gold * 3, 0.04)
    w.fx.flash(p, 0.06, vec3(6.0, 5.5, 4.5), 0.04)
    w.fx.magicTrail(p, vec3(0), gold * 2.5)
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotHeight = 0.8
      if faceOff(w) { next() }
    case 1:  // rise and release the orbs one by one
      if enter() { w.sound(.charge, 0.8, a.pos, 1.1) }
      a.pos.y = 0.25 * easeInOut(pt / 0.5) + 0.02 * sin(w.time * 6)
      a.glow = vec4(gold, min(1.2, pt * 2))
      a.idleWeight = 0
      spinA += dt * 2
      while orbsOut < Self.kOrbs && pt >= releaseAt(orbsOut) {
        w.fx.magicBurst(head(w), gold * 3, 10, 1.2)
        w.sound(.zap, 0.45, head(w), 1.5 + 0.12 * Float(orbsOut))
        orbsOut += 1
      }
      for i in 0..<orbsOut {
        let ui = clamp((pt - releaseAt(i)) / 0.45, 0, 1)
        let p = mix(head(w), orbit(w, i), easeInOut(ui)) + vec3(0, 0.4 * sin(ui * kPI), 0)
        drawOrb(w, p)
      }
      w.frameLights.append(PointLight(victimCenter(w), 3.0, gold * (2 * Float(orbsOut))))
      v.tremble = 0.6
      if pt >= releaseAt(Self.kOrbs - 1) + 0.8 { next() }
    case 2:  // tighten
      let u = clamp(pt / 0.6, 0, 1)
      spinA += dt * (2 + 9 * u)
      radius = mix(0.75, 0.45, u)
      for i in 0..<Self.kOrbs { drawOrb(w, orbit(w, i)) }
      w.frameLights.append(PointLight(victimCenter(w), 3.0, gold * (10 + 10 * u)))
      crackleT -= dt
      if crackleT <= 0 {
        crackleT = 0.18
        w.sound(.crackle, 0.4, V, w.fx.rnd(1.3, 1.6))
      }
      v.crackColor = vec3(1.0, 0.86, 0.55)
      v.crack = 0.3 * u
      v.tremble = 0.8 + 0.6 * u
      if u >= 1 { next() }
    case 3:  // converge
      let u = clamp(pt / 0.16, 0, 1)
      spinA += dt * 11
      radius = 0.45 * (1 - easeIn(u))
      for i in 0..<Self.kOrbs { drawOrb(w, orbit(w, i)) }
      if u >= 1 {
        let c = victimCenter(w)
        v.crack = 1
        w.shatter(v, c, vec3(0), 2.8, 1.2, 9)
        w.fx.flash(c, 1.5, gold * 8, 0.25)
        w.fx.magicBurst(c, gold * 5, 100, 3.0)
        w.fx.ring(c, 0.3, gold * 3, 0.4)
        w.groundRing(V, gold * 1.6, 3.0, 0.7, 0.08)
        w.light(c, gold * 40, 5.0, 0.5)
        w.addFlash(gold * 0.15)
        w.sound(.explode, 0.6, c, 1.5)
        w.shake(0.4)
        w.slowmo(0.3, 0.4)
        next()
      }
    case 4:  // descend
      let u = clamp(pt / 0.55, 0, 1)
      a.pos.y = 0.25 * (1 - easeInOut(u))
      a.glow = vec4(gold, 1.2 * (1 - u))
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
