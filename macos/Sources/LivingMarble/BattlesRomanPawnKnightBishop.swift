// Roman battles of the legionary (pilum, shield and gladius, vault), the chariot
// (run down, scythed wheels, the charioteer's leap) and the centurion (duel,
// leaping strike, thrown knife). The toolkit (RBattle and friends) is in
// BattlesRoman.swift.
import Foundation
import simd

// ===========================================================================
// PAWN - the legionary
// ===========================================================================
/// Pilum: he hurls it from afar; it pierces shield and all, and he draws his gladius.
final class RPawnPilum: RBattle {
  private var pilum = RMissile()
  private var stuck = RStuck()

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    if stuck.puppet >= 0 { stuck.update(w, fall.corpse >= 0 ? fall.corpse : tumble.corpse) }
    switch phase {
    case 0:
      shotExtra = 0.7
      if faceOffR(w) { next() }
    case 1:  // draw back, take aim
      let u = easeInOut(pt / 0.6)
      a.ctl.armR = vec3(2.3 * u, 0.15 * u, -0.6 * u)
      a.ctl.itemPitch = -0.55 * u
      a.ctl.twist = 0.5 * u
      a.ctl.lean = -0.12 * u
      a.ctl.armL = vec3(0.5 * u, 0, 0)
      a.ctl.gait = kPI * 1.5
      a.ctl.stride = 0.4 * u
      if rIsHuman(v) { rVictimGuard(v, rSeg(pt, 0.2, 0.7)) }
      brace(w, 0.7)
      if pt > 0.7 { next() }
    case 2:  // and throw
      let u = easeIn(pt / 0.14)
      a.ctl.armR = vec3(mix(2.3, 1.4, u), 0.15, -0.6 * (1 - u))
      a.ctl.twist = mix(0.5, -0.3, u)
      a.ctl.lean = mix(-0.12, 0.25, u)
      a.ctl.gait = mix(kPI * 1.5, kPI * 0.5, u)
      if pt >= 0.14 {
        a.ctl.itemR = 0
        let from: vec3 = bonePoint(a, RH.itemR, Roman.handR(Kind.pawn)) + vec3(0, 0.5, 0) + dir * 0.1
        let T: Float = 0.3 + 0.07 * L
        pilum.launch(w, RAsset.pilum, a.color, from, rChestOf(v), T, 0.95)
        w.sound(.whoosh, 0.9, from, 1.2)
        w.sound(.grunt, 0.7, a.pos, w.fx.rnd(0.95, 1.1))
        next()
      }
    case 3:  // in flight
      let u = easeOut(pt / 0.4)
      a.ctl.armR = vec3(mix(1.4, 0.4, u), 0.15 * (1 - u), 0)
      a.ctl.twist = -0.3 * (1 - u)
      a.ctl.lean = 0.25 * (1 - u)
      a.ctl.armL = vec3(0.5 * (1 - u), 0, 0)
      a.ctl.stride = 0.4 * (1 - u)
      brace(w, 1.0)
      if rIsHuman(v) { rVictimGuard(v, 1.0) }
      if pilum.update(w, dt) {
        let c = rChestOf(v)
        let hd = pilum.heading()
        // it goes in deep, and stays there
        let butt: vec3 = pilum.tip() + hd * 0.09 - hd * 0.95
        let M = rPropMatrix(butt, hd)
        if let p = w.puppet(pilum.puppet) { rSetPuppetMatrix(p, M) }
        stuck.attach(w, pilum.puppet, v, M)
        if rIsHuman(v) && v.kind == Kind.pawn { w.sound(.shield, 0.7, c, 1.2) }
        rWound(w, c, flat(hd) + vec3(0, 0.2, 0), rIsTower(v) ? 0.0 : 1.0)
        kill(w, 1.0, flat(hd) * 0.7, 0.45)
        w.shake(0.25)
        w.slowmo(0.35, 0.45)
        next()
      }
    case 4:  // he draws his gladius while his enemy goes down
      let u = rSeg(pt, 0.2, 0.9)
      let b = rBump(u, 0.0, 0.6), s = rSeg(u, 0.5, 1.0)
      a.ctl.armR = vec3(0.35 * b + 0.45 * s, -0.25 * b, 0.45 * u)
      a.ctl.itemPitch = 0.6 * s
      if u > 0.45 && a.ctl.draw < 0.5 {
        a.ctl.draw = 1
        a.ctl.itemR = 1
        w.sound(.draw, 0.7, a.pos)
      }
      let down = victimDown(w, dt)
      if down && pt > 1.0 { next() }
    case 5:  // "Roma!"
      if enter() { w.sound(.shout, 0.6, a.pos, w.fx.rnd(0.95, 1.1)) }
      triumph(w, pt / 0.7)
      if pt > 0.75 { next() }
    case 6:
      if first {
        a.ctl.armR = vec3(0.45, 0.1, 0.45)
        a.ctl.itemPitch = 0.6
        a.ctl.headPitch = 0
      }
      if moveInR(w, dt) { next() }
    case 7:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// Shield and gladius: he takes the blow on his scutum, punches with the boss and
/// stabs underneath it - as the legions were drilled.
final class RPawnShield: RBattle {
  private var blocked = false, punched = false, stabbed = false
  private var start = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOffR(w) { next() }
    case 1:  // draw the gladius, shield up
      let u = rSeg(pt, 0.0, 0.6)
      let b = rBump(u, 0.0, 0.6), s = rSeg(u, 0.5, 1.0)
      a.ctl.armR = vec3(0.35 * b + 0.45 * s, -0.25 * b, 0.45 * u)
      if u > 0.4 && a.ctl.draw < 0.5 {
        a.ctl.draw = 1
        w.sound(.draw, 0.8, a.pos)
      }
      a.ctl.armL = vec3(0.55, 0.05, -0.15) * u
      a.ctl.itemPitch = 0.6 * s
      a.ctl.crouch = 0.15 * u
      rVictimGuard(v, u)
      brace(w, 0.5)
      if pt > 0.7 { next() }
    case 2:  // close in
      if enter() {
        let stop: vec3 = V - dir * (touchDist(a, v) * 1.35 + 0.08)
        walker = RWalker(plan.mover, stop, .foot, max(0.9, L / 2.0))
        w.sound(.shout, 0.55, a.pos, w.fx.rnd(1.0, 1.15))
      }
      rGuard(&a.ctl, 1.0, true)
      rVictimGuard(v, 1.0)
      if walker.update(w, dt) { next() }
    case 3:  // the victim strikes; the blow rings off the scutum
      let u = rSeg(pt, 0.0, 0.85)
      rGuard(&a.ctl, 1.0, true)
      rBlock(&a.ctl, rBump(u, 0.25, 0.8))
      if !rIsTower(v) { rVictimStrike(v, u) }
      if !blocked && u > 0.55 {
        blocked = true
        let s: vec3 = rHandOf(a, false) + dir * 0.05 + vec3(0, 0.08, 0)
        if !rIsTower(v) {
          w.sound(.shield, 1.0, s, w.fx.rnd(0.9, 1.05))
          rSparksAt(w, s, -dir + vec3(0, 0.6, 0), 14)
          rSplinters(w, s, -dir, 10)
          w.shake(0.12)
        }
      }
      if pt > 0.95 { next() }
    case 4:  // a punch with the boss: the victim reels
      if enter() { start = flat(a.pos) }
      let u = rSeg(pt, 0.0, 0.45)
      a.pos = start + dir * 0.1 * sin(u * kPI * 0.5)
      let k = rBump(u, 0.0, 1.0)
      a.ctl.armL = mix(vec3(0.55, 0.05, -0.15), vec3(1.1, -0.1, -0.6), k)
      a.ctl.lean = 0.06 + 0.2 * k
      if !rIsTower(v) { rVictimGuard(v, 1.0 - 0.6 * u) }
      if !punched && u > 0.45 {
        punched = true
        let s: vec3 = rHandOf(a, false) + dir * 0.08
        w.sound(.shield, 0.9, s, 0.8)
        w.sound(.grunt, 0.8, a.pos, w.fx.rnd(0.9, 1.05))
        w.shake(0.15)
        if rIsHuman(v) { w.sound(.grunt, 0.6, v.pos, 0.8) }
      }
      if punched {
        v.pos += dir * dt * 0.35 * (1.0 - rSeg(pt, 0.2, 0.6))
        v.ctl.lean = approach(v.ctl.lean, -0.2, dt * 2.0)
        v.ctl.headPitch = approach(v.ctl.headPitch, -0.25, dt * 2.0)
      }
      if pt > 0.6 { next() }
    case 5:  // and the stab, low under the shield
      let u = rSeg(pt, 0.0, 0.75)
      rThrust(&a.ctl, u)
      a.ctl.armL = vec3(0.55, 0.05, -0.15)
      if !stabbed && u > 0.52 {
        stabbed = true
        let c = rChestOf(v) - vec3(0, 0.1, 0)
        rWound(w, c, dir + vec3(0, 0.1, 0), rIsTower(v) ? 0.2 : 1.0)
        kill(w, 1.0, dir * 0.45, 0.5)
        w.shake(0.25)
        w.slowmo(0.3, 0.45)
      }
      if stabbed { victimDown(w, dt) }
      if pt > 0.8 { next() }
    case 6:  // he steps back as the body falls
      rGuard(&a.ctl, 1.0, true)
      a.ctl.lean = 0.06
      if victimDown(w, dt) && pt > 0.3 { next() }
    case 7:
      if enter() { w.sound(.shout, 0.6, a.pos, w.fx.rnd(0.9, 1.05)) }
      triumph(w, pt / 0.7)
      if pt > 0.75 { next() }
    case 8:
      if first { rGuard(&a.ctl, 1.0, true) }
      if moveInR(w, dt) { next() }
    case 9:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// Vault: he runs, plants his pilum and pole-vaults over the victim with a
/// somersault, lands behind it, spins round and stabs it in the back.
final class RPawnVault: RBattle {
  private var flip = RFlip()
  private var dropped = -1
  private var plant = vec3(0), land = vec3(0)
  private var dropAngle: Float = 0
  private var stabbed = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    if dropped >= 0, let p = w.puppet(dropped) {  // the pilum he let go of topples over
      dropAngle = min(1.45, dropAngle + dt * (0.6 + dropAngle * 4.0))
      p.pos = plant
      p.yaw = faceAngle(dir)
      p.tilt = angleAxis(dropAngle, vec3(1, 0, 0))
    }
    switch phase {
    case 0:
      shotExtra = 1.0
      shotHeight = 0.75
      if faceOffR(w) { next() }
    case 1:  // the run-up, the pilum lowered
      if enter() {
        let stop: vec3 = V - dir * min(L - 0.05, 0.78)
        walker = RWalker(plan.mover, stop, .foot, max(1.7, L / 2.0))
        w.sound(.shout, 0.7, a.pos, w.fx.rnd(0.95, 1.1))
      }
      a.ctl.itemPitch = approach(a.ctl.itemPitch, 0.9, dt * 2.0)
      a.ctl.armR = vec3(0.3, 0, 0.1)
      rVictimGuard(v, rSeg(pt, 0.0, 0.5))
      brace(w, 0.8)
      if walker.update(w, dt) || walker.s > walker.len - 0.05 { next() }
    case 2:  // plant the pilum and vault
      if enter() {
        plant = flat(a.pos) + dir * 0.32
        land = V + dir * (v.radius() + 0.32)
        flip.from = a.pos
        flip.to = land
        flip.height = max(0.75, v.height() * 0.85 + 0.25)
        flip.dur = 0.95 + 0.1 * flip.height
        flip.turns = 1.0
        w.sound(.grunt, 0.8, a.pos, 1.05)
        w.sound(.whoosh, 0.8, a.pos, 0.8)
      }
      let landed = flip.update(a, dt)
      if dropped < 0 && flip.t > 0.22 * flip.dur {  // let go of the pilum at the top of the swing
        a.ctl.itemR = 0
        dropped = w.addPuppet(RAsset.pilum, a.color, 3)
        if let p = w.puppet(dropped) {
          p.life = 2.5
          p.fadeRate = 1.0
        }
        dropAngle = 0.15
      }
      v.ctl.headPitch = -0.6 * rBump(flip.t, 0.0, flip.dur)  // looking up as he flies over
      brace(w, 1.0)
      if landed {
        a.tilt = quat.identity
        a.ctl.tuck = 0
        a.pos = land
        w.sound(.thud, 0.7, land, 1.2)
        w.sound(.armor, 0.7, land, 1.0)
        w.fx.dust(land, 10, 0.25, 0.8)
        next()
      }
    case 3:  // land, spin round, draw
      let u = rSeg(pt, 0.0, 0.4)
      if enter() {
        a.yawTarget = faceAngle(V - a.pos)
        a.yawRate = 14.0
      }
      a.ctl.crouch = 0.5 * (1.0 - u)
      a.ctl.cape = 0.9 * (1.0 - u)
      if u > 0.3 && a.ctl.draw < 0.5 {
        a.ctl.draw = 1
        a.ctl.itemR = 1
        w.sound(.draw, 0.8, a.pos)
      }
      rGuard(&a.ctl, u, true)
      v.ctl.headPitch = -0.6 * (1.0 - u)
      if pt > 0.42 { next() }
    case 4:  // the stab in the back
      let u = rSeg(pt, 0.0, 0.7)
      a.yawRate = 14.0
      rThrust(&a.ctl, u)
      v.ctl.twist = -0.4 * rSeg(pt, 0.0, 0.4)  // too late to turn round
      if !stabbed && u > 0.52 {
        stabbed = true
        let c = rChestOf(v)
        let d = flatDir(a.pos, V)
        rWound(w, c, d + vec3(0, 0.2, 0), rIsTower(v) ? 0.2 : 1.1)
        kill(w, -1.0, d * 0.4, 0.45)
        w.shake(0.25)
        w.slowmo(0.3, 0.45)
      }
      if stabbed { victimDown(w, dt) }
      if pt > 0.8 { next() }
    case 5:
      rGuard(&a.ctl, 1.0, true)
      if victimDown(w, dt) && pt > 0.3 { next() }
    case 6:
      if enter() { w.sound(.shout, 0.6, a.pos, w.fx.rnd(0.95, 1.1)) }
      triumph(w, pt / 0.7)
      if pt > 0.75 { next() }
    case 7:
      if first {
        rGuard(&a.ctl, 1.0, true)
        a.yawRate = 9.0  // back to his usual pace of turning
      }
      if moveInR(w, dt) { next() }
    case 8:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// KNIGHT - the chariot
// ===========================================================================
/// A point kept on the board (within m of its middle along x and z).
func rClampBoard(_ p: vec3, _ m: Float = 3.35) -> vec3 {
  var p = p
  p.x = clamp(p.x, -m, m)
  p.z = clamp(p.z, -m, m)
  return p
}

/// Run down: a crack of the whip, a charge, and the victim goes under the hooves
/// and the wheels; the chariot thunders on, wheels round and comes back.
final class RChariotRunDown: RBattle {
  private var hit = false
  private var end = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.3
      shotHeight = 0.45
      if faceOffR(w) { next() }
    case 1:  // the whip cracks, the horses rear
      if enter() {
        w.sound(.whip, 0.9, a.pos, 1.0)
        w.sound(.neigh, 0.8, a.pos, 1.05)
      }
      let u = rSeg(pt, 0.0, 0.8)
      a.ctl.rear = 0.45 * sin(u * kPI)
      a.ctl.armR = vec3(1.6 * rBump(u, 0.0, 0.6), 0.3, 0.6)
      a.ctl.cape = 0.3
      rVictimGuard(v, u)
      brace(w, 0.9)
      if pt > 0.85 { next() }
    case 2:  // the charge
      if enter() {
        end = rClampBoard(V + dir * 1.0)
        walker = RWalker(plan.mover, end, .chariot, 2.7)
        walker.run = 1.0
        walker.face = false
        w.sound(.wheels, 1.0, a.pos, 1.0)
        w.sound(.shout, 0.5, a.pos, 1.15)
      }
      walker.update(w, dt)
      a.ctl.lean = 0.2
      a.ctl.armR = vec3(0.4, 0.2, 0.5)
      brace(w, 1.0)
      let front: vec3 = a.pos + a.forward() * 0.42
      if !hit && dot(flat(front - V), dir) > -v.radius() * 0.8 {
        hit = true
        let side = vec3(-dir.z, 0, dir.x)
        let sgn: Float = w.fx.rnd() < 0.5 ? -1.0 : 1.0
        let c = rChestOf(v)
        w.sound(.thud, 1.0, c, 0.8)
        w.sound(.neigh, 0.9, c, 0.95)
        if !rIsTower(v) {
          let spray: vec3 = dir + side * sgn + vec3(0, 0.5, 0)
          rBloodSpray(w, c, spray, 60, 3.5)
          w.sound(.blood, 0.8, c)
        }
        let fly: vec3 = dir * 1.4 + side * sgn * 1.6 + vec3(0, 3.4, 0)
        knockFlying(w, fly, 9.0)
        w.shake(0.45)
        w.slowmo(0.3, 0.5)
      }
      if hit { victimDown(w, dt) }
      if walker.s >= walker.len - 1e-3 { next() }
    case 3:  // rein in and turn
      if enter() { a.yawTarget = faceAngle(V - end) }
      a.ctl.lean = approach(a.ctl.lean, 0.0, dt)
      a.ctl.gait += dt * 5.0
      a.ctl.stride = approach(a.ctl.stride, 0.3, dt * 2.0)
      victimDown(w, dt)
      if pt > 0.7 { next() }
    case 4:  // and back at a canter, over the blood
      if enter() {
        walker = RWalker(plan.mover, V, .chariot, 1.3)
        walker.run = 0.4
      }
      victimDown(w, dt)
      if walker.update(w, dt) && victimDown(w, dt) { next() }
    case 5:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// Scythed wheels: the chariot races past the victim close enough for the blades
/// on its axle to cut it down.
final class RChariotScythes: RBattle {
  private var hit = false
  private var end = vec3(0), side = vec3(0)
  private var sgn: Float = 1

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.3
      shotHeight = 0.4
      if faceOffR(w) { next() }
    case 1:  // the whip, and off
      if enter() {
        side = vec3(-dir.z, 0, dir.x)
        // pass the victim on the side with room to spare
        let ahead: vec3 = V + dir * 1.0
        let e1: vec3 = ahead + side * 0.33, e2: vec3 = ahead - side * 0.33
        sgn = length(rClampBoard(e1) - e1) <= length(rClampBoard(e2) - e2) ? 1.0 : -1.0
        end = rClampBoard(ahead + side * 0.33 * sgn)
        w.sound(.whip, 0.9, a.pos, 1.1)
        w.sound(.neigh, 0.6, a.pos, 1.1)
      }
      a.ctl.armR = vec3(1.6 * rBump(pt, 0.0, 0.4), 0.3, 0.6)
      rVictimGuard(v, rSeg(pt, 0.0, 0.5))
      brace(w, 0.9)
      if pt > 0.5 { next() }
    case 2:  // racing past: the scythes cut
      if enter() {
        walker = RWalker(plan.mover, end, .chariot, 2.6)
        walker.run = 1.0
        w.sound(.wheels, 1.0, a.pos, 1.05)
      }
      walker.update(w, dt)
      a.ctl.lean = 0.2
      // the blade on the victim's side of the axle
      let blade = rModelPoint(a, vec3(sgn * -0.29, 0.14, -0.26))
      if !hit && dot(flat(blade - V), a.forward()) > 0.0 {
        hit = true
        let hitAt: vec3 = V + vec3(0, min(0.32, v.height() * 0.35), 0)
        let fwd = a.forward()
        w.sound(.slash, 1.0, hitAt, 0.85)
        if rIsTower(v) {
          rSplinters(w, hitAt, fwd, 30)
          kill(w, 1.0, fwd * 0.5, 0.0)
        } else {
          let cut: vec3 = fwd * 0.6 + side * -sgn * 0.6
          rWound(w, hitAt, cut, 1.3, true)
          rSparksAt(w, blade, -fwd, 10)
          v.yawRate = 7.0
          v.yawTarget = v.yaw + kPI * 1.3 * sgn  // spun round by the blow
          kill(w, 1.0, fwd * 0.6, 0.5)
        }
        w.shake(0.35)
        w.slowmo(0.3, 0.5)
      }
      if hit { victimDown(w, dt) }
      if walker.s >= walker.len - 1e-3 { next() }
    case 3:
      if enter() { a.yawTarget = faceAngle(V - end) }
      a.ctl.lean = approach(a.ctl.lean, 0.0, dt)
      a.ctl.gait += dt * 5.0
      a.ctl.stride = approach(a.ctl.stride, 0.3, dt * 2.0)
      victimDown(w, dt)
      if pt > 0.7 { next() }
    case 4:
      if enter() {
        walker = RWalker(plan.mover, V, .chariot, 1.3)
        walker.run = 0.4
        triumph(w, 1.0)
      }
      victimDown(w, dt)
      if walker.update(w, dt) && victimDown(w, dt) { next() }
    case 5:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// The charioteer's leap: the chariot pulls up short, the horses rear, and he
/// somersaults over their heads onto the victim with his spear - then flips back
/// into his car.
final class RChariotLeap: RBattle {
  private var target = vec3(0)  // where he lands, as an offset of the charioteer in the chariot's frame
  private var struck = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.0
      shotHeight = 0.6
      if faceOffR(w) { next() }
    case 1:  // gallop up and pull up short
      if enter() {
        let stop: vec3 = V - dir * min(L, 1.02)
        walker = RWalker(plan.mover, stop, .chariot, max(1.8, L / 1.4))
        walker.run = 0.8
        w.sound(.whip, 0.7, a.pos)
      }
      rVictimGuard(v, rSeg(pt, 0.0, 0.8))
      brace(w, 0.8)
      if walker.update(w, dt) { next() }
    case 2:  // the horses rear; he crouches
      if enter() {
        w.sound(.neigh, 1.0, a.pos, 1.0)
        a.yawTarget = faceAngle(V - a.pos)
        // where to land: just short of the victim, on the ground
        let toLocal = angleAxis(a.yaw, vec3(0, 1, 0)).inverse
        let off: vec3 = V - dir * (v.radius() + 0.12) - a.pos
        let local = toLocal * off
        let rest = vec3(0, 0.417, -0.24)
        target = vec3(local.x - rest.x, 0.267 - rest.y, local.z - rest.z)
      }
      let u = rSeg(pt, 0.0, 0.5)
      a.ctl.rear = 0.7 * rBump(pt, 0.0, 0.9)
      a.ctl.rider = vec3(0, -0.05 * u, 0)
      a.ctl.lean = 0.3 * u
      a.ctl.armR = vec3(0.9, 0.1, 0.6) * u
      rVictimGuard(v, 1.0)
      if pt > 0.5 {
        w.sound(.shout, 0.8, a.pos, 1.15)
        w.sound(.whoosh, 0.7, a.pos, 0.75)
        next()
      }
    case 3:  // the somersault over the horses
      let dur: Float = 0.85
      let u = clamp(pt / dur, 0.0, 1.0)
      a.ctl.rear = 0.7 * rBump(pt + 0.5, 0.0, 0.9)
      var r = target * u
      r.y = mix(-0.05, target.y, u) + 4.0 * u * (1.0 - u) * 0.62
      a.ctl.rider = r
      a.ctl.riderSpin = kTau * smoothstep(0.1, 0.9, u)
      a.ctl.cape = 1.0
      a.ctl.lean = 0.3 * (1.0 - u)
      a.ctl.armR = vec3(0.9 + 0.6 * u, 0.1, 0.6)
      a.ctl.itemPitch = 1.4 * u
      v.ctl.headPitch = -0.5 * sin(u * kPI)
      brace(w, 1.0)
      if u >= 1.0 {
        w.sound(.thud, 0.6, V - dir * 0.3, 1.3)
        w.fx.dust(V - dir * (v.radius() + 0.12), 8, 0.2, 0.7)
        next()
      }
    case 4:  // and the spear goes in
      let u = rSeg(pt, 0.0, 0.5)
      let keys: [RKey] = [
        rK(0.0, vec3(1.5, 0.1, 0.6), vec3(0), 0.0, 0, 0, 1.4),
        rK(0.4, vec3(0.5, 0.1, 1.2), vec3(0), -0.1, -0.2, 0, 1.5),
        rK(0.6, vec3(1.3, 0.0, -0.4), vec3(0), 0.35, 0.2, 0, 1.55),
        rK(1.0, vec3(1.0, 0.0, 0.2), vec3(0), 0.1, 0, 0, 1.4),
      ]
      rPlay(&a.ctl, keys, u)
      a.ctl.cape = approach(a.ctl.cape, 0.0, dt * 2.0)
      if !struck && u > 0.55 {
        struck = true
        let c = rChestOf(v)
        rWound(w, c, dir + vec3(0, 0.2, 0), rIsTower(v) ? 0.2 : 1.1)
        kill(w, 1.0, dir * 0.45, 0.45)
        w.shake(0.25)
        w.slowmo(0.3, 0.45)
      }
      if struck { victimDown(w, dt) }
      if pt > 0.65 { next() }
    case 5:  // a back somersault, home into the car
      let dur: Float = 0.8
      let u = clamp(pt / dur, 0.0, 1.0)
      if enter() { w.sound(.whoosh, 0.6, a.pos, 0.8) }
      var r = target * (1.0 - u)
      r.y = target.y * (1.0 - u) + 4.0 * u * (1.0 - u) * 0.6
      a.ctl.rider = r
      a.ctl.riderSpin = kTau - kTau * smoothstep(0.1, 0.9, u)
      a.ctl.armR = mix(vec3(1.0, 0.0, 0.2), vec3(0), u)
      a.ctl.itemPitch = 1.4 * (1.0 - u)
      a.ctl.cape = 0.8
      victimDown(w, dt)
      if u >= 1.0 {
        a.ctl.rider = vec3(0)
        a.ctl.riderSpin = 0
        w.sound(.thud, 0.5, a.pos, 1.4)
        next()
      }
    case 6:
      triumph(w, pt / 0.8)
      a.ctl.cape = approach(a.ctl.cape, 0.0, dt * 2.0)
      if victimDown(w, dt) && pt > 0.8 { next() }
    case 7:
      if first { a.ctl.armR = vec3(0) }
      if moveInR(w, dt) { next() }
    case 8:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// BISHOP - the centurion: gladius in his right hand, pugio in his left
// ===========================================================================
/// The clash of two blades: sparks where they meet.
func rClash(_ w: World, _ a: Actor, _ v: Actor, _ loud: Float = 1.0) {
  var p: vec3 = rBladeTipOf(a) * 0.6 + rBladeTipOf(v) * 0.4
  if rIsTower(v) { p = rChestOf(v) }
  let up: vec3 = vec3(0, 1, 0) + flatDir(a.pos, v.pos) * -0.5
  rSparksAt(w, p, up, 26)
  w.sound(.parry, loud, p, w.fx.rnd(0.9, 1.12))
  w.shake(0.1)
}

/// Duel: he parries two blows, spins round with a cut across the chest and
/// finishes it with his dagger.
final class RCenturionDuel: RBattle {
  private var c1 = false, c2 = false, cut = false, stab = false
  private var spin0: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let fights = !rIsTower(v)
    switch phase {
    case 0:
      if faceOffR(w) { next() }
    case 1:  // close in, on guard
      if enter() {
        let stop: vec3 = V - dir * (touchDist(a, v) * 1.5 + 0.06)
        walker = RWalker(plan.mover, stop, .foot, max(0.9, L / 2.0))
      }
      rGuard(&a.ctl, rSeg(pt, 0.0, 0.4), false)
      a.ctl.armL = vec3(0.6, 0.0, 0.2) * rSeg(pt, 0.0, 0.4)
      rVictimGuard(v, rSeg(pt, 0.0, 0.5))
      brace(w, 0.5)
      if walker.update(w, dt) { next() }
    case 2:  // the victim's first blow: parried high
      let u = rSeg(pt, 0.0, 0.85)
      if fights { rVictimStrike(v, u) }
      rParry(&a.ctl, rBump(u, 0.2, 0.85), true)
      if !c1 && u > 0.56 {
        c1 = true
        if fights { rClash(w, a, v) }
      }
      if pt > 0.9 { next() }
    case 3:  // the second, a thrust: swept aside
      let u = rSeg(pt, 0.0, 0.75)
      if fights {
        if rIsHuman(v) {
          rThrust(&v.ctl, u)
        } else {
          rVictimStrike(v, u)
        }
      }
      rParry(&a.ctl, rBump(u, 0.2, 0.85), false)
      a.ctl.itemYaw = -0.8 * rBump(u, 0.3, 0.8)
      if !c2 && u > 0.5 {
        c2 = true
        if fights { rClash(w, a, v, 0.8) }
      }
      if pt > 0.8 { next() }
    case 4:  // he spins round and cuts across the chest
      if enter() {
        spin0 = a.yaw
        w.sound(.grunt, 0.8, a.pos, 0.95)
        w.sound(.whoosh, 0.8, a.pos, 1.0)
      }
      let u = rSeg(pt, 0.0, 0.6)
      a.yaw = spin0 + kTau * smoothstep(0.0, 1.0, u)
      a.yawTarget = a.yaw
      a.ctl.cape = 0.8 * rBump(u, 0.0, 1.0)
      rSweep(&a.ctl, 0.25 + 0.45 * u)
      a.ctl.crouch = 0.3
      if rIsHuman(v) { rVictimGuard(v, 1.0 - u) }
      if !cut && u > 0.85 {
        cut = true
        let c = rChestOf(v) + vec3(0, 0.04, 0)
        let side = vec3(-dir.z, 0, dir.x)
        let along: vec3 = side + dir * 0.3 + vec3(0, 0.3, 0)
        rWound(w, c, along, rIsTower(v) ? 0.2 : 1.0, true)
        v.ctl.lean = -0.2
        v.ctl.twist = 0.4
      }
      if pt > 0.65 {
        a.yawTarget = spin0
        a.yaw = spin0
        next()
      }
    case 5:  // the pugio, up under the ribs
      let u = rSeg(pt, 0.0, 0.5)
      let k = rBump(u, 0.0, 1.4)
      a.ctl.armL = mix(vec3(0.3, 0.1, 0.6), vec3(1.3, -0.15, -0.5), k)
      a.ctl.lean = 0.06 + 0.25 * k
      a.ctl.twist = -0.3 * k
      a.ctl.gait = kPI * 1.5
      a.ctl.stride = 0.5 * k
      if !stab && u > 0.55 {
        stab = true
        let c = rChestOf(v) - vec3(0, 0.05, 0)
        rWound(w, c, dir + vec3(0, 0.15, 0), rIsTower(v) ? 0.2 : 1.0)
        kill(w, 1.0, dir * 0.3, 0.7)
        w.shake(0.25)
        w.slowmo(0.3, 0.5)
      }
      if stab { victimDown(w, dt) }
      if pt > 0.7 { next() }
    case 6:  // a flick of the blade (the blood flies off it)
      let u = rSeg(pt, 0.0, 0.5)
      a.ctl.armL = mix(a.ctl.armL, vec3(0.6, 0.0, 0.2), min(1.0, dt * 6.0))
      a.ctl.lean = approach(a.ctl.lean, 0.0, dt)
      a.ctl.twist = approach(a.ctl.twist, 0.0, dt * 2.0)
      a.ctl.stride = approach(a.ctl.stride, 0.0, dt * 2.0)
      a.ctl.armR = mix(vec3(0.45, 0.1, 0.45), vec3(0.9, -0.4, 0.0), rBump(u, 0.0, 1.0))
      if pt > 0.3 && pt - dt <= 0.3 { rBloodSpray(w, rBladeTipOf(a), -dir + vec3(0, -0.3, 0), 14, 2.0, 0.3) }
      if victimDown(w, dt) && pt > 0.5 { next() }
    case 7:
      if enter() { w.sound(.shout, 0.6, a.pos, 0.95) }
      triumph(w, pt / 0.7)
      if pt > 0.75 { next() }
    case 8:
      if first { rGuard(&a.ctl, 1.0, false) }
      if moveInR(w, dt) { next() }
    case 9:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// Leaping strike: a run, a somersault, and he comes down with his sword
/// driven in from above.
final class RCenturionLeap: RBattle {
  private var flip = RFlip()
  private var struck = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.9
      shotHeight = 0.8
      if faceOffR(w) { next() }
    case 1:  // the run
      if enter() {
        let stop: vec3 = V - dir * min(L - 0.05, 1.15)
        walker = RWalker(plan.mover, stop, .foot, max(1.8, L / 2.0))
        w.sound(.shout, 0.7, a.pos, 0.95)
      }
      rGuard(&a.ctl, 1.0, false)
      rVictimGuard(v, rSeg(pt, 0.0, 0.5))
      brace(w, 0.8)
      if walker.update(w, dt) || walker.s > walker.len - 0.05 { next() }
    case 2:  // crouch to spring
      a.ctl.crouch = 0.45 * rSeg(pt, 0.0, 0.15)
      if pt > 0.16 {
        flip.from = a.pos
        flip.to = V - dir * (v.radius() + 0.1)
        flip.height = max(0.7, v.height() * 0.7 + 0.3)
        flip.dur = 0.95
        flip.turns = 1.0
        w.sound(.grunt, 0.8, a.pos, 1.0)
        w.sound(.whoosh, 0.9, a.pos, 0.7)
        next()
      }
    case 3:  // the somersault, the sword raised in both hands as he comes down
      let landed = flip.update(a, dt)
      let u = clamp(flip.t / flip.dur, 0.0, 1.0)
      a.ctl.crouch = 0
      let raise = rSeg(u, 0.55, 0.85)
      a.ctl.armR = mix(vec3(0.6, 0.1, 0.5), vec3(2.8, -0.1, 0.7), raise)
      a.ctl.armL = mix(vec3(0.6, 0.1, 0.5), vec3(2.7, -0.2, 0.7), raise)
      a.ctl.itemPitch = mix(0.6, -0.2, raise)
      v.ctl.headPitch = -0.5 * sin(u * kPI)
      if rIsHuman(v) { rVictimGuard(v, 1.0) }
      brace(w, 1.0)
      if landed {
        a.tilt = quat.identity
        a.ctl.tuck = 0
        a.pos = flip.to
        w.sound(.thud, 0.8, a.pos, 1.0)
        w.fx.dust(a.pos, 10, 0.25, 0.8)
        next()
      }
    case 4:  // the blade comes down
      let u = rSeg(pt, 0.0, 0.3)
      let e = easeIn(u)
      a.ctl.armR = mix(vec3(2.8, -0.1, 0.7), vec3(0.8, 0.0, -0.3), e)
      a.ctl.armL = mix(vec3(2.7, -0.2, 0.7), vec3(0.7, 0.1, -0.2), e)
      a.ctl.itemPitch = mix(-0.2, 1.9, e)
      a.ctl.crouch = 0.45 * u
      a.ctl.lean = 0.35 * u
      a.ctl.cape = 0.6 * (1.0 - u)
      if !struck && u >= 1.0 {
        struck = true
        let c = rChestOf(v) + vec3(0, 0.06, 0)
        let down: vec3 = vec3(0, 1.2, 0) + dir * 0.4
        rWound(w, c, down, rIsTower(v) ? 0.3 : 1.5)
        kill(w, 1.0, dir * 0.2, 0.25)
        w.shake(0.4)
        w.slowmo(0.3, 0.5)
      }
      if struck { victimDown(w, dt) }
      if pt > 0.55 { next() }
    case 5:  // rise
      a.ctl.crouch = approach(a.ctl.crouch, 0.15, dt)
      a.ctl.lean = approach(a.ctl.lean, 0.06, dt)
      a.ctl.armR = mix(a.ctl.armR, vec3(0.45, 0.1, 0.45), min(1.0, dt * 4.0))
      a.ctl.armL = mix(a.ctl.armL, vec3(0.6, 0.0, 0.2), min(1.0, dt * 4.0))
      a.ctl.itemPitch = approach(a.ctl.itemPitch, 0.6, dt * 3.0)
      if victimDown(w, dt) && pt > 0.4 { next() }
    case 6:
      if enter() { w.sound(.shout, 0.6, a.pos, 0.95) }
      triumph(w, pt / 0.7)
      if pt > 0.75 { next() }
    case 7:
      if moveInR(w, dt) { next() }
    case 8:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// Thrown knife: he flips his pugio up and catches it, then hurls it end over end
/// into the victim's throat, and walks over to pull it out.
final class RCenturionKnife: RBattle {
  private var knife = -1
  private var kp0 = vec3(0), kv = vec3(0), kTarget = vec3(0)
  private var kt: Float = 0, T: Float = 0.3
  private var stuck = RStuck()
  private var flew = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    if stuck.puppet >= 0 { stuck.update(w, fall.corpse) }
    switch phase {
    case 0:
      shotExtra = 0.6
      if faceOffR(w) { next() }
    case 1:  // a flip of the knife into the air, and caught
      let u = rSeg(pt, 0.0, 0.7)
      a.ctl.armL = vec3(0.7 + 0.3 * rBump(u, 0.0, 0.3), 0.0, 0.6)
      if knife < 0 && pt > 0.12 && pt < 0.6 {  // (once: it is caught at 0.62)
        a.ctl.itemL = 0
        knife = w.addPuppet(RAsset.knife, a.color, 3)
        kp0 = rHandOf(a, false)
        w.sound(.whoosh, 0.3, kp0, 1.8)
      }
      if let k = w.puppet(knife) {
        let t: Float = pt - 0.12, T0: Float = 0.5
        let h: Float = 4.0 * (t / T0) * (1.0 - t / T0) * 0.28
        k.pos = rHandOf(a, false) + vec3(0, max(0.0, h), 0)
        k.yaw = a.yaw
        k.tilt = angleAxis(t * 22.0, vec3(1, 0, 0))
        if t >= T0 {
          k.alive = false
          knife = -1
          a.ctl.itemL = 1
          w.sound(.parry, 0.15, a.pos, 2.2)
        }
      }
      rVictimGuard(v, rSeg(pt, 0.2, 0.7))
      brace(w, 0.6)
      if pt > 0.75 && knife < 0 { next() }
    case 2:  // draw back the left arm
      let u = easeInOut(rSeg(pt, 0.0, 0.45))
      a.ctl.armL = mix(vec3(0.7, 0.0, 0.6), vec3(2.3, 0.2, 1.0), u)
      a.ctl.twist = -0.45 * u
      a.ctl.lean = -0.1 * u
      a.ctl.gait = kPI * 0.5
      a.ctl.stride = 0.4 * u
      if pt > 0.5 { next() }
    case 3:  // the throw
      let u = easeIn(rSeg(pt, 0.0, 0.12))
      a.ctl.armL = mix(vec3(2.3, 0.2, 1.0), vec3(1.2, -0.1, -0.3), u)
      a.ctl.twist = mix(-0.45, 0.35, u)
      a.ctl.lean = mix(-0.1, 0.25, u)
      a.ctl.gait = mix(kPI * 0.5, kPI * 1.5, u)
      if u >= 1.0 && !flew {
        flew = true
        a.ctl.itemL = 0
        knife = w.addPuppet(RAsset.knife, a.color, 3)
        kp0 = rHandOf(a, false) + vec3(0, 0.05, 0)
        kTarget = rIsHuman(v) ? rHeadOf(v) - vec3(0, 0.1, 0) * v.scale : rChestOf(v)
        T = 0.22 + 0.05 * L
        kv = (kTarget - kp0) / T + vec3(0, 0.5 * 3.0 * T, 0)
        kt = 0
        w.sound(.grunt, 0.7, a.pos, 1.05)
        next()
      }
    case 4:  // end over end through the air
      kt += dt
      let t = min(kt, T)
      let p: vec3 = kp0 + kv * t + vec3(0, -0.5 * 3.0 * t * t, 0)
      if let k = w.puppet(knife) {
        k.pos = p
        k.yaw = faceAngle(dir)
        k.tilt = angleAxis(kt * 26.0, vec3(1, 0, 0))
        if Int(kt * 12.0) != Int((kt - dt) * 12.0) { w.sound(.whoosh, 0.25, p, 2.4) }
      }
      a.ctl.armL = mix(a.ctl.armL, vec3(0.6, 0.0, 0.2), min(1.0, dt * 3.0))
      a.ctl.twist = approach(a.ctl.twist, 0.0, dt)
      a.ctl.lean = approach(a.ctl.lean, 0.0, dt)
      brace(w, 1.0)
      if kt >= T {
        // it sticks, point first, in the throat
        let M = rPropMatrix(kTarget - dir * 0.05, dir + vec3(0, -0.15, 0))
        if let k = w.puppet(knife) { rSetPuppetMatrix(k, M) }
        stuck.attach(w, knife, v, M)
        rWound(w, kTarget, dir + vec3(0, 0.25, 0), rIsTower(v) ? 0.0 : 0.9)
        kill(w, 1.0, dir * 0.3, 0.8)
        w.slowmo(0.35, 0.45)
        next()
      }
    case 5:  // it clutches its throat and goes down
      if fall.active() && fall.t < fall.stagger && rIsHuman(v) {
        victimDown(w, dt)
        v.ctl.armR = mix(v.ctl.armR, vec3(2.0, -0.5, 1.7), min(1.0, dt * 8.0))
        v.ctl.armL = mix(v.ctl.armL, vec3(2.0, -0.5, 1.7), min(1.0, dt * 8.0))
        if fmod(pt, 0.25) < dt { rBloodSpray(w, kTarget, dir + vec3(0, 0.3, 0), 6, 1.2) }
      } else if victimDown(w, dt) && pt > 1.0 {
        next()
      }
    case 6:  // he walks over and pulls his knife out
      if enter() { walker = RWalker(plan.mover, V - dir * 0.3, .foot, 0.8) }
      if walker.update(w, dt) {
        if let k = w.puppet(knife) {
          rBloodSpray(w, k.pos, vec3(0, 1, 0), 10, 1.0)
          k.alive = false
        }
        stuck.puppet = -1
        a.ctl.itemL = 1
        a.ctl.armL = vec3(0.4, 0.0, 0.6)
        w.sound(.stab, 0.4, a.pos, 1.4)
        next()
      }
    case 7:
      if moveInR(w, dt) { next() }
    case 8:
      return finishR(w)
    default:
      break
    }
    return false
  }
}
