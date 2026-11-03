// The dinosaurs: a fifth set of pieces. Feathered raptors, pteranodons,
// triceratops, a brachiosaurus for a tower, the spinosaurus queen and the
// tyrannosaurus king. Built like the medieval army and the Romans (painted,
// skinned SDF figures) and posed with the same animation controls (MControls,
// MPose, MSkeleton); they fight like animals - claws, horns, jaws and tails -
// and the fallen bleed.
// (Sculpting: DinoMesh.swift. Posing: DinoPose.swift.)
import Foundation
import simd

/// Assets. The first six are the chessmen in Kind order.
enum DAsset {
  static let pawn = 0     // velociraptor: feathered, with sickle claws
  static let knight = 1   // pteranodon: crested, toothless beak, wings of skin
  static let bishop = 2   // triceratops: three horns, a great frill, a parrot's beak
  static let rook = 3     // brachiosaurus: a living tower on four legs
  static let queen = 4    // spinosaurus: a sail on her back, a crocodile's snout, hooked thumb claws
  static let king = 5     // tyrannosaurus rex
  static let pool = 6     // a pool of blood: a flat disc of radius 1 lying on the board
  static let meteor = 7   // a rock from the sky (checkmate): glowing cracks, radius ~1
  static let count = 8
}

/// Per-vertex materials (integer part of Vertex.mat). Colours depend on the army:
/// the white army is sand and cream with rust markings, the black army slate and
/// charcoal with crimson.
enum DMat {
  static let skin = 0       // scaly hide (countershaded, banded)
  static let scute = 1      // bony knobs and scutes along the back
  static let feather = 2    // a raptor's body feathers
  static let quill = 3      // flight feathers on its arms and tail
  static let horn = 4       // horns, the beak (keratin)
  static let claw = 5       // claws (dark keratin)
  static let teeth = 6
  static let eye = 7        // the iris (amber / gold)
  static let dark = 8       // pupils, nostrils, the throat
  static let mouth = 9      // gums, the inside of the mouth, the tongue
  static let frill = 10     // the triceratops' display frill
  static let sail = 11      // the spinosaurus' sail
  static let membrane = 12  // pteranodon wings
  static let crest = 13     // the pteranodon's crest
  static let blood = 14
  static let rock = 15      // the meteor
  static let lava = 16      // ... and its glowing cracks
  static let belly = 17     // pale throat and belly scales
  static let count = 18
}

/// Theropods (raptor, spinosaurus, tyrannosaurus). "Left" is the animal's own left
/// (+x when it faces +z).
enum THB {
  static let root = 0  // the hips
  static let chest = 1, neck = 2, head = 3, jaw = 4
  static let tail1 = 5, tail2 = 6, tail3 = 7
  static let lthigh = 8, lshin = 9, lfoot = 10, rthigh = 11, rshin = 12, rfoot = 13
  static let larm = 14, lfore = 15, rarm = 16, rfore = 17
  static let count = 18
}
/// The pteranodon: each wing is the arm, the forearm, the long hand (metacarpal) it
/// walks on, and its long fourth finger; the trailing edges of the membrane ride on
/// bones of their own (on the forearm and the finger), drawn in against them as the
/// wing folds.
enum PTB {
  static let body = 0, neck = 1, head = 2, jaw = 3
  static let larm = 4, lfore = 5, lmeta = 6, lfinger = 7, rarm = 8, rfore = 9, rmeta = 10, rfinger = 11
  static let lthigh = 12, lshin = 13, rthigh = 14, rshin = 15
  static let ltrail = 16, rtrail = 17, ltrail2 = 18, rtrail2 = 19
  static let count = 20
}
/// Four-legged: the triceratops and the brachiosaurus (legs: upper and lower).
enum QDB {
  static let body = 0, neck1 = 1, neck2 = 2, neck3 = 3, head = 4, jaw = 5
  static let tail1 = 6, tail2 = 7, tail3 = 8
  static let flu = 9, fll = 10, fru = 11, frl = 12, hlu = 13, hll = 14, hru = 15, hrl = 16
  static let count = 17
}

final class DinoLibrary: @unchecked Sendable {  // filled on a loader thread, read after it finishes
  var asset = [PieceAssets](repeating: PieceAssets(), count: DAsset.count)
  var built = false
}

// ---------------------------------------------------------------------------
// Joints (the skeletons and the sculpting share them)
// ---------------------------------------------------------------------------
/// The theropods (THB bones).
struct DinoTheroJ {
  var root = vec3(0), chest = vec3(0), neck = vec3(0), head = vec3(0), jaw = vec3(0)
  var tail1 = vec3(0), tail2 = vec3(0), tail3 = vec3(0), tip = vec3(0)
  var hipL = vec3(0), kneeL = vec3(0), ankleL = vec3(0), ballL = vec3(0)
  var shL = vec3(0), elL = vec3(0), wrL = vec3(0)
  var S: Float = 1
}

/// The pteranodon, standing on its feet with its wings spread (they fold in the pose).
enum DinoPJ {
  static let body = vec3(0, 0.27, -0.02), neck = vec3(0, 0.37, 0.08), head = vec3(0, 0.49, 0.16)
  static let jaw = vec3(0, 0.485, 0.2)
  static let shL = vec3(0.045, 0.35, 0.07), elL = vec3(0.14, 0.36, 0.06), wrL = vec3(0.27, 0.37, 0.04)
  static let knL = vec3(0.42, 0.37, 0.02), tipL = vec3(0.84, 0.36, -0.06)
  static let hipL = vec3(0.035, 0.24, -0.08), kneeL = vec3(0.045, 0.13, -0.04), footL = vec3(0.045, 0.012, -0.07)
}

/// The four-legged: joints of the triceratops (in its own units) and the brachiosaurus.
struct DinoQuadJ {
  var body = vec3(0), neck1 = vec3(0), neck2 = vec3(0), neck3 = vec3(0), head = vec3(0), jaw = vec3(0)
  var tail1 = vec3(0), tail2 = vec3(0), tail3 = vec3(0), tip = vec3(0)
  var flu = vec3(0), fll = vec3(0), ffoot = vec3(0), hlu = vec3(0), hll = vec3(0), hfoot = vec3(0)
}

enum Dino {
  /// Each animal is sculpted in its own units and scaled by this afterwards (the skeleton with it).
  static func animalScale(_ kind: Int) -> Float {
    switch kind {
    case DAsset.knight: return 0.95
    case DAsset.bishop: return 0.85
    case DAsset.queen: return 0.82
    case DAsset.king: return 0.85
    default: return 1.0
    }
  }
  /// ... and moved back a little, so that two animals facing each other (or one behind
  /// the other) on neighbouring squares don't run into each other.
  static func animalShift(_ kind: Int) -> Float {
    switch kind {
    case DAsset.knight: return -0.03
    case DAsset.bishop: return -0.05
    case DAsset.rook: return -0.05
    case DAsset.queen: return -0.08
    case DAsset.king: return -0.04
    default: return 0.0
    }
  }
  /// An animal's own units -> model space.
  static func toModel(_ kind: Int, _ v: vec3) -> vec3 { v * animalScale(kind) + vec3(0, 0, animalShift(kind)) }

  /// In the animal's own units.
  static func theroBase(_ kind: Int) -> DinoTheroJ {
    var j = DinoTheroJ()
    switch kind {
    case DAsset.pawn:  // a velociraptor: light, long-legged, a stiff tail held out straight
      j.root = vec3(0, 0.27, -0.02)
      j.chest = vec3(0, 0.28, 0.06)
      j.neck = vec3(0, 0.3, 0.14)
      j.head = vec3(0, 0.42, 0.2)
      j.jaw = vec3(0, 0.41, 0.215)
      j.tail1 = vec3(0, 0.28, -0.1)
      j.tail2 = vec3(0, 0.282, -0.22)
      j.tail3 = vec3(0, 0.28, -0.33)
      j.tip = vec3(0, 0.276, -0.46)
      j.hipL = vec3(0.042, 0.262, -0.02)
      j.kneeL = vec3(0.048, 0.175, 0.03)
      j.ankleL = vec3(0.048, 0.085, -0.035)
      j.ballL = vec3(0.048, 0.012, 0.0)
      j.shL = vec3(0.036, 0.262, 0.12)
      j.elL = vec3(0.056, 0.215, 0.1)
      j.wrL = vec3(0.058, 0.205, 0.165)
    case DAsset.queen:  // a spinosaurus: short legs, a long body under a sail, a long neck and snout
      j.root = vec3(0, 0.44, 0)
      j.chest = vec3(0, 0.47, 0.12)
      j.neck = vec3(0, 0.5, 0.29)
      j.head = vec3(0, 0.62, 0.43)
      j.jaw = vec3(0, 0.605, 0.45)
      j.tail1 = vec3(0, 0.46, -0.15)
      j.tail2 = vec3(0, 0.44, -0.34)
      j.tail3 = vec3(0, 0.41, -0.52)
      j.tip = vec3(0, 0.37, -0.7)
      j.hipL = vec3(0.058, 0.43, 0)
      j.kneeL = vec3(0.066, 0.29, 0.07)
      j.ankleL = vec3(0.066, 0.13, -0.02)
      j.ballL = vec3(0.066, 0.016, 0.03)
      j.shL = vec3(0.06, 0.43, 0.27)
      j.elL = vec3(0.085, 0.34, 0.29)
      j.wrL = vec3(0.08, 0.31, 0.37)
    default:  // the tyrannosaurus: a huge head, massive legs, tiny arms
      j.root = vec3(0, 0.5, 0)
      j.chest = vec3(0, 0.52, 0.1)
      j.neck = vec3(0, 0.56, 0.29)
      j.head = vec3(0, 0.69, 0.4)
      j.jaw = vec3(0, 0.665, 0.43)
      j.tail1 = vec3(0, 0.52, -0.16)
      j.tail2 = vec3(0, 0.5, -0.36)
      j.tail3 = vec3(0, 0.46, -0.53)
      j.tip = vec3(0, 0.41, -0.68)
      j.hipL = vec3(0.075, 0.48, -0.01)
      j.kneeL = vec3(0.085, 0.31, 0.075)
      j.ankleL = vec3(0.085, 0.14, -0.045)
      j.ballL = vec3(0.085, 0.018, 0.03)
      j.shL = vec3(0.08, 0.45, 0.27)
      j.elL = vec3(0.1, 0.4, 0.285)
      j.wrL = vec3(0.095, 0.385, 0.33)
    }
    return j
  }

  /// The theropod's joints in model space.
  static func theroJ(_ kind: Int) -> DinoTheroJ {
    var j = theroBase(kind)
    j.root = toModel(kind, j.root); j.chest = toModel(kind, j.chest); j.neck = toModel(kind, j.neck)
    j.head = toModel(kind, j.head); j.jaw = toModel(kind, j.jaw); j.tail1 = toModel(kind, j.tail1)
    j.tail2 = toModel(kind, j.tail2); j.tail3 = toModel(kind, j.tail3); j.tip = toModel(kind, j.tip)
    j.hipL = toModel(kind, j.hipL); j.kneeL = toModel(kind, j.kneeL); j.ankleL = toModel(kind, j.ankleL)
    j.ballL = toModel(kind, j.ballL); j.shL = toModel(kind, j.shL); j.elL = toModel(kind, j.elL)
    j.wrL = toModel(kind, j.wrL)
    j.S = animalScale(kind)
    return j
  }

  private static func theroSkel(_ j: DinoTheroJ) -> MSkeleton {
    MSkeleton([
      (-1, j.root, false),                    // ROOT
      (THB.root, j.chest, false),             // CHEST
      (THB.chest, j.neck, false),             // NECK
      (THB.neck, j.head, false),              // HEAD
      (THB.head, j.jaw, false),               // JAW
      (THB.root, j.tail1, false),             // TAIL1
      (THB.tail1, j.tail2, false),
      (THB.tail2, j.tail3, false),
      (THB.root, j.hipL, false),              // LTHIGH
      (THB.lthigh, j.kneeL, false),           // LSHIN
      (THB.lshin, j.ankleL, false),           // LFOOT
      (THB.root, mirrorXv(j.hipL), false),    // RTHIGH
      (THB.rthigh, mirrorXv(j.kneeL), false),
      (THB.rshin, mirrorXv(j.ankleL), false),
      (THB.chest, j.shL, false),              // LARM
      (THB.larm, j.elL, false),               // LFORE
      (THB.chest, mirrorXv(j.shL), false),    // RARM
      (THB.rarm, mirrorXv(j.elL), false),
    ])
  }

  /// The four-legged, in their own units.
  static func quadBase(_ kind: Int) -> DinoQuadJ {
    var j = DinoQuadJ()
    if kind == DAsset.bishop {
      j.body = vec3(0, 0.33, -0.02)
      j.neck1 = vec3(0, 0.33, 0.2)
      j.neck2 = vec3(0, 0.33, 0.235)
      j.neck3 = vec3(0, 0.33, 0.265)
      j.head = vec3(0, 0.33, 0.28)
      j.jaw = vec3(0, 0.25, 0.36)
      j.tail1 = vec3(0, 0.36, -0.3)
      j.tail2 = vec3(0, 0.33, -0.45)
      j.tail3 = vec3(0, 0.27, -0.56)
      j.tip = vec3(0, 0.19, -0.65)
      j.flu = vec3(0.1, 0.3, 0.15)
      j.fll = vec3(0.12, 0.16, 0.16)
      j.ffoot = vec3(0.115, 0.0, 0.17)
      j.hlu = vec3(0.1, 0.36, -0.2)
      j.hll = vec3(0.105, 0.18, -0.15)
      j.hfoot = vec3(0.105, 0.0, -0.18)
    } else {
      j.body = vec3(0, 0.52, 0)
      j.neck1 = vec3(0, 0.66, 0.22)
      j.neck2 = vec3(0, 0.86, 0.3)
      j.neck3 = vec3(0, 1.03, 0.37)
      j.head = vec3(0, 1.19, 0.43)
      j.jaw = vec3(0, 1.185, 0.47)
      j.tail1 = vec3(0, 0.55, -0.24)
      j.tail2 = vec3(0, 0.47, -0.42)
      j.tail3 = vec3(0, 0.37, -0.56)
      j.tip = vec3(0, 0.25, -0.7)
      j.flu = vec3(0.1, 0.58, 0.16)
      j.fll = vec3(0.105, 0.3, 0.16)
      j.ffoot = vec3(0.105, 0.0, 0.17)
      j.hlu = vec3(0.1, 0.53, -0.16)
      j.hll = vec3(0.105, 0.26, -0.14)
      j.hfoot = vec3(0.105, 0.0, -0.17)
    }
    return j
  }

  /// The four-legged's joints in model space.
  static func quadJ(_ kind: Int) -> DinoQuadJ {
    var j = quadBase(kind)
    j.body = toModel(kind, j.body); j.neck1 = toModel(kind, j.neck1); j.neck2 = toModel(kind, j.neck2)
    j.neck3 = toModel(kind, j.neck3); j.head = toModel(kind, j.head); j.jaw = toModel(kind, j.jaw)
    j.tail1 = toModel(kind, j.tail1); j.tail2 = toModel(kind, j.tail2); j.tail3 = toModel(kind, j.tail3)
    j.tip = toModel(kind, j.tip); j.flu = toModel(kind, j.flu); j.fll = toModel(kind, j.fll)
    j.ffoot = toModel(kind, j.ffoot); j.hlu = toModel(kind, j.hlu); j.hll = toModel(kind, j.hll)
    j.hfoot = toModel(kind, j.hfoot)
    return j
  }

  private static func quadSkel(_ j: DinoQuadJ) -> MSkeleton {
    MSkeleton([
      (-1, j.body, false),                  // BODY
      (QDB.body, j.neck1, false),           // NECK1
      (QDB.neck1, j.neck2, false),
      (QDB.neck2, j.neck3, false),
      (QDB.neck3, j.head, false),           // HEAD
      (QDB.head, j.jaw, false),             // JAW
      (QDB.body, j.tail1, false),           // TAIL1
      (QDB.tail1, j.tail2, false),
      (QDB.tail2, j.tail3, false),
      (QDB.body, j.flu, false),             // FLU
      (QDB.flu, j.fll, false),
      (QDB.body, mirrorXv(j.flu), false),   // FRU
      (QDB.fru, mirrorXv(j.fll), false),
      (QDB.body, j.hlu, false),             // HLU
      (QDB.hlu, j.hll, false),
      (QDB.body, mirrorXv(j.hlu), false),   // HRU
      (QDB.hru, mirrorXv(j.hll), false),
    ])
  }

  // ---------------------------------------------------------------------------
  // Skeletons
  // ---------------------------------------------------------------------------
  private static let pawnSkel = theroSkel(theroJ(DAsset.pawn))
  private static let queenSkel = theroSkel(theroJ(DAsset.queen))
  private static let kingSkel = theroSkel(theroJ(DAsset.king))
  private static let bishopSkel = quadSkel(quadJ(DAsset.bishop))
  private static let rookSkel = quadSkel(quadJ(DAsset.rook))
  private static let pteroSkel: MSkeleton = {
    func P(_ v: vec3) -> vec3 { Dino.toModel(DAsset.knight, v) }
    return MSkeleton([
      (-1, P(DinoPJ.body), false),                      // BODY
      (PTB.body, P(DinoPJ.neck), false),                // NECK
      (PTB.neck, P(DinoPJ.head), false),                // HEAD
      (PTB.head, P(DinoPJ.jaw), false),                 // JAW
      (PTB.body, P(DinoPJ.shL), false),                 // LARM
      (PTB.larm, P(DinoPJ.elL), false),                 // LFORE
      (PTB.lfore, P(DinoPJ.wrL), false),                // LMETA (the hand)
      (PTB.lmeta, P(DinoPJ.knL), false),                // LFINGER (the wing finger)
      (PTB.body, P(mirrorXv(DinoPJ.shL)), false),       // RARM
      (PTB.rarm, P(mirrorXv(DinoPJ.elL)), false),
      (PTB.rfore, P(mirrorXv(DinoPJ.wrL)), false),
      (PTB.rmeta, P(mirrorXv(DinoPJ.knL)), false),
      (PTB.body, P(DinoPJ.hipL), false),                // LTHIGH
      (PTB.lthigh, P(DinoPJ.kneeL), false),             // LSHIN
      (PTB.body, P(mirrorXv(DinoPJ.hipL)), false),      // RTHIGH
      (PTB.rthigh, P(mirrorXv(DinoPJ.kneeL)), false),
      (PTB.lfore, P(DinoPJ.elL), false),                // LTRAIL: the inner membrane's trailing edge
      (PTB.rfore, P(mirrorXv(DinoPJ.elL)), false),
      (PTB.lfinger, P(DinoPJ.knL), false),              // LTRAIL2: the outer membrane's
      (PTB.rfinger, P(mirrorXv(DinoPJ.knL)), false),
    ])
  }()
  private static let noSkel = MSkeleton()

  /// count == 0 for rigid assets.
  static func skeleton(_ asset: Int) -> MSkeleton {
    switch asset {
    case DAsset.pawn: return pawnSkel
    case DAsset.knight: return pteroSkel
    case DAsset.bishop: return bishopSkel
    case DAsset.rook: return rookSkel
    case DAsset.queen: return queenSkel
    case DAsset.king: return kingSkel
    default: return noSkel
    }
  }

  // ---------------------------------------------------------------------------
  static func pieceHeight(_ kind: Int) -> Float {
    let H: [Float] = [0.46, 0.57, 0.53, 1.28, 0.67, 0.69]
    return H[max(0, min(5, kind))]
  }
  static func pieceRadius(_ kind: Int) -> Float {
    let R: [Float] = [0.19, 0.22, 0.24, 0.28, 0.22, 0.25]
    return R[max(0, min(5, kind))]
  }
  /// Raptor, spinosaurus, tyrannosaurus.
  static func theropod(_ kind: Int) -> Bool { kind == DAsset.pawn || kind == DAsset.queen || kind == DAsset.king }
  /// Triceratops, brachiosaurus.
  static func quadruped(_ kind: Int) -> Bool { kind == DAsset.bishop || kind == DAsset.rook }

  // Rest-pose points (model space)
  /// The middle of the head.
  static func headPoint(_ kind: Int) -> vec3 {
    switch kind {
    case DAsset.pawn: return toModel(kind, vec3(0, 0.43, 0.25))
    case DAsset.knight: return toModel(kind, vec3(0, 0.5, 0.22))
    case DAsset.bishop: return toModel(kind, vec3(0, 0.31, 0.42))
    case DAsset.rook: return toModel(kind, vec3(0, 1.21, 0.48))
    case DAsset.queen: return toModel(kind, vec3(0, 0.63, 0.51))
    default: return toModel(kind, vec3(0, 0.71, 0.52))
    }
  }
  /// Between the jaws (on the head bone), the beak tip for the pteranodon.
  static func mouthPoint(_ kind: Int) -> vec3 {
    switch kind {
    case DAsset.pawn: return toModel(kind, vec3(0, 0.413, 0.3))
    case DAsset.knight: return toModel(kind, vec3(0, 0.465, 0.49))
    case DAsset.bishop: return toModel(kind, vec3(0, 0.22, 0.6))
    case DAsset.rook: return toModel(kind, vec3(0, 1.185, 0.54))
    case DAsset.queen: return toModel(kind, vec3(0, 0.598, 0.66))
    default: return toModel(kind, vec3(0, 0.664, 0.61))
    }
  }
  /// The heart (where blows land).
  static func chestPoint(_ kind: Int) -> vec3 {
    switch kind {
    case DAsset.pawn: return toModel(kind, vec3(0, 0.27, 0.06))
    case DAsset.knight: return toModel(kind, vec3(0, 0.32, 0.03))
    case DAsset.bishop: return toModel(kind, vec3(0, 0.33, 0.08))
    case DAsset.rook: return toModel(kind, vec3(0, 0.58, 0.12))
    case DAsset.queen: return toModel(kind, vec3(0, 0.45, 0.14))
    default: return toModel(kind, vec3(0, 0.48, 0.15))
    }
  }
  /// The raptor's sickle claw (right foot), the spinosaurus' right thumb claw, the
  /// pteranodon's right foot.
  static func clawPoint(_ kind: Int) -> vec3 {
    switch kind {
    case DAsset.pawn: return toModel(kind, mirrorXv(theroBase(DAsset.pawn).ballL) + vec3(0.006, 0.045, 0.045))
    case DAsset.knight: return toModel(kind, mirrorXv(DinoPJ.footL) + vec3(0, 0.0, 0.02))
    case DAsset.queen: return toModel(kind, mirrorXv(theroBase(DAsset.queen).wrL) + vec3(0, -0.03, 0.06))
    default: return chestPoint(kind)
    }
  }
  /// The triceratops' brow horns (between their tips).
  static func hornPoint(_ kind: Int) -> vec3 {
    if kind == DAsset.bishop { return toModel(kind, vec3(0, 0.49, 0.62)) }
    return mouthPoint(kind)
  }
  /// The end of the tail (on the last tail bone).
  static func tailTip(_ kind: Int) -> vec3 {
    if theropod(kind) { return theroJ(kind).tip }
    if quadruped(kind) { return quadJ(kind).tip }
    return toModel(kind, DinoPJ.body + vec3(0, 0, -0.08))
  }
  /// Distance covered by one walk cycle.
  static func strideCycle(_ kind: Int) -> Float {
    switch kind {
    case DAsset.pawn: return 0.32
    case DAsset.knight: return 0.2
    case DAsset.bishop: return 0.38
    case DAsset.rook: return 0.6
    case DAsset.queen: return 0.36
    default: return 0.42
    }
  }
  /// From the snout to the tail tip (rest pose).
  static func bodyLength(_ kind: Int) -> Float {
    switch kind {
    case DAsset.pawn: return 0.8
    case DAsset.knight: return 0.56 * animalScale(kind)
    case DAsset.bishop: return 1.27 * animalScale(kind)
    case DAsset.rook: return 1.22
    case DAsset.queen: return 1.42 * animalScale(kind)
    default: return 1.32 * animalScale(kind)
    }
  }
  /// The pteranodon's left wing tip, wings spread (rest pose).
  static func wingTip() -> vec3 { toModel(DAsset.knight, DinoPJ.tipL) }
  /// Half the width of the body (how far a flank is from the middle).
  static func bodyHalfWidth(_ kind: Int) -> Float {
    let W: [Float] = [0.05, 0.05, 0.13, 0.13, 0.06, 0.088]
    return W[max(0, min(5, kind))]
  }

  /// Rest-pose points that stand on the board (feet, and the pteranodon's hands) and their
  /// bones; the pose keeps the lowest of them on the board.
  static func groundPoints(_ asset: Int) -> [(p: vec3, bone: Int)] {
    switch asset {
    case DAsset.pawn, DAsset.queen, DAsset.king:
      let j = theroJ(asset)
      let toe: Float = asset == DAsset.pawn ? 0.03 : 0.05
      let p0 = vec3(j.ballL.x, 0.0, j.ballL.z + toe * j.S)
      let p2 = vec3(j.ballL.x, 0.0, j.ballL.z - 0.01 * j.S)
      return [(p0, THB.lfoot), (mirrorXv(p0), THB.rfoot), (p2, THB.lfoot), (mirrorXv(p2), THB.rfoot)]
    case DAsset.knight:
      let p0 = toModel(asset, vec3(DinoPJ.footL.x, 0.0, DinoPJ.footL.z + 0.01))
      // the hands (they reach the board once the wings are folded)
      let p2 = toModel(asset, DinoPJ.knL + vec3(0, -0.012, 0.012))
      return [(p0, PTB.lshin), (mirrorXv(p0), PTB.rshin), (p2, PTB.lmeta), (mirrorXv(p2), PTB.rmeta)]
    default:
      let j = quadJ(asset)
      let p0 = vec3(j.ffoot.x, 0.0, j.ffoot.z)
      let p2 = vec3(j.hfoot.x, 0.0, j.hfoot.z)
      return [(p0, QDB.fll), (mirrorXv(p0), QDB.frl), (p2, QDB.hll), (mirrorXv(p2), QDB.hrl)]
    }
  }
}
