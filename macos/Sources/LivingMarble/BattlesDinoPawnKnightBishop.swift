// Dinosaur battles of the velociraptor (killing claw, pack attack, dodge and bite), the
// pteranodon (dive, wing buffet, talon strike) and the triceratops (horn charge, horn
// lock, frill bash). (As battle_dino.cpp; the toolkit - DBattle, DWalker, DSoar, DFall,
// DTumble, DGlide, dAirborne and friends - is in BattlesDino.swift.)
import Foundation
import simd

// ===========================================================================
// PAWN - the velociraptor
// ===========================================================================
/// Killing claw: a hiss, a sprint and a leap; it lands on the victim feet first and
/// clings there, kicking with its sickle claws, until the victim goes down.
final class DRaptorClaw: DBattle {
  private var leap0 = vec3(0), perch = vec3(0), offset = vec3(0), landAt = vec3(0)
  private var slashes = 0
  private var growled = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.4
      if faceOffD(w) { next() }
    case 1:  // crouched, hissing, the tail lashing; the victim answers
      if first { dWarnOf(w, a) }
      let u = dSeg(pt, 0.0, 0.6)
      dThreat(a, u)
      a.ctl.crouch = 0.35 * u
      a.ctl.itemYaw = 0.3 * sin(pt * 14.0) * u
      dVictimGuard(v, dSeg(pt, 0.2, 0.8))
      if !growled && pt > 0.35 {
        growled = true
        dWarnOf(w, v, 0.8)
      }
      brace(w, 0.6)
      if pt > 0.85 { next() }
    case 2:  // the sprint
      if first {
        let leapFrom: Float = dBiteDist(a, v) + 0.55
        let back: Float = min(leapFrom, max(0.05, L - 0.05))
        let to: vec3 = V - dir * back
        walker = DWalker(plan.mover, to, max(2.2, L / 1.0))
        walker.run = 1.0
        dCallOf(w, a, 0.8)
      }
      a.ctl.jaw = 0.6
      a.ctl.itemYaw = approach(a.ctl.itemYaw, 0.0, dt * 3.0)
      a.ctl.crouch = approach(a.ctl.crouch, 0.0, dt * 3.0)
      dVictimGuard(v, 1.0)
      brace(w, 0.8)
      if walker.update(w, dt) || walker.s > walker.len - 0.03 { next() }
    case 3:  // the leap: feet first, claws out, arms spread
      let dur: Float = 0.42
      if first {
        leap0 = a.pos
        let target = dNeckOf(v)
        let back: Float = 0.13 + dHalfWidthOf(v) * 0.6
        perch = flat(target) - dir * back
        perch.y = max(0.0, target.y - 0.22 * a.scale)
        offset = perch - v.pos
        a.yawTarget = faceAngle(dir)
        w.sound(.whoosh, 0.6, a.pos, 1.3)
      }
      let u = min(1.0, pt / dur)
      a.pos = mix(leap0, perch, easeOut(u))
      a.pos.y += 4.0 * u * (1.0 - u) * 0.22
      a.ctl.tuck = max(a.ctl.tuck, smoothstep01(dSeg(u, 0.0, 0.5)))
      a.ctl.jaw = 0.85
      a.ctl.lean = -0.25 * u
      v.ctl.headPitch = approach(v.ctl.headPitch, -0.3, dt * 3.0)
      brace(w, 1.0)
      if u >= 1.0 {
        w.sound(.thud, 0.6, a.pos, 1.4)
        dFeathers(w, v, dNeckOf(v), -dir, 10)
        dFeathers(w, a, a.pos + vec3(0, 0.25, 0), dir, 6)
        v.pos += dir * 0.04
        dCallOf(w, v, 0.9)
        next()
      }
    case 4:  // clinging on, kicking: three slashes of the sickle claws, a bite
      a.pos = v.pos + offset
      a.ctl.tuck = 0.75 + 0.1 * sin(pt * 9.0)
      let k = fmod(pt, 0.36) / 0.36
      a.ctl.kick = 0.6 * dBump(k, 0.0, 0.7)
      a.ctl.neck = -0.35 + 0.25 * sin(pt * 12.0)
      a.ctl.jaw = 0.5 + 0.4 * sin(pt * 12.0 + 1.0)
      a.ctl.lean = -0.2
      // the victim thrashes, roaring
      v.ctl.headYaw = 0.55 * sin(pt * 9.0)
      v.ctl.neck = 0.3 * sin(pt * 7.0) + 0.2
      v.ctl.jaw = 0.8
      v.ctl.crouch = 0.15 * abs(sin(pt * 6.0))
      v.tremble = 0.4
      if slashes < 3 && pt > 0.25 + 0.33 * Float(slashes) {
        let c: vec3 = dNeckOf(v) * 0.5 + dChestOf(v) * 0.5
        let side = vec3(-dir.z, 0, dir.x)
        let across: Float = slashes % 2 != 0 ? 0.6 : -0.6
        let cut: vec3 = dir + side * across + vec3(0, 0.4, 0)
        dWound(w, c, cut, slashes == 2 ? 0.9 : 0.55, DWound.claw)
        dFeathers(w, v, c, -dir, 6)
        w.shake(0.08)
        slashes += 1
      }
      if pt > 1.2 { next() }
    case 5:  // the victim goes down; the raptor springs clear
      if first {
        leap0 = a.pos
        landAt = dClampBoard(flat(a.pos) - dir * 0.3, 4.0)
        kill(w, 0.0, dir * 0.25, 0.15)
        w.slowmo(0.35, 0.5)
        w.shake(0.15)
      }
      let u = min(1.0, pt / 0.42)
      a.pos = mix(leap0, landAt, u)
      let arc: Float = 4.0 * u * (1.0 - u) * 0.15
      a.pos.y = mix(leap0.y, 0.0, u) + arc
      a.ctl.tuck = 1.0 - u
      a.ctl.kick = 0
      a.ctl.lean = 0
      victimDown(w, dt)
      if u >= 1.0 {
        a.pos = landAt
        w.sound(.step, 0.4, landAt, 1.5)
        next()
      }
    case 6:
      relax(a, dt)
      dThreat(a, 0.4)
      if victimDown(w, dt) && pt > 0.3 { next() }
    case 7:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.0)
      if pt > 1.0 { next() }
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
  private func smoothstep01(_ x: Float) -> Float { x * x * (3.0 - 2.0 * x) }
}

/// Pack attack: it calls, and two more raptors come running from the edges of the board;
/// the victim turns on one of them, and the others are on it.
final class DRaptorPack: DBattle {
  private var mates = [-1, -1]
  private var from = [vec3](repeating: vec3(0), count: 2)
  private var flank = [vec3](repeating: vec3(0), count: 2)
  private var lunge0 = vec3(0)
  private var answered = false, snapped = false, bit = false, kicked = false
  private var feedT: Float = 0
  private func mate(_ w: World, _ i: Int) -> Puppet? { w.puppet(mates[i]) }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let side = vec3(-dir.z, 0, dir.x)
    for i in 0..<2 where mates[i] >= 0 { w.solidPuppet(mates[i]) }
    switch phase {
    case 0:
      shotExtra = 1.1
      shotHeight = 0.35
      if faceOffD(w) { next() }
    case 1:  // the call, and the answers from the edges of the board
      if first {
        dCallOf(w, a)
        for i in 0..<2 {
          let s: Float = i == 0 ? 1.0 : -1.0
          let out: vec3 = V + side * s * 3.0 + dir * 0.4
          var f = dClampBoard(out, 4.2)
          if length(flat(f - V)) < 1.6 {
            let behind: vec3 = V - dir * 2.6 + side * s * 1.3
            f = dClampBoard(behind, 4.2)
          }
          from[i] = f
          let reach: Float = dHalfWidthOf(v) + 0.36
          let fl: vec3 = V + side * s * reach - dir * 0.06
          flank[i] = dClampBoard(fl, 3.9)
          mates[i] = w.addPuppet(DAsset.pawn, a.color, 4)
          if let p = mate(w, i) {
            p.pos = f
            p.yaw = faceAngle(V - f)
            p.dissolve = 1.0
            p.texOffset = a.texOffset + vec3(1.7 + Float(i), 0.6 * Float(i), 2.3)
          }
        }
      }
      if !answered && pt > 0.35 {
        answered = true
        w.sound(.screech, 0.65, from[0], 1.12)
        w.sound(.screech, 0.6, from[1], 0.97)
      }
      for i in 0..<2 {
        if let p = mate(w, i) { p.dissolve = max(0.0, p.dissolve - dt * 2.5) }
      }
      dThreat(a, dSeg(pt, 0.0, 0.5))
      dVictimGuard(v, dSeg(pt, 0.2, 0.7))
      v.ctl.headYaw = 0.6 * sin(pt * 4.0)
      brace(w, 0.8)
      if pt > 0.8 { next() }
    case 2:  // the pack closes in
      if first {
        let stop: vec3 = V - dir * (dBiteDist(a, v) + 0.12)
        walker = DWalker(plan.mover, stop, 1.4)
      }
      let d0 = walker.update(w, dt)
      var d1 = true
      for i in 0..<2 {
        if let p = mate(w, i) {
          p.dissolve = max(0.0, p.dissolve - dt * 2.5)
          let there = dRunPuppet(w, p, flank[i], 2.6, dt)
          if there {
            let turn = clamp(wrapAngle(faceAngle(V - p.pos) - p.yaw), -dt * 8.0, dt * 8.0)
            p.yaw = wrapAngle(p.yaw + turn)
            p.ctl.stride = approach(p.ctl.stride, 0.0, dt * 4.0)
            p.ctl.jaw = 0.5
            p.ctl.crouch = 0.25
          }
          d1 = d1 && there
        }
      }
      dThreat(a, 1.0)
      v.ctl.headYaw = 0.55 * sin(pt * 5.0)
      brace(w, 1.0)
      if (d0 && d1) || pt > 3.0 { next() }
    case 3:  // the victim turns on the first of them, which springs back from its jaws
      if first {
        v.yawTarget = faceAngle(flank[0] - V)
        v.yawRate = 6.0
      }
      let u = dSeg(pt, 0.1, 0.85)
      dVictimStrike(v, u)
      if let p = mate(w, 0) {
        let hop = dSeg(pt, 0.4, 0.65)
        let away = flatDir(V, flank[0])
        p.pos = flank[0] + away * 0.22 * easeOut(hop)
        p.pos.y = 4.0 * hop * (1.0 - hop) * 0.1
        p.ctl.jaw = 0.7
        p.ctl.tuck = 0.3 * dBump(hop, 0.0, 1.0)
      }
      if !snapped && u > 0.5 {
        snapped = true
        w.sound(.snap, 0.8, dHeadOf(v), 1.0)
        if let p = mate(w, 0) { w.sound(.hiss, 0.5, p.pos, 1.2) }
      }
      dThreat(a, 1.0)
      brace(w, 0.5)
      if pt > 0.9 { next() }
    case 4:  // and the others are on it: a bite at the neck, a kick of the sickle claw
      if first {
        lunge0 = a.pos
        w.sound(.screech, 0.7, a.pos, 1.18)
      }
      let u = dSeg(pt, 0.0, 0.55)
      dLunge(a, u, 1.1)
      a.pos = lunge0 + dir * 0.12 * dBump(u, 0.2, 1.0)
      a.ctl.tuck = 0.3 * dBump(u, 0.2, 0.8)
      if let p = mate(w, 1) {
        p.ctl.kick = dBump(dSeg(pt, 0.1, 0.5), 0.0, 1.0)
        p.ctl.lean = 0.15
        p.ctl.jaw = 0.8
      }
      if let p = mate(w, 0) {  // back in, at the tail
        p.pos = mix(p.pos, flank[0], min(1.0, dt * 6.0))
        p.ctl.neck = -0.5 * dBump(pt, 0.2, 0.6)
        p.ctl.jaw = 0.5 + 0.4 * sin(pt * 14.0)
      }
      if !bit && pt > 0.28 {
        bit = true
        dWound(w, dNeckOf(v), dir + vec3(0, 0.3, 0), 1.0, DWound.bite)
        dFeathers(w, v, dNeckOf(v), dir, 10)
      }
      if !kicked && pt > 0.36 {
        kicked = true
        let rake: vec3 = side * -1.0 + vec3(0, 0.3, 0)
        dWound(w, dChestOf(v), rake, 0.7, DWound.claw)
        let push: vec3 = -side * 0.15 + dir * 0.1
        kill(w, 0.0, push, 0.25)
        w.slowmo(0.35, 0.5)
        w.shake(0.15)
      }
      if kicked { victimDown(w, dt) }
      if pt > 0.65 { next() }
    case 5:  // the pack feeds a moment
      let down = victimDown(w, dt)
      feedT += dt
      var body = v.pos
      if let cp = w.puppet(fall.corpse) { body = cp.pos }
      let k = sin(pt * 9.0)
      a.ctl.neck = -0.55 + 0.25 * k
      a.ctl.headPitch = 0.35
      a.ctl.jaw = 0.5 + 0.4 * sin(pt * 13.0)
      a.ctl.lean = 0.2
      for i in 0..<2 {
        if let p = mate(w, i) {
          p.ctl.kick = approach(p.ctl.kick, 0.0, dt * 3.0)
          p.ctl.neck = -0.55 + 0.25 * sin(pt * 8.0 + 1.5 * Float(i + 1))
          p.ctl.headPitch = 0.35
          p.ctl.jaw = 0.5 + 0.4 * sin(pt * 12.0 + Float(i))
          p.ctl.lean = 0.2
        }
      }
      if fmod(pt, 0.45) < dt {
        let rx = w.fx.rnd(-0.1, 0.1)
        let rz = w.fx.rnd(-0.1, 0.1)
        dBloodSpray(w, flat(body) + vec3(rx, 0.08, rz), vec3(0, 1, 0), 12, 1.4)
        w.sound(.tear, 0.4, body, w.fx.rnd(1.1, 1.3))
      }
      if down && pt > 1.4 { next() }
    case 6:  // the pack runs off; the hunter calls after them
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.0)
      var gone = true
      for i in 0..<2 {
        if let p = mate(w, i) {
          p.ctl.neck = approach(p.ctl.neck, 0.0, dt * 3.0)
          p.ctl.headPitch = approach(p.ctl.headPitch, 0.0, dt * 3.0)
          p.ctl.jaw = approach(p.ctl.jaw, 0.0, dt * 3.0)
          p.ctl.lean = approach(p.ctl.lean, 0.0, dt * 3.0)
          if pt > 0.25 { dRunPuppet(w, p, from[i], 2.4, dt) }
          if length(flat(p.pos - from[i])) < 0.9 || pt > 1.6 { p.dissolve = min(1.0, p.dissolve + dt * 2.5) }
          if p.dissolve >= 1.0 { p.alive = false }
          gone = false
        }
      }
      if gone || pt > 2.6 {
        for i in 0..<2 {
          if let p = mate(w, i) { p.alive = false }
        }
        next()
      }
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

/// Dodge and bite: it circles, hissing; the victim lunges and it slips aside, then darts
/// in and seizes the throat, hanging on as the victim thrashes, until it rips it open.
final class DRaptorDodge: DBattle {
  private var R: Float = 0.8, th0: Float = 0, th1: Float = 0
  private var dodge0 = vec3(0), dodgeTo = vec3(0), seize = vec3(0), hold = vec3(0)
  private var snapped = false, bit = false
  private var ripT: Float = -1

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.5
      if faceOffD(w) { next() }
    case 1:  // in, to circling distance
      if first {
        R = dBiteDist(a, v) + 0.32
        walker = DWalker(plan.mover, V - dir * R, 1.3)
        dWarnOf(w, a)
      }
      dThreat(a, dSeg(pt, 0.0, 0.4))
      dVictimGuard(v, dSeg(pt, 0.0, 0.6))
      brace(w, 0.6)
      if walker.update(w, dt) { next() }
    case 2:  // circling, the victim turning to keep it in front of it
      if first {
        th0 = faceAngle(flat(a.pos) - V)
        let way: Float = w.fx.rnd() < 0.5 ? -1.0 : 1.0
        th1 = th0 + way * 0.9
      }
      let u = dSeg(pt, 0.0, 1.3)
      let th = mix(th0, th1, easeInOut(u))
      let prev = a.pos
      a.pos = V + vec3(sin(th), 0, cos(th)) * R
      a.yawTarget = faceAngle(V - a.pos)
      let stepLen: Float = length(flat(a.pos - prev))
      a.ctl.gait += stepLen / (Dino.strideCycle(a.kind) * a.scale) * kTau
      a.ctl.stride = u < 1.0 ? 0.6 : approach(a.ctl.stride, 0.0, dt * 4.0)
      dThreat(a, 1.0)
      v.yawTarget = faceAngle(a.pos - V)
      v.yawRate = 4.0
      dVictimGuard(v, 1.0)
      brace(w, 0.7)
      if pt > 1.4 { next() }
    case 3:  // the victim lunges; the raptor springs aside and the jaws close on air
      if first {
        dodge0 = a.pos
        let d = flatDir(V, a.pos)
        let side = vec3(-d.z, 0, d.x)
        let aside: vec3 = dodge0 + side * 0.3 + d * 0.05
        dodgeTo = dClampBoard(aside, 4.0)
        dWarnOf(w, v)
      }
      let u = dSeg(pt, 0.0, 0.8)
      dVictimStrike(v, u)
      v.pos += flatDir(V, dodge0) * dt * 0.25 * dBump(u, 0.3, 0.6)  // it overreaches
      let hop = dSeg(pt, 0.22, 0.45)
      a.pos = mix(dodge0, dodgeTo, easeOut(hop))
      a.pos.y = 4.0 * hop * (1.0 - hop) * 0.12
      a.ctl.tuck = 0.4 * dBump(hop, 0.0, 1.0)
      a.yawTarget = faceAngle(V - a.pos)
      if !snapped && u > 0.5 {
        snapped = true
        w.sound(.snap, 0.9, dHeadOf(v), 0.95)
        w.sound(.whoosh, 0.4, dHeadOf(v), 0.8)
      }
      if pt > 0.85 { next() }
    case 4:  // the counter: in from the side and on the throat
      if first {
        dodge0 = a.pos
        let n = dNeckOf(v)
        let d = flatDir(a.pos, n)
        seize = flat(n) - d * dReachOf(a) * 0.95
        a.yawTarget = faceAngle(d)
        a.yawRate = 14.0
      }
      let u = dSeg(pt, 0.0, 0.32)
      a.pos = mix(dodge0, seize, easeIn(u))
      dLunge(a, 0.3 + 0.25 * u, 1.0)
      if !bit && u >= 1.0 {
        bit = true
        dWound(w, dNeckOf(v), flatDir(a.pos, V) + vec3(0, 0.4, 0), 0.8, DWound.bite)
        dFeathers(w, v, dNeckOf(v), flatDir(a.pos, V), 8)
        dCallOf(w, v, 0.9)
        hold = a.pos - v.pos
        next()
      }
    case 5:  // hanging on by its jaws as the victim thrashes; then it rips
      let sh = sin(pt * 13.0)
      v.ctl.headYaw = 0.6 * sin(pt * 7.0)
      v.ctl.neck = 0.35 + 0.2 * sh
      v.ctl.jaw = 0.8
      v.tremble = 0.5
      v.pos += flatDir(a.pos, v.pos) * dt * 0.05 * sin(pt * 6.0)
      a.pos = v.pos + hold + flatDir(v.pos, a.pos) * 0.03 * sh
      a.ctl.jaw = 0.1
      a.ctl.neck = -0.45 + 0.1 * sh
      a.ctl.headYaw = 0.4 * sin(pt * 16.0)
      a.ctl.crouch = 0.3
      a.ctl.lean = 0.15
      a.ctl.tuck = 0
      if fmod(pt, 0.3) < dt { dBloodSpray(w, dNeckOf(v), vec3(0, 0.5, 0) + flatDir(a.pos, v.pos), 14, 1.3) }
      if pt > 1.1 && ripT < 0 {
        ripT = pt
        dWound(w, dNeckOf(v), flatDir(v.pos, a.pos) + vec3(0, 0.6, 0), 1.1, DWound.claw)
        kill(w, 0.0, flatDir(a.pos, v.pos) * 0.2, 0.3)
        w.slowmo(0.35, 0.5)
        w.shake(0.12)
      }
      if ripT >= 0 {
        let r = dSeg(pt - ripT, 0.0, 0.3)
        a.ctl.neck = mix(-0.45, 0.4, r)
        a.ctl.headYaw = 0
        a.pos -= flatDir(a.pos, v.pos) * dt * 0.4 * (1.0 - r)
        victimDown(w, dt)
        if pt - ripT > 0.35 { next() }
      }
    case 6:
      relax(a, dt)
      if victimDown(w, dt) && pt > 0.3 { next() }
    case 7:
      if first {
        dCallOf(w, a)
        a.yawRate = 9.0
      }
      triumph(w, pt / 1.0)
      if pt > 1.0 { next() }
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
// KNIGHT - the pteranodon
// ===========================================================================
// (dAirborne, flying with the controls eased towards a pose in the air, and DGlide, a
// flight from one point to another, are in BattlesDino.swift.)

/// Dive: it takes off, climbs and circles, then folds its wings and stoops on the victim,
/// beak first; it pulls out of the dive and lands beyond.
final class DPteroDive: DBattle {
  private var g = DGlide()
  private var offset = vec3(0), hitAt = vec3(0), flightDir = vec3(0), landAt = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let side = vec3(-dir.z, 0, dir.x)
    switch phase {
    case 0:
      shotExtra = 1.4
      shotHeight = 0.9
      if faceOffD(w) { next() }
    case 1:  // take off and climb
      if first {
        let up: vec3 = A - dir * 0.3 + side * 0.3 + vec3(0, 1.5, 0)
        g.set(a.pos, up, 1.0)
        dCallOf(w, a)
        w.sound(.flap, 0.8, a.pos, 0.95)
        w.fx.dust(A, 10, 0.4, 0.9)
      }
      dAirborne(a, dt, 15.0, -0.2)
      g.update(a, dt, false)
      a.yawTarget = faceAngle(V - a.pos)
      dVictimGuard(v, dSeg(pt, 0.2, 0.8))
      v.ctl.headPitch = approach(v.ctl.headPitch, -0.35, dt * 2.0)
      brace(w, 0.6)
      if pt > 1.0 { next() }
    case 2:  // round to the stooping point, high and behind
      if first {
        let high: vec3 = V - dir * 1.2 - side * 0.35 + vec3(0, 1.9, 0)
        g.set(a.pos, high, 0.9, 0.15)
      }
      dAirborne(a, dt, 9.0, 0.0)
      g.update(a, dt)
      if pt > 0.6 {  // folding for the dive
        a.yawTarget = faceAngle(V - a.pos)
        a.ctl.wings = approach(a.ctl.wings, 0.35, dt * 3.0)
        a.ctl.lean = approach(a.ctl.lean, 0.8, dt * 3.0)
        a.ctl.neck = approach(a.ctl.neck, -0.5, dt * 3.0)
      }
      v.ctl.headPitch = approach(v.ctl.headPitch, -0.5, dt * 2.0)
      brace(w, 0.8)
      if pt > 0.95 { next() }
    case 3:  // the stoop
      let dur: Float = 0.45
      if first {
        offset = dMouthOf(a) - a.pos
        hitAt = dNeckOf(v) * 0.5 + dHeadOf(v) * 0.5
        g.set(a.pos, hitAt - offset, dur)
        flightDir = normalize(g.p1 - g.p0)
        w.sound(.whoosh, 0.9, a.pos, 0.7)
        w.sound(.squawk, 0.5, a.pos, 1.2)
      }
      a.ctl.wings = 0.3
      a.ctl.hover = 1.0
      a.ctl.lean = 0.8
      a.ctl.neck = -0.5
      a.ctl.jaw = 0
      let u = min(1.0, pt / dur)
      a.pos = mix(g.p0, g.p1, u * u)
      a.yawTarget = faceAngle(flightDir)
      brace(w, 1.0)
      if u >= 1.0 {
        dWound(w, hitAt, flightDir + vec3(0, 0.3, 0), 1.1, DWound.gore)
        dFeathers(w, v, hitAt, flightDir, 12)
        kill(w, 0.0, flat(flightDir) * 0.5, 0.12)
        w.slowmo(0.3, 0.5)
        w.shake(0.3)
        next()
      }
    case 4:  // pulling out, wings beating hard
      if first {
        let out: vec3 = flat(a.pos) + dir * 0.5 + vec3(0, 0.9, 0)
        g.set(a.pos, out, 0.55)
      }
      dAirborne(a, dt, 16.0, -0.3)
      a.ctl.neck = approach(a.ctl.neck, 0.0, dt * 4.0)
      g.update(a, dt, false)
      victimDown(w, dt)
      if pt > 0.55 { next() }
    case 5:  // and down beyond the victim, legs forward, wings back-beating, folded
      if first {
        landAt = dClampBoard(V + dir * 0.6, 4.0)
        g.set(a.pos, landAt, 0.75, 0.1)
        a.yawTarget = faceAngle(-dir)
      }
      let down = g.update(a, dt, false)
      let u = dSeg(pt, 0.0, 0.75)
      a.ctl.flap += dt * (u > 0.6 ? 14.0 : 7.0)
      a.ctl.wings = 1.0
      a.ctl.hover = 1.0 - dSeg(u, 0.55, 1.0)
      a.ctl.tuck = dSeg(u, 0.5, 0.9)
      a.ctl.lean = -0.35 * dSeg(u, 0.5, 1.0)
      victimDown(w, dt)
      if down {
        a.pos = landAt
        w.sound(.step, 0.5, landAt, 1.5)
        w.sound(.flap, 0.5, landAt, 1.1)
        w.fx.dust(landAt, 8, 0.3, 0.6)
        next()
      }
    case 6:
      let u = dSeg(pt, 0.0, 0.4)
      a.ctl.tuck = 1.0 - u
      a.ctl.lean = -0.35 * (1.0 - u)
      a.ctl.wings = 1.0 - u
      a.ctl.hover = 0
      if victimDown(w, dt) && pt > 0.45 { next() }
    case 7:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.0)
      if pt > 1.0 { next() }
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

/// Wing buffet: it flies up to the victim and rears, mantling its wings; it beats them in
/// the victim's face, knocks its jaws aside with a wing, and stabs - three times.
final class DPteroBuffet: DBattle {
  private var stabs = 0
  private var buffeted = false
  private var stand = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.7
      shotHeight = 0.5
      if faceOffD(w) { next() }
    case 1:  // a hop through the air, to land in front of it
      if first {
        let back: Float = dReachOf(a) * 0.75 + dReachOf(v) * 0.45 + dHalfWidthOf(v) + 0.1
        stand = V - dir * back
        soar = DSoar(plan.mover, stand, 0.45)
      }
      dVictimGuard(v, dSeg(pt, 0.2, 0.8))
      brace(w, 0.7)
      if soar.update(w, dt) { next() }
    case 2:  // mantling: up tall, wings spread, beating them at the victim
      if first { dCallOf(w, a) }
      let u = dSeg(pt, 0.0, 0.4)
      a.ctl.wings = u
      a.ctl.lean = -0.45 * u
      a.ctl.neck = 0.35 * u
      a.ctl.jaw = 0.8 * u
      if pt > 0.35 {
        let before = floor(a.ctl.flap / kTau)
        a.ctl.flap += dt * 11.0
        if floor(a.ctl.flap / kTau) != before {
          w.sound(.flap, 0.7, a.pos, w.fx.rnd(0.9, 1.0))
          let g: vec3 = flat(V) - dir * 0.15
          for _ in 0..<6 {  // the downdraught blowing dust over the victim
            var p = Particle()
            let ox = w.fx.rnd(-0.25, 0.25)
            let oy = w.fx.rnd(0.0, 0.1)
            let oz = w.fx.rnd(-0.25, 0.25)
            p.pos = g + vec3(ox, oy, oz)
            let speed = w.fx.rnd(1.0, 2.0)
            let lift = w.fx.rnd(0.1, 0.5)
            p.vel = dir * speed + vec3(0, lift, 0)
            p.size = w.fx.rnd(0.05, 0.1)
            p.grow = 0.3
            p.color = vec3(0.55, 0.5, 0.45)
            p.colorEnd = p.color
            p.opacity = w.fx.rnd(0.15, 0.3)
            p.maxLife = w.fx.rnd(0.6, 1.0)
            p.type = .smoke
            p.drag = 2.5
            p.rot = w.fx.rnd(0.0, 6.28)
            p.seed = w.fx.rnd()
            p.fadeIn = 0.05
            w.fx.add(p)
          }
        }
      }
      // the victim flinches back from the wind of the wings
      v.ctl.crouch = 0.3 * dSeg(pt, 0.4, 0.8)
      v.ctl.headPitch = -0.3 * dSeg(pt, 0.4, 0.8)
      v.ctl.headYaw = 0.25 * sin(pt * 6.0) * dSeg(pt, 0.4, 0.8)
      if pt > 0.4 && pt < 1.0 { v.pos += dir * dt * 0.08 }
      brace(w, 1.0)
      if pt > 1.3 { next() }
    case 3:  // the victim strikes back: a wing slams its jaws aside
      let u = dSeg(pt, 0.0, 0.8)
      dVictimStrike(v, u)
      let held: Float = ceil(a.ctl.flap / kTau) * kTau + kPI * 0.5
      a.ctl.flap = mix(a.ctl.flap, held, min(1.0, dt * 5.0))
      if !buffeted && u > 0.45 {
        buffeted = true
        w.sound(.flap, 1.0, dHeadOf(v), 0.8)
        w.sound(.thud, 0.6, dHeadOf(v), 1.3)
        w.shake(0.1)
      }
      if buffeted {
        v.ctl.headYaw = approach(v.ctl.headYaw, 0.8, dt * 6.0)
        v.ctl.neck = approach(v.ctl.neck, 0.4, dt * 4.0)
      }
      if pt > 0.85 { next() }
    case 4:  // three stabs of the beak
      let k = fmod(pt, 0.4) / 0.4
      dLunge(a, k, 1.15)
      a.ctl.jaw = 0.05
      a.ctl.wings = approach(a.ctl.wings, 0.6, dt * 2.0)
      a.ctl.lean = approach(a.ctl.lean, 0.1, dt * 3.0)
      v.ctl.headYaw = approach(v.ctl.headYaw, 0.2, dt * 2.0)
      if stabs < 3 && pt > 0.21 + 0.4 * Float(stabs) {
        let at = stabs == 2 ? dNeckOf(v) : dHeadOf(v)
        dWound(w, at, dir + vec3(0, 0.25, 0), stabs == 2 ? 1.0 : 0.45, DWound.gore)
        dFeathers(w, v, at, dir, 6)
        v.pos += dir * 0.03
        v.ctl.headYaw = (stabs % 2 != 0 ? -0.5 : 0.5)
        if stabs == 2 {
          kill(w, 0.0, dir * 0.3, 0.3)
          w.slowmo(0.35, 0.45)
        } else {
          dCallOf(w, v, 0.7)
        }
        stabs += 1
      }
      if stabs >= 3 { victimDown(w, dt) }
      if pt > 1.25 { next() }
    case 5:
      relax(a, dt)
      if victimDown(w, dt) && pt > 0.3 { next() }
    case 6:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.0)
      if pt > 1.0 { next() }
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

/// Talon strike: it flies a wide loop and comes in low, feet first, and hits the victim
/// like an eagle; it lands, and finishes it with its beak.
final class DPteroTalons: DBattle {
  private var g = DGlide()
  private var offset = vec3(0), hitAt = vec3(0), flightDir = vec3(0), landAt = vec3(0)
  private var stabs = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let side = vec3(-dir.z, 0, dir.x)
    switch phase {
    case 0:
      shotExtra = 1.3
      shotHeight = 0.6
      if faceOffD(w) { next() }
    case 1:  // up, and out to the side
      if first {
        let up: vec3 = A + side * 0.9 + dir * 0.2 + vec3(0, 1.1, 0)
        g.set(a.pos, up, 0.9)
        w.sound(.flap, 0.8, a.pos, 0.95)
        w.fx.dust(A, 10, 0.4, 0.9)
      }
      dAirborne(a, dt, 15.0, -0.15)
      g.update(a, dt)
      dVictimGuard(v, dSeg(pt, 0.2, 0.8))
      brace(w, 0.6)
      if pt > 0.9 { next() }
    case 2:  // round behind, and down low for the run in
      if first {
        let low: vec3 = V - dir * 1.5 + side * 0.2 + vec3(0, 0.55, 0)
        g.set(a.pos, low, 1.0, 0.2)
      }
      dAirborne(a, dt, 9.0, 0.0)
      g.update(a, dt)
      if pt > 0.7 { a.yawTarget = faceAngle(V - a.pos) }
      v.yawTarget = faceAngle(a.pos - V)
      if pt > 1.0 { next() }
    case 3:  // the strike: feet thrust forward, wings back-beating at the last moment
      let dur: Float = 0.5
      if first {
        a.ctl.tuck = 1.0
        offset = bonePoint(a, PTB.rshin, Dino.clawPoint(Kind.knight)) - a.pos
        hitAt = dChestOf(v) * 0.6 + dNeckOf(v) * 0.4
        g.set(a.pos, hitAt - offset - dir * 0.03, dur)
        flightDir = flatDir(g.p0, g.p1)
        dCallOf(w, a, 0.8)
        w.sound(.whoosh, 0.7, a.pos, 0.8)
      }
      let u = min(1.0, pt / dur)
      a.pos = mix(g.p0, g.p1, u)
      a.yawTarget = faceAngle(flightDir)
      a.ctl.tuck = 1.0
      a.ctl.hover = 1.0
      // at the last moment the wings are thrown up and half folded (clear of the victim's
      // body): the beat held at the top of an upstroke
      let flare = dSeg(u, 0.5, 0.85)
      a.ctl.wings = 1.0 - 0.5 * flare
      a.ctl.flap += dt * (u > 0.7 ? 16.0 : 4.0) * (1.0 - flare)
      let top: Float = kPI * 0.5 + kTau * ceil((a.ctl.flap - kPI * 0.5) / kTau)
      a.ctl.flap = mix(a.ctl.flap, top, flare)
      a.ctl.lean = -0.3 * u
      a.ctl.jaw = 0.6
      brace(w, 1.0)
      if u >= 1.0 {
        dWound(w, hitAt, flightDir + vec3(0, 0.3, 0), 0.8, DWound.claw)
        dFeathers(w, v, hitAt, flightDir, 12)
        w.sound(.thud, 0.9, hitAt, 1.2)
        knockFlying(w, flightDir * 2.0 + vec3(0, 1.6, 0), 8.0)
        w.slowmo(0.35, 0.5)
        w.shake(0.25)
        next()
      }
    case 4:  // down after it
      if first {
        landAt = dClampBoard(flat(a.pos) + flightDir * 0.55, 4.0)
        g.set(a.pos, landAt, 0.55, 0.15)
      }
      let down = g.update(a, dt, false)
      let u = dSeg(pt, 0.0, 0.55)
      a.ctl.flap += dt * 13.0
      a.ctl.wings = approach(a.ctl.wings, 1.0, dt * 3.0)  // (open again to come down)
      a.ctl.hover = 1.0 - dSeg(u, 0.4, 1.0)
      a.ctl.tuck = 1.0
      a.ctl.lean = -0.35
      victimDown(w, dt)
      if down {
        a.pos = landAt
        w.sound(.step, 0.5, landAt, 1.5)
        w.fx.dust(landAt, 8, 0.3, 0.6)
        next()
      }
    case 5:  // the wings folded, it stabs down at the fallen
      let u = dSeg(pt, 0.0, 0.4)
      a.ctl.tuck = 1.0 - u
      a.ctl.lean = -0.35 * (1.0 - u)
      a.ctl.wings = 1.0 - u
      a.ctl.hover = 0
      let down = victimDown(w, dt)
      var body = v.pos
      if let cp = w.puppet(tumble.corpse >= 0 ? tumble.corpse : fall.corpse) { body = cp.pos }
      if pt > 0.3 { a.yawTarget = faceAngle(body - a.pos) }
      if pt > 0.5 {
        let k = fmod(pt - 0.5, 0.45) / 0.45
        dLunge(a, k, 1.0)
        a.ctl.headPitch += 0.5
        a.ctl.jaw = 0.05
        if stabs < 2 && pt > 0.72 + 0.45 * Float(stabs) {
          let spray: vec3 = vec3(0, 1, 0) - flatDir(a.pos, body)
          dBloodSpray(w, flat(body) + vec3(0, 0.08, 0), spray, 25, 1.6)
          w.sound(.gore, 0.6, body, 1.2)
          stabs += 1
        }
      }
      if down && stabs >= 2 && pt > 1.5 { next() }
    case 6:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.0)
      if pt > 1.0 { next() }
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

// ===========================================================================
// BISHOP - the triceratops
// ===========================================================================
/// Horn charge: it paws the ground and snorts, lowers its horns and charges; the victim is
/// gored and tossed (the big ones bowled over).
final class DTrikeCharge: DBattle {
  private var snorted = false
  private var skid: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.8
      if faceOffD(w) { next() }
    case 1:  // pawing the ground, snorting, the horns lowered
      if first { dWarnOf(w, a) }
      if !snorted && pt > 0.6 {
        snorted = true
        dWarnOf(w, a)
      }
      dThreat(a, dSeg(pt, 0.0, 0.5))
      a.ctl.gait += dt * 7.0
      a.ctl.stride = 0.3
      if fmod(pt, 0.3) < dt { w.fx.dust(flat(a.pos) + a.forward() * 0.2, 3, 0.15, 0.5) }
      dVictimGuard(v, dSeg(pt, 0.3, 0.9))
      brace(w, 0.8)
      if pt > 1.15 { next() }
    case 2:  // the charge
      if first {
        walker = DWalker(plan.mover, V + dir * 0.3, max(2.5, L / 0.8))
        walker.run = 1.0
        walker.face = true
        dCallOf(w, a, 0.9)
      }
      a.ctl.stride = max(a.ctl.stride, 0.3)
      dThreat(a, 1.0)
      a.ctl.headPitch = 0.42
      walker.update(w, dt)
      brace(w, 1.0)
      let hitRange: Float = dReachOf(a) * 0.85 + dHalfWidthOf(v) * 0.5
      if length(flat(V - a.pos)) <= hitRange {
        let c: vec3 = dChestOf(v) * 0.5 + dNeckOf(v) * 0.5
        dWound(w, c, dir + vec3(0, 0.5, 0), 1.2, DWound.gore)
        dFeathers(w, v, c, dir, 14)
        knockFlying(w, dir * 2.2 + vec3(0, 3.0, 0), 9.0)
        w.slowmo(0.3, 0.5)
        w.shake(0.35)
        skid = walker.v
        next()
      }
    case 3:  // the toss of the head; skidding to a halt
      a.ctl.headPitch = dKf(pt, [vec2(0.0, 0.42), vec2(0.18, -0.45), vec2(0.7, 0.0)])
      a.ctl.neck = dKf(pt, [vec2(0.0, -0.2), vec2(0.18, 0.4), vec2(0.7, 0.0)])
      skid = approach(skid, 0.0, dt * 5.0)
      a.pos += dir * skid * dt
      a.pos = dClampBoard(a.pos, 4.0)
      a.ctl.gait += skid * dt / (Dino.strideCycle(a.kind) * a.scale) * kTau
      a.ctl.stride = min(1.0, skid)
      if fmod(pt, 0.1) < dt && skid > 0.3 { w.fx.dust(flat(a.pos), 2, 0.2, 0.5) }
      let down = victimDown(w, dt)
      if down && pt > 0.8 { next() }
    case 4:
      if first { dCallOf(w, a) }
      triumph(w, pt / 1.2)
      if pt > 1.2 { next() }
    case 5:
      relax(a, dt)
      if moveInD(w, dt) { next() }
    case 6:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

/// Horn lock: the victim bites at it and its jaws close on the frill; the triceratops
/// drives it back across the board, twists its head and gores it.
final class DTrikeLock: DBattle {
  private var blocked = false, gored = false
  private var pushDist: Float = 0, pushed: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOffD(w) { next() }
    case 1:  // in close, the horns lowered
      if first {
        let back: Float = dReachOf(a) * 0.8 + dReachOf(v) * 0.35 + dHalfWidthOf(v) * 0.3 + 0.06
        walker = DWalker(plan.mover, V - dir * back, 1.0)
      }
      dThreat(a, dSeg(pt, 0.0, 0.5))
      dVictimGuard(v, dSeg(pt, 0.0, 0.6))
      brace(w, 0.7)
      if walker.update(w, dt) { next() }
    case 2:  // the victim strikes; its jaws close on the frill
      let u = dSeg(pt, 0.0, 0.8)
      dVictimStrike(v, u)
      dThreat(a, 1.0)
      a.ctl.headPitch = 0.42 + 0.1 * dBump(u, 0.45, 0.7)
      if !blocked && u > 0.5 {
        blocked = true
        let f: vec3 = dHeadOf(a) + vec3(0, 0.1, 0)
        w.sound(.snap, 0.9, f, 0.9)
        w.sound(.thud, 0.7, f, 1.2)
        w.fx.dust(f, 4, 0.1, 0.4)
        a.pos -= dir * 0.03
        dWarnOf(w, a)
        w.shake(0.1)
      }
      if pt > 0.85 { next() }
    case 3:  // the shove: it drives the victim back across the board, feet scrabbling
      if first {
        let far = dClampBoard(V + dir * 0.9, 3.7)
        pushDist = max(0.0, dot(far - V, dir))
        pushed = 0
        dCallOf(w, a, 0.7)
        dWarnOf(w, v, 0.8)
      }
      let spd: Float = 0.55 * dBump(pt, 0.0, 1.3) + 0.1
      let d = min(spd * dt, pushDist - pushed)
      pushed += d
      a.pos += dir * d
      v.pos += dir * d
      let walked: Float = d / (Dino.strideCycle(a.kind) * a.scale) * kTau
      a.ctl.gait += walked + dt * 2.0
      a.ctl.stride = 0.7
      a.ctl.lean = 0.08
      a.ctl.crouch = 0.2
      dThreat(a, 1.0)
      v.ctl.gait -= dt * 12.0
      v.ctl.stride = 0.6
      v.ctl.headYaw = 0.45 * sin(pt * 8.0)
      v.ctl.jaw = 0.7
      v.tremble = 0.6
      if fmod(pt, 0.15) < dt { w.fx.dust(flat(v.pos), 2, 0.2, 0.5) }
      if fmod(pt, 0.4) < dt { dFootfall(w, a, 0.8) }
      if pt > 1.3 { next() }
    case 4:  // the twist, and the horns go in
      let u = dSeg(pt, 0.0, 0.5)
      a.ctl.headYaw = dKf(u, [vec2(0.0, 0.0), vec2(0.35, -0.45), vec2(0.7, 0.6), vec2(1.0, 0.3)])
      a.ctl.headPitch = dKf(u, [vec2(0.0, 0.42), vec2(0.6, 0.2), vec2(1.0, -0.3)])
      if !gored && u > 0.5 {
        gored = true
        let hook = vec3(dir.z, 0, -dir.x)  // its left: the way the twist of its head carries the horns
        let gore: vec3 = dir + hook * 0.5 + vec3(0, 0.4, 0)
        dWound(w, dChestOf(v), gore, 1.2, DWound.gore)
        dFeathers(w, v, dChestOf(v), dir, 12)
        let push: vec3 = dir * 0.3 + hook * 0.4
        kill(w, 0.0, push, 0.25)
        w.slowmo(0.35, 0.5)
        w.shake(0.25)
      }
      if gored { victimDown(w, dt) }
      if pt > 0.6 { next() }
    case 5:
      relax(a, dt)
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

/// Frill bash: a swing of the great head knocks the victim off its feet; it rears and
/// tramples it.
final class DTrikeBash: DBattle {
  private var hit = false, stamped = false
  private var swingSide: Float = 1
  private var body = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOffD(w) { next() }
    case 1:
      if first {
        let back: Float = dReachOf(a) * 0.55 + dReachOf(v) * 0.3 + dHalfWidthOf(v) * 0.6 + 0.05
        walker = DWalker(plan.mover, V - dir * back, 1.0)
      }
      dThreat(a, dSeg(pt, 0.0, 0.5))
      dVictimGuard(v, dSeg(pt, 0.0, 0.6))
      brace(w, 0.7)
      if walker.update(w, dt) { next() }
    case 2:  // the wind-up: the head swung away
      if first {
        swingSide = w.fx.rnd() < 0.5 ? 1.0 : -1.0
        dWarnOf(w, a)
      }
      let u = dSeg(pt, 0.0, 0.4)
      a.ctl.headYaw = -0.7 * swingSide * easeOut(u)
      a.ctl.headPitch = 0.2
      a.ctl.twist = -0.15 * swingSide * u
      dVictimStrike(v, dSeg(pt, 0.0, 0.5) * 0.4)
      if pt > 0.45 { next() }
    case 3:  // the bash
      let u = dSeg(pt, 0.0, 0.2)
      a.ctl.headYaw = mix(-0.7, 0.95, easeIn(u)) * swingSide
      a.ctl.twist = mix(-0.15, 0.2, u) * swingSide
      if !hit && u > 0.55 {
        hit = true
        let left = vec3(cos(a.yaw), 0, -sin(a.yaw))
        let sweep: vec3 = left * swingSide
        w.sound(.thud, 1.0, dHeadOf(v), 0.9)
        dWound(w, dNeckOf(v), sweep + vec3(0, 0.3, 0), 0.6, DWound.gore)
        dFeathers(w, v, dNeckOf(v), sweep, 10)
        let fly: vec3 = sweep * 1.7 + dir * 0.4 + vec3(0, 1.6, 0)
        knockFlying(w, fly, 7.0)
        w.shake(0.3)
        w.slowmo(0.35, 0.4)
      }
      if hit { victimDown(w, dt) }
      if pt > 0.4 { next() }
    case 4:  // up to the fallen
      let down = victimDown(w, dt)
      a.ctl.headYaw = approach(a.ctl.headYaw, 0.0, dt * 2.0)
      a.ctl.twist = approach(a.ctl.twist, 0.0, dt * 2.0)
      if down {
        body = v.pos
        if let cp = w.puppet(tumble.corpse >= 0 ? tumble.corpse : fall.corpse) { body = cp.pos }
        next()
      }
    case 5:  // to the body
      if first {
        let stop: vec3 = body - flatDir(a.pos, body) * 0.3 * a.scale
        walker = DWalker(plan.mover, stop, 1.0)
      }
      relax(a, dt)
      if walker.update(w, dt) { next() }
    case 6:  // it rears, and tramples
      let u = dSeg(pt, 0.0, 0.75)
      a.ctl.rear = dKf(u, [vec2(0.0, 0.0), vec2(0.6, 0.75), vec2(0.82, 0.0)])
      a.ctl.jaw = 0.5 * dBump(u, 0.2, 0.8)
      if !stamped && u > 0.82 {
        stamped = true
        let f: vec3 = flat(a.pos) + a.forward() * 0.2
        w.sound(.quake, 0.9, f, 1.0)
        w.sound(.crunch, 0.9, f, 0.9)
        dBloodSpray(w, body + vec3(0, 0.06, 0), vec3(0, 1, 0), 40, 2.2, 0.7)
        dSplatter(w, body, a.forward(), 4, 0.35)
        w.fx.dust(f, 18, 0.35, 1.0)
        w.shake(0.35)
      }
      if pt > 1.0 { next() }
    case 7:
      if first { dCallOf(w, a) }
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
