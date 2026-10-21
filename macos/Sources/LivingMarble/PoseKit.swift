// Building skeleton poses: per-bone rotations, offsets and scales about the rest
// joints, chained down the hierarchy (shared by the skinned armies). The pose
// files take file-private shorthands for these (aa, swing, knee, smooth, X/Y/Z).
import Foundation
import simd

enum PoseKit {
  static let PI: Float = 3.14159265
  static let X = vec3(1, 0, 0), Y = vec3(0, 1, 0), Z = vec3(0, 0, 1)
  @inline(__always) static func aa(_ a: Float, _ axis: vec3) -> quat { quat(angle: a, axis: axis) }
  /// Pitch a limb that hangs down: + swings it forward (+z).
  @inline(__always) static func swing(_ a: Float) -> quat { aa(-a, X) }
  /// Bend a knee: + folds the shin backwards.
  @inline(__always) static func knee(_ a: Float) -> quat { aa(a, X) }
  @inline(__always) static func smooth(_ t: Float) -> Float {
    let t = clamp(t, 0, 1)
    return t * t * (3 - 2 * t)
  }

  struct Builder {
    let s: MSkeleton
    var R = [quat](repeating: .identity, count: Medieval.maxBones)
    var off = [vec3](repeating: vec3(0), count: Medieval.maxBones)
    var sc = [Float](repeating: 1, count: Medieval.maxBones)
    /// Extra stretch along the rest-pose axes (an alien's tongue).
    var st = [vec3](repeating: vec3(1, 1, 1), count: Medieval.maxBones)
    init(_ sk: MSkeleton) { s = sk }

    /// Rotate bone b about an arbitrary pivot (in its parent's space) instead of its joint.
    mutating func rotateAbout(_ b: Int, _ q: quat, _ pivot: vec3) {
      let j = s.joint[b]
      R[b] = q * R[b]
      off[b] += (pivot - j) - q * (pivot - j)
    }
    func finish(_ out: inout MPose) {
      out.count = s.count
      for b in 0..<s.count {
        let j = s.joint[b]
        let L = translate(mat4.identity, j + off[b]) * mat4_cast(R[b]) *
          scale(mat4.identity, vec3(repeating: sc[b]) * st[b]) * translate(mat4.identity, -j)
        out.bone[b] = s.parent[b] >= 0 ? out.bone[s.parent[b]] * L : L
      }
    }
  }

  /// The shortest rotation taking direction a onto direction b.
  static func rotationBetween(_ a0: vec3, _ b0: vec3) -> quat {
    let a = normalize(a0), b = normalize(b0)
    let d = dot(a, b)
    if d > 0.99999 { return .identity }
    if d < -0.99999 {
      let ax = abs(a.x) < 0.9 ? cross(a, vec3(1, 0, 0)) : cross(a, vec3(0, 1, 0))
      return quat(angle: PI, axis: normalize(ax))
    }
    let c = cross(a, b)
    let s = ((1 + d) * 2).squareRoot()
    return normalize(quat(s * 0.5, c.x / s, c.y / s, c.z / s))
  }
}
