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
      j.tail2 = vec3(0, 0.44f, -0.35f);
      j.tail3 = vec3(0, 0.41f, -0.55f);
      j.tip = vec3(0, 0.37f, -0.77f);
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
      j.tail2 = vec3(0, 0.5f, -0.37f);
      j.tail3 = vec3(0, 0.465f, -0.56f);
      j.tip = vec3(0, 0.42f, -0.76f);
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
  vec3 flu, fll, fwrist, ffoot, hlu, hll, hankle, hfoot;  // shoulder, elbow, wrist, foot; hip, knee, ankle, foot
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
    j.tail2 = vec3(0, 0.32f, -0.48f);
    j.tail3 = vec3(0, 0.26f, -0.63f);
    j.tip = vec3(0, 0.18f, -0.78f);
    j.flu = vec3(0.1f, 0.3f, 0.15f);
    j.fll = vec3(0.13f, 0.17f, 0.165f);
    j.fwrist = vec3(0.122f, 0.06f, 0.18f);
    j.ffoot = vec3(0.12f, 0.0f, 0.195f);
    j.hlu = vec3(0.1f, 0.36f, -0.2f);
    j.hll = vec3(0.11f, 0.2f, -0.13f);
    j.hankle = vec3(0.11f, 0.075f, -0.2f);
    j.hfoot = vec3(0.11f, 0.0f, -0.165f);
  } else {
    j.body = vec3(0, 0.52f, 0);
    j.neck1 = vec3(0, 0.66f, 0.22f);
    j.neck2 = vec3(0, 0.86f, 0.3f);
    j.neck3 = vec3(0, 1.03f, 0.37f);
    j.head = vec3(0, 1.19f, 0.43f);
    j.jaw = vec3(0, 1.185f, 0.47f);
    j.tail1 = vec3(0, 0.55f, -0.24f);
    j.tail2 = vec3(0, 0.47f, -0.45f);
    j.tail3 = vec3(0, 0.36f, -0.63f);
    j.tip = vec3(0, 0.22f, -0.82f);
    j.flu = vec3(0.1f, 0.58f, 0.16f);
    j.fll = vec3(0.105f, 0.33f, 0.14f);
    j.fwrist = vec3(0.105f, 0.09f, 0.17f);
    j.ffoot = vec3(0.105f, 0.0f, 0.172f);
    j.hlu = vec3(0.1f, 0.53f, -0.16f);
    j.hll = vec3(0.105f, 0.28f, -0.11f);
    j.hankle = vec3(0.105f, 0.08f, -0.17f);
    j.hfoot = vec3(0.105f, 0.0f, -0.15f);
  }
  return j;
}
QuadJ quadJ(int kind) {
  QuadJ j = quadBase(kind);
  for (vec3* v : {&j.body, &j.neck1, &j.neck2, &j.neck3, &j.head, &j.jaw, &j.tail1, &j.tail2, &j.tail3, &j.tip, &j.flu,
                  &j.fll, &j.fwrist, &j.ffoot, &j.hlu, &j.hll, &j.hankle, &j.hfoot})
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
      {QD_FLL, j.fwrist, false},     // FLF: the forefoot (hand)
      {QD_FRL, mx(j.fwrist), false},
      {QD_HLL, j.hankle, false},     // HLF: the hind foot
      {QD_HRL, mx(j.hankle), false},
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
        {PT_LARM, P(shL), false},         // LTRAILA: behind the arm
        {PT_RARM, P(mx(shL)), false},
        {PT_LMETA, P(wrL), false},        // LTRAILH: behind the hand
        {PT_RMETA, P(mx(wrL)), false},
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
  m.add(bone, DM_SKIN, r * 0.5f, ellS(c + vec3(0, r * 0.88f, 0) - out * r * 0.3f, vec3(r * 1.1f, r * 0.38f, r * 1.05f), F));
}

// A row of teeth along the jaw from a to b (both sides of the mouth: x and -x), pointing 'dir'.
// Each tooth is rigid on its jaw: it never bends towards the other one.
void addTeeth(Model& m, int bone, vec3 a, vec3 b, int n, vec3 dir, float len, float r) {
  for (int i = 0; i < n; ++i) {
    float u = n > 1 ? (float)i / (n - 1) : 0.0f;
    vec3 p = glm::mix(a, b, u);
    float s = 1.0f - 0.35f * std::fabs(u - 0.35f);
    for (int sd = -1; sd <= 1; sd += 2) {
      vec3 q(p.x * sd, p.y, p.z);
      // (curved a little back along their length, like a real tooth)
      vec3 tip = q + dir * len * s, mid = glm::mix(q, tip, 0.55f) + vec3(0, 0, len * s * 0.08f);
      m.addR(bone, DM_TEETH, 0.0f, coneS(q, mid, r * s, r * s * 0.62f));
      m.addR(bone, DM_TEETH, 0.0f, coneS(mid, tip, r * s * 0.62f, r * 0.12f));
    }
  }
}

// The mouth. Every animal is sculpted with its mouth agape: the lower jaw's parts (and the
// paint on them), from 'from' on, are turned down about the hinge by gape(), so that the jaws,
// their teeth, the palate and the tongue are surfaces of their own that come apart cleanly as
// the mouth opens (blended into one, they would stretch a sheet of skin between them). The pose
// closes the mouth again.
void openJaw(Model& m, size_t from, vec3 hinge, float angle) {
  mat3 R = rotXm(angle), Rt = glm::transpose(R);
  for (size_t i = from; i < m.parts.size(); ++i) {
    Model::Part& q = m.parts[i];
    Fn f = q.s.f;
    q.s.f = [f, Rt, hinge](const vec3& p) { return f(Rt * (p - hinge) + hinge); };
    q.s.c = R * (q.s.c - hinge) + hinge;
    if (q.w) {
      Fn w = q.w;
      q.w = [w, Rt, hinge](const vec3& p) { return w(Rt * (p - hinge) + hinge); };
    }
  }
}

// Trims the lower jaw's parts (from 'from' to 'to', before openJaw) to the mouth line from the
// hinge along 'dir', a little rounded at the rim, so that they don't reach up into the upper
// jaw; or (upper) the upper jaw's parts, in front of the hinge, so that they don't hang down
// over the lower one.
void trimJaw(Model& m, size_t from, vec3 hinge, vec3 dir, float k, bool upper = false, size_t to = (size_t)-1) {
  dir = glm::normalize(vec3(0, dir.y, dir.z));
  vec3 up(0, dir.z, -dir.y);
  for (size_t i = from; i < std::min(to, m.parts.size()); ++i) {
    Model::Part& q = m.parts[i];
    if (q.op != Model::ADD) continue;
    Fn f = q.s.f;
    if (upper)
      q.s.f = [f, hinge, up, dir, k](const vec3& p) {
        vec3 d = p - hinge;
        return smax(f(p), std::min(-glm::dot(d, up), glm::dot(d, dir)), k);
      };
    else
      q.s.f = [f, hinge, up, k](const vec3& p) { return smax(f(p), glm::dot(p - hinge, up), k); };
  }
}

// The throat under the lower jaw: skin that goes with the jaw at the front, but towards the
// back (where it runs into the neck) more and more with the head, so that it stretches smoothly
// as the jaw moves.
void addThroat(Model& m, int jawBone, int headBone, int mat, float k, const Shape& s, vec3 hinge, vec3 dir, float len) {
  dir = glm::normalize(vec3(0, dir.y, dir.z));
  m.addStretch(jawBone, headBone, mat, k, s, [hinge, dir, len](const vec3& p) {
    return glm::smoothstep(-0.15f * len, len, glm::dot(p - hinge, dir));
  });
}

// The inside of the mouth, painted over the skin: the palate under the upper jaw, or (upper =
// false, painted with the lower jaw's parts so that it turns with them) the tongue and gums on
// the lower jaw. The mouth line (where the jaws meet when closed) runs from the hinge forward
// along 'dir' (in the yz plane) to 'len' from it. The palate is the upper jaw's surface over
// the line bisecting the gape and no more than 'depth' above the mouth line; the tongue the
// lower jaw's under that line (its own, before it is turned) and no more than 'depth' below it;
// both inside the half-width hw0 (at the hinge) .. hw1 (at the front), short of the lips.
void paintMouth(Model& m, vec3 hinge, vec3 dir, float len, float gapeAngle, bool upper, float depth, float hw0, float hw1,
                int mat = DM_MOUTH, int only = DM_SKIN) {
  dir = glm::normalize(vec3(0, dir.y, dir.z));
  vec3 up(0, dir.z, -dir.y);
  vec3 bis = rotXm(upper ? gapeAngle * 0.5f : -gapeAngle * 0.5f) * up;  // the bisector's normal
  float sg = upper ? 1.0f : -1.0f;
  m.paint(mat, fnS([=](const vec3& p) {
            vec3 q = p - hinge;
            float t = glm::dot(q, dir), h = glm::dot(q, up) * sg, b = glm::dot(q, bis) * sg;
            float hw = glm::mix(hw0, hw1, glm::clamp(t / len, 0.0f, 1.0f));
            float d = std::max(std::max(h - depth, -b), std::fabs(p.x) - hw);
            return std::max(d, std::max(-t, t - len));
          }, hinge + dir * len * 0.5f, len * 0.75f + std::max(hw0, hw1) + depth), only);
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

// Folds of skin round a neck: soft rings about the axis a->b (radius ra at a, rb at b) at the
// fractions given, sunk into the surface so that only a ridge shows.
void addFolds(Model& m, int bone, vec3 a, vec3 b, float ra, float rb, std::initializer_list<float> at, float r) {
  mat3 F = frameY(b - a);
  for (float t : at) m.add(bone, DM_SKIN, r * 0.9f, torusS(glm::mix(a, b, t), glm::mix(ra, rb, t) * 0.97f - r * 0.35f, r, F));
}

// A tail: a tube through points along the midline, each segment bound to its bone; thick
// with muscle at the root, then tapering quickly into a long, thin end.
void addTail(Model& m, int mat, const std::vector<vec3>& pts, float r0, float r1, const std::vector<int>& bones, float k) {
  int n = (int)pts.size();
  auto rad = [&](int i) {
    float t = (float)i / (n - 1);
    return glm::mix(r1, r0, std::pow(1.0f - t, 1.35f));
  };
  for (int i = 0; i + 1 < n; ++i) m.add(bones[i], mat, k, coneS(pts[i], pts[i + 1], rad(i), rad(i + 1)));
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

// ---------------------------------------------------------------------------
// Four-legged legs: the upper leg under its muscles, the elbow (pointing back) or the
// knee (forward), the forearm or shin with its calf, the wrist or ankle, and the foot: a
// broad pad that spreads on the board, with hooves (triceratops) or claws (brachiosaurus).
// ---------------------------------------------------------------------------
struct QuadLegSpec {
  float upperR, elbowR, foreR, wristR;  // front: the upper arm's radius, the elbow's, the forearm's, the wrist's
  float thighR, kneeR, shinR, ankleR;   // hind
  vec3 fPad, hPad;                      // the pads' half-sizes
  vec3 thigh;                           // the thigh muscle's half-size
  bool hooves;
};
void addQuadLegs(Model& m, const QuadJ& j, const QuadLegSpec& L) {
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int fu = sd > 0 ? QD_FLU : QD_FRU, fl = sd > 0 ? QD_FLL : QD_FRL, ff = sd > 0 ? QD_FLF : QD_FRF;
    int hu = sd > 0 ? QD_HLU : QD_HRU, hl = sd > 0 ? QD_HLL : QD_HRL, hf = sd > 0 ? QD_HLF : QD_HRF;
    auto S = [x](vec3 v) { return vec3(v.x * x, v.y, v.z); };
    vec3 FU = S(j.flu), FE = S(j.fll), FW = S(j.fwrist), FF = S(j.ffoot);
    vec3 HU = S(j.hlu), HK = S(j.hll), HA = S(j.hankle), HF = S(j.hfoot);
    // --- the foreleg: the upper arm under the shoulder muscles (deltoid in front, triceps
    // behind, bulging out over the elbow)
    m.add(fu, DM_SKIN, 0.04f, coneS(FU + vec3(0, 0.035f, -0.005f), FE, L.upperR, L.elbowR * 1.05f));
    m.add(fu, DM_SKIN, 0.03f,
          ellS(glm::mix(FU, FE, 0.42f) + vec3(x * 0.012f, 0, -0.014f), vec3(L.upperR * 0.8f, glm::length(FE - FU) * 0.42f, L.upperR * 0.86f),
               frameY(FU - FE)));
    m.add(fu, DM_SKIN, 0.02f,
          ellS(glm::mix(FU, FE, 0.3f) + vec3(x * 0.006f, 0, L.upperR * 0.45f), vec3(L.upperR * 0.6f, glm::length(FE - FU) * 0.3f, L.upperR * 0.5f),
               frameY(FU - FE)));
    // the point of the elbow, the forearm with its muscles thick below the elbow, the wrist
    m.add(fl, DM_SKIN, 0.014f, sphereS(FE + vec3(x * 0.004f, 0.004f, -L.elbowR * 0.35f), L.elbowR * 0.95f));
    m.add(fl, DM_SKIN, 0.018f, coneS(FE, FW, L.foreR, L.wristR));
    m.add(fl, DM_SKIN, 0.02f,
          ellS(glm::mix(FE, FW, 0.3f) + vec3(x * L.foreR * 0.2f, 0, L.foreR * 0.15f), vec3(L.foreR * 0.95f, glm::length(FW - FE) * 0.3f, L.foreR),
               frameY(FE - FW)));
    m.add(ff, DM_SKIN, 0.012f, sphereS(FW, L.wristR * 1.02f));
    // the forefoot: a column down to a broad pad spreading under the weight
    vec3 fp(FW.x + x * 0.004f, L.fPad.y, FF.z + 0.004f);
    m.add(ff, DM_SKIN, 0.02f, coneS(FW, fp + vec3(0, L.fPad.y * 0.4f, 0), L.wristR, L.fPad.x * 0.85f));
    m.add(ff, DM_SKIN, 0.022f, ellS(fp, L.fPad));
    // --- the hind leg: the drumstick of thigh muscle, the kneecap, the shin and its calf
    m.add(hu, DM_SKIN, 0.05f,
          ellS(glm::mix(HU, HK, 0.42f) + vec3(x * 0.012f, 0, -0.01f), L.thigh, frameY(HU - HK)));
    m.add(hu, DM_SKIN, 0.03f, coneS(HU, HK, L.thighR, L.kneeR * 1.05f));
    m.add(hl, DM_SKIN, 0.014f, sphereS(HK + vec3(x * 0.004f, 0, L.kneeR * 0.25f), L.kneeR));
    m.add(hl, DM_SKIN, 0.02f, coneS(HK, HA, L.shinR, L.ankleR));
    m.add(hl, DM_SKIN, 0.025f,
          ellS(glm::mix(HK, HA, 0.33f) + vec3(0, 0, -L.shinR * 0.45f), vec3(L.shinR * 0.85f, glm::length(HA - HK) * 0.32f, L.shinR * 0.9f),
               frameY(HK - HA)));
    m.add(hf, DM_SKIN, 0.012f, sphereS(HA + vec3(0, 0, -L.ankleR * 0.2f), L.ankleR * 1.05f));  // the heel
    // the hind foot: the metatarsus down and forward to the pad
    vec3 hp(HA.x + x * 0.004f, L.hPad.y, HF.z + 0.006f);
    m.add(hf, DM_SKIN, 0.02f, coneS(HA, hp + vec3(0, L.hPad.y * 0.4f, 0.01f), L.ankleR, L.hPad.x * 0.85f));
    m.add(hf, DM_SKIN, 0.022f, ellS(hp, L.hPad));
    // toes: hooves on short, thick toes; or a sauropod's claws
    if (L.hooves) {
      for (int k = -1; k <= 1; ++k) {
        float a = 0.38f * k + 0.1f * x;
        vec3 d(std::sin(a), 0, std::cos(a));
        vec3 f0 = fp + vec3(0, -0.002f, 0) + d * L.fPad.z * 0.55f, f1 = fp + d * (L.fPad.z * 1.02f) + vec3(0, -0.009f, 0);
        m.add(ff, DM_SKIN, 0.008f, coneS(f0, f1, 0.0135f, 0.012f));
        m.add(ff, DM_CLAW, 0.004f, ellS(f1 + d * 0.006f + vec3(0, -0.002f, 0), vec3(0.0115f, 0.0105f, 0.011f), frameY(vec3(0, 1, 0), d)));
        vec3 g0 = hp + vec3(0, -0.002f, 0) + d * L.hPad.z * 0.55f, g1 = hp + d * (L.hPad.z * 1.02f) + vec3(0, -0.009f, 0);
        m.add(hf, DM_SKIN, 0.008f, coneS(g0, g1, 0.0145f, 0.0125f));
        m.add(hf, DM_CLAW, 0.004f, ellS(g1 + d * 0.006f + vec3(0, -0.002f, 0), vec3(0.012f, 0.011f, 0.0115f), frameY(vec3(0, 1, 0), d)));
      }
    } else {
      // forefoot: a ring of blunt nails round the front of the pad and the thumb claw, turned in
      for (int k = -1; k <= 1; ++k) {
        float a = 0.42f * k - 0.15f * x;
        vec3 d(std::sin(a), 0, std::cos(a));
        m.add(ff, DM_CLAW, 0.004f, ellS(fp + d * (L.fPad.z + 0.002f) + vec3(0, -0.004f, 0), vec3(0.012f, 0.011f, 0.008f), frameY(vec3(0, 1, 0), d)));
      }
      addClaw(m, ff, fp + vec3(-x * L.fPad.x * 0.7f, 0.004f, L.fPad.z * 0.55f), vec3(-x * 0.35f, 0.25f, 1.0f), vec3(0, -1, 0), 0.032f, 0.0105f, 0.9f);
      // hind foot: three big sickle-shaped claws turned outward, a smaller fourth
      for (int k = 0; k < 4; ++k) {
        float a = -0.45f * x + 0.32f * x * k;
        vec3 d(std::sin(a), 0, std::cos(a));
        vec3 base = hp + d * L.hPad.z * 0.8f + vec3(0, 0.002f, 0);
        float len = k < 3 ? 0.03f - 0.004f * k : 0.014f;
        addClaw(m, hf, base, glm::normalize(d + vec3(x * 0.4f, 0.2f, 0)), vec3(0, -1, 0), len, k < 3 ? 0.0095f : 0.007f, 1.0f);
      }
    }
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
    addEye(m, TH_HEAD, hc + vec3(sd * 0.021f, 0.009f, -0.004f), 0.0102f, vec3(sd * 1.0f, 0.1f, 0.5f));
    m.paint(DM_DARK, ellS(vec3(sd * 0.008f, 0.428f, 0.322f), vec3(0.0035f, 0.0025f, 0.005f)), DM_SKIN);  // nostril
  }
  // the palate; the lower jaw, sculpted agape (openJaw) with its tongue, gums and teeth
  const vec3 mouthDir(0, -0.02f, 1);
  const float G = gape(D_PAWN);
  paintMouth(m, j.jaw, mouthDir, 0.125f, G, true, 0.008f, 0.012f, 0.006f);
  size_t jaw0 = m.parts.size();
  m.add(TH_JAW, DM_SKIN, 0.008f, coneS(vec3(0, 0.405f, 0.222f), vec3(0, 0.403f, 0.327f), 0.015f, 0.0072f));
  addThroat(m, TH_JAW, TH_HEAD, DM_BELLY, 0.01f, ellS(vec3(0, 0.396f, 0.235f), vec3(0.013f, 0.01f, 0.03f)), j.jaw, mouthDir, 0.05f);
  trimJaw(m, jaw0, j.jaw, mouthDir, 0.003f);
  m.add(TH_JAW, DM_MOUTH, 0.004f, ellS(vec3(0, 0.407f, 0.265f), vec3(0.0075f, 0.004f, 0.04f)));  // the tongue
  paintMouth(m, j.jaw, mouthDir, 0.125f, G, false, 0.008f, 0.011f, 0.005f);
  addTeeth(m, TH_JAW, vec3(0.008f, 0.41f, 0.262f), vec3(0.005f, 0.408f, 0.318f), 5, vec3(0, 1, 0.1f), 0.0065f, 0.0024f);
  openJaw(m, jaw0, j.jaw, G);
  addTeeth(m, TH_HEAD, vec3(0.0125f, 0.416f, 0.255f), vec3(0.0075f, 0.415f, 0.326f), 6, vec3(0, -1, 0.15f), 0.008f, 0.0042f);
  // arms folded at the sides, a folded half-wing of quills on each: separate vanes rooted along
  // the forearm and the hand, overlapping like a closed fan, the short ones at the elbow
  // hanging down and back, the long ones at the hand lying back along the forearm and past the
  // elbow; each tip rounded, so that the trailing edge is scalloped
  addTheroArms(m, j, 0.0135f, 0.0095f, 3, 0.013f, DM_FEATHER);
  for (int sd = -1; sd <= 1; sd += 2) {
    int fa = sd > 0 ? TH_LFORE : TH_RFORE;
    vec3 el(j.elL.x * sd, j.elL.y, j.elL.z), wr(j.wrL.x * sd, j.wrL.y, j.wrL.z);
    vec3 along = glm::normalize(wr - el);
    const int n = 8;
    for (int i = 0; i < n; ++i) {
      float t = 1.3f * i / (n - 1);  // (past the wrist: onto the hand)
      float u = t / 1.3f;
      vec3 root = glm::mix(el, wr, t) + vec3(sd * (0.0065f + 0.002f * u), -0.004f, 0.0f);
      vec3 back = -along + vec3(sd * 0.12f, -0.42f, 0.0f);
      vec3 d = glm::normalize(glm::mix(vec3(sd * 0.16f, -0.8f, -0.58f), glm::normalize(back), u));
      float len = glm::mix(0.03f, 0.074f, u);
      m.add(fa, DM_QUILL, 0.0025f, ellS(root + d * (len * 0.5f), vec3(0.003f, len * 0.5f, 0.0068f), frameY(d, along)));
    }
  }
  // legs: feathered thighs, scaly shins and feet, the sickle claws
  addTheroLegs(m, j, 0.03f, 0.0145f, 0.009f, 0.036f, DM_FEATHER, true);
  // the tail: stiff and straight, a fan of feathers at its end
  addTail(m, DM_FEATHER,
          {vec3(0, 0.282f, -0.08f), vec3(0, 0.284f, -0.15f), vec3(0, 0.284f, -0.22f), vec3(0, 0.283f, -0.28f), vec3(0, 0.282f, -0.33f),
           vec3(0, 0.28f, -0.39f), vec3(0, 0.277f, -0.445f)},
          0.038f, 0.006f, {TH_TAIL1, TH_TAIL1, TH_TAIL2, TH_TAIL2, TH_TAIL3, TH_TAIL3}, 0.014f);
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
  addFolds(m, TH_NECK, vec3(0, 0.49f, 0.27f), vec3(0, 0.605f, 0.42f), 0.05f, 0.035f, {0.3f, 0.48f, 0.66f}, 0.0045f);
  m.add(TH_CHEST, DM_SKIN, 0.04f, ellS(vec3(0, 0.39f, 0.08f), vec3(0.06f, 0.055f, 0.12f)));  // the belly
  // the head: a long, narrow snout ending in a rosette of teeth, a small crest
  vec3 hc(0, 0.635f, 0.465f);
  m.add(TH_HEAD, DM_SKIN, 0.0f, ellS(hc + vec3(0, 0.003f, 0), vec3(0.034f, 0.034f, 0.055f)));
  m.add(TH_HEAD, DM_SKIN, 0.02f, coneS(hc + vec3(0, -0.004f, 0.03f), vec3(0, 0.607f, 0.69f), 0.026f, 0.015f));
  m.add(TH_HEAD, DM_SKIN, 0.012f, ellS(vec3(0, 0.605f, 0.69f), vec3(0.02f, 0.018f, 0.03f)));
  m.add(TH_HEAD, DM_SCUTE, 0.008f, ellS(vec3(0, 0.663f, 0.5f), vec3(0.007f, 0.011f, 0.03f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    addEye(m, TH_HEAD, hc + vec3(sd * 0.029f, 0.017f, 0.002f), 0.0115f, vec3(sd * 1.0f, 0.15f, 0.35f));
    m.paint(DM_DARK, ellS(vec3(sd * 0.009f, 0.63f, 0.58f), vec3(0.0035f, 0.003f, 0.007f)), DM_SKIN);
  }
  // the palate; the lower jaw, trimmed to the mouth line and sculpted agape (openJaw), with its
  // tongue, gums and teeth
  const vec3 mouthDir(0, -0.05f, 1);
  const float G = gape(D_QUEEN);
  paintMouth(m, j.jaw, mouthDir, 0.27f, G, true, 0.01f, 0.02f, 0.012f);
  size_t jaw0 = m.parts.size();
  m.add(TH_JAW, DM_SKIN, 0.01f, coneS(vec3(0, 0.597f, 0.46f), vec3(0, 0.585f, 0.68f), 0.022f, 0.012f));
  m.add(TH_JAW, DM_SKIN, 0.01f, ellS(vec3(0, 0.586f, 0.685f), vec3(0.017f, 0.012f, 0.024f)));
  addThroat(m, TH_JAW, TH_HEAD, DM_BELLY, 0.01f, ellS(vec3(0, 0.585f, 0.49f), vec3(0.022f, 0.012f, 0.05f)), j.jaw, mouthDir, 0.08f);
  trimJaw(m, jaw0, j.jaw, mouthDir, 0.004f);
  m.add(TH_JAW, DM_MOUTH, 0.006f, ellS(vec3(0, 0.598f, 0.53f), vec3(0.011f, 0.006f, 0.07f), rotXm(0.05f)));  // the tongue
  paintMouth(m, j.jaw, mouthDir, 0.27f, G, false, 0.01f, 0.018f, 0.01f);
  addTeeth(m, TH_JAW, vec3(0.012f, 0.596f, 0.52f), vec3(0.01f, 0.588f, 0.69f), 7, vec3(0, 1, 0.1f), 0.01f, 0.003f);
  openJaw(m, jaw0, j.jaw, G);
  addTeeth(m, TH_HEAD, vec3(0.016f, 0.604f, 0.5f), vec3(0.012f, 0.594f, 0.7f), 8, vec3(0, -1, 0.1f), 0.012f, 0.0035f);
  // arms: strong, with hooked thumb claws
  addTheroArms(m, j, 0.022f, 0.016f, 2, 0.022f, DM_SKIN, 0.06f);
  addTheroLegs(m, j, 0.055f, 0.027f, 0.017f, 0.05f, DM_SKIN, false);
  // the root of the tail: the great muscle that pulls the thighs back
  for (int sd = -1; sd <= 1; sd += 2)
    m.add(TH_ROOT, DM_SKIN, 0.04f, ellS(vec3(sd * 0.03f, 0.43f, -0.11f), vec3(0.04f, 0.05f, 0.11f)));
  // the tail: deep, with a fin above and below, like a paddle
  addTail(m, DM_SKIN,
          {vec3(0, 0.465f, -0.12f), vec3(0, 0.455f, -0.24f), vec3(0, 0.44f, -0.35f), vec3(0, 0.427f, -0.45f), vec3(0, 0.41f, -0.55f),
           vec3(0, 0.39f, -0.66f), vec3(0, 0.37f, -0.77f)},
          0.06f, 0.008f, {TH_TAIL1, TH_TAIL1, TH_TAIL2, TH_TAIL2, TH_TAIL3, TH_TAIL3}, 0.02f);
  const vec3 tp[4] = {vec3(0, 0.465f, -0.18f), vec3(0, 0.44f, -0.35f), vec3(0, 0.41f, -0.55f), vec3(0, 0.37f, -0.78f)};
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
  m.add(TH_ROOT, DM_SKIN, 0.05f, ellS(vec3(0, 0.43f, 0.03f), vec3(0.085f, 0.07f, 0.13f)));  // the belly, sagging
  m.paint(DM_BELLY, ellS(vec3(0, 0.36f, 0.12f), vec3(0.08f, 0.06f, 0.2f)), DM_SKIN);
  // a deep, muscled neck, folds of skin round the throat
  addTube(m, DM_SKIN, {vec3(0, 0.53f, 0.25f), vec3(0, 0.6f, 0.33f), vec3(0, 0.66f, 0.39f)}, {0.088f, 0.074f, 0.066f},
          {TH_NECK, TH_NECK}, 0.04f);
  addFolds(m, TH_NECK, vec3(0, 0.53f, 0.25f), vec3(0, 0.66f, 0.39f), 0.088f, 0.066f, {0.25f, 0.45f, 0.65f, 0.83f}, 0.0065f);
  m.paint(DM_BELLY, ellS(vec3(0, 0.55f, 0.37f), vec3(0.05f, 0.04f, 0.08f), rotXm(0.6f)), DM_SKIN);
  // the shoulders and the chest muscles over the little arms
  for (int sd = -1; sd <= 1; sd += 2)
    m.add(TH_CHEST, DM_SKIN, 0.03f, ellS(vec3(sd * 0.075f, 0.47f, 0.25f), vec3(0.035f, 0.055f, 0.05f)));
  // the skull: long, deep and boxy, broad at the back, the snout sloping down and rounding
  // off at the nose; bosses over the eyes, a knobbly ridge down the nose. Its underside is the
  // palate.
  Fn skull = [](const vec3& p) {
    const float z0 = 0.4f, z1 = 0.675f;
    float t = glm::clamp((p.z - z0) / (z1 - z0), 0.0f, 1.0f);
    float e = std::max(0.0f, (t - 0.74f) / 0.26f), round = std::sqrt(std::max(0.0f, 1.0f - e * e));
    float w = glm::mix(0.076f, 0.04f, t) * glm::mix(0.4f, 1.0f, round), h = glm::mix(0.07f, 0.036f, t) * glm::mix(0.6f, 1.0f, round);
    float yc = glm::mix(0.73f, 0.697f, t) - 0.008f * e;
    float ax = std::fabs(p.x) / w, ay = std::fabs(p.y - yc) / h;
    float r = std::pow(std::pow(ax, 2.6f) + std::pow(ay, 2.6f), 1.0f / 2.6f);
    return smax((r - 1.0f) * std::min(w, h), std::max(z0 - p.z, p.z - z1), 0.02f) * 0.7f;
  };
  m.add(TH_HEAD, DM_SKIN, 0.0f, fnS(skull, vec3(0, 0.72f, 0.53f), 0.16f));
  const vec3 mouthDir(0, -0.004f, 1);
  const float G = gape(D_KING);
  paintMouth(m, j.jaw, mouthDir, 0.21f, G, true, 0.009f, 0.046f, 0.012f);  // the palate, inside the lips
  for (int sd = -1; sd <= 1; sd += 2) {
    m.add(TH_HEAD, DM_SKIN, 0.02f, ellS(vec3(sd * 0.062f, 0.69f, 0.465f), vec3(0.022f, 0.032f, 0.05f)));  // the cheeks
    m.add(TH_HEAD, DM_SCUTE, 0.012f, ellS(vec3(sd * 0.045f, 0.773f, 0.49f), vec3(0.018f, 0.012f, 0.024f)));  // brow bosses
    m.add(TH_HEAD, DM_SCUTE, 0.01f, ellS(vec3(sd * 0.04f, 0.769f, 0.533f), vec3(0.011f, 0.011f, 0.016f)));  // the lacrimal horns
    // the jaw muscles bulging at the back of the skull, which is broad behind the eyes
    m.add(TH_HEAD, DM_SKIN, 0.025f, ellS(vec3(sd * 0.056f, 0.735f, 0.425f), vec3(0.03f, 0.045f, 0.042f)));
    addEye(m, TH_HEAD, vec3(sd * 0.057f, 0.746f, 0.497f), 0.0135f, vec3(sd * 0.75f, 0.1f, 0.7f));
    // lips: a ridge of scaly skin along the upper jaw, over the roots of the teeth
    addTube(m, DM_SKIN, {vec3(sd * 0.059f, 0.666f, 0.455f), vec3(sd * 0.05f, 0.664f, 0.555f), vec3(sd * 0.037f, 0.663f, 0.648f)},
            {0.0075f, 0.007f, 0.0055f}, {TH_HEAD, TH_HEAD}, 0.01f);
    m.paint(DM_DARK, ellS(vec3(sd * 0.022f, 0.72f, 0.655f), vec3(0.008f, 0.005f, 0.01f)), DM_SKIN);
  }
  addScutes(m, TH_HEAD, vec3(0, 0.762f, 0.53f), vec3(0, 0.733f, 0.63f), 5, 0.009f);
  // the lower jaw: deep at the back, tapering to a rounded chin, narrower than the skull (the
  // upper teeth close over it on the outside); its top is the tongue and gums
  Fn mandible = [](const vec3& p) {
    const float z0 = 0.43f, z1 = 0.658f;
    float t = glm::clamp((p.z - z0) / (z1 - z0), 0.0f, 1.0f);
    float e = std::max(0.0f, (t - 0.72f) / 0.28f), round = std::sqrt(std::max(0.0f, 1.0f - e * e));
    float w = glm::mix(0.058f, 0.031f, t) * glm::mix(0.45f, 1.0f, round), h = glm::mix(0.038f, 0.019f, t);
    float yc = glm::mix(0.625f, 0.644f, t);
    float ax = std::fabs(p.x) / w, ay = std::fabs(p.y - yc) / h;
    float r = std::pow(std::pow(ax, 2.4f) + std::pow(ay, 2.4f), 1.0f / 2.4f);
    return smax((r - 1.0f) * std::min(w, h), std::max(z0 - p.z, p.z - z1), 0.02f) * 0.7f;
  };
  // (sculpted agape: openJaw; the pose closes it)
  size_t jaw0 = m.parts.size();
  addThroat(m, TH_JAW, TH_HEAD, DM_SKIN, 0.01f, fnS(mandible, vec3(0, 0.64f, 0.54f), 0.14f), j.jaw, mouthDir, 0.06f);
  addThroat(m, TH_JAW, TH_HEAD, DM_BELLY, 0.02f, ellS(vec3(0, 0.615f, 0.49f), vec3(0.05f, 0.022f, 0.08f)), j.jaw, mouthDir, 0.1f);
  trimJaw(m, jaw0, j.jaw, mouthDir, 0.004f);
  m.add(TH_JAW, DM_MOUTH, 0.006f, ellS(vec3(0, 0.657f, 0.555f), vec3(0.024f, 0.009f, 0.075f)));  // the tongue
  paintMouth(m, j.jaw, mouthDir, 0.2f, G, false, 0.012f, 0.044f, 0.01f);  // gums, inside the jaw's rim
  addTeeth(m, TH_JAW, vec3(0.042f, 0.655f, 0.505f), vec3(0.024f, 0.657f, 0.63f), 6, glm::normalize(vec3(0, 1, 0.1f)), 0.017f, 0.006f);
  openJaw(m, jaw0, j.jaw, G);
  addTeeth(m, TH_HEAD, vec3(0.053f, 0.666f, 0.47f), vec3(0.031f, 0.664f, 0.645f), 8, glm::normalize(vec3(0, -1, 0.12f)), 0.027f, 0.0075f);
  // arms: tiny, two-fingered
  addTheroArms(m, j, 0.02f, 0.014f, 2, 0.016f, DM_SKIN);
  // legs: huge
  addTheroLegs(m, j, 0.084f, 0.044f, 0.028f, 0.07f, DM_SKIN, false);
  // the root of the tail: the great muscle that pulls the thighs back
  for (int sd = -1; sd <= 1; sd += 2)
    m.add(TH_ROOT, DM_SKIN, 0.05f, ellS(vec3(sd * 0.04f, 0.47f, -0.13f), vec3(0.055f, 0.07f, 0.14f)));
  // the tail
  addTail(m, DM_SKIN,
          {vec3(0, 0.52f, -0.12f), vec3(0, 0.515f, -0.26f), vec3(0, 0.5f, -0.37f), vec3(0, 0.485f, -0.47f), vec3(0, 0.465f, -0.56f),
           vec3(0, 0.445f, -0.66f), vec3(0, 0.42f, -0.765f)},
          0.09f, 0.008f, {TH_TAIL1, TH_TAIL1, TH_TAIL2, TH_TAIL2, TH_TAIL3, TH_TAIL3}, 0.03f);
  // scutes along the back, from the neck to the tail
  addScutes(m, TH_NECK, vec3(0, 0.62f, 0.32f), vec3(0, 0.67f, 0.38f), 3, 0.012f);
  addScutes(m, TH_CHEST, vec3(0, 0.6f, 0.06f), vec3(0, 0.6f, 0.22f), 4, 0.013f);
  addScutes(m, TH_ROOT, vec3(0, 0.605f, -0.16f), vec3(0, 0.605f, 0.0f), 4, 0.013f);
  addScutes(m, TH_TAIL1, vec3(0, 0.6f, -0.2f), vec3(0, 0.56f, -0.34f), 3, 0.011f);
  addScutes(m, TH_TAIL2, vec3(0, 0.552f, -0.39f), vec3(0, 0.525f, -0.52f), 3, 0.009f);
  return m;
}

// The pteranodon: a crest swept back from the skull, a long toothless beak, a small
// furry body (pycnofibres, painted as skin), wings of skin from the long fourth finger
// to the legs. Sculpted with its wings spread; the pose folds them.
Model pteroModel() {
  using namespace pj;
  Model m;
  m.skel = &skeleton(D_KNIGHT);
  // the body, tilted up at the shoulders, in its coat of fine fur; the breast muscles that
  // drive the wings
  m.add(PT_BODY, DM_FUZZ, 0.0f, ellS(vec3(0, 0.298f, -0.008f), vec3(0.061f, 0.066f, 0.116f), rotXm(-0.6f)));
  m.add(PT_BODY, DM_FUZZ, 0.025f, ellS(vec3(0, 0.34f, 0.055f), vec3(0.072f, 0.052f, 0.052f)));  // the shoulders
  m.add(PT_BODY, DM_FUZZ, 0.025f, ellS(vec3(0, 0.3f, 0.07f), vec3(0.052f, 0.05f, 0.04f)));   // the breast
  // the neck, furred at its base
  addTube(m, DM_SKIN, {vec3(0, 0.35f, 0.07f), vec3(0, 0.43f, 0.125f), vec3(0, 0.485f, 0.158f)}, {0.03f, 0.022f, 0.018f},
          {PT_NECK, PT_NECK}, 0.012f);
  m.add(PT_NECK, DM_FUZZ, 0.015f, ellS(vec3(0, 0.37f, 0.085f), vec3(0.03f, 0.036f, 0.03f), rotXm(-0.6f)));
  // the head: skull, the long beak, the crest
  vec3 hc(0, 0.5f, 0.195f);
  m.add(PT_HEAD, DM_SKIN, 0.0f, ellS(hc, vec3(0.024f, 0.028f, 0.045f)));
  // the beak: upper and lower halves meeting along the mouth line (the lower sculpted agape: openJaw)
  const vec3 mouthDir(0, -0.075f, 1);
  const float G = gape(D_KNIGHT);
  size_t beak0 = m.parts.size();
  m.add(PT_HEAD, DM_HORN, 0.012f, coneS(hc + vec3(0, -0.004f, 0.03f), vec3(0, 0.466f, 0.5f), 0.019f, 0.0018f));
  trimJaw(m, beak0, jaw, mouthDir, 0.003f, true);
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
  paintMouth(m, jaw, mouthDir, 0.09f, G, true, 0.006f, 0.012f, 0.007f, DM_MOUTH, DM_HORN);  // (the rest of the beak is horn)
  paintMouth(m, jaw, mouthDir, 0.09f, G, true, 0.006f, 0.012f, 0.007f, DM_MOUTH, DM_SKIN);
  size_t jaw0 = m.parts.size();
  m.add(PT_JAW, DM_HORN, 0.006f, coneS(vec3(0, 0.483f, 0.215f), vec3(0, 0.46f, 0.49f), 0.0135f, 0.0016f));
  m.add(PT_JAW, DM_SKIN, 0.008f, ellS(vec3(0, 0.476f, 0.23f), vec3(0.014f, 0.01f, 0.035f)));  // the throat pouch
  trimJaw(m, jaw0, jaw, mouthDir, 0.003f);
  paintMouth(m, jaw, mouthDir, 0.09f, G, false, 0.006f, 0.011f, 0.006f, DM_MOUTH, DM_HORN);
  paintMouth(m, jaw, mouthDir, 0.09f, G, false, 0.006f, 0.011f, 0.006f, DM_MOUTH, DM_SKIN);
  openJaw(m, jaw0, jaw, G);
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
    m.add(ua, DM_SKIN, 0.012f, coneS(S, E, 0.026f, 0.0175f));
    m.add(ua, DM_SKIN, 0.01f, ellS(glm::mix(S, E, 0.35f) + vec3(0, 0, 0.008f), vec3(0.036f, 0.017f, 0.021f)));  // the crest of the arm
    m.add(fa, DM_SKIN, 0.008f, coneS(E, W, 0.0175f, 0.0135f));
    m.add(fa, DM_SKIN, 0.008f, ellS(glm::mix(E, W, 0.3f), vec3(0.016f, 0.0145f, 0.034f), frameY(vec3(0, 1, 0), W - E)));  // its muscles
    m.add(fa, DM_SKIN, 0.006f, sphereS(W, 0.0165f));  // the wrist
    // the pteroid: a slender bone at the wrist pointing back at the shoulder, holding up the
    // membrane in front of the arm
    m.add(fa, DM_SKIN, 0.004f, coneS(W, W + (S - W) * 0.3f + vec3(0, 0.004f, 0.012f), 0.005f, 0.0024f));
    m.add(mc, DM_SKIN, 0.006f, coneS(W, N, 0.0135f, 0.0115f));
    m.add(mc, DM_SKIN, 0.005f, sphereS(N, 0.0152f));  // the knuckle of the wing finger
    // the wing finger: four long joints, each a little thinner
    for (int s = 0; s < 4; ++s) {
      vec3 a = glm::mix(N, T, s / 4.0f), b = glm::mix(N, T, (s + 1) / 4.0f);
      float ra = glm::mix(0.0118f, 0.003f, s / 4.0f), rb = glm::mix(0.0118f, 0.003f, (s + 1) / 4.0f);
      m.add(fg, DM_SKIN, 0.005f, coneS(a, b, ra, rb));
      if (s > 0) m.add(fg, DM_SKIN, 0.003f, sphereS(a, ra * 1.18f));
    }
    for (int f = 0; f < 3; ++f) {  // the little fingers it walks on
      vec3 d = glm::normalize(vec3(x * 0.25f, -0.3f + 0.2f * f, 1.0f));
      vec3 f1 = N + d * 0.016f + vec3(0, -0.004f, 0);
      m.add(mc, DM_SKIN, 0.003f, coneS(N, f1, 0.0052f, 0.0036f));
      addClaw(m, mc, f1, d, vec3(0, -1, 0), 0.01f, 0.0032f, 1.2f);
    }
    // the membrane: from the leading edge (arm, hand and finger) back to a trailing edge that
    // sweeps in one smooth curve from the wing tip to the knee - narrow at the tip, broad
    // near the body - billowing a little between them. It is cut into strips; each rides on
    // its bone of the leading edge and on the trailing edge's own bone (drawn in against the
    // wing as it folds), weighted by how near it is to each edge.
    int tr = sd > 0 ? PT_LTRAIL : PT_RTRAIL, tr2 = sd > 0 ? PT_LTRAIL2 : PT_RTRAIL2;
    int trA = sd > 0 ? PT_LTRAILA : PT_RTRAILA, trH = sd > 0 ? PT_LTRAILH : PT_RTRAILH;
    vec3 K1 = K + vec3(x * 0.006f, 0.012f, 0.0f);
    vec3 C1(0.62f * x, 0.36f, -0.15f), C2(0.22f * x, 0.33f, -0.155f);
    auto trail = [=](float u) {  // from the tip (u = 0) to the knee (1)
      float a = 1.0f - u;
      return a * a * a * T + 3.0f * a * a * u * C1 + 3.0f * a * u * u * C2 + u * u * u * K1;
    };
    const vec3 lead[5] = {S, E, W, N, T};
    auto leadAt = [&](float ax) {  // the leading edge where |x| = ax
      for (int i = 0; i < 4; ++i) {
        float x0 = std::fabs(lead[i].x), x1 = std::fabs(lead[i + 1].x);
        if (ax <= x1 || i == 3) return glm::mix(lead[i], lead[i + 1], glm::clamp((ax - x0) / (x1 - x0), 0.0f, 1.0f));
      }
      return T;
    };
    auto trailAt = [&](float ax) {  // the trailing edge where |x| = ax (|x| shrinks from the tip to the knee)
      float lo = 0.0f, hi = 1.0f;
      for (int it = 0; it < 24; ++it) {
        float mid = 0.5f * (lo + hi);
        if (std::fabs(trail(mid).x) > ax) lo = mid;
        else hi = mid;
      }
      return trail(0.5f * (lo + hi));
    };
    std::vector<vec3> leadPts(lead, lead + 5);
    // the membrane follows its trailing-edge bone (which, spread, moves exactly with the wing
    // bone, and folded squeezes it in against that bone), except right on the leading edge,
    // where it is fixed to the bone
    Fn wEdge = [leadPts](const vec3& p) {
      float best = 1e9f;
      for (size_t i = 0; i + 1 < leadPts.size(); ++i) {
        vec3 uv = leadPts[i + 1] - leadPts[i];
        float t = glm::clamp(glm::dot(p - leadPts[i], uv) / glm::dot(uv, uv), 0.0f, 1.0f);
        best = std::min(best, glm::length(p - (leadPts[i] + uv * t)));
      }
      return 1.0f - glm::smoothstep(0.006f, 0.02f, best);
    };
    // strips between these spans (|x|), split at the joints so each lies on one bone
    const float cuts[] = {std::fabs(K1.x), std::fabs(E.x), std::fabs(W.x), std::fabs(N.x), std::fabs(T.x) - 0.004f};
    const int pieces[] = {1, 2, 2, 5};
    const int leadBone[] = {ua, fa, mc, fg}, trailBone[] = {trA, tr, trH, tr2};
    for (int s = 0; s < 4; ++s)
      for (int k = 0; k < pieces[s]; ++k) {
        float a0 = glm::mix(cuts[s], cuts[s + 1], (float)k / pieces[s]), a1 = glm::mix(cuts[s], cuts[s + 1], (float)(k + 1) / pieces[s]);
        vec3 l0 = leadAt(a0), l1 = leadAt(a1), t0 = trailAt(a0), t1 = trailAt(a1);
        vec3 m0 = glm::mix(l0, t0, 0.45f) + vec3(0, 0.06f * glm::length(l0 - t0), 0);  // the billow
        vec3 m1 = glm::mix(l1, t1, 0.45f) + vec3(0, 0.06f * glm::length(l1 - t1), 0);
        int lb = leadBone[s], tb = trailBone[s];
        m.addStretch(lb, tb, DM_MEMBRANE, 0.004f, sheetS(l0, l1, m1, 0.0042f), wEdge);
        m.addStretch(lb, tb, DM_MEMBRANE, 0.004f, sheetS(l0, m1, m0, 0.0042f), wEdge);
        m.addStretch(lb, tb, DM_MEMBRANE, 0.004f, sheetS(m0, m1, t1, 0.0042f), wEdge);
        m.addStretch(lb, tb, DM_MEMBRANE, 0.004f, sheetS(m0, t1, t0, 0.0042f), wEdge);
      }
    // ... and where it meets the flank, from the shoulder down to the knee
    m.addStretch(ua, trA, DM_MEMBRANE, 0.004f, sheetS(S + vec3(0, -0.006f, -0.01f), leadAt(cuts[0]), K1, 0.0042f), wEdge);
    m.addStretch(fa, ua, DM_MEMBRANE, 0.004f, sheetS(S + vec3(0, 0.002f, 0.012f), W + vec3(0, 0.002f, 0.004f), E + vec3(0, 0.002f, 0.02f), 0.0038f),
                 along(W, S));  // the propatagium in front of the elbow
    // legs and clawed feet
    m.add(th, DM_SKIN, 0.01f, coneS(H, K, 0.021f, 0.0115f));
    m.add(th, DM_FUZZ, 0.012f, ellS(glm::mix(H, K, 0.3f), vec3(0.019f, 0.03f, 0.022f)));  // the thigh, furred
    m.add(sh, DM_SKIN, 0.006f, coneS(K, F, 0.0108f, 0.0074f));
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
  // the bristly back: a row of low knobs following the curve of the back
  for (int i = 0; i < 7; ++i) {
    float z = -0.25f + 0.05f * i;
    m.add(QD_BODY, DM_SCUTE, 0.01f, ellS(vec3(0, 0.486f - 1.2f * (z + 0.09f) * (z + 0.09f), z), vec3(0.012f, 0.009f, 0.016f)));
  }
  // the neck under the frill
  addTube(m, DM_SKIN, {vec3(0, 0.33f, 0.16f), vec3(0, 0.33f, 0.23f), vec3(0, 0.325f, 0.3f)}, {0.1f, 0.092f, 0.085f},
          {QD_NECK1, QD_NECK3}, 0.04f);
  addFolds(m, QD_NECK2, vec3(0, 0.33f, 0.16f), vec3(0, 0.325f, 0.3f), 0.1f, 0.085f, {0.3f, 0.55f, 0.8f}, 0.007f);
  // the head, its underside cut back to the mouth line in front of the hinge (the lower jaw is
  // sculpted agape: openJaw)
  const vec3 mouthDir(0, -0.15f, 1);
  const float G = gape(D_BISHOP);
  size_t head0 = m.parts.size();
  m.add(QD_HEAD, DM_SKIN, 0.0f, ellS(vec3(0, 0.31f, 0.4f), vec3(0.078f, 0.085f, 0.13f)));
  m.add(QD_HEAD, DM_SKIN, 0.03f, coneS(vec3(0, 0.3f, 0.45f), vec3(0, 0.235f, 0.575f), 0.058f, 0.026f));
  m.add(QD_HEAD, DM_HORN, 0.006f, coneS(vec3(0, 0.245f, 0.565f), vec3(0, 0.195f, 0.62f), 0.026f, 0.005f));  // the beak
  trimJaw(m, head0, j.jaw, mouthDir, 0.006f, true);
  for (int sd = -1; sd <= 1; sd += 2) {
    addEye(m, QD_HEAD, vec3(sd * 0.063f, 0.352f, 0.432f), 0.0145f, vec3(sd * 1.0f, 0.15f, 0.25f));
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
  // the palate; the lower jaw and its beak
  paintMouth(m, j.jaw, mouthDir, 0.2f, G, true, 0.008f, 0.05f, 0.03f);
  size_t jaw0 = m.parts.size();
  m.add(QD_JAW, DM_SKIN, 0.015f, ellS(vec3(0, 0.229f, 0.46f), vec3(0.058f, 0.041f, 0.11f)));
  m.add(QD_JAW, DM_HORN, 0.006f, coneS(vec3(0, 0.215f, 0.54f), vec3(0, 0.205f, 0.595f), 0.024f, 0.006f));
  trimJaw(m, jaw0, j.jaw, mouthDir, 0.005f);
  m.add(QD_JAW, DM_MOUTH, 0.008f, ellS(vec3(0, 0.231f, 0.44f), vec3(0.03f, 0.01f, 0.08f), rotXm(0.15f)));  // the tongue
  paintMouth(m, j.jaw, mouthDir, 0.19f, G, false, 0.01f, 0.046f, 0.03f);
  openJaw(m, jaw0, j.jaw, G);
  // legs: the front ones half-sprawled (elbows out and back), the hind ones pillars; broad
  // padded feet with blunt hooves
  QuadLegSpec L;
  L.upperR = 0.058f; L.elbowR = 0.036f; L.foreR = 0.037f; L.wristR = 0.029f;
  L.thighR = 0.066f; L.kneeR = 0.04f; L.shinR = 0.04f; L.ankleR = 0.031f;
  L.fPad = vec3(0.04f, 0.022f, 0.043f); L.hPad = vec3(0.042f, 0.022f, 0.05f);
  L.thigh = vec3(0.062f, 0.12f, 0.085f);
  L.hooves = true;
  addQuadLegs(m, j, L);
  // the tail: heavy at the root, held off the ground, tapering
  addTail(m, DM_SKIN,
          {vec3(0, 0.37f, -0.27f), vec3(0, 0.35f, -0.38f), vec3(0, 0.32f, -0.48f), vec3(0, 0.293f, -0.555f), vec3(0, 0.262f, -0.63f),
           vec3(0, 0.225f, -0.705f), vec3(0, 0.185f, -0.785f)},
          0.095f, 0.008f, {QD_TAIL1, QD_TAIL1, QD_TAIL2, QD_TAIL2, QD_TAIL3, QD_TAIL3}, 0.03f);
  return m;
}

// The brachiosaurus: a deep chest on pillar legs, longer in front; the neck rising
// like a tower; a small head with a domed nose; a tail sweeping down to the board.
Model brachioModel() {
  QuadJ j = quadBase(D_ROOK);
  Model m;
  m.skel = &skeleton(D_ROOK);
  m.add(QD_BODY, DM_SKIN, 0.0f, ellS(vec3(0, 0.57f, 0.06f), vec3(0.15f, 0.165f, 0.2f)));
  m.add(QD_BODY, DM_SKIN, 0.09f, ellS(vec3(0, 0.54f, -0.13f), vec3(0.135f, 0.13f, 0.15f)));
  m.add(QD_BODY, DM_SKIN, 0.06f, ellS(vec3(0, 0.64f, 0.15f), vec3(0.12f, 0.095f, 0.1f)));  // the withers
  m.add(QD_BODY, DM_SKIN, 0.07f, ellS(vec3(0, 0.46f, 0.0f), vec3(0.12f, 0.075f, 0.17f)));  // the belly, sagging
  m.paint(DM_BELLY, ellS(vec3(0, 0.42f, 0.0f), vec3(0.1f, 0.05f, 0.2f)), DM_SKIN);
  // the neck, in three bones, its underside pale
  addTube(m, DM_SKIN, {vec3(0, 0.62f, 0.19f), vec3(0, 0.86f, 0.3f), vec3(0, 1.03f, 0.37f), vec3(0, 1.185f, 0.43f)},
          {0.076f, 0.055f, 0.044f, 0.034f}, {QD_NECK1, QD_NECK2, QD_NECK3}, 0.03f);
  addFolds(m, QD_NECK1, vec3(0, 0.62f, 0.19f), vec3(0, 0.86f, 0.3f), 0.076f, 0.055f, {0.22f, 0.42f, 0.62f}, 0.0045f);
  m.paint(DM_BELLY, fnS([](const vec3& p) {
            vec3 a(0, 0.62f, 0.19f), b(0, 1.185f, 0.43f);
            vec3 ab = b - a;
            float t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
            vec3 c = a + ab * t;
            vec3 front = glm::normalize(vec3(0, -0.35f, 1.0f));
            return 0.25f * (0.028f - glm::dot(p - c, front));
          }, vec3(0, 0.9f, 0.31f), 0.4f), DM_SKIN);
  // the head: small, with the arched nose of a brachiosaur
  // (a wedge from the braincase to a long, low muzzle, rounded at the end; the arch of the
  // nose rising over the eyes); its underside cut back to the mouth line
  const vec3 mouthDir(0, -0.02f, 1);
  const float G = gape(D_ROOK);
  size_t head0 = m.parts.size();
  m.add(QD_HEAD, DM_SKIN, 0.0f, fnS([](const vec3& p) {
          const float z0 = 0.425f, z1 = 0.562f;
          float t = glm::clamp((p.z - z0) / (z1 - z0), 0.0f, 1.0f);
          float e = std::max(0.0f, (t - 0.78f) / 0.22f), round = std::sqrt(std::max(0.0f, 1.0f - e * e));
          float w = glm::mix(0.03f, 0.0185f, t) * glm::mix(0.5f, 1.0f, round), h = glm::mix(0.031f, 0.0165f, t) * glm::mix(0.6f, 1.0f, round);
          float yc = glm::mix(1.217f, 1.2f, t);
          float ax = std::fabs(p.x) / w, ay = std::fabs(p.y - yc) / h;
          float r = std::pow(std::pow(ax, 2.4f) + std::pow(ay, 2.4f), 1.0f / 2.4f);
          return smax((r - 1.0f) * std::min(w, h), std::max(z0 - p.z, p.z - z1), 0.018f) * 0.75f;
        }, vec3(0, 1.21f, 0.49f), 0.09f));
  m.add(QD_HEAD, DM_SKIN, 0.018f, ellS(vec3(0, 1.241f, 0.47f), vec3(0.017f, 0.025f, 0.026f), rotXm(-0.35f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    addEye(m, QD_HEAD, vec3(sd * 0.0285f, 1.229f, 0.466f), 0.0105f, vec3(sd * 1.0f, 0.1f, 0.3f));
    m.paint(DM_DARK, ellS(vec3(sd * 0.009f, 1.266f, 0.466f), vec3(0.0055f, 0.005f, 0.008f)), DM_SKIN);  // nostrils high on the arch
  }
  trimJaw(m, head0, j.jaw, mouthDir, 0.004f, true, head0 + 1);
  // the palate; the lower jaw (sculpted agape: openJaw) and its tongue
  paintMouth(m, j.jaw, mouthDir, 0.095f, G, true, 0.006f, 0.016f, 0.01f);
  size_t jaw0 = m.parts.size();
  m.add(QD_JAW, DM_SKIN, 0.01f, ellS(vec3(0, 1.183f, 0.502f), vec3(0.018f, 0.011f, 0.052f)));
  trimJaw(m, jaw0, j.jaw, mouthDir, 0.003f);
  m.add(QD_JAW, DM_MOUTH, 0.004f, ellS(vec3(0, 1.1825f, 0.5f), vec3(0.009f, 0.0045f, 0.035f)));  // the tongue
  paintMouth(m, j.jaw, mouthDir, 0.09f, G, false, 0.008f, 0.015f, 0.009f);
  openJaw(m, jaw0, j.jaw, G);
  addTeeth(m, QD_HEAD, vec3(0.013f, 1.188f, 0.51f), vec3(0.008f, 1.188f, 0.54f), 4, vec3(0, -1, 0.3f), 0.007f, 0.0026f);
  // pillar legs, the front ones taller: columns of muscle, elephantine feet (the forefeet a
  // horseshoe of toes with a single thumb claw, the hind feet with three great claws)
  QuadLegSpec L;
  L.upperR = 0.06f; L.elbowR = 0.04f; L.foreR = 0.042f; L.wristR = 0.035f;
  L.thighR = 0.07f; L.kneeR = 0.044f; L.shinR = 0.046f; L.ankleR = 0.037f;
  L.fPad = vec3(0.043f, 0.019f, 0.042f); L.hPad = vec3(0.047f, 0.019f, 0.052f);
  L.thigh = vec3(0.068f, 0.14f, 0.095f);
  L.hooves = false;
  addQuadLegs(m, j, L);
  // the tail: a long whip sweeping down towards the board
  addTail(m, DM_SKIN,
          {vec3(0, 0.56f, -0.2f), vec3(0, 0.52f, -0.33f), vec3(0, 0.47f, -0.45f), vec3(0, 0.42f, -0.54f), vec3(0, 0.36f, -0.63f),
           vec3(0, 0.295f, -0.725f), vec3(0, 0.22f, -0.825f)},
          0.085f, 0.007f, {QD_TAIL1, QD_TAIL1, QD_TAIL2, QD_TAIL2, QD_TAIL3, QD_TAIL3}, 0.03f);
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
float gape(int kind) {
  static const float G[6] = {0.35f, 0.25f, 0.3f, 0.32f, 0.3f, 0.3f};
  return G[std::max(0, std::min(5, kind))];
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
float bodyMass(int kind) {
  static const float M[6] = {1.0f, 0.8f, 7.0f, 30.0f, 9.0f, 12.0f};
  return M[std::max(0, std::min(5, kind))];
}
float bodyHalfWidth(int kind) {
  static const float W[6] = {0.05f, 0.05f, 0.13f, 0.15f, 0.06f, 0.09f};
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
      bones[0] = QD_FLF;
      bones[1] = QD_FRF;
      bones[2] = QD_HLF;
      bones[3] = QD_HRF;
      return 4;
    }
  }
}

// Orders a bone's balls in runs of kBulkRun lying close together (split again and again
// across their longest side, a whole number of runs on one side).
static void orderRuns(std::vector<Ball>& v, int first, int end) {
  const int run = kBulkRun;
  int n = end - first;
  if (n <= run) return;
  vec3 lo(1e9f), hi(-1e9f);
  for (int i = first; i < end; ++i) {
    lo = glm::min(lo, v[i].c);
    hi = glm::max(hi, v[i].c);
  }
  vec3 e = hi - lo;
  int ax = e.x >= e.y && e.x >= e.z ? 0 : (e.y >= e.z ? 1 : 2);
  int mid = first + run * ((n + 2 * run - 1) / (2 * run));  // (a whole number of runs on one side)
  std::stable_sort(v.begin() + first, v.begin() + end, [ax](const Ball& a, const Ball& b) { return a.c[ax] < b.c[ax]; });
  orderRuns(v, first, mid);
  orderRuns(v, mid, end);
}

// The bulk of an animal: balls packed inside its body, each riding on the bone its middle is
// skinned to (rest-pose model space, in bone order). Every candidate is a point inside with the
// biggest ball that fits there; the one over which the balls already taken leave the skin
// furthest out goes next, until they leave none of it more than a few millimetres out (or there
// are 'most' of them). Then the same, with smaller balls, for the horns and the beak, and for the
// frill, the sail and the crest (thin, but they must not pass through anyone either); and the
// last of the tail and the beak, to their tips. Thin skin, wings, claws and teeth are left out.
static std::vector<Ball> fillBulk(const Model& m, vec3 lo, vec3 hi, float rmin, int most,
                                  const std::vector<std::pair<vec3, vec3>>& tips) {
  const float hc = 0.02f, hf = 0.004f, hf2 = 0.002f, rminThin = 0.0015f;
  struct Cand {
    vec3 p;
    float r;
    float ext;  // how far below the skin over it the balls taken so far leave it (as of 'seen' of them)
    int seen;
    bool trimmed;
  };
  // rays all round (an icosahedron and the middles of its edges)
  std::vector<vec3> dirs;
  {
    const float g = 1.618034f;
    vec3 ico[12] = {{-1, g, 0}, {1, g, 0}, {-1, -g, 0}, {1, -g, 0}, {0, -1, g}, {0, 1, g},
                    {0, -1, -g}, {0, 1, -g}, {g, 0, -1}, {g, 0, 1}, {-g, 0, -1}, {-g, 0, 1}};
    for (const vec3& a : ico) dirs.push_back(glm::normalize(a));
    for (int i = 0; i < 12; ++i)
      for (int j = i + 1; j < 12; ++j)
        if (glm::length(ico[i] - ico[j]) < 2.1f) dirs.push_back(glm::normalize(ico[i] + ico[j]));
  }
  glm::ivec3 n = glm::max(glm::ivec3(1), glm::ivec3(glm::ceil((hi - lo) / hc)));
  std::vector<std::vector<Cand>> slab((size_t)n.z), slabRod((size_t)n.z), slabSheet((size_t)n.z);
  // the coarse cells: deep inside, a cell's middle will do; near the skin, it is sampled finely
  // (and more finely still where it is horn, frill, sail or crest)
  parallelFor(n.z, [&](int k) {
    std::vector<Cand>& out = slab[(size_t)k];
    for (int j = 0; j < n.y; ++j)
      for (int i = 0; i < n.x; ++i) {
        vec3 q = lo + (vec3((float)i, (float)j, (float)k) + 0.5f) * hc;
        float d = m.eval(q);
        if (d > hc * 0.87f) continue;  // all outside
        if (d < -hc * 1.5f) {
          out.push_back({q, -d, -d, 0, false});
          continue;
        }
        const int f = 5;
        for (int c = 0; c < f; ++c)
          for (int b = 0; b < f; ++b)
            for (int a = 0; a < f; ++a) {
              vec3 p = q + (vec3((float)a, (float)b, (float)c) - (f - 1) * 0.5f) * hf;
              float e = m.eval(p);
              if (e < -rmin) out.push_back({p, -e, -e, 0, false});
            }
        int mat = m.materialAt(q, nullptr, nullptr);
        if (mat == DM_HORN || mat == DM_FRILL || mat == DM_SAIL || mat == DM_CREST) {
          std::vector<Cand>& thin = mat == DM_HORN ? slabRod[(size_t)k] : slabSheet[(size_t)k];
          const int f2 = 10;
          for (int c = 0; c < f2; ++c)
            for (int b = 0; b < f2; ++b)
              for (int a = 0; a < f2; ++a) {
                vec3 p = q + (vec3((float)a, (float)b, (float)c) - (f2 - 1) * 0.5f) * hf2;
                float e = m.eval(p);
                if (e < -rminThin) thin.push_back({p, -e, -e, 0, false});
              }
        }
      }
  });
  // how far below the skin a ball b leaves the skin over a candidate (the skin is c.r from it;
  // inside b, b's surface is sqrt(b.r^2 - d^2) away across the line between them)
  auto gap = [](const vec3& p, float r, const Ball& b) {
    float d = glm::length(p - b.c);
    return d < b.r ? r - std::sqrt(b.r * b.r - d * d) : r + d - b.r;
  };
  // a max-heap on 'ext' (ties: the bigger ball, then the position, so that it is repeatable)
  auto less = [](const Cand& a, const Cand& b) {
    if (a.ext != b.ext) return a.ext < b.ext;
    if (a.r != b.r) return a.r < b.r;
    if (a.p.z != b.p.z) return a.p.z > b.p.z;
    if (a.p.y != b.p.y) return a.p.y > b.p.y;
    return a.p.x > b.p.x;
  };
  std::vector<Ball> out;
  auto fill = [&](std::vector<std::vector<Cand>>& from, int budget, float smallest, float enough) {
    std::vector<Cand> heap;
    for (auto& s : from) heap.insert(heap.end(), s.begin(), s.end());
    for (Cand& c : heap) c.seen = 0;
    std::make_heap(heap.begin(), heap.end(), less);
    int taken = 0;
    while (!heap.empty() && taken < budget) {
      std::pop_heap(heap.begin(), heap.end(), less);
      Cand c = heap.back();
      heap.pop_back();
      if (c.seen < (int)out.size()) {  // stale: measure it against the newer balls and put it back
        for (size_t i = (size_t)c.seen; i < out.size(); ++i) c.ext = std::min(c.ext, gap(c.p, c.r, out[i]));
        c.seen = (int)out.size();
        if (c.ext > enough) {
          heap.push_back(c);
          std::push_heap(heap.begin(), heap.end(), less);
        }
        continue;
      }
      if (c.ext <= enough) break;
      // the field says how deep a point is only roughly (blends make it deeper than it is): the
      // ball is trimmed to the nearest point of the skin along rays all round, and then waits
      // its turn again
      if (!c.trimmed) {
        float r = c.r;
        for (const vec3& d : dirs) {
          float t = 0, last = 0;
          while (t < r) {
            float e = m.eval(c.p + d * t);
            if (e >= 0.0f) {
              float a = last, b = t;
              for (int k = 0; k < 5; ++k) {
                float mid = 0.5f * (a + b);
                if (m.eval(c.p + d * mid) >= 0.0f) b = mid;
                else a = mid;
              }
              r = std::min(r, a);
              break;
            }
            last = t;
            t += std::max(0.001f, -e * 0.5f);
          }
        }
        r -= 0.0008f;
        if (r < smallest) continue;
        c.r = r;
        c.trimmed = true;
        c.ext = r;
        for (const Ball& b : out) c.ext = std::min(c.ext, gap(c.p, r, b));
        c.seen = (int)out.size();
        if (c.ext > enough) {
          heap.push_back(c);
          std::push_heap(heap.begin(), heap.end(), less);
        }
        continue;
      }
      Vertex v;
      v.pos = c.p;
      m.skin(v);
      int b1 = (int)std::floor(v.bones / 64.0f + 0.001f);
      int b0 = (int)(v.bones - 64.0f * (float)b1 + 0.5f);
      float w0 = v.mat - std::floor(v.mat + 0.0001f);
      out.push_back({c.p, c.r, w0 >= 0.499f ? b0 : b1});
      taken++;
    }
  };
  fill(slab, most, rmin, 0.003f);
  fill(slabRod, 200, rminThin, 0.002f);    // horns, the beak
  fill(slabSheet, 240, rminThin, 0.004f);  // the frill, the sail, the crest
  // the end of a tail or a beak tapers to a point, below the smallest of them: small balls are
  // laid along it from the tip back towards the body (as thick as it is there), until they meet
  // the balls already there
  for (const auto& t : tips) {
    vec3 d = glm::normalize(t.second - t.first);
    float len = glm::length(t.second - t.first);
    for (float s = 0.0f; s < len; s += 0.008f) {
      vec3 p = t.first + d * s;
      float r = -m.eval(p) - 0.0003f;
      if (r < 0.0012f) continue;  // (the very point)
      bool met = false;
      for (const Ball& b : out)
        if (glm::length(p - b.c) + r <= b.r + 0.002f) met = true;
      if (met) break;
      Vertex v;
      v.pos = p;
      m.skin(v);
      int b1 = (int)std::floor(v.bones / 64.0f + 0.001f);
      int b0 = (int)(v.bones - 64.0f * (float)b1 + 0.5f);
      float w0 = v.mat - std::floor(v.mat + 0.0001f);
      out.push_back({p, r, w0 >= 0.499f ? b0 : b1});
    }
  }
  // in bone order, and each bone's in runs of 16 close together (split again and again across
  // their longest side): the bounds of a run, or of a bone, tell whom it may touch
  std::stable_sort(out.begin(), out.end(), [](const Ball& a, const Ball& b) { return a.bone < b.bone; });
  for (size_t i = 0; i < out.size();) {
    size_t j = i;
    while (j < out.size() && out[j].bone == out[i].bone) ++j;
    orderRuns(out, (int)i, (int)j);
    i = j;
  }
  return out;
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
    // the tips: of the tail, and the pteranodon's beak
    std::vector<std::pair<vec3, vec3>> tips;
    if (theropod(kind)) tips.push_back({theroJ(kind).tip, theroJ(kind).tail3});
    if (quadruped(kind)) tips.push_back({quadJ(kind).tip, quadJ(kind).tail3});
    if (kind == D_KNIGHT) tips.push_back({toModel(kind, vec3(0, 0.466f, 0.5f)), headPoint(kind)});
    lib.bulk[kind] = fillBulk(m, toModel(kind, lo), toModel(kind, hi), 0.003f, kind <= D_KNIGHT ? 240 : 400, tips);
  };
  build(D_PAWN, raptorModel(), vec3(-0.11f, -0.005f, -0.52f), vec3(0.11f, 0.5f, 0.36f));
  tick("Hatching the raptors");
  build(D_KNIGHT, pteroModel(), vec3(-0.88f, -0.005f, -0.18f), vec3(0.88f, 0.66f, 0.52f));
  tick("Teaching the pteranodons to fly");
  build(D_BISHOP, trikeModel(), vec3(-0.25f, -0.005f, -0.86f), vec3(0.25f, 0.68f, 0.66f));
  tick("Sharpening the triceratops' horns");
  build(D_ROOK, brachioModel(), vec3(-0.2f, -0.005f, -0.9f), vec3(0.2f, 1.32f, 0.6f));
  tick("Raising the brachiosaurus");
  build(D_QUEEN, spinoModel(), vec3(-0.15f, -0.005f, -0.84f), vec3(0.15f, 0.84f, 0.76f));
  tick("Hoisting the spinosaurus' sail");
  build(D_KING, rexModel(), vec3(-0.19f, -0.005f, -0.82f), vec3(0.19f, 0.84f, 0.7f));
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
