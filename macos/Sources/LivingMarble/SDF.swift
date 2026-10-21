// Signed-distance-field modelling toolkit + a sparse Surface-Nets mesher.
// All chess pieces, their fracture fragments and the sword are sculpted
// procedurally from SDFs at startup - no model files are needed.
// (SDFFn, Vertex and MeshData live in Geometry.swift.)
import Foundation
import simd

enum SDF {
  // ---- primitives (Inigo Quilez style) ----------------------------------------
  @inline(__always) static func sphere(_ p: vec3, _ r: Float) -> Float { length(p) - r }

  @inline(__always) static func box(_ p: vec3, _ b: vec3) -> Float {
    let q = vabs(p) - b
    return length(vmax(q, vec3(0))) + min(max(q.x, max(q.y, q.z)), 0)
  }

  @inline(__always) static func roundBox(_ p: vec3, _ b: vec3, _ r: Float) -> Float {
    box(p, b - vec3(r)) - r
  }

  /// Torus in the xz plane.
  @inline(__always) static func torus(_ p: vec3, _ R: Float, _ r: Float) -> Float {
    let q = vec2((p.x * p.x + p.z * p.z).squareRoot() - R, p.y)
    return length(q) - r
  }

  @inline(__always) static func capsule(_ p: vec3, _ a: vec3, _ b: vec3, _ r: Float) -> Float {
    let pa = p - a, ba = b - a
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0, 1)
    return length(pa - ba * h) - r
  }

  static func roundCone(_ p: vec3, _ a: vec3, _ b: vec3, _ r1: Float, _ r2: Float) -> Float {
    let ba = b - a
    let l2 = dot(ba, ba)
    let rr = r1 - r2
    let a2 = l2 - rr * rr
    let il2 = 1 / l2
    let pa = p - a
    let y = dot(pa, ba)
    let z = y - l2
    let xv = pa * l2 - ba * y
    let x2 = dot(xv, xv)
    let y2 = y * y * l2
    let z2 = z * z * l2
    let k = (rr > 0 ? 1 : (rr < 0 ? -1 : 0)) * rr * rr * x2
    let sz: Float = z > 0 ? 1 : (z < 0 ? -1 : 0)
    let sy: Float = y > 0 ? 1 : (y < 0 ? -1 : 0)
    if sz * a2 * z2 > k { return (x2 + z2).squareRoot() * il2 - r2 }
    if sy * a2 * y2 < k { return (x2 + y2).squareRoot() * il2 - r1 }
    return ((x2 * a2 * il2).squareRoot() + y * rr) * il2 - r1
  }

  @inline(__always) static func ellipsoid(_ p: vec3, _ r: vec3) -> Float {
    let k0 = length(p / r)
    let k1 = length(p / (r * r))
    if k1 < 1e-9 { return -min(r.x, min(r.y, r.z)) }
    return k0 * (k0 - 1) / k1
  }

  /// Capped cylinder around the y axis.
  @inline(__always) static func cylinder(_ p: vec3, _ h: Float, _ r: Float) -> Float {
    let d = vabs(vec2((p.x * p.x + p.z * p.z).squareRoot(), p.y)) - vec2(r, h)
    return min(max(d.x, d.y), 0) + length(vmax(d, vec2(0)))
  }

  @inline(__always) static func smin(_ a: Float, _ b: Float, _ k: Float) -> Float {
    if k <= 0 { return min(a, b) }
    let h = max(k - abs(a - b), 0) / k
    return min(a, b) - h * h * k * 0.25
  }

  @inline(__always) static func smax(_ a: Float, _ b: Float, _ k: Float) -> Float { -smin(-a, -b, k) }

  @inline(__always) static func rotY(_ p: vec3, _ a: Float) -> vec3 {
    let c = cos(a), s = sin(a)
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z)
  }

  @inline(__always) static func rotX(_ p: vec3, _ a: Float) -> vec3 {
    let c = cos(a), s = sin(a)
    return vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z)
  }

  @inline(__always) static func rotZ(_ p: vec3, _ a: Float) -> vec3 {
    let c = cos(a), s = sin(a)
    return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z)
  }

  /// Central-difference gradient.
  @inline(__always) static func gradient(_ f: SDFFn, _ p: vec3, _ e: Float) -> vec3 {
    vec3(f(p + vec3(e, 0, 0)) - f(p - vec3(e, 0, 0)),
         f(p + vec3(0, e, 0)) - f(p - vec3(0, e, 0)),
         f(p + vec3(0, 0, e)) - f(p - vec3(0, 0, e))) / (2 * e)
  }
}

// ---------------------------------------------------------------------------
/// Solid of revolution around +Y defined by an outline in (radius, height)
/// that starts and ends on the axis. Corners are rounded by Chaikin subdivision.
/// Points tagged 'sharp' (repeat a point twice) stay crisper.
struct Profile {
  struct Edge {
    var a: vec2, e: vec2
    var inv: Float, ymin: Float, ymax: Float
    var outline: Bool
  }

  /// Owns the contiguous edge list read by dist2D (shared by copies of the profile).
  private final class EdgeStore {
    let buf: UnsafeMutableBufferPointer<Edge>
    init(_ edges: [Edge]) {
      buf = .allocate(capacity: max(1, edges.count))
      _ = buf.initialize(from: edges)
    }
    deinit { buf.deallocate() }
  }

  private(set) var pts: [vec2] = []
  private(set) var edges: [Edge] = []
  var yMin: Float = 0, yMax: Float = 0, rMax: Float = 0
  private var store: EdgeStore?
  // raw view of store.buf so the hot loop touches no reference counts
  private var ep: UnsafePointer<Edge>?
  private var ne = 0

  mutating func build(_ outline: [vec2], _ chaikinIterations: Int = 3) {
    var p = outline
    for _ in 0..<chaikinIterations {
      var q: [vec2] = []
      q.reserveCapacity(p.count * 2)
      q.append(p[0])
      var i = 0
      while i + 1 < p.count {
        let a = p[i], b = p[i + 1]
        if i > 0 { q.append(a * 0.75 + b * 0.25) }
        if i + 2 < p.count { q.append(a * 0.25 + b * 0.75) }
        i += 1
      }
      q.append(p[p.count - 1])
      p = q
    }
    pts.removeAll()
    for v in p where pts.isEmpty || length(v - pts[pts.count - 1]) > 1e-6 { pts.append(v) }
    pts[0].x = 0
    pts[pts.count - 1].x = 0
    edges.removeAll()
    yMin = 1e9
    yMax = -1e9
    rMax = 0
    let n = pts.count
    for i in 0..<n {
      let a = pts[i], b = pts[(i + 1) % n]
      let e = b - a
      let l2 = dot(e, e)
      if l2 < 1e-14 { continue }
      // the last edge closes along the axis
      edges.append(Edge(a: a, e: e, inv: 1 / l2, ymin: min(a.y, b.y), ymax: max(a.y, b.y), outline: i + 1 < n))
      yMin = min(yMin, a.y)
      yMax = max(yMax, a.y)
      rMax = max(rMax, a.x)
    }
    let s = EdgeStore(edges)
    store = s
    ep = UnsafePointer(s.buf.baseAddress)
    ne = edges.count
  }

  func dist2D(_ p: vec2) -> Float {
    var best2: Float = 1e30, best: Float = 1e15
    var inside = false
    guard let ep = ep else { return best }
    for k in 0..<ne {
      let E = ep[k]
      // even-odd crossing test (all edges) for the sign
      let ay = E.a.y, by = E.a.y + E.e.y
      if (ay > p.y) != (by > p.y) {
        let t = (p.y - ay) / E.e.y
        let xi = E.a.x + t * E.e.x
        if p.x < xi { inside.toggle() }
      }
      // distance to the outline (the closing axis edge doesn't count)
      if !E.outline { continue }
      if p.y < E.ymin - best || p.y > E.ymax + best { continue }
      let w = p - E.a
      let h = clamp(dot(w, E.e) * E.inv, 0, 1)
      let d = w - E.e * h
      let dd = dot(d, d)
      if dd < best2 {
        best2 = dd
        best = dd.squareRoot()
      }
    }
    return inside ? -best : best
  }

  @inline(__always) func eval(_ p: vec3) -> Float {
    dist2D(vec2((p.x * p.x + p.z * p.z).squareRoot(), p.y))
  }
}

// ---- meshing ----------------------------------------------------------------
struct MeshOptions {
  var cell: Float = 0.01
  var bmin = vec3(-1), bmax = vec3(1)
  var computeAO = true
  var aoRadius: Float = 0.12
  var newtonSteps = 2
  var aux: SDFFn? = nil          // optional per-vertex aux attribute
  var normalField: SDFFn? = nil  // optional field used for normals/projection (defaults to f)
}

/// Surface nets over a grid with 4^3 blocks: the field is sampled on a coarse
/// lattice first and blocks that are clearly far from the surface are filled
/// with a constant instead of being evaluated.
func polygonize(_ f: SDFFn, _ o: MeshOptions) -> MeshData {
  let h = o.cell
  let B = 4
  let b0 = o.bmin - vec3(2 * h)
  let b1 = o.bmax + vec3(2 * h)
  func dimOf(_ lo: Float, _ hi: Float) -> Int {
    let n = Int(((hi - lo) / h).rounded(.up)) + 1
    return ((n - 1 + B - 1) / B) * B + 1
  }
  let nx = dimOf(b0.x, b1.x), ny = dimOf(b0.y, b1.y), nz = dimOf(b0.z, b1.z)
  let sxy = nx * ny
  @inline(__always) func P(_ x: Int, _ y: Int, _ z: Int) -> vec3 { b0 + vec3(Float(x), Float(y), Float(z)) * h }

  let F = UnsafeMutablePointer<Float>.allocate(capacity: nx * ny * nz)
  F.initialize(repeating: 1, count: nx * ny * nz)
  defer { F.deallocate() }
  let cx = (nx - 1) / B, cy = (ny - 1) / B, cz = (nz - 1) / B
  let C = UnsafeMutablePointer<Float>.allocate(capacity: (cx + 1) * (cy + 1) * (cz + 1))
  defer { C.deallocate() }
  @inline(__always) func CI(_ x: Int, _ y: Int, _ z: Int) -> Int { (z * (cy + 1) + y) * (cx + 1) + x }
  parallelFor(cz + 1) { k in
    for j in 0...cy {
      for i in 0...cx { C[CI(i, j, k)] = f(P(i * B, j * B, k * B)) }
    }
  }
  let thr: Float = 0.8660254 * Float(B) * h * 1.6
  parallelFor(cz) { bz in
    for by in 0..<cy {
      for bx in 0..<cx {
        var far = true
        var s: Float = 1
        for q in 0..<8 {
          let c = C[CI(bx + (q & 1), by + ((q >> 1) & 1), bz + ((q >> 2) & 1))]
          let sg: Float = c < 0 ? -1 : 1
          if q == 0 { s = sg }
          if sg != s || abs(c) < thr { far = false }
        }
        let x0 = bx * B, y0 = by * B, z0 = bz * B
        let x1 = (bx == cx - 1) ? x0 + B : x0 + B - 1
        let y1 = (by == cy - 1) ? y0 + B : y0 + B - 1
        let z1 = (bz == cz - 1) ? z0 + B : z0 + B - 1
        for z in z0...z1 {
          for y in y0...y1 {
            let row = F + (z * sxy + y * nx)
            if far {
              for x in x0...x1 { row[x] = s * thr }
            } else {
              for x in x0...x1 { row[x] = f(P(x, y, z)) }
            }
          }
        }
      }
    }
  }

  // --- surface nets: one vertex per sign-changing cell ---
  let mx = nx - 1, my = ny - 1, mz = nz - 1
  let cellIdx = UnsafeMutablePointer<Int32>.allocate(capacity: mx * my * mz)
  cellIdx.initialize(repeating: -1, count: mx * my * mz)
  defer { cellIdx.deallocate() }
  @inline(__always) func CIdx(_ x: Int, _ y: Int, _ z: Int) -> Int { (z * my + y) * mx + x }
  // corner q of a cell sits at offset (q & 1, (q >> 1) & 1, (q >> 2) & 1)
  let cornerOff = UnsafeMutablePointer<Int>.allocate(capacity: 8)
  defer { cornerOff.deallocate() }
  for q in 0..<8 { cornerOff[q] = (q & 1) + ((q >> 1) & 1) * nx + ((q >> 2) & 1) * sxy }
  let edgeAB = UnsafeMutablePointer<Int>.allocate(capacity: 24)
  defer { edgeAB.deallocate() }
  edgeAB.initialize(from: [0, 2, 4, 6, 0, 1, 4, 5, 0, 1, 2, 3,
                           1, 3, 5, 7, 2, 3, 6, 7, 4, 5, 6, 7], count: 24)
  let v = UnsafeMutablePointer<Float>.allocate(capacity: 8)
  defer { v.deallocate() }
  var verts: [Vertex] = []
  for z in 0..<mz {
    for y in 0..<my {
      for x in 0..<mx {
        let base = F + (z * sxy + y * nx + x)
        var mask = 0
        for q in 0..<8 {
          let fv = base[cornerOff[q]]
          v[q] = fv
          if fv < 0 { mask |= 1 << q }
        }
        if mask == 0 || mask == 255 { continue }
        var acc = vec3(0)
        var cnt = 0
        for e in 0..<12 {
          let a = edgeAB[e], b = edgeAB[e + 12]
          if ((mask >> a) & 1) == ((mask >> b) & 1) { continue }
          let t = v[a] / (v[a] - v[b])
          let pa = vec3(Float(a & 1), Float((a >> 1) & 1), Float((a >> 2) & 1))
          let pb = vec3(Float(b & 1), Float((b >> 1) & 1), Float((b >> 2) & 1))
          acc += pa + (pb - pa) * t
          cnt += 1
        }
        cellIdx[CIdx(x, y, z)] = Int32(verts.count)
        verts.append(Vertex(pos: P(x, y, z) + (acc / Float(cnt)) * h, nrm: vec3(0, 1, 0), ao: 1, aux: 0))
      }
    }
  }

  // --- project vertices onto the surface, compute normals / AO / aux ---
  // (nf: the field used for projection and normals, defaults to f)
  let aux = o.aux
  let ge = h * 0.25
  let nsteps = o.newtonSteps, computeAO = o.computeAO, aoRadius = o.aoRadius
  let aoDist = SIMD8<Float>(0.05, 0.14, 0.3, 0.55, 1.0, 0, 0, 0)
  let aoW = SIMD8<Float>(1.0, 0.85, 0.65, 0.45, 0.3, 0, 0, 0)
  let nv = verts.count
  func project(_ nf: SDFFn) {
    verts.withUnsafeMutableBufferPointer { vb in
      guard let vp = vb.baseAddress else { return }
      parallelFor((nv + 255) / 256) { blk in
        let i0 = blk * 256, i1 = min(nv, i0 + 256)
        for i in i0..<i1 {
          var vt = vp[i]
          var p = vt.pos
          for _ in 0..<nsteps {
            let d = nf(p)
            let g = SDF.gradient(nf, p, ge)
            let g2 = dot(g, g)
            if g2 < 1e-8 { break }
            var step = g * (d / g2)
            let sl = length(step)
            if sl > h * 0.75 { step *= h * 0.75 / sl }
            p -= step
          }
          if length(p - vt.pos) < h * 1.5 { vt.pos = p }
          let g = SDF.gradient(nf, vt.pos, ge)
          let gl = length(g)
          vt.nrm = gl > 1e-8 ? g / gl : vec3(0, 1, 0)
          if computeAO {
            var occ: Float = 0, wsum: Float = 0
            for s in 0..<5 {
              let dd = aoDist[s] * aoRadius
              let fv = f(vt.pos + vt.nrm * dd)
              occ += aoW[s] * max(0, dd - fv) / dd
              wsum += aoW[s]
            }
            vt.ao = clamp(1 - 1.35 * occ / wsum, 0, 1)
          }
          if let aux = aux { vt.aux = aux(vt.pos) }
          vp[i] = vt
        }
      }
    }
  }
  if let nf = o.normalField { project(nf) } else { project(f) }

  // --- quads across sign-changing grid edges ---
  var idx: [UInt32] = []
  idx.reserveCapacity(nv * 6 + 64)
  verts.withUnsafeBufferPointer { vb in
    guard let vp = vb.baseAddress else { return }
    func emitQuad(_ a: Int32, _ b: Int32, _ c: Int32, _ d: Int32) {
      if a < 0 || b < 0 || c < 0 || d < 0 { return }
      let pa = vp[Int(a)].pos, pb = vp[Int(b)].pos, pc = vp[Int(c)].pos, pd = vp[Int(d)].pos
      let nsum = vp[Int(a)].nrm + vp[Int(b)].nrm + vp[Int(c)].nrm + vp[Int(d)].nrm
      // split along the shorter diagonal
      let diagAC = dot(pa - pc, pa - pc) <= dot(pb - pd, pb - pd)
      var t: (UInt32, UInt32, UInt32, UInt32, UInt32, UInt32)
      if diagAC {
        t = (UInt32(a), UInt32(b), UInt32(c), UInt32(a), UInt32(c), UInt32(d))
      } else {
        t = (UInt32(a), UInt32(b), UInt32(d), UInt32(b), UInt32(c), UInt32(d))
      }
      // orient each triangle along the averaged vertex normals
      let p0 = vp[Int(t.0)].pos, p1 = vp[Int(t.1)].pos, p2 = vp[Int(t.2)].pos
      if dot(cross(p1 - p0, p2 - p0), nsum) < 0 { swap(&t.1, &t.2) }
      let q0 = vp[Int(t.3)].pos, q1 = vp[Int(t.4)].pos, q2 = vp[Int(t.5)].pos
      if dot(cross(q1 - q0, q2 - q0), nsum) < 0 { swap(&t.4, &t.5) }
      idx.append(t.0)
      idx.append(t.1)
      idx.append(t.2)
      idx.append(t.3)
      idx.append(t.4)
      idx.append(t.5)
    }
    for z in 0..<nz {
      for y in 0..<ny {
        for x in 0..<nx {
          let i = z * sxy + y * nx + x
          let in0 = F[i] < 0
          if x < nx - 1 && in0 != (F[i + 1] < 0) && y >= 1 && z >= 1 && y < ny - 1 && z < nz - 1 {
            emitQuad(cellIdx[CIdx(x, y - 1, z - 1)], cellIdx[CIdx(x, y, z - 1)], cellIdx[CIdx(x, y, z)],
                     cellIdx[CIdx(x, y - 1, z)])
          }
          if y < ny - 1 && in0 != (F[i + nx] < 0) && x >= 1 && z >= 1 && x < nx - 1 && z < nz - 1 {
            emitQuad(cellIdx[CIdx(x - 1, y, z - 1)], cellIdx[CIdx(x, y, z - 1)], cellIdx[CIdx(x, y, z)],
                     cellIdx[CIdx(x - 1, y, z)])
          }
          if z < nz - 1 && in0 != (F[i + sxy] < 0) && x >= 1 && y >= 1 && x < nx - 1 && y < ny - 1 {
            emitQuad(cellIdx[CIdx(x - 1, y - 1, z)], cellIdx[CIdx(x, y - 1, z)], cellIdx[CIdx(x, y, z)],
                     cellIdx[CIdx(x - 1, y, z)])
          }
        }
      }
    }
  }
  var out = MeshData()
  out.v = verts
  out.idx = idx
  out.computeBounds()
  return out
}

// ---- generic helpers for non-SDF meshes ---------------------------------------
func makeUVSphere(_ r: Float, _ seg: Int, _ rings: Int) -> MeshData {
  var m = MeshData()
  m.v.reserveCapacity((rings + 1) * (seg + 1))
  for j in 0...rings {
    let v = Float(j) / Float(rings)
    let th = v * 3.14159265
    for i in 0...seg {
      let u = Float(i) / Float(seg)
      let ph = u * 6.2831853
      let n = vec3(sin(th) * cos(ph), cos(th), sin(th) * sin(ph))
      m.v.append(Vertex(pos: n * r, nrm: n, ao: 1, aux: 0))
    }
  }
  for j in 0..<rings {
    for i in 0..<seg {
      let a = UInt32(j * (seg + 1) + i), b = a + UInt32(seg + 1)
      m.idx.append(contentsOf: [a, a + 1, b, a + 1, b + 1, b])
    }
  }
  m.computeBounds()
  return m
}
