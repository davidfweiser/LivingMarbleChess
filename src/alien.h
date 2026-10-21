// The alien army: a third set of pieces. Little green men with ray guns, a
// flying saucer, a big-brained psychic in his jar, a tripod war machine, a
// crested alien queen and a pudgy overlord on his hover throne. Built like
// the medieval army (painted, skinned SDF figures) and posed with the same
// animation controls.
#pragma once
#include <glm/glm.hpp>

#include "medieval.h"

namespace alien {
using medieval::Controls;
using medieval::kMaxBones;
using medieval::Pose;
using medieval::Skeleton;

// Assets. The first six are the chessmen in meshgen::Kind order.
enum Asset {
  AL_PAWN = 0,   // little green man: big head, antennae, jumpsuit, ray gun
  AL_KNIGHT,     // flying saucer with a little pilot (canopy: AL_CANOPY)
  AL_BISHOP,     // big-brained psychic floating on a disc (his brain jar: AL_JAR)
  AL_ROOK,       // tripod war machine with a heat-ray eye and two tentacles
  AL_QUEEN,      // tall crested alien queen with a tail and a sceptre
  AL_KING,       // pudgy overlord with a cape on his hover throne
  AL_CANOPY,     // the saucer's glass canopy (glass, skinned like the saucer)
  AL_JAR,        // the psychic's glass brain jar (glass, skinned like the psychic)
  AL_COW,        // a cow (for dropping on people)
  AL_MOTHERSHIP, // the mothership
  AL_CRITTER,    // one of the queen's hatchlings
  AL_EGG,        // the queen's egg
  AL_GOO,        // a blob of slime (thrown by the little green men)
  AL_COUNT
};

// Per-vertex materials (integer part of Vertex::mat). Colours depend on the army:
// the white army is green-skinned in silver and pearl with cyan lights, the black
// army violet-skinned in black and gunmetal with magenta lights.
enum Mat {
  AM_SKIN = 0,  // wet, slightly translucent alien skin
  AM_SKIN2,     // paler markings: faces, bellies, spots
  AM_EYE,       // big glossy black eyes
  AM_SUIT,      // jumpsuits
  AM_CHROME,    // polished metal
  AM_HULL,      // painted hull plating with panel lines
  AM_LIGHT,     // running lights (glowing, chasing)
  AM_BRAIN,     // a big pink brain
  AM_GOLD,      // regalia
  AM_ROBE,      // the overlord's cape, the queen's gown
  AM_GEM,       // glowing crystals
  AM_SLIME,     // green goo
  AM_RUBBER,    // hoses, seals, tyres
  AM_HIDE,      // cowhide
  AM_PINK,      // tongues, gums, cow noses and udders
  AM_TEETH,
  AM_DARK,      // mouths, vents, the dark inside of things
  AM_SHELL,     // the queen's glossy carapace
  AM_ENERGY,    // plasma coils and emitters (glowing)
  AM_HORN,      // horns, hooves, claws
  AM_EGG,       // leathery egg
  AM_COUNT
};

// Humanoid aliens (pawn, bishop, queen, king) share one bone layout. The base is
// what they stand on: the ground, the psychic's floating disc, the overlord's throne.
// "Left" is the figure's own left (+x when it faces +z).
enum HumanoidBone {
  AH_BASE = 0,
  AH_PELVIS, AH_CHEST, AH_HEAD,
  AH_LTHIGH, AH_LSHIN, AH_RTHIGH, AH_RSHIN,
  AH_LARM, AH_LFORE, AH_RARM, AH_RFORE,
  AH_ITEMR, AH_ITEML,
  AH_ANTL, AH_ANTR,  // antennae
  AH_JAW,            // tongue (pawn), inner jaw (queen)
  AH_TAIL1, AH_TAIL2, AH_TAIL3,
  AH_COUNT
};
// The flying saucer.
enum SaucerBone {
  UB_BODY = 0,
  UB_RIM,            // the light ring (spins)
  UB_PILOT, UB_HEAD, UB_LARM, UB_RARM,
  UB_HATCH,          // the belly emitter (opens for the tractor beam)
  UB_LEG1, UB_LEG2, UB_LEG3,
  UB_ANTL, UB_ANTR,
  UB_COUNT
};
// The tripod.
enum TripodBone {
  TB_BODY = 0,
  TB_HOOD,           // the heat-ray eye (turns, opens)
  TB_L1U, TB_L1L, TB_L2U, TB_L2L, TB_L3U, TB_L3L,
  TB_TL1, TB_TL2, TB_TL3, TB_TR1, TB_TR2, TB_TR3,  // tentacles
  TB_COUNT
};
// The cow.
enum CowBone { CW_BODY = 0, CW_HEAD, CW_FL, CW_FR, CW_HL, CW_HR, CW_TAIL, CW_COUNT };

const Skeleton& skeleton(int asset);  // count == 0 for rigid assets

// What Controls mean to the aliens, besides walking (gait, stride), leaning,
// turning the head and the arms like the medieval figures:
//   jaw      mouth open (0..1)        tongue   tongue / inner jaw out (model units)
//   perk     antennae up (+) or droop (-)
//   hover    the base floats this high (bishop's disc, king's throne, saucer)
//   spin     the saucer's light ring, the overlord throne's fans (radians)
//   hatch    saucer belly / tripod eye opening (0..1)
//   legs     saucer landing legs out / tripod standing tall (0..1)
//   tail     tail swish phase          crouch, lift (tripod: raise one foot)
//   wings    the cow's legs flailing (0..1)      kick     the queen's tail lashing sideways
void buildPose(int asset, const Controls& c, Pose& out);

struct Library {
  meshgen::PieceAssets asset[AL_COUNT];
  bool built = false;
};
// quality: 0 = fast, 1 = normal, 2 = ultra
void buildLibrary(Library& lib, int quality, meshgen::BuildProgress* progress);
float pieceHeight(int kind);
float pieceRadius(int kind);
int glassAsset(int kind);  // AL_CANOPY / AL_JAR / -1

// Rest-pose points (model space)
glm::vec3 handR(int kind);       // right hand (ray gun, sceptre)
glm::vec3 muzzle(int kind);      // where its weapon fires from (ray gun, eye, sceptre, mouth)
glm::vec3 headPoint(int kind);   // middle of the head (brain for the psychic)
float strideCycle(int kind);     // distance covered by one walk cycle
float hoverHeight(int kind);     // resting float height of the base (0 for walkers)

}  // namespace alien
