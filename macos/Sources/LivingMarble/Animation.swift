// Living pieces: a spring-driven squash/bend rig for every chessman, rigid
// body debris for shattered pieces, weapon props, dynamic lights and the
// cinematic camera.
import Foundation
import simd

@inline(__always) func squareCenter(_ sq: Int) -> vec3 { vec3(Float(sq & 7) - 3.5, 0, 3.5 - Float(sq >> 3)) }
@inline(__always) func faceAngle(_ d: vec3) -> Float { atan2(d.x, d.z) }

func wrapAngle(_ a: Float) -> Float {
  var a = a
  while a > kPI { a -= 2 * kPI }
  while a < -kPI { a += 2 * kPI }
  return a
}
func easeInOut(_ t: Float) -> Float {
  let t = clamp(t, 0, 1)
  return t * t * (3 - 2 * t)
}
func easeOut(_ t: Float) -> Float {
  let t = clamp(t, 0, 1)
  return 1 - (1 - t) * (1 - t)
}
func easeIn(_ t: Float) -> Float {
  let t = clamp(t, 0, 1)
  return t * t
}
func easeOutBack(_ t: Float, _ s: Float = 1.7) -> Float {
  let t = clamp(t, 0, 1) - 1
  return t * t * ((s + 1) * t + s) + 1
}

extension PointLight {
  init(_ pos: vec3, _ radius: Float, _ color: vec3) {
    self.init(pos: pos, radius: radius, color: color)
  }
}

/// Bone matrices of a skinned figure (set 1 medieval, 2 aliens).
func figurePose(_ set: Int, _ asset: Int, _ c: MControls, _ out: inout MPose) {
  if set == 2 {
    Alien.buildPose(asset, c, &out)
  } else {
    Medieval.buildPose(asset, c, &out)
  }
}

struct Spring1 {
  var x: Float = 0, v: Float = 0
  mutating func step(_ target: Float, _ k: Float, _ zeta: Float, _ dt: Float) {
    let c = 2 * zeta * sqrt(k)
    let n = max(1, Int(ceil(dt * 240)))
    let h = dt / Float(n)
    for _ in 0..<n {
      let a = k * (target - x) - c * v
      v += a * h
      x += v * h
    }
  }
}

// ---------------------------------------------------------------------------
// Actor
// ---------------------------------------------------------------------------
final class Actor {
  var id = -1, kind = 0, color = 0, square = -1
  var set = 0  // 0 marble, 1 medieval, 2 aliens
  var alive = true, visible = true
  var pos = vec3(0)
  var yaw: Float = 0, yawTarget: Float = 0, baseYaw: Float = 0, yawRate: Float = 9
  // explicit pose (owned by animations)
  var poseBend = vec2(0)  // local (x = right, y = forward), radians
  var poseSquash: Float = 1, poseTwist: Float = 0
  var tilt = quat.identity
  var tiltPivot = vec3(0)
  var scale: Float = 1
  // look
  var glow = vec4(0)
  var crack: Float = 0
  var crackColor = vec3(1.0, 0.6, 0.25)
  var dissolve: Float = 0
  var dissolveColor = vec3(1.0, 0.8, 0.4)
  var texOffset = vec3(0)
  var stone: Float = 0  // turned to salt
  // behaviour
  var idleWeight: Float = 1
  var tremble: Float = 0
  var selected = false, inCheck = false
  var phase: Float = 0
  var selectT: Float = 0, bob: Float = 0, prevSelS: Float = 0
  var dissolveVel: Float = 0  // automatic dissolve change per second (materialise / vanish)
  var fidgetT: Float = -1, fidgetDir: Float = 1
  var fidgetType = 0
  // secondary motion
  var prevPos = vec3(0), vel = vec3(0), accS = vec3(0)
  var first = true
  var bx = Spring1(), bz = Spring1(), sq = Spring1(), tw = Spring1()
  var rig = vec4(0, 0, 1, 0)
  // medieval figures: what the animations ask for, and the resulting skeleton pose
  var ctl = MControls()
  var pose = MPose()
  var heat: Float = 0  // scorched by dragon fire
  // throne (medieval bishop, queen and king): follows the figure unless it got up
  var seatFree = false
  var seatPos = vec3(0)
  var seatYaw: Float = 0
  var hasSeat: Bool { self.set == 1 && Medieval.throned(kind) }

  /// The figure's resting pose.
  func resetControls() {
    let idle = ctl.idle
    ctl = MControls()
    ctl.idle = idle
    ctl.sit = self.set == 1 && Medieval.throned(kind) ? 1 : 0
    seatFree = false
    seatPos = pos
    seatYaw = yaw
    heat = 0
    figurePose(self.set, kind, ctl, &pose)
  }

  /// A field-by-field copy (C++ copied actors by value).
  func copy() -> Actor {
    let a = Actor()
    a.id = id; a.kind = kind; a.color = color; a.square = square; a.set = set
    a.alive = alive; a.visible = visible
    a.pos = pos
    a.yaw = yaw; a.yawTarget = yawTarget; a.baseYaw = baseYaw; a.yawRate = yawRate
    a.poseBend = poseBend; a.poseSquash = poseSquash; a.poseTwist = poseTwist
    a.tilt = tilt; a.tiltPivot = tiltPivot; a.scale = scale
    a.glow = glow; a.crack = crack; a.crackColor = crackColor
    a.dissolve = dissolve; a.dissolveColor = dissolveColor; a.texOffset = texOffset; a.stone = stone
    a.idleWeight = idleWeight; a.tremble = tremble
    a.selected = selected; a.inCheck = inCheck
    a.phase = phase; a.selectT = selectT; a.bob = bob; a.prevSelS = prevSelS
    a.dissolveVel = dissolveVel
    a.fidgetT = fidgetT; a.fidgetDir = fidgetDir; a.fidgetType = fidgetType
    a.prevPos = prevPos; a.vel = vel; a.accS = accS; a.first = first
    a.bx = bx; a.bz = bz; a.sq = sq; a.tw = tw
    a.rig = rig
    a.ctl = ctl; a.pose = pose; a.heat = heat
    a.seatFree = seatFree; a.seatPos = seatPos; a.seatYaw = seatYaw
    return a
  }

  func height() -> Float {
    let h = set == 1 ? Medieval.pieceHeight(kind) : (set == 2 ? Alien.pieceHeight(kind) : MeshGen.pieceHeight(kind))
    return h * scale
  }
  func radius() -> Float {
    let r = set == 1 ? Medieval.pieceRadius(kind) : (set == 2 ? Alien.pieceRadius(kind) : MeshGen.pieceRadius(kind))
    return r * scale
  }
  /// A medieval or alien figure (ctl / pose).
  var skinned: Bool { self.set != 0 }
  func forward() -> vec3 { vec3(sin(yaw), 0, cos(yaw)) }

  func update(_ dt: Float, _ t: Float, _ audio: Audio?) {
    if dt <= 0 { return }
    if first {
      prevPos = pos
      vel = vec3(0)
      accS = vec3(0)
      sq.x = 1
      first = false
    }
    let prevY = prevPos.y
    let nv = (pos - prevPos) / dt
    let acc = (nv - vel) / dt
    vel = nv
    prevPos = pos
    let k = 1 - exp(-dt * 22)
    accS += (clamp(acc, -80, 80) - accS) * k

    let side = vec3(cos(yaw), 0, -sin(yaw)), fwd = vec3(sin(yaw), 0, cos(yaw))
    let accL = vec2(dot(accS, side), dot(accS, fwd))
    var inertia = -accL * 0.011
    let il = length(inertia)
    if il > 0.5 { inertia *= 0.5 / il }
    let idle = vec2(sin(t * 0.71 + phase), sin(t * 0.53 + phase * 1.7)) * 0.012 * idleWeight
    let breath = 1 + 0.009 * sin(t * 1.8 + phase) * idleWeight
    var trem = vec2(0)
    if tremble > 0 {
      trem = vec2(sin(t * 47 + phase), sin(t * 41 + phase * 2)) * 0.035 * tremble
    }

    // eager hopping while selected
    if selected {
      selectT += dt
      let s = sin(selectT * 5.5)
      let target = 0.07 * abs(s)
      bob += (target - bob) * (1 - exp(-dt * 30))
      if prevSelS * s < 0 && selectT > 0.05 { sq.v -= 1.6 }
      prevSelS = s
    } else {
      selectT = 0
      prevSelS = 0
      bob *= exp(-dt * 12)
    }

    let bt = poseBend + idle + inertia + trem
    bx.step(bt.x, 150, 0.30, dt)
    bz.step(bt.y, 150, 0.30, dt)
    let vy = abs(vel.y)
    let sqT = poseSquash * breath * (1 + min(vy * 0.025, 0.12))
    if prevY > 0.012 && pos.y <= 0.006 && vel.y < -0.8 { sq.v -= min(-vel.y * 1.1, 5) }
    sq.step(sqT, 260, 0.22, dt)
    tw.step(poseTwist, 120, 0.45, dt)
    let d = wrapAngle(yawTarget - yaw)
    yaw = wrapAngle(yaw + d * (1 - exp(-dt * yawRate)))
    if dissolveVel != 0 {
      dissolve = clamp(dissolve + dissolveVel * dt, 0, 1)
      if dissolve <= 0 || dissolve >= 1 { dissolveVel = 0 }
    }
    let b = vec2(bx.x, bz.x)
    rig = vec4(length(b), atan2(b.y, b.x), clamp(sq.x, 0.35, 1.9), tw.x)
    if set != 0 {
      ctl.idle += dt
      figurePose(set, kind, ctl, &pose)
      if !seatFree {
        seatPos = pos
        seatYaw = yaw
      }
    }
  }

  /// Snap the rig to rest (before shattering).
  func neutralize() {
    bx = Spring1()
    bz = Spring1()
    tw = Spring1()
    sq.x = 1
    sq.v = 0
    rig = vec4(0, 0, 1, 0)
    bob = 0
  }

  func rotation() -> quat { angleAxis(yaw, vec3(0, 1, 0)) * tilt }

  /// Without tilt.
  func rigidModel() -> mat4 {
    var m = translate(mat4.identity, pos + vec3(0, set == 1 ? bob * 0.3 : bob, 0))
    m = rotate(m, yaw, vec3(0, 1, 0))
    return m * mat4(diagonal: vec4(scale, scale, scale, 1))
  }

  func model() -> mat4 {
    var m = translate(mat4.identity, pos + vec3(0, set == 1 ? bob * 0.3 : bob, 0))
    m = rotate(m, yaw, vec3(0, 1, 0))
    if abs(tilt.w) < 0.99999 {
      m = translate(m, tiltPivot * scale)
      m = m * mat4_cast(tilt)
      m = translate(m, -tiltPivot * scale)
    }
    return m * mat4(diagonal: vec4(scale, scale, scale, 1))
  }
}

final class Chunk {
  var kind = 0, color = 0, index = 0
  var set = 0  // 1: 'kind' is a medieval asset, 2: an alien one
  var half = false
  var pos = vec3(0), v = vec3(0), w = vec3(0)
  var rot = quat.identity
  var scale: Float = 1  // of the piece it broke off (a shrunken victim leaves small pieces)
  var radius: Float = 0.1, mass: Float = 1, inertia: Float = 1
  var age: Float = 0, still: Float = 0, dissolve: Float = 0
  var sleeping = false
  var texOffset = vec3(0)
  var glow: Float = 0
  var glowColor = vec3(1, 0.6, 0.2)
  var lastHit: Float = 0
}

enum PropType { case sword, cannonball }

final class Prop {
  var type = PropType.sword
  var pos = vec3(0)
  var rot = quat.identity
  var scale: Float = 1
  var dissolve: Float = 0
  var glow = vec4(0)
  var heat: Float = 0
  var alive = true
  var id = 0
}

/// A skinned creature or object that is not a chessman (the dragon, a cow, the mothership).
final class Puppet {
  var asset = 0, color = 0, id = 0
  var set = 1  // 1 medieval asset, 2 alien asset
  var pos = vec3(0)
  var yaw: Float = 0, scale: Float = 1
  var tilt = quat.identity  // applied after the yaw (a flying spear's pitch)
  var ctl = MControls()
  var pose = MPose()
  var dissolve: Float = 0
  var glow = vec4(0)
  var texOffset = vec3(0)
  var alive = true

  func model() -> mat4 {
    var m = translate(mat4.identity, pos)
    m = rotate(m, yaw, vec3(0, 1, 0))
    m = m * mat4_cast(tilt)
    return m * mat4(diagonal: vec4(scale, scale, scale, 1))
  }
}

struct GroundRing {
  var pos: vec3, color: vec3
  var speed: Float, life: Float, maxLife: Float, width: Float
}

struct TimedLight {
  var pos: vec3, color: vec3
  var radius: Float, life: Float, maxLife: Float
}

struct CamPose {
  var pos = vec3(0, 9, 10), target = vec3(0)
  var fov: Float = 38, focus: Float = 12, aperture: Float = 2.5
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
final class CameraDirector {
  var yaw: Float = 0, pitch: Float = 0.80, dist: Float = 12.6
  var target = vec3(0, 0.15, 0)
  var autoSpin: Float = 0  // idle orbit (attract mode)
  var current = CamPose()

  private var yawS: Float = 0, pitchS: Float = 0.8, distS: Float = 12.6
  private var initialized = false
  private var shotOn = false
  private var shot = CamPose()
  private var blend: Float = 0, blendSpeed: Float = 2
  private var shotT: Float = 0

  var inShot: Bool { shotOn }

  func orbit(_ dy: Float, _ dp: Float, _ dd: Float) {
    yaw = wrapAngle(yaw + dy)
    pitch = clamp(pitch + dp, -1.45, 1.45)  // the board floats: look at it from above or below
    dist = clamp(dist * exp(dd), 5, 24)
  }
  func setSide(_ color: Int, _ instant: Bool = false) {
    yaw = color == 0 ? 0 : kPI
    if instant { yawS = yaw }
  }
  func setShot(_ p: CamPose, _ blendTime: Float) {
    shot = p
    shotOn = true
    shotT = 0
    blendSpeed = 1 / max(0.05, blendTime)
  }
  func release(_ blendTime: Float) {
    shotOn = false
    blendSpeed = 1 / max(0.05, blendTime)
  }
  func orbitPose() -> CamPose {
    var p = CamPose()
    p.target = target
    p.pos = target + distS * vec3(sin(yawS) * cos(pitchS), sin(pitchS), cos(yawS) * cos(pitchS))
    p.fov = 38
    p.focus = length(p.pos - p.target)
    p.aperture = 2.2
    return p
  }
  @discardableResult
  func evaluate(_ dt: Float, _ trauma: Float, _ time: Float) -> CamPose {
    if !initialized {
      yawS = yaw
      pitchS = pitch
      distS = dist
      initialized = true
    }
    if autoSpin != 0 { yaw = wrapAngle(yaw + autoSpin * dt) }
    let k = 1 - exp(-dt * 5)
    yawS = wrapAngle(yawS + wrapAngle(yaw - yawS) * k)
    pitchS += (pitch - pitchS) * k
    distS += (dist - distS) * k
    let o = orbitPose()
    shotT += dt
    blend = clamp(blend + (shotOn ? 1 : -1) * blendSpeed * dt, 0, 1)
    let e = easeInOut(blend)
    var r = o
    if e > 0 {
      var s = shot
      // slow cinematic drift around the action
      var off = s.pos - s.target
      let a = shotT * 0.07
      off = vec3(cos(a) * off.x + sin(a) * off.z, off.y, -sin(a) * off.x + cos(a) * off.z)
      s.pos = s.target + off
      let oo = o.pos - o.target, so = s.pos - s.target
      let lo = length(oo), ls = length(so)
      let dir = normalize(mix(oo / lo, so / ls, e) + vec3(1e-5))
      r.target = mix(o.target, s.target, e)
      r.pos = r.target + dir * mix(lo, ls, e)
      r.fov = mix(o.fov, s.fov, e)
      r.focus = length(r.pos - r.target)
      r.aperture = mix(o.aperture, s.aperture, e)
    }
    let amt = trauma * trauma * 0.10
    if amt > 0 {
      let n = vec3(sin(time * 37) + sin(time * 23 + 1.3), sin(time * 31 + 2.1) + sin(time * 19),
                   sin(time * 29 + 4.2) + sin(time * 41 + 0.7))
      r.pos += n * amt
      r.target += n * amt * 0.4
    }
    current = r
    return r
  }
}

/// A choreographed sequence (move, battle, promotion, ...).
protocol Anim: AnyObject {
  /// Returns true when finished.
  func update(_ w: World, _ dt: Float) -> Bool
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
final class World {
  var lib = Library()
  var mlib = MedievalLibrary()
  var alib = AlienLibrary()
  var pieceSet = 0  // 0 Living Marble, 1 medieval, 2 aliens
  var puppets: [Puppet] = []
  var nextPuppetId = 1
  var audio: Audio?
  var actors: [Actor] = []
  var actorAt = [Int](repeating: -1, count: 64)
  let fx = ParticleSystem()
  var chunks: [Chunk] = []
  var props: [Prop] = []
  var lights: [TimedLight] = []
  var rings: [GroundRing] = []
  var frameLights: [PointLight] = []  // cleared every frame
  let cam = CameraDirector()
  var trauma: Float = 0
  var timeScale: Float = 1, slowTarget: Float = 1, slowTimer: Float = 0
  var flash = vec3(0)
  var time: Float = 0
  var lastDt: Float = 0  // scaled (slow-motion aware) step of the last update
  var cinematic = true
  var nextPropId = 1
  var moteTimer: Float = 0
  var motes = true
  var fidgetAllowed = false
  var fidgetTimer: Float = 2
  var excludeFidget = -1

  func at(_ square: Int) -> Actor? { actorAt[square] >= 0 ? actors[actorAt[square]] : nil }
  func puppet(_ id: Int) -> Puppet? { puppets.first { $0.id == id } }
  func addPuppet(_ asset: Int, _ color: Int, _ set: Int = 1) -> Int {
    let p = Puppet()
    p.asset = asset
    p.color = color
    p.set = set
    p.id = nextPuppetId
    nextPuppetId += 1
    p.texOffset = vec3(fx.rnd(-20, 20), fx.rnd(-20, 20), fx.rnd(-20, 20))
    figurePose(set, asset, p.ctl, &p.pose)
    puppets.append(p)
    return p.id
  }
  func seatModel(_ a: Actor) -> mat4 {
    var m = translate(mat4.identity, a.seatPos)
    m = rotate(m, a.seatYaw, vec3(0, 1, 0))
    let s = a.scale * Medieval.seatScale(a.kind)
    return m * mat4(diagonal: vec4(s, s, s, 1))
  }
  /// The meshes of a skinned set's asset (set 1 medieval, 2 aliens).
  func figureAssets(_ set: Int, _ asset: Int) -> PieceAssets { set == 2 ? alib.asset[asset] : mlib.asset[asset] }
  func assetsOf(_ c: Chunk) -> PieceAssets { c.set != 0 ? figureAssets(c.set, c.kind) : lib.piece[c.kind] }
  func shake(_ amount: Float) { trauma = min(1, trauma + amount) }
  func slowmo(_ scale: Float, _ duration: Float) {
    slowTarget = scale
    slowTimer = duration
  }
  func light(_ p: vec3, _ c: vec3, _ radius: Float, _ life: Float) {
    lights.append(TimedLight(pos: p, color: c, radius: radius, life: life, maxLife: life))
  }
  func addFlash(_ c: vec3) { flash += c }
  func groundRing(_ p: vec3, _ c: vec3, _ speed: Float = 3, _ life: Float = 0.6, _ width: Float = 0.08) {
    rings.append(GroundRing(pos: p, color: c, speed: speed, life: life, maxLife: life, width: width))
  }

  func reset() {
    actors.removeAll()
    puppets.removeAll()
    for i in 0..<64 { actorAt[i] = -1 }
    fx.clear()
    chunks.removeAll()
    props.removeAll()
    lights.removeAll()
    rings.removeAll()
    frameLights.removeAll()
    trauma = 0
    timeScale = 1
    slowTarget = 1
    slowTimer = 0
    flash = vec3(0)
  }

  func spawnFromBoard(_ sq: SIMD64<UInt8>, _ materialize: Bool) {
    actors.removeAll()
    for i in 0..<64 { actorAt[i] = -1 }
    for s in 0..<64 {
      let p = Int(sq[s])
      if p == 0 { continue }
      let a = Actor()
      a.id = actors.count
      a.kind = (p & 7) - 1
      a.color = p >> 3
      a.square = s
      a.set = pieceSet
      a.pos = squareCenter(s)
      a.baseYaw = a.color == 0 ? kPI : 0
      a.yaw = a.baseYaw
      a.yawTarget = a.baseYaw
      a.phase = fx.rnd(0, 100)
      a.ctl.idle = fx.rnd(0, 100)
      a.resetControls()
      a.texOffset = vec3(fx.rnd(-20, 20), fx.rnd(-20, 20), fx.rnd(-20, 20))
      if materialize {
        a.dissolve = 1
        a.dissolveVel = -fx.rnd(0.9, 1.4)
        a.dissolveColor = vec3(1.0, 0.75, 0.35)
      }
      actorAt[s] = a.id
      actors.append(a)
    }
  }

  func sound(_ s: Sfx, _ vol: Float = 1, _ at: vec3 = vec3(0), _ pitch: Float = 1) {
    guard let audio else { return }
    // pan relative to the camera's right vector
    let f = normalize(cam.current.target - cam.current.pos)
    let right = normalize(cross(f, vec3(0, 1, 0)) + vec3(1e-5))
    let pan = clamp(dot(at - cam.current.target, right) / 5, -0.8, 0.8)
    audio.play(s, volume: vol, pan: pan, pitch: pitch * (timeScale < 0.9 ? mix(0.75, 1, timeScale) : 1))
  }

  func prop(_ id: Int) -> Prop? { props.first { $0.id == id } }

  func addProp(_ t: PropType) -> Int {
    let p = Prop()
    p.type = t
    p.id = nextPropId
    nextPropId += 1
    props.append(p)
    return p.id
  }

  /// Break a medieval or alien asset (e.g. a throne) drawn with model matrix M.
  func shatterAsset(_ asset: Int, _ color: Int, _ M: mat4, _ R: quat, _ vel: vec3, _ texOffset: vec3, _ impact: vec3,
                    _ impulse: vec3, _ explode: Float, _ upBias: Float, _ spin: Float, _ glow: Float,
                    _ glowColor: vec3, _ set: Int = 1) {
    let A = figureAssets(set, asset)
    let sc = length(vec3(M.columns.0))
    let center = vec3(M * vec4(0, A.height * 0.5, 0, 1))
    for (i, f) in A.shards.enumerated() {
      let c = Chunk()
      c.set = set
      c.kind = asset
      c.color = color
      c.index = i
      c.pos = vec3(M * vec4(f.centroid, 1))
      c.rot = R
      c.radius = f.radius * sc
      c.scale = sc
      c.mass = max(f.volume, 1e-4)
      c.inertia = 0.4 * c.mass * c.radius * c.radius
      let dir = normalize(c.pos - center + fx.rndDir() * 0.05)
      let fall = exp(-length(c.pos - impact) * 2.2)
      c.v = vel * 0.5 + dir * explode * fx.rnd(0.5, 1.2) + impulse * fall * fx.rnd(0.7, 1.3) +
        vec3(0, upBias * fx.rnd(0.4, 1.2), 0)
      c.w = fx.rndDir() * spin * fx.rnd(0.3, 1.0)
      c.texOffset = texOffset
      c.glow = glow
      c.glowColor = glowColor
      chunks.append(c)
    }
  }

  /// Glittering glass shards flying from an alien's canopy or jar.
  func glassBurst(_ a: Actor) {
    let c = vec3(a.model() * vec4(Alien.headPoint(a.kind), 1))
    for _ in 0..<70 {
      var p = Particle()
      let d = fx.rndDir()
      p.pos = c + d * 0.12 * a.scale
      p.vel = d * fx.rnd(1.0, 3.2) + vec3(0, fx.rnd(0.5, 2.0), 0)
      p.size = fx.rnd(0.006, 0.014)
      p.color = vec3(1.4, 1.6, 1.8) * fx.rnd(0.6, 1.4)
      p.colorEnd = p.color * 0.4
      p.opacity = 0
      p.maxLife = fx.rnd(0.7, 1.6)
      p.type = .spark
      p.stretch = 0.012
      p.gravity = 9.0
      p.drag = 0.5
      p.bounce = true
      p.fadeIn = 0.0
      fx.add(p)
    }
    sound(.chime, 0.5, c, 2.4)
    sound(.shatter, 0.5, c, 1.6)
  }

  /// Shatter an actor into Voronoi chunks.
  func shatter(_ a: Actor, _ impact: vec3, _ impulse: vec3, _ explode: Float, _ upBias: Float, _ spin: Float = 6) {
    if a.set != 0 {
      a.neutralize()
      let M = a.model()
      let R = a.rotation()
      let glow: Float = a.crack > 0.05 ? 1 : 0
      shatterAsset(a.kind, a.color, M, R, a.vel, a.texOffset, impact, impulse, explode, upBias, spin, glow, a.crackColor,
                   a.set)
      // the saucer's canopy and the psychic's jar burst into glass
      if a.set == 2 && Alien.glassAsset(a.kind) >= 0 { glassBurst(a) }
      if a.hasSeat && !a.seatFree {
        shatterAsset(Medieval.seatAsset(a.kind), a.color, seatModel(a), angleAxis(a.seatYaw, vec3(0, 1, 0)), a.vel,
                     a.texOffset + vec3(3.1), impact, impulse, explode * 0.8, upBias * 0.7, spin * 0.7, glow,
                     a.crackColor)
      }
      a.alive = false
      a.visible = false
      let center = vec3(M * vec4(0, a.height() * 0.5 / max(a.scale, 1e-3), 0, 1))
      let tint = a.color == 0 ? vec3(0.62, 0.60, 0.57) : vec3(0.22, 0.21, 0.2)
      fx.dust(vec3(center.x, 0.05, center.z), 16, 0.3, 1.2, tint)
      fx.smoke(center, 6, 0.2, 0.3, a.color == 0 ? 0.1 : 0.6)
      fx.chips(center, 45, 3.2, a.color == 1)
      sound(.shatter, 1, center, fx.rnd(0.92, 1.08))
      return
    }
    let A = lib.piece[a.kind]
    a.neutralize()
    let M = a.model()
    let R = a.rotation()
    let center = vec3(M * vec4(0, A.height * 0.5, 0, 1))
    for (i, f) in A.shards.enumerated() {
      let c = Chunk()
      c.kind = a.kind
      c.color = a.color
      c.index = i
      c.pos = vec3(M * vec4(f.centroid, 1))
      c.rot = R
      c.radius = f.radius * a.scale
      c.scale = a.scale
      c.mass = max(f.volume, 1e-4)
      c.inertia = 0.4 * c.mass * c.radius * c.radius
      let dir = normalize(c.pos - center + fx.rndDir() * 0.05)
      let fall = exp(-length(c.pos - impact) * 2.2)
      c.v = a.vel * 0.5 + dir * explode * fx.rnd(0.5, 1.2) + impulse * fall * fx.rnd(0.7, 1.3) +
        vec3(0, upBias * fx.rnd(0.4, 1.2), 0)
      c.w = fx.rndDir() * spin * fx.rnd(0.3, 1.0)
      c.texOffset = a.texOffset + f.centroid * 1.25
      c.glow = a.crack > 0.05 ? 1 : 0
      c.glowColor = a.crackColor
      chunks.append(c)
    }
    a.alive = false
    a.visible = false
    let tint = a.color == 0 ? vec3(0.62, 0.60, 0.57) : vec3(0.22, 0.21, 0.2)
    fx.dust(vec3(center.x, 0.05, center.z), 16, 0.3, 1.2, tint)
    fx.smoke(center, 6, 0.2, 0.3, a.color == 0 ? 0.1 : 0.6)
    fx.chips(center, 45, 3.2, a.color == 1)
    sound(.shatter, 1, center, fx.rnd(0.92, 1.08))
  }

  /// Slice an actor in two along its slice plane.
  func slice(_ a: Actor, _ sep: Float) {
    let A = a.set != 0 ? figureAssets(a.set, a.kind) : lib.piece[a.kind]
    a.neutralize()
    let M = a.model()
    let R = a.rotation()
    let nW = R * A.sliceN
    let down = vec3(0, -1, 0)
    var td = down - nW * dot(down, nW)
    td = length(td) > 1e-3 ? normalize(td) : vec3(0, -1, 0)
    for (i, f) in A.halves.enumerated() {
      let c = Chunk()
      c.set = a.set
      c.kind = a.kind
      c.color = a.color
      c.index = i
      c.half = true
      c.pos = vec3(M * vec4(f.centroid, 1))
      c.rot = R
      c.radius = f.radius * a.scale
      c.scale = a.scale
      c.mass = max(f.volume, 1e-4)
      c.inertia = 0.4 * c.mass * c.radius * c.radius
      if i == 0 {
        c.v = nW * sep + td * 0.8 + vec3(0, 0.6, 0)
        c.w = cross(nW, td) * 1.8 + fx.rndDir() * 0.5
      } else {
        c.v = -nW * sep * 0.25
        c.w = fx.rndDir() * 0.6
      }
      c.texOffset = a.texOffset + f.centroid * 1.25
      c.glow = 1
      c.glowColor = vec3(1.0, 0.75, 0.35)
      chunks.append(c)
    }
    // a throne under a figure that is cut in two breaks apart as well
    if a.hasSeat && !a.seatFree {
      shatterAsset(Medieval.seatAsset(a.kind), a.color, seatModel(a), angleAxis(a.seatYaw, vec3(0, 1, 0)), a.vel,
                   a.texOffset + vec3(3.1), a.pos + vec3(0, 0.5, 0), vec3(0, -1.0, 0), 1.0, 0.5, 5.0, 0.0, vec3(1.0))
    }
    // ... and a glass canopy or jar bursts
    if a.set == 2 && Alien.glassAsset(a.kind) >= 0 { glassBurst(a) }
    a.alive = false
    a.visible = false
  }

  private func updateChunks(_ dt: Float) {
    let g: Float = 14
    for c in chunks {
      c.age += dt
      c.glow *= exp(-dt * 1.6)
      if c.age > 3.2 && (c.sleeping || c.age > 6) {
        if c.dissolve == 0 { fx.dust(c.pos, 2, 0.05, 0.3, c.color == 0 ? vec3(0.6) : vec3(0.2)) }
        c.dissolve = min(1, c.dissolve + dt * 0.85)
      }
      // standing pieces act as cylinders and shove debris (even resting debris) aside
      for a in actors {
        if !a.alive || !a.visible { continue }
        let h = a.pos.y + a.height()
        if c.pos.y - c.radius * 0.5 > h || c.pos.y + c.radius < a.pos.y { continue }
        let d = vec2(c.pos.x - a.pos.x, c.pos.z - a.pos.z)
        let dist = length(d)
        let minD = a.radius() * 0.85 + c.radius * 0.55
        if dist < minD {
          let n = dist > 1e-4 ? d / dist : vec2(1, 0)
          let push = minD - dist
          c.pos.x += n.x * push
          c.pos.z += n.y * push
          let vn = c.v.x * n.x + c.v.z * n.y
          let shove = min(2.5, length(vec2(a.vel.x, a.vel.z)) * 1.2)
          if vn < shove {
            c.v.x += n.x * (shove - vn)
            c.v.z += n.y * (shove - vn)
          }
          if c.sleeping && push > 0.004 {
            c.sleeping = false
            c.still = 0
            c.w += vec3(n.y, 0, -n.x) * 3
          }
        }
      }
      if c.sleeping { continue }
      c.v.y -= g * dt
      c.pos += c.v * dt
      let wq = quat(0, c.w.x, c.w.y, c.w.z)
      c.rot = (c.rot + (wq * c.rot) * (0.5 * dt)).normalized
      // the board floats in the air: debris that leaves it tumbles away below
      let onBoard = abs(c.pos.x) < 4.62 && abs(c.pos.z) < 4.62 && c.pos.y > -0.1
      if !onBoard { continue }
      let floorY: Float = 0
      let A = assetsOf(c)
      let hull = c.half ? A.halves[c.index].hull : A.shards[c.index].hull
      var maxPen: Float = 0
      var contact = false
      for hp in hull {
        let r = c.rot * (hp * c.scale)
        let p = c.pos + r
        let pen = floorY - p.y
        if pen <= 0 { continue }
        contact = true
        maxPen = max(maxPen, pen)
        let vp = c.v + cross(c.w, r)
        if vp.y >= 0 { continue }
        let n = vec3(0, 1, 0)
        let invM = 1 / c.mass, invI = 1 / c.inertia
        let rn = cross(r, n)
        let e: Float = abs(vp.y) > 1 ? 0.28 : 0
        let j = -(1 + e) * vp.y / (invM + dot(rn, rn) * invI)
        c.v += n * j * invM
        c.w += invI * cross(r, n * j)
        let vt = vp - n * vp.y
        let vtl = length(vt)
        if vtl > 1e-4 {
          let t = -vt / vtl
          let rt = cross(r, t)
          let jt = min(0.55 * j, vtl / (invM + dot(rt, rt) * invI))
          c.v += t * jt * invM
          c.w += invI * cross(r, t * jt)
        }
        if -vp.y > 1.4 && c.age - c.lastHit > 0.12 {
          c.lastHit = c.age
          sound(.debris, min(1, -vp.y / 6) * 0.7, c.pos, fx.rnd(0.8, 1.3) * (1 / (0.6 + c.radius * 3)))
          if -vp.y > 3 { fx.chips(p, 3, 1.2, c.color == 1) }
        }
      }
      if contact {
        c.pos.y += maxPen
        c.w *= exp(-dt * 2)
        if length(c.v) < 0.06 && length(c.w) < 0.4 {
          c.still += dt
          if c.still > 0.35 {
            c.sleeping = true
            c.v = vec3(0)
            c.w = vec3(0)
          }
        } else {
          c.still = 0
        }
      }
    }
    // chunk vs chunk separation
    for i in chunks.indices {
      for j in (i + 1)..<chunks.count {
        let a = chunks[i], b = chunks[j]
        let d = b.pos - a.pos
        let r = (a.radius + b.radius) * 0.5
        let l2 = dot(d, d)
        if l2 >= r * r || l2 < 1e-8 { continue }
        let l = sqrt(l2)
        let n = d / l
        let pen = r - l
        let wa = b.mass / (a.mass + b.mass), wb = 1 - wa
        if !a.sleeping { a.pos -= n * pen * wa }
        if !b.sleeping { b.pos += n * pen * wb }
        let rv = dot(b.v - a.v, n)
        if rv < 0 {
          a.v += n * rv * 0.5 * wa
          b.v -= n * rv * 0.5 * wb
        }
        if pen > 0.02 {
          a.sleeping = false
          b.sleeping = false
          a.still = 0
          b.still = 0
        }
      }
    }
    chunks.removeAll { $0.dissolve >= 1 }
  }

  /// Slow-motion and decays; returns the scaled dt.
  func beginFrame(_ dtReal: Float) -> Float {
    if slowTimer > 0 {
      slowTimer -= dtReal
    } else {
      slowTarget = 1
    }
    timeScale += (slowTarget - timeScale) * (1 - exp(-dtReal * 14))
    let dt = dtReal * timeScale
    lastDt = dt
    time += dt
    trauma = max(0, trauma - dtReal * 1.5)
    flash *= exp(-dtReal * 7)
    frameLights.removeAll(keepingCapacity: true)
    return dt
  }

  func stopFidgets() {
    for a in actors where a.fidgetT >= 0 {
      if a.set != 0 {
        a.ctl.headYaw = 0
        a.ctl.headPitch = 0
        a.ctl.lean = 0
        a.ctl.twist = 0
        a.ctl.armL = vec3(0)
        a.ctl.armR = vec3(0)
        a.ctl.perk = 0
        a.ctl.jaw = 0
        a.ctl.tongue = 0
        a.ctl.hover = 0
        a.ctl.crouch = 0
        a.ctl.hatch = 0
      }
      a.fidgetT = -1
      a.yawTarget = a.baseYaw
      a.poseSquash = 1
      a.poseBend = vec2(0)
      if a.fidgetType == 1 { a.pos.y = 0 }
    }
  }

  /// Idle life: now and then a piece glances around, hops on the spot or stretches.
  private func updateFidgets(_ dt: Float) {
    if !fidgetAllowed {
      stopFidgets()
      return
    }
    fidgetTimer -= dt
    if fidgetTimer <= 0 && !actors.isEmpty {
      fidgetTimer = fx.rnd(0.7, 2.0)
      var tries = 6
      while tries > 0 {
        tries -= 1
        let a = actors[Int(fx.rnd() * Float(actors.count)) % actors.count]
        if !a.alive || !a.visible || a.fidgetT >= 0 || a.id == excludeFidget || a.dissolve > 0.01 { continue }
        a.fidgetT = 0
        let r = fx.rnd()
        a.fidgetType = r < 0.5 ? 0 : (r < 0.78 ? 1 : 2)
        a.fidgetDir = fx.rnd() < 0.5 ? -1 : 1
        break
      }
    }
    for a in actors {
      if a.fidgetT < 0 || !a.alive { continue }
      a.fidgetT += dt
      let t = a.fidgetT
      var end: Float = 1
      if a.set == 1 {
        // medieval figures don't hop: they look about, shift their weight, toss their heads
        switch a.fidgetType {
        case 0:  // a glance to one side
          end = 2.2
          let u = sin(clamp(t / end, 0, 1) * kPI)
          if a.kind == Kind.knight {
            a.ctl.headYaw = a.fidgetDir * 0.5 * u
            a.ctl.neck = -0.2 * u
          } else if a.kind == Kind.rook {
            a.ctl.idle += dt * 3  // the banner catches a gust
          } else {
            a.ctl.headYaw = a.fidgetDir * 0.6 * u
            a.ctl.twist = a.fidgetDir * 0.12 * u
          }
        case 1:  // horse: toss of the head and a pawing hoof; others: shift weight, adjust grip
          end = 1.3
          let u = clamp(t / end, 0, 1)
          if a.kind == Kind.knight {
            a.ctl.headPitch = -0.35 * sin(u * kPI * 2) * (1 - u)
            a.ctl.tail = u * 6
          } else if a.kind == Kind.pawn {
            a.ctl.armR = vec3(0.25 * sin(u * kPI), 0, 0)
            a.ctl.lean = -0.04 * sin(u * kPI)
          } else {
            a.ctl.lean = 0.05 * sin(u * kPI)
            a.ctl.headPitch = 0.12 * sin(u * kPI)
          }
        default:  // stretch, look up
          end = 1.6
          let u = sin(clamp(t / end, 0, 1) * kPI)
          a.ctl.lean = -0.06 * u
          a.ctl.headPitch = -0.25 * u
          if a.kind == Kind.knight { a.ctl.neck = 0.3 * u }
        }
        if t >= end {
          a.fidgetT = -1
          a.ctl.headYaw = 0
          a.ctl.headPitch = 0
          a.ctl.lean = 0
          a.ctl.twist = 0
          a.ctl.neck = 0
          a.ctl.armR = vec3(0)
        }
        continue
      }
      if a.set == 2 {
        // aliens: antennae twitch, a look round with a blink of the lights, a nervous hop
        switch a.fidgetType {
        case 0:  // look about, antennae swivel
          end = 2.0
          let u = sin(clamp(t / end, 0, 1) * kPI)
          a.ctl.headYaw = a.fidgetDir * 0.7 * u
          a.ctl.perk = 0.6 * u
          if a.kind == Kind.knight { a.ctl.spin += dt * 6 * u }  // the light ring whirls
          if a.kind == Kind.rook { a.ctl.hatch = 0.5 * u }  // the eye peers about
        case 1:  // a quick hop (the saucer bobs, the tripod shifts its weight)
          end = 0.7
          let u = clamp(t / 0.4, 0, 1)
          if a.kind == Kind.rook {
            a.ctl.crouch = 0.3 * sin(u * kPI)
          } else if a.kind == Kind.knight || a.kind == Kind.bishop || a.kind == Kind.king {
            a.ctl.hover = 0.06 * sin(u * kPI)
          } else {
            a.pos.y = 0.08 * sin(u * kPI)
            a.poseSquash = t < 0.07 ? 0.85 : 1
            a.ctl.perk = -0.8 * sin(u * kPI)
          }
        default:  // stretch, a big blink of the eyes / a scratch of the head
          end = 1.5
          let u = sin(clamp(t / end, 0, 1) * kPI)
          a.ctl.lean = -0.08 * u
          a.ctl.headPitch = -0.3 * u
          a.ctl.armL = vec3(0.6 * u, 0.2 * u, 0.4 * u)
          if a.kind == Kind.pawn { a.ctl.jaw = 0.5 * u }  // a yawn
        }
        if t >= end {
          a.fidgetT = -1
          a.ctl.headYaw = 0
          a.ctl.headPitch = 0
          a.ctl.lean = 0
          a.ctl.twist = 0
          a.ctl.perk = 0
          a.ctl.jaw = 0
          a.ctl.hover = 0
          a.ctl.crouch = 0
          a.ctl.hatch = 0
          a.ctl.armL = vec3(0)
          a.ctl.armR = vec3(0)
          a.poseSquash = 1
          if a.fidgetType == 1 { a.pos.y = 0 }
        }
        continue
      }
      switch a.fidgetType {
      case 0:  // glance to one side and back
        end = 1.8
        let u = clamp(t / end, 0, 1)
        let k: Float = a.kind == Kind.knight ? 0.75 : 0.45
        a.yawTarget = a.baseYaw + a.fidgetDir * k * sin(u * kPI)
        a.poseBend = vec2(a.fidgetDir * 0.05 * sin(u * kPI), 0)
      case 1:  // a little hop
        end = 0.6
        let u = clamp(t / 0.34, 0, 1)
        a.pos.y = 0.065 * sin(u * kPI)
        a.poseSquash = t < 0.06 ? 0.9 : 1
      default:  // stretch
        end = 1.4
        let u = clamp(t / end, 0, 1)
        a.poseSquash = 1 + 0.06 * sin(u * kPI)
        a.poseBend = vec2(0, -0.06 * sin(u * kPI))
        a.poseTwist = 0.15 * a.fidgetDir * sin(u * kPI * 2)
      }
      if t >= end {
        a.fidgetT = -1
        a.yawTarget = a.baseYaw
        a.poseSquash = 1
        a.poseBend = vec2(0)
        a.poseTwist = 0
        if a.fidgetType == 1 { a.pos.y = 0 }
      }
    }
  }

  /// Physics, particles, lights, actors.
  func simulate(_ dt: Float) {
    updateFidgets(dt)
    // dust motes drifting through the pool of light
    if motes {
      moteTimer -= dt
      while moteTimer < 0 {
        moteTimer += 0.09
        var p = Particle()
        p.pos = vec3(fx.rnd(-5.5, 5.5), fx.rnd(0.15, 3.2), fx.rnd(-5.5, 5.5))
        p.vel = fx.rndDir() * 0.025 + vec3(0, 0.006, 0)
        p.size = fx.rnd(0.0025, 0.006)
        p.color = vec3(1.0, 0.9, 0.76) * fx.rnd(0.25, 0.8)
        p.colorEnd = p.color
        p.opacity = 0
        p.maxLife = fx.rnd(7, 13)
        p.type = .glow
        p.drag = 0
        p.fadeIn = 2
        fx.add(p)
      }
    }
    for a in actors where a.alive { a.update(dt, time, audio) }
    for p in puppets {
      p.ctl.idle += dt
      figurePose(p.set, p.asset, p.ctl, &p.pose)
    }
    puppets.removeAll { !$0.alive }
    fx.update(dt)
    updateChunks(dt)
    for i in lights.indices { lights[i].life -= dt }
    for i in rings.indices { rings[i].life -= dt }
    rings.removeAll { $0.life <= 0 }
    lights.removeAll { $0.life <= 0 }
    props.removeAll { !$0.alive }
  }

  /// medieval: the medieval set's meshes once they are on the GPU (nil until then).
  /// `figures(set)`: the GPU meshes of a skinned set (1 medieval, 2 aliens) once uploaded, else nil.
  func buildDrawItems(_ pieces: [PieceGpu], _ figures: (Int) -> [PieceGpu]?, _ sword: GpuMesh, _ cannonball: GpuMesh,
                      _ out: inout [DrawItem]) {
    for a in actors {
      if !a.alive || !a.visible || a.dissolve >= 0.999 { continue }
      if a.set != 0 {
        guard let G = figures(a.set) else { continue }
        var d = DrawItem()
        d.mesh = G[a.kind].lod0
        d.meshLow = G[a.kind].lod1
        d.model = a.model()
        if a.set == 1 {
          // painted figures are not made of rubber: only a hint of the squash-and-stretch rig
          d.rig = vec4(a.rig.x * 0.3, a.rig.y, 1 + (a.rig.z - 1) * 0.12, a.rig.w * 0.25)
        } else {
          // aliens are a bit rubbery (the saucer and the tripod less so)
          let k: Float = (a.kind == Kind.knight || a.kind == Kind.rook) ? 0.4 : 0.8
          d.rig = vec4(a.rig.x * k, a.rig.y, 1 + (a.rig.z - 1) * k, a.rig.w * k)
        }
        d.rigH = a.height() / max(a.scale, 1e-3)
        d.material = a.set == 2 ? .alien : .medieval
        d.team = Int32(a.color)
        d.bones = a.pose.active
        d.texOffset = a.texOffset
        d.glow = a.glow
        d.crack = a.crack
        d.crackColor = a.crackColor
        d.dissolve = a.dissolve
        d.dissolveColor = a.dissolveColor
        d.heat = a.heat
        d.stone = a.stone
        out.append(d)
        let ga = a.set == 2 ? Alien.glassAsset(a.kind) : -1
        if ga >= 0 {  // the saucer's canopy, the psychic's jar
          var g = d
          g.mesh = G[ga].lod0
          g.meshLow = G[ga].lod1
          g.material = .glass
          g.glass = true
          g.castShadow = false
          out.append(g)
        }
        if a.set == 1 && a.kind == Kind.rook && a.ctl.legs > 0.001 {
          var l = d
          l.mesh = G[MAsset.legs].lod0
          l.meshLow = G[MAsset.legs].lod1
          l.rig = vec4(0, 0, 1, 0)
          out.append(l)
        }
        if a.hasSeat {
          let sa = Medieval.seatAsset(a.kind)
          var t = d
          t.mesh = G[sa].lod0
          t.meshLow = G[sa].lod1
          t.model = seatModel(a)
          t.rig = a.seatFree ? vec4(0, 0, 1, 0) : d.rig
          t.bones = []
          t.texOffset = a.texOffset + vec3(3.1)
          if a.seatFree { t.glow = vec4(0) }
          out.append(t)
        }
        continue
      }
      var d = DrawItem()
      d.mesh = pieces[a.kind].lod0
      d.meshLow = pieces[a.kind].lod1
      d.model = a.model()
      d.rig = a.rig
      d.rigH = MeshGen.pieceHeight(a.kind)
      d.material = a.color == 0 ? .white : .black
      d.texOffset = a.texOffset
      d.glow = a.glow
      d.crack = a.crack
      d.crackColor = a.crackColor
      d.dissolve = a.dissolve
      d.dissolveColor = a.dissolveColor
      out.append(d)
    }
    for c in chunks {
      let P: PieceGpu
      if c.set != 0 {
        guard let G = figures(c.set) else { continue }
        P = G[c.kind]
      } else {
        P = pieces[c.kind]
      }
      let v = c.half ? P.halves : P.shards
      if c.index >= v.count { continue }
      var d = DrawItem()
      d.mesh = v[c.index]
      d.meshLow = v[c.index]
      d.model = translate(mat4.identity, c.pos) * mat4_cast(c.rot) * scale(mat4.identity, vec3(c.scale))
      d.material = c.set == 1 ? .medieval : (c.set == 2 ? .alien : (c.color == 0 ? .white : .black))
      if c.set == 2 && (c.kind == AAsset.canopy || c.kind == AAsset.jar) {
        d.material = .glass
        d.glass = true
        d.castShadow = false
      }
      d.team = Int32(c.color)
      d.texOffset = c.texOffset
      d.glow = vec4(c.glowColor, c.glow * 1.2)
      d.dissolve = c.dissolve
      d.dissolveColor = c.color == 0 ? vec3(0.9, 0.85, 0.75) : vec3(0.6, 0.5, 0.4)
      out.append(d)
    }
    for p in puppets where p.dissolve < 0.999 {
      guard let G = figures(p.set) else { continue }
      var d = DrawItem()
      d.mesh = G[p.asset].lod0
      d.meshLow = G[p.asset].lod1
      d.model = p.model()
      d.rigH = 1
      d.material = p.set == 2 ? .alien : .medieval
      d.team = Int32(p.color)
      d.bones = p.pose.active
      d.texOffset = p.texOffset
      d.glow = p.glow
      d.dissolve = p.dissolve
      d.dissolveColor = vec3(1.0, 0.5, 0.15)
      out.append(d)
    }
    for p in props {
      var d = DrawItem()
      d.model = translate(mat4.identity, p.pos) * mat4_cast(p.rot) * scale(mat4.identity, vec3(p.scale))
      d.dissolve = p.dissolve
      d.dissolveColor = vec3(1.0, 0.75, 0.3)
      d.glow = p.glow
      d.groundAO = false
      if p.type == .sword {
        d.mesh = sword
        d.meshLow = sword
        d.material = .sword
        d.rigH = 1
      } else {
        d.mesh = cannonball
        d.meshLow = cannonball
        d.material = .iron
        d.heat = p.heat
      }
      out.append(d)
    }
  }

  func collectLights(_ out: inout [PointLight]) {
    out.append(contentsOf: frameLights)
    for l in lights {
      let f = l.life / l.maxLife
      out.append(PointLight(l.pos, l.radius, l.color * f * f))
    }
    // brightest first, renderer keeps 8
    out.sort { dot($0.color, vec3(1)) > dot($1.color, vec3(1)) }
    if out.count > 8 { out.removeLast(out.count - 8) }
  }

  func collectRings(_ r: inout [vec4], _ c: inout [vec4]) {
    for g in rings {
      if r.count >= 6 { break }
      let age = g.maxLife - g.life
      let f = g.life / g.maxLife
      r.append(vec4(g.pos.x, g.pos.z, 0.15 + age * g.speed, f * f))
      c.append(vec4(g.color, g.width * (1 + age * 1.5)))
    }
  }

  func collectOccluders(_ out: inout [vec4]) {
    for a in actors {
      if !a.alive || !a.visible { continue }
      let s = exp(-max(0, a.pos.y + a.bob) / 0.12) * (1 - a.dissolve)
      out.append(vec4(a.pos.x, a.pos.z, a.radius() * 1.05, s))
      if a.hasSeat && a.seatFree { out.append(vec4(a.seatPos.x, a.seatPos.z, 0.3, 1 - a.dissolve)) }
    }
    for c in chunks {
      if out.count >= 32 { break }
      if c.pos.y < -0.1 { continue }  // fallen off the board
      let s = exp(-max(0, c.pos.y - c.radius) / 0.1) * 0.6 * (1 - c.dissolve)
      out.append(vec4(c.pos.x, c.pos.z, c.radius * 0.9, s))
    }
    if out.count > 32 { out.removeLast(out.count - 32) }
  }
}
