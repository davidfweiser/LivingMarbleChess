// Sculpting the Roman army. Like the medieval army, every figure is a list of
// parts - capsules, cones, ellipsoids, boxes and fields of their own - each
// bound to a bone and a material, blended into one signed distance field and
// meshed; every vertex is then skinned to the bone(s) of the part(s) it lies on
// and painted with their material.
//
// The humans are sculpted at the legionary's size and scaled up afterwards
// (the centurion, the empress and the emperor are a little taller).
// (The sculpting kit - shapes, Model, buildAsset - is in Sculpt.swift.)
import Foundation
import simd

extension Roman {
  // ---------------------------------------------------------------------------
  // Shared parts
  // ---------------------------------------------------------------------------
  /// A tube around the y axis whose radius follows the knots (y, r), with vertical
  /// folds of the given depth and number (a skirt, a gown).
  private static func tubeS(_ knots: [vec2], _ folds: Float, _ foldDepth: Float, _ zScale: Float = 1,
                            _ at: vec3 = vec3(0)) -> Shape {
    let ylo = knots[0].x, yhi = knots[knots.count - 1].x
    var rmax: Float = 0
    for k in knots { rmax = max(rmax, k.y) }
    let f: SDFFn = { p0 in
      let p = p0 - at
      let y = clamp(p.y, ylo, yhi)
      var r = knots[knots.count - 1].y
      for i in 1..<knots.count {
        if y <= knots[i].x {
          let t = (y - knots[i - 1].x) / max(1e-5, knots[i].x - knots[i - 1].x)
          r = romanMix(knots[i - 1].y, knots[i].y, t)
          break
        }
      }
      let a = atan2(p.x, p.z)
      let depth: Float = foldDepth * clamp((yhi - p.y) / max(1e-4, yhi - ylo) * 1.6, 0, 1)
      let rr = (p.x * p.x + p.z * p.z / (zScale * zScale)).squareRoot()
      let side: Float = (rr - r - depth * sin(a * folds)) * 0.85
      let cap = max(ylo - p.y, p.y - yhi)
      return max(side, cap)
    }
    let br: Float = (rmax * rmax + 0.25 * (yhi - ylo) * (yhi - ylo)).squareRoot() + 0.02
    return fnS(f, at + vec3(0, (ylo + yhi) * 0.5, 0), br)
  }

  /// A head facing +z: skull, jaw, nose, brows, ears, eyes and mouth.
  private static func addHead(_ m: Model, _ bone: Int, _ c: vec3, _ s: Float, _ female: Bool) {
    let jw: Float = female ? 0.9 : 1.0
    m.add(bone, RMat.skin, 0, ellS(c, vec3(0.044, 0.054, 0.049) * s))
    m.add(bone, RMat.skin, 0.018 * s,
          ellS(c + vec3(0, -0.026, 0.012) * s, vec3(0.036 * jw, 0.032, 0.038) * s))  // jaw
    m.add(bone, RMat.skin, 0.008 * s,
          coneS(c + vec3(0, 0.006, 0.046) * s, c + vec3(0, -0.012, 0.058) * s, 0.006 * s, 0.009 * s))  // nose
    if !female {
      m.add(bone, RMat.skin, 0.01 * s, ellS(c + vec3(0, 0.014, 0.038) * s, vec3(0.034, 0.008, 0.012) * s))  // brow
    }
    m.add2(bone, bone, RMat.skin, 0.006 * s, ellS(c + vec3(0.045, 0.0, 0.0) * s, vec3(0.007, 0.014, 0.01) * s))
    for sd in stride(from: -1, through: 1, by: 2) {
      let e = c + vec3(Float(sd) * 0.017, 0.004, 0.041) * s
      m.sub(RMat.skin, 0.006 * s, sphereS(e + vec3(0, 0, 0.004) * s, 0.008 * s))
      m.add(bone, RMat.eye, 0.002 * s, sphereS(e - vec3(0, 0, 0.002) * s, 0.0065 * s))
    }
    m.paint(RMat.dark, ellS(c + vec3(0, -0.028, 0.05) * s, vec3(0.012, 0.0025, 0.02) * s), RMat.skin)  // mouth
  }

  /// Cropped hair over the top and back of the skull (fringe: down over the forehead).
  private static func addHair(_ m: Model, _ bone: Int, _ c: vec3, _ s: Float, _ fringe: Float) {
    m.add(bone, RMat.hair, 0.008 * s, fnS({ p in
      let e = SDF.ellipsoid(p - (c + vec3(0, 0.007, -0.004) * s), vec3(0.048, 0.059, 0.053) * s)
      let f0: Float = (p.z - c.z) - (0.016 + 0.03 * fringe) * s
      let front: Float = f0 + 0.035 * max(0, (p.y - c.y) / s - 0.012) * s
      let low: Float = (c.y - 0.012 * s + 0.02 * s * max(0, -(p.z - c.z) / s)) - p.y  // down to the nape
      return max(max(e, front), low)
    }, c, 0.07 * s))
    if fringe > 0 {
      for i in -3...3 {  // comma-shaped locks over the brow
        let fi = Float(i)
        m.add(bone, RMat.hair, 0.004 * s,
              ellS(c + vec3(fi * 0.011, 0.033, 0.04) * s, vec3(0.008, 0.006, 0.008) * s, rotZm(0.4 * fi / 3.0)))
      }
    }
  }

  /// A fist closed around a grip along 'axis'.
  private static func addFist(_ m: Model, _ bone: Int, _ c: vec3, _ axis: vec3, _ s: Float = 1) {
    m.add(bone, RMat.skin, 0.008 * s, ellS(c, vec3(0.019, 0.024, 0.02) * s, frameY(axis)))
  }

  /// An arm from the shoulder: a short sleeve, the bare forearm.
  private static func addArm(_ m: Model, _ left: Bool, _ j: RomanJ, _ sleeveMat: Int, _ sleeveLen: Float,
                             _ rU: Float = 0.033, _ rF: Float = 0.025) {
    let sh = left ? j.shL : j.shR, el = left ? j.elL : j.elR, wr = left ? j.wrL : j.wrR
    let ua = left ? RH.larm : RH.rarm, fa = left ? RH.lfore : RH.rfore
    let d = normalize(wr - el)
    m.add(ua, RMat.skin, 0.02, coneS(sh, el, rU, rU * 0.8))
    m.add(fa, RMat.skin, 0.012, coneS(el, wr - d * 0.02, rF, rF * 0.78))
    if sleeveLen > 0 {
      m.add(ua, sleeveMat, 0.01, coneS(sh, romanMix(sh, el, sleeveLen), rU + 0.006, rU * 0.92 + 0.004))
    }
  }

  /// Bare legs with hobnailed caligae: leather soles and straps up to the ankle.
  private static func addLegs(_ m: Model, _ j: RomanJ, _ bootMat: Int, _ bootTop: Float) {
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let th = sd > 0 ? RH.lthigh : RH.rthigh, sh = sd > 0 ? RH.lshin : RH.rshin
      let hip = vec3(j.hipL.x * x, j.hipL.y, j.hipL.z), knee = vec3(j.kneeL.x * x, j.kneeL.y, j.kneeL.z)
      let ankle = vec3(j.ankleL.x * x, j.ankleL.y, j.ankleL.z)
      m.add(th, RMat.skin, 0, coneS(hip, knee, 0.038, 0.028))
      m.add(sh, RMat.skin, 0.012, coneS(knee, ankle + vec3(0, 0.03, 0), 0.028, 0.019))
      m.add(sh, RMat.skin, 0.02, ellS(knee + vec3(0, -0.06, -0.012), vec3(0.024, 0.045, 0.022)))  // calf
      m.add(sh, bootMat, 0.008, coneS(ankle + vec3(0, 0.02, 0), ankle - vec3(0, 0.02, 0), 0.022, 0.023))
      m.add(sh, bootMat, 0.012, boxS(vec3(ankle.x, 0.02, 0.024), vec3(0.022, 0.018, 0.054), 0.015))
      m.add(sh, RMat.leather, 0, boxS(vec3(ankle.x, 0.006, 0.022), vec3(0.025, 0.006, 0.06), 0.005))  // sole
      // the straps of the sandal-boot, criss-crossing up the shin
      let top = bootTop
      m.paint(bootMat, fnS({ p in
        let band: Float = abs(sin((p.y + 0.6 * (p.x - ankle.x)) * 230)) - 0.45
        return max(band * 0.01, p.y - top)
      }, ankle + vec3(0, 0.03, 0), 0.08), RMat.skin)
    }
  }

  /// A cloak hanging down the back from the shoulders to 'bottom', open in front:
  /// it follows the slope of the shoulders, falls in folds and flares out a little;
  /// a rolled collar along its top. Skinned from the cloak bone (top) to the hem bone.
  private static func addCloak(_ m: Model, _ mat: Int, _ bottom: Float, _ flare: Float, _ top: Float = 0.632) {
    let f: SDFFn = { p in
      let ax = abs(p.x)
      let sx = clamp(ax / 0.15, 0, 1)
      let topY: Float = top - 0.045 * sx * sx
      let bx = clamp(ax / 0.17, 0, 1)
      let botY: Float = bottom + 0.04 * bx * bx
      let t = clamp((top - p.y) / (top - bottom), 0, 1)
      let rx: Float = 0.132 + (0.03 + flare) * t.squareRoot()
      let rz: Float = 0.068 + (0.052 + flare * 0.6) * t
      let cz: Float = -0.004 - 0.022 * t
      let ang = atan2(p.x, -(p.z - cz))
      let fw: Float = 0.009 * sin(ang * 7 + 1.3) + 0.004 * sin(ang * 13 + 0.4)
      let folds: Float = fw * pow(t, 0.7)
      let qx = p.x / rx, qz = (p.z - cz) / rz
      let r = (qx * qx + qz * qz).squareRoot()
      let d: Float = (r - 1) * (rx + rz) * 0.5 - folds
      let shell = abs(d) - 0.0045
      let cut = max(p.y - topY, botY - p.y)
      let front = p.z - (cz - 0.012)
      let body = max(max(shell, cut), front)
      // the rolled collar along the top
      let cx = p.x / 0.13, cq = (p.z + 0.004) / 0.067
      let rr = (cx * cx + cq * cq).squareRoot()
      var ring = length(vec2((rr - 1) * 0.1, p.y - topY)) - 0.0105
      ring = max(ring, p.z + 0.012)
      return SDF.smin(body, ring, 0.008)
    }
    let a = vec3(0, top, -0.06), b = vec3(0, bottom, -0.12)
    m.addStretch(RH.cloak, RH.hem, mat, 0.004, fnS(f, vec3(0, (top + bottom) * 0.5, -0.05), 0.35), along(a, b))
  }

  /// The body under the armour: chest, shoulders, waist.
  private static func torsoField(_ bust: Float = 0) -> SDFFn {
    { p in
      var c = SDF.ellipsoid(p - vec3(0, 0.567, 0.002), vec3(0.085, 0.09, 0.057))
      c = SDF.smin(c, SDF.ellipsoid(p - vec3(0, 0.612, -0.004), vec3(0.098, 0.046, 0.054)), 0.03)
      c = SDF.smin(c, SDF.ellipsoid(p - vec3(0, 0.5, 0.0), vec3(0.078, 0.065, 0.054)), 0.03)
      if bust > 0 {
        for sd in stride(from: -1, through: 1, by: 2) {
          let e = SDF.ellipsoid(p - vec3(Float(sd) * 0.036, 0.575, 0.04), vec3(0.034, 0.032, 0.03) * bust)
          c = SDF.smin(c, e, 0.02)
        }
      }
      return c
    }
  }

  /// A gladius along 'dir' from the fist at 'g': pommel, grip, guard and a broad pointed blade.
  private static func addGladius(_ m: Model, _ bone: Int, _ g: vec3, _ dir: vec3, _ bladeLen: Float = 0.2,
                                 _ hiltMat: Int = RMat.bronze) {
    let F = frameY(dir, vec3(1, 0, 0))
    m.addR(bone, RMat.wood, 0.002, capsuleS(g - dir * 0.022, g + dir * 0.02, 0.0062))
    m.addR(bone, hiltMat, 0.002, sphereS(g - dir * 0.032, 0.011))
    m.addR(bone, hiltMat, 0.002, ellS(g + dir * 0.027, vec3(0.012, 0.006, 0.021), F * rotYm(1.5708)))
    let b0 = g + dir * 0.032
    let Ft = F.transpose
    m.addR(bone, RMat.steel, 0, fnS({ p in
      let q = Ft * (p - b0)
      let t = clamp(q.y / bladeLen, 0, 1)
      let taper: Float = t < 0.8 ? 1 - 0.08 * t : (1 - t) / 0.2 * 0.94
      let w: Float = 0.0105 * taper + 0.0005
      let th: Float = 0.0026 * (1 - 0.5 * t) + 0.0006
      let body: Float = max(abs(q.x) / w + abs(q.z) / th - 1, 0) * min(w, th)
      let box = max(abs(q.x) - w, abs(q.z) - th)
      return max(max(box, body), max(-q.y, q.y - bladeLen))
    }, b0 + dir * bladeLen * 0.5, bladeLen * 0.55 + 0.02))
  }

  /// A pugio (dagger) along 'dir' from the fist at 'g'.
  private static func addPugio(_ m: Model, _ bone: Int, _ g: vec3, _ dir: vec3) {
    let F = frameY(dir, vec3(1, 0, 0))
    let Ft = F.transpose
    m.addR(bone, RMat.wood, 0.002, capsuleS(g - dir * 0.018, g + dir * 0.016, 0.0055))
    m.addR(bone, RMat.bronze, 0.002, ellS(g - dir * 0.025, vec3(0.011, 0.007, 0.007), F))
    m.addR(bone, RMat.bronze, 0.002, ellS(g + dir * 0.021, vec3(0.012, 0.005, 0.007), F))
    let b0 = g + dir * 0.024
    m.addR(bone, RMat.steel, 0, fnS({ p in
      let q = Ft * (p - b0)
      let L: Float = 0.085
      let t = clamp(q.y / L, 0, 1)
      let w0: Float = 0.013 * sin(pow(t, 0.55) * 3.14159) * (1 - 0.3 * t)
      let w: Float = w0 + 0.004 * (1 - t) + 0.0004
      let th: Float = 0.0022 * (1 - 0.6 * t) + 0.0005
      return max(max(abs(q.x) - w, abs(q.z) - th), max(-q.y, q.y - L))
    }, b0 + dir * 0.045, 0.06))
  }

  /// A scabbard hanging from the belt at 'top', pointing 'dir' (down), and the hilt of
  /// the sword in it on its own bone (hidden once the blade is drawn).
  private static func addScabbard(_ m: Model, _ top: vec3, _ dir: vec3, _ hiltBone: Int, _ fitMat: Int) {
    let F = frameY(dir, vec3(1, 0, 0))
    let Ft = F.transpose
    let L: Float = 0.19
    m.add(RH.root, RMat.leather, 0.003, fnS({ p in
      let q = Ft * (p - top)
      let t = clamp(q.y / L, 0, 1)
      let w: Float = 0.0135 * (1 - 0.25 * t * t)
      return max(SDF.ellipsoid(vec3(q.x, 0, q.z), vec3(w, 1, 0.0065)) * 0.006, max(-q.y, q.y - L))
    }, top + dir * L * 0.5, L * 0.6))
    let lockets: [Float] = [0.02, 0.07, 0.14]
    for u in lockets {  // bronze lockets
      m.add(RH.root, fitMat, 0.002, ellS(top + dir * u, vec3(0.016, 0.004, 0.009), F))
    }
    m.add(RH.root, fitMat, 0.002, ellS(top + dir * (L + 0.004), vec3(0.01, 0.012, 0.0075), F))  // the chape
    // the hilt sticking out of it
    let up = -dir
    m.addR(hiltBone, fitMat, 0.002, ellS(top + up * 0.005, vec3(0.012, 0.006, 0.021), F * rotYm(1.5708)))
    m.addR(hiltBone, RMat.wood, 0.002, capsuleS(top + up * 0.01, top + up * 0.048, 0.006))
    m.addR(hiltBone, fitMat, 0.002, sphereS(top + up * 0.058, 0.011))
  }

  /// An imperial Gallic helmet: bowl, brow ridge, neck guard, cheek guards and a crest
  /// (fore-and-aft, or across for a centurion). 'c' is the head's centre, 's' its size.
  private static func addHelmet(_ m: Model, _ bone: Int, _ c: vec3, _ transverse: Bool, _ metal: Int, _ trim: Int,
                                _ s: Float = 1) {
    let hc = c + vec3(0, 0.012, -0.004) * s
    m.add(bone, metal, 0.004 * s, fnS({ p in
      let q = (p - hc) / s
      let e = SDF.ellipsoid(q, vec3(0.056, 0.057, 0.06))
      let shell = max(e, -(e + 0.006))
      let low: Float = -0.004 - 0.016 * clamp(-q.z / 0.05, 0, 1)
      return max(shell, low - q.y) * s
    }, hc + vec3(0, 0.02, 0) * s, 0.075 * s))
    // brow ridge and the reinforcing cross on the crown
    m.add(bone, trim, 0.003 * s, fnS({ p in
      let q = (p - hc) / s
      let ex: Float = q.x * q.x / (0.058 * 0.058), ez: Float = q.z * q.z / (0.062 * 0.062)
      let r = (ex + ez).squareRoot()
      let ring = length(vec2((r - 1) * 0.06, q.y + 0.002)) - 0.004
      return max(ring, -q.z + 0.012) * s
    }, hc + vec3(0, 0, 0.04) * s, 0.07 * s))
    m.add(bone, trim, 0.003 * s, fnS({ p in
      let q = (p - hc) / s
      let e = SDF.ellipsoid(q, vec3(0.06, 0.061, 0.064))
      return max(max(abs(e) - 0.0016, abs(q.x) - 0.006), 0.025 - q.y) * s
    }, hc + vec3(0, 0.04, 0) * s, 0.07 * s))
    // neck guard sloping down at the back
    m.add(bone, metal, 0.006 * s, fnS({ p in
      let q = (p - hc) / s
      let back = -q.z
      let slope = max(0, back - 0.045)
      let d: Float = abs(q.y + 0.018 + 0.4 * slope) - 0.003
      let wide: Float = abs(q.x) - (0.05 + 0.1 * slope)
      return max(max(d, wide), max(back - 0.092, 0.03 - back)) * s
    }, hc + vec3(0, -0.03, -0.06) * s, 0.07 * s))
    // cheek guards, hinged at the temples
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let cc = hc + vec3(x * 0.049, -0.04, 0.008) * s
      m.add(bone, metal, 0.003 * s,
            boxS(cc, vec3(0.003, 0.025, 0.019) * s, 0.004 * s, rotZm(x * 0.16) * rotYm(-x * 0.4)))
      m.paint(trim, sphereS(cc + vec3(x * 0.004, 0.018, 0.0) * s, 0.0045 * s), metal)
    }
    // the crest on its holder
    m.add(bone, trim, 0.003 * s, cylS(hc + vec3(0, 0.054, 0) * s, hc + vec3(0, 0.066, 0) * s, 0.008 * s))
    let cb = hc + vec3(0, 0.06, 0) * s
    m.add(bone, RMat.crest, 0.004 * s, fnS({ p in
      var q = (p - cb) / s
      if transverse { q = vec3(q.z, q.y, q.x) }  // fanned out from side to side
      let len: Float = transverse ? 0.078 : 0.066
      let a = atan2(q.y, q.z)
      let r: Float = (q.y * q.y + q.z * q.z).squareRoot() - len * (1 + 0.03 * sin(a * 23))
      let w: Float = 0.0095 - 0.004 * clamp(q.y / len, 0, 1)
      return max(max(r, -q.y - 0.002), abs(q.x) - w) * s
    }, cb + vec3(0, 0.035, 0) * s, 0.09 * s))
  }

  /// The pilum: a wooden shaft, a lead weight, a long iron shank and a small point.
  /// From 'butt' up along +y.
  private static func addPilum(_ m: Model, _ bone: Int, _ butt: vec3, _ scale: Float = 1) {
    let s = scale
    let top = butt + vec3(0, 0.66 * s, 0)
    m.addR(bone, RMat.wood, 0, cylS(butt + vec3(0, 0.004, 0), top, 0.0072 * s))
    m.addR(bone, RMat.iron, 0.002, cylS(butt, butt + vec3(0, 0.02 * s, 0), 0.0082 * s))
    m.addR(bone, RMat.iron, 0.004 * s, ellS(top + vec3(0, 0.008 * s, 0), vec3(0.011, 0.022, 0.011) * s))
    m.addR(bone, RMat.iron, 0, cylS(top, top + vec3(0, 0.26 * s, 0), 0.0034 * s))
    let tip = top + vec3(0, 0.26 * s, 0)
    m.addR(bone, RMat.steel, 0, fnS({ p in
      let q = (p - tip) / s
      let t = clamp(q.y / 0.034, 0, 1)
      let w: Float = 0.0072 * (1 - t) + 0.0004
      return max(abs(q.x) + abs(q.z) - w, max(-q.y, q.y - 0.034)) * 0.7 * s
    }, tip + vec3(0, 0.017 * s, 0), 0.03 * s))
  }

  // ---------------------------------------------------------------------------
  // The pieces
  // ---------------------------------------------------------------------------
  /// Legionary: Gallic helmet with a crest, banded armour, tunic and cloak,
  /// a curved scutum, a pilum, and his gladius at his right hip.
  private static func legionaryModel() -> Model {
    let j = RomanJ.base(RAsset.pawn)
    let m = Model()
    m.setSkeleton(skeleton(RAsset.pawn))
    addLegs(m, j, RMat.leather, 0.1)
    // tunic skirt, to just above the knee
    m.add(RH.root, RMat.tunic, 0,
          tubeS([vec2(0.272, 0.093), vec2(0.3, 0.09), vec2(0.4, 0.08), vec2(0.47, 0.074), vec2(0.5, 0.07)], 13, 0.004))
    let torso = torsoField()
    m.add(RH.chest, RMat.tunic, 0.03, fnS(torso, vec3(0, 0.56, 0), 0.15))
    // banded armour: girth hoops, each overlapping the one below
    m.add(RH.chest, RMat.steel, 0.004, fnS({ p in
      let h: Float = 0.0265, y0: Float = 0.462
      let band = (p.y - y0) / h
      let lap: Float = 0.0035 * (1 - (band - floor(band)))
      let d = torso(p) - 0.0065 - lap
      return max(d, max(y0 - p.y, p.y - 0.64))
    }, vec3(0, 0.55, 0), 0.16))
    // the upper chest plates: hinges and buckles
    for sd in stride(from: -1, through: 1, by: 2) {
      for k in 0..<2 {
        var c = vec3(Float(sd) * 0.036, 0.595 - Float(k) * 0.04, 0.0)
        let ex = c.x / 0.085, ey = (c.y - 0.567) / 0.09
        let inner: Float = max(0, 1 - ex * ex - ey * ey)
        c.z = 0.002 + 0.057 * inner.squareRoot() + 0.011
        m.add(RH.chest, RMat.bronze, 0.002, boxS(c, vec3(0.008, 0.005, 0.003), 0.002))
      }
    }
    // shoulder guards: stepped lames down the upper arm
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let ua = sd > 0 ? RH.larm : RH.rarm
      for k in 0..<3 {
        let fk = Float(k)
        let c = vec3(x * (0.098 + 0.01 * fk), 0.638 - 0.019 * fk, -0.004)
        m.add(ua, RMat.steel, 0.003,
              ellS(c, vec3(0.045 - 0.002 * fk, 0.017, 0.052 - 0.003 * fk), rotZm(-x * (0.25 + 0.25 * fk))))
      }
    }
    m.add(RH.chest, RMat.cloak, 0.006, torusS(vec3(0, 0.652, 0.004), 0.046, 0.012))  // the scarf
    // belt with bronze plates and the studded apron
    m.add(RH.root, RMat.leather, 0.003, fnS({ p in
      let ex: Float = p.x * p.x / (0.084 * 0.084), ez: Float = p.z * p.z / (0.064 * 0.064)
      let r = (ex + ez).squareRoot()
      return length(vec2((r - 1) * 0.07, p.y - 0.458)) - 0.0085
    }, vec3(0, 0.458, 0), 0.1))
    for i in 0..<8 {
      let a: Float = -1.2 + 2.4 * Float(i) / 7.0
      let c = vec3(0.086 * sin(a), 0.458, 0.066 * cos(a) + 0.002)
      m.paint(RMat.bronze, ellS(c, vec3(0.0085, 0.007, 0.012)), RMat.leather)
    }
    for i in 0..<4 {
      let x: Float = (Float(i) - 1.5) * 0.017
      let z: Float = 0.077 - 0.25 * x * x / 0.01 * 0.01
      m.add(RH.root, RMat.leather, 0.002, boxS(vec3(x, 0.395, z), vec3(0.0055, 0.056, 0.0022), 0.001, rotXm(-0.1)))
      for k in 0..<4 {
        let fk = Float(k)
        m.add(RH.root, RMat.bronze, 0, sphereS(vec3(x, 0.44 - fk * 0.025, z + 0.002 - 0.0025 * fk), 0.0042))
      }
      m.add(RH.root, RMat.bronze, 0.001, ellS(vec3(x, 0.335, z - 0.006), vec3(0.0055, 0.009, 0.003)))
    }
    // arms: tunic sleeves under the shoulder guards, fists
    addArm(m, true, j, RMat.tunic, 0.45)
    addArm(m, false, j, RMat.tunic, 0.45)
    addFist(m, RH.rfore, j.gripR, vec3(0, 1, 0))
    addFist(m, RH.lfore, j.gripL, vec3(0, 0.25, 1))
    // head and helmet
    let hc = vec3(0, 0.738, 0.012)
    addHead(m, RH.head, hc, 1, false)
    addHair(m, RH.head, hc, 1, 0)
    addHelmet(m, RH.head, hc, false, RMat.steel, RMat.bronze)
    // cloak
    addCloak(m, RMat.cloak, 0.255, 0)
    m.add(RH.chest, RMat.bronze, 0.002, ellS(vec3(-0.084, 0.628, 0.03), vec3(0.013, 0.013, 0.006)))  // the brooch
    // pilum in the right hand, butt on the ground
    addPilum(m, RH.itemR, vec3(j.gripR.x, 0.004, j.gripR.z))
    // the gladius: drawn into the right hand, or in its scabbard at the right hip
    addGladius(m, RH.blade, j.gripR, vec3(0, 1, 0))
    addScabbard(m, j.sheath, normalize(vec3(-0.05, -1.0, 0.4)), RH.sheath, RMat.bronze)
    // the scutum on the left arm: a curved oblong of painted wood with a bronze rim and boss
    do {
      let R = rotYm(0.55)
      let Rt = R.transpose
      let c = j.gripL + R * vec3(-0.005, -0.03, 0.028)
      let hw: Float = 0.104, hh: Float = 0.196, curve: Float = 0.052
      let rect: (vec2, Float) -> Float = { q, grow in
        let d = vabs(q) - vec2(hw + grow, hh + grow) + vec2(0.016)
        return length(vmax(d, vec2(0))) + min(max(d.x, d.y), 0) - 0.016
      }
      let bulge: (Float) -> Float = { x in curve * (x / hw) * (x / hw) }
      m.addR(RH.itemL, RMat.shield, 0, fnS({ p in
        let q = Rt * (p - c)
        return max(rect(vec2(q.x, q.y), 0), abs(q.z + bulge(q.x)) - 0.0062)
      }, c, 0.26))
      m.addR(RH.itemL, RMat.bronze, 0, fnS({ p in
        let q = Rt * (p - c)
        let d2 = rect(vec2(q.x, q.y), 0.003)
        return max(abs(d2 + 0.0045) - 0.0045, abs(q.z + bulge(q.x)) - 0.0085)
      }, c, 0.26))
      m.addR(RH.itemL, RMat.steel, 0.002, boxS(c + R * vec3(0, 0, 0.007), vec3(0.043, 0.043, 0.003), 0.006, R))
      m.addR(RH.itemL, RMat.bronze, 0.003, fnS({ p in
        let q = Rt * (p - c)
        return max(SDF.ellipsoid(q - vec3(0, 0, 0.008), vec3(0.03, 0.03, 0.024)), 0.006 - q.z)
      }, c + R * vec3(0, 0, 0.02), 0.04))
      // the emblem: a laurel ring round the boss, thunderbolts above and below, wings to each side
      m.paint(RMat.gold, fnS({ p in
        let q = Rt * (p - c)
        let face = abs(q.z + bulge(q.x)) - 0.0075
        let ring = abs(length(vec2(q.x, q.y)) - 0.055) - 0.0035
        let ay = abs(q.y)
        let saw: Float = 2 * abs((ay * 45).truncatingRemainder(dividingBy: 2) - 1) - 1
        let zig: Float = q.x - 0.009 * saw
        let bolt = max(abs(zig) - 0.0034, max(0.065 - ay, ay - 0.175))
        let ax = abs(q.x)
        var wing: Float = 1e9
        for k in -1...1 {
          let wl: Float = abs(q.y - Float(k) * 0.013 - 0.25 * (ax - 0.06)) - 0.0028
          wing = min(wing, wl)
        }
        wing = max(wing, max(0.06 - ax, ax - 0.094))
        return max(min(min(ring, bolt), wing), face)
      }, c, 0.26), RMat.shield)
    }
    return m
  }

  /// Centurion: mail and medals, greaves, a silvered helmet with a transverse crest,
  /// a gladius in his right hand and a pugio in his left.
  private static func centurionModel() -> Model {
    let j = RomanJ.base(RAsset.bishop)
    let m = Model()
    m.setSkeleton(skeleton(RAsset.bishop))
    addLegs(m, j, RMat.leather, 0.1)
    // greaves over the shins
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let sh = sd > 0 ? RH.lshin : RH.rshin
      let knee = vec3(j.kneeL.x * x, j.kneeL.y, j.kneeL.z), ankle = vec3(j.ankleL.x * x, j.ankleL.y, j.ankleL.z)
      m.add(sh, RMat.steel, 0.004, fnS({ p in
        let c = SDF.roundCone(p, ankle + vec3(0, 0.035, 0.004), knee + vec3(0, 0.005, 0.006), 0.025, 0.034)
        return max(abs(c) - 0.003, (knee.z - 0.012) - p.z)
      }, (knee + ankle) * 0.5, 0.11))
      m.add(sh, RMat.bronze, 0.003, ellS(knee + vec3(0, 0.004, 0.026), vec3(0.022, 0.02, 0.012)))
    }
    m.add(RH.root, RMat.tunic, 0,
          tubeS([vec2(0.272, 0.093), vec2(0.3, 0.09), vec2(0.4, 0.08), vec2(0.47, 0.074), vec2(0.5, 0.07)], 13, 0.004))
    let torso = torsoField()
    m.add(RH.chest, RMat.tunic, 0.03, fnS(torso, vec3(0, 0.56, 0), 0.15))
    // mail shirt: the torso, and a skirt of mail to mid-thigh
    m.add(RH.chest, RMat.mail, 0.006, fnS({ p in
      max(torso(p) - 0.006, max(0.47 - p.y, p.y - 0.645))
    }, vec3(0, 0.56, 0), 0.16))
    m.add(RH.root, RMat.mail, 0.004,
          tubeS([vec2(0.325, 0.098), vec2(0.36, 0.094), vec2(0.43, 0.086), vec2(0.49, 0.08)], 9, 0.003))
    for sd in stride(from: -1, through: 1, by: 2) {  // mail over the shoulders
      m.add(sd > 0 ? RH.larm : RH.rarm, RMat.mail, 0.01,
            ellS(vec3(Float(sd) * 0.1, 0.632, -0.004), vec3(0.045, 0.03, 0.05)))
    }
    // the harness of phalerae: leather straps with nine medals
    let chestZ: (Float, Float) -> Float = { x, y in
      let ex = x / 0.085, ey = (y - 0.567) / 0.09
      let inner: Float = max(0, 1 - ex * ex - ey * ey)
      return 0.002 + 0.057 * inner.squareRoot() + 0.008
    }
    for sd in -1...1 {
      let x = Float(sd) * 0.036
      m.add(RH.chest, RMat.leather, 0.002,
            boxS(vec3(x, 0.565, chestZ(x, 0.565) - 0.002), vec3(0.006, 0.058, 0.003), 0.001))
      for k in 0..<3 {
        let y: Float = 0.528 + Float(k) * 0.036
        m.add(RH.chest, RMat.gold, 0.002, ellS(vec3(x, y, chestZ(x, y) + 0.002), vec3(0.0105, 0.0105, 0.0045)))
      }
    }
    m.add(RH.chest, RMat.leather, 0.002, boxS(vec3(0, 0.6, chestZ(0, 0.6) - 0.002), vec3(0.05, 0.005, 0.003), 0.001))
    m.add(RH.chest, RMat.gold, 0.004, torusS(vec3(0, 0.655, 0.006), 0.043, 0.0065))  // a torque
    // belt
    m.add(RH.root, RMat.leather, 0.003, fnS({ p in
      let ex: Float = p.x * p.x / (0.086 * 0.086), ez: Float = p.z * p.z / (0.066 * 0.066)
      let r = (ex + ez).squareRoot()
      return length(vec2((r - 1) * 0.07, p.y - 0.47)) - 0.009
    }, vec3(0, 0.47, 0), 0.1))
    m.paint(RMat.gold, boxS(vec3(0, 0.47, 0.07), vec3(0.014, 0.008, 0.01), 0.002), RMat.leather)
    addArm(m, true, j, RMat.tunic, 0.4)
    addArm(m, false, j, RMat.tunic, 0.4)
    let dirR = normalize(vec3(0.0, 0.42, 1.0)), dirL = normalize(vec3(0.0, 0.2, 1.0))
    addFist(m, RH.rfore, j.gripR, dirR)
    addFist(m, RH.lfore, j.gripL, dirL)
    let fd = normalize(j.wrL - j.elL)
    m.add(RH.lfore, RMat.gold, 0.003, torusS(j.wrL - fd * 0.03, 0.022, 0.004, frameY(fd)))
    let hc = vec3(0, 0.738, 0.012)
    addHead(m, RH.head, hc, 1, false)
    addHair(m, RH.head, hc, 1, 0)
    m.add(RH.head, RMat.hair, 0.01, ellS(hc + vec3(0, -0.034, 0.024), vec3(0.035, 0.024, 0.028)))  // a short beard
    m.paint(RMat.skin, ellS(hc + vec3(0, -0.03, 0.052), vec3(0.014, 0.006, 0.02)), RMat.hair)
    addHelmet(m, RH.head, hc, true, RMat.steel, RMat.bronze)
    addCloak(m, RMat.cloak, 0.24, 0.01)
    m.add(RH.chest, RMat.gold, 0.002, ellS(vec3(-0.084, 0.628, 0.03), vec3(0.013, 0.013, 0.006)))
    addGladius(m, RH.itemR, j.gripR, dirR, 0.2, RMat.gold)
    addPugio(m, RH.itemL, j.gripL, dirL)
    return m
  }

  /// The empress: diadem and pearls, a long stola girdled under the bust, a palla
  /// over her back and shoulder, her spear, a golden goblet - and a dagger.
  private static func empressModel() -> Model {
    let j = RomanJ.base(RAsset.queen)
    let m = Model()
    m.setSkeleton(skeleton(RAsset.queen))
    // the stola falls to the floor; sandals peep out from under it
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      let sh = sd > 0 ? RH.lshin : RH.rshin, th = sd > 0 ? RH.lthigh : RH.rthigh
      let hip = vec3(j.hipL.x * x, j.hipL.y, 0), knee = vec3(j.kneeL.x * x, j.kneeL.y, j.kneeL.z)
      let ankle = vec3(j.ankleL.x * x, j.ankleL.y, j.ankleL.z)
      m.add(th, RMat.skin, 0, coneS(hip, knee, 0.034, 0.025))
      m.add(sh, RMat.skin, 0.01, coneS(knee, ankle + vec3(0, 0.02, 0), 0.025, 0.017))
      m.add(sh, RMat.skin, 0.01, boxS(vec3(ankle.x, 0.016, 0.026), vec3(0.018, 0.013, 0.05), 0.012))
      m.add(sh, RMat.gold, 0, boxS(vec3(ankle.x, 0.005, 0.024), vec3(0.021, 0.005, 0.055), 0.004))
      m.paint(RMat.gold, boxS(vec3(ankle.x, 0.02, 0.05), vec3(0.03, 0.004, 0.003), 0), RMat.skin)
    }
    m.add(RH.root, RMat.linen, 0,
          tubeS([vec2(0.012, 0.122), vec2(0.06, 0.112), vec2(0.2, 0.095), vec2(0.38, 0.082), vec2(0.5, 0.072)], 15,
                0.006, 0.92))
    let torso = torsoField(1)
    let slim: SDFFn = { p in torso(vec3(p.x * 1.1, p.y, p.z)) / 1.1 }
    m.add(RH.chest, RMat.linen, 0.03, fnS(slim, vec3(0, 0.56, 0), 0.15))
    m.add(RH.chest, RMat.gold, 0.004, fnS({ p in  // girdle under the bust
      let ex: Float = p.x * p.x / (0.076 * 0.076), ez: Float = p.z * p.z / (0.06 * 0.06)
      let r = (ex + ez).squareRoot()
      return length(vec2((r - 1) * 0.07, p.y - 0.535)) - 0.0055
    }, vec3(0, 0.535, 0), 0.1))
    // the palla: a mantle down her back, and its end thrown over the left shoulder and across her front
    addCloak(m, RMat.cloak, 0.2, 0.02, 0.628)
    m.add(RH.chest, RMat.cloak, 0.008, fnS({ p in
      let n = normalize(vec3(0.75, 1.0, 0.0))
      let band = abs(dot(p - vec3(0, 0.55, 0), n)) - 0.028
      let d = slim(p) - 0.009
      return max(max(abs(d) - 0.004, band), max(0.47 - p.y, p.y - 0.645))
    }, vec3(0, 0.56, 0), 0.15))
    // arms: short linen sleeves, gold armlets
    addArm(m, true, j, RMat.linen, 0.5, 0.028, 0.021)
    addArm(m, false, j, RMat.linen, 0.5, 0.028, 0.021)
    for sd in stride(from: -1, through: 1, by: 2) {
      let el = sd > 0 ? j.elL : j.elR, wr = sd > 0 ? j.wrL : j.wrR
      let d = normalize(wr - el)
      m.add(sd > 0 ? RH.lfore : RH.rfore, RMat.gold, 0.003, torusS(wr - d * 0.028, 0.019, 0.0038, frameY(d)))
    }
    addFist(m, RH.rfore, j.gripR, vec3(0, 1, 0), 0.92)
    addFist(m, RH.lfore, j.gripL, vec3(0, 1, 0.1), 0.92)
    // head: her hair drawn back into a knot, curls over the brow, a diadem, earrings and pearls
    let hc = vec3(0, 0.742, 0.012)
    m.add(RH.chest, RMat.skin, 0.02, coneS(vec3(0, 0.63, 0.0), vec3(0, 0.69, 0.008), 0.032, 0.024))  // neck
    addHead(m, RH.head, hc, 0.96, true)
    m.add(RH.head, RMat.hair, 0.008, fnS({ p in
      let e = SDF.ellipsoid(p - (hc + vec3(0, 0.008, -0.006)), vec3(0.05, 0.06, 0.055))
      let front: Float = (p.z - hc.z) - 0.03 + 0.04 * max(0, (p.y - hc.y) - 0.01)
      let low: Float = (hc.y - 0.03) - p.y + 0.025 * max(0, -(p.z - hc.z)) / 0.05
      return max(max(e, front), low)
    }, hc, 0.07))
    m.add(RH.head, RMat.hair, 0.012, ellS(hc + vec3(0, 0.0, -0.066), vec3(0.034, 0.03, 0.026)))  // the knot
    m.add(RH.head, RMat.gold, 0.002, torusS(hc + vec3(0, 0.0, -0.066), 0.026, 0.0035, rotXm(1.5708)))
    for i in -4...4 {
      let a = Float(i) * 0.24
      m.add(RH.head, RMat.hair, 0.003,
            sphereS(hc + vec3(0.046 * sin(a), 0.026, 0.046 * cos(a) - 0.008), 0.0085))
    }
    m.add(RH.head, RMat.gold, 0.002, fnS({ p in  // the diadem, rising to a crescent in front
      let q = p - (hc + vec3(0, 0.036, -0.004))
      let ex: Float = q.x * q.x / (0.05 * 0.05), ez: Float = q.z * q.z / (0.055 * 0.055)
      let r = (ex + ez).squareRoot()
      let zf: Float = max(0, q.z / 0.055)
      let h: Float = 0.006 + 0.014 * zf * zf
      return max(abs((r - 1) * 0.05) - 0.0022, max(-q.y - 0.002, q.y - h))
    }, hc + vec3(0, 0.04, 0), 0.07))
    m.add(RH.head, RMat.cloak, 0, sphereS(hc + vec3(0, 0.05, 0.05), 0.0065))  // a gem
    for sd in stride(from: -1, through: 1, by: 2) {
      m.add(RH.head, RMat.gold, 0.002, ellS(hc + vec3(Float(sd) * 0.047, -0.02, 0.0), vec3(0.004, 0.009, 0.004)))
    }
    for i in 0..<11 {  // a string of pearls
      let a: Float = -1.2 + 2.4 * Float(i) / 10.0
      let pc = vec3(0.04 * sin(a), 0.648 - 0.012 * cos(a * 0.8), 0.012 + 0.04 * cos(a))
      m.add(RH.chest, RMat.linen, 0, sphereS(pc, 0.0052))
    }
    // her spear (hasta), a golden goblet and a hidden dagger
    do {
      let butt = vec3(j.gripR.x, 0.004, j.gripR.z)
      let top = butt + vec3(0, 0.98, 0)
      m.addR(RH.itemR, RMat.wood, 0, cylS(butt + vec3(0, 0.004, 0), top, 0.0068))
      m.addR(RH.itemR, RMat.gold, 0.002, cylS(butt, butt + vec3(0, 0.03, 0), 0.0082))
      m.addR(RH.itemR, RMat.gold, 0.002, cylS(top - vec3(0, 0.03, 0), top + vec3(0, 0.005, 0), 0.009))
      m.addR(RH.itemR, RMat.gold, 0, fnS({ p in
        let q = p - top
        let t = clamp(q.y / 0.11, 0, 1)
        let w: Float = 0.021 * sin(pow(t, 0.6) * 3.14159) + 0.001
        let th: Float = 0.004 * (1 - t) + 0.001
        return max(max(abs(q.x) - w, abs(q.z) - th), max(-q.y, q.y - 0.11))
      }, top + vec3(0, 0.055, 0), 0.07))
      let rb = top - vec3(0, 0.05, 0)  // a ribbon tied under the head
      m.addR(RH.itemR, RMat.cloak, 0.004, torusS(rb, 0.011, 0.004))
      m.addR(RH.itemR, RMat.cloak, 0.004,
             boxS(rb + vec3(0.012, -0.04, 0.0), vec3(0.006, 0.04, 0.0015), 0.001, rotZm(0.15)))
      m.addR(RH.itemR, RMat.cloak, 0.004,
             boxS(rb + vec3(-0.008, -0.05, 0.004), vec3(0.006, 0.05, 0.0015), 0.001, rotZm(-0.1)))
    }
    do {
      let g = j.gripL  // the goblet, held by its stem
      m.addR(RH.itemL, RMat.gold, 0.002, cylS(g - vec3(0, 0.026, 0), g - vec3(0, 0.022, 0), 0.016))
      m.addR(RH.itemL, RMat.gold, 0, cylS(g - vec3(0, 0.024, 0), g + vec3(0, 0.014, 0), 0.0035))
      m.addR(RH.itemL, RMat.gold, 0.004, fnS({ p in
        let e = SDF.ellipsoid(p - (g + vec3(0, 0.034, 0)), vec3(0.022, 0.026, 0.022))
        return max(max(e, -(e + 0.002)), p.y - (g.y + 0.046))
      }, g + vec3(0, 0.03, 0), 0.04))
      m.addR(RH.itemL, RMat.wine, 0, cylS(g + vec3(0, 0.03, 0), g + vec3(0, 0.041, 0), 0.018))
    }
    addPugio(m, RH.blade, j.gripR, normalize(vec3(0, 0.3, 1.0)))
    return m
  }

  /// The emperor: a gilded muscled cuirass, leather pteruges, a purple cloak, a laurel
  /// wreath, the eagle sceptre and his sword at his left hip.
  private static func emperorModel() -> Model {
    let j = RomanJ.base(RAsset.king)
    let m = Model()
    m.setSkeleton(skeleton(RAsset.king))
    addLegs(m, j, RMat.cloak, 0.16)  // red (or blue) boots laced up the shins
    for sd in stride(from: -1, through: 1, by: 2) {
      let ankle = vec3(j.ankleL.x * Float(sd), j.ankleL.y, j.ankleL.z)
      let sh = sd > 0 ? RH.lshin : RH.rshin
      m.add(sh, RMat.cloak, 0.006, coneS(ankle + vec3(0, 0.02, 0), ankle + vec3(0, 0.1, 0.002), 0.024, 0.023))
      m.add(sh, RMat.gold, 0.002, torusS(ankle + vec3(0, 0.105, 0.002), 0.024, 0.004))
    }
    m.add(RH.root, RMat.tunic, 0,
          tubeS([vec2(0.262, 0.094), vec2(0.3, 0.09), vec2(0.4, 0.08), vec2(0.47, 0.074), vec2(0.5, 0.07)], 13, 0.004))
    // pteruges: two rows of leather strips with gold tips
    for row in 0..<2 {
      let n = 14
      let fr = Float(row)
      for i in 0..<n {
        let a: Float = (Float(i) + 0.5 * fr) / Float(n) * 6.2831853
        let r: Float = 0.088 + 0.004 * fr
        let c = vec3(r * sin(a), 0.4 - 0.022 * fr, r * 0.8 * cos(a))
        let F = rotYm(a) * rotXm(-0.15)
        m.add(RH.root, RMat.leather, 0.001, boxS(c, vec3(0.0095, 0.042, 0.0025), 0.0015, F))
        m.add(RH.root, RMat.gold, 0, boxS(c - F * vec3(0, 0.04, 0), vec3(0.0095, 0.004, 0.003), 0.0012, F))
      }
    }
    // the cuirass: pectorals, a ridged belly, a gorgon on the chest
    let torso = torsoField()
    m.add(RH.chest, RMat.bronze, 0.004, fnS({ p in
      var d = torso(p) - 0.009
      for sd in stride(from: -1, through: 1, by: 2) {
        let e = SDF.ellipsoid(p - vec3(Float(sd) * 0.036, 0.588, 0.045), vec3(0.036, 0.027, 0.022))
        d = SDF.smin(d, e, 0.012)
      }
      for r in 0..<3 {
        let fr = Float(r)
        for sd in stride(from: -1, through: 1, by: 2) {
          let e = SDF.ellipsoid(p - vec3(Float(sd) * 0.019, 0.535 - fr * 0.024, 0.054 - 0.002 * fr),
                                vec3(0.017, 0.0115, 0.011))
          d = SDF.smin(d, e, 0.006)
        }
      }
      return max(d, max(0.432 - p.y, p.y - 0.646))
    }, vec3(0, 0.54, 0), 0.17))
    m.add(RH.chest, RMat.gold, 0.003, fnS({ p in  // the gilded rim
      let ex: Float = p.x * p.x / (0.09 * 0.09), ez: Float = p.z * p.z / (0.07 * 0.07)
      let r = (ex + ez).squareRoot()
      return length(vec2((r - 1) * 0.075, p.y - 0.436)) - 0.0065
    }, vec3(0, 0.436, 0), 0.1))
    m.add(RH.chest, RMat.gold, 0.002, ellS(vec3(0, 0.607, 0.066), vec3(0.015, 0.016, 0.006)))
    m.add(RH.chest, RMat.cloak, 0.004, fnS({ p in  // a sash knotted at the waist
      let ex: Float = p.x * p.x / (0.087 * 0.087), ez: Float = p.z * p.z / (0.068 * 0.068)
      let r = (ex + ez).squareRoot()
      return length(vec2((r - 1) * 0.075, (p.y - 0.488) * 0.6)) - 0.006
    }, vec3(0, 0.488, 0), 0.1))
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      m.add(RH.chest, RMat.cloak, 0.004, ellS(vec3(x * 0.018, 0.47, 0.07), vec3(0.008, 0.03, 0.004), rotZm(x * 0.35)))
      // shoulder straps and short pteruges over the upper arm
      let ua = sd > 0 ? RH.larm : RH.rarm
      m.add(RH.chest, RMat.bronze, 0.006, ellS(vec3(x * 0.072, 0.646, 0.0), vec3(0.03, 0.011, 0.05)))
      for i in 0..<5 {
        let a: Float = -0.9 + 1.8 * Float(i) / 4.0
        let d = vec3(x * cos(a) * 0.4 + x * 0.6, 0, sin(a))
        let c = vec3(x * 0.11, 0.605, -0.004) + normalize(d) * 0.032
        m.add(ua, RMat.leather, 0.001, boxS(c, vec3(0.0085, 0.026, 0.0022), 0.0012, rotYm(atan2(d.x, d.z))))
      }
    }
    addArm(m, true, j, RMat.tunic, 0.6)
    addArm(m, false, j, RMat.tunic, 0.6)
    addFist(m, RH.rfore, j.gripR, vec3(0, 1, 0))
    addFist(m, RH.lfore, j.gripL, vec3(0, 0, 1))
    // head: short hair with a fringe, the laurel wreath
    let hc = vec3(0, 0.738, 0.012)
    addHead(m, RH.head, hc, 1, false)
    addHair(m, RH.head, hc, 1, 1)
    for i in 0..<26 {
      let a: Float = (Float(i) / 26.0) * 6.2831853
      if abs(sin(a * 0.5)) < 0.08 { continue }  // a gap at the back where it is tied
      let d = vec3(sin(a), 0, cos(a))
      let c = hc + vec3(0.053 * d.x, 0.028 - 0.006 * cos(a), 0.058 * d.z - 0.004)
      let side: Float = i % 2 != 0 ? 1 : -1
      let F = rotYm(a) * rotXm(0.5 * side) * rotZm(0.5)
      m.add(RH.head, RMat.gold, 0.001, ellS(c + vec3(0, 0.006 * side, 0), vec3(0.004, 0.0105, 0.0018), F))
    }
    m.add(RH.head, RMat.gold, 0.002, fnS({ p in
      let q = p - hc
      let ex: Float = q.x * q.x / (0.052 * 0.052)
      let ez: Float = (q.z + 0.004) * (q.z + 0.004) / (0.057 * 0.057)
      let r = (ex + ez).squareRoot()
      let yy: Float = q.y - 0.028 + 0.006 * (q.z / 0.057)
      return length(vec2((r - 1) * 0.055, yy)) - 0.0025
    }, hc + vec3(0, 0.028, 0), 0.07))
    for sd in stride(from: -1, through: 1, by: 2) {  // the ribbons at the back
      let x = Float(sd)
      m.add(RH.head, RMat.cloak, 0.002,
            boxS(hc + vec3(x * 0.009, 0.0, -0.06), vec3(0.004, 0.028, 0.0015), 0.001, rotZm(x * 0.2)))
    }
    // the paludamentum, pinned on the right shoulder with a gold brooch
    addCloak(m, RMat.purple, 0.16, 0.025)
    m.add(RH.chest, RMat.purple, 0.01, ellS(vec3(0.085, 0.632, 0.0), vec3(0.05, 0.022, 0.045), rotZm(-0.35)))
    m.add(RH.chest, RMat.gold, 0.002, ellS(vec3(-0.084, 0.628, 0.03), vec3(0.015, 0.015, 0.007)))
    // the eagle sceptre
    do {
      let butt = vec3(j.gripR.x, 0.004, j.gripR.z)
      let top = butt + vec3(0, 0.84, 0)
      m.addR(RH.itemR, RMat.gold, 0, cylS(butt + vec3(0, 0.004, 0), top, 0.0068))
      let knobs: [Float] = [0.1, 0.3, 0.6, 0.8]
      for y in knobs { m.addR(RH.itemR, RMat.gold, 0.002, sphereS(butt + vec3(0, y, 0), 0.0105)) }
      let g = top + vec3(0, 0.016, 0)
      m.addR(RH.itemR, RMat.gold, 0.002, sphereS(g, 0.016))  // the globe
      let e = g + vec3(0, 0.03, 0)
      m.addR(RH.itemR, RMat.gold, 0.004, ellS(e, vec3(0.012, 0.018, 0.014), rotXm(-0.4)))
      m.addR(RH.itemR, RMat.gold, 0.004, sphereS(e + vec3(0, 0.02, 0.008), 0.009))
      m.addR(RH.itemR, RMat.gold, 0.002, coneS(e + vec3(0, 0.02, 0.014), e + vec3(0, 0.016, 0.024), 0.004, 0.002))
      for sd in stride(from: -1, through: 1, by: 2) {  // wings raised
        let x = Float(sd)
        m.addR(RH.itemR, RMat.gold, 0.003,
               triS(e + vec3(x * 0.008, 0.01, 0.0), e + vec3(x * 0.045, 0.05, -0.008),
                    e + vec3(x * 0.03, -0.006, -0.006), 0.0025))
      }
    }
    addGladius(m, RH.blade, j.gripR, vec3(0, 1, 0), 0.21, RMat.gold)
    addScabbard(m, j.sheath, normalize(vec3(0.05, -1.0, 0.4)), RH.sheath, RMat.gold)
    return m
  }

  // --- the chariot ---------------------------------------------------------------
  private static func horseBodyField(_ x: Float) -> SDFFn {
    let CS = RChariotJ.CS
    let cBarrel = RChariotJ.HU(0, 0.53, -0.01), cChest = RChariotJ.HU(0, 0.54, 0.2)
    let cHaunch = RChariotJ.HU(0, 0.56, -0.21)
    let rBarrel = vec3(0.13, 0.15, 0.27) * CS, rChest = vec3(0.12, 0.155, 0.13) * CS
    let rHaunch = vec3(0.135, 0.16, 0.15) * CS
    let k: Float = 0.08 * CS
    return { p in
      let q = p - vec3(x, 0, 0)
      let barrel = SDF.ellipsoid(q - cBarrel, rBarrel)
      let chest = SDF.ellipsoid(q - cChest, rChest)
      let haunch = SDF.ellipsoid(q - cHaunch, rHaunch)
      return SDF.smin(SDF.smin(barrel, chest, k), haunch, k)
    }
  }

  private static func chariotModel() -> Model {
    let CS = RChariotJ.CS, HX = RChariotJ.HX
    let axleY = RChariotJ.axleY, axleZ = RChariotJ.axleZ, wheelX = RChariotJ.wheelX, wheelR = RChariotJ.wheelR
    let floorY = RChariotJ.floorY
    func HU(_ x: Float, _ y: Float, _ z: Float) -> vec3 { RChariotJ.HU(x, y, z) }
    let m = Model()
    m.setSkeleton(skeleton(RAsset.knight))
    // --- two horses abreast
    for hs in stride(from: -1, through: 1, by: 2) {
      let x = Float(hs) * HX
      let Xs = vec3(x, 0, 0)
      let bodyF = horseBodyField(x)
      m.add(CHB.horses, RMat.horse, 0, fnS(bodyF, HU(0, 0.55, 0) + Xs, 0.42 * CS))
      m.add(CHB.neck, RMat.horse, 0.06 * CS,
            coneS(HU(0, 0.6, 0.24) + Xs, HU(0, 0.86, 0.39) + Xs, 0.095 * CS, 0.058 * CS))
      m.add(CHB.head, RMat.horse, 0.04 * CS, ellS(HU(0, 0.885, 0.43) + Xs, vec3(0.05, 0.06, 0.07) * CS, rotXm(0.6)))
      m.add(CHB.head, RMat.horse, 0.05 * CS, ellS(HU(0, 0.82, 0.5) + Xs, vec3(0.046, 0.052, 0.08) * CS, rotXm(0.8)))
      m.add(CHB.head, RMat.horse, 0.03 * CS, ellS(HU(0, 0.755, 0.565) + Xs, vec3(0.04, 0.045, 0.05) * CS))
      for sd in stride(from: -1, through: 1, by: 2) {
        let s = Float(sd)
        m.add(CHB.head, RMat.horse, 0.01 * CS,
              coneS(HU(s * 0.025, 0.94, 0.4) + Xs, HU(s * 0.032, 1.0, 0.38) + Xs, 0.016 * CS, 0.004 * CS))
        m.add(CHB.head, RMat.dark, 0.002, sphereS(HU(s * 0.044, 0.875, 0.47) + Xs, 0.011 * CS))
        m.paint(RMat.dark, sphereS(HU(s * 0.022, 0.75, 0.61) + Xs, 0.011 * CS), RMat.horse)
      }
      // a hogged mane, a plume between the ears, the bridle
      let maneA = HU(0, 0.95, 0.37), maneB = HU(0, 0.72, 0.16)
      let maneR: Float = 0.022 * CS
      m.add(CHB.neck, RMat.mane, 0.015 * CS, fnS({ p in
        let q = vec3((p.x - Xs.x) * 2.6, p.y, p.z)
        return SDF.capsule(q, maneA, maneB, maneR) / 2.6
      }, HU(0, 0.83, 0.27) + Xs, 0.2 * CS))
      m.add(CHB.head, RMat.bronze, 0.003, cylS(HU(0, 0.95, 0.41) + Xs, HU(0, 0.99, 0.4) + Xs, 0.012 * CS))
      m.add(CHB.head, RMat.crest, 0.01 * CS,
            coneS(HU(0, 1.0, 0.4) + Xs, HU(0, 1.17, 0.32) + Xs, 0.03 * CS, 0.016 * CS))
      let noseC = HU(0, 0.79, 0.53), browC = HU(0, 0.9, 0.43), browB = HU(0, 0.9, 0.45)
      let noseR = vec3(0.05, 0.058, 0.06) * CS, browR = vec3(0.054, 0.06, 0.06) * CS
      m.add(CHB.head, RMat.leather, 0.002, fnS({ p in  // noseband and browband
        let q = p - Xs
        let a = abs(SDF.ellipsoid(q - noseC, noseR)) - 0.002
        let b = abs(SDF.ellipsoid(q - browC, browR)) - 0.002
        let band1 = abs(dot(q - noseC, normalize(vec3(0, 0.6, -0.8)))) - 0.006
        let band2 = abs(dot(q - browB, normalize(vec3(0, 0.3, 1.0)))) - 0.006
        return min(max(a, band1), max(b, band2))
      }, HU(0, 0.85, 0.48) + Xs, 0.12 * CS))
      // tail
      m.add(CHB.tail, RMat.mane, 0.03 * CS,
            coneS(HU(0, 0.66, -0.35) + Xs, HU(0, 0.5, -0.42) + Xs, 0.03 * CS, 0.036 * CS))
      m.add(CHB.tail, RMat.mane, 0.03 * CS,
            coneS(HU(0, 0.5, -0.42) + Xs, HU(0, 0.32, -0.43) + Xs, 0.036 * CS, 0.018 * CS))
      // legs
      for sd in stride(from: -1, through: 1, by: 2) {
        let L = sd > 0
        let lx = x + Float(sd) * 0.07 * CS
        let bFU = L ? CHB.flu : CHB.fru, bFL = L ? CHB.fll : CHB.frl
        let bHU = L ? CHB.hlu : CHB.hru, bHL = L ? CHB.hll : CHB.hrl
        let fu = vec3(lx, RChariotJ.flu.y, RChariotJ.flu.z), fl = vec3(lx, RChariotJ.fll.y, RChariotJ.fll.z)
        let hu = vec3(lx, RChariotJ.hlu.y, RChariotJ.hlu.z), hl = vec3(lx, RChariotJ.hll.y, RChariotJ.hll.z)
        m.add(bFU, RMat.horse, 0.03 * CS, coneS(fu, fl, 0.05 * CS, 0.033 * CS))
        m.add(bFL, RMat.horse, 0.02 * CS, coneS(fl, vec3(lx, 0.075 * CS, fl.z + 0.008 * CS), 0.032 * CS, 0.023 * CS))
        m.add(bFL, RMat.horse, 0.012 * CS, ellS(vec3(lx, 0.07 * CS, fl.z + 0.012 * CS), vec3(0.029, 0.031, 0.033) * CS))
        m.add(bFL, RMat.dark, 0.006 * CS,
              coneS(vec3(lx, 0.045 * CS, fl.z + 0.022 * CS), vec3(lx, 0.006 * CS, fl.z + 0.03 * CS), 0.026 * CS,
                    0.035 * CS))
        m.add(bHU, RMat.horse, 0.04 * CS, coneS(hu + vec3(0, -0.02, 0.03) * CS, hl, 0.07 * CS, 0.035 * CS))
        m.add(bHL, RMat.horse, 0.02 * CS, coneS(hl, vec3(lx, 0.075 * CS, hl.z + 0.035 * CS), 0.033 * CS, 0.023 * CS))
        m.add(bHL, RMat.horse, 0.012 * CS, ellS(vec3(lx, 0.07 * CS, hl.z + 0.04 * CS), vec3(0.029, 0.031, 0.033) * CS))
        m.add(bHL, RMat.dark, 0.006 * CS,
              coneS(vec3(lx, 0.045 * CS, hl.z + 0.05 * CS), vec3(lx, 0.006 * CS, hl.z + 0.058 * CS), 0.026 * CS,
                    0.035 * CS))
      }
      // breastband and girth
      let bandZ = HU(0, 0, 0.2).z, bandY = HU(0, 0.5, 0).y
      m.add(CHB.horses, RMat.leather, 0.003, fnS({ p in
        let dd = abs(bodyF(p) - 0.003) - 0.0025
        let band = abs(p.z - bandZ + 0.4 * (p.y - bandY)) - 0.008
        return max(dd, band)
      }, HU(0, 0.55, 0.15) + Xs, 0.3 * CS))
      let bossC = HU(0, 0.5, 0.27)
      m.paint(RMat.bronze, fnS({ p in
        let q = p - Xs - bossC
        return length(vec2(q.x, q.y)) - 0.008
      }, HU(0, 0.5, 0.27) + Xs, 0.05), RMat.leather)
      // reins from the bit to the charioteer's left hand
      let d = RomanJ.base(RAsset.pawn)
      let hand = RChariotJ.DU(d.gripL)
      for sd in stride(from: -1, through: 1, by: 2) {
        let bit = HU(Float(sd) * 0.04, 0.76, 0.56) + Xs
        m.addStretch(CHB.head, CHB.dlfore, RMat.leather, 0.002,
                     capsuleS(bit, hand + vec3(Float(sd) * 0.004, 0, 0), 0.0028), along(bit, hand))
      }
    }
    // --- the yoke over both horses' withers, and the pole down to the car
    let yoke = HU(0, 0.71, 0.15)
    m.add(CHB.horses, RMat.wood, 0.004, fnS({ p in
      let q = p - yoke
      let bend: Float = 0.18 * q.x * q.x
      return SDF.capsule(vec3(q.x, q.y - bend, q.z), vec3(-0.17, 0, 0), vec3(0.17, 0, 0), 0.011)
    }, yoke, 0.2))
    for sd in stride(from: -1, through: 1, by: 2) {
      m.add(CHB.horses, RMat.bronze, 0.003, sphereS(yoke + vec3(Float(sd) * 0.175, 0.008, 0), 0.012))
    }
    do {
      let a = vec3(0, 0.19, -0.13), b = yoke + vec3(0, -0.012, 0.0)
      let mid = (a + b) * 0.5 + vec3(0, -0.03, 0)
      m.addStretch(CHB.car, CHB.horses, RMat.wood, 0.004, fnS({ p in
        min(SDF.capsule(p, a, mid, 0.0095), SDF.capsule(p, mid, b, 0.009))
      }, mid, length(b - a) * 0.6), along(a, b))
    }
    // --- the car: floor, a curved breastwork painted in the army's colour, gilded rail
    m.add(CHB.car, RMat.wood, 0.004, boxS(vec3(0, floorY - 0.008, axleZ - 0.015), vec3(0.128, 0.009, 0.105), 0.006))
    let wallTop: (Float) -> Float = { z in 0.29 + 0.09 * clamp((z - (axleZ - 0.02)) / 0.12, 0, 1) }
    let ell: (vec3) -> Float = { p in
      let rx: Float = 0.13, rz: Float = 0.115
      let dz = p.z - (axleZ - 0.02)
      let r = (p.x * p.x / (rx * rx) + dz * dz / (rz * rz)).squareRoot()
      return (r - 1) * 0.12
    }
    m.add(CHB.car, RMat.shield, 0.003, fnS({ p in
      let wall = abs(ell(p)) - 0.006
      return max(max(wall, (axleZ - 0.03) - p.z), max(floorY - 0.01 - p.y, p.y - wallTop(p.z)))
    }, vec3(0, 0.27, axleZ + 0.04), 0.2))
    m.add(CHB.car, RMat.gold, 0.003, fnS({ p in
      let d = length(vec2(ell(p), p.y - wallTop(p.z))) - 0.008
      return max(d, (axleZ - 0.03) - p.z)
    }, vec3(0, 0.33, axleZ + 0.04), 0.2))
    m.paint(RMat.gold, fnS({ p in  // an eagle-and-laurel roundel on the front, a gilded band
      let q = p - vec3(0, 0.29, axleZ + 0.1)
      let ring = abs(length(vec2(q.x, q.y)) - 0.032) - 0.004
      let disc = length(vec2(q.x, q.y)) - 0.016
      let band = abs(p.y - (floorY + 0.022)) - 0.006
      return min(min(ring, disc), band)
    }, vec3(0, 0.27, axleZ + 0.1), 0.2), RMat.shield)
    // the axle, wheels with eight spokes and iron tyres, and scythe blades on the axle ends
    m.add(CHB.car, RMat.iron, 0.002,
          capsuleS(vec3(-wheelX - 0.03, axleY, axleZ), vec3(wheelX + 0.03, axleY, axleZ), 0.008))
    for sd in stride(from: -1, through: 1, by: 2) {
      let fs = Float(sd)
      let wb = sd > 0 ? CHB.wheelL : CHB.wheelR
      let c = vec3(fs * wheelX, axleY, axleZ)
      let F = rotZm(1.5708)  // ring in the yz plane
      m.addR(wb, RMat.wood, 0.002, torusS(c, wheelR - 0.009, 0.0085, F))
      m.addR(wb, RMat.iron, 0.001, torusS(c, wheelR - 0.001, 0.005, F))
      m.addR(wb, RMat.wood, 0.003, capsuleS(c - vec3(0.022, 0, 0), c + vec3(0.022, 0, 0), 0.02))
      m.addR(wb, RMat.bronze, 0.002, capsuleS(c + vec3(fs * 0.02, 0, 0), c + vec3(fs * 0.03, 0, 0), 0.013))
      for k in 0..<8 {
        let a = Float(k) * 0.785398
        let d = vec3(0, cos(a), sin(a))
        m.addR(wb, RMat.wood, 0.003, coneS(c + d * 0.016, c + d * (wheelR - 0.012), 0.0062, 0.0045))
      }
      let s0 = c + vec3(fs * 0.036, 0, 0)
      m.add(CHB.car, RMat.steel, 0, fnS({ p in
        let q = p - s0
        let u = fs * q.x  // outwards
        let mu: Float = max(0, u)
        let curve: Float = q.z + 0.6 * mu * mu / 0.09
        let w: Float = 0.011 * (1 - clamp(u / 0.11, 0, 1)) + 0.001
        return max(max(abs(curve) - w, abs(q.y) - 0.0018), max(-u, u - 0.11))
      }, s0 + vec3(fs * 0.055, 0, -0.01), 0.07))
    }
    // --- the charioteer: helmet and crest, a tunic bound with leather straps, a short cloak
    do {
      let d = RomanJ.base(RAsset.pawn)
      let D0 = RChariotJ.D0
      func D(_ v: vec3) -> vec3 { RChariotJ.DU(v) }
      let s = RChariotJ.DS
      for sd in stride(from: -1, through: 1, by: 2) {  // legs (inside the car)
        let fs = Float(sd)
        let hip = D(vec3(d.hipL.x * fs, d.hipL.y, 0)), knee = D(vec3(d.kneeL.x * fs, d.kneeL.y, 0.02))
        let ankle = D(vec3(d.ankleL.x * fs, d.ankleL.y, 0.0))
        m.add(CHB.driver, RMat.skin, 0.006, coneS(hip, knee, 0.036 * s, 0.027 * s))
        m.add(CHB.driver, RMat.skin, 0.006, coneS(knee, ankle, 0.027 * s, 0.019 * s))
        m.add(CHB.driver, RMat.leather, 0.006,
              boxS(vec3(ankle.x, floorY + 0.012, ankle.z + 0.012), vec3(0.02, 0.013, 0.04) * s, 0.008))
      }
      m.add(CHB.driver, RMat.tunic, 0,
            tubeS([vec2(0.3 * s + floorY, 0.09 * s), vec2(0.4 * s + floorY, 0.08 * s), vec2(0.5 * s + floorY, 0.07 * s)],
                  11, 0.003, 1.0, vec3(D0.x, 0, D0.z)))
      let torso = torsoField()
      m.add(CHB.driver, RMat.tunic, 0.02, fnS({ p in torso((p - D0) / s) * s }, D(vec3(0, 0.56, 0)), 0.1))
      m.add(CHB.driver, RMat.leather, 0.003, fnS({ p in  // the fasciae: straps wound round the chest
        let q = (p - D0) / s
        let band = abs(sin(q.y * 150)) - 0.35
        return max(max(torso(q) - 0.004, band * 0.006), max(0.47 - q.y, q.y - 0.62)) * s
      }, D(vec3(0, 0.55, 0)), 0.1))
      for sd in stride(from: -1, through: 1, by: 2) {
        let L = sd > 0
        let sh = D(L ? d.shL : d.shR), el = D(L ? d.elL : d.elR), gr = D(L ? d.gripL : d.gripR)
        let ua = L ? CHB.dlarm : CHB.drarm, fa = L ? CHB.dlfore : CHB.drfore
        m.add(ua, RMat.skin, 0.012, coneS(sh, el, 0.033 * s, 0.027 * s))
        m.add(ua, RMat.tunic, 0.008, coneS(sh, romanMix(sh, el, 0.45), 0.039 * s, 0.033 * s))
        m.add(fa, RMat.skin, 0.008, coneS(el, gr - normalize(gr - el) * 0.012, 0.025 * s, 0.019 * s))
        m.add(fa, RMat.skin, 0.005, ellS(gr, vec3(0.019, 0.024, 0.02) * s, frameY(L ? vec3(0, 0.2, 1) : vec3(0, 1, 0))))
      }
      let hc = D(vec3(0, 0.738, 0.012))
      addHead(m, CHB.dhead, hc, s, false)
      addHelmet(m, CHB.dhead, hc, false, RMat.bronze, RMat.bronze, s)
      // a short cloak flying from his shoulders
      let ct = D(vec3(0, 0.632, -0.03))
      m.add(CHB.cloak, RMat.cloak, 0.004, fnS({ p in
        let q = (p - ct) / s
        let t = clamp(-q.y / 0.26, 0, 1)
        let rx: Float = 0.13 + 0.04 * t, rz: Float = 0.06 + 0.04 * t
        let ex: Float = q.x * q.x / (rx * rx)
        let ez: Float = (q.z + 0.01) * (q.z + 0.01) / (rz * rz)
        let r = (ex + ez).squareRoot()
        let wave: Float = 0.003 * t * sin(atan2(q.x, -q.z) * 5)
        let shell = abs((r - 1) * (rx + rz) * 0.5 - wave) - 0.0075
        return max(max(shell, max(q.y, -0.26 - q.y)), q.z + 0.02) * s
      }, ct + vec3(0, -0.08, -0.03), 0.16))
      // his spear, held upright
      let g = D(d.gripR)
      m.addR(CHB.item, RMat.wood, 0, cylS(g - vec3(0, 0.12, 0), g + vec3(0, 0.36, 0), 0.0058))
      m.addR(CHB.item, RMat.steel, 0, fnS({ p in
        let q = p - (g + vec3(0, 0.36, 0))
        let t = clamp(q.y / 0.07, 0, 1)
        let w: Float = 0.013 * sin(pow(t, 0.7) * 3.14159) + 0.001
        return max(max(abs(q.x) - w, abs(q.z) - 0.0025), max(-q.y, q.y - 0.07))
      }, g + vec3(0, 0.4, 0), 0.05))
    }
    return m
  }

  // --- the siege tower -----------------------------------------------------------
  private static func towerModel() -> Model {
    let W = RTowerJ.W, D = RTowerJ.D, topW = RTowerJ.topW, topD = RTowerJ.topD
    let y0 = RTowerJ.y0, y1 = RTowerJ.y1, y2 = RTowerJ.y2, y3 = RTowerJ.y3
    let wheel1 = RTowerJ.wheel1, ram = RTowerJ.ram, bridge = RTowerJ.bridge, door = RTowerJ.door
    let m = Model()
    m.setSkeleton(skeleton(RAsset.rook))
    let halfW: (Float) -> Float = { y in romanMix(W, topW, clamp((y - y0) / (y3 - y0), 0, 1)) }
    let halfD: (Float) -> Float = { y in romanMix(D, topD, clamp((y - y0) / (y3 - y0), 0, 1)) }
    // the walls: rawhide on the front and sides, planks behind; storeys marked by beams
    let shell: SDFFn = { p in
      let y = clamp(p.y, y0, y3)
      let d = vec2(abs(p.x) - halfW(y), abs(p.z) - halfD(y))
      let side = length(vmax(d, vec2(0))) + min(max(d.x, d.y), 0)
      let box = max(side, max(y0 - p.y, p.y - y3))
      return max(box, -(box + 0.02))  // hollow
    }
    m.add(TWB.body, RMat.timber, 0, fnS(shell, vec3(0, (y0 + y3) * 0.5, 0), 0.75))
    // rawhide nailed over the top storey's sides against fire arrows
    m.paint(RMat.hide, fnS({ p in max(0.17 - abs(p.x), max(y2 + 0.03 - p.y, p.y - y3 + 0.03)) },
                           vec3(0, (y2 + y3) * 0.5, 0), 0.4), RMat.timber)
    // the door opening at the foot and a window for the bridge at the top
    m.sub(RMat.dark, 0.004, boxS(vec3(0, 0.22, 0.21), vec3(0.1, 0.17, 0.04), 0.004))
    m.sub(RMat.dark, 0.004, boxS(vec3(0, (y2 + y3) * 0.5 + 0.01, 0.19), vec3(0.095, 0.16, 0.04), 0.004))
    // arrow slits in the middle storey
    for sd in stride(from: -1, through: 1, by: 2) {
      let fs = Float(sd)
      m.sub(RMat.dark, 0.002, boxS(vec3(fs * 0.09, 0.61, 0.2), vec3(0.012, 0.05, 0.04), 0.003))
      m.sub(RMat.dark, 0.002, boxS(vec3(fs * 0.21, 0.61, 0.0), vec3(0.04, 0.05, 0.012), 0.003))
    }
    m.sub(RMat.dark, 0.002, boxS(vec3(0, 0.61, 0.2), vec3(0.012, 0.05, 0.04), 0.003))
    // the frame: corner posts, storey beams, braces
    for sx in stride(from: -1, through: 1, by: 2) {
      for sz in stride(from: -1, through: 1, by: 2) {
        let fx = Float(sx), fz = Float(sz)
        let a = vec3(fx * (W + 0.006), y0 - 0.01, fz * (D + 0.006))
        let b = vec3(fx * (topW + 0.006), y3 + 0.03, fz * (topD + 0.006))
        m.add(TWB.body, RMat.timber, 0.002, fnS({ p in
          let ab = b - a
          let t = clamp(dot(p - a, ab) / dot(ab, ab), 0, 1)
          let c = a + ab * t
          let q = p - c
          return max(max(abs(q.x) - 0.017, abs(q.z) - 0.017), max(a.y - p.y, p.y - b.y))
        }, (a + b) * 0.5, 0.62))
      }
    }
    let floors: [Float] = [y0, y1, y2, y3]
    for y in floors {
      let w = halfW(y) + 0.012, d = halfD(y) + 0.012
      m.add(TWB.body, RMat.timber, 0.002, fnS({ p in
        let e = vec2(abs(p.x) - w, abs(p.z) - d)
        let rect = length(vmax(e, vec2(0))) + min(max(e.x, e.y), 0)
        return max(abs(rect + 0.006) - 0.009, abs(p.y - y) - 0.014)
      }, vec3(0, y, 0), 0.34))
    }
    for sd in stride(from: -1, through: 1, by: 2) {  // diagonal braces on the sides
      let fs = Float(sd)
      for k in 0..<2 {
        let ya = k == 0 ? y1 : y2, yb = k == 0 ? y2 : y3
        var a = vec3(fs * (halfW(ya) + 0.012), ya + 0.02, -halfD(ya) + 0.02)
        var b = vec3(fs * (halfW(yb) + 0.012), yb - 0.02, halfD(yb) - 0.02)
        if k == 1 {
          let t = a.z
          a.z = b.z
          b.z = t
        }
        m.add(TWB.body, RMat.timber, 0.002, capsuleS(a, b, 0.0095))
      }
    }
    // the roof platform with a parapet of planks, and two soldiers keeping watch
    m.add(TWB.body, RMat.timber, 0.003, boxS(vec3(0, y3 + 0.005, 0), vec3(topW + 0.03, 0.012, topD + 0.03), 0.004))
    m.add(TWB.body, RMat.timber, 0.002, fnS({ p in
      let w = topW + 0.022, d = topD + 0.022
      let e = vec2(abs(p.x) - w, abs(p.z) - d)
      let rect = length(vmax(e, vec2(0))) + min(max(e.x, e.y), 0)
      let saw = abs((abs(p.x + p.z) * 30).truncatingRemainder(dividingBy: 2) - 1)
      let notch: Float = saw > 0.5 ? 1 : 0
      let top: Float = y3 + 0.07 + 0.012 * notch
      return max(abs(rect + 0.006) - 0.007, max(y3 - p.y, p.y - top))
    }, vec3(0, y3 + 0.04, 0), 0.33))
    for sd in stride(from: -1, through: 1, by: 2) {
      let h = vec3(Float(sd) * 0.11, y3 + 0.085, 0.1)
      m.add(TWB.body, RMat.skin, 0.004, sphereS(h, 0.028))
      m.add(TWB.body, RMat.steel, 0.004, fnS({ p in
        let e = SDF.ellipsoid(p - h - vec3(0, 0.008, -0.003), vec3(0.034, 0.032, 0.036))
        return max(e, h.y - 0.004 - p.y)
      }, h, 0.05))
      m.add(TWB.body, RMat.crest, 0.003, fnS({ p in
        let q = p - (h + vec3(0, 0.035, -0.003))
        return max(max(length(vec2(q.y, q.z)) - 0.034, -q.y), abs(q.x) - 0.006)
      }, h + vec3(0, 0.05, 0), 0.05))
      m.add(TWB.body, RMat.tunic, 0.01, ellS(h - vec3(0, 0.045, 0.0), vec3(0.045, 0.03, 0.032)))
    }
    // the battering ram, hung on ropes in the bottom storey: a log with an iron ram's head
    m.addR(TWB.ram, RMat.wood, 0.004, capsuleS(vec3(0, ram.y, -0.17), vec3(0, ram.y, 0.15), 0.032))
    let ramY = ram.y
    m.addR(TWB.ram, RMat.iron, 0.004, fnS({ p in
      let q = p - vec3(0, ramY, 0.17)
      var head = SDF.ellipsoid(q, vec3(0.04, 0.042, 0.045))
      for sd in stride(from: -1, through: 1, by: 2) {  // curled horns
        let horn = SDF.torus(vec3(q.y, q.x - Float(sd) * 0.032, q.z + 0.005), 0.022, 0.009)
        head = SDF.smin(head, horn, 0.008)
      }
      return head
    }, vec3(0, ram.y, 0.17), 0.07))
    let hoops: [Float] = [-0.1, 0.06]
    for z in hoops {
      m.addR(TWB.ram, RMat.iron, 0.001, torusS(vec3(0, ram.y, z), 0.034, 0.005, rotXm(1.5708)))
      let top = vec3(0, y1 - 0.01, z), low = vec3(0, ram.y + 0.03, z)
      m.addStretch(TWB.body, TWB.ram, RMat.rope, 0.002, capsuleS(top, low, 0.0045), along(top, low))
    }
    // the door: a ramp of planks hinged at its foot
    m.addR(TWB.door, RMat.timber, 0.002, boxS(vec3(0, 0.22, door.z + 0.004), vec3(0.106, 0.168, 0.009), 0.003))
    let straps: [Float] = [0.12, 0.31]
    for y in straps {
      m.addR(TWB.door, RMat.iron, 0.001, boxS(vec3(0, y, door.z + 0.014), vec3(0.1, 0.008, 0.003), 0.002))
    }
    // the boarding bridge, standing up across the top window, with its chains
    m.addR(TWB.bridge, RMat.timber, 0.002,
           boxS(vec3(0, bridge.y + 0.17, bridge.z + 0.012), vec3(0.1, 0.17, 0.009), 0.003))
    for k in 0..<4 {
      m.addR(TWB.bridge, RMat.timber, 0.001,
             boxS(vec3(0, bridge.y + 0.05 + Float(k) * 0.08, bridge.z + 0.024), vec3(0.09, 0.006, 0.005), 0.002))
    }
    for sd in stride(from: -1, through: 1, by: 2) {
      let fs = Float(sd)
      let a = vec3(fs * 0.095, bridge.y + 0.33, bridge.z + 0.02), b = vec3(fs * 0.12, y3 + 0.02, bridge.z - 0.01)
      m.addStretch(TWB.bridge, TWB.body, RMat.iron, 0.001, capsuleS(a, b, 0.004), along(a, b))
    }
    // wheels: solid discs of planks with iron tyres
    for i in 0..<4 {
      let sx: Float = i % 2 == 0 ? 1 : -1, sz: Float = i < 2 ? 1 : -1
      let c = vec3(sx * wheel1.x, wheel1.y, sz * wheel1.z)
      let F = rotZm(1.5708)
      let wb = TWB.wheel1 + i
      m.addR(wb, RMat.wood, 0.003, fnS({ p in
        let q = p - c
        return max(length(vec2(q.y, q.z)) - 0.072, abs(q.x) - 0.014)
      }, c, 0.08))
      m.addR(wb, RMat.iron, 0.002, torusS(c, 0.072, 0.006, F))
      m.addR(wb, RMat.iron, 0.002, capsuleS(c - vec3(0.024, 0, 0), c + vec3(0.024, 0, 0), 0.016))
      m.paint(RMat.iron, fnS({ p in  // two cleats across the planks show it turning
        let q = p - c
        return min(abs(q.y) - 0.006, abs(q.z - 0.035) - 0.005)
      }, c, 0.08), RMat.wood)
    }
    for sz in stride(from: -1, through: 1, by: 2) {
      let fz = Float(sz)
      m.add(TWB.body, RMat.iron, 0.002,
            capsuleS(vec3(-wheel1.x, wheel1.y, fz * wheel1.z), vec3(wheel1.x, wheel1.y, fz * wheel1.z), 0.009))
    }
    // the scorpion on the roof: a stand, two torsion springs, the arms and the bolt in its trough
    do {
      let s = RTowerJ.scorpion
      m.add(TWB.body, RMat.timber, 0.003, capsuleS(vec3(0, y3 + 0.01, s.z), s - vec3(0, 0.01, 0), 0.011))
      m.addR(TWB.scorpion, RMat.timber, 0.002, boxS(s + vec3(0, 0.008, 0.0), vec3(0.012, 0.008, 0.085), 0.002))
      m.addR(TWB.scorpion, RMat.timber, 0.002, boxS(s + vec3(0, 0.02, 0.045), vec3(0.046, 0.018, 0.012), 0.003))
      for sd in stride(from: -1, through: 1, by: 2) {
        let fs = Float(sd)
        m.addR(TWB.scorpion, RMat.rope, 0.003,
               cylS(s + vec3(fs * 0.03, -0.005, 0.045), s + vec3(fs * 0.03, 0.048, 0.045), 0.0085))
        m.addR(TWB.scorpion, RMat.bronze, 0.001,
               cylS(s + vec3(fs * 0.03, 0.044, 0.045), s + vec3(fs * 0.03, 0.05, 0.045), 0.011))
        let arm0 = s + vec3(fs * 0.03, 0.02, 0.045), arm1 = s + vec3(fs * 0.075, 0.02, 0.02)
        m.addR(TWB.scorpion, RMat.wood, 0.002, capsuleS(arm0, arm1, 0.0045))
        m.addR(TWB.scorpion, RMat.rope, 0.001, capsuleS(arm1, s + vec3(0, 0.02, -0.06), 0.0018))
      }
      m.addR(TWB.loaded, RMat.wood, 0, capsuleS(s + vec3(0, 0.02, -0.07), s + vec3(0, 0.02, 0.1), 0.0035))
      m.addR(TWB.loaded, RMat.iron, 0, coneS(s + vec3(0, 0.02, 0.1), s + vec3(0, 0.02, 0.125), 0.006, 0.001))
    }
    // the vexillum: a crimson (or blue) banner with a gold fringe
    do {
      let f = RTowerJ.flag
      m.add(TWB.body, RMat.wood, 0.002, cylS(f, f + vec3(0, 0.25, 0), 0.006))
      m.addR(TWB.flag, RMat.wood, 0.002, capsuleS(f + vec3(-0.045, 0.24, 0), f + vec3(0.045, 0.24, 0), 0.004))
      let fc = f + vec3(0, 0.19, 0)
      m.addR(TWB.flag, RMat.cloak, 0.002, fnS({ p in
        let q = p - fc
        let wave = 0.006 * sin(q.x * 60)
        return max(max(abs(q.x) - 0.042, abs(q.y) - 0.045), abs(q.z - wave) - 0.0025)
      }, f + vec3(0, 0.19, 0), 0.07))
      m.paint(RMat.gold, fnS({ p in
        let q = p - fc
        return min(abs(q.y + 0.04) - 0.005, length(vec2(q.x, q.y - 0.005)) - 0.015)
      }, f + vec3(0, 0.19, 0), 0.07), RMat.cloak)
      m.add(TWB.body, RMat.gold, 0.002, sphereS(f + vec3(0, 0.255, 0), 0.01))
    }
    return m
  }

  // --- the props ---------------------------------------------------------------
  private static func pilumModel() -> Model {
    let m = Model()
    addPilum(m, 0, vec3(0), 1)
    return m
  }

  /// A scorpion bolt: butt at the origin, along +y.
  private static func boltModel() -> Model {
    let m = Model()
    m.add(0, RMat.wood, 0, cylS(vec3(0, 0.0, 0), vec3(0, 0.27, 0), 0.0055))
    m.add(0, RMat.iron, 0.002, fnS({ p in
      let q = p - vec3(0, 0.27, 0)
      let t = clamp(q.y / 0.05, 0, 1)
      let w: Float = 0.0095 * (1 - t) + 0.0006
      return max(abs(q.x) + abs(q.z) - w, max(-q.y, q.y - 0.05)) * 0.7
    }, vec3(0, 0.295, 0), 0.04))
    for k in 0..<3 {  // three vanes of wood
      let a = Float(k) * 2.0944
      let d = vec3(sin(a), 0, cos(a))
      m.add(0, RMat.wood, 0.001,
            triS(vec3(0, 0.012, 0) + d * 0.004, vec3(0, 0.07, 0) + d * 0.004, vec3(0, 0.02, 0) + d * 0.022, 0.0012))
    }
    return m
  }

  /// A pool of blood with splashes round it; radius ~1 (scaled down when used).
  private static func poolModel() -> Model {
    let m = Model()
    m.add(0, RMat.blood, 0, fnS({ p in
      let a = atan2(p.x, p.z)
      let r = (p.x * p.x + p.z * p.z).squareRoot()
      let e0: Float = 0.78 + 0.1 * sin(a * 3 + 0.7) + 0.06 * sin(a * 7 + 2.1)
      let edge: Float = e0 + 0.03 * sin(a * 13)
      var d = r - edge
      for k in 0..<7 {  // splashes
        let b = Float(k) * 0.9 + 0.4
        let c = vec2(sin(b), cos(b)) * (0.98 + 0.13 * Float(k % 3))
        d = min(d, length(vec2(p.x, p.z) - c) - (0.06 + 0.025 * Float(k % 4)))
      }
      let th: Float = 0.024 * clamp(-d / 0.25 + 0.25, 0.25, 1)
      return max(d * 0.8, abs(p.y) - th)
    }, vec3(0), 1.2))
    return m
  }

  /// The empress's goblet, held by its stem at the origin.
  private static func gobletModel() -> Model {
    let m = Model()
    let g = vec3(0)
    m.add(0, RMat.gold, 0.002, cylS(g - vec3(0, 0.026, 0), g - vec3(0, 0.022, 0), 0.016))
    m.add(0, RMat.gold, 0, cylS(g - vec3(0, 0.024, 0), g + vec3(0, 0.014, 0), 0.0035))
    m.add(0, RMat.gold, 0.004, fnS({ p in
      let e = SDF.ellipsoid(p - (g + vec3(0, 0.034, 0)), vec3(0.022, 0.026, 0.022))
      return max(max(e, -(e + 0.002)), p.y - (g.y + 0.046))
    }, g + vec3(0, 0.03, 0), 0.04))
    m.add(0, RMat.wine, 0, cylS(g + vec3(0, 0.03, 0), g + vec3(0, 0.041, 0), 0.018))
    return m
  }

  /// The centurion's pugio on its own: grip at the origin, blade along +y.
  private static func knifeModel() -> Model {
    let m = Model()
    addPugio(m, 0, vec3(0), vec3(0, 1, 0))
    return m
  }

  // --- the lion --------------------------------------------------------------------
  private static func lionModel() -> Model {
    let flu = RLionJ.flu, fll = RLionJ.fll, hlu = RLionJ.hlu, hll = RLionJ.hll
    let tail1 = RLionJ.tail1, tail2 = RLionJ.tail2
    let m = Model()
    m.setSkeleton(skeleton(RAsset.lion))
    let bodyF: SDFFn = { p in
      let chest = SDF.ellipsoid(p - vec3(0, 0.31, 0.1), vec3(0.085, 0.1, 0.12))
      let belly = SDF.ellipsoid(p - vec3(0, 0.3, -0.05), vec3(0.075, 0.08, 0.16))
      let haunch = SDF.ellipsoid(p - vec3(0, 0.31, -0.17), vec3(0.08, 0.085, 0.09))
      return SDF.smin(SDF.smin(chest, belly, 0.06), haunch, 0.05)
    }
    m.add(LNB.body, RMat.fur, 0, fnS(bodyF, vec3(0, 0.3, -0.03), 0.3))
    // the mane, shaggy, round the neck and the back of the head
    m.add(LNB.neck, RMat.lionMane, 0.03, fnS({ p in
      let e = SDF.ellipsoid(p - vec3(0, 0.38, 0.21), vec3(0.1, 0.12, 0.1))
      let a = atan2(p.x, p.y - 0.38), b = atan2(p.z - 0.21, p.y - 0.38)
      let s1: Float = 0.012 * sin(a * 9) * sin(b * 7)
      let s2: Float = 0.006 * sin(a * 23 + b * 5)
      return e - s1 - s2
    }, vec3(0, 0.38, 0.21), 0.14))
    m.add(LNB.head, RMat.lionMane, 0.02, ellS(vec3(0, 0.43, 0.25), vec3(0.075, 0.06, 0.06)))
    // the head: skull, muzzle, nose, eyes, ears, the jaw with its teeth
    m.add(LNB.head, RMat.fur, 0.02, ellS(vec3(0, 0.4, 0.31), vec3(0.056, 0.052, 0.06)))
    m.add(LNB.head, RMat.fur, 0.02, ellS(vec3(0, 0.38, 0.37), vec3(0.04, 0.033, 0.038)))
    m.add(LNB.head, RMat.dark, 0.006, ellS(vec3(0, 0.392, 0.405), vec3(0.017, 0.011, 0.009)))
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      m.add(LNB.head, RMat.eye, 0.003, sphereS(vec3(x * 0.025, 0.418, 0.355), 0.008))
      m.add(LNB.head, RMat.fur, 0.006, ellS(vec3(x * 0.042, 0.45, 0.29), vec3(0.016, 0.016, 0.008)))
    }
    m.add(LNB.jaw, RMat.fur, 0.012, ellS(vec3(0, 0.348, 0.36), vec3(0.032, 0.016, 0.04)))
    m.add(LNB.jaw, RMat.dark, 0.004, ellS(vec3(0, 0.356, 0.37), vec3(0.026, 0.006, 0.03)))
    for sd in stride(from: -1, through: 1, by: 2) {
      let x = Float(sd)
      m.add(LNB.head, RMat.teeth, 0, coneS(vec3(x * 0.018, 0.368, 0.392), vec3(x * 0.017, 0.35, 0.394), 0.004, 0.001))
      m.add(LNB.jaw, RMat.teeth, 0, coneS(vec3(x * 0.016, 0.352, 0.388), vec3(x * 0.016, 0.366, 0.39), 0.0035, 0.001))
    }
    // legs and paws with claws
    for sd in stride(from: -1, through: 1, by: 2) {
      let L = sd > 0
      let fu = L ? flu : mirrorXv(flu), fl = L ? fll : mirrorXv(fll)
      let hu = L ? hlu : mirrorXv(hlu), hl = L ? hll : mirrorXv(hll)
      let bFU = L ? LNB.flu : LNB.fru, bFL = L ? LNB.fll : LNB.frl
      let bHU = L ? LNB.hlu : LNB.hru, bHL = L ? LNB.hll : LNB.hrl
      m.add(bFU, RMat.fur, 0.03, coneS(fu + vec3(0, 0.02, 0), fl, 0.045, 0.03))
      m.add(bFL, RMat.fur, 0.015, coneS(fl, vec3(fl.x, 0.03, fl.z + 0.01), 0.028, 0.022))
      m.add(bFL, RMat.fur, 0.012, ellS(vec3(fl.x, 0.018, fl.z + 0.022), vec3(0.026, 0.018, 0.032)))
      m.add(bHU, RMat.fur, 0.035, coneS(hu, hl, 0.055, 0.028))
      m.add(bHL, RMat.fur, 0.015, coneS(hl, vec3(hl.x, 0.03, hl.z + 0.03), 0.026, 0.021))
      m.add(bHL, RMat.fur, 0.012, ellS(vec3(hl.x, 0.018, hl.z + 0.045), vec3(0.025, 0.018, 0.03)))
      for k in -1...1 {
        let fk = Float(k)
        m.add(bFL, RMat.teeth, 0,
              coneS(vec3(fl.x + fk * 0.011, 0.014, fl.z + 0.05), vec3(fl.x + fk * 0.011, 0.004, fl.z + 0.058), 0.0035,
                    0.001))
      }
    }
    m.add(LNB.tail1, RMat.fur, 0.02, coneS(tail1, tail2, 0.016, 0.011))
    m.add(LNB.tail2, RMat.fur, 0.01, coneS(tail2, tail2 + vec3(0, -0.06, -0.1), 0.011, 0.008))
    m.add(LNB.tail2, RMat.lionMane, 0.008, ellS(tail2 + vec3(0, -0.065, -0.11), vec3(0.018, 0.022, 0.026)))
    return m
  }

  // ---------------------------------------------------------------------------
  /// quality: 0 = fast, 1 = normal, 2 = ultra, 3 = extreme
  static func buildLibrary(_ lib: RomanLibrary, quality qIn: Int, progress prog: BuildProgress?) {
    let quality = max(0, min(3, qIn))
    if let prog {
      prog.total = 8
      prog.done = 0
    }
    func tick(_ s: String) {
      if let prog {
        prog.stage = s
        prog.step()
      }
    }
    func buildHuman(_ kind: Int, _ m: Model, _ seed: UInt32) {
      let S = RomanJ.figScale(kind)
      m.scaleAll(S)
      let top: Float = kind == RAsset.queen ? 1.13 : 1.0
      buildAsset(&lib.asset[kind], m, vec3(-0.26, -0.005, -0.2) * S, vec3(0.28, top, 0.3) * S, quality, 0, seed,
                 pieceHeight(kind), pieceRadius(kind))
    }
    buildHuman(RAsset.pawn, legionaryModel(), 7001)
    tick("Arming the legionaries")
    do {
      let m = chariotModel()
      buildAsset(&lib.asset[RAsset.knight], m, vec3(-0.34, -0.005, -0.42), vec3(0.34, 0.92, 0.53), quality, 0, 7101,
                 pieceHeight(RAsset.knight), pieceRadius(RAsset.knight))
      tick("Harnessing the chariots")
    }
    buildHuman(RAsset.bishop, centurionModel(), 7201)
    tick("Promoting the centurions")
    do {
      let m = towerModel()
      buildAsset(&lib.asset[RAsset.rook], m, vec3(-0.3, -0.005, -0.27), vec3(0.3, 1.47, 0.3), quality, 16, 7301,
                 pieceHeight(RAsset.rook), pieceRadius(RAsset.rook))
      tick("Building the siege towers")
    }
    buildHuman(RAsset.queen, empressModel(), 7401)
    tick("Crowning the empresses")
    buildHuman(RAsset.king, emperorModel(), 7501)
    tick("Hailing the emperors")
    do {
      let a = pilumModel()
      buildAsset(&lib.asset[RAsset.pilum], a, vec3(-0.03, -0.01, -0.03), vec3(0.03, 0.98, 0.03), quality, 0, 0, 0.96,
                 0.02)
      let b = boltModel()
      buildAsset(&lib.asset[RAsset.bolt], b, vec3(-0.03, -0.01, -0.03), vec3(0.03, 0.34, 0.03), quality, 0, 0, 0.33,
                 0.02)
      let c = poolModel()
      buildAsset(&lib.asset[RAsset.pool], c, vec3(-1.25, -0.04, -1.25), vec3(1.25, 0.04, 1.25), 0, 0, 0, 0.03, 1.0)
      let d = gobletModel()
      buildAsset(&lib.asset[RAsset.goblet], d, vec3(-0.03, -0.03, -0.03), vec3(0.03, 0.06, 0.03), quality, 0, 0, 0.07,
                 0.03)
      let e = knifeModel()
      buildAsset(&lib.asset[RAsset.knife], e, vec3(-0.025, -0.04, -0.025), vec3(0.025, 0.12, 0.025), quality, 0, 0,
                 0.15, 0.02)
      tick("Forging the pila")
    }
    do {
      let m = lionModel()
      buildAsset(&lib.asset[RAsset.lion], m, vec3(-0.15, -0.01, -0.6), vec3(0.15, 0.56, 0.46), quality, 0, 0, 0.5,
                 0.3)
      tick("Waking the lions")
    }
    lib.built = true
    prog?.finished = true
  }
}
