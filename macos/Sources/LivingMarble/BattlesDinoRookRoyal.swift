// Dinosaur battles of the brachiosaurus (rearing stomp, neck swing, tail lash), of the
// spinosaurus (hooked claws, snatch, tail swipe) and of the tyrannosaurus
// (bone-crusher, the roar, head ram and stomp). (As battle_dino.cpp; the toolkit is in
// BattlesDino.swift.)
import Foundation
import simd

// ===========================================================================
// ROOK - the brachiosaurus
// ===========================================================================
/// Rearing stomp: it walks up to the cowering victim, rears up on its hind legs, towering
/// over it, and comes crashing down on it with its forefeet.
final class DBrachioStomp: DBattle {
  private var start = vec3(0)
  private var lungeDist: Float = 0  // (C++ `lunge`)
  private var bellowed = false, crashed = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.1
      shotHeight = 1.0
      if faceOffD(w, 0.6) { next() }
    case 1:  // up to it; the victim turns side-on and cowers, looking up
      if first {
        let d: Float = 0.17 + dHalfWidthOf(v) + 0.22
        walker = DWalker(plan.mover, V - dir * d, max(0.7, L / 2.6))
        let turn: Float = w.fx.rnd() < 0.5 ? 1.0 : -1.0
        v.yawTarget = faceAngle(A - V) + turn * 1.3
        v.yawRate = 3.0
      }
      v.ctl.crouch = 0.3 * dSeg(pt, 0.3, 1.0)
      v.ctl.neck = 0.3 * dSeg(pt, 0.3, 1.0)
      v.ctl.headPitch = -0.35 * dSeg(pt, 0.3, 1.0)
      let look: Float = wrapAngle(v.yawTarget - faceAngle(A - V)) > 0 ? -1.0 : 1.0
      v.ctl.headYaw = 0.5 * dSeg(pt, 0.3, 1.0) * look
      brace(w, 0.8)
      if walker.update(w, dt) { next() }
    case 2:  // rearing up, bellowing; the victim snaps up at it
      a.ctl.rear = easeInOut(dSeg(pt, 0.0, 1.1))
      a.ctl.neck = 0.35 * dSeg(pt, 0.0, 1.0)
      a.ctl.jaw = 0.6 * dBump(pt, 0.3, 1.3)
      if !bellowed && pt > 0.3 {
        bellowed = true
        dCallOf(w, a)
      }
      if pt > 0.4 { dVictimStrike(v, dSeg(pt, 0.4, 1.2)) }
      v.tremble = 1.0
      if pt > 1.35 { next() }
    case 3:  // and down
      let dur: Float = 0.3
      if first {
        start = a.pos
        lungeDist = dHalfWidthOf(v) + 0.2
        w.sound(.whoosh, 0.8, dHeadOf(a), 0.5)
      }
      let u = min(1.0, pt / dur)
      a.ctl.rear = 1.0 - easeIn(u)
      a.pos = start + dir * lungeDist * easeIn(u)
      a.ctl.neck = 0.35 * (1.0 - u) - 0.2 * u
      if !crashed && u >= 1.0 {
        crashed = true
        let f = flat(V)
        w.sound(.quake, 1.0, f, 0.8)
        w.sound(.quake, 0.8, f, 0.95)
        dWound(w, dChestOf(v), vec3(0, 1, 0), 1.2, DWound.crush)
        dFeathers(w, v, dChestOf(v), vec3(0, 1, 0), 16)
        w.groundRing(f, vec3(0.8, 0.7, 0.55), 3.0, 0.6, 0.08)
        w.fx.dust(f, 40, 0.8, 2.0)
        w.shake(0.6)
        w.slowmo(0.3, 0.5)
        fall.bleed = 1.4
        kill(w, 0.0, dir * 0.15, 0.04)
        next()
      }
    case 4:
      a.ctl.neck = approach(a.ctl.neck, 0.0, dt)
      if victimDown(w, dt) && pt > 0.5 { next() }
    case 5:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.4)
      if pt > 1.4 { next() }
    case 6:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 7:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

/// Neck swing: the long neck lowered and swung like a club.
final class DBrachioNeck: DBattle {
  private var swingSide: Float = 1
  private var hit = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.5
      shotHeight = 0.8
      if faceOffD(w) { next() }
    case 1:
      if first { walker = DWalker(plan.mover, V - dir * 0.98, max(0.7, L / 2.6)) }
      dVictimGuard(v, dSeg(pt, 0.0, 0.8))
      brace(w, 0.6)
      if walker.update(w, dt) { next() }
    case 2:  // the wind-up: the neck lowered and drawn back to one side
      if first {
        swingSide = w.fx.rnd() < 0.5 ? 1.0 : -1.0
        dCallOf(w, a, 0.8)
      }
      let u = easeInOut(dSeg(pt, 0.0, 0.9))
      a.ctl.neck = -1.25 * u
      a.ctl.headYaw = -1.35 * swingSide * u
      a.ctl.twist = -0.15 * swingSide * u
      a.ctl.jaw = 0.3 * u
      dVictimGuard(v, 1.0)
      v.ctl.headPitch = -0.2
      brace(w, 0.9)
      if pt > 1.0 { next() }
    case 3:  // the swing
      let dur: Float = 0.38
      if first { w.sound(.whoosh, 0.9, dHeadOf(a), 0.55) }
      let u = min(1.0, pt / dur)
      a.ctl.headYaw = mix(-1.35, 1.15, easeIn(u)) * swingSide
      a.ctl.twist = mix(-0.15, 0.2, u) * swingSide
      a.ctl.neck = -1.25
      let left = vec3(cos(a.yaw), 0, -sin(a.yaw))
      let sweep = left * swingSide  // (+headYaw carries the head to the animal's left)
      if !hit && (u > 0.95 || (u > 0.25 && sweptInto(v, flat(dChestOf(a)), flat(dHeadOf(a)), sweep))) {
        hit = true
        let at = dHeadOf(a)
        w.sound(.thud, 1.0, at, 0.7)
        let struck: vec3 = dChestOf(v) * 0.5 + dNeckOf(v) * 0.5
        dWound(w, struck, sweep + vec3(0, 0.3, 0), 0.7, DWound.crush)
        dFeathers(w, v, at, sweep, 14)
        let fly: vec3 = sweep * 2.4 + vec3(0, 2.0, 0)
        knockFlying(w, fly, 9.0)
        w.shake(0.4)
        w.slowmo(0.3, 0.5)
      }
      if hit { victimDown(w, dt) }
      if u >= 1.0 && pt > dur + 0.15 { next() }
    case 4:  // and up again
      let u = easeInOut(dSeg(pt, 0.0, 1.0))
      a.ctl.headYaw = 1.15 * swingSide * (1.0 - u)
      a.ctl.twist = 0.2 * swingSide * (1.0 - u)
      a.ctl.neck = -1.25 * (1.0 - u)
      if victimDown(w, dt) && pt > 1.0 { next() }
    case 5:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.4)
      if pt > 1.4 { next() }
    case 6:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 7:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

/// Tail lash: it turns its back on the victim and cracks its tail across it like a whip.
final class DBrachioTail: DBattle {
  private var swingSide: Float = 1
  private var hit = false, cracked = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.6
      shotHeight = 0.6
      if faceOffD(w) { next() }
    case 1:
      if first { walker = DWalker(plan.mover, V - dir * 0.78, max(0.7, L / 2.6)) }
      dVictimGuard(v, dSeg(pt, 0.0, 0.8))
      brace(w, 0.6)
      if walker.update(w, dt) { next() }
    case 2:  // it turns its back
      if first {
        swingSide = w.fx.rnd() < 0.5 ? 1.0 : -1.0
        a.yawTarget = faceAngle(-dir) + 0.25 * swingSide
        a.yawRate = 2.6
        dCallOf(w, a, 0.7)
      }
      a.ctl.gait += dt * 3.0
      a.ctl.stride = 0.3 * (1.0 - dSeg(pt, 0.9, 1.3))
      a.ctl.headYaw = 0.8 * swingSide * dSeg(pt, 0.4, 1.2)  // looking back over its shoulder
      dVictimGuard(v, 1.0)
      brace(w, 0.8)
      if pt > 1.35 { next() }
    case 3:  // the tail drawn round to one side
      let u = easeInOut(dSeg(pt, 0.0, 0.6))
      a.ctl.itemYaw = 1.15 * swingSide * u
      a.ctl.itemPitch = 0.3 * u
      a.ctl.twist = -0.15 * swingSide * u
      a.ctl.stride = 0
      if pt > 0.65 { next() }
    case 4:  // the lash
      let dur: Float = 0.3
      if !cracked {
        cracked = true
        w.sound(.tailwhip, 1.0, dTailOf(a), 1.0)
      }
      let u = min(1.0, pt / dur)
      a.ctl.itemYaw = mix(1.15, -1.25, easeIn(u)) * swingSide
      a.ctl.twist = mix(-0.15, 0.15, u) * swingSide
      // (+itemYaw draws the tail to the animal's right, so the lash carries it across to
      // its left: that is the way the victim goes)
      let left = vec3(cos(a.yaw), 0, -sin(a.yaw))
      let sweep = left * swingSide
      if !hit && (u > 0.95 || (u > 0.3 && sweptInto(v, flat(a.pos), flat(dTailOf(a)), sweep))) {
        hit = true
        dWound(w, dChestOf(v), sweep + vec3(0, 0.4, 0), 0.8, DWound.claw)
        dFeathers(w, v, dChestOf(v), sweep, 14)
        let fly: vec3 = sweep * 2.3 + vec3(0, 1.8, 0)
        knockFlying(w, fly, 8.0)
        w.shake(0.3)
        w.slowmo(0.3, 0.45)
      }
      if hit { victimDown(w, dt) }
      if u >= 1.0 && pt > dur + 0.2 { next() }
    case 5:  // and round again
      if first {
        a.yawTarget = faceAngle(dir)
        a.yawRate = 2.6
      }
      let u = easeInOut(dSeg(pt, 0.0, 0.8))
      a.ctl.itemYaw = -1.25 * swingSide * (1.0 - u)
      a.ctl.itemPitch = 0.3 * (1.0 - u)
      a.ctl.twist = 0.15 * swingSide * (1.0 - u)
      a.ctl.headYaw = approach(a.ctl.headYaw, 0.0, dt * 2.0)
      a.ctl.gait += dt * 3.0
      a.ctl.stride = 0.3 * (1.0 - dSeg(pt, 0.9, 1.3))
      if victimDown(w, dt) && pt > 1.3 { next() }
    case 6:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.4)
      if pt > 1.4 { next() }
    case 7:
      if first { a.yawRate = 9.0 }
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 8:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// QUEEN - the spinosaurus
// ===========================================================================
/// Hooked claws: she rears with her arms raised and roars; she swats the victim's jaws
/// aside and rakes it with her great thumb claws.
final class DSpinoClaws: DBattle {
  private var swatted = false
  private var slashes = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let side = vec3(-dir.z, 0, dir.x)
    switch phase {
    case 0:
      if faceOffD(w) { next() }
    case 1:
      if first {
        let d: Float = dReachOf(a) * 0.55 + dReachOf(v) * 0.35 + dHalfWidthOf(v) + 0.12
        walker = DWalker(plan.mover, V - dir * d, 1.0)
      }
      dThreat(a, dSeg(pt, 0.0, 0.5))
      dVictimGuard(v, dSeg(pt, 0.0, 0.6))
      brace(w, 0.7)
      if walker.update(w, dt) { next() }
    case 2:  // she rears, claws raised, and roars
      if first { dCallOf(w, a) }
      let u = easeInOut(dSeg(pt, 0.0, 0.5))
      a.ctl.lean = -0.4 * u
      a.ctl.neck = 0.3 * u
      a.ctl.jaw = 0.9 * dBump(pt, 0.0, 0.9)
      let arms = vec3(1.5, 0.35, 0.5) * u
      a.ctl.armR = arms
      a.ctl.armL = arms
      a.ctl.itemPitch = 0.2 * u
      dVictimGuard(v, 1.0)
      brace(w, 1.0)
      if pt > 0.9 { next() }
    case 3:  // the victim lunges; she swats its jaws aside
      let u = dSeg(pt, 0.0, 0.8)
      dVictimStrike(v, u)
      a.ctl.armL = mix(vec3(1.5, 0.35, 0.5), vec3(0.4, -0.25, 0.1), easeIn(dSeg(u, 0.35, 0.55)))
      a.ctl.jaw = 0.3
      if !swatted && u > 0.52 {
        swatted = true
        w.sound(.thud, 0.8, dHeadOf(v), 1.2)
        w.sound(.snap, 0.6, dHeadOf(v), 1.0)
        v.ctl.headYaw = 0.8
        w.shake(0.1)
      }
      if swatted { v.ctl.headYaw = approach(v.ctl.headYaw, 0.0, dt * 1.5) }
      if pt > 0.85 { next() }
    case 4:  // the rakes: right, then left
      let u1 = dSeg(pt, 0.0, 0.35), u2 = dSeg(pt, 0.4, 0.75)
      a.ctl.armR = mix(vec3(1.9, 0.3, 0.6), vec3(0.1, -0.3, -0.2), easeIn(u1))
      a.ctl.armL = mix(vec3(1.9, 0.3, 0.6), vec3(0.1, -0.3, -0.2), easeIn(u2))
      if pt < 0.4 { a.ctl.armL = mix(a.ctl.armL, vec3(1.9, 0.3, 0.6), min(1.0, dt * 6.0)) }
      let b1 = dBump(u1, 0.0, 1.0), b2 = dBump(u2, 0.0, 1.0)
      a.ctl.lean = -0.4 + 0.55 * (b1 * 0.7 + b2)
      a.ctl.twist = 0.2 * (b1 - b2)
      a.ctl.jaw = 0.6
      let us = slashes == 0 ? u1 : u2
      if slashes < 2 && us > 0.6 {
        let at = slashes == 0 ? dNeckOf(v) : dChestOf(v)
        let across: vec3 = slashes == 0 ? -side : side
        let cut: vec3 = across + dir * 0.6 + vec3(0, 0.2, 0)
        let power: Float = slashes == 0 ? 0.7 : 1.1
        dWound(w, at, cut, power, DWound.claw)
        dFeathers(w, v, at, dir, 10)
        v.pos += dir * 0.04
        if slashes == 1 {
          let push: vec3 = dir * 0.3 + side * 0.2
          kill(w, 0.0, push, 0.35)
          w.slowmo(0.35, 0.5)
          w.shake(0.2)
        } else {
          dCallOf(w, v, 0.7)
        }
        slashes += 1
      }
      if slashes >= 2 { victimDown(w, dt) }
      if pt > 0.9 { next() }
    case 5:
      relax(a, dt, 2.0)
      if victimDown(w, dt) && pt > 0.3 { next() }
    case 6:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.2)
      if pt > 1.2 { next() }
    case 7:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 8:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

/// Snatch: she stalks in low like a crocodile, seizes the victim in her long jaws, shakes
/// it and flings it away.
final class DSpinoSnatch: DBattle {
  private var held = DHeld()
  private var fling = DFling()
  private var bit = false, letGo = false  // C++ `released`, which BattleBase already names
  private var lunge0 = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let side = vec3(-dir.z, 0, dir.x)
    switch phase {
    case 0:
      if faceOffD(w) { next() }
    case 1:  // the stalk: low, slow
      if first {
        let d: Float = dReachOf(a) + dReachOf(v) * 0.3 + dHalfWidthOf(v) * 0.5 + 0.08
        walker = DWalker(plan.mover, V - dir * d, 0.55)
        dWarnOf(w, a)
      }
      a.ctl.crouch = approach(a.ctl.crouch, 0.35, dt)
      a.ctl.lean = approach(a.ctl.lean, 0.22, dt)
      a.ctl.neck = approach(a.ctl.neck, -0.35, dt)
      a.ctl.headPitch = approach(a.ctl.headPitch, -0.1, dt)
      dVictimGuard(v, dSeg(pt, 0.0, 0.8))
      brace(w, 0.8)
      if walker.update(w, dt) { next() }
    case 2:  // the snap
      if first { lunge0 = a.pos }
      let u = dSeg(pt, 0.0, 0.3)
      dLunge(a, 0.2 + 0.35 * u, 1.2)
      a.pos = lunge0 + dir * 0.1 * easeIn(u)
      if !bit && u >= 1.0 {
        bit = true
        dWound(w, dNeckOf(v), side + vec3(0, 0.3, 0), 0.8, DWound.bite)
        dFeathers(w, v, dNeckOf(v), dir, 12)
        dCallOf(w, v, 0.8)
        held.take(a, v)
        next()
      }
    case 3:  // lifted and shaken
      a.ctl.jaw = 0.12
      a.ctl.neck = approach(a.ctl.neck, 0.35, dt * 3.0)
      a.ctl.lean = approach(a.ctl.lean, -0.1, dt * 3.0)
      a.ctl.crouch = approach(a.ctl.crouch, 0.1, dt * 2.0)
      a.ctl.headYaw = 0.55 * sin(pt * 15.0) * dSeg(pt, 0.2, 0.5)
      a.ctl.headPitch = 0.1 * sin(pt * 11.0)
      held.update(a, v, pt, dt)
      held.grip(w, a, v)
      if fmod(pt, 0.3) < dt {
        let spray: vec3 = side * sin(pt * 15.0) + vec3(0, 0.3, 0)
        dBloodSpray(w, dMouthOf(a), spray, 18, 1.6)
        w.sound(.tear, 0.4, dMouthOf(a), w.fx.rnd(1.0, 1.2))
      }
      if pt > 1.25 { next() }
    case 4:  // the fling: the head drawn back, then swung across, and she lets go
      if first {
        fling.begin(w, a, v, dir)
        w.sound(.whoosh, 0.7, dMouthOf(a), 0.8)
      }
      if fling.swing(a, pt) {
        held.update(a, v, pt + 1.25, dt)
        held.grip(w, a, v)
      } else if !letGo {
        letGo = true
        let out = fling.out
        if held.lifted {
          let vel: vec3 = out * 2.4 + dir * 0.5 + vec3(0, 2.2, 0)
          tumble.launch(w, v, vel, 10.0)
        } else {
          v.tilt = quat.identity
          v.tiltPivot = vec3(0)
          dWound(w, dNeckOf(v), out + vec3(0, 0.4, 0), 1.0, DWound.claw)
          kill(w, 0.0, out * 0.5, 0.12)
        }
        w.slowmo(0.35, 0.5)
        w.shake(0.2)
      }
      a.ctl.jaw = letGo ? 0.5 : 0.12
      if letGo && victimDown(w, dt) && pt > 0.6 { next() }
    case 5:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.2)
      if pt > 1.2 { next() }
    case 6:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 7:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

/// Tail swipe: she spins round and her tail sweeps the victim off its feet; she pins it
/// with a foot and bites.
final class DSpinoTail: DBattle {
  private var spinDir: Float = 1, yaw0: Float = 0
  private var hit = false, bit = false, whooshed = false
  private var body = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOffD(w) { next() }
    case 1:
      if first {
        let d: Float = 0.5 * a.scale + dHalfWidthOf(v) + 0.1
        walker = DWalker(plan.mover, V - dir * d, 1.0)
      }
      dThreat(a, dSeg(pt, 0.0, 0.5))
      dVictimGuard(v, dSeg(pt, 0.0, 0.6))
      brace(w, 0.7)
      if walker.update(w, dt) { next() }
    case 2:  // the wind-up: a growl, the body coiling
      if first {
        spinDir = w.fx.rnd() < 0.5 ? 1.0 : -1.0
        dWarnOf(w, a)
      }
      let u = easeInOut(dSeg(pt, 0.0, 0.45))
      a.ctl.twist = -0.35 * spinDir * u
      a.ctl.itemYaw = 0.5 * spinDir * u
      a.ctl.crouch = 0.2 * u
      dVictimStrike(v, dSeg(pt, 0.0, 0.5) * 0.3)
      if pt > 0.5 { next() }
    case 3:  // the spin: the tail sweeps through the victim's legs
      let dur: Float = 0.75
      if first { yaw0 = a.yaw }
      let u = min(1.0, pt / dur)
      let turn = kTau * easeInOut(u) * spinDir
      a.yaw = wrapAngle(yaw0 + turn)
      a.yawTarget = a.yaw
      a.ctl.itemYaw = -0.9 * spinDir * dBump(u, 0.0, 1.0)
      a.ctl.twist = 0.2 * spinDir * dBump(u, 0.0, 1.0)
      a.ctl.lean = 0.12 * dBump(u, 0.0, 1.0)
      a.ctl.gait += dt * 9.0
      a.ctl.stride = 0.5 * dBump(u, 0.0, 1.0)
      if !whooshed && u > 0.2 {
        whooshed = true
        w.sound(.tailwhip, 0.8, a.pos, 1.15)
      }
      // the tail comes round behind her, through the victim's legs (turning her to her left,
      // +spinDir, carries her tail across to her right: along 'tang' as it passes the victim)
      let tang = vec3(dir.z, 0, -dir.x) * spinDir
      if !hit && (u > 0.62 || (u > 0.3 && sweptInto(v, flat(a.pos), flat(dTailOf(a)), tang))) {
        hit = true
        w.sound(.thud, 0.9, dChestOf(v), 1.0)
        dWound(w, dChestOf(v) - vec3(0, 0.1, 0), tang + vec3(0, 0.3, 0), 0.5, DWound.claw)
        dFeathers(w, v, dChestOf(v), tang, 10)
        let fly: vec3 = tang * 1.2 + dir * 0.3 + vec3(0, 1.5, 0)
        knockFlying(w, fly, -7.0)  // (swept off its feet: they go first)
        w.shake(0.25)
        w.slowmo(0.35, 0.4)
      }
      if hit { victimDown(w, dt) }
      if u >= 1.0 {
        a.yawTarget = faceAngle(V - a.pos)
        next()
      }
    case 4:  // to the fallen
      let down = victimDown(w, dt)
      relax(a, dt)
      if down {
        body = v.pos
        if let cp = w.puppet(tumble.corpse >= 0 ? tumble.corpse : fall.corpse) { body = cp.pos }
        next()
      }
    case 5:
      if first { walker = DWalker(plan.mover, body - flatDir(a.pos, body) * (dReachOf(a) * 0.75), 0.9) }
      relax(a, dt)
      if walker.update(w, dt) { next() }
    case 6:  // a foot on it, and the bite
      let u = dSeg(pt, 0.0, 0.6)
      a.ctl.kick = 0.5 * dBump(u, 0.0, 0.5)
      a.ctl.neck = dKf(u, [vec2(0.0, 0.0), vec2(0.4, 0.25), vec2(0.65, -1.2), vec2(1.0, -0.9)])
      a.ctl.headPitch = dKf(u, [vec2(0.0, 0.0), vec2(0.65, 0.6), vec2(1.0, 0.5)])
      a.ctl.jaw = dKf(u, [vec2(0.0, 0.3), vec2(0.45, 0.95), vec2(0.66, 0.05), vec2(1.0, 0.1)])
      a.ctl.lean = dKf(u, [vec2(0.0, 0.0), vec2(0.65, 0.3), vec2(1.0, 0.25)])
      if !bit && u > 0.66 {
        bit = true
        dBloodSpray(w, flat(body) + vec3(0, 0.08, 0), vec3(0, 1, 0), 45, 2.0, 0.6)
        dSplatter(w, body, a.forward(), 4, 0.3)
        w.sound(.crunch, 0.9, body, 1.0)
        w.sound(.blood, 0.5, body, 1.0)
      }
      if pt > 1.0 { next() }
    case 7:
      if first { dCallOf(w, a) }
      relax(a, dt * 0.5)
      triumph(w, pt / 1.2)
      if pt > 1.2 { next() }
    case 8:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 9:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// KING - the tyrannosaurus
// ===========================================================================
/// Bone-crusher: it charges, seizes the victim in its jaws, lifts it, shakes it and
/// throws it aside.
final class DRexCrush: DBattle {
  private var held = DHeld()
  private var fling = DFling()
  private var bit = false, letGo = false  // C++ `released`, which BattleBase already names
  private var lunge0 = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let side = vec3(-dir.z, 0, dir.x)
    switch phase {
    case 0:
      shotExtra = 0.6
      if faceOffD(w) { next() }
    case 1:  // a growl, the head low, and the charge
      if first {
        let d: Float = dBiteDist(a, v) + 0.12
        walker = DWalker(plan.mover, V - dir * d, max(1.7, L / 1.1))
        walker.run = 1.0
        dWarnOf(w, a)
      }
      dThreat(a, 1.0)
      let near = length(flat(V - a.pos)) < dBiteDist(a, v) + 0.5
      a.ctl.jaw = near ? 0.9 : 0.4
      dVictimGuard(v, dSeg(pt, 0.0, 0.6))
      brace(w, 1.0)
      if walker.update(w, dt) || walker.s > walker.len - 0.03 { next() }
    case 2:  // the bite
      if first { lunge0 = a.pos }
      let u = dSeg(pt, 0.0, 0.32)
      dLunge(a, 0.25 + 0.3 * u, 1.1)
      a.pos = lunge0 + dir * 0.1 * easeIn(u)
      if !bit && u >= 1.0 {
        bit = true
        dWound(w, dNeckOf(v), dir + vec3(0, 0.5, 0), 1.2, DWound.bite)
        dFeathers(w, v, dNeckOf(v), dir, 14)
        w.sound(.crunch, 1.0, dNeckOf(v), 0.8)
        w.shake(0.25)
        w.slowmo(0.4, 0.35)
        held.take(a, v)
        dCallOf(w, v, 0.8)
        next()
      }
    case 3:  // lifted and shaken
      a.ctl.jaw = 0.15
      a.ctl.neck = approach(a.ctl.neck, 0.55, dt * 2.5)
      a.ctl.lean = approach(a.ctl.lean, -0.12, dt * 2.5)
      a.ctl.crouch = approach(a.ctl.crouch, 0.15, dt * 2.0)
      a.ctl.headYaw = 0.5 * sin(pt * 12.0) * dSeg(pt, 0.25, 0.5)
      held.update(a, v, pt, dt)
      held.grip(w, a, v)
      if !held.lifted {  // a big one: its front hauled up
        v.tiltPivot = vec3(0, 0, -0.3)
        let pitch = angleAxis(-0.22 * dSeg(pt, 0.0, 0.4), vec3(1, 0, 0))
        let roll = angleAxis(0.1 * sin(pt * 12.0), vec3(0, 0, 1))
        v.tilt = pitch * roll
      }
      if fmod(pt, 0.28) < dt {
        let spray: vec3 = side * sin(pt * 12.0) + vec3(0, 0.3, 0)
        dBloodSpray(w, dMouthOf(a), spray, 22, 1.8)
        w.sound(.crunch, 0.4, dMouthOf(a), w.fx.rnd(1.0, 1.25))
      }
      if pt > 1.35 { next() }
    case 4:  // the throw: the head drawn back, then swung across, and he lets go
      if first {
        fling.begin(w, a, v, dir)
        w.sound(.whoosh, 0.8, dMouthOf(a), 0.6)
      }
      if fling.swing(a, pt) {
        held.update(a, v, pt + 1.35, dt)
        held.grip(w, a, v)
        if !held.lifted {  // (a big one: its front still hauled up)
          v.tiltPivot = vec3(0, 0, -0.3)
          v.tilt = angleAxis(-0.22, vec3(1, 0, 0)) * angleAxis(0.1 * sin(pt * 12), vec3(0, 0, 1))
        }
      } else if !letGo {
        letGo = true
        let out = fling.out
        if held.lifted {
          let vel: vec3 = out * 2.8 + dir * 0.8 + vec3(0, 2.4, 0)
          tumble.launch(w, v, vel, 10.0)
        } else {
          v.tilt = quat.identity
          v.tiltPivot = vec3(0)
          dWound(w, dNeckOf(v), out + vec3(0, 0.4, 0), 1.0, DWound.claw)
          kill(w, 0.0, out * 0.8, 0.08)
        }
        w.slowmo(0.35, 0.5)
        w.shake(0.3)
      }
      a.ctl.jaw = letGo ? 0.5 : 0.15
      if letGo && victimDown(w, dt) && pt > 0.7 { next() }
    case 5:  // the roar over its kill
      if first {
        dCallOf(w, a)
        w.groundRing(flat(a.pos) + a.forward() * 0.3, vec3(0.7, 0.65, 0.55), 2.2, 0.5, 0.05)
      }
      triumph(w, pt / 1.6)
      if pt > 1.6 { next() }
    case 6:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 7:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

/// The roar: the victim snaps at it and misses; it roars into the victim's face - the
/// board shakes, the dust flies, the victim cowers - and bites down on it from above.
final class DRexRoar: DBattle {
  private var snapped = false, roared = false, bit = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOffD(w) { next() }
    case 1:  // stalking in, growling
      if first {
        let d: Float = dBiteDist(a, v) + 0.1
        walker = DWalker(plan.mover, V - dir * d, 0.8)
        dWarnOf(w, a)
      }
      dThreat(a, dSeg(pt, 0.0, 0.6))
      dVictimGuard(v, dSeg(pt, 0.0, 0.8))
      brace(w, 0.8)
      if walker.update(w, dt) { next() }
    case 2:  // the victim lunges at its face; it rears its head out of reach
      let u = dSeg(pt, 0.0, 0.8)
      dVictimStrike(v, u)
      a.ctl.neck = 0.45 * dBump(u, 0.3, 0.9)
      a.ctl.headPitch = -0.2 * dBump(u, 0.3, 0.9)
      a.ctl.jaw = 0.3
      if !snapped && u > 0.5 {
        snapped = true
        w.sound(.snap, 0.9, dHeadOf(v), 1.0)
      }
      if pt > 0.85 { next() }
    case 3:  // THE ROAR
      if !roared {
        roared = true
        w.sound(.rexroar, 1.0, dHeadOf(a), 0.92)
        w.shake(0.35)
      }
      let u = dSeg(pt, 0.0, 0.3)
      a.ctl.neck = mix(0.0, -0.35, u)
      a.ctl.headPitch = mix(0.0, -0.12, u)
      a.ctl.jaw = 1.0 * u * (1.0 - dSeg(pt, 1.6, 1.9))
      a.ctl.lean = 0.15 * u
      a.ctl.headYaw = 0.06 * sin(pt * 25.0) * u
      w.trauma = max(w.trauma, 0.3 * (1.0 - dSeg(pt, 1.2, 1.8)))
      if pt < 1.6 && w.fx.rnd() < dt * 40.0 {  // breath and dust blasted at the victim
        let m = dMouthOf(a)
        var p = Particle()
        let ox = w.fx.rnd(-0.3, 0.3)
        let oy = w.fx.rnd(0.0, 0.15)
        let oz = w.fx.rnd(-0.3, 0.3)
        p.pos = flat(V) + vec3(ox, oy, oz) - dir * 0.2
        let blast: vec3 = normalize(flat(V - m) + w.fx.rndDir() * 0.4) * w.fx.rnd(1.5, 3.0)
        p.vel = blast + vec3(0, w.fx.rnd(0.1, 0.6), 0)
        p.size = w.fx.rnd(0.05, 0.1)
        p.grow = 0.35
        p.color = vec3(0.55, 0.5, 0.45)
        p.colorEnd = p.color
        p.opacity = w.fx.rnd(0.15, 0.3)
        p.maxLife = w.fx.rnd(0.6, 1.1)
        p.type = .smoke
        p.drag = 2.0
        p.rot = w.fx.rnd(0.0, 6.28)
        p.seed = w.fx.rnd()
        p.fadeIn = 0.05
        w.fx.add(p)
      }
      // the victim cowers, head turned away, trembling
      let c = dSeg(pt, 0.15, 0.5)
      v.ctl.crouch = 0.4 * c
      v.ctl.neck = -0.3 * c
      v.ctl.headYaw = 0.6 * c
      v.ctl.jaw = 0.0
      v.ctl.headPitch = 0.2 * c
      v.tremble = 1.0
      if c > 0.2 && fmod(pt, 0.25) < dt { dFeathers(w, v, dChestOf(v), dir, 2) }
      if pt > 1.9 { next() }
    case 4:  // the bite from above
      let u = dSeg(pt, 0.0, 0.4)
      a.ctl.neck = dKf(u, [vec2(0.0, -0.35), vec2(0.3, 0.2), vec2(0.62, -1.1), vec2(1.0, -0.9)])
      a.ctl.headPitch = dKf(u, [vec2(0.0, -0.12), vec2(0.62, 0.55), vec2(1.0, 0.45)])
      a.ctl.jaw = dKf(u, [vec2(0.0, 0.8), vec2(0.45, 1.0), vec2(0.64, 0.08), vec2(1.0, 0.1)])
      a.ctl.lean = dKf(u, [vec2(0.0, 0.15), vec2(0.62, 0.35), vec2(1.0, 0.3)])
      if !bit && u > 0.64 {
        bit = true
        let at: vec3 = dNeckOf(v) * 0.5 + dChestOf(v) * 0.5
        let down: vec3 = vec3(0, 0.6, 0) + dir * 0.5
        dWound(w, at, down, 1.3, DWound.bite)
        dFeathers(w, v, dNeckOf(v), dir, 14)
        fall.bleed = 1.3
        kill(w, 0.0, dir * 0.15, 0.1)
        w.shake(0.3)
        w.slowmo(0.35, 0.5)
      }
      if bit { victimDown(w, dt) }
      if pt > 0.8 { next() }
    case 5:  // it lifts its bloody head
      let u = easeInOut(dSeg(pt, 0.0, 0.8))
      a.ctl.neck = mix(-0.9, 0.2, u)
      a.ctl.headPitch = mix(0.45, -0.1, u)
      a.ctl.lean = mix(0.3, 0.0, u)
      a.ctl.jaw = 0.25 * u
      if first { w.sound(.growl, 0.8, dHeadOf(a), 0.85) }
      if victimDown(w, dt) && pt > 1.0 { next() }
    case 6:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 7:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

/// Head ram and stomp: it rams the victim with its massive skull, stamps on it with a
/// great foot and tears at it.
final class DRexStomp: DBattle {
  private var rammed = false, stamped = false, torn = false
  private var lunge0 = vec3(0), body = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOffD(w) { next() }
    case 1:  // head down, a short charge
      if first {
        let d: Float = dReachOf(a) * 0.6 + dReachOf(v) * 0.3 + dHalfWidthOf(v) * 0.5 + 0.12
        walker = DWalker(plan.mover, V - dir * d, max(1.5, L / 1.2))
        walker.run = 1.0
        dCallOf(w, a, 0.7)
      }
      a.ctl.headPitch = approach(a.ctl.headPitch, 0.3, dt * 2.0)
      a.ctl.neck = approach(a.ctl.neck, -0.3, dt * 2.0)
      dVictimGuard(v, dSeg(pt, 0.0, 0.6))
      brace(w, 1.0)
      if walker.update(w, dt) || walker.s > walker.len - 0.03 { next() }
    case 2:  // the ram
      if first { lunge0 = a.pos }
      let u = dSeg(pt, 0.0, 0.25)
      a.ctl.neck = mix(-0.3, -0.85, easeIn(u))
      a.ctl.lean = 0.3 * u
      a.pos = lunge0 + dir * 0.12 * easeIn(u)
      if !rammed && u >= 1.0 {
        rammed = true
        w.sound(.thud, 1.0, dHeadOf(a), 0.7)
        w.sound(.crunch, 0.7, dHeadOf(a), 0.85)
        dWound(w, dNeckOf(v), dir + vec3(0, 0.3, 0), 0.5, DWound.crush)
        dFeathers(w, v, dNeckOf(v), dir, 12)
        let fly: vec3 = dir * 2.0 + vec3(0, 1.8, 0)
        knockFlying(w, fly, 8.0)
        w.shake(0.4)
        w.slowmo(0.35, 0.4)
      }
      if rammed { victimDown(w, dt) }
      if pt > 0.45 { next() }
    case 3:  // waiting for it to fall
      a.ctl.neck = approach(a.ctl.neck, 0.0, dt * 2.0)
      a.ctl.lean = approach(a.ctl.lean, 0.0, dt * 2.0)
      let down = victimDown(w, dt)
      if down {
        body = v.pos
        if let cp = w.puppet(tumble.corpse >= 0 ? tumble.corpse : fall.corpse) { body = cp.pos }
        next()
      }
    case 4:  // up to the body
      if first { walker = DWalker(plan.mover, body - flatDir(a.pos, body) * 0.42 * a.scale, 0.9) }
      relax(a, dt)
      if walker.update(w, dt) { next() }
    case 5:  // the great foot raised, and stamped down
      let u = dSeg(pt, 0.0, 0.55)
      a.ctl.kick = dKf(u, [vec2(0.0, 0.0), vec2(0.65, 0.75), vec2(0.85, 0.0)])
      a.ctl.lean = -0.1 * dBump(u, 0.0, 0.8)
      if !stamped && u > 0.85 {
        stamped = true
        w.sound(.quake, 1.0, body, 0.85)
        w.sound(.crunch, 0.9, body, 0.85)
        dBloodSpray(w, body + vec3(0, 0.06, 0), vec3(0, 1, 0), 45, 2.0, 0.7)
        dSplatter(w, body, a.forward(), 5, 0.35)
        w.fx.dust(body, 20, 0.35, 1.0)
        w.shake(0.4)
      }
      if pt > 0.7 { next() }
    case 6:  // and the jaws: it tears at the body
      let u = dSeg(pt, 0.0, 0.9)
      a.ctl.neck = dKf(u, [vec2(0.0, 0.0), vec2(0.35, -1.15), vec2(0.6, -1.1), vec2(0.85, 0.25)])
      a.ctl.headPitch = dKf(u, [vec2(0.0, 0.0), vec2(0.35, 0.6), vec2(0.85, -0.2)])
      a.ctl.jaw = dKf(u, [vec2(0.0, 0.2), vec2(0.25, 0.9), vec2(0.4, 0.05), vec2(0.85, 0.15)])
      a.ctl.lean = dKf(u, [vec2(0.0, 0.0), vec2(0.35, 0.3), vec2(0.85, 0.0)])
      if !torn && u > 0.62 {
        torn = true
        let spray: vec3 = vec3(0, 1, 0) - a.forward() * 0.3
        dBloodSpray(w, dMouthOf(a), spray, 50, 2.3, 0.5)
        w.sound(.tear, 1.0, body, 0.85)
        w.sound(.blood, 0.6, body, 1.0)
      }
      if pt > 1.0 { next() }
    case 7:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.6)
      if pt > 1.6 { next() }
    case 8:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 9:
      return finishD(w)
    default:
      break
    }
    return false
  }
}
