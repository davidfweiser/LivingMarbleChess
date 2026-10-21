// GLM-flavoured vector math on top of simd, so the game code reads like the
// shader code it talks to. Matrices are column-major (like GLM and Metal);
// projections map depth to Metal's [0, 1] clip range.
import Foundation
import simd

typealias vec2 = SIMD2<Float>
typealias vec3 = SIMD3<Float>
typealias vec4 = SIMD4<Float>
typealias mat3 = simd_float3x3
typealias mat4 = simd_float4x4
typealias quat = simd_quatf

let kPI: Float = 3.14159265358979

// ---- constructors ----------------------------------------------------------
extension SIMD2 where Scalar == Float {
  init(_ s: Float) { self.init(repeating: s) }
}

extension SIMD3 where Scalar == Float {
  init(_ s: Float) { self.init(repeating: s) }
  init(_ v: SIMD4<Float>) { self.init(v.x, v.y, v.z) }
  var xz: vec2 { vec2(x, z) }
}

extension SIMD4 where Scalar == Float {
  init(_ s: Float) { self.init(repeating: s) }
  var xyz: vec3 {
    get { vec3(x, y, z) }
    set { x = newValue.x; y = newValue.y; z = newValue.z }
  }
}

extension simd_float4x4 {
  static let identity = matrix_identity_float4x4
}

extension simd_float3x3 {
  static let identity = matrix_identity_float3x3
  /// Upper-left 3x3 block (glm::mat3(mat4)).
  init(_ m: simd_float4x4) {
    self.init(columns: (vec3(m.columns.0), vec3(m.columns.1), vec3(m.columns.2)))
  }
  /// Columns x, y, z (glm::mat3(x, y, z)).
  init(_ x: vec3, _ y: vec3, _ z: vec3) { self.init(columns: (x, y, z)) }
}

extension simd_quatf {
  static let identity = simd_quatf(ix: 0, iy: 0, iz: 0, r: 1)
  /// GLM argument order: quat(w, x, y, z).
  init(_ w: Float, _ x: Float, _ y: Float, _ z: Float) { self.init(ix: x, iy: y, iz: z, r: w) }
  var w: Float { real }
}

/// Rotates a vector (glm: quat * vec3).
@inline(__always) func * (q: quat, v: vec3) -> vec3 { q.act(v) }

// ---- scalar helpers ----------------------------------------------------------
@inline(__always) func clamp(_ x: Float, _ a: Float, _ b: Float) -> Float { min(max(x, a), b) }
@inline(__always) func clamp(_ x: Int, _ a: Int, _ b: Int) -> Int { min(max(x, a), b) }
@inline(__always) func clamp(_ v: vec2, _ a: Float, _ b: Float) -> vec2 { simd_clamp(v, vec2(a), vec2(b)) }
@inline(__always) func clamp(_ v: vec3, _ a: Float, _ b: Float) -> vec3 { simd_clamp(v, vec3(a), vec3(b)) }
@inline(__always) func clamp(_ v: vec3, _ a: vec3, _ b: vec3) -> vec3 { simd_clamp(v, a, b) }
@inline(__always) func saturate(_ x: Float) -> Float { clamp(x, 0, 1) }

@inline(__always) func mix(_ a: Float, _ b: Float, _ t: Float) -> Float { a + (b - a) * t }
@inline(__always) func mix(_ a: vec2, _ b: vec2, _ t: Float) -> vec2 { a + (b - a) * t }
@inline(__always) func mix(_ a: vec3, _ b: vec3, _ t: Float) -> vec3 { a + (b - a) * t }
@inline(__always) func mix(_ a: vec4, _ b: vec4, _ t: Float) -> vec4 { a + (b - a) * t }
@inline(__always) func mix(_ a: vec3, _ b: vec3, _ t: vec3) -> vec3 { a + (b - a) * t }

@inline(__always) func smoothstep(_ e0: Float, _ e1: Float, _ x: Float) -> Float {
  let t = clamp((x - e0) / (e1 - e0), 0, 1)
  return t * t * (3 - 2 * t)
}
@inline(__always) func fract(_ x: Float) -> Float { x - floor(x) }
@inline(__always) func radians(_ deg: Float) -> Float { deg * kPI / 180 }

// ---- vector helpers ------------------------------------------------------------
@inline(__always) func vmin(_ a: vec2, _ b: vec2) -> vec2 { simd_min(a, b) }
@inline(__always) func vmax(_ a: vec2, _ b: vec2) -> vec2 { simd_max(a, b) }
@inline(__always) func vmin(_ a: vec3, _ b: vec3) -> vec3 { simd_min(a, b) }
@inline(__always) func vmax(_ a: vec3, _ b: vec3) -> vec3 { simd_max(a, b) }
@inline(__always) func vabs(_ a: vec2) -> vec2 { simd_abs(a) }
@inline(__always) func vabs(_ a: vec3) -> vec3 { simd_abs(a) }
@inline(__always) func vfloor(_ a: vec3) -> vec3 { a.rounded(.down) }

// ---- matrices (glm semantics: the new transform is applied first) -------------
func translate(_ m: mat4, _ v: vec3) -> mat4 {
  var r = m
  r.columns.3 = m.columns.0 * v.x + m.columns.1 * v.y + m.columns.2 * v.z + m.columns.3
  return r
}

func scale(_ m: mat4, _ v: vec3) -> mat4 {
  mat4(columns: (m.columns.0 * v.x, m.columns.1 * v.y, m.columns.2 * v.z, m.columns.3))
}

func rotate(_ m: mat4, _ angle: Float, _ axis: vec3) -> mat4 {
  m * mat4(quat(angle: angle, axis: normalize(axis)))
}

@inline(__always) func mat4_cast(_ q: quat) -> mat4 { mat4(q) }
@inline(__always) func quat_cast(_ m: mat3) -> quat { quat(m) }
@inline(__always) func angleAxis(_ angle: Float, _ axis: vec3) -> quat { quat(angle: angle, axis: normalize(axis)) }
@inline(__always) func inverse(_ m: mat4) -> mat4 { m.inverse }

/// Right-handed view matrix (glm::lookAt).
func lookAt(_ eye: vec3, _ center: vec3, _ up: vec3) -> mat4 {
  let f = normalize(center - eye)
  let s = normalize(cross(f, up))
  let u = cross(s, f)
  return mat4(columns: (vec4(s.x, u.x, -f.x, 0),
                        vec4(s.y, u.y, -f.y, 0),
                        vec4(s.z, u.z, -f.z, 0),
                        vec4(-dot(s, eye), -dot(u, eye), dot(f, eye), 1)))
}

/// Right-handed perspective projection with Metal's [0, 1] depth range.
func perspective(_ fovy: Float, _ aspect: Float, _ near: Float, _ far: Float) -> mat4 {
  let f = 1 / tan(fovy * 0.5)
  return mat4(columns: (vec4(f / aspect, 0, 0, 0),
                        vec4(0, f, 0, 0),
                        vec4(0, 0, far / (near - far), -1),
                        vec4(0, 0, near * far / (near - far), 0)))
}

/// Right-handed orthographic projection with Metal's [0, 1] depth range.
func ortho(_ l: Float, _ r: Float, _ b: Float, _ t: Float, _ n: Float, _ f: Float) -> mat4 {
  mat4(columns: (vec4(2 / (r - l), 0, 0, 0),
                 vec4(0, 2 / (t - b), 0, 0),
                 vec4(0, 0, -1 / (f - n), 0),
                 vec4(-(r + l) / (r - l), -(t + b) / (t - b), -n / (f - n), 1)))
}

// ---- random numbers ------------------------------------------------------------
/// Small, fast, seedable generator (SplitMix64).
struct Rng {
  private var state: UInt64
  init(seed: UInt64) { state = seed }
  mutating func next() -> UInt64 {
    state &+= 0x9E37_79B9_7F4A_7C15
    var z = state
    z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
    z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
    return z ^ (z >> 31)
  }
  /// Uniform in [0, 1).
  mutating func uniform() -> Float { Float(next() >> 40) * (1.0 / 16_777_216.0) }
  mutating func uniform(_ a: Float, _ b: Float) -> Float { a + (b - a) * uniform() }
}
