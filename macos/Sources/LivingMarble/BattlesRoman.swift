// Choreography for the Roman army. Legionaries march, chariots gallop and leap,
// the siege tower rolls, the centurion, the empress and the emperor stride
// across the board. The Romans don't shatter like marble: they fight hand to
// hand, bleed, and fall - and the dead lie in their blood for a moment before
// they fade away. Captures:
//
//   Pawn    pilum (a javelin thrown from afar), shield and gladius (blocks the
//           victim's blow with his scutum, punches with the boss and stabs),
//           vault (pole-vaults over the victim and stabs it in the back)
//   Knight  run down (horses and wheels), scythed wheels (slashes past),
//           charioteer's leap (he somersaults out of the car onto the victim)
//   Bishop  duel (parries twice, then a spinning cut and the dagger), leaping
//           strike (a somersault ending in a downward thrust), thrown knife
//   Rook    battering ram, sally (two legionaries charge out of the door and
//           cut the victim down), scorpion (a bolt from the roof)
//   Queen   hidden dagger, poisoned cup, whirling spear (sweeps the victim's
//           legs, flips it into the air and spears it on the ground)
//   King    thumbs down (a volley of pila rains from the sky), gladius duel,
//           the lion (a lion bounds onto the board and mauls the victim)
//
// Victims fight back where they can: they raise their shields and weapons and
// strike at the attacker. A siege tower that is beaten collapses in splinters;
// a chariot overturns.
//
// This file holds the toolkit (blood, keyframed sword play, falling, walking,
// the fight camera, RBattle), the quick capture, the checkmate and the
// factories. The battles themselves live in BattlesRomanPawnKnightBishop.swift
// and BattlesRomanRookRoyal.swift (as battle_roman.cpp).
// (approach, bonePoint, puppetPoint, kTau and kGold are the shared ones in
// BattlesMedieval.swift: the same as the C++ file's own.)
import Foundation
import simd

let rBloodColor = vec3(0.3, 0.004, 0.003)
let rSparkColor = vec3(3.2, 2.6, 1.8)

/// Progress through [a, b] (0 before, 1 after).
func rSeg(_ t: Float, _ a: Float, _ b: Float) -> Float { clamp((t - a) / (b - a), 0, 1) }
func rBump(_ t: Float, _ a: Float, _ b: Float) -> Float { sin(rSeg(t, a, b) * kPI) }

func rIsHuman(_ a: Actor) -> Bool { Roman.human(a.kind) }
func rIsChariot(_ a: Actor) -> Bool { a.kind == Kind.knight }
func rIsTower(_ a: Actor) -> Bool { a.kind == Kind.rook }

/// Which half-cycle of a gait we are in (a footstep falls on every change).
@inline(__always) func rHalfCycle(_ gait: Float) -> Int {
  let x = floor(gait / kPI)
  return x.isFinite ? Int(x) : 0
}

/// World position of a point in the model's (unskinned) coordinates.
func rModelPoint(_ a: Actor, _ rest: vec3) -> vec3 { vec3(a.model() * vec4(rest, 1)) }

// where blows land, the head, the weapons
func rChestOf(_ a: Actor) -> vec3 {
  if rIsChariot(a) { return bonePoint(a, CHB.driver, Roman.chestPoint(Kind.knight)) }
  if rIsTower(a) { return rModelPoint(a, Roman.chestPoint(Kind.rook)) }
  return bonePoint(a, RH.chest, Roman.chestPoint(a.kind))
}
func rHeadOf(_ a: Actor) -> vec3 {
  if rIsChariot(a) { return bonePoint(a, CHB.dhead, Roman.headPoint(Kind.knight)) }
  if rIsTower(a) { return rModelPoint(a, Roman.headPoint(Kind.rook)) }
  return bonePoint(a, RH.head, Roman.headPoint(a.kind))
}
func rBladeTipOf(_ a: Actor) -> vec3 {
  if rIsChariot(a) { return bonePoint(a, CHB.item, Roman.itemTip(Kind.knight)) }
  if a.kind == Kind.bishop { return bonePoint(a, RH.itemR, Roman.itemTip(Kind.bishop)) }
  if a.ctl.draw > 0.5 { return bonePoint(a, RH.blade, Roman.bladeTip(a.kind)) }
  return bonePoint(a, RH.itemR, Roman.itemTip(a.kind))
}
func rHandOf(_ a: Actor, _ right: Bool) -> vec3 {
  if rIsChariot(a) {
    return bonePoint(a, right ? CHB.item : CHB.dlfore, right ? Roman.handR(Kind.knight) : Roman.handL(Kind.knight))
  }
  if !rIsHuman(a) { return rChestOf(a) }
  return bonePoint(a, right ? RH.itemR : RH.itemL, right ? Roman.handR(a.kind) : Roman.handL(a.kind))
}

// ---------------------------------------------------------------------------
// Blood
// ---------------------------------------------------------------------------
func rBloodSpray(_ w: World, _ at: vec3, _ dir: vec3, _ n: Int, _ speed: Float, _ spread: Float = 0.45) {
  for _ in 0..<max(0, n) {
    var p = Particle()
    let d = normalize(dir + w.fx.rndDir() * spread + vec3(0, 0.15, 0))
    p.pos = at + w.fx.rndDir() * 0.012
    p.vel = d * speed * w.fx.rnd(0.35, 1.15)
    p.size = w.fx.rnd(0.006, 0.016)
    p.color = rBloodColor * w.fx.rnd(0.7, 1.3)
    p.colorEnd = rBloodColor * 0.55
    p.opacity = 1
    p.maxLife = w.fx.rnd(0.5, 1.1)
    p.type = .spark
    p.stretch = 0.022
    p.gravity = 9.8
    p.drag = 0.5
    p.fadeIn = 0
    w.fx.add(p)
  }
  for _ in 0..<max(0, n / 7 + 1) {  // a fine red mist
    var p = Particle()
    p.pos = at + w.fx.rndDir() * 0.02
    p.vel = normalize(dir + w.fx.rndDir() * 0.6) * w.fx.rnd(0.2, 0.6)
    p.size = w.fx.rnd(0.02, 0.04)
    p.grow = 0.12
    p.color = rBloodColor * 0.9
    p.colorEnd = rBloodColor * 0.5
    p.opacity = w.fx.rnd(0.25, 0.45)
    p.maxLife = w.fx.rnd(0.35, 0.6)
    p.type = .smoke
    p.drag = 3
    p.gravity = 0.3
    p.rot = w.fx.rnd(0, 6.28)
    p.seed = w.fx.rnd()
    p.fadeIn = 0.02
    w.fx.add(p)
  }
}

/// Successive pools sit a hair above each other.
var rPoolLift: Float = 0

/// A pool of blood spreading on the board (it fades long after the fight).
func rBloodPool(_ w: World, _ at: vec3, _ size: Float, _ spread: Float = 1, _ life: Float = 7) {
  if !overBoard(at) { return }
  let id = w.addPuppet(RAsset.pool, 0, 3)
  guard let p = w.puppet(id) else { return }
  rPoolLift = rPoolLift > 0.0012 ? 0 : rPoolLift + 0.00015
  p.pos = vec3(at.x, 0.0012 + rPoolLift, at.z)
  p.yaw = w.fx.rnd(0, kTau)
  p.scale = 0.004
  p.growTo = size
  p.growRate = size / max(0.05, spread)
  p.life = life
  p.fadeRate = 0.35
  p.shadow = false
}

/// Drops and splashes thrown along 'dir'.
func rSplatter(_ w: World, _ at: vec3, _ dir: vec3, _ n: Int, _ reach: Float = 0.4) {
  let d = flat(dir)
  for _ in 0..<max(0, n) {
    let along = d * w.fx.rnd(0.05, reach)
    let ox = w.fx.rnd(-1, 1)
    let oz = w.fx.rnd(-1, 1)
    let p = flat(at) + along + vec3(ox, 0, oz) * 0.07
    let size = w.fx.rnd(0.025, 0.06)
    rBloodPool(w, p, size, 0.15, w.fx.rnd(6, 9))
  }
}

/// A wound: the spray, splashes on the board and the sounds of it.
func rWound(_ w: World, _ at: vec3, _ dir: vec3, _ power: Float, _ slash: Bool = false) {
  rBloodSpray(w, at, dir, Int(80 * power), 2.2 + 1.6 * power)
  rSplatter(w, at, dir, 4 + Int(4 * power))
  let cut: Sfx = slash ? .slash : .stab
  w.sound(cut, 0.9, at, w.fx.rnd(0.9, 1.1))
  w.sound(.blood, 0.55 * power, at, w.fx.rnd(0.9, 1.15))
}

func rSparksAt(_ w: World, _ at: vec3, _ dir: vec3, _ n: Int = 22) {
  w.fx.sparks(at, dir, n, 3.0, 0.7, rSparkColor)
  w.fx.flash(at, 0.25, vec3(2.2, 2.0, 1.6), 0.07)
  w.light(at, vec3(5.0, 4.2, 3.2), 2.0, 0.12)
}

/// Wooden splinters (a shield struck, the tower breaking up).
func rSplinters(_ w: World, _ at: vec3, _ dir: vec3, _ n: Int) {
  for _ in 0..<max(0, n) {
    let d = normalize(dir + w.fx.rndDir() * 0.9 + vec3(0, 0.4, 0))
    var p = Particle()
    p.pos = at
    p.vel = d * w.fx.rnd(1.5, 4.5)
    p.size = w.fx.rnd(0.006, 0.013)
    p.color = vec3(0.45, 0.3, 0.16) * w.fx.rnd(0.6, 1.2)
    p.colorEnd = p.color
    p.opacity = 1
    p.maxLife = w.fx.rnd(0.8, 1.6)
    p.type = .spark
    p.stretch = 0.02
    p.gravity = 9.8
    p.drag = 0.6
    p.bounce = true
    p.fadeIn = 0
    w.fx.add(p)
  }
}

// ---------------------------------------------------------------------------
// Poses by keyframes: a human's (or the charioteer's) arms, torso and weapon
// ---------------------------------------------------------------------------
struct RKey {
  var t: Float
  var armR: vec3, armL: vec3
  var lean: Float, twist: Float, crouch: Float, pitch: Float

  init(_ t: Float, _ aR: vec3, _ aL: vec3 = vec3(0), _ lean: Float = 0, _ twist: Float = 0, _ crouch: Float = 0,
       _ pitch: Float = 0) {
    self.t = t
    armR = aR
    armL = aL
    self.lean = lean
    self.twist = twist
    self.crouch = crouch
    self.pitch = pitch
  }
}
/// The C++ K(...): a key at time t.
func rK(_ t: Float, _ aR: vec3, _ aL: vec3 = vec3(0), _ lean: Float = 0, _ twist: Float = 0, _ crouch: Float = 0,
        _ pitch: Float = 0) -> RKey {
  RKey(t, aR, aL, lean, twist, crouch, pitch)
}

/// Eases between the keys (held before the first and after the last).
func rPlay(_ c: inout MControls, _ keys: [RKey], _ t: Float) {
  let n = keys.count
  if n == 0 { return }
  var i = 0
  while i + 1 < n && t > keys[i + 1].t { i += 1 }
  let a = keys[i]
  let b = keys[min(i + 1, n - 1)]
  var u: Float
  if i + 1 < n && b.t > a.t {
    u = smoothstep(a.t, b.t, t)
  } else {
    u = t >= b.t ? 1 : 0
  }
  if i + 1 >= n { u = 1 }
  c.armR = mix(a.armR, b.armR, u)
  c.armL = mix(a.armL, b.armL, u)
  c.lean = mix(a.lean, b.lean, u)
  c.twist = mix(a.twist, b.twist, u)
  c.crouch = mix(a.crouch, b.crouch, u)
  c.itemPitch = mix(a.pitch, b.pitch, u)
}

// A fighter's moves, as functions of their progress u (0..1).
/// On guard: weapon up and forward, shield (or left hand) raised.
func rGuard(_ c: inout MControls, _ u: Float, _ shield: Bool) {
  c.armR = vec3(0.45, 0.1, 0.45) * u
  let l = shield ? vec3(0.55, 0.05, -0.15) : vec3(0.4, 0.1, 0.3)
  c.armL = l * u
  c.itemPitch = 0.6 * u
  c.crouch = 0.15 * u
  c.lean = 0.06 * u
}
/// An overhead blow: wind up (to 0.45), strike (to 0.6), recover.
func rOverhead(_ c: inout MControls, _ u: Float) {
  let keys: [RKey] = [
    rK(0.0, vec3(0.45, 0.1, 0.45), vec3(0.4, 0.1, 0.2), 0.06, 0, 0.15, 0.6),
    rK(0.42, vec3(2.7, 0.25, 1.1), vec3(0.6, 0.2, 0.4), -0.12, 0.2, 0.05, -0.4),
    rK(0.58, vec3(0.75, 0.1, -0.25), vec3(0.2, 0.2, 0.2), 0.3, -0.15, 0.25, 1.5),
    rK(1.0, vec3(0.45, 0.1, 0.45), vec3(0.4, 0.1, 0.2), 0.06, 0, 0.15, 0.6),
  ]
  rPlay(&c, keys, u)
  c.gait = kPI * 0.5
  c.stride = 0.5 * rBump(u, 0.35, 1.0)
}
/// A thrust: draw back (to 0.4), lunge (to 0.55), recover.
func rThrust(_ c: inout MControls, _ u: Float) {
  let keys: [RKey] = [
    rK(0.0, vec3(0.45, 0.1, 0.45), vec3(0.4, 0.1, 0.2), 0.06, 0, 0.15, 0.6),
    rK(0.4, vec3(0.1, 0.25, 1.2), vec3(0.6, 0.0, 0.0), -0.05, -0.35, 0.2, 1.4),
    rK(0.55, vec3(1.25, 0.0, -0.55), vec3(0.2, 0.1, 0.3), 0.32, 0.3, 0.3, 1.55),
    rK(1.0, vec3(0.45, 0.1, 0.45), vec3(0.4, 0.1, 0.2), 0.06, 0, 0.15, 0.6),
  ]
  rPlay(&c, keys, u)
  c.gait = kPI * 0.5
  c.stride = 0.7 * rBump(u, 0.35, 1.0)
}
/// A sweeping cut from right to left.
func rSweep(_ c: inout MControls, _ u: Float) {
  let keys: [RKey] = [
    rK(0.0, vec3(0.45, 0.1, 0.45), vec3(0.4, 0.1, 0.2), 0.06, 0, 0.15, 0.6),
    rK(0.4, vec3(1.3, 1.3, 0.3), vec3(0.6, 0.3, 0.3), 0.0, 0.75, 0.2, 1.5),
    rK(0.6, vec3(1.4, -0.45, 0.1), vec3(0.3, 0.1, 0.1), 0.18, -0.8, 0.25, 1.5),
    rK(1.0, vec3(0.45, 0.1, 0.45), vec3(0.4, 0.1, 0.2), 0.06, 0, 0.15, 0.6),
  ]
  rPlay(&c, keys, u)
}
/// A parry: the blade raised across the body to catch a blow.
func rParry(_ c: inout MControls, _ u: Float, _ high: Bool) {
  let r = high ? vec3(1.9, 0.15, 0.55) : vec3(1.0, -0.25, 0.6)
  c.armR = mix(vec3(0.45, 0.1, 0.45), r, u)
  let pitchTo: Float = high ? 1.6 : 0.4
  c.itemPitch = mix(0.6, pitchTo, u)
  let yawTo: Float = high ? 1.2 : 0.0
  c.itemYaw = yawTo * u
  c.crouch = 0.15 + 0.1 * u
  c.lean = 0.06 - 0.12 * u
}
/// The scutum (or the left arm) thrown up against a blow.
func rBlock(_ c: inout MControls, _ u: Float) {
  c.armL = mix(vec3(0.55, 0.05, -0.15), vec3(1.25, 0.0, -0.2), u)
  c.crouch = 0.15 + 0.15 * u
  c.lean = 0.06 - 0.1 * u
}

// The victim's part: on guard, and striking at the attacker (whatever it is).
func rVictimGuard(_ v: Actor, _ u: Float) {
  if rIsHuman(v) {
    if v.kind == Kind.pawn { v.ctl.draw = u > 0.3 ? 1 : 0 }  // a legionary draws his sword
    if v.kind == Kind.queen || v.kind == Kind.king { v.ctl.draw = u > 0.3 ? 1 : 0 }
    rGuard(&v.ctl, u, v.kind == Kind.pawn)
  } else if rIsChariot(v) {
    v.ctl.armR = vec3(0.6, 0.0, 0.4) * u
    v.ctl.itemPitch = 1.2 * u
    v.ctl.headYaw = 0
  }
}
func rVictimStrike(_ v: Actor, _ u: Float) {
  if rIsHuman(v) {
    rOverhead(&v.ctl, u)
  } else if rIsChariot(v) {
    let keys: [RKey] = [
      rK(0.0, vec3(0.6, 0.0, 0.4), vec3(0), 0, 0, 0, 1.2),
      rK(0.4, vec3(0.2, 0.0, 0.9), vec3(0), -0.15, -0.2, 0, 1.3),
      rK(0.55, vec3(1.0, 0.0, -0.3), vec3(0), 0.3, 0.2, 0, 1.4),
      rK(1.0, vec3(0.6, 0.0, 0.4), vec3(0), 0, 0, 0, 1.2),
    ]
    rPlay(&v.ctl, keys, u)
    v.ctl.rear = 0.3 * rBump(u, 0.2, 0.8)
  }
}

// ---------------------------------------------------------------------------
// Falling: a Roman struck down staggers, his knees give way and he falls on his
// back (or on his face); a chariot overturns; a siege tower collapses. The body
// lies in its blood a moment, then fades away (a puppet: the piece is gone).
// ---------------------------------------------------------------------------
/// Replaces the actor by a puppet in the same pose (it fades 'life' seconds from
/// now); the actor is gone. Returns the puppet's id.
@discardableResult
func rMakeCorpse(_ w: World, _ v: Actor, _ life: Float) -> Int {
  let id = w.addPuppet(v.kind, v.color, 3)
  if let p = w.puppet(id) {
    p.ctl = v.ctl
    p.pose = v.pose
    p.yaw = v.yaw
    p.scale = v.scale
    p.tilt = v.tilt
    let piv = v.tiltPivot * v.scale
    p.pos = v.pos + angleAxis(v.yaw, vec3(0, 1, 0)) * (piv - v.tilt * piv)
    p.texOffset = v.texOffset
    p.life = life
    p.fadeRate = 0.9
    p.interior = 1
  }
  v.alive = false
  v.visible = false
  return id
}

/// The siege tower breaks up into timbers.
func rCollapseTower(_ w: World, _ v: Actor, _ impact: vec3, _ push: vec3) {
  let c = v.pos + vec3(0, v.height() * 0.5, 0)
  rSplinters(w, c, push + vec3(0, 0.5, 0), 70)
  w.fx.dust(flat(v.pos), 30, 0.6, 1.6)
  w.fx.smoke(c, 10, 0.3, 0.4, 0.5)
  w.sound(.splinter, 1.0, c, 0.7)
  w.sound(.creak, 0.8, c, 0.8)
  w.shatter(v, impact, push, 1.2, 0.6, 6)
  w.shake(0.4)
}

struct RFall {
  var actor = -1, corpse = -1
  var t: Float = 0, dir: Float = 1   // +1 on the back, -1 on the face (chariot: +1 tips to its left, -1 right)
  var stagger: Float = 0.35          // seconds of staggering first
  var push = vec3(0)                 // knocked back along this (world, per second)
  var landed = false, done = false, cry = true
  var life: Float = 1.6              // seconds the body lies there before it fades
  var bleed: Float = 1               // the size of the pool it leaves
  var settle: Float = 0
  func active() -> Bool { actor >= 0 && !done }

  mutating func start(_ w: World, _ v: Actor, _ direction: Float, _ pushVel: vec3, _ staggerTime: Float = 0.35) {
    actor = v.id
    dir = direction
    push = flat(pushVel)
    stagger = staggerTime
    t = 0
    landed = false
    done = false
    v.idleWeight = 0
    v.tremble = 0
    v.yawRate = 2
    if rIsTower(v) {
      rCollapseTower(w, v, rChestOf(v), push * 2 + vec3(0, 0.5, 0))
      done = true
      return
    }
    if cry {
      let ch = rIsChariot(v)
      let sfx: Sfx = ch ? .neigh : .death
      w.sound(sfx, ch ? 1.0 : 0.8, rChestOf(v), w.fx.rnd(0.85, 1.1))
    }
  }

  /// True once the body is down (and has become a corpse), or there is nothing to do.
  @discardableResult
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if done || actor < 0 { return true }
    let v = w.actors[actor]
    if !v.alive {
      done = true
      return true
    }
    t += dt
    v.idleWeight = 0
    v.tremble = 0
    if rIsChariot(v) { return updateChariot(w, v, dt) }
    let leanTo: Float = dir > 0 ? -0.25 : 0.45
    if t < stagger {  // staggering: reeling back (or doubling over), arms thrown out
      let u = t / max(stagger, 1e-3)
      v.pos += push * dt * (1 - 0.5 * u)
      let headTo: Float = dir > 0 ? -0.3 : 0.3
      v.ctl.lean = mix(v.ctl.lean, leanTo, min(1, dt * 8))
      v.ctl.headPitch = mix(v.ctl.headPitch, headTo, min(1, dt * 8))
      v.ctl.crouch = approach(v.ctl.crouch, 0.35, dt * 1.5)
      v.ctl.armR = mix(v.ctl.armR, vec3(0.6, 0.5, 0.6), min(1, dt * 5))
      v.ctl.armL = mix(v.ctl.armL, vec3(0.7, 0.5, 0.6), min(1, dt * 5))
      v.ctl.stride = 0.3 * (1 - u)
      v.ctl.gait += dt * 6
      return false
    }
    let ft = t - stagger
    let dur: Float = 0.55
    let u = clamp(ft / dur, 0, 1)
    var ang: Float = 1.5 * u * u  // falling faster and faster
    if !landed && u >= 1 {
      landed = true
      settle = 0
      let p = flat(v.pos)
      w.sound(.fall, 0.9, p, w.fx.rnd(0.9, 1.1))
      w.fx.dust(p - v.forward() * dir * 0.3, 14, 0.35, 0.9)
      w.shake(0.12)
      // the blood pools under the body
      let chest = flat(v.pos) - v.forward() * dir * 0.3 * v.scale
      rBloodPool(w, chest, 0.27 * bleed * v.scale, 1.5, 9)
    }
    if landed {  // a little bounce, then still
      settle += dt
      ang = 1.5 - 0.08 * exp(-settle * 9) * sin(settle * 26)
    } else {
      v.pos += push * dt * (1 - u) * 0.6
    }
    v.tiltPivot = vec3(0, 0, -dir * 0.075)
    v.tilt = angleAxis(-dir * ang, vec3(1, 0, 0))
    v.ctl.dead = u
    v.ctl.crouch = 0.35 * (1 - u)
    v.ctl.lean = mix(leanTo, 0, u)
    v.ctl.stride = 0
    if landed && settle > 0.4 {
      corpse = rMakeCorpse(w, v, life)
      done = true
    }
    return done
  }

  mutating func updateChariot(_ w: World, _ v: Actor, _ dt: Float) -> Bool {
    if t < stagger {  // the horses rear and scream, the car slews
      v.ctl.rear = approach(v.ctl.rear, 0.8, dt * 3)
      v.pos += push * dt
      v.ctl.stride = 0
      return false
    }
    let u = clamp((t - stagger) / 0.6, 0, 1)
    var ang: Float = 1.45 * u * u
    v.ctl.rear = 0.8 * (1 - u)
    if !landed && u >= 1 {
      landed = true
      settle = 0
      let p = flat(v.pos)
      w.sound(.fall, 1.0, p, 0.8)
      w.sound(.splinter, 0.8, p, 0.9)
      w.fx.dust(p, 26, 0.5, 1.4)
      rSplinters(w, p + vec3(0, 0.15, 0), vec3(0, 1, 0), 30)
      w.shake(0.3)
      rBloodPool(w, p + v.forward() * 0.1, 0.24 * bleed, 1.5, 9)
    }
    if landed {
      settle += dt
      ang = 1.45 - 0.07 * exp(-settle * 9) * sin(settle * 24)
    } else {
      v.pos += push * dt * (1 - u)
    }
    v.tiltPivot = vec3(dir * 0.2, 0, 0)
    v.tilt = angleAxis(-dir * ang, vec3(0, 0, 1))
    v.ctl.dead = u
    if landed && settle > 0.45 {
      corpse = rMakeCorpse(w, v, life)
      done = true
    }
    return done
  }
}

/// A body knocked flying: it tumbles through the air, crashes down, slides to a
/// stop on its back or its face and lies there (then fades). Spinning about its
/// middle. (A chariot overturns instead, a siege tower collapses: see knockFlying.)
struct RTumble {
  var actor = -1, corpse = -1
  var vel = vec3(0)
  var spin: Float = 0, angle: Float = 0, target: Float = 0, t: Float = 0, settle: Float = 0, hmid: Float = 0.4
  var landed = false, done = false
  var life: Float = 1.6, bleed: Float = 1
  func active() -> Bool { actor >= 0 && !done }

  mutating func launch(_ w: World, _ v: Actor, _ velocity: vec3, _ spinRate: Float) {
    actor = v.id
    vel = velocity
    spin = spinRate
    angle = 0
    t = 0
    settle = 0
    landed = false
    done = false
    v.idleWeight = 0
    v.tremble = 0
    hmid = v.height() / max(v.scale, 1e-3) * 0.45
    v.tiltPivot = vec3(0, hmid, 0)
    w.sound(.death, 0.8, rChestOf(v), w.fx.rnd(0.9, 1.15))
  }

  /// True once the body lies still (and has become a corpse), or has fallen off the board.
  @discardableResult
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if done || actor < 0 { return true }
    let v = w.actors[actor]
    if !v.alive {
      done = true
      return true
    }
    t += dt
    v.idleWeight = 0
    v.tremble = 0
    if !landed {
      vel.y -= 14 * dt
      v.pos += vel * dt
      angle += spin * dt
      v.tilt = angleAxis(-angle, vec3(1, 0, 0))
      v.ctl.dead = min(1, 0.5 + t)
      v.ctl.cape = 1
      let h = hmid * v.scale
      let reach = abs(cos(angle)) * h + abs(sin(angle)) * 0.07 * v.scale
      let low = v.pos.y + h - reach
      if overBoard(v.pos) && low <= 0 && vel.y < 0 && t > 0.15 {
        landed = true
        // lie on the back or on the face, whichever is nearer
        let k = ((angle - kPI * 0.5) / kPI).rounded()
        target = kPI * 0.5 + k * kPI
        let p = flat(v.pos)
        w.sound(.fall, 1.0, p, w.fx.rnd(0.85, 1.0))
        w.fx.dust(p, 18, 0.4, 1.2)
        w.shake(0.18)
        rBloodPool(w, p, 0.27 * bleed * v.scale, 1.4, 9)
        rSplatter(w, p, vel, 3, 0.35)
      }
      if v.pos.y < -2.5 || t > 3.5 {  // off the edge of the board, falling away
        v.alive = false
        v.visible = false
        done = true
      }
      return done
    }
    settle += dt
    let slow = exp(-dt * 6)
    vel.x *= slow
    vel.z *= slow
    v.pos.x += vel.x * dt
    v.pos.z += vel.z * dt
    angle += (target - angle) * (1 - exp(-dt * 16))
    let restY = (0.065 - hmid) * v.scale
    v.pos.y += (restY - v.pos.y) * (1 - exp(-dt * 18))
    v.tilt = angleAxis(-angle, vec3(1, 0, 0))
    v.ctl.dead = 1
    v.ctl.cape = approach(v.ctl.cape, 0, dt * 3)
    if settle > 0.45 {
      corpse = rMakeCorpse(w, v, life)
      done = true
    }
    return done
  }
}

// ---------------------------------------------------------------------------
// RWalker: on foot, at the gallop (the chariot) or rolling (the siege tower)
// ---------------------------------------------------------------------------
struct RWalker {
  enum Mode { case foot, chariot, tower }
  var actor = -1
  var from = vec3(0), to = vec3(0)
  var mode = Mode.foot
  var speed: Float = 0.8, accel: Float = 1.6, cycle: Float = 0.3, run: Float = 0
  var s: Float = 0, v: Float = 0, len: Float = 0, turnWait: Float = 0, creak: Float = 0
  var lastStep = 0
  var begun = false, face = true, soundOn = true

  init() {}
  init(_ a: Int, _ dest: vec3, _ m: Mode, _ spd: Float) {
    actor = a
    to = flat(dest)
    mode = m
    speed = spd
  }

  mutating func begin(_ w: World) {
    let a = w.actors[actor]
    from = flat(a.pos)
    len = length(to - from)
    cycle = Roman.strideCycle(a.kind)
    accel = mode == .tower ? speed * 1.6 : speed * 2.2
    if face && len > 0.05 {
      a.yawTarget = faceAngle(to - from)
      let most: Float = mode == .tower ? 0.9 : 0.55
      turnWait = min(most, abs(wrapAngle(a.yawTarget - a.yaw)) * 0.3)
    }
    lastStep = rHalfCycle(a.ctl.gait)
    begun = true
    if !soundOn || len < 0.05 { return }
    if mode == .chariot { w.sound(.wheels, min(1, 0.4 + 0.25 * len), a.pos, w.fx.rnd(0.9, 1.1)) }
    if mode == .tower {
      w.sound(.creak, 0.7, a.pos, w.fx.rnd(0.85, 1.05))
      w.sound(.grind, 0.35, a.pos, 0.7)
    }
  }

  func footstep(_ w: World, _ a: Actor) {
    if !soundOn { return }
    let p = flat(a.pos)
    if mode == .foot {
      if a.kind == Kind.pawn || a.kind == Kind.bishop {
        w.sound(.armor, 0.4, p, w.fx.rnd(0.95, 1.2))
      } else {
        w.sound(.step, 0.35, p, w.fx.rnd(0.9, 1.05))
      }
      if speed > 1.3 { w.fx.dust(p, 1, 0.08, 0.3) }
    } else if mode == .chariot {
      w.sound(.hoof, 0.45 + 0.3 * run, p + a.forward() * 0.3, w.fx.rnd(0.95, 1.2))
      if run > 0.4 { w.fx.dust(p + a.forward() * 0.25, 2, 0.15, 0.6) }
    }
  }

  /// True once it has arrived and come to rest.
  @discardableResult
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if !begun { begin(w) }
    let a = w.actors[actor]
    if turnWait > 0 {
      turnWait -= dt
      if mode != .tower {  // shuffle round on the spot
        a.ctl.gait += dt * 4
        a.ctl.stride = approach(a.ctl.stride, 0.25, dt * 2)
      }
      return false
    }
    let rem = len - s
    let vt = min(speed, sqrt(2 * accel * max(rem, 0)))
    v = v < vt ? min(vt, v + accel * dt) : max(vt, v - accel * 2.5 * dt)
    if rem > 1e-3 && v < 0.08 { v = 0.08 }
    s = min(len, s + v * dt)
    let p = len > 1e-4 ? mix(from, to, s / len) : to
    a.pos.x = p.x
    a.pos.z = p.z
    switch mode {
    case .foot:
      let pace: Float = v > 1.2 ? 0.75 : 1.0
      a.ctl.gait += v * dt / cycle * kTau * pace
      a.ctl.stride = clamp(v / max(0.1, min(speed, 0.9) * 0.6), 0, 1)
      let leanTo: Float = v > 1.2 ? 0.18 : 0
      a.ctl.lean = approach(a.ctl.lean, leanTo, dt * 1.5)
      let capeTo: Float = v > 1.2 ? 0.6 : 0.15 * v
      a.ctl.cape = approach(a.ctl.cape, capeTo, dt * 2)
    case .chariot:
      a.ctl.gait += v * dt / cycle * kTau
      a.ctl.stride = clamp(v / max(0.1, speed * 0.5), 0, 1)
      a.ctl.run = approach(a.ctl.run, run, dt * 3)
      a.ctl.spin += v * dt / Roman.wheelRadius(Kind.knight)
    case .tower:
      a.ctl.spin += v * dt / Roman.wheelRadius(Kind.rook)
      a.ctl.gait += v * dt / 0.18 * kTau  // rocking on its wheels
      a.ctl.stride = clamp(v / max(0.1, speed), 0, 1)
      creak -= dt
      if creak <= 0 && soundOn {
        creak = w.fx.rnd(0.7, 1.2)
        w.sound(.creak, 0.45, a.pos, w.fx.rnd(0.8, 1.1))
      }
      if w.fx.rnd() < dt * 8 {
        let ox = w.fx.rnd(-0.2, 0.2)
        let oz = w.fx.rnd(-0.2, 0.2)
        w.fx.dust(p + vec3(ox, 0, oz), 1, 0.05, 0.2)
      }
    }
    if mode != .tower {
      let step = rHalfCycle(a.ctl.gait)
      if step != lastStep {
        lastStep = step
        footstep(w, a)
      }
    }
    if s >= len - 1e-4 {
      a.ctl.stride = approach(a.ctl.stride, 0, dt * 4)
      a.ctl.cape = approach(a.ctl.cape, 0, dt * 2)
      a.ctl.lean = approach(a.ctl.lean, 0, dt * 2)
      if mode == .chariot { a.ctl.run = approach(a.ctl.run, 0, dt * 3) }
      return a.ctl.stride <= 0.001
    }
    return false
  }
}

func rModeOf(_ a: Actor) -> RWalker.Mode {
  rIsChariot(a) ? .chariot : (rIsTower(a) ? .tower : .foot)
}
func rPaceOf(_ a: Actor, _ dist: Float) -> Float {
  switch a.kind {
  case Kind.pawn: return max(0.8, dist / 2.0)
  case Kind.knight: return max(1.6, dist / 1.4)
  case Kind.rook: return max(0.7, dist / 2.6)
  case Kind.king: return max(0.7, dist / 2.4)
  default: return max(0.85, dist / 2.1)
  }
}

/// A somersault: the actor flies along a parabola from 'from' to 'to', turning head over
/// heels 'turns' times (forwards), knees tucked in.
struct RFlip {
  var from = vec3(0), to = vec3(0)
  var height: Float = 0.6, dur: Float = 0.8, turns: Float = 1, t: Float = 0
  @discardableResult
  mutating func update(_ a: Actor, _ dt: Float) -> Bool {
    t += dt
    let u = clamp(t / dur, 0, 1)
    var p = mix(from, to, u)
    p.y = mix(from.y, to.y, u) + 4 * u * (1 - u) * height
    a.pos = p
    let spin = turns * kTau * smoothstep(0.08, 0.92, u)
    a.tiltPivot = vec3(0, a.height() / max(a.scale, 1e-3) * 0.45, 0)
    a.tilt = angleAxis(spin, vec3(1, 0, 0))
    a.ctl.tuck = sin(u * kPI) * 0.9
    a.ctl.cape = 0.9
    return u >= 1
  }
}

// ---------------------------------------------------------------------------
// Quiet moves (and castling)
// ---------------------------------------------------------------------------
final class RQuietMove: Staged {
  let plan: MovePlan
  var walker = RWalker(), rookWalker = RWalker()
  var A = vec3(0), B = vec3(0)
  var dur: Float = 1
  var arcSound = false
  init(_ p: MovePlan) { plan = p }

  /// The chariot: a few strides at the gallop, then it leaps whatever is in the way.
  private func chariotLeap(_ w: World, _ a: Actor, _ dt: Float) -> Bool {
    switch phase {
    case 0:
      if enter() {
        A = flat(a.pos)
        B = squareCenter(Int(plan.move.to))
        a.yawTarget = faceAngle(B - A)
        w.sound(.whip, 0.6, a.pos, w.fx.rnd(0.95, 1.1))
        w.sound(.neigh, 0.35, a.pos, w.fx.rnd(1.0, 1.15))
      }
      a.ctl.gait += dt * 8
      a.ctl.stride = approach(a.ctl.stride, 0.6, dt * 2)
      a.ctl.run = 0.6
      a.ctl.armR = vec3(1.2 * rBump(pt, 0, 0.4), 0.2, 0.5)  // the spear raised: "Hyah!"
      if pt > 0.5 {
        dur = 0.85 + 0.07 * length(B - A)
        w.sound(.wheels, 0.8, a.pos, 1.05)
        w.sound(.whoosh, 0.5, a.pos, 0.7)
        next()
      }
    case 1:  // the leap: the horses gather and fly, the car bounds after them
      let u = clamp(pt / dur, 0, 1)
      var p = mix(A, B, easeInOut(u) * 0.75 + u * 0.25)
      p.y = 4 * u * (1 - u) * 0.55
      a.pos = p
      a.ctl.tuck = sin(u * kPI)
      a.ctl.gait += dt * 6
      a.ctl.stride = 0.5
      a.ctl.run = 1
      a.ctl.spin += dt * 9
      a.ctl.cape = 1
      a.ctl.armR = vec3(0)
      if u >= 1 {
        a.pos = B
        a.ctl.tuck = 0
        w.sound(.hoof, 0.9, B, 0.85)
        w.sound(.thud, 0.7, B, 1.1)
        w.sound(.wheels, 0.5, B, 0.9)
        w.fx.dust(B, 14, 0.4, 1.2)
        w.shake(0.1)
        next()
      }
    case 2:  // pull up, toss of the heads
      if enter() { a.yawTarget = a.baseYaw }
      let s = rSeg(pt, 0, 0.6)
      a.ctl.headPitch = -0.25 * rBump(pt, 0, 0.6)
      a.ctl.gait += dt * 5
      a.ctl.stride = 0.3 * (1 - s)
      a.ctl.run = 0.3 * (1 - s)
      a.ctl.spin += dt * 3 * (1 - s)
      a.ctl.cape = 1 - s
      if pt > 0.65 {
        a.ctl.headPitch = 0
        a.ctl.stride = 0
        a.ctl.run = 0
        a.ctl.cape = 0
        return true
      }
    default:
      break
    }
    return false
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    if a.kind == Kind.knight { return chariotLeap(w, a, dt) }
    switch phase {
    case 0:
      if enter() {
        A = flat(a.pos)
        B = squareCenter(Int(plan.move.to))
        let dist = length(B - A)
        walker = RWalker(plan.mover, B, rModeOf(a), rPaceOf(a, dist))
        if plan.rook >= 0 {
          rookWalker = RWalker(plan.rook, squareCenter(plan.rookTo), .tower, 0.8)
        }
      }
      let d1: Bool
      if plan.rook >= 0 {
        // castling: the emperor strides round in an arc behind the rolling tower (on the board's
        // edge side of his rank, clear of the tower and of his own men)
        let u = clamp(pt / 1.9, 0, 1), e = easeInOut(u)
        let toEdge = vec3(0, 0, a.color == 0 ? 1 : -1)
        let half: vec3 = (A + B) * 0.5
        let W: vec3 = half + toEdge * 1.1
        let pa = (1 - e) * (1 - e) * A
        let pw = 2 * (1 - e) * e * W
        let p = pa + pw + e * e * B
        let prev = a.pos
        a.pos = vec3(p.x, 0, p.z)
        let d = flat(a.pos - prev)
        if length(d) > 1e-5 { a.yawTarget = faceAngle(d) }
        a.ctl.gait += length(d) / Roman.strideCycle(a.kind) * kTau
        a.ctl.stride = u < 1 ? 0.8 : approach(a.ctl.stride, 0, dt * 4)
        if !arcSound {
          arcSound = true
          w.sound(.cornu, 0.35, a.pos, 1.0)
        }
        d1 = u >= 1 && a.ctl.stride <= 0.001
      } else {
        d1 = walker.update(w, dt)
      }
      var d2 = true
      if plan.rook >= 0 { d2 = pt > 0.2 ? rookWalker.update(w, dt) : false }
      if d1 && d2 { next() }
    case 1:
      if enter() {
        a.yawTarget = a.baseYaw
        a.ctl.lean = 0
        a.ctl.cape = 0
        if plan.rook >= 0 { w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw }
      }
      if pt > 0.3 { return true }
    default:
      break
    }
    return false
  }
}

/// The cinematic camera for a fight: like battleShot, but it looks at the two
/// fighters from the side (so that a blow, a parry or an offered cup is not hidden
/// behind the attacker's back) and comes a little closer.
func rFightShot(_ w: World, _ A: vec3, _ V: vec3, _ heightBias: Float, _ extra: Float, _ attacker: Int,
                _ victim: Int) {
  if !w.cinematic { return }
  let mid = A * 0.4 + V * 0.6
  let dir = flatDir(A, V)
  let L = length(flat(V - A))
  let side = vec3(-dir.z, 0, dir.x)
  let orbitCam = w.cam.orbitPose().pos
  let toViewer = normalize(flat(orbitCam - mid) + vec3(1e-4))
  let target = mid + vec3(0, heightBias, 0)
  let dist = 2.9 + L * 0.4 + extra
  let baseH = 0.85 + L * 0.12 + extra * 0.3
  let look = [A + vec3(0, 0.6, 0), V + vec3(0, 0.5, 0), target]
  var bestScore: Float = 1e9
  var best = target + side * dist
  for sgn in stride(from: -1, through: 1, by: 2) {
    for k in -4...4 {
      let ang = radians(15 * Float(k))
      let sd = side * Float(sgn)
      let offDir = vec3(cos(ang) * sd.x - sin(ang) * sd.z, 0, sin(ang) * sd.x + cos(ang) * sd.z)
      for e in 0..<3 {
        let cam = target + offDir * dist * (1 - 0.08 * Float(e)) + vec3(0, baseH + 0.7 * Float(e), 0)
        var occ: Float = 0
        for a in w.actors {
          if !a.alive || !a.visible || a.id == attacker || a.id == victim { continue }
          let b0 = a.pos, b1 = a.pos + vec3(0, a.height(), 0)
          let r = a.radius() * 1.05
          if length(flat(cam - a.pos)) < r + 0.35 && cam.y < b1.y + 0.3 { occ += 6 }
          for p in look {
            if segSegDist(cam, p, b0, b1) < r {
              let near = length(flat(a.pos - cam)) / max(0.1, length(flat(p - cam)))
              occ += near < 0.5 ? 2.5 : 1.0
            }
          }
        }
        var score = occ * 4
        score += (1 - dot(offDir, toViewer)) * 1.0
        score += abs(dot(offDir, dir)) * 2.6
        score += Float(e) * 0.6
        if score < bestScore {
          bestScore = score
          best = cam
        }
      }
    }
  }
  var shot = CamPose()
  shot.pos = best
  shot.target = target
  shot.fov = 34
  shot.focus = length(best - target)
  shot.aperture = 7
  w.cam.setShot(shot, 0.75)
}

// ---------------------------------------------------------------------------
// Shared prelude and epilogue for the Romans' battles
// ---------------------------------------------------------------------------
class RBattle: BattleBase {
  var walker = RWalker()
  var fall = RFall()
  var tumble = RTumble()

  override init(_ p: MovePlan) { super.init(p) }

  /// Staged clears 'first' after every step (so on its own it only holds for the very
  /// first frame); the Romans' phases set themselves up with it, so it is raised
  /// again for the first frame of each new phase.
  override func update(_ w: World, _ dt: Float) -> Bool {
    let before = phase
    let done = super.update(w, dt)
    if phase != before { first = true }
    return done
  }

  /// The face-off, with the fight camera. True when it is over.
  @discardableResult
  func faceOffR(_ w: World, _ duration: Float = 0.45) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    if enter() {
      setup(w)
      a.yawTarget = faceAngle(V - A)
      v.yawTarget = faceAngle(A - V)
      rFightShot(w, A, V, shotHeight, shotExtra, plan.mover, plan.victim)
    }
    v.tremble = clamp(pt / duration, 0, 1) * 0.6
    v.idleWeight = 0
    return pt >= duration
  }
  /// Walk (gallop, roll) onto the target square.
  @discardableResult
  func moveInR(_ w: World, _ dt: Float, _ speedScale: Float = 1) -> Bool {
    let a = w.actors[plan.mover]
    if enter() {
      let B = squareCenter(Int(plan.move.to))
      walker = RWalker(plan.mover, B, rModeOf(a), rPaceOf(a, length(flat(B - a.pos))) * speedScale)
      if rIsChariot(a) { walker.run = 0.4 }
    }
    return walker.update(w, dt)
  }
  /// Walk (gallop, roll) to a point.
  @discardableResult
  func goTo(_ w: World, _ dt: Float, _ p: vec3, _ speed: Float, _ run: Float = 0) -> Bool {
    let a = w.actors[plan.mover]
    if enter() {
      walker = RWalker(plan.mover, p, rModeOf(a), speed)
      walker.run = run
    }
    return walker.update(w, dt)
  }
  func finishR(_ w: World) -> Bool {
    let a = w.actors[plan.mover]
    let done = finish(w)
    if done {
      a.pos = squareCenter(Int(plan.move.to))
      a.tilt = quat.identity
      a.resetControls()
    }
    return done
  }
  /// The victim's part while it waits: trembling, on guard.
  func brace(_ w: World, _ amount: Float) {
    let v = w.actors[plan.victim]
    v.tremble = amount
    v.idleWeight = 0
  }
  /// A dying victim falls (or tumbles through the air); the body fades on its own; true once it is down.
  @discardableResult
  func victimDown(_ w: World, _ dt: Float) -> Bool {
    let a = fall.actor < 0 || fall.update(w, dt)
    let b = tumble.actor < 0 || tumble.update(w, dt)
    return a && b
  }
  func victimDying() -> Bool { fall.active() || tumble.active() }
  /// Knocked flying: a body tumbles, a chariot overturns, a siege tower collapses.
  func knockFlying(_ w: World, _ vel: vec3, _ spin: Float) {
    let v = w.actors[plan.victim]
    if rIsHuman(v) {
      tumble.launch(w, v, vel, spin)
    } else {
      // (a chariot overturns away from the blow: its own left is +x)
      let left = vec3(cos(v.yaw), 0, -sin(v.yaw))
      let side: Float = dot(flat(vel), left) >= 0 ? 1 : -1
      fall.start(w, v, side, flat(vel) * 0.35, 0.15)
    }
  }
  /// The victim is struck down: it falls on its back (dir +1) or its face (-1), pushed along 'push'.
  func kill(_ w: World, _ direction: Float, _ push: vec3, _ stagger: Float = 0.35) {
    let v = w.actors[plan.victim]
    var d0 = direction
    if rIsChariot(v) {  // a chariot overturns to the side the blow drives it
      let left = vec3(cos(v.yaw), 0, -sin(v.yaw))
      let d = dot(flat(push), left)
      if abs(d) > 1e-3 {
        d0 = d > 0 ? 1 : -1
      } else {
        d0 = w.fx.rnd() < 0.5 ? -1 : 1
      }
    }
    fall.start(w, v, d0, push, stagger)
  }
  /// A cheer and the victor's gesture: sword (or spear) raised.
  func triumph(_ w: World, _ u: Float) {
    let a = w.actors[plan.mover]
    if rIsHuman(a) {
      let k = min(1, u * 3)
      a.ctl.armR = mix(a.ctl.armR, vec3(2.6, 0.25, 0.3), k)
      a.ctl.itemPitch = mix(a.ctl.itemPitch, 0, k)
      a.ctl.headPitch = -0.2 * k
    } else if rIsChariot(a) {
      a.ctl.armR = vec3(1.8, 0.2, 0.2) * min(1, u * 3)
      a.ctl.rear = 0.5 * sin(clamp(u, 0, 1) * kPI)
    }
  }
}

// ---------------------------------------------------------------------------
// Props: weapons in flight and weapons stuck in bodies
// ---------------------------------------------------------------------------
/// A puppet placed by a world matrix.
func rSetPuppetMatrix(_ p: Puppet, _ M: mat4) {
  let sc = length(vec3(M.columns.0))
  let R = mat3(vec3(M.columns.0) / sc, vec3(M.columns.1) / sc, vec3(M.columns.2) / sc)
  p.pos = vec3(M.columns.3)
  p.yaw = 0
  p.tilt = quat_cast(R).normalized
  p.scale = sc
}
/// A prop's world matrix: its butt (origin) at 'butt', its +y axis along 'along'.
func rPropMatrix(_ butt: vec3, _ along: vec3, _ sc: Float = 1) -> mat4 {
  translate(mat4.identity, butt) * mat4_cast(aimY(along)) * scale(mat4.identity, vec3(sc))
}

/// A weapon stuck in a body: it moves with it as it falls, and fades with it.
struct RStuck {
  var puppet = -1, victim = -1
  var local = mat4.identity
  mutating func attach(_ w: World, _ puppetId: Int, _ v: Actor, _ world: mat4) {
    puppet = puppetId
    victim = v.id
    local = v.model().inverse * world
  }
  /// corpse: the puppet id of the victim's body once it has fallen (or -1).
  func update(_ w: World, _ corpse: Int) {
    guard let p = w.puppet(puppet), victim >= 0 else { return }
    let v = w.actors[victim]
    if v.alive {
      rSetPuppetMatrix(p, v.model() * local)
    } else if let c = w.puppet(corpse) {
      rSetPuppetMatrix(p, c.model() * local)
      p.life = c.life
      p.dissolve = c.dissolve
      p.fadeRate = c.fadeRate
    } else if p.life < 0 {
      p.life = 0  // (the tower it was stuck in fell to pieces)
      p.fadeRate = 3
    }
  }
}

/// A thrown weapon on a ballistic arc, its point first.
struct RMissile {
  var puppet = -1
  var p0 = vec3(0), v0 = vec3(0)
  var T: Float = 0.5, g: Float = 9.8, len: Float = 0.95, t: Float = 0
  /// Adds the puppet (a Roman asset) and starts it from 'from' so that its tip reaches
  /// 'target' after 'time' seconds; 'length' is the distance from its butt to its tip.
  mutating func launch(_ w: World, _ asset: Int, _ color: Int, _ from: vec3, _ target: vec3, _ time: Float,
                       _ length: Float, _ gravity: Float = 9.8) {
    puppet = w.addPuppet(asset, color, 3)
    p0 = from
    T = time
    len = length
    g = gravity
    v0 = (target - from) / T + vec3(0, 0.5 * g * T, 0)
    t = 0
    update(w, 0)
  }
  func tip() -> vec3 {
    let tt = min(t, T)
    return p0 + v0 * tt + vec3(0, -0.5 * g * tt * tt, 0)
  }
  func heading() -> vec3 { normalize(v0 + vec3(0, -g * min(t, T), 0)) }
  func matrix() -> mat4 { rPropMatrix(tip() - heading() * len, heading()) }
  /// True once it has arrived.
  @discardableResult
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    t += dt
    if let p = w.puppet(puppet) { rSetPuppetMatrix(p, matrix()) }
    return t >= T
  }
}

// ---------------------------------------------------------------------------
// Battles off: the victim falls where it stands while the attacker moves in.
// ---------------------------------------------------------------------------
final class RQuickCapture: RBattle {
  override func step(_ w: World, _ dt: Float) -> Bool {
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if enter() { setup(w) }
      v.tremble = 1
      if pt > 0.15 {
        if !rIsTower(v) {
          rBloodSpray(w, rChestOf(v), dir + vec3(0, 0.3, 0), 25, 2.0)
          w.sound(.stab, 0.6, v.pos)
        }
        fall.life = 0.25
        fall.cry = false
        kill(w, 1.0, dir * 0.4, 0.1)
        next()
      }
    case 1:
      let down = victimDown(w, dt)
      let arrived = moveInR(w, dt, 1.4)
      if down && arrived { next() }
    case 2:
      return finishR(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Checkmate: the emperor is struck by unseen daggers (the conspirators' - et tu?),
// staggers, drops to his knees and falls.
// ---------------------------------------------------------------------------
final class RCheckmate: Staged {
  let king: Int, winner: Int
  var fall = RFall()
  var stabs = 0
  init(_ king: Int, _ winner: Int) {
    self.king = king
    self.winner = winner
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let k = w.actors[king]
    switch phase {
    case 0:
      if enter() {
        w.sound(.mate, 1, k.pos)
        w.sound(.crowd, 0.5, k.pos, 0.8)
        if w.cinematic {
          var p = CamPose()
          p.target = k.pos + vec3(0, 0.6, 0) + k.forward() * 0.3
          let player = normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4))
          let fwd: vec3 = k.forward() * 1.0
          let back = normalize(fwd + player * 0.45 + vec3(1e-4))
          p.pos = p.target + back * 3.1 + vec3(0, 1.7, 0)  // high enough to see over his own men
          p.fov = 32
          p.focus = length(p.pos - p.target)
          p.aperture = 7
          w.cam.setShot(p, 0.9)
        }
      }
      k.idleWeight = 0
      // three blows, from behind and from the side
      let at: [Float] = [0.5, 1.0, 1.4]
      if stabs < 3 && pt > at[stabs] {
        let sx: Float = stabs == 1 ? 0.06 : -0.05
        let off = vec3(sx, -0.03 * Float(stabs), -0.09)
        let c = vec3(k.model() * Medieval.boneMatrix(k.pose, RH.chest) * vec4(Roman.chestPoint(k.kind) + off, 1))
        let dx: Float = stabs == 1 ? 0.6 : -0.4
        let d = -k.forward() + vec3(dx, 0.2, 0)
        rWound(w, c, d, 0.9)
        w.sound(.grunt, 0.7, c, 0.75 - 0.05 * Float(stabs))
        k.ctl.lean = 0.2 + 0.1 * Float(stabs)
        k.ctl.twist = stabs == 1 ? -0.3 : 0.25
        stabs += 1
      }
      k.ctl.lean = approach(k.ctl.lean, 0.1, dt * 0.5)
      k.ctl.armL = mix(k.ctl.armL, vec3(1.3, -0.3, 1.4), min(1, dt * 3))  // clutching his chest
      if pt > 1.0 { k.ctl.itemR = 0 }  // the sceptre falls from his hand
      k.ctl.crouch = 0.6 * rSeg(pt, 1.4, 2.0)
      k.tremble = 0.3
      if pt > 2.1 {
        fall.life = 6
        fall.bleed = 1.4
        fall.start(w, k, -1, k.forward() * 0.1, 0.2)
        w.sound(.rumble, 0.6, k.pos, 0.8)
        next()
      }
    case 1:
      if fall.update(w, dt) {
        w.slowmo(0.5, 0.6)
        next()
      }
    case 2:  // the winners celebrate
      for a in w.actors where a.alive && a.color == winner {
        a.selected = pt > (a.phase - floor(a.phase)) * 0.4 && pt < 2.4
      }
      if pt > 0.2 && pt - w.lastDt <= 0.2 { w.sound(.cornu, 0.7, vec3(0)) }
      if pt > 2.6 {
        for a in w.actors { a.selected = false }
        if w.cinematic { w.cam.release(1.2) }
        return true
      }
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------
private let rStyleNames: [[String]] = [
  ["Pilum", "Shield and gladius", "Vault"],
  ["Run down", "Scythed wheels", "Charioteer's leap"],
  ["Duel", "Leaping strike", "Thrown knife"],
  ["Battering ram", "Sally", "Scorpion"],
  ["Hidden dagger", "Poisoned cup", "Whirling spear"],
  ["Thumbs down", "Gladius duel", "The lion"],
]

func romanBattleStyleName(_ kind: Int, _ style: Int) -> String {
  if kind < 0 || kind >= Kind.count || style < 0 || style >= kBattleStyles { return "" }
  return rStyleNames[kind][style]
}

func makeRomanBattle(_ w: World, _ kind: Int, _ style: Int, _ plan: MovePlan) -> Anim {
  var style = style
  // a siege tower cannot drink from a cup
  if kind == Kind.queen && style == 1 && plan.victim >= 0 && w.actors[plan.victim].kind == Kind.rook { style = 0 }
  switch kind * kBattleStyles + style {
  case Kind.pawn * kBattleStyles + 0: return RPawnPilum(plan)
  case Kind.pawn * kBattleStyles + 1: return RPawnShield(plan)
  case Kind.pawn * kBattleStyles + 2: return RPawnVault(plan)
  case Kind.knight * kBattleStyles + 0: return RChariotRunDown(plan)
  case Kind.knight * kBattleStyles + 1: return RChariotScythes(plan)
  case Kind.knight * kBattleStyles + 2: return RChariotLeap(plan)
  case Kind.bishop * kBattleStyles + 0: return RCenturionDuel(plan)
  case Kind.bishop * kBattleStyles + 1: return RCenturionLeap(plan)
  case Kind.bishop * kBattleStyles + 2: return RCenturionKnife(plan)
  case Kind.rook * kBattleStyles + 0: return RTowerRam(plan)
  case Kind.rook * kBattleStyles + 1: return RTowerSally(plan)
  case Kind.rook * kBattleStyles + 2: return RTowerScorpion(plan)
  case Kind.queen * kBattleStyles + 0: return REmpressDagger(plan)
  case Kind.queen * kBattleStyles + 1: return REmpressCup(plan)
  case Kind.queen * kBattleStyles + 2: return REmpressSpear(plan)
  case Kind.king * kBattleStyles + 0: return REmperorThumbsDown(plan)
  case Kind.king * kBattleStyles + 1: return REmperorDuel(plan)
  default: return REmperorLion(plan)
  }
}

func makeRomanQuiet(_ plan: MovePlan) -> Anim { RQuietMove(plan) }
func makeRomanQuickCapture(_ plan: MovePlan) -> Anim { RQuickCapture(plan) }
func makeRomanCheckmate(_ kingId: Int, _ winnerColor: Int) -> Anim { RCheckmate(kingId, winnerColor) }
