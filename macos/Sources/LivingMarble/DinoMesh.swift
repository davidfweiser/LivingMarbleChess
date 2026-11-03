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
    let brow = c + vec3(0, r * 0.95, 0) - out * r * 0.1
    m.add(bone, DMat.skin, r * 0.5, ellS(brow, vec3(r * 1.25, r * 0.45, r * 1.2), F))
  }

  /// A row of teeth along the jaw from a to b (both sides of the mouth: x and -x), pointing 'dir'.
  private static func addTeeth(_ m: Model, _ bone: Int, _ a: vec3, _ b: vec3, _ n: Int, _ dir: vec3, _ len: Float,
                               _ r: Float) {
    for i in 0..<n {
      let u: Float = n > 1 ? Float(i) / Float(n - 1) : 0
      let p = romanMix(a, b, u)
      let s: Float = 1 - 0.35 * abs(u - 0.35)
      for sd in stride(from: -1, through: 1, by: 2) {
        let q = vec3(p.x * Float(sd), p.y, p.z)
        m.add(bone, DMat.teeth, 0, coneS(q, q + dir * len * s, r * s, r * 0.15))
      }
    }
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
      addEye(m, THB.head, hc + vec3(s * 0.022, 0.008, -0.006), 0.0085, vec3(s * 1.0, 0.1, 0.5))
      m.paint(DMat.dark, ellS(vec3(s * 0.008, 0.428, 0.322), vec3(0.0035, 0.0025, 0.005)), DMat.skin)  // nostril
    }
    // the lower jaw
    m.add(THB.jaw, DMat.skin, 0.008, coneS(vec3(0, 0.407, 0.222), vec3(0, 0.403, 0.327), 0.017, 0.0075))
    m.add(THB.jaw, DMat.belly, 0.01, ellS(vec3(0, 0.398, 0.235), vec3(0.014, 0.01, 0.03)))  // the throat
    m.paint(DMat.mouth, fnS({ p in
      let d: Float = abs(p.y - 0.413) - 0.003
      return max(d, max(0.235 - p.z, p.z - 0.335))
    }, vec3(0, 0.413, 0.28), 0.08), DMat.skin)
    addTeeth(m, THB.head, vec3(0.012, 0.416, 0.255), vec3(0.007, 0.415, 0.326), 6, vec3(0, -1, 0.15), 0.008, 0.0042)
    addTeeth(m, THB.jaw, vec3(0.011, 0.41, 0.26), vec3(0.006, 0.408, 0.318), 5, vec3(0, 1, 0.1), 0.0065, 0.0024)
    // arms folded at the sides, a fan of quills on each forearm
    addTheroArms(m, j, 0.0135, 0.0095, 3, 0.013, DMat.feather)
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      let fa = sd > 0 ? THB.lfore : THB.rfore
      let el = vec3(j.elL.x * s, j.elL.y, j.elL.z), wr = vec3(j.wrL.x * s, j.wrL.y, j.wrL.z)
      let out = vec3(s * 0.006, 0, 0)
      let a = wr + out + vec3(0, -0.006, -0.004), b = el + out + vec3(0, -0.004, 0.0)
      let c = b + vec3(s * 0.004, -0.052, -0.034), d = a + vec3(s * 0.004, -0.05, -0.022)
      m.add(fa, DMat.quill, 0.004, sheetS(a, b, c, 0.0036))
      m.add(fa, DMat.quill, 0.004, sheetS(a, c, d, 0.0036))
    }
    // legs: feathered thighs, scaly shins and feet, the sickle claws
    addTheroLegs(m, j, 0.03, 0.0145, 0.009, 0.036, DMat.feather, true)
    // the tail: stiff and straight, a fan of feathers at its end
    addTube(m, DMat.feather, [vec3(0, 0.282, -0.08), vec3(0, 0.284, -0.22), vec3(0, 0.282, -0.33), vec3(0, 0.277, -0.44)],
            [0.038, 0.022, 0.013, 0.006], [THB.tail1, THB.tail2, THB.tail3], 0.014)
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
    // the head: a long, narrow snout ending in a rosette of teeth, a small crest
    let hc = vec3(0, 0.635, 0.465)
    m.add(THB.head, DMat.skin, 0, ellS(hc, vec3(0.034, 0.038, 0.055)))
    m.add(THB.head, DMat.skin, 0.02, coneS(hc + vec3(0, -0.004, 0.03), vec3(0, 0.607, 0.69), 0.026, 0.015))
    m.add(THB.head, DMat.skin, 0.012, ellS(vec3(0, 0.605, 0.69), vec3(0.02, 0.018, 0.03)))
    m.add(THB.head, DMat.scute, 0.008, ellS(vec3(0, 0.663, 0.5), vec3(0.007, 0.011, 0.03)))
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      addEye(m, THB.head, hc + vec3(s * 0.03, 0.016, 0.0), 0.0095, vec3(s * 1.0, 0.15, 0.35))
      m.paint(DMat.dark, ellS(vec3(s * 0.009, 0.63, 0.58), vec3(0.0035, 0.003, 0.007)), DMat.skin)
    }
    m.add(THB.jaw, DMat.skin, 0.01, coneS(vec3(0, 0.597, 0.46), vec3(0, 0.585, 0.68), 0.022, 0.012))
    m.add(THB.jaw, DMat.skin, 0.01, ellS(vec3(0, 0.586, 0.685), vec3(0.017, 0.012, 0.024)))
    m.add(THB.jaw, DMat.belly, 0.01, ellS(vec3(0, 0.585, 0.49), vec3(0.022, 0.012, 0.05)))
    m.paint(DMat.mouth, fnS({ p in
      let y: Float = 0.6035 - 0.06 * (p.z - 0.47)
      return max(abs(p.y - y) - 0.0035, max(0.48 - p.z, p.z - 0.71))
    }, vec3(0, 0.6, 0.6), 0.14), DMat.skin)
    addTeeth(m, THB.head, vec3(0.016, 0.604, 0.5), vec3(0.012, 0.594, 0.7), 8, vec3(0, -1, 0.1), 0.012, 0.0035)
    addTeeth(m, THB.jaw, vec3(0.014, 0.595, 0.51), vec3(0.011, 0.588, 0.69), 7, vec3(0, 1, 0.1), 0.01, 0.003)
    // arms: strong, with hooked thumb claws
    addTheroArms(m, j, 0.022, 0.016, 2, 0.022, DMat.skin, 0.06)
    addTheroLegs(m, j, 0.05, 0.026, 0.017, 0.05, DMat.skin, false)
    // the tail: deep, with a fin above and below, like a paddle
    addTube(m, DMat.skin, [vec3(0, 0.465, -0.12), vec3(0, 0.44, -0.34), vec3(0, 0.41, -0.52), vec3(0, 0.37, -0.7)],
            [0.058, 0.042, 0.028, 0.01], [THB.tail1, THB.tail2, THB.tail3], 0.02)
    let tp = [vec3(0, 0.465, -0.18), vec3(0, 0.44, -0.34), vec3(0, 0.41, -0.52), vec3(0, 0.37, -0.71)]
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
    m.paint(DMat.belly, ellS(vec3(0, 0.36, 0.12), vec3(0.08, 0.06, 0.2)), DMat.skin)
    // a deep, muscled neck
    addTube(m, DMat.skin, [vec3(0, 0.53, 0.25), vec3(0, 0.6, 0.33), vec3(0, 0.66, 0.39)], [0.088, 0.074, 0.066],
            [THB.neck, THB.neck], 0.04)
    m.paint(DMat.belly, ellS(vec3(0, 0.55, 0.37), vec3(0.05, 0.04, 0.08), rotXm(0.6)), DMat.skin)
    // the skull: long, deep and boxy, wide at the back, the snout sloping down; bosses over
    // the eyes, a knobbly ridge down the nose. Its underside is the palate.
    let skull: SDFFn = { p in
      let z0: Float = 0.4, z1: Float = 0.665
      let t = clamp((p.z - z0) / (z1 - z0), 0, 1)
      let w = romanMix(0.074, 0.04, t), h = romanMix(0.07, 0.036, t), yc = romanMix(0.73, 0.697, t)
      let ax = abs(p.x) / w, ay = abs(p.y - yc) / h
      let e: Float = pow(ax, 2.6) + pow(ay, 2.6)
      let r: Float = pow(e, 1 / 2.6)
      return SDF.smax((r - 1) * min(w, h), max(z0 - p.z, p.z - z1), 0.03) * 0.85
    }
    m.add(THB.head, DMat.skin, 0, fnS(skull, vec3(0, 0.72, 0.53), 0.16))
    m.paint(DMat.mouth, fnS({ p in
      let y1: Float = 0.6645 + 0.01 * (p.z - 0.4)
      return max(max(p.y - y1, (y1 - 0.012) - p.y), max(0.43 - p.z, abs(p.x) - 0.06))
    }, vec3(0, 0.66, 0.55), 0.13), DMat.skin)
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      m.add(THB.head, DMat.skin, 0.02, ellS(vec3(s * 0.062, 0.69, 0.465), vec3(0.022, 0.032, 0.05)))  // the cheeks
      m.add(THB.head, DMat.scute, 0.012, ellS(vec3(s * 0.045, 0.773, 0.49), vec3(0.018, 0.012, 0.024)))  // brow bosses
      addEye(m, THB.head, vec3(s * 0.056, 0.745, 0.497), 0.011, vec3(s * 0.75, 0.1, 0.7))
      m.paint(DMat.dark, ellS(vec3(s * 0.022, 0.72, 0.655), vec3(0.008, 0.005, 0.01)), DMat.skin)
    }
    addScutes(m, THB.head, vec3(0, 0.762, 0.53), vec3(0, 0.733, 0.63), 5, 0.009)
    // the lower jaw: deep at the back, tapering to the chin; its top is the tongue and gums
    let mandible: SDFFn = { p in
      let z0: Float = 0.43, z1: Float = 0.652
      let t = clamp((p.z - z0) / (z1 - z0), 0, 1)
      let w = romanMix(0.066, 0.036, t), h = romanMix(0.033, 0.016, t), yc = romanMix(0.63, 0.647, t)
      let ax = abs(p.x) / w, ay = abs(p.y - yc) / h
      let e: Float = pow(ax, 2.4) + pow(ay, 2.4)
      let r: Float = pow(e, 1 / 2.4)
      return SDF.smax((r - 1) * min(w, h), max(z0 - p.z, p.z - z1), 0.02) * 0.85
    }
    m.add(THB.jaw, DMat.skin, 0.01, fnS(mandible, vec3(0, 0.64, 0.54), 0.14))
    m.add(THB.jaw, DMat.belly, 0.02, ellS(vec3(0, 0.615, 0.49), vec3(0.05, 0.022, 0.08)))
    m.paint(DMat.mouth, fnS({ p in
      let y0: Float = 0.656 + 0.008 * (p.z - 0.43)
      return max(max(y0 - p.y, p.y - (y0 + 0.012)), max(0.45 - p.z, abs(p.x) - 0.05))
    }, vec3(0, 0.66, 0.55), 0.13), DMat.skin)
    addTeeth(m, THB.head, vec3(0.053, 0.666, 0.47), vec3(0.031, 0.664, 0.645), 8, normalize(vec3(0, -1, 0.12)), 0.024,
             0.0075)
    addTeeth(m, THB.jaw, vec3(0.05, 0.66, 0.49), vec3(0.028, 0.66, 0.63), 7, normalize(vec3(0, 1, 0.1)), 0.017, 0.006)
    // arms: tiny, two-fingered
    addTheroArms(m, j, 0.02, 0.014, 2, 0.016, DMat.skin)
    // legs: huge
    addTheroLegs(m, j, 0.075, 0.042, 0.028, 0.07, DMat.skin, false)
    // the tail
    addTube(m, DMat.skin, [vec3(0, 0.52, -0.12), vec3(0, 0.5, -0.36), vec3(0, 0.46, -0.53), vec3(0, 0.41, -0.68)],
            [0.085, 0.06, 0.036, 0.008], [THB.tail1, THB.tail2, THB.tail3], 0.03)
    // scutes along the back, from the neck to the tail
    addScutes(m, THB.neck, vec3(0, 0.62, 0.32), vec3(0, 0.67, 0.38), 3, 0.012)
    addScutes(m, THB.chest, vec3(0, 0.6, 0.06), vec3(0, 0.6, 0.22), 4, 0.013)
    addScutes(m, THB.root, vec3(0, 0.605, -0.16), vec3(0, 0.605, 0.0), 4, 0.013)
    addScutes(m, THB.tail1, vec3(0, 0.6, -0.2), vec3(0, 0.56, -0.34), 3, 0.011)
    addScutes(m, THB.tail2, vec3(0, 0.55, -0.38), vec3(0, 0.5, -0.5), 3, 0.009)
    return m
  }

  /// The pteranodon: a crest swept back from the skull, a long toothless beak, a small
  /// furry body (pycnofibres, painted as skin), wings of skin from the long fourth finger
  /// to the legs. Sculpted with its wings spread; the pose folds them.
  private static func pteroModel() -> Model {
    let m = Model()
    m.setSkeleton(skeleton(DAsset.knight))
    // the body, tilted up at the shoulders
    m.add(PTB.body, DMat.skin, 0, ellS(vec3(0, 0.3, 0.0), vec3(0.042, 0.048, 0.095), rotXm(-0.6)))
    m.add(PTB.body, DMat.skin, 0.02, ellS(vec3(0, 0.34, 0.055), vec3(0.05, 0.038, 0.04)))  // the shoulders
    // the neck
    addTube(m, DMat.skin, [vec3(0, 0.35, 0.07), vec3(0, 0.43, 0.125), vec3(0, 0.485, 0.158)], [0.024, 0.019, 0.017],
            [PTB.neck, PTB.neck], 0.012)
    // the head: skull, the long beak, the crest
    let hc = vec3(0, 0.5, 0.195)
    m.add(PTB.head, DMat.skin, 0, ellS(hc, vec3(0.024, 0.028, 0.045)))
    m.add(PTB.head, DMat.horn, 0.012, coneS(hc + vec3(0, -0.004, 0.03), vec3(0, 0.466, 0.5), 0.019, 0.0018))
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
    m.add(PTB.jaw, DMat.horn, 0.006, coneS(vec3(0, 0.483, 0.215), vec3(0, 0.46, 0.49), 0.0135, 0.0016))
    m.add(PTB.jaw, DMat.skin, 0.008, ellS(vec3(0, 0.476, 0.23), vec3(0.014, 0.01, 0.035)))  // the throat pouch
    m.paint(DMat.dark, fnS({ p in
      let y: Float = 0.486 - 0.075 * (p.z - 0.2)
      return max(abs(p.y - y) - 0.0018, max(0.22 - p.z, p.z - 0.47))
    }, vec3(0, 0.47, 0.35), 0.16), DMat.horn)
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
      m.add(ua, DMat.skin, 0.012, coneS(S, E, 0.02, 0.014))
      m.add(ua, DMat.skin, 0.01, ellS(romanMix(S, E, 0.35) + vec3(0, 0, 0.008), vec3(0.028, 0.012, 0.016)))  // the crest of the arm
      m.add(fa, DMat.skin, 0.008, coneS(E, W, 0.013, 0.011))
      m.add(mc, DMat.skin, 0.006, coneS(W, N, 0.011, 0.0095))
      m.add(fg, DMat.skin, 0.006, coneS(N, T, 0.0095, 0.0025))
      for f in 0..<3 {  // the little fingers it walks on
        let d = normalize(vec3(x * 0.25, -0.3 + 0.2 * Float(f), 1.0))
        let f1 = N + d * 0.016 + vec3(0, -0.004, 0)
        m.add(mc, DMat.skin, 0.003, coneS(N, f1, 0.0052, 0.0036))
        addClaw(m, mc, f1, d, vec3(0, -1, 0), 0.01, 0.0032, 1.2)
      }
      // the membrane: from the leading edge (arm, hand and finger) to the trailing edge, which
      // rides on its own bones (drawn in against the wing as it folds)
      let tr = sd > 0 ? PTB.ltrail : PTB.rtrail, tr2 = sd > 0 ? PTB.ltrail2 : PTB.rtrail2
      let T2 = vec3(0.62 * x, 0.355, -0.12), W1 = vec3(0.3 * x, 0.335, -0.1), K1 = K + vec3(x * 0.006, 0.012, 0.0)
      // 1 on the leading edge a-b, 0 on the trailing edge t0-t1
      func edgeW(_ a: vec3, _ b: vec3, _ t0: vec3, _ t1: vec3) -> SDFFn {
        { p in
          func segD(_ q: vec3, _ u: vec3, _ v: vec3) -> Float {
            let uv = v - u
            let t = clamp(dot(q - u, uv) / dot(uv, uv), 0, 1)
            return length(q - (u + uv * t))
          }
          let dl = segD(p, a, b), dt = segD(p, t0, t1)
          return dt / max(1e-4, dl + dt)
        }
      }
      let wOut = edgeW(N, T, T, W1)
      m.addStretch(fg, tr2, DMat.membrane, 0.004, sheetS(N, T, T2, 0.0042), wOut)
      m.addStretch(fg, tr2, DMat.membrane, 0.004, sheetS(N, T2, W1, 0.0042), wOut)
      let wIn = edgeW(S, N, W1, K1)
      m.addStretch(mc, tr, DMat.membrane, 0.004, sheetS(W, N, W1, 0.0042), wIn)
      m.addStretch(fa, tr, DMat.membrane, 0.004, sheetS(E, W, W1, 0.0042), wIn)
      m.addStretch(fa, tr, DMat.membrane, 0.004, sheetS(E, W1, K1, 0.0042), wIn)
      m.addStretch(ua, tr, DMat.membrane, 0.004, sheetS(S + vec3(0, -0.006, -0.01), E, K1, 0.0042), wIn)
      let pa = S + vec3(0, 0.002, 0.012), pb = W + vec3(0, 0.002, 0.004), pc = E + vec3(0, 0.002, 0.02)
      m.addStretch(fa, ua, DMat.membrane, 0.004, sheetS(pa, pb, pc, 0.0038), along(W, S))  // the propatagium in front of the elbow
      // legs and clawed feet
      m.add(th, DMat.skin, 0.01, coneS(H, K, 0.016, 0.009))
      m.add(sh, DMat.skin, 0.006, coneS(K, F, 0.0085, 0.006))
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
    // the bristly back: a row of low knobs
    addScutes(m, QDB.body, vec3(0, 0.49, -0.28), vec3(0, 0.47, 0.08), 7, 0.014)
    // the neck under the frill
    addTube(m, DMat.skin, [vec3(0, 0.33, 0.16), vec3(0, 0.33, 0.23), vec3(0, 0.325, 0.3)], [0.1, 0.092, 0.085],
            [QDB.neck1, QDB.neck3], 0.04)
    // the head
    m.add(QDB.head, DMat.skin, 0, ellS(vec3(0, 0.31, 0.4), vec3(0.078, 0.085, 0.13)))
    m.add(QDB.head, DMat.skin, 0.03, coneS(vec3(0, 0.3, 0.45), vec3(0, 0.235, 0.575), 0.058, 0.026))
    m.add(QDB.head, DMat.horn, 0.006, coneS(vec3(0, 0.245, 0.565), vec3(0, 0.195, 0.62), 0.026, 0.005))  // the beak
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      addEye(m, QDB.head, vec3(s * 0.064, 0.35, 0.43), 0.012, vec3(s * 1.0, 0.15, 0.25))
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
    // the lower jaw and its beak
    m.add(QDB.jaw, DMat.skin, 0.015, ellS(vec3(0, 0.235, 0.46), vec3(0.058, 0.034, 0.11)))
    m.add(QDB.jaw, DMat.horn, 0.006, coneS(vec3(0, 0.215, 0.54), vec3(0, 0.205, 0.595), 0.024, 0.006))
    m.paint(DMat.mouth, fnS({ p in
      let d: Float = abs(p.y - 0.253 + 0.15 * (p.z - 0.4)) - 0.003
      return max(d, max(0.37 - p.z, p.z - 0.6))
    }, vec3(0, 0.24, 0.48), 0.15), DMat.skin)
    // legs: sprawled front legs, pillars behind, hoof-like claws
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let fu = sd > 0 ? QDB.flu : QDB.fru, fl = sd > 0 ? QDB.fll : QDB.frl
      let hu = sd > 0 ? QDB.hlu : QDB.hru, hl = sd > 0 ? QDB.hll : QDB.hrl
      let FU = vec3(j.flu.x * x, j.flu.y, j.flu.z), FL = vec3(j.fll.x * x, j.fll.y, j.fll.z)
      let FF = vec3(j.ffoot.x * x, 0.03, j.ffoot.z)
      let HU = vec3(j.hlu.x * x, j.hlu.y, j.hlu.z), HL = vec3(j.hll.x * x, j.hll.y, j.hll.z)
      let HF = vec3(j.hfoot.x * x, 0.03, j.hfoot.z)
      m.add(fu, DMat.skin, 0.04, coneS(FU + vec3(0, 0.03, 0), FL, 0.055, 0.038))
      m.add(fl, DMat.skin, 0.02, coneS(FL, FF, 0.038, 0.033))
      m.add(fl, DMat.skin, 0.015, ellS(FF + vec3(0, -0.006, 0.012), vec3(0.04, 0.026, 0.042)))
      m.add(hu, DMat.skin, 0.05, ellS(romanMix(HU, HL, 0.4) + vec3(x * 0.01, 0, 0), vec3(0.06, 0.11, 0.08)))
      m.add(hu, DMat.skin, 0.03, coneS(HU, HL, 0.065, 0.045))
      m.add(hl, DMat.skin, 0.02, coneS(HL, HF, 0.044, 0.036))
      m.add(hl, DMat.skin, 0.015, ellS(HF + vec3(0, -0.006, 0.014), vec3(0.043, 0.027, 0.048)))
      for k in -1...1 {
        let fk = Float(k)
        m.add(fl, DMat.claw, 0.004, ellS(FF + vec3(fk * 0.022, -0.016, 0.048), vec3(0.011, 0.012, 0.012)))
        m.add(hl, DMat.claw, 0.004, ellS(HF + vec3(fk * 0.024, -0.016, 0.056), vec3(0.012, 0.012, 0.013)))
      }
    }
    // the tail
    addTube(m, DMat.skin, [vec3(0, 0.37, -0.27), vec3(0, 0.33, -0.45), vec3(0, 0.27, -0.56), vec3(0, 0.19, -0.65)],
            [0.088, 0.058, 0.034, 0.008], [QDB.tail1, QDB.tail2, QDB.tail3], 0.03)
    return m
  }

  /// The brachiosaurus: a deep chest on pillar legs, longer in front; the neck rising
  /// like a tower; a small head with a domed nose; a tail sweeping down to the board.
  private static func brachioModel() -> Model {
    let j = quadBase(DAsset.rook)
    let m = Model()
    m.setSkeleton(skeleton(DAsset.rook))
    m.add(QDB.body, DMat.skin, 0, ellS(vec3(0, 0.57, 0.07), vec3(0.13, 0.15, 0.19)))
    m.add(QDB.body, DMat.skin, 0.09, ellS(vec3(0, 0.53, -0.13), vec3(0.12, 0.12, 0.14)))
    m.add(QDB.body, DMat.skin, 0.06, ellS(vec3(0, 0.64, 0.15), vec3(0.11, 0.09, 0.1)))  // the withers
    m.paint(DMat.belly, ellS(vec3(0, 0.42, 0.0), vec3(0.1, 0.05, 0.2)), DMat.skin)
    // the neck, in three bones, its underside pale
    addTube(m, DMat.skin, [vec3(0, 0.62, 0.19), vec3(0, 0.86, 0.3), vec3(0, 1.03, 0.37), vec3(0, 1.185, 0.43)],
            [0.076, 0.055, 0.044, 0.034], [QDB.neck1, QDB.neck2, QDB.neck3], 0.03)
    m.paint(DMat.belly, fnS({ p in
      let a = vec3(0, 0.62, 0.19), b = vec3(0, 1.185, 0.43)
      let ab = b - a
      let t = clamp(dot(p - a, ab) / dot(ab, ab), 0, 1)
      let c = a + ab * t
      let front = normalize(vec3(0, -0.35, 1.0))
      return 0.25 * (0.028 - dot(p - c, front))
    }, vec3(0, 0.9, 0.31), 0.4), DMat.skin)
    // the head: small, with the arched nose of a brachiosaur
    let hc = vec3(0, 1.21, 0.47)
    m.add(QDB.head, DMat.skin, 0, ellS(hc, vec3(0.032, 0.032, 0.048)))
    m.add(QDB.head, DMat.skin, 0.025, ellS(vec3(0, 1.243, 0.455), vec3(0.027, 0.029, 0.033)))
    m.add(QDB.head, DMat.skin, 0.015, coneS(vec3(0, 1.202, 0.49), vec3(0, 1.195, 0.545), 0.026, 0.021))
    for sd in stride(from: -1, through: 1, by: 2) {
      let s = Float(sd)
      addEye(m, QDB.head, vec3(s * 0.029, 1.228, 0.465), 0.0085, vec3(s * 1.0, 0.1, 0.3))
      m.paint(DMat.dark, ellS(vec3(s * 0.011, 1.271, 0.455), vec3(0.006, 0.004, 0.008)), DMat.skin)
    }
    m.add(QDB.jaw, DMat.skin, 0.01, coneS(vec3(0, 1.178, 0.48), vec3(0, 1.175, 0.542), 0.02, 0.016))
    m.paint(DMat.mouth, fnS({ p in
      max(abs(p.y - 1.186) - 0.0025, max(0.49 - p.z, p.z - 0.56))
    }, vec3(0, 1.186, 0.52), 0.06), DMat.skin)
    addTeeth(m, QDB.head, vec3(0.016, 1.188, 0.505), vec3(0.01, 1.188, 0.543), 4, vec3(0, -1, 0.3), 0.007, 0.0028)
    // pillar legs, the front ones taller; each foot with a thumb claw
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let fu = sd > 0 ? QDB.flu : QDB.fru, fl = sd > 0 ? QDB.fll : QDB.frl
      let hu = sd > 0 ? QDB.hlu : QDB.hru, hl = sd > 0 ? QDB.hll : QDB.hrl
      let FU = vec3(j.flu.x * x, j.flu.y, j.flu.z), FL = vec3(j.fll.x * x, j.fll.y, j.fll.z)
      let FF = vec3(j.ffoot.x * x, 0.035, j.ffoot.z)
      let HU = vec3(j.hlu.x * x, j.hlu.y, j.hlu.z), HL = vec3(j.hll.x * x, j.hll.y, j.hll.z)
      let HF = vec3(j.hfoot.x * x, 0.035, j.hfoot.z)
      m.add(fu, DMat.skin, 0.04, coneS(FU + vec3(0, 0.03, 0), FL, 0.058, 0.04))
      m.add(fl, DMat.skin, 0.04, coneS(FL, FF + vec3(0, 0.03, 0), 0.04, 0.036))
      m.add(fl, DMat.skin, 0.035, coneS(FF + vec3(0, 0.03, 0), vec3(FF.x, 0.014, FF.z + 0.006), 0.036, 0.044))
      m.add(hu, DMat.skin, 0.05, ellS(romanMix(HU, HL, 0.4), vec3(0.066, 0.13, 0.09)))
      m.add(hl, DMat.skin, 0.04, coneS(HL, HF + vec3(0, 0.03, 0), 0.048, 0.04))
      m.add(hl, DMat.skin, 0.035, coneS(HF + vec3(0, 0.03, 0), vec3(HF.x, 0.014, HF.z + 0.01), 0.04, 0.048))
      addClaw(m, fl, vec3(FF.x - x * 0.03, 0.02, FF.z + 0.032), vec3(-x * 0.3, 0.2, 1.0), vec3(0, -1, 0), 0.03, 0.01, 0.9)
      for k in -1...1 {
        m.add(hl, DMat.claw, 0.004, ellS(HF + vec3(Float(k) * 0.026, -0.022, 0.05), vec3(0.012, 0.012, 0.012)))
      }
    }
    // the tail
    addTube(m, DMat.skin, [vec3(0, 0.56, -0.2), vec3(0, 0.47, -0.42), vec3(0, 0.37, -0.56), vec3(0, 0.25, -0.7)],
            [0.075, 0.048, 0.03, 0.008], [QDB.tail1, QDB.tail2, QDB.tail3], 0.03)
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
    }
    build(DAsset.pawn, raptorModel(), vec3(-0.11, -0.005, -0.52), vec3(0.11, 0.5, 0.36))
    tick("Hatching the raptors")
    build(DAsset.knight, pteroModel(), vec3(-0.88, -0.005, -0.18), vec3(0.88, 0.66, 0.52))
    tick("Teaching the pteranodons to fly")
    build(DAsset.bishop, trikeModel(), vec3(-0.25, -0.005, -0.7), vec3(0.25, 0.68, 0.66))
    tick("Sharpening the triceratops' horns")
    build(DAsset.rook, brachioModel(), vec3(-0.2, -0.005, -0.74), vec3(0.2, 1.32, 0.6))
    tick("Raising the brachiosaurus")
    build(DAsset.queen, spinoModel(), vec3(-0.15, -0.005, -0.76), vec3(0.15, 0.84, 0.76))
    tick("Hoisting the spinosaurus' sail")
    build(DAsset.king, rexModel(), vec3(-0.19, -0.005, -0.72), vec3(0.19, 0.84, 0.7))
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
