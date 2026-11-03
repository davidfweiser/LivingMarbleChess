// The Roman army: a fourth set of pieces. Legionaries in cloaks with pilum,
// scutum and gladius, a war chariot drawn by two horses, a centurion with
// sword and dagger, a wooden siege tower on wheels, the empress and the
// emperor. Built like the medieval army (painted, skinned SDF figures) and
// posed with the same animation controls (MControls, MPose, MSkeleton).
// (Sculpting: RomanMesh.swift. Posing: RomanPose.swift.)
import Foundation
import simd

/// Assets. The first six are the chessmen in Kind order.
enum RAsset {
  static let pawn = 0     // legionary: helmet with a crest, banded armour, cloak, scutum, pilum, gladius
  static let knight = 1   // a war chariot drawn by two horses, with its charioteer
  static let bishop = 2   // centurion: transverse crest, mail and medals, gladius and pugio
  static let rook = 3     // wooden siege tower on wheels: battering ram, boarding bridge, scorpion
  static let queen = 4    // the empress: diadem, stola and palla, a spear (and a hidden dagger)
  static let king = 5     // the emperor: laurel wreath, gilded cuirass, purple cloak, eagle sceptre
  static let pilum = 6    // a pilum on its own (thrown): butt at the origin, along +y
  static let bolt = 7     // a scorpion bolt: butt at the origin, along +y
  static let pool = 8     // a pool of blood: a flat disc of radius 1 lying on the board
  static let goblet = 9   // the empress's golden cup (of poisoned wine)
  static let knife = 10   // the centurion's pugio on its own (thrown): grip at the origin, blade along +y
  static let lion = 11    // a lion (the emperor's games)
  static let count = 12
}

/// Per-vertex materials (integer part of Vertex.mat). Colours depend on the army:
/// the white legion wears ivory and crimson over bright steel, the black legion
/// black and midnight blue over blackened steel.
enum RMat {
  static let steel = 0     // armour plates, blades, helmets
  static let bronze = 1    // helmet fittings, shield bosses, buckles
  static let gold = 2      // laurels, diadem, eagles, medals
  static let tunic = 3     // army colour: tunics (ivory / black)
  static let cloak = 4     // army second colour: cloaks (crimson / midnight blue)
  static let crest = 5     // horsehair crests
  static let skin = 6
  static let hair = 7
  static let leather = 8   // belts, straps, sandals, harness
  static let wood = 9      // shafts, the chariot
  static let timber = 10   // the siege tower's rough beams
  static let shield = 11   // the painted face of a scutum
  static let horse = 12
  static let mane = 13
  static let hide = 14     // wet rawhide nailed over the siege tower
  static let iron = 15     // dark iron: tyres, nails, the ram's head
  static let rope = 16
  static let purple = 17   // imperial purple
  static let linen = 18    // the empress's stola
  static let blood = 19
  static let dark = 20     // mouths, doorways, the inside of things
  static let mail = 21     // the centurion's mail shirt
  static let fur = 22      // lion
  static let lionMane = 23
  static let teeth = 24    // teeth and claws
  static let wine = 25
  static let eye = 26      // whites of the eyes with a dark iris
  static let count = 27
}

/// Humans (legionary, centurion, empress, emperor). "Left" is the figure's own left
/// (+x when it faces +z).
enum RH {
  static let root = 0  // pelvis
  static let chest = 1, head = 2
  static let lthigh = 3, lshin = 4, rthigh = 5, rshin = 6
  static let larm = 7, lfore = 8, rarm = 9, rfore = 10
  static let itemR = 11   // right hand: pilum, spear, sceptre, the centurion's gladius
  static let itemL = 12   // left hand: scutum, the centurion's pugio, the empress's goblet
  static let blade = 13   // a blade drawn into the right hand (MControls.draw)
  static let sheath = 14  // ... and the same blade in its scabbard at the hip until then
  static let cloak = 15, hem = 16  // the cloak, from the shoulders, and its hem
  static let count = 17
}
/// The chariot: two horses (sharing one set of leg bones), the car and the charioteer.
enum CHB {
  static let car = 0
  static let wheelL = 1, wheelR = 2
  static let horses = 3, neck = 4, head = 5, tail = 6
  static let flu = 7, fll = 8, fru = 9, frl = 10, hlu = 11, hll = 12, hru = 13, hrl = 14
  static let driver = 15, dhead = 16, drarm = 17, drfore = 18, dlarm = 19, dlfore = 20
  static let item = 21   // the charioteer's spear
  static let cloak = 22
  static let count = 23
}
/// The siege tower.
enum TWB {
  static let body = 0
  static let wheel1 = 1, wheel2 = 2, wheel3 = 3, wheel4 = 4  // front left, front right, back left, back right
  static let ram = 5       // the battering ram, hung on ropes in the bottom storey
  static let bridge = 6    // the boarding bridge, hinged at the top storey
  static let door = 7      // the door at the foot: a ramp that drops
  static let scorpion = 8  // the bolt-thrower on the roof (turns and aims)
  static let loaded = 9    // the bolt lying in it, ready to shoot
  static let flag = 10     // the vexillum
  static let count = 11
}
/// The lion.
enum LNB {
  static let body = 0, neck = 1, head = 2, jaw = 3, tail1 = 4, tail2 = 5
  static let flu = 6, fll = 7, fru = 8, frl = 9, hlu = 10, hll = 11, hru = 12, hrl = 13
  static let count = 14
}

final class RomanLibrary: @unchecked Sendable {  // filled on a loader thread, read after it finishes
  var asset = [PieceAssets](repeating: PieceAssets(), count: RAsset.count)
  var built = false
}

/// glm::mix (x * (1 - a) + y * a), so the figures come out exactly as in the C++ version.
@inline(__always) func romanMix(_ x: Float, _ y: Float, _ a: Float) -> Float { x * (1 - a) + y * a }
@inline(__always) func romanMix(_ x: vec3, _ y: vec3, _ a: Float) -> vec3 { x * (1 - a) + y * a }

// ---------------------------------------------------------------------------
// Joint layouts (the skeletons and the sculpting share them)
// ---------------------------------------------------------------------------
/// The humans (RH bones).
struct RomanJ {
  var root = vec3(0), waist = vec3(0), neck = vec3(0), hipL = vec3(0), kneeL = vec3(0), ankleL = vec3(0)
  var shL = vec3(0), elL = vec3(0), wrL = vec3(0), shR = vec3(0), elR = vec3(0), wrR = vec3(0)
  var gripR = vec3(0), gripL = vec3(0), sheath = vec3(0), cloak = vec3(0), hem = vec3(0)
  var S: Float = 1

  /// The humans are sculpted at the legionary's size and built this much larger.
  static func figScale(_ kind: Int) -> Float {
    switch kind {
    case RAsset.bishop: return 1.04
    case RAsset.queen: return 1.05
    case RAsset.king: return 1.1
    default: return 1.0
    }
  }

  /// The standing figure at the legionary's size: 0.86 tall without the crest.
  static func base(_ kind: Int) -> RomanJ {
    var j = RomanJ()
    j.root = vec3(0, 0.43, 0)
    j.waist = vec3(0, 0.49, 0)
    j.neck = vec3(0, 0.668, 0.004)
    j.hipL = vec3(0.05, 0.41, 0)
    j.kneeL = vec3(0.052, 0.228, 0.01)
    j.ankleL = vec3(0.052, 0.054, -0.004)
    j.shL = vec3(0.102, 0.63, -0.004)
    j.elL = vec3(0.124, 0.49, -0.014)
    j.wrL = vec3(0.12, 0.472, 0.1)
    j.gripL = vec3(0.124, 0.474, 0.084)
    j.shR = mirrorXv(j.shL)
    j.elR = vec3(-0.122, 0.488, -0.014)
    j.wrR = vec3(-0.116, 0.47, 0.104)
    j.gripR = vec3(-0.114, 0.472, 0.124)
    j.sheath = vec3(-0.096, 0.452, 0.03)  // the scabbard at the right hip
    j.cloak = vec3(0, 0.632, -0.05)
    j.hem = vec3(0, 0.42, -0.09)
    switch kind {
    case RAsset.bishop:  // gladius in the right hand, pugio in the left, both held low and forward
      j.elR = vec3(-0.13, 0.492, -0.012)
      j.wrR = vec3(-0.13, 0.44, 0.09)
      j.gripR = vec3(-0.128, 0.436, 0.11)
      j.elL = mirrorXv(j.elR)
      j.wrL = mirrorXv(j.wrR)
      j.gripL = mirrorXv(j.gripR)
    case RAsset.queen:  // narrower shoulders; her spear upright in the right hand, the left hand at her waist
      j.shL = vec3(0.09, 0.626, -0.004)
      j.shR = mirrorXv(j.shL)
      j.elL = vec3(0.116, 0.49, -0.02)
      j.wrL = vec3(0.07, 0.47, 0.078)
      j.gripL = vec3(0.058, 0.472, 0.092)
      j.elR = vec3(-0.114, 0.488, -0.014)
      j.wrR = vec3(-0.11, 0.47, 0.1)
      j.gripR = vec3(-0.108, 0.472, 0.12)
    case RAsset.king:  // the eagle sceptre upright in the right hand, the left arm at his side; his sword on the left
      j.elL = vec3(0.134, 0.49, -0.02)
      j.wrL = vec3(0.14, 0.375, 0.004)
      j.gripL = vec3(0.142, 0.358, 0.01)
      j.sheath = vec3(0.098, 0.452, 0.03)
    default: break
    }
    return j
  }

  /// The standing figure at its own size (the joints scaled by figScale).
  static func stand(_ kind: Int) -> RomanJ {
    var j = base(kind)
    let S = figScale(kind)
    j.root *= S; j.waist *= S; j.neck *= S; j.hipL *= S; j.kneeL *= S; j.ankleL *= S
    j.shL *= S; j.elL *= S; j.wrL *= S; j.shR *= S; j.elR *= S
    j.wrR *= S; j.gripR *= S; j.gripL *= S; j.sheath *= S; j.cloak *= S; j.hem *= S
    j.S = S
    return j
  }

  func skeleton() -> MSkeleton {
    MSkeleton([
      (-1, root, false),              // ROOT
      (RH.root, waist, false),        // CHEST
      (RH.chest, neck, false),        // HEAD
      (RH.root, hipL, false),         // LTHIGH
      (RH.lthigh, kneeL, false),      // LSHIN
      (RH.root, mirrorXv(hipL), false),  // RTHIGH
      (RH.rthigh, mirrorXv(kneeL), false),
      (RH.chest, shL, false),         // LARM
      (RH.larm, elL, false),
      (RH.chest, shR, false),         // RARM
      (RH.rarm, elR, false),
      (RH.rfore, gripR, true),        // ITEMR
      (RH.lfore, gripL, true),        // ITEML
      (RH.rfore, gripR, true),        // BLADE
      (RH.root, sheath, true),        // SHEATH
      (RH.chest, cloak, false),       // CLOAK
      (RH.cloak, hem, false),         // HEM
    ])
  }
}

/// The chariot: the horses are the medieval horse in "horse units", smaller, two abreast.
enum RChariotJ {
  static let CS: Float = 0.56   // horse units -> chariot
  static let HX: Float = 0.098  // each horse's distance from the pole
  static let H0 = vec3(0, 0, 0.15)
  @inline(__always) static func HU(_ x: Float, _ y: Float, _ z: Float) -> vec3 { vec3(x, y, z) * CS + H0 }
  static let axleY: Float = 0.14, axleZ: Float = -0.25, wheelX: Float = 0.178, wheelR: Float = 0.132
  static let floorY: Float = 0.15
  static let car = vec3(0, axleY, axleZ), wheelL = vec3(wheelX, axleY, axleZ)
  static let horses = HU(0, 0.53, 0), neck = HU(0, 0.62, 0.24), head = HU(0, 0.88, 0.40), tail = HU(0, 0.67, -0.35)
  static let flu = HU(0.075, 0.48, 0.22), fll = HU(0.075, 0.27, 0.23)
  static let hlu = HU(0.08, 0.53, -0.22), hll = HU(0.08, 0.27, -0.30)
  // the charioteer: the legionary's figure, smaller, standing in the car
  static let DS: Float = 0.62
  static let D0 = vec3(0, floorY, axleZ + 0.01)
  @inline(__always) static func DU(_ v: vec3) -> vec3 { v * DS + D0 }
}

/// The siege tower: three storeys of timber, 0.44 wide.
enum RTowerJ {
  static let W: Float = 0.215, D: Float = 0.205  // half width and half depth at the foot
  static let topW: Float = 0.185, topD: Float = 0.178
  static let y0: Float = 0.05, y1: Float = 0.43, y2: Float = 0.79, y3: Float = 1.14  // floors and the roof
  static let wheel1 = vec3(0.232, 0.075, 0.14)
  static let ram = vec3(0, 0.25, 0.0)
  static let bridge = vec3(0, y2 + 0.01, 0.2)
  static let door = vec3(0, 0.055, 0.222)
  static let scorpion = vec3(0, y3 + 0.11, -0.02)
  static let flag = vec3(-0.15, y3 + 0.02, -0.15)
}

/// The lion, standing, facing +z.
enum RLionJ {
  static let body = vec3(0, 0.3, 0), neck = vec3(0, 0.36, 0.17), head = vec3(0, 0.39, 0.29), jaw = vec3(0, 0.36, 0.33)
  static let tail1 = vec3(0, 0.34, -0.25), tail2 = vec3(0, 0.24, -0.42)
  static let flu = vec3(0.07, 0.29, 0.16), fll = vec3(0.072, 0.15, 0.17)
  static let hlu = vec3(0.075, 0.31, -0.17), hll = vec3(0.075, 0.16, -0.24)
}

// ---------------------------------------------------------------------------
// Skeletons and rest-pose queries
// ---------------------------------------------------------------------------
enum Roman {
  private static let pawnSkel = RomanJ.stand(RAsset.pawn).skeleton()
  private static let bishopSkel = RomanJ.stand(RAsset.bishop).skeleton()
  private static let queenSkel = RomanJ.stand(RAsset.queen).skeleton()
  private static let kingSkel = RomanJ.stand(RAsset.king).skeleton()
  private static let chariotSkel: MSkeleton = {
    let d = RomanJ.base(RAsset.pawn)
    return MSkeleton([
      (-1, RChariotJ.car, false),                      // CAR
      (CHB.car, RChariotJ.wheelL, true),               // WHEELL
      (CHB.car, mirrorXv(RChariotJ.wheelL), true),     // WHEELR
      (CHB.car, RChariotJ.horses, false),              // HORSES
      (CHB.horses, RChariotJ.neck, false),
      (CHB.neck, RChariotJ.head, false),
      (CHB.horses, RChariotJ.tail, false),
      (CHB.horses, RChariotJ.flu, false),
      (CHB.flu, RChariotJ.fll, false),
      (CHB.horses, mirrorXv(RChariotJ.flu), false),
      (CHB.fru, mirrorXv(RChariotJ.fll), false),
      (CHB.horses, RChariotJ.hlu, false),
      (CHB.hlu, RChariotJ.hll, false),
      (CHB.horses, mirrorXv(RChariotJ.hlu), false),
      (CHB.hru, mirrorXv(RChariotJ.hll), false),
      (CHB.car, RChariotJ.DU(d.root), false),          // DRIVER
      (CHB.driver, RChariotJ.DU(d.neck), false),       // DHEAD
      (CHB.driver, RChariotJ.DU(d.shR), false),        // DRARM
      (CHB.drarm, RChariotJ.DU(d.elR), false),
      (CHB.driver, RChariotJ.DU(d.shL), false),        // DLARM
      (CHB.dlarm, RChariotJ.DU(d.elL), false),
      (CHB.drfore, RChariotJ.DU(d.gripR), true),       // ITEM
      (CHB.driver, RChariotJ.DU(d.cloak), false),      // CLOAK
    ])
  }()
  private static let towerSkel: MSkeleton = {
    let w = RTowerJ.wheel1
    return MSkeleton([
      (-1, vec3(0), false),                          // BODY
      (TWB.body, w, true),                           // WHEEL1 (front left)
      (TWB.body, mirrorXv(w), true),                 // front right
      (TWB.body, vec3(w.x, w.y, -w.z), true),
      (TWB.body, vec3(-w.x, w.y, -w.z), true),
      (TWB.body, RTowerJ.ram, true),                 // RAM
      (TWB.body, RTowerJ.bridge, true),              // BRIDGE
      (TWB.body, RTowerJ.door, true),                // DOOR
      (TWB.body, RTowerJ.scorpion, true),            // SCORPION
      (TWB.scorpion, RTowerJ.scorpion, true),        // LOADED
      (TWB.body, RTowerJ.flag, true),                // FLAG
    ])
  }()
  private static let lionSkel = MSkeleton([
    (-1, RLionJ.body, false),
    (LNB.body, RLionJ.neck, false),
    (LNB.neck, RLionJ.head, false),
    (LNB.head, RLionJ.jaw, false),
    (LNB.body, RLionJ.tail1, false),
    (LNB.tail1, RLionJ.tail2, false),
    (LNB.body, RLionJ.flu, false),
    (LNB.flu, RLionJ.fll, false),
    (LNB.body, mirrorXv(RLionJ.flu), false),
    (LNB.fru, mirrorXv(RLionJ.fll), false),
    (LNB.body, RLionJ.hlu, false),
    (LNB.hlu, RLionJ.hll, false),
    (LNB.body, mirrorXv(RLionJ.hlu), false),
    (LNB.hru, mirrorXv(RLionJ.hll), false),
  ])
  private static let noSkel = MSkeleton()

  /// count == 0 for rigid assets.
  static func skeleton(_ asset: Int) -> MSkeleton {
    switch asset {
    case RAsset.pawn: return pawnSkel
    case RAsset.knight: return chariotSkel
    case RAsset.bishop: return bishopSkel
    case RAsset.rook: return towerSkel
    case RAsset.queen: return queenSkel
    case RAsset.king: return kingSkel
    case RAsset.lion: return lionSkel
    default: return noSkel
    }
  }

  static func pieceHeight(_ kind: Int) -> Float {
    let H: [Float] = [0.92, 0.78, 0.95, 1.3, 0.92, 0.98]
    return H[max(0, min(5, kind))]
  }
  static func pieceRadius(_ kind: Int) -> Float {
    let R: [Float] = [0.21, 0.36, 0.21, 0.33, 0.2, 0.22]
    return R[max(0, min(5, kind))]
  }
  /// Legionary, centurion, empress, emperor.
  static func human(_ kind: Int) -> Bool {
    kind == RAsset.pawn || kind == RAsset.bishop || kind == RAsset.queen || kind == RAsset.king
  }

  // Rest-pose points (model space)
  /// The right hand's grip.
  static func handR(_ kind: Int) -> vec3 {
    if kind == RAsset.knight { return RChariotJ.DU(RomanJ.base(RAsset.pawn).gripR) }
    if human(kind) { return RomanJ.stand(kind).gripR }
    return vec3(0, 0.5, 0.1)
  }
  /// The left hand's grip.
  static func handL(_ kind: Int) -> vec3 {
    if kind == RAsset.knight { return RChariotJ.DU(RomanJ.base(RAsset.pawn).gripL) }
    if human(kind) { return RomanJ.stand(kind).gripL }
    return vec3(0, 0.5, 0.1)
  }
  /// Tip of the right-hand item (pilum, spear, sceptre, the charioteer's spear).
  static func itemTip(_ kind: Int) -> vec3 {
    switch kind {
    case RAsset.pawn: return vec3(RomanJ.stand(RAsset.pawn).gripR.x, 0.954, RomanJ.stand(RAsset.pawn).gripR.z)
    case RAsset.knight: return handR(RAsset.knight) + vec3(0, 0.43, 0)
    case RAsset.bishop:
      let j = RomanJ.stand(RAsset.bishop)
      return j.gripR + normalize(vec3(0.0, 0.42, 1.0)) * 0.24 * j.S
    case RAsset.queen:
      let g = RomanJ.stand(RAsset.queen).gripR
      return vec3(g.x, 1.094 * RomanJ.figScale(RAsset.queen), g.z)
    case RAsset.king:
      let g = RomanJ.stand(RAsset.king).gripR
      return vec3(g.x, 0.92 * RomanJ.figScale(RAsset.king), g.z)
    default: return handR(kind)
    }
  }
  /// Tip of the drawn blade (gladius / dagger) in the rest pose.
  static func bladeTip(_ kind: Int) -> vec3 {
    let j = RomanJ.stand(human(kind) ? kind : RAsset.pawn)
    if kind == RAsset.bishop { return itemTip(kind) }
    if kind == RAsset.queen { return j.gripR + normalize(vec3(0, 0.3, 1.0)) * 0.11 * j.S }
    return j.gripR + vec3(0, 0.24, 0) * j.S
  }
  /// Middle of the head (the charioteer's for the chariot).
  static func headPoint(_ kind: Int) -> vec3 {
    if kind == RAsset.knight { return RChariotJ.DU(vec3(0, 0.738, 0.012)) }
    if kind == RAsset.rook { return vec3(0, 0.9, 0) }
    return vec3(0, 0.738, 0.012) * RomanJ.figScale(kind)
  }
  /// The heart (where blows land).
  static func chestPoint(_ kind: Int) -> vec3 {
    if kind == RAsset.knight { return RChariotJ.DU(vec3(0, 0.56, 0.03)) }
    if kind == RAsset.rook { return vec3(0, 0.45, 0.2) }
    return vec3(0, 0.56, 0.04) * RomanJ.figScale(kind)
  }
  /// Distance covered by one walk / gallop cycle.
  static func strideCycle(_ kind: Int) -> Float {
    switch kind {
    case RAsset.knight: return 0.36
    case RAsset.rook: return 0.5
    default: return 0.3 * RomanJ.figScale(kind)
    }
  }
  /// Chariot and tower wheels.
  static func wheelRadius(_ kind: Int) -> Float { kind == RAsset.rook ? 0.078 : RChariotJ.wheelR }
  /// The tower's bolt-thrower (pivot), and where its bolt sits.
  static func scorpionPoint() -> vec3 { RTowerJ.scorpion + vec3(0, 0.02, 0.0) }
  /// The head of the battering ram, swung fully out.
  static func ramTip() -> vec3 { vec3(0, RTowerJ.ram.y, 0.215 + 0.32) }
  /// The foot of the tower's door.
  static func doorPoint() -> vec3 { vec3(0, 0.0, RTowerJ.door.z + 0.34) }
  static func lionMouth() -> vec3 { vec3(0, 0.37, 0.4) }
}
