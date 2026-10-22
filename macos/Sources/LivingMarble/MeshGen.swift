// Procedural geometry: Staunton pieces, fracture shards, weapons, board.
// (Fragment, PieceAssets, Library and BuildProgress live in Geometry.swift.)
import Foundation
import simd

enum MeshGen {
  // ---------------------------------------------------------------------------
  // tiny value noise used for fracture domain warping
  // ---------------------------------------------------------------------------
  @inline(__always) private static func hash3(_ x: Int, _ y: Int, _ z: Int) -> Float {
    var h = UInt32(truncatingIfNeeded: x) &* 374_761_393 &+ UInt32(truncatingIfNeeded: y) &* 668_265_263
      &+ UInt32(truncatingIfNeeded: z) &* 2_246_822_519
    h = (h ^ (h >> 13)) &* 1_274_126_177
    h ^= h >> 16
    return Float(h & 0xffffff) / Float(0xffffff) * 2 - 1
  }

  private static func vnoise(_ p: vec3) -> Float {
    let f = vfloor(p)
    var t = p - f
    t = t * t * (3 - 2 * t)
    let x = Int(f.x), y = Int(f.y), z = Int(f.z)
    let n000 = hash3(x, y, z), n100 = hash3(x + 1, y, z), n010 = hash3(x, y + 1, z), n110 = hash3(x + 1, y + 1, z)
    let n001 = hash3(x, y, z + 1), n101 = hash3(x + 1, y, z + 1), n011 = hash3(x, y + 1, z + 1)
    let n111 = hash3(x + 1, y + 1, z + 1)
    let a = mix(mix(n000, n100, t.x), mix(n010, n110, t.x), t.y)
    let b = mix(mix(n001, n101, t.x), mix(n011, n111, t.x), t.y)
    return mix(a, b, t.z)
  }

  @inline(__always) private static func warp(_ p: vec3, _ freq: Float, _ amp: Float) -> vec3 {
    p + amp * vec3(vnoise(p * freq + vec3(11.3)), vnoise(p * freq + vec3(-7.1, 3.3, 19.7)),
                   vnoise(p * freq + vec3(5.9, -13.4, 2.2)))
  }

  /// std::mt19937 + libc++'s uniform_real_distribution<float>(0, 1), so the
  /// fracture seeds (and therefore the shards) come out the same as the C++ build.
  private struct MT19937 {
    private var mt = [UInt32](repeating: 0, count: 624)
    private var index = 624
    init(_ seed: UInt32) {
      mt[0] = seed
      for i in 1..<624 { mt[i] = 1_812_433_253 &* (mt[i - 1] ^ (mt[i - 1] >> 30)) &+ UInt32(i) }
    }
    mutating func next() -> UInt32 {
      if index >= 624 {
        for i in 0..<624 {
          let y = (mt[i] & 0x8000_0000) | (mt[(i + 1) % 624] & 0x7fff_ffff)
          var v = mt[(i + 397) % 624] ^ (y >> 1)
          if y & 1 != 0 { v ^= 0x9908_b0df }
          mt[i] = v
        }
        index = 0
      }
      var y = mt[index]
      index += 1
      y ^= y >> 11
      y ^= (y << 7) & 0x9d2c_5680
      y ^= (y << 15) & 0xefc6_0000
      y ^= y >> 18
      return y
    }
    /// generate_canonical<float, 24>: one 32-bit draw divided by 2^32.
    mutating func uniform() -> Float { Float(next()) / 4_294_967_296.0 }
  }

  // ---------------------------------------------------------------------------
  // Profiles (radius, height). All units: one board square = 1.0
  // ---------------------------------------------------------------------------
  private static func baseOutline(_ R: Float) -> [vec2] {
    [vec2(0, 0),
     vec2(R * 0.94, 0),
     vec2(R * 0.98, 0.006),
     vec2(R, 0.022),
     vec2(R * 0.995, 0.040),
     vec2(R * 0.955, 0.054),
     vec2(R * 0.91, 0.060),
     vec2(R * 0.91, 0.068),
     vec2(R * 0.94, 0.078),
     vec2(R * 0.942, 0.088),
     vec2(R * 0.908, 0.098),
     vec2(R * 0.865, 0.104),
     vec2(R * 0.865, 0.110)]
  }

  private static func appendArc(_ o: inout [vec2], _ cy: Float, _ R: Float, _ a0deg: Float, _ a1deg: Float,
                                _ steps: Int) {
    for i in 0...steps {
      let a = radians(a0deg + (a1deg - a0deg) * Float(i) / Float(steps))
      o.append(vec2(R * cos(a), cy + R * sin(a)))
    }
    o[o.count - 1].x = 0
  }

  /// Builds an outline from flat (radius, height) pairs.
  private static func pts(_ xy: [Float]) -> [vec2] {
    stride(from: 0, to: xy.count, by: 2).map { vec2(xy[$0], xy[$0 + 1]) }
  }

  private static func makeProfile(_ o: [vec2]) -> Profile {
    var p = Profile()
    p.build(o, 3)
    return p
  }

  private static let pawnProfile: Profile = {
    var o = baseOutline(0.325)
    o += pts([0.258, 0.122, 0.226, 0.142, 0.196, 0.17, 0.170, 0.21, 0.150, 0.27,
              0.136, 0.34, 0.126, 0.41, 0.120, 0.462, 0.133, 0.472, 0.183, 0.484,
              0.196, 0.498, 0.190, 0.513, 0.148, 0.523, 0.106, 0.532, 0.092, 0.547])
    appendArc(&o, 0.692, 0.152, -52.0, 90.0, 14)
    return makeProfile(o)
  }()

  private static let rookProfile: Profile = {
    var o = baseOutline(0.37)
    o += pts([0.300, 0.124, 0.270, 0.145, 0.252, 0.175, 0.240, 0.22, 0.229, 0.40,
              0.219, 0.60, 0.215, 0.70, 0.222, 0.725, 0.250, 0.745, 0.263, 0.762,
              0.259, 0.778, 0.245, 0.788, 0.250, 0.795, 0.272, 0.808, 0.278, 0.83,
              0.278, 0.83, 0.280, 0.94, 0.280, 0.94, 0.274, 0.955, 0.262, 0.958,
              0.206, 0.958, 0.206, 0.958, 0.199, 0.95, 0.197, 0.905, 0.197, 0.905,
              0.180, 0.898, 0.0, 0.898])
    return makeProfile(o)
  }()

  private static let knightBaseProfile: Profile = {
    var o = baseOutline(0.37)
    o += pts([0.300, 0.124, 0.270, 0.145, 0.252, 0.175, 0.240, 0.21, 0.252, 0.224,
              0.274, 0.238, 0.279, 0.255, 0.271, 0.270, 0.242, 0.279, 0.222, 0.283,
              0.200, 0.290, 0.0, 0.290])
    return makeProfile(o)
  }()

  private static let bishopProfile: Profile = {
    var o = baseOutline(0.355)
    o += pts([0.288, 0.124, 0.254, 0.145, 0.215, 0.175, 0.181, 0.22, 0.156, 0.30,
              0.136, 0.42, 0.121, 0.55, 0.113, 0.66, 0.111, 0.70, 0.123, 0.712,
              0.186, 0.726, 0.199, 0.740, 0.191, 0.755, 0.136, 0.765, 0.119, 0.772,
              0.151, 0.780, 0.159, 0.792, 0.151, 0.802, 0.106, 0.810, 0.086, 0.825,
              0.101, 0.845, 0.136, 0.88, 0.159, 0.93, 0.166, 0.98, 0.159, 1.03,
              0.139, 1.08, 0.106, 1.125, 0.069, 1.16, 0.036, 1.18, 0.027, 1.19])
    appendArc(&o, 1.226, 0.043, -50.0, 90.0, 10)
    return makeProfile(o)
  }()

  private static let queenProfile: Profile = {
    var o = baseOutline(0.375)
    o += pts([0.300, 0.124, 0.265, 0.145, 0.226, 0.175, 0.191, 0.22, 0.166, 0.30,
              0.144, 0.44, 0.128, 0.60, 0.119, 0.74, 0.116, 0.80, 0.127, 0.812,
              0.201, 0.828, 0.215, 0.843, 0.207, 0.858, 0.146, 0.868, 0.126, 0.875,
              0.166, 0.884, 0.173, 0.896, 0.165, 0.906, 0.113, 0.914, 0.099, 0.935,
              0.109, 1.00, 0.131, 1.07, 0.161, 1.13, 0.191, 1.18, 0.209, 1.215,
              0.215, 1.235, 0.211, 1.25, 0.186, 1.255, 0.151, 1.262, 0.126, 1.275,
              0.101, 1.30, 0.076, 1.325, 0.051, 1.338, 0.033, 1.345])
    appendArc(&o, 1.386, 0.049, -48.0, 90.0, 10)
    return makeProfile(o)
  }()

  private static let kingProfile: Profile = {
    var o = baseOutline(0.385)
    o += pts([0.312, 0.124, 0.278, 0.145, 0.238, 0.175, 0.201, 0.22, 0.175, 0.31,
              0.153, 0.46, 0.135, 0.64, 0.125, 0.80, 0.121, 0.88, 0.132, 0.893,
              0.213, 0.91, 0.228, 0.926, 0.219, 0.942, 0.153, 0.952, 0.131, 0.958,
              0.176, 0.968, 0.183, 0.982, 0.173, 0.992, 0.119, 1.0, 0.103, 1.02,
              0.113, 1.09, 0.136, 1.16, 0.169, 1.22, 0.196, 1.265, 0.206, 1.29,
              0.206, 1.305, 0.197, 1.315, 0.171, 1.32, 0.151, 1.33, 0.126, 1.35,
              0.099, 1.375, 0.071, 1.395, 0.049, 1.405, 0.041, 1.41, 0.037, 1.42,
              0.041, 1.43, 0.035, 1.44, 0.0, 1.44])
    return makeProfile(o)
  }()

  // ---------------------------------------------------------------------------
  // Piece SDFs
  // ---------------------------------------------------------------------------
  private static func pawnSDF(_ p: vec3) -> Float { pawnProfile.eval(p) }

  private static func rookSDF(_ p: vec3) -> Float {
    var d = rookProfile.eval(p)
    if p.y > 0.85 {
      // three slots through the crown -> six merlons
      for k in 0..<3 {
        let q = SDF.rotY(p, radians(30.0 + 60.0 * Float(k)))
        let slot = SDF.roundBox(q - vec3(0, 1.0, 0), vec3(0.042, 0.087, 0.5), 0.01)
        d = SDF.smax(d, -slot, 0.006)
      }
    }
    return d
  }

  private static let bishopSlabN = normalize(vec3(0.28, 0.80, 0.52))
  private static let bishopFrontN = normalize(vec3(0.45, 0.0, 1.0))

  private static func bishopSDF(_ p: vec3) -> Float {
    var d = bishopProfile.eval(p)
    if p.y > 0.84 && p.y < 1.2 {
      // the mitre's slanted notch
      let slab = abs(dot(p - vec3(0.0, 1.035, 0.0), bishopSlabN)) - 0.0125
      let front = -(dot(p, bishopFrontN) + 0.035)
      let cut = max(slab, front)
      d = SDF.smax(d, -cut, 0.008)
    }
    return d
  }

  private static func queenSDF(_ p: vec3) -> Float {
    var d = queenProfile.eval(p)
    if p.y > 1.12 && p.y < 1.36 {
      let a = atan2(p.z, p.x)
      let step: Float = 6.2831853 / 8.0
      // scallops between the crown points
      let k = (a / step + 0.5).rounded(.down)
      let an = k * step
      let c = vec3(0.236 * cos(an), 1.268, 0.236 * sin(an))
      d = SDF.smax(d, -(length(p - c) - 0.054), 0.012)
      // small balls on the points
      let kp = ((a - step * 0.5) / step + 0.5).rounded(.down)
      let ap = kp * step + step * 0.5
      let b = vec3(0.204 * cos(ap), 1.246, 0.204 * sin(ap))
      d = SDF.smin(d, length(p - b) - 0.021, 0.008)
    }
    return d
  }

  private static func kingSDF(_ p: vec3) -> Float {
    var d = kingProfile.eval(p)
    if p.y > 1.38 {
      let v = SDF.roundBox(p - vec3(0, 1.535, 0), vec3(0.029, 0.098, 0.023), 0.009)
      let h = SDF.roundBox(p - vec3(0, 1.556, 0), vec3(0.086, 0.027, 0.023), 0.009)
      // flared ends (cross pattee)
      var c = min(v, h)
      for s in stride(from: -1, through: 1, by: 2) {
        c = SDF.smin(c, SDF.roundBox(p - vec3(Float(s) * 0.082, 1.556, 0), vec3(0.010, 0.036, 0.023), 0.008), 0.012)
      }
      c = SDF.smin(c, SDF.roundBox(p - vec3(0, 1.627, 0), vec3(0.037, 0.010, 0.023), 0.008), 0.012)
      d = SDF.smin(d, c, 0.018)
    }
    return d
  }

  @inline(__always) private static func narrowX(_ p: vec3, _ s: Float) -> vec3 { vec3(p.x / s, p.y, p.z) }

  private static func knightSDF(_ pIn: vec3) -> Float {
    let d = knightBaseProfile.eval(pIn)
    if pIn.y < 0.22 { return d }
    // horse head built in +Z-facing local space, scaled up slightly
    let S: Float = 1.08
    let p = pIn / S
    let sx: Float = 0.74
    let pn = narrowX(p, sx)
    let neck = SDF.roundCone(pn, vec3(0, 0.25, -0.03), vec3(0, 0.60, -0.075), 0.165, 0.118) * sx
    let neck2 = SDF.roundCone(pn, vec3(0, 0.60, -0.075), vec3(0, 0.79, -0.025), 0.118, 0.102) * sx
    let chest = SDF.ellipsoid(p - vec3(0, 0.39, 0.065), vec3(0.128, 0.13, 0.14))
    let shx: Float = 0.62
    let ph = narrowX(p, shx)
    let head = SDF.roundCone(ph, vec3(0, 0.83, 0.03), vec3(0, 0.655, 0.295), 0.122, 0.078) * shx
    let cheek = SDF.ellipsoid(p - vec3(0, 0.735, 0.045), vec3(0.083, 0.10, 0.105))
    let brow = SDF.ellipsoid(p - vec3(0, 0.83, 0.085), vec3(0.07, 0.06, 0.09))
    let muzzle = SDF.ellipsoid(p - vec3(0, 0.648, 0.302), vec3(0.060, 0.066, 0.068))
    var h = SDF.smin(neck, neck2, 0.06)
    h = SDF.smin(h, chest, 0.09)
    h = SDF.smin(h, head, 0.06)
    h = SDF.smin(h, cheek, 0.05)
    h = SDF.smin(h, brow, 0.04)
    h = SDF.smin(h, muzzle, 0.035)
    // ears
    for s in stride(from: -1, through: 1, by: 2) {
      let fs = Float(s)
      let ear = SDF.roundCone(p, vec3(fs * 0.038, 0.875, -0.012), vec3(fs * 0.05, 0.99, -0.05), 0.027, 0.006)
      h = SDF.smin(h, ear, 0.025)
    }
    // mane ridge running down the back of the neck
    let pm = narrowX(p, 0.45)
    let mane = SDF.capsule(pm, vec3(0, 0.33, -0.172), vec3(0, 0.66, -0.152), 0.05) * 0.45
    let mane2 = SDF.capsule(pm, vec3(0, 0.66, -0.152), vec3(0, 0.88, -0.055), 0.045) * 0.45
    var m = SDF.smin(mane, mane2, 0.03)
    // carved locks of hair
    let locks = sin((p.y * 0.85 - p.z * 0.55) * 70.0 + sin(p.x * 40.0) * 0.8) * 0.0026
    m += locks
    h = SDF.smin(h, m, 0.03)
    // eye sockets + eyeballs, nostrils, mouth
    for s in stride(from: -1, through: 1, by: 2) {
      let fs = Float(s)
      h = SDF.smax(h, -(length(p - vec3(fs * 0.062, 0.79, 0.112)) - 0.024), 0.012)
      h = SDF.smin(h, length(p - vec3(fs * 0.050, 0.79, 0.115)) - 0.017, 0.006)
      h = SDF.smax(h, -(length(p - vec3(fs * 0.030, 0.668, 0.368)) - 0.016), 0.008)
    }
    do {
      let q = SDF.rotX(p - vec3(0, 0.618, 0.325), radians(-32.0))
      let mouth = SDF.box(q, vec3(0.08, 0.0045, 0.05))
      h = SDF.smax(h, -mouth, 0.006)
    }
    h *= S
    return SDF.smin(d, h, 0.03)
  }

  // ---------------------------------------------------------------------------
  private static func swordBlade(_ p: vec3) -> Float {
    let y = p.y
    let t = clamp((y - 0.06) / 0.94, 0, 1)
    let tip = clamp((1 - y) / 0.17, 0, 1).squareRoot()
    let w = 0.047 * (1 - 0.22 * t) * tip + 0.0015
    let th = 0.014 * (1 - 0.35 * t) * (0.4 + 0.6 * tip) + 0.002
    let q = vec2(abs(p.x), abs(p.z))
    var dx = (q.x / w + q.y / th - 1) * (w * th / (w * w + th * th).squareRoot())
    // fuller (groove) on both faces
    let fuller = length(vec2(q.x / 0.5, q.y - th * 1.05)) * 0.5 - 0.0055
    if y < 0.7 { dx = max(dx, -fuller) }
    return max(dx, max(0.04 - y, y - 1))
  }

  private static func swordGuard(_ p: vec3) -> Float {
    var g = SDF.roundBox(p - vec3(0, 0.03, 0), vec3(0.15, 0.020, 0.030), 0.012)
    for s in stride(from: -1, through: 1, by: 2) {
      g = SDF.smin(g, length(p - vec3(Float(s) * 0.165, 0.045, 0)) - 0.026, 0.02)
    }
    g = SDF.smin(g, SDF.ellipsoid(p - vec3(0, 0.05, 0), vec3(0.04, 0.04, 0.036)), 0.02)
    return g
  }

  private static func swordGrip(_ p: vec3) -> Float {
    var g = SDF.capsule(p, vec3(0, -0.01, 0), vec3(0, -0.25, 0), 0.023)
    g += 0.0022 * sin(p.y * 140.0)
    return g
  }

  private static func swordPommel(_ p: vec3) -> Float {
    SDF.ellipsoid(p - vec3(0, -0.29, 0), vec3(0.04, 0.045, 0.04))
  }

  private static func swordSDF(_ p: vec3) -> Float {
    var d = min(swordBlade(p), swordGuard(p))
    d = SDF.smin(d, swordGrip(p), 0.01)
    d = SDF.smin(d, swordPommel(p), 0.012)
    return d
  }

  /// 0 steel, 1 gold, 2 leather.
  private static func swordMaterial(_ p: vec3) -> Float {
    let b = swordBlade(p), g = swordGuard(p), gr = swordGrip(p), po = swordPommel(p)
    let m = min(min(b, g), min(gr, po))
    if m == b { return 0 }
    if m == gr { return 2 }
    return 1
  }

  // ---------------------------------------------------------------------------
  private static func volumeCentroid(_ m: MeshData) -> (vol: Float, c: vec3) {
    var vol: Float = 0
    var c = vec3(0)
    m.v.withUnsafeBufferPointer { vp in
      m.idx.withUnsafeBufferPointer { ip in
        var i = 0
        while i + 2 < ip.count {
          let a = vp[Int(ip[i])].pos, b = vp[Int(ip[i + 1])].pos, d = vp[Int(ip[i + 2])].pos
          let v = dot(a, cross(b, d)) / 6
          vol += v
          c += v * (a + b + d) / 4
          i += 3
        }
      }
    }
    var ok = abs(vol) > 1e-9
    if ok {
      c /= vol
      // a nearly flat fragment can put the integral's centroid far outside it
      var lo = vec3(1e9), hi = vec3(-1e9)
      for x in m.v {
        lo = vmin(lo, x.pos)
        hi = vmax(hi, x.pos)
      }
      ok = all(c .>= lo - 0.01) && all(c .<= hi + 0.01)
    }
    if !ok {
      c = vec3(0)
      for x in m.v { c += x.pos }
      if !m.v.isEmpty { c /= Float(m.v.count) }
    }
    return (abs(vol), c)
  }

  private static func finishFragment(_ meshIn: MeshData) -> Fragment {
    var mesh = meshIn
    var f = Fragment()
    (f.volume, f.centroid) = volumeCentroid(mesh)
    mesh.translate(-f.centroid)
    f.radius = 0
    for x in mesh.v { f.radius = max(f.radius, length(x.pos)) }
    // hull contact points: extreme vertices in 26 directions
    mesh.v.withUnsafeBufferPointer { vp in
      for dx in -1...1 {
        for dy in -1...1 {
          for dz in -1...1 {
            if dx == 0 && dy == 0 && dz == 0 { continue }
            let dir = normalize(vec3(Float(dx), Float(dy), Float(dz)))
            var best: Float = -1e9
            var bp = vec3(0)
            for x in vp {
              let s = dot(x.pos, dir)
              if s > best {
                best = s
                bp = x.pos
              }
            }
            var dup = false
            for h in f.hull where length(h - bp) < 1e-4 { dup = true }
            if !dup { f.hull.append(bp) }
          }
        }
      }
    }
    f.mesh = mesh
    return f
  }

  // ---------------------------------------------------------------------------
  static func pieceHeight(_ kind: Int) -> Float {
    switch kind {
    case Kind.pawn: return 0.844
    case Kind.knight: return 1.08
    case Kind.bishop: return 1.269
    case Kind.rook: return 0.958
    case Kind.queen: return 1.435
    default: return 1.64
    }
  }

  static func pieceRadius(_ kind: Int) -> Float {
    switch kind {
    case Kind.pawn: return 0.325
    case Kind.knight: return 0.37
    case Kind.bishop: return 0.355
    case Kind.rook: return 0.37
    case Kind.queen: return 0.375
    default: return 0.385
    }
  }

  static func pieceField(_ kind: Int) -> SDFFn {
    switch kind {
    case Kind.pawn: return { pawnSDF($0) }
    case Kind.knight: return { knightSDF($0) }
    case Kind.bishop: return { bishopSDF($0) }
    case Kind.rook: return { rookSDF($0) }
    case Kind.queen: return { queenSDF($0) }
    default: return { kingSDF($0) }
    }
  }

  private static func pieceBounds(_ kind: Int) -> (lo: vec3, hi: vec3) {
    let r = pieceRadius(kind) + 0.01
    var lo = vec3(-r, -0.005, -r)
    var hi = vec3(r, pieceHeight(kind) + 0.01, r)
    if kind == Kind.knight {
      lo.z = -0.39
      hi.z = 0.44
      hi.y = 1.10
    }
    return (lo, hi)
  }

  /// Breaks a solid inside [lo, hi] into Voronoi cells (with domain-warped borders
  /// so the cracks aren't perfectly planar) and meshes each cell as its own fragment.
  /// 'skin' (optional) fills each vertex's material while it is still in piece space
  /// (shared with the medieval set).
  static func voronoiShatter(_ F: @escaping SDFFn, _ lo: vec3, _ hi: vec3, _ nSeeds: Int, _ seedVal: UInt32,
                             _ cell: Float, skin: ((inout Vertex) -> Void)? = nil) -> [Fragment] {
    var rng = MT19937(seedVal)
    var seeds: [vec3] = []
    var minD: Float = 0.10
    var tries = 0
    while tries < 20000 && seeds.count < nSeeds {
      defer { tries += 1 }
      let ux = rng.uniform(), uy = rng.uniform(), uz = rng.uniform()
      let p = vec3(mix(lo.x, hi.x, ux), mix(lo.y, hi.y, uy), mix(lo.z, hi.z, uz))
      if F(p) > -0.02 { continue }
      var ok = true
      for s in seeds where length(s - p) < minD { ok = false }
      if ok { seeds.append(p) }
      if tries % 4000 == 3999 { minD *= 0.8 }
    }
    let n = seeds.count
    if n == 0 { return [] }  // nothing thick enough to break into chunks
    // bounding boxes of each cell
    var bl = [vec3](repeating: vec3(1e9), count: n), bh = [vec3](repeating: vec3(-1e9), count: n)
    let g: Float = 0.02
    var z = lo.z
    while z <= hi.z {
      var y = lo.y
      while y <= hi.y {
        var x = lo.x
        while x <= hi.x {
          let p = vec3(x, y, z)
          x += g
          if F(p) > g { continue }
          let pw = warp(p, 9.0, 0.03)
          var best = 0
          var bd: Float = 1e9
          for i in 0..<n {
            let dd = length(pw - seeds[i])
            if dd < bd {
              bd = dd
              best = i
            }
          }
          bl[best] = vmin(bl[best], p)
          bh[best] = vmax(bh[best], p)
        }
        y += g
      }
      z += g
    }
    var out: [Fragment] = []
    let gap: Float = 0.0022
    // bisector planes of cell i: (normal, midpoint) per neighbour, stored contiguously
    let planes = UnsafeMutablePointer<vec3>.allocate(capacity: max(1, 2 * n))
    defer { planes.deallocate() }
    for i in 0..<n {
      if bl[i].x > bh[i].x { continue }
      var np = 0
      for j in 0..<n where j != i {
        planes[2 * np] = normalize(seeds[j] - seeds[i])
        planes[2 * np + 1] = (seeds[i] + seeds[j]) * 0.5
        np += 1
      }
      let pl = UnsafePointer(planes)
      let frag: SDFFn = { p in
        let pw = warp(p, 9.0, 0.03)
        // distance to the Voronoi cell (max over the bisector half-spaces)
        var d: Float = -1e9
        for j in 0..<np { d = max(d, dot(pw - pl[2 * j + 1], pl[2 * j])) }
        return max(F(p), d + gap)
      }
      var o = MeshOptions()
      o.cell = cell
      o.bmin = vmax(bl[i] - vec3(0.04), lo - vec3(0.01))
      o.bmax = vmin(bh[i] + vec3(0.04), hi + vec3(0.01))
      o.computeAO = false
      o.newtonSteps = 2
      // fresh fracture faces (inside the original surface) get aux = 1
      o.aux = { p in clamp((-F(p) - 0.0015) / 0.004, 0, 1) }
      o.skin = skin
      let m = polygonize(frag, o)
      if m.idx.count < 60 { continue }
      let fr = finishFragment(m)
      if fr.volume < 2e-5 { continue }
      out.append(fr)
    }
    return out
  }

  /// Cuts a solid inside [lo, hi] in two along the plane through p0 with normal n.
  static func sliceHalves(_ F: @escaping SDFFn, _ lo: vec3, _ hi: vec3, _ n: vec3, _ p0: vec3, _ cell: Float,
                          skin: ((inout Vertex) -> Void)? = nil) -> [Fragment] {
    var out: [Fragment] = []
    for s in stride(from: -1, through: 1, by: 2) {
      let fs = Float(s)
      let frag: SDFFn = { p in
        let pl = dot(p - p0, n) * fs
        return max(F(p), pl + 0.0018)
      }
      var o = MeshOptions()
      o.cell = cell
      o.bmin = lo
      o.bmax = hi
      o.computeAO = true
      o.aoRadius = 0.12
      o.aux = { p in clamp((-F(p) - 0.0015) / 0.004, 0, 1) }
      o.skin = skin
      let m = polygonize(frag, o)
      out.append(finishFragment(m))
    }
    return out
  }

  /// Rounded slab: E = half extent, T = thickness. y = 0 is the playing surface.
  private static func buildBoard(_ E: Float, _ T: Float) -> MeshData {
    var m = MeshData()
    let rc: Float = 0.10, rb: Float = 0.045, rbb: Float = 0.02
    // perimeter of rounded rectangle (counter-clockwise seen from above: +x -> +z)
    var per: [(p: vec2, n: vec2)] = []
    let cseg = 10
    let corners = [vec2(E - rc, E - rc), vec2(-(E - rc), E - rc), vec2(-(E - rc), -(E - rc)), vec2(E - rc, -(E - rc))]
    let startA: [Float] = [0, 90, 180, 270]
    for c in 0..<4 {
      for i in 0...cseg {
        let a = radians(startA[c] + 90.0 * Float(i) / Float(cseg))
        let n = vec2(cos(a), sin(a))
        per.append((corners[c] + n * rc, n))
      }
    }
    // profile: (outward offset, y, normal.out, normal.y)
    var prof: [(off: Float, y: Float, no: Float, ny: Float)] = []
    let bseg = 8
    for i in 0...bseg {
      let a = radians(90.0 - 90.0 * Float(i) / Float(bseg))
      prof.append((-rb + rb * cos(a), -rb + rb * sin(a), cos(a), sin(a)))
    }
    prof.append((0, -T + rbb, 1, 0))
    for i in 1...4 {
      let a = radians(-90.0 * Float(i) / 4)
      prof.append((-rbb + rbb * cos(a), -T + rbb + rbb * sin(a), cos(a), sin(a)))
    }
    // top cap
    var c = Vertex(pos: vec3(0, 0, 0), nrm: vec3(0, 1, 0), ao: 1, aux: 0)
    m.v.append(c)
    for q in per {
      var v = c
      let pp = q.p - q.n * rb
      v.pos = vec3(pp.x, 0, pp.y)
      m.v.append(v)
    }
    let np = per.count
    for i in 0..<np {
      let a = UInt32(1 + i), b = UInt32(1 + (i + 1) % np)
      m.idx += [0, b, a]
    }
    // sides
    let base = UInt32(m.v.count)
    for q in per {
      for r in prof {
        let pp = q.p + q.n * r.off
        m.v.append(Vertex(pos: vec3(pp.x, r.y, pp.y), nrm: normalize(vec3(q.n.x * r.no, r.ny, q.n.y * r.no)),
                          ao: 1, aux: 0))
      }
    }
    let nr = prof.count
    for i in 0..<np {
      let i2 = (i + 1) % np
      for j in 0..<(nr - 1) {
        let a = base + UInt32(i * nr + j), b = base + UInt32(i2 * nr + j)
        let cc = base + UInt32(i2 * nr + j + 1), d = base + UInt32(i * nr + j + 1)
        m.idx += [a, b, cc, a, cc, d]
      }
    }
    // bottom cap (the board floats, so its underside is on show)
    let bc = UInt32(m.v.count)
    c.pos = vec3(0, -T, 0)
    c.nrm = vec3(0, -1, 0)
    m.v.append(c)
    for q in per {
      var v = c
      let pp = q.p - q.n * rbb
      v.pos = vec3(pp.x, -T, pp.y)
      m.v.append(v)
    }
    for i in 0..<np {
      let a = bc + 1 + UInt32(i), b = bc + 1 + UInt32((i + 1) % np)
      m.idx += [bc, a, b]
    }
    // fix winding against normals
    var i = 0
    while i < m.idx.count {
      let p0 = m.v[Int(m.idx[i])].pos, p1 = m.v[Int(m.idx[i + 1])].pos, p2 = m.v[Int(m.idx[i + 2])].pos
      let fn = cross(p1 - p0, p2 - p0)
      let ns = m.v[Int(m.idx[i])].nrm + m.v[Int(m.idx[i + 1])].nrm + m.v[Int(m.idx[i + 2])].nrm
      if dot(fn, ns) < 0 { m.idx.swapAt(i + 1, i + 2) }
      i += 3
    }
    m.computeBounds()
    return m
  }

  // ---------------------------------------------------------------------------
  /// quality: 0 = fast, 1 = normal, 2 = ultra, 3 = extreme
  static func buildLibrary(_ lib: Library, quality qIn: Int, progress prog: BuildProgress?) {
    let quality = max(0, min(3, qIn))
    let c0: [Float] = [0.0125, 0.0095, 0.0074, 0.0052]  // lod0 cell size
    let c1: [Float] = [0.022, 0.018, 0.015, 0.011]      // lod1
    let cs: [Float] = [0.016, 0.0125, 0.0105, 0.008]    // shards / halves
    let seeds = [10, 13, 13, 13, 15, 16]
    let total = Kind.count * 4 + 3
    if let prog = prog {
      prog.total = total
      prog.done = 0
    }
    func tick(_ s: String) {
      if let prog = prog {
        prog.stage = s
        prog.step()
      }
    }
    for k in 0..<Kind.count {
      var A = PieceAssets()
      A.height = pieceHeight(k)
      A.radius = pieceRadius(k)
      let F = pieceField(k)
      var o = MeshOptions()
      (o.bmin, o.bmax) = pieceBounds(k)
      o.cell = c0[quality]
      o.aoRadius = 0.12
      A.lod0 = polygonize(F, o)
      tick("Sculpting marble")
      o.cell = c1[quality]
      o.newtonSteps = 3
      A.lod1 = polygonize(F, o)
      tick("Sculpting marble")
      let (lo, hi) = pieceBounds(k)
      A.shards = voronoiShatter(F, lo, hi, seeds[k], 1234 + 77 * UInt32(k), cs[quality])
      tick("Planning fractures")
      A.sliceN = normalize(vec3(0.78, 0.62, 0.0))
      A.slicePoint = vec3(0, A.height * 0.52, 0)
      A.halves = sliceHalves(F, lo, hi, A.sliceN, A.slicePoint, cs[quality])
      tick("Planning fractures")
      lib.piece[k] = A
    }
    do {
      var o = MeshOptions()
      o.cell = quality == 0 ? 0.0055 : (quality < 3 ? 0.0042 : 0.0032)
      o.bmin = vec3(-0.22, -0.36, -0.06)
      o.bmax = vec3(0.22, 1.02, 0.06)
      o.aoRadius = 0.04
      o.aux = { swordMaterial($0) }
      lib.sword = polygonize({ swordSDF($0) }, o)
      tick("Forging steel")
    }
    lib.cannonball = makeUVSphere(0.085, 40, 20)
    tick("Casting iron")
    lib.board = buildBoard(lib.boardHalf, lib.boardThickness)
    tick("Carving the board")
    prog?.finished = true
  }
}
