// Sculpting the dinosaurs. Like the medieval army and the Romans, every animal
// is a list of parts - ellipsoids for muscle and belly, tapering cones for
// necks, tails and limbs, curved horns and claws, thin sheets for wings, frills
// and sails - each bound to a bone and a material, blended into one signed
// distance field and meshed; every vertex is then skinned to the bone(s) of the
// part(s) it lies on and painted with their material. The scales, feathers and
// skin are drawn by the shader.
//
// Each animal is sculpted in its own units and scaled afterwards (the skeleton
// with it). All of them face +z with their feet on y = 0.
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include "dino.h"
#include "sculpt.h"

namespace dino {
using glm::mat3;
using glm::vec2;
using glm::vec3;
using namespace sdf;
using namespace sculpt;

namespace {

// ---------------------------------------------------------------------------
// Joints
// ---------------------------------------------------------------------------
struct TheroJ {
  vec3 root, chest, neck, head, jaw, tail1, tail2, tail3, tip;
  vec3 hipL, kneeL, ankleL, ballL;
  vec3 shL, elL, wrL;
  float S = 1;
};

float animalScale(int kind) {
  switch (kind) {
    case D_KNIGHT: return 0.95f;
    case D_BISHOP: return 0.85f;
    case D_QUEEN: return 0.82f;
    case D_KING: return 0.85f;
    default: return 1.0f;
  }
}
// ... and moved back a little, so that two animals facing each other (or one behind
// the other) on neighbouring squares don't run into each other
float animalShift(int kind) {
  switch (kind) {
    case D_KNIGHT: return -0.03f;
    case D_BISHOP: return -0.05f;
    case D_ROOK: return -0.05f;
    case D_QUEEN: return -0.08f;
    case D_KING: return -0.04f;
    default: return 0.0f;
  }
}
// an animal's own units -> model space
vec3 toModel(int kind, vec3 v) { return v * animalScale(kind) + vec3(0, 0, animalShift(kind)); }

// in the animal's own units
TheroJ theroBase(int kind) {
  TheroJ j;
  switch (kind) {
    case D_PAWN:  // a velociraptor: light, long-legged, a stiff tail held out straight
      j.root = vec3(0, 0.27f, -0.02f);
      j.chest = vec3(0, 0.28f, 0.06f);
      j.neck = vec3(0, 0.3f, 0.14f);
      j.head = vec3(0, 0.42f, 0.2f);
      j.jaw = vec3(0, 0.41f, 0.215f);
      j.tail1 = vec3(0, 0.28f, -0.1f);
      j.tail2 = vec3(0, 0.282f, -0.22f);
      j.tail3 = vec3(0, 0.28f, -0.33f);
      j.tip = vec3(0, 0.276f, -0.46f);
      j.hipL = vec3(0.042f, 0.262f, -0.02f);
      j.kneeL = vec3(0.048f, 0.175f, 0.03f);
      j.ankleL = vec3(0.048f, 0.085f, -0.035f);
      j.ballL = vec3(0.048f, 0.012f, 0.0f);
      j.shL = vec3(0.036f, 0.262f, 0.12f);
      j.elL = vec3(0.056f, 0.215f, 0.1f);
      j.wrL = vec3(0.058f, 0.205f, 0.165f);
      break;
    case D_QUEEN:  // a spinosaurus: short legs, a long body under a sail, a long neck and snout
      j.root = vec3(0, 0.44f, 0);
      j.chest = vec3(0, 0.47f, 0.12f);
      j.neck = vec3(0, 0.5f, 0.29f);
      j.head = vec3(0, 0.62f, 0.43f);
      j.jaw = vec3(0, 0.605f, 0.45f);
      j.tail1 = vec3(0, 0.46f, -0.15f);
      j.tail2 = vec3(0, 0.44f, -0.34f);
      j.tail3 = vec3(0, 0.41f, -0.52f);
      j.tip = vec3(0, 0.37f, -0.7f);
      j.hipL = vec3(0.058f, 0.43f, 0);
      j.kneeL = vec3(0.066f, 0.29f, 0.07f);
      j.ankleL = vec3(0.066f, 0.13f, -0.02f);
      j.ballL = vec3(0.066f, 0.016f, 0.03f);
      j.shL = vec3(0.06f, 0.43f, 0.27f);
      j.elL = vec3(0.085f, 0.34f, 0.29f);
      j.wrL = vec3(0.08f, 0.31f, 0.37f);
      break;
    default:  // the tyrannosaurus: a huge head, massive legs, tiny arms
      j.root = vec3(0, 0.5f, 0);
      j.chest = vec3(0, 0.52f, 0.1f);
      j.neck = vec3(0, 0.56f, 0.29f);
      j.head = vec3(0, 0.69f, 0.4f);
      j.jaw = vec3(0, 0.665f, 0.43f);
      j.tail1 = vec3(0, 0.52f, -0.16f);
      j.tail2 = vec3(0, 0.5f, -0.36f);
      j.tail3 = vec3(0, 0.46f, -0.53f);
      j.tip = vec3(0, 0.41f, -0.68f);
      j.hipL = vec3(0.075f, 0.48f, -0.01f);
      j.kneeL = vec3(0.085f, 0.31f, 0.075f);
      j.ankleL = vec3(0.085f, 0.14f, -0.045f);
      j.ballL = vec3(0.085f, 0.018f, 0.03f);
      j.shL = vec3(0.08f, 0.45f, 0.27f);
      j.elL = vec3(0.1f, 0.4f, 0.285f);
      j.wrL = vec3(0.095f, 0.385f, 0.33f);
      break;
  }
  return j;
}

TheroJ theroJ(int kind) {
  TheroJ j = theroBase(kind);
  for (vec3* v : {&j.root, &j.chest, &j.neck, &j.head, &j.jaw, &j.tail1, &j.tail2, &j.tail3, &j.tip, &j.hipL, &j.kneeL,
                  &j.ankleL, &j.ballL, &j.shL, &j.elL, &j.wrL})
    *v = toModel(kind, *v);
  j.S = animalScale(kind);
  return j;
}

Skeleton theroSkel(const TheroJ& j) {
  return makeSkeleton({
      {-1, j.root, false},              // ROOT
      {TH_ROOT, j.chest, false},        // CHEST
      {TH_CHEST, j.neck, false},        // NECK
      {TH_NECK, j.head, false},         // HEAD
      {TH_HEAD, j.jaw, false},          // JAW
      {TH_ROOT, j.tail1, false},        // TAIL1
      {TH_TAIL1, j.tail2, false},
      {TH_TAIL2, j.tail3, false},
      {TH_ROOT, j.hipL, false},         // LTHIGH
      {TH_LTHIGH, j.kneeL, false},      // LSHIN
      {TH_LSHIN, j.ankleL, false},      // LFOOT
      {TH_ROOT, mx(j.hipL), false},     // RTHIGH
      {TH_RTHIGH, mx(j.kneeL), false},
      {TH_RSHIN, mx(j.ankleL), false},
      {TH_CHEST, j.shL, false},         // LARM
      {TH_LARM, j.elL, false},          // LFORE
      {TH_CHEST, mx(j.shL), false},     // RARM
      {TH_RARM, mx(j.elL), false},
  });
}

// The pteranodon, standing on its feet with its wings spread (they fold in the pose).
namespace pj {
const vec3 body(0, 0.27f, -0.02f), neck(0, 0.37f, 0.08f), head(0, 0.49f, 0.16f), jaw(0, 0.485f, 0.2f);
const vec3 shL(0.045f, 0.35f, 0.07f), elL(0.14f, 0.36f, 0.06f), wrL(0.27f, 0.37f, 0.04f), knL(0.42f, 0.37f, 0.02f),
    tipL(0.84f, 0.36f, -0.06f);
const vec3 hipL(0.035f, 0.24f, -0.08f), kneeL(0.045f, 0.13f, -0.04f), footL(0.045f, 0.012f, -0.07f);
}  // namespace pj

// The four-legged: joints of the triceratops (in its own units) and the brachiosaurus.
struct QuadJ {
  vec3 body, neck1, neck2, neck3, head, jaw, tail1, tail2, tail3, tip;
  vec3 flu, fll, ffoot, hlu, hll, hfoot;
};
QuadJ quadBase(int kind) {
  QuadJ j;
  if (kind == D_BISHOP) {
    j.body = vec3(0, 0.33f, -0.02f);
    j.neck1 = vec3(0, 0.33f, 0.2f);
    j.neck2 = vec3(0, 0.33f, 0.235f);
    j.neck3 = vec3(0, 0.33f, 0.265f);
    j.head = vec3(0, 0.33f, 0.28f);
    j.jaw = vec3(0, 0.25f, 0.36f);
    j.tail1 = vec3(0, 0.36f, -0.3f);
    j.tail2 = vec3(0, 0.33f, -0.45f);
    j.tail3 = vec3(0, 0.27f, -0.56f);
    j.tip = vec3(0, 0.19f, -0.65f);
    j.flu = vec3(0.1f, 0.3f, 0.15f);
    j.fll = vec3(0.12f, 0.16f, 0.16f);
    j.ffoot = vec3(0.115f, 0.0f, 0.17f);
    j.hlu = vec3(0.1f, 0.36f, -0.2f);
    j.hll = vec3(0.105f, 0.18f, -0.15f);
    j.hfoot = vec3(0.105f, 0.0f, -0.18f);
  } else {
    j.body = vec3(0, 0.52f, 0);
    j.neck1 = vec3(0, 0.66f, 0.22f);
    j.neck2 = vec3(0, 0.86f, 0.3f);
    j.neck3 = vec3(0, 1.03f, 0.37f);
    j.head = vec3(0, 1.19f, 0.43f);
    j.jaw = vec3(0, 1.185f, 0.47f);
    j.tail1 = vec3(0, 0.55f, -0.24f);
    j.tail2 = vec3(0, 0.47f, -0.42f);
    j.tail3 = vec3(0, 0.37f, -0.56f);
    j.tip = vec3(0, 0.25f, -0.7f);
    j.flu = vec3(0.1f, 0.58f, 0.16f);
    j.fll = vec3(0.105f, 0.3f, 0.16f);
    j.ffoot = vec3(0.105f, 0.0f, 0.17f);
    j.hlu = vec3(0.1f, 0.53f, -0.16f);
    j.hll = vec3(0.105f, 0.26f, -0.14f);
    j.hfoot = vec3(0.105f, 0.0f, -0.17f);
  }
  return j;
}
QuadJ quadJ(int kind) {
  QuadJ j = quadBase(kind);
  for (vec3* v : {&j.body, &j.neck1, &j.neck2, &j.neck3, &j.head, &j.jaw, &j.tail1, &j.tail2, &j.tail3, &j.tip, &j.flu,
                  &j.fll, &j.ffoot, &j.hlu, &j.hll, &j.hfoot})
    *v = toModel(kind, *v);
  return j;
}
Skeleton quadSkel(const QuadJ& j) {
  return makeSkeleton({
      {-1, j.body, false},           // BODY
      {QD_BODY, j.neck1, false},     // NECK1
      {QD_NECK1, j.neck2, false},
      {QD_NECK2, j.neck3, false},
      {QD_NECK3, j.head, false},     // HEAD
      {QD_HEAD, j.jaw, false},       // JAW
      {QD_BODY, j.tail1, false},     // TAIL1
      {QD_TAIL1, j.tail2, false},
      {QD_TAIL2, j.tail3, false},
      {QD_BODY, j.flu, false},       // FLU
      {QD_FLU, j.fll, false},
      {QD_BODY, mx(j.flu), false},   // FRU
      {QD_FRU, mx(j.fll), false},
      {QD_BODY, j.hlu, false},       // HLU
      {QD_HLU, j.hll, false},
      {QD_BODY, mx(j.hlu), false},   // HRU
      {QD_HRU, mx(j.hll), false},
  });
}

}  // namespace

// ===========================================================================
// Skeletons
// ===========================================================================
const Skeleton& skeleton(int asset) {
  static const Skeleton pawn = theroSkel(theroJ(D_PAWN));
  static const Skeleton queen = theroSkel(theroJ(D_QUEEN));
  static const Skeleton king = theroSkel(theroJ(D_KING));
  static const Skeleton bishop = quadSkel(quadJ(D_BISHOP));
  static const Skeleton rook = quadSkel(quadJ(D_ROOK));
  static const Skeleton ptero = [] {
    using namespace pj;
    auto P = [](vec3 v) { return toModel(D_KNIGHT, v); };
    return makeSkeleton({
        {-1, P(body), false},             // BODY
        {PT_BODY, P(neck), false},        // NECK
        {PT_NECK, P(head), false},        // HEAD
        {PT_HEAD, P(jaw), false},         // JAW
        {PT_BODY, P(shL), false},         // LARM
        {PT_LARM, P(elL), false},         // LFORE
        {PT_LFORE, P(wrL), false},        // LMETA (the hand)
        {PT_LMETA, P(knL), false},        // LFINGER (the wing finger)
        {PT_BODY, P(mx(shL)), false},     // RARM
        {PT_RARM, P(mx(elL)), false},
        {PT_RFORE, P(mx(wrL)), false},
        {PT_RMETA, P(mx(knL)), false},
        {PT_BODY, P(hipL), false},        // LTHIGH
        {PT_LTHIGH, P(kneeL), false},     // LSHIN
        {PT_BODY, P(mx(hipL)), false},    // RTHIGH
        {PT_RTHIGH, P(mx(kneeL)), false},
        {PT_LFORE, P(elL), false},        // LTRAIL: the inner membrane's trailing edge
        {PT_RFORE, P(mx(elL)), false},
        {PT_LFINGER, P(knL), false},      // LTRAIL2: the outer membrane's
        {PT_RFINGER, P(mx(knL)), false},
    });
  }();
  static const Skeleton none = makeSkeleton({});
  switch (asset) {
    case D_PAWN: return pawn;
    case D_KNIGHT: return ptero;
    case D_BISHOP: return bishop;
    case D_ROOK: return rook;
    case D_QUEEN: return queen;
    case D_KING: return king;
    default: return none;
  }
}

namespace {

// ---------------------------------------------------------------------------
// Shared parts
// ---------------------------------------------------------------------------
// A tapering horn or claw along a circular arc: it starts out along 'dir' and bends
// towards 'down' by 'curl' radians in all.
void addClaw(Model& m, int bone, vec3 base, vec3 dir, vec3 down, float len, float r, float curl, int mat = DM_CLAW,
             float k = 0.0f) {
  const int N = 4;
  vec3 p = base, d = glm::normalize(dir);
  vec3 ax = glm::cross(d, down);  // a fixed axis: a true arc, which may curl on past 'down'
  ax = glm::length(ax) > 1e-5f ? glm::normalize(ax) : vec3(1, 0, 0);
  glm::quat step = glm::angleAxis(curl / N, ax);
  for (int i = 0; i < N; ++i) {
    d = glm::normalize(step * d);
    vec3 q = p + d * (len / N);
    float r0 = r * (1.0f - 0.85f * i / N), r1 = r * (1.0f - 0.85f * (i + 1) / N);
    m.add(bone, mat, i == 0 ? k : 0.0f, coneS(p, q, r0, r1));
    p = q;
  }
}

// An eye looking along 'out': a glossy iris with a slit pupil, under a ridge of brow.
void addEye(Model& m, int bone, vec3 c, float r, vec3 out) {
  out = glm::normalize(out);
  m.sub(DM_SKIN, r * 0.4f, sphereS(c + out * r * 0.5f, r * 1.05f));
  m.add(bone, DM_EYE, r * 0.15f, sphereS(c, r));
  mat3 F = frameY(vec3(0, 1, 0), out);
  m.add(bone, DM_DARK, 0.0f, ellS(c + out * r * 0.82f, vec3(r * 0.22f, r * 0.62f, r * 0.24f), F));
  m.add(bone, DM_SKIN, r * 0.5f, ellS(c + vec3(0, r * 0.95f, 0) - out * r * 0.1f, vec3(r * 1.25f, r * 0.45f, r * 1.2f), F));
}

// A row of teeth along the jaw from a to b (both sides of the mouth: x and -x), pointing 'dir'.
void addTeeth(Model& m, int bone, vec3 a, vec3 b, int n, vec3 dir, float len, float r) {
  for (int i = 0; i < n; ++i) {
    float u = n > 1 ? (float)i / (n - 1) : 0.0f;
    vec3 p = glm::mix(a, b, u);
    float s = 1.0f - 0.35f * std::fabs(u - 0.35f);
    for (int sd = -1; sd <= 1; sd += 2) {
      vec3 q(p.x * sd, p.y, p.z);
      m.add(bone, DM_TEETH, 0.0f, coneS(q, q + dir * len * s, r * s, r * 0.15f));
    }
  }
}

// A tube through a polyline: tapering cones, each bound to its bone.
void addTube(Model& m, int mat, std::initializer_list<vec3> pts, std::initializer_list<float> radii,
             std::initializer_list<int> bones, float k) {
  const vec3* P = pts.begin();
  const float* R = radii.begin();
  const int* B = bones.begin();
  for (size_t i = 0; i + 1 < pts.size(); ++i) m.add(B[i], mat, k, coneS(P[i], P[i + 1], R[i], R[i + 1]));
}

// A thin sheet stretched over a triangle (a wing, a frill, a feather vane).
Shape sheetS(vec3 a, vec3 b, vec3 c, float t) { return triS(a, b, c, t); }

// The mouth line: the inside of the lips painted where the jaws meet (y = level),
// between z0 and z1, over the skin.
void addMouthLine(Model& m, float level, float z0, float z1, float halfW) {
  m.paint(DM_MOUTH, fnS([level, z0, z1, halfW](const vec3& p) {
            float d = std::fabs(p.y - level) - 0.004f;
            return std::max(std::max(d, std::fabs(p.x) - halfW), std::max(z0 - p.z, p.z - z1));
          }, vec3(0, level, (z0 + z1) * 0.5f), (z1 - z0) * 0.6f + halfW), DM_SKIN);
}

// A row of knobbly scutes along the back (y on the midline, from z0 to z1)
void addScutes(Model& m, int bone, vec3 a, vec3 b, int n, float r) {
  for (int i = 0; i < n; ++i) {
    vec3 p = glm::mix(a, b, (n > 1 ? (float)i / (n - 1) : 0.0f));
    m.add(bone, DM_SCUTE, r * 0.8f, ellS(p, vec3(r, r * 0.7f, r * 1.3f)));
  }
}

// ---------------------------------------------------------------------------
// Theropod legs: a muscular thigh, a long shin, the foot (metatarsus) and three toes
// ---------------------------------------------------------------------------
void addTheroLegs(Model& m, const TheroJ& j, float thighR, float shinR, float footR, float toeLen, int thighMat,
                  bool sickle) {
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int th = sd > 0 ? TH_LTHIGH : TH_RTHIGH, sh = sd > 0 ? TH_LSHIN : TH_RSHIN, ft = sd > 0 ? TH_LFOOT : TH_RFOOT;
    vec3 hip(j.hipL.x * x, j.hipL.y, j.hipL.z), knee(j.kneeL.x * x, j.kneeL.y, j.kneeL.z);
    vec3 ankle(j.ankleL.x * x, j.ankleL.y, j.ankleL.z), ball(j.ballL.x * x, j.ballL.y, j.ballL.z);
    // the drumstick: thigh muscles bulging out over the knee
    m.add(th, thighMat, thighR * 0.5f, coneS(hip, knee, thighR, thighR * 0.62f));
    m.add(th, thighMat, thighR * 0.6f,
          ellS(glm::mix(hip, knee, 0.45f) + vec3(x * thighR * 0.15f, 0, -thighR * 0.3f),
               vec3(thighR * 0.85f, glm::length(knee - hip) * 0.55f, thighR * 1.05f), frameY(hip - knee)));
    // the shin, with its calf muscle
    m.add(sh, DM_SKIN, shinR * 0.6f, coneS(knee, ankle, shinR, shinR * 0.6f));
    m.add(sh, DM_SKIN, shinR * 0.8f,
          ellS(glm::mix(knee, ankle, 0.3f) + vec3(0, 0, -shinR * 0.45f), vec3(shinR * 0.85f, glm::length(ankle - knee) * 0.3f, shinR),
               frameY(knee - ankle)));
    // the foot and the three toes, each with its claw
    m.add(ft, DM_SKIN, footR * 0.5f, coneS(ankle, ball, footR, footR * 0.85f));
    for (int k = -1; k <= 1; ++k) {
      if (sickle && k == -1) continue;  // (a raptor's inner toe is the raised one)
      float spread = sickle ? (k == 0 ? -0.15f : 0.25f) : 0.32f * k;
      vec3 d = glm::normalize(vec3(std::sin(spread) * x, -0.12f, std::cos(spread)));
      float L = toeLen * (k == 0 ? 1.0f : 0.8f);
      vec3 t0 = ball + vec3(0, -footR * 0.25f, 0), t1 = t0 + d * L;
      t1.y = std::max(footR * 0.45f, t1.y);
      m.add(ft, DM_SKIN, footR * 0.4f, coneS(t0, t1, footR * 0.72f, footR * 0.42f));
      addClaw(m, ft, t1 - d * footR * 0.2f, d + vec3(0, 0.25f, 0), vec3(0, -1, 0), footR * 1.6f, footR * 0.42f, 1.1f);
    }
    if (sickle) {
      // the killing claw on the second toe, held up off the ground
      vec3 t0 = ball + vec3(-x * footR * 0.55f, footR * 0.3f, footR * 0.2f);
      vec3 t1 = t0 + vec3(-x * 0.004f, 0.016f, 0.02f);
      m.add(ft, DM_SKIN, footR * 0.3f, coneS(t0, t1, footR * 0.62f, footR * 0.5f));
      addClaw(m, ft, t1, vec3(0, 0.9f, 0.6f), vec3(0, -0.2f, 1), 0.052f, footR * 0.62f, 2.4f);
    }
    // the dewclaw at the back
    if (!sickle) addClaw(m, ft, glm::mix(ankle, ball, 0.75f) + vec3(-x * footR * 0.6f, 0, -footR * 0.6f), vec3(-x * 0.3f, -0.6f, -0.5f),
                         vec3(0, -1, 0), footR * 1.1f, footR * 0.35f, 0.6f);
  }
}

// Theropod arms: upper arm, forearm and the hand's fingers with hooked claws
void addTheroArms(Model& m, const TheroJ& j, float rU, float rF, int fingers, float clawLen, int mat, float thumb = 0.0f) {
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int ua = sd > 0 ? TH_LARM : TH_RARM, fa = sd > 0 ? TH_LFORE : TH_RFORE;
    vec3 sh(j.shL.x * x, j.shL.y, j.shL.z), el(j.elL.x * x, j.elL.y, j.elL.z), wr(j.wrL.x * x, j.wrL.y, j.wrL.z);
    m.add(ua, mat, rU * 0.8f, coneS(sh, el, rU, rU * 0.8f));
    m.add(fa, mat, rF * 0.6f, coneS(el, wr, rF, rF * 0.8f));
    vec3 fwd = glm::normalize(wr - el);
    for (int f = 0; f < fingers; ++f) {
      float a = fingers > 1 ? (f / (float)(fingers - 1) - 0.5f) * 0.6f : 0.0f;
      vec3 d = glm::normalize(fwd + vec3(x * a, -0.35f, 0));
      vec3 f1 = wr + d * rF * 2.2f;
      m.add(fa, DM_SKIN, rF * 0.3f, coneS(wr, f1, rF * 0.55f, rF * 0.4f));
      addClaw(m, fa, f1, d, vec3(0, -1, 0), clawLen, rF * 0.42f, 1.3f);
    }
    if (thumb > 0)  // the spinosaurus' great hooked thumb claw
      addClaw(m, fa, wr + vec3(-x * rF * 0.4f, -rF * 0.3f, rF * 0.4f), vec3(-x * 0.25f, -0.15f, 1.0f), vec3(0, -1, 0), thumb, rF * 0.75f,
              1.6f);
  }
}

// moves every part of a model (its skeleton is placed by toModel to match)
void shiftAll(Model& m, vec3 d) {
  for (Model::Part& q : m.parts) {
    Fn f = q.s.f;
    q.s.f = [f, d](const vec3& p) { return f(p - d); };
    q.s.c += d;
    if (q.w) {
      Fn w = q.w;
      q.w = [w, d](const vec3& p) { return w(p - d); };
    }
  }
}

// ===========================================================================
// The pieces
// ===========================================================================
// The velociraptor: feathers over the body, the arms (half-wings) and the tail's
// end, a scaly snout and legs, a sickle claw on each foot.
Model raptorModel() {
  TheroJ j = theroBase(D_PAWN);
  Model m;
  m.skel = &skeleton(D_PAWN);
  // body: hips and belly, the chest
  m.add(TH_ROOT, DM_FEATHER, 0.0f, ellS(vec3(0, 0.272f, -0.035f), vec3(0.044f, 0.048f, 0.088f)));
  m.add(TH_CHEST, DM_FEATHER, 0.03f, ellS(vec3(0, 0.262f, 0.068f), vec3(0.048f, 0.056f, 0.08f), rotXm(-0.18f)));
  // a ruff of longer feathers on the back of the neck and over the shoulders
  m.add(TH_CHEST, DM_FEATHER, 0.015f, ellS(vec3(0, 0.3f, 0.1f), vec3(0.036f, 0.02f, 0.05f), rotXm(-0.5f)));
  // the neck in an S
  addTube(m, DM_FEATHER, {vec3(0, 0.282f, 0.125f), vec3(0, 0.33f, 0.172f), vec3(0, 0.375f, 0.19f), vec3(0, 0.41f, 0.2f)},
          {0.032f, 0.026f, 0.023f, 0.021f}, {TH_NECK, TH_NECK, TH_NECK}, 0.016f);
  // the head: a long, low skull and snout
  vec3 hc(0, 0.43f, 0.243f);
  m.add(TH_HEAD, DM_SKIN, 0.0f, ellS(hc, vec3(0.026f, 0.025f, 0.046f)));
  m.add(TH_HEAD, DM_SKIN, 0.016f, coneS(hc + vec3(0, 0.002f, 0.02f), vec3(0, 0.421f, 0.333f), 0.021f, 0.0095f));
  m.add(TH_HEAD, DM_FEATHER, 0.012f, ellS(hc + vec3(0, 0.012f, -0.028f), vec3(0.022f, 0.017f, 0.03f)));  // feathers on the nape
  for (int sd = -1; sd <= 1; sd += 2) {
    addEye(m, TH_HEAD, hc + vec3(sd * 0.022f, 0.008f, -0.006f), 0.0085f, vec3(sd * 1.0f, 0.1f, 0.5f));
    m.paint(DM_DARK, ellS(vec3(sd * 0.008f, 0.428f, 0.322f), vec3(0.0035f, 0.0025f, 0.005f)), DM_SKIN);  // nostril
  }
  // the lower jaw
  m.add(TH_JAW, DM_SKIN, 0.008f, coneS(vec3(0, 0.407f, 0.222f), vec3(0, 0.403f, 0.327f), 0.017f, 0.0075f));
  m.add(TH_JAW, DM_BELLY, 0.01f, ellS(vec3(0, 0.398f, 0.235f), vec3(0.014f, 0.01f, 0.03f)));  // the throat
  m.paint(DM_MOUTH, fnS([](const vec3& p) {
            return std::max(std::fabs(p.y - 0.413f) - 0.003f, std::max(0.235f - p.z, p.z - 0.335f));
          }, vec3(0, 0.413f, 0.28f), 0.08f), DM_SKIN);
  addTeeth(m, TH_HEAD, vec3(0.012f, 0.416f, 0.255f), vec3(0.007f, 0.415f, 0.326f), 6, vec3(0, -1, 0.15f), 0.008f, 0.0042f);
  addTeeth(m, TH_JAW, vec3(0.011f, 0.41f, 0.26f), vec3(0.006f, 0.408f, 0.318f), 5, vec3(0, 1, 0.1f), 0.0065f, 0.0024f);
  // arms folded at the sides, a fan of quills on each forearm
  addTheroArms(m, j, 0.0135f, 0.0095f, 3, 0.013f, DM_FEATHER);
  for (int sd = -1; sd <= 1; sd += 2) {
    int fa = sd > 0 ? TH_LFORE : TH_RFORE;
    vec3 el(j.elL.x * sd, j.elL.y, j.elL.z), wr(j.wrL.x * sd, j.wrL.y, j.wrL.z);
    vec3 out(sd * 0.006f, 0, 0);
    vec3 a = wr + out + vec3(0, -0.006f, -0.004f), b = el + out + vec3(0, -0.004f, 0.0f);
    vec3 c = b + vec3(sd * 0.004f, -0.052f, -0.034f), d = a + vec3(sd * 0.004f, -0.05f, -0.022f);
    m.add(fa, DM_QUILL, 0.004f, sheetS(a, b, c, 0.0036f));
    m.add(fa, DM_QUILL, 0.004f, sheetS(a, c, d, 0.0036f));
  }
  // legs: feathered thighs, scaly shins and feet, the sickle claws
  addTheroLegs(m, j, 0.03f, 0.0145f, 0.009f, 0.036f, DM_FEATHER, true);
  // the tail: stiff and straight, a fan of feathers at its end
  addTube(m, DM_FEATHER, {vec3(0, 0.282f, -0.08f), vec3(0, 0.284f, -0.22f), vec3(0, 0.282f, -0.33f), vec3(0, 0.277f, -0.44f)},
          {0.038f, 0.022f, 0.013f, 0.006f}, {TH_TAIL1, TH_TAIL2, TH_TAIL3}, 0.014f);
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 a(0, 0.283f, -0.27f), b(sd * 0.04f, 0.28f, -0.38f), c(0, 0.277f, -0.48f);
    m.add(TH_TAIL3, DM_QUILL, 0.005f, sheetS(a, b, c, 0.0036f));
  }
  return m;
}

// The spinosaurus: the sail along her back, a long neck and a crocodile's snout, strong
// arms with hooked thumb claws, short legs and a tall, finned tail.
Fn sailField(float zSplit, bool front) {
  return [zSplit, front](const vec3& p) {
    const float z0 = -0.25f, z1 = 0.33f, bottom = 0.47f;
    float u = glm::clamp((p.z - z0) / (z1 - z0), 0.0f, 1.0f);
    float arch = std::pow(std::sin(u * 3.14159f), 0.75f) * (1.0f + 0.07f * std::sin(u * 3.14159f * 3.0f + 0.4f));
    float top = 0.5f + 0.31f * arch;
    float spine = std::pow(std::fabs(std::cos(p.z * 3.14159f / 0.034f)), 6.0f);
    float h = glm::clamp((p.y - bottom) / std::max(0.01f, top - bottom), 0.0f, 1.0f);
    float th = (0.005f + 0.0026f * spine) * (1.0f - 0.3f * h);
    float d = std::max(std::fabs(p.x) - th, std::max(p.y - top, bottom - p.y) * 0.55f);
    d = std::max(d, std::max(z0 - p.z, p.z - z1) * 0.7f);
    return std::max(d, front ? zSplit - p.z : p.z - zSplit);
  };
}

Model spinoModel() {
  TheroJ j = theroBase(D_QUEEN);
  Model m;
  m.skel = &skeleton(D_QUEEN);
  m.add(TH_ROOT, DM_SKIN, 0.0f, ellS(vec3(0, 0.462f, -0.03f), vec3(0.068f, 0.082f, 0.13f)));
  m.add(TH_CHEST, DM_SKIN, 0.04f, ellS(vec3(0, 0.44f, 0.16f), vec3(0.07f, 0.098f, 0.14f), rotXm(-0.1f)));
  m.paint(DM_BELLY, ellS(vec3(0, 0.37f, 0.12f), vec3(0.06f, 0.05f, 0.2f)), DM_SKIN);
  // the sail, its front half riding on the chest
  m.add(TH_ROOT, DM_SAIL, 0.006f, fnS(sailField(0.04f, false), vec3(0, 0.62f, -0.1f), 0.24f));
  m.add(TH_CHEST, DM_SAIL, 0.006f, fnS(sailField(0.04f, true), vec3(0, 0.62f, 0.18f), 0.24f));
  // the neck
  addTube(m, DM_SKIN, {vec3(0, 0.49f, 0.27f), vec3(0, 0.555f, 0.36f), vec3(0, 0.605f, 0.42f)}, {0.05f, 0.04f, 0.035f},
          {TH_NECK, TH_NECK}, 0.025f);
  m.paint(DM_BELLY, ellS(vec3(0, 0.5f, 0.38f), vec3(0.03f, 0.03f, 0.08f), rotXm(0.7f)), DM_SKIN);
  addScutes(m, TH_NECK, vec3(0, 0.55f, 0.32f), vec3(0, 0.635f, 0.41f), 4, 0.01f);
  // the head: a long, narrow snout ending in a rosette of teeth, a small crest
  vec3 hc(0, 0.635f, 0.465f);
  m.add(TH_HEAD, DM_SKIN, 0.0f, ellS(hc, vec3(0.034f, 0.038f, 0.055f)));
  m.add(TH_HEAD, DM_SKIN, 0.02f, coneS(hc + vec3(0, -0.004f, 0.03f), vec3(0, 0.607f, 0.69f), 0.026f, 0.015f));
  m.add(TH_HEAD, DM_SKIN, 0.012f, ellS(vec3(0, 0.605f, 0.69f), vec3(0.02f, 0.018f, 0.03f)));
  m.add(TH_HEAD, DM_SCUTE, 0.008f, ellS(vec3(0, 0.663f, 0.5f), vec3(0.007f, 0.011f, 0.03f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    addEye(m, TH_HEAD, hc + vec3(sd * 0.03f, 0.016f, 0.0f), 0.0095f, vec3(sd * 1.0f, 0.15f, 0.35f));
    m.paint(DM_DARK, ellS(vec3(sd * 0.009f, 0.63f, 0.58f), vec3(0.0035f, 0.003f, 0.007f)), DM_SKIN);
  }
  m.add(TH_JAW, DM_SKIN, 0.01f, coneS(vec3(0, 0.597f, 0.46f), vec3(0, 0.585f, 0.68f), 0.022f, 0.012f));
  m.add(TH_JAW, DM_SKIN, 0.01f, ellS(vec3(0, 0.586f, 0.685f), vec3(0.017f, 0.012f, 0.024f)));
  m.add(TH_JAW, DM_BELLY, 0.01f, ellS(vec3(0, 0.585f, 0.49f), vec3(0.022f, 0.012f, 0.05f)));
  m.paint(DM_MOUTH, fnS([](const vec3& p) {
            float y = 0.6035f - 0.06f * (p.z - 0.47f);
            return std::max(std::fabs(p.y - y) - 0.0035f, std::max(0.48f - p.z, p.z - 0.71f));
          }, vec3(0, 0.6f, 0.6f), 0.14f), DM_SKIN);
  addTeeth(m, TH_HEAD, vec3(0.016f, 0.604f, 0.5f), vec3(0.012f, 0.594f, 0.7f), 8, vec3(0, -1, 0.1f), 0.012f, 0.0035f);
  addTeeth(m, TH_JAW, vec3(0.014f, 0.595f, 0.51f), vec3(0.011f, 0.588f, 0.69f), 7, vec3(0, 1, 0.1f), 0.01f, 0.003f);
  // arms: strong, with hooked thumb claws
  addTheroArms(m, j, 0.022f, 0.016f, 2, 0.022f, DM_SKIN, 0.06f);
  addTheroLegs(m, j, 0.05f, 0.026f, 0.017f, 0.05f, DM_SKIN, false);
  // the tail: deep, with a fin above and below, like a paddle
  addTube(m, DM_SKIN, {vec3(0, 0.465f, -0.12f), vec3(0, 0.44f, -0.34f), vec3(0, 0.41f, -0.52f), vec3(0, 0.37f, -0.7f)},
          {0.058f, 0.042f, 0.028f, 0.01f}, {TH_TAIL1, TH_TAIL2, TH_TAIL3}, 0.02f);
  const vec3 tp[4] = {vec3(0, 0.465f, -0.18f), vec3(0, 0.44f, -0.34f), vec3(0, 0.41f, -0.52f), vec3(0, 0.37f, -0.71f)};
  const float up[4] = {0.03f, 0.075f, 0.085f, 0.02f}, dn[4] = {0.0f, 0.04f, 0.05f, 0.012f};
  const int tb[3] = {TH_TAIL1, TH_TAIL2, TH_TAIL3};
  for (int i = 0; i < 3; ++i) {
    vec3 a0 = tp[i] + vec3(0, up[i], 0), a1 = tp[i + 1] + vec3(0, up[i + 1], 0);
    vec3 b0 = tp[i] - vec3(0, dn[i], 0), b1 = tp[i + 1] - vec3(0, dn[i + 1], 0);
    m.add(tb[i], DM_SAIL, 0.008f, sheetS(a0, a1, b1, 0.0045f));
    m.add(tb[i], DM_SAIL, 0.008f, sheetS(a0, b1, b0, 0.0045f));
  }
  return m;
}

// The tyrannosaurus: the great head with its banana teeth, a thick neck, massive
// legs and tail, tiny two-fingered arms, knobbly scutes along the back.
Model rexModel() {
  TheroJ j = theroBase(D_KING);
  Model m;
  m.skel = &skeleton(D_KING);
  m.add(TH_ROOT, DM_SKIN, 0.0f, ellS(vec3(0, 0.505f, -0.04f), vec3(0.09f, 0.1f, 0.15f)));
  m.add(TH_CHEST, DM_SKIN, 0.05f, ellS(vec3(0, 0.47f, 0.16f), vec3(0.102f, 0.13f, 0.15f), rotXm(-0.12f)));
  m.paint(DM_BELLY, ellS(vec3(0, 0.36f, 0.12f), vec3(0.08f, 0.06f, 0.2f)), DM_SKIN);
  // a deep, muscled neck
  addTube(m, DM_SKIN, {vec3(0, 0.53f, 0.25f), vec3(0, 0.6f, 0.33f), vec3(0, 0.66f, 0.39f)}, {0.088f, 0.074f, 0.066f},
          {TH_NECK, TH_NECK}, 0.04f);
  m.paint(DM_BELLY, ellS(vec3(0, 0.55f, 0.37f), vec3(0.05f, 0.04f, 0.08f), rotXm(0.6f)), DM_SKIN);
  // the skull: long, deep and boxy, wide at the back, the snout sloping down; bosses over
  // the eyes, a knobbly ridge down the nose. Its underside is the palate.
  Fn skull = [](const vec3& p) {
    const float z0 = 0.4f, z1 = 0.665f;
    float t = glm::clamp((p.z - z0) / (z1 - z0), 0.0f, 1.0f);
    float w = glm::mix(0.074f, 0.04f, t), h = glm::mix(0.07f, 0.036f, t), yc = glm::mix(0.73f, 0.697f, t);
    float ax = std::fabs(p.x) / w, ay = std::fabs(p.y - yc) / h;
    float r = std::pow(std::pow(ax, 2.6f) + std::pow(ay, 2.6f), 1.0f / 2.6f);
    return smax((r - 1.0f) * std::min(w, h), std::max(z0 - p.z, p.z - z1), 0.03f) * 0.85f;
  };
  m.add(TH_HEAD, DM_SKIN, 0.0f, fnS(skull, vec3(0, 0.72f, 0.53f), 0.16f));
  m.paint(DM_MOUTH, fnS([](const vec3& p) {
            float y1 = 0.6645f + 0.01f * (p.z - 0.4f);
            return std::max(std::max(p.y - y1, (y1 - 0.012f) - p.y), std::max(0.43f - p.z, std::fabs(p.x) - 0.06f));
          }, vec3(0, 0.66f, 0.55f), 0.13f), DM_SKIN);
  for (int sd = -1; sd <= 1; sd += 2) {
    m.add(TH_HEAD, DM_SKIN, 0.02f, ellS(vec3(sd * 0.062f, 0.69f, 0.465f), vec3(0.022f, 0.032f, 0.05f)));  // the cheeks
    m.add(TH_HEAD, DM_SCUTE, 0.012f, ellS(vec3(sd * 0.045f, 0.773f, 0.49f), vec3(0.018f, 0.012f, 0.024f)));  // brow bosses
    addEye(m, TH_HEAD, vec3(sd * 0.056f, 0.745f, 0.497f), 0.011f, vec3(sd * 0.75f, 0.1f, 0.7f));
    m.paint(DM_DARK, ellS(vec3(sd * 0.022f, 0.72f, 0.655f), vec3(0.008f, 0.005f, 0.01f)), DM_SKIN);
  }
  addScutes(m, TH_HEAD, vec3(0, 0.762f, 0.53f), vec3(0, 0.733f, 0.63f), 5, 0.009f);
  // the lower jaw: deep at the back, tapering to the chin; its top is the tongue and gums
  Fn mandible = [](const vec3& p) {
    const float z0 = 0.43f, z1 = 0.652f;
    float t = glm::clamp((p.z - z0) / (z1 - z0), 0.0f, 1.0f);
    float w = glm::mix(0.066f, 0.036f, t), h = glm::mix(0.033f, 0.016f, t), yc = glm::mix(0.63f, 0.647f, t);
    float ax = std::fabs(p.x) / w, ay = std::fabs(p.y - yc) / h;
    float r = std::pow(std::pow(ax, 2.4f) + std::pow(ay, 2.4f), 1.0f / 2.4f);
    return smax((r - 1.0f) * std::min(w, h), std::max(z0 - p.z, p.z - z1), 0.02f) * 0.85f;
  };
  m.add(TH_JAW, DM_SKIN, 0.01f, fnS(mandible, vec3(0, 0.64f, 0.54f), 0.14f));
  m.add(TH_JAW, DM_BELLY, 0.02f, ellS(vec3(0, 0.615f, 0.49f), vec3(0.05f, 0.022f, 0.08f)));
  m.paint(DM_MOUTH, fnS([](const vec3& p) {
            float y0 = 0.656f + 0.008f * (p.z - 0.43f);
            return std::max(std::max(y0 - p.y, p.y - (y0 + 0.012f)), std::max(0.45f - p.z, std::fabs(p.x) - 0.05f));
          }, vec3(0, 0.66f, 0.55f), 0.13f), DM_SKIN);
  addTeeth(m, TH_HEAD, vec3(0.053f, 0.666f, 0.47f), vec3(0.031f, 0.664f, 0.645f), 8, glm::normalize(vec3(0, -1, 0.12f)), 0.024f, 0.0075f);
  addTeeth(m, TH_JAW, vec3(0.05f, 0.66f, 0.49f), vec3(0.028f, 0.66f, 0.63f), 7, glm::normalize(vec3(0, 1, 0.1f)), 0.017f, 0.006f);
  // arms: tiny, two-fingered
  addTheroArms(m, j, 0.02f, 0.014f, 2, 0.016f, DM_SKIN);
  // legs: huge
  addTheroLegs(m, j, 0.075f, 0.042f, 0.028f, 0.07f, DM_SKIN, false);
  // the tail
  addTube(m, DM_SKIN, {vec3(0, 0.52f, -0.12f), vec3(0, 0.5f, -0.36f), vec3(0, 0.46f, -0.53f), vec3(0, 0.41f, -0.68f)},
          {0.085f, 0.06f, 0.036f, 0.008f}, {TH_TAIL1, TH_TAIL2, TH_TAIL3}, 0.03f);
  // scutes along the back, from the neck to the tail
  addScutes(m, TH_NECK, vec3(0, 0.62f, 0.32f), vec3(0, 0.67f, 0.38f), 3, 0.012f);
  addScutes(m, TH_CHEST, vec3(0, 0.6f, 0.06f), vec3(0, 0.6f, 0.22f), 4, 0.013f);
  addScutes(m, TH_ROOT, vec3(0, 0.605f, -0.16f), vec3(0, 0.605f, 0.0f), 4, 0.013f);
  addScutes(m, TH_TAIL1, vec3(0, 0.6f, -0.2f), vec3(0, 0.56f, -0.34f), 3, 0.011f);
  addScutes(m, TH_TAIL2, vec3(0, 0.55f, -0.38f), vec3(0, 0.5f, -0.5f), 3, 0.009f);
  return m;
}

// The pteranodon: a crest swept back from the skull, a long toothless beak, a small
// furry body (pycnofibres, painted as skin), wings of skin from the long fourth finger
// to the legs. Sculpted with its wings spread; the pose folds them.
Model pteroModel() {
  using namespace pj;
  Model m;
  m.skel = &skeleton(D_KNIGHT);
  // the body, tilted up at the shoulders
  m.add(PT_BODY, DM_SKIN, 0.0f, ellS(vec3(0, 0.3f, 0.0f), vec3(0.042f, 0.048f, 0.095f), rotXm(-0.6f)));
  m.add(PT_BODY, DM_SKIN, 0.02f, ellS(vec3(0, 0.34f, 0.055f), vec3(0.05f, 0.038f, 0.04f)));  // the shoulders
  // the neck
  addTube(m, DM_SKIN, {vec3(0, 0.35f, 0.07f), vec3(0, 0.43f, 0.125f), vec3(0, 0.485f, 0.158f)}, {0.024f, 0.019f, 0.017f},
          {PT_NECK, PT_NECK}, 0.012f);
  // the head: skull, the long beak, the crest
  vec3 hc(0, 0.5f, 0.195f);
  m.add(PT_HEAD, DM_SKIN, 0.0f, ellS(hc, vec3(0.024f, 0.028f, 0.045f)));
  m.add(PT_HEAD, DM_HORN, 0.012f, coneS(hc + vec3(0, -0.004f, 0.03f), vec3(0, 0.466f, 0.5f), 0.019f, 0.0018f));
  m.add(PT_HEAD, DM_CREST, 0.01f, fnS([hc](const vec3& p) {
          // a blade swept up and back: its lower edge along the skull, tapering to a point
          vec3 q = p - (hc + vec3(0, 0.012f, 0.0f));
          float u = glm::clamp(-q.z / 0.27f, 0.0f, 1.0f);
          float lo = 0.006f + 0.32f * (-q.z) * 0.45f, hi = 0.035f + 0.4f * (-q.z) * 0.8f - 0.045f * u * u;
          float cy = (q.y - (lo + hi) * 0.5f), hh = std::max(0.0015f, (hi - lo) * 0.5f);
          float d = std::max(std::fabs(cy) - hh, std::max(q.z - 0.02f, -q.z - 0.27f));
          return std::max(d * 0.6f, std::fabs(q.x) - (0.0065f - 0.0025f * u));
        }, hc + vec3(0, 0.07f, -0.12f), 0.17f));
  for (int sd = -1; sd <= 1; sd += 2) addEye(m, PT_HEAD, hc + vec3(sd * 0.02f, 0.008f, 0.008f), 0.0075f, vec3(sd * 1.0f, 0.1f, 0.4f));
  m.add(PT_JAW, DM_HORN, 0.006f, coneS(vec3(0, 0.483f, 0.215f), vec3(0, 0.46f, 0.49f), 0.0135f, 0.0016f));
  m.add(PT_JAW, DM_SKIN, 0.008f, ellS(vec3(0, 0.476f, 0.23f), vec3(0.014f, 0.01f, 0.035f)));  // the throat pouch
  m.paint(DM_DARK, fnS([](const vec3& p) {
            float y = 0.486f - 0.075f * (p.z - 0.2f);
            return std::max(std::fabs(p.y - y) - 0.0018f, std::max(0.22f - p.z, p.z - 0.47f));
          }, vec3(0, 0.47f, 0.35f), 0.16f), DM_HORN);
  // the wings: arm, forearm, the long hand and the wing finger; three small clawed fingers
  // at the knuckle, which it walks on
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int ua = sd > 0 ? PT_LARM : PT_RARM, fa = sd > 0 ? PT_LFORE : PT_RFORE, mc = sd > 0 ? PT_LMETA : PT_RMETA;
    int fg = sd > 0 ? PT_LFINGER : PT_RFINGER;
    int th = sd > 0 ? PT_LTHIGH : PT_RTHIGH, sh = sd > 0 ? PT_LSHIN : PT_RSHIN;
    vec3 S(shL.x * x, shL.y, shL.z), E(elL.x * x, elL.y, elL.z), W(wrL.x * x, wrL.y, wrL.z), N(knL.x * x, knL.y, knL.z);
    vec3 T(tipL.x * x, tipL.y, tipL.z);
    vec3 H(hipL.x * x, hipL.y, hipL.z), K(kneeL.x * x, kneeL.y, kneeL.z), F(footL.x * x, footL.y, footL.z);
    m.add(ua, DM_SKIN, 0.012f, coneS(S, E, 0.02f, 0.014f));
    m.add(ua, DM_SKIN, 0.01f, ellS(glm::mix(S, E, 0.35f) + vec3(0, 0, 0.008f), vec3(0.028f, 0.012f, 0.016f)));  // the crest of the arm
    m.add(fa, DM_SKIN, 0.008f, coneS(E, W, 0.013f, 0.011f));
    m.add(mc, DM_SKIN, 0.006f, coneS(W, N, 0.011f, 0.0095f));
    m.add(fg, DM_SKIN, 0.006f, coneS(N, T, 0.0095f, 0.0025f));
    for (int f = 0; f < 3; ++f) {  // the little fingers it walks on
      vec3 d = glm::normalize(vec3(x * 0.25f, -0.3f + 0.2f * f, 1.0f));
      vec3 f1 = N + d * 0.016f + vec3(0, -0.004f, 0);
      m.add(mc, DM_SKIN, 0.003f, coneS(N, f1, 0.0052f, 0.0036f));
      addClaw(m, mc, f1, d, vec3(0, -1, 0), 0.01f, 0.0032f, 1.2f);
    }
    // the membrane: from the leading edge (arm, hand and finger) to the trailing edge, which
    // rides on its own bones (drawn in against the wing as it folds)
    int tr = sd > 0 ? PT_LTRAIL : PT_RTRAIL, tr2 = sd > 0 ? PT_LTRAIL2 : PT_RTRAIL2;
    (void)th;
    vec3 T2(0.62f * x, 0.355f, -0.12f), W1(0.3f * x, 0.335f, -0.1f), K1 = K + vec3(x * 0.006f, 0.012f, 0.0f);
    auto edgeW = [](vec3 a, vec3 b, vec3 t0, vec3 t1) -> Fn {  // 1 on the leading edge a-b, 0 on the trailing edge t0-t1
      return [a, b, t0, t1](const vec3& p) {
        auto segD = [](const vec3& q, const vec3& u, const vec3& v) {
          vec3 uv = v - u;
          float t = glm::clamp(glm::dot(q - u, uv) / glm::dot(uv, uv), 0.0f, 1.0f);
          return glm::length(q - (u + uv * t));
        };
        float dl = segD(p, a, b), dt = segD(p, t0, t1);
        return dt / std::max(1e-4f, dl + dt);
      };
    };
    Fn wOut = edgeW(N, T, T, W1);
    m.addStretch(fg, tr2, DM_MEMBRANE, 0.004f, sheetS(N, T, T2, 0.0042f), wOut);
    m.addStretch(fg, tr2, DM_MEMBRANE, 0.004f, sheetS(N, T2, W1, 0.0042f), wOut);
    Fn wIn = edgeW(S, N, W1, K1);
    m.addStretch(mc, tr, DM_MEMBRANE, 0.004f, sheetS(W, N, W1, 0.0042f), wIn);
    m.addStretch(fa, tr, DM_MEMBRANE, 0.004f, sheetS(E, W, W1, 0.0042f), wIn);
    m.addStretch(fa, tr, DM_MEMBRANE, 0.004f, sheetS(E, W1, K1, 0.0042f), wIn);
    m.addStretch(ua, tr, DM_MEMBRANE, 0.004f, sheetS(S + vec3(0, -0.006f, -0.01f), E, K1, 0.0042f), wIn);
    m.addStretch(fa, ua, DM_MEMBRANE, 0.004f, sheetS(S + vec3(0, 0.002f, 0.012f), W + vec3(0, 0.002f, 0.004f), E + vec3(0, 0.002f, 0.02f), 0.0038f),
                 along(W, S));  // the propatagium in front of the elbow
    // legs and clawed feet
    m.add(th, DM_SKIN, 0.01f, coneS(H, K, 0.016f, 0.009f));
    m.add(sh, DM_SKIN, 0.006f, coneS(K, F, 0.0085f, 0.006f));
    for (int t = -1; t <= 2; ++t) {
      vec3 d = glm::normalize(vec3(x * 0.15f * t, -0.1f, 1.0f));
      vec3 t1 = F + d * 0.022f;
      t1.y = 0.004f;
      m.add(sh, DM_SKIN, 0.003f, coneS(F, t1, 0.0045f, 0.0032f));
      addClaw(m, sh, t1, d, vec3(0, -1, 0), 0.009f, 0.0028f, 1.0f);
    }
  }
  return m;
}

// The triceratops: the frill, three horns and a parrot's beak on a great head, a
// barrel of a body on stout legs, a short heavy tail.
Model trikeModel() {
  QuadJ j = quadBase(D_BISHOP);
  Model m;
  m.skel = &skeleton(D_BISHOP);
  m.add(QD_BODY, DM_SKIN, 0.0f, ellS(vec3(0, 0.34f, -0.05f), vec3(0.15f, 0.15f, 0.25f)));
  m.add(QD_BODY, DM_SKIN, 0.08f, ellS(vec3(0, 0.37f, -0.2f), vec3(0.13f, 0.12f, 0.12f)));  // the hips
  m.add(QD_BODY, DM_SKIN, 0.08f, ellS(vec3(0, 0.31f, 0.12f), vec3(0.13f, 0.13f, 0.12f)));  // the shoulders
  m.paint(DM_BELLY, ellS(vec3(0, 0.2f, -0.05f), vec3(0.12f, 0.05f, 0.22f)), DM_SKIN);
  // the bristly back: a row of low knobs
  addScutes(m, QD_BODY, vec3(0, 0.49f, -0.28f), vec3(0, 0.47f, 0.08f), 7, 0.014f);
  // the neck under the frill
  addTube(m, DM_SKIN, {vec3(0, 0.33f, 0.16f), vec3(0, 0.33f, 0.23f), vec3(0, 0.325f, 0.3f)}, {0.1f, 0.092f, 0.085f},
          {QD_NECK1, QD_NECK3}, 0.04f);
  // the head
  m.add(QD_HEAD, DM_SKIN, 0.0f, ellS(vec3(0, 0.31f, 0.4f), vec3(0.078f, 0.085f, 0.13f)));
  m.add(QD_HEAD, DM_SKIN, 0.03f, coneS(vec3(0, 0.3f, 0.45f), vec3(0, 0.235f, 0.575f), 0.058f, 0.026f));
  m.add(QD_HEAD, DM_HORN, 0.006f, coneS(vec3(0, 0.245f, 0.565f), vec3(0, 0.195f, 0.62f), 0.026f, 0.005f));  // the beak
  for (int sd = -1; sd <= 1; sd += 2) {
    addEye(m, QD_HEAD, vec3(sd * 0.064f, 0.35f, 0.43f), 0.012f, vec3(sd * 1.0f, 0.15f, 0.25f));
    // the cheek horns (epijugals)
    addClaw(m, QD_HEAD, vec3(sd * 0.075f, 0.27f, 0.37f), vec3(sd * 1.0f, -0.4f, 0.1f), vec3(0, -1, 0), 0.04f, 0.016f, 0.4f, DM_HORN, 0.01f);
    // the brow horns, curving forward
    addClaw(m, QD_HEAD, vec3(sd * 0.042f, 0.37f, 0.425f), vec3(sd * 0.18f, 0.62f, 1.0f), vec3(0, -1, 0.3f), 0.25f, 0.022f, 0.35f,
            DM_HORN, 0.012f);
    m.paint(DM_DARK, ellS(vec3(sd * 0.022f, 0.27f, 0.555f), vec3(0.008f, 0.01f, 0.012f)), DM_SKIN);  // nostril
  }
  addClaw(m, QD_HEAD, vec3(0, 0.3f, 0.53f), vec3(0, 1.0f, 0.55f), vec3(0, 0, 1), 0.065f, 0.017f, 0.3f, DM_HORN, 0.008f);  // nose horn
  // the frill: a shield rising up and back from the skull, its edge scalloped with bony knobs
  {
    vec3 base(0, 0.33f, 0.32f);
    vec3 up = glm::normalize(vec3(0, 0.87f, -0.5f));
    vec3 n = glm::cross(vec3(1, 0, 0), up);
    m.add(QD_HEAD, DM_FRILL, 0.02f, fnS([base, up, n](const vec3& p) {
            vec3 q = p - base;
            float u = glm::dot(q, up), v = q.x;
            float w = glm::dot(q, n) - 0.35f * v * v - 0.25f * std::max(0.0f, u) * std::max(0.0f, u);
            float r = std::sqrt((v / 0.205f) * (v / 0.205f) + ((u - 0.02f) / 0.31f) * ((u - 0.02f) / 0.31f));
            float outline = (r - 1.0f) * 0.2f;
            float th = 0.011f * (1.0f - 0.45f * glm::clamp(u / 0.33f, 0.0f, 1.0f)) + 0.002f;
            return std::max(std::max(outline, -u + 0.02f), std::fabs(w) - th);
          }, base + up * 0.15f, 0.27f));
    for (int i = 0; i < 13; ++i) {  // the epoccipitals round the edge
      float a = -1.35f + 2.7f * i / 12.0f;
      float v = 0.205f * std::sin(a), u = 0.02f + 0.31f * std::cos(a);
      vec3 p = base + up * u + vec3(v, 0, 0) + n * (0.35f * v * v + 0.25f * u * u);
      m.add(QD_HEAD, DM_HORN, 0.008f, ellS(p, vec3(0.016f, 0.016f, 0.01f), frameY(up, n)));
    }
  }
  // the lower jaw and its beak
  m.add(QD_JAW, DM_SKIN, 0.015f, ellS(vec3(0, 0.235f, 0.46f), vec3(0.058f, 0.034f, 0.11f)));
  m.add(QD_JAW, DM_HORN, 0.006f, coneS(vec3(0, 0.215f, 0.54f), vec3(0, 0.205f, 0.595f), 0.024f, 0.006f));
  m.paint(DM_MOUTH, fnS([](const vec3& p) {
            return std::max(std::fabs(p.y - 0.253f + 0.15f * (p.z - 0.4f)) - 0.003f, std::max(0.37f - p.z, p.z - 0.6f));
          }, vec3(0, 0.24f, 0.48f), 0.15f), DM_SKIN);
  // legs: sprawled front legs, pillars behind, hoof-like claws
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int fu = sd > 0 ? QD_FLU : QD_FRU, fl = sd > 0 ? QD_FLL : QD_FRL, hu = sd > 0 ? QD_HLU : QD_HRU, hl = sd > 0 ? QD_HLL : QD_HRL;
    vec3 FU(j.flu.x * x, j.flu.y, j.flu.z), FL(j.fll.x * x, j.fll.y, j.fll.z), FF(j.ffoot.x * x, 0.03f, j.ffoot.z);
    vec3 HU(j.hlu.x * x, j.hlu.y, j.hlu.z), HL(j.hll.x * x, j.hll.y, j.hll.z), HF(j.hfoot.x * x, 0.03f, j.hfoot.z);
    m.add(fu, DM_SKIN, 0.04f, coneS(FU + vec3(0, 0.03f, 0), FL, 0.055f, 0.038f));
    m.add(fl, DM_SKIN, 0.02f, coneS(FL, FF, 0.038f, 0.033f));
    m.add(fl, DM_SKIN, 0.015f, ellS(FF + vec3(0, -0.006f, 0.012f), vec3(0.04f, 0.026f, 0.042f)));
    m.add(hu, DM_SKIN, 0.05f, ellS(glm::mix(HU, HL, 0.4f) + vec3(x * 0.01f, 0, 0), vec3(0.06f, 0.11f, 0.08f)));
    m.add(hu, DM_SKIN, 0.03f, coneS(HU, HL, 0.065f, 0.045f));
    m.add(hl, DM_SKIN, 0.02f, coneS(HL, HF, 0.044f, 0.036f));
    m.add(hl, DM_SKIN, 0.015f, ellS(HF + vec3(0, -0.006f, 0.014f), vec3(0.043f, 0.027f, 0.048f)));
    for (int k = -1; k <= 1; ++k) {
      m.add(fl, DM_CLAW, 0.004f, ellS(FF + vec3(k * 0.022f, -0.016f, 0.048f), vec3(0.011f, 0.012f, 0.012f)));
      m.add(hl, DM_CLAW, 0.004f, ellS(HF + vec3(k * 0.024f, -0.016f, 0.056f), vec3(0.012f, 0.012f, 0.013f)));
    }
  }
  // the tail
  addTube(m, DM_SKIN, {vec3(0, 0.37f, -0.27f), vec3(0, 0.33f, -0.45f), vec3(0, 0.27f, -0.56f), vec3(0, 0.19f, -0.65f)},
          {0.088f, 0.058f, 0.034f, 0.008f}, {QD_TAIL1, QD_TAIL2, QD_TAIL3}, 0.03f);
  return m;
}

// The brachiosaurus: a deep chest on pillar legs, longer in front; the neck rising
// like a tower; a small head with a domed nose; a tail sweeping down to the board.
Model brachioModel() {
  QuadJ j = quadBase(D_ROOK);
  Model m;
  m.skel = &skeleton(D_ROOK);
  m.add(QD_BODY, DM_SKIN, 0.0f, ellS(vec3(0, 0.57f, 0.07f), vec3(0.13f, 0.15f, 0.19f)));
  m.add(QD_BODY, DM_SKIN, 0.09f, ellS(vec3(0, 0.53f, -0.13f), vec3(0.12f, 0.12f, 0.14f)));
  m.add(QD_BODY, DM_SKIN, 0.06f, ellS(vec3(0, 0.64f, 0.15f), vec3(0.11f, 0.09f, 0.1f)));  // the withers
  m.paint(DM_BELLY, ellS(vec3(0, 0.42f, 0.0f), vec3(0.1f, 0.05f, 0.2f)), DM_SKIN);
  // the neck, in three bones, its underside pale
  addTube(m, DM_SKIN, {vec3(0, 0.62f, 0.19f), vec3(0, 0.86f, 0.3f), vec3(0, 1.03f, 0.37f), vec3(0, 1.185f, 0.43f)},
          {0.076f, 0.055f, 0.044f, 0.034f}, {QD_NECK1, QD_NECK2, QD_NECK3}, 0.03f);
  m.paint(DM_BELLY, fnS([](const vec3& p) {
            vec3 a(0, 0.62f, 0.19f), b(0, 1.185f, 0.43f);
            vec3 ab = b - a;
            float t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
            vec3 c = a + ab * t;
            vec3 front = glm::normalize(vec3(0, -0.35f, 1.0f));
            return 0.25f * (0.028f - glm::dot(p - c, front));
          }, vec3(0, 0.9f, 0.31f), 0.4f), DM_SKIN);
  // the head: small, with the arched nose of a brachiosaur
  vec3 hc(0, 1.21f, 0.47f);
  m.add(QD_HEAD, DM_SKIN, 0.0f, ellS(hc, vec3(0.032f, 0.032f, 0.048f)));
  m.add(QD_HEAD, DM_SKIN, 0.025f, ellS(vec3(0, 1.243f, 0.455f), vec3(0.027f, 0.029f, 0.033f)));
  m.add(QD_HEAD, DM_SKIN, 0.015f, coneS(vec3(0, 1.202f, 0.49f), vec3(0, 1.195f, 0.545f), 0.026f, 0.021f));
  for (int sd = -1; sd <= 1; sd += 2) {
    addEye(m, QD_HEAD, vec3(sd * 0.029f, 1.228f, 0.465f), 0.0085f, vec3(sd * 1.0f, 0.1f, 0.3f));
    m.paint(DM_DARK, ellS(vec3(sd * 0.011f, 1.271f, 0.455f), vec3(0.006f, 0.004f, 0.008f)), DM_SKIN);
  }
  m.add(QD_JAW, DM_SKIN, 0.01f, coneS(vec3(0, 1.178f, 0.48f), vec3(0, 1.175f, 0.542f), 0.02f, 0.016f));
  m.paint(DM_MOUTH, fnS([](const vec3& p) {
            return std::max(std::fabs(p.y - 1.186f) - 0.0025f, std::max(0.49f - p.z, p.z - 0.56f));
          }, vec3(0, 1.186f, 0.52f), 0.06f), DM_SKIN);
  addTeeth(m, QD_HEAD, vec3(0.016f, 1.188f, 0.505f), vec3(0.01f, 1.188f, 0.543f), 4, vec3(0, -1, 0.3f), 0.007f, 0.0028f);
  // pillar legs, the front ones taller; each foot with a thumb claw
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int fu = sd > 0 ? QD_FLU : QD_FRU, fl = sd > 0 ? QD_FLL : QD_FRL, hu = sd > 0 ? QD_HLU : QD_HRU, hl = sd > 0 ? QD_HLL : QD_HRL;
    vec3 FU(j.flu.x * x, j.flu.y, j.flu.z), FL(j.fll.x * x, j.fll.y, j.fll.z), FF(j.ffoot.x * x, 0.035f, j.ffoot.z);
    vec3 HU(j.hlu.x * x, j.hlu.y, j.hlu.z), HL(j.hll.x * x, j.hll.y, j.hll.z), HF(j.hfoot.x * x, 0.035f, j.hfoot.z);
    m.add(fu, DM_SKIN, 0.04f, coneS(FU + vec3(0, 0.03f, 0), FL, 0.058f, 0.04f));
    m.add(fl, DM_SKIN, 0.04f, coneS(FL, FF + vec3(0, 0.03f, 0), 0.04f, 0.036f));
    m.add(fl, DM_SKIN, 0.035f, coneS(FF + vec3(0, 0.03f, 0), vec3(FF.x, 0.014f, FF.z + 0.006f), 0.036f, 0.044f));
    m.add(hu, DM_SKIN, 0.05f, ellS(glm::mix(HU, HL, 0.4f), vec3(0.066f, 0.13f, 0.09f)));
    m.add(hl, DM_SKIN, 0.04f, coneS(HL, HF + vec3(0, 0.03f, 0), 0.048f, 0.04f));
    m.add(hl, DM_SKIN, 0.035f, coneS(HF + vec3(0, 0.03f, 0), vec3(HF.x, 0.014f, HF.z + 0.01f), 0.04f, 0.048f));
    addClaw(m, fl, vec3(FF.x - x * 0.03f, 0.02f, FF.z + 0.032f), vec3(-x * 0.3f, 0.2f, 1.0f), vec3(0, -1, 0), 0.03f, 0.01f, 0.9f);
    for (int k = -1; k <= 1; ++k) m.add(hl, DM_CLAW, 0.004f, ellS(HF + vec3(k * 0.026f, -0.022f, 0.05f), vec3(0.012f, 0.012f, 0.012f)));
  }
  // the tail
  addTube(m, DM_SKIN, {vec3(0, 0.56f, -0.2f), vec3(0, 0.47f, -0.42f), vec3(0, 0.37f, -0.56f), vec3(0, 0.25f, -0.7f)},
          {0.075f, 0.048f, 0.03f, 0.008f}, {QD_TAIL1, QD_TAIL2, QD_TAIL3}, 0.03f);
  return m;
}

// --- the props ---------------------------------------------------------------
Model poolModel() {  // a pool of blood with splashes round it; radius ~1 (scaled down when used)
  Model m;
  m.add(0, DM_BLOOD, 0.0f, fnS([](const vec3& p) {
          float a = std::atan2(p.x, p.z);
          float r = std::sqrt(p.x * p.x + p.z * p.z);
          float edge = 0.78f + 0.1f * std::sin(a * 3.0f + 0.7f) + 0.06f * std::sin(a * 7.0f + 2.1f) + 0.03f * std::sin(a * 13.0f);
          float d = r - edge;
          for (int k = 0; k < 7; ++k) {
            float b = k * 0.9f + 0.4f;
            vec2 c = vec2(std::sin(b), std::cos(b)) * (0.98f + 0.13f * (k % 3));
            d = std::min(d, glm::length(vec2(p.x, p.z) - c) - (0.06f + 0.025f * (k % 4)));
          }
          float th = 0.024f * glm::clamp(-d / 0.25f + 0.25f, 0.25f, 1.0f);
          return std::max(d * 0.8f, std::fabs(p.y) - th);
        }, vec3(0), 1.2f));
  return m;
}

Model meteorModel() {  // a lumpy rock, scorched, glowing in its cracks; radius ~1
  Model m;
  m.add(0, DM_ROCK, 0.0f, fnS([](const vec3& p) {
          float r = glm::length(p);
          vec3 n = p / std::max(1e-4f, r);
          float bump = 0.09f * std::sin(n.x * 5.0f + 1.0f) * std::sin(n.y * 4.0f + 2.0f) +
                       0.05f * std::sin(n.z * 9.0f + n.x * 3.0f) + 0.025f * std::sin(n.y * 17.0f + n.z * 11.0f);
          return (r - 0.9f - bump) * 0.8f;
        }, vec3(0), 1.1f));
  m.paint(DM_LAVA, fnS([](const vec3& p) {
            vec3 n = glm::normalize(p + vec3(1e-4f));
            float c = std::fabs(std::sin(n.x * 7.0f + std::sin(n.y * 5.0f) * 2.0f) * std::sin(n.z * 6.0f + n.y * 3.0f));
            return c - 0.12f;
          }, vec3(0), 1.2f), DM_ROCK);
  return m;
}

}  // namespace

// ---------------------------------------------------------------------------
float pieceHeight(int kind) {
  static const float H[6] = {0.46f, 0.57f, 0.53f, 1.28f, 0.67f, 0.69f};
  return H[std::max(0, std::min(5, kind))];
}
float pieceRadius(int kind) {
  static const float R[6] = {0.19f, 0.22f, 0.24f, 0.28f, 0.22f, 0.25f};
  return R[std::max(0, std::min(5, kind))];
}
bool theropod(int kind) { return kind == D_PAWN || kind == D_QUEEN || kind == D_KING; }
bool quadruped(int kind) { return kind == D_BISHOP || kind == D_ROOK; }

vec3 headPoint(int kind) {
  switch (kind) {
    case D_PAWN: return toModel(kind, vec3(0, 0.43f, 0.25f));
    case D_KNIGHT: return toModel(kind, vec3(0, 0.5f, 0.22f));
    case D_BISHOP: return toModel(kind, vec3(0, 0.31f, 0.42f));
    case D_ROOK: return toModel(kind, vec3(0, 1.21f, 0.48f));
    case D_QUEEN: return toModel(kind, vec3(0, 0.63f, 0.51f));
    default: return toModel(kind, vec3(0, 0.71f, 0.52f));
  }
}
vec3 mouthPoint(int kind) {
  switch (kind) {
    case D_PAWN: return toModel(kind, vec3(0, 0.413f, 0.3f));
    case D_KNIGHT: return toModel(kind, vec3(0, 0.465f, 0.49f));
    case D_BISHOP: return toModel(kind, vec3(0, 0.22f, 0.6f));
    case D_ROOK: return toModel(kind, vec3(0, 1.185f, 0.54f));
    case D_QUEEN: return toModel(kind, vec3(0, 0.598f, 0.66f));
    default: return toModel(kind, vec3(0, 0.664f, 0.61f));
  }
}
vec3 chestPoint(int kind) {
  switch (kind) {
    case D_PAWN: return toModel(kind, vec3(0, 0.27f, 0.06f));
    case D_KNIGHT: return toModel(kind, vec3(0, 0.32f, 0.03f));
    case D_BISHOP: return toModel(kind, vec3(0, 0.33f, 0.08f));
    case D_ROOK: return toModel(kind, vec3(0, 0.58f, 0.12f));
    case D_QUEEN: return toModel(kind, vec3(0, 0.45f, 0.14f));
    default: return toModel(kind, vec3(0, 0.48f, 0.15f));
  }
}
vec3 clawPoint(int kind) {
  switch (kind) {
    case D_PAWN: return toModel(kind, mx(theroBase(D_PAWN).ballL) + vec3(0.006f, 0.045f, 0.045f));
    case D_KNIGHT: return toModel(kind, mx(pj::footL) + vec3(0, 0.0f, 0.02f));
    case D_QUEEN: return toModel(kind, mx(theroBase(D_QUEEN).wrL) + vec3(0, -0.03f, 0.06f));
    default: return chestPoint(kind);
  }
}
vec3 hornPoint(int kind) {
  if (kind == D_BISHOP) return toModel(kind, vec3(0, 0.49f, 0.62f));
  return mouthPoint(kind);
}
vec3 tailTip(int kind) {
  if (theropod(kind)) return theroJ(kind).tip;
  if (quadruped(kind)) return quadJ(kind).tip;
  return toModel(kind, pj::body + vec3(0, 0, -0.08f));
}
float strideCycle(int kind) {
  switch (kind) {
    case D_PAWN: return 0.32f;
    case D_KNIGHT: return 0.2f;
    case D_BISHOP: return 0.38f;
    case D_ROOK: return 0.6f;
    case D_QUEEN: return 0.36f;
    default: return 0.42f;
  }
}
float bodyLength(int kind) {
  switch (kind) {
    case D_PAWN: return 0.8f;
    case D_KNIGHT: return 0.56f * animalScale(kind);
    case D_BISHOP: return 1.27f * animalScale(kind);
    case D_ROOK: return 1.22f;
    case D_QUEEN: return 1.42f * animalScale(kind);
    default: return 1.32f * animalScale(kind);
  }
}
vec3 wingTip() { return toModel(D_KNIGHT, pj::tipL); }
float bodyHalfWidth(int kind) {
  static const float W[6] = {0.05f, 0.05f, 0.13f, 0.13f, 0.06f, 0.088f};
  return W[std::max(0, std::min(5, kind))];
}

int groundPoints(int asset, vec3* pts, int* bones) {
  switch (asset) {
    case D_PAWN:
    case D_QUEEN:
    case D_KING: {
      TheroJ j = theroJ(asset);
      float toe = asset == D_PAWN ? 0.03f : 0.05f;
      pts[0] = vec3(j.ballL.x, 0.0f, j.ballL.z + toe * j.S);
      pts[1] = mx(pts[0]);
      pts[2] = vec3(j.ballL.x, 0.0f, j.ballL.z - 0.01f * j.S);
      pts[3] = mx(pts[2]);
      bones[0] = bones[2] = TH_LFOOT;
      bones[1] = bones[3] = TH_RFOOT;
      return 4;
    }
    case D_KNIGHT:
      pts[0] = toModel(asset, vec3(pj::footL.x, 0.0f, pj::footL.z + 0.01f));
      pts[1] = mx(pts[0]);
      bones[0] = PT_LSHIN;
      bones[1] = PT_RSHIN;
      pts[2] = toModel(asset, pj::knL + vec3(0, -0.012f, 0.012f));  // the hands (they reach the board once the wings are folded)
      pts[3] = mx(pts[2]);
      bones[2] = PT_LMETA;
      bones[3] = PT_RMETA;
      return 4;
    default: {
      QuadJ j = quadJ(asset);
      pts[0] = vec3(j.ffoot.x, 0.0f, j.ffoot.z);
      pts[1] = mx(pts[0]);
      pts[2] = vec3(j.hfoot.x, 0.0f, j.hfoot.z);
      pts[3] = mx(pts[2]);
      bones[0] = QD_FLL;
      bones[1] = QD_FRL;
      bones[2] = QD_HLL;
      bones[3] = QD_HRL;
      return 4;
    }
  }
}

void buildLibrary(Library& lib, int quality, meshgen::BuildProgress* prog) {
  quality = std::max(0, std::min(3, quality));
  if (prog) {
    prog->total = 7;
    prog->done = 0;
  }
  auto tick = [&](const char* s) {
    if (prog) {
      prog->stage = s;
      prog->done++;
    }
  };
  auto build = [&](int kind, Model m, vec3 lo, vec3 hi) {
    float S = animalScale(kind);
    if (S != 1.0f) m.scaleAll(S);
    vec3 d(0, 0, animalShift(kind));
    if (d.z != 0.0f) shiftAll(m, d);
    buildAsset(lib.asset[kind], m, toModel(kind, lo), toModel(kind, hi), quality, 0, 0u, pieceHeight(kind), pieceRadius(kind));
  };
  build(D_PAWN, raptorModel(), vec3(-0.11f, -0.005f, -0.52f), vec3(0.11f, 0.5f, 0.36f));
  tick("Hatching the raptors");
  build(D_KNIGHT, pteroModel(), vec3(-0.88f, -0.005f, -0.18f), vec3(0.88f, 0.66f, 0.52f));
  tick("Teaching the pteranodons to fly");
  build(D_BISHOP, trikeModel(), vec3(-0.25f, -0.005f, -0.7f), vec3(0.25f, 0.68f, 0.66f));
  tick("Sharpening the triceratops' horns");
  build(D_ROOK, brachioModel(), vec3(-0.2f, -0.005f, -0.74f), vec3(0.2f, 1.32f, 0.6f));
  tick("Raising the brachiosaurus");
  build(D_QUEEN, spinoModel(), vec3(-0.15f, -0.005f, -0.76f), vec3(0.15f, 0.84f, 0.76f));
  tick("Hoisting the spinosaurus' sail");
  build(D_KING, rexModel(), vec3(-0.19f, -0.005f, -0.72f), vec3(0.19f, 0.84f, 0.7f));
  tick("Waking the tyrant king");
  {
    Model c = poolModel();
    buildAsset(lib.asset[D_POOL], c, vec3(-1.25f, -0.04f, -1.25f), vec3(1.25f, 0.04f, 1.25f), 0, 0, 0u, 0.03f, 1.0f);
    Model d = meteorModel();
    buildAsset(lib.asset[D_METEOR], d, vec3(-1.1f), vec3(1.1f), 0, 0, 0u, 2.0f, 1.0f);
    tick("Scattering the bones");
  }
  lib.built = true;
  if (prog) prog->finished = true;
}

}  // namespace dino
