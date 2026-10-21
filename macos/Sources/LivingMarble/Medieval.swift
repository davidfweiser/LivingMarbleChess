// The medieval army: a second set of pieces made of painted, articulated
// figures (men-at-arms, knights on horseback, bishops and royals on their
// thrones, a gatehouse with a drawbridge - and the dragon that lives in it).
// Like the marble set everything is sculpted from signed distance fields at
// startup; on top of that every figure is skinned to a small skeleton so it
// can walk, stand up, swing a sword or spread its wings.
// (Sculpting: MedievalMesh.swift. Posing: MedievalPose.swift.)
import Foundation
import simd

/// Assets. The first six are the chessmen in Kind order.
enum MAsset {
  static let pawn = 0      // man-at-arms: kettle hat, gambeson, tabard, kite shield, spear
  static let knight = 1    // knight in a great helm on a caparisoned horse, with a lance
  static let bishop = 2    // bishop in mitre and cope with his crozier (sits on the cathedra)
  static let rook = 3      // gatehouse tower with battlements, portcullis and drawbridge
  static let queen = 4     // queen in crown and veil with a sceptre (sits on the throne)
  static let king = 5      // bearded king with crown, ermine mantle and sword (sits on the throne)
  static let throne = 6    // royal throne (king and queen)
  static let cathedra = 7  // the bishop's gothic chair
  static let legs = 8      // the gatehouse's stone legs (drawn only while they are out)
  static let dragon = 9    // the dragon that lives in the gatehouse
  static let spear = 10    // a man-at-arms' spear on its own (thrown as a javelin), butt at the origin along +y
  static let count = 11
}

/// Per-vertex materials (integer part of Vertex.mat). Colours depend on the army.
enum MMat {
  static let stone = 0      // castle masonry (pale limestone / dark basalt)
  static let wood = 1       // thrones, shafts, drawbridge planks
  static let steel = 2      // plate armour, helmets, blades (bright / blackened)
  static let gold = 3       // crowns, trims, crozier, finials
  static let cloth = 4      // army colour: tabards, robes, gowns, caparisons
  static let cloth2 = 5     // heraldic second colour (azure / gules)
  static let skin = 6
  static let hair = 7       // hair and beards
  static let horse = 8      // horse coat (grey / black)
  static let leather = 9    // belts, boots, saddle, reins
  static let fur = 10       // ermine
  static let gem = 11       // rubies and sapphires
  static let mail = 12      // chain mail
  static let iron = 13      // wrought iron: portcullis, hinges, chains, bands
  static let scale = 14     // dragon hide
  static let membrane = 15  // dragon wings
  static let horn = 16      // horns, teeth, claws
  static let eye = 17       // glowing dragon eyes
  static let velvet = 18    // cushions and linings
  static let dark = 19      // the dark inside of the gate, mouths
  static let linen = 20     // veils, wimples, albs
  static let slate = 21     // roofs
  static let mane = 22      // horses' manes and tails
  static let count = 23
}

// Bones of each skeleton. "Left" is the figure's own left (+x when it faces +z).
enum HB {
  static let root = 0  // pelvis
  static let chest = 1, head = 2
  static let lThigh = 3, lShin = 4
  static let rThigh = 5, rShin = 6
  static let lArm = 7, lFore = 8
  static let rArm = 9, rFore = 10
  static let itemR = 11  // held in the right hand (spear, sword, sceptre)
  static let itemL = 12  // held in the left hand (shield, crozier)
  static let count = 13
}
enum KB {
  static let body = 0
  static let neck = 1, head = 2, tail = 3
  static let flu = 4, fll = 5, fru = 6, frl = 7  // front legs: upper (shoulder), lower (knee)
  static let hlu = 8, hll = 9, hru = 10, hrl = 11  // hind legs: upper (hip), lower (hock)
  static let rider = 12, rHead = 13
  static let rArm = 14, rFore = 15, lArm = 16, lFore = 17
  static let lance = 18
  static let count = 19
}
enum CB {
  static let body = 0
  static let bridge = 1  // hinged at the foot of the gate
  static let gate = 2    // portcullis (slides up)
  static let flag = 3
  static let lThigh = 4, lShin = 5, rThigh = 6, rShin = 7  // the stone legs
  static let count = 8
}
enum DB {
  static let body = 0
  static let neck1 = 1, neck2 = 2, neck3 = 3, head = 4, jaw = 5
  static let tail1 = 6, tail2 = 7, tail3 = 8, tail4 = 9
  static let lWing1 = 10, lWing2 = 11, rWing1 = 12, rWing2 = 13
  static let fl = 14, fr = 15, hl = 16, hr = 17
  static let count = 18
}

struct MSkeleton {
  var count = 0
  var parent: [Int] = []
  var joint: [vec3] = []  // pivot in model space (rest pose)
  var rigid: [Bool] = []  // never blended with a neighbour (held items)

  init(_ bones: [(parent: Int, joint: vec3, rigid: Bool)] = []) {
    count = bones.count
    parent = bones.map { $0.parent }
    joint = bones.map { $0.joint }
    rigid = bones.map { $0.rigid }
  }
}

/// What the animations ask of a figure; Medieval.buildPose() turns it into bone matrices.
struct MControls {
  var gait: Float = 0       // walk / gallop phase (radians, advanced with the distance travelled)
  var stride: Float = 0     // 0 standing .. 1 full stride
  var run: Float = 0        // horse: 0 walk .. 1 gallop
  var sit: Float = 1        // throned figures: 1 seated .. 0 standing
  var lean: Float = 0       // torso pitch (+ forward)
  var twist: Float = 0      // torso yaw
  var headYaw: Float = 0, headPitch: Float = 0
  var armR = vec3(0), armL = vec3(0)  // shoulder pitch (+ raises the arm forward), abduction (+ outward), elbow bend
  var itemR: Float = 1, itemL: Float = 1  // held items shown (1) or hidden (0)
  var itemPitch: Float = 0  // extra pitch of the right-hand item (lowering a lance, tilting a sword)
  var itemYaw: Float = 0    // turn of the right-hand item about its grip (the king's sword: -pi/2 points it forward)
  var crouch: Float = 0     // bend the knees
  var rear: Float = 0       // horse: rear up on the hind legs (0..1)
  var tuck: Float = 0       // horse: fold the legs for a jump (0..1)
  var kick: Float = 0       // horse: buck, hind legs out (0..1)
  var bridge: Float = 0     // castle: drawbridge lowered (0..1)
  var gate: Float = 0       // castle: portcullis raised (0..1)
  var legs: Float = 0       // castle: legs out and standing (0..1)
  var lift: Float = 0       // castle: raise one foot for a stomp (-1 left .. 1 right)
  var jaw: Float = 0        // dragon: mouth open (0..1)
  var wings: Float = 0      // dragon: wings spread (0..1)
  var flap: Float = 0       // dragon: wing beat phase (radians)
  var neck: Float = 0       // dragon: rear the neck back (+) or lunge (-)
  var tail: Float = 0       // tail swish phase
  var idle: Float = 0       // breathing / idle phase (radians)
  // the aliens (Alien.swift)
  var tongue: Float = 0     // tongue / inner jaw out (model units)
  var perk: Float = 0       // antennae up (+) or droop (-)
  var hover: Float = 0      // the base floats this much higher than at rest
  var spin: Float = 0       // saucer light ring, throne fans (radians)
  var hatch: Float = 0      // saucer belly hatch / tripod eye opening (0..1)
}

struct MPose {
  var count = 0
  var bone = [mat4](repeating: .identity, count: Medieval.maxBones)
  /// The bones in use (what the renderer uploads).
  var active: [mat4] { count > 0 ? Array(bone[0..<count]) : [] }
}

final class MedievalLibrary: @unchecked Sendable {  // filled on a loader thread, read after it finishes
  var asset = [PieceAssets](repeating: PieceAssets(), count: MAsset.count)
  var built = false
}

// ---------------------------------------------------------------------------
// Joint layouts (the skeletons and the sculpting share them)
// ---------------------------------------------------------------------------
@inline(__always) func mirrorXv(_ v: vec3) -> vec3 { vec3(-v.x, v.y, v.z) }

struct HumanJ {
  var root = vec3(0), waist = vec3(0), neck = vec3(0), hipL = vec3(0), kneeL = vec3(0), ankleL = vec3(0)
  var shL = vec3(0), elL = vec3(0), wrL = vec3(0), shR = vec3(0), elR = vec3(0), wrR = vec3(0)
  var gripR = vec3(0), gripL = vec3(0)
  var S: Float = 1

  /// The man-at-arms (pawn): standing, 0.84 tall.
  static func pawn() -> HumanJ {
    var j = HumanJ()
    j.root = vec3(0, 0.42, 0)
    j.waist = vec3(0, 0.48, 0)
    j.neck = vec3(0, 0.655, 0.005)
    j.hipL = vec3(0.048, 0.40, 0)
    j.kneeL = vec3(0.05, 0.22, 0.008)
    j.ankleL = vec3(0.05, 0.05, -0.004)
    j.shL = vec3(0.098, 0.618, -0.004)
    j.elL = vec3(0.122, 0.478, -0.012)
    j.wrL = vec3(0.118, 0.462, 0.105)
    j.shR = vec3(-0.098, 0.618, -0.004)
    j.elR = vec3(-0.12, 0.476, -0.012)
    j.wrR = vec3(-0.114, 0.46, 0.108)
    j.gripR = vec3(-0.112, 0.462, 0.128)
    j.gripL = vec3(0.122, 0.466, 0.085)
    j.S = 1
    return j
  }

  /// Throned figures, seated (the rest pose). The king is the largest; the queen and
  /// the bishop are the same figure a little smaller, with their own arms.
  static func seated(_ kind: Int) -> HumanJ {
    var j = HumanJ()
    j.root = vec3(0, 0.36, -0.07)
    j.waist = vec3(0, 0.44, -0.08)
    j.neck = vec3(0, 0.80, -0.08)
    j.hipL = vec3(0.068, 0.345, -0.06)
    j.kneeL = vec3(0.075, 0.335, 0.20)
    j.ankleL = vec3(0.08, 0.065, 0.215)
    j.shL = vec3(0.13, 0.755, -0.09)
    j.shR = vec3(-0.13, 0.755, -0.09)
    j.elL = vec3(0.168, 0.555, -0.045)
    j.wrL = vec3(0.14, 0.445, 0.10)
    j.gripL = vec3(0.13, 0.435, 0.14)
    if kind == MAsset.king {
      j.elR = vec3(-0.168, 0.555, -0.045)
      j.wrR = vec3(-0.13, 0.445, 0.105)
      j.gripR = vec3(-0.115, 0.43, 0.142)
    } else if kind == MAsset.queen {  // sceptre held upright
      j.elR = vec3(-0.172, 0.56, -0.05)
      j.wrR = vec3(-0.152, 0.585, 0.085)
      j.gripR = vec3(-0.148, 0.595, 0.112)
    } else {  // bishop: right hand raised in blessing, crozier in the left
      j.elR = vec3(-0.175, 0.565, -0.035)
      j.wrR = vec3(-0.158, 0.745, 0.045)
      j.gripR = vec3(-0.155, 0.79, 0.055)
      j.elL = vec3(0.172, 0.56, -0.035)
      j.wrL = vec3(0.162, 0.5, 0.1)
      j.gripL = vec3(0.16, 0.505, 0.128)
    }
    let S = Medieval.seatedScale(kind)
    j.root *= S; j.waist *= S; j.neck *= S; j.hipL *= S; j.kneeL *= S; j.ankleL *= S
    j.shL *= S; j.elL *= S; j.wrL *= S; j.shR *= S; j.elR *= S; j.wrR *= S; j.gripR *= S; j.gripL *= S
    j.S = S
    return j
  }

  func skeleton() -> MSkeleton {
    MSkeleton([
      (-1, root, false),
      (HB.root, waist, false),
      (HB.chest, neck, false),
      (HB.root, hipL, false),
      (HB.lThigh, kneeL, false),
      (HB.root, mirrorXv(hipL), false),
      (HB.rThigh, mirrorXv(kneeL), false),
      (HB.chest, shL, false),
      (HB.lArm, elL, false),
      (HB.chest, shR, false),
      (HB.rArm, elR, false),
      (HB.rFore, gripR, true),
      (HB.lFore, gripL, true),
    ])
  }
}

/// The knight's horse is sculpted in "horse units" and shrunk to fit a square.
enum KJ {
  static let KS: Float = 0.82
  @inline(__always) static func K(_ x: Float, _ y: Float, _ z: Float) -> vec3 { vec3(x, y, z) * KS }
  static let body = K(0, 0.53, 0), neck = K(0, 0.62, 0.24), head = K(0, 0.88, 0.40), tail = K(0, 0.67, -0.35)
  static let flu = K(0.075, 0.48, 0.22), fll = K(0.075, 0.27, 0.23)
  static let hlu = K(0.08, 0.53, -0.22), hll = K(0.08, 0.27, -0.30)
  static let rider = K(0, 0.78, -0.04), rhead = K(0, 1.03, -0.04)
  static let shR = K(-0.11, 1.0, -0.05), elR = K(-0.15, 0.87, 0.0), wrR = K(-0.13, 0.83, 0.11)
  static let shL = K(0.11, 1.0, -0.05), elL = K(0.15, 0.87, 0.0), wrL = K(0.11, 0.82, 0.10)
  static let gripR = K(-0.13, 0.83, 0.13), gripL = K(0.10, 0.815, 0.12)
}

/// The gatehouse. Its legs are a separate mesh sharing the skeleton; they are
/// sculpted fully out (hips just under the floor of the tower).
enum CJ {
  static let bridge = vec3(0, 0.012, 0.174), gate = vec3(0, 0.0, 0.11), flag = vec3(0.215, 1.2, 0.1)
  static let hipL = vec3(0.115, 0.02, -0.04), kneeL = vec3(0.125, -0.17, 0.0), ankleL = vec3(0.125, -0.35, -0.03)
  static let legH: Float = 0.42  // how far the tower rises when it stands up
}

/// The dragon: body centred over the origin, facing +z, about 1.4 long.
enum DJ {
  static let body = vec3(0, 0.34, 0), neck1 = vec3(0, 0.42, 0.2), neck2 = vec3(0, 0.5, 0.32)
  static let neck3 = vec3(0, 0.6, 0.42), head = vec3(0, 0.67, 0.5), jaw = vec3(0, 0.64, 0.56)
  static let tail1 = vec3(0, 0.33, -0.22), tail2 = vec3(0, 0.27, -0.42), tail3 = vec3(0, 0.2, -0.6)
  static let tail4 = vec3(0, 0.13, -0.76)
  static let wingL1 = vec3(0.1, 0.46, 0.1), wingL2 = vec3(0.38, 0.62, 0.02)
  static let fl = vec3(0.11, 0.3, 0.14), hl = vec3(0.12, 0.3, -0.12)
}

// ---------------------------------------------------------------------------
// Skeletons and rest-pose queries
// ---------------------------------------------------------------------------
enum Medieval {
  static let maxBones = 20

  private static let pawnSkel = HumanJ.pawn().skeleton()
  private static let bishopSkel = HumanJ.seated(MAsset.bishop).skeleton()
  private static let queenSkel = HumanJ.seated(MAsset.queen).skeleton()
  private static let kingSkel = HumanJ.seated(MAsset.king).skeleton()
  private static let knightSkel = MSkeleton([
    (-1, KJ.body, false),
    (KB.body, KJ.neck, false),
    (KB.neck, KJ.head, false),
    (KB.body, KJ.tail, false),
    (KB.body, KJ.flu, false),
    (KB.flu, KJ.fll, false),
    (KB.body, mirrorXv(KJ.flu), false),
    (KB.fru, mirrorXv(KJ.fll), false),
    (KB.body, KJ.hlu, false),
    (KB.hlu, KJ.hll, false),
    (KB.body, mirrorXv(KJ.hlu), false),
    (KB.hru, mirrorXv(KJ.hll), false),
    (KB.body, KJ.rider, false),
    (KB.rider, KJ.rhead, false),
    (KB.rider, KJ.shR, false),
    (KB.rArm, KJ.elR, false),
    (KB.rider, KJ.shL, false),
    (KB.lArm, KJ.elL, false),
    (KB.rFore, KJ.gripR, true),
  ])
  private static let castleSkel = MSkeleton([
    (-1, vec3(0), false),
    (CB.body, CJ.bridge, true),
    (CB.body, CJ.gate, true),
    (CB.body, CJ.flag, true),
    (CB.body, CJ.hipL, false),
    (CB.lThigh, CJ.kneeL, false),
    (CB.body, mirrorXv(CJ.hipL), false),
    (CB.rThigh, mirrorXv(CJ.kneeL), false),
  ])
  private static let dragonSkel = MSkeleton([
    (-1, DJ.body, false),
    (DB.body, DJ.neck1, false),
    (DB.neck1, DJ.neck2, false),
    (DB.neck2, DJ.neck3, false),
    (DB.neck3, DJ.head, false),
    (DB.head, DJ.jaw, false),
    (DB.body, DJ.tail1, false),
    (DB.tail1, DJ.tail2, false),
    (DB.tail2, DJ.tail3, false),
    (DB.tail3, DJ.tail4, false),
    (DB.body, DJ.wingL1, false),
    (DB.lWing1, DJ.wingL2, false),
    (DB.body, mirrorXv(DJ.wingL1), false),
    (DB.rWing1, mirrorXv(DJ.wingL2), false),
    (DB.body, DJ.fl, false),
    (DB.body, mirrorXv(DJ.fl), false),
    (DB.body, DJ.hl, false),
    (DB.body, mirrorXv(DJ.hl), false),
  ])
  private static let noSkel = MSkeleton()

  /// count == 0 for rigid assets.
  static func skeleton(_ asset: Int) -> MSkeleton {
    switch asset {
    case MAsset.pawn: return pawnSkel
    case MAsset.knight: return knightSkel
    case MAsset.bishop: return bishopSkel
    case MAsset.rook, MAsset.legs: return castleSkel
    case MAsset.queen: return queenSkel
    case MAsset.king: return kingSkel
    case MAsset.dragon: return dragonSkel
    default: return noSkel
    }
  }

  static func seatedScale(_ kind: Int) -> Float {
    kind == MAsset.queen ? 0.93 : (kind == MAsset.bishop ? 0.95 : 1.0)
  }

  /// Includes the throne for throned pieces.
  static func pieceHeight(_ kind: Int) -> Float {
    let H: [Float] = [0.86, 1.06, 1.2, 1.1, 1.12, 1.2]
    return H[max(0, min(5, kind))]
  }
  static func pieceRadius(_ kind: Int) -> Float {
    let R: [Float] = [0.2, 0.32, 0.3, 0.36, 0.3, 0.31]
    return R[max(0, min(5, kind))]
  }
  /// Bishop, queen, king.
  static func throned(_ kind: Int) -> Bool { kind == MAsset.bishop || kind == MAsset.queen || kind == MAsset.king }
  /// MAsset.throne / MAsset.cathedra / -1.
  static func seatAsset(_ kind: Int) -> Int {
    if kind == MAsset.bishop { return MAsset.cathedra }
    if kind == MAsset.queen || kind == MAsset.king { return MAsset.throne }
    return -1
  }
  /// The queen's throne is a little smaller than the king's.
  static func seatScale(_ kind: Int) -> Float { kind == MAsset.queen ? seatedScale(MAsset.queen) : 1 }

  // Where held items and important points sit in the rest pose (model space).
  static func handR(_ kind: Int) -> vec3 {
    switch kind {
    case MAsset.pawn: return HumanJ.pawn().gripR
    case MAsset.knight: return KJ.gripR
    case MAsset.bishop, MAsset.queen, MAsset.king: return HumanJ.seated(kind).gripR
    default: return vec3(0, 0.5, 0.1)
    }
  }
  /// Centre of the gate opening (castle model space).
  static func gatePoint() -> vec3 { vec3(0, 0.15, 0.16) }
  /// Top of the throne seat.
  static func seatHeight(_ kind: Int) -> Float { 0.30 * seatedScale(kind) }
  /// How far in front of its throne a standing royal's body is.
  static func standOffset(_ kind: Int) -> Float {
    if !throned(kind) { return 0 }
    let j = HumanJ.seated(kind)
    return j.kneeL.z - j.hipL.z
  }
  /// Distance covered by one full walk / gait cycle.
  static func strideCycle(_ kind: Int) -> Float {
    switch kind {
    case MAsset.pawn: return 0.3
    case MAsset.knight: return 0.62
    case MAsset.rook: return 0.6
    default: return 0.42 * seatedScale(kind)
    }
  }
  /// Rest-pose tip of the right-hand item (spear, lance, sword, sceptre).
  static func itemTip(_ kind: Int) -> vec3 {
    switch kind {
    case MAsset.pawn: return HumanJ.pawn().gripR + vec3(0, 0.6, 0)
    case MAsset.knight: return KJ.gripR + KJ.K(0, 0.81, 0)
    case MAsset.king: return HumanJ.seated(MAsset.king).gripR + vec3(0.48, 0, 0)
    case MAsset.queen: return HumanJ.seated(MAsset.queen).gripR + vec3(0, 0.29, 0) * seatedScale(MAsset.queen)
    default: return handR(kind)
    }
  }

  /// Model-space transform of one bone (rest -> posed); for props held in a hand.
  static func boneMatrix(_ p: MPose, _ bone: Int) -> mat4 {
    bone >= 0 && bone < p.count ? p.bone[bone] : .identity
  }
}
