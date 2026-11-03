// Choreography for the dinosaurs. Raptors run, pteranodons fly their moves,
// the four-legged plod and the great theropods stride, shaking the board. They
// don't shatter like marble: they fight like animals - claws, horns, beaks,
// jaws and tails - bleed, and fall on their sides, and the dead lie in their
// blood a moment before they fade away. Captures:
//
//   Pawn    killing claw (a leap onto the victim, kicking with the sickle claws),
//           pack attack (two more raptors come running and they take it down
//           together), dodge and bite (it slips the victim's lunge and seizes its throat)
//   Knight  dive (a stoop out of the sky, beak first), wing buffet (it rears and beats
//           its wings in the victim's face, then stabs), talon strike (it swoops in low,
//           feet first, and finishes the victim on the ground)
//   Bishop  horn charge (gored and tossed), horn lock (it takes the victim's bite on its
//           frill, drives it back and gores it), frill bash (a swing of the head knocks the
//           victim down, then it tramples it)
//   Rook    rearing stomp (it rears up and crashes down on the victim), neck swing (the
//           neck swung like a club), tail lash (the tail cracks like a whip)
//   Queen   hooked claws, snatch (seized in the long jaws, shaken and flung), tail swipe
//   King    bone-crusher (seized, lifted, shaken and thrown), the roar (and a bite from
//           above), head ram and stomp
//
// Victims fight back where they can: they threaten, lunge and snap at the attacker.
// Checkmate: a meteor.
//
// This file holds the toolkit (blood, feathers, voices, poses, falling, walking and
// flying, the fight camera, DBattle, the flight and jaws helpers), the quick capture,
// the checkmate and the factories. The battles themselves live in
// BattlesDinoPawnKnightBishop.swift and BattlesDinoRookRoyal.swift (as battle_dino.cpp).
// (approach, bonePoint, puppetPoint and kTau are the shared ones in BattlesMedieval.swift,
// rHalfCycle the Romans': the same as the C++ file's own.)
import Foundation
import simd

let dBloodColor = vec3(0.3, 0.004, 0.003)

/// Progress through [a, b] (0 before, 1 after).
func dSeg(_ t: Float, _ a: Float, _ b: Float) -> Float { clamp((t - a) / (b - a), 0, 1) }
func dBump(_ t: Float, _ a: Float, _ b: Float) -> Float { sin(dSeg(t, a, b) * kPI) }
/// Eases through keys (time, value), held before the first and after the last.
func dKf(_ t: Float, _ keys: [vec2]) -> Float {
  let n = keys.count
  if n == 0 { return 0 }
  if t <= keys[0].x { return keys[0].y }
  for i in 1..<n {
    if t <= keys[i].x {
      let a = keys[i - 1], b = keys[i]
      return mix(a.y, b.y, smoothstep(a.x, b.x, t))
    }
  }
  return keys[n - 1].y
}

func dIsThero(_ a: Actor) -> Bool { Dino.theropod(a.kind) }
func dIsPtero(_ a: Actor) -> Bool { a.kind == Kind.knight }
func dIsQuad(_ a: Actor) -> Bool { Dino.quadruped(a.kind) }
func dIsSmall(_ a: Actor) -> Bool { a.kind == Kind.pawn || a.kind == Kind.knight }  // can be lifted and flung
func dIsHuge(_ a: Actor) -> Bool { a.kind == Kind.rook }

func dHeadBone(_ a: Actor) -> Int { dIsThero(a) ? THB.head : (dIsPtero(a) ? PTB.head : QDB.head) }
func dChestBone(_ a: Actor) -> Int { dIsThero(a) ? THB.chest : (dIsPtero(a) ? PTB.body : QDB.body) }
func dTailBone(_ a: Actor) -> Int { dIsThero(a) ? THB.tail3 : (dIsPtero(a) ? PTB.body : QDB.tail3) }

/// World position of a point in the model's (unskinned) coordinates.
func dModelPoint(_ a: Actor, _ rest: vec3) -> vec3 { vec3(a.model() * vec4(rest, 1)) }

func dHeadOf(_ a: Actor) -> vec3 { bonePoint(a, dHeadBone(a), Dino.headPoint(a.kind)) }
func dMouthOf(_ a: Actor) -> vec3 { bonePoint(a, dHeadBone(a), Dino.mouthPoint(a.kind)) }
func dChestOf(_ a: Actor) -> vec3 { bonePoint(a, dChestBone(a), Dino.chestPoint(a.kind)) }
func dNeckOf(_ a: Actor) -> vec3 {
  let k: Float = dIsHuge(a) ? 0.25 : 0.45
  return mix(dChestOf(a), dHeadOf(a), k)
}
func dTailOf(_ a: Actor) -> vec3 { bonePoint(a, dTailBone(a), Dino.tailTip(a.kind)) }
func dHornsOf(_ a: Actor) -> vec3 { bonePoint(a, QDB.head, Dino.hornPoint(Kind.bishop)) }
/// How far the snout (or the horns) reach in front of the animal's middle.
func dReachOf(_ a: Actor) -> Float { Dino.mouthPoint(a.kind).z * a.scale }
func dHalfWidthOf(_ a: Actor) -> Float { Dino.bodyHalfWidth(a.kind) * a.scale }
/// Centre distance at which the attacker's jaws close on the neck of a victim facing it.
func dBiteDist(_ a: Actor, _ v: Actor) -> Float { dReachOf(a) + dReachOf(v) * 0.4 + dHalfWidthOf(v) * 0.5 }

func dClampBoard(_ p: vec3, _ m: Float = 3.35) -> vec3 {
  var p = p
  p.x = clamp(p.x, -m, m)
  p.z = clamp(p.z, -m, m)
  return p
}

// ---------------------------------------------------------------------------
// Blood, feathers and dust
// ---------------------------------------------------------------------------
func dBloodSpray(_ w: World, _ at: vec3, _ dir: vec3, _ n: Int, _ speed: Float, _ spread: Float = 0.45) {
  for _ in 0..<max(0, n) {
    var p = Particle()
    let d = normalize(dir + w.fx.rndDir() * spread + vec3(0, 0.15, 0))
    p.pos = at + w.fx.rndDir() * 0.012
    p.vel = d * speed * w.fx.rnd(0.35, 1.15)
    p.size = w.fx.rnd(0.006, 0.016)
    p.color = dBloodColor * w.fx.rnd(0.7, 1.3)
    p.colorEnd = dBloodColor * 0.55
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
    p.color = dBloodColor * 0.9
    p.colorEnd = dBloodColor * 0.5
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
var dPoolLift: Float = 0

/// A pool of blood spreading on the board (it fades long after the fight).
func dBloodPool(_ w: World, _ at: vec3, _ size: Float, _ spread: Float = 1, _ life: Float = 7) {
  if !overBoard(at) { return }
  let id = w.addPuppet(DAsset.pool, 0, 4)
  guard let p = w.puppet(id) else { return }
  dPoolLift = dPoolLift > 0.0012 ? 0 : dPoolLift + 0.00015
  p.pos = vec3(at.x, 0.0012 + dPoolLift, at.z)
  p.yaw = w.fx.rnd(0, kTau)
  p.scale = 0.004
  p.growTo = size
  p.growRate = size / max(0.05, spread)
  p.life = life
  p.fadeRate = 0.35
  p.shadow = false
}

/// Drops and splashes thrown along 'dir'.
func dSplatter(_ w: World, _ at: vec3, _ dir: vec3, _ n: Int, _ reach: Float = 0.4) {
  let d = flat(dir)
  for _ in 0..<max(0, n) {
    let along = d * w.fx.rnd(0.05, reach)
    let ox = w.fx.rnd(-1, 1)
    let oz = w.fx.rnd(-1, 1)
    let p = flat(at) + along + vec3(ox, 0, oz) * 0.07
    let size = w.fx.rnd(0.025, 0.06)
    dBloodPool(w, p, size, 0.15, w.fx.rnd(6, 9))
  }
}

/// The kinds of wound (C++ W_BITE, W_CLAW, W_GORE, W_CRUSH).
enum DWound {
  static let bite = 0, claw = 1, gore = 2, crush = 3
}

/// A wound: the spray, splashes on the board and the sounds of it.
func dWound(_ w: World, _ at: vec3, _ dir: vec3, _ power: Float, _ kind: Int) {
  dBloodSpray(w, at, dir, Int(80 * power), 2.2 + 1.6 * power)
  dSplatter(w, at, dir, 4 + Int(4 * power))
  switch kind {
  case DWound.bite:
    w.sound(.snap, 0.7, at, w.fx.rnd(0.9, 1.1))
    w.sound(.crunch, 0.8, at, w.fx.rnd(0.9, 1.1))
  case DWound.claw:
    w.sound(.tear, 0.9, at, w.fx.rnd(0.9, 1.15))
  case DWound.gore:
    w.sound(.gore, 1.0, at, w.fx.rnd(0.9, 1.05))
  default:
    w.sound(.crunch, 1.0, at, w.fx.rnd(0.8, 0.95))
    w.sound(.quake, 0.6, at, 1.2)
  }
  w.sound(.blood, 0.55 * power, at, w.fx.rnd(0.9, 1.15))
}

/// Feathers knocked out of a raptor, the colour of its army.
func dFeathers(_ w: World, _ v: Actor, _ at: vec3, _ dir: vec3, _ n: Int) {
  if v.kind != Kind.pawn { return }
  let col = v.color == 0 ? vec3(0.55, 0.42, 0.27) : vec3(0.05, 0.05, 0.055)
  for _ in 0..<max(0, n) {
    var p = Particle()
    p.pos = at + w.fx.rndDir() * 0.03
    let d = normalize(dir + w.fx.rndDir() * 0.9 + vec3(0, 0.6, 0))
    p.vel = d * w.fx.rnd(0.4, 1.4)
    p.size = w.fx.rnd(0.008, 0.016)
    p.color = col * w.fx.rnd(0.7, 1.3)
    p.colorEnd = p.color
    p.opacity = 0.9
    p.maxLife = w.fx.rnd(1.2, 2.2)
    p.type = .smoke
    p.drag = 3.5
    p.gravity = 0.6
    p.rot = w.fx.rnd(0, 6.28)
    p.rotVel = w.fx.rnd(-6, 6)
    p.seed = w.fx.rnd()
    p.fadeIn = 0
    w.fx.add(p)
  }
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------
/// An animal's call: a threat, a cry of triumph.
func dCallOf(_ w: World, _ a: Actor, _ vol: Float = 1) {
  let at = dHeadOf(a)
  switch a.kind {
  case Kind.pawn: w.sound(.screech, 0.8 * vol, at, w.fx.rnd(0.92, 1.12))
  case Kind.knight: w.sound(.squawk, 0.8 * vol, at, w.fx.rnd(0.95, 1.1))
  case Kind.bishop: w.sound(.bellow, 0.8 * vol, at, w.fx.rnd(1.05, 1.2))
  case Kind.rook: w.sound(.bellow, 1.0 * vol, at, w.fx.rnd(0.55, 0.65))
  case Kind.queen: w.sound(.rexroar, 0.8 * vol, at, w.fx.rnd(1.2, 1.3))
  default:
    w.sound(.rexroar, 1.0 * vol, at, w.fx.rnd(0.95, 1.02))
    w.shake(0.15 * vol)
  }
}
/// A warning: a hiss, a growl, a snort.
func dWarnOf(_ w: World, _ a: Actor, _ vol: Float = 1) {
  let at = dHeadOf(a)
  switch a.kind {
  case Kind.pawn: w.sound(.hiss, 0.7 * vol, at, w.fx.rnd(1.0, 1.2))
  case Kind.knight: w.sound(.squawk, 0.5 * vol, at, w.fx.rnd(1.15, 1.3))
  case Kind.bishop: w.sound(.snort, 0.8 * vol, at, w.fx.rnd(0.9, 1.05))
  case Kind.rook: w.sound(.bellow, 0.6 * vol, at, w.fx.rnd(0.7, 0.8))
  case Kind.queen: w.sound(.growl, 0.8 * vol, at, w.fx.rnd(1.15, 1.3))
  default: w.sound(.growl, 1.0 * vol, at, w.fx.rnd(0.8, 0.9))
  }
}
func dDeathPitch(_ kind: Int) -> Float {
  let P: [Float] = [1.7, 1.9, 0.95, 0.6, 1.05, 0.85]
  return P[max(0, min(5, kind))]
}
/// A footfall: light for the raptors, the board shaking under the giants.
func dFootfall(_ w: World, _ a: Actor, _ vol: Float = 1) {
  let p = flat(a.pos)
  switch a.kind {
  case Kind.pawn: w.sound(.step, 0.3 * vol, p, w.fx.rnd(1.4, 1.65))
  case Kind.knight: w.sound(.step, 0.22 * vol, p, w.fx.rnd(1.7, 1.9))
  case Kind.bishop:
    w.sound(.quake, 0.25 * vol, p, w.fx.rnd(1.3, 1.45))
    w.fx.dust(p + a.forward() * 0.15, 2, 0.15, 0.3)
  case Kind.rook:
    w.sound(.quake, 0.55 * vol, p, w.fx.rnd(0.85, 1.0))
    w.fx.dust(p + a.forward() * 0.12, 4, 0.2, 0.45)
    w.shake(0.05 * vol)
  case Kind.queen: w.sound(.quake, 0.3 * vol, p, w.fx.rnd(1.15, 1.3))
  default:
    w.sound(.quake, 0.45 * vol, p, w.fx.rnd(0.95, 1.1))
    w.fx.dust(p, 2, 0.15, 0.3)
    w.shake(0.035 * vol)
  }
}

// ---------------------------------------------------------------------------
// Fighting poses, as functions of their progress u (0..1)
// ---------------------------------------------------------------------------
/// A threat display: jaws open, head low, tail up; wings half-spread; horns lowered.
func dThreat(_ a: Actor, _ u: Float) {
  if dIsThero(a) {
    a.ctl.jaw = 0.55 * u
    a.ctl.neck = -0.15 * u
    a.ctl.headPitch = 0.12 * u
    a.ctl.crouch = 0.22 * u
    a.ctl.lean = 0.1 * u
    a.ctl.itemPitch = 0.25 * u
    let arm = vec3(0.35, 0.2, 0.15) * u
    a.ctl.armL = arm
    a.ctl.armR = arm
  } else if dIsPtero(a) {
    a.ctl.wings = 0.55 * u
    a.ctl.jaw = 0.6 * u
    a.ctl.neck = 0.25 * u
    a.ctl.lean = -0.1 * u
  } else if a.kind == Kind.bishop {
    a.ctl.headPitch = 0.32 * u
    a.ctl.neck = -0.2 * u
    a.ctl.crouch = 0.12 * u
  } else {
    a.ctl.neck = 0.25 * u
    a.ctl.headPitch = -0.15 * u
    a.ctl.jaw = 0.3 * u
  }
}
/// A lunge with the jaws (or beak): drawn back (to 0.35), the snap (0.5), pulled back.
func dLunge(_ a: Actor, _ u: Float, _ depth: Float = 1) {
  a.ctl.neck = dKf(u, [vec2(0.0, -0.1), vec2(0.35, 0.3), vec2(0.52, -1.0 * depth), vec2(1.0, -0.1)])
  a.ctl.jaw = dKf(u, [vec2(0.0, 0.4), vec2(0.35, 0.95), vec2(0.48, 0.95), vec2(0.56, 0.05), vec2(1.0, 0.3)])
  a.ctl.lean = dKf(u, [vec2(0.0, 0.08), vec2(0.35, -0.08), vec2(0.52, 0.25 * depth), vec2(1.0, 0.08)])
  a.ctl.headPitch = dKf(u, [vec2(0.0, 0.1), vec2(0.35, -0.15), vec2(0.52, 0.2), vec2(1.0, 0.1)])
  if dIsPtero(a) { a.ctl.wings = 0.55 }
}
// The victim's part: a threat, and its counter-attack (whatever it is).
func dVictimGuard(_ v: Actor, _ u: Float) { dThreat(v, u) }
func dVictimStrike(_ v: Actor, _ u: Float) {
  if v.kind == Kind.bishop {  // a hook of the horns
    v.ctl.headPitch = dKf(u, [vec2(0.0, 0.32), vec2(0.35, 0.5), vec2(0.55, -0.35), vec2(1.0, 0.32)])
    v.ctl.headYaw = dKf(u, [vec2(0.0, 0.0), vec2(0.35, -0.3), vec2(0.55, 0.35), vec2(1.0, 0.0)])
  } else if v.kind == Kind.rook {  // a swing of the neck
    v.ctl.headYaw = dKf(u, [vec2(0.0, 0.0), vec2(0.4, 0.5), vec2(0.6, -0.6), vec2(1.0, 0.0)])
    v.ctl.neck = dKf(u, [vec2(0.0, 0.25), vec2(0.4, 0.4), vec2(0.6, -0.6), vec2(1.0, 0.25)])
  } else {
    dLunge(v, u, 0.9)
  }
}

// ---------------------------------------------------------------------------
// Falling: a dinosaur struck down staggers, its legs give way and it falls on its
// side; a body knocked flying tumbles and crashes down. The body lies in its blood
// a moment, then fades away (a puppet: the piece is gone).
// ---------------------------------------------------------------------------
/// Replaces the actor by a puppet in the same pose (it fades 'life' seconds from
/// now); the actor is gone. Returns the puppet's id.
@discardableResult
func dMakeCorpse(_ w: World, _ v: Actor, _ life: Float) -> Int {
  let id = w.addPuppet(v.kind, v.color, 4)
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

struct DFall {
  var actor = -1, corpse = -1
  var t: Float = 0, side: Float = 1  // +1 onto its left flank, -1 its right
  var stagger: Float = 0.4           // seconds of staggering first
  var push = vec3(0)                 // knocked back along this (world, per second)
  var landed = false, done = false, cry = true
  var life: Float = 1.8              // seconds the body lies there before it fades
  var bleed: Float = 1               // the size of the pool it leaves
  var settle: Float = 0
  func active() -> Bool { actor >= 0 && !done }

  mutating func start(_ w: World, _ v: Actor, _ sideIn: Float, _ pushVel: vec3, _ staggerTime: Float = 0.4) {
    actor = v.id
    side = sideIn >= 0 ? 1 : -1
    push = flat(pushVel)
    stagger = staggerTime
    t = 0
    landed = false
    done = false
    v.idleWeight = 0
    v.tremble = 0
    v.yawRate = 2
    v.ctl.wings = 0
    v.ctl.hover = 0
    if cry {
      let vol: Float = dIsHuge(v) ? 1.0 : 0.8
      w.sound(.dinodeath, vol, dHeadOf(v), dDeathPitch(v.kind) * w.fx.rnd(0.94, 1.06))
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
    if t < stagger {  // staggering, the head swinging, the legs buckling
      let u = t / max(stagger, 1e-3)
      v.pos += push * dt * (1 - 0.5 * u)
      v.ctl.crouch = approach(v.ctl.crouch, 0.35, dt * 1.5)
      v.ctl.neck = mix(v.ctl.neck, 0.25 * sin(t * 9), min(1, dt * 6))
      v.ctl.headYaw = mix(v.ctl.headYaw, 0.35 * side * sin(t * 5), min(1, dt * 5))
      v.ctl.jaw = approach(v.ctl.jaw, 0.45, dt * 2)
      v.ctl.stride = 0.3 * (1 - u)
      v.ctl.gait += dt * 5
      return false
    }
    let h = v.height() / max(v.scale, 1e-3)
    let dur = 0.45 + 0.18 * h
    let u = clamp((t - stagger) / dur, 0, 1)
    var ang = 1.42 * u * u  // falling faster and faster
    if !landed && u >= 1 {
      landed = true
      settle = 0
      let p = flat(v.pos)
      let big = min(1, h / 1.0)
      w.sound(.bodyfall, 0.6 + 0.4 * big, p, 1.35 - 0.5 * big)
      let left = vec3(cos(v.yaw), 0, -sin(v.yaw))
      let dustAt = p + left * side * h * 0.3
      w.fx.dust(dustAt, 10 + Int(20 * big), 0.25 + 0.4 * big, 0.6 + 0.8 * big)
      w.shake(0.08 + 0.3 * big)
      // the blood pools under the body
      let body = flat(dChestOf(v)) * 0.6 + p * 0.4
      dBloodPool(w, body, (0.17 + 0.2 * big) * bleed, 1.5, 9)
    }
    if landed {  // a little bounce, then still
      settle += dt
      ang = 1.42 - 0.08 * exp(-settle * 9) * sin(settle * 24)
    } else {
      v.pos += push * dt * (1 - u) * 0.6
    }
    v.tiltPivot = vec3(side * Dino.bodyHalfWidth(v.kind), 0, 0)
    v.tilt = angleAxis(-side * ang, vec3(0, 0, 1))
    v.ctl.dead = u
    v.ctl.crouch = 0.35 * (1 - u)
    v.ctl.stride = 0
    if landed && settle > 0.45 {
      corpse = dMakeCorpse(w, v, life)
      done = true
    }
    return done
  }
}

/// A body knocked flying: it tumbles through the air, crashes down and comes to rest on
/// its side (then fades).
struct DTumble {
  var actor = -1, corpse = -1
  var vel = vec3(0)
  var spin: Float = 0, angle: Float = 0, target: Float = 0, t: Float = 0, settle: Float = 0, hmid: Float = 0.25
  var landed = false, done = false
  var life: Float = 1.8, bleed: Float = 1
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
    v.ctl.wings = 0
    v.ctl.hover = 0
    hmid = v.height() / max(v.scale, 1e-3) * 0.4
    v.tiltPivot = vec3(0, hmid, 0)
    w.sound(.dinodeath, 0.8, dHeadOf(v), dDeathPitch(v.kind) * w.fx.rnd(1.0, 1.12))
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
    let hw = Dino.bodyHalfWidth(v.kind)
    if !landed {
      vel.y -= 14 * dt
      v.pos += vel * dt
      angle += spin * dt
      v.tilt = angleAxis(-angle, vec3(0, 0, 1)) * angleAxis(0.15 * sin(t * 7), vec3(1, 0, 0))
      v.ctl.dead = min(1, 0.5 + t)
      let h = hmid * v.scale
      let reach = abs(cos(angle)) * h + abs(sin(angle)) * hw * v.scale
      let low = v.pos.y + h - reach
      if overBoard(v.pos) && low <= 0 && vel.y < 0 && t > 0.15 {
        landed = true
        let k = ((angle - kPI * 0.5) / kPI).rounded()  // whichever side is nearer
        target = kPI * 0.5 + k * kPI
        let p = flat(v.pos)
        let big = min(1, v.height())
        w.sound(.bodyfall, 0.7 + 0.3 * big, p, 1.3 - 0.4 * big)
        w.fx.dust(p, 14 + Int(16 * big), 0.3 + 0.3 * big, 1.0 + 0.6 * big)
        w.shake(0.12 + 0.25 * big)
        dBloodPool(w, p, (0.17 + 0.15 * big) * bleed, 1.4, 9)
        dSplatter(w, p, vel, 3, 0.35)
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
    let restY = (hw - hmid) * v.scale
    v.pos.y += (restY - v.pos.y) * (1 - exp(-dt * 18))
    v.tilt = angleAxis(-angle, vec3(0, 0, 1))
    v.ctl.dead = 1
    if settle > 0.45 {
      corpse = dMakeCorpse(w, v, life)
      done = true
    }
    return done
  }
}

// ---------------------------------------------------------------------------
// Walking (and running), and the pteranodon's flight
// ---------------------------------------------------------------------------
struct DWalker {
  var actor = -1
  var from = vec3(0), to = vec3(0)
  var speed: Float = 0.8, accel: Float = 1.6, cycle: Float = 0.3, run: Float = 0
  var s: Float = 0, v: Float = 0, len: Float = 0, turnWait: Float = 0
  var lastStep = 0
  var begun = false, face = true, soundOn = true

  init() {}
  init(_ a: Int, _ dest: vec3, _ spd: Float) {
    actor = a
    to = flat(dest)
    speed = spd
  }

  mutating func begin(_ w: World) {
    let a = w.actors[actor]
    from = flat(a.pos)
    len = length(to - from)
    cycle = Dino.strideCycle(a.kind) * a.scale
    let k: Float = a.kind == Kind.rook ? 1.4 : 2.4
    accel = speed * k
    if face && len > 0.05 {
      a.yawTarget = faceAngle(to - from)
      let slow: Float = a.kind == Kind.rook ? 0.45 : (a.kind == Kind.pawn ? 0.2 : 0.3)
      let most: Float = a.kind == Kind.rook ? 1.0 : 0.6
      turnWait = min(most, abs(wrapAngle(a.yawTarget - a.yaw)) * slow)
    }
    lastStep = rHalfCycle(a.ctl.gait)
    begun = true
  }

  /// True once it has arrived and come to rest.
  @discardableResult
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if !begun { begin(w) }
    let a = w.actors[actor]
    if turnWait > 0 {  // shuffling round on the spot
      turnWait -= dt
      a.ctl.gait += dt * 4
      a.ctl.stride = approach(a.ctl.stride, 0.25, dt * 2)
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
    let fast = clamp((v - 0.9) / 1.0, 0, 1)
    let pace = 1 - 0.35 * fast
    a.ctl.gait += v * dt / cycle * kTau * pace
    a.ctl.stride = clamp(v / max(0.1, min(speed, 0.9) * 0.6), 0, 1)
    a.ctl.run = approach(a.ctl.run, max(run, fast), dt * 3)
    let step = rHalfCycle(a.ctl.gait)
    if step != lastStep {
      lastStep = step
      if soundOn {
        dFootfall(w, a, 0.8 + 0.4 * fast)
        if fast > 0.5 { w.fx.dust(flat(a.pos), 1, 0.08, 0.3) }
      }
    }
    if s >= len - 1e-4 {
      a.ctl.stride = approach(a.ctl.stride, 0, dt * 4)
      a.ctl.run = approach(a.ctl.run, 0, dt * 3)
      return a.ctl.stride <= 0.001
    }
    return false
  }
}

func dPaceOf(_ a: Actor, _ dist: Float) -> Float {
  switch a.kind {
  case Kind.pawn: return max(1.4, dist / 1.3)
  case Kind.knight: return 0.6
  case Kind.rook: return max(0.55, dist / 3.0)
  case Kind.king: return max(0.8, dist / 2.3)
  default: return max(0.85, dist / 2.1)
  }
}

/// The pteranodon's flight: it crouches, springs up with a beat of its wings, flies
/// there along an arc and comes down, back-beating its wings, legs forward, and folds them.
struct DSoar {
  var actor = -1
  var from = vec3(0), to = vec3(0)
  var height: Float = 1, dur: Float = 1, t: Float = 0, rise: Float = 0.35, land: Float = 0.4
  var lastBeat = 0
  var begun = false, touched = false, soundOn = true

  init() {}
  init(_ a: Int, _ dest: vec3, _ h: Float = -1) {
    actor = a
    to = flat(dest)
    height = h
  }

  mutating func begin(_ w: World) {
    let a = w.actors[actor]
    from = flat(a.pos)
    let d = length(to - from)
    if height < 0 { height = 0.45 + 0.22 * d }
    dur = 0.7 + 0.3 * d
    if d > 0.05 { a.yawTarget = faceAngle(to - from) }
    a.yawRate = 7
    lastBeat = DSoar.beatOf(a.ctl.flap)
    begun = true
    if soundOn {
      w.sound(.flap, 0.6, a.pos, w.fx.rnd(0.95, 1.1))
      if w.fx.rnd() < 0.5 { w.sound(.squawk, 0.3, a.pos, w.fx.rnd(1.0, 1.2)) }
    }
  }

  // seconds left before it is down
  @discardableResult
  mutating func update(_ w: World, _ dt: Float) -> Bool {
    if !begun { begin(w) }
    let a = w.actors[actor]
    t += dt
    if t < rise {  // crouch, wings open, and spring
      let u = t / rise
      a.ctl.crouch = 0.4 * dBump(u, 0, 1)
      a.ctl.wings = approach(a.ctl.wings, 1, dt * 5)
      a.ctl.flap += dt * 10
      a.ctl.hover = 0
      a.pos = from
      a.pos.y = 0
    } else if t < rise + dur {
      let u = (t - rise) / dur
      var p = mix(from, to, easeInOut(u) * 0.8 + u * 0.2)
      p.y = 4 * u * (1 - u) * height + 0.05 * sin(u * kPI)
      a.pos = p
      a.ctl.crouch = 0
      a.ctl.wings = 1
      let hoverTo: Float = u < 0.82 ? 1 : 0
      a.ctl.hover = approach(a.ctl.hover, hoverTo, dt * 4)
      let beatRate: Float = u < 0.3 || u > 0.75 ? 15 : 8
      a.ctl.flap += dt * beatRate
      a.ctl.tuck = dSeg(u, 0.72, 1)
      a.ctl.lean = -0.35 * dSeg(u, 0.7, 1)
    } else {
      let u = min(1, (t - rise - dur) / land)
      a.pos = to
      a.pos.y = 0
      if !touched {
        touched = true
        if soundOn {
          w.sound(.step, 0.5, to, 1.5)
          w.sound(.flap, 0.4, to, 1.15)
        }
        w.fx.dust(to, 6, 0.3, 0.6)
      }
      a.ctl.hover = 0
      a.ctl.tuck = 1 - u
      a.ctl.lean = -0.35 * (1 - u)
      a.ctl.wings = 1 - DSoar.smoothstepf(u)
      a.ctl.flap += dt * 6 * (1 - u)
      a.ctl.crouch = 0.3 * dBump(u, 0, 0.7)
      if u >= 1 {
        a.ctl.tuck = 0
        a.ctl.lean = 0
        a.ctl.wings = 0
        a.ctl.crouch = 0
        a.yawRate = 9
        return true
      }
    }
    let beat = DSoar.beatOf(a.ctl.flap)
    if beat != lastBeat {
      lastBeat = beat
      if soundOn && t < rise + dur { w.sound(.flap, 0.25, a.pos, w.fx.rnd(1.0, 1.2)) }
    }
    return false
  }
  static func smoothstepf(_ u: Float) -> Float { u * u * (3 - 2 * u) }
  /// Which wing beat we are in: (int)floor(flap / tau).
  static func beatOf(_ flap: Float) -> Int {
    let x = floor(flap / kTau)
    return x.isFinite ? Int(x) : 0
  }
}

// ---------------------------------------------------------------------------
// Quiet moves (and castling)
// ---------------------------------------------------------------------------
final class DQuietMove: Staged {
  let plan: MovePlan
  var walker = DWalker(), rookWalker = DWalker()
  var soar = DSoar()
  var A = vec3(0), B = vec3(0)
  var arcSound = false
  var lastStep = 0
  init(_ p: MovePlan) { plan = p }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let a = w.actors[plan.mover]
    switch phase {
    case 0:
      if enter() {
        A = flat(a.pos)
        B = squareCenter(Int(plan.move.to))
        let dist = length(B - A)
        if a.kind == Kind.knight {
          soar = DSoar(plan.mover, B)
        } else {
          walker = DWalker(plan.mover, B, dPaceOf(a, dist))
        }
        if plan.rook >= 0 { rookWalker = DWalker(plan.rook, squareCenter(plan.rookTo), 0.75) }
        lastStep = rHalfCycle(a.ctl.gait)
      }
      let d1: Bool
      if a.kind == Kind.knight {
        d1 = soar.update(w, dt)
      } else if plan.rook >= 0 {
        // castling: the tyrant king strides round in an arc behind the brachiosaurus (on the
        // board's edge side of his rank, clear of it and of his own army)
        let u = clamp(pt / 2.1, 0, 1), e = easeInOut(u)
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
        let cyc = Dino.strideCycle(a.kind) * a.scale
        a.ctl.gait += length(d) / cyc * kTau
        a.ctl.stride = u < 1 ? 0.8 : approach(a.ctl.stride, 0, dt * 4)
        let stp = rHalfCycle(a.ctl.gait)
        if stp != lastStep {
          lastStep = stp
          dFootfall(w, a, 0.8)
        }
        if !arcSound {
          arcSound = true
          dWarnOf(w, a, 0.6)
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
        if plan.rook >= 0 { w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw }
      }
      if pt > 0.3 { return true }
    default:
      break
    }
    return false
  }
}

/// The cinematic camera for a fight: it looks at the two animals from the side and
/// comes a little closer.
func dFightShot(_ w: World, _ A: vec3, _ V: vec3, _ heightBias: Float, _ extra: Float, _ attacker: Int,
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
  let look = [A + vec3(0, 0.5, 0), V + vec3(0, 0.4, 0), target]
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
// Shared prelude and epilogue for the dinosaurs' battles
// ---------------------------------------------------------------------------
class DBattle: BattleBase {
  var walker = DWalker()
  var soar = DSoar()
  var fall = DFall()
  var tumble = DTumble()

  override init(_ p: MovePlan) { super.init(p) }

  /// Staged clears 'first' after every step; the phases set themselves up with it, so it
  /// is raised again for the first frame of each new phase (see the Romans).
  override func update(_ w: World, _ dt: Float) -> Bool {
    let before = phase
    let done = super.update(w, dt)
    if phase != before { first = true }
    return done
  }

  /// The face-off, with the fight camera; the victim turns to face its attacker.
  @discardableResult
  func faceOffD(_ w: World, _ duration: Float = 0.5) -> Bool {
    let a = w.actors[plan.mover]
    let v = w.actors[plan.victim]
    if enter() {
      setup(w)
      a.yawTarget = faceAngle(V - A)
      v.yawTarget = faceAngle(A - V)
      let big = max(a.height(), v.height())
      let bias = shotHeight + 0.25 * max(0, big - 0.6)
      let extra = shotExtra + 0.6 * max(0, big - 0.7)
      dFightShot(w, A, V, bias, extra, plan.mover, plan.victim)
    }
    v.tremble = clamp(pt / duration, 0, 1) * 0.5
    v.idleWeight = 0
    return pt >= duration
  }
  /// Walk (run, fly) onto the target square.
  @discardableResult
  func moveInD(_ w: World, _ dt: Float, _ speedScale: Float = 1) -> Bool {
    let a = w.actors[plan.mover]
    let B = squareCenter(Int(plan.move.to))
    if enter() {
      let d = length(flat(B - a.pos))
      if dIsPtero(a) && d > 0.45 {
        soar = DSoar(plan.mover, B, 0.35 + 0.15 * d)
        soar.soundOn = false
      } else {
        let k: Float = dIsPtero(a) ? 1.0 : 0.8
        walker = DWalker(plan.mover, B, dPaceOf(a, d) * speedScale * k)
        soar.actor = -1
      }
    }
    return soar.actor >= 0 ? soar.update(w, dt) : walker.update(w, dt)
  }
  /// Walk (or run) to a point.
  @discardableResult
  func goTo(_ w: World, _ dt: Float, _ p: vec3, _ speed: Float, _ run: Float = 0) -> Bool {
    if enter() {
      walker = DWalker(plan.mover, p, speed)
      walker.run = run
    }
    return walker.update(w, dt)
  }
  func finishD(_ w: World) -> Bool {
    let a = w.actors[plan.mover]
    let done = finish(w)
    if done {
      a.pos = squareCenter(Int(plan.move.to))
      a.tilt = quat.identity
      a.yawRate = 9
      a.resetControls()
    }
    return done
  }
  func brace(_ w: World, _ amount: Float) {
    let v = w.actors[plan.victim]
    v.tremble = amount
    v.idleWeight = 0
  }
  /// A dying victim falls (or tumbles through the air); true once it is down.
  @discardableResult
  func victimDown(_ w: World, _ dt: Float) -> Bool {
    let a = fall.actor < 0 || fall.update(w, dt)
    let b = tumble.actor < 0 || tumble.update(w, dt)
    return a && b
  }
  func victimDying() -> Bool { fall.active() || tumble.active() }
  /// Knocked flying (the small ones), or bowled over (the heavy).
  func knockFlying(_ w: World, _ vel: vec3, _ spin: Float) {
    let v = w.actors[plan.victim]
    if dIsSmall(v) || v.kind == Kind.bishop {
      let k: Float = v.kind == Kind.bishop ? 0.6 : 1.0
      tumble.launch(w, v, vec3(vel.x * k, vel.y * k, vel.z * k), spin * k)
    } else {
      let left = vec3(cos(v.yaw), 0, -sin(v.yaw))
      let side: Float = dot(flat(vel), left) >= 0 ? 1 : -1
      kill(w, side, flat(vel) * 0.3, 0.12)
    }
  }
  /// Struck down: it falls on the flank the blow drives it to (or a random one).
  func kill(_ w: World, _ side: Float, _ push: vec3, _ stagger: Float = 0.4) {
    let v = w.actors[plan.victim]
    var side = side
    if side == 0 {
      let left = vec3(cos(v.yaw), 0, -sin(v.yaw))
      let d = dot(flat(push), left)
      if abs(d) > 1e-3 {
        side = d > 0 ? 1 : -1
      } else {
        side = w.fx.rnd() < 0.5 ? -1 : 1
      }
    }
    fall.start(w, v, side, push, stagger)
  }
  /// The victor's call: head up, jaws wide (wings spread, a rear).
  func triumph(_ w: World, _ u: Float) {
    let a = w.actors[plan.mover]
    let e = min(1, u * 3) * (1 - dSeg(u, 0.75, 1))
    a.ctl.jaw = 0.95 * e
    a.ctl.neck = 0.45 * e
    a.ctl.headPitch = -0.2 * e
    a.ctl.crouch = approach(a.ctl.crouch, 0, 0.05)
    if dIsThero(a) {
      a.ctl.lean = -0.15 * e
      a.ctl.itemPitch = 0.2 * e
      let arm = vec3(0.4, 0.25, 0.0) * e
      a.ctl.armL = arm
      a.ctl.armR = arm
    }
    if dIsPtero(a) {
      a.ctl.wings = e
      a.ctl.flap += 0.08
    }
    if a.kind == Kind.bishop { a.ctl.rear = 0.35 * e }
    if a.kind == Kind.rook { a.ctl.rear = 0.25 * e }
  }
  /// Back to rest after the fight, eased (the controls a battle may have left).
  func relax(_ a: Actor, _ dt: Float, _ rate: Float = 3) {
    let k = dt * rate
    a.ctl.jaw = approach(a.ctl.jaw, 0, k)
    a.ctl.neck = approach(a.ctl.neck, 0, k)
    a.ctl.headPitch = approach(a.ctl.headPitch, 0, k)
    a.ctl.headYaw = approach(a.ctl.headYaw, 0, k)
    a.ctl.lean = approach(a.ctl.lean, 0, k)
    a.ctl.crouch = approach(a.ctl.crouch, 0, k)
    a.ctl.twist = approach(a.ctl.twist, 0, k)
    a.ctl.itemYaw = approach(a.ctl.itemYaw, 0, k)
    a.ctl.itemPitch = approach(a.ctl.itemPitch, 0, k)
    a.ctl.kick = approach(a.ctl.kick, 0, k)
    a.ctl.tuck = approach(a.ctl.tuck, 0, k)
    a.ctl.rear = approach(a.ctl.rear, 0, k)
    a.ctl.wings = approach(a.ctl.wings, 0, k)
    a.ctl.armL = mix(a.ctl.armL, vec3(0), min(1, k))
    a.ctl.armR = mix(a.ctl.armR, vec3(0), min(1, k))
  }
}

/// A raptor of the pack (a puppet) running somewhere: true once it is there.
@discardableResult
func dRunPuppet(_ w: World, _ p: Puppet, _ target: vec3, _ speed: Float, _ dt: Float) -> Bool {
  let d = flat(target - p.pos)
  let l = length(d)
  let stepLen = min(l, speed * dt)
  if l > 1e-4 {
    p.pos += d / l * stepLen
    p.yaw = wrapAngle(p.yaw + clamp(wrapAngle(faceAngle(d) - p.yaw), -dt * 8, dt * 8))
  }
  let before = floor(p.ctl.gait / kPI)
  let cyc = Dino.strideCycle(Kind.pawn) * p.scale
  p.ctl.gait += stepLen / cyc * kTau * 0.7
  if floor(p.ctl.gait / kPI) != before { w.sound(.step, 0.2, p.pos, w.fx.rnd(1.45, 1.7)) }
  let strideTo: Float = stepLen > 1e-4 ? 1 : 0
  p.ctl.stride = approach(p.ctl.stride, strideTo, dt * 5)
  let runTo: Float = speed > 1.4 ? 1 : 0
  p.ctl.run = approach(p.ctl.run, runTo, dt * 3)
  if speed > 1.4 && w.fx.rnd() < dt * 8 { w.fx.dust(p.pos, 1, 0.08, 0.3) }
  return l < 0.02
}

// ---------------------------------------------------------------------------
// The flight and jaws helpers of the knight's and the queen's battles (in the C++
// they sit next to the battles that use them)
// ---------------------------------------------------------------------------
/// Flying, controls eased towards a pose in the air (the wings beating).
func dAirborne(_ a: Actor, _ dt: Float, _ beat: Float, _ lean: Float = 0) {
  a.ctl.wings = approach(a.ctl.wings, 1, dt * 5)
  a.ctl.hover = approach(a.ctl.hover, 1, dt * 4)
  a.ctl.flap += dt * beat
  a.ctl.lean = approach(a.ctl.lean, lean, dt * 3)
  a.ctl.crouch = 0
}

/// A flight through the air from one point to another (heights included).
struct DGlide {
  var p0 = vec3(0), p1 = vec3(0)
  var dur: Float = 1, arc: Float = 0, t: Float = 0
  mutating func set(_ a: vec3, _ b: vec3, _ d: Float, _ h: Float = 0) {
    p0 = a
    p1 = b
    dur = d
    arc = h
    t = 0
  }
  @discardableResult
  mutating func update(_ a: Actor, _ dt: Float, _ face: Bool = true) -> Bool {
    t += dt
    let u = clamp(t / dur, 0, 1), e = easeInOut(u)
    var p = mix(p0, p1, e)
    p.y += 4 * e * (1 - e) * arc
    if face && length(flat(p - a.pos)) > 1e-4 { a.yawTarget = faceAngle(p - a.pos) }
    a.pos = p
    return u >= 1
  }
}

/// Holding a victim in the jaws: a small one hangs from them, kicking; a big one is held
/// by the neck and wrenched about.
struct DHeld {
  var grab = vec3(0), base = vec3(0)
  var baseYaw: Float = 0
  var lifted = false
  mutating func take(_ a: Actor, _ v: Actor) {
    grab = v.pos - dMouthOf(a)
    base = v.pos
    baseYaw = v.yaw
    lifted = dIsSmall(v)
  }
  func update(_ a: Actor, _ v: Actor, _ t: Float, _ dt: Float, _ lift: Float) {
    if lifted {
      var p = dMouthOf(a) + grab
      p.y = max(0, p.y + lift)
      v.pos = p
      v.yaw = baseYaw + 0.9 * a.ctl.headYaw
      v.yawTarget = v.yaw
      v.tiltPivot = vec3(0, v.height() / max(v.scale, 1e-3) * 0.6, 0)
      v.tilt = angleAxis(0.35 * sin(t * 9), vec3(0, 0, 1)) * angleAxis(0.3, vec3(1, 0, 0))
      v.ctl.gait += dt * 14
      v.ctl.stride = 1
      v.ctl.dead = 0.25
    } else {
      let side = vec3(cos(v.yaw), 0, -sin(v.yaw))
      v.pos = base + side * 0.05 * sin(t * 12)
      v.yaw = baseYaw + 0.12 * sin(t * 10)
      v.yawTarget = v.yaw
      v.ctl.headYaw = 0.5 * sin(t * 8)
      v.ctl.crouch = 0.25
    }
    v.ctl.jaw = 0.7
    v.tremble = 0.5
    v.idleWeight = 0
  }
}

// ---------------------------------------------------------------------------
// Battles off: the victim falls where it stands while the attacker moves in.
// ---------------------------------------------------------------------------
final class DQuickCapture: DBattle {
  override func step(_ w: World, _ dt: Float) -> Bool {
    let v = w.actors[plan.victim]
    switch phase {
    case 0:
      if enter() { setup(w) }
      v.tremble = 1
      if pt > 0.15 {
        dBloodSpray(w, dChestOf(v), dir + vec3(0, 0.3, 0), 25, 2.0)
        w.sound(.tear, 0.6, v.pos)
        fall.life = 0.25
        fall.cry = false
        kill(w, 0, dir * 0.4, 0.1)
        next()
      }
    case 1:
      let down = victimDown(w, dt)
      let arrived = moveInD(w, dt, 1.4)
      if down && arrived { next() }
    case 2:
      return finishD(w)
    default:
      break
    }
    return false
  }
}

// ---------------------------------------------------------------------------
// Checkmate: a meteor. The tyrant king looks up at a light growing in the sky and
// roars at it; it strikes the board beside him in a blinding blast that throws him
// down - the end of the dinosaurs.
// ---------------------------------------------------------------------------
final class DCheckmate: Staged {
  let king: Int, winner: Int
  var fall = DFall()
  var meteor = -1
  var from = vec3(0), impact = vec3(0)
  var roared = false
  init(_ king: Int, _ winner: Int) {
    self.king = king
    self.winner = winner
  }

  override func step(_ w: World, _ dt: Float) -> Bool {
    let k = w.actors[king]
    let hit: Float = 2.2
    switch phase {
    case 0:
      if enter() {
        let side = vec3(cos(k.yaw), 0, -sin(k.yaw))
        let spot = flat(k.pos) + side * 1.1 + k.forward() * 0.5
        impact = dClampBoard(spot, 3.9)
        // the camera behind the king, from the player's side; the meteor comes in from in
        // front of it, a little to one side, low over the horizon
        var p = CamPose()
        p.target = mix(k.pos, impact, 0.4) + vec3(0, 0.9, 0)
        let player = normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4))
        let toImpact: vec3 = flatDir(k.pos, impact) * 0.35
        let camBack = normalize(player * 1.0 - toImpact + vec3(1e-4))
        p.pos = p.target + camBack * 4.6 + vec3(0, 1.0, 0)
        p.fov = 40
        p.focus = length(p.pos - p.target)
        p.aperture = 7
        let eye = w.cinematic ? p.pos : w.cam.orbitPose().pos
        let ahead = flatDir(eye, impact)
        let aside = vec3(ahead.z, 0, -ahead.x) * 0.45
        let away = normalize(ahead + aside)
        from = impact + away * 6.0 + vec3(0, 2.8, 0)
        meteor = w.addPuppet(DAsset.meteor, 0, 4)
        if let m = w.puppet(meteor) {
          m.pos = from
          m.scale = 0.16
          m.shadow = false
        }
        w.sound(.meteor, 1.0, impact)
        w.sound(.mate, 0.6, k.pos)
        if w.cinematic { w.cam.setShot(p, 0.9) }
      }
      k.idleWeight = 0
      let u = min(1, pt / hit)
      let pos = mix(from, impact, u * u * (0.6 + 0.4 * u))
      if let p = w.puppet(meteor) {
        p.pos = pos
        p.glow = vec4(1.0, 0.45, 0.1, 0.6 + 1.4 * u)
        p.yaw += dt * 3
      }
      // the fiery trail
      let back = normalize(from - impact)
      for _ in 0..<3 {
        var q = Particle()
        q.pos = pos + back * w.fx.rnd(0, 0.25) + w.fx.rndDir() * 0.06
        q.vel = back * w.fx.rnd(0.5, 1.5) + w.fx.rndDir() * 0.3
        q.size = w.fx.rnd(0.1, 0.26)
        q.grow = 0.4
        q.color = vec3(4.0, 1.6, 0.4)
        q.colorEnd = vec3(0.6, 0.15, 0.05)
        q.opacity = 0
        q.maxLife = w.fx.rnd(0.3, 0.6)
        q.type = .glow
        q.drag = 1
        w.fx.add(q)
      }
      if w.fx.rnd() < dt * 20 { w.fx.smoke(pos + back * 0.2, 1, 0.1, 0.2, 0.6) }
      w.light(pos, vec3(6.0, 3.0, 1.2) * (0.4 + u), 3.0 + 3.0 * u, 0.05)
      // the king looks up at it, and roars
      let toM = pos - dHeadOf(k)
      k.ctl.headYaw = clamp(wrapAngle(faceAngle(toM) - k.yaw), -1, 1) * 0.8
      k.ctl.neck = 0.5 * dSeg(pt, 0.2, 0.8)
      k.ctl.headPitch = -0.4 * dSeg(pt, 0.2, 0.8)
      if !roared && pt > 0.8 {
        roared = true
        w.sound(.rexroar, 0.9, dHeadOf(k), 0.95)
      }
      k.ctl.jaw = 0.95 * dBump(pt, 0.8, 2.0)
      k.tremble = 0.3 * u
      if pt >= hit {
        if let p = w.puppet(meteor) { p.alive = false }
        w.fx.fireball(impact + vec3(0, 0.3, 0), 1.6)
        w.fx.embers(impact + vec3(0, 0.2, 0), 60, 4.0)
        w.fx.smoke(impact, 30, 0.8, 1.2, 0.75)
        w.fx.dust(impact, 60, 1.2, 3.0)
        w.fx.flash(impact + vec3(0, 0.4, 0), 3.0, vec3(6.0, 4.5, 3.0), 0.25)
        w.addFlash(vec3(1.5, 1.1, 0.8))
        w.light(impact + vec3(0, 0.5, 0), vec3(12.0, 6.0, 2.0), 7.0, 1.2)
        w.groundRing(impact, vec3(1.0, 0.6, 0.3), 4.5, 1.0, 0.12)
        w.groundRing(impact, vec3(0.8, 0.7, 0.55), 2.5, 1.4, 0.08)
        w.shake(1.0)
        let away = flatDir(impact, k.pos)
        let left = vec3(cos(k.yaw), 0, -sin(k.yaw))
        fall.life = 6.0
        fall.bleed = 1.3
        let side: Float = dot(away, left) >= 0 ? 1 : -1
        fall.start(w, k, side, away * 0.9, 0.25)
        dBloodSpray(w, dChestOf(k), away + vec3(0, 0.5, 0), 40, 2.0)
        w.slowmo(0.4, 0.8)
        next()
      }
    case 1:
      if fmod(pt, 0.08) < w.lastDt { w.fx.smoke(impact + vec3(0, 0.1, 0), 2, 0.5, 0.8, 0.7) }
      if fall.update(w, dt) && pt > 1.2 { next() }
    case 2:  // the winners celebrate
      for a in w.actors where a.alive && a.color == winner {
        a.selected = pt > (a.phase - floor(a.phase)) * 0.4 && pt < 2.4
      }
      if pt > 0.2 && pt - w.lastDt <= 0.2 {
        for a in w.actors where a.alive && a.color == winner && a.kind == Kind.king {
          w.sound(.rexroar, 0.7, a.pos, 1.0)
          break
        }
      }
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
private let dStyleNames: [[String]] = [
  ["Killing claw", "Pack attack", "Dodge and bite"],
  ["Dive", "Wing buffet", "Talon strike"],
  ["Horn charge", "Horn lock", "Frill bash"],
  ["Rearing stomp", "Neck swing", "Tail lash"],
  ["Hooked claws", "Snatch", "Tail swipe"],
  ["Bone-crusher", "The roar", "Head ram"],
]

func dinoBattleStyleName(_ kind: Int, _ style: Int) -> String {
  if kind < 0 || kind >= Kind.count || style < 0 || style >= kBattleStyles { return "" }
  return dStyleNames[kind][style]
}

func makeDinoBattle(_ w: World, _ kind: Int, _ style: Int, _ plan: MovePlan) -> Anim {
  switch kind * kBattleStyles + style {
  case Kind.pawn * kBattleStyles + 0: return DRaptorClaw(plan)
  case Kind.pawn * kBattleStyles + 1: return DRaptorPack(plan)
  case Kind.pawn * kBattleStyles + 2: return DRaptorDodge(plan)
  case Kind.knight * kBattleStyles + 0: return DPteroDive(plan)
  case Kind.knight * kBattleStyles + 1: return DPteroBuffet(plan)
  case Kind.knight * kBattleStyles + 2: return DPteroTalons(plan)
  case Kind.bishop * kBattleStyles + 0: return DTrikeCharge(plan)
  case Kind.bishop * kBattleStyles + 1: return DTrikeLock(plan)
  case Kind.bishop * kBattleStyles + 2: return DTrikeBash(plan)
  case Kind.rook * kBattleStyles + 0: return DBrachioStomp(plan)
  case Kind.rook * kBattleStyles + 1: return DBrachioNeck(plan)
  case Kind.rook * kBattleStyles + 2: return DBrachioTail(plan)
  case Kind.queen * kBattleStyles + 0: return DSpinoClaws(plan)
  case Kind.queen * kBattleStyles + 1: return DSpinoSnatch(plan)
  case Kind.queen * kBattleStyles + 2: return DSpinoTail(plan)
  case Kind.king * kBattleStyles + 0: return DRexCrush(plan)
  case Kind.king * kBattleStyles + 1: return DRexRoar(plan)
  default: return DRexStomp(plan)
  }
}

func makeDinoQuiet(_ plan: MovePlan) -> Anim { DQuietMove(plan) }
func makeDinoQuickCapture(_ plan: MovePlan) -> Anim { DQuickCapture(plan) }
func makeDinoCheckmate(_ kingId: Int, _ winnerColor: Int) -> Anim { DCheckmate(kingId, winnerColor) }
