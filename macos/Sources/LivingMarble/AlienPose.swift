// Posing the aliens: the little green men waddle with bobbing antennae and
// shoot their tongues out, the saucer wobbles and spins its light ring, the
// psychic and the overlord float, the queen glides and swishes her tail, the
// tripod walks on three legs (inverse kinematics keeps its feet planted) and
// stands up tall, and the cow kicks its legs as it falls.
import Foundation
import simd

// the pose kit (PoseKit.swift)
private let X = PoseKit.X, Y = PoseKit.Y, Z = PoseKit.Z
@inline(__always) private func aa(_ a: Float, _ axis: vec3) -> quat { PoseKit.aa(a, axis) }
@inline(__always) private func swing(_ a: Float) -> quat { PoseKit.swing(a) }
@inline(__always) private func knee(_ a: Float) -> quat { PoseKit.knee(a) }
@inline(__always) private func smooth(_ t: Float) -> Float { PoseKit.smooth(t) }
private typealias Builder = PoseKit.Builder

/// Antennae: bob with the walk, perk up (+) or droop outwards (-).
private func antennae(_ b: inout Builder, _ c: MControls, _ L: Int, _ R: Int, _ bounce: Float) {
  let droop = max(0, -c.perk) * 1.1
  let alert = max(0, c.perk) * 0.35
  for side in 0..<2 {
    let sd: Float = side == 0 ? 1 : -1
    let fs = Float(side)
    let sway = 0.1 * sin(c.idle * 2.3 + fs * 1.7) + 0.06 * sin(c.idle * 5.1 + fs)
    b.R[side == 0 ? L : R] = aa(-sd * droop + sd * 0.25 * alert, Z) * aa(alert + bounce + sway, X)
  }
}

private func humanoidPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
  let sk = Alien.skeleton(asset)
  var b = Builder(sk)
  let breath = sin(c.idle * 1.9)
  let walker = asset == AAsset.pawn
  let floater = !walker
  let st = c.stride, sL = sin(c.gait), cL = cos(c.gait)
  // the base: floaters bob gently
  let bobF: Float = floater ? 0.009 * sin(c.idle * 2.2) : 0
  b.off[AH.base] = vec3(0, c.hover + bobF, 0)
  if asset == AAsset.king { b.R[AH.base] = aa(0.02 * sin(c.idle * 1.3), Z) * aa(0.015 * sin(c.idle * 1.7), X) }
  if walker {
    // a waddle: short legs swing, the body rocks from side to side
    let hipSwing = 0.5 * st
    b.R[AH.lThigh] = swing(hipSwing * sL + c.crouch * 0.7)
    b.R[AH.rThigh] = swing(-hipSwing * sL + c.crouch * 0.7)
    b.R[AH.lShin] = knee(st * (0.85 * max(0, cL) + 0.15) + c.crouch * 1.3)
    b.R[AH.rShin] = knee(st * (0.85 * max(0, -cL) + 0.15) + c.crouch * 1.3)
    let bob = st * 0.016 * abs(cL) - st * 0.008 - c.crouch * 0.05
    b.off[AH.pelvis] = vec3(0, bob, 0)
    b.R[AH.pelvis] = aa(0.17 * st * sL, Z) * aa(0.07 * st * cL, Y)
  } else if asset == AAsset.queen {
    // gliding: the gown sways
    b.R[AH.pelvis] = aa(0.05 * st * sL, Z) * aa(0.03 * sin(c.idle * 0.9), Y) * aa(0.04 * st, X)
  }
  // torso, head
  let rock: Float = walker ? -0.1 * st * sL : 0
  b.R[AH.chest] = aa(c.twist, Y) * aa(c.lean + 0.015 * breath, X) * aa(rock, Z)
  let headBob: Float = walker ? 0.08 * st * sin(c.gait * 2) : 0
  let headRock: Float = walker ? -0.07 * st * sL : 0
  b.R[AH.head] = aa(c.headYaw, Y) * aa(c.headPitch + headBob, X) * aa(headRock, Z)
  // arms: offsets from the sculpted pose, swinging with a walk
  let armSwing = (walker ? Float(0.4) : 0) * st * sL
  b.R[AH.lArm] = aa(c.armL.y, Z) * swing(c.armL.x - armSwing)
  b.R[AH.rArm] = aa(-c.armR.y, Z) * swing(c.armR.x + armSwing)
  b.R[AH.lFore] = swing(c.armL.z)
  b.R[AH.rFore] = swing(c.armR.z)
  b.R[AH.itemR] = aa(c.itemPitch, X) * aa(c.itemYaw, Y)
  b.sc[AH.itemR] = c.itemR > 0.5 ? 1 : 0
  b.sc[AH.itemL] = c.itemL > 0.5 ? 1 : 0
  antennae(&b, c, AH.antL, AH.antR, st * 0.3 * sin(c.gait * 2 - 0.8))
  if asset == AAsset.pawn {
    // the chin drops, the tongue shoots out (the shaft stretches, the tip rides on its end)
    b.R[AH.jaw] = aa(0.55 * clamp(c.jaw, 0, 1), X)
    let t = max(0, c.tongue)
    let len = length(sk.joint[AH.tail2] - sk.joint[AH.tail1])
    b.st[AH.tail1] = vec3(1, 1, 1 + t / len)
    b.off[AH.tail2] = vec3(0, 0, t)
  } else if asset == AAsset.queen {
    b.R[AH.jaw] = aa(-0.1 * c.jaw, X)
    b.off[AH.jaw] = vec3(0, 0, max(0, c.tongue))
    // the tail: a slow travelling swish; 'lift' raises it, 'kick' lashes it round sideways
    let ts = c.tail + c.idle * 1.1
    let raise = clamp(c.lift, 0, 1)
    b.R[AH.tail1] = aa(0.22 * sin(ts) + 0.55 * c.kick, Y) * aa(-0.9 * raise, X)
    b.R[AH.tail2] = aa(0.3 * sin(ts - 0.9) + 0.45 * c.kick, Y) * aa(-1.0 * raise, X)
    b.R[AH.tail3] = aa(0.38 * sin(ts - 1.8) + 0.35 * c.kick, Y) * aa(-0.8 * raise, X)
  } else {
    b.R[AH.jaw] = aa(0.35 * clamp(c.jaw, 0, 1), X)
  }
  b.finish(&out)
}

/// The saucer: hovers, wobbles, banks, spins its light ring, opens its belly, lands on three legs.
private func saucerPose(_ c: MControls, _ out: inout MPose) {
  let sk = Alien.skeleton(AAsset.knight)
  var b = Builder(sk)
  let bob = 0.012 * sin(c.idle * 2)
  b.off[UB.body] = vec3(0, c.hover + bob, 0)
  b.R[UB.body] = aa(c.twist, Z) * aa(c.lean, X) * aa(0.035 * sin(c.idle * 1.3), Z) *
    aa(0.03 * sin(c.idle * 1.1 + 1), X)
  b.R[UB.rim] = aa(c.spin + c.idle * 1.2, Y)
  b.R[UB.pilot] = aa(0.05 * sin(c.idle * 2.6), Z) * aa(0.6 * c.headYaw, Y)
  b.R[UB.head] = aa(0.4 * c.headYaw, Y) * aa(c.headPitch, X)
  b.R[UB.lArm] = aa(c.armL.y, Z) * swing(c.armL.x + 0.1 * sin(c.idle * 3.1))
  b.R[UB.rArm] = aa(-c.armR.y, Z) * swing(c.armR.x + 0.1 * sin(c.idle * 2.7 + 1))
  let h = clamp(c.hatch, 0, 1)
  b.off[UB.hatch] = vec3(0, -0.028 * smooth(h), 0)
  b.sc[UB.hatch] = 1 + 0.25 * smooth(h)
  // landing legs fold up flat under the hull while flying
  let A: [Float] = [3.14159265, 3.14159265 / 3.0, -3.14159265 / 3.0]
  let fold = 1 - smooth(c.legs)
  for i in 0..<3 {
    let k = vec3(-cos(A[i]), 0, sin(A[i]))
    b.R[UB.leg1 + i] = aa(fold * 1.35, k)
  }
  antennae(&b, c, UB.antL, UB.antR, 0)
  b.finish(&out)
}

/// The tripod: three legs solved with two-bone IK so the feet stay planted while
/// the pod rises, crouches, leans and walks; a turning heat-ray eye; two tentacles.
private func tripodPose(_ c: MControls, _ out: inout MPose) {
  let sk = Alien.skeleton(AAsset.rook)
  var b = Builder(sk)
  let A: [Float] = [3.14159265, 1.0471976, -1.0471976]
  let L = smooth(c.legs)
  let st = c.stride, g = c.gait
  let bob = st * 0.02 * abs(sin(g * 1.5)) + 0.006 * sin(c.idle * 1.6)
  let podOff = vec3(0, 0.34 * L - c.crouch * 0.22 + bob, 0)
  let podR = aa(c.twist, Y) * aa(c.lean, X) * aa(0.045 * st * sin(g), Z)
  b.off[TB.body] = podOff
  b.R[TB.body] = podR
  b.R[TB.hood] = aa(c.headYaw, Y) * aa(c.headPitch, X)
  let J = sk.joint[TB.body]
  for i in 0..<3 {
    let up = TB.l1u + 2 * i, lo = up + 1
    let hip0 = sk.joint[up], knee0 = sk.joint[lo]
    let d = vec3(sin(A[i]), 0, cos(A[i]))
    let foot0 = vec3(d.x * 0.34, 0.03, d.z * 0.34)
    // the foot: planted, or stepping (each leg in turn), or raised high to stamp
    var ft = foot0
    let ph = g + Float(i) * 2.0943951
    ft.z += st * 0.11 * sin(ph)
    ft.y += st * 0.07 * max(0, cos(ph))
    let stamping = (c.lift < 0 && i == 1) || (c.lift > 0 && i == 2)
    if stamping {
      let l = abs(c.lift)
      ft += vec3(0, 0.32 * l, 0) + d * 0.04 * l + vec3(0, 0, 0.06 * l)
    }
    let hipW = J + podOff + podR * (hip0 - J)
    let L1 = length(knee0 - hip0), L2 = length(foot0 - knee0)
    let toF = ft - hipW
    let dist = clamp(length(toF), abs(L1 - L2) + 1e-3, L1 + L2 - 1e-3)
    let dirF = normalize(toF)
    let hint = normalize(d + vec3(0, 1.4, 0))
    var n = cross(dirF, hint)
    if length(n) < 1e-4 { n = cross(dirF, vec3(0, 0, 1)) }
    n = normalize(n)
    let perp = normalize(cross(n, dirF))
    let ca = (L1 * L1 + dist * dist - L2 * L2) / (2 * L1 * dist)
    let sa = max(0, 1 - ca * ca).squareRoot()
    let kneeW = hipW + dirF * (ca * L1) + perp * (sa * L1)
    let Rup = PoseKit.rotationBetween(knee0 - hip0, podR.inverse * (kneeW - hipW))
    let Rlo = PoseKit.rotationBetween(foot0 - knee0, (podR * Rup).inverse * (ft - kneeW))
    b.R[up] = Rup
    b.R[lo] = Rlo
  }
  // tentacles: dangle and sway; armL / armR reach (x), swing sideways (y) and curl (z)
  for side in 0..<2 {
    let t1 = side == 0 ? TB.tl1 : TB.tr1
    let a = side == 0 ? c.armL : c.armR
    let sd: Float = side == 0 ? 1 : -1
    let w = c.idle * 1.4 + Float(side) * 2.1
    b.R[t1] = aa(sd * (0.12 * sin(w) + a.y), Z) * swing(a.x + 0.1 * sin(w * 0.7))
    b.R[t1 + 1] = swing(a.z * 0.6 + 0.15 * sin(w - 0.8)) * aa(sd * 0.1 * sin(w * 1.3), Z)
    b.R[t1 + 2] = swing(a.z * 0.8 + 0.2 * sin(w - 1.6))
  }
  b.finish(&out)
}

/// The cow: walks, kicks its legs in the air ('wings' 0..1), swishes its tail.
private func cowPose(_ c: MControls, _ out: inout MPose) {
  let sk = Alien.skeleton(AAsset.cow)
  var b = Builder(sk)
  let st = c.stride, g = c.gait, fl = clamp(c.wings, 0, 1)
  let phase: [Float] = [0.0, 3.14159, 3.14159, 0.0]
  for i in 0..<4 {
    let walk = 0.45 * st * sin(g + phase[i])
    let flail = fl * (0.7 * sin(c.idle * 17 + Float(i) * 1.9) + (i < 2 ? Float(0.5) : -0.5))
    b.R[CW.fl + i] = swing(walk + flail) * aa((i % 2 != 0 ? Float(-1) : 1) * fl * 0.35, Z)
  }
  b.R[CW.head] = aa(c.headYaw, Y) * aa(c.headPitch + 0.06 * sin(c.idle * 1.2) - fl * 0.5, X)
  b.R[CW.tail] = aa(0.4 * sin(c.idle * 2 + c.tail) + fl * 0.8 * sin(c.idle * 13), Z) * aa(-fl * 1.2, X)
  b.R[CW.body] = aa(0.04 * st * sin(g), Z)
  b.finish(&out)
}

extension Alien {
  /// What MControls mean to the aliens, besides walking (gait, stride), leaning,
  /// turning the head and the arms like the medieval figures:
  ///   jaw      mouth open (0..1)        tongue   tongue / inner jaw out (model units)
  ///   perk     antennae up (+) or droop (-)
  ///   hover    the base floats this high (bishop's disc, king's throne, saucer)
  ///   spin     the saucer's light ring, the overlord throne's fans (radians)
  ///   hatch    saucer belly / tripod eye opening (0..1)
  ///   legs     saucer landing legs out / tripod standing tall (0..1)
  ///   tail     tail swish phase          crouch, lift (tripod: raise one foot)
  ///   wings    the cow's legs flailing (0..1)      kick     the queen's tail lashing sideways
  static func buildPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
    switch asset {
    case AAsset.pawn, AAsset.bishop, AAsset.jar, AAsset.queen, AAsset.king:
      humanoidPose(asset == AAsset.jar ? AAsset.bishop : asset, c, &out)
    case AAsset.knight, AAsset.canopy: saucerPose(c, &out)
    case AAsset.rook: tripodPose(c, &out)
    case AAsset.cow: cowPose(c, &out)
    default: out.count = 0
    }
  }
}
