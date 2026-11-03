// Roman battles of the siege tower (battering ram, sally, scorpion), of the
// empress (hidden dagger, poisoned cup, whirling spear) and of the emperor
// (thumbs down, gladius duel, the lion). (As battle_roman.cpp; the toolkit is in
// BattlesRoman.swift.)
import Foundation
import simd

// ===========================================================================
// ROOK - the siege tower
// ===========================================================================
/// A puppet soldier (the tower's garrison) running about.
struct RTrooper {
  var id = -1
  var target = vec3(0)
  var speed: Float = 1.4
  var arrived = false

  mutating func spawn(_ w: World, _ color: Int, _ at: vec3, _ yaw: Float) {
    id = w.addPuppet(RAsset.pawn, color, 3)
    if let p = w.puppet(id) {
      p.pos = flat(at)
      p.yaw = yaw
      p.dissolve = 1.0
      p.ctl.draw = 1
      p.ctl.idle = w.fx.rnd(0.0, 50.0)
    }
  }
  func get(_ w: World) -> Puppet? { w.puppet(id) }
  mutating func run(_ w: World, _ dt: Float) -> Bool {
    guard let p = get(w) else { return true }
    let d = flat(target - p.pos)
    let l = length(d)
    if l < 0.01 {
      p.ctl.stride = approach(p.ctl.stride, 0.0, dt * 4.0)
      arrived = true
      return p.ctl.stride <= 0.001
    }
    let stp = min(l, speed * dt)
    p.pos += d / l * stp
    let want = faceAngle(d)
    p.yaw = wrapAngle(p.yaw + clamp(wrapAngle(want - p.yaw), -dt * 10.0, dt * 10.0))
    p.ctl.gait += stp / Roman.strideCycle(Kind.pawn) * kTau * 0.8
    p.ctl.stride = 1.0
    p.ctl.lean = 0.12
    p.ctl.cape = 0.5
    // (the C++ int() truncation, compared as floats)
    let now = (p.ctl.gait / kPI).rounded(.towardZero)
    let before = ((p.ctl.gait - stp / Roman.strideCycle(Kind.pawn) * kTau * 0.8) / kPI).rounded(.towardZero)
    if now != before { w.sound(.armor, 0.25, p.pos, w.fx.rnd(1.0, 1.2)) }
    return false
  }
}

/// Battering ram: the tower rolls up, the door drops, and the ram swings out.
final class RTowerRam: RBattle {
  private var hit = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.0
      shotHeight = 0.5
      if first { w.sound(.cornu, 0.6, a.pos) }
      if faceOffR(w) { next() }
    case 1:  // roll up within reach
      if enter() { walker = RWalker(plan.mover, V - dir * min(L, v.radius() + 0.47), .tower, max(0.8, L / 2.6)) }
      rVictimGuard(v, rSeg(pt, 0.0, 0.8))
      brace(w, 0.8)
      if walker.update(w, dt) { next() }
    case 2:  // the door drops
      if enter() {
        w.sound(.chain, 0.8, a.pos, 1.1)
        w.sound(.creak, 0.6, a.pos, 0.9)
      }
      a.ctl.gate = easeIn(rSeg(pt, 0.0, 0.5))
      if pt > 0.5 {
        w.sound(.thud, 0.6, a.pos + dir * 0.4, 1.1)
        w.fx.dust(a.pos + dir * 0.45, 10, 0.2, 0.7)
        next()
      }
    case 3:  // heave back...
      if enter() { w.sound(.creak, 0.7, a.pos, 0.75) }
      a.ctl.kick = -0.35 * easeInOut(rSeg(pt, 0.0, 0.6))
      a.ctl.lean = -0.015 * rSeg(pt, 0.0, 0.6)
      brace(w, 1.0)
      if pt > 0.65 { next() }
    case 4:  // ...and swing!
      let u = easeIn(rSeg(pt, 0.0, 0.2))
      a.ctl.kick = mix(-0.35, 1.0, u)
      a.ctl.lean = mix(-0.015, 0.02, u)
      if !hit && u >= 1.0 {
        hit = true
        let c = rModelPoint(a, Roman.ramTip())
        w.sound(.ram, 1.0, c)
        w.sound(.grunt, 0.5, a.pos + vec3(0, 0.3, 0), 0.8)
        w.shake(0.55)
        w.slowmo(0.3, 0.5)
        w.fx.dust(flat(c), 16, 0.3, 1.2)
        if !rIsTower(v) {
          rBloodSpray(w, rChestOf(v), dir + vec3(0, 0.6, 0), 45, 3.0)
          w.sound(.blood, 0.6, c)
        }
        knockFlying(w, dir * 2.4 + vec3(0, 2.8, 0), 7.5)
      }
      if pt > 0.25 { next() }
    case 5:  // the ram swings back in, the door is raised
      a.ctl.kick = 1.0 - easeInOut(rSeg(pt, 0.0, 0.8))
      a.ctl.lean = 0.02 * (1.0 - rSeg(pt, 0.0, 0.4))
      a.ctl.gate = 1.0 - easeInOut(rSeg(pt, 0.6, 1.2))
      if pt > 0.6 && pt - dt <= 0.6 { w.sound(.chain, 0.6, a.pos, 1.2) }
      if victimDown(w, dt) && pt > 1.25 { next() }
    case 6:
      if moveInR(w, dt) { next() }
    case 7:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// Sally: the door drops and two legionaries of the garrison charge out; one takes
/// the victim's blow on his shield, the other cuts it down; back they go.
final class RTowerSally: RBattle {
  private var t1 = RTrooper(), t2 = RTrooper()
  private var blocked = false, stabbed = false
  private var door = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let side = vec3(-dir.z, 0, dir.x)
    switch phase {
    case 0:
      shotExtra = 1.1
      shotHeight = 0.5
      if first { w.sound(.cornu, 0.6, a.pos, 1.05) }
      if faceOffR(w) { next() }
    case 1:
      if enter() { walker = RWalker(plan.mover, V - dir * min(L, 1.3), .tower, max(0.8, L / 2.6)) }
      rVictimGuard(v, rSeg(pt, 0.0, 0.8))
      brace(w, 0.6)
      if walker.update(w, dt) { next() }
    case 2:  // the door and the bridge drop; the garrison roars
      if enter() {
        w.sound(.chain, 0.8, a.pos, 1.05)
        w.sound(.shout, 0.7, a.pos + vec3(0, 1.0, 0), 0.95)
      }
      a.ctl.gate = easeIn(rSeg(pt, 0.0, 0.5))
      a.ctl.bridge = easeIn(rSeg(pt, 0.1, 0.6))
      if pt > 0.6 {
        w.sound(.thud, 0.6, a.pos + dir * 0.4, 1.1)
        door = rModelPoint(a, Roman.doorPoint())
        t1.spawn(w, a.color, door + side * 0.05 - dir * 0.15, a.yaw)
        t2.spawn(w, a.color, door - side * 0.05 - dir * 0.15, a.yaw)
        t1.target = V - dir * 0.43 + side * 0.2
        t2.target = V - dir * 0.2 - side * 0.36
        next()
      }
    case 3:  // they charge out
      if enter() { w.sound(.shout, 0.8, door, 1.08) }
      for t in [t1, t2] {
        if let p = t.get(w) { p.dissolve = max(0.0, p.dissolve - dt * 3.0) }
      }
      let d1 = t1.run(w, dt)
      let d2 = pt > 0.15 ? t2.run(w, dt) : false
      rVictimGuard(v, 1.0)
      brace(w, 0.9)
      if rIsHuman(v) { v.yawTarget = faceAngle(t1.target - V) }
      if d1 && d2 { next() }
    case 4:  // the victim strikes at the first; his shield takes it; the second stabs
      let p1 = t1.get(w)
      let p2 = t2.get(w)
      let u = rSeg(pt, 0.0, 0.85)
      if let p1 {
        p1.yaw = faceAngle(V - p1.pos)
        rGuard(&p1.ctl, 1.0, true)
        rBlock(&p1.ctl, rBump(u, 0.2, 0.8))
      }
      if !rIsTower(v) { rVictimStrike(v, u) }
      if !blocked && u > 0.55, let p1 {
        blocked = true
        let s = puppetPoint(p1, RH.itemL, Roman.handL(Kind.pawn)) + vec3(0, 0.08, 0)
        if !rIsTower(v) {
          w.sound(.shield, 1.0, s)
          rSparksAt(w, s, vec3(0, 1, 0), 12)
        }
      }
      if let p2 {
        p2.yaw = faceAngle(V - p2.pos)
        let s2 = rSeg(pt, 0.45, 1.2)
        rThrust(&p2.ctl, s2)
        if !stabbed && s2 > 0.52 {
          stabbed = true
          let c = rChestOf(v) - vec3(0, 0.06, 0)
          let d = flatDir(p2.pos, V)
          rWound(w, c, d + vec3(0, 0.15, 0), rIsTower(v) ? 0.2 : 1.1)
          kill(w, 1.0, (dir + d) * 0.25, 0.5)
          w.shake(0.25)
          w.slowmo(0.3, 0.45)
        }
      }
      if stabbed { victimDown(w, dt) }
      if pt > 1.25 { next() }
    case 5:  // they cheer
      for t in [t1, t2] {
        if let p = t.get(w) {
          p.ctl.armR = mix(p.ctl.armR, vec3(2.6, 0.3, 0.3), min(1.0, dt * 5.0))
          p.ctl.itemPitch = approach(p.ctl.itemPitch, 0.0, dt * 3.0)
          p.ctl.armL = mix(p.ctl.armL, vec3(0.5, 0.05, -0.15), min(1.0, dt * 5.0))
        }
      }
      if enter() { w.sound(.shout, 0.8, V, 1.0) }
      if victimDown(w, dt) && pt > 0.8 {
        t1.target = door - dir * 0.15
        t2.target = door - dir * 0.15
        t1.arrived = false
        t2.arrived = false
        next()
      }
    case 6:  // and back inside
      for t in [t1, t2] {
        if let p = t.get(w) {
          p.ctl.armR = mix(p.ctl.armR, vec3(0), min(1.0, dt * 4.0))
          if length(flat(p.pos - door)) < 0.25 { p.dissolve = min(1.0, p.dissolve + dt * 4.0) }
        }
      }
      let d1 = t1.run(w, dt)
      let d2 = t2.run(w, dt)
      if d1 && d2 {
        for t in [t1, t2] {
          if let p = t.get(w) { p.alive = false }
        }
        next()
      }
    case 7:  // the door goes up
      if enter() { w.sound(.chain, 0.7, a.pos, 1.15) }
      a.ctl.gate = 1.0 - easeInOut(rSeg(pt, 0.0, 0.6))
      a.ctl.bridge = 1.0 - easeInOut(rSeg(pt, 0.0, 0.6))
      if pt > 0.65 { next() }
    case 8:
      if moveInR(w, dt) { next() }
    case 9:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// Scorpion: the bolt-thrower on the roof swings round, takes aim and looses.
final class RTowerScorpion: RBattle {
  private var bolt = RMissile()
  private var stuck = RStuck()
  private var yaw0: Float = 0, pitch0: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    if stuck.puppet >= 0 { stuck.update(w, fall.corpse >= 0 ? fall.corpse : tumble.corpse) }
    switch phase {
    case 0:
      shotExtra = 1.4
      shotHeight = 0.8
      if faceOffR(w) { next() }
    case 1:  // aim
      if enter() {
        w.sound(.creak, 0.6, a.pos + vec3(0, 1.2, 0), 1.2)
        w.sound(.shout, 0.5, a.pos + vec3(0, 1.2, 0), 1.1)
        let s = rModelPoint(a, Roman.scorpionPoint())
        let t = rChestOf(v)
        let d = length(flat(t - s))
        pitch0 = atan2(s.y - t.y, d)
        yaw0 = wrapAngle(faceAngle(t - s) - a.yaw)
      }
      let u = easeInOut(rSeg(pt, 0.0, 0.9))
      a.ctl.itemYaw = yaw0 * u
      a.ctl.itemPitch = pitch0 * u + 0.05 * sin(pt * 30.0) * (1.0 - u)
      rVictimGuard(v, rSeg(pt, 0.2, 0.9))
      brace(w, 0.9)
      if pt > 1.1 { next() }
    case 2:  // loose!
      if enter() {
        a.ctl.hatch = 1
        let s = rModelPoint(a, Roman.scorpionPoint())
        let t = rChestOf(v)
        bolt.launch(w, RAsset.bolt, a.color, s, t, length(t - s) / 7.5, 0.32, 3.0)
        w.sound(.scorpion, 1.0, s)
        w.shake(0.12)
      }
      a.ctl.lean = -0.02 * rBump(pt, 0.0, 0.3)
      brace(w, 1.0)
      if bolt.update(w, dt) {
        let hd = bolt.heading()
        let M = rPropMatrix(bolt.tip() + hd * 0.1 - hd * 0.32, hd)
        if let p = w.puppet(bolt.puppet) { rSetPuppetMatrix(p, M) }
        stuck.attach(w, bolt.puppet, v, M)
        rWound(w, rChestOf(v), flat(hd) + vec3(0, 0.3, 0), rIsTower(v) ? 0.0 : 1.2)
        if rIsHuman(v) {
          w.sound(.stab, 0.8, v.pos, 0.8)
          knockFlying(w, flat(hd) * 1.6 + vec3(0, 1.6, 0), 3.2)
        } else {
          knockFlying(w, flat(hd) * 1.5, 0.0)
        }
        w.slowmo(0.3, 0.5)
        next()
      }
    case 3:  // the crew cheers; a new bolt
      if enter() { w.sound(.shout, 0.7, a.pos + vec3(0, 1.2, 0), 1.0) }
      a.ctl.lean = 0
      if pt > 1.0 { a.ctl.hatch = 0 }
      a.ctl.itemYaw = yaw0 * (1.0 - easeInOut(rSeg(pt, 0.6, 1.4)))
      a.ctl.itemPitch = pitch0 * (1.0 - easeInOut(rSeg(pt, 0.6, 1.4)))
      if victimDown(w, dt) && pt > 1.4 { next() }
    case 4:
      if moveInR(w, dt) { next() }
    case 5:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// QUEEN - the empress
// ===========================================================================
/// Hidden dagger: she holds out her hand in friendship; as the victim bows, she
/// draws a dagger from her palla and stabs, and stabs again.
final class REmpressDagger: RBattle {
  private var s1 = false, s2 = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOffR(w, 0.3) { next() }
    case 1:  // she walks up, smiling
      if enter() { walker = RWalker(plan.mover, V - dir * (touchDist(a, v) * 1.15), .foot, max(0.8, L / 1.8)) }
      v.tremble = 0
      v.idleWeight = 0
      if walker.update(w, dt) { next() }
    case 2:  // her hand held out; the victim bows
      let u = easeInOut(rSeg(pt, 0.0, 0.6))
      a.ctl.armL = vec3(1.0, 0.15, -0.25) * u
      a.ctl.headPitch = 0.1 * u
      if rIsHuman(v) {
        v.ctl.lean = 0.3 * easeInOut(rSeg(pt, 0.3, 0.9))
        v.ctl.headPitch = 0.3 * easeInOut(rSeg(pt, 0.3, 0.9))
      } else if rIsChariot(v) {
        v.ctl.lean = 0.25 * easeInOut(rSeg(pt, 0.3, 0.9))
      }
      if pt > 1.0 { next() }
    case 3:  // the dagger
      if enter() {
        a.ctl.draw = 1
        w.sound(.draw, 0.4, a.pos, 1.4)
      }
      let u1 = rSeg(pt, 0.0, 0.55), u2 = rSeg(pt, 0.6, 1.15)
      rThrust(&a.ctl, pt < 0.6 ? u1 : u2)
      let offered: vec3 = vec3(1.0, 0.15, -0.25) * (1.0 - u1)
      a.ctl.armL = offered + vec3(0.6, 0.0, 0.4) * u1
      if !s1 && u1 > 0.52 {
        s1 = true
        var c = rHeadOf(v) - vec3(0, 0.13, 0) * v.scale
        if rIsChariot(v) || rIsTower(v) { c = rChestOf(v) }
        rWound(w, c, dir + vec3(0, 0.3, 0), rIsTower(v) ? 0.2 : 0.9)
        w.sound(.death, 0.5, c, 1.25)
        v.ctl.lean = -0.1
        v.ctl.headPitch = -0.3
      }
      if !s2 && u2 > 0.52 {
        s2 = true
        let c = rChestOf(v)
        rWound(w, c, dir + vec3(0, 0.1, 0), rIsTower(v) ? 0.2 : 1.0)
        kill(w, 1.0, dir * 0.25, 0.8)
        w.slowmo(0.35, 0.45)
      }
      if s2 { victimDown(w, dt) }
      if pt > 1.2 { next() }
    case 4:  // she wipes her blade on her palla
      let u = rSeg(pt, 0.0, 0.8)
      a.ctl.armR = mix(vec3(0.45, 0.1, 0.45), vec3(0.5, -0.5, 0.8), rBump(u, 0.0, 1.0))
      a.ctl.armL = vec3(0.4, 0.1, 0.6) * (1.0 - u)
      a.ctl.lean = 0.05 * rBump(u, 0.0, 1.0)
      a.ctl.headPitch = 0.15 * rBump(u, 0.0, 1.0)
      a.ctl.itemPitch = 0.6
      if victimDown(w, dt) && pt > 1.0 {
        a.ctl.draw = 0
        next()
      }
    case 5:
      a.ctl.armR = mix(a.ctl.armR, vec3(0), min(1.0, dt * 4.0))
      a.ctl.itemPitch = approach(a.ctl.itemPitch, 0.0, dt * 2.0)
      if moveInR(w, dt) { next() }
    case 6:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// The poisoned cup: she offers the victim her goblet; it drinks, chokes, and falls.
final class REmpressCup: RBattle {
  private var cup = -1
  private var cupVel = vec3(0)
  private var dropped = false, gulped = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    if let c = w.puppet(cup) {
      if !dropped {  // in the victim's hand
        c.pos = rHandOf(v, true) + vec3(0, -0.005, 0)
        c.yaw = v.yaw
        let tip: Float = rIsChariot(v) ? 0.0 : -0.9 * clamp(v.ctl.armR.z / 1.9, 0.0, 1.0)
        c.tilt = angleAxis(tip, vec3(1, 0, 0))
      } else {  // falling, and rolling on the board
        cupVel.y -= 9.8 * dt
        c.pos += cupVel * dt
        if c.pos.y < 0.012 {
          c.pos.y = 0.012
          if cupVel.y < -0.5 { w.sound(.parry, 0.25, c.pos, 1.8) }
          cupVel = vec3(cupVel.x * 0.5, -cupVel.y * 0.3, cupVel.z * 0.5)
          c.tilt = angleAxis(1.5708, vec3(0, 0, 1))
        }
      }
    }
    switch phase {
    case 0:
      if faceOffR(w, 0.3) { next() }
    case 1:
      if enter() { walker = RWalker(plan.mover, V - dir * (touchDist(a, v) * 1.25), .foot, max(0.8, L / 1.8)) }
      v.tremble = 0
      v.idleWeight = 0
      if walker.update(w, dt) { next() }
    case 2:  // she offers the cup
      a.ctl.hatch = 1
      let u = easeInOut(rSeg(pt, 0.0, 0.6))
      a.ctl.armL = vec3(1.05, 0.05, -0.35) * u
      a.ctl.headPitch = 0.1 * u
      if pt > 0.8 { next() }
    case 3:  // the victim takes it
      let u = easeInOut(rSeg(pt, 0.0, 0.5))
      v.ctl.itemR = 0
      v.ctl.draw = 0
      v.ctl.armR = vec3(0.75, 0.0, 0.3) * u
      if cup < 0 && pt > 0.3 {
        a.ctl.hatch = 0
        cup = w.addPuppet(RAsset.goblet, a.color, 3)
        w.sound(.parry, 0.1, rHandOf(a, false), 2.4)
      }
      a.ctl.armL = vec3(1.05, 0.05, -0.35) * (1.0 - rSeg(pt, 0.3, 0.6))
      if pt > 0.6 { next() }
    case 4:  // it drinks, deep
      let u = easeInOut(rSeg(pt, 0.0, 0.5))
      v.ctl.armR = mix(vec3(0.75, 0.0, 0.3), vec3(1.3, -0.25, 1.9), u)
      v.ctl.headPitch = -0.45 * easeInOut(rSeg(pt, 0.3, 0.8))
      v.ctl.lean = -0.08 * u
      if !gulped && pt > 0.5 {
        gulped = true
        w.sound(.gulp, 0.8, rHeadOf(v))
      }
      a.ctl.armL = vec3(0.3, 0.1, 0.6) * rSeg(pt, 0.0, 0.5)
      if pt > 1.3 { next() }
    case 5:  // the poison: the cup falls, it clutches its throat, chokes, and falls on its face
      if !dropped && pt > 0.1 {
        dropped = true
        cupVel = v.forward() * 0.4 + vec3(0, 0.3, 0)
        w.sound(.grunt, 0.6, v.pos, 0.7)
      }
      let u = easeInOut(rSeg(pt, 0.1, 0.4))
      if rIsHuman(v) {
        v.ctl.armR = mix(vec3(1.3, -0.25, 1.9), vec3(2.0, -0.5, 1.7), u)
        v.ctl.armL = mix(v.ctl.armL, vec3(2.0, -0.5, 1.7), min(1.0, dt * 6.0))
        v.ctl.headPitch = mix(-0.45, 0.2, u)
        v.ctl.lean = 0.25 * u + 0.08 * sin(pt * 31.0) * u
        v.ctl.crouch = 0.3 * u
      } else if rIsChariot(v) {
        v.ctl.armR = mix(v.ctl.armR, vec3(1.2, 0.0, 1.8), min(1.0, dt * 6.0))
        v.ctl.rear = 0.3 * u
      }
      v.tremble = 1.4 * u
      if pt > 0.5 && fmod(pt, 0.35) < dt { w.sound(.gulp, 0.35, rHeadOf(v), 0.75) }
      if pt > 1.5 {
        fall.bleed = 0.5
        kill(w, -1.0, dir * 0.1, 0.25)
        if let cp = w.puppet(cup) {
          cp.life = 2.0
          rBloodPool(w, cp.pos, 0.06, 0.4, 6.0)  // the wine spilled
        }
        next()
      }
    case 6:
      a.ctl.armL = mix(a.ctl.armL, vec3(0), min(1.0, dt * 2.0))
      if victimDown(w, dt) && pt > 0.6 { next() }
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

/// Whirling spear: she twirls her spear, sweeps the victim's legs from under it so
/// that it flips through the air, and spears it where it lands.
final class REmpressSpear: RBattle {
  private var spin: Float = 0, whirl: Float = 0
  private var swept = false, speared = false
  private var body = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.7
      if faceOffR(w) { next() }
    case 1:  // the spear twirls; she advances
      if enter() { walker = RWalker(plan.mover, V - dir * min(L, 0.62), .foot, max(0.9, L / 2.0)) }
      whirl = approach(whirl, 17.0, dt * 18.0)
      let prev = spin
      spin += whirl * dt
      // (the C++ int() truncation, compared as floats)
      if (spin / kPI).rounded(.towardZero) != (prev / kPI).rounded(.towardZero) {
        w.sound(.whoosh, 0.45, a.pos, 1.2)
      }
      a.ctl.armR = vec3(0.9, 0.2, 0.2)
      a.ctl.itemPitch = 1.5708 + spin
      rVictimGuard(v, rSeg(pt, 0.0, 0.5))
      brace(w, 0.8)
      if walker.update(w, dt) && pt > 1.0 { next() }
    case 2:  // the sweep, low
      let u = rSeg(pt, 0.0, 0.45)
      spin += whirl * dt * (1.0 - u)
      a.ctl.crouch = 0.55 * rBump(u, 0.0, 1.3)
      a.ctl.twist = mix(0.6, -0.7, u)
      a.ctl.lean = 0.25 * rBump(u, 0.0, 1.3)
      a.ctl.armR = mix(vec3(0.9, 0.6, 0.2), vec3(0.8, -0.4, 0.0), u)
      a.ctl.itemPitch = mix(fmod(1.5708 + spin, kTau), 2.3, u)
      a.ctl.itemYaw = mix(1.0, -1.0, u)
      if !swept && u > 0.55 {
        swept = true
        w.sound(.whoosh, 0.9, V, 0.8)
        w.sound(.thud, 0.8, V, 1.3)
        w.fx.dust(V, 12, 0.25, 0.9)
        if rIsHuman(v) {
          tumble.life = 3.0
          tumble.launch(w, v, vec3(0, 3.6, 0) + dir * 0.15, 10.5)  // up it goes, head over heels
        } else {
          knockFlying(w, dir * 1.0, 0.0)
        }
        w.slowmo(0.35, 0.6)
      }
      if pt > 0.5 { next() }
    case 3:  // she rises with the spear high, point down, waiting for it to land
      let u = easeInOut(rSeg(pt, 0.0, 0.4))
      a.ctl.crouch = 0
      a.ctl.twist = -0.7 * (1.0 - u)
      a.ctl.armR = mix(vec3(0.8, -0.4, 0.0), vec3(2.5, 0.0, 0.7), u)
      a.ctl.itemPitch = mix(2.3, 3.0, u)
      a.ctl.itemYaw = -1.0 * (1.0 - u)
      let down = victimDown(w, dt)
      if (tumble.landed || down) && pt > 0.4 {
        body = flat(v.alive ? v.pos : V)
        if let cp = w.puppet(tumble.corpse) { body = flat(cp.pos) }
        next()
      }
    case 4:  // and drives it down
      let u = easeIn(rSeg(pt, 0.0, 0.22))
      a.ctl.armR = mix(vec3(2.5, 0.0, 0.7), vec3(0.9, 0.0, -0.3), u)
      a.ctl.lean = 0.35 * u
      a.ctl.crouch = 0.3 * u
      if !speared && u >= 1.0 {
        speared = true
        let c = body + vec3(0, 0.06, 0)
        if rIsHuman(v) || tumble.corpse >= 0 {
          rWound(w, c, vec3(0, 1.3, 0), 1.3)
          w.sound(.death, 0.6, c, 0.8)
        }
        w.shake(0.3)
      }
      victimDown(w, dt)
      if pt > 0.7 { next() }
    case 5:  // she pulls it out and raises it
      let u = easeInOut(rSeg(pt, 0.0, 0.6))
      a.ctl.armR = mix(vec3(0.9, 0.0, -0.3), vec3(2.4, 0.2, 0.3), u)
      a.ctl.itemPitch = mix(3.0, 0.0, u)
      a.ctl.lean = 0.35 * (1.0 - u)
      a.ctl.crouch = 0.3 * (1.0 - u)
      if victimDown(w, dt) && pt > 0.8 { next() }
    case 6:
      if first {
        a.ctl.armR = vec3(0)
        a.ctl.itemPitch = 0
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

// ===========================================================================
// KING - the emperor
// ===========================================================================
/// Thumbs down: the crowd roars, and a volley of pila rains down on the victim.
final class REmperorThumbsDown: RBattle {
  private static let kN = 7
  private var pila = [RMissile](repeating: RMissile(), count: REmperorThumbsDown.kN)
  private var stuck = [RStuck](repeating: RStuck(), count: REmperorThumbsDown.kN)
  private var launchAt = [Float](repeating: 0, count: REmperorThumbsDown.kN)
  private var launched = [Bool](repeating: false, count: REmperorThumbsDown.kN)
  private var landed = [Bool](repeating: false, count: REmperorThumbsDown.kN)
  private var targets = [vec3](repeating: vec3(0), count: REmperorThumbsDown.kN)
  private var killed = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let n = REmperorThumbsDown.kN
    for i in 0..<n where stuck[i].puppet >= 0 { stuck[i].update(w, fall.corpse) }
    switch phase {
    case 0:
      shotExtra = 1.3
      shotHeight = 0.7
      if first { w.sound(.cornu, 0.7, a.pos) }
      if faceOffR(w) { next() }
    case 1:  // his arm out, thumb down; the crowd roars
      if enter() { w.sound(.crowd, 0.9, a.pos) }
      let u = easeInOut(rSeg(pt, 0.0, 0.6))
      a.ctl.armL = vec3(1.55, 0.05, -0.1) * u
      a.ctl.headPitch = -0.1 * u
      a.ctl.lean = -0.05 * u
      rVictimGuard(v, rSeg(pt, 0.3, 0.9))
      brace(w, 1.0)
      if pt > 1.0 { next() }
    case 2:  // the volley
      if enter() {
        let side = vec3(-dir.z, 0, dir.x)
        for i in 0..<n {
          launchAt[i] = 0.08 * Float(i) + w.fx.rnd(0.0, 0.05)
          if i < 3 {
            let ox = w.fx.rnd(-0.04, 0.04)
            let oy = w.fx.rnd(-0.08, 0.06)
            let oz = w.fx.rnd(-0.04, 0.04)
            targets[i] = rChestOf(v) + vec3(ox, oy, oz)
          } else {
            let across = w.fx.rnd(-0.45, 0.45)
            let along = w.fx.rnd(-0.3, 0.45)
            targets[i] = V + side * across + dir * along + vec3(0, -0.06, 0)
          }
        }
        w.sound(.whoosh, 1.0, V, 0.6)
      }
      a.ctl.armL = vec3(1.55, 0.05, -0.1)
      brace(w, 1.0)
      if rIsHuman(v) && !killed { v.ctl.headPitch = -0.5 }  // it looks up
      var all = true
      for i in 0..<n {
        if !launched[i] {
          if pt >= launchAt[i] {
            launched[i] = true
            let ox = w.fx.rnd(-1.5, 1.5)
            let oz = w.fx.rnd(-1.5, 1.5)
            let from = A - dir * 5.0 + vec3(ox, 5.0, oz)
            pila[i].launch(w, RAsset.pilum, a.color, from, targets[i], 0.85, 0.95, 9.8)
            if i % 2 == 0 { w.sound(.whoosh, 0.5, targets[i], w.fx.rnd(0.7, 0.9)) }
          }
          all = false
          continue
        }
        if landed[i] { continue }
        if pila[i].update(w, dt) {
          landed[i] = true
          let hd = pila[i].heading()
          let M = rPropMatrix(pila[i].tip() + hd * 0.08 - hd * 0.95, hd)
          if let p = w.puppet(pila[i].puppet) { rSetPuppetMatrix(p, M) }
          if i < 3 && v.alive {
            stuck[i].attach(w, pila[i].puppet, v, M)
            let spray: vec3 = -flat(hd) * 0.3 + vec3(0, 0.4, 0)
            rWound(w, targets[i], spray + dir * 0.3, rIsTower(v) ? 0.0 : 0.8)
            if !killed {
              killed = true
              kill(w, 1.0, dir * 0.3, 0.8)
              w.slowmo(0.35, 0.5)
            }
          } else {
            if let p = w.puppet(pila[i].puppet) {
              p.life = 4.0
              p.fadeRate = 1.0
            }
            w.sound(.thud, 0.5, targets[i], 1.5)
            w.fx.dust(flat(targets[i]), 4, 0.06, 0.5)
          }
        } else {
          all = false
        }
      }
      if killed { victimDown(w, dt) }
      if all { next() }
    case 3:
      a.ctl.armL = vec3(1.55, 0.05, -0.1) * (1.0 - easeInOut(rSeg(pt, 0.5, 1.1)))
      if pt > 0.3 && pt - dt <= 0.3 { w.sound(.crowd, 0.7, a.pos, 1.05) }
      if victimDown(w, dt) && pt > 1.2 { next() }
    case 4:
      if moveInR(w, dt) { next() }
    case 5:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// Gladius duel: he draws his sword, parries, is parried, then spins and runs the
/// victim through.
final class REmperorDuel: RBattle {
  private var c1 = false, c2 = false, stab = false
  private var spin0: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let fights = !rIsTower(v)
    switch phase {
    case 0:
      if faceOffR(w) { next() }
    case 1:  // the cross draw from his left hip
      let u = rSeg(pt, 0.0, 0.7)
      let reach = mix(vec3(0), vec3(0.5, -0.6, 0.7), rBump(u, 0.0, 1.2))
      a.ctl.armR = reach + vec3(0.45, 0.1, 0.45) * rSeg(u, 0.6, 1.0)
      a.ctl.twist = 0.25 * rBump(u, 0.0, 1.2)
      if u > 0.5 && a.ctl.draw < 0.5 {
        a.ctl.draw = 1
        w.sound(.draw, 0.9, a.pos, 0.9)
      }
      a.ctl.itemPitch = 0.6 * rSeg(u, 0.6, 1.0)
      rVictimGuard(v, rSeg(pt, 0.2, 0.8))
      brace(w, 0.5)
      if pt > 0.8 { next() }
    case 2:
      if enter() {
        walker = RWalker(plan.mover, V - dir * (touchDist(a, v) * 1.5 + 0.06), .foot, max(0.85, L / 2.0))
      }
      rGuard(&a.ctl, 1.0, false)
      if walker.update(w, dt) { next() }
    case 3:  // its blow, parried
      let u = rSeg(pt, 0.0, 0.85)
      if fights { rVictimStrike(v, u) }
      rParry(&a.ctl, rBump(u, 0.2, 0.85), true)
      if !c1 && u > 0.56 {
        c1 = true
        if fights { rClash(w, a, v) }
      }
      if pt > 0.9 { next() }
    case 4:  // his cut, parried in turn
      let u = rSeg(pt, 0.0, 0.8)
      rOverhead(&a.ctl, u)
      if rIsHuman(v) { rParry(&v.ctl, rBump(u, 0.15, 0.85), true) }
      if !c2 && u > 0.56 {
        c2 = true
        if fights {
          rClash(w, a, v)
        } else {
          rWound(w, rChestOf(v), dir, 0.0, true)
          rSplinters(w, rChestOf(v), -dir, 20)
        }
      }
      if pt > 0.85 { next() }
    case 5:  // a pirouette, and the thrust
      if enter() {
        spin0 = a.yaw
        w.sound(.whoosh, 0.8, a.pos, 0.9)
        w.sound(.grunt, 0.8, a.pos, 0.85)
      }
      let u = rSeg(pt, 0.0, 0.5)
      a.yaw = spin0 + kTau * smoothstep(0.0, 1.0, u)
      a.yawTarget = a.yaw
      a.ctl.cape = 0.9 * rBump(u, 0.0, 1.0)
      let t2 = rSeg(pt, 0.35, 0.95)
      rThrust(&a.ctl, 0.3 + 0.7 * t2)
      if rIsHuman(v) { rVictimGuard(v, 1.0 - u) }
      if !stab && t2 > 0.34 {
        stab = true
        let c = rChestOf(v)
        rWound(w, c, dir + vec3(0, 0.15, 0), rIsTower(v) ? 0.2 : 1.2)
        kill(w, 1.0, dir * 0.3, 0.75)
        w.shake(0.3)
        w.slowmo(0.3, 0.5)
      }
      if u >= 1.0 {
        a.yawTarget = spin0
        a.yaw = spin0
      }
      if stab { victimDown(w, dt) }
      if pt > 1.0 {
        a.yawTarget = spin0
        a.yaw = spin0
        next()
      }
    case 6:
      rGuard(&a.ctl, 1.0, false)
      if victimDown(w, dt) && pt > 0.3 { next() }
    case 7:
      if enter() { w.sound(.crowd, 0.7, a.pos) }
      triumph(w, pt / 0.8)
      if pt > 0.9 { next() }
    case 8:
      if moveInR(w, dt) { next() }
    case 9:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

/// The lion: he points; a lion bounds onto the board, leaps on the victim and
/// mauls it; it roars, and lopes away.
final class REmperorLion: RBattle {
  private var lion = -1
  private var from = vec3(0), leap0 = vec3(0), leapTo = vec3(0), exitTo = vec3(0)
  private var run: Float = 0
  private var hit = false

  private func lionOf(_ w: World) -> Puppet? { w.puppet(lion) }
  private func lionTo(_ w: World, _ target: vec3, _ speed: Float, _ dt: Float) {
    guard let p = lionOf(w) else { return }
    let d = flat(target - p.pos)
    let l = length(d)
    let stp = min(l, speed * dt)
    if l > 1e-4 {
      p.pos += d / l * stp
      p.yaw = wrapAngle(p.yaw + clamp(wrapAngle(faceAngle(d) - p.yaw), -dt * 6.0, dt * 6.0))
    }
    p.ctl.gait += stp / 0.42 * kTau
    p.ctl.stride = approach(p.ctl.stride, stp > 1e-4 ? 1.0 : 0.0, dt * 4.0)
    p.ctl.run = approach(p.ctl.run, speed > 1.5 ? 1.0 : 0.0, dt * 3.0)
    if speed > 1.5 && w.fx.rnd() < dt * 10.0 { w.fx.dust(p.pos, 1, 0.1, 0.4) }
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 1.6
      shotHeight = 0.5
      if faceOffR(w) { next() }
    case 1:  // he points; a roar from beyond the board
      let u = easeInOut(rSeg(pt, 0.0, 0.5))
      a.ctl.armR = vec3(1.4, 0.0, -0.3) * u
      a.ctl.itemPitch = 1.2 * u
      if enter() {
        let side = vec3(-dir.z, 0, dir.x)
        // it comes from the nearer side of the board
        let s1 = V + side * 3.0, s2 = V - side * 3.0
        let c1 = rClampBoard(s1, 4.25), c2 = rClampBoard(s2, 4.25)
        from = length(c1 - V) < length(c2 - V) ? c1 : c2
        if length(from - V) < 1.2 { from = length(c1 - V) > length(c2 - V) ? c1 : c2 }
        lion = w.addPuppet(RAsset.lion, 0, 3)
        if let p = lionOf(w) {
          p.pos = from
          p.yaw = faceAngle(V - from)
          p.dissolve = 1.0
        }
        w.sound(.lion, 1.0, from)
        w.sound(.crowd, 0.6, a.pos, 0.95)
      }
      if let p = lionOf(w) {
        p.dissolve = max(0.0, p.dissolve - dt * 2.5)
        p.ctl.jaw = 0.8 * rBump(pt, 0.0, 0.9)
        p.ctl.headPitch = -0.3 * rBump(pt, 0.0, 0.9)
      }
      rVictimGuard(v, rSeg(pt, 0.4, 1.0))
      brace(w, 1.0)
      if rIsHuman(v) { v.yawTarget = faceAngle(from - V) }
      if pt > 1.0 { next() }
    case 2:  // it bounds across the board
      lionTo(w, V + flatDir(V, from) * 0.75, 2.6, dt)
      if let p = lionOf(w) {
        p.ctl.jaw = 0.2
        if length(flat(p.pos - V)) < 0.8 {
          leap0 = p.pos
          leapTo = V + flatDir(V, from) * 0.12
          w.sound(.lion, 0.9, p.pos, 1.1)
          next()
        }
      }
      if rIsHuman(v) {
        v.yawTarget = faceAngle(from - V)
        rVictimGuard(v, 1.0)
      }
      brace(w, 1.0)
    case 3:  // the pounce
      let u = rSeg(pt, 0.0, 0.45)
      if let p = lionOf(w) {
        p.pos = mix(leap0, leapTo, u)
        p.pos.y = 4.0 * u * (1.0 - u) * 0.32
        p.ctl.rear = rBump(u, 0.0, 0.7)
        p.ctl.tuck = rBump(u, 0.3, 1.0)
        p.ctl.jaw = 0.9
        p.ctl.stride = 0
      }
      if !hit && u > 0.6 {
        hit = true
        let d = flatDir(from, V)
        let c = rChestOf(v)
        rWound(w, c, d + vec3(0, 0.4, 0), rIsTower(v) ? 0.0 : 1.1, true)
        if rIsHuman(v) {
          kill(w, 1.0, d * 0.9, 0.08)
        } else {
          knockFlying(w, d * 1.2, 0.0)
        }
        w.shake(0.35)
        w.slowmo(0.35, 0.5)
      }
      if hit { victimDown(w, dt) }
      if u >= 1.0 { next() }
    case 4:  // the mauling
      if let p = lionOf(w) {
        var body = V + flatDir(from, V) * 0.45
        if let cp = w.puppet(fall.corpse) { body = flat(cp.pos) + flatDir(from, V) * 0.4 }
        let want = body - flatDir(from, V) * 0.32
        p.pos = mix(p.pos, want, min(1.0, dt * 4.0))
        p.pos.y = 0
        p.ctl.rear = approach(p.ctl.rear, 0.0, dt * 3.0)
        p.ctl.tuck = approach(p.ctl.tuck, 0.0, dt * 4.0)
        p.ctl.crouch = 0.4
        p.ctl.headPitch = 0.5 + 0.3 * sin(pt * 13.0)
        p.ctl.headYaw = 0.25 * sin(pt * 7.0)
        p.ctl.jaw = 0.5 + 0.5 * sin(pt * 13.0 + 1.0)
        if fmod(pt, 0.4) < dt && !rIsTower(v) {
          let m = puppetPoint(p, LNB.jaw, Roman.lionMouth())
          let sx = w.fx.rnd(-1, 1)
          let sz = w.fx.rnd(-1, 1)
          rBloodSpray(w, m, vec3(sx, 1.0, sz), 18, 1.8)
          w.sound(.blood, 0.4, m, w.fx.rnd(0.9, 1.2))
        }
        if fmod(pt, 0.6) < dt { w.sound(.lion, 0.4, p.pos, 1.4) }
      }
      victimDown(w, dt)
      if pt > 1.5 { next() }
    case 5:  // a roar to the crowd
      if enter() {
        w.sound(.lion, 1.0, V, 0.9)
        w.sound(.crowd, 0.8, a.pos, 1.0)
        w.shake(0.15)
      }
      if let p = lionOf(w) {
        p.ctl.crouch = approach(p.ctl.crouch, 0.0, dt * 2.0)
        p.ctl.headPitch = -0.6 * rBump(pt, 0.0, 1.0)
        p.ctl.jaw = rBump(pt, 0.0, 1.0)
        p.ctl.headYaw = 0
      }
      a.ctl.armR = mix(a.ctl.armR, vec3(0), min(1.0, dt * 3.0))
      a.ctl.itemPitch = approach(a.ctl.itemPitch, 0.0, dt * 2.0)
      victimDown(w, dt)
      if pt > 1.1 {
        exitTo = from
        next()
      }
    case 6:  // and away it goes
      lionTo(w, exitTo, 1.6, dt)
      if let p = lionOf(w) {
        p.ctl.jaw = 0
        if length(flat(p.pos - exitTo)) < 0.6 { p.dissolve = min(1.0, p.dissolve + dt * 2.0) }
        if p.dissolve >= 1.0 { p.alive = false }
      }
      let gone = lionOf(w) == nil
      if victimDown(w, dt) && (gone || pt > 2.5) {
        if let p = lionOf(w) { p.alive = false }
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
