// Alien battles of the tripod (heat ray, stomp, tentacle toss), the alien queen
// (acid spit, brood, tail strike) and the overlord on his hover throne (shrink
// ray, the mothership, disintegrator).
import Foundation
import simd

// ===========================================================================
// ROOK - the tripod
// ===========================================================================
/// The tripod's eye, aimed at a point: returns the head pitch (+ looks down).
func aimPitch(_ a: Actor, _ target: vec3) -> Float {
  let e = muzzleOf(a)
  return atan2(e.y - target.y, max(0.1, length(flat(target - e))))
}

/// Heat ray: it rises up tall, sounds its horn, and burns the victim to a cinder.
final class RookHeatRay: ABattle {
  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let hot = vec3(1.0, 0.55, 0.2)
    switch phase {
    case 0:
      shotExtra = 0.3
      shotHeight = 0.9
      if faceOff(w) { next() }
    case 1:  // stand up tall; the horn
      if pt <= dt + 1e-6 {
        w.sound(.horn, 1, a.pos)
        w.sound(.grind, 0.4, a.pos, 1.4)
      }
      let u = clamp(pt / 1.3, 0, 1)
      a.ctl.legs = easeInOut(u)
      a.ctl.headPitch = aimPitch(a, victimCenter(w)) * u
      if pt > 2.0 { next() }
    case 2:  // the eye glows
      let u = clamp(pt / 0.6, 0, 1)
      a.ctl.headPitch = aimPitch(a, victimCenter(w))
      a.ctl.hatch = u
      w.frameLights.append(PointLight(muzzleOf(a), 1.5, hot * (3 + 10 * u)))
      if pt <= dt + 1e-6 { w.sound(.charge, 0.6, a.pos, 0.6) }
      if u >= 1 {
        w.sound(.heatray, 1, a.pos)
        next()
      }
    case 3:  // the ray
      let u = clamp(pt / 1.7, 0, 1)
      let m = muzzleOf(a), c = victimCenter(w)
      a.ctl.headPitch = aimPitch(a, c) + 0.01 * sin(pt * 40)
      beamLine(w, m, c, hot, 0.006, 1.2)
      v.heat = easeIn(u)
      v.tremble = 0.5 + 0.5 * u
      v.idleWeight = 0
      v.glow = vec4(1.0, 0.4, 0.1, 1.2 * u)
      if w.fx.rnd() < 0.5 { w.fx.embers(c, 2, 1.0) }
      if w.fx.rnd() < 0.3 { w.fx.smoke(c + vec3(0, v.height() * 0.4, 0), 1, 0.08, 0.6, 0.8) }
      if v.set == 2 { v.ctl.jaw = 0.8 }
      if u >= 1 {
        w.fx.fireball(c, 1.1)
        w.sound(.explode, 1, c)
        w.shatter(v, c, dir * 1.0 + vec3(0, 1, 0), 2.4, 1.0, 9)
        w.light(c, vec3(30.0, 15.0, 4.0), 5, 0.5)
        w.shake(0.5)
        next()
      }
    case 4:  // sink back down
      let u = clamp(pt / 1.0, 0, 1)
      a.ctl.hatch = 1 - u
      a.ctl.legs = 1 - easeInOut(u)
      a.ctl.headPitch *= 1 - min(1, dt * 3)
      if pt <= dt + 1e-6 { w.sound(.grind, 0.35, a.pos, 1.2) }
      if u >= 1 {
        a.ctl.headPitch = 0
        next()
      }
    case 5:
      if moveInA(w, dt, 0.8) { next() }
    case 6:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// Stomp: it raises a foot over the victim, which hops aside - a miss - then it
/// tries again.
final class RookStompA: ABattle {
  private var stompPos = vec3(0), from = vec3(0), dodge = vec3(0)
  private var yaw0: Float = 0, yaw1: Float = 0
  private var walking = false

  /// Where the tripod must stand (and face) so its front-left foot comes down on p.
  private func placeFor(_ p: vec3, _ facing: vec3) -> (vec3, Float) {
    let yaw = faceAngle(facing)
    let F = vec3(sin(1.0471976) * 0.34, 0, cos(1.0471976) * 0.34)
    let f = vec3(cos(yaw) * F.x + sin(yaw) * F.z, 0, -sin(yaw) * F.x + cos(yaw) * F.z)
    return (flat(p) - f, yaw)
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.1
      if faceOff(w) { next() }
    case 1:  // stride up beside it
      if !walking {
        walking = true
        (stompPos, yaw0) = placeFor(V, dir)
        walker = AWalker(plan.mover, stompPos, .tripod, 1.0)
      }
      if walker.update(w, dt) {
        a.yawTarget = yaw0
        next()
      }
    case 2:  // the foot goes up; the victim scampers aside
      let u = clamp(pt / 0.55, 0, 1)
      a.ctl.lift = -easeInOut(u)
      a.ctl.lean = 0.05 * u
      if pt <= dt + 1e-6 {
        w.sound(.grind, 0.4, a.pos, 1.6)
        let side = vec3(-dir.z, 0, dir.x)
        let off: Float = w.fx.rnd() < 0.5 ? 0.42 : -0.42
        let d: vec3 = V + side * off
        dodge = d + dir * 0.1
      }
      let d = clamp((pt - 0.25) / 0.3, 0, 1)
      v.pos = mix(V, dodge, easeInOut(d))
      v.pos.y = 0.15 * sin(d * kPI)
      v.tremble = 0.8
      if pt > 0.25 && pt - dt <= 0.25 { w.sound(.boing, 0.6, v.pos, 1.3) }
      if u >= 1 && d >= 1 { next() }
    case 3:  // STOMP - on nothing
      let u = clamp(pt / 0.14, 0, 1)
      a.ctl.lift = -(1 - easeIn(u))
      if u >= 1 && pt - dt < 0.14 {
        let f = V
        w.sound(.stomp, 1, f)
        w.fx.dust(f, 20, 0.4, 1.4)
        w.groundRing(f, vec3(0.8, 0.7, 0.55), 2.4, 0.4, 0.06)
        w.shake(0.35)
      }
      if pt > 0.3 { next() }
    case 4:  // it looks round - there it is - and shuffles over
      let u = clamp(pt / 1.2, 0, 1)
      a.ctl.headYaw = 0.7 * sin(u * kPI * 2) * (1 - u)
      if pt <= dt + 1e-6 {
        from = flat(a.pos)
        (stompPos, yaw1) = placeFor(dodge, flatDir(from, dodge) + dir * 0.5)
        w.sound(.horn, 0.35, a.pos, 1.6)
      }
      let m = clamp((pt - 0.6) / 0.6, 0, 1)
      if m > 0 {
        let p = mix(from, stompPos, easeInOut(m))
        a.pos = vec3(p.x, 0, p.z)
        a.yawTarget = yaw1
        a.ctl.gait += dt * 6
        a.ctl.stride = 0.6 * sin(m * kPI)
      }
      v.tremble = 0.6
      if u >= 1 {
        a.ctl.stride = 0
        next()
      }
    case 5:  // up again...
      let u = clamp(pt / 0.4, 0, 1)
      a.ctl.lift = -easeInOut(u)
      if pt <= dt + 1e-6 { w.sound(.grind, 0.4, a.pos, 1.6) }
      v.tremble = 1
      if u >= 1 { next() }
    case 6:  // ...and down. Got it.
      let u = clamp(pt / 0.12, 0, 1)
      a.ctl.lift = -(1 - easeIn(u))
      if u >= 1 && v.alive {
        let f = v.pos
        w.shatter(v, f + vec3(0, v.height(), 0), vec3(0, -3.5, 0), 2.0, 0.3, 8)
        w.sound(.stomp, 1, f)
        w.sound(.explode, 0.5, f, 1.5)
        w.fx.dust(f, 30, 0.6, 1.8)
        w.groundRing(f, vec3(0.8, 0.7, 0.55), 3.0, 0.6, 0.08)
        w.shake(0.55)
      }
      if pt > 0.5 { next() }
    case 7:
      a.ctl.headYaw = 0
      if moveInA(w, dt, 0.8) { next() }
    case 8:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// Tentacle toss: the tentacles grab the victim, lift it, give it a shake and fling it away.
final class RookToss: ABattle {
  private var flight = Flight()
  private var walking = false, flung = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let tipL = bonePoint(a, TB.tl3, vec3(0.075, 0.36, 0.125))
    let tipR = bonePoint(a, TB.tr3, vec3(-0.075, 0.36, 0.125))
    let hold = (tipL + tipR) * 0.5
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // close in
      if !walking {
        walking = true
        walker = AWalker(plan.mover, V - dir * min(L, 0.6), .tripod, 1.0)
      }
      if walker.update(w, dt) { next() }
    case 2:  // reach out
      let u = easeInOut(pt / 0.5)
      a.ctl.armL = vec3(1.25, 0.25, 0.5) * u
      a.ctl.armR = vec3(1.25, 0.25, 0.5) * u
      if pt <= dt + 1e-6 { w.sound(.whoosh, 0.5, a.pos, 0.8) }
      v.tremble = 0.8
      if pt > 0.5 {
        w.sound(.clack, 0.7, V, 1.4)
        next()
      }
    case 3:  // lift it
      let u = easeInOut(pt / 0.9)
      a.ctl.armL = vec3(1.25 + 0.9 * u, 0.25, 0.5 + 0.3 * u)
      a.ctl.armR = a.ctl.armL
      a.ctl.legs = 0.3 * u
      let p = hold - vec3(0, v.height() * 0.55, 0)
      v.pos = mix(V, p, min(1, pt / 0.2))
      v.idleWeight = 0
      v.tremble = 0.6
      if v.set == 2 { v.ctl.perk = -1 }
      if pt <= dt + 1e-6 { w.sound(.grind, 0.4, a.pos, 1.5) }
      if pt > 0.9 { next() }
    case 4:  // a good shake
      let s = sin(pt * 22)
      a.ctl.armL = vec3(2.15, 0.25 + 0.35 * s, 0.8)
      a.ctl.armR = a.ctl.armL
      v.pos = hold - vec3(0, v.height() * 0.55, 0)
      v.yawTarget = v.yaw + 0.5 * s
      if fmod(pt, 0.15) < dt { w.sound(.clack, 0.4, v.pos, w.fx.rnd(1.0, 1.5)) }
      if pt > 0.9 { next() }
    case 5:  // and away it goes
      if !flung {
        flung = true
        v.pos = hold - vec3(0, v.height() * 0.55, 0)
        flight.launch(v, dir * 3.2 + vec3(0, 5, 0), 11)
        w.sound(.whoosh, 0.9, v.pos, 0.9)
        w.sound(.whistle, 0.5, v.pos, 0.8)
      }
      let u = easeInOut(pt / 0.5)
      a.ctl.armL = mix(vec3(2.4, 0.25, 0.2), vec3(0), u)
      a.ctl.armR = a.ctl.armL
      a.ctl.legs = 0.3 * (1 - u)
      if v.alive && flight.update(v, dt) { crashLanding(w, v, dir) }
      if !v.alive && pt > 0.5 { next() }
    case 6:
      if moveInA(w, dt, 0.8) { next() }
    case 7:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// QUEEN - the alien queen
// ===========================================================================
/// Acid spit: she rears back and spits a glob of acid; the victim sizzles and melts.
final class QueenAcid: ABattle {
  private var glob = -1
  private var lob = Lob()

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let mouth = bonePoint(a, AH.jaw, vec3(0, 0.83, 0.07))
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // rear back, the inner jaw slides out with a hiss
      let u = easeInOut(pt / 0.6)
      a.ctl.lean = -0.25 * u
      a.ctl.headPitch = -0.25 * u
      a.ctl.jaw = u
      a.ctl.tongue = 0.05 * u
      a.ctl.armL = vec3(0.5, 0.4, 0.4) * u
      if pt <= dt + 1e-6 { w.sound(.sizzle, 0.4, a.pos, 1.5) }
      if pt > 0.6 { next() }
    case 2:  // spit!
      let u = easeOut(pt / 0.15)
      a.ctl.lean = mix(-0.25, 0.2, u)
      a.ctl.headPitch = mix(-0.25, 0.15, u)
      a.ctl.tongue = 0.05 + 0.05 * u
      if pt >= 0.15 {
        glob = w.addPuppet(AAsset.goo, a.color, 2)
        if let p = w.puppet(glob) { p.scale = 0.8 }
        lob.throwAt(glob, mouth, V + vec3(0, v.height() * 0.8, 0), 0.25 + 0.05 * L, 0.3 + 0.07 * L)
        w.sound(.splat, 0.4, mouth, 1.8)
        w.sound(.whoosh, 0.4, mouth, 1.6)
        next()
      }
    case 3:
      a.ctl.lean *= 1 - min(1, dt * 4)
      a.ctl.headPitch *= 1 - min(1, dt * 4)
      a.ctl.tongue = approach(a.ctl.tongue, 0, dt * 0.5)
      a.ctl.jaw = approach(a.ctl.jaw, 0, dt * 2)
      if lob.update(w, dt) {
        if let p = w.puppet(glob) { p.alive = false }
        let hit = V + vec3(0, v.height() * 0.8, 0)
        gooSplash(w, hit, a.color, 45)
        w.sound(.splat, 1, hit)
        w.sound(.sizzle, 1, hit)
        next()
      }
    case 4:  // it sizzles away in a cloud of fumes
      let u = clamp(pt / 2.0, 0, 1)
      melt(w, v, u, armyGoo(a.color), dt)
      if w.fx.rnd() < dt * 20 {
        var f = Particle()
        let fx = w.fx.rnd(-0.1, 0.1)
        let fy = w.fx.rnd(0, v.height() * 0.7)
        let fz = w.fx.rnd(-0.1, 0.1)
        f.pos = v.pos + vec3(fx, fy, fz)
        f.vel = vec3(0, w.fx.rnd(0.2, 0.5), 0)
        f.size = w.fx.rnd(0.05, 0.12)
        f.grow = 0.2
        f.color = armyGoo(a.color) * 0.35
        f.colorEnd = f.color * 0.5
        f.opacity = 0.35
        f.maxLife = 1.2
        f.type = .smoke
        f.rot = w.fx.rnd(0, 6.28)
        f.rotVel = 1
        f.seed = w.fx.rnd()
        w.fx.add(f)
      }
      a.ctl.tongue = 0
      a.ctl.jaw = 0
      if u >= 1 {
        v.alive = false
        v.visible = false
        w.groundRing(V, armyGoo(a.color), 1.0, 0.8, 0.08)
        next()
      }
    case 5:
      if moveInA(w, dt, 1.0) { next() }
    case 6:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// Brood: she lays an egg, which hops over to the victim and hatches a swarm of
/// hungry little critters.
final class QueenBrood: ABattle {
  private var egg = -1
  private var critter = [Int](repeating: -1, count: 5)
  private var eggPos = vec3(0), hopFrom = vec3(0), hopTo = vec3(0), nest = vec3(0)
  private var hops = 0, hop = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // she lays an egg
      if enter() {
        egg = w.addPuppet(AAsset.egg, a.color, 2)
        eggPos = flat(a.pos) + dir * 0.3
        w.sound(.splat, 0.5, eggPos, 0.6)
      }
      let u = clamp(pt / 0.6, 0, 1)
      a.ctl.lean = 0.15 * sin(u * kPI)
      a.ctl.hover = -0.03 * sin(u * kPI)
      if let p = w.puppet(egg) {
        p.pos = eggPos
        p.scale = easeOutBack(u)
        p.yaw = a.yaw
      }
      if u >= 1 {
        nest = V - dir * 0.24
        hops = max(1, Int((length(flat(nest - eggPos)) / 0.5).rounded()))
        hop = 0
        hopFrom = eggPos
        next()
      }
    case 2:  // it hops over to the victim
      let seg: Float = 0.32
      let u = clamp(pt / seg, 0, 1)
      hopTo = mix(eggPos, nest, Float(hop + 1) / Float(hops))
      if let p = w.puppet(egg) {
        p.pos = mix(hopFrom, hopTo, u) + vec3(0, 0.18 * sin(u * kPI), 0)
        p.tilt = angleAxis(0.3 * sin(u * kPI * 2), vec3(1, 0, 0))
      }
      if u >= 1 {
        w.sound(.boing, 0.4, hopTo, 1.4)
        hop += 1
        hopFrom = hopTo
        pt = 0
        if hop >= hops { next() }
      }
    case 3:  // it wobbles... and hatches
      if let p = w.puppet(egg) { p.tilt = angleAxis(0.15 * sin(pt * 30) * pt, vec3(0, 0, 1)) }
      v.tremble = 0.7
      if pt > 0.7 {
        if let p = w.puppet(egg) {
          w.shatterAsset(AAsset.egg, a.color, p.model(), angleAxis(p.yaw, vec3(0, 1, 0)), vec3(0), p.texOffset,
                         p.pos + vec3(0, 0.1, 0), vec3(0, 0.6, 0), 1.2, 0.6, 6.0, 0.0, vec3(1), 2)
          p.alive = false
        }
        w.sound(.hatch, 1, nest)
        gooSplash(w, nest + vec3(0, 0.08, 0), a.color, 20)
        for i in 0..<5 {
          critter[i] = w.addPuppet(AAsset.critter, a.color, 2)
          if let c = w.puppet(critter[i]) {
            let an = kTau * Float(i) / 5
            c.pos = nest + vec3(sin(an), 0, cos(an)) * 0.08
            c.yaw = an
            c.scale = 1.1
          }
        }
        next()
      }
    case 4:  // the swarm: round and up the victim, nibbling
      let u = clamp(pt / 2.0, 0, 1)
      for i in 0..<5 {
        guard let c = w.puppet(critter[i]) else { continue }
        let fi = Float(i)
        let an = kTau * fi / 5 + pt * (3 + 0.4 * fi)
        let climb = v.height() * clamp(u * 1.4 - 0.15 * fi, 0, 0.85)
        let r = v.radius() * 0.95 + 0.04
        let target = V + vec3(sin(an) * r, climb + 0.03 * abs(sin(pt * 20 + fi)), cos(an) * r)
        c.pos = mix(c.pos, target, min(1, dt * 10))
        c.yaw = an + kPI * 0.5
        c.tilt = angleAxis(-0.6 * (climb > 0.05 ? 1 : 0), vec3(1, 0, 0))
      }
      v.tremble = 1
      v.idleWeight = 0
      v.crack = 0.6 * u
      v.crackColor = armyGoo(a.color)
      if v.set == 2 {
        v.ctl.perk = -1
        v.ctl.jaw = 0.8
        v.ctl.armL = vec3(1.5 + 0.5 * sin(pt * 15), 0.5, 0.6)
        v.ctl.armR = vec3(1.5 + 0.5 * sin(pt * 15 + 1), 0.5, 0.6)
      }
      if fmod(pt, 0.13) < dt { w.sound(.bite, 0.35, V, w.fx.rnd(1.6, 2.1)) }
      if u >= 1 {
        w.shatter(v, victimCenter(w), vec3(0, 0.5, 0), 1.8, 0.8, 9)
        w.sound(.hatch, 0.6, V, 1.4)
        next()
      }
    case 5:  // and they scurry back to mother, and vanish under her gown
      let u = clamp(pt / 1.0, 0, 1)
      for i in 0..<5 {
        guard let c = w.puppet(critter[i]) else { continue }
        let fi = Float(i)
        let home = flat(a.pos) + dir * 0.1
        c.pos = mix(c.pos, home, min(1, dt * (3 + fi)))
        c.pos.y = 0.04 * abs(sin(pt * 18 + fi))
        c.yaw = faceAngle(home - c.pos)
        c.tilt = quat.identity
        c.dissolve = clamp((u - 0.5) * 2, 0, 1)
        if u >= 1 { c.alive = false }
      }
      if u >= 1 { next() }
    case 6:
      if moveInA(w, dt, 1.0) { next() }
    case 7:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// Tail strike: she turns her back and her tail lashes round, swatting the victim away.
final class QueenTail: ABattle {
  private var flight = Flight()
  private var walking = false, struck = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:
      if !walking {
        walking = true
        walker = AWalker(plan.mover, V - dir * min(L, 0.62), .hover, 1.0)
      }
      if walker.update(w, dt) { next() }
    case 2:  // turn her back, raise the tail
      let u = easeInOut(pt / 0.6)
      a.yawRate = 6
      a.yawTarget = faceAngle(A - V) + 0.6
      a.ctl.lift = u
      a.ctl.headYaw = 0.9 * u  // looking back over her shoulder
      a.ctl.kick = -0.6 * u
      v.tremble = 0.7
      if pt <= dt + 1e-6 { w.sound(.sizzle, 0.3, a.pos, 2.0) }
      if pt > 0.7 {
        w.sound(.whoosh, 1, a.pos, 0.7)
        next()
      }
    case 3:  // swish!
      let u = clamp(pt / 0.25, 0, 1)
      a.ctl.kick = mix(-0.6, 1.3, easeIn(u))
      a.ctl.lift = 1 - 0.7 * u
      if u >= 0.6 && !struck {
        struck = true
        let side = vec3(-dir.z, 0, dir.x)
        let kick: vec3 = dir * 1.2 + side * 3.4
        flight.launch(v, kick + vec3(0, 4.2, 0), 12)
        w.sound(.clack, 1, victimCenter(w), 0.7)
        w.sound(.boing, 0.6, victimCenter(w), 0.9)
        w.fx.sparks(victimCenter(w), side, 25, 3.0, 0.6, armyLight(a.color) * 3)
        w.shake(0.35)
        w.slowmo(0.3, 0.4)
      }
      if v.alive && struck && flight.update(v, dt) { crashLanding(w, v, dir) }
      if u >= 1 { next() }
    case 4:  // turn back round
      let u = easeInOut(pt / 0.6)
      a.ctl.kick = 1.3 * (1 - u)
      a.ctl.lift = 0.3 * (1 - u)
      a.ctl.headYaw = 0.9 * (1 - u)
      a.yawTarget = faceAngle(V - A)
      if v.alive && flight.update(v, dt) { crashLanding(w, v, dir) }
      if pt > 0.6 && !v.alive { next() }
    case 5:
      if moveInA(w, dt, 1.0) { next() }
    case 6:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// KING - the overlord
// ===========================================================================
let kRoyal = vec3(1.0, 0.85, 0.4)

/// The sceptre raised and pointed at a target (u 0..1).
func pointSceptre(_ a: Actor, _ u: Float) {
  a.ctl.armR = vec3(0.7, -0.1, -0.5) * u
  a.ctl.itemPitch = -0.4 * u
}

/// Shrink ray: zap - the victim shrinks to a squeaking speck - and the throne
/// floats over and sits on it.
final class KingShrink: ABattle {
  private var from = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:
      let u = easeInOut(pt / 0.5)
      pointSceptre(a, u)
      a.glow = vec4(armyLight(a.color), 0.4 * u)
      if pt <= dt + 1e-6 { w.sound(.charge, 0.5, a.pos, 1.3) }
      if pt > 0.5 {
        w.sound(.shrink, 1, a.pos)
        next()
      }
    case 2:  // the ray: wobbly rings; the victim dwindles
      let u = clamp(pt / 1.2, 0, 1)
      let m = muzzleOf(a), c = victimCenter(w)
      beamLine(w, m, c, armyLight(a.color), 0.03, 0.6)
      if w.fx.rnd() < dt * 12 { w.fx.ring(mix(m, c, w.fx.rnd()), 0.04, armyLight(a.color) * 1.5, 0.3) }
      v.scale = 1 - 0.84 * easeInOut(u)
      v.tremble = 0.5
      v.glow = vec4(armyLight(a.color), 0.8 * (1 - u))
      if v.set == 2 { v.ctl.perk = 1 }
      if u >= 1 {
        pointSceptre(a, 1)
        w.sound(.gibber, 0.7, v.pos, 2.2)  // a tiny squeak of protest
        next()
      }
    case 3:  // the speck hops about; the overlord chortles
      let u = clamp(pt / 0.9, 0, 1)
      pointSceptre(a, 1 - easeInOut(u))
      a.glow = vec4(0)
      v.pos.y = 0.08 * abs(sin(pt * 14))
      v.yawTarget = v.yaw + dt * 8
      a.ctl.lean = -0.08 * abs(sin(pt * 9))
      a.ctl.jaw = 0.5 * abs(sin(pt * 9))
      if pt <= dt + 1e-6 { w.sound(.gibber, 0.8, a.pos, 0.55) }
      if u >= 1 {
        v.pos.y = 0
        a.ctl.jaw = 0
        a.ctl.lean = 0
        from = flat(a.pos)
        w.sound(.whoosh, 0.5, a.pos, 0.6)
        next()
      }
    case 4:  // float over it
      let u = clamp(pt / 0.9, 0, 1)
      let p = mix(from, V, easeInOut(u))
      a.pos = vec3(p.x, 0, p.z)
      a.ctl.hover = 0.18 * sin(u * kPI * 0.5)
      v.tremble = 1
      if u >= 1 { next() }
    case 5:  // and sit down on it - hard
      let u = clamp(pt / 0.14, 0, 1)
      a.ctl.hover = mix(0.18, -0.06, easeIn(u))
      if u >= 1 && v.alive {
        w.shatter(v, V + vec3(0, 0.05, 0), vec3(0, -1, 0), 1.6, 0.2, 6)
        w.sound(.splat, 0.8, V, 1.4)
        w.sound(.thud, 0.9, V, 0.9)
        w.fx.dust(V, 24, 0.45, 1.5)
        w.shake(0.3)
      }
      if pt > 0.14 { a.ctl.hover = -0.06 + 0.06 * easeOutBack(clamp((pt - 0.14) / 0.4, 0, 1)) }
      if pt > 0.6 {
        a.ctl.hover = 0
        next()
      }
    case 6:
      if moveInA(w, dt, 1.0) { next() }
    case 7:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// The mothership: he summons it; it fills the sky, beams the victim to dust and leaves.
final class KingMothership: ABattle {
  private var ship = -1
  private var shipPos = vec3(0)
  private var shipYaw: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let S: Float = 5.5, low: Float = 3.4
    let p = w.puppet(ship)
    if let p = p {
      shipYaw += dt * 0.35
      p.yaw = shipYaw
      p.pos = shipPos
    }
    let belly = shipPos + vec3(0, (0.1 - 0.09) * S, 0)
    switch phase {
    case 0:
      shotExtra = 2.6
      shotHeight = 1.3
      if faceOff(w) { next() }
    case 1:  // sceptre to the sky
      let u = easeInOut(pt / 0.5)
      a.ctl.armR = vec3(2.5, 0.2, -0.6) * u
      a.ctl.headPitch = -0.35 * u
      a.glow = vec4(armyLight(a.color), 0.5 * u)
      if pt > 0.5 {
        ship = w.addPuppet(AAsset.mothership, a.color, 2)
        shipPos = vec3(V.x, 12, V.z)
        if let q = w.puppet(ship) {
          q.scale = S
          q.pos = shipPos
          q.yaw = shipYaw
        }
        w.sound(.drone, 1, V)
        if w.cinematic {
          // down low, looking up at the sky - towards the dark side of the room, away
          // from the studio lights, and as side-on to the fighters as that allows
          let mid = (A + V) * 0.5
          var s = CamPose()
          var best: Float = 1e9
          for k in -1...1 {
            let ang = atan2(Float(0.8), Float(-0.6)) + 0.17 * Float(k)
            let look = vec3(cos(ang), 0, sin(ang))
            let score = abs(dot(look, dir)) + 0.2 * Float(abs(k))
            if score < best {
              best = score
              s.pos = mid - look * 3.6 + vec3(0, 0.45, 0)
            }
          }
          s.target = mid + vec3(0, 1.7, 0)
          s.fov = 52
          s.focus = length(s.pos - V)
          s.aperture = 4
          w.cam.setShot(s, 1.6)
        }
        next()
      }
    case 2:  // it descends, filling the sky
      let u = clamp(pt / 2.2, 0, 1)
      shipPos.y = mix(12, low, easeOut(u))
      w.shake(0.04)
      v.tremble = 0.5
      v.idleWeight = 0
      if v.set == 2 { v.ctl.headPitch = -0.6 * u }  // looking up at it
      w.frameLights.append(PointLight(belly, 6.0, armyLight(a.color) * 5 * u))
      if u >= 1 {
        w.sound(.beam, 1, V, 0.7)
        w.sound(.theremin, 0.6, V, 0.8)
        next()
      }
    case 3:  // the great beam
      let u = clamp(pt / 1.8, 0, 1)
      tractorBeam(w, belly, V, 0.4, armyLight(a.color), dt, 2)
      v.pos.y = 0.4 * easeIn(u)
      v.glow = vec4(armyLight(a.color), 2 * u)
      v.dissolveColor = armyLight(a.color) * 2
      v.dissolve = clamp((u - 0.35) / 0.65, 0, 1)
      v.tremble = 1
      if w.fx.rnd() < 0.6 {
        stream(w, V + vec3(0, v.pos.y + v.height() * 0.5, 0), belly, armyLight(a.color), 2, 0.5)
      }
      if u >= 1 {
        v.alive = false
        v.visible = false
        w.fx.flash(V + vec3(0, 0.5, 0), 1.0, armyLight(a.color) * 4, 0.2)
        next()
      }
    case 4:  // and away it goes
      let u = clamp(pt / 1.4, 0, 1)
      shipPos.y = mix(low, 14, easeIn(u))
      if let p = p { p.dissolve = clamp((u - 0.6) / 0.4, 0, 1) }
      a.ctl.armR = vec3(2.5, 0.2, -0.6) * (1 - easeInOut(u))
      a.ctl.headPitch = -0.35 * (1 - u)
      a.glow = vec4(armyLight(a.color), 0.5 * (1 - u))
      if pt <= dt + 1e-6 { w.sound(.whoosh, 0.8, V, 0.4) }
      if u >= 1 {
        if let p = p { p.alive = false }
        next()
      }
    case 5:
      if moveInA(w, dt, 1.0) { next() }
    case 6:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// Disintegrator: it sputters; he gives it a tap... and it fires far too hard,
/// blowing the victim apart and his throne backwards.
final class KingDisintegrator: ABattle {
  private var from = vec3(0)
  private var kickBack: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // aim
      let u = easeInOut(pt / 0.45)
      pointSceptre(a, u)
      if pt > 0.5 {
        w.sound(.sputter, 0.9, a.pos)
        next()
      }
    case 2:  // it sputters
      pointSceptre(a, 1)
      let m = muzzleOf(a)
      if w.fx.rnd() < dt * 9 {
        w.fx.sparks(m, vec3(0, 1, 0), 6, 1.2, 1.0, vec3(3.0, 2.0, 1.0))
        if w.fx.rnd() < 0.5 { w.fx.bolt(m, m + flatDir(m, V) * w.fx.rnd(0.1, 0.3), armyLight(a.color) * 2, 0.02, 3) }
      }
      if w.fx.rnd() < dt * 4 { w.fx.smoke(m, 1, 0.01, 0.2, 0.7) }
      a.ctl.headPitch = 0.25 * min(1, pt / 0.4)
      a.ctl.headYaw = -0.35 * min(1, pt / 0.4)
      if pt > 1.3 { next() }
    case 3:  // tap, tap, tap
      pointSceptre(a, 1)
      let tap = max(0, sin(pt * 14))
      a.ctl.armL = vec3(1.0 + 0.25 * tap, -0.6, 0.9)
      if fmod(pt + 0.06, 0.449) < dt && pt < 1.2 { w.sound(.clack, 0.5, a.pos, 1.8) }
      if pt > 0.3 && pt - dt <= 0.3 { w.sound(.gibber, 0.5, a.pos, 0.7) }  // mutters
      if pt > 1.35 {
        a.ctl.armL = vec3(0)
        next()
      }
    case 4:  // aim again
      let u = easeInOut(pt / 0.35)
      pointSceptre(a, 1)
      a.ctl.headPitch = 0.25 * (1 - u)
      a.ctl.headYaw = -0.35 * (1 - u)
      a.ctl.lean = 0.05 * u
      if pt > 0.45 {
        from = flat(a.pos)
        w.sound(.heatray, 1, a.pos, 0.7)
        w.sound(.explode, 0.8, a.pos, 1.3)
        next()
      }
    case 5:  // KABOOM - and the recoil
      let u = clamp(pt / 0.55, 0, 1)
      pointSceptre(a, 1)
      let m = muzzleOf(a), c = victimCenter(w)
      if u < 0.7 { beamLine(w, m, v.alive ? c : V + vec3(0, 0.4, 0), kRoyal, 0.03, 3.0) }
      if v.alive && pt > 0.06 {
        w.fx.fireball(c, 1.4)
        w.fx.flash(c, 1.2, vec3(8.0, 6.0, 3.0), 0.2)
        w.shatter(v, c, dir * 3 + vec3(0, 1, 0), 3.0, 1.2, 12)
        w.shake(0.7)
        w.slowmo(0.35, 0.5)
        w.addFlash(vec3(0.3, 0.25, 0.15))
      }
      kickBack = easeOut(u) * 0.55
      let p = from - dir * kickBack
      a.pos = vec3(p.x, 0, p.z)
      a.ctl.hover = 0.1 * sin(u * kPI)
      a.ctl.lean = -0.25 * sin(u * kPI)
      a.yawTarget = faceAngle(V - from) + 0.5 * sin(u * kPI * 2)
      a.ctl.perk = -1
      if u >= 1 { next() }
    case 6:  // compose himself
      let u = clamp(pt / 0.8, 0, 1)
      pointSceptre(a, 1 - easeInOut(u))
      a.ctl.lean = 0
      a.ctl.headPitch = 0.15 * sin(u * kPI * 3) * (1 - u)
      a.ctl.perk = -1 + u
      if u >= 1 { next() }
    case 7:
      if moveInA(w, dt, 1.0) { next() }
    case 8:
      return finishA(w)
    default:
      break
    }
    return false
  }
}
