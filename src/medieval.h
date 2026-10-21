// The medieval army: a second set of pieces made of painted, articulated
// figures (men-at-arms, knights on horseback, bishops and royals on their
// thrones, a gatehouse with a drawbridge - and the dragon that lives in it).
// Like the marble set everything is sculpted from signed distance fields at
// startup; on top of that every figure is skinned to a small skeleton so it
// can walk, stand up, swing a sword or spread its wings.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "meshgen.h"

namespace medieval {

// Assets. The first six are the chessmen in meshgen::Kind order.
enum Asset {
  A_PAWN = 0,   // man-at-arms: kettle hat, gambeson, tabard, kite shield, spear
  A_KNIGHT,     // knight in a great helm on a caparisoned horse, with a lance
  A_BISHOP,     // bishop in mitre and cope with his crozier (sits on A_CATHEDRA)
  A_ROOK,       // gatehouse tower with battlements, portcullis and drawbridge
  A_QUEEN,      // queen in crown and veil with a sceptre (sits on A_THRONE)
  A_KING,       // bearded king with crown, ermine mantle and sword (sits on A_THRONE)
  A_THRONE,     // royal throne (king and queen)
  A_CATHEDRA,   // the bishop's gothic chair
  A_LEGS,       // the gatehouse's stone legs (drawn only while they are out)
  A_DRAGON,     // the dragon that lives in the gatehouse
  A_SPEAR,      // a man-at-arms' spear on its own (thrown as a javelin), butt at the origin along +y
  A_COUNT
};

// Per-vertex materials (integer part of Vertex::mat). Colours depend on the army.
enum Mat {
  MT_STONE = 0,  // castle masonry (pale limestone / dark basalt)
  MT_WOOD,       // thrones, shafts, drawbridge planks
  MT_STEEL,      // plate armour, helmets, blades (bright / blackened)
  MT_GOLD,       // crowns, trims, crozier, finials
  MT_CLOTH,      // army colour: tabards, robes, gowns, caparisons
  MT_CLOTH2,     // heraldic second colour (azure / gules)
  MT_SKIN,
  MT_HAIR,       // hair and beards
  MT_HORSE,      // horse coat (grey / black)
  MT_LEATHER,    // belts, boots, saddle, reins
  MT_FUR,        // ermine
  MT_GEM,        // rubies and sapphires
  MT_MAIL,       // chain mail
  MT_IRON,       // wrought iron: portcullis, hinges, chains, bands
  MT_SCALE,      // dragon hide
  MT_MEMBRANE,   // dragon wings
  MT_HORN,       // horns, teeth, claws
  MT_EYE,        // glowing dragon eyes
  MT_VELVET,     // cushions and linings
  MT_DARK,       // the dark inside of the gate, mouths
  MT_LINEN,      // veils, wimples, albs
  MT_SLATE,      // roofs
  MT_MANE,       // horses' manes and tails
  MT_COUNT
};

constexpr int kMaxBones = 20;

// Bones of each skeleton. "Left" is the figure's own left (+x when it faces +z).
enum HumanBone {
  HB_ROOT = 0,  // pelvis
  HB_CHEST,
  HB_HEAD,
  HB_LTHIGH, HB_LSHIN,
  HB_RTHIGH, HB_RSHIN,
  HB_LARM, HB_LFORE,
  HB_RARM, HB_RFORE,
  HB_ITEMR,     // held in the right hand (spear, sword, sceptre)
  HB_ITEML,     // held in the left hand (shield, crozier)
  HB_COUNT
};
enum HorseBone {
  KB_BODY = 0,
  KB_NECK, KB_HEAD, KB_TAIL,
  KB_FLU, KB_FLL, KB_FRU, KB_FRL,  // front legs: upper (shoulder), lower (knee)
  KB_HLU, KB_HLL, KB_HRU, KB_HRL,  // hind legs: upper (hip), lower (hock)
  KB_RIDER, KB_RHEAD,
  KB_RARM, KB_RFORE, KB_LARM, KB_LFORE,
  KB_LANCE,
  KB_COUNT
};
enum CastleBone {
  CB_BODY = 0,
  CB_BRIDGE,    // hinged at the foot of the gate
  CB_GATE,      // portcullis (slides up)
  CB_FLAG,
  CB_LTHIGH, CB_LSHIN, CB_RTHIGH, CB_RSHIN,  // the stone legs
  CB_COUNT
};
enum DragonBone {
  DB_BODY = 0,
  DB_NECK1, DB_NECK2, DB_NECK3, DB_HEAD, DB_JAW,
  DB_TAIL1, DB_TAIL2, DB_TAIL3, DB_TAIL4,
  DB_LWING1, DB_LWING2, DB_RWING1, DB_RWING2,
  DB_FL, DB_FR, DB_HL, DB_HR,
  DB_COUNT
};

struct Skeleton {
  int count = 0;
  int parent[kMaxBones];
  glm::vec3 joint[kMaxBones];  // pivot in model space (rest pose)
  bool rigid[kMaxBones];       // never blended with a neighbour (held items)
};
const Skeleton& skeleton(int asset);  // count == 0 for rigid assets

// What the animations ask of a figure; buildPose() turns it into bone matrices.
struct Controls {
  float gait = 0;          // walk / gallop phase (radians, advanced with the distance travelled)
  float stride = 0;        // 0 standing .. 1 full stride
  float run = 0;           // horse: 0 walk .. 1 gallop
  float sit = 1;           // throned figures: 1 seated .. 0 standing
  float lean = 0;          // torso pitch (+ forward)
  float twist = 0;         // torso yaw
  float headYaw = 0, headPitch = 0;
  glm::vec3 armR{0}, armL{0};  // shoulder pitch (+ raises the arm forward), abduction (+ outward), elbow bend
  float itemR = 1, itemL = 1;  // held items shown (1) or hidden (0)
  float itemPitch = 0;     // extra pitch of the right-hand item (lowering a lance, tilting a sword)
  float itemYaw = 0;       // turn of the right-hand item about its grip (the king's sword: -pi/2 points it forward)
  float crouch = 0;        // bend the knees
  float rear = 0;          // horse: rear up on the hind legs (0..1)
  float tuck = 0;          // horse: fold the legs for a jump (0..1)
  float kick = 0;          // horse: buck, hind legs out (0..1)
  float bridge = 0;        // castle: drawbridge lowered (0..1)
  float gate = 0;          // castle: portcullis raised (0..1)
  float legs = 0;          // castle: legs out and standing (0..1)
  float lift = 0;          // castle: raise one foot for a stomp (-1 left .. 1 right)
  float jaw = 0;           // dragon: mouth open (0..1)
  float wings = 0;         // dragon: wings spread (0..1)
  float flap = 0;          // dragon: wing beat phase (radians)
  float neck = 0;          // dragon: rear the neck back (+) or lunge (-)
  float tail = 0;          // tail swish phase
  float idle = 0;          // breathing / idle phase (radians)
  // the aliens (alien.h)
  float tongue = 0;        // tongue / inner jaw out (model units)
  float perk = 0;          // antennae up (+) or droop (-)
  float hover = 0;         // the base floats this much higher than at rest
  float spin = 0;          // saucer light ring, throne fans (radians)
  float hatch = 0;         // saucer belly hatch / tripod eye opening (0..1)
};

struct Pose {
  int count = 0;
  glm::mat4 bone[kMaxBones];
};
void buildPose(int asset, const Controls& c, Pose& out);
// Model-space transform of one bone (rest -> posed); for props held in a hand.
glm::mat4 boneMatrix(const Pose& p, int bone);

struct Library {
  meshgen::PieceAssets asset[A_COUNT];
  bool built = false;
};
// quality: 0 = fast, 1 = normal, 2 = ultra
void buildLibrary(Library& lib, int quality, meshgen::BuildProgress* progress);
float pieceHeight(int kind);  // includes the throne for throned pieces
float pieceRadius(int kind);
bool throned(int kind);       // bishop, queen, king
int seatAsset(int kind);      // A_THRONE / A_CATHEDRA / -1
float seatScale(int kind);    // the queen's throne is a little smaller than the king's

// Where held items and important points sit in the rest pose (model space).
glm::vec3 handR(int kind);
glm::vec3 gatePoint();        // centre of the gate opening (castle model space)
float seatHeight(int kind);   // top of the throne seat
float standOffset(int kind);  // how far in front of its throne a standing royal's body is
float strideCycle(int kind);  // distance covered by one full walk / gait cycle
glm::vec3 itemTip(int kind);  // rest-pose tip of the right-hand item (spear, lance, sword, sceptre)

}  // namespace medieval
