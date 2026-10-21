// Sculpting the alien army with the shared kit (Sculpt.swift): every figure is a
// list of SDF parts bound to bones and painted with materials, meshed by the
// surface-nets mesher and skinned per vertex.
import Foundation
import simd

/// glm::mix (x * (1 - a) + y * a), so the figures come out exactly as in the C++ version.
@inline(__always) private func gmix(_ x: vec3, _ y: vec3, _ a: Float) -> vec3 { x * (1 - a) + y * a }

// ---------------------------------------------------------------------------
// Shared parts
// ---------------------------------------------------------------------------
/// An alien head facing +z: a big cranium over a small face, huge slanted eyes,
/// nostril slits and a small mouth. c: centre of the cranium, s: scale.
/// jawBone >= 0: the chin is bound to that bone (the mouth can open).
private func addAlienHead(_ m: Model, _ bone: Int, _ jawBone: Int, _ c: vec3, _ s: Float, _ skin: Int) {
  func P(_ x: Float, _ y: Float, _ z: Float) -> vec3 { c + vec3(x, y, z) * s }
  m.add(bone, skin, 0, ellS(P(0, 0, 0), vec3(0.088, 0.082, 0.082) * s))
  m.add(bone, skin, 0.045 * s, ellS(P(0, -0.056, 0.024), vec3(0.05, 0.048, 0.05) * s))
  // cheekbones and a brow ridge
  m.add2(bone, bone, skin, 0.025 * s, ellS(P(0.04, -0.046, 0.045), vec3(0.018, 0.013, 0.02) * s))
  // the chin (opens with the jaw)
  m.add(jawBone >= 0 ? jawBone : bone, skin, 0.018 * s, ellS(P(0, -0.083, 0.045), vec3(0.028, 0.016, 0.028) * s))
  // eyes: deep sockets with big glossy almond eyes, slanted up and outwards
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let R = rotYm(0.42 * x) * rotZm(0.4 * x)
    let e = P(0.039 * x, -0.022, 0.069)
    m.sub(-1, 0.006 * s, ellS(e + vec3(0, 0, 0.005) * s, vec3(0.034, 0.0225, 0.015) * s, R))
    m.add(bone, AMat.eye, 0.002 * s, ellS(e + vec3(0, 0, 0.002) * s, vec3(0.032, 0.0205, 0.0135) * s, R))
  }
  // nostrils and the mouth
  for sd in stride(from: -1, through: 1, by: 2) {
    m.sub(skin, 0.002 * s, ellS(P(0.0065 * Float(sd), -0.052, 0.076), vec3(0.0022, 0.004, 0.004) * s))
  }
  m.add(bone, AMat.dark, 0.003 * s, ellS(P(0, -0.072, 0.052), vec3(0.013, 0.006, 0.018) * s))
  m.paint(AMat.dark, ellS(P(0, -0.07, 0.074), vec3(0.017, 0.0028, 0.012) * s, rotXm(-0.15)), skin)
}

/// A three-fingered hand. open = 0: a fist around a grip along 'dir'; 1: fingers spread forward.
private func addHand(_ m: Model, _ bone: Int, _ wrist: vec3, _ dir0: vec3, _ side0: vec3, _ s: Float, _ open: Float,
                     _ mat: Int) {
  let dir = normalize(dir0)
  let side = normalize(side0 - dir * dot(side0, dir))
  let up = cross(dir, side)
  m.add(bone, mat, 0.008 * s, ellS(wrist + dir * 0.012 * s, vec3(0.013, 0.009, 0.015) * s, mat3(side, up, dir)))
  for f in -1...1 {
    let ff = Float(f)
    let d22: vec3 = dir * 0.022 * s, sx: vec3 = side * (0.007 * ff) * s
    let a: vec3 = wrist + d22 + sx
    let fwd: vec3 = dir * (0.4 + 0.6 * open) - up * (1 - open)
    let d = normalize(fwd + side * (0.25 * ff * open))
    let len: Float = (f == 0 ? 0.03 : 0.026) * s
    m.add(bone, mat, 0.004 * s, capsuleS(a, a + d * len, 0.0042 * s))
    m.add(bone, mat, 0.002 * s, sphereS(a + d * len, 0.0052 * s))  // fingertip pads
  }
  // the thumb
  let d12: vec3 = dir * 0.012 * s, d30: vec3 = dir * 0.03 * s
  let t0: vec3 = wrist + d12 + up * 0.006 * s
  let u16: vec3 = up * 0.016 * s, s6: vec3 = side * 0.006 * s
  let t1: vec3 = wrist + d30 + u16 - s6
  m.add(bone, mat, 0.004 * s, capsuleS(t0, t1, 0.004 * s))
}

/// A retro ray gun along +z with its grip at g.
private func addRayGun(_ m: Model, _ bone: Int, _ g: vec3, _ s: Float) {
  func P(_ x: Float, _ y: Float, _ z: Float) -> vec3 { g + vec3(x, y, z) * s }
  m.addR(bone, AMat.rubber, 0.004 * s,
         boxS(P(0, -0.012, -0.004), vec3(0.0075, 0.017, 0.01) * s, 0.006 * s, rotXm(-0.25)))
  m.addR(bone, AMat.chrome, 0.006 * s, ellS(P(0, 0.012, 0.008), vec3(0.015, 0.016, 0.026) * s))
  m.addR(bone, AMat.chrome, 0.004 * s, coneS(P(0, 0.012, 0.02), P(0, 0.012, 0.066), 0.0085 * s, 0.0048 * s))
  for i in 0..<3 {
    let fi = Float(i)
    m.addR(bone, AMat.gold, 0.002 * s,
           torusS(P(0, 0.012, 0.032 + 0.012 * fi), (0.0095 - 0.0012 * fi) * s, 0.0022 * s, frameY(vec3(0, 0, 1))))
  }
  m.addR(bone, AMat.light, 0.002 * s, sphereS(P(0, 0.012, 0.069), 0.0058 * s))
  // fins
  m.addR(bone, AMat.chrome, 0.003 * s, boxS(P(0, 0.03, 0.0), vec3(0.0015, 0.008, 0.012) * s, 0.0012 * s))
  m.addR(bone, AMat.light, 0.002 * s, sphereS(P(0, 0.012, -0.017), 0.006 * s))
}

/// Antennae: a thin stalk and a glowing bulb.
private func addAntennae(_ m: Model, _ boneL: Int, _ boneR: Int, _ rootL: vec3, _ tipL: vec3, _ r: Float,
                         _ bulb: Float, _ skin: Int) {
  for sd in stride(from: -1, through: 1, by: 2) {
    let b = sd > 0 ? boneL : boneR
    let a = sd > 0 ? rootL : mirrorXv(rootL), t = sd > 0 ? tipL : mirrorXv(tipL)
    m.add(b, skin, 0.006, sphereS(a, r * 1.9))
    m.add(b, skin, 0.004, capsuleS(a, t, r))
    m.add(b, AMat.light, 0.003, sphereS(t + normalize(t - a) * bulb * 0.6, bulb))
  }
}

// ---------------------------------------------------------------------------
// Pawn: the little green man
// ---------------------------------------------------------------------------
private func pawnModel() -> Model {
  let j = AlienJ.pawn()
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.pawn))
  // legs in the jumpsuit, chrome boots
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let th = sd > 0 ? AH.lThigh : AH.rThigh, sh = sd > 0 ? AH.lShin : AH.rShin
    let hip = vec3(j.hipL.x * x, j.hipL.y, j.hipL.z), knee = vec3(j.kneeL.x * x, j.kneeL.y, j.kneeL.z)
    let ankle = vec3(j.ankleL.x * x, j.ankleL.y, j.ankleL.z)
    m.add(th, AMat.suit, 0, coneS(hip, knee, 0.025, 0.02))
    m.add(sh, AMat.suit, 0.01, coneS(knee, ankle + vec3(0, 0.03, 0), 0.02, 0.018))
    m.add(sh, AMat.chrome, 0.008, coneS(ankle + vec3(0, 0.036, 0), ankle, 0.0205, 0.021))
    m.add(sh, AMat.chrome, 0.012, boxS(vec3(ankle.x, 0.017, 0.012), vec3(0.019, 0.015, 0.034), 0.013))
    m.add(sh, AMat.rubber, 0, boxS(vec3(ankle.x, 0.004, 0.012), vec3(0.02, 0.004, 0.036), 0.004))
  }
  // body
  m.add(AH.pelvis, AMat.suit, 0, ellS(vec3(0, 0.222, 0.002), vec3(0.052, 0.046, 0.04)))
  m.add(AH.chest, AMat.suit, 0.035, ellS(vec3(0, 0.302, 0), vec3(0.058, 0.066, 0.043)))
  m.add2(AH.chest, AH.chest, AMat.chrome, 0.01, ellS(vec3(0.066, 0.35, -0.004), vec3(0.025, 0.013, 0.025)))
  m.add(AH.chest, AMat.chrome, 0.006, torusS(vec3(0, 0.36, -0.002), 0.029, 0.0062))
  m.add(AH.pelvis, AMat.chrome, 0.004, torusS(vec3(0, 0.244, 0.002), 0.054, 0.0058))
  m.add(AH.pelvis, AMat.light, 0.003, boxS(vec3(0, 0.244, 0.056), vec3(0.011, 0.008, 0.004), 0.003))
  // chest emblem: a glowing disc in a chrome ring
  m.add(AH.chest, AMat.chrome, 0.004, torusS(vec3(0, 0.318, 0.041), 0.013, 0.0035, frameY(vec3(0, 0.25, 1))))
  m.add(AH.chest, AMat.light, 0.003, ellS(vec3(0, 0.318, 0.04), vec3(0.01, 0.01, 0.005)))
  // arms: sleeves to the elbow, bare skinny forearms
  for sd in stride(from: -1, through: 1, by: 2) {
    let L = sd > 0
    let sh = L ? j.shL : j.shR, el = L ? j.elL : j.elR, wr = L ? j.wrL : j.wrR
    m.add(L ? AH.lArm : AH.rArm, AMat.suit, 0.012, coneS(sh, el, 0.0175, 0.0145))
    m.add(L ? AH.lFore : AH.rFore, AMat.skin, 0.008, coneS(el, wr, 0.0118, 0.0098))
    let d = normalize(wr - el)
    m.add(L ? AH.lFore : AH.rFore, AMat.chrome, 0.004, coneS(el + d * 0.008, el + d * 0.02, 0.0152, 0.014))
  }
  addHand(m, AH.rFore, j.wrR, vec3(0, -0.1, 1), vec3(1, 0, 0), 1, 0, AMat.skin)
  addHand(m, AH.lFore, j.wrL, vec3(0, -0.6, 1), vec3(-1, 0, 0), 1, 0.8, AMat.skin)
  addRayGun(m, AH.itemR, j.gripR, 1)
  // neck and the head
  m.add(AH.head, AMat.skin, 0.012, capsuleS(vec3(0, 0.355, 0), vec3(0, 0.41, 0.012), 0.019))
  addAlienHead(m, AH.head, AH.jaw, vec3(0, 0.49, 0.008), 1, AMat.skin)
  addAntennae(m, AH.antL, AH.antR, j.antL, vec3(0.06, 0.635, -0.032), 0.0042, 0.0125, AMat.skin)
  // the tongue, rolled up behind the lips (it shoots out in a battle)
  m.addR(AH.tail1, AMat.pink, 0, capsuleS(j.tail1 - vec3(0, 0, 0.008), j.tail2, 0.0075))
  m.addR(AH.tail2, AMat.pink, 0.002, ellS(j.tail2 + vec3(0, 0, 0.003), vec3(0.011, 0.0065, 0.009)))
  m.scaleAll(Alien.PS)
  return m
}

// ---------------------------------------------------------------------------
// Knight: the flying saucer and its pilot
// ---------------------------------------------------------------------------
private func knightModel() -> Model {
  let H0 = Alien.H0
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.knight))
  var hull = Profile()
  hull.build([vec2(0, -0.064), vec2(0.08, -0.06), vec2(0.13, -0.047), vec2(0.2, -0.029), vec2(0.27, -0.013),
              vec2(0.305, -0.004), vec2(0.318, 0.0), vec2(0.305, 0.005), vec2(0.27, 0.013), vec2(0.22, 0.029),
              vec2(0.17, 0.046), vec2(0.115, 0.06), vec2(0.06, 0.067), vec2(0, 0.068)], 3)
  let c = UJ.body
  m.add(UB.body, AMat.hull, 0, latheS(hull, c))
  // the underside is polished metal, with a ring of ports and the beam emitter
  m.paint(AMat.chrome, boxS(c - vec3(0, 0.2, 0), vec3(0.4, 0.2 - 0.006, 0.4), 0), AMat.hull)
  m.add(UB.body, AMat.chrome, 0.004, torusS(c + vec3(0, 0.032, 0), 0.2, 0.0055))
  m.add(UB.body, AMat.chrome, 0.004, torusS(c - vec3(0, 0.04, 0), 0.15, 0.006))
  for i in 0..<8 {
    let a: Float = 6.2831853 * (Float(i) + 0.5) / 8
    let d = vec3(sin(a), 0, cos(a))
    m.add(UB.body, AMat.dark, 0.003, ellS(c + d * 0.15 + vec3(0, 0.046, 0), vec3(0.018, 0.005, 0.018)))
    m.add(UB.body, AMat.energy, 0.003, sphereS(c + d * 0.215 - vec3(0, 0.026, 0), 0.0105))
  }
  // the light ring round the rim (it spins)
  m.add(UB.rim, AMat.chrome, 0.004, torusS(c, 0.304, 0.0105))
  for i in 0..<14 {
    let a: Float = 6.2831853 * Float(i) / 14
    m.add(UB.rim, AMat.light, 0.002, sphereS(c + vec3(sin(a), 0, cos(a)) * 0.314, 0.0112))
  }
  // the beam emitter (lowers when the hatch opens)
  m.add(UB.body, AMat.chrome, 0.004, torusS(c - vec3(0, 0.06, 0), 0.058, 0.008))
  m.addR(UB.hatch, AMat.energy, 0.004, ellS(c - vec3(0, 0.062, 0), vec3(0.048, 0.013, 0.048)))
  m.addR(UB.hatch, AMat.chrome, 0.002, torusS(c - vec3(0, 0.068, 0), 0.03, 0.004))
  // landing legs (sculpted out; folded up under the hull while flying)
  for i in 0..<3 {
    let r = UJ.legRoot(i), d = vec3(sin(UJ.legA[i]), 0, cos(UJ.legA[i]))
    let knee = r + d * 0.035 - vec3(0, 0.06, 0), foot = r + d * 0.055 - vec3(0, 0.115, 0)
    m.addR(UB.leg1 + i, AMat.chrome, 0.004, sphereS(r, 0.012))
    m.addR(UB.leg1 + i, AMat.chrome, 0.004, capsuleS(r, knee, 0.0075))
    m.addR(UB.leg1 + i, AMat.chrome, 0.004, capsuleS(knee, foot, 0.006))
    m.addR(UB.leg1 + i, AMat.rubber, 0.004, ellS(foot, vec3(0.02, 0.006, 0.02)))
  }
  // the cockpit: chrome collar, a dark floor, the console
  m.add(UB.body, AMat.chrome, 0.004, torusS(c + vec3(0, 0.066, 0), 0.129, 0.0105))
  m.add(UB.body, AMat.dark, 0.004, cylS(c + vec3(0, 0.06, 0), c + vec3(0, 0.072, 0), 0.12))
  m.add(UB.body, AMat.chrome, 0.004,
        boxS(c + vec3(0, 0.083, 0.07), vec3(0.04, 0.012, 0.014), 0.006, rotXm(-0.5)))
  for i in -1...1 {
    m.add(UB.body, i == 0 ? AMat.gem : AMat.light, 0.002, sphereS(c + vec3(0.02 * Float(i), 0.092, 0.072), 0.0045))
  }
  // the pilot: a little green man from the waist up
  let pilot = UJ.pilot, armL = UJ.armL
  m.add(UB.pilot, AMat.suit, 0, ellS(pilot + vec3(0, 0.008, -0.006), vec3(0.036, 0.032, 0.027)))
  m.add(UB.pilot, AMat.chrome, 0.004, torusS(pilot + vec3(0, 0.035, -0.004), 0.018, 0.004))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let sh = sd > 0 ? armL : mirrorXv(armL)
    let hand = vec3(0.024 * x, H0 + 0.083, 0.058)
    let elbow = sh + vec3(0.006 * x, -0.022, 0.02)
    let arm = sd > 0 ? UB.lArm : UB.rArm
    m.add(arm, AMat.suit, 0.008, coneS(sh, elbow, 0.0105, 0.009))
    m.add(arm, AMat.skin, 0.006, coneS(elbow, hand, 0.0075, 0.0065))
    m.add(arm, AMat.skin, 0.004, sphereS(hand, 0.009))
  }
  addAlienHead(m, UB.head, -1, UJ.head + vec3(0, 0.02, 0.008), 0.5, AMat.skin)
  m.add(UB.head, AMat.skin, 0.008, capsuleS(pilot + vec3(0, 0.03, 0), UJ.head + vec3(0, 0.0, 0.004), 0.011))
  addAntennae(m, UB.antL, UB.antR, UJ.antL, UJ.antL + vec3(0.01, 0.022, -0.005), 0.0026, 0.0065, AMat.skin)
  m.scaleAll(Alien.US)
  return m
}

private func canopyModel() -> Model {
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.knight))
  let c = UJ.body + vec3(0, 0.066, 0)
  m.add(UB.body, AMat.chrome, 0, fnS({ p in
    let s = abs(length(p - c) - 0.127) - 0.006
    return max(s, c.y + 0.004 - p.y)
  }, c + vec3(0, 0.065, 0), 0.15))
  m.scaleAll(Alien.US)
  return m
}

// ---------------------------------------------------------------------------
// Bishop: the psychic and his brain
// ---------------------------------------------------------------------------
private func bishopModel() -> Model {
  let j = AlienJ.bishop()
  let S = Alien.BS
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.bishop))
  func P(_ x: Float, _ y: Float, _ z: Float) -> vec3 { vec3(x, y, z) * S }
  // the floating disc
  do {
    var disc = Profile()
    disc.build([vec2(0, -0.03), vec2(0.12, -0.026), vec2(0.19, -0.012), vec2(0.205, 0.0), vec2(0.19, 0.012),
                vec2(0.12, 0.016), vec2(0, 0.017)], 3)
    // (the disc is sculpted at its unscaled size and placed under the scaled figure)
    m.add(AH.base, AMat.chrome, 0, latheS(disc, j.base))
    m.add(AH.base, AMat.light, 0.003, torusS(j.base, 0.2, 0.0065))
    m.add(AH.base, AMat.energy, 0.004, ellS(j.base - vec3(0, 0.03, 0), vec3(0.08, 0.01, 0.08)))
    m.add(AH.base, AMat.hull, 0.004, cylS(j.base + vec3(0, 0.012, 0), j.base + vec3(0, 0.02, 0), 0.15))
  }
  // legs crossed in the lotus position, a flowing robe
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let th = sd > 0 ? AH.lThigh : AH.rThigh, sh = sd > 0 ? AH.lShin : AH.rShin
    let hip = P(0.04 * x, 0.245, 0), knee = P(0.12 * x, 0.215, 0.08), ankle = P(-0.035 * x, 0.205, 0.115)
    m.add(th, AMat.suit, 0.02, coneS(hip, knee, 0.034 * S, 0.026 * S))
    m.add(sh, AMat.suit, 0.012, coneS(knee, ankle, 0.026 * S, 0.02 * S))
    m.add(sh, AMat.skin, 0.006, ellS(ankle + P(-0.012 * x, 0.004, 0.008), P(0.026, 0.012, 0.016)))
  }
  m.add(AH.pelvis, AMat.suit, 0.03, ellS(P(0, 0.25, -0.01), P(0.075, 0.04, 0.06)))
  m.add(AH.chest, AMat.suit, 0.035, ellS(P(0, 0.33, -0.012), P(0.06, 0.085, 0.044)))
  // the psychic collar: a tall flared chrome ruff behind the head
  m.add(AH.chest, AMat.chrome, 0.006, fnS({ p in
    let q = p - vec3(0, 0.43, -0.035) * S
    let r = (q.x * q.x + q.z * q.z).squareRoot()
    let shell = abs(r - (0.06 + 0.55 * max(0, q.y)) * S) - 0.005 * S
    let h = max(-q.y - 0.02 * S, q.y - 0.09 * S)
    return max(max(shell, h), q.z - 0.0)
  }, P(0, 0.47, -0.06), 0.13 * S))
  m.add(AH.chest, AMat.light, 0.004, torusS(P(0, 0.4, -0.012), 0.042 * S, 0.005 * S))
  for sd in stride(from: -1, through: 1, by: 2) {
    let L = sd > 0
    let sh = L ? j.shL : j.shR, el = L ? j.elL : j.elR, wr = L ? j.wrL : j.wrR
    m.add(L ? AH.lArm : AH.rArm, AMat.suit, 0.012, coneS(sh, el, 0.017 * S, 0.015 * S))
    m.add(L ? AH.lFore : AH.rFore, AMat.suit, 0.008, coneS(el, wr, 0.015 * S, 0.019 * S))  // wide sleeves
    addHand(m, L ? AH.lFore : AH.rFore, wr, vec3(0, 0.15, 1), vec3(L ? -1 : 1, 0, 0), S, 1, AMat.skin)
  }
  // the little face under an enormous brain
  let face = P(0, 0.485, 0.03)
  m.add(AH.head, AMat.skin, 0.012, capsuleS(P(0, 0.42, -0.005), P(0, 0.47, 0.01), 0.02 * S))
  m.add(AH.head, AMat.skin, 0, ellS(face, P(0.05, 0.058, 0.048)))
  m.add(AH.jaw, AMat.skin, 0.02 * S, ellS(face + P(0, -0.042, 0.012), P(0.026, 0.02, 0.03)))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let R = rotYm(0.45 * x) * rotZm(0.4 * x)
    let e = face + P(0.023 * x, 0.002, 0.04)
    m.sub(-1, 0.005 * S, ellS(e + P(0, 0, 0.003), P(0.021, 0.0125, 0.01), R))
    m.add(AH.head, AMat.eye, 0.002, ellS(e, P(0.0195, 0.0115, 0.009), R))
  }
  m.paint(AMat.dark, ellS(face + P(0, -0.04, 0.042), P(0.011, 0.002, 0.01)), AMat.skin)
  let bc = P(0, 0.6, -0.01)
  m.add(AH.head, AMat.brain, 0.025 * S, ellS(bc, P(0.122, 0.102, 0.13)))
  m.sub(AMat.brain, 0.008 * S, capsuleS(bc + P(0, 0.105, -0.14), bc + P(0, 0.1, 0.13), 0.008 * S))  // the fissure
  m.add(AH.head, AMat.gem, 0.003, sphereS(face + P(0, 0.035, 0.045), 0.009 * S))  // a third eye
  // the jar's collar
  m.add(AH.head, AMat.chrome, 0.006, torusS(P(0, 0.515, -0.01), 0.142 * S, 0.009 * S))
  m.add(AH.head, AMat.light, 0.003, torusS(P(0, 0.515, -0.01), 0.152 * S, 0.0035 * S))
  return m
}

private func jarModel() -> Model {
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.bishop))
  let c = vec3(0, 0.585, -0.01) * Alien.BS
  let y0: Float = 0.515 * Alien.BS, R: Float = 0.168 * Alien.BS
  m.add(AH.head, AMat.chrome, 0, fnS({ p in
    let s = abs(length(p - c) - R) - 0.006
    return max(s, y0 - p.y)
  }, c, R + 0.01))
  return m
}

// ---------------------------------------------------------------------------
// Rook: the tripod
// ---------------------------------------------------------------------------
private func rookModel() -> Model {
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.rook))
  let c = TJ.body
  // the pod: a domed hull with a chrome belt of lights
  m.add(TB.body, AMat.hull, 0, ellS(c, vec3(0.205, 0.115, 0.205)))
  m.add(TB.body, AMat.hull, 0.04, ellS(c + vec3(0, 0.075, -0.012), vec3(0.158, 0.1, 0.162)))
  m.add(TB.body, AMat.chrome, 0.006, torusS(c + vec3(0, 0.005, 0), 0.203, 0.0135))
  for i in 0..<12 {
    let a: Float = 6.2831853 * (Float(i) + 0.5) / 12
    m.add(TB.body, AMat.light, 0.002, sphereS(c + vec3(sin(a) * 0.214, 0.005, cos(a) * 0.214), 0.0095))
  }
  for i in 0..<6 {  // vents on the dome
    let a: Float = 6.2831853 * Float(i) / 6 + 0.5
    let d = vec3(sin(a), 0, cos(a))
    let F = mat3(cross(vec3(0, 1, 0), d), vec3(0, 1, 0), d) * rotXm(-0.6)
    m.add(TB.body, AMat.dark, 0.003, boxS(c + d * 0.125 + vec3(0, 0.11, 0), vec3(0.025, 0.006, 0.007), 0.004, F))
  }
  // underside: a ribbed cone where the legs and tentacles meet
  m.add(TB.body, AMat.chrome, 0.02, coneS(c - vec3(0, 0.07, 0), c - vec3(0, 0.13, 0), 0.12, 0.06))
  for i in 0..<3 {
    let fi = Float(i)
    m.add(TB.body, AMat.dark, 0.002, torusS(c - vec3(0, 0.09 + 0.018 * fi, 0), 0.105 - 0.014 * fi, 0.004))
  }
  // an antenna dish and a beacon on top
  m.add(TB.body, AMat.chrome, 0.004, capsuleS(c + vec3(0.05, 0.15, -0.06), c + vec3(0.065, 0.24, -0.07), 0.0045))
  let dishRt = rotXm(0.6).transpose
  m.add(TB.body, AMat.chrome, 0.003, fnS({ p in
    let q = dishRt * (p - (c + vec3(0.068, 0.245, -0.07)))
    let r = (q.x * q.x + q.z * q.z).squareRoot()
    let bowl = abs(q.y - 0.9 * r * r / 0.04) - 0.002
    return max(bowl, r - 0.03)
  }, c + vec3(0.068, 0.25, -0.07), 0.04))
  m.add(TB.body, AMat.light, 0.004, sphereS(c + vec3(0, 0.168, -0.01), 0.012))
  // the heat-ray eye in its hood
  let h = TJ.hood
  m.add(TB.hood, AMat.hull, 0.02, ellS(h + vec3(0, 0.022, 0.02), vec3(0.07, 0.045, 0.075)))
  m.add(TB.hood, AMat.chrome, 0.006, cylS(h + vec3(0, 0, 0.0), h + vec3(0, 0, 0.1), 0.048))
  m.add(TB.hood, AMat.chrome, 0.003, torusS(h + vec3(0, 0, 0.1), 0.046, 0.0075, frameY(vec3(0, 0, 1))))
  m.add(TB.hood, AMat.energy, 0.002, ellS(h + vec3(0, 0, 0.1), vec3(0.038, 0.038, 0.012)))
  m.add(TB.hood, AMat.dark, 0.002, sphereS(h + vec3(0, 0, 0.108), 0.012))
  for sd in stride(from: -1, through: 1, by: 2) {  // little side lamps
    m.add(TB.hood, AMat.light, 0.002, sphereS(h + vec3(0.058 * Float(sd), 0.028, 0.07), 0.0085))
  }
  // legs: chrome tubes with ball joints and clawed feet
  for i in 0..<3 {
    let up = TB.l1u + 2 * i, lo = up + 1
    let a = TJ.hip(i), k = TJ.knee(i), f = TJ.foot(i)
    m.add(up, AMat.chrome, 0.006, sphereS(a, 0.038))
    m.add(up, AMat.hull, 0.008, coneS(a, k, 0.03, 0.024))
    m.add(up, AMat.chrome, 0.004,
          capsuleS(gmix(a, k, 0.15) + vec3(0, 0.03, 0), gmix(a, k, 0.75) + vec3(0, 0.03, 0), 0.008))  // piston
    m.add(lo, AMat.chrome, 0.006, sphereS(k, 0.034))
    m.add(lo, AMat.hull, 0.008, coneS(k, gmix(k, f, 0.35), 0.024, 0.019))
    m.add(lo, AMat.chrome, 0.006, coneS(gmix(k, f, 0.33), f + vec3(0, 0.03, 0), 0.016, 0.013))
    for r in 2...5 {
      let q = gmix(k, f, 0.14 * Float(r))
      m.add(lo, AMat.rubber, 0.002, torusS(q, 0.018 - 0.0008 * Float(r), 0.004, frameY(k - f)))
    }
    m.add(lo, AMat.chrome, 0.005, sphereS(f + vec3(0, 0.03, 0), 0.02))
    for t in -1...1 {  // three toes
      let ta = TJ.legA[i] + 0.9 * Float(t)
      let td = vec3(sin(ta), 0, cos(ta))
      m.add(lo, AMat.chrome, 0.004, coneS(f + vec3(0, 0.025, 0), f + td * 0.04, 0.008, 0.004))
    }
  }
  // tentacles: ribbed hoses ending in three-fingered claws
  for sd in stride(from: -1, through: 1, by: 2) {
    let b1 = sd > 0 ? TB.tl1 : TB.tr1
    let p0 = sd > 0 ? TJ.tl1 : mirrorXv(TJ.tl1), p1 = sd > 0 ? TJ.tl2 : mirrorXv(TJ.tl2)
    let p2 = sd > 0 ? TJ.tl3 : mirrorXv(TJ.tl3), p3 = sd > 0 ? TJ.tlTip : mirrorXv(TJ.tlTip)
    let pts = [p0, p1, p2, p3]
    for s in 0..<3 {
      let fs = Float(s)
      m.add(b1 + s, AMat.rubber, 0.008, coneS(pts[s], pts[s + 1], 0.019 - 0.003 * fs, 0.016 - 0.003 * fs))
      for r in 0..<3 {
        m.add(b1 + s, AMat.chrome, 0.002,
              torusS(gmix(pts[s], pts[s + 1], 0.2 + 0.3 * Float(r)), 0.0195 - 0.003 * fs, 0.0042,
                     frameY(pts[s + 1] - pts[s])))
      }
    }
    m.add(b1 + 2, AMat.chrome, 0.004, sphereS(p3, 0.013))
    for t in 0..<3 {
      let a: Float = 6.2831853 * Float(t) / 3
      let o = vec3(sin(a) * 0.012, 0, cos(a) * 0.012)
      m.add(b1 + 2, AMat.chrome, 0.003, coneS(p3 + o, p3 + o * 1.8 - vec3(0, 0.035, 0), 0.0055, 0.002))
    }
  }
  return m
}

// ---------------------------------------------------------------------------
// Queen: the crested alien queen
// ---------------------------------------------------------------------------
private func queenModel() -> Model {
  let j = AlienJ.queen()
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.queen))
  // the gown, floating just above the board, with a gold hem
  var gown = Profile()
  gown.build([vec2(0, 0.035), vec2(0.2, 0.035), vec2(0.205, 0.05), vec2(0.17, 0.15), vec2(0.13, 0.28),
              vec2(0.095, 0.4), vec2(0.072, 0.47), vec2(0.07, 0.5), vec2(0, 0.5)], 3)
  let prof = gown
  let gownF: SDFFn = { p in
    let a = atan2(p.x, p.z)
    let folds = 0.006 * sin(a * 9) * clamp((0.36 - p.y) / 0.25, 0, 1)
    return prof.eval(p) - folds
  }
  m.add(AH.pelvis, AMat.robe, 0, fnS(gownF, vec3(0, 0.27, 0), 0.3))
  m.add(AH.pelvis, AMat.gold, 0.006, torusS(vec3(0, 0.05, 0), 0.197, 0.008))
  m.paint(AMat.gold, fnS({ p in max(gownF(p) - 0.004, abs(p.x) - 0.012) }, vec3(0, 0.3, 0.1), 0.3), AMat.robe)
  // carapace bodice, spiked shoulders, jewelled collar
  m.add(AH.chest, AMat.shell, 0.03, ellS(vec3(0, 0.59, -0.006), vec3(0.07, 0.11, 0.052)))
  m.add(AH.pelvis, AMat.shell, 0.03, ellS(vec3(0, 0.49, -0.006), vec3(0.06, 0.05, 0.048)))
  for i in 0..<4 {
    let fi = Float(i)
    m.sub(AMat.shell, 0.004, torusS(vec3(0, 0.5 + 0.035 * fi, -0.006), 0.063 + 0.006 * sin(fi * 1.3), 0.003))
  }
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    m.add(AH.chest, AMat.shell, 0.012, ellS(vec3(0.078 * x, 0.708, -0.014), vec3(0.03, 0.018, 0.032)))
    m.add(AH.chest, AMat.shell, 0.006,
          coneS(vec3(0.082 * x, 0.72, -0.02), vec3(0.122 * x, 0.79, -0.04), 0.012, 0.002))
  }
  m.add(AH.chest, AMat.gold, 0.006, torusS(vec3(0, 0.69, -0.004), 0.042, 0.006, frameY(vec3(0, 1, 0.2))))
  m.add(AH.chest, AMat.gem, 0.003, ellS(vec3(0, 0.665, 0.046), vec3(0.013, 0.017, 0.008)))
  // arms with carapace bracers and long fingers
  for sd in stride(from: -1, through: 1, by: 2) {
    let L = sd > 0
    let sh = L ? j.shL : j.shR, el = L ? j.elL : j.elR, wr = L ? j.wrL : j.wrR
    let fore = L ? AH.lFore : AH.rFore
    m.add(L ? AH.lArm : AH.rArm, AMat.skin, 0.012, coneS(sh, el, 0.017, 0.0135))
    m.add(fore, AMat.skin, 0.008, coneS(el, wr, 0.013, 0.01))
    m.add(fore, AMat.shell, 0.004, coneS(gmix(el, wr, 0.3), gmix(el, wr, 0.85), 0.0155, 0.0125))
    addHand(m, fore, wr, L ? vec3(0, -0.5, 1) : vec3(0, 0.1, 1), vec3(L ? -1 : 1, 0, 0), 1.15, L ? 0.8 : 0,
            AMat.skin)
  }
  // the sceptre: a gold staff with a glowing orb in a claw
  do {
    let g = j.gripR
    m.addR(AH.itemR, AMat.gold, 0, cylS(g - vec3(0, 0.36, 0), g + vec3(0, 0.3, 0), 0.0075))
    m.addR(AH.itemR, AMat.gold, 0.004, sphereS(g - vec3(0, 0.36, 0), 0.011))
    for t in 0..<3 {
      let a: Float = 6.2831853 * Float(t) / 3
      let o = vec3(sin(a), 0, cos(a))
      m.addR(AH.itemR, AMat.gold, 0.004,
             coneS(g + vec3(0, 0.29, 0), g + vec3(0, 0.345, 0) + o * 0.03, 0.006, 0.0025))
    }
    m.addR(AH.itemR, AMat.gem, 0.003, sphereS(g + vec3(0, 0.335, 0), 0.022))
  }
  // neck with gold rings, the face, and the great sweeping crest
  m.add(AH.head, AMat.skin, 0.012, capsuleS(vec3(0, 0.68, -0.006), vec3(0, 0.8, 0.01), 0.021))
  for i in 0..<3 {
    let fi = Float(i)
    m.add(AH.head, AMat.gold, 0.003, torusS(vec3(0, 0.71 + 0.026 * fi, -0.003 + 0.0035 * fi), 0.0225, 0.0045))
  }
  let fc = vec3(0, 0.85, 0.028)
  m.add(AH.head, AMat.skin, 0, ellS(fc, vec3(0.05, 0.066, 0.054)))
  m.add(AH.head, AMat.skin, 0.03, ellS(fc + vec3(0, -0.045, 0.02), vec3(0.03, 0.03, 0.032)))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let R = rotYm(0.4 * x) * rotZm(0.45 * x)
    let e = fc + vec3(0.026 * x, 0.006, 0.045)
    m.sub(-1, 0.005, ellS(e + vec3(0, 0, 0.004), vec3(0.027, 0.016, 0.012), R))
    m.add(AH.head, AMat.eye, 0.002, ellS(e + vec3(0, 0, 0.0015), vec3(0.0255, 0.0145, 0.0105), R))
  }
  m.add(AH.head, AMat.dark, 0.003, ellS(fc + vec3(0, -0.048, 0.042), vec3(0.012, 0.006, 0.012)))
  m.paint(AMat.pink, ellS(fc + vec3(0, -0.045, 0.064), vec3(0.014, 0.006, 0.01)), AMat.skin)
  // the inner jaw, hidden in her mouth until it shoots out
  m.addR(AH.jaw, AMat.pink, 0, capsuleS(j.jaw + vec3(0, -0.003, -0.015), j.jaw + vec3(0, -0.003, 0.012), 0.008))
  m.addR(AH.jaw, AMat.teeth, 0.002, ellS(j.jaw + vec3(0, -0.003, 0.017), vec3(0.008, 0.007, 0.005)))
  // the crest: a carapace crown sweeping up and back, edged with gold spikes and gems
  for k in -1...1 {
    let side = Float(k)
    let c0 = fc + vec3(0.03 * side, 0.08 - 0.02 * abs(side), -0.07)
    let Rt = (rotZm(-0.45 * side) * rotXm(-0.85 + 0.12 * abs(side))).transpose
    let sz: Float = side == 0 ? 1 : 0.72
    let rad = vec3(0.022, 0.055, 0.19) * sz
    m.add(AH.head, AMat.shell, 0.02, fnS({ p in SDF.ellipsoid(Rt * (p - c0), rad) },
                                          fc + vec3(0.04 * side, 0.15, -0.13), 0.21))
  }
  for i in 0..<6 {
    let t = Float(i) / 5
    let base = fc + vec3(0, 0.1 + 0.15 * t, -0.03 - 0.125 * t)
    let tip = base + vec3(0, 0.05 - 0.015 * t, 0.016 - 0.03 * t)
    m.add(AH.head, AMat.gold, 0.003, coneS(base, tip, 0.011, 0.0025))
    m.add(AH.head, AMat.gem, 0.002, sphereS(tip, 0.0055))
  }
  // the tail: carapace segments ending in a spade
  let tp = [j.tail1, j.tail2, j.tail3, j.tail3 + vec3(0, -0.02, -0.1)]
  for s in 0..<3 {
    let fs = Float(s)
    m.add(AH.tail1 + s, AMat.shell, 0.01, coneS(tp[s], tp[s + 1], 0.03 - 0.007 * fs, 0.024 - 0.007 * fs))
    m.add(AH.tail1 + s, AMat.dark, 0.002,
          torusS(gmix(tp[s], tp[s + 1], 0.5), 0.027 - 0.007 * fs, 0.0025, frameY(tp[s + 1] - tp[s])))
  }
  let spade = tp[3] + vec3(0, 0.0, -0.025)
  m.add(AH.tail3, AMat.shell, 0.006, fnS({ p in
    let q = p - spade
    return max(abs(q.x) * 1.3 + abs(q.z + 0.012) - 0.04, abs(q.y) - 0.006)
  }, tp[3] + vec3(0, 0, -0.025), 0.05))
  return m
}

// ---------------------------------------------------------------------------
// King: the overlord on his hover throne
// ---------------------------------------------------------------------------
private func kingModel() -> Model {
  let j = AlienJ.king()
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.king))
  let b = j.base
  // --- the hover throne: an egg chair on a hovering drive with fans and lights
  let shell: SDFFn = { p in
    let q = p - (b + vec3(0, 0.44, -0.04))
    let outer = SDF.ellipsoid(q, vec3(0.25, 0.46, 0.23))
    let inner = SDF.ellipsoid(q - vec3(0, 0.05, 0.075), vec3(0.205, 0.42, 0.2))
    let open = -(q.z - 0.06 + 0.18 * max(0, q.y + 0.1))  // cut away the front, more at the top
    return max(max(outer, -inner), -open)
  }
  m.add(AH.base, AMat.hull, 0, fnS(shell, b + vec3(0, 0.44, -0.06), 0.48))
  m.add(AH.base, AMat.robe, 0.006, fnS({ p in  // the cushion
    let q = p - (b + vec3(0, 0.24, 0.03))
    return SDF.roundBox(q, vec3(0.16, 0.03, 0.13), 0.03)
  }, b + vec3(0, 0.24, 0.03), 0.22))
  for sd in stride(from: -1, through: 1, by: 2) {  // armrests
    let x = Float(sd)
    m.add(AH.base, AMat.hull, 0.012,
          capsuleS(b + vec3(0.2 * x, 0.32, -0.08), b + vec3(0.2 * x, 0.32, 0.12), 0.032))
    m.add(AH.base, AMat.chrome, 0.004,
          torusS(b + vec3(0.2 * x, 0.32, 0.12), 0.026, 0.006, frameY(vec3(0, 0, 1))))
  }
  // the control panel on the right armrest: lights and a big red button
  m.add(AH.base, AMat.chrome, 0.004, boxS(b + vec3(-0.2, 0.355, 0.06), vec3(0.03, 0.008, 0.045), 0.006))
  m.add(AH.base, AMat.gem, 0.003, ellS(b + vec3(-0.2, 0.366, 0.085), vec3(0.014, 0.007, 0.014)))
  for i in 0..<3 {
    m.add(AH.base, AMat.light, 0.002, sphereS(b + vec3(-0.212 + 0.012 * Float(i), 0.364, 0.045), 0.0045))
  }
  // the drive underneath: a chrome pod with a glowing ring and four fan jets
  m.add(AH.base, AMat.chrome, 0.03, ellS(b + vec3(0, 0.07, -0.01), vec3(0.17, 0.055, 0.17)))
  m.add(AH.base, AMat.energy, 0.004, torusS(b + vec3(0, 0.03, -0.01), 0.12, 0.01))
  for i in 0..<4 {
    let a: Float = 6.2831853 * (Float(i) + 0.5) / 4
    let d = vec3(sin(a), 0, cos(a))
    let f = b + d * 0.17 + vec3(0, 0.05, -0.01)
    m.add(AH.base, AMat.hull, 0.006, cylS(f - vec3(0, 0.022, 0), f + vec3(0, 0.022, 0), 0.04))
    m.add(AH.base, AMat.energy, 0.002, cylS(f - vec3(0, 0.026, 0), f - vec3(0, 0.018, 0), 0.03))
  }
  // the tall back: a crest rising out of the shell, rimmed in gold round a glowing crystal
  let fin: SDFFn = { p in
    let q = p - (b + vec3(0, 0.9, -0.2))
    let f = max(abs(q.z) - 0.014, SDF.ellipsoid(vec3(q.x, q.y, 0), vec3(0.12, 0.2, 1.0)))
    return max(f, -q.y - 0.18)
  }
  m.add(AH.base, AMat.hull, 0.01, fnS(fin, b + vec3(0, 0.9, -0.2), 0.22))
  m.add(AH.base, AMat.gold, 0.004, fnS({ p in  // a gold rim round the crest
    let q = p - (b + vec3(0, 0.9, -0.2))
    let e = SDF.ellipsoid(vec3(q.x, q.y, 0), vec3(0.12, 0.2, 1.0))
    return max(max(abs(e) - 0.007, abs(q.z) - 0.018), -q.y - 0.12)
  }, b + vec3(0, 0.9, -0.2), 0.22))
  m.add(AH.base, AMat.gem, 0.004, ellS(b + vec3(0, 0.97, -0.18), vec3(0.028, 0.05, 0.018)))
  // --- the overlord: short legs, a big belly, a cape with a towering collar
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let th = sd > 0 ? AH.lThigh : AH.rThigh, sh = sd > 0 ? AH.lShin : AH.rShin
    let hip = vec3(j.hipL.x * x, j.hipL.y, j.hipL.z), knee = vec3(j.kneeL.x * x, j.kneeL.y, j.kneeL.z)
    let ankle = vec3(j.ankleL.x * x, j.ankleL.y, j.ankleL.z)
    m.add(th, AMat.suit, 0, coneS(hip, knee, 0.042, 0.036))
    m.add(sh, AMat.suit, 0.012, coneS(knee, ankle + vec3(0, 0.02, 0), 0.034, 0.028))
    m.add(sh, AMat.gold, 0.01, boxS(ankle + vec3(0, -0.005, 0.02), vec3(0.026, 0.02, 0.042), 0.018))
  }
  m.add(AH.pelvis, AMat.robe, 0, ellS(j.pelvis + vec3(0, 0.0, 0.03), vec3(0.12, 0.07, 0.1)))
  m.add(AH.chest, AMat.robe, 0.05, ellS(j.chest + vec3(0, 0.06, 0.045), vec3(0.135, 0.12, 0.125)))  // the belly
  m.add(AH.chest, AMat.robe, 0.04, ellS(j.chest + vec3(0, 0.15, -0.01), vec3(0.13, 0.06, 0.08)))
  m.paint(AMat.gold, boxS(j.chest + vec3(0, 0.08, 0.17), vec3(0.016, 0.14, 0.06), 0), AMat.robe)  // a gold placket
  m.add(AH.pelvis, AMat.gold, 0.006,
        torusS(j.pelvis + vec3(0, 0.05, 0.04), 0.118, 0.009, frameY(vec3(0, 1, 0.25))))
  m.add(AH.pelvis, AMat.gem, 0.003, ellS(j.pelvis + vec3(0, 0.03, 0.158), vec3(0.02, 0.017, 0.008)))
  for i in 0..<3 {  // medals
    let fi = Float(i)
    let a = j.chest + vec3(0.06 + 0.026 * fi, 0.13 - 0.006 * fi, 0.105 - 0.012 * fi)
    let e = j.chest + vec3(0.06 + 0.026 * fi, 0.13 - 0.006 * fi, 0.115 - 0.012 * fi)
    m.add(AH.chest, i == 1 ? AMat.gem : AMat.gold, 0.002, cylS(a, e, 0.011))
  }
  // cape and collar
  let chest = j.chest, head = j.head
  m.add(AH.chest, AMat.robe, 0.02, fnS({ p in
    let q = p - (chest + vec3(0, 0.1, -0.03))
    let e = SDF.ellipsoid(q, vec3(0.155, 0.12, 0.115))
    return max(max(e, -(e + 0.016)), q.z - 0.02 + 0.3 * max(0, -q.y))
  }, j.chest + vec3(0, 0.1, -0.06), 0.18))
  m.add(AH.chest, AMat.gold, 0.006, fnS({ p in
    let q = p - (head + vec3(0, -0.02, -0.04))
    let r = (q.x * q.x + q.z * q.z).squareRoot()
    let shell = abs(r - (0.1 + 0.5 * max(0, q.y))) - 0.006
    return max(max(shell, max(-q.y, q.y - 0.17)), q.z - 0.01)
  }, j.head + vec3(0, 0.07, -0.08), 0.2))
  for sd in stride(from: -1, through: 1, by: 2) {
    let L = sd > 0
    let sh = L ? j.shL : j.shR, el = L ? j.elL : j.elR, wr = L ? j.wrL : j.wrR
    let fore = L ? AH.lFore : AH.rFore
    m.add(L ? AH.lArm : AH.rArm, AMat.robe, 0.016, coneS(sh, el, 0.034, 0.028))
    m.add(fore, AMat.robe, 0.01, coneS(el, wr, 0.027, 0.022))
    m.add(fore, AMat.gold, 0.004, coneS(gmix(el, wr, 0.75), wr, 0.026, 0.025))
    addHand(m, fore, wr, L ? vec3(0, -0.4, 1) : vec3(0, 0, 1), vec3(L ? -1 : 1, 0, 0), 1.6, L ? 0.6 : 0, AMat.skin)
  }
  // the disintegrator sceptre: a gold rod with a crystal, held up like a wand
  do {
    let g = j.gripR
    let ax = normalize(vec3(0, 0.55, 1))
    m.addR(AH.itemR, AMat.gold, 0, cylS(g - ax * 0.06, g + ax * 0.16, 0.009))
    m.addR(AH.itemR, AMat.gold, 0.004, torusS(g + ax * 0.16, 0.018, 0.005, frameY(ax)))
    m.addR(AH.itemR, AMat.gem, 0.003, ellS(g + ax * 0.185, vec3(0.016, 0.016, 0.016)))
    m.addR(AH.itemR, AMat.gold, 0.003, sphereS(g - ax * 0.065, 0.013))
  }
  // a huge head: double chin, veined dome, little eyes of disdain, a monocle and a tiny crown
  let hc = j.head + vec3(0, 0.12, 0.02)
  m.add(AH.head, AMat.skin, 0.012, capsuleS(j.head - vec3(0, 0.02, 0), j.head + vec3(0, 0.05, 0.02), 0.05))
  m.add(AH.head, AMat.skin, 0, ellS(hc, vec3(0.125, 0.118, 0.115)))
  m.add(AH.head, AMat.skin, 0.04, ellS(hc + vec3(0, -0.09, 0.045), vec3(0.075, 0.05, 0.065)))
  m.add(AH.jaw, AMat.skin, 0.03, ellS(hc + vec3(0, -0.135, 0.05), vec3(0.06, 0.032, 0.05)))  // double chin
  for i in 0..<5 {  // veins on the dome
    let a: Float = -0.8 + 0.4 * Float(i)
    let s0 = hc + vec3(sin(a) * 0.07, 0.07, cos(a) * 0.07)
    let s1 = hc + vec3(sin(a + 0.25) * 0.105, 0.0, cos(a + 0.25) * 0.08)
    m.add(AH.head, AMat.skin2, 0.006, capsuleS(s0, s1, 0.0045))
  }
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    let R = rotYm(0.35 * x) * rotZm(0.3 * x)
    let e = hc + vec3(0.048 * x, -0.033, 0.097)
    m.sub(-1, 0.008, ellS(e + vec3(0, 0, 0.005), vec3(0.036, 0.021, 0.016), R))
    m.add(AH.head, AMat.eye, 0.003, ellS(e + vec3(0, 0, 0.002), vec3(0.034, 0.019, 0.014), R))
    m.add(AH.head, AMat.skin, 0.006, ellS(e + vec3(0, 0.016, 0.006), vec3(0.036, 0.008, 0.014), R))  // heavy lids
  }
  m.add(AH.head, AMat.chrome, 0.002,
        torusS(hc + vec3(-0.046, -0.035, 0.114), 0.027, 0.0032, frameY(vec3(-0.3, 0, 1))))
  m.add(AH.head, AMat.gold, 0.002, capsuleS(hc + vec3(-0.07, -0.05, 0.11), hc + vec3(-0.085, -0.12, 0.09), 0.0018))
  m.paint(AMat.dark, ellS(hc + vec3(0, -0.098, 0.102), vec3(0.016, 0.003, 0.012), rotZm(0.15)), AMat.skin)  // smirk
  do {  // the tiny crown
    let cc = hc + vec3(0, 0.112, 0.0)
    m.add(AH.head, AMat.gold, 0.003, fnS({ p in
      let q = p - cc
      let r = (q.x * q.x + q.z * q.z).squareRoot()
      return max(abs(r - 0.032) - 0.004, abs(q.y) - 0.01)
    }, cc, 0.04))
    for i in 0..<5 {
      let a: Float = 6.2831853 * Float(i) / 5
      let d = vec3(sin(a), 0, cos(a))
      m.add(AH.head, AMat.gold, 0.002,
            coneS(cc + d * 0.032 + vec3(0, 0.008, 0), cc + d * 0.03 + vec3(0, 0.03, 0), 0.006, 0.002))
      m.add(AH.head, AMat.gem, 0.002, sphereS(cc + d * 0.03 + vec3(0, 0.032, 0), 0.004))
    }
  }
  return m
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------
private func cowModel() -> Model {
  let m = Model()
  m.setSkeleton(Alien.skeleton(AAsset.cow))
  m.add(CW.body, AMat.hide, 0, ellS(vec3(0, 0.235, 0), vec3(0.095, 0.085, 0.165)))
  m.add(CW.body, AMat.hide, 0.05, ellS(vec3(0, 0.255, 0.1), vec3(0.085, 0.075, 0.06)))
  m.add(CW.body, AMat.pink, 0.02, ellS(vec3(0, 0.155, -0.065), vec3(0.04, 0.03, 0.04)))  // udder
  for i in 0..<4 {
    let x: Float = i % 2 != 0 ? 0.014 : -0.014
    let z: Float = -0.05 - Float(i / 2) * 0.026
    m.add(CW.body, AMat.pink, 0.004, capsuleS(vec3(x, 0.13, z), vec3(x, 0.11, z), 0.005))
  }
  // head
  let head = CowJ.head
  m.add(CW.head, AMat.hide, 0.02, capsuleS(head, head + vec3(0, 0.0, 0.04), 0.045))
  m.add(CW.head, AMat.hide, 0.015, ellS(vec3(0, 0.29, 0.22), vec3(0.042, 0.05, 0.065), rotXm(0.5)))
  m.add(CW.head, AMat.pink, 0.015, ellS(vec3(0, 0.255, 0.27), vec3(0.038, 0.028, 0.03)))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    m.sub(AMat.pink, 0.002, sphereS(vec3(0.013 * x, 0.258, 0.298), 0.006))
    m.add(CW.head, AMat.eye, 0.002, sphereS(vec3(0.034 * x, 0.305, 0.235), 0.0095))
    m.add(CW.head, AMat.horn, 0.004,
          coneS(vec3(0.03 * x, 0.335, 0.19), vec3(0.06 * x, 0.355, 0.2), 0.009, 0.003))
    m.add(CW.head, AMat.hide, 0.004,
          ellS(vec3(0.055 * x, 0.315, 0.18), vec3(0.028, 0.008, 0.016), rotZm(-0.4 * x)))
  }
  // a cowbell
  m.add(CW.head, AMat.rubber, 0.004, torusS(vec3(0, 0.25, 0.145), 0.04, 0.005, frameY(vec3(0, 0.6, 1))))
  m.add(CW.head, AMat.gold, 0.004, coneS(vec3(0, 0.205, 0.17), vec3(0, 0.175, 0.175), 0.012, 0.018))
  // legs and hooves
  let roots = [CowJ.fl, mirrorXv(CowJ.fl), CowJ.hl, mirrorXv(CowJ.hl)]
  for i in 0..<4 {
    let r = roots[i]
    m.add(CW.fl + i, AMat.hide, 0.015, coneS(r, vec3(r.x, 0.04, r.z), 0.026, 0.017))
    m.add(CW.fl + i, AMat.horn, 0.004, cylS(vec3(r.x, 0.0, r.z), vec3(r.x, 0.035, r.z), 0.018))
  }
  let tail = CowJ.tail
  m.add(CW.tail, AMat.hide, 0.006, capsuleS(tail, tail + vec3(0, -0.12, -0.03), 0.006))
  m.add(CW.tail, AMat.hide, 0.004, ellS(tail + vec3(0, -0.14, -0.035), vec3(0.012, 0.025, 0.012)))
  return m
}

/// Sculpted small; drawn about five times larger.
private func mothershipModel() -> Model {
  let m = Model()
  var hull = Profile()
  hull.build([vec2(0, -0.07), vec2(0.15, -0.065), vec2(0.3, -0.05), vec2(0.42, -0.03), vec2(0.49, -0.01),
              vec2(0.5, 0.0), vec2(0.49, 0.012), vec2(0.42, 0.03), vec2(0.3, 0.06), vec2(0.22, 0.075),
              vec2(0.18, 0.11), vec2(0.1, 0.135), vec2(0, 0.14)], 3)
  let c = vec3(0, 0.1, 0)
  m.add(0, AMat.hull, 0, latheS(hull, c))
  m.paint(AMat.chrome, boxS(c - vec3(0, 0.3, 0), vec3(0.6, 0.3 - 0.004, 0.6), 0), AMat.hull)
  for ring in 0..<3 {
    let fr = Float(ring)
    let R: Float = 0.2 + 0.13 * fr
    m.add(0, AMat.chrome, 0.004, torusS(c + vec3(0, 0.06 - 0.026 * fr, 0), R, 0.007))
    let n = 12 + 8 * ring
    for i in 0..<n {
      let a: Float = 6.2831853 * (Float(i) + 0.5 * fr) / Float(n)
      m.add(0, AMat.light, 0.002, sphereS(c + vec3(sin(a) * R, -0.045 + 0.012 * fr, cos(a) * R), 0.012))
    }
  }
  for i in 0..<6 {  // docking bays
    let a: Float = 6.2831853 * Float(i) / 6
    let d = vec3(sin(a), 0, cos(a))
    m.add(0, AMat.dark, 0.004, boxS(c + d * 0.3 - vec3(0, 0.05, 0), vec3(0.04, 0.012, 0.03), 0.01,
                                    mat3(cross(vec3(0, 1, 0), d), vec3(0, 1, 0), d)))
  }
  m.add(0, AMat.energy, 0.006, ellS(c - vec3(0, 0.07, 0), vec3(0.11, 0.02, 0.11)))
  m.add(0, AMat.chrome, 0.004, torusS(c - vec3(0, 0.068, 0), 0.12, 0.01))
  m.add(0, AMat.light, 0.004, torusS(c + vec3(0, 0.0, 0), 0.5, 0.008))
  m.add(0, AMat.gem, 0.004, sphereS(c + vec3(0, 0.15, 0), 0.025))
  return m
}

private func critterModel() -> Model {
  let m = Model()
  m.add(0, AMat.shell, 0, ellS(vec3(0, 0.035, -0.005), vec3(0.032, 0.026, 0.042)))
  m.add(0, AMat.skin, 0.012, ellS(vec3(0, 0.035, 0.036), vec3(0.024, 0.02, 0.02)))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    m.add(0, AMat.skin, 0.004, capsuleS(vec3(0.01 * x, 0.05, 0.035), vec3(0.016 * x, 0.072, 0.04), 0.0035))
    m.add(0, AMat.eye, 0.002, sphereS(vec3(0.016 * x, 0.076, 0.042), 0.0085))  // eyes on stalks
    for k in 0..<3 {
      let z: Float = 0.02 - 0.02 * Float(k)
      m.add(0, AMat.shell, 0.003, capsuleS(vec3(0.025 * x, 0.03, z), vec3(0.045 * x, 0.02, z + 0.006), 0.0035))
      m.add(0, AMat.shell, 0.003, capsuleS(vec3(0.045 * x, 0.02, z + 0.006), vec3(0.052 * x, 0.0, z + 0.01), 0.003))
    }
  }
  m.add(0, AMat.dark, 0.003, ellS(vec3(0, 0.03, 0.054), vec3(0.012, 0.005, 0.006)))
  for sd in stride(from: -1, through: 1, by: 2) {
    let x = Float(sd)
    m.add(0, AMat.teeth, 0.001, coneS(vec3(0.006 * x, 0.027, 0.056), vec3(0.006 * x, 0.02, 0.057), 0.002, 0.0008))
  }
  return m
}

private func eggModel() -> Model {
  let m = Model()
  m.add(0, AMat.egg, 0, fnS({ p in
    let q = p - vec3(0, 0.1, 0)
    let e = SDF.ellipsoid(q, vec3(0.075, 0.1, 0.075) * (1 + 0.15 * clamp(-q.y / 0.1, 0, 1)))
    // four lobes at the top, ready to open
    let a = atan2(q.x, q.z)
    let seam = abs(sin(a * 2)) * 0.04
    return e + 0.006 * clamp((q.y - 0.04) / 0.05, 0, 1) * (1 - seam / 0.04)
  }, vec3(0, 0.1, 0), 0.13))
  m.add(0, AMat.egg, 0.02, ellS(vec3(0, 0.012, 0), vec3(0.06, 0.014, 0.06)))
  return m
}

private func gooModel() -> Model {
  let m = Model()
  m.add(0, AMat.slime, 0, sphereS(vec3(0, 0.05, 0), 0.045))
  m.add(0, AMat.slime, 0.02, sphereS(vec3(0.03, 0.075, -0.01), 0.022))
  m.add(0, AMat.slime, 0.02, sphereS(vec3(-0.025, 0.03, 0.03), 0.024))
  m.add(0, AMat.slime, 0.015, sphereS(vec3(0.0, 0.095, 0.015), 0.014))
  return m
}

// ---------------------------------------------------------------------------
extension Alien {
  /// quality: 0 = fast, 1 = normal, 2 = ultra
  static func buildLibrary(_ lib: AlienLibrary, quality qIn: Int, progress prog: BuildProgress?) {
    let quality = max(0, min(2, qIn))
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
    do {
      let m = pawnModel()
      buildAsset(&lib.asset[AAsset.pawn], m, vec3(-0.13, -0.004, -0.08) * PS, vec3(0.13, 0.67, 0.16) * PS, quality,
                 10, 7321, pieceHeight(AAsset.pawn), pieceRadius(AAsset.pawn))
      tick("Cloning little green men")
    }
    do {
      let m = knightModel()
      buildAsset(&lib.asset[AAsset.knight], m, vec3(-0.34, H0 - 0.2, -0.34) * US, vec3(0.34, H0 + 0.2, 0.34) * US,
                 quality, 14, 7421, pieceHeight(AAsset.knight), pieceRadius(AAsset.knight))
      let g = canopyModel()
      buildAsset(&lib.asset[AAsset.canopy], g, vec3(-0.145, H0 + 0.05, -0.145) * US,
                 vec3(0.145, H0 + 0.21, 0.145) * US, quality, 0, 0, 0.2, 0.12)
      tick("Polishing flying saucers")
    }
    do {
      let m = bishopModel()
      buildAsset(&lib.asset[AAsset.bishop], m, vec3(-0.26, 0.13, -0.26), vec3(0.26, 0.95, 0.26), quality, 12, 7521,
                 pieceHeight(AAsset.bishop), pieceRadius(AAsset.bishop))
      let g = jarModel()
      buildAsset(&lib.asset[AAsset.jar], g, vec3(-0.215, 0.57, -0.225), vec3(0.215, 0.9, 0.205), quality, 0, 0, 0.3,
                 0.2)
      tick("Growing big brains")
    }
    do {
      let m = rookModel()
      buildAsset(&lib.asset[AAsset.rook], m, vec3(-0.4, -0.005, -0.4), vec3(0.4, 1.1, 0.4), quality, 14, 7621,
                 pieceHeight(AAsset.rook), pieceRadius(AAsset.rook))
      tick("Assembling tripods")
    }
    do {
      let m = queenModel()
      buildAsset(&lib.asset[AAsset.queen], m, vec3(-0.22, -0.005, -0.53), vec3(0.22, 1.2, 0.22), quality, 13, 7721,
                 pieceHeight(AAsset.queen), pieceRadius(AAsset.queen))
      tick("Crowning the queens")
    }
    do {
      let m = kingModel()
      buildAsset(&lib.asset[AAsset.king], m, vec3(-0.27, 0.08, -0.33), vec3(0.27, 1.2, 0.34), quality, 14, 7821,
                 pieceHeight(AAsset.king), pieceRadius(AAsset.king))
      tick("Enthroning the overlords")
    }
    do {
      let m = cowModel()
      buildAsset(&lib.asset[AAsset.cow], m, vec3(-0.1, -0.005, -0.27), vec3(0.1, 0.38, 0.33), quality, 0, 0, 0.38,
                 0.2)
      let e = eggModel()
      buildAsset(&lib.asset[AAsset.egg], e, vec3(-0.1, -0.005, -0.1), vec3(0.1, 0.21, 0.1), quality, 6, 7921, 0.2,
                 0.09)
      let g = gooModel()
      buildAsset(&lib.asset[AAsset.goo], g, vec3(-0.07, -0.005, -0.07), vec3(0.07, 0.12, 0.07), quality, 0, 0, 0.1,
                 0.05)
      let c = critterModel()
      buildAsset(&lib.asset[AAsset.critter], c, vec3(-0.07, -0.005, -0.06), vec3(0.07, 0.09, 0.07), quality, 0, 0,
                 0.08, 0.05)
      tick("Stealing cows")
    }
    do {
      let m = mothershipModel()
      buildAsset(&lib.asset[AAsset.mothership], m, vec3(-0.52, 0.0, -0.52), vec3(0.52, 0.26, 0.52), quality, 0, 0,
                 0.26, 0.5)
      tick("Summoning the mothership")
    }
    lib.built = true
    prog?.finished = true
  }
}
