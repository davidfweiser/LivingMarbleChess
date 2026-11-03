// The dinosaurs: a fifth set of pieces. Feathered raptors, pteranodons,
// triceratops, a brachiosaurus for a tower, the spinosaurus queen and the
// tyrannosaurus king. Built like the medieval army and the Romans (painted,
// skinned SDF figures) and posed with the same animation controls; they fight
// like animals - claws, horns, jaws and tails - and the fallen bleed.
#pragma once
#include <glm/glm.hpp>

#include "medieval.h"

namespace dino {
using medieval::Controls;
using medieval::kMaxBones;
using medieval::Pose;
using medieval::Skeleton;

// Assets. The first six are the chessmen in meshgen::Kind order.
enum Asset {
  D_PAWN = 0,  // velociraptor: feathered, with sickle claws
  D_KNIGHT,    // pteranodon: crested, toothless beak, wings of skin
  D_BISHOP,    // triceratops: three horns, a great frill, a parrot's beak
  D_ROOK,      // brachiosaurus: a living tower on four legs
  D_QUEEN,     // spinosaurus: a sail on her back, a crocodile's snout, hooked thumb claws
  D_KING,      // tyrannosaurus rex
  D_POOL,      // a pool of blood: a flat disc of radius 1 lying on the board
  D_METEOR,    // a rock from the sky (checkmate): glowing cracks, radius ~1
  D_COUNT
};

// Per-vertex materials (integer part of Vertex::mat). Colours depend on the army:
// the white army is sand and tan with dark brown markings, the black army slate and
// charcoal with crimson.
enum Mat {
  DM_SKIN = 0,   // scaly hide (countershaded, banded)
  DM_SCUTE,      // bony knobs and scutes along the back
  DM_FEATHER,    // a raptor's body feathers
  DM_QUILL,      // flight feathers on its arms and tail
  DM_HORN,       // horns, the beak (keratin)
  DM_CLAW,       // claws (dark keratin)
  DM_TEETH,
  DM_EYE,        // the iris (amber / gold)
  DM_DARK,       // pupils, nostrils, the throat
  DM_MOUTH,      // gums, the inside of the mouth, the tongue
  DM_FRILL,      // the triceratops' display frill
  DM_SAIL,       // the spinosaurus' sail
  DM_MEMBRANE,   // pteranodon wings
  DM_CREST,      // the pteranodon's crest
  DM_BLOOD,
  DM_ROCK,       // the meteor
  DM_LAVA,       // ... and its glowing cracks
  DM_BELLY,      // pale throat and belly scales
  DM_FUZZ,       // the pteranodon's coat of fine fur (pycnofibres)
  DM_COUNT
};

// Theropods (raptor, spinosaurus, tyrannosaurus). "Left" is the animal's own left
// (+x when it faces +z).
enum TheroBone {
  TH_ROOT = 0,  // the hips
  TH_CHEST, TH_NECK, TH_HEAD, TH_JAW,
  TH_TAIL1, TH_TAIL2, TH_TAIL3,
  TH_LTHIGH, TH_LSHIN, TH_LFOOT, TH_RTHIGH, TH_RSHIN, TH_RFOOT,
  TH_LARM, TH_LFORE, TH_RARM, TH_RFORE,
  TH_COUNT
};
// The pteranodon: each wing is the arm, the forearm, the long hand (metacarpal) it
// walks on, and its long fourth finger; the trailing edge of the membrane behind each
// of them rides on a bone of its own (TRAIL: the forearm's, TRAIL2: the finger's, TRAILA:
// the arm's, TRAILH: the hand's), drawn in against it as the wing folds.
enum PteroBone {
  PT_BODY = 0, PT_NECK, PT_HEAD, PT_JAW,
  PT_LARM, PT_LFORE, PT_LMETA, PT_LFINGER, PT_RARM, PT_RFORE, PT_RMETA, PT_RFINGER,
  PT_LTHIGH, PT_LSHIN, PT_RTHIGH, PT_RSHIN,
  PT_LTRAIL, PT_RTRAIL, PT_LTRAIL2, PT_RTRAIL2,
  PT_LTRAILA, PT_RTRAILA, PT_LTRAILH, PT_RTRAILH,
  PT_COUNT
};
// Four-legged: the triceratops and the brachiosaurus. Each leg is the upper leg, the
// lower leg (forearm, shin) and the foot.
enum QuadBone {
  QD_BODY = 0, QD_NECK1, QD_NECK2, QD_NECK3, QD_HEAD, QD_JAW,
  QD_TAIL1, QD_TAIL2, QD_TAIL3,
  QD_FLU, QD_FLL, QD_FRU, QD_FRL, QD_HLU, QD_HLL, QD_HRU, QD_HRL,
  QD_FLF, QD_FRF, QD_HLF, QD_HRF,
  QD_COUNT
};

const Skeleton& skeleton(int asset);  // count == 0 for rigid assets

// What Controls mean to the dinosaurs, besides walking (gait, stride, run), turning
// the head (headYaw, headPitch) and leaning (lean: + the body pitches nose down):
//   jaw       mouth open (0..1)            neck   + rears the neck back, - lunges
//   twist     the body turns (yaw)         crouch the legs bend, the body sinks
//   armR/armL theropods' arms: reach forward, out, elbow bend (as the humans')
//   itemYaw   the tail swung to one side (radians, spread along it)   itemPitch the tail raised
//   kick      a raptor's killing claw: the right leg kicks forward (0..1); 'tuck' both legs
//             forward, claws out (a pounce), or a pteranodon's feet thrust forward (talons)
//   rear      four-legged: up on the hind legs (0..1); theropods: the body rears up
//   wings     the pteranodon's wings: 0 folded (walking on its hands) .. 1 spread
//   flap      wing beat phase (radians)    hover  the pteranodon in the air: its body level, legs trailed (0..1)
//   dead      limp: a body falling and lying still on its side (0..1)
//   tail      tail swish phase
void buildPose(int asset, const Controls& c, Pose& out);

struct Library {
  meshgen::PieceAssets asset[D_COUNT];
  bool built = false;
};
// quality: 0 = fast, 1 = normal, 2 = ultra, 3 = extreme
void buildLibrary(Library& lib, int quality, meshgen::BuildProgress* progress);
float pieceHeight(int kind);
float pieceRadius(int kind);
bool theropod(int kind);   // raptor, spinosaurus, tyrannosaurus
bool quadruped(int kind);  // triceratops, brachiosaurus

// Rest-pose points (model space)
glm::vec3 headPoint(int kind);   // the middle of the head
glm::vec3 mouthPoint(int kind);  // between the jaws (on the head bone), the beak tip for the pteranodon
glm::vec3 chestPoint(int kind);  // the heart (where blows land)
glm::vec3 clawPoint(int kind);   // the raptor's sickle claw (right foot), the spinosaurus' right thumb claw,
                                 // the pteranodon's right foot
glm::vec3 hornPoint(int kind);   // the triceratops' brow horns (between their tips)
glm::vec3 tailTip(int kind);     // the end of the tail (on the last tail bone)
float strideCycle(int kind);     // distance covered by one walk cycle
// The mouths are sculpted agape, the lower jaw turned down about its hinge by this much
// (radians), so that the jaws are separate surfaces; the pose closes them.
float gape(int kind);
float bodyLength(int kind);      // from the snout to the tail tip (rest pose)
float bodyHalfWidth(int kind);   // half the width of the body (how far a flank is from the middle)
glm::vec3 wingTip();             // the pteranodon's left wing tip, wings spread (rest pose)
// Rest-pose points that stand on the board (feet, and the pteranodon's hands) and their bones;
// the pose keeps the lowest of them on the board. Returns how many.
int groundPoints(int asset, glm::vec3* pts, int* bones);

}  // namespace dino
