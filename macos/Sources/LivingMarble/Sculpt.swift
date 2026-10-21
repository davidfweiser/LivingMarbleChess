// The sculpting kit shared by the skinned armies (medieval, aliens): shapes with
// bounding spheres, a Model of parts blended into one signed distance field and
// bound to bones and materials, skin weights, and meshing a model into its LODs,
// fracture shards and slice halves.
import Foundation
import simd

// ---------------------------------------------------------------------------
// Shapes: a distance function plus a bounding sphere for quick rejection
// ---------------------------------------------------------------------------
struct Shape {
  var f: SDFFn
  var c = vec3(0)
  var r: Float = -1  // < 0: no bound
}

func cappedCylinder(_ p: vec3, _ a: vec3, _ b: vec3, _ r: Float) -> Float {
  let ba = b - a, pa = p - a
  let baba = dot(ba, ba), paba = dot(pa, ba)
  let x = length(pa * baba - ba * paba) - r * baba
  let y = abs(paba - baba * 0.5) - baba * 0.5
  let x2 = x * x, y2 = y * y * baba
  let d: Float = max(x, y) < 0 ? -min(x2, y2) : ((x > 0 ? x2 : 0) + (y > 0 ? y2 : 0))
  let sg: Float = d < 0 ? -1 : 1
  return sg * abs(d).squareRoot() / baba
}

/// Orthonormal frame whose y axis points along 'up' and whose z axis is as close to 'fwd' as possible.
func frameY(_ up: vec3, _ fwd: vec3 = vec3(0, 0, 1)) -> mat3 {
  let y = normalize(up)
  let f = abs(dot(y, normalize(fwd))) > 0.98 ? vec3(1, 0, 0) : fwd
  let x = normalize(cross(y, f))
  let z = cross(x, y)
  return mat3(x, y, z)
}
func rotXm(_ a: Float) -> mat3 { mat3(vec3(1, 0, 0), vec3(0, cos(a), sin(a)), vec3(0, -sin(a), cos(a))) }
func rotYm(_ a: Float) -> mat3 { mat3(vec3(cos(a), 0, -sin(a)), vec3(0, 1, 0), vec3(sin(a), 0, cos(a))) }
func rotZm(_ a: Float) -> mat3 { mat3(vec3(cos(a), sin(a), 0), vec3(-sin(a), cos(a), 0), vec3(0, 0, 1)) }

func sphereS(_ c: vec3, _ r: Float) -> Shape { Shape(f: { p in length(p - c) - r }, c: c, r: r) }
func capsuleS(_ a: vec3, _ b: vec3, _ r: Float) -> Shape {
  Shape(f: { p in SDF.capsule(p, a, b, r) }, c: (a + b) * 0.5, r: length(b - a) * 0.5 + r)
}
func coneS(_ a: vec3, _ b: vec3, _ ra: Float, _ rb: Float) -> Shape {
  Shape(f: { p in SDF.roundCone(p, a, b, ra, rb) }, c: (a + b) * 0.5, r: length(b - a) * 0.5 + max(ra, rb))
}
func cylS(_ a: vec3, _ b: vec3, _ r: Float) -> Shape {
  Shape(f: { p in cappedCylinder(p, a, b, r) }, c: (a + b) * 0.5, r: (dot(b - a, b - a) * 0.25 + r * r).squareRoot())
}
/// Ellipsoid with local axes R (columns), centre c.
func ellS(_ c: vec3, _ rad: vec3, _ R: mat3? = nil) -> Shape {
  let br = max(rad.x, max(rad.y, rad.z))
  guard let R else { return Shape(f: { p in SDF.ellipsoid(p - c, rad) }, c: c, r: br) }
  let Rt = R.transpose
  return Shape(f: { p in SDF.ellipsoid(Rt * (p - c), rad) }, c: c, r: br)
}
func boxS(_ c: vec3, _ half: vec3, _ round: Float, _ R: mat3? = nil) -> Shape {
  guard let R else { return Shape(f: { p in SDF.roundBox(p - c, half, round) }, c: c, r: length(half)) }
  let Rt = R.transpose
  return Shape(f: { p in SDF.roundBox(Rt * (p - c), half, round) }, c: c, r: length(half))
}
/// Ring in the frame's xz plane.
func torusS(_ c: vec3, _ R: Float, _ r: Float, _ F: mat3? = nil) -> Shape {
  guard let F else { return Shape(f: { p in SDF.torus(p - c, R, r) }, c: c, r: R + r) }
  let Ft = F.transpose
  return Shape(f: { p in SDF.torus(Ft * (p - c), R, r) }, c: c, r: R + r)
}
func latheS(_ prof: Profile, _ base: vec3) -> Shape {
  let h = prof.yMax - prof.yMin
  return Shape(f: { p in prof.eval(p - base) }, c: base + vec3(0, (prof.yMin + prof.yMax) * 0.5, 0),
               r: (prof.rMax * prof.rMax + 0.25 * h * h).squareRoot() + 0.01)
}
func fnS(_ f: @escaping SDFFn, _ c: vec3, _ r: Float) -> Shape { Shape(f: f, c: c, r: r) }
/// Mirror a shape across x = 0 (the other side of a symmetric figure).
func mirrorX(_ s: Shape) -> Shape {
  let f = s.f
  return Shape(f: { p in f(vec3(-p.x, p.y, p.z)) }, c: vec3(-s.c.x, s.c.y, s.c.z), r: s.r)
}

// ---------------------------------------------------------------------------
// Model: parts blended into one field, with bones and materials
// ---------------------------------------------------------------------------
/// A distance function the evaluators call through an unmanaged reference, so
/// the hot loops (run on every core at once) touch no reference counts.
fileprivate final class FnBox {
  let f: SDFFn
  init(_ f: @escaping SDFFn) { self.f = f }
}

final class Model {
  enum Op { case add, sub, paint }
  fileprivate struct Part {
    var op: Op
    var c: vec3, r: Float  // bounding sphere (r < 0: none)
    var bone: Int, mat: Int
    var k: Float
    var only: Int      // paint: only over this material (-1 any)
    var rigid: Bool    // never blended with a neighbouring bone
    var bone2: Int     // >= 0: stretches between 'bone' and 'bone2'
    var f: Unmanaged<FnBox>
    var w: Unmanaged<FnBox>?  // ... with this weight of 'bone' (chains, reins)
    @inline(__always) func eval(_ p: vec3) -> Float { f._withUnsafeGuaranteedRef { $0.f(p) } }
  }
  private var boxes: [FnBox] = []  // keeps the closures alive
  private var parts = ContiguousArray<Part>()
  // sealed copies read by the (concurrent) evaluators: solids (add and sub, in order),
  // the added parts, the carved parts with a material and the paint
  private var solid: UnsafeMutableBufferPointer<Part>?
  private var adds: UnsafeMutableBufferPointer<Part>?
  private var subs: UnsafeMutableBufferPointer<Part>?
  private var paints: UnsafeMutableBufferPointer<Part>?
  // the skeleton (a raw copy, for the same reason)
  private let skParent = UnsafeMutablePointer<Int>.allocate(capacity: Medieval.maxBones)
  private let skRigid = UnsafeMutablePointer<Bool>.allocate(capacity: Medieval.maxBones)
  private var skCount = 0
  var skinBlend: Float = 0.012

  init() {
    skParent.initialize(repeating: -1, count: Medieval.maxBones)
    skRigid.initialize(repeating: false, count: Medieval.maxBones)
  }
  deinit {
    for b in [solid, adds, subs, paints] { b?.deallocate() }
    skParent.deallocate()
    skRigid.deallocate()
  }

  func setSkeleton(_ s: MSkeleton) {
    skCount = s.count
    for b in 0..<s.count {
      skParent[b] = s.parent[b]
      skRigid[b] = s.rigid[b]
    }
  }

  private func box(_ f: @escaping SDFFn) -> Unmanaged<FnBox> {
    let b = FnBox(f)
    boxes.append(b)
    return Unmanaged.passUnretained(b)
  }
  private func push(_ op: Op, _ s: Shape, _ bone: Int, _ mat: Int, _ k: Float, _ only: Int, _ rigid: Bool,
                    _ bone2: Int, _ w: SDFFn?) {
    parts.append(Part(op: op, c: s.c, r: s.r, bone: bone, mat: mat, k: k, only: only, rigid: rigid, bone2: bone2,
                      f: box(s.f), w: w.map { box($0) }))
  }

  func add(_ bone: Int, _ mat: Int, _ k: Float, _ s: Shape) { push(.add, s, bone, mat, k, -1, false, -1, nil) }
  func addR(_ bone: Int, _ mat: Int, _ k: Float, _ s: Shape) { push(.add, s, bone, mat, k, -1, true, -1, nil) }
  func addStretch(_ bone: Int, _ bone2: Int, _ mat: Int, _ k: Float, _ s: Shape, _ w: @escaping SDFFn) {
    push(.add, s, bone, mat, k, -1, false, bone2, w)
  }
  /// Symmetric pair: the shape as given plus its mirror image on the other side.
  func add2(_ boneL: Int, _ boneR: Int, _ mat: Int, _ k: Float, _ s: Shape) {
    add(boneL, mat, k, s)
    add(boneR, mat, k, mirrorX(s))
  }
  func sub(_ mat: Int, _ k: Float, _ s: Shape) { push(.sub, s, -1, mat, k, -1, false, -1, nil) }
  func paint(_ mat: Int, _ s: Shape, _ only: Int = -1) { push(.paint, s, -1, mat, 0, only, false, -1, nil) }
  func paint2(_ mat: Int, _ s: Shape, _ only: Int = -1) {
    paint(mat, s, only)
    paint(mat, mirrorX(s), only)
  }

  /// Scales the whole model about the origin (its skeleton must be scaled to match).
  func scaleAll(_ s: Float) {
    for i in parts.indices {
      let f = parts[i].f.takeUnretainedValue().f
      parts[i].f = box { p in f(p / s) * s }
      parts[i].c *= s
      if parts[i].r > 0 { parts[i].r *= s }
      parts[i].k *= s
      if let w = parts[i].w?.takeUnretainedValue().f { parts[i].w = box { p in w(p / s) } }
    }
    skinBlend *= s
  }

  /// Grows any bounding sphere that does not contain its whole shape (cheap safety net:
  /// a sphere that is too small would cull the part where it should still show).
  /// (Paint ignores the bounds, so only the solids are checked.)
  func fitBounds() {
    for i in parts.indices {
      let q = parts[i]
      if q.op == .paint || q.r <= 0 { continue }
      var r = q.r
      for _ in 0..<4 {
        let N = 20
        let ext = r * 1.7
        var far: Float = 0
        for k in 0...N {
          for j in 0...N {
            for ii in 0...N {
              let p = q.c + vec3(Float(ii) * 2 / Float(N) - 1, Float(j) * 2 / Float(N) - 1,
                                 Float(k) * 2 / Float(N) - 1) * ext
              if q.eval(p) < 0.004 { far = max(far, length(p - q.c)) }
            }
          }
        }
        if far <= r { break }
        r = far + ext * 2 / Float(N) + 0.004
      }
      parts[i].r = r
    }
  }

  /// Freezes the parts for meshing (call after the last add and fitBounds).
  func seal() {
    func copy(_ keep: (Part) -> Bool) -> UnsafeMutableBufferPointer<Part> {
      let sel = parts.filter(keep)
      let b = UnsafeMutableBufferPointer<Part>.allocate(capacity: max(1, sel.count))
      _ = b.initialize(from: sel)
      return UnsafeMutableBufferPointer(rebasing: b[0..<sel.count])
    }
    for b in [solid, adds, subs, paints] { b?.deallocate() }
    solid = copy { $0.op != .paint }
    adds = copy { $0.op == .add }
    subs = copy { $0.op == .sub && $0.mat >= 0 }
    paints = copy { $0.op == .paint }
  }

  func eval(_ p: vec3) -> Float {
    var d: Float = 1e9
    guard let solid else { return d }
    for q in solid {
      if q.r > 0 {
        let bd = length(p - q.c) - q.r
        if q.op == .add && bd > d + q.k { continue }
        if q.op == .sub && bd > q.k - d { continue }
      }
      let x = q.eval(p)
      d = q.op == .add ? SDF.smin(d, x, q.k) : SDF.smax(d, -x, q.k)
    }
    return d
  }

  /// The material at p, the closest added part (index into 'adds') and, if
  /// boneDist is given, the distance to the nearest part of every bone.
  func materialAt(_ p: vec3, _ boneDist: UnsafeMutablePointer<Float>?) -> (mat: Int, nearest: Int) {
    var best: Float = 1e9
    var mat = 0, nearest = -1
    guard let adds, let subs, let paints else { return (0, -1) }
    for i in 0..<adds.count {
      let q = adds[i]
      let x = q.eval(p)
      if x < best {
        best = x
        mat = q.mat
        nearest = i
      }
      if let bd = boneDist, q.bone >= 0, x < bd[q.bone] { bd[q.bone] = x }
    }
    for q in subs where abs(q.eval(p)) < 0.006 { mat = q.mat }  // on the carved surface
    for q in paints {
      if q.only >= 0 && q.only != mat { continue }
      if q.eval(p) < 0 { mat = q.mat }
    }
    return (mat, nearest)
  }

  @inline(__always) func adjacent(_ a: Int, _ b: Int) -> Bool {
    skCount > 0 && (skParent[a] == b || skParent[b] == a)
  }

  func skin(_ v: inout Vertex) {
    let pos = v.pos
    let (mat, b0, b1, w0) = withUnsafeTemporaryAllocation(of: Float.self, capacity: Medieval.maxBones) {
      buf -> (Int, Int, Int, Float) in
      let bd = buf.baseAddress!
      for i in 0..<Medieval.maxBones { bd[i] = 1e9 }
      let (mat, np) = materialAt(pos, bd)
      var b0 = 0, b1 = 0
      var w0: Float = 1
      if skCount > 0 {
        let near: Part? = np >= 0 ? adds![np] : nil
        if let near, near.bone2 >= 0 {
          b0 = near.bone
          b1 = near.bone2
          w0 = clamp(near.w!._withUnsafeGuaranteedRef { $0.f(pos) }, 0, 1)
        } else if let near, near.rigid {
          b0 = near.bone
          b1 = near.bone
        } else {
          for b in 1..<skCount where bd[b] < bd[b0] { b0 = b }
          b1 = b0
          var d1: Float = 1e9
          if !skRigid[b0] {
            for b in 0..<skCount where b != b0 && !skRigid[b] && adjacent(b0, b) && bd[b] < d1 {
              d1 = bd[b]
              b1 = b
            }
          }
          if b1 != b0 { w0 = 1 - 0.5 * exp(-max(0, d1 - bd[b0]) / skinBlend) }
        }
      }
      return (mat, b0, b1, w0)
    }
    v.bones = Float(b0 + 64 * b1)
    v.mat = Float(mat) + 0.998 * w0
  }
  func paintOnly(_ v: inout Vertex) { v.mat = Float(materialAt(v.pos, nil).mat) + 0.998 }
}

/// Weight of the first bone along a segment a->b (1 at a, 0 at b).
func along(_ a: vec3, _ b: vec3) -> SDFFn {
  { p in
    let ab = b - a
    return 1 - clamp(dot(p - a, ab) / dot(ab, ab), 0, 1)
  }
}

/// A skeleton with every joint scaled about the origin (for a model built with scaleAll).
func scaledSkeleton(_ s: MSkeleton, _ k: Float) -> MSkeleton {
  var r = s
  for b in 0..<r.count { r.joint[b] *= k }
  return r
}

// thin triangles (wing membranes, fins)
@inline(__always) func dot2(_ v: vec3) -> Float { dot(v, v) }
func triangleDist(_ p: vec3, _ a: vec3, _ b: vec3, _ c: vec3) -> Float {
  let ba = b - a, pa = p - a, cb = c - b, pb = p - b, ac = a - c, pc = p - c
  let nor = cross(ba, ac)
  func sgn(_ x: Float) -> Float { x > 0 ? 1 : (x < 0 ? -1 : 0) }
  let s = sgn(dot(cross(ba, nor), pa)) + sgn(dot(cross(cb, nor), pb)) + sgn(dot(cross(ac, nor), pc))
  var d2: Float
  if s < 2 {
    d2 = min(min(dot2(ba * clamp(dot(ba, pa) / dot2(ba), 0, 1) - pa),
                 dot2(cb * clamp(dot(cb, pb) / dot2(cb), 0, 1) - pb)),
             dot2(ac * clamp(dot(ac, pc) / dot2(ac), 0, 1) - pc))
  } else {
    d2 = dot(nor, pa) * dot(nor, pa) / dot2(nor)
  }
  return d2.squareRoot()
}
func triS(_ a: vec3, _ b: vec3, _ c: vec3, _ t: Float) -> Shape {
  let m = (a + b + c) / 3
  let r = max(length(a - m), max(length(b - m), length(c - m))) + t
  return Shape(f: { p in triangleDist(p, a, b, c) - t }, c: m, r: r)
}

// ---------------------------------------------------------------------------
/// Meshes a model: LOD 0 and 1 (skinned and painted), and when seeds > 0 the
/// fracture shards and slice halves (painted, rigid).
func buildAsset(_ A: inout PieceAssets, _ m: Model, _ lo: vec3, _ hi: vec3, _ quality: Int, _ seeds: Int,
                _ seed: UInt32, _ height: Float, _ radius: Float) {
  let c0: [Float] = [0.0085, 0.0062, 0.0048]
  let c1: [Float] = [0.016, 0.013, 0.011]
  let cs: [Float] = [0.014, 0.011, 0.009]
  m.fitBounds()
  m.seal()
  let F: SDFFn = { p in m.eval(p) }
  var o = MeshOptions()
  o.bmin = lo
  o.bmax = hi
  o.cell = c0[quality]
  o.aoRadius = 0.06
  o.skin = { v in m.skin(&v) }
  A.lod0 = polygonize(F, o)
  o.cell = c1[quality]
  o.newtonSteps = 3
  A.lod1 = polygonize(F, o)
  let paint: (inout Vertex) -> Void = { v in m.paintOnly(&v) }
  if seeds > 0 {
    A.shards = MeshGen.voronoiShatter(F, lo, hi, seeds, seed, cs[quality], skin: paint)
    A.sliceN = normalize(vec3(0.78, 0.62, 0.0))
    A.slicePoint = vec3(0, height * 0.5, 0)
    A.halves = MeshGen.sliceHalves(F, lo, hi, A.sliceN, A.slicePoint, cs[quality], skin: paint)
  }
  A.height = height
  A.radius = radius
}
