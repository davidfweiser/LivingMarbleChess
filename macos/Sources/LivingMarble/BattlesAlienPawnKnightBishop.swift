// Alien battles of the little green man (ray gun, goo grenade, tongue), the
// flying saucer (abduction, crop circle, cow drop) and the big-brained psychic
// (mind blast, hypnosis, telekinetic crush).
import Foundation
import simd

// ===========================================================================
// PAWN - the little green man
// ===========================================================================
/// Ray gun: two wild misses (the victim hops out of the way), then a hit that
/// disintegrates it. He blows the smoke off the muzzle.
final class PawnRayGun: ABattle {
  private var bolt = Bolt()
  private var shot = 0
  private var nextShot: Float = 0
  private var walking = false

  private func aimAt(_ w: World, _ k: Int) -> vec3 {
    let v = w.actors[plan.victim]
    let c = victimCenter(w)
    let side = vec3(-dir.z, 0, dir.x)
    if k == 0 { return flat(c) + side * 0.45 + dir * 0.6 }  // wide to the left, into the board
    if k == 1 { return c + side * -0.32 + vec3(0, v.height() * 0.45, 0) + dir * 0.9 }  // high and right, over the board
    return c
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = -0.3
      if faceOff(w) { next() }
    case 1:  // stroll into range
      if !walking {
        walking = true
        let keep = clamp(L, 1.2, 2.3)
        walker = AWalker(plan.mover, V - dir * keep, .waddle, 0.8)
      }
      if walker.update(w, dt) { next() }
    case 2:  // draw a bead on it
      let u = easeInOut(pt / 0.35)
      a.yawTarget = faceAngle(V - flat(a.pos))
      a.ctl.armR = vec3(1.05 * u, 0.05 * u, -0.7 * u)
      a.ctl.lean = -0.05 * u
      a.ctl.perk = 0.6 * u
      if pt > 0.4 {
        nextShot = 0
        next()
      }
    case 3:  // pew - pew - PEW
      a.ctl.armR = vec3(1.05, 0.05, -0.7) + vec3(0.25, 0, 0) * exp(-max(0, pt - nextShot + 0.6) * 12)
      if shot < 3 && pt >= nextShot {
        let m = muzzleOf(a)
        bolt.fire(m, aimAt(w, shot), armyLight(a.color))
        w.sound(.pew, 0.9, m, shot == 2 ? 0.9 : w.fx.rnd(1.0, 1.15))
        w.fx.flash(m, 0.12, armyLight(a.color) * 4, 0.08)
        shot += 1
        nextShot = pt + (shot == 1 ? 0.75 : 0.85)
      }
      if bolt.update(w, dt) {
        let hit = bolt.b
        if shot < 3 {  // a miss: sparks where it lands, the victim jumps
          w.fx.sparks(hit, vec3(0, 1, 0), 18, 2.2, 0.7, armyLight(a.color) * 3)
          w.fx.ring(flat(hit) + vec3(0, 0.01, 0), 0.05, armyLight(a.color) * 1.5, 0.3)
          w.sound(.zap, 0.4, hit, 1.4)
          v.sq.v += 3
          v.poseSquash = 0.8
          if shot == 2 { w.sound(.gibber, 0.55, a.pos, 1.15) }  // grumbling
        } else {
          w.sound(.zap, 0.9, hit, 1.0)
          w.sound(.crackle, 0.6, hit, 1.2)
          w.fx.magicBurst(hit, armyLight(a.color) * 3, 40, 1.6)
          v.dissolveColor = armyLight(a.color) * 1.4
          next()
        }
      }
      // between shots the victim hops about in alarm
      if shot >= 1 {
        v.tremble = 0.6
        v.pos.y = max(0, 0.12 * sin(pt * 9)) * (shot < 3 ? 1 : 0)
      }
      if shot == 2 { a.ctl.armR.z = -0.7 + 0.3 * sin(pt * 30) * exp(-(pt - nextShot + 0.85) * 4) }
    case 4:  // the victim fizzles away into motes
      let u = clamp(pt / 0.9, 0, 1)
      v.pos.y = approach(v.pos.y, 0, dt)
      v.glow = vec4(armyLight(a.color), 1.5 * (1 - u * 0.5))
      v.dissolve = u
      v.tremble = 0.8
      if w.fx.rnd() < 0.8 {
        let ox = w.fx.rnd(-0.1, 0.1)
        let oy = w.fx.rnd(0, v.height())
        let oz = w.fx.rnd(-0.1, 0.1)
        stream(w, v.pos + vec3(ox, oy, oz), v.pos + vec3(0, v.height() + 0.8, 0), armyLight(a.color), 2, 1.2)
      }
      if u >= 1 {
        v.alive = false
        v.visible = false
        w.fx.smoke(V + vec3(0, 0.1, 0), 6, 0.15, 0.4, 0.3)
        next()
      }
    case 5:  // blow the smoke off the muzzle
      let u = sin(clamp(pt / 1.0, 0, 1) * kPI)
      a.ctl.armR = mix(vec3(1.05, 0.05, -0.7), vec3(1.5, 0.25, 1.1), u)
      a.ctl.headPitch = 0.25 * u
      a.ctl.headYaw = -0.25 * u
      if pt > 0.35 && pt - dt <= 0.35 { w.fx.smoke(muzzleOf(a), 3, 0.02, 0.3, 0.1) }
      if pt > 1.0 {
        a.ctl.armR = vec3(0)
        a.ctl.headPitch = 0
        a.ctl.headYaw = 0
        a.ctl.perk = 0
        a.ctl.lean = 0
        next()
      }
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

/// Goo grenade: a blob of slime lobbed at the victim, which melts into a puddle.
final class PawnGoo: ABattle {
  private var goo = -1
  private var lob = Lob()

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let hand = bonePoint(a, AH.lFore, vec3(0.082, 0.198, 0.06) * 1.2)
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // a blob of goo grows in his hand as he winds up
      if enter() {
        goo = w.addPuppet(AAsset.goo, a.color, 2)
        w.sound(.slurp, 0.4, a.pos, 1.4)
      }
      let u = clamp(pt / 0.6, 0, 1)
      a.ctl.armL = vec3(-0.9 * easeInOut(u), -0.2 * u, 0.6 * u)
      a.ctl.lean = 0.06 * u
      if let p = w.puppet(goo) {
        p.scale = 0.9 * easeOutBack(u)
        p.pos = hand - vec3(0, 0.05, 0) * p.scale
      }
      if u >= 1 { next() }
    case 2:  // and throws
      let u = clamp(pt / 0.18, 0, 1)
      a.ctl.armL = mix(vec3(-0.9, -0.2, 0.6), vec3(1.6, 0.1, -0.2), easeOut(u))
      a.ctl.lean = mix(0.06, -0.08, u)
      if let p = w.puppet(goo) { p.pos = hand - vec3(0, 0.045, 0) }
      if u >= 1 {
        let from = hand
        lob.throwAt(goo, from, V + vec3(0, v.height() * 0.85, 0), 0.45 + 0.08 * L, 0.45 + 0.08 * L)
        w.sound(.whoosh, 0.5, a.pos, 1.5)
        next()
      }
    case 3:
      a.ctl.armL = mix(a.ctl.armL, vec3(0), min(1, dt * 5))
      if lob.update(w, dt) {
        if let p = w.puppet(goo) { p.alive = false }
        let hit = V + vec3(0, v.height() * 0.85, 0)
        gooSplash(w, hit, a.color, 40)
        w.sound(.splat, 1, hit)
        w.sound(.sizzle, 0.7, hit)
        v.sq.v -= 3
        next()
      }
    case 4:  // it melts; he does a little jig
      let u = clamp(pt / 2.0, 0, 1)
      melt(w, v, u, armyGoo(a.color), dt)
      a.pos.y = max(0, 0.06 * sin(pt * 12)) * (pt > 0.4 && pt < 1.6 ? 1 : 0)
      a.ctl.perk = 0.8 * sin(clamp(pt / 1.8, 0, 1) * kPI)
      a.ctl.armL = vec3(0.6, 0.5, 0.8) * sin(clamp(pt / 1.8, 0, 1) * kPI)
      if pt > 0.5 && pt - dt <= 0.5 { w.sound(.gibber, 0.6, a.pos, 1.3) }  // a giggle
      if u >= 1 {
        v.alive = false
        v.visible = false
        w.groundRing(V, armyGoo(a.color), 1.0, 0.8, 0.08)
        w.fx.smoke(V + vec3(0, 0.05, 0), 8, 0.2, 0.3, 0.2)
        a.pos.y = 0
        a.ctl.perk = 0
        a.ctl.armL = vec3(0)
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

/// Tongue: it shoots out, wraps the victim, reels it in - gulp - and he burps up the pieces.
final class PawnTongue: ABattle {
  private var walking = false
  private var reach: Float = 0, pitch: Float = 0, vScale: Float = 1
  private var mouth = vec3(0)

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // waddle up close
      if !walking {
        walking = true
        walker = AWalker(plan.mover, V - dir * min(L, 0.95), .waddle, 0.8)
      }
      if walker.update(w, dt) { next() }
    case 2:  // open wide and take aim
      let u = clamp(pt / 0.3, 0, 1)
      a.yawTarget = faceAngle(V - flat(a.pos))
      mouth = bonePoint(a, AH.jaw, vec3(0, 0.416, 0.06) * 1.2)
      let tgt = victimCenter(w) + vec3(0, v.height() * 0.1, 0)
      let horiz = length(flat(tgt - mouth))
      pitch = atan2(mouth.y - tgt.y, horiz)
      a.ctl.jaw = u
      a.ctl.headPitch = (pitch - 0.55) * u  // (the open jaw tips the tongue down)
      a.ctl.lean = 0.05 * u
      reach = length(tgt - mouth) - 0.02
      if u >= 1 {
        w.sound(.whoosh, 0.6, mouth, 1.8)
        next()
      }
    case 3:  // thwip!
      let u = clamp(pt / 0.14, 0, 1)
      a.ctl.tongue = reach * easeOut(u)
      if u >= 1 {
        w.sound(.splat, 0.4, victimCenter(w), 1.6)
        v.tremble = 1
        next()
      }
    case 4:  // reel it in, shrinking into his mouth
      let u = clamp((pt - 0.15) / 0.55, 0, 1)
      if pt <= dt + 1e-6 { w.sound(.slurp, 1, a.pos) }
      let e = easeIn(u)
      a.ctl.tongue = reach * (1 - e)
      let m = bonePoint(a, AH.jaw, vec3(0, 0.416, 0.06) * 1.2)
      vScale = 1 - 0.85 * e
      v.scale = vScale
      let c = mix(V + vec3(0, 0, 0), m - vec3(0, v.height() * 0.5, 0), e)
      v.pos = c
      v.yawTarget = v.yaw + 4 * dt
      v.idleWeight = 0
      if u >= 1 {
        v.visible = false
        a.ctl.tongue = 0
        w.sound(.pop, 0.5, m, 0.8)
        next()
      }
    case 5:  // gulp, chew, chew...
      a.ctl.jaw = max(0, 0.6 * sin(pt * 14)) * (pt < 0.9 ? 1 : 0)
      a.ctl.headPitch = approach(a.ctl.headPitch, 0, dt * 2)
      a.poseSquash = pt < 0.2 ? 1.2 : 0.95
      if fmod(pt, 0.22) < dt && pt < 0.9 { w.sound(.bite, 0.35, a.pos, w.fx.rnd(1.4, 1.7)) }
      if pt > 1.1 {
        a.poseSquash = 1
        next()
      }
    case 6:  // ...BURP - and out come the pieces
      if enter() {
        w.sound(.burp, 1, a.pos)
        a.ctl.jaw = 1
        a.ctl.lean = -0.1
      }
      if pt > 0.25 && v.alive && !v.visible {
        let m = bonePoint(a, AH.jaw, vec3(0, 0.416, 0.08) * 1.2)
        v.scale = 0.38
        v.pos = m - vec3(0, v.height() * 0.5, 0)
        v.visible = true
        v.dissolve = 0
        w.shatter(v, m, a.forward() * 2.5 + vec3(0, 1, 0), 0.9, 0.8, 8)
        w.fx.ring(m, 0.06, armyGoo(a.color) * 1.2, 0.45)
        w.fx.smoke(m, 5, 0.04, 0.2, 0.2)
        a.ctl.lean = 0.15
      }
      if pt > 0.9 {
        a.ctl.jaw = 0
        a.ctl.lean = 0
        next()
      }
    case 7:
      if moveInA(w, dt, 0.8) { next() }
    case 8:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// KNIGHT - the flying saucer
// ===========================================================================
/// Abduction: it flies over the victim and beams it up, spinning; a burp of
/// noises inside, and out it comes again - in pieces.
final class KnightAbduction: ABattle {
  private var hop = SaucerHop()
  private var lift: Float = 0, over: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.1
      shotHeight = 0.85
      if faceOff(w) { next() }
    case 1:  // over the victim
      if enter() {
        over = max(0.55, v.height() + 0.25 - Alien.hoverHeight(Kind.knight) + 0.12)
        hop.begin(a, V, over, 0.25, 0.9 + 0.15 * L)
        w.sound(.ufo, 0.8, a.pos)
      }
      if hop.update(w, a, dt) { next() }
    case 2:  // the belly opens
      a.ctl.hatch = easeInOut(pt / 0.35)
      a.ctl.spin += dt * 6
      if pt > 0.35 {
        w.sound(.beam, 1, V)
        next()
      }
    case 3:  // the beam: up it goes, spinning and shrinking
      let u = clamp(pt / 2.2, 0, 1)
      let top = muzzleOf(a)
      tractorBeam(w, top, V, 0.24, armyLight(a.color), dt, 1)
      a.ctl.spin += dt * 8
      lift = (top.y - 0.05 - v.height() * 0.4) * easeIn(u)
      v.pos = vec3(V.x, lift, V.z)
      v.yawRate = 30
      v.yawTarget = v.yaw + dt * (3 + 10 * u)
      v.tilt = angleAxis(0.3 * sin(pt * 3) * u, vec3(1, 0, 0))
      v.tiltPivot = vec3(0, v.height() * 0.5, 0)
      v.scale = 1 - 0.55 * easeIn(u)
      v.tremble = 0.4
      v.idleWeight = 0
      v.glow = vec4(armyLight(a.color), 0.6 * u)
      if v.set == 2 { v.ctl.perk = -1 }
      if u >= 1 {
        v.visible = false
        a.ctl.hatch = 0
        w.sound(.pop, 0.6, top, 0.7)
        next()
      }
    case 4:  // noises inside
      a.ctl.hatch = approach(a.ctl.hatch, 0, dt * 4)
      a.ctl.twist = 0.12 * sin(pt * 21)
      a.ctl.lean = 0.08 * sin(pt * 17)
      a.tremble = 0.4
      if fmod(pt, 0.3) < dt {
        let s: Sfx = w.fx.rnd() < 0.5 ? .clack : .bite
        w.sound(s, 0.5, a.pos, w.fx.rnd(1.2, 1.6))
      }
      if pt > 0.15 && pt - dt <= 0.15 { w.sound(.gibber, 0.6, a.pos, 1.4) }
      if pt > 1.0 {
        a.ctl.twist = 0
        a.ctl.lean = 0
        a.tremble = 0
        next()
      }
    case 5:  // ...and spits it out in pieces
      if enter() {
        a.ctl.hatch = 1
        let top = muzzleOf(a)
        v.visible = true
        v.scale = 0.6
        v.tilt = quat.identity
        v.glow = vec4(0)
        v.pos = top - vec3(0, v.height() + 0.02, 0)
        w.shatter(v, top, vec3(0, -2, 0) + dir * 0.6, 1.6, -0.5, 8)
        w.sound(.burp, 0.6, top, 1.6)
        w.fx.smoke(top, 6, 0.06, -0.2, 0.4)
      }
      if pt > 0.6 {
        a.ctl.hatch = 0
        next()
      }
    case 6:  // settle on the square
      if enter() { hop.begin(a, squareCenter(Int(plan.move.to)), 0, 0.05, 0.9) }
      if hop.update(w, a, dt) { next() }
    case 7:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// Crop circle: it circles the victim, a laser carving a glowing ring into the
/// board, then cuts the victim clean in two from above.
final class KnightCropCircle: ABattle {
  private var hop = SaucerHop()
  private var ang0: Float = 0, mark: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    let R: Float = 0.55
    switch phase {
    case 0:
      shotExtra = 0.2
      if faceOff(w) { next() }
    case 1:  // to the start of the circle
      if enter() {
        let s = V - dir * R
        ang0 = atan2(s.x - V.x, s.z - V.z)
        hop.begin(a, s, 0.2, 0.2, 0.7 + 0.12 * L)
        w.sound(.ufo, 0.7, a.pos, 1.15)
      }
      if hop.update(w, a, dt) {
        w.sound(.heatray, 0.45, V, 1.5)
        next()
      }
    case 2:  // round it goes, burning a ring into the board
      let u = clamp(pt / 2.0, 0, 1)
      let an = ang0 + kTau * 1.15 * easeInOut(u)
      a.pos = vec3(V.x + sin(an) * R, 0, V.z + cos(an) * R)
      a.yawTarget = an + kPI * 0.5
      a.ctl.twist = -0.3
      a.ctl.spin += dt * 14
      a.ctl.hatch = 1
      let m = muzzleOf(a)
      let g = flat(a.pos) + vec3(0, 0.005, 0)
      beamLine(w, m, g, vec3(1.0, 0.35, 0.12), 0.004, 0.7)
      mark -= dt
      if mark <= 0 {  // a glowing scorch that lingers
        mark = 0.02
        var p = Particle()
        p.pos = g
        p.vel = vec3(0)
        p.size = 0.035
        p.color = vec3(2.5, 0.9, 0.25)
        p.colorEnd = vec3(0.3, 0.05, 0.0)
        p.opacity = 0
        p.maxLife = 2.6
        p.type = .glow
        p.still = true
        p.fadeIn = 0
        w.fx.add(p)
        if w.fx.rnd() < 0.3 { w.fx.smoke(g, 1, 0.02, 0.3, 0.6) }
      }
      v.tremble = 0.7
      v.yawTarget = faceAngle(flat(a.pos) - V)
      if u >= 1 {
        a.ctl.twist = 0
        w.groundRing(V, vec3(1.0, 0.4, 0.1), 0.2, 0.8, 0.05)
        next()
      }
    case 3:  // up over the victim
      if enter() { hop.begin(a, V, max(0.4, v.height() + 0.2 - Alien.hoverHeight(Kind.knight) + 0.12), 0.1, 0.6) }
      if hop.update(w, a, dt) {
        w.sound(.zap, 0.9, V, 0.8)
        next()
      }
    case 4:  // the cut
      let u = clamp(pt / 0.5, 0, 1)
      let m = muzzleOf(a)
      let bottom = mix(m - vec3(0, 0.1, 0), V, easeIn(u))
      beamLine(w, m, bottom, vec3(1.0, 0.4, 0.15), 0.003, 1.0)
      w.fx.sparks(bottom, vec3(0, 1, 0), 3, 1.5, 0.8, vec3(4.0, 2.0, 0.6))
      if u >= 1 {
        w.sound(.clang, 0.8, V, 1.3)
        w.slice(v, 0.7)
        w.fx.flash(V + vec3(0, 0.4, 0), 0.6, vec3(5.0, 2.5, 0.8), 0.12)
        a.ctl.hatch = 0
        next()
      }
    case 5:
      if pt < 0.3 { break }
      if enter() { hop.begin(a, squareCenter(Int(plan.move.to)), 0, 0.15, 0.7) }
      if hop.update(w, a, dt) { next() }
    case 6:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// Cow drop: a cow falls out of the saucer onto the victim, bounces, moos, and
/// is beamed back up.
final class KnightCowDrop: ABattle {
  private var hop = SaucerHop()
  private var cow = -1
  private var cp = vec3(0), cv = vec3(0)
  private var spin: Float = 0
  private var hit = false

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      shotExtra = 0.5
      shotHeight = 1.0
      if faceOff(w) { next() }
    case 1:  // high over the victim
      if enter() {
        hop.begin(a, V, 1.05, 0.2, 0.9 + 0.15 * L)
        w.sound(.ufo, 0.8, a.pos, 0.9)
      }
      if hop.update(w, a, dt) { next() }
    case 2:  // the belly opens; a cow appears
      if enter() {
        cow = w.addPuppet(AAsset.cow, a.color, 2)
        w.sound(.moo, 0.9, a.pos, 1.05)
      }
      a.ctl.hatch = easeInOut(pt / 0.4)
      let u = clamp((pt - 0.2) / 0.5, 0, 1)
      if let p = w.puppet(cow) {
        p.scale = 1.3 * easeOutBack(u)
        p.pos = muzzleOf(a) - vec3(0, 0.38 * p.scale, 0)
        p.yaw = a.yaw + 0.5
        p.ctl.wings = u
      }
      if pt > 0.8 {
        if let p = w.puppet(cow) { cp = p.pos }
        cv = vec3(0, -0.5, 0)
        w.sound(.whistle, 0.25, a.pos, 0.6)
        next()
      }
    case 3:  // down it comes
      a.ctl.hatch = approach(a.ctl.hatch, 0, dt * 2)
      cv.y -= 14 * dt
      cp += cv * dt
      spin += dt * 4
      guard let p = w.puppet(cow) else {
        next()
        break
      }
      p.pos = cp
      p.ctl.wings = 1
      p.tilt = angleAxis(0.4 * sin(spin), vec3(0, 0, 1))
      if !hit && v.alive && cp.y <= v.pos.y + v.height() * 0.85 {
        hit = true
        w.shatter(v, V + vec3(0, v.height() * 0.8, 0), vec3(0, -3, 0), 1.8, 0.4, 9)
        w.sound(.thud, 1, V, 0.8)
        w.sound(.boing, 0.9, V)
        w.shake(0.4)
        w.fx.dust(V, 20, 0.5, 1.6)
        cv = vec3(dir.x * 0.6 + 0.3, 3.2, dir.z * 0.6)
      }
      if cp.y <= 0 && cv.y < 0 {
        cp.y = 0
        p.pos = cp
        p.tilt = quat.identity
        p.ctl.wings = 0
        w.sound(.thud, 0.6, cp, 1.3)
        w.fx.dust(cp, 8, 0.25, 0.6)
        next()
      }
    case 4:  // it stands there, bewildered, and moos
      if let p = w.puppet(cow) {
        p.ctl.headYaw = 0.6 * sin(pt * 3)
        p.ctl.headPitch = -0.2
      }
      if pt > 0.3 && pt - dt <= 0.3 { w.sound(.moo, 1, cp, 0.95) }
      if pt > 1.3 {
        a.ctl.hatch = 0
        w.sound(.beam, 0.7, cp, 1.2)
        next()
      }
    case 5:  // and is beamed back up
      let u = clamp(pt / 1.6, 0, 1)
      a.ctl.hatch = 1
      let k = easeInOut(min(1, pt / 0.4))
      a.pos = vec3(mix(V.x, cp.x, k), 0, mix(V.z, cp.z, k))
      let top = muzzleOf(a)
      tractorBeam(w, top, flat(cp), 0.22, armyLight(a.color), dt, 1)
      if let p = w.puppet(cow) {
        let e = easeIn(clamp((u - 0.15) / 0.85, 0, 1))
        p.pos = mix(cp, top - vec3(0, 0.1, 0), e)
        p.scale = 1.3 * (1 - 0.85 * e)
        p.yaw += dt * 3
        p.ctl.wings = 0.4
        if u >= 1 { p.alive = false }
      }
      if u >= 1 {
        a.ctl.hatch = 0
        next()
      }
    case 6:
      if enter() { hop.begin(a, squareCenter(Int(plan.move.to)), 0, 0.1, 1.0) }
      if hop.update(w, a, dt) { next() }
    case 7:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

// ===========================================================================
// BISHOP - the big-brained psychic
// ===========================================================================
let kPsi = vec3(1.0, 0.42, 0.75)

/// Fingertips to the temples (u 0..1).
func temples(_ a: Actor, _ u: Float) {
  a.ctl.armL = vec3(2.0, 0.15, 1.5) * u
  a.ctl.armR = vec3(2.0, 0.15, 1.5) * u
  a.ctl.headPitch = -0.1 * u
}

/// Mind blast: the victim swells under the strain and pops.
final class BishopMindBlast: ABattle {
  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // concentrate
      let u = easeInOut(pt / 0.6)
      temples(a, u)
      a.glow = vec4(kPsi, 0.8 * u)
      a.ctl.hover = 0.08 * u
      if pt <= dt + 1e-6 { w.sound(.charge, 0.7, a.pos, 0.7) }
      if pt > 0.6 {
        w.sound(.theremin, 0.7, a.pos, 1.3)
        next()
      }
    case 2:  // waves of thought; the victim swells, wobbling
      let u = clamp(pt / 2.2, 0, 1)
      temples(a, 1)
      a.glow = vec4(kPsi, 0.8 + 0.6 * sin(pt * 10))
      psychicRings(w, headOf(a), victimCenter(w), kPsi, dt)
      v.scale = 1 + 0.5 * easeIn(u) + 0.04 * sin(pt * 31) * u
      v.poseSquash = 1 + 0.12 * sin(pt * 13) * u
      v.tremble = 0.4 + 0.6 * u
      v.idleWeight = 0
      v.glow = vec4(kPsi, 0.3 + 1.2 * u)
      if v.set == 2 {
        v.ctl.perk = 1
        v.ctl.jaw = 0.8 * u
      }
      w.frameLights.append(PointLight(victimCenter(w), 2.0, kPsi * (2 + 6 * u)))
      if u >= 1 {
        let c = victimCenter(w)
        w.sound(.pop, 1, c, 0.7)
        w.sound(.explode, 0.6, c, 1.4)
        w.fx.magicBurst(c, kPsi * 3, 70, 2.5)
        w.fx.flash(c, 0.8, kPsi * 5, 0.15)
        w.shatter(v, c, vec3(0, 0.5, 0), 3.0, 1.0, 12)
        w.shake(0.35)
        next()
      }
    case 3:
      let u = easeInOut(pt / 0.5)
      temples(a, 1 - u)
      a.glow = vec4(kPsi, 0.8 * (1 - u))
      a.ctl.hover = 0.08 * (1 - u)
      if pt > 0.5 { next() }
    case 4:
      if moveInA(w, dt, 1.0) { next() }
    case 5:
      return finishA(w)
    default:
      break
    }
    return false
  }
}

/// Hypnosis: the victim dances in a daze, then blasts off into the sky - and twinkles.
final class BishopHypnosis: ABattle {
  private var vy: Float = 0

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // the stare
      let u = easeInOut(pt / 0.6)
      a.glow = vec4(kPsi, 1.0 * u)
      a.ctl.armR = vec3(1.4, 0.0, -0.3) * u
      a.ctl.lean = 0.08 * u
      if pt <= dt + 1e-6 { w.sound(.theremin, 1, a.pos) }
      if pt > 0.6 { next() }
    case 2:  // the dance
      let t = pt
      psychicRings(w, headOf(a), victimCenter(w), kPsi, dt * 0.5)
      w.fx.swirl(V + vec3(0, v.height() * 0.6, 0), 0.32, kPsi, w.time)
      v.idleWeight = 0
      v.tremble = 0
      v.poseBend = vec2(0.28 * sin(t * 6), 0.1 * sin(t * 3))
      v.pos.y = 0.08 * abs(sin(t * 6))
      v.yawRate = 6
      v.yawTarget = faceAngle(A - V) + 0.9 * sin(t * 2)
      if v.set == 2 {
        v.ctl.perk = -1
        v.ctl.headYaw = 0.6 * sin(t * 4)
        v.ctl.headPitch = 0.2 * sin(t * 3)
        v.ctl.armL = vec3(1.2 + 0.6 * sin(t * 6), 0.6, 0.5)
        v.ctl.armR = vec3(1.2 + 0.6 * sin(t * 6 + kPI), 0.6, 0.5)
      }
      if fmod(t, 0.5) < dt { w.sound(.boing, 0.25, v.pos, w.fx.rnd(1.2, 1.6)) }
      if t > 2.6 {
        v.poseBend = vec2(0)
        v.pos.y = 0
        next()
      }
    case 3:  // the point to the sky...
      let u = easeInOut(pt / 0.35)
      a.ctl.armR = mix(vec3(1.4, 0.0, -0.3), vec3(2.6, 0.1, -0.3), u)
      a.ctl.headPitch = -0.3 * u
      v.ctl.perk = 1
      if pt > 0.35 {
        vy = 0.3
        w.sound(.whistle, 1, v.pos)
        w.fx.dust(V, 16, 0.3, 1.4)
        next()
      }
    case 4:  // ...and off it goes like a rocket
      vy += 18 * dt
      v.pos.y += vy * dt
      v.pos.x += 0.4 * dt * dir.x
      v.pos.z += 0.4 * dt * dir.z
      v.yawTarget = v.yaw + dt * 12
      w.fx.smoke(v.pos, 1, 0.05, -0.3, 0.2)
      w.fx.sparks(v.pos, vec3(0, -1, 0), 2, 2.0, 0.4, vec3(4.0, 2.2, 0.6))
      a.ctl.headPitch = max(-0.7, -0.3 - 0.1 * v.pos.y)
      if v.pos.y > 7 {
        w.fx.flash(v.pos, 0.4, vec3(6.0, 6.0, 7.0), 0.5)
        w.fx.flash(v.pos, 0.15, vec3(9.0), 0.8)
        w.sound(.twinkle, 1, v.pos)
        v.alive = false
        v.visible = false
        next()
      }
    case 5:  // a satisfied nod
      let u = clamp(pt / 0.7, 0, 1)
      a.ctl.armR = mix(vec3(2.6, 0.1, -0.3), vec3(0), easeInOut(u))
      a.ctl.headPitch = mix(-0.7, 0.0, u) + 0.2 * sin(u * kPI * 2)
      a.glow = vec4(kPsi, 1 - u)
      a.ctl.lean = 0.08 * (1 - u)
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

/// Telekinetic crush: lifted into the air and squeezed until it bursts.
final class BishopCrush: ABattle {
  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if faceOff(w) { next() }
    case 1:  // hands out
      let u = easeInOut(pt / 0.5)
      a.ctl.armL = vec3(1.3, -0.2, -0.4) * u
      a.ctl.armR = vec3(1.3, -0.2, -0.4) * u
      a.glow = vec4(kPsi, 0.7 * u)
      if pt > 0.5 {
        w.sound(.rumble, 0.6, V, 1.5)
        next()
      }
    case 2:  // up it floats
      let u = clamp(pt / 0.9, 0, 1)
      v.pos.y = 0.55 * easeInOut(u)
      v.idleWeight = 0
      v.tremble = 0.5
      v.glow = vec4(kPsi, 0.5 * u)
      psychicRings(w, headOf(a), victimCenter(w), kPsi, dt * 0.6)
      if v.set == 2 { v.ctl.perk = -0.6 }
      if u >= 1 { next() }
    case 3:  // and is squeezed - crunch, crunch
      let u = clamp(pt / 1.3, 0, 1)
      a.ctl.armL = vec3(1.3, -0.2 - 0.5 * u, -0.4)
      a.ctl.armR = vec3(1.3, -0.2 - 0.5 * u, -0.4)
      v.pos.y = 0.55 + 0.02 * sin(pt * 40)
      v.poseSquash = 1 - 0.55 * easeIn(u)
      v.poseBend = vec2(0.1 * sin(pt * 25), 0) * u
      v.crack = u
      v.crackColor = kPsi
      v.tremble = 1
      psychicRings(w, headOf(a), victimCenter(w), kPsi, dt)
      if fmod(pt, 0.3) < dt { w.sound(.clack, 0.6, victimCenter(w), w.fx.rnd(0.6, 0.9)) }
      if u >= 1 {
        let c = victimCenter(w)
        w.sound(.shatter, 1, c, 0.9)
        w.sound(.pop, 0.7, c, 0.6)
        w.fx.magicBurst(c, kPsi * 3, 50, 2.0)
        w.shatter(v, c, vec3(0, -0.5, 0), 2.2, 0.6, 10)
        w.shake(0.3)
        next()
      }
    case 4:
      let u = easeInOut(pt / 0.5)
      a.ctl.armL = vec3(1.3, -0.7, -0.4) * (1 - u)
      a.ctl.armR = a.ctl.armL
      a.glow = vec4(kPsi, 0.7 * (1 - u))
      if pt > 0.5 { next() }
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
