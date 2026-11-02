// The Roman army: a fourth set of pieces. Legionaries in cloaks with pilum,
// scutum and gladius, a war chariot drawn by two horses, a centurion with
// sword and dagger, a wooden siege tower on wheels, the empress and the
// emperor. Built like the medieval army (painted, skinned SDF figures) and
// posed with the same animation controls.
#pragma once
#include <glm/glm.hpp>

#include "medieval.h"

namespace roman {
using medieval::Controls;
using medieval::kMaxBones;
using medieval::Pose;
using medieval::Skeleton;

// Assets. The first six are the chessmen in meshgen::Kind order.
enum Asset {
  R_PAWN = 0,  // legionary: helmet with a crest, banded armour, cloak, scutum, pilum, gladius
  R_KNIGHT,    // a war chariot drawn by two horses, with its charioteer
  R_BISHOP,    // centurion: transverse crest, mail and medals, gladius and pugio
  R_ROOK,      // wooden siege tower on wheels: battering ram, boarding bridge, scorpion
  R_QUEEN,     // the empress: diadem, stola and palla, a spear (and a hidden dagger)
  R_KING,      // the emperor: laurel wreath, gilded cuirass, purple cloak, eagle sceptre
  R_PILUM,     // a pilum on its own (thrown): butt at the origin, along +y
  R_BOLT,      // a scorpion bolt: butt at the origin, along +y
  R_POOL,      // a pool of blood: a flat disc of radius 1 lying on the board
  R_GOBLET,    // the empress's golden cup (of poisoned wine)
  R_KNIFE,     // the centurion's pugio on its own (thrown): grip at the origin, blade along +y
  R_LION,      // a lion (the emperor's games)
  R_COUNT
};

// Per-vertex materials (integer part of Vertex::mat). Colours depend on the army:
// the white legion wears ivory and crimson over bright steel, the black legion
// black and midnight blue over blackened steel.
enum Mat {
  RM_STEEL = 0,  // armour plates, blades, helmets
  RM_BRONZE,     // helmet fittings, shield bosses, buckles
  RM_GOLD,       // laurels, diadem, eagles, medals
  RM_TUNIC,      // army colour: tunics (ivory / black)
  RM_CLOAK,      // army second colour: cloaks (crimson / midnight blue)
  RM_CREST,      // horsehair crests
  RM_SKIN,
  RM_HAIR,
  RM_LEATHER,    // belts, straps, sandals, harness
  RM_WOOD,       // shafts, the chariot
  RM_TIMBER,     // the siege tower's rough beams
  RM_SHIELD,     // the painted face of a scutum
  RM_HORSE,
  RM_MANE,
  RM_HIDE,       // wet rawhide nailed over the siege tower
  RM_IRON,       // dark iron: tyres, nails, the ram's head
  RM_ROPE,
  RM_PURPLE,     // imperial purple
  RM_LINEN,      // the empress's stola
  RM_BLOOD,
  RM_DARK,       // mouths, doorways, the inside of things
  RM_MAIL,       // the centurion's mail shirt
  RM_FUR,        // lion
  RM_LIONMANE,
  RM_TEETH,      // teeth and claws
  RM_WINE,
  RM_EYE,        // whites of the eyes with a dark iris
  RM_COUNT
};

// Humans (legionary, centurion, empress, emperor). "Left" is the figure's own left
// (+x when it faces +z).
enum HumanBone {
  RH_ROOT = 0,  // pelvis
  RH_CHEST, RH_HEAD,
  RH_LTHIGH, RH_LSHIN, RH_RTHIGH, RH_RSHIN,
  RH_LARM, RH_LFORE, RH_RARM, RH_RFORE,
  RH_ITEMR,   // right hand: pilum, spear, sceptre, the centurion's gladius
  RH_ITEML,   // left hand: scutum, the centurion's pugio, the empress's goblet
  RH_BLADE,   // a blade drawn into the right hand (Controls::draw)
  RH_SHEATH,  // ... and the same blade in its scabbard at the hip until then
  RH_CLOAK, RH_HEM,  // the cloak, from the shoulders, and its hem
  RH_COUNT
};
// The chariot: two horses (sharing one set of leg bones), the car and the charioteer.
enum ChariotBone {
  CH_CAR = 0,
  CH_WHEELL, CH_WHEELR,
  CH_HORSES, CH_NECK, CH_HEAD, CH_TAIL,
  CH_FLU, CH_FLL, CH_FRU, CH_FRL, CH_HLU, CH_HLL, CH_HRU, CH_HRL,
  CH_DRIVER, CH_DHEAD, CH_DRARM, CH_DRFORE, CH_DLARM, CH_DLFORE,
  CH_ITEM,   // the charioteer's spear
  CH_CLOAK,
  CH_COUNT
};
// The siege tower.
enum TowerBone {
  TW_BODY = 0,
  TW_WHEEL1, TW_WHEEL2, TW_WHEEL3, TW_WHEEL4,  // front left, front right, back left, back right
  TW_RAM,       // the battering ram, hung on ropes in the bottom storey
  TW_BRIDGE,    // the boarding bridge, hinged at the top storey
  TW_DOOR,      // the door at the foot: a ramp that drops
  TW_SCORPION,  // the bolt-thrower on the roof (turns and aims)
  TW_LOADED,    // the bolt lying in it, ready to shoot
  TW_FLAG,      // the vexillum
  TW_COUNT
};
// The lion.
enum LionBone {
  LN_BODY = 0, LN_NECK, LN_HEAD, LN_JAW, LN_TAIL1, LN_TAIL2,
  LN_FLU, LN_FLL, LN_FRU, LN_FRL, LN_HLU, LN_HLL, LN_HRU, LN_HRL,
  LN_COUNT
};

const Skeleton& skeleton(int asset);  // count == 0 for rigid assets

// What Controls mean to the Romans, besides walking (gait, stride), leaning,
// turning the head and the arms like the medieval figures:
//   draw    the blade is drawn (0 in its scabbard .. 1 in the right hand)
//   cape    the cloak streams out behind (running, leaping)
//   dead    limbs go limp (a body falling and lying still)
//   tuck    knees drawn up to the chest (a somersault)     kick   a front kick (right leg)
//   chariot: run, rear (the horses), spin (the wheels' roll), rider / riderSpin (the
//            charioteer leaping out of the car), itemPitch (his spear)
//   tower:   spin (wheels), kick (the ram: 1 swung out), bridge (lowered), gate (the
//            door ramp down), itemYaw / itemPitch (the scorpion's aim), hatch (its bolt
//            has been shot: 1 hides the one lying in it)
//   empress: hatch shows her goblet in the left hand
//   lion:    gait, stride, run, jaw, rear (rearing up to pounce), tuck (in the air)
void buildPose(int asset, const Controls& c, Pose& out);

struct Library {
  meshgen::PieceAssets asset[R_COUNT];
  bool built = false;
};
// quality: 0 = fast, 1 = normal, 2 = ultra, 3 = extreme
void buildLibrary(Library& lib, int quality, meshgen::BuildProgress* progress);
float pieceHeight(int kind);
float pieceRadius(int kind);
bool human(int kind);      // legionary, centurion, empress, emperor

// Rest-pose points (model space)
glm::vec3 handR(int kind);       // the right hand's grip
glm::vec3 handL(int kind);       // the left hand's grip
glm::vec3 itemTip(int kind);     // tip of the right-hand item (pilum, spear, sceptre, the charioteer's spear)
glm::vec3 bladeTip(int kind);    // tip of the drawn blade (gladius / dagger) in the rest pose
glm::vec3 headPoint(int kind);   // middle of the head (the charioteer's for the chariot)
glm::vec3 chestPoint(int kind);  // the heart (where blows land)
float strideCycle(int kind);     // distance covered by one walk / gallop cycle
float wheelRadius(int kind);     // chariot and tower wheels
glm::vec3 scorpionPoint();       // the tower's bolt-thrower (pivot), and where its bolt sits
glm::vec3 ramTip();              // the head of the battering ram, swung fully out
glm::vec3 doorPoint();           // the foot of the tower's door
glm::vec3 lionMouth();

}  // namespace roman
