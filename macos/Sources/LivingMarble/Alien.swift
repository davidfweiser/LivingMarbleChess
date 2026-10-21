// The alien army: a third set of pieces. Little green men with ray guns, a
// flying saucer, a big-brained psychic in his jar, a tripod war machine, a
// crested alien queen and a pudgy overlord on his hover throne. Built like
// the medieval army (painted, skinned SDF figures) and posed with the same
// animation controls (MControls, MPose, MSkeleton).
// (Sculpting: AlienMesh.swift. Posing: AlienPose.swift.)
import Foundation
import simd

/// Assets. The first six are the chessmen in Kind order.
enum AAsset {
  static let pawn = 0        // little green man: big head, antennae, jumpsuit, ray gun
  static let knight = 1      // flying saucer with a little pilot (canopy: AAsset.canopy)
  static let bishop = 2      // big-brained psychic floating on a disc (his brain jar: AAsset.jar)
  static let rook = 3        // tripod war machine with a heat-ray eye and two tentacles
  static let queen = 4       // tall crested alien queen with a tail and a sceptre
  static let king = 5        // pudgy overlord with a cape on his hover throne
  static let canopy = 6      // the saucer's glass canopy (glass, skinned like the saucer)
  static let jar = 7         // the psychic's glass brain jar (glass, skinned like the psychic)
  static let cow = 8         // a cow (for dropping on people)
  static let mothership = 9  // the mothership
  static let critter = 10    // one of the queen's hatchlings
  static let egg = 11        // the queen's egg
  static let goo = 12        // a blob of slime (thrown by the little green men)
  static let count = 13
}

/// Per-vertex materials (integer part of Vertex.mat). Colours depend on the army:
/// the white army is green-skinned in silver and pearl with cyan lights, the black
/// army violet-skinned in black and gunmetal with magenta lights.
enum AMat {
  static let skin = 0     // wet, slightly translucent alien skin
  static let skin2 = 1    // paler markings: faces, bellies, spots
  static let eye = 2      // big glossy black eyes
  static let suit = 3     // jumpsuits
  static let chrome = 4   // polished metal
  static let hull = 5     // painted hull plating with panel lines
  static let light = 6    // running lights (glowing, chasing)
  static let brain = 7    // a big pink brain
  static let gold = 8     // regalia
  static let robe = 9     // the overlord's cape, the queen's gown
  static let gem = 10     // glowing crystals
  static let slime = 11   // green goo
  static let rubber = 12  // hoses, seals, tyres
  static let hide = 13    // cowhide
  static let pink = 14    // tongues, gums, cow noses and udders
  static let teeth = 15
  static let dark = 16    // mouths, vents, the dark inside of things
  static let shell = 17   // the queen's glossy carapace
  static let energy = 18  // plasma coils and emitters (glowing)
  static let horn = 19    // horns, hooves, claws
  static let egg = 20     // leathery egg
  static let count = 21
}

/// Humanoid aliens (pawn, bishop, queen, king) share one bone layout. The base is
/// what they stand on: the ground, the psychic's floating disc, the overlord's throne.
/// "Left" is the figure's own left (+x when it faces +z).
enum AH {
  static let base = 0
  static let pelvis = 1, chest = 2, head = 3
  static let lThigh = 4, lShin = 5, rThigh = 6, rShin = 7
  static let lArm = 8, lFore = 9, rArm = 10, rFore = 11
  static let itemR = 12, itemL = 13
  static let antL = 14, antR = 15  // antennae
  static let jaw = 16              // tongue (pawn), inner jaw (queen)
  static let tail1 = 17, tail2 = 18, tail3 = 19
  static let count = 20
}
/// The flying saucer.
enum UB {
  static let body = 0
  static let rim = 1    // the light ring (spins)
  static let pilot = 2, head = 3, lArm = 4, rArm = 5
  static let hatch = 6  // the belly emitter (opens for the tractor beam)
  static let leg1 = 7, leg2 = 8, leg3 = 9
  static let antL = 10, antR = 11
  static let count = 12
}
/// The tripod.
enum TB {
  static let body = 0
  static let hood = 1  // the heat-ray eye (turns, opens)
  static let l1u = 2, l1l = 3, l2u = 4, l2l = 5, l3u = 6, l3l = 7
  static let tl1 = 8, tl2 = 9, tl3 = 10, tr1 = 11, tr2 = 12, tr3 = 13  // tentacles
  static let count = 14
}
/// The cow.
enum CW {
  static let body = 0, head = 1, fl = 2, fr = 3, hl = 4, hr = 5, tail = 6
  static let count = 7
}

final class AlienLibrary: @unchecked Sendable {  // filled on a loader thread, read after it finishes
  var asset = [PieceAssets](repeating: PieceAssets(), count: AAsset.count)
  var built = false
}

// ---------------------------------------------------------------------------
// Joint layouts (the skeletons and the sculpting share them)
// ---------------------------------------------------------------------------
/// Humanoid aliens (AH bones).
struct AlienJ {
  var base = vec3(0), pelvis = vec3(0), chest = vec3(0), head = vec3(0)
  var hipL = vec3(0), kneeL = vec3(0), ankleL = vec3(0)
  var shL = vec3(0), elL = vec3(0), wrL = vec3(0), shR = vec3(0), elR = vec3(0), wrR = vec3(0)
  var gripR = vec3(0), gripL = vec3(0)
  var antL = vec3(0), jaw = vec3(0), tail1 = vec3(0), tail2 = vec3(0), tail3 = vec3(0)
  var tongue = false  // tail1/tail2 are the tongue (pawn) instead of a tail

  /// The little green man: sculpted 0.66 tall (to the tips of his antennae), built Alien.PS times larger.
  static func pawn() -> AlienJ {
    var j = AlienJ()
    j.pelvis = vec3(0, 0.2, 0)
    j.chest = vec3(0, 0.255, 0)
    j.head = vec3(0, 0.375, 0.004)
    j.hipL = vec3(0.034, 0.195, 0)
    j.kneeL = vec3(0.036, 0.108, 0.006)
    j.ankleL = vec3(0.037, 0.03, -0.002)
    j.shL = vec3(0.07, 0.345, -0.004)
    j.elL = vec3(0.086, 0.268, -0.012)
    j.wrL = vec3(0.082, 0.205, 0.028)
    j.shR = vec3(-0.07, 0.345, -0.004)
    j.elR = vec3(-0.086, 0.27, -0.01)
    j.wrR = vec3(-0.082, 0.22, 0.052)
    j.gripR = vec3(-0.082, 0.216, 0.07)
    j.gripL = vec3(0.082, 0.198, 0.04)
    j.antL = vec3(0.03, 0.55, -0.012)
    j.jaw = vec3(0, 0.425, 0.04)
    j.tail1 = vec3(0, 0.416, 0.046)  // the tongue
    j.tail2 = vec3(0, 0.416, 0.066)  // its tip
    j.tongue = true
    return j
  }

  /// The psychic sits cross-legged on his floating disc.
  static func bishop() -> AlienJ {
    var j = AlienJ()
    j.base = vec3(0, 0.16, 0)
    j.pelvis = vec3(0, 0.25, -0.01)
    j.chest = vec3(0, 0.29, -0.01)
    j.head = vec3(0, 0.44, 0.0)
    j.hipL = vec3(0.04, 0.245, 0)
    j.kneeL = vec3(0.12, 0.215, 0.08)
    j.ankleL = vec3(-0.03, 0.205, 0.11)
    j.shL = vec3(0.064, 0.405, -0.012)
    j.elL = vec3(0.095, 0.33, 0.0)
    j.wrL = vec3(0.09, 0.27, 0.075)
    j.shR = vec3(-0.064, 0.405, -0.012)
    j.elR = vec3(-0.095, 0.33, 0.0)
    j.wrR = vec3(-0.09, 0.27, 0.075)
    j.gripR = vec3(-0.088, 0.262, 0.1)
    j.gripL = vec3(0.088, 0.262, 0.1)
    j.antL = vec3(0.03, 0.7, 0)
    j.jaw = vec3(0, 0.485, 0.04)
    j.tail1 = vec3(0, 0.24, -0.06)
    j.tail2 = vec3(0, 0.2, -0.1)
    j.tail3 = vec3(0, 0.18, -0.14)
    let S = Alien.BS
    j.base *= S; j.pelvis *= S; j.chest *= S; j.head *= S; j.hipL *= S; j.kneeL *= S; j.ankleL *= S
    j.shL *= S; j.elL *= S; j.wrL *= S; j.shR *= S; j.elR *= S; j.wrR *= S; j.gripR *= S; j.gripL *= S
    j.antL *= S; j.jaw *= S; j.tail1 *= S; j.tail2 *= S; j.tail3 *= S
    return j
  }

  /// The queen floats a little above the board in her gown; her tail trails behind.
  static func queen() -> AlienJ {
    var j = AlienJ()
    j.base = vec3(0, 0, 0)
    j.pelvis = vec3(0, 0.44, -0.01)
    j.chest = vec3(0, 0.52, -0.01)
    j.head = vec3(0, 0.78, 0.0)
    j.hipL = vec3(0.05, 0.42, 0)
    j.kneeL = vec3(0.055, 0.24, 0.01)
    j.ankleL = vec3(0.055, 0.08, 0)
    j.shL = vec3(0.088, 0.715, -0.014)
    j.elL = vec3(0.118, 0.585, -0.022)
    j.wrL = vec3(0.105, 0.475, 0.04)
    j.shR = vec3(-0.088, 0.715, -0.014)
    j.elR = vec3(-0.12, 0.59, -0.02)
    j.wrR = vec3(-0.11, 0.5, 0.07)
    j.gripR = vec3(-0.108, 0.5, 0.092)
    j.gripL = vec3(0.1, 0.465, 0.06)
    j.antL = vec3(0.025, 0.94, -0.06)
    j.jaw = vec3(0, 0.83, 0.03)
    j.tail1 = vec3(0, 0.3, -0.12)
    j.tail2 = vec3(0, 0.14, -0.24)
    j.tail3 = vec3(0, 0.05, -0.34)
    return j
  }

  /// The overlord sits on his hover throne.
  static func king() -> AlienJ {
    var j = AlienJ()
    j.base = vec3(0, 0.12, -0.04)
    j.pelvis = vec3(0, 0.44, -0.05)
    j.chest = vec3(0, 0.49, -0.05)
    j.head = vec3(0, 0.71, -0.02)
    j.hipL = vec3(0.075, 0.43, -0.02)
    j.kneeL = vec3(0.085, 0.425, 0.15)
    j.ankleL = vec3(0.085, 0.27, 0.17)
    j.shL = vec3(0.13, 0.66, -0.05)
    j.elL = vec3(0.185, 0.535, -0.01)
    j.wrL = vec3(0.18, 0.47, 0.11)
    j.shR = vec3(-0.13, 0.66, -0.05)
    j.elR = vec3(-0.185, 0.54, -0.01)
    j.wrR = vec3(-0.165, 0.5, 0.12)
    j.gripR = vec3(-0.16, 0.5, 0.145)
    j.gripL = vec3(0.175, 0.46, 0.14)
    j.antL = vec3(0.03, 0.93, -0.02)
    j.jaw = vec3(0, 0.7, 0.06)
    j.tail1 = vec3(0, 0.3, -0.15)
    j.tail2 = vec3(0, 0.25, -0.2)
    j.tail3 = vec3(0, 0.2, -0.25)
    return j
  }

  func skeleton() -> MSkeleton {
    MSkeleton([
      (-1, base, false),
      (AH.base, pelvis, false),
      (AH.pelvis, chest, false),
      (AH.chest, head, false),
      (AH.pelvis, hipL, false),
      (AH.lThigh, kneeL, false),
      (AH.pelvis, mirrorXv(hipL), false),
      (AH.rThigh, mirrorXv(kneeL), false),
      (AH.chest, shL, false),
      (AH.lArm, elL, false),
      (AH.chest, shR, false),
      (AH.rArm, elR, false),
      (AH.rFore, gripR, true),
      (AH.lFore, gripL, true),
      (AH.head, antL, false),
      (AH.head, mirrorXv(antL), false),
      (AH.head, jaw, false),
      (tongue ? AH.jaw : AH.pelvis, tail1, tongue),
      (tongue ? AH.jaw : AH.tail1, tail2, tongue),
      (AH.tail2, tail3, false),
    ])
  }
}

/// The saucer hovers with its hull's mid-plane at Alien.H0 (sculpted, then built Alien.US times larger).
enum UJ {
  static let H0 = Alien.H0
  static let body = vec3(0, H0, 0), rim = vec3(0, H0, 0), pilot = vec3(0, H0 + 0.07, 0), head = vec3(0, H0 + 0.1, 0)
  static let armL = vec3(0.034, H0 + 0.092, -0.004), hatch = vec3(0, H0 - 0.06, 0)
  static let antL = vec3(0.016, H0 + 0.152, -0.008)
  static let legA: [Float] = [3.14159265, 3.14159265 / 3.0, -3.14159265 / 3.0]  // back, front-left, front-right
  static func legRoot(_ i: Int) -> vec3 { vec3(sin(legA[i]) * 0.17, H0 - 0.035, cos(legA[i]) * 0.17) }
}

/// The tripod: pod at 0.8, three legs (one behind, two in front), two tentacles.
enum TJ {
  static let body = vec3(0, 0.8, 0), hood = vec3(0, 0.82, 0.15)
  static let legA: [Float] = [3.14159265, 1.0471976, -1.0471976]
  // the legs rise from the pod's sides to high knees and come down to splayed feet
  static func hip(_ i: Int) -> vec3 { vec3(sin(legA[i]) * 0.17, 0.79, cos(legA[i]) * 0.17) }
  static func knee(_ i: Int) -> vec3 { vec3(sin(legA[i]) * 0.3, 1.0, cos(legA[i]) * 0.3) }
  static func foot(_ i: Int) -> vec3 { vec3(sin(legA[i]) * 0.34, 0.03, cos(legA[i]) * 0.34) }
  static let tl1 = vec3(0.075, 0.71, 0.1), tl2 = vec3(0.085, 0.6, 0.125), tl3 = vec3(0.085, 0.49, 0.13)
  static let tlTip = vec3(0.075, 0.38, 0.125)
}

/// The cow, standing, facing +z.
enum CowJ {
  static let body = vec3(0, 0.22, 0), head = vec3(0, 0.29, 0.15), fl = vec3(0.06, 0.18, 0.11)
  static let hl = vec3(0.06, 0.18, -0.11), tail = vec3(0, 0.29, -0.17)
}

// ---------------------------------------------------------------------------
// Skeletons and rest-pose queries
// ---------------------------------------------------------------------------
enum Alien {
  /// The little green man is sculpted small and built this much larger.
  static let PS: Float = 1.2
  /// The psychic's joints are this much larger than sculpted.
  static let BS: Float = 1.15
  /// The saucer's hull mid-plane (sculpted) and how much larger it is built.
  static let H0: Float = 0.36
  static let US: Float = 1.1

  private static let pawnSkel = scaledSkeleton(AlienJ.pawn().skeleton(), PS)
  private static let bishopSkel = AlienJ.bishop().skeleton()
  private static let queenSkel = AlienJ.queen().skeleton()
  private static let kingSkel = AlienJ.king().skeleton()
  private static let saucerSkel = scaledSkeleton(MSkeleton([
    (-1, UJ.body, false),
    (UB.body, UJ.rim, true),
    (UB.body, UJ.pilot, false),
    (UB.pilot, UJ.head, false),
    (UB.pilot, UJ.armL, false),
    (UB.pilot, mirrorXv(UJ.armL), false),
    (UB.body, UJ.hatch, true),
    (UB.body, UJ.legRoot(0), true),
    (UB.body, UJ.legRoot(1), true),
    (UB.body, UJ.legRoot(2), true),
    (UB.head, UJ.antL, false),
    (UB.head, mirrorXv(UJ.antL), false),
  ]), US)
  private static let tripodSkel = MSkeleton([
    (-1, TJ.body, false),
    (TB.body, TJ.hood, false),
    (TB.body, TJ.hip(0), false),
    (TB.l1u, TJ.knee(0), false),
    (TB.body, TJ.hip(1), false),
    (TB.l2u, TJ.knee(1), false),
    (TB.body, TJ.hip(2), false),
    (TB.l3u, TJ.knee(2), false),
    (TB.body, TJ.tl1, false),
    (TB.tl1, TJ.tl2, false),
    (TB.tl2, TJ.tl3, false),
    (TB.body, mirrorXv(TJ.tl1), false),
    (TB.tr1, mirrorXv(TJ.tl2), false),
    (TB.tr2, mirrorXv(TJ.tl3), false),
  ])
  private static let cowSkel = MSkeleton([
    (-1, CowJ.body, false),
    (CW.body, CowJ.head, false),
    (CW.body, CowJ.fl, false),
    (CW.body, mirrorXv(CowJ.fl), false),
    (CW.body, CowJ.hl, false),
    (CW.body, mirrorXv(CowJ.hl), false),
    (CW.body, CowJ.tail, false),
  ])
  private static let noSkel = MSkeleton()

  /// count == 0 for rigid assets.
  static func skeleton(_ asset: Int) -> MSkeleton {
    switch asset {
    case AAsset.pawn: return pawnSkel
    case AAsset.knight, AAsset.canopy: return saucerSkel
    case AAsset.bishop, AAsset.jar: return bishopSkel
    case AAsset.rook: return tripodSkel
    case AAsset.queen: return queenSkel
    case AAsset.king: return kingSkel
    case AAsset.cow: return cowSkel
    default: return noSkel
    }
  }

  static func pieceHeight(_ kind: Int) -> Float {
    let H: [Float] = [0.66 * PS, 0.6 * US, 0.95, 1.08, 1.12, 1.16]
    return H[max(0, min(5, kind))]
  }
  static func pieceRadius(_ kind: Int) -> Float {
    let R: [Float] = [0.17 * PS, 0.33 * US, 0.25, 0.36, 0.22, 0.29]
    return R[max(0, min(5, kind))]
  }
  /// AAsset.canopy / AAsset.jar / -1.
  static func glassAsset(_ kind: Int) -> Int {
    kind == AAsset.knight ? AAsset.canopy : (kind == AAsset.bishop ? AAsset.jar : -1)
  }

  // Rest-pose points (model space)
  /// Right hand (ray gun, sceptre).
  static func handR(_ kind: Int) -> vec3 {
    switch kind {
    case AAsset.pawn: return AlienJ.pawn().gripR * PS
    case AAsset.bishop: return AlienJ.bishop().gripR
    case AAsset.queen: return AlienJ.queen().gripR
    case AAsset.king: return AlienJ.king().gripR
    case AAsset.knight: return UJ.hatch * US
    default: return TJ.hood + vec3(0, 0, 0.1)
    }
  }
  /// Where its weapon fires from (ray gun, eye, sceptre, mouth).
  static func muzzle(_ kind: Int) -> vec3 {
    switch kind {
    case AAsset.pawn: return (AlienJ.pawn().gripR + vec3(0, 0.012, 0.072)) * PS
    case AAsset.knight: return (UJ.hatch - vec3(0, 0.012, 0)) * US
    case AAsset.bishop: return vec3(0, 0.6, 0.06) * BS
    case AAsset.rook: return TJ.hood + vec3(0, 0, 0.11)
    case AAsset.queen: return AlienJ.queen().gripR + vec3(0, 0.335, 0)
    default: return AlienJ.king().gripR + normalize(vec3(0, 0.55, 1)) * 0.19
    }
  }
  /// Middle of the head (brain for the psychic).
  static func headPoint(_ kind: Int) -> vec3 {
    switch kind {
    case AAsset.pawn: return vec3(0, 0.48, 0.01) * PS
    case AAsset.knight: return (UJ.head + vec3(0, 0.03, 0)) * US
    case AAsset.bishop: return vec3(0, 0.6, -0.01) * BS
    case AAsset.rook: return TJ.body + vec3(0, 0.05, 0)
    case AAsset.queen: return vec3(0, 0.86, 0.02)
    default: return AlienJ.king().head + vec3(0, 0.12, 0.02)
    }
  }
  /// Distance covered by one walk cycle.
  static func strideCycle(_ kind: Int) -> Float {
    switch kind {
    case AAsset.pawn: return 0.22 * PS
    case AAsset.rook: return 0.5
    case AAsset.queen: return 0.4
    default: return 0.4
    }
  }
  /// Resting float height of the base (0 for walkers).
  static func hoverHeight(_ kind: Int) -> Float {
    switch kind {
    case AAsset.knight: return H0 * US
    case AAsset.bishop: return AlienJ.bishop().base.y
    case AAsset.king: return AlienJ.king().base.y
    case AAsset.queen: return 0.035
    default: return 0
    }
  }
}
