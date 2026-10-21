// Turning animation controls into bone matrices: walk cycles, standing up
// from a throne, a horse's gaits, the gatehouse's legs and drawbridge, and
// the dragon's neck, wings and tail.
import Foundation
import simd

private let PI: Float = 3.14159265
private let X = vec3(1, 0, 0), Y = vec3(0, 1, 0), Z = vec3(0, 0, 1)
@inline(__always) private func aa(_ a: Float, _ axis: vec3) -> quat { quat(angle: a, axis: axis) }
/// Pitch a limb that hangs down: + swings it forward (+z).
@inline(__always) private func swing(_ a: Float) -> quat { aa(-a, X) }
/// Bend a knee: + folds the shin backwards.
@inline(__always) private func knee(_ a: Float) -> quat { aa(a, X) }
@inline(__always) private func smooth(_ t: Float) -> Float {
  let t = clamp(t, 0, 1)
  return t * t * (3 - 2 * t)
}

private struct Builder {
  let s: MSkeleton
  var R = [quat](repeating: .identity, count: Medieval.maxBones)
  var off = [vec3](repeating: vec3(0), count: Medieval.maxBones)
  var sc = [Float](repeating: 1, count: Medieval.maxBones)
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
      let L = translate(mat4.identity, j + off[b]) * mat4_cast(R[b]) * scale(mat4.identity, vec3(sc[b])) *
        translate(mat4.identity, -j)
      out.bone[b] = s.parent[b] >= 0 ? out.bone[s.parent[b]] * L : L
    }
  }
}

/// Men-at-arms walk; kings, queens and bishops can also get up from their thrones.
private func humanPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
  let sk = Medieval.skeleton(asset)
  var b = Builder(sk)
  let breath = sin(c.idle * 1.7)
  // --- standing up: thighs swing down, the shins straighten, the hips rise over the feet
  let throned = Medieval.throned(asset)
  let up: Float = throned ? 1 - clamp(c.sit, 0, 1) : 0
  let rise = smooth(up)
  if up > 0 {
    // the ankles sit under the knees in the seated pose: the hips end up over them
    let hip = sk.joint[HB.lThigh], kn = sk.joint[HB.lShin]
    let thigh = length(kn - hip)
    b.off[HB.root] += vec3(0, thigh * rise, (kn.z - hip.z) * smooth(up * 1.25))
  }
  // --- walk cycle: legs swing, knees fold on the forward swing, arms counter-swing
  let st = c.stride
  let sL = sin(c.gait), cL = cos(c.gait)
  let hipSwing = 0.42 * st
  let sitThigh = PI * 0.5 * rise, sitShin = -PI * 0.5 * rise
  b.R[HB.lThigh] = aa(sitThigh, X) * swing(hipSwing * sL + c.crouch * 0.6)
  b.R[HB.rThigh] = aa(sitThigh, X) * swing(-hipSwing * sL + c.crouch * 0.6)
  b.R[HB.lShin] = aa(sitShin, X) * knee(st * (0.75 * max(0, cL) + 0.12) + c.crouch * 1.1)
  b.R[HB.rShin] = aa(sitShin, X) * knee(st * (0.75 * max(0, -cL) + 0.12) + c.crouch * 1.1)
  let bob = st * 0.012 * abs(cos(c.gait)) - st * 0.01 - c.crouch * 0.05 * (1 + 0.6 * rise)
  b.off[HB.root] += vec3(0, bob, 0)
  b.R[HB.root] = aa(0.06 * st * sL, Y) * aa(0.03 * st * cL, Z)
  // --- torso: leans forward while getting up
  let getUpLean: Float = 0.45 * sin(PI * up) * (throned ? 1 : 0)
  b.R[HB.chest] = aa(c.twist, Y) * aa(c.lean + getUpLean + 0.012 * breath, X) * aa(-0.05 * st * sL, Y)
  b.R[HB.head] = aa(c.headYaw, Y) * aa(c.headPitch - 0.3 * getUpLean, X)
  // --- arms: controls are offsets from the sculpted pose
  let armSwing = 0.3 * st * sL
  b.R[HB.lArm] = aa(c.armL.y, Z) * swing(c.armL.x - armSwing)
  b.R[HB.rArm] = aa(-c.armR.y, Z) * swing(c.armR.x + armSwing)
  b.R[HB.lFore] = swing(c.armL.z)
  b.R[HB.rFore] = swing(c.armR.z)
  b.R[HB.itemR] = aa(c.itemPitch, X) * aa(c.itemYaw, Y)
  b.sc[HB.itemR] = c.itemR > 0.5 ? 1 : 0
  b.sc[HB.itemL] = c.itemL > 0.5 ? 1 : 0
  b.finish(&out)
}

/// The horse trots (diagonal pairs) and gallops, rears, jumps and bucks; the rider
/// leans, swings his arms and lowers his lance.
private func horsePose(_ c: MControls, _ out: inout MPose) {
  let sk = Medieval.skeleton(MAsset.knight)
  var b = Builder(sk)
  let st = c.stride, run = clamp(c.run, 0, 1)
  let g = c.gait
  // phase of each leg: trot pairs diagonals, the gallop staggers all four
  let trot: [Float] = [0, PI, PI, 0]  // FL FR HL HR
  let gallop: [Float] = [0, 0.55, 2.6, 3.15]
  let upper = [KB.flu, KB.fru, KB.hlu, KB.hru]
  let lower = [KB.fll, KB.frl, KB.hll, KB.hrl]
  for i in 0..<4 {
    let ph = g + mix(trot[i], gallop[i], run)
    let s = sin(ph), co = cos(ph)
    let front = i < 2
    let amp = (0.42 + 0.18 * run) * st
    let lift = max(0, co)  // swinging forward: the leg is in the air
    if front {
      b.R[upper[i]] = swing(amp * s + 0.12 * lift * st)
      b.R[lower[i]] = knee((0.55 + 0.35 * run) * lift * lift * st)
    } else {
      b.R[upper[i]] = swing(amp * s * 0.9 - 0.05 * st)
      b.R[lower[i]] = knee(-(0.45 + 0.3 * run) * lift * lift * st)
    }
  }
  let bodyPitch = 0.05 * st * (0.3 + run) * sin(g * (run > 0.5 ? 1 : 2))
  // the legs are never all straight while moving: the body rides a little lower
  let bob = 0.01 * st * (1 + run) * abs(sin(g)) - 0.022 * st * (0.5 + run)
  b.off[KB.body] = vec3(0, bob, 0)
  b.R[KB.body] = aa(bodyPitch, X)
  b.R[KB.neck] = aa(0.08 * st * sin(g + 0.8) + c.neck * 0.5, X)
  b.R[KB.head] = aa(0.05 * sin(c.idle * 0.9) + c.headPitch, X) * aa(c.headYaw, Y)
  b.R[KB.tail] = aa(0.25 * sin(c.idle * 1.3 + c.tail), Y) * aa(-0.35 * run * st, X)

  // rearing up on the hind legs: the body pivots about the hind hooves' line
  var rearA: Float = 0
  if c.rear > 0.001 {
    // the body pivots about the hind hips; the hind legs stay upright, so the hooves stay planted
    let a = 0.95 * smooth(c.rear)
    rearA = a
    let pivot = vec3(0, sk.joint[KB.hlu].y, sk.joint[KB.hlu].z)
    b.rotateAbout(KB.body, aa(-a, X), pivot)
    for i in 2..<4 { b.R[upper[i]] = aa(a, X) * swing(0.25 * smooth(c.rear)) * b.R[upper[i]] }
    b.R[KB.hll] = knee(-0.3 * smooth(c.rear)) * b.R[KB.hll]
    b.R[KB.hrl] = knee(-0.3 * smooth(c.rear)) * b.R[KB.hrl]
    // forelegs paw the air
    let paw = sin(c.idle * 9)
    b.R[KB.flu] = swing(1.2 * smooth(c.rear) + 0.3 * paw) * b.R[KB.flu]
    b.R[KB.fru] = swing(1.2 * smooth(c.rear) - 0.3 * paw) * b.R[KB.fru]
    b.R[KB.fll] = knee(1.6 * smooth(c.rear))
    b.R[KB.frl] = knee(1.6 * smooth(c.rear))
    b.R[KB.neck] = aa(-0.3 * smooth(c.rear), X) * b.R[KB.neck]
  }
  // folding the legs for a jump
  if c.tuck > 0.001 {
    let t = smooth(c.tuck)
    b.R[KB.flu] = swing(1.1 * t) * b.R[KB.flu]
    b.R[KB.fru] = swing(1.0 * t) * b.R[KB.fru]
    b.R[KB.fll] = knee(1.9 * t) * b.R[KB.fll]
    b.R[KB.frl] = knee(1.8 * t) * b.R[KB.frl]
    b.R[KB.hlu] = swing(-0.9 * t) * b.R[KB.hlu]
    b.R[KB.hru] = swing(-0.85 * t) * b.R[KB.hru]
    b.R[KB.hll] = knee(-0.4 * t) * b.R[KB.hll]
    b.R[KB.hrl] = knee(-0.4 * t) * b.R[KB.hrl]
  }
  // bucking: weight on the forelegs, hind legs lashing out backwards
  if c.kick > 0.001 {
    let t = smooth(c.kick)
    let pivot = vec3(0, sk.joint[KB.flu].y, sk.joint[KB.flu].z)
    b.rotateAbout(KB.body, aa(0.45 * t, X), pivot)
    rearA = -0.45 * t
    b.R[KB.flu] = aa(-0.45 * t, X) * b.R[KB.flu]
    b.R[KB.fru] = aa(-0.45 * t, X) * b.R[KB.fru]
    b.R[KB.hlu] = swing(-1.4 * t) * b.R[KB.hlu]
    b.R[KB.hru] = swing(-1.3 * t) * b.R[KB.hru]
    b.R[KB.hll] = knee(0.3 * t)
    b.R[KB.hrl] = knee(0.3 * t)
  }
  // the rider stays upright as the horse rears or bucks (and his lance with him)
  b.R[KB.rider] = aa(c.twist, Y) * aa(c.lean - bodyPitch * 0.6 + rearA * 0.75 + 0.01 * sin(c.idle * 1.6), X)
  b.R[KB.rHead] = aa(c.headYaw * 0.5, Y)
  b.R[KB.lArm] = aa(c.armL.y, Z) * swing(c.armL.x)
  b.R[KB.rArm] = aa(-c.armR.y, Z) * swing(c.armR.x)
  b.R[KB.lFore] = swing(c.armL.z)
  b.R[KB.rFore] = swing(c.armR.z)
  b.R[KB.lance] = aa(c.itemPitch, X)
  b.sc[KB.lance] = c.itemR > 0.5 ? 1 : 0
  b.finish(&out)
}

/// The gatehouse: stone legs, drawbridge, portcullis and its banner.
private func castlePose(_ c: MControls, _ out: inout MPose) {
  let sk = Medieval.skeleton(MAsset.rook)
  var b = Builder(sk)
  let legH = CJ.legH
  let L = clamp(c.legs, 0, 1)
  let st = c.stride
  let sL = sin(c.gait), cL = cos(c.gait)
  // the tower rises on its legs; while they are in, they hide inside it
  let bob = st * 0.025 * abs(cL) - c.crouch * 0.08
  b.off[CB.body] = vec3(0, legH * smooth(L) + bob * L, 0)
  b.R[CB.body] = aa(0.06 * st * sL, Z) * aa(c.lean, X) * aa(0.05 * st * sL, Y)
  for side in 0..<2 {
    let th = side == 0 ? CB.lThigh : CB.rThigh, sh = side == 0 ? CB.lShin : CB.rShin
    let s = side == 0 ? sL : -sL, co = side == 0 ? cL : -cL
    b.off[th] = vec3(0, legH * (1 - smooth(L)), 0)
    var lift: Float = 0
    if (c.lift > 0 && side == 1) || (c.lift < 0 && side == 0) { lift = abs(c.lift) }
    // counter the body's sway so the feet stay flat
    b.R[th] = swing(0.55 * st * s + 0.9 * lift - c.crouch * 0.5)
    b.R[sh] = knee(st * 0.8 * max(0, co) + 1.1 * lift + c.crouch * 0.9)
  }
  // drawbridge swings down from its hinge, portcullis slides up
  b.R[CB.bridge] = aa(PI * 0.5 * smooth(c.bridge), X)
  b.off[CB.gate] = vec3(0, 0.3 * smooth(c.gate), 0)
  b.R[CB.flag] = aa(0.25 * sin(c.idle * 1.3) + 0.1 * sin(c.idle * 3.7), Y)
  b.finish(&out)
}

/// The dragon walks, rears its neck back, lunges, opens its jaws, spreads and beats its wings.
private func dragonPose(_ c: MControls, _ out: inout MPose) {
  let sk = Medieval.skeleton(MAsset.dragon)
  var b = Builder(sk)
  let st = c.stride, g = c.gait
  let breath = sin(c.idle * 1.4)
  // walk: diagonal legs together, the body rolls a little
  b.R[DB.fl] = swing(0.5 * st * sin(g))
  b.R[DB.hr] = swing(0.5 * st * sin(g))
  b.R[DB.fr] = swing(-0.5 * st * sin(g))
  b.R[DB.hl] = swing(-0.5 * st * sin(g))
  b.off[DB.body] = vec3(0, 0.015 * st * abs(cos(g)) + 0.005 * breath, 0)
  b.R[DB.body] = aa(0.05 * st * sin(g), Z) * aa(-0.12 * c.neck + c.lean, X)
  // neck: + rears back (S-curve), - lunges forward and down
  let n = c.neck
  let sway = 0.08 * sin(c.idle * 0.8)
  b.R[DB.neck1] = aa(-0.35 * max(0, n) + 0.25 * max(0, -n) + c.headYaw * 0.3, X) * aa(sway, Y)
  b.R[DB.neck2] = aa(-0.3 * max(0, n) + 0.2 * max(0, -n), X) * aa(c.headYaw * 0.3 + sway, Y)
  b.R[DB.neck3] = aa(0.25 * max(0, n) + 0.1 * max(0, -n), X) * aa(c.headYaw * 0.3, Y)
  b.R[DB.head] = aa(c.headPitch + 0.4 * max(0, n) - 0.15 * max(0, -n), X)
  b.R[DB.jaw] = aa(0.75 * clamp(c.jaw, 0, 1), X)
  // tail: a travelling wave
  let ts = c.tail + c.idle * 1.1
  b.R[DB.tail1] = aa(0.18 * sin(ts), Y) * aa(-0.05, X)
  b.R[DB.tail2] = aa(0.24 * sin(ts - 0.8), Y)
  b.R[DB.tail3] = aa(0.3 * sin(ts - 1.6), Y) * aa(0.08, X)
  b.R[DB.tail4] = aa(0.36 * sin(ts - 2.4), Y)
  // wings: folded along the flanks (0) or spread and beating (1)
  let w = clamp(c.wings, 0, 1)
  let beat = w * 0.45 * sin(c.flap)
  for side in 0..<2 {
    let sd: Float = side == 0 ? 1 : -1
    let w1 = side == 0 ? DB.lWing1 : DB.rWing1, w2 = side == 0 ? DB.lWing2 : DB.rWing2
    let fold = 1 - w
    // folding swings the arm back and down along the body, the fingers close onto the arm
    b.R[w1] = aa(sd * (0.75 * fold + beat - 0.25 * w), Z) * aa(sd * 0.9 * fold, Y)
    b.R[w2] = aa(sd * -1.6 * fold, Y) * aa(sd * 0.5 * fold - sd * 0.3 * beat, Z)
  }
  b.finish(&out)
}

extension Medieval {
  /// Turns the controls into bone matrices (rest -> posed, model space).
  static func buildPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
    switch asset {
    case MAsset.pawn, MAsset.bishop, MAsset.queen, MAsset.king: humanPose(asset, c, &out)
    case MAsset.knight: horsePose(c, &out)
    case MAsset.rook, MAsset.legs: castlePose(c, &out)
    case MAsset.dragon: dragonPose(c, &out)
    default: out.count = 0
    }
  }
}
