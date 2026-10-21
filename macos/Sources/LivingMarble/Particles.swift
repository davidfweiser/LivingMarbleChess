// CPU particle system: dust, smoke, sparks, embers, magic, shockwaves.
import Foundation
import simd

enum ParticleType: Int {
  case glow = 0, smoke = 1, spark = 2, ring = 3
}

struct Particle {
  var pos = vec3(0), vel = vec3(0)
  var size: Float = 0.05, grow: Float = 0
  var color = vec3(1), colorEnd = vec3(1)
  var opacity: Float = 1  // >0 alpha blended (smoke/dust), 0 = additive
  var life: Float = 0, maxLife: Float = 1
  var type = ParticleType.glow
  var drag: Float = 1, gravity: Float = 0
  var rot: Float = 0, rotVel: Float = 0
  var stretch: Float = 0
  var seed: Float = 0
  var fadeIn: Float = 0.05
  var bounce = false
  var still = false  // keep position (used for velocity-stretched bolts)
}

final class ParticleSystem {
  private var p: [Particle] = []
  private var rng = Rng(seed: 1337)

  init() { p.reserveCapacity(6000) }

  var count: Int { p.count }
  func clear() { p.removeAll(keepingCapacity: true) }
  func add(_ q: Particle) {
    if p.count < 6000 { p.append(q) }
  }

  func rnd() -> Float { rng.uniform() }
  func rnd(_ a: Float, _ b: Float) -> Float { rng.uniform(a, b) }
  func rndDir() -> vec3 {
    let z = rnd(-1, 1), a = rnd(0, 6.2831853)
    let r = sqrt(max(0, 1 - z * z))
    return vec3(r * cos(a), z, r * sin(a))
  }

  func update(_ dt: Float) {
    var i = 0
    while i < p.count {
      p[i].life += dt
      if p[i].life >= p[i].maxLife {
        p[i] = p[p.count - 1]
        p.removeLast()
        continue
      }
      if !p[i].still {
        p[i].vel *= exp(-p[i].drag * dt)
        p[i].vel.y -= p[i].gravity * dt
        p[i].pos += p[i].vel * dt
        // only the board top catches particles; past its edge they drop away
        let pos = p[i].pos
        let onBoard = abs(pos.x) < 4.62 && abs(pos.z) < 4.62 && pos.y > -0.1
        if p[i].bounce && onBoard && pos.y < p[i].size * 0.5 {
          p[i].pos.y = p[i].size * 0.5
          if p[i].vel.y < 0 { p[i].vel.y *= -0.32 }
          p[i].vel.x *= 0.6
          p[i].vel.z *= 0.6
        }
      }
      p[i].size = max(0.001, p[i].size + p[i].grow * dt)
      p[i].rot += p[i].rotVel * dt
      i += 1
    }
  }

  func collect(_ out: inout [ParticleGPU]) {
    out.reserveCapacity(out.count + p.count)
    for q in p {
      let f = q.life / q.maxLife
      let a = smoothstep(0, max(q.fadeIn, 1e-4), q.life) * (1 - smoothstep(0.55, 1, f))
      let c = mix(q.color, q.colorEnd, f)
      var g = ParticleGPU()
      g.posSize = vec4(q.pos, q.size)
      if q.opacity > 0 {
        g.color = vec4(c * q.opacity * a, q.opacity * a)
      } else {
        g.color = vec4(c * a, 0)
      }
      g.velType = vec4(q.vel, Float(q.type.rawValue))
      g.misc = vec4(q.rot, q.stretch, q.seed, f)
      out.append(g)
    }
  }

  // ---- effect recipes ----
  func dust(_ at: vec3, _ n: Int, _ radius: Float, _ speed: Float, _ tint: vec3 = vec3(0.55, 0.5, 0.45)) {
    for _ in 0..<n {
      var q = Particle()
      let a = rnd(0, 6.2831853)
      let d = vec3(cos(a), 0, sin(a))
      q.pos = at + d * rnd(0, radius) + vec3(0, rnd(0, 0.05), 0)
      q.vel = d * speed * rnd(0.4, 1.0) + vec3(0, rnd(0.05, 0.35) * speed, 0)
      q.size = rnd(0.06, 0.15)
      q.grow = rnd(0.12, 0.3)
      q.color = tint * rnd(0.85, 1.1)
      q.colorEnd = q.color * 0.9
      q.opacity = rnd(0.18, 0.35)
      q.maxLife = rnd(1.2, 2.4)
      q.type = .smoke
      q.drag = 2.6
      q.gravity = -0.05
      q.rot = rnd(0, 6.28)
      q.rotVel = rnd(-0.6, 0.6)
      q.seed = rnd()
      q.fadeIn = 0.08
      add(q)
    }
  }

  func chips(_ at: vec3, _ n: Int, _ speed: Float, _ black: Bool) {
    for _ in 0..<n {
      var q = Particle()
      var d = rndDir()
      d.y = abs(d.y) * 0.8 + 0.2
      q.pos = at + d * 0.03
      q.vel = normalize(d) * speed * rnd(0.3, 1.0)
      q.size = rnd(0.004, 0.011)
      let v = black ? rnd(0.02, 0.06) : rnd(0.55, 0.8)
      q.color = vec3(v, v * 0.98, v * 0.95)
      q.colorEnd = q.color
      q.opacity = 1
      q.maxLife = rnd(1.0, 2.0)
      q.type = .glow
      q.drag = 0.3
      q.gravity = 9.8
      q.bounce = true
      q.fadeIn = 0.01
      add(q)
    }
  }

  func sparks(_ at: vec3, _ dir: vec3, _ n: Int, _ speed: Float, _ spread: Float,
              _ col: vec3 = vec3(4.0, 2.2, 0.8)) {
    for _ in 0..<n {
      var q = Particle()
      let d = normalize(dir + rndDir() * spread)
      q.pos = at
      q.vel = d * speed * rnd(0.35, 1.0)
      q.size = rnd(0.006, 0.014)
      q.color = col * rnd(0.7, 1.3)
      q.colorEnd = vec3(1.2, 0.25, 0.05)
      q.opacity = 0
      q.maxLife = rnd(0.25, 0.8)
      q.type = .spark
      q.stretch = 0.035
      q.drag = 1.2
      q.gravity = 7.0
      q.bounce = true
      q.fadeIn = 0
      add(q)
    }
  }

  func smoke(_ at: vec3, _ n: Int, _ radius: Float, _ rise: Float, _ darkness: Float = 0.5) {
    for _ in 0..<n {
      var q = Particle()
      q.pos = at + rndDir() * radius * rnd(0.2, 1.0)
      q.vel = rndDir() * 0.25 + vec3(0, rise * rnd(0.5, 1.2), 0)
      q.size = rnd(0.08, 0.18)
      q.grow = rnd(0.16, 0.3)
      let g = mix(0.30, 0.06, darkness)
      q.color = vec3(g, g * 0.97, g * 0.94)
      q.colorEnd = q.color * 0.8
      q.opacity = rnd(0.16, 0.32)
      q.maxLife = rnd(1.3, 2.4)
      q.type = .smoke
      q.drag = 1.4
      q.gravity = -0.12
      q.rot = rnd(0, 6.28)
      q.rotVel = rnd(-0.5, 0.5)
      q.seed = rnd()
      q.fadeIn = 0.15
      add(q)
    }
  }

  func fireball(_ at: vec3, _ s: Float) {
    flash(at, 1.4 * s, vec3(8.0, 5.0, 2.2), 0.18)
    for _ in 0..<40 {
      var q = Particle()
      var d = rndDir()
      d.y = abs(d.y) * 0.9 + 0.1
      q.pos = at + d * 0.05 * s
      q.vel = d * rnd(0.6, 2.6) * s
      q.size = rnd(0.08, 0.2) * s
      q.grow = rnd(0.3, 0.7) * s
      q.color = vec3(6.0, 2.6, 0.7) * rnd(0.6, 1.2)
      q.colorEnd = vec3(0.6, 0.08, 0.02)
      q.opacity = 0
      q.maxLife = rnd(0.35, 0.8)
      q.type = .smoke
      q.drag = 3.0
      q.gravity = -0.6
      q.rot = rnd(0, 6.28)
      q.seed = rnd()
      q.fadeIn = 0.02
      add(q)
    }
    smoke(at + vec3(0, 0.1, 0), 7, 0.2 * s, 0.5, 0.8)
    embers(at, 30, 2.2 * s)
  }

  func embers(_ at: vec3, _ n: Int, _ speed: Float) {
    for _ in 0..<n {
      var q = Particle()
      var d = rndDir()
      d.y = abs(d.y)
      q.pos = at
      q.vel = d * speed * rnd(0.2, 1.0)
      q.size = rnd(0.008, 0.018)
      q.color = vec3(5.0, 1.8, 0.4) * rnd(0.6, 1.4)
      q.colorEnd = vec3(0.8, 0.1, 0.02)
      q.opacity = 0
      q.maxLife = rnd(0.8, 2.4)
      q.type = .glow
      q.drag = 1.6
      q.gravity = 1.5
      q.bounce = true
      add(q)
    }
  }

  func magicBurst(_ at: vec3, _ col: vec3, _ n: Int, _ speed: Float) {
    for _ in 0..<n {
      var q = Particle()
      let d = rndDir()
      q.pos = at + d * 0.03
      q.vel = d * speed * rnd(0.3, 1.0)
      q.size = rnd(0.012, 0.035)
      q.color = col * rnd(0.6, 1.4)
      q.colorEnd = col * 0.2
      q.opacity = 0
      q.maxLife = rnd(0.4, 1.3)
      q.type = .glow
      q.drag = 2.0
      q.gravity = -0.2
      add(q)
    }
  }

  func magicTrail(_ at: vec3, _ vel: vec3, _ col: vec3) {
    for _ in 0..<3 {
      var q = Particle()
      q.pos = at + rndDir() * 0.03
      q.vel = vel * 0.1 + rndDir() * 0.15
      q.size = rnd(0.015, 0.04)
      q.color = col * rnd(0.5, 1.2)
      q.colorEnd = col * 0.1
      q.opacity = 0
      q.maxLife = rnd(0.25, 0.6)
      q.type = .glow
      q.drag = 3.0
      add(q)
    }
  }

  func swirl(_ c: vec3, _ radius: Float, _ col: vec3, _ t: Float) {
    for i in 0..<2 {
      var q = Particle()
      let a = t * 7.0 + Float(i) * 3.14159 + rnd(-0.3, 0.3)
      let d = vec3(cos(a), 0, sin(a))
      q.pos = c + d * radius + vec3(0, rnd(-0.3, 0.3), 0)
      q.vel = vec3(-d.z, 0, d.x) * 2.5 * radius + vec3(0, rnd(0.2, 0.8), 0) - d * 0.4
      q.size = rnd(0.012, 0.03)
      q.color = col * rnd(0.7, 1.3)
      q.colorEnd = col * 0.15
      q.opacity = 0
      q.maxLife = rnd(0.5, 1.0)
      q.type = .spark
      q.stretch = 0.03
      q.drag = 1.0
      add(q)
    }
  }

  func ring(_ at: vec3, _ size: Float, _ col: vec3, _ life: Float) {
    var q = Particle()
    q.pos = at
    q.vel = vec3(0)
    q.size = size
    q.grow = 3.5
    q.color = col
    q.colorEnd = col * 0.1
    q.opacity = 0
    q.maxLife = life
    q.type = .ring
    q.drag = 0
    q.fadeIn = 0
    add(q)
  }

  func flash(_ at: vec3, _ size: Float, _ col: vec3, _ life: Float) {
    var q = Particle()
    q.pos = at
    q.vel = vec3(0)
    q.size = size
    q.grow = size * 1.5
    q.color = col
    q.colorEnd = col * 0.2
    q.opacity = 0
    q.maxLife = life
    q.type = .glow
    q.fadeIn = 0
    q.still = true
    add(q)
  }

  func bolt(_ a: vec3, _ b: vec3, _ col: vec3, _ jitter: Float, _ segs: Int) {
    var pts = [vec3](repeating: vec3(0), count: segs + 1)
    let ab = b - a
    let side = normalize(cross(ab, vec3(0, 1, 0)) + vec3(1e-4))
    let up = normalize(cross(side, ab))
    for i in 0...segs {
      let t = Float(i) / Float(segs)
      let env = sin(t * 3.14159)
      pts[i] = a + ab * t + (side * rnd(-1, 1) + up * rnd(-1, 1)) * jitter * env
    }
    for i in 0..<segs {
      let m = (pts[i] + pts[i + 1]) * 0.5
      let d = pts[i + 1] - pts[i]
      var q = Particle()
      q.pos = m
      q.vel = d * 0.5  // used only for stretching
      q.still = true
      q.stretch = 1
      q.size = 0.007
      q.color = col * rnd(0.9, 1.4)
      q.colorEnd = col * 0.5
      q.opacity = 0
      q.maxLife = rnd(0.06, 0.12)
      q.type = .spark
      q.fadeIn = 0
      add(q)
      var g = q
      g.size = 0.03
      g.color = col * 0.12
      add(g)
    }
  }
}
