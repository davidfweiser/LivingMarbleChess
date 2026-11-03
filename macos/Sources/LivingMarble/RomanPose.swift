// Turning animation controls into bone matrices for the Romans: soldiers
// marching, fighting, kicking, tucking into a somersault and falling limp,
// their cloaks swinging; the chariot's horses and wheels and its charioteer;
// the siege tower's wheels, ram, bridge, door and scorpion; the lion.
import Foundation
import simd

extension Roman {
  // the pose kit (PoseKit.swift)
  private static let PI = PoseKit.PI
  private static let X = PoseKit.X, Y = PoseKit.Y, Z = PoseKit.Z
  @inline(__always) private static func aa(_ a: Float, _ axis: vec3) -> quat { PoseKit.aa(a, axis) }
  @inline(__always) private static func swing(_ a: Float) -> quat { PoseKit.swing(a) }
  @inline(__always) private static func knee(_ a: Float) -> quat { PoseKit.knee(a) }
  @inline(__always) private static func smooth(_ t: Float) -> Float { PoseKit.smooth(t) }
  private typealias Builder = PoseKit.Builder

  private static func humanPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
    let sk = skeleton(asset)
    var b = Builder(sk)
    let breath = sin(c.idle * 1.7) * (1 - clamp(c.dead, 0, 1))
    let st = c.stride
    let sL = sin(c.gait), cL = cos(c.gait)
    let hipSwing = 0.42 * st
    let dead = clamp(c.dead, 0, 1)
    let tuck = smooth(c.tuck)
    let kick = clamp(c.kick, 0, 1)
    // --- legs: the walk cycle, a crouch, a front kick, knees to the chest, limp
    let lt: Float = hipSwing * sL + c.crouch * 0.6 + tuck * 1.75 + dead * 0.3
    b.R[RH.lthigh] = aa(dead * 0.14, Z) * swing(lt)
    let rt0: Float = -hipSwing * sL + c.crouch * 0.6 + tuck * 1.75
    let rt: Float = rt0 + kick * 1.4 + dead * 0.1
    b.R[RH.rthigh] = aa(-dead * 0.1, Z) * swing(rt)
    let ls: Float = st * (0.75 * max(0, cL) + 0.12) + c.crouch * 1.1 + tuck * 2.1 + dead * 0.45
    b.R[RH.lshin] = knee(ls)
    let rs0: Float = st * (0.75 * max(0, -cL) + 0.12) + c.crouch * 1.1 + tuck * 2.1
    let rs: Float = rs0 + dead * 0.2 + 1.5 * kick * (1 - kick)
    b.R[RH.rshin] = knee(rs)
    let bob: Float = st * 0.012 * abs(cos(c.gait)) - st * 0.01 - c.crouch * 0.05
    b.off[RH.root] += vec3(0, bob, 0)
    b.R[RH.root] = aa(0.06 * st * sL, Y) * aa(0.03 * st * cL, Z)
    // --- torso and head
    let chestPitch: Float = c.lean + 0.012 * breath + tuck * 0.5 - kick * 0.2
    b.R[RH.chest] = aa(c.twist, Y) * aa(chestPitch, X) * aa(-0.05 * st * sL, Y)
    b.R[RH.head] = aa(c.headYaw, Y) * aa(c.headPitch - dead * 0.4 + tuck * 0.3, X) * aa(dead * 0.3, Z)
    // --- arms: offsets from the sculpted pose; tucked in for a somersault, flung out when limp
    let armSwing = 0.3 * st * sL
    // (limp: the arms fall out to the sides, straight - lying on the board when the body is down)
    let tuckArm: vec3 = tuck * vec3(0.7, 0.1, 0.9)
    let aL = romanMix(c.armL + tuckArm, vec3(0.1, 1.25, -1.3), dead)
    let aR = romanMix(c.armR + tuckArm, vec3(0.15, 1.1, -1.2), dead)
    b.R[RH.larm] = aa(aL.y, Z) * swing(aL.x - armSwing)
    b.R[RH.rarm] = aa(-aR.y, Z) * swing(aR.x + armSwing)
    b.R[RH.lfore] = swing(aL.z)
    b.R[RH.rfore] = swing(aR.z)
    // --- what the hands hold: the right hand's item, or the blade drawn instead of it
    // (limp: what the hands still hold turns to lie along the arm, flat on the board once the body is down)
    let ip = aa(c.itemPitch * (1 - dead) - 1.2 * dead, X) * aa(c.itemYaw * (1 - dead), Y)
    b.R[RH.itemR] = ip
    b.R[RH.blade] = ip
    b.R[RH.itemL] = aa(-1.3 * dead, X)
    let drawn = c.draw > 0.5
    b.sc[RH.itemR] = c.itemR > 0.5 && !drawn ? 1 : 0
    b.sc[RH.blade] = c.itemR > 0.5 && drawn ? 1 : 0
    b.sc[RH.sheath] = drawn ? 0 : 1
    let left = c.itemL > 0.5 && (asset != RAsset.queen || c.hatch > 0.5)  // the empress's goblet only when she offers it
    b.sc[RH.itemL] = left ? 1 : 0
    // --- the cloak hangs down whichever way the body leans, swings with the stride and streams out
    let hang = max(0, -chestPitch)
    let wind = clamp(c.cape + 0.25 * st, 0, 1.2)
    let alive = 1 - dead
    let cl: Float = hang * 0.9 + 0.06 * st + 0.55 * wind + 0.02 * sin(c.idle * 1.3)
    b.R[RH.cloak] = aa(cl * alive - 0.12 * dead, X) * aa(0.05 * st * sL, Z)
    let hm0: Float = 0.1 * st * abs(sL) + 0.5 * wind + 0.08 * wind * sin(c.idle * 11)
    let hm: Float = hm0 + 0.03 * sin(c.idle * 1.1 + 1) + 0.3 * tuck
    b.R[RH.hem] = aa(hm * alive - 0.1 * dead, X)
    b.finish(&out)
  }

  /// Legs of a four-legged animal for a walk / trot / gallop phase.
  private struct Quad {
    var upper: [Int], lower: [Int]  // FL FR HL HR
  }
  private static func quadLegs(_ b: inout Builder, _ q: Quad, _ c: MControls, _ frontKnee: Float, _ hindKnee: Float) {
    let trot: [Float] = [0, PI, PI, 0]
    let gallop: [Float] = [0, 0.55, 2.6, 3.15]
    let st = c.stride, run = clamp(c.run, 0, 1)
    for i in 0..<4 {
      let ph = c.gait + romanMix(trot[i], gallop[i], run)
      let s = sin(ph), co = cos(ph)
      let front = i < 2
      let amp = (0.42 + 0.18 * run) * st
      let lift = max(0, co)
      if front {
        b.R[q.upper[i]] = swing(amp * s + 0.12 * lift * st)
        b.R[q.lower[i]] = knee(frontKnee * (0.55 + 0.35 * run) * lift * lift * st)
      } else {
        b.R[q.upper[i]] = swing(amp * s * 0.9 - 0.05 * st)
        b.R[q.lower[i]] = knee(-hindKnee * (0.45 + 0.3 * run) * lift * lift * st)
      }
    }
  }

  /// The chariot: the horses walk, gallop, rear and leap; the wheels roll; the
  /// charioteer leans into the gallop, swings his spear - or leaps out of the car.
  private static func chariotPose(_ c: MControls, _ out: inout MPose) {
    let sk = skeleton(RAsset.knight)
    var b = Builder(sk)
    let st = c.stride, run = clamp(c.run, 0, 1), g = c.gait
    let q = Quad(upper: [CHB.flu, CHB.fru, CHB.hlu, CHB.hru], lower: [CHB.fll, CHB.frl, CHB.hll, CHB.hrl])
    quadLegs(&b, q, c, 1.0, 1.0)
    let bodyPitch: Float = 0.05 * st * (0.3 + run) * sin(g * (run > 0.5 ? 1 : 2))
    let bob: Float = 0.008 * st * (1 + run) * abs(sin(g)) - 0.014 * st * (0.5 + run)
    b.off[CHB.horses] = vec3(0, bob, 0)
    b.R[CHB.horses] = aa(bodyPitch, X)
    b.R[CHB.neck] = aa(0.08 * st * sin(g + 0.8) + c.neck * 0.5, X)
    b.R[CHB.head] = aa(0.05 * sin(c.idle * 0.9) + c.headPitch, X)
    b.R[CHB.tail] = aa(0.25 * sin(c.idle * 1.3 + c.tail), Y) * aa(-0.35 * run * st, X)
    var rearA: Float = 0
    if c.rear > 0.001 {  // the horses rear: their bodies pivot about the hind hips
      let a = 0.85 * smooth(c.rear)
      rearA = a
      let pivot = vec3(0, sk.joint[CHB.hlu].y, sk.joint[CHB.hlu].z)
      b.rotateAbout(CHB.horses, aa(-a, X), pivot)
      for i in 2..<4 { b.R[q.upper[i]] = aa(a, X) * swing(0.25 * smooth(c.rear)) * b.R[q.upper[i]] }
      let paw = sin(c.idle * 9)
      b.R[CHB.flu] = swing(1.2 * smooth(c.rear) + 0.3 * paw) * b.R[CHB.flu]
      b.R[CHB.fru] = swing(1.2 * smooth(c.rear) - 0.3 * paw) * b.R[CHB.fru]
      b.R[CHB.fll] = knee(1.6 * smooth(c.rear))
      b.R[CHB.frl] = knee(1.6 * smooth(c.rear))
      b.R[CHB.neck] = aa(-0.3 * smooth(c.rear), X) * b.R[CHB.neck]
    }
    if c.tuck > 0.001 {  // legs folded for a leap
      let t = smooth(c.tuck)
      b.R[CHB.flu] = swing(1.1 * t) * b.R[CHB.flu]
      b.R[CHB.fru] = swing(1.0 * t) * b.R[CHB.fru]
      b.R[CHB.fll] = knee(1.9 * t) * b.R[CHB.fll]
      b.R[CHB.frl] = knee(1.8 * t) * b.R[CHB.frl]
      b.R[CHB.hlu] = swing(-0.9 * t) * b.R[CHB.hlu]
      b.R[CHB.hru] = swing(-0.85 * t) * b.R[CHB.hru]
    }
    // the car jolts along; the wheels roll
    b.off[CHB.car] = vec3(0, 0.005 * st * run * abs(sin(g * 2)), 0)
    b.R[CHB.car] = aa(0.015 * st * run * sin(g * 2 + 1), Z)
    b.R[CHB.wheelL] = aa(c.spin, X)
    b.R[CHB.wheelR] = aa(c.spin, X)
    // the charioteer
    b.off[CHB.driver] = c.rider
    let dl: Float = c.lean + 0.18 * run * st - 0.3 * rearA + 0.01 * sin(c.idle * 1.6)
    b.R[CHB.driver] = aa(c.riderSpin, X) * aa(c.twist, Y) * aa(dl, X)
    b.R[CHB.dhead] = aa(c.headYaw * 0.8, Y)
    b.R[CHB.dlarm] = aa(c.armL.y, Z) * swing(c.armL.x + 0.2 * run * st)
    b.R[CHB.drarm] = aa(-c.armR.y, Z) * swing(c.armR.x)
    b.R[CHB.dlfore] = swing(c.armL.z)
    b.R[CHB.drfore] = swing(c.armR.z)
    b.R[CHB.item] = aa(c.itemPitch, X)
    b.sc[CHB.item] = c.itemR > 0.5 ? 1 : 0
    let wind = clamp(c.cape + st * run, 0, 1.2)
    b.R[CHB.cloak] = aa(0.05 + 0.75 * wind + 0.1 * wind * sin(c.idle * 12), X)
    b.finish(&out)
  }

  /// The siege tower rolls and rocks; its ram swings out, its bridge and door drop,
  /// its scorpion turns, aims and shoots.
  private static func towerPose(_ c: MControls, _ out: inout MPose) {
    let sk = skeleton(RAsset.rook)
    var b = Builder(sk)
    let st = c.stride
    b.off[TWB.body] = vec3(0, 0.004 * st * abs(sin(c.gait * 2)), 0)
    b.R[TWB.body] = aa(c.lean, X) * aa(0.014 * st * sin(c.gait * 2) + c.twist * 0, Z)
    for w in TWB.wheel1...TWB.wheel4 { b.R[w] = aa(c.spin, X) }
    let k = c.kick
    b.off[TWB.ram] = vec3(0, 0.035 * k * k, 0.32 * k)
    b.R[TWB.ram] = aa(-0.08 * k, X)
    b.R[TWB.bridge] = aa(PI * 0.5 * smooth(c.bridge), X)
    b.R[TWB.door] = aa(PI * 0.52 * smooth(c.gate), X)
    b.R[TWB.scorpion] = aa(c.itemYaw, Y) * aa(c.itemPitch, X)
    b.sc[TWB.loaded] = c.hatch > 0.5 ? 0 : 1
    b.R[TWB.flag] = aa(0.25 * sin(c.idle * 1.3) + 0.1 * sin(c.idle * 3.7), Y)
    b.finish(&out)
  }

  /// The lion prowls, bounds, rears up to pounce and roars.
  private static func lionPose(_ c: MControls, _ out: inout MPose) {
    let sk = skeleton(RAsset.lion)
    var b = Builder(sk)
    let q = Quad(upper: [LNB.flu, LNB.fru, LNB.hlu, LNB.hru], lower: [LNB.fll, LNB.frl, LNB.hll, LNB.hrl])
    quadLegs(&b, q, c, 1.1, 1.0)
    let st = c.stride, run = clamp(c.run, 0, 1), g = c.gait
    let flex: Float = 0.08 * run * st * sin(g)  // the spine flexes in the bound
    let lift: Float = 0.012 * st * (1 + run) * abs(sin(g)) - 0.05 * c.crouch
    b.off[LNB.body] = vec3(0, lift, 0)
    b.R[LNB.body] = aa(flex + c.lean, X)
    b.R[LNB.neck] = aa(-flex * 0.8 + 0.1 * c.crouch, X)
    b.R[LNB.head] = aa(c.headYaw, Y) * aa(c.headPitch + 0.04 * sin(c.idle * 0.8), X)
    b.R[LNB.jaw] = aa(0.75 * clamp(c.jaw, 0, 1), X)
    let ts = c.tail + c.idle * 1.2
    b.R[LNB.tail1] = aa(0.3 * sin(ts), Y) * aa(0.4 - 0.5 * run * st, X)
    b.R[LNB.tail2] = aa(0.4 * sin(ts - 1), Y) * aa(-0.3, X)
    for i in 0..<4 {  // crouching: the legs fold under it
      if c.crouch <= 0.001 { break }
      let front = i < 2
      let up: Float = front ? -0.5 : 0.6
      let lo: Float = front ? 0.9 : -0.9
      b.R[q.upper[i]] = swing(up * c.crouch) * b.R[q.upper[i]]
      b.R[q.lower[i]] = knee(lo * c.crouch) * b.R[q.lower[i]]
    }
    if c.rear > 0.001 {  // rearing to pounce: the forepaws reach up and out, claws first
      let a = 0.9 * smooth(c.rear)
      let pivot = vec3(0, sk.joint[LNB.hlu].y, sk.joint[LNB.hlu].z)
      b.rotateAbout(LNB.body, aa(-a, X), pivot)
      b.R[LNB.hlu] = aa(a, X) * b.R[LNB.hlu]
      b.R[LNB.hru] = aa(a, X) * b.R[LNB.hru]
      b.R[LNB.flu] = swing(1.5 * smooth(c.rear)) * b.R[LNB.flu]
      b.R[LNB.fru] = swing(1.4 * smooth(c.rear)) * b.R[LNB.fru]
      b.R[LNB.fll] = knee(-0.3 * smooth(c.rear))
      b.R[LNB.frl] = knee(-0.3 * smooth(c.rear))
      b.R[LNB.neck] = aa(0.35 * smooth(c.rear), X) * b.R[LNB.neck]
    }
    if c.tuck > 0.001 {  // stretched out in a leap
      let t = smooth(c.tuck)
      b.R[LNB.flu] = swing(1.2 * t) * b.R[LNB.flu]
      b.R[LNB.fru] = swing(1.1 * t) * b.R[LNB.fru]
      b.R[LNB.hlu] = swing(-1.0 * t) * b.R[LNB.hlu]
      b.R[LNB.hru] = swing(-1.0 * t) * b.R[LNB.hru]
    }
    b.finish(&out)
  }

  /// What MControls mean to the Romans, besides walking (gait, stride), leaning,
  /// turning the head and the arms like the medieval figures:
  ///   draw    the blade is drawn (0 in its scabbard .. 1 in the right hand)
  ///   cape    the cloak streams out behind (running, leaping)
  ///   dead    limbs go limp (a body falling and lying still)
  ///   tuck    knees drawn up to the chest (a somersault)     kick   a front kick (right leg)
  ///   chariot: run, rear (the horses), spin (the wheels' roll), rider / riderSpin (the
  ///            charioteer leaping out of the car), itemPitch (his spear)
  ///   tower:   spin (wheels), kick (the ram: 1 swung out), bridge (lowered), gate (the
  ///            door ramp down), itemYaw / itemPitch (the scorpion's aim), hatch (its bolt
  ///            has been shot: 1 hides the one lying in it)
  ///   empress: hatch shows her goblet in the left hand
  ///   lion:    gait, stride, run, jaw, rear (rearing up to pounce), tuck (in the air)
  static func buildPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
    switch asset {
    case RAsset.pawn, RAsset.bishop, RAsset.queen, RAsset.king: humanPose(asset, c, &out)
    case RAsset.knight: chariotPose(c, &out)
    case RAsset.rook: towerPose(c, &out)
    case RAsset.lion: lionPose(c, &out)
    default: out.count = 0
    }
  }
}
