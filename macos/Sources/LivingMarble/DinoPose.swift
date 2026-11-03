// Turning animation controls into bone matrices for the dinosaurs: theropods
// striding, lunging, kicking and pouncing; the pteranodon walking on its folded
// wings or flying; the four-legged plodding, charging and rearing; and all of
// them going limp. The lowest foot is kept on the board (a stride lowers the
// hips, a crouch sinks the body).
import Foundation
import simd

extension Dino {
  // the pose kit (PoseKit.swift)
  private static let PI = PoseKit.PI
  private static let X = PoseKit.X, Y = PoseKit.Y, Z = PoseKit.Z
  @inline(__always) private static func aa(_ a: Float, _ axis: vec3) -> quat { PoseKit.aa(a, axis) }
  @inline(__always) private static func swing(_ a: Float) -> quat { PoseKit.swing(a) }
  @inline(__always) private static func knee(_ a: Float) -> quat { PoseKit.knee(a) }
  @inline(__always) private static func smooth(_ t: Float) -> Float { PoseKit.smooth(t) }
  @inline(__always) private static func rotationBetween(_ a: vec3, _ b: vec3) -> quat { PoseKit.rotationBetween(a, b) }
  private typealias Builder = PoseKit.Builder

  /// The tail's resting curl to one side (radians per bone): it drifts slowly from side to
  /// side over a couple of minutes (each animal has its own idle phase), lessens as the animal
  /// walks, and gives way to a swing of the tail ('itemYaw').
  private static func restCurl(_ c: MControls, _ amount: Float) -> Float {
    var side: Float = sin(c.idle * 0.045 + 1.3)
    side = clamp(side * 1.6, -1, 1)
    let use: Float = 1 - clamp(abs(c.itemYaw) * 2.5 + abs(c.itemPitch) * 1.5, 0, 1)
    let walking: Float = 1 - 0.7 * clamp(c.stride, 0, 1)
    return amount * side * walking * use
  }

  /// Moves the root so that the lowest of the ground points rests on the board
  /// ('weight' 0..1: how much; off in the air or when lying dead).
  private static func groundFit(_ asset: Int, _ b: inout Builder, _ root: Int, _ out: inout MPose, _ weight: Float) {
    if weight <= 0.001 { return }
    let gp = groundPoints(asset)
    var lo: Float = 1e9
    for g in gp { lo = min(lo, (out.bone[g.bone] * vec4(g.p, 1)).y) }
    if lo > 1e8 { return }
    b.off[root].y -= lo * weight
    b.finish(&out)
  }

  /// A theropod: raptor, spinosaurus, tyrannosaurus.
  private static func theroPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
    let sk = skeleton(asset)
    var b = Builder(sk)
    let st = c.stride, run = clamp(c.run, 0, 1), g = c.gait
    let dead = clamp(c.dead, 0, 1), alive = 1 - dead
    let tuck = smooth(c.tuck), kick = clamp(c.kick, 0, 1)
    let breath = sin(c.idle * (asset == DAsset.pawn ? 2.6 : 1.4)) * alive
    // --- the body: pitched by 'lean' (+ nose down), swaying with the stride
    let pitch: Float = c.lean + 0.1 * run * st - 0.25 * tuck
    b.R[THB.root] = aa(0.05 * st * sin(g) + c.twist * 0.3, Y) * aa(pitch, X) * aa(0.03 * st * cos(g), Z)
    b.R[THB.chest] = aa(c.twist * 0.5, Y) * aa(0.015 * breath, X)
    // --- the legs: thigh, shin and foot; the foot stays level unless it is folding up in a step
    let amp: Float = (0.42 + 0.25 * run) * st
    for side in 0..<2 {
      let L = side == 0
      let th = L ? THB.lthigh : THB.rthigh, sh = L ? THB.lshin : THB.rshin, ft = L ? THB.lfoot : THB.rfoot
      let ph: Float = g + (L ? 0 : PI)
      let s = sin(ph), lift = max(0, cos(ph))
      let k = c.crouch
      let kk: Float = L ? 0 : kick
      let thigh0: Float = amp * s + k * 0.55 + tuck * 1.3
      let thigh: Float = thigh0 + kk * 1.4 + dead * 0.35
      let shin0: Float = (0.75 + 0.35 * run) * lift * st + 0.06 * st + k * 0.95
      let shin: Float = shin0 + tuck * 0.15 + kk * 0.2 + dead * 0.6
      let foot0: Float = thigh - shin + 0.65 * lift * st
      let foot: Float = foot0 - tuck * 1.0 - kk * 0.9 + dead * 0.5
      let sideSign: Float = L ? 1 : -1
      b.R[th] = aa(-pitch, X) * swing(thigh) * aa(sideSign * (0.05 * tuck + 0.08 * dead), Z)
      b.R[sh] = knee(shin)
      b.R[ft] = aa(foot, X)
    }
    // --- the neck and head: 'neck' rears it back (+) or lunges (-); the dead throw it back
    let neckUp: Float = c.neck * 0.6 + dead * 0.9 - 0.04 * st * sin(g * 2)
    b.R[THB.neck] = aa(c.headYaw * 0.45 + c.twist * 0.3, Y) * aa(-neckUp + 0.012 * breath, X)
    let headPitch: Float = c.headPitch + c.neck * 0.25 + dead * 0.3
    b.R[THB.head] = aa(c.headYaw * 0.55, Y) * aa(headPitch, X) * aa(dead * 0.2, Z)
    let jawMax: Float = asset == DAsset.king ? 0.75 : (asset == DAsset.queen ? 0.7 : 0.8)
    b.R[THB.jaw] = aa(jawMax * clamp(c.jaw + 0.25 * dead, 0, 1), X)
    // --- the tail: a gentle curl at rest, the stride's sway, a lash to one side ('itemYaw'),
    // raised ('itemPitch'); limp when dead
    let curlAmount: Float = asset == DAsset.pawn ? 0.06 : (asset == DAsset.king ? 0.13 : 0.15)
    let curl: Float = restCurl(c, curlAmount) * alive
    let sway0: Float = 0.1 * sin(c.idle * 0.8 + c.tail) * (asset == DAsset.pawn ? 0.6 : 1.0)
    let sway: Float = (sway0 + 0.12 * st * sin(g)) * alive
    let tb = [THB.tail1, THB.tail2, THB.tail3]
    let share: [Float] = [0.4, 0.35, 0.25]
    for i in 0..<3 {
      let fi = Float(i)
      let first: Float = i == 0 ? 1 : 0
      let yaw0: Float = curl * (0.7 + 0.3 * fi) + sway * (0.6 + 0.4 * fi)
      let yaw: Float = yaw0 - c.twist * 0.25 * first + c.itemYaw * share[i]
      let up0: Float = i == 0 ? -c.lean * 0.5 + 0.1 * run * st + 0.3 * tuck : 0
      let up: Float = c.itemPitch * share[i] + up0 - 0.12 * dead + 0.03 * sin(c.idle * 0.7 + fi) * alive
      b.R[tb[i]] = aa(yaw, Y) * aa(up, X)
    }
    // --- the arms (the human offsets): reaching out, raised for balance in a pounce
    let tuckArm: vec3 = tuck * vec3(0.9, 0.4, -0.4)
    let aL = romanMix(c.armL + tuckArm, vec3(0.3, 0.25, -0.4), dead)
    let aR = romanMix(c.armR + tuckArm, vec3(0.3, 0.25, -0.4), dead)
    let armSwing: Float = 0.15 * st * sin(g)
    b.R[THB.larm] = aa(aL.y, Z) * swing(aL.x - armSwing)
    b.R[THB.rarm] = aa(-aR.y, Z) * swing(aR.x + armSwing)
    b.R[THB.lfore] = swing(aL.z)
    b.R[THB.rfore] = swing(aR.z)
    b.finish(&out)
    groundFit(asset, &b, THB.root, &out, (1 - dead) * (1 - tuck))
  }

  /// Four-legged: the triceratops and the brachiosaurus.
  private struct Quad {
    var upper: [Int], lower: [Int], foot: [Int]  // FL FR HL HR
  }
  /// The legs walking: each foot is kept level on the board while it bears weight, and
  /// flexes (the sole turning back) as it is lifted. A front leg's elbow points back, a
  /// hind leg's knee forward; a crouch folds them that way.
  private static func quadLegs(_ b: inout Builder, _ q: Quad, _ c: MControls, _ pitch: Float) {
    let walk: [Float] = [0, PI, PI * 1.5, PI * 0.5]  // a lateral-sequence walk
    let trot: [Float] = [0, PI, PI, 0]
    let st = c.stride, run = clamp(c.run, 0, 1)
    let dead = clamp(c.dead, 0, 1)
    for i in 0..<4 {
      let ph = c.gait + romanMix(walk[i], trot[i], run)
      let s = sin(ph), lift = max(0, cos(ph))
      let front = i < 2
      let amp: Float = (0.32 + 0.18 * run) * st
      let deadSw: Float = front ? 0.3 : -0.35, crouchSw: Float = front ? -0.4 : 0.5
      let sw: Float = amp * s + deadSw * dead + crouchSw * c.crouch
      // the elbow / knee: (+ folds the lower leg back)
      let bendFront: Float = (0.3 + 0.25 * run) * lift * st - 0.8 * c.crouch - 0.15 * dead
      let bendHind: Float = (0.7 + 0.3 * run) * lift * lift * st + 0.9 * c.crouch - 0.1 * dead
      let bend: Float = front ? bendFront : bendHind
      let flexAmount: Float = front ? 0.9 : 0.6
      let flex: Float = flexAmount * lift * st + 0.5 * dead
      b.R[q.upper[i]] = aa(-pitch, X) * swing(sw)
      b.R[q.lower[i]] = knee(bend)
      b.R[q.foot[i]] = aa(sw - bend + flex, X)  // level: undo the leg's swing and bend
    }
  }

  private static func quadPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
    let sk = skeleton(asset)
    var b = Builder(sk)
    let brachio = asset == DAsset.rook
    let st = c.stride, g = c.gait
    let dead = clamp(c.dead, 0, 1), alive = 1 - dead
    let breath = sin(c.idle * (brachio ? 0.9 : 1.3)) * alive
    let q = Quad(upper: [QDB.flu, QDB.fru, QDB.hlu, QDB.hru], lower: [QDB.fll, QDB.frl, QDB.hll, QDB.hrl],
                 foot: [QDB.flf, QDB.frf, QDB.hlf, QDB.hrf])
    let pitch: Float = c.lean + 0.02 * st * sin(g * 2)
    let roll: Float = 0.035 * st * sin(g) + 0.008 * breath
    b.R[QDB.body] = aa(c.twist * 0.4 + 0.04 * st * sin(g), Y) * aa(pitch, X) * aa(roll, Z)
    quadLegs(&b, q, c, pitch)
    // --- the neck: rears up (+) or lunges (-), the head turning with it
    let nb = [QDB.neck1, QDB.neck2, QDB.neck3]
    let sway: Float = (brachio ? 0.035 : 0.0) * sin(c.idle * 0.45) * alive
    let yawShare: Float = brachio ? 0.22 : 0.12
    let neckRear: Float = brachio ? 0.32 : 0.15, neckDead: Float = brachio ? 0.35 : 0.12
    for i in 0..<3 {
      let up: Float = neckRear * c.neck + neckDead * dead + 0.01 * breath - 0.02 * st * sin(g * 2)
      b.R[nb[i]] = aa(c.headYaw * yawShare + sway + c.twist * 0.15, Y) * aa(-up, X)
    }
    let headYaw: Float = c.headYaw * (1 - 3 * yawShare)
    let headNeck: Float = brachio ? 0.25 : 0.1, headDead: Float = brachio ? -0.3 : 0.2
    let headPitch: Float = c.headPitch + headNeck * c.neck - pitch * 0.5 + headDead * dead
    b.R[QDB.head] = aa(headYaw, Y) * aa(headPitch, X)
    let jawMax: Float = brachio ? 0.5 : 0.45
    b.R[QDB.jaw] = aa(jawMax * clamp(c.jaw + 0.2 * dead, 0, 1), X)
    // --- the tail: curled to one side at rest (slowly shifting from side to side), drawn
    // straight behind as it walks or swings it
    let tb = [QDB.tail1, QDB.tail2, QDB.tail3]
    let share: [Float] = [0.4, 0.35, 0.25]
    let curlAmount: Float = brachio ? 0.24 : 0.2
    let curl: Float = restCurl(c, curlAmount) * alive
    let tsway: Float = (0.08 * sin(c.idle * 0.6 + c.tail) + 0.1 * st * sin(g)) * alive
    for i in 0..<3 {
      let fi = Float(i)
      let first: Float = i == 0 ? 1 : 0
      let yaw0: Float = curl * (0.8 + 0.2 * fi) + tsway * (0.6 + 0.4 * fi)
      let yaw: Float = yaw0 + c.itemYaw * share[i] - c.twist * 0.2 * first
      let up0: Float = i == 0 ? -pitch * 0.5 : 0
      let up: Float = c.itemPitch * share[i] + up0 - 0.1 * dead
      b.R[tb[i]] = aa(yaw, Y) * aa(up, X)
    }
    // --- rearing up on the hind legs: the body pivots about the hips, the knees give a little
    // under the weight; the forelegs are drawn up, elbows back, the forefeet hanging and pawing
    if c.rear > 0.001 {
      let r = smooth(c.rear)
      let a: Float = (brachio ? 1.05 : 0.45) * r
      let h = sk.joint[QDB.hlu]
      let pivot = vec3(0, h.y, h.z)
      b.rotateAbout(QDB.body, aa(-a, X), pivot)
      let kneeGive: Float = 0.35 * r, thighFwd: Float = 0.2 * r
      for i in 2..<4 {
        b.R[q.upper[i]] = aa(a, X) * swing(thighFwd) * b.R[q.upper[i]]
        b.R[q.lower[i]] = knee(kneeGive) * b.R[q.lower[i]]
        b.R[q.foot[i]] = aa(thighFwd - kneeGive, X) * b.R[q.foot[i]]
      }
      let paw: Float = sin(c.idle * 6) * 0.25 * r
      let armUp: Float = brachio ? 0.35 : 0.2
      let elbowBack: Float = brachio ? 0.55 : 0.45
      let footHang: Float = brachio ? 0.7 : 0.5
      for i in 0..<2 {
        // (the body is pitched up by a: an upper arm that stays put in the body swings forward with it)
        let pawSide: Float = i == 0 ? paw : -paw
        let upper: Float = armUp * r - a + pawSide
        b.R[q.upper[i]] = swing(upper) * b.R[q.upper[i]]
        let elbow: Float = -elbowBack * r + 0.5 * paw
        b.R[q.lower[i]] = knee(elbow) * b.R[q.lower[i]]
        b.R[q.foot[i]] = aa(footHang * r, X) * b.R[q.foot[i]]
      }
      for i in 0..<3 {  // the tail props it up
        let tailUp: Float = i == 0 ? 0.7 : 0.15
        b.R[tb[i]] = aa(a * tailUp, X) * b.R[tb[i]]
      }
      for i in 0..<3 { b.R[nb[i]] = aa(a * 0.18, X) * b.R[nb[i]] }  // the neck leans forward over it
    }
    b.finish(&out)
    groundFit(asset, &b, QDB.body, &out, 1 - dead)
  }

  /// The pteranodon: on the ground it walks on its hands with the wings folded; in the
  /// air the body levels, the legs trail and the wings beat.
  private static func pteroPose(_ c: MControls, _ out: inout MPose) {
    let sk = skeleton(DAsset.knight)
    var b = Builder(sk)
    let st = c.stride, g = c.gait
    let spread = smooth(clamp(c.wings, 0, 1))
    let air = smooth(clamp(c.hover, 0, 1))
    let dead = clamp(c.dead, 0, 1), alive = 1 - dead
    let tuck = smooth(c.tuck)
    let breath = sin(c.idle * 2) * alive
    let pitch: Float = c.lean + 0.6 * air + 0.15 * (1 - spread) * (1 - air)
    b.R[PTB.body] = aa(c.twist * 0.5, Y) * aa(pitch, X) * aa(0.04 * st * sin(g), Z)
    // neck and head: level in flight, stabbing ('neck' -) or drawn back (+)
    let neckPitch: Float = -(c.neck * 0.7) + 0.35 * air - 0.5 * dead + 0.01 * breath
    b.R[PTB.neck] = aa(c.headYaw * 0.4, Y) * aa(neckPitch, X)
    let headPitch: Float = c.headPitch + c.neck * 0.25 - 0.15 * air + 0.25 * dead
    b.R[PTB.head] = aa(c.headYaw * 0.6, Y) * aa(headPitch, X)
    b.R[PTB.jaw] = aa(0.55 * clamp(c.jaw + 0.2 * dead, 0, 1), X)
    // the wings
    for side in 0..<2 {
      let s: Float = side == 0 ? 1 : -1
      let ua = side == 0 ? PTB.larm : PTB.rarm, fa = side == 0 ? PTB.lfore : PTB.rfore
      let mc = side == 0 ? PTB.lmeta : PTB.rmeta, fg = side == 0 ? PTB.lfinger : PTB.rfinger
      let S = sk.joint[ua], E = sk.joint[fa], W = sk.joint[mc], N = sk.joint[fg]
      var T = wingTip()
      T.x *= s
      // folded: the arm out and back, the forearm forward and down, the hand straight down to
      // the board, the finger folded up and back from the knuckle beside the arm, its tip over the
      // hips; stepping as it walks
      let ph: Float = g + (side == 0 ? 0 : PI)
      let step: Float = 0.35 * st * sin(ph), lift: Float = max(0, cos(ph)) * st
      let dA = normalize(vec3(s * 0.3, -0.6, -0.55 + step * 0.6))
      let dF = normalize(vec3(s * 0.05, -0.75 + 0.25 * lift, 0.66 + step))
      let dM = normalize(vec3(s * 0.03, -1.0, 0.05 + 0.6 * lift + step * 0.4))
      let dT = normalize(vec3(s * 0.2, 0.62, -0.76))
      let fA = rotationBetween(E - S, dA)
      let fF = rotationBetween(W - E, fA.inverse * dF)
      let fM = rotationBetween(N - W, (fA * fF).inverse * dM)
      let fT = rotationBetween(T - N, (fA * fF * fM).inverse * dT)
      // spread: beating (the outer joints lag behind), swept a little forward on the downstroke
      let fl = c.flap
      let beat = alive * spread
      let sA = aa(s * (0.55 * sin(fl) * beat + 0.05), Z) * aa(-s * 0.12 * cos(fl) * beat, Y)
      let sF = aa(s * 0.2 * sin(fl - 0.6) * beat, Z)
      let sM = aa(s * 0.15 * sin(fl - 1.0) * beat, Z)
      let sT = aa(s * (0.3 * sin(fl - 1.4) * beat - 0.04), Z) * aa(s * 0.06, Y)
      let u = spread
      // (glm::slerp: the shortest arc, like simd_slerp)
      b.R[ua] = simd_slerp(fA, sA, u)
      b.R[fa] = simd_slerp(fF, sF, u)
      b.R[mc] = simd_slerp(fM, sM, u)
      b.R[fg] = simd_slerp(fT, sT, u)
    }
    // the membranes: drawn in against the bones of the wing as it folds. Each trailing-edge
    // bone squeezes the membrane towards a line through its joint (along x at rest), turned onto
    // its wing bone as it folds.
    let k: Float = romanMix(0.12, 1.0, spread)
    let fold: Float = (1 - k) / 0.88
    for side in 0..<2 {
      let L = side == 0
      var T = wingTip()
      if !L { T.x = -T.x }
      let trail: [Int] = [L ? PTB.ltrailA : PTB.rtrailA, L ? PTB.ltrail : PTB.rtrail,
                          L ? PTB.ltrailH : PTB.rtrailH, L ? PTB.ltrail2 : PTB.rtrail2]
      let lead: [Int] = [L ? PTB.larm : PTB.rarm, L ? PTB.lfore : PTB.rfore,
                         L ? PTB.lmeta : PTB.rmeta, L ? PTB.lfinger : PTB.rfinger, -1]
      for i in 0..<4 {
        let a = sk.joint[lead[i]]
        let e = lead[i + 1] >= 0 ? sk.joint[lead[i + 1]] : T
        let f = normalize(e - a)
        let fx: Float = f.x < 0 ? -1 : 1
        b.st[trail[i]] = vec3(1.0, k, k)
        // (glm::slerp from the identity quat(1, 0, 0, 0))
        b.R[trail[i]] = simd_slerp(quat.identity, rotationBetween(vec3(fx, 0, 0), f), fold)
      }
    }
    // the legs: walking; trailed behind in flight; thrust forward, talons first ('tuck')
    for side in 0..<2 {
      let th = side == 0 ? PTB.lthigh : PTB.rthigh, sh = side == 0 ? PTB.lshin : PTB.rshin
      let ph: Float = g + (side == 0 ? PI : 0)
      let s = sin(ph), lift = max(0, cos(ph))
      let sw0: Float = 0.4 * st * s * (1 - air) - 1.3 * air * (1 - tuck)
      let sw: Float = sw0 + 1.2 * tuck + 0.3 * dead
      b.R[th] = aa(-pitch * (1 - air), X) * swing(sw)
      let kn0: Float = 0.6 * lift * st * (1 - air) + 0.15 * air
      let kn: Float = kn0 - 0.2 * tuck + 0.4 * dead
      b.R[sh] = knee(kn)
    }
    b.finish(&out)
    groundFit(DAsset.knight, &b, PTB.body, &out, (1 - air) * (1 - dead) * (1 - tuck))
  }

  /// What MControls mean to the dinosaurs, besides walking (gait, stride, run), turning
  /// the head (headYaw, headPitch) and leaning (lean: + the body pitches nose down):
  ///   jaw       mouth open (0..1)            neck   + rears the neck back, - lunges
  ///   twist     the body turns (yaw)         crouch the legs bend, the body sinks
  ///   armR/armL theropods' arms: reach forward, out, elbow bend (as the humans')
  ///   itemYaw   the tail swung to one side (radians, spread along it)   itemPitch the tail raised
  ///   kick      a raptor's killing claw: the right leg kicks forward (0..1); 'tuck' both legs
  ///             forward, claws out (a pounce), or a pteranodon's feet thrust forward (talons)
  ///   rear      four-legged: up on the hind legs (0..1); theropods: the body rears up
  ///   wings     the pteranodon's wings: 0 folded (walking on its hands) .. 1 spread
  ///   flap      wing beat phase (radians)    hover  the pteranodon in the air: its body level, legs trailed (0..1)
  ///   dead      limp: a body falling and lying still on its side (0..1)
  ///   tail      tail swish phase
  static func buildPose(_ asset: Int, _ c: MControls, _ out: inout MPose) {
    switch asset {
    case DAsset.pawn, DAsset.queen, DAsset.king: theroPose(asset, c, &out)
    case DAsset.knight: pteroPose(c, &out)
    case DAsset.bishop, DAsset.rook: quadPose(asset, c, &out)
    default: out.count = 0
    }
  }
}
