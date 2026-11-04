// Sculpting the dinosaurs. Like the medieval army and the Romans, every animal
// is a list of parts - ellipsoids for muscle and belly, tapering cones for
// necks, tails and limbs, curved horns and claws, thin sheets for wings, frills
// and sails - each bound to a bone and a material, blended into one signed
// distance field and meshed; every vertex is then skinned to the bone(s) of the
// part(s) it lies on and painted with their material. The scales, feathers and
// skin are drawn by the shader.
//
// Each animal is sculpted in its own units and scaled afterwards (the skeleton
// with it). All of them face +z with their feet on y = 0.
// (The joints and skeletons are in Dino.swift, the sculpting kit - shapes, Model,
// buildAsset - in Sculpt.swift.)
import Foundation
import simd

/// The legs of a four-legged animal (Dino.addQuadLegs).
struct DinoQuadLegSpec {
  // front: the upper arm's radius, the elbow's, the forearm's, the wrist's
  var upperR: Float = 0, elbowR: Float = 0, foreR: Float = 0, wristR: Float = 0
  var thighR: Float = 0, kneeR: Float = 0, shinR: Float = 0, ankleR: Float = 0  // hind
  var fPad = vec3(0), hPad = vec3(0)  // the pads' half-sizes
  var thigh = vec3(0)                 // the thigh muscle's half-size
  var hooves = false
}

extension Dino {
  // ---------------------------------------------------------------------------
  // Shared parts
  // ---------------------------------------------------------------------------
  /// A tapering horn or claw along a circular arc: it starts out along 'dir' and bends
  /// towards 'down' by 'curl' radians in all.
  private static func addClaw(_ m: Model, _ bone: Int, _ base: vec3, _ dir: vec3, _ down: vec3, _ len: Float,
                              _ r: Float, _ curl: Float, _ mat: Int = DMat.claw, _ k: Float = 0) {
    let N = 4
    var p = base, d = normalize(dir)
    var ax = cross(d, down)  // a fixed axis: a true arc, which may curl on past 'down'
    ax = length(ax) > 1e-5 ? normalize(ax) : vec3(1, 0, 0)
    let step = angleAxis(curl / Float(N), ax)
    for i in 0..<N {
      d = normalize(step * d)
      let q = p + d * (len / Float(N))
      let r0: Float = r * (1 - 0.85 * Float(i) / Float(N))
      let r1: Float = r * (1 - 0.85 * Float(i + 1) / Float(N))
      m.add(bone, mat, i == 0 ? k : 0, coneS(p, q, r0, r1))
      p = q
    }
  }

  /// An eye looking along 'out': a glossy iris with a slit pupil, under a ridge of brow.
  private static func addEye(_ m: Model, _ bone: Int, _ c: vec3, _ r: Float, _ out0: vec3) {
    let out = normalize(out0)
    m.sub(DMat.skin, r * 0.4, sphereS(c + out * r * 0.5, r * 1.05))
    m.add(bone, DMat.eye, r * 0.15, sphereS(c, r))
    let F = frameY(vec3(0, 1, 0), out)
    m.add(bone, DMat.dark, 0, ellS(c + out * r * 0.82, vec3(r * 0.22, r * 0.62, r * 0.24), F))
    let brow = c + vec3(0, r * 0.88, 0) - out * r * 0.3
    m.add(bone, DMat.skin, r * 0.5, ellS(brow, vec3(r * 1.1, r * 0.38, r * 1.05), F))
  }

  /// A row of teeth along the jaw from a to b (both sides of the mouth: x and -x), pointing 'dir'.
  /// Each tooth is rigid on its jaw: it never bends towards the other one.
  private static func addTeeth(_ m: Model, _ bone: Int, _ a: vec3, _ b: vec3, _ n: Int, _ dir: vec3, _ len: Float,
                               _ r: Float) {
    for i in 0..<n {
      let u: Float = n > 1 ? Float(i) / Float(n - 1) : 0
      let p = romanMix(a, b, u)
      let s: Float = 1 - 0.35 * abs(u - 0.35)
      for sd in stride(from: -1, through: 1, by: 2) {
        let q = vec3(p.x * Float(sd), p.y, p.z)
        // (curved a little along their length, like a real tooth)
        let tip = q + dir * len * s
        let mid = romanMix(q, tip, 0.55) + vec3(0, 0, len * s * 0.08)
        m.addR(bone, DMat.teeth, 0, coneS(q, mid, r * s, r * s * 0.62))
        m.addR(bone, DMat.teeth, 0, coneS(mid, tip, r * s * 0.62, r * 0.12))
      }
    }
  }

  /// The mouth. Every animal is sculpted with its mouth agape: the lower jaw's parts (and the
  /// paint on them), from 'from' on, are turned down about the hinge by gape(), so that the jaws,
  /// their teeth, the palate and the tongue are surfaces of their own that come apart cleanly as
  /// the mouth opens (blended into one, they would stretch a sheet of skin between them). The pose
  /// closes the mouth again.
  private static func openJaw(_ m: Model, _ from: Int, _ hinge: vec3, _ angle: Float) {
    let R = rotXm(angle), Rt = R.transpose
    m.moveParts(from..<m.partCount, fwd: { p in R * (p - hinge) + hinge }, back: { p in Rt * (p - hinge) + hinge })
  }

  /// Trims the lower jaw's parts (from 'from' to 'to', before openJaw) to the mouth line from the
  /// hinge along 'dir', a little rounded at the rim, so that they don't reach up into the upper
  /// jaw; or (upper) the upper jaw's parts, in front of the hinge, so that they don't hang down
  /// over the lower one.
  private static func trimJaw(_ m: Model, _ from: Int, _ hinge: vec3, _ dir0: vec3, _ k: Float, upper: Bool = false,
                              to: Int? = nil) {
    let dir = normalize(vec3(0, dir0.y, dir0.z))
    let up = vec3(0, dir.z, -dir.y)
    if upper {
      m.reshapeParts(from..<(to ?? m.partCount)) { f, p in
        let d = p - hinge
        return SDF.smax(f, min(-dot(d, up), dot(d, dir)), k)
      }
    } else {
      m.reshapeParts(from..<(to ?? m.partCount)) { f, p in SDF.smax(f, dot(p - hinge, up), k) }
    }
  }

  /// The throat under the lower jaw: skin that goes with the jaw at the front, but towards the
  /// back (where it runs into the neck) more and more with the head, so that it stretches smoothly
  /// as the jaw moves.
  private static func addThroat(_ m: Model, _ jawBone: Int, _ headBone: Int, _ mat: Int, _ k: Float, _ s: Shape,
                                _ hinge: vec3, _ dir0: vec3, _ len: Float) {
    let dir = normalize(vec3(0, dir0.y, dir0.z))
    m.addStretch(jawBone, headBone, mat, k, s) { p in smoothstep(-0.15 * len, len, dot(p - hinge, dir)) }
  }

  /// The inside of the mouth, painted over the skin: the palate under the upper jaw, or (upper =
  /// false, painted with the lower jaw's parts so that it turns with them) the tongue and gums on
  /// the lower jaw. The mouth line (where the jaws meet when closed) runs from the hinge forward
  /// along 'dir' (in the yz plane) to 'len' from it. The palate is the upper jaw's surface over
  /// the line bisecting the gape and no more than 'depth' above the mouth line; the tongue the
  /// lower jaw's under that line (its own, before it is turned) and no more than 'depth' below it;
  /// both inside the half-width hw0 (at the hinge) .. hw1 (at the front), short of the lips.
  private static func paintMouth(_ m: Model, _ hinge: vec3, _ dir0: vec3, _ len: Float, _ gapeAngle: Float,
                                 _ upper: Bool, _ depth: Float, _ hw0: Float, _ hw1: Float,
                                 _ mat: Int = DMat.mouth, _ only: Int = DMat.skin) {
    let dir = normalize(vec3(0, dir0.y, dir0.z))
    let up = vec3(0, dir.z, -dir.y)
    let bis = rotXm(upper ? gapeAngle * 0.5 : -gapeAngle * 0.5) * up  // the bisector's normal
    let sg: Float = upper ? 1 : -1
    m.paint(mat, fnS({ p in
      let q = p - hinge
      let t = dot(q, dir), h = dot(q, up) * sg, b = dot(q, bis) * sg
      let hw = romanMix(hw0, hw1, clamp(t / len, 0, 1))
      let d = max(max(h - depth, -b), abs(p.x) - hw)
      return max(d, max(-t, t - len))
    }, hinge + dir * len * 0.5, len * 0.75 + max(hw0, hw1) + depth), only)
  }

  /// A tube through a polyline: tapering cones, each bound to its bone.
  private static func addTube(_ m: Model, _ mat: Int, _ pts: [vec3], _ radii: [Float], _ bones: [Int], _ k: Float) {
    for i in 0..<max(0, pts.count - 1) { m.add(bones[i], mat, k, coneS(pts[i], pts[i + 1], radii[i], radii[i + 1])) }
  }

  /// A thin sheet stretched over a triangle (a wing, a frill, a feather vane).
  private static func sheetS(_ a: vec3, _ b: vec3, _ c: vec3, _ t: Float) -> Shape { triS(a, b, c, t) }

  /// The mouth line: the inside of the lips painted where the jaws meet (y = level),
  /// between z0 and z1, over the skin.
  private static func addMouthLine(_ m: Model, _ level: Float, _ z0: Float, _ z1: Float, _ halfW: Float) {
    m.paint(DMat.mouth, fnS({ p in
      let d: Float = abs(p.y - level) - 0.004
      return max(max(d, abs(p.x) - halfW), max(z0 - p.z, p.z - z1))
    }, vec3(0, level, (z0 + z1) * 0.5), (z1 - z0) * 0.6 + halfW), DMat.skin)
  }

  /// Folds of skin round a neck: soft rings about the axis a->b (radius ra at a, rb at b) at the
  /// fractions given, sunk into the surface so that only a ridge shows.
  private static func addFolds(_ m: Model, _ bone: Int, _ a: vec3, _ b: vec3, _ ra: Float, _ rb: Float, _ at: [Float],
                               _ r: Float) {
    let F = frameY(b - a)
    for t in at {
      let R: Float = romanMix(ra, rb, t) * 0.97 - r * 0.35
      m.add(bone, DMat.skin, r * 0.9, torusS(romanMix(a, b, t), R, r, F))
    }
  }

  /// A tail: a tube through points along the midline, each segment bound to its bone; thick
  /// with muscle at the root, then tapering quickly into a long, thin end.
  private static func addTail(_ m: Model, _ mat: Int, _ pts: [vec3], _ r0: Float, _ r1: Float, _ bones: [Int],
                              _ k: Float) {
    let n = pts.count
    func rad(_ i: Int) -> Float {
      let t = Float(i) / Float(n - 1)
      let s: Float = pow(1 - t, 1.35)
      return romanMix(r1, r0, s)
    }
    for i in 0..<max(0, n - 1) { m.add(bones[i], mat, k, coneS(pts[i], pts[i + 1], rad(i), rad(i + 1))) }
  }

  /// A row of knobbly scutes along the back (y on the midline, from z0 to z1).
  private static func addScutes(_ m: Model, _ bone: Int, _ a: vec3, _ b: vec3, _ n: Int, _ r: Float) {
    for i in 0..<n {
      let u: Float = n > 1 ? Float(i) / Float(n - 1) : 0
      let p = romanMix(a, b, u)
      m.add(bone, DMat.scute, r * 0.8, ellS(p, vec3(r, r * 0.7, r * 1.3)))
    }
  }

  // ---------------------------------------------------------------------------
  // Theropod legs: a muscular thigh, a long shin, the foot (metatarsus) and three toes
  // ---------------------------------------------------------------------------
  private static func addTheroLegs(_ m: Model, _ j: DinoTheroJ, _ thighR: Float, _ shinR: Float, _ footR: Float,
                                   _ toeLen: Float, _ thighMat: Int, _ sickle: Bool) {
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let th = sd > 0 ? THB.lthigh : THB.rthigh, sh = sd > 0 ? THB.lshin : THB.rshin
      let ft = sd > 0 ? THB.lfoot : THB.rfoot
      let hip = vec3(j.hipL.x * x, j.hipL.y, j.hipL.z), knee = vec3(j.kneeL.x * x, j.kneeL.y, j.kneeL.z)
      let ankle = vec3(j.ankleL.x * x, j.ankleL.y, j.ankleL.z), ball = vec3(j.ballL.x * x, j.ballL.y, j.ballL.z)
      // the drumstick: thigh muscles bulging out over the knee
      m.add(th, thighMat, thighR * 0.5, coneS(hip, knee, thighR, thighR * 0.62))
      let thighC = romanMix(hip, knee, 0.45) + vec3(x * thighR * 0.15, 0, -thighR * 0.3)
      let thighRad = vec3(thighR * 0.85, length(knee - hip) * 0.55, thighR * 1.05)
      m.add(th, thighMat, thighR * 0.6, ellS(thighC, thighRad, frameY(hip - knee)))
      // the shin, with its calf muscle
      m.add(sh, DMat.skin, shinR * 0.6, coneS(knee, ankle, shinR, shinR * 0.6))
      let calfC = romanMix(knee, ankle, 0.3) + vec3(0, 0, -shinR * 0.45)
      let calfRad = vec3(shinR * 0.85, length(ankle - knee) * 0.3, shinR)
      m.add(sh, DMat.skin, shinR * 0.8, ellS(calfC, calfRad, frameY(knee - ankle)))
      // the foot and the three toes, each with its claw
      m.add(ft, DMat.skin, footR * 0.5, coneS(ankle, ball, footR, footR * 0.85))
      for k in -1...1 {
        if sickle && k == -1 { continue }  // (a raptor's inner toe is the raised one)
        let spread: Float = sickle ? (k == 0 ? -0.15 : 0.25) : 0.32 * Float(k)
        let d = normalize(vec3(sin(spread) * x, -0.12, cos(spread)))
        let L: Float = toeLen * (k == 0 ? 1.0 : 0.8)
        let t0 = ball + vec3(0, -footR * 0.25, 0)
        var t1 = t0 + d * L
        t1.y = max(footR * 0.45, t1.y)
        m.add(ft, DMat.skin, footR * 0.4, coneS(t0, t1, footR * 0.72, footR * 0.42))
        addClaw(m, ft, t1 - d * footR * 0.2, d + vec3(0, 0.25, 0), vec3(0, -1, 0), footR * 1.6, footR * 0.42, 1.1)
      }
      if sickle {
        // the killing claw on the second toe, held up off the ground
        let t0 = ball + vec3(-x * footR * 0.55, footR * 0.3, footR * 0.2)
        let t1 = t0 + vec3(-x * 0.004, 0.016, 0.02)
        m.add(ft, DMat.skin, footR * 0.3, coneS(t0, t1, footR * 0.62, footR * 0.5))
        addClaw(m, ft, t1, vec3(0, 0.9, 0.6), vec3(0, -0.2, 1), 0.052, footR * 0.62, 2.4)
      }
      // the dewclaw at the back
      if !sickle {
        let dew = romanMix(ankle, ball, 0.75) + vec3(-x * footR * 0.6, 0, -footR * 0.6)
        addClaw(m, ft, dew, vec3(-x * 0.3, -0.6, -0.5), vec3(0, -1, 0), footR * 1.1, footR * 0.35, 0.6)
      }
    }
  }

  /// Theropod arms: upper arm, forearm and the hand's fingers with hooked claws.
  private static func addTheroArms(_ m: Model, _ j: DinoTheroJ, _ rU: Float, _ rF: Float, _ fingers: Int,
                                   _ clawLen: Float, _ mat: Int, _ thumb: Float = 0) {
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let ua = sd > 0 ? THB.larm : THB.rarm, fa = sd > 0 ? THB.lfore : THB.rfore
      let sh = vec3(j.shL.x * x, j.shL.y, j.shL.z), el = vec3(j.elL.x * x, j.elL.y, j.elL.z)
      let wr = vec3(j.wrL.x * x, j.wrL.y, j.wrL.z)
      m.add(ua, mat, rU * 0.8, coneS(sh, el, rU, rU * 0.8))
      m.add(fa, mat, rF * 0.6, coneS(el, wr, rF, rF * 0.8))
      let fwd = normalize(wr - el)
      for f in 0..<fingers {
        let a: Float = fingers > 1 ? (Float(f) / Float(fingers - 1) - 0.5) * 0.6 : 0
        let d = normalize(fwd + vec3(x * a, -0.35, 0))
        let f1 = wr + d * rF * 2.2
        m.add(fa, DMat.skin, rF * 0.3, coneS(wr, f1, rF * 0.55, rF * 0.4))
        addClaw(m, fa, f1, d, vec3(0, -1, 0), clawLen, rF * 0.42, 1.3)
      }
      if thumb > 0 {  // the spinosaurus' great hooked thumb claw
        let tb = wr + vec3(-x * rF * 0.4, -rF * 0.3, rF * 0.4)
        addClaw(m, fa, tb, vec3(-x * 0.25, -0.15, 1.0), vec3(0, -1, 0), thumb, rF * 0.75, 1.6)
      }
    }
  }

  // ---------------------------------------------------------------------------
  // Four-legged legs: the upper leg under its muscles, the elbow (pointing back) or the
  // knee (forward), the forearm or shin with its calf, the wrist or ankle, and the foot: a
  // broad pad that spreads on the board, with hooves (triceratops) or claws (brachiosaurus).
  // ---------------------------------------------------------------------------
  private static func addQuadLegs(_ m: Model, _ j: DinoQuadJ, _ L: DinoQuadLegSpec) {
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let fu = sd > 0 ? QDB.flu : QDB.fru, fl = sd > 0 ? QDB.fll : QDB.frl, ff = sd > 0 ? QDB.flf : QDB.frf
      let hu = sd > 0 ? QDB.hlu : QDB.hru, hl = sd > 0 ? QDB.hll : QDB.hrl, hf = sd > 0 ? QDB.hlf : QDB.hrf
      func S(_ v: vec3) -> vec3 { vec3(v.x * x, v.y, v.z) }
      let FU = S(j.flu), FE = S(j.fll), FW = S(j.fwrist), FF = S(j.ffoot)
      let HU = S(j.hlu), HK = S(j.hll), HA = S(j.hankle), HF = S(j.hfoot)
      // --- the foreleg: the upper arm under the shoulder muscles (deltoid in front, triceps
      // behind, bulging out over the elbow)
      m.add(fu, DMat.skin, 0.04, coneS(FU + vec3(0, 0.035, -0.005), FE, L.upperR, L.elbowR * 1.05))
      let upperLen: Float = length(FE - FU)
      let triC: vec3 = romanMix(FU, FE, 0.42) + vec3(x * 0.012, 0, -0.014)
      let triRad = vec3(L.upperR * 0.8, upperLen * 0.42, L.upperR * 0.86)
      m.add(fu, DMat.skin, 0.03, ellS(triC, triRad, frameY(FU - FE)))
      let delC: vec3 = romanMix(FU, FE, 0.3) + vec3(x * 0.006, 0, L.upperR * 0.45)
      let delRad = vec3(L.upperR * 0.6, upperLen * 0.3, L.upperR * 0.5)
      m.add(fu, DMat.skin, 0.02, ellS(delC, delRad, frameY(FU - FE)))
      // the point of the elbow, the forearm with its muscles thick below the elbow, the wrist
      m.add(fl, DMat.skin, 0.014, sphereS(FE + vec3(x * 0.004, 0.004, -L.elbowR * 0.35), L.elbowR * 0.95))
      m.add(fl, DMat.skin, 0.018, coneS(FE, FW, L.foreR, L.wristR))
      let foreC: vec3 = romanMix(FE, FW, 0.3) + vec3(x * L.foreR * 0.2, 0, L.foreR * 0.15)
      let foreRad = vec3(L.foreR * 0.95, length(FW - FE) * 0.3, L.foreR)
      m.add(fl, DMat.skin, 0.02, ellS(foreC, foreRad, frameY(FE - FW)))
      m.add(ff, DMat.skin, 0.012, sphereS(FW, L.wristR * 1.02))
      // the forefoot: a column down to a broad pad spreading under the weight
      let fp = vec3(FW.x + x * 0.004, L.fPad.y, FF.z + 0.004)
      m.add(ff, DMat.skin, 0.02, coneS(FW, fp + vec3(0, L.fPad.y * 0.4, 0), L.wristR, L.fPad.x * 0.85))
      m.add(ff, DMat.skin, 0.022, ellS(fp, L.fPad))
      // --- the hind leg: the drumstick of thigh muscle, the kneecap, the shin and its calf
      let thighC: vec3 = romanMix(HU, HK, 0.42) + vec3(x * 0.012, 0, -0.01)
      m.add(hu, DMat.skin, 0.05, ellS(thighC, L.thigh, frameY(HU - HK)))
      m.add(hu, DMat.skin, 0.03, coneS(HU, HK, L.thighR, L.kneeR * 1.05))
      m.add(hl, DMat.skin, 0.014, sphereS(HK + vec3(x * 0.004, 0, L.kneeR * 0.25), L.kneeR))
      m.add(hl, DMat.skin, 0.02, coneS(HK, HA, L.shinR, L.ankleR))
      let calfC: vec3 = romanMix(HK, HA, 0.33) + vec3(0, 0, -L.shinR * 0.45)
      let calfRad = vec3(L.shinR * 0.85, length(HA - HK) * 0.32, L.shinR * 0.9)
      m.add(hl, DMat.skin, 0.025, ellS(calfC, calfRad, frameY(HK - HA)))
      m.add(hf, DMat.skin, 0.012, sphereS(HA + vec3(0, 0, -L.ankleR * 0.2), L.ankleR * 1.05))  // the heel
      // the hind foot: the metatarsus down and forward to the pad
      let hp = vec3(HA.x + x * 0.004, L.hPad.y, HF.z + 0.006)
      m.add(hf, DMat.skin, 0.02, coneS(HA, hp + vec3(0, L.hPad.y * 0.4, 0.01), L.ankleR, L.hPad.x * 0.85))
      m.add(hf, DMat.skin, 0.022, ellS(hp, L.hPad))
      // toes: hooves on short, thick toes; or a sauropod's claws
      if L.hooves {
        for k in -1...1 {
          let a: Float = 0.38 * Float(k) + 0.1 * x
          let d = vec3(sin(a), 0, cos(a))
          let toeF = frameY(vec3(0, 1, 0), d)
          let f0: vec3 = fp + vec3(0, -0.002, 0) + d * L.fPad.z * 0.55
          let f1: vec3 = fp + d * (L.fPad.z * 1.02) + vec3(0, -0.009, 0)
          m.add(ff, DMat.skin, 0.008, coneS(f0, f1, 0.0135, 0.012))
          let fh: vec3 = f1 + d * 0.006 + vec3(0, -0.002, 0)
          m.add(ff, DMat.claw, 0.004, ellS(fh, vec3(0.0115, 0.0105, 0.011), toeF))
          let g0: vec3 = hp + vec3(0, -0.002, 0) + d * L.hPad.z * 0.55
          let g1: vec3 = hp + d * (L.hPad.z * 1.02) + vec3(0, -0.009, 0)
          m.add(hf, DMat.skin, 0.008, coneS(g0, g1, 0.0145, 0.0125))
          let gh: vec3 = g1 + d * 0.006 + vec3(0, -0.002, 0)
          m.add(hf, DMat.claw, 0.004, ellS(gh, vec3(0.012, 0.011, 0.0115), toeF))
        }
      } else {
        // forefoot: a ring of blunt nails round the front of the pad and the thumb claw, turned in
        for k in -1...1 {
          let a: Float = 0.42 * Float(k) - 0.15 * x
          let d = vec3(sin(a), 0, cos(a))
          let nail: vec3 = fp + d * (L.fPad.z + 0.002) + vec3(0, -0.004, 0)
          m.add(ff, DMat.claw, 0.004, ellS(nail, vec3(0.012, 0.011, 0.008), frameY(vec3(0, 1, 0), d)))
        }
        let thumb: vec3 = fp + vec3(-x * L.fPad.x * 0.7, 0.004, L.fPad.z * 0.55)
        addClaw(m, ff, thumb, vec3(-x * 0.35, 0.25, 1.0), vec3(0, -1, 0), 0.032, 0.0105, 0.9)
        // hind foot: three big sickle-shaped claws turned outward, a smaller fourth
        for k in 0..<4 {
          let fk = Float(k)
          let a: Float = -0.45 * x + 0.32 * x * fk
          let d = vec3(sin(a), 0, cos(a))
          let base: vec3 = hp + d * L.hPad.z * 0.8 + vec3(0, 0.002, 0)
          let len: Float = k < 3 ? 0.03 - 0.004 * fk : 0.014
          let cr: Float = k < 3 ? 0.0095 : 0.007
          addClaw(m, hf, base, normalize(d + vec3(x * 0.4, 0.2, 0)), vec3(0, -1, 0), len, cr, 1.0)
        }
      }
    }
  }

  /// Moves a built animal by d (its skeleton is placed by toModel to match). The C++
  /// version moves every part of the model (shape, bounding sphere, weight) before it
  /// is meshed; the Swift Model keeps its parts to itself, so the model is meshed where
  /// it stands - in the box moved back by d - and its meshes are moved instead: the same
  /// surface, skinning and paint. (The animals have no shards or slice halves.)
  private static func shiftAll(_ A: inout PieceAssets, _ d: vec3) {
    A.lod0.translate(d)
    A.lod1.translate(d)
  }

  // ===========================================================================
  // The pieces
  // ===========================================================================
  /// The velociraptor: feathers over the body, the arms (half-wings) and the tail's
  /// end, a scaly snout and legs, a sickle claw on each foot.
  private static func raptorModel() -> Model {
    let j = theroBase(DAsset.pawn)
    let m = Model()
    m.setSkeleton(skeleton(DAsset.pawn))
    // body: hips and belly, the chest
    m.add(THB.root, DMat.feather, 0, ellS(vec3(0, 0.272, -0.035), vec3(0.044, 0.048, 0.088)))
    m.add(THB.chest, DMat.feather, 0.03, ellS(vec3(0, 0.262, 0.068), vec3(0.048, 0.056, 0.08), rotXm(-0.18)))
    // a ruff of longer feathers on the back of the neck and over the shoulders
    m.add(THB.chest, DMat.feather, 0.015, ellS(vec3(0, 0.3, 0.1), vec3(0.036, 0.02, 0.05), rotXm(-0.5)))
    // the neck in an S
    addTube(m, DMat.feather, [vec3(0, 0.282, 0.125), vec3(0, 0.33, 0.172), vec3(0, 0.375, 0.19), vec3(0, 0.41, 0.2)],
            [0.032, 0.026, 0.023, 0.021], [THB.neck, THB.neck, THB.neck], 0.016)
    // the head: a long, low skull and snout
    let hc = vec3(0, 0.43, 0.243)
    m.add(THB.head, DMat.skin, 0, ellS(hc, vec3(0.026, 0.025, 0.046)))
    m.add(THB.head, DMat.skin, 0.016, coneS(hc + vec3(0, 0.002, 0.02), vec3(0, 0.421, 0.333), 0.021, 0.0095))
    m.add(THB.head, DMat.feather, 0.012, ellS(hc + vec3(0, 0.012, -0.028), vec3(0.022, 0.017, 0.03)))  // feathers on the nape
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      addEye(m, THB.head, hc + vec3(s * 0.021, 0.009, -0.004), 0.0102, vec3(s * 1.0, 0.1, 0.5))
      m.paint(DMat.dark, ellS(vec3(s * 0.008, 0.428, 0.322), vec3(0.0035, 0.0025, 0.005)), DMat.skin)  // nostril
    }
    // the palate; the lower jaw, sculpted agape (openJaw) with its tongue, gums and teeth
    let mouthDir = vec3(0, -0.02, 1)
    let G = gape(DAsset.pawn)
    paintMouth(m, j.jaw, mouthDir, 0.125, G, true, 0.008, 0.012, 0.006)
    let jaw0 = m.partCount
    m.add(THB.jaw, DMat.skin, 0.008, coneS(vec3(0, 0.405, 0.222), vec3(0, 0.403, 0.327), 0.015, 0.0072))
    addThroat(m, THB.jaw, THB.head, DMat.belly, 0.01, ellS(vec3(0, 0.396, 0.235), vec3(0.013, 0.01, 0.03)), j.jaw, mouthDir, 0.05)
    trimJaw(m, jaw0, j.jaw, mouthDir, 0.003)
    m.add(THB.jaw, DMat.mouth, 0.004, ellS(vec3(0, 0.407, 0.265), vec3(0.0075, 0.004, 0.04)))  // the tongue
    paintMouth(m, j.jaw, mouthDir, 0.125, G, false, 0.008, 0.011, 0.005)
    addTeeth(m, THB.jaw, vec3(0.008, 0.41, 0.262), vec3(0.005, 0.408, 0.318), 5, vec3(0, 1, 0.1), 0.0065, 0.0024)
    openJaw(m, jaw0, j.jaw, G)
    addTeeth(m, THB.head, vec3(0.0125, 0.416, 0.255), vec3(0.0075, 0.415, 0.326), 6, vec3(0, -1, 0.15), 0.008, 0.0042)
    // arms folded at the sides, a folded half-wing of quills on each: separate vanes rooted along
    // the forearm and the hand, overlapping like a closed fan, the short ones at the elbow
    // hanging down and back, the long ones at the hand lying back along the forearm and past the
    // elbow; each tip rounded, so that the trailing edge is scalloped
    addTheroArms(m, j, 0.0135, 0.0095, 3, 0.013, DMat.feather)
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      let fa = sd > 0 ? THB.lfore : THB.rfore
      let el = vec3(j.elL.x * s, j.elL.y, j.elL.z), wr = vec3(j.wrL.x * s, j.wrL.y, j.wrL.z)
      let along = normalize(wr - el)
      let n = 8
      for i in 0..<n {
        let t: Float = 1.3 * Float(i) / Float(n - 1)  // (past the wrist: onto the hand)
        let u: Float = t / 1.3
        let root = romanMix(el, wr, t) + vec3(s * (0.0065 + 0.002 * u), -0.004, 0)
        let back = -along + vec3(s * 0.12, -0.42, 0)
        let d = normalize(romanMix(vec3(s * 0.16, -0.8, -0.58), normalize(back), u))
        let len: Float = romanMix(0.03, 0.074, u)
        m.add(fa, DMat.quill, 0.0025, ellS(root + d * (len * 0.5), vec3(0.003, len * 0.5, 0.0068), frameY(d, along)))
      }
    }
    // legs: feathered thighs, scaly shins and feet, the sickle claws
    addTheroLegs(m, j, 0.03, 0.0145, 0.009, 0.036, DMat.feather, true)
    // the tail: stiff and straight, a fan of feathers at its end
    addTail(m, DMat.feather,
            [vec3(0, 0.282, -0.08), vec3(0, 0.284, -0.15), vec3(0, 0.284, -0.22), vec3(0, 0.283, -0.28),
             vec3(0, 0.282, -0.33), vec3(0, 0.28, -0.39), vec3(0, 0.277, -0.445)],
            0.038, 0.006, [THB.tail1, THB.tail1, THB.tail2, THB.tail2, THB.tail3, THB.tail3], 0.014)
    for sd in stride(from: -1, through: 1, by: 2) {
      let a = vec3(0, 0.283, -0.27), b = vec3(Float(sd) * 0.04, 0.28, -0.38), c = vec3(0, 0.277, -0.48)
      m.add(THB.tail3, DMat.quill, 0.005, sheetS(a, b, c, 0.0036))
    }
    return m
  }

  /// The spinosaurus' sail: a thin blade arched along her back, thickened along its
  /// spines; split at zSplit into the half that rides on the hips and the front half.
  private static func sailField(_ zSplit: Float, _ front: Bool) -> SDFFn {
    { p in
      let z0: Float = -0.25, z1: Float = 0.33, bottom: Float = 0.47
      let u = clamp((p.z - z0) / (z1 - z0), 0, 1)
      let a0: Float = pow(sin(u * 3.14159), 0.75)
      let arch: Float = a0 * (1 + 0.07 * sin(u * 3.14159 * 3 + 0.4))
      let top: Float = 0.5 + 0.31 * arch
      let spine: Float = pow(abs(cos(p.z * 3.14159 / 0.034)), 6)
      let h = clamp((p.y - bottom) / max(0.01, top - bottom), 0, 1)
      let th: Float = (0.005 + 0.0026 * spine) * (1 - 0.3 * h)
      var d = max(abs(p.x) - th, max(p.y - top, bottom - p.y) * 0.55)
      d = max(d, max(z0 - p.z, p.z - z1) * 0.7)
      return max(d, front ? zSplit - p.z : p.z - zSplit)
    }
  }

  /// The spinosaurus: the sail along her back, a long neck and a crocodile's snout, strong
  /// arms with hooked thumb claws, short legs and a tall, finned tail.
  private static func spinoModel() -> Model {
    let j = theroBase(DAsset.queen)
    let m = Model()
    m.setSkeleton(skeleton(DAsset.queen))
    m.add(THB.root, DMat.skin, 0, ellS(vec3(0, 0.462, -0.03), vec3(0.068, 0.082, 0.13)))
    m.add(THB.chest, DMat.skin, 0.04, ellS(vec3(0, 0.44, 0.16), vec3(0.07, 0.098, 0.14), rotXm(-0.1)))
    m.paint(DMat.belly, ellS(vec3(0, 0.37, 0.12), vec3(0.06, 0.05, 0.2)), DMat.skin)
    // the sail, its front half riding on the chest
    m.add(THB.root, DMat.sail, 0.006, fnS(sailField(0.04, false), vec3(0, 0.62, -0.1), 0.24))
    m.add(THB.chest, DMat.sail, 0.006, fnS(sailField(0.04, true), vec3(0, 0.62, 0.18), 0.24))
    // the neck
    addTube(m, DMat.skin, [vec3(0, 0.49, 0.27), vec3(0, 0.555, 0.36), vec3(0, 0.605, 0.42)], [0.05, 0.04, 0.035],
            [THB.neck, THB.neck], 0.025)
    m.paint(DMat.belly, ellS(vec3(0, 0.5, 0.38), vec3(0.03, 0.03, 0.08), rotXm(0.7)), DMat.skin)
    addScutes(m, THB.neck, vec3(0, 0.55, 0.32), vec3(0, 0.635, 0.41), 4, 0.01)
    addFolds(m, THB.neck, vec3(0, 0.49, 0.27), vec3(0, 0.605, 0.42), 0.05, 0.035, [0.3, 0.48, 0.66], 0.0045)
    m.add(THB.chest, DMat.skin, 0.04, ellS(vec3(0, 0.39, 0.08), vec3(0.06, 0.055, 0.12)))  // the belly
    // the head: a long, narrow snout ending in a rosette of teeth, a small crest
    let hc = vec3(0, 0.635, 0.465)
    m.add(THB.head, DMat.skin, 0, ellS(hc + vec3(0, 0.003, 0), vec3(0.034, 0.034, 0.055)))
    m.add(THB.head, DMat.skin, 0.02, coneS(hc + vec3(0, -0.004, 0.03), vec3(0, 0.607, 0.69), 0.026, 0.015))
    m.add(THB.head, DMat.skin, 0.012, ellS(vec3(0, 0.605, 0.69), vec3(0.02, 0.018, 0.03)))
    m.add(THB.head, DMat.scute, 0.008, ellS(vec3(0, 0.663, 0.5), vec3(0.007, 0.011, 0.03)))
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      addEye(m, THB.head, hc + vec3(s * 0.029, 0.017, 0.002), 0.0115, vec3(s * 1.0, 0.15, 0.35))
      m.paint(DMat.dark, ellS(vec3(s * 0.009, 0.63, 0.58), vec3(0.0035, 0.003, 0.007)), DMat.skin)
    }
    // the palate; the lower jaw, trimmed to the mouth line and sculpted agape (openJaw), with its
    // tongue, gums and teeth
    let mouthDir = vec3(0, -0.05, 1)
    let G = gape(DAsset.queen)
    paintMouth(m, j.jaw, mouthDir, 0.27, G, true, 0.01, 0.02, 0.012)
    let jaw0 = m.partCount
    m.add(THB.jaw, DMat.skin, 0.01, coneS(vec3(0, 0.597, 0.46), vec3(0, 0.585, 0.68), 0.022, 0.012))
    m.add(THB.jaw, DMat.skin, 0.01, ellS(vec3(0, 0.586, 0.685), vec3(0.017, 0.012, 0.024)))
    addThroat(m, THB.jaw, THB.head, DMat.belly, 0.01, ellS(vec3(0, 0.585, 0.49), vec3(0.022, 0.012, 0.05)), j.jaw, mouthDir, 0.08)
    trimJaw(m, jaw0, j.jaw, mouthDir, 0.004)
    m.add(THB.jaw, DMat.mouth, 0.006, ellS(vec3(0, 0.598, 0.53), vec3(0.011, 0.006, 0.07), rotXm(0.05)))  // the tongue
    paintMouth(m, j.jaw, mouthDir, 0.27, G, false, 0.01, 0.018, 0.01)
    addTeeth(m, THB.jaw, vec3(0.012, 0.596, 0.52), vec3(0.01, 0.588, 0.69), 7, vec3(0, 1, 0.1), 0.01, 0.003)
    openJaw(m, jaw0, j.jaw, G)
    addTeeth(m, THB.head, vec3(0.016, 0.604, 0.5), vec3(0.012, 0.594, 0.7), 8, vec3(0, -1, 0.1), 0.012, 0.0035)
    // arms: strong, with hooked thumb claws
    addTheroArms(m, j, 0.022, 0.016, 2, 0.022, DMat.skin, 0.06)
    addTheroLegs(m, j, 0.055, 0.027, 0.017, 0.05, DMat.skin, false)
    // the root of the tail: the great muscle that pulls the thighs back
    for sd in stride(from: -1, through: 1, by: 2) {
      m.add(THB.root, DMat.skin, 0.04, ellS(vec3(Float(sd) * 0.03, 0.43, -0.11), vec3(0.04, 0.05, 0.11)))
    }
    // the tail: deep, with a fin above and below, like a paddle
    addTail(m, DMat.skin,
            [vec3(0, 0.465, -0.12), vec3(0, 0.455, -0.24), vec3(0, 0.44, -0.35), vec3(0, 0.427, -0.45),
             vec3(0, 0.41, -0.55), vec3(0, 0.39, -0.66), vec3(0, 0.37, -0.77)],
            0.06, 0.008, [THB.tail1, THB.tail1, THB.tail2, THB.tail2, THB.tail3, THB.tail3], 0.02)
    let tp = [vec3(0, 0.465, -0.18), vec3(0, 0.44, -0.35), vec3(0, 0.41, -0.55), vec3(0, 0.37, -0.78)]
    let up: [Float] = [0.03, 0.075, 0.085, 0.02], dn: [Float] = [0.0, 0.04, 0.05, 0.012]
    let tb = [THB.tail1, THB.tail2, THB.tail3]
    for i in 0..<3 {
      let a0 = tp[i] + vec3(0, up[i], 0), a1 = tp[i + 1] + vec3(0, up[i + 1], 0)
      let b0 = tp[i] - vec3(0, dn[i], 0), b1 = tp[i + 1] - vec3(0, dn[i + 1], 0)
      m.add(tb[i], DMat.sail, 0.008, sheetS(a0, a1, b1, 0.0045))
      m.add(tb[i], DMat.sail, 0.008, sheetS(a0, b1, b0, 0.0045))
    }
    return m
  }

  /// The tyrannosaurus: the great head with its banana teeth, a thick neck, massive
  /// legs and tail, tiny two-fingered arms, knobbly scutes along the back.
  private static func rexModel() -> Model {
    let j = theroBase(DAsset.king)
    let m = Model()
    m.setSkeleton(skeleton(DAsset.king))
    m.add(THB.root, DMat.skin, 0, ellS(vec3(0, 0.505, -0.04), vec3(0.09, 0.1, 0.15)))
    m.add(THB.chest, DMat.skin, 0.05, ellS(vec3(0, 0.47, 0.16), vec3(0.102, 0.13, 0.15), rotXm(-0.12)))
    m.add(THB.root, DMat.skin, 0.05, ellS(vec3(0, 0.43, 0.03), vec3(0.085, 0.07, 0.13)))  // the belly, sagging
    m.paint(DMat.belly, ellS(vec3(0, 0.36, 0.12), vec3(0.08, 0.06, 0.2)), DMat.skin)
    // a deep, muscled neck, folds of skin round the throat
    addTube(m, DMat.skin, [vec3(0, 0.53, 0.25), vec3(0, 0.6, 0.33), vec3(0, 0.66, 0.39)], [0.088, 0.074, 0.066],
            [THB.neck, THB.neck], 0.04)
    addFolds(m, THB.neck, vec3(0, 0.53, 0.25), vec3(0, 0.66, 0.39), 0.088, 0.066, [0.25, 0.45, 0.65, 0.83], 0.0065)
    m.paint(DMat.belly, ellS(vec3(0, 0.55, 0.37), vec3(0.05, 0.04, 0.08), rotXm(0.6)), DMat.skin)
    // the shoulders and the chest muscles over the little arms
    for sd in stride(from: -1, through: 1, by: 2) {
      m.add(THB.chest, DMat.skin, 0.03, ellS(vec3(Float(sd) * 0.075, 0.47, 0.25), vec3(0.035, 0.055, 0.05)))
    }
    // the skull: long, deep and boxy, broad at the back, the snout sloping down and rounding
    // off at the nose; bosses over the eyes, a knobbly ridge down the nose. Its underside is the
    // palate.
    let skull: SDFFn = { p in
      let z0: Float = 0.4, z1: Float = 0.675
      let t = clamp((p.z - z0) / (z1 - z0), 0, 1)
      let e: Float = max(0, (t - 0.74) / 0.26)
      let e2: Float = max(0, 1 - e * e)
      let rnd: Float = e2.squareRoot()
      let w: Float = romanMix(0.076, 0.04, t) * romanMix(0.4, 1.0, rnd)
      let h: Float = romanMix(0.07, 0.036, t) * romanMix(0.6, 1.0, rnd)
      let yc: Float = romanMix(0.73, 0.697, t) - 0.008 * e
      let ax = abs(p.x) / w, ay = abs(p.y - yc) / h
      let sp: Float = pow(ax, 2.6) + pow(ay, 2.6)
      let r: Float = pow(sp, 1 / 2.6)
      return SDF.smax((r - 1) * min(w, h), max(z0 - p.z, p.z - z1), 0.02) * 0.7
    }
    m.add(THB.head, DMat.skin, 0, fnS(skull, vec3(0, 0.72, 0.53), 0.16))
    let mouthDir = vec3(0, -0.004, 1)
    let G = gape(DAsset.king)
    paintMouth(m, j.jaw, mouthDir, 0.21, G, true, 0.009, 0.046, 0.012)  // the palate, inside the lips
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      m.add(THB.head, DMat.skin, 0.02, ellS(vec3(s * 0.062, 0.69, 0.465), vec3(0.022, 0.032, 0.05)))  // the cheeks
      m.add(THB.head, DMat.scute, 0.012, ellS(vec3(s * 0.045, 0.773, 0.49), vec3(0.018, 0.012, 0.024)))  // brow bosses
      m.add(THB.head, DMat.scute, 0.01, ellS(vec3(s * 0.04, 0.769, 0.533), vec3(0.011, 0.011, 0.016)))  // the lacrimal horns
      // the jaw muscles bulging at the back of the skull, which is broad behind the eyes
      m.add(THB.head, DMat.skin, 0.025, ellS(vec3(s * 0.056, 0.735, 0.425), vec3(0.03, 0.045, 0.042)))
      addEye(m, THB.head, vec3(s * 0.057, 0.746, 0.497), 0.0135, vec3(s * 0.75, 0.1, 0.7))
      // lips: a ridge of scaly skin along the upper jaw, over the roots of the teeth
      addTube(m, DMat.skin, [vec3(s * 0.059, 0.666, 0.455), vec3(s * 0.05, 0.664, 0.555), vec3(s * 0.037, 0.663, 0.648)],
              [0.0075, 0.007, 0.0055], [THB.head, THB.head], 0.01)
      m.paint(DMat.dark, ellS(vec3(s * 0.022, 0.72, 0.655), vec3(0.008, 0.005, 0.01)), DMat.skin)
    }
    addScutes(m, THB.head, vec3(0, 0.762, 0.53), vec3(0, 0.733, 0.63), 5, 0.009)
    // the lower jaw: deep at the back, tapering to a rounded chin, narrower than the skull (the
    // upper teeth close over it on the outside); its top is the tongue and gums
    let mandible: SDFFn = { p in
      let z0: Float = 0.43, z1: Float = 0.658
      let t = clamp((p.z - z0) / (z1 - z0), 0, 1)
      let e: Float = max(0, (t - 0.72) / 0.28)
      let e2: Float = max(0, 1 - e * e)
      let rnd: Float = e2.squareRoot()
      let w: Float = romanMix(0.058, 0.031, t) * romanMix(0.45, 1.0, rnd)
      let h: Float = romanMix(0.038, 0.019, t)
      let yc: Float = romanMix(0.625, 0.644, t)
      let ax = abs(p.x) / w, ay = abs(p.y - yc) / h
      let sp: Float = pow(ax, 2.4) + pow(ay, 2.4)
      let r: Float = pow(sp, 1 / 2.4)
      return SDF.smax((r - 1) * min(w, h), max(z0 - p.z, p.z - z1), 0.02) * 0.7
    }
    // (sculpted agape: openJaw; the pose closes it)
    let jaw0 = m.partCount
    addThroat(m, THB.jaw, THB.head, DMat.skin, 0.01, fnS(mandible, vec3(0, 0.64, 0.54), 0.14), j.jaw, mouthDir, 0.06)
    addThroat(m, THB.jaw, THB.head, DMat.belly, 0.02, ellS(vec3(0, 0.615, 0.49), vec3(0.05, 0.022, 0.08)), j.jaw, mouthDir, 0.1)
    trimJaw(m, jaw0, j.jaw, mouthDir, 0.004)
    m.add(THB.jaw, DMat.mouth, 0.006, ellS(vec3(0, 0.657, 0.555), vec3(0.024, 0.009, 0.075)))  // the tongue
    paintMouth(m, j.jaw, mouthDir, 0.2, G, false, 0.012, 0.044, 0.01)  // gums, inside the jaw's rim
    addTeeth(m, THB.jaw, vec3(0.042, 0.655, 0.505), vec3(0.024, 0.657, 0.63), 6, normalize(vec3(0, 1, 0.1)), 0.017, 0.006)
    openJaw(m, jaw0, j.jaw, G)
    addTeeth(m, THB.head, vec3(0.053, 0.666, 0.47), vec3(0.031, 0.664, 0.645), 8, normalize(vec3(0, -1, 0.12)), 0.027,
             0.0075)
    // arms: tiny, two-fingered
    addTheroArms(m, j, 0.02, 0.014, 2, 0.016, DMat.skin)
    // legs: huge
    addTheroLegs(m, j, 0.084, 0.044, 0.028, 0.07, DMat.skin, false)
    // the root of the tail: the great muscle that pulls the thighs back
    for sd in stride(from: -1, through: 1, by: 2) {
      m.add(THB.root, DMat.skin, 0.05, ellS(vec3(Float(sd) * 0.04, 0.47, -0.13), vec3(0.055, 0.07, 0.14)))
    }
    // the tail
    addTail(m, DMat.skin,
            [vec3(0, 0.52, -0.12), vec3(0, 0.515, -0.26), vec3(0, 0.5, -0.37), vec3(0, 0.485, -0.47),
             vec3(0, 0.465, -0.56), vec3(0, 0.445, -0.66), vec3(0, 0.42, -0.765)],
            0.09, 0.008, [THB.tail1, THB.tail1, THB.tail2, THB.tail2, THB.tail3, THB.tail3], 0.03)
    // scutes along the back, from the neck to the tail
    addScutes(m, THB.neck, vec3(0, 0.62, 0.32), vec3(0, 0.67, 0.38), 3, 0.012)
    addScutes(m, THB.chest, vec3(0, 0.6, 0.06), vec3(0, 0.6, 0.22), 4, 0.013)
    addScutes(m, THB.root, vec3(0, 0.605, -0.16), vec3(0, 0.605, 0.0), 4, 0.013)
    addScutes(m, THB.tail1, vec3(0, 0.6, -0.2), vec3(0, 0.56, -0.34), 3, 0.011)
    addScutes(m, THB.tail2, vec3(0, 0.552, -0.39), vec3(0, 0.525, -0.52), 3, 0.009)
    return m
  }

  /// The pteranodon: a crest swept back from the skull, a long toothless beak, a small
  /// furry body (pycnofibres, painted as skin), wings of skin from the long fourth finger
  /// to the legs. Sculpted with its wings spread; the pose folds them.
  private static func pteroModel() -> Model {
    let m = Model()
    m.setSkeleton(skeleton(DAsset.knight))
    // the body, tilted up at the shoulders, in its coat of fine fur; the breast muscles that
    // drive the wings
    m.add(PTB.body, DMat.fuzz, 0, ellS(vec3(0, 0.298, -0.008), vec3(0.061, 0.066, 0.116), rotXm(-0.6)))
    m.add(PTB.body, DMat.fuzz, 0.025, ellS(vec3(0, 0.34, 0.055), vec3(0.072, 0.052, 0.052)))  // the shoulders
    m.add(PTB.body, DMat.fuzz, 0.025, ellS(vec3(0, 0.3, 0.07), vec3(0.052, 0.05, 0.04)))  // the breast
    // the neck, furred at its base
    addTube(m, DMat.skin, [vec3(0, 0.35, 0.07), vec3(0, 0.43, 0.125), vec3(0, 0.485, 0.158)], [0.03, 0.022, 0.018],
            [PTB.neck, PTB.neck], 0.012)
    m.add(PTB.neck, DMat.fuzz, 0.015, ellS(vec3(0, 0.37, 0.085), vec3(0.03, 0.036, 0.03), rotXm(-0.6)))
    // the head: skull, the long beak, the crest
    let hc = vec3(0, 0.5, 0.195)
    m.add(PTB.head, DMat.skin, 0, ellS(hc, vec3(0.024, 0.028, 0.045)))
    // the beak: upper and lower halves meeting along the mouth line (the lower sculpted agape: openJaw)
    let jaw = DinoPJ.jaw
    let mouthDir = vec3(0, -0.075, 1)
    let G = gape(DAsset.knight)
    let beak0 = m.partCount
    m.add(PTB.head, DMat.horn, 0.012, coneS(hc + vec3(0, -0.004, 0.03), vec3(0, 0.466, 0.5), 0.019, 0.0018))
    trimJaw(m, beak0, jaw, mouthDir, 0.003, upper: true)
    m.add(PTB.head, DMat.crest, 0.01, fnS({ p in
      // a blade swept up and back: its lower edge along the skull, tapering to a point
      let q = p - (hc + vec3(0, 0.012, 0.0))
      let u = clamp(-q.z / 0.27, 0, 1)
      let lo: Float = 0.006 + 0.32 * (-q.z) * 0.45
      let hi: Float = 0.035 + 0.4 * (-q.z) * 0.8 - 0.045 * u * u
      let cy: Float = q.y - (lo + hi) * 0.5
      let hh: Float = max(0.0015, (hi - lo) * 0.5)
      let d = max(abs(cy) - hh, max(q.z - 0.02, -q.z - 0.27))
      return max(d * 0.6, abs(q.x) - (0.0065 - 0.0025 * u))
    }, hc + vec3(0, 0.07, -0.12), 0.17))
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      addEye(m, PTB.head, hc + vec3(s * 0.02, 0.008, 0.008), 0.0075, vec3(s * 1.0, 0.1, 0.4))
    }
    paintMouth(m, jaw, mouthDir, 0.09, G, true, 0.006, 0.012, 0.007, DMat.mouth, DMat.horn)  // (the rest of the beak is horn)
    paintMouth(m, jaw, mouthDir, 0.09, G, true, 0.006, 0.012, 0.007, DMat.mouth, DMat.skin)
    let jaw0 = m.partCount
    m.add(PTB.jaw, DMat.horn, 0.006, coneS(vec3(0, 0.483, 0.215), vec3(0, 0.46, 0.49), 0.0135, 0.0016))
    m.add(PTB.jaw, DMat.skin, 0.008, ellS(vec3(0, 0.476, 0.23), vec3(0.014, 0.01, 0.035)))  // the throat pouch
    trimJaw(m, jaw0, jaw, mouthDir, 0.003)
    paintMouth(m, jaw, mouthDir, 0.09, G, false, 0.006, 0.011, 0.006, DMat.mouth, DMat.horn)
    paintMouth(m, jaw, mouthDir, 0.09, G, false, 0.006, 0.011, 0.006, DMat.mouth, DMat.skin)
    openJaw(m, jaw0, jaw, G)
    // the wings: arm, forearm, the long hand and the wing finger; three small clawed fingers
    // at the knuckle, which it walks on
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let ua = sd > 0 ? PTB.larm : PTB.rarm, fa = sd > 0 ? PTB.lfore : PTB.rfore, mc = sd > 0 ? PTB.lmeta : PTB.rmeta
      let fg = sd > 0 ? PTB.lfinger : PTB.rfinger
      let th = sd > 0 ? PTB.lthigh : PTB.rthigh, sh = sd > 0 ? PTB.lshin : PTB.rshin
      let S = vec3(DinoPJ.shL.x * x, DinoPJ.shL.y, DinoPJ.shL.z), E = vec3(DinoPJ.elL.x * x, DinoPJ.elL.y, DinoPJ.elL.z)
      let W = vec3(DinoPJ.wrL.x * x, DinoPJ.wrL.y, DinoPJ.wrL.z), N = vec3(DinoPJ.knL.x * x, DinoPJ.knL.y, DinoPJ.knL.z)
      let T = vec3(DinoPJ.tipL.x * x, DinoPJ.tipL.y, DinoPJ.tipL.z)
      let H = vec3(DinoPJ.hipL.x * x, DinoPJ.hipL.y, DinoPJ.hipL.z)
      let K = vec3(DinoPJ.kneeL.x * x, DinoPJ.kneeL.y, DinoPJ.kneeL.z)
      let F = vec3(DinoPJ.footL.x * x, DinoPJ.footL.y, DinoPJ.footL.z)
      m.add(ua, DMat.skin, 0.012, coneS(S, E, 0.026, 0.0175))
      m.add(ua, DMat.skin, 0.01, ellS(romanMix(S, E, 0.35) + vec3(0, 0, 0.008), vec3(0.036, 0.017, 0.021)))  // the crest of the arm
      m.add(fa, DMat.skin, 0.008, coneS(E, W, 0.0175, 0.0135))
      m.add(fa, DMat.skin, 0.008, ellS(romanMix(E, W, 0.3), vec3(0.016, 0.0145, 0.034), frameY(vec3(0, 1, 0), W - E)))  // its muscles
      m.add(fa, DMat.skin, 0.006, sphereS(W, 0.0165))  // the wrist
      // the pteroid: a slender bone at the wrist pointing back at the shoulder, holding up the
      // membrane in front of the arm
      let pteroid: vec3 = W + (S - W) * 0.3 + vec3(0, 0.004, 0.012)
      m.add(fa, DMat.skin, 0.004, coneS(W, pteroid, 0.005, 0.0024))
      m.add(mc, DMat.skin, 0.006, coneS(W, N, 0.0135, 0.0115))
      m.add(mc, DMat.skin, 0.005, sphereS(N, 0.0152))  // the knuckle of the wing finger
      // the wing finger: four long joints, each a little thinner
      for s in 0..<4 {
        let u0 = Float(s) / 4, u1 = Float(s + 1) / 4
        let a = romanMix(N, T, u0), b = romanMix(N, T, u1)
        let ra: Float = romanMix(0.0118, 0.003, u0), rb: Float = romanMix(0.0118, 0.003, u1)
        m.add(fg, DMat.skin, 0.005, coneS(a, b, ra, rb))
        if s > 0 { m.add(fg, DMat.skin, 0.003, sphereS(a, ra * 1.18)) }
      }
      for f in 0..<3 {  // the little fingers it walks on
        let d = normalize(vec3(x * 0.25, -0.3 + 0.2 * Float(f), 1.0))
        let f1 = N + d * 0.016 + vec3(0, -0.004, 0)
        m.add(mc, DMat.skin, 0.003, coneS(N, f1, 0.0052, 0.0036))
        addClaw(m, mc, f1, d, vec3(0, -1, 0), 0.01, 0.0032, 1.2)
      }
      // the membrane: from the leading edge (arm, hand and finger) back to a trailing edge that
      // sweeps in one smooth curve from the wing tip to the knee - narrow at the tip, broad
      // near the body - billowing a little between them. It is cut into strips; each rides on
      // its bone of the leading edge and on the trailing edge's own bone (drawn in against the
      // wing as it folds), weighted by how near it is to each edge.
      let tr = sd > 0 ? PTB.ltrail : PTB.rtrail, tr2 = sd > 0 ? PTB.ltrail2 : PTB.rtrail2
      let trA = sd > 0 ? PTB.ltrailA : PTB.rtrailA, trH = sd > 0 ? PTB.ltrailH : PTB.rtrailH
      let K1 = K + vec3(x * 0.006, 0.012, 0.0)
      let C1 = vec3(0.62 * x, 0.36, -0.15), C2 = vec3(0.22 * x, 0.33, -0.155)
      func trail(_ u: Float) -> vec3 {  // from the tip (u = 0) to the knee (1)
        let a: Float = 1 - u
        let b0: Float = a * a * a, b1: Float = 3 * a * a * u
        let b2: Float = 3 * a * u * u, b3: Float = u * u * u
        let p01: vec3 = T * b0 + C1 * b1
        let p012: vec3 = p01 + C2 * b2
        return p012 + K1 * b3
      }
      let lead: [vec3] = [S, E, W, N, T]
      func leadAt(_ ax: Float) -> vec3 {  // the leading edge where |x| = ax
        for i in 0..<4 {
          let x0 = abs(lead[i].x), x1 = abs(lead[i + 1].x)
          if ax <= x1 || i == 3 { return romanMix(lead[i], lead[i + 1], clamp((ax - x0) / (x1 - x0), 0, 1)) }
        }
        return T
      }
      func trailAt(_ ax: Float) -> vec3 {  // the trailing edge where |x| = ax (|x| shrinks from the tip to the knee)
        var lo: Float = 0, hi: Float = 1
        for _ in 0..<24 {
          let mid: Float = 0.5 * (lo + hi)
          if abs(trail(mid).x) > ax { lo = mid } else { hi = mid }
        }
        return trail(0.5 * (lo + hi))
      }
      // the membrane follows its trailing-edge bone (which, spread, moves exactly with the wing
      // bone, and folded squeezes it in against that bone), except right on the leading edge,
      // where it is fixed to the bone
      let leadPts = lead
      let wEdge: SDFFn = { p in
        var best: Float = 1e9
        for i in 0..<(leadPts.count - 1) {
          let uv = leadPts[i + 1] - leadPts[i]
          let t = clamp(dot(p - leadPts[i], uv) / dot(uv, uv), 0, 1)
          best = min(best, length(p - (leadPts[i] + uv * t)))
        }
        return 1 - smoothstep(0.006, 0.02, best)
      }
      // strips between these spans (|x|), split at the joints so each lies on one bone
      let cuts: [Float] = [abs(K1.x), abs(E.x), abs(W.x), abs(N.x), abs(T.x) - 0.004]
      let pieces: [Int] = [1, 2, 2, 5]
      let leadBone: [Int] = [ua, fa, mc, fg], trailBone: [Int] = [trA, tr, trH, tr2]
      for s in 0..<4 {
        for k in 0..<pieces[s] {
          let np = Float(pieces[s])
          let a0: Float = romanMix(cuts[s], cuts[s + 1], Float(k) / np)
          let a1: Float = romanMix(cuts[s], cuts[s + 1], Float(k + 1) / np)
          let l0 = leadAt(a0), l1 = leadAt(a1), t0 = trailAt(a0), t1 = trailAt(a1)
          let h0: Float = 0.06 * length(l0 - t0), h1: Float = 0.06 * length(l1 - t1)
          let m0: vec3 = romanMix(l0, t0, 0.45) + vec3(0, h0, 0)  // the billow
          let m1: vec3 = romanMix(l1, t1, 0.45) + vec3(0, h1, 0)
          let lb = leadBone[s], tb = trailBone[s]
          m.addStretch(lb, tb, DMat.membrane, 0.004, sheetS(l0, l1, m1, 0.0042), wEdge)
          m.addStretch(lb, tb, DMat.membrane, 0.004, sheetS(l0, m1, m0, 0.0042), wEdge)
          m.addStretch(lb, tb, DMat.membrane, 0.004, sheetS(m0, m1, t1, 0.0042), wEdge)
          m.addStretch(lb, tb, DMat.membrane, 0.004, sheetS(m0, t1, t0, 0.0042), wEdge)
        }
      }
      // ... and where it meets the flank, from the shoulder down to the knee
      m.addStretch(ua, trA, DMat.membrane, 0.004, sheetS(S + vec3(0, -0.006, -0.01), leadAt(cuts[0]), K1, 0.0042), wEdge)
      let pa = S + vec3(0, 0.002, 0.012), pb = W + vec3(0, 0.002, 0.004), pc = E + vec3(0, 0.002, 0.02)
      m.addStretch(fa, ua, DMat.membrane, 0.004, sheetS(pa, pb, pc, 0.0038), along(W, S))  // the propatagium in front of the elbow
      // legs and clawed feet
      m.add(th, DMat.skin, 0.01, coneS(H, K, 0.021, 0.0115))
      m.add(th, DMat.fuzz, 0.012, ellS(romanMix(H, K, 0.3), vec3(0.019, 0.03, 0.022)))  // the thigh, furred
      m.add(sh, DMat.skin, 0.006, coneS(K, F, 0.0108, 0.0074))
      for t in -1...2 {
        let d = normalize(vec3(x * 0.15 * Float(t), -0.1, 1.0))
        var t1 = F + d * 0.022
        t1.y = 0.004
        m.add(sh, DMat.skin, 0.003, coneS(F, t1, 0.0045, 0.0032))
        addClaw(m, sh, t1, d, vec3(0, -1, 0), 0.009, 0.0028, 1.0)
      }
    }
    return m
  }

  /// The triceratops: the frill, three horns and a parrot's beak on a great head, a
  /// barrel of a body on stout legs, a short heavy tail.
  private static func trikeModel() -> Model {
    let j = quadBase(DAsset.bishop)
    let m = Model()
    m.setSkeleton(skeleton(DAsset.bishop))
    m.add(QDB.body, DMat.skin, 0, ellS(vec3(0, 0.34, -0.05), vec3(0.15, 0.15, 0.25)))
    m.add(QDB.body, DMat.skin, 0.08, ellS(vec3(0, 0.37, -0.2), vec3(0.13, 0.12, 0.12)))  // the hips
    m.add(QDB.body, DMat.skin, 0.08, ellS(vec3(0, 0.31, 0.12), vec3(0.13, 0.13, 0.12)))  // the shoulders
    m.paint(DMat.belly, ellS(vec3(0, 0.2, -0.05), vec3(0.12, 0.05, 0.22)), DMat.skin)
    // the bristly back: a row of low knobs following the curve of the back
    for i in 0..<7 {
      let z: Float = -0.25 + 0.05 * Float(i)
      let zz: Float = z + 0.09
      let y: Float = 0.486 - 1.2 * zz * zz
      m.add(QDB.body, DMat.scute, 0.01, ellS(vec3(0, y, z), vec3(0.012, 0.009, 0.016)))
    }
    // the neck under the frill
    addTube(m, DMat.skin, [vec3(0, 0.33, 0.16), vec3(0, 0.33, 0.23), vec3(0, 0.325, 0.3)], [0.1, 0.092, 0.085],
            [QDB.neck1, QDB.neck3], 0.04)
    addFolds(m, QDB.neck2, vec3(0, 0.33, 0.16), vec3(0, 0.325, 0.3), 0.1, 0.085, [0.3, 0.55, 0.8], 0.007)
    // the head, its underside cut back to the mouth line in front of the hinge (the lower jaw is
    // sculpted agape: openJaw)
    let mouthDir = vec3(0, -0.15, 1)
    let G = gape(DAsset.bishop)
    let head0 = m.partCount
    m.add(QDB.head, DMat.skin, 0, ellS(vec3(0, 0.31, 0.4), vec3(0.078, 0.085, 0.13)))
    m.add(QDB.head, DMat.skin, 0.03, coneS(vec3(0, 0.3, 0.45), vec3(0, 0.235, 0.575), 0.058, 0.026))
    m.add(QDB.head, DMat.horn, 0.006, coneS(vec3(0, 0.245, 0.565), vec3(0, 0.195, 0.62), 0.026, 0.005))  // the beak
    trimJaw(m, head0, j.jaw, mouthDir, 0.006, upper: true)
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      addEye(m, QDB.head, vec3(s * 0.063, 0.352, 0.432), 0.0145, vec3(s * 1.0, 0.15, 0.25))
      // the cheek horns (epijugals)
      addClaw(m, QDB.head, vec3(s * 0.075, 0.27, 0.37), vec3(s * 1.0, -0.4, 0.1), vec3(0, -1, 0), 0.04, 0.016, 0.4,
              DMat.horn, 0.01)
      // the brow horns, curving forward
      addClaw(m, QDB.head, vec3(s * 0.042, 0.37, 0.425), vec3(s * 0.18, 0.62, 1.0), vec3(0, -1, 0.3), 0.25, 0.022, 0.35,
              DMat.horn, 0.012)
      m.paint(DMat.dark, ellS(vec3(s * 0.022, 0.27, 0.555), vec3(0.008, 0.01, 0.012)), DMat.skin)  // nostril
    }
    addClaw(m, QDB.head, vec3(0, 0.3, 0.53), vec3(0, 1.0, 0.55), vec3(0, 0, 1), 0.065, 0.017, 0.3, DMat.horn, 0.008)  // nose horn
    // the frill: a shield rising up and back from the skull, its edge scalloped with bony knobs
    do {
      let base = vec3(0, 0.33, 0.32)
      let up = normalize(vec3(0, 0.87, -0.5))
      let n = cross(vec3(1, 0, 0), up)
      m.add(QDB.head, DMat.frill, 0.02, fnS({ p in
        let q = p - base
        let u = dot(q, up), v = q.x
        let um = max(0, u)
        let w: Float = dot(q, n) - 0.35 * v * v - 0.25 * um * um
        let rv: Float = v / 0.205, ru: Float = (u - 0.02) / 0.31
        let r = (rv * rv + ru * ru).squareRoot()
        let outline: Float = (r - 1) * 0.2
        let th: Float = 0.011 * (1 - 0.45 * clamp(u / 0.33, 0, 1)) + 0.002
        return max(max(outline, -u + 0.02), abs(w) - th)
      }, base + up * 0.15, 0.27))
      for i in 0..<13 {  // the epoccipitals round the edge
        let a: Float = -1.35 + 2.7 * Float(i) / 12.0
        let v: Float = 0.205 * sin(a), u: Float = 0.02 + 0.31 * cos(a)
        let bend: Float = 0.35 * v * v + 0.25 * u * u
        let p = base + up * u + vec3(v, 0, 0) + n * bend
        m.add(QDB.head, DMat.horn, 0.008, ellS(p, vec3(0.016, 0.016, 0.01), frameY(up, n)))
      }
    }
    // the palate; the lower jaw and its beak
    paintMouth(m, j.jaw, mouthDir, 0.2, G, true, 0.008, 0.05, 0.03)
    let jaw0 = m.partCount
    m.add(QDB.jaw, DMat.skin, 0.015, ellS(vec3(0, 0.229, 0.46), vec3(0.058, 0.041, 0.11)))
    m.add(QDB.jaw, DMat.horn, 0.006, coneS(vec3(0, 0.215, 0.54), vec3(0, 0.205, 0.595), 0.024, 0.006))
    trimJaw(m, jaw0, j.jaw, mouthDir, 0.005)
    m.add(QDB.jaw, DMat.mouth, 0.008, ellS(vec3(0, 0.231, 0.44), vec3(0.03, 0.01, 0.08), rotXm(0.15)))  // the tongue
    paintMouth(m, j.jaw, mouthDir, 0.19, G, false, 0.01, 0.046, 0.03)
    openJaw(m, jaw0, j.jaw, G)
    // legs: the front ones half-sprawled (elbows out and back), the hind ones pillars; broad
    // padded feet with blunt hooves
    var L = DinoQuadLegSpec()
    L.upperR = 0.058; L.elbowR = 0.036; L.foreR = 0.037; L.wristR = 0.029
    L.thighR = 0.066; L.kneeR = 0.04; L.shinR = 0.04; L.ankleR = 0.031
    L.fPad = vec3(0.04, 0.022, 0.043); L.hPad = vec3(0.042, 0.022, 0.05)
    L.thigh = vec3(0.062, 0.12, 0.085)
    L.hooves = true
    addQuadLegs(m, j, L)
    // the tail: heavy at the root, held off the ground, tapering
    addTail(m, DMat.skin,
            [vec3(0, 0.37, -0.27), vec3(0, 0.35, -0.38), vec3(0, 0.32, -0.48), vec3(0, 0.293, -0.555),
             vec3(0, 0.262, -0.63), vec3(0, 0.225, -0.705), vec3(0, 0.185, -0.785)],
            0.095, 0.008, [QDB.tail1, QDB.tail1, QDB.tail2, QDB.tail2, QDB.tail3, QDB.tail3], 0.03)
    return m
  }

  /// The brachiosaurus: a deep chest on pillar legs, longer in front; the neck rising
  /// like a tower; a small head with a domed nose; a tail sweeping down to the board.
  private static func brachioModel() -> Model {
    let j = quadBase(DAsset.rook)
    let m = Model()
    m.setSkeleton(skeleton(DAsset.rook))
    m.add(QDB.body, DMat.skin, 0, ellS(vec3(0, 0.57, 0.06), vec3(0.15, 0.165, 0.2)))
    m.add(QDB.body, DMat.skin, 0.09, ellS(vec3(0, 0.54, -0.13), vec3(0.135, 0.13, 0.15)))
    m.add(QDB.body, DMat.skin, 0.06, ellS(vec3(0, 0.64, 0.15), vec3(0.12, 0.095, 0.1)))  // the withers
    m.add(QDB.body, DMat.skin, 0.07, ellS(vec3(0, 0.46, 0.0), vec3(0.12, 0.075, 0.17)))  // the belly, sagging
    m.paint(DMat.belly, ellS(vec3(0, 0.42, 0.0), vec3(0.1, 0.05, 0.2)), DMat.skin)
    // the neck, in three bones, its underside pale
    addTube(m, DMat.skin, [vec3(0, 0.62, 0.19), vec3(0, 0.86, 0.3), vec3(0, 1.03, 0.37), vec3(0, 1.185, 0.43)],
            [0.076, 0.055, 0.044, 0.034], [QDB.neck1, QDB.neck2, QDB.neck3], 0.03)
    addFolds(m, QDB.neck1, vec3(0, 0.62, 0.19), vec3(0, 0.86, 0.3), 0.076, 0.055, [0.22, 0.42, 0.62], 0.0045)
    m.paint(DMat.belly, fnS({ p in
      let a = vec3(0, 0.62, 0.19), b = vec3(0, 1.185, 0.43)
      let ab = b - a
      let t = clamp(dot(p - a, ab) / dot(ab, ab), 0, 1)
      let c = a + ab * t
      let front = normalize(vec3(0, -0.35, 1.0))
      return 0.25 * (0.028 - dot(p - c, front))
    }, vec3(0, 0.9, 0.31), 0.4), DMat.skin)
    // the head: small, with the arched nose of a brachiosaur
    // (a wedge from the braincase to a long, low muzzle, rounded at the end; the arch of the
    // nose rising over the eyes); its underside cut back to the mouth line
    let mouthDir = vec3(0, -0.02, 1)
    let G = gape(DAsset.rook)
    let head0 = m.partCount
    m.add(QDB.head, DMat.skin, 0, fnS({ p in
      let z0: Float = 0.425, z1: Float = 0.562
      let t = clamp((p.z - z0) / (z1 - z0), 0, 1)
      let e: Float = max(0, (t - 0.78) / 0.22)
      let e2: Float = max(0, 1 - e * e)
      let rnd: Float = e2.squareRoot()
      let w: Float = romanMix(0.03, 0.0185, t) * romanMix(0.5, 1.0, rnd)
      let h: Float = romanMix(0.031, 0.0165, t) * romanMix(0.6, 1.0, rnd)
      let yc: Float = romanMix(1.217, 1.2, t)
      let ax = abs(p.x) / w, ay = abs(p.y - yc) / h
      let sp: Float = pow(ax, 2.4) + pow(ay, 2.4)
      let r: Float = pow(sp, 1 / 2.4)
      return SDF.smax((r - 1) * min(w, h), max(z0 - p.z, p.z - z1), 0.018) * 0.75
    }, vec3(0, 1.21, 0.49), 0.09))
    m.add(QDB.head, DMat.skin, 0.018, ellS(vec3(0, 1.241, 0.47), vec3(0.017, 0.025, 0.026), rotXm(-0.35)))
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      addEye(m, QDB.head, vec3(s * 0.0285, 1.229, 0.466), 0.0105, vec3(s * 1.0, 0.1, 0.3))
      m.paint(DMat.dark, ellS(vec3(s * 0.009, 1.266, 0.466), vec3(0.0055, 0.005, 0.008)), DMat.skin)  // nostrils high on the arch
    }
    trimJaw(m, head0, j.jaw, mouthDir, 0.004, upper: true, to: head0 + 1)
    // the palate; the lower jaw (sculpted agape: openJaw) and its tongue
    paintMouth(m, j.jaw, mouthDir, 0.095, G, true, 0.006, 0.016, 0.01)
    let jaw0 = m.partCount
    m.add(QDB.jaw, DMat.skin, 0.01, ellS(vec3(0, 1.183, 0.502), vec3(0.018, 0.011, 0.052)))
    trimJaw(m, jaw0, j.jaw, mouthDir, 0.003)
    m.add(QDB.jaw, DMat.mouth, 0.004, ellS(vec3(0, 1.1825, 0.5), vec3(0.009, 0.0045, 0.035)))  // the tongue
    paintMouth(m, j.jaw, mouthDir, 0.09, G, false, 0.008, 0.015, 0.009)
    openJaw(m, jaw0, j.jaw, G)
    addTeeth(m, QDB.head, vec3(0.013, 1.188, 0.51), vec3(0.008, 1.188, 0.54), 4, vec3(0, -1, 0.3), 0.007, 0.0026)
    // pillar legs, the front ones taller: columns of muscle, elephantine feet (the forefeet a
    // horseshoe of toes with a single thumb claw, the hind feet with three great claws)
    var L = DinoQuadLegSpec()
    L.upperR = 0.06; L.elbowR = 0.04; L.foreR = 0.042; L.wristR = 0.035
    L.thighR = 0.07; L.kneeR = 0.044; L.shinR = 0.046; L.ankleR = 0.037
    L.fPad = vec3(0.043, 0.019, 0.042); L.hPad = vec3(0.047, 0.019, 0.052)
    L.thigh = vec3(0.068, 0.14, 0.095)
    L.hooves = false
    addQuadLegs(m, j, L)
    // the tail: a long whip sweeping down towards the board
    addTail(m, DMat.skin,
            [vec3(0, 0.56, -0.2), vec3(0, 0.52, -0.33), vec3(0, 0.47, -0.45), vec3(0, 0.42, -0.54),
             vec3(0, 0.36, -0.63), vec3(0, 0.295, -0.725), vec3(0, 0.22, -0.825)],
            0.085, 0.007, [QDB.tail1, QDB.tail1, QDB.tail2, QDB.tail2, QDB.tail3, QDB.tail3], 0.03)
    return m
  }

  // --- the props ---------------------------------------------------------------
  /// A pool of blood with splashes round it; radius ~1 (scaled down when used).
  private static func poolModel() -> Model {
    let m = Model()
    m.add(0, DMat.blood, 0, fnS({ p in
      let a = atan2(p.x, p.z)
      let r = (p.x * p.x + p.z * p.z).squareRoot()
      let e0: Float = 0.78 + 0.1 * sin(a * 3 + 0.7) + 0.06 * sin(a * 7 + 2.1)
      let edge: Float = e0 + 0.03 * sin(a * 13)
      var d = r - edge
      for k in 0..<7 {
        let b = Float(k) * 0.9 + 0.4
        let c = vec2(sin(b), cos(b)) * (0.98 + 0.13 * Float(k % 3))
        d = min(d, length(vec2(p.x, p.z) - c) - (0.06 + 0.025 * Float(k % 4)))
      }
      let th: Float = 0.024 * clamp(-d / 0.25 + 0.25, 0.25, 1)
      return max(d * 0.8, abs(p.y) - th)
    }, vec3(0), 1.2))
    return m
  }

  /// A lumpy rock, scorched, glowing in its cracks; radius ~1.
  private static func meteorModel() -> Model {
    let m = Model()
    m.add(0, DMat.rock, 0, fnS({ p in
      let r = length(p)
      let n = p / max(1e-4, r)
      let b0: Float = 0.09 * sin(n.x * 5 + 1) * sin(n.y * 4 + 2)
      let bump: Float = b0 + 0.05 * sin(n.z * 9 + n.x * 3) + 0.025 * sin(n.y * 17 + n.z * 11)
      return (r - 0.9 - bump) * 0.8
    }, vec3(0), 1.1))
    m.paint(DMat.lava, fnS({ p in
      let n = normalize(p + vec3(1e-4))
      let s1: Float = sin(n.x * 7 + sin(n.y * 5) * 2)
      let s2: Float = sin(n.z * 6 + n.y * 3)
      let c = abs(s1 * s2)
      return c - 0.12
    }, vec3(0), 1.2), DMat.rock)
    return m
  }

  // ---------------------------------------------------------------------------
  /// Orders a bone's balls in runs of Dino.bulkRun lying close together (split again and again
  /// across their longest side, a whole number of runs on one side).
  private static func orderRuns(_ v: inout [DinoBall], _ first: Int, _ end: Int) {
    let run = Dino.bulkRun
    let n = end - first
    if n <= run { return }
    var lo = vec3(1e9), hi = vec3(-1e9)
    for i in first..<end {
      lo = simd_min(lo, v[i].c)
      hi = simd_max(hi, v[i].c)
    }
    let e = hi - lo
    let ax = e.x >= e.y && e.x >= e.z ? 0 : (e.y >= e.z ? 1 : 2)
    let mid = first + run * ((n + 2 * run - 1) / (2 * run))
    v[first..<end].sort { $0.c[ax] < $1.c[ax] }
    orderRuns(&v, first, mid)
    orderRuns(&v, mid, end)
  }

  /// The bulk of an animal: balls packed inside its body, each riding on the bone its middle
  /// is skinned to (rest-pose model space, in bone order). Every candidate is a point inside
  /// with the biggest ball that fits there; the one over which the balls already taken leave
  /// the skin furthest out goes next, until they leave none of it more than a few millimetres
  /// out (or there are 'most' of them). Then the same, with smaller balls, for the horns and
  /// the beak, and for the frill, the sail and the crest (thin, but they must not pass through
  /// anyone either); and the last of the tail and the beak, to their tips. Thin skin, wings,
  /// claws and teeth are left out.
  private static func fillBulk(_ m: Model, _ lo: vec3, _ hi: vec3, rmin: Float, most: Int,
                               tips: [(vec3, vec3)]) -> [DinoBall] {
    let hc: Float = 0.02, hf: Float = 0.004, hf2: Float = 0.002, rminThin: Float = 0.0015
    struct Cand {
      var p: vec3
      var r: Float
      var ext: Float  // how far below the skin over it the balls taken so far leave it (as of 'seen' of them)
      var seen: Int
      var trimmed: Bool
    }
    // rays all round (an icosahedron and the middles of its edges)
    let g: Float = 1.618034
    let ico: [vec3] = [vec3(-1, g, 0), vec3(1, g, 0), vec3(-1, -g, 0), vec3(1, -g, 0), vec3(0, -1, g), vec3(0, 1, g),
                       vec3(0, -1, -g), vec3(0, 1, -g), vec3(g, 0, -1), vec3(g, 0, 1), vec3(-g, 0, -1), vec3(-g, 0, 1)]
    var dirs = ico.map { normalize($0) }
    for i in 0..<12 {
      for j in (i + 1)..<12 where length(ico[i] - ico[j]) < 2.1 { dirs.append(normalize(ico[i] + ico[j])) }
    }
    let nx = max(1, Int(ceil((hi.x - lo.x) / hc)))
    let ny = max(1, Int(ceil((hi.y - lo.y) / hc)))
    let nz = max(1, Int(ceil((hi.z - lo.z) / hc)))
    // per slab of cells: the candidates of the body, of the horns and beak, of the frill, sail and crest
    let slab = UnsafeMutablePointer<[Cand]>.allocate(capacity: nz * 3)
    slab.initialize(repeating: [], count: nz * 3)
    defer {
      slab.deinitialize(count: nz * 3)
      slab.deallocate()
    }
    // the coarse cells: deep inside, a cell's middle will do; near the skin, it is sampled finely
    // (and more finely still where it is horn, frill, sail or crest)
    parallelFor(nz) { k in
      var out: [Cand] = []
      var rod: [Cand] = []
      var sheet: [Cand] = []
      for j in 0..<ny {
        for i in 0..<nx {
          let q = lo + (vec3(Float(i), Float(j), Float(k)) + vec3(0.5)) * hc
          let d = m.eval(q)
          if d > hc * 0.87 { continue }  // all outside
          if d < -hc * 1.5 {
            out.append(Cand(p: q, r: -d, ext: -d, seen: 0, trimmed: false))
            continue
          }
          let f = 5
          for c in 0..<f {
            for b in 0..<f {
              for a in 0..<f {
                let p = q + (vec3(Float(a), Float(b), Float(c)) - vec3(Float(f - 1) * 0.5)) * hf
                let e = m.eval(p)
                if e < -rmin { out.append(Cand(p: p, r: -e, ext: -e, seen: 0, trimmed: false)) }
              }
            }
          }
          let mat = m.materialAt(q, nil).mat
          if mat == DMat.horn || mat == DMat.frill || mat == DMat.sail || mat == DMat.crest {
            let f2 = 10
            for c in 0..<f2 {
              for b in 0..<f2 {
                for a in 0..<f2 {
                  let p = q + (vec3(Float(a), Float(b), Float(c)) - vec3(Float(f2 - 1) * 0.5)) * hf2
                  let e = m.eval(p)
                  if e < -rminThin {
                    let cand = Cand(p: p, r: -e, ext: -e, seen: 0, trimmed: false)
                    if mat == DMat.horn { rod.append(cand) } else { sheet.append(cand) }
                  }
                }
              }
            }
          }
        }
      }
      slab[k] = out
      slab[nz + k] = rod
      slab[2 * nz + k] = sheet
    }
    // how far below the skin a ball b leaves the skin over a candidate (the skin is r from it;
    // inside b, b's surface is sqrt(b.r^2 - d^2) away across the line between them)
    func gap(_ p: vec3, _ r: Float, _ b: DinoBall) -> Float {
      let d = length(p - b.c)
      return d < b.r ? r - (b.r * b.r - d * d).squareRoot() : r + d - b.r
    }
    // a max-heap on 'ext' (ties: the bigger ball, then the position, so that it is repeatable)
    func less(_ a: Cand, _ b: Cand) -> Bool {
      if a.ext != b.ext { return a.ext < b.ext }
      if a.r != b.r { return a.r < b.r }
      if a.p.z != b.p.z { return a.p.z > b.p.z }
      if a.p.y != b.p.y { return a.p.y > b.p.y }
      return a.p.x > b.p.x
    }
    func siftDown(_ h: inout [Cand], _ start: Int) {
      var i = start
      while true {
        let l = 2 * i + 1, r = l + 1
        var top = i
        if l < h.count && less(h[top], h[l]) { top = l }
        if r < h.count && less(h[top], h[r]) { top = r }
        if top == i { return }
        h.swapAt(i, top)
        i = top
      }
    }
    func push(_ h: inout [Cand], _ c: Cand) {
      h.append(c)
      var i = h.count - 1
      while i > 0 {
        let parent = (i - 1) / 2
        if !less(h[parent], h[i]) { return }
        h.swapAt(i, parent)
        i = parent
      }
    }
    func pop(_ h: inout [Cand]) -> Cand {
      let top = h[0]
      let last = h.removeLast()
      if !h.isEmpty {
        h[0] = last
        siftDown(&h, 0)
      }
      return top
    }
    var out: [DinoBall] = []
    func fill(_ kind: Int, _ budget: Int, _ smallest: Float, _ enough: Float) {
      var heap: [Cand] = []
      for k in 0..<nz { heap.append(contentsOf: slab[kind * nz + k]) }
      if heap.isEmpty { return }
      for i in stride(from: heap.count / 2 - 1, through: 0, by: -1) { siftDown(&heap, i) }
      var taken = 0
      while !heap.isEmpty && taken < budget {
        var c = pop(&heap)
        if c.seen < out.count {  // stale: measure it against the newer balls and put it back
          for i in c.seen..<out.count { c.ext = min(c.ext, gap(c.p, c.r, out[i])) }
          c.seen = out.count
          if c.ext > enough { push(&heap, c) }
          continue
        }
        if c.ext <= enough { break }
        // the field says how deep a point is only roughly (blends make it deeper than it is):
        // the ball is trimmed to the nearest point of the skin along rays all round, and then
        // waits its turn again
        if !c.trimmed {
          var r = c.r
          for d in dirs {
            var t: Float = 0, last: Float = 0
            while t < r {
              let e = m.eval(c.p + d * t)
              if e >= 0 {
                var a = last, b = t
                for _ in 0..<5 {
                  let mid = 0.5 * (a + b)
                  if m.eval(c.p + d * mid) >= 0 { b = mid } else { a = mid }
                }
                r = min(r, a)
                break
              }
              last = t
              t += max(0.001, -e * 0.5)
            }
          }
          r -= 0.0008
          if r < smallest { continue }
          c.r = r
          c.trimmed = true
          c.ext = r
          for b in out { c.ext = min(c.ext, gap(c.p, r, b)) }
          c.seen = out.count
          if c.ext > enough { push(&heap, c) }
          continue
        }
        var v = Vertex(pos: c.p, nrm: vec3(0, 1, 0))
        m.skin(&v)
        let b1 = Int(floor(v.bones / 64 + 0.001))
        let b0 = Int(v.bones - 64 * Float(b1) + 0.5)
        let w0 = v.mat - floor(v.mat + 0.0001)
        out.append(DinoBall(c: c.p, r: c.r, bone: w0 >= 0.499 ? b0 : b1))
        taken += 1
      }
    }
    fill(0, most, rmin, 0.003)
    fill(1, 200, rminThin, 0.002)  // horns, the beak
    fill(2, 240, rminThin, 0.004)  // the frill, the sail, the crest
    // the end of a tail or a beak tapers to a point, below the smallest of them: small balls are
    // laid along it from the tip back towards the body (as thick as it is there), until they meet
    // the balls already there
    for (tip, inner) in tips {
      let d = normalize(inner - tip)
      let len = length(inner - tip)
      var s: Float = 0
      while s < len {
        let p = tip + d * s
        s += 0.008
        let r = -m.eval(p) - 0.0003
        if r < 0.0012 { continue }  // (the very point)
        if out.contains(where: { length(p - $0.c) + r <= $0.r + 0.002 }) { break }
        var v = Vertex(pos: p, nrm: vec3(0, 1, 0))
        m.skin(&v)
        let b1 = Int(floor(v.bones / 64 + 0.001))
        let b0 = Int(v.bones - 64 * Float(b1) + 0.5)
        let w0 = v.mat - floor(v.mat + 0.0001)
        out.append(DinoBall(c: p, r: r, bone: w0 >= 0.499 ? b0 : b1))
      }
    }
    // in bone order, and each bone's in runs lying close together: the bounds of a run, or of a
    // bone, tell whom it may touch
    out = out.enumerated().sorted { $0.element.bone != $1.element.bone ? $0.element.bone < $1.element.bone : $0.offset < $1.offset }
      .map { $0.element }
    var i = 0
    while i < out.count {
      var j = i
      while j < out.count && out[j].bone == out[i].bone { j += 1 }
      orderRuns(&out, i, j)
      i = j
    }
    return out
  }

  // ---------------------------------------------------------------------------
  /// quality: 0 = fast, 1 = normal, 2 = ultra, 3 = extreme.
  static func buildLibrary(_ lib: DinoLibrary, quality qIn: Int, progress prog: BuildProgress?) {
    let quality = max(0, min(3, qIn))
    if let prog {
      prog.total = 7
      prog.done = 0
    }
    func tick(_ s: String) {
      if let prog {
        prog.stage = s
        prog.step()
      }
    }
    func build(_ kind: Int, _ m: Model, _ lo: vec3, _ hi: vec3) {
      let S = animalScale(kind)
      if S != 1 { m.scaleAll(S) }
      let d = vec3(0, 0, animalShift(kind))
      // (meshed in the box moved back by d, then moved by d: see shiftAll)
      buildAsset(&lib.asset[kind], m, toModel(kind, lo) - d, toModel(kind, hi) - d, quality, 0, 0, pieceHeight(kind),
                 pieceRadius(kind))
      if d.z != 0 { shiftAll(&lib.asset[kind], d) }
      // its bulk (found where the model stands, then moved by d like the meshes)
      // the tips: of the tail, and the pteranodon's beak (where the model stands, d behind)
      var tips: [(vec3, vec3)] = []
      if theropod(kind) { tips.append((theroJ(kind).tip - d, theroJ(kind).tail3 - d)) }
      if quadruped(kind) { tips.append((quadJ(kind).tip - d, quadJ(kind).tail3 - d)) }
      if kind == DAsset.knight { tips.append((toModel(kind, vec3(0, 0.466, 0.5)) - d, headPoint(kind) - d)) }
      let small = kind <= DAsset.knight
      lib.bulk[kind] = fillBulk(m, toModel(kind, lo) - d, toModel(kind, hi) - d, rmin: 0.003,
                                most: small ? 240 : 400, tips: tips).map { DinoBall(c: $0.c + d, r: $0.r, bone: $0.bone) }
    }
    build(DAsset.pawn, raptorModel(), vec3(-0.11, -0.005, -0.52), vec3(0.11, 0.5, 0.36))
    tick("Hatching the raptors")
    build(DAsset.knight, pteroModel(), vec3(-0.88, -0.005, -0.18), vec3(0.88, 0.66, 0.52))
    tick("Teaching the pteranodons to fly")
    build(DAsset.bishop, trikeModel(), vec3(-0.25, -0.005, -0.86), vec3(0.25, 0.68, 0.66))
    tick("Sharpening the triceratops' horns")
    build(DAsset.rook, brachioModel(), vec3(-0.2, -0.005, -0.9), vec3(0.2, 1.32, 0.6))
    tick("Raising the brachiosaurus")
    build(DAsset.queen, spinoModel(), vec3(-0.15, -0.005, -0.84), vec3(0.15, 0.84, 0.76))
    tick("Hoisting the spinosaurus' sail")
    build(DAsset.king, rexModel(), vec3(-0.19, -0.005, -0.82), vec3(0.19, 0.84, 0.7))
    tick("Waking the tyrant king")
    do {
      let c = poolModel()
      buildAsset(&lib.asset[DAsset.pool], c, vec3(-1.25, -0.04, -1.25), vec3(1.25, 0.04, 1.25), 0, 0, 0, 0.03, 1.0)
      let d = meteorModel()
      buildAsset(&lib.asset[DAsset.meteor], d, vec3(-1.1), vec3(1.1), 0, 0, 0, 2.0, 1.0)
      tick("Scattering the bones")
    }
    lib.built = true
    prog?.finished = true
  }
}
