// Sculpting the medieval army. Every figure is a list of parts - capsules,
// cones, ellipsoids, boxes and lathe profiles - each bound to a bone and a
// material. The parts are blended into one signed distance field which the
// surface-nets mesher turns into a mesh; afterwards every vertex is skinned
// to the bone(s) of the part(s) it lies on and painted with their material.
import Foundation
import simd

// ---------------------------------------------------------------------------
// Shapes: a distance function plus a bounding sphere for quick rejection
// ---------------------------------------------------------------------------
private struct Shape {
  var f: SDFFn
  var c = vec3(0)
  var r: Float = -1  // < 0: no bound
}

private func cappedCylinder(_ p: vec3, _ a: vec3, _ b: vec3, _ r: Float) -> Float {
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
private func frameY(_ up: vec3, _ fwd: vec3 = vec3(0, 0, 1)) -> mat3 {
  let y = normalize(up)
  let f = abs(dot(y, normalize(fwd))) > 0.98 ? vec3(1, 0, 0) : fwd
  let x = normalize(cross(y, f))
  let z = cross(x, y)
  return mat3(x, y, z)
}
private func rotXm(_ a: Float) -> mat3 { mat3(vec3(1, 0, 0), vec3(0, cos(a), sin(a)), vec3(0, -sin(a), cos(a))) }
private func rotYm(_ a: Float) -> mat3 { mat3(vec3(cos(a), 0, -sin(a)), vec3(0, 1, 0), vec3(sin(a), 0, cos(a))) }
private func rotZm(_ a: Float) -> mat3 { mat3(vec3(cos(a), sin(a), 0), vec3(-sin(a), cos(a), 0), vec3(0, 0, 1)) }

private func sphereS(_ c: vec3, _ r: Float) -> Shape { Shape(f: { p in length(p - c) - r }, c: c, r: r) }
private func capsuleS(_ a: vec3, _ b: vec3, _ r: Float) -> Shape {
  Shape(f: { p in SDF.capsule(p, a, b, r) }, c: (a + b) * 0.5, r: length(b - a) * 0.5 + r)
}
private func coneS(_ a: vec3, _ b: vec3, _ ra: Float, _ rb: Float) -> Shape {
  Shape(f: { p in SDF.roundCone(p, a, b, ra, rb) }, c: (a + b) * 0.5, r: length(b - a) * 0.5 + max(ra, rb))
}
private func cylS(_ a: vec3, _ b: vec3, _ r: Float) -> Shape {
  Shape(f: { p in cappedCylinder(p, a, b, r) }, c: (a + b) * 0.5, r: (dot(b - a, b - a) * 0.25 + r * r).squareRoot())
}
/// Ellipsoid with local axes R (columns), centre c.
private func ellS(_ c: vec3, _ rad: vec3, _ R: mat3? = nil) -> Shape {
  let br = max(rad.x, max(rad.y, rad.z))
  guard let R else { return Shape(f: { p in SDF.ellipsoid(p - c, rad) }, c: c, r: br) }
  let Rt = R.transpose
  return Shape(f: { p in SDF.ellipsoid(Rt * (p - c), rad) }, c: c, r: br)
}
private func boxS(_ c: vec3, _ half: vec3, _ round: Float, _ R: mat3? = nil) -> Shape {
  guard let R else { return Shape(f: { p in SDF.roundBox(p - c, half, round) }, c: c, r: length(half)) }
  let Rt = R.transpose
  return Shape(f: { p in SDF.roundBox(Rt * (p - c), half, round) }, c: c, r: length(half))
}
/// Ring in the frame's xz plane.
private func torusS(_ c: vec3, _ R: Float, _ r: Float, _ F: mat3? = nil) -> Shape {
  guard let F else { return Shape(f: { p in SDF.torus(p - c, R, r) }, c: c, r: R + r) }
  let Ft = F.transpose
  return Shape(f: { p in SDF.torus(Ft * (p - c), R, r) }, c: c, r: R + r)
}
private func latheS(_ prof: Profile, _ base: vec3) -> Shape {
  let h = prof.yMax - prof.yMin
  return Shape(f: { p in prof.eval(p - base) }, c: base + vec3(0, (prof.yMin + prof.yMax) * 0.5, 0),
               r: (prof.rMax * prof.rMax + 0.25 * h * h).squareRoot() + 0.01)
}
private func fnS(_ f: @escaping SDFFn, _ c: vec3, _ r: Float) -> Shape { Shape(f: f, c: c, r: r) }
/// Mirror a shape across x = 0 (the other side of a symmetric figure).
private func mirrorX(_ s: Shape) -> Shape {
  let f = s.f
  return Shape(f: { p in f(vec3(-p.x, p.y, p.z)) }, c: vec3(-s.c.x, s.c.y, s.c.z), r: s.r)
}

/// 2D kite / heater shield outline in (u across, v up), negative inside.
private func shield2D(_ q: vec2, _ w: Float, _ h: Float, _ point: Float) -> Float {
  // rounded top + taper to a point at the bottom
  let top = h * 0.5
  let u = abs(q.x)
  let v = q.y
  // straight-ish sides that curve into the point
  let t = clamp((top - v) / h, 0, 1)
  let halfW = w * 0.5 * pow(max(0, 1 - pow(t, point)), 0.5)
  let side = u - halfW
  let lid = v - top
  let bottom = -(v + h * 0.5)
  return max(max(side, lid), bottom) * 0.8
}

// ---------------------------------------------------------------------------
// Model: parts blended into one field, with bones and materials
// ---------------------------------------------------------------------------
/// A distance function the evaluators call through an unmanaged reference, so
/// the hot loops (run on every core at once) touch no reference counts.
private final class FnBox {
  let f: SDFFn
  init(_ f: @escaping SDFFn) { self.f = f }
}

private final class Model {
  enum Op { case add, sub, paint }
  struct Part {
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

  @inline(__always) private func adjacent(_ a: Int, _ b: Int) -> Bool {
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
private func along(_ a: vec3, _ b: vec3) -> SDFFn {
  { p in
    let ab = b - a
    return 1 - clamp(dot(p - a, ab) / dot(ab, ab), 0, 1)
  }
}

// ---------------------------------------------------------------------------
// Shared body parts
// ---------------------------------------------------------------------------
/// A head facing +z: skull, jaw, nose, brows, ears, eyes and mouth.
/// beard: 0 none, 1 short, 2 full and long
private func addHead(_ m: Model, _ bone: Int, _ c: vec3, _ s: Float, _ beard: Int, _ hairMat: Int) {
  m.add(bone, MMat.skin, 0, ellS(c, vec3(0.044, 0.054, 0.049) * s))
  m.add(bone, MMat.skin, 0.018 * s, ellS(c + vec3(0, -0.026, 0.012) * s, vec3(0.036, 0.032, 0.038) * s))  // jaw
  m.add(bone, MMat.skin, 0.008 * s,
        coneS(c + vec3(0, 0.006, 0.046) * s, c + vec3(0, -0.012, 0.058) * s, 0.006 * s, 0.009 * s))  // nose
  m.add(bone, MMat.skin, 0.01 * s, ellS(c + vec3(0, 0.014, 0.038) * s, vec3(0.034, 0.008, 0.012) * s))  // brow
  m.add2(bone, bone, MMat.skin, 0.006 * s, ellS(c + vec3(0.045, 0.0, 0.0) * s, vec3(0.007, 0.014, 0.01) * s))
  for sd in stride(from: -1, through: 1, by: 2) {
    let e = c + vec3(Float(sd) * 0.017, 0.004, 0.041) * s
    m.sub(MMat.skin, 0.006 * s, sphereS(e + vec3(0, 0, 0.004) * s, 0.008 * s))
    m.add(bone, MMat.dark, 0.002 * s, sphereS(e - vec3(0, 0, 0.002) * s, 0.0065 * s))
  }
  m.paint(MMat.dark, ellS(c + vec3(0, -0.028, 0.05) * s, vec3(0.012, 0.0025, 0.02) * s), MMat.skin)  // mouth
  if beard > 0 {
    let L: Float = beard == 2 ? 1.7 : 1.0
    let bc = c + vec3(0, -0.036 - 0.012 * (L - 1), 0.022 + 0.004 * (L - 1)) * s
    m.add(bone, hairMat, 0.012 * s, ellS(bc, vec3(0.036, 0.03 * L, 0.03) * s))
    m.add(bone, hairMat, 0.006 * s,
          ellS(c + vec3(0, -0.018, 0.047) * s, vec3(0.02, 0.006, 0.008) * s, rotXm(-0.2)))
  }
}

/// Hair covering the back and top of the skull.
private func addHair(_ m: Model, _ bone: Int, _ c: vec3, _ s: Float, _ mat: Int, _ len: Float) {
  m.add(bone, mat, 0.01 * s, fnS({ p in
    let e = SDF.ellipsoid(p - (c + vec3(0, 0.006, -0.006) * s), vec3(0.048, 0.058, 0.053) * s)
    // only behind the face
    return max(e, (p.z - c.z) - 0.018 * s + 0.03 * max(0, (p.y - c.y) / s - 0.01) * s)
  }, c, 0.07 * s))
  if len > 0 {
    m.add(bone, mat, 0.02 * s, ellS(c + vec3(0, -0.04 * len, -0.03) * s, vec3(0.05, 0.05 * len, 0.03) * s))
  }
}

/// A hand closed around a grip along 'axis' (fist), centred at c.
private func addFist(_ m: Model, _ bone: Int, _ c: vec3, _ axis: vec3, _ s: Float, _ mat: Int = MMat.skin) {
  let F = frameY(axis)
  m.add(bone, mat, 0.008 * s, ellS(c, vec3(0.019, 0.024, 0.02) * s, F))
}

private func addArm(_ m: Model, _ left: Bool, _ j: HumanJ, _ mat: Int, _ rU: Float, _ rF: Float, _ cuffMat: Int) {
  let sh = left ? j.shL : j.shR, el = left ? j.elL : j.elR, wr = left ? j.wrL : j.wrR
  let ua = left ? HB.lArm : HB.rArm, fa = left ? HB.lFore : HB.rFore
  let S = j.S
  let d = normalize(wr - el)
  m.add(ua, mat, 0.02 * S, coneS(sh, el, rU, rU * 0.82))
  m.add(fa, mat, 0.012 * S, coneS(el, wr - d * 0.02 * S, rF, rF * 0.82))
  if cuffMat >= 0 {
    m.add(fa, cuffMat, 0.004 * S, coneS(wr - d * 0.034 * S, wr - d * 0.014 * S, rF * 0.95, rF * 0.9))
  }
}

/// Gold crown band with points (fleurons) and gems; 'arches' closes it into an imperial crown.
private func addCrown(_ m: Model, _ bone: Int, _ c: vec3, _ r: Float, _ h: Float, _ points: Int, _ arches: Bool) {
  m.add(bone, MMat.gold, 0, fnS({ p in
    let q = p - c
    let rr = (q.x * q.x + q.z * q.z).squareRoot()
    let ring = abs(rr - r) - 0.006
    return max(ring, abs(q.y) - h * 0.5)
  }, c, r + h))
  for i in 0..<points {
    let a = 6.2831853 * (Float(i) + 0.5) / Float(points)
    let d = vec3(sin(a), 0, cos(a))
    let tall = i % 2 == 0
    let base = c + d * r + vec3(0, h * 0.5, 0)
    let ph = tall ? h * 1.25 : h * 0.6
    m.add(bone, MMat.gold, 0.004, coneS(base, base + vec3(0, ph, 0) - d * 0.004, 0.012, 0.004))
    if tall {
      // fleur-de-lis: a centre petal and two curling side leaves
      let top = base + vec3(0, ph, 0) - d * 0.004
      let side = vec3(d.z, 0, -d.x)
      m.add(bone, MMat.gold, 0.004, sphereS(top, 0.009))
      m.add(bone, MMat.gold, 0.004,
            capsuleS(base + vec3(0, ph * 0.45, 0), base + vec3(0, ph * 0.7, 0) + side * 0.016, 0.0045))
      m.add(bone, MMat.gold, 0.004,
            capsuleS(base + vec3(0, ph * 0.45, 0), base + vec3(0, ph * 0.7, 0) - side * 0.016, 0.0045))
    } else {
      m.add(bone, MMat.gold, 0.003, sphereS(base + vec3(0, ph, 0), 0.007))
    }
    // gems on the band
    let g = c + d * (r + 0.005)
    m.add(bone, tall ? MMat.gem : MMat.gold, 0.002, sphereS(g, tall ? 0.0085 : 0.005))
  }
  if arches {
    for k in 0..<2 {
      let Ft = (k == 0 ? mat3.identity : rotYm(1.5708)).transpose
      let arch: SDFFn = { p in
        let q = Ft * (p - c - vec3(0, h * 0.5, 0))
        let e = (q.x * q.x / (r * r) + q.y * q.y / (r * r * 0.81)).squareRoot() * r - r  // half ellipse over the band
        return max(max(abs(e) - 0.005, abs(q.z) - 0.007), -q.y)
      }
      m.add(bone, MMat.gold, 0.003, fnS(arch, c + vec3(0, h, 0), r + 0.02))
    }
    let top = c + vec3(0, h * 0.5 + r * 0.9, 0)
    m.add(bone, MMat.gold, 0.004, sphereS(top + vec3(0, 0.012, 0), 0.013))
    m.add(bone, MMat.gold, 0.002, boxS(top + vec3(0, 0.04, 0), vec3(0.0035, 0.017, 0.0035), 0.001))
    m.add(bone, MMat.gold, 0.002, boxS(top + vec3(0, 0.045, 0), vec3(0.011, 0.0035, 0.0035), 0.001))
    // velvet cap inside the arches
    m.add(bone, MMat.velvet, 0.006, fnS({ p in
      let e = SDF.ellipsoid(p - (c + vec3(0, h * 0.5, 0)), vec3(r - 0.004, r * 0.8, r - 0.004))
      return max(e, c.y - p.y)
    }, c + vec3(0, h, 0), r))
  }
}

/// A spear head on top of the shaft end b (pointing up +y).
private func spearHead(_ b: vec3) -> SDFFn {
  { p in
    let q = p - (b + vec3(0, 0.01, 0))
    let t = clamp(q.y / 0.095, 0, 1)
    let w = 0.019 * sin(pow(t, 0.7) * 3.14159) + 0.001
    let th = 0.0035 * (1 - t) + 0.001
    let d = max(abs(q.x) / w + abs(q.z) / th - 1, 0) * min(w, th)
    let body = max(abs(q.x) - w, abs(q.z) - th)
    return max(max(body, d), max(-q.y, q.y - 0.095))
  }
}

// ---------------------------------------------------------------------------
// The pieces
// ---------------------------------------------------------------------------
/// Pawn: a man-at-arms in a kettle hat with a spear and a kite shield.
private func pawnModel() -> Model {
  let j = HumanJ.pawn()
  let m = Model()
  m.setSkeleton(Medieval.skeleton(MAsset.pawn))
  // --- legs: hose and boots
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let th = sd > 0 ? HB.lThigh : HB.rThigh, sh = sd > 0 ? HB.lShin : HB.rShin
    let hip = vec3(j.hipL.x * x, j.hipL.y, j.hipL.z), knee = vec3(j.kneeL.x * x, j.kneeL.y, j.kneeL.z)
    let ankle = vec3(j.ankleL.x * x, j.ankleL.y, j.ankleL.z)
    m.add(th, MMat.cloth2, 0, coneS(hip, knee, 0.038, 0.028))
    m.add(sh, MMat.cloth2, 0.012, coneS(knee, ankle + vec3(0, 0.04, 0), 0.028, 0.021))
    m.add(sh, MMat.leather, 0.008, coneS(ankle + vec3(0, 0.07, 0), ankle, 0.026, 0.025))
    m.add(sh, MMat.leather, 0.012, boxS(vec3(ankle.x, 0.022, 0.025), vec3(0.024, 0.02, 0.058), 0.016))
    m.add(sh, MMat.leather, 0, boxS(vec3(ankle.x, 0.006, 0.022), vec3(0.026, 0.006, 0.062), 0.005))  // sole
  }
  // --- body: quilted gambeson with a flared, pleated skirt; a tabard hangs over it as a shell
  do {
    var skirt = Profile()
    skirt.build([vec2(0, 0.262), vec2(0.094, 0.262), vec2(0.101, 0.27), vec2(0.096, 0.30), vec2(0.084, 0.38),
                 vec2(0.078, 0.45), vec2(0.074, 0.50), vec2(0, 0.50)], 3)
    let prof = skirt
    let skirtF: SDFFn = { p in
      let a = atan2(p.x, p.z)
      let folds = 0.0035 * sin(a * 13) * clamp((0.40 - p.y) / 0.12, 0, 1)
      return prof.eval(p) - folds
    }
    let chestF: SDFFn = { p in
      let c = SDF.ellipsoid(p - vec3(0, 0.555, 0.002), vec3(0.084, 0.085, 0.056))
      return SDF.smin(c, SDF.ellipsoid(p - vec3(0, 0.598, -0.004), vec3(0.094, 0.045, 0.052)), 0.03)
    }
    m.add(HB.root, MMat.cloth2, 0, fnS(skirtF, vec3(0, 0.38, 0), 0.16))
    m.add(HB.chest, MMat.cloth2, 0.04, fnS(chestF, vec3(0, 0.57, 0), 0.12))
    for i in 0..<4 {  // quilting
      let y = 0.31 + Float(i) * 0.05
      m.sub(-1, 0.003, torusS(vec3(0, y, 0), 0.094 - (y > 0.42 ? 0.012 : 0) + (y < 0.33 ? 0.004 : 0), 0.0028))
    }
    for side in 0..<2 {
      let bone = side == 0 ? HB.chest : HB.root
      let shell: SDFFn = { p in
        let body = side == 0 ? chestF(p) : skirtF(p)
        let d = body - 0.0045
        let panel = abs(p.x) - 0.064
        let y0: Float = side == 0 ? 0.43 : 0.275, y1: Float = side == 0 ? 0.645 : 0.48
        let ylim = max(y0 - p.y, p.y - y1)
        return max(max(d, panel), ylim)
      }
      m.add(bone, MMat.cloth, 0.004, fnS(shell, vec3(0, side == 0 ? 0.56 : 0.37, 0), 0.15))
    }
  }
  m.paint(MMat.cloth2, boxS(vec3(0, 0.525, 0.08), vec3(0.012, 0.062, 0.05), 0), MMat.cloth)
  m.paint(MMat.cloth2, boxS(vec3(0, 0.54, 0.08), vec3(0.046, 0.012, 0.05), 0), MMat.cloth)
  m.add(HB.root, MMat.leather, 0.004, torusS(vec3(0, 0.445, 0.004), 0.083, 0.008))
  m.add(HB.root, MMat.gold, 0.002, boxS(vec3(0, 0.445, 0.09), vec3(0.012, 0.011, 0.005), 0.003))
  m.add(HB.chest, MMat.mail, 0.016, coneS(vec3(0, 0.62, -0.006), vec3(0, 0.676, -0.004), 0.072, 0.034))
  // --- arms: quilted sleeves, mail cuffs, fists
  addArm(m, true, j, MMat.cloth2, 0.033, 0.025, MMat.mail)
  addArm(m, false, j, MMat.cloth2, 0.033, 0.025, MMat.mail)
  addFist(m, HB.rFore, j.gripR, vec3(0, 1, 0), 1)
  addFist(m, HB.lFore, j.gripL, vec3(0, 0.3, 1), 1)
  // --- head with a kettle hat
  addHead(m, HB.head, vec3(0, 0.728, 0.012), 1, 1, MMat.hair)
  m.add(HB.head, MMat.steel, 0.004, fnS({ p in
    let e = SDF.ellipsoid(p - vec3(0, 0.762, 0.006), vec3(0.056, 0.062, 0.06))
    return max(e, 0.772 - p.y)
  }, vec3(0, 0.79, 0.006), 0.07))
  m.add(HB.head, MMat.steel, 0.006, fnS({ p in
    let q = p - vec3(0, 0.776, 0.006)
    let r = (q.x * q.x + q.z * q.z).squareRoot()
    let y = q.y + 0.12 * max(0, r - 0.05)
    let d = vec2(max(0, r - 0.092), abs(y) - 0.0035)
    return min(max(d.x, d.y), 0) + length(vmax(d, vec2(0))) - 0.002
  }, vec3(0, 0.776, 0.006), 0.1))
  m.add(HB.head, MMat.steel, 0.004, capsuleS(vec3(0, 0.79, -0.05), vec3(0, 0.83, 0.03), 0.004))
  // --- spear (right hand), butt on the ground
  do {
    let a = vec3(j.gripR.x, 0.006, j.gripR.z), b = vec3(j.gripR.x, 0.97, j.gripR.z)
    m.addR(HB.itemR, MMat.wood, 0, cylS(a, b, 0.0075))
    m.addR(HB.itemR, MMat.iron, 0.002, cylS(a, a + vec3(0, 0.025, 0), 0.0085))
    m.addR(HB.itemR, MMat.iron, 0.002, cylS(b - vec3(0, 0.02, 0), b + vec3(0, 0.012, 0), 0.0095))
    m.addR(HB.itemR, MMat.steel, 0, fnS(spearHead(b), b + vec3(0, 0.06, 0), 0.07))
  }
  // --- kite shield on the left forearm
  do {
    let c = vec3(0.168, 0.4, 0.075)
    let R = rotYm(0.62)
    let Rt = R.transpose
    let plate: SDFFn = { p in
      let q = Rt * (p - c)
      let bulge = 0.025 * (q.x / 0.085) * (q.x / 0.085)
      let d2 = shield2D(vec2(q.x, q.y), 0.17, 0.30, 2.2)
      return max(d2, abs(q.z + bulge) - 0.007)
    }
    m.addR(HB.itemL, MMat.cloth, 0, fnS(plate, c, 0.17))
    let rim: SDFFn = { p in
      let q = Rt * (p - c)
      let bulge = 0.025 * (q.x / 0.085) * (q.x / 0.085)
      let d2 = shield2D(vec2(q.x, q.y), 0.178, 0.31, 2.2)
      return max(abs(d2 + 0.006) - 0.006, abs(q.z + bulge) - 0.0095)
    }
    m.addR(HB.itemL, MMat.iron, 0, fnS(rim, c, 0.18))
    let chevron: SDFFn = { p in
      let q = Rt * (p - c)
      let v = q.y + 0.6 * abs(q.x) - 0.02
      return max(abs(v) - 0.022, plate(p) - 0.004)
    }
    m.paint(MMat.cloth2, fnS(chevron, c, 0.2), MMat.cloth)
    m.addR(HB.itemL, MMat.gold, 0.003, ellS(c + R * vec3(0, 0.045, 0.004), vec3(0.016, 0.016, 0.008), R))
  }
  return m
}

// --- seated royals and the bishop ------------------------------------------
/// Torso of a seated figure (pelvis on the root bone, the rest on the chest).
private func seatedChestField(_ j: HumanJ) -> SDFFn {
  let S = j.S
  let w = j.waist
  return { p in
    let c = SDF.ellipsoid(p - (w + vec3(0, 0.2, 0.0) * S), vec3(0.122, 0.15, 0.085) * S)
    let sh = SDF.ellipsoid(p - (w + vec3(0, 0.305, -0.01) * S), vec3(0.15, 0.055, 0.075) * S)
    let wa = SDF.ellipsoid(p - (w + vec3(0, 0.03, 0.005) * S), vec3(0.105, 0.075, 0.08) * S)
    return SDF.smin(SDF.smin(c, sh, 0.05 * S), wa, 0.05 * S)
  }
}

private func seatedBody(_ m: Model, _ j: HumanJ, _ bodyMat: Int, _ legMat: Int, _ shoeMat: Int, _ drape: Bool,
                        _ drapeMat: Int) {
  let S = j.S
  m.add(HB.root, bodyMat, 0, ellS(j.root + vec3(0, -0.005, 0.0) * S, vec3(0.128, 0.075, 0.11) * S))
  let chest = seatedChestField(j)
  m.add(HB.chest, bodyMat, 0.05 * S, fnS(chest, j.waist + vec3(0, 0.18, 0) * S, 0.24 * S))
  m.add(HB.head, MMat.skin, 0.01 * S,
        cylS(j.neck - vec3(0, 0.04, 0.005) * S, j.neck + vec3(0, 0.05, 0.01) * S, 0.036 * S))
  for sd in stride(from: -1, through: 1, by: 2) {
    let L = sd > 0
    let hip = L ? j.hipL : mirrorXv(j.hipL), knee = L ? j.kneeL : mirrorXv(j.kneeL)
    let ankle = L ? j.ankleL : mirrorXv(j.ankleL)
    let th = L ? HB.lThigh : HB.rThigh, sh = L ? HB.lShin : HB.rShin
    m.add(th, bodyMat, 0.03 * S, coneS(hip, knee, 0.066 * S, 0.052 * S))
    // the lap: cloth spanning the gap between the thighs (each half follows its own thigh)
    m.add(th, bodyMat, 0.03 * S,
          boxS(vec3(hip.x * 0.5, hip.y + 0.005 * S, (hip.z + knee.z) * 0.5 + 0.01 * S),
               vec3(0.045, 0.045, 0.11) * S, 0.035 * S))
    if drape {
      // a gown or alb falling from the knees to the floor
      m.add(sh, drapeMat, 0.03 * S, coneS(knee, ankle + vec3(0, -0.04, 0.0) * S, 0.06 * S, 0.075 * S))
      m.add(sh, drapeMat, 0.03 * S,
            boxS(vec3(knee.x * 0.5, (knee.y + 0.01 * S) * 0.5, knee.z + 0.01 * S),
                 vec3(0.066 * S, (knee.y - 0.01 * S) * 0.5, 0.052 * S), 0.03 * S))
      // the hem flares a little where it meets the floor
      m.add(sh, drapeMat, 0.03 * S,
            boxS(vec3(knee.x * 0.5, 0.02 * S, knee.z + 0.03 * S), vec3(0.075, 0.02, 0.07) * S, 0.02 * S))
      m.add(sh, shoeMat, 0.01 * S,
            ellS(vec3(ankle.x, 0.018 * S, ankle.z + 0.07 * S), vec3(0.03, 0.018, 0.05) * S))
    } else {
      m.add(sh, legMat, 0.015 * S, coneS(knee, ankle + vec3(0, 0.03, 0) * S, 0.046 * S, 0.034 * S))
      m.add(sh, shoeMat, 0.012 * S,
            boxS(vec3(ankle.x, 0.028 * S, ankle.z + 0.032 * S), vec3(0.033, 0.026, 0.07) * S, 0.022 * S))
    }
  }
}

/// A cloak over the shoulders and down the back to the seat.
private func addMantle(_ m: Model, _ j: HumanJ, _ mat: Int, _ offset: Float, _ bottom: Float, _ frontLimit: Float) {
  let S = j.S
  let chest = seatedChestField(j)
  let shL = j.shL, shR = j.shR, elL = j.elL, elR = j.elR
  let zc = j.waist.z
  let f: SDFFn = { p in
    let yTop = shL.y - 0.07 * S
    let q = vec3(p.x * (p.y < yTop ? 1 + 0.25 * (yTop - p.y) / S : 1), max(p.y, yTop), p.z)
    let body = chest(q)
    let arms = min(SDF.roundCone(p, shL, elL, 0.05 * S, 0.044 * S), SDF.roundCone(p, shR, elR, 0.05 * S, 0.044 * S))
    var d = SDF.smin(body, arms, 0.04 * S) - offset * S
    d = max(d, -(d + 0.012 * S))  // a shell, not a solid
    let front = (p.z - zc) - frontLimit * S
    return max(max(d, front), bottom * S - p.y)
  }
  m.add(HB.chest, mat, 0.006 * S, fnS(f, j.waist + vec3(0, 0.15, -0.05) * S, 0.34 * S))
}

/// King: crowned, bearded, in an ermine-trimmed mantle, a sword across his knees.
private func kingModel() -> Model {
  let j = HumanJ.seated(MAsset.king)
  let S = j.S
  let m = Model()
  m.setSkeleton(Medieval.skeleton(MAsset.king))
  seatedBody(m, j, MMat.cloth, MMat.cloth2, MMat.leather, false, 0)
  // gold hem of the tunic at the knees and a jewelled belt
  for sd in stride(from: -1, through: 1, by: 2) {
    m.paint(MMat.gold, boxS(vec3(Float(sd) * 0.075, 0.335, 0.172) * S, vec3(0.08, 0.08, 0.009) * S, 0), MMat.cloth)
  }
  m.add(HB.root, MMat.gold, 0.006 * S, torusS(j.waist + vec3(0, -0.005, 0.01) * S, 0.104 * S, 0.009 * S))
  m.add(HB.root, MMat.gem, 0.002 * S, sphereS(j.waist + vec3(0, -0.005, 0.096) * S, 0.011 * S))
  // tunic: a broad gold band down the front
  m.paint(MMat.gold, boxS(j.waist + vec3(0, 0.18, 0.09) * S, vec3(0.018, 0.2, 0.04) * S, 0), MMat.cloth)
  addMantle(m, j, MMat.cloth2, 0.012, 0.29, 0.03)
  // ermine shoulder cape
  m.add(HB.chest, MMat.fur, 0.02 * S, fnS({ p in
    let e = SDF.ellipsoid(p - (j.waist + vec3(0, 0.3, -0.012) * S), vec3(0.172, 0.075, 0.105) * S)
    return max(e, -(e + 0.02 * S))
  }, j.waist + vec3(0, 0.3, 0) * S, 0.18 * S))
  addArm(m, true, j, MMat.cloth, 0.046 * S, 0.039 * S, MMat.gold)
  addArm(m, false, j, MMat.cloth, 0.046 * S, 0.039 * S, MMat.gold)
  addFist(m, HB.rFore, j.gripR, vec3(1, 0, 0), 1.35 * S)
  addFist(m, HB.lFore, j.gripL + vec3(0, 0.008, 0) * S, vec3(1, 0, 0.3), 1.35 * S)
  let hc = j.neck + vec3(0, 0.078, 0.012) * S
  addHead(m, HB.head, hc, 1.42 * S, 2, MMat.hair)
  addHair(m, HB.head, hc, 1.42 * S, MMat.hair, 0.6)
  addCrown(m, HB.head, hc + vec3(0, 0.056, -0.004) * S, 0.066 * S, 0.034 * S, 8, true)
  // the sword lies across his lap, hilt in his right hand
  do {
    let g = j.gripR
    let ax = vec3(1, 0, 0)
    m.addR(HB.itemR, MMat.leather, 0, cylS(g - ax * 0.05 * S, g + ax * 0.03 * S, 0.011 * S))
    m.addR(HB.itemR, MMat.gold, 0.004, sphereS(g - ax * 0.065 * S, 0.019 * S))
    m.addR(HB.itemR, MMat.gold, 0.004 * S, boxS(g + ax * 0.04 * S, vec3(0.009, 0.012, 0.075) * S, 0.006 * S))
    m.addR(HB.itemR, MMat.steel, 0, fnS({ p in
      let q = p - g
      let t = clamp((q.x - 0.05 * S) / (0.43 * S), 0, 1)
      let w = 0.021 * S * (1 - 0.55 * t) * max(0, 1 - pow(t, 6)).squareRoot() + 0.001
      let th = 0.006 * S * (1 - 0.5 * t) + 0.001
      let body = max(abs(q.z) - w, abs(q.y) - th)
      return max(body, max(0.05 * S - q.x, q.x - 0.48 * S))
    }, g + vec3(0.26, 0, 0) * S, 0.25 * S))
  }
  return m
}

/// Queen: crowned and veiled, in a long gown, with a sceptre.
private func queenModel() -> Model {
  let j = HumanJ.seated(MAsset.queen)
  let S = j.S
  let m = Model()
  m.setSkeleton(Medieval.skeleton(MAsset.queen))
  seatedBody(m, j, MMat.cloth2, MMat.cloth2, MMat.leather, true, MMat.cloth2)
  // slimmer bodice: a gold girdle and a jewelled necklace
  m.add(HB.root, MMat.gold, 0.006 * S, torusS(j.waist + vec3(0, -0.005, 0.01) * S, 0.1 * S, 0.007 * S))
  m.paint(MMat.gold, torusS(j.neck + vec3(0, -0.035, 0.02) * S, 0.06 * S, 0.008 * S, rotXm(0.5)), MMat.cloth2)
  m.add(HB.chest, MMat.gem, 0.002 * S, sphereS(j.neck + vec3(0, -0.075, 0.08) * S, 0.012 * S))
  // embroidered hem of the gown
  m.paint(MMat.gold, boxS(vec3(0, 0.03, 0.25) * S, vec3(0.2, 0.03, 0.1) * S, 0), MMat.cloth2)
  addMantle(m, j, MMat.cloth, 0.012, 0.29, 0.01)
  addArm(m, true, j, MMat.cloth2, 0.042 * S, 0.034 * S, MMat.gold)
  addArm(m, false, j, MMat.cloth2, 0.042 * S, 0.034 * S, MMat.gold)
  addFist(m, HB.rFore, j.gripR, vec3(0, 1, 0), 1.25 * S)
  addFist(m, HB.lFore, j.gripL + vec3(0, 0.008, 0) * S, vec3(1, 0, 0.3), 1.25 * S)
  let hc = j.neck + vec3(0, 0.078, 0.012) * S
  addHead(m, HB.head, hc, 1.36 * S, 0, MMat.hair)
  // braided hair coiled over the ears, under a linen veil
  m.add2(HB.head, HB.head, MMat.hair, 0.008 * S,
         ellS(hc + vec3(0.062, -0.02, -0.005) * S, vec3(0.022, 0.034, 0.03) * S))
  m.add(HB.head, MMat.linen, 0.012 * S, fnS({ p in
    let e = SDF.ellipsoid(p - (hc + vec3(0, 0.008, -0.008) * S), vec3(0.066, 0.078, 0.072) * S)
    let q = vec3(p.x, min(p.y, hc.y - 0.02 * S), p.z)  // falls down behind the neck
    let fall = SDF.ellipsoid(q - (hc + vec3(0, 0.008, -0.02) * S), vec3(0.07, 0.08, 0.064) * S)
    let v = SDF.smin(e, max(fall, hc.y - 0.15 * S - p.y), 0.02 * S)
    return max(v, (p.z - hc.z) - 0.02 * S)
  }, hc + vec3(0, -0.04, -0.02) * S, 0.16 * S))
  addCrown(m, HB.head, hc + vec3(0, 0.058, -0.006) * S, 0.07 * S, 0.03 * S, 6, false)
  // sceptre: a gold rod with a jewelled orb and cross
  do {
    let g = j.gripR
    m.addR(HB.itemR, MMat.gold, 0, cylS(g - vec3(0, 0.12, 0) * S, g + vec3(0, 0.27, 0) * S, 0.008 * S))
    m.addR(HB.itemR, MMat.gold, 0.004, sphereS(g - vec3(0, 0.125, 0) * S, 0.012 * S))
    let t = g + vec3(0, 0.29, 0) * S
    m.addR(HB.itemR, MMat.gem, 0.003, sphereS(t, 0.022 * S))
    m.addR(HB.itemR, MMat.gold, 0.003, torusS(t, 0.022 * S, 0.004 * S, rotXm(1.5708)))
    m.addR(HB.itemR, MMat.gold, 0.002, boxS(t + vec3(0, 0.04, 0) * S, vec3(0.004, 0.02, 0.004) * S, 0.001))
    m.addR(HB.itemR, MMat.gold, 0.002, boxS(t + vec3(0, 0.045, 0) * S, vec3(0.014, 0.004, 0.004) * S, 0.001))
  }
  return m
}

/// Bishop: mitre, cope and crozier, blessing with his right hand.
private func bishopModel() -> Model {
  let j = HumanJ.seated(MAsset.bishop)
  let S = j.S
  let m = Model()
  m.setSkeleton(Medieval.skeleton(MAsset.bishop))
  seatedBody(m, j, MMat.cloth, MMat.linen, MMat.cloth2, true, MMat.linen)
  // chasuble: a bell of cloth over the shoulders, its gold orphrey a Y-cross
  do {
    let chest = seatedChestField(j)
    let w = j.waist
    m.add(HB.chest, MMat.cloth, 0.02 * S, fnS({ p in
      let yTop = w.y + 0.2 * S
      let spread = max(0, yTop - p.y) * 0.32
      let q = vec3(p.x / (1 + spread / S * 1), max(p.y, yTop - 0.06 * S), p.z)
      let d = chest(q) - 0.014 * S
      return max(d, w.y - 0.02 * S - p.y)
    }, j.waist + vec3(0, 0.15, 0) * S, 0.3 * S))
  }
  m.paint(MMat.gold, boxS(j.waist + vec3(0, 0.1, 0.1) * S, vec3(0.022, 0.16, 0.06) * S, 0), MMat.cloth)
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    m.paint(MMat.gold,
            boxS(j.waist + vec3(x * 0.055, 0.285, 0.07) * S, vec3(0.018, 0.075, 0.08) * S, 0, rotZm(x * 0.75)),
            MMat.cloth)
  }
  m.paint(MMat.gold, boxS(vec3(0, 0.335, 0.24) * S, vec3(0.2, 0.02, 0.06) * S, 0), MMat.cloth)
  // alb sleeves, gloved hands
  addArm(m, true, j, MMat.cloth, 0.046 * S, 0.036 * S, MMat.linen)
  addArm(m, false, j, MMat.cloth, 0.046 * S, 0.036 * S, MMat.linen)
  addFist(m, HB.lFore, j.gripL, vec3(0, 1, 0), 1.3 * S)
  do {  // blessing hand: open palm, two fingers raised
    let g = j.gripR
    m.add(HB.rFore, MMat.skin, 0.008 * S, ellS(g + vec3(0, -0.012, 0) * S, vec3(0.02, 0.028, 0.012) * S))
    m.add(HB.rFore, MMat.skin, 0.004 * S,
          capsuleS(g + vec3(-0.004, 0.01, 0) * S, g + vec3(-0.006, 0.04, 0.002) * S, 0.0055 * S))
    m.add(HB.rFore, MMat.skin, 0.004 * S,
          capsuleS(g + vec3(0.007, 0.01, 0) * S, g + vec3(0.008, 0.038, 0.002) * S, 0.0055 * S))
    m.add(HB.rFore, MMat.gold, 0.002 * S,
          torusS(g + vec3(0.012, -0.005, 0.0) * S, 0.007 * S, 0.0025 * S, rotZm(1.5708)))
  }
  let hc = j.neck + vec3(0, 0.078, 0.012) * S
  addHead(m, HB.head, hc, 1.4 * S, 2, MMat.hair)
  addHair(m, HB.head, hc, 1.4 * S, MMat.hair, 0)
  // mitre: two pointed panels, gold bands, lappets hanging behind
  do {
    let mc = hc + vec3(0, 0.07, -0.006) * S
    let mitre: SDFFn = { p in
      let q = (p - mc) / S
      let h: Float = 0.15
      let t = clamp(q.y / h, 0, 1)
      let w = 0.062 * (1 - pow(t, 2.2)) + 0.004
      let dz = 0.056 * (1 - 0.65 * t)
      let side = abs(q.x) - w
      let depth = abs(q.z) / dz * 0.05 - 0.05
      let d = max(max(side, depth), max(-q.y, q.y - h))
      // cleft between the front and back peaks
      let cleft = max(abs(q.z) - 0.006, 0.07 - q.y)
      return max(d, -cleft) * S
    }
    m.add(HB.head, MMat.cloth, 0.004 * S, fnS(mitre, mc + vec3(0, 0.075, 0) * S, 0.12 * S))
    m.paint(MMat.gold, boxS(mc + vec3(0, 0.012, 0) * S, vec3(0.08, 0.012, 0.08) * S, 0), MMat.cloth)
    m.paint(MMat.gold, boxS(mc + vec3(0, 0.07, 0) * S, vec3(0.011, 0.07, 0.08) * S, 0), MMat.cloth)
    m.add(HB.head, MMat.gem, 0.002 * S, sphereS(mc + vec3(0, 0.04, 0.054) * S, 0.009 * S))
    for sd in stride(from: -1, through: 1, by: 2) {
      m.add(HB.head, MMat.cloth, 0.004 * S,
            boxS(mc + vec3(Float(sd) * 0.03, -0.05, -0.055) * S, vec3(0.012, 0.05, 0.003) * S, 0.002 * S,
                 rotXm(0.2)))
    }
  }
  // crozier: a gold staff ending in a curled crook
  do {
    let g = j.gripL
    m.addR(HB.itemL, MMat.gold, 0, cylS(vec3(g.x, 0.02 * S, g.z), g + vec3(0, 0.62, 0) * S, 0.0085 * S))
    m.addR(HB.itemL, MMat.gold, 0.004, ellS(g + vec3(0, 0.6, 0) * S, vec3(0.02, 0.026, 0.02) * S))
    let cc = g + vec3(0, 0.68, 0.035) * S
    m.addR(HB.itemL, MMat.gold, 0, fnS({ p in
      let q = (p - cc) / S  // crook in the y-z plane, open downward at the front
      let a = atan2(q.y, -q.z)
      let rr = (q.y * q.y + q.z * q.z).squareRoot()
      let spiral = 0.04 - 0.012 * clamp((a + 3.14159) / 6.28318, 0, 1)
      let tube = ((rr - spiral) * (rr - spiral) + q.x * q.x).squareRoot() - 0.0075
      let keep: Float = (q.y < 0 && q.z > 0) ? max(-q.y - 0.03, 0) : 0
      return (tube + keep) * S
    }, cc, 0.06 * S))
    m.addR(HB.itemL, MMat.gold, 0, cylS(g + vec3(0, 0.6, 0) * S, cc + vec3(0, -0.035, -0.035) * S, 0.0078 * S))
  }
  return m
}

// --- thrones ---------------------------------------------------------------
private func throneModel(_ cathedra: Bool) -> Model {
  let m = Model()
  let S = cathedra ? Medieval.seatedScale(MAsset.bishop) : 1
  let W = 0.235 * S, ZF = 0.09 * S, ZB = -0.235 * S
  func post(_ base: vec3, _ h: Float, _ r: Float) {
    m.add(0, MMat.wood, 0.004, boxS(base + vec3(0, h * 0.5, 0), vec3(r, h * 0.5, r), 0.006 * S))
  }
  // four posts with gold finials
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    post(vec3(x * W, 0, ZF), 0.47 * S, 0.026 * S)
    m.add(0, MMat.gold, 0.006, sphereS(vec3(x * W, 0.5 * S, ZF), 0.03 * S))
    m.add(0, MMat.wood, 0.006, ellS(vec3(x * W, 0.025 * S, ZF + 0.006), vec3(0.036, 0.028, 0.04) * S))  // paw foot
    let backH: Float = cathedra ? 1.06 : 1.0
    post(vec3(x * W, 0, ZB), backH * S, 0.028 * S)
    if cathedra {
      // pinnacle: a slender spire with crockets
      let b = vec3(x * W, backH * S, ZB)
      m.add(0, MMat.wood, 0.004, coneS(b, b + vec3(0, 0.2, 0) * S, 0.032 * S, 0.004 * S))
      for k in 0..<3 {
        let kf = Float(k)
        m.add(0, MMat.gold, 0.004, sphereS(b + vec3(0, (0.05 + 0.05 * kf) * S, 0), (0.024 - 0.006 * kf) * S))
      }
      m.add(0, MMat.gold, 0.004, sphereS(b + vec3(0, 0.21, 0) * S, 0.012 * S))
    } else {
      m.add(0, MMat.gold, 0.006, sphereS(vec3(x * W, (backH + 0.035) * S, ZB), 0.034 * S))
      m.add(0, MMat.gold, 0.004, cylS(vec3(x * W, backH * S, ZB), vec3(x * W, (backH + 0.012) * S, ZB), 0.034 * S))
    }
    // armrest and the panel under it
    m.add(0, MMat.wood, 0.006,
          boxS(vec3(x * W, 0.45 * S, (ZF + ZB) * 0.5), vec3(0.03 * S, 0.016 * S, (ZF - ZB) * 0.5), 0.01 * S))
    m.add(0, MMat.wood, 0.004,
          boxS(vec3(x * W, 0.33 * S, (ZF + ZB) * 0.5), vec3(0.012 * S, 0.11 * S, (ZF - ZB) * 0.5), 0.004 * S))
    // tracery arches in the side panel
    for k in 0..<3 {
      let z = ZB + (ZF - ZB) * (Float(k) + 0.5) / 3
      m.sub(MMat.velvet, 0.004, fnS({ p in
        let q = p - vec3(x * W, 0.3 * S, z)
        let arch = max(abs(q.z) - 0.03 * S, q.y - 0.05 * S + 0.6 * abs(q.z))
        return max(max(arch, -q.y - 0.07 * S), abs(q.x) - 0.009 * S)
      }, vec3(x * W, 0.3 * S, z), 0.1 * S))
    }
  }
  // seat, apron and cushion
  m.add(0, MMat.wood, 0.006,
        boxS(vec3(0, 0.245, (ZF + ZB) * 0.5) * vec3(1, S, 1), vec3(W, 0.03 * S, (ZF - ZB) * 0.5), 0.008 * S))
  m.add(0, MMat.wood, 0.004, boxS(vec3(0, 0.18 * S, ZF), vec3(W, 0.04 * S, 0.012 * S), 0.004 * S))
  m.add(0, MMat.velvet, 0.012,
        boxS(vec3(0, 0.29 * S, (ZF + ZB) * 0.5 + 0.01 * S),
             vec3(W - 0.02 * S, 0.022 * S, (ZF - ZB) * 0.5 - 0.015 * S), 0.02 * S))
  // back panel
  let top: Float = cathedra ? 1.0 : 0.93
  m.add(0, MMat.wood, 0.006, fnS({ p in
    let q = p - vec3(0, 0, ZB)
    let box = max(abs(q.x) - W, abs(q.z) - 0.022 * S)
    let y = q.y / S
    // pointed gable rising to a peak in the middle
    let peak = cathedra ? top + 0.18 - 1.2 * abs(q.x) / S : top + 0.1 - 0.5 * abs(q.x) / S
    let lid = (y - min(peak, cathedra ? 1.18 : 1.04)) * S
    return max(max(box, lid), (0.24 - y) * S)
  }, vec3(0, 0.65 * S, ZB), 0.55 * S))
  // velvet inset with a gold frame
  m.add(0, MMat.velvet, 0.008,
        boxS(vec3(0, 0.62 * S, ZB + 0.024 * S), vec3(0.175 * S, 0.27 * S, 0.006 * S), 0.012 * S))
  m.add(0, MMat.gold, 0, fnS({ p in
    let q = p - vec3(0, 0.62 * S, ZB + 0.024 * S)
    let outer = SDF.roundBox(q, vec3(0.188, 0.283, 0.008) * S, 0.014 * S)
    let inner = SDF.roundBox(q, vec3(0.172, 0.267, 0.03) * S, 0.012 * S)
    return max(outer, -inner)
  }, vec3(0, 0.62 * S, ZB), 0.36 * S))
  if cathedra {
    // a gold cross atop the gable and a trefoil window in it
    let apex = vec3(0, 1.2 * S, ZB)
    m.add(0, MMat.gold, 0.003, boxS(apex + vec3(0, 0.05, 0) * S, vec3(0.007, 0.05, 0.007) * S, 0.002))
    m.add(0, MMat.gold, 0.003, boxS(apex + vec3(0, 0.065, 0) * S, vec3(0.03, 0.007, 0.007) * S, 0.002))
    for k in 0..<3 {
      let a = 6.2831853 * Float(k) / 3
      let a0 = vec3(sin(a) * 0.022, 1.03 + cos(a) * 0.022, -0.3) * vec3(S, S, 1) + vec3(0, 0, ZB + 0.3 - 0.3)
      let a1 = vec3(sin(a) * 0.022, 1.03 + cos(a) * 0.022, 0) * vec3(S, S, 1) + vec3(0, 0, ZB + 0.06)
      m.sub(MMat.dark, 0.004, cylS(a0, a1, 0.016 * S))
    }
  } else {
    // royal arms: a gold crown carved on the crest
    let c = vec3(0, 0.985 * S, ZB + 0.02 * S)
    m.add(0, MMat.gold, 0.004, boxS(c, vec3(0.06, 0.016, 0.012) * S, 0.004 * S))
    for k in -1...1 { m.add(0, MMat.gold, 0.004, sphereS(c + vec3(Float(k) * 0.045, 0.03, 0) * S, 0.012 * S)) }
  }
  return m
}

// --- knight ----------------------------------------------------------------
private let KS = KJ.KS
@inline(__always) private func K(_ x: Float, _ y: Float, _ z: Float) -> vec3 { vec3(x, y, z) * KS }

private func horseBodyField() -> SDFFn {
  { p in
    let barrel = SDF.ellipsoid(p - K(0, 0.53, -0.01), vec3(0.135, 0.155, 0.27) * KS)
    let chest = SDF.ellipsoid(p - K(0, 0.54, 0.2), vec3(0.125, 0.16, 0.13) * KS)
    let haunch = SDF.ellipsoid(p - K(0, 0.56, -0.21), vec3(0.14, 0.165, 0.15) * KS)
    return SDF.smin(SDF.smin(barrel, chest, 0.08 * KS), haunch, 0.08 * KS)
  }
}

private func knightModel() -> Model {
  let m = Model()
  m.setSkeleton(Medieval.skeleton(MAsset.knight))
  let bodyF = horseBodyField()
  m.add(KB.body, MMat.horse, 0, fnS(bodyF, K(0, 0.55, 0), 0.42 * KS))
  // neck and head
  m.add(KB.neck, MMat.horse, 0.06 * KS, coneS(K(0, 0.6, 0.24), K(0, 0.86, 0.39), 0.1 * KS, 0.06 * KS))
  m.add(KB.head, MMat.horse, 0.04 * KS, ellS(K(0, 0.885, 0.43), vec3(0.05, 0.06, 0.07) * KS, rotXm(0.6)))
  m.add(KB.head, MMat.horse, 0.05 * KS, ellS(K(0, 0.82, 0.5), vec3(0.046, 0.052, 0.08) * KS, rotXm(0.8)))
  m.add(KB.head, MMat.horse, 0.03 * KS, ellS(K(0, 0.755, 0.565), vec3(0.04, 0.045, 0.05) * KS))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    m.add(KB.head, MMat.horse, 0.01 * KS,
          coneS(K(x * 0.025, 0.94, 0.4), K(x * 0.032, 1.0, 0.38), 0.016 * KS, 0.004 * KS))
    m.add(KB.head, MMat.dark, 0.002, sphereS(K(x * 0.044, 0.875, 0.47), 0.011 * KS))
    m.paint(MMat.dark, sphereS(K(x * 0.022, 0.75, 0.61), 0.011 * KS), MMat.horse)  // nostrils
  }
  // mane and forelock
  m.add(KB.neck, MMat.mane, 0.02 * KS, fnS({ p in
    let q = vec3(p.x * 2.2, p.y, p.z)
    return SDF.capsule(q, K(0, 0.94, 0.37), K(0, 0.7, 0.17), 0.026 * KS) / 2.2
  }, K(0, 0.82, 0.27), 0.2 * KS))
  m.add(KB.head, MMat.mane, 0.01 * KS, ellS(K(0, 0.93, 0.445), vec3(0.015, 0.025, 0.02) * KS))
  // tail
  m.add(KB.tail, MMat.mane, 0.03 * KS, coneS(K(0, 0.66, -0.35), K(0, 0.5, -0.42), 0.032 * KS, 0.038 * KS))
  m.add(KB.tail, MMat.mane, 0.03 * KS, coneS(K(0, 0.5, -0.42), K(0, 0.3, -0.44), 0.038 * KS, 0.02 * KS))
  // legs: front (forearm, knee, cannon, fetlock, hoof) and hind (gaskin, hock, cannon ...)
  for sd in stride(from: -1, through: 1, by: 2) {
    let L = sd > 0
    let fu = L ? KJ.flu : mirrorXv(KJ.flu), fl = L ? KJ.fll : mirrorXv(KJ.fll)
    let hu = L ? KJ.hlu : mirrorXv(KJ.hlu), hl = L ? KJ.hll : mirrorXv(KJ.hll)
    let bFU = L ? KB.flu : KB.fru, bFL = L ? KB.fll : KB.frl, bHU = L ? KB.hlu : KB.hru, bHL = L ? KB.hll : KB.hrl
    let x = fu.x
    m.add(bFU, MMat.horse, 0.03 * KS, coneS(fu, fl, 0.05 * KS, 0.034 * KS))
    m.add(bFL, MMat.horse, 0.02 * KS, coneS(fl, vec3(x, 0.075 * KS, fl.z + 0.008 * KS), 0.033 * KS, 0.024 * KS))
    m.add(bFL, MMat.horse, 0.012 * KS, ellS(vec3(x, 0.07 * KS, fl.z + 0.012 * KS), vec3(0.03, 0.032, 0.034) * KS))
    m.add(bFL, MMat.leather, 0.006 * KS,
          coneS(vec3(x, 0.045 * KS, fl.z + 0.022 * KS), vec3(x, 0.006 * KS, fl.z + 0.03 * KS), 0.027 * KS,
                0.036 * KS))
    let hx = hu.x
    m.add(bHU, MMat.horse, 0.04 * KS, coneS(hu + K(0, -0.02, 0.03), hl, 0.07 * KS, 0.036 * KS))
    m.add(bHL, MMat.horse, 0.02 * KS, coneS(hl, vec3(hx, 0.075 * KS, hl.z + 0.035 * KS), 0.034 * KS, 0.024 * KS))
    m.add(bHL, MMat.horse, 0.012 * KS, ellS(vec3(hx, 0.07 * KS, hl.z + 0.04 * KS), vec3(0.03, 0.032, 0.034) * KS))
    m.add(bHL, MMat.leather, 0.006 * KS,
          coneS(vec3(hx, 0.045 * KS, hl.z + 0.05 * KS), vec3(hx, 0.006 * KS, hl.z + 0.058 * KS), 0.027 * KS,
                0.036 * KS))
  }
  // caparison: cloth draped over the horse down to the knees, with a dagged hem
  m.add(KB.body, MMat.cloth, 0.01 * KS, fnS({ p in
    let yc = 0.55 * KS
    let q = vec3(p.x, max(p.y, yc), p.z)
    var d = bodyF(q) - 0.016 * KS
    d = max(d, -(d + 0.03 * KS))
    let a = atan2(p.x, p.z * 0.45)
    let hem = 0.31 * KS + 0.016 * KS * abs(sin(a * 9))
    return max(d, hem - p.y)
  }, K(0, 0.5, 0), 0.44 * KS))
  m.paint(MMat.cloth2, fnS({ p in abs(p.y - 0.355 * KS) - 0.022 * KS }, K(0, 0.35, 0), 0.5), MMat.cloth)
  for sd in stride(from: -1, through: 1, by: 2) {  // a great cross on each flank
    let x = Float(sd)
    m.paint(MMat.cloth2, boxS(K(x * 0.16, 0.47, -0.02), vec3(0.1, 0.022, 0.09) * KS, 0), MMat.cloth)
    m.paint(MMat.cloth2, boxS(K(x * 0.16, 0.47, -0.02), vec3(0.1, 0.09, 0.022) * KS, 0), MMat.cloth)
  }
  // chanfron (face armour) and bridle
  m.add(KB.head, MMat.steel, 0.004 * KS, fnS({ p in
    let c = K(0, 0.83, 0.49)
    let e = SDF.ellipsoid(p - c, vec3(0.056, 0.066, 0.105) * KS)
    let shell = max(e, -(e + 0.006 * KS))
    return max(shell, max(0.79 * KS - p.y + (p.z - c.z) * 0.6, -p.y + 0))
  }, K(0, 0.85, 0.5), 0.14 * KS))
  m.add(KB.head, MMat.gold, 0.003, sphereS(K(0, 0.9, 0.475), 0.012 * KS))
  // saddle
  m.add(KB.body, MMat.leather, 0.02 * KS, ellS(K(0, 0.705, -0.04), vec3(0.1, 0.03, 0.13) * KS))
  m.add(KB.body, MMat.leather, 0.02 * KS, ellS(K(0, 0.74, -0.155), vec3(0.08, 0.05, 0.025) * KS))
  m.add(KB.body, MMat.leather, 0.02 * KS, ellS(K(0, 0.73, 0.075), vec3(0.06, 0.04, 0.025) * KS))
  // --- rider: legs clamp the horse; torso, arms and helm move on their own
  m.add(KB.rider, MMat.mail, 0.03 * KS, ellS(K(0, 0.78, -0.05), vec3(0.09, 0.06, 0.08) * KS))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    m.add(KB.body, MMat.mail, 0.02 * KS,
          coneS(K(x * 0.07, 0.78, -0.03), K(x * 0.155, 0.66, 0.08), 0.045 * KS, 0.037 * KS))
    m.add(KB.body, MMat.steel, 0.006 * KS, ellS(K(x * 0.158, 0.665, 0.095), vec3(0.032, 0.034, 0.026) * KS))
    m.add(KB.body, MMat.mail, 0.012 * KS,
          coneS(K(x * 0.155, 0.66, 0.08), K(x * 0.16, 0.475, 0.035), 0.034 * KS, 0.027 * KS))
    m.add(KB.body, MMat.steel, 0.008 * KS, boxS(K(x * 0.16, 0.45, 0.06), vec3(0.024, 0.02, 0.05) * KS, 0.015 * KS))
    m.add(KB.body, MMat.iron, 0.002, torusS(K(x * 0.16, 0.43, 0.06), 0.034 * KS, 0.005 * KS, rotZm(1.5708)))
    m.add(KB.body, MMat.leather, 0.002, capsuleS(K(x * 0.12, 0.7, -0.04), K(x * 0.16, 0.44, 0.06), 0.004 * KS))
  }
  // surcoat over mail, with a cross
  m.add(KB.rider, MMat.mail, 0.05 * KS, ellS(K(0, 0.93, -0.04), vec3(0.1, 0.12, 0.068) * KS))
  m.add(KB.rider, MMat.cloth, 0.04 * KS, fnS({ p in
    let c = SDF.ellipsoid(p - K(0, 0.925, -0.04), vec3(0.106, 0.125, 0.074) * KS)
    let skirt = SDF.roundCone(p, K(0, 0.86, -0.04), K(0, 0.71, -0.02), 0.1 * KS, 0.16 * KS)
    let sc = max(skirt, -(skirt + 0.012 * KS))
    return SDF.smin(max(c, 0.84 * KS - p.y), max(sc, 0.68 * KS - p.y), 0.02 * KS)
  }, K(0, 0.85, -0.04), 0.25 * KS))
  m.paint(MMat.cloth2, boxS(K(0, 0.92, 0.04), vec3(0.016, 0.08, 0.05) * KS, 0), MMat.cloth)
  m.paint(MMat.cloth2, boxS(K(0, 0.95, 0.04), vec3(0.06, 0.016, 0.05) * KS, 0), MMat.cloth)
  m.add(KB.rider, MMat.leather, 0.004, torusS(K(0, 0.835, -0.035), 0.088 * KS, 0.008 * KS))
  // arms in mail with steel gauntlets
  for sd in stride(from: -1, through: 1, by: 2) {
    let L = sd > 0
    let s0 = L ? KJ.shL : KJ.shR, e0 = L ? KJ.elL : KJ.elR, w0 = L ? KJ.wrL : KJ.wrR
    let ua = L ? KB.lArm : KB.rArm, fa = L ? KB.lFore : KB.rFore
    m.add(ua, MMat.mail, 0.02 * KS, coneS(s0, e0, 0.036 * KS, 0.03 * KS))
    m.add(ua, MMat.steel, 0.01 * KS, ellS(s0 + K(Float(sd) * 0.012, 0.012, 0), vec3(0.045, 0.035, 0.045) * KS))  // pauldron
    m.add(fa, MMat.mail, 0.012 * KS, coneS(e0, w0, 0.029 * KS, 0.024 * KS))
    m.add(fa, MMat.steel, 0.008 * KS, ellS(L ? KJ.gripL : KJ.gripR, vec3(0.024, 0.028, 0.026) * KS))
  }
  // great helm with an eye slit, a gold cross and a crest
  do {
    let hc = K(0, 1.1, -0.035)
    m.add(KB.rHead, MMat.steel, 0.008 * KS, fnS({ p in
      let q = p - hc
      let r = (q.x * q.x + q.z * q.z * 1.15).squareRoot()
      let cyl = max(r - 0.058 * KS, abs(q.y) - 0.07 * KS)
      let top = SDF.ellipsoid(q - vec3(0, 0.06, 0) * KS, vec3(0.058, 0.025, 0.055) * KS)
      return SDF.smin(cyl, top, 0.01 * KS)
    }, hc, 0.11 * KS))
    m.paint(MMat.dark, boxS(hc + K(0, 0.012, 0.06), vec3(0.05, 0.005, 0.05) * KS, 0), MMat.steel)
    m.paint(MMat.gold, boxS(hc + K(0, -0.02, 0.06), vec3(0.007, 0.03, 0.05) * KS, 0), MMat.steel)
    m.paint(MMat.gold, boxS(hc + K(0, 0.0, 0.06), vec3(0.03, 0.006, 0.05) * KS, 0), MMat.steel)
    for k in 0..<3 {
      for l in 0..<2 {
        let c = hc + K(-0.03 - 0.012 * Float(l), -0.025 - 0.014 * Float(k), 0.058)
        m.paint(MMat.dark, sphereS(c, 0.0035 * KS), MMat.steel)
      }
    }
    // a twisted torse and a plume in the army's colours
    m.add(KB.rHead, MMat.cloth2, 0.004, torusS(hc + K(0, 0.072, 0), 0.05 * KS, 0.01 * KS))
    m.add(KB.rHead, MMat.cloth, 0.01 * KS, coneS(hc + K(0, 0.085, 0.0), hc + K(0, 0.15, -0.05), 0.02 * KS, 0.006 * KS))
    m.add(KB.rHead, MMat.mail, 0.02 * KS, coneS(K(0, 0.99, -0.045), K(0, 1.04, -0.04), 0.07 * KS, 0.05 * KS))
  }
  // heater shield on the left arm
  do {
    let c = K(0.175, 0.86, 0.03)
    let R = rotYm(1.15)
    let Rt = R.transpose
    let plate: SDFFn = { p in
      let q = Rt * (p - c)
      let bulge = 0.02 * KS * (q.x / (0.1 * KS)) * (q.x / (0.1 * KS))
      let d2 = shield2D(vec2(q.x, q.y), 0.19 * KS, 0.25 * KS, 1.4)
      return max(d2, abs(q.z + bulge) - 0.007 * KS)
    }
    m.add(KB.lFore, MMat.cloth2, 0, fnS(plate, c, 0.16 * KS))
    m.paint(MMat.cloth, fnS({ p in
      let q = Rt * (p - c)
      let cross = min(abs(q.x) - 0.016 * KS, abs(q.y - 0.04 * KS) - 0.016 * KS)
      return max(cross, plate(p) - 0.004)
    }, c, 0.2), MMat.cloth2)
    m.add(KB.lFore, MMat.gold, 0, fnS({ p in
      let q = Rt * (p - c)
      let bulge = 0.02 * KS * (q.x / (0.1 * KS)) * (q.x / (0.1 * KS))
      let d2 = shield2D(vec2(q.x, q.y), 0.197 * KS, 0.258 * KS, 1.4)
      return max(abs(d2 + 0.004 * KS) - 0.004 * KS, abs(q.z + bulge) - 0.009 * KS)
    }, c, 0.17 * KS))
  }
  // reins from the bit to the left hand (they stretch between the horse's head and the hand)
  for sd in stride(from: -1, through: 1, by: 2) {
    let bit = K(Float(sd) * 0.04, 0.76, 0.56), hand = KJ.gripL + K(-0.02 * Float(sd + 1), 0, 0)
    m.addStretch(KB.head, KB.lFore, MMat.leather, 0.002, capsuleS(bit, hand, 0.004 * KS), along(bit, hand))
  }
  // lance: striped shaft, steel vamplate and point, a pennon
  do {
    let g = KJ.gripR
    let butt = g - K(0, 0.22, 0), tip = g + K(0, 0.74, 0)
    m.addR(KB.lance, MMat.wood, 0, fnS({ p in
      let ab = tip - butt
      let t = clamp(dot(p - butt, ab) / dot(ab, ab), 0, 1)
      let r = mix(0.013, 0.007, t) * KS
      return length(p - (butt + ab * t)) - r
    }, (butt + tip) * 0.5, length(tip - butt) * 0.5 + 0.02))
    m.paint(MMat.cloth2, fnS({ p in
      let q = p - g
      let a = atan2(q.x, q.z)
      let band: Float = sin(q.y / KS * 60 + a * 2)
      return max(-band, max(length(vec2(q.x, q.z)) - 0.02 * KS, -(q.y - 0.12 * KS)))
    }, g + K(0, 0.4, 0), 0.5), MMat.wood)
    m.addR(KB.lance, MMat.steel, 0, coneS(g + K(0, 0.035, 0), g + K(0, 0.1, 0), 0.008 * KS, 0.034 * KS))
    m.addR(KB.lance, MMat.steel, 0, coneS(tip, tip + K(0, 0.07, 0), 0.011 * KS, 0.001))
    let pc = tip + K(0, -0.06, 0)
    m.addR(KB.lance, MMat.cloth2, 0, fnS({ p in
      let q = (p - pc) / KS  // the pennon streams forward (+z) from the shaft
      let len: Float = 0.12
      let half = 0.03 * (1 - 0.5 * q.z / len)
      let notch: Float = q.z > len * 0.6 ? 0.4 * (q.z - len * 0.6) - abs(q.y) : -1
      let d = max(max(abs(q.y) - half, max(-q.z, q.z - len)), abs(q.x) - 0.0025)
      return max(d, notch) * KS
    }, pc + K(0, 0, 0.06), 0.08 * KS))
    m.paint(MMat.cloth, fnS({ p in abs((p.y - pc.y) / KS) - 0.007 }, pc, 0.15), MMat.cloth2)
  }
  return m
}

// --- the gatehouse -----------------------------------------------------------
private func castleModel() -> Model {
  let m = Model()
  m.setSkeleton(Medieval.skeleton(MAsset.rook))
  let TX: Float = 0.215, TZ: Float = 0.1, TR: Float = 0.13, TH: Float = 0.9  // flanking towers
  // central block on a battered plinth
  m.add(CB.body, MMat.stone, 0.004, boxS(vec3(0, 0.40, -0.045), vec3(0.2, 0.4, 0.205), 0.006))
  m.add(CB.body, MMat.stone, 0.01, fnS({ p in
    let q = p - vec3(0, 0, -0.045)
    let flare = 0.03 * clamp(1 - p.y / 0.09, 0, 1)
    return max(max(abs(q.x) - 0.2 - flare, abs(q.z) - 0.205 - flare), max(-p.y, p.y - 0.09))
  }, vec3(0, 0.05, -0.045), 0.32))
  // parapet with crenels, on a row of corbels
  m.add(CB.body, MMat.stone, 0.004, fnS({ p in
    let q = p - vec3(0, 0.845, -0.045)
    let outer = SDF.roundBox(q, vec3(0.218, 0.045, 0.223), 0.004)
    let inner = SDF.roundBox(q - vec3(0, 0.03, 0), vec3(0.19, 0.05, 0.195), 0.002)
    let d = max(outer, -inner)
    // crenels along each wall
    let u = abs(q.x) > abs(q.z) - 0.005 ? q.z : q.x
    let cren = max(abs((u + 10).truncatingRemainder(dividingBy: 0.075) - 0.0375) - 0.017, 0.012 - q.y)
    return max(d, -cren)
  }, vec3(0, 0.845, -0.045), 0.33))
  for i in -2...2 {
    let x = Float(i) * 0.075
    m.add(CB.body, MMat.stone, 0.004, boxS(vec3(x, 0.785, 0.17), vec3(0.016, 0.02, 0.016), 0.003))
    m.add(CB.body, MMat.stone, 0.004, boxS(vec3(x, 0.785, -0.26), vec3(0.016, 0.02, 0.016), 0.003))
  }
  // round towers either side of the gate
  for sd in stride(from: -1, through: 1, by: 2) {
    let c = vec3(Float(sd) * TX, 0, TZ)
    m.add(CB.body, MMat.stone, 0.01, fnS({ p in
      let q = p - c
      let r = (q.x * q.x + q.z * q.z).squareRoot()
      let flare = 0.03 * clamp(1 - p.y / 0.1, 0, 1)
      let body = max(r - TR - flare, max(-p.y, p.y - TH))
      // parapet ring with crenels
      var ring = max(max(r - TR - 0.014, TR - 0.02 - r), max(TH - 0.09 - p.y, p.y - TH - 0.07))
      let a = atan2(q.x, q.z)
      let cren = max(abs((a * TR + 10).truncatingRemainder(dividingBy: 0.072) - 0.036) - 0.016, TH + 0.025 - p.y)
      ring = max(ring, -cren)
      let top = max(r - TR + 0.005, abs(p.y - TH + 0.01) - 0.01)
      return min(min(body, ring), top)
    }, c + vec3(0, 0.5, 0), 0.6))
    // corbels under the tower parapet
    for k in 0..<10 {
      let a = 6.2831853 * Float(k) / 10
      let d = vec3(sin(a), 0, cos(a))
      m.add(CB.body, MMat.stone, 0.004,
            boxS(c + d * (TR + 0.004) + vec3(0, TH - 0.105, 0), vec3(0.012, 0.018, 0.012), 0.003, rotYm(a)))
    }
    // arrow slits (cross loops) facing out and forward
    for k in 0..<2 {
      let a = Float(sd) * (0.4 + 1.1 * Float(k))
      let d = vec3(sin(a), 0, cos(a))
      let s = c + d * TR + vec3(0, 0.48 - 0.2 * Float(k), 0)
      let R = rotYm(a)
      m.sub(MMat.dark, 0.002, boxS(s, vec3(0.006, 0.05, 0.03), 0.001, R))
      m.sub(MMat.dark, 0.002, boxS(s + vec3(0, 0.012, 0), vec3(0.022, 0.006, 0.03), 0.001, R))
    }
  }
  // the gate: a pointed arch, its dark passage and the portcullis inside
  let arch: SDFFn = { p in
    let x = abs(p.x), y = p.y
    let w: Float = 0.085
    let sides = x - w
    // pointed arch from two circles of radius 1.3 w
    let R: Float = 0.12
    let cx = w - R
    let springY: Float = 0.2
    let circ = ((x - cx) * (x - cx) + (y - springY) * (y - springY)).squareRoot() - R
    let d = y < springY ? sides : circ
    return max(d, -y)
  }
  m.sub(MMat.stone, 0.002, fnS({ p in max(arch(p), abs(p.z - 0.16) - 0.06) }, vec3(0, 0.16, 0.16), 0.25))
  m.add(CB.body, MMat.dark, 0, boxS(vec3(0, 0.17, 0.098), vec3(0.09, 0.17, 0.004), 0))
  // voussoirs: a ring of stones around the arch
  m.add(CB.body, MMat.stone, 0.002, fnS({ p in
    let a = arch(p)
    return max(max(a - 0.018, -a), abs(p.z - 0.165) - 0.006)
  }, vec3(0, 0.18, 0.165), 0.24))
  // portcullis grid (slides up into the wall)
  m.addR(CB.gate, MMat.iron, 0, fnS({ p in
    let q = p - vec3(0, 0, 0.122)
    let bx = abs((q.x + 1).truncatingRemainder(dividingBy: 0.034) - 0.017)
    let by = abs((q.y + 1).truncatingRemainder(dividingBy: 0.045) - 0.0225)
    let bars = min(bx, by) - 0.0035
    let plane = abs(q.z) - 0.0045
    let inside = arch(p) + 0.004
    return max(max(bars, plane), inside)
  }, vec3(0, 0.17, 0.122), 0.22))
  // drawbridge (hinged at its foot), planks and iron straps
  do {
    let bw: Float = 0.105, bh: Float = 0.355, bz: Float = 0.18
    m.addR(CB.bridge, MMat.wood, 0, fnS({ p in
      let q = p - vec3(0, bh * 0.5 + 0.004, bz)
      let b = SDF.roundBox(q, vec3(bw, bh * 0.5, 0.011), 0.003)
      let plank = abs((q.x + 1).truncatingRemainder(dividingBy: 0.035) - 0.0175) - 0.0012
      return max(b, -max(plank, -(q.z - 0.009)))
    }, vec3(0, bh * 0.5, bz), 0.2))
    for k in 0..<3 {
      m.addR(CB.bridge, MMat.iron, 0,
             boxS(vec3(0, 0.05 + Float(k) * 0.13, bz + 0.011), vec3(bw - 0.006, 0.008, 0.003), 0.002))
    }
    // chains from the top corners to the wall above the arch: they stretch as the bridge lowers
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let a = vec3(x * 0.095, bh - 0.01, bz + 0.012), b = vec3(x * 0.11, 0.52, 0.155)
      m.addStretch(CB.bridge, CB.body, MMat.iron, 0.002, capsuleS(a, b, 0.0045), along(a, b))
      m.add(CB.body, MMat.dark, 0.002, sphereS(b + vec3(0, 0, 0.006), 0.01))
    }
  }
  // flag on the left tower
  do {
    let base = vec3(TX, TH, TZ)
    m.addR(CB.flag, MMat.wood, 0, cylS(base, base + vec3(0, 0.3, 0), 0.006))
    m.addR(CB.flag, MMat.gold, 0.002, sphereS(base + vec3(0, 0.305, 0), 0.011))
    let fc = base + vec3(0, 0.25, 0)
    m.addR(CB.flag, MMat.cloth2, 0, fnS({ p in
      let q = p - fc  // the banner streams outward (+x) from the pole
      let len: Float = 0.16
      let wave = 0.01 * sin(q.x * 38) * clamp(q.x / 0.04, 0, 1)
      let notch: Float = q.x > len * 0.65 ? 0.5 * (q.x - len * 0.65) - abs(q.y) : -1
      let d = max(max(abs(q.y) - 0.045, max(-q.x, q.x - len)), abs(q.z - wave) - 0.003)
      return max(d, notch)
    }, fc + vec3(0.08, 0, 0), 0.1))
    m.paint(MMat.cloth, fnS({ p in
      let q = p - fc
      return min(abs(q.y) - 0.01, abs(q.x - 0.055) - 0.01)
    }, fc + vec3(0.06, 0, 0), 0.12), MMat.cloth2)
  }
  return m
}

/// The gatehouse's stone legs, sculpted standing (hips just under the tower floor).
private func castleLegsModel() -> Model {
  let m = Model()
  m.setSkeleton(Medieval.skeleton(MAsset.legs))
  for sd in stride(from: -1, through: 1, by: 2) {
    let L = sd > 0
    let hip = L ? CJ.hipL : mirrorXv(CJ.hipL), knee = L ? CJ.kneeL : mirrorXv(CJ.kneeL)
    let ankle = L ? CJ.ankleL : mirrorXv(CJ.ankleL)
    let th = L ? CB.lThigh : CB.rThigh, sh = L ? CB.lShin : CB.rShin
    // rough-hewn blocks, a boulder at the knee and a broad stone foot
    m.add(th, MMat.stone, 0.012,
          boxS(hip * 0.6 + knee * 0.4 + vec3(0, -0.01, 0), vec3(0.07, 0.07, 0.07), 0.02, frameY(hip - knee)))
    m.add(th, MMat.stone, 0.012, boxS(hip * 0.15 + knee * 0.85, vec3(0.06, 0.05, 0.062), 0.018, frameY(hip - knee)))
    m.add(sh, MMat.stone, 0.012, sphereS(knee, 0.065))
    m.add(sh, MMat.stone, 0.012,
          boxS(knee * 0.55 + ankle * 0.45, vec3(0.055, 0.08, 0.055), 0.016, frameY(knee - ankle)))
    m.add(sh, MMat.stone, 0.012, boxS(ankle + vec3(0, -0.005, 0), vec3(0.05, 0.04, 0.05), 0.015))
    m.add(sh, MMat.stone, 0.01,
          boxS(vec3(ankle.x, ankle.y - 0.04, ankle.z + 0.04), vec3(0.058, 0.032, 0.105), 0.02))
    for t in -1...1 {
      m.add(sh, MMat.stone, 0.008,
            ellS(vec3(ankle.x + Float(t) * 0.035, ankle.y - 0.05, ankle.z + 0.14), vec3(0.018, 0.02, 0.022)))
    }
  }
  return m
}

// --- the dragon -----------------------------------------------------------------
@inline(__always) private func dot2(_ v: vec3) -> Float { dot(v, v) }
private func triangleDist(_ p: vec3, _ a: vec3, _ b: vec3, _ c: vec3) -> Float {
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
private func triS(_ a: vec3, _ b: vec3, _ c: vec3, _ t: Float) -> Shape {
  let m = (a + b + c) / 3
  let r = max(length(a - m), max(length(b - m), length(c - m))) + t
  return Shape(f: { p in triangleDist(p, a, b, c) - t }, c: m, r: r)
}

private func dragonModel() -> Model {
  let m = Model()
  m.setSkeleton(Medieval.skeleton(MAsset.dragon))
  // body and belly
  m.add(DB.body, MMat.scale, 0, ellS(vec3(0, 0.35, -0.02), vec3(0.155, 0.15, 0.28)))
  m.add(DB.body, MMat.scale, 0.08, ellS(vec3(0, 0.38, 0.13), vec3(0.14, 0.15, 0.14)))
  m.paint(MMat.horn, fnS({ p in  // ribbed belly plates
    let band = abs((p.z + 10).truncatingRemainder(dividingBy: 0.04) - 0.02) - 0.016
    let region = max(max(p.y - 0.27, abs(p.x) - 0.085), abs(p.z - 0.04) - 0.27)
    return max(region, band)
  }, vec3(0, 0.25, 0), 0.4), MMat.scale)
  // neck: three segments
  m.add(DB.neck1, MMat.scale, 0.05, coneS(vec3(0, 0.4, 0.17), DJ.neck2, 0.1, 0.078))
  m.add(DB.neck2, MMat.scale, 0.04, coneS(DJ.neck2, DJ.neck3, 0.078, 0.064))
  m.add(DB.neck3, MMat.scale, 0.04, coneS(DJ.neck3, DJ.head + vec3(0, 0.02, 0.02), 0.062, 0.055))
  // head: skull, long snout, brows, horns, glowing eyes, nostrils, teeth
  m.add(DB.head, MMat.scale, 0.03, ellS(vec3(0, 0.705, 0.54), vec3(0.058, 0.05, 0.07)))
  m.add(DB.head, MMat.scale, 0.035, coneS(vec3(0, 0.695, 0.57), vec3(0, 0.672, 0.7), 0.045, 0.028))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    m.add(DB.head, MMat.scale, 0.015, ellS(vec3(x * 0.038, 0.735, 0.57), vec3(0.022, 0.012, 0.03), rotXm(-0.3)))
    m.add(DB.head, MMat.horn, 0.008, coneS(vec3(x * 0.035, 0.735, 0.5), vec3(x * 0.07, 0.79, 0.4), 0.014, 0.006))
    m.add(DB.head, MMat.horn, 0.004, coneS(vec3(x * 0.07, 0.79, 0.4), vec3(x * 0.075, 0.8, 0.33), 0.006, 0.0015))
    m.add(DB.head, MMat.horn, 0.004, coneS(vec3(x * 0.05, 0.69, 0.5), vec3(x * 0.09, 0.7, 0.45), 0.009, 0.002))
    m.sub(MMat.scale, 0.004, sphereS(vec3(x * 0.044, 0.716, 0.592), 0.015))
    m.add(DB.head, MMat.eye, 0.002, sphereS(vec3(x * 0.041, 0.716, 0.59), 0.0125))
    m.paint(MMat.dark, sphereS(vec3(x * 0.016, 0.69, 0.722), 0.007), MMat.scale)
    for t in 0..<4 {  // upper fangs
      let tf = Float(t)
      let r = vec3(x * (0.028 - tf * 0.003), 0.655, 0.6 + tf * 0.03)
      m.add(DB.head, MMat.horn, 0.002, coneS(r, r + vec3(0, -0.022 + tf * 0.003, 0.003), 0.005, 0.0012))
    }
  }
  // lower jaw with its own teeth; the inside of the mouth is dark red
  m.add(DB.jaw, MMat.scale, 0.02, coneS(DJ.jaw + vec3(0, -0.005, 0), vec3(0, 0.632, 0.69), 0.03, 0.02))
  for sd in stride(from: -1, through: 1, by: 2) {
    for t in 0..<3 {
      let tf = Float(t)
      let r = vec3(Float(sd) * (0.02 - tf * 0.003), 0.648, 0.62 + tf * 0.025)
      m.add(DB.jaw, MMat.horn, 0.002, coneS(r, r + vec3(0, 0.018, 0), 0.004, 0.001))
    }
  }
  m.paint(MMat.dark, fnS({ p in
    max(max(abs(p.y - 0.652) - 0.012, 0.58 - p.z), abs(p.x) - 0.03)
  }, vec3(0, 0.65, 0.65), 0.1), MMat.scale)
  // spines along the back, from the head to the tail
  for i in 0..<14 {
    let t = Float(i) / 13
    var a: vec3
    if t < 0.35 {
      let u = t / 0.35
      a = mix(vec3(0, 0.74, 0.47), vec3(0, 0.49, 0.17), u)
    } else {
      let u = (t - 0.35) / 0.65
      a = mix(vec3(0, 0.49, 0.12), vec3(0, 0.22, -0.72), u)
    }
    let h = 0.045 * (1 - 0.6 * abs(t - 0.4))
    let bone = t < 0.12 ? DB.neck3 : (t < 0.24 ? DB.neck2 : (t < 0.33 ? DB.neck1 : (t < 0.6 ? DB.body :
      (t < 0.75 ? DB.tail1 : DB.tail2))))
    m.add(bone, MMat.horn, 0.006, coneS(a + vec3(0, -0.02, 0), a + vec3(0, h, -0.02), 0.014, 0.002))
  }
  // tail ending in a spade
  m.add(DB.tail1, MMat.scale, 0.04, coneS(vec3(0, 0.35, -0.2), DJ.tail2, 0.085, 0.06))
  m.add(DB.tail2, MMat.scale, 0.03, coneS(DJ.tail2, DJ.tail3, 0.06, 0.042))
  m.add(DB.tail3, MMat.scale, 0.02, coneS(DJ.tail3, DJ.tail4, 0.042, 0.026))
  m.add(DB.tail4, MMat.scale, 0.015, coneS(DJ.tail4, vec3(0, 0.08, -0.9), 0.026, 0.012))
  m.add(DB.tail4, MMat.horn, 0.006, fnS({ p in
    let q = p - vec3(0, 0.075, -0.93)
    let diamond: Float = abs(q.x) / 0.05 + abs(q.z + 0.005) / 0.06 - 1
    return max(diamond * 0.035, abs(q.y) - 0.006)
  }, vec3(0, 0.075, -0.93), 0.07))
  // legs with claws
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let f = sd > 0 ? DB.fl : DB.fr, h = sd > 0 ? DB.hl : DB.hr
    m.add(f, MMat.scale, 0.03, coneS(vec3(x * 0.11, 0.32, 0.14), vec3(x * 0.14, 0.15, 0.19), 0.058, 0.04))
    m.add(f, MMat.scale, 0.02, coneS(vec3(x * 0.14, 0.15, 0.19), vec3(x * 0.14, 0.03, 0.2), 0.04, 0.032))
    m.add(h, MMat.scale, 0.04, ellS(vec3(x * 0.125, 0.3, -0.12), vec3(0.075, 0.1, 0.095)))
    m.add(h, MMat.scale, 0.03, coneS(vec3(x * 0.14, 0.24, -0.08), vec3(x * 0.15, 0.12, -0.16), 0.05, 0.036))
    m.add(h, MMat.scale, 0.02, coneS(vec3(x * 0.15, 0.12, -0.16), vec3(x * 0.15, 0.03, -0.12), 0.034, 0.028))
    for legI in 0..<2 {
      let bone = legI == 0 ? f : h
      let foot = legI == 0 ? vec3(x * 0.14, 0.022, 0.21) : vec3(x * 0.15, 0.022, -0.11)
      m.add(bone, MMat.scale, 0.012, ellS(foot, vec3(0.038, 0.022, 0.042)))
      for c in -1...1 {
        let cf = Float(c)
        let a = foot + vec3(cf * 0.018, -0.002, 0.025)
        m.add(bone, MMat.horn, 0.003, coneS(a, a + vec3(cf * 0.006, -0.012, 0.03), 0.0065, 0.0015))
      }
    }
  }
  // wings: an arm, three fingers and the membrane between them
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let w1 = sd > 0 ? DB.lWing1 : DB.rWing1, w2 = sd > 0 ? DB.lWing2 : DB.rWing2
    let S0 = vec3(x * 0.1, 0.46, 0.1), W = vec3(x * 0.38, 0.62, 0.02)
    let F1 = vec3(x * 0.7, 0.6, -0.1), F2 = vec3(x * 0.62, 0.42, -0.24), F3 = vec3(x * 0.44, 0.3, -0.3)
    let B = vec3(x * 0.09, 0.38, -0.18)
    m.add(w1, MMat.scale, 0.02, coneS(S0, W, 0.03, 0.018))
    m.add(w2, MMat.horn, 0.004, coneS(W + vec3(0, 0.01, 0), W + vec3(x * 0.02, 0.06, 0.02), 0.012, 0.002))  // thumb claw
    for F in [F1, F2, F3] { m.add(w2, MMat.scale, 0.008, coneS(W, F, 0.014, 0.004)) }
    let t: Float = 0.0085
    m.add(w1, MMat.membrane, 0.006, triS(S0, W, B, t))
    m.add(w2, MMat.membrane, 0.006, triS(W, F1, F2, t))
    m.add(w2, MMat.membrane, 0.006, triS(W, F2, F3, t))
    m.add(w2, MMat.membrane, 0.006, triS(W, F3, B, t))
  }
  return m
}

/// The man-at-arms' spear on its own: butt at the origin, pointing up +y.
private func spearModel() -> Model {
  let m = Model()
  let a = vec3(0, 0, 0), b = vec3(0, 0.964, 0)
  m.add(0, MMat.wood, 0, cylS(a, b, 0.0075))
  m.add(0, MMat.iron, 0.002, cylS(a, a + vec3(0, 0.025, 0), 0.0085))
  m.add(0, MMat.iron, 0.002, cylS(b - vec3(0, 0.02, 0), b + vec3(0, 0.012, 0), 0.0095))
  m.add(0, MMat.steel, 0, fnS(spearHead(b), b + vec3(0, 0.06, 0), 0.07))
  return m
}

// ---------------------------------------------------------------------------
private func buildAsset(_ A: inout PieceAssets, _ m: Model, _ lo: vec3, _ hi: vec3, _ quality: Int, _ seeds: Int,
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

extension Medieval {
  /// quality: 0 = fast, 1 = normal, 2 = ultra
  static func buildLibrary(_ lib: MedievalLibrary, quality qIn: Int, progress prog: BuildProgress?) {
    let quality = max(0, min(2, qIn))
    if let prog {
      prog.total = 10
      prog.done = 0
    }
    func tick(_ s: String) {
      if let prog {
        prog.stage = s
        prog.step()
      }
    }
    do {
      let m = pawnModel()
      buildAsset(&lib.asset[MAsset.pawn], m, vec3(-0.21, -0.005, -0.1), vec3(0.24, 1.0, 0.17), quality, 10, 4321,
                 pieceHeight(MAsset.pawn), pieceRadius(MAsset.pawn))
      tick("Arming the men-at-arms")
    }
    do {
      let m = knightModel()
      buildAsset(&lib.asset[MAsset.knight], m, vec3(-0.25, -0.005, -0.42), vec3(0.25, 1.42, 0.55), quality, 14,
                 4421, pieceHeight(MAsset.knight), pieceRadius(MAsset.knight))
      tick("Saddling the knights")
    }
    do {
      let m = bishopModel()
      let S = seatedScale(MAsset.bishop)
      buildAsset(&lib.asset[MAsset.bishop], m, vec3(-0.25, -0.005, -0.2) * S, vec3(0.25, 1.3, 0.36) * S, quality,
                 12, 4521, pieceHeight(MAsset.bishop), pieceRadius(MAsset.bishop))
      tick("Consecrating the bishops")
    }
    do {
      let m = castleModel()
      buildAsset(&lib.asset[MAsset.rook], m, vec3(-0.37, -0.005, -0.29), vec3(0.37, 1.24, 0.26), quality, 14, 4621,
                 pieceHeight(MAsset.rook), pieceRadius(MAsset.rook))
      tick("Raising the gatehouses")
    }
    do {
      let m = queenModel()
      let S = seatedScale(MAsset.queen)
      buildAsset(&lib.asset[MAsset.queen], m, vec3(-0.26, -0.005, -0.22) * S, vec3(0.26, 1.1, 0.36) * S, quality,
                 13, 4721, pieceHeight(MAsset.queen), pieceRadius(MAsset.queen))
      tick("Crowning the queens")
    }
    do {
      let m = kingModel()
      buildAsset(&lib.asset[MAsset.king], m, vec3(-0.26, -0.005, -0.22), vec3(0.42, 1.14, 0.36), quality, 14, 4821,
                 pieceHeight(MAsset.king), pieceRadius(MAsset.king))
      tick("Crowning the kings")
    }
    do {
      let m = throneModel(false)
      buildAsset(&lib.asset[MAsset.throne], m, vec3(-0.3, -0.005, -0.3), vec3(0.3, 1.12, 0.16), quality, 12, 4921,
                 1.1, 0.3)
      let c = throneModel(true)
      let S = seatedScale(MAsset.bishop)
      buildAsset(&lib.asset[MAsset.cathedra], c, vec3(-0.3, -0.005, -0.3) * S, vec3(0.3, 1.32, 0.16) * S, quality,
                 12, 5021, 1.25, 0.3)
      tick("Carving the thrones")
    }
    do {
      let m = dragonModel()
      buildAsset(&lib.asset[MAsset.dragon], m, vec3(-0.74, -0.01, -0.98), vec3(0.74, 0.86, 0.76), quality, 0, 0, 0.8,
                 0.4)
      tick("Waking the dragon")
    }
    do {
      let m = spearModel()
      buildAsset(&lib.asset[MAsset.spear], m, vec3(-0.03, -0.01, -0.03), vec3(0.03, 1.08, 0.03), quality, 0, 0, 1.07,
                 0.02)
    }
    do {
      let m = castleLegsModel()
      buildAsset(&lib.asset[MAsset.legs], m, vec3(-0.22, -0.43, -0.16), vec3(0.22, 0.12, 0.22), quality, 0, 0, 0.5,
                 0.2)
      tick("Quarrying the stone legs")
    }
    lib.built = true
    prog?.finished = true
  }
}
