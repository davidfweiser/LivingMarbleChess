// Sculpting the Roman army. Like the medieval army, every figure is a list of
// parts - capsules, cones, ellipsoids, boxes and fields of their own - each
// bound to a bone and a material, blended into one signed distance field and
// meshed; every vertex is then skinned to the bone(s) of the part(s) it lies on
// and painted with their material.
//
// The humans are sculpted at the legionary's size and scaled up afterwards
// (the centurion, the empress and the emperor are a little taller).
#include <algorithm>
#include <cmath>
#include <vector>

#include "roman.h"
#include "sculpt.h"

namespace roman {
using glm::mat3;
using glm::vec2;
using glm::vec3;
using namespace sdf;
using namespace sculpt;

namespace {

// ---------------------------------------------------------------------------
// Joints
// ---------------------------------------------------------------------------
struct HumanJ {
  vec3 root, waist, neck, hipL, kneeL, ankleL, shL, elL, wrL, shR, elR, wrR, gripR, gripL, sheath, cloak, hem;
  float S = 1;
};

float figScale(int kind) {
  switch (kind) {
    case R_BISHOP: return 1.04f;
    case R_QUEEN: return 1.05f;
    case R_KING: return 1.1f;
    default: return 1.0f;
  }
}

// The standing figure at the legionary's size: 0.86 tall without the crest.
HumanJ baseJ(int kind) {
  HumanJ j;
  j.root = vec3(0, 0.43f, 0);
  j.waist = vec3(0, 0.49f, 0);
  j.neck = vec3(0, 0.668f, 0.004f);
  j.hipL = vec3(0.05f, 0.41f, 0);
  j.kneeL = vec3(0.052f, 0.228f, 0.01f);
  j.ankleL = vec3(0.052f, 0.054f, -0.004f);
  j.shL = vec3(0.102f, 0.63f, -0.004f);
  j.elL = vec3(0.124f, 0.49f, -0.014f);
  j.wrL = vec3(0.12f, 0.472f, 0.1f);
  j.gripL = vec3(0.124f, 0.474f, 0.084f);
  j.shR = mx(j.shL);
  j.elR = vec3(-0.122f, 0.488f, -0.014f);
  j.wrR = vec3(-0.116f, 0.47f, 0.104f);
  j.gripR = vec3(-0.114f, 0.472f, 0.124f);
  j.sheath = vec3(-0.096f, 0.452f, 0.03f);  // the scabbard at the right hip
  j.cloak = vec3(0, 0.632f, -0.05f);
  j.hem = vec3(0, 0.42f, -0.09f);
  switch (kind) {
    case R_BISHOP:  // gladius in the right hand, pugio in the left, both held low and forward
      j.elR = vec3(-0.13f, 0.492f, -0.012f);
      j.wrR = vec3(-0.13f, 0.44f, 0.09f);
      j.gripR = vec3(-0.128f, 0.436f, 0.11f);
      j.elL = mx(j.elR);
      j.wrL = mx(j.wrR);
      j.gripL = mx(j.gripR);
      break;
    case R_QUEEN:  // narrower shoulders; her spear upright in the right hand, the left hand at her waist
      j.shL = vec3(0.09f, 0.626f, -0.004f);
      j.shR = mx(j.shL);
      j.elL = vec3(0.116f, 0.49f, -0.02f);
      j.wrL = vec3(0.07f, 0.47f, 0.078f);
      j.gripL = vec3(0.058f, 0.472f, 0.092f);
      j.elR = vec3(-0.114f, 0.488f, -0.014f);
      j.wrR = vec3(-0.11f, 0.47f, 0.1f);
      j.gripR = vec3(-0.108f, 0.472f, 0.12f);
      break;
    case R_KING:  // the eagle sceptre upright in the right hand, the left arm at his side; his sword on the left
      j.elL = vec3(0.134f, 0.49f, -0.02f);
      j.wrL = vec3(0.14f, 0.375f, 0.004f);
      j.gripL = vec3(0.142f, 0.358f, 0.01f);
      j.sheath = vec3(0.098f, 0.452f, 0.03f);
      break;
    default: break;
  }
  return j;
}

HumanJ standJ(int kind) {
  HumanJ j = baseJ(kind);
  float S = figScale(kind);
  for (vec3* v : {&j.root, &j.waist, &j.neck, &j.hipL, &j.kneeL, &j.ankleL, &j.shL, &j.elL, &j.wrL, &j.shR, &j.elR,
                  &j.wrR, &j.gripR, &j.gripL, &j.sheath, &j.cloak, &j.hem})
    *v *= S;
  j.S = S;
  return j;
}

Skeleton humanSkel(const HumanJ& j) {
  return makeSkeleton({
      {-1, j.root, false},           // ROOT
      {RH_ROOT, j.waist, false},     // CHEST
      {RH_CHEST, j.neck, false},     // HEAD
      {RH_ROOT, j.hipL, false},      // LTHIGH
      {RH_LTHIGH, j.kneeL, false},   // LSHIN
      {RH_ROOT, mx(j.hipL), false},  // RTHIGH
      {RH_RTHIGH, mx(j.kneeL), false},
      {RH_CHEST, j.shL, false},      // LARM
      {RH_LARM, j.elL, false},
      {RH_CHEST, j.shR, false},      // RARM
      {RH_RARM, j.elR, false},
      {RH_RFORE, j.gripR, true},     // ITEMR
      {RH_LFORE, j.gripL, true},     // ITEML
      {RH_RFORE, j.gripR, true},     // BLADE
      {RH_ROOT, j.sheath, true},     // SHEATH
      {RH_CHEST, j.cloak, false},    // CLOAK
      {RH_CLOAK, j.hem, false},      // HEM
  });
}

// The chariot: the horses are the medieval horse in "horse units", smaller, two abreast.
namespace cj {
const float CS = 0.56f;   // horse units -> chariot
const float HX = 0.098f;  // each horse's distance from the pole
const vec3 H0(0, 0, 0.15f);
inline vec3 HU(float x, float y, float z) { return vec3(x, y, z) * CS + H0; }
const float axleY = 0.14f, axleZ = -0.25f, wheelX = 0.178f, wheelR = 0.132f, floorY = 0.15f;
const vec3 car(0, axleY, axleZ), wheelL(wheelX, axleY, axleZ);
const vec3 horses = HU(0, 0.53f, 0), neck = HU(0, 0.62f, 0.24f), head = HU(0, 0.88f, 0.40f), tail = HU(0, 0.67f, -0.35f);
const vec3 flu = HU(0.075f, 0.48f, 0.22f), fll = HU(0.075f, 0.27f, 0.23f);
const vec3 hlu = HU(0.08f, 0.53f, -0.22f), hll = HU(0.08f, 0.27f, -0.30f);
// the charioteer: the legionary's figure, smaller, standing in the car
const float DS = 0.62f;
const vec3 D0(0, floorY, axleZ + 0.01f);
inline vec3 DU(const vec3& v) { return v * DS + D0; }
}  // namespace cj

// The siege tower: three storeys of timber, 0.44 wide.
namespace tj {
const float W = 0.215f, D = 0.205f;   // half width and half depth at the foot
const float topW = 0.185f, topD = 0.178f;
const float y0 = 0.05f, y1 = 0.43f, y2 = 0.79f, y3 = 1.14f;  // floors and the roof
const vec3 wheel1(0.232f, 0.075f, 0.14f);
const vec3 ram(0, 0.25f, 0.0f);
const vec3 bridge(0, y2 + 0.01f, 0.2f);
const vec3 door(0, 0.055f, 0.222f);
const vec3 scorpion(0, y3 + 0.11f, -0.02f);
const vec3 flag(-0.15f, y3 + 0.02f, -0.15f);
}  // namespace tj

namespace lj {
const vec3 body(0, 0.3f, 0), neck(0, 0.36f, 0.17f), head(0, 0.39f, 0.29f), jaw(0, 0.36f, 0.33f);
const vec3 tail1(0, 0.34f, -0.25f), tail2(0, 0.24f, -0.42f);
const vec3 flu(0.07f, 0.29f, 0.16f), fll(0.072f, 0.15f, 0.17f);
const vec3 hlu(0.075f, 0.31f, -0.17f), hll(0.075f, 0.16f, -0.24f);
}  // namespace lj

}  // namespace

// ===========================================================================
// Skeletons
// ===========================================================================
const Skeleton& skeleton(int asset) {
  static const Skeleton pawn = humanSkel(standJ(R_PAWN));
  static const Skeleton bishop = humanSkel(standJ(R_BISHOP));
  static const Skeleton queen = humanSkel(standJ(R_QUEEN));
  static const Skeleton king = humanSkel(standJ(R_KING));
  static const Skeleton chariot = [] {
    using namespace cj;
    HumanJ d = baseJ(R_PAWN);
    return makeSkeleton({
        {-1, car, false},                 // CAR
        {CH_CAR, wheelL, true},           // WHEELL
        {CH_CAR, mx(wheelL), true},       // WHEELR
        {CH_CAR, horses, false},          // HORSES
        {CH_HORSES, neck, false},
        {CH_NECK, head, false},
        {CH_HORSES, tail, false},
        {CH_HORSES, flu, false},
        {CH_FLU, fll, false},
        {CH_HORSES, mx(flu), false},
        {CH_FRU, mx(fll), false},
        {CH_HORSES, hlu, false},
        {CH_HLU, hll, false},
        {CH_HORSES, mx(hlu), false},
        {CH_HRU, mx(hll), false},
        {CH_CAR, DU(d.root), false},      // DRIVER
        {CH_DRIVER, DU(d.neck), false},   // DHEAD
        {CH_DRIVER, DU(d.shR), false},    // DRARM
        {CH_DRARM, DU(d.elR), false},
        {CH_DRIVER, DU(d.shL), false},    // DLARM
        {CH_DLARM, DU(d.elL), false},
        {CH_DRFORE, DU(d.gripR), true},   // ITEM
        {CH_DRIVER, DU(d.cloak), false},  // CLOAK
    });
  }();
  static const Skeleton tower = [] {
    using namespace tj;
    return makeSkeleton({
        {-1, vec3(0), false},                    // BODY
        {TW_BODY, wheel1, true},                 // WHEEL1 (front left)
        {TW_BODY, mx(wheel1), true},             // front right
        {TW_BODY, vec3(wheel1.x, wheel1.y, -wheel1.z), true},
        {TW_BODY, vec3(-wheel1.x, wheel1.y, -wheel1.z), true},
        {TW_BODY, ram, true},                    // RAM
        {TW_BODY, bridge, true},                 // BRIDGE
        {TW_BODY, door, true},                   // DOOR
        {TW_BODY, scorpion, true},               // SCORPION
        {TW_SCORPION, scorpion, true},           // LOADED
        {TW_BODY, flag, true},                   // FLAG
    });
  }();
  static const Skeleton lion = [] {
    using namespace lj;
    return makeSkeleton({
        {-1, body, false},
        {LN_BODY, neck, false},
        {LN_NECK, head, false},
        {LN_HEAD, jaw, false},
        {LN_BODY, tail1, false},
        {LN_TAIL1, tail2, false},
        {LN_BODY, flu, false},
        {LN_FLU, fll, false},
        {LN_BODY, mx(flu), false},
        {LN_FRU, mx(fll), false},
        {LN_BODY, hlu, false},
        {LN_HLU, hll, false},
        {LN_BODY, mx(hlu), false},
        {LN_HRU, mx(hll), false},
    });
  }();
  static const Skeleton none = makeSkeleton({});
  switch (asset) {
    case R_PAWN: return pawn;
    case R_KNIGHT: return chariot;
    case R_BISHOP: return bishop;
    case R_ROOK: return tower;
    case R_QUEEN: return queen;
    case R_KING: return king;
    case R_LION: return lion;
    default: return none;
  }
}

namespace {

// ---------------------------------------------------------------------------
// Shared parts
// ---------------------------------------------------------------------------
// A tube around the y axis whose radius follows the knots (y, r), with vertical
// folds of the given depth and number (a skirt, a gown).
Shape tubeS(std::vector<vec2> knots, float folds, float foldDepth, float zScale = 1.0f, vec3 at = vec3(0)) {
  float ylo = knots.front().x, yhi = knots.back().x, rmax = 0;
  for (const vec2& k : knots) rmax = std::max(rmax, k.y);
  Fn f = [knots, folds, foldDepth, zScale, at, ylo, yhi](const vec3& p0) {
    vec3 p = p0 - at;
    float y = glm::clamp(p.y, ylo, yhi);
    float r = knots.back().y;
    for (size_t i = 1; i < knots.size(); ++i)
      if (y <= knots[i].x) {
        float t = (y - knots[i - 1].x) / std::max(1e-5f, knots[i].x - knots[i - 1].x);
        r = glm::mix(knots[i - 1].y, knots[i].y, t);
        break;
      }
    float a = std::atan2(p.x, p.z);
    float depth = foldDepth * glm::clamp((yhi - p.y) / std::max(1e-4f, yhi - ylo) * 1.6f, 0.0f, 1.0f);
    float rr = std::sqrt(p.x * p.x + p.z * p.z / (zScale * zScale));
    float side = (rr - r - depth * std::sin(a * folds)) * 0.85f;
    float cap = std::max(ylo - p.y, p.y - yhi);
    return std::max(side, cap);
  };
  return fnS(f, at + vec3(0, (ylo + yhi) * 0.5f, 0), std::sqrt(rmax * rmax + 0.25f * (yhi - ylo) * (yhi - ylo)) + 0.02f);
}

// A head facing +z: skull, jaw, nose, brows, ears, eyes and mouth.
void addHead(Model& m, int bone, vec3 c, float s, bool female) {
  float jw = female ? 0.9f : 1.0f;
  m.add(bone, RM_SKIN, 0.0f, ellS(c, vec3(0.044f, 0.054f, 0.049f) * s));
  m.add(bone, RM_SKIN, 0.018f * s,
        ellS(c + vec3(0, -0.026f, 0.012f) * s, vec3(0.036f * jw, 0.032f, 0.038f) * s));  // jaw
  m.add(bone, RM_SKIN, 0.008f * s,
        coneS(c + vec3(0, 0.006f, 0.046f) * s, c + vec3(0, -0.012f, 0.058f) * s, 0.006f * s, 0.009f * s));  // nose
  if (!female)
    m.add(bone, RM_SKIN, 0.01f * s, ellS(c + vec3(0, 0.014f, 0.038f) * s, vec3(0.034f, 0.008f, 0.012f) * s));  // brow
  m.add2(bone, bone, RM_SKIN, 0.006f * s, ellS(c + vec3(0.045f, 0.0f, 0.0f) * s, vec3(0.007f, 0.014f, 0.01f) * s));
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 e = c + vec3(sd * 0.017f, 0.004f, 0.041f) * s;
    m.sub(RM_SKIN, 0.006f * s, sphereS(e + vec3(0, 0, 0.004f) * s, 0.008f * s));
    m.add(bone, RM_EYE, 0.002f * s, sphereS(e - vec3(0, 0, 0.002f) * s, 0.0065f * s));
  }
  m.paint(RM_DARK, ellS(c + vec3(0, -0.028f, 0.05f) * s, vec3(0.012f, 0.0025f, 0.02f) * s), RM_SKIN);  // mouth
}

// cropped hair over the top and back of the skull (fringe: down over the forehead)
void addHair(Model& m, int bone, vec3 c, float s, float fringe) {
  m.add(bone, RM_HAIR, 0.008f * s, fnS([c, s, fringe](const vec3& p) {
          float e = ellipsoid(p - (c + vec3(0, 0.007f, -0.004f) * s), vec3(0.048f, 0.059f, 0.053f) * s);
          float front = (p.z - c.z) - (0.016f + 0.03f * fringe) * s + 0.035f * std::max(0.0f, (p.y - c.y) / s - 0.012f) * s;
          float low = (c.y - 0.012f * s + 0.02f * s * std::max(0.0f, -(p.z - c.z) / s)) - p.y;  // down to the nape
          return std::max(std::max(e, front), low);
        }, c, 0.07f * s));
  for (int i = -3; i <= 3 && fringe > 0; ++i)  // comma-shaped locks over the brow
    m.add(bone, RM_HAIR, 0.004f * s,
          ellS(c + vec3(i * 0.011f, 0.033f, 0.04f) * s, vec3(0.008f, 0.006f, 0.008f) * s, rotZm(0.4f * (float)i / 3.0f)));
}

// A fist closed around a grip along 'axis'.
void addFist(Model& m, int bone, vec3 c, vec3 axis, float s = 1.0f) {
  m.add(bone, RM_SKIN, 0.008f * s, ellS(c, vec3(0.019f, 0.024f, 0.02f) * s, frameY(axis)));
}

// An arm from the shoulder: a short sleeve, the bare forearm.
void addArm(Model& m, bool left, const HumanJ& j, int sleeveMat, float sleeveLen, float rU = 0.033f, float rF = 0.025f) {
  vec3 sh = left ? j.shL : j.shR, el = left ? j.elL : j.elR, wr = left ? j.wrL : j.wrR;
  int ua = left ? RH_LARM : RH_RARM, fa = left ? RH_LFORE : RH_RFORE;
  vec3 d = glm::normalize(wr - el);
  m.add(ua, RM_SKIN, 0.02f, coneS(sh, el, rU, rU * 0.8f));
  m.add(fa, RM_SKIN, 0.012f, coneS(el, wr - d * 0.02f, rF, rF * 0.78f));
  if (sleeveLen > 0) m.add(ua, sleeveMat, 0.01f, coneS(sh, glm::mix(sh, el, sleeveLen), rU + 0.006f, rU * 0.92f + 0.004f));
}

// Bare legs with hobnailed caligae: leather soles and straps up to the ankle.
void addLegs(Model& m, const HumanJ& j, int bootMat, float bootTop) {
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int th = sd > 0 ? RH_LTHIGH : RH_RTHIGH, sh = sd > 0 ? RH_LSHIN : RH_RSHIN;
    vec3 hip(j.hipL.x * x, j.hipL.y, j.hipL.z), knee(j.kneeL.x * x, j.kneeL.y, j.kneeL.z),
        ankle(j.ankleL.x * x, j.ankleL.y, j.ankleL.z);
    m.add(th, RM_SKIN, 0.0f, coneS(hip, knee, 0.038f, 0.028f));
    m.add(sh, RM_SKIN, 0.012f, coneS(knee, ankle + vec3(0, 0.03f, 0), 0.028f, 0.019f));
    m.add(sh, RM_SKIN, 0.02f, ellS(knee + vec3(0, -0.06f, -0.012f), vec3(0.024f, 0.045f, 0.022f)));  // calf
    m.add(sh, bootMat, 0.008f, coneS(ankle + vec3(0, 0.02f, 0), ankle - vec3(0, 0.02f, 0), 0.022f, 0.023f));
    m.add(sh, bootMat, 0.012f, boxS(vec3(ankle.x, 0.02f, 0.024f), vec3(0.022f, 0.018f, 0.054f), 0.015f));
    m.add(sh, RM_LEATHER, 0.0f, boxS(vec3(ankle.x, 0.006f, 0.022f), vec3(0.025f, 0.006f, 0.06f), 0.005f));  // sole
    // the straps of the sandal-boot, criss-crossing up the shin
    float top = bootTop;
    m.paint(bootMat, fnS([ankle, top](const vec3& p) {
              float band = std::fabs(std::sin((p.y + 0.6f * (p.x - ankle.x)) * 230.0f)) - 0.45f;
              return std::max(band * 0.01f, p.y - top);
            }, ankle + vec3(0, 0.03f, 0), 0.08f), RM_SKIN);
  }
}

// A cloak hanging down the back from the shoulders to 'bottom', open in front:
// it follows the slope of the shoulders, falls in folds and flares out a little;
// a rolled collar along its top. Skinned from the cloak bone (top) to the hem bone.
void addCloak(Model& m, int mat, float bottom, float flare, float top = 0.632f) {
  Fn f = [bottom, flare, top](const vec3& p) {
    float ax = std::fabs(p.x);
    float sx = glm::clamp(ax / 0.15f, 0.0f, 1.0f);
    float topY = top - 0.045f * sx * sx;
    float botY = bottom + 0.04f * glm::clamp(ax / 0.17f, 0.0f, 1.0f) * glm::clamp(ax / 0.17f, 0.0f, 1.0f);
    float t = glm::clamp((top - p.y) / (top - bottom), 0.0f, 1.0f);
    float rx = 0.132f + (0.03f + flare) * std::sqrt(t), rz = 0.068f + (0.052f + flare * 0.6f) * t;
    float cz = -0.004f - 0.022f * t;
    float a = std::atan2(p.x, -(p.z - cz));
    float folds = (0.009f * std::sin(a * 7.0f + 1.3f) + 0.004f * std::sin(a * 13.0f + 0.4f)) * std::pow(t, 0.7f);
    float r = std::sqrt((p.x / rx) * (p.x / rx) + ((p.z - cz) / rz) * ((p.z - cz) / rz));
    float d = (r - 1.0f) * (rx + rz) * 0.5f - folds;
    float shell = std::fabs(d) - 0.0045f;
    float cut = std::max(p.y - topY, botY - p.y);
    float front = p.z - (cz - 0.012f);
    float body = std::max(std::max(shell, cut), front);
    // the rolled collar along the top
    float rr = std::sqrt((p.x / 0.13f) * (p.x / 0.13f) + ((p.z + 0.004f) / 0.067f) * ((p.z + 0.004f) / 0.067f));
    float ring = glm::length(vec2((rr - 1.0f) * 0.1f, p.y - topY)) - 0.0105f;
    ring = std::max(ring, p.z + 0.012f);
    return smin(body, ring, 0.008f);
  };
  vec3 a(0, top, -0.06f), b(0, bottom, -0.12f);
  m.addStretch(RH_CLOAK, RH_HEM, mat, 0.004f, fnS(f, vec3(0, (top + bottom) * 0.5f, -0.05f), 0.35f), along(a, b));
}

// The body under the armour: chest, shoulders, waist.
Fn torsoField(float bust = 0) {
  return [bust](const vec3& p) {
    float c = ellipsoid(p - vec3(0, 0.567f, 0.002f), vec3(0.085f, 0.09f, 0.057f));
    c = smin(c, ellipsoid(p - vec3(0, 0.612f, -0.004f), vec3(0.098f, 0.046f, 0.054f)), 0.03f);
    c = smin(c, ellipsoid(p - vec3(0, 0.5f, 0.0f), vec3(0.078f, 0.065f, 0.054f)), 0.03f);
    if (bust > 0)
      for (int sd = -1; sd <= 1; sd += 2)
        c = smin(c, ellipsoid(p - vec3(sd * 0.036f, 0.575f, 0.04f), vec3(0.034f, 0.032f, 0.03f) * bust), 0.02f);
    return c;
  };
}

// A gladius along 'dir' from the fist at 'g': pommel, grip, guard and a broad pointed blade.
void addGladius(Model& m, int bone, vec3 g, vec3 dir, float bladeLen = 0.2f, int hiltMat = RM_BRONZE) {
  mat3 F = frameY(dir, vec3(1, 0, 0));
  m.addR(bone, RM_WOOD, 0.002f, capsuleS(g - dir * 0.022f, g + dir * 0.02f, 0.0062f));
  m.addR(bone, hiltMat, 0.002f, sphereS(g - dir * 0.032f, 0.011f));
  m.addR(bone, hiltMat, 0.002f, ellS(g + dir * 0.027f, vec3(0.012f, 0.006f, 0.021f), F * rotYm(1.5708f)));
  vec3 b0 = g + dir * 0.032f;
  mat3 Ft = glm::transpose(F);
  m.addR(bone, RM_STEEL, 0.0f, fnS([b0, Ft, bladeLen](const vec3& p) {
           vec3 q = Ft * (p - b0);
           float t = glm::clamp(q.y / bladeLen, 0.0f, 1.0f);
           float w = 0.0105f * (t < 0.8f ? 1.0f - 0.08f * t : (1.0f - t) / 0.2f * 0.94f) + 0.0005f;
           float th = 0.0026f * (1.0f - 0.5f * t) + 0.0006f;
           float body = std::max(std::fabs(q.x) / w + std::fabs(q.z) / th - 1.0f, 0.0f) * std::min(w, th);
           float box = std::max(std::fabs(q.x) - w, std::fabs(q.z) - th);
           return std::max(std::max(box, body), std::max(-q.y, q.y - bladeLen));
         }, b0 + dir * bladeLen * 0.5f, bladeLen * 0.55f + 0.02f));
}

// A pugio (dagger) along 'dir' from the fist at 'g'.
void addPugio(Model& m, int bone, vec3 g, vec3 dir) {
  mat3 F = frameY(dir, vec3(1, 0, 0));
  mat3 Ft = glm::transpose(F);
  m.addR(bone, RM_WOOD, 0.002f, capsuleS(g - dir * 0.018f, g + dir * 0.016f, 0.0055f));
  m.addR(bone, RM_BRONZE, 0.002f, ellS(g - dir * 0.025f, vec3(0.011f, 0.007f, 0.007f), F));
  m.addR(bone, RM_BRONZE, 0.002f, ellS(g + dir * 0.021f, vec3(0.012f, 0.005f, 0.007f), F));
  vec3 b0 = g + dir * 0.024f;
  m.addR(bone, RM_STEEL, 0.0f, fnS([b0, Ft](const vec3& p) {
           vec3 q = Ft * (p - b0);
           const float L = 0.085f;
           float t = glm::clamp(q.y / L, 0.0f, 1.0f);
           float w = 0.013f * std::sin(std::pow(t, 0.55f) * 3.14159f) * (1.0f - 0.3f * t) + 0.004f * (1.0f - t) + 0.0004f;
           float th = 0.0022f * (1.0f - 0.6f * t) + 0.0005f;
           return std::max(std::max(std::fabs(q.x) - w, std::fabs(q.z) - th), std::max(-q.y, q.y - L));
         }, b0 + dir * 0.045f, 0.06f));
}

// A scabbard hanging from the belt at 'top', pointing 'dir' (down), and the hilt of
// the sword in it on its own bone (hidden once the blade is drawn).
void addScabbard(Model& m, vec3 top, vec3 dir, int hiltBone, int fitMat) {
  mat3 F = frameY(dir, vec3(1, 0, 0));
  mat3 Ft = glm::transpose(F);
  float L = 0.19f;
  m.add(RH_ROOT, RM_LEATHER, 0.003f, fnS([top, Ft, L](const vec3& p) {
          vec3 q = Ft * (p - top);
          float t = glm::clamp(q.y / L, 0.0f, 1.0f);
          float w = 0.0135f * (1.0f - 0.25f * t * t);
          return std::max(ellipsoid(vec3(q.x, 0, q.z), vec3(w, 1, 0.0065f)) * 0.006f, std::max(-q.y, q.y - L));
        }, top + dir * L * 0.5f, L * 0.6f));
  for (float u : {0.02f, 0.07f, 0.14f})  // bronze lockets
    m.add(RH_ROOT, fitMat, 0.002f, ellS(top + dir * u, vec3(0.016f, 0.004f, 0.009f), F));
  m.add(RH_ROOT, fitMat, 0.002f, ellS(top + dir * (L + 0.004f), vec3(0.01f, 0.012f, 0.0075f), F));  // the chape
  // the hilt sticking out of it
  vec3 up = -dir;
  m.addR(hiltBone, fitMat, 0.002f, ellS(top + up * 0.005f, vec3(0.012f, 0.006f, 0.021f), F * rotYm(1.5708f)));
  m.addR(hiltBone, RM_WOOD, 0.002f, capsuleS(top + up * 0.01f, top + up * 0.048f, 0.006f));
  m.addR(hiltBone, fitMat, 0.002f, sphereS(top + up * 0.058f, 0.011f));
}

// An imperial Gallic helmet: bowl, brow ridge, neck guard, cheek guards and a crest
// (fore-and-aft, or across for a centurion). 'c' is the head's centre, 's' its size.
void addHelmet(Model& m, int bone, vec3 c, bool transverse, int metal, int trim, float s = 1.0f) {
  vec3 hc = c + vec3(0, 0.012f, -0.004f) * s;
  m.add(bone, metal, 0.004f * s, fnS([hc, s](const vec3& p) {
          vec3 q = (p - hc) / s;
          float e = ellipsoid(q, vec3(0.056f, 0.057f, 0.06f));
          float shell = std::max(e, -(e + 0.006f));
          float low = -0.004f - 0.016f * glm::clamp(-q.z / 0.05f, 0.0f, 1.0f);
          return std::max(shell, low - q.y) * s;
        }, hc + vec3(0, 0.02f, 0) * s, 0.075f * s));
  // brow ridge and the reinforcing cross on the crown
  m.add(bone, trim, 0.003f * s, fnS([hc, s](const vec3& p) {
          vec3 q = (p - hc) / s;
          float r = std::sqrt(q.x * q.x / (0.058f * 0.058f) + q.z * q.z / (0.062f * 0.062f));
          float ring = glm::length(vec2((r - 1.0f) * 0.06f, q.y + 0.002f)) - 0.004f;
          return std::max(ring, -q.z + 0.012f) * s;
        }, hc + vec3(0, 0, 0.04f) * s, 0.07f * s));
  m.add(bone, trim, 0.003f * s, fnS([hc, s](const vec3& p) {
          vec3 q = (p - hc) / s;
          float e = ellipsoid(q, vec3(0.06f, 0.061f, 0.064f));
          return std::max(std::max(std::fabs(e) - 0.0016f, std::fabs(q.x) - 0.006f), 0.025f - q.y) * s;
        }, hc + vec3(0, 0.04f, 0) * s, 0.07f * s));
  // neck guard sloping down at the back
  m.add(bone, metal, 0.006f * s, fnS([hc, s](const vec3& p) {
          vec3 q = (p - hc) / s;
          float back = -q.z;
          float d = std::fabs(q.y + 0.018f + 0.4f * std::max(0.0f, back - 0.045f)) - 0.003f;
          float wide = std::fabs(q.x) - (0.05f + 0.1f * std::max(0.0f, back - 0.045f));
          return std::max(std::max(d, wide), std::max(back - 0.092f, 0.03f - back)) * s;
        }, hc + vec3(0, -0.03f, -0.06f) * s, 0.07f * s));
  // cheek guards, hinged at the temples
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 cc = hc + vec3(sd * 0.049f, -0.04f, 0.008f) * s;
    m.add(bone, metal, 0.003f * s, boxS(cc, vec3(0.003f, 0.025f, 0.019f) * s, 0.004f * s, rotZm(sd * 0.16f) * rotYm(-sd * 0.4f)));
    m.paint(trim, sphereS(cc + vec3(sd * 0.004f, 0.018f, 0.0f) * s, 0.0045f * s), metal);
  }
  // the crest on its holder
  m.add(bone, trim, 0.003f * s, cylS(hc + vec3(0, 0.054f, 0) * s, hc + vec3(0, 0.066f, 0) * s, 0.008f * s));
  vec3 cb = hc + vec3(0, 0.06f, 0) * s;
  m.add(bone, RM_CREST, 0.004f * s, fnS([cb, transverse, s](const vec3& p) {
          vec3 q = (p - cb) / s;
          if (transverse) q = vec3(q.z, q.y, q.x);  // fanned out from side to side
          float len = transverse ? 0.078f : 0.066f;
          float a = std::atan2(q.y, q.z);
          float r = std::sqrt(q.y * q.y + q.z * q.z) - len * (1.0f + 0.03f * std::sin(a * 23.0f));
          float w = 0.0095f - 0.004f * glm::clamp(q.y / len, 0.0f, 1.0f);
          return std::max(std::max(r, -q.y - 0.002f), std::fabs(q.x) - w) * s;
        }, cb + vec3(0, 0.035f, 0) * s, 0.09f * s));
}

// The pilum: a wooden shaft, a lead weight, a long iron shank and a small point.
// From 'butt' up along +y.
void addPilum(Model& m, int bone, vec3 butt, float scale = 1.0f) {
  float s = scale;
  vec3 top = butt + vec3(0, 0.66f * s, 0);
  m.addR(bone, RM_WOOD, 0.0f, cylS(butt + vec3(0, 0.004f, 0), top, 0.0072f * s));
  m.addR(bone, RM_IRON, 0.002f, cylS(butt, butt + vec3(0, 0.02f * s, 0), 0.0082f * s));
  m.addR(bone, RM_IRON, 0.004f * s, ellS(top + vec3(0, 0.008f * s, 0), vec3(0.011f, 0.022f, 0.011f) * s));
  m.addR(bone, RM_IRON, 0.0f, cylS(top, top + vec3(0, 0.26f * s, 0), 0.0034f * s));
  vec3 tip = top + vec3(0, 0.26f * s, 0);
  m.addR(bone, RM_STEEL, 0.0f, fnS([tip, s](const vec3& p) {
           vec3 q = (p - tip) / s;
           float t = glm::clamp(q.y / 0.034f, 0.0f, 1.0f);
           float w = 0.0072f * (1.0f - t) + 0.0004f;
           return std::max(std::fabs(q.x) + std::fabs(q.z) - w, std::max(-q.y, q.y - 0.034f)) * 0.7f * s;
         }, tip + vec3(0, 0.017f * s, 0), 0.03f * s));
}

// ---------------------------------------------------------------------------
// The pieces
// ---------------------------------------------------------------------------
// Legionary: Gallic helmet with a crest, banded armour, tunic and cloak,
// a curved scutum, a pilum, and his gladius at his right hip.
Model legionaryModel() {
  HumanJ j = baseJ(R_PAWN);
  Model m;
  m.skel = &skeleton(R_PAWN);
  addLegs(m, j, RM_LEATHER, 0.1f);
  // tunic skirt, to just above the knee
  m.add(RH_ROOT, RM_TUNIC, 0.0f, tubeS({{0.272f, 0.093f}, {0.3f, 0.09f}, {0.4f, 0.08f}, {0.47f, 0.074f}, {0.5f, 0.07f}}, 13, 0.004f));
  Fn torso = torsoField();
  m.add(RH_CHEST, RM_TUNIC, 0.03f, fnS(torso, vec3(0, 0.56f, 0), 0.15f));
  // banded armour: girth hoops, each overlapping the one below
  m.add(RH_CHEST, RM_STEEL, 0.004f, fnS([torso](const vec3& p) {
          const float h = 0.0265f, y0 = 0.462f;
          float band = (p.y - y0) / h;
          float lap = 0.0035f * (1.0f - (band - std::floor(band)));
          float d = torso(p) - 0.0065f - lap;
          return std::max(d, std::max(y0 - p.y, p.y - 0.64f));
        }, vec3(0, 0.55f, 0), 0.16f));
  // the upper chest plates: hinges and buckles
  for (int sd = -1; sd <= 1; sd += 2)
    for (int k = 0; k < 2; ++k) {
      vec3 c(sd * 0.036f, 0.595f - k * 0.04f, 0.0f);
      c.z = 0.002f + 0.057f * std::sqrt(std::max(0.0f, 1.0f - (c.x / 0.085f) * (c.x / 0.085f) - ((c.y - 0.567f) / 0.09f) * ((c.y - 0.567f) / 0.09f))) + 0.011f;
      m.add(RH_CHEST, RM_BRONZE, 0.002f, boxS(c, vec3(0.008f, 0.005f, 0.003f), 0.002f));
    }
  // shoulder guards: stepped lames down the upper arm
  for (int sd = -1; sd <= 1; sd += 2) {
    int ua = sd > 0 ? RH_LARM : RH_RARM;
    for (int k = 0; k < 3; ++k) {
      vec3 c(sd * (0.098f + 0.01f * k), 0.638f - 0.019f * k, -0.004f);
      m.add(ua, RM_STEEL, 0.003f, ellS(c, vec3(0.045f - 0.002f * k, 0.017f, 0.052f - 0.003f * k), rotZm(-sd * (0.25f + 0.25f * k))));
    }
  }
  m.add(RH_CHEST, RM_CLOAK, 0.006f, torusS(vec3(0, 0.652f, 0.004f), 0.046f, 0.012f));  // the scarf
  // belt with bronze plates and the studded apron
  m.add(RH_ROOT, RM_LEATHER, 0.003f, fnS([](const vec3& p) {
          float r = std::sqrt(p.x * p.x / (0.084f * 0.084f) + p.z * p.z / (0.064f * 0.064f));
          return glm::length(vec2((r - 1.0f) * 0.07f, p.y - 0.458f)) - 0.0085f;
        }, vec3(0, 0.458f, 0), 0.1f));
  for (int i = 0; i < 8; ++i) {
    float a = -1.2f + 2.4f * i / 7.0f;
    vec3 c(0.086f * std::sin(a), 0.458f, 0.066f * std::cos(a) + 0.002f);
    m.paint(RM_BRONZE, ellS(c, vec3(0.0085f, 0.007f, 0.012f)), RM_LEATHER);
  }
  for (int i = 0; i < 4; ++i) {
    float x = (i - 1.5f) * 0.017f;
    float z = 0.077f - 0.25f * x * x / 0.01f * 0.01f;
    m.add(RH_ROOT, RM_LEATHER, 0.002f, boxS(vec3(x, 0.395f, z), vec3(0.0055f, 0.056f, 0.0022f), 0.001f, rotXm(-0.1f)));
    for (int k = 0; k < 4; ++k)
      m.add(RH_ROOT, RM_BRONZE, 0.0f, sphereS(vec3(x, 0.44f - k * 0.025f, z + 0.002f - 0.0025f * k), 0.0042f));
    m.add(RH_ROOT, RM_BRONZE, 0.001f, ellS(vec3(x, 0.335f, z - 0.006f), vec3(0.0055f, 0.009f, 0.003f)));
  }
  // arms: tunic sleeves under the shoulder guards, fists
  addArm(m, true, j, RM_TUNIC, 0.45f);
  addArm(m, false, j, RM_TUNIC, 0.45f);
  addFist(m, RH_RFORE, j.gripR, vec3(0, 1, 0));
  addFist(m, RH_LFORE, j.gripL, vec3(0, 0.25f, 1));
  // head and helmet
  vec3 hc(0, 0.738f, 0.012f);
  addHead(m, RH_HEAD, hc, 1.0f, false);
  addHair(m, RH_HEAD, hc, 1.0f, 0.0f);
  addHelmet(m, RH_HEAD, hc, false, RM_STEEL, RM_BRONZE);
  // cloak
  addCloak(m, RM_CLOAK, 0.255f, 0.0f);
  m.add(RH_CHEST, RM_BRONZE, 0.002f, ellS(vec3(-0.084f, 0.628f, 0.03f), vec3(0.013f, 0.013f, 0.006f)));  // the brooch
  // pilum in the right hand, butt on the ground
  addPilum(m, RH_ITEMR, vec3(j.gripR.x, 0.004f, j.gripR.z));
  // the gladius: drawn into the right hand, or in its scabbard at the right hip
  addGladius(m, RH_BLADE, j.gripR, vec3(0, 1, 0));
  addScabbard(m, j.sheath, glm::normalize(vec3(-0.05f, -1.0f, 0.4f)), RH_SHEATH, RM_BRONZE);
  // the scutum on the left arm: a curved oblong of painted wood with a bronze rim and boss
  {
    mat3 R = rotYm(0.55f);
    mat3 Rt = glm::transpose(R);
    vec3 c = j.gripL + R * vec3(-0.005f, -0.03f, 0.028f);
    const float hw = 0.104f, hh = 0.196f, curve = 0.052f;
    auto rect = [hw, hh](vec2 q, float grow) {
      vec2 d = glm::abs(q) - vec2(hw + grow, hh + grow) + vec2(0.016f);
      return glm::length(glm::max(d, vec2(0.0f))) + std::min(std::max(d.x, d.y), 0.0f) - 0.016f;
    };
    auto bulge = [hw, curve](float x) { return curve * (x / hw) * (x / hw); };
    m.addR(RH_ITEML, RM_SHIELD, 0.0f, fnS([c, Rt, rect, bulge](const vec3& p) {
             vec3 q = Rt * (p - c);
             return std::max(rect(vec2(q.x, q.y), 0.0f), std::fabs(q.z + bulge(q.x)) - 0.0062f);
           }, c, 0.26f));
    m.addR(RH_ITEML, RM_BRONZE, 0.0f, fnS([c, Rt, rect, bulge](const vec3& p) {
             vec3 q = Rt * (p - c);
             float d2 = rect(vec2(q.x, q.y), 0.003f);
             return std::max(std::fabs(d2 + 0.0045f) - 0.0045f, std::fabs(q.z + bulge(q.x)) - 0.0085f);
           }, c, 0.26f));
    m.addR(RH_ITEML, RM_STEEL, 0.002f, boxS(c + R * vec3(0, 0, 0.007f), vec3(0.043f, 0.043f, 0.003f), 0.006f, R));
    m.addR(RH_ITEML, RM_BRONZE, 0.003f, fnS([c, Rt](const vec3& p) {
             vec3 q = Rt * (p - c);
             return std::max(ellipsoid(q - vec3(0, 0, 0.008f), vec3(0.03f, 0.03f, 0.024f)), 0.006f - q.z);
           }, c + R * vec3(0, 0, 0.02f), 0.04f));
    // the emblem: a laurel ring round the boss, thunderbolts above and below, wings to each side
    m.paint(RM_GOLD, fnS([c, Rt, bulge](const vec3& p) {
              vec3 q = Rt * (p - c);
              float face = std::fabs(q.z + bulge(q.x)) - 0.0075f;
              float ring = std::fabs(glm::length(vec2(q.x, q.y)) - 0.055f) - 0.0035f;
              float ay = std::fabs(q.y);
              float zig = q.x - 0.009f * (2.0f * std::fabs(std::fmod(ay * 45.0f, 2.0f) - 1.0f) - 1.0f);
              float bolt = std::max(std::fabs(zig) - 0.0034f, std::max(0.065f - ay, ay - 0.175f));
              float ax = std::fabs(q.x);
              float wing = 1e9f;
              for (int k = -1; k <= 1; ++k)
                wing = std::min(wing, std::fabs(q.y - k * 0.013f - 0.25f * (ax - 0.06f)) - 0.0028f);
              wing = std::max(wing, std::max(0.06f - ax, ax - 0.094f));
              return std::max(std::min(std::min(ring, bolt), wing), face);
            }, c, 0.26f), RM_SHIELD);
  }
  return m;
}

// Centurion: mail and medals, greaves, a silvered helmet with a transverse crest,
// a gladius in his right hand and a pugio in his left.
Model centurionModel() {
  HumanJ j = baseJ(R_BISHOP);
  Model m;
  m.skel = &skeleton(R_BISHOP);
  addLegs(m, j, RM_LEATHER, 0.1f);
  // greaves over the shins
  for (int sd = -1; sd <= 1; sd += 2) {
    int sh = sd > 0 ? RH_LSHIN : RH_RSHIN;
    vec3 knee(j.kneeL.x * sd, j.kneeL.y, j.kneeL.z), ankle(j.ankleL.x * sd, j.ankleL.y, j.ankleL.z);
    m.add(sh, RM_STEEL, 0.004f, fnS([knee, ankle](const vec3& p) {
            float c = roundCone(p, ankle + vec3(0, 0.035f, 0.004f), knee + vec3(0, 0.005f, 0.006f), 0.025f, 0.034f);
            return std::max(std::fabs(c) - 0.003f, (knee.z - 0.012f) - p.z);
          }, (knee + ankle) * 0.5f, 0.11f));
    m.add(sh, RM_BRONZE, 0.003f, ellS(knee + vec3(0, 0.004f, 0.026f), vec3(0.022f, 0.02f, 0.012f)));
  }
  m.add(RH_ROOT, RM_TUNIC, 0.0f, tubeS({{0.272f, 0.093f}, {0.3f, 0.09f}, {0.4f, 0.08f}, {0.47f, 0.074f}, {0.5f, 0.07f}}, 13, 0.004f));
  Fn torso = torsoField();
  m.add(RH_CHEST, RM_TUNIC, 0.03f, fnS(torso, vec3(0, 0.56f, 0), 0.15f));
  // mail shirt: the torso, and a skirt of mail to mid-thigh
  m.add(RH_CHEST, RM_MAIL, 0.006f, fnS([torso](const vec3& p) {
          return std::max(torso(p) - 0.006f, std::max(0.47f - p.y, p.y - 0.645f));
        }, vec3(0, 0.56f, 0), 0.16f));
  m.add(RH_ROOT, RM_MAIL, 0.004f, tubeS({{0.325f, 0.098f}, {0.36f, 0.094f}, {0.43f, 0.086f}, {0.49f, 0.08f}}, 9, 0.003f));
  for (int sd = -1; sd <= 1; sd += 2)  // mail over the shoulders
    m.add(sd > 0 ? RH_LARM : RH_RARM, RM_MAIL, 0.01f, ellS(vec3(sd * 0.1f, 0.632f, -0.004f), vec3(0.045f, 0.03f, 0.05f)));
  // the harness of phalerae: leather straps with nine medals
  auto chestZ = [](float x, float y) {
    return 0.002f + 0.057f * std::sqrt(std::max(0.0f, 1.0f - (x / 0.085f) * (x / 0.085f) - ((y - 0.567f) / 0.09f) * ((y - 0.567f) / 0.09f))) + 0.008f;
  };
  for (int sd = -1; sd <= 1; ++sd) {
    float x = sd * 0.036f;
    m.add(RH_CHEST, RM_LEATHER, 0.002f, boxS(vec3(x, 0.565f, chestZ(x, 0.565f) - 0.002f), vec3(0.006f, 0.058f, 0.003f), 0.001f));
    for (int k = 0; k < 3; ++k) {
      float y = 0.528f + k * 0.036f;
      m.add(RH_CHEST, RM_GOLD, 0.002f, ellS(vec3(x, y, chestZ(x, y) + 0.002f), vec3(0.0105f, 0.0105f, 0.0045f)));
    }
  }
  m.add(RH_CHEST, RM_LEATHER, 0.002f, boxS(vec3(0, 0.6f, chestZ(0, 0.6f) - 0.002f), vec3(0.05f, 0.005f, 0.003f), 0.001f));
  m.add(RH_CHEST, RM_GOLD, 0.004f, torusS(vec3(0, 0.655f, 0.006f), 0.043f, 0.0065f));  // a torque
  // belt
  m.add(RH_ROOT, RM_LEATHER, 0.003f, fnS([](const vec3& p) {
          float r = std::sqrt(p.x * p.x / (0.086f * 0.086f) + p.z * p.z / (0.066f * 0.066f));
          return glm::length(vec2((r - 1.0f) * 0.07f, p.y - 0.47f)) - 0.009f;
        }, vec3(0, 0.47f, 0), 0.1f));
  m.paint(RM_GOLD, boxS(vec3(0, 0.47f, 0.07f), vec3(0.014f, 0.008f, 0.01f), 0.002f), RM_LEATHER);
  addArm(m, true, j, RM_TUNIC, 0.4f);
  addArm(m, false, j, RM_TUNIC, 0.4f);
  vec3 dirR = glm::normalize(vec3(0.0f, 0.42f, 1.0f)), dirL = glm::normalize(vec3(0.0f, 0.2f, 1.0f));
  addFist(m, RH_RFORE, j.gripR, dirR);
  addFist(m, RH_LFORE, j.gripL, dirL);
  m.add(RH_LFORE, RM_GOLD, 0.003f, torusS(j.wrL - glm::normalize(j.wrL - j.elL) * 0.03f, 0.022f, 0.004f,
                                         frameY(glm::normalize(j.wrL - j.elL))));
  vec3 hc(0, 0.738f, 0.012f);
  addHead(m, RH_HEAD, hc, 1.0f, false);
  addHair(m, RH_HEAD, hc, 1.0f, 0.0f);
  m.add(RH_HEAD, RM_HAIR, 0.01f, ellS(hc + vec3(0, -0.034f, 0.024f), vec3(0.035f, 0.024f, 0.028f)));  // a short beard
  m.paint(RM_SKIN, ellS(hc + vec3(0, -0.03f, 0.052f), vec3(0.014f, 0.006f, 0.02f)), RM_HAIR);
  addHelmet(m, RH_HEAD, hc, true, RM_STEEL, RM_BRONZE);
  addCloak(m, RM_CLOAK, 0.24f, 0.01f);
  m.add(RH_CHEST, RM_GOLD, 0.002f, ellS(vec3(-0.084f, 0.628f, 0.03f), vec3(0.013f, 0.013f, 0.006f)));
  addGladius(m, RH_ITEMR, j.gripR, dirR, 0.2f, RM_GOLD);
  addPugio(m, RH_ITEML, j.gripL, dirL);
  return m;
}

// The empress: diadem and pearls, a long stola girdled under the bust, a palla
// over her back and shoulder, her spear, a golden goblet - and a dagger.
Model empressModel() {
  HumanJ j = baseJ(R_QUEEN);
  Model m;
  m.skel = &skeleton(R_QUEEN);
  // the stola falls to the floor; sandals peep out from under it
  for (int sd = -1; sd <= 1; sd += 2) {
    int sh = sd > 0 ? RH_LSHIN : RH_RSHIN, th = sd > 0 ? RH_LTHIGH : RH_RTHIGH;
    vec3 hip(j.hipL.x * sd, j.hipL.y, 0), knee(j.kneeL.x * sd, j.kneeL.y, j.kneeL.z), ankle(j.ankleL.x * sd, j.ankleL.y, j.ankleL.z);
    m.add(th, RM_SKIN, 0.0f, coneS(hip, knee, 0.034f, 0.025f));
    m.add(sh, RM_SKIN, 0.01f, coneS(knee, ankle + vec3(0, 0.02f, 0), 0.025f, 0.017f));
    m.add(sh, RM_SKIN, 0.01f, boxS(vec3(ankle.x, 0.016f, 0.026f), vec3(0.018f, 0.013f, 0.05f), 0.012f));
    m.add(sh, RM_GOLD, 0.0f, boxS(vec3(ankle.x, 0.005f, 0.024f), vec3(0.021f, 0.005f, 0.055f), 0.004f));
    m.paint(RM_GOLD, boxS(vec3(ankle.x, 0.02f, 0.05f), vec3(0.03f, 0.004f, 0.003f), 0.0f), RM_SKIN);
  }
  m.add(RH_ROOT, RM_LINEN, 0.0f, tubeS({{0.012f, 0.122f}, {0.06f, 0.112f}, {0.2f, 0.095f}, {0.38f, 0.082f}, {0.5f, 0.072f}}, 15, 0.006f, 0.92f));
  Fn torso = torsoField(1.0f);
  Fn slim = [torso](const vec3& p) { return torso(vec3(p.x * 1.1f, p.y, p.z)) / 1.1f; };
  m.add(RH_CHEST, RM_LINEN, 0.03f, fnS(slim, vec3(0, 0.56f, 0), 0.15f));
  m.add(RH_CHEST, RM_GOLD, 0.004f, fnS([](const vec3& p) {  // girdle under the bust
          float r = std::sqrt(p.x * p.x / (0.076f * 0.076f) + p.z * p.z / (0.06f * 0.06f));
          return glm::length(vec2((r - 1.0f) * 0.07f, p.y - 0.535f)) - 0.0055f;
        }, vec3(0, 0.535f, 0), 0.1f));
  // the palla: a mantle down her back, and its end thrown over the left shoulder and across her front
  addCloak(m, RM_CLOAK, 0.2f, 0.02f, 0.628f);
  m.add(RH_CHEST, RM_CLOAK, 0.008f, fnS([slim](const vec3& p) {
          vec3 n = glm::normalize(vec3(0.75f, 1.0f, 0.0f));
          float band = std::fabs(glm::dot(p - vec3(0, 0.55f, 0), n)) - 0.028f;
          float d = slim(p) - 0.009f;
          return std::max(std::max(std::fabs(d) - 0.004f, band), std::max(0.47f - p.y, p.y - 0.645f));
        }, vec3(0, 0.56f, 0), 0.15f));
  // arms: short linen sleeves, gold armlets
  addArm(m, true, j, RM_LINEN, 0.5f, 0.028f, 0.021f);
  addArm(m, false, j, RM_LINEN, 0.5f, 0.028f, 0.021f);
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 el = sd > 0 ? j.elL : j.elR, wr = sd > 0 ? j.wrL : j.wrR;
    vec3 d = glm::normalize(wr - el);
    m.add(sd > 0 ? RH_LFORE : RH_RFORE, RM_GOLD, 0.003f, torusS(wr - d * 0.028f, 0.019f, 0.0038f, frameY(d)));
  }
  addFist(m, RH_RFORE, j.gripR, vec3(0, 1, 0), 0.92f);
  addFist(m, RH_LFORE, j.gripL, vec3(0, 1, 0.1f), 0.92f);
  // head: her hair drawn back into a knot, curls over the brow, a diadem, earrings and pearls
  vec3 hc(0, 0.742f, 0.012f);
  m.add(RH_CHEST, RM_SKIN, 0.02f, coneS(vec3(0, 0.63f, 0.0f), vec3(0, 0.69f, 0.008f), 0.032f, 0.024f));  // neck
  addHead(m, RH_HEAD, hc, 0.96f, true);
  m.add(RH_HEAD, RM_HAIR, 0.008f, fnS([hc](const vec3& p) {
          float e = ellipsoid(p - (hc + vec3(0, 0.008f, -0.006f)), vec3(0.05f, 0.06f, 0.055f));
          float front = (p.z - hc.z) - 0.03f + 0.04f * std::max(0.0f, (p.y - hc.y) - 0.01f);
          return std::max(std::max(e, front), (hc.y - 0.03f) - p.y + 0.025f * std::max(0.0f, -(p.z - hc.z)) / 0.05f);
        }, hc, 0.07f));
  m.add(RH_HEAD, RM_HAIR, 0.012f, ellS(hc + vec3(0, 0.0f, -0.066f), vec3(0.034f, 0.03f, 0.026f)));  // the knot
  m.add(RH_HEAD, RM_GOLD, 0.002f, torusS(hc + vec3(0, 0.0f, -0.066f), 0.026f, 0.0035f, rotXm(1.5708f)));
  for (int i = -4; i <= 4; ++i) {
    float a = i * 0.24f;
    m.add(RH_HEAD, RM_HAIR, 0.003f, sphereS(hc + vec3(0.046f * std::sin(a), 0.026f, 0.046f * std::cos(a) - 0.008f), 0.0085f));
  }
  m.add(RH_HEAD, RM_GOLD, 0.002f, fnS([hc](const vec3& p) {  // the diadem, rising to a crescent in front
          vec3 q = p - (hc + vec3(0, 0.036f, -0.004f));
          float r = std::sqrt(q.x * q.x / (0.05f * 0.05f) + q.z * q.z / (0.055f * 0.055f));
          float h = 0.006f + 0.014f * std::max(0.0f, q.z / 0.055f) * std::max(0.0f, q.z / 0.055f);
          return std::max(std::fabs((r - 1.0f) * 0.05f) - 0.0022f, std::max(-q.y - 0.002f, q.y - h));
        }, hc + vec3(0, 0.04f, 0), 0.07f));
  m.add(RH_HEAD, RM_CLOAK, 0.0f, sphereS(hc + vec3(0, 0.05f, 0.05f), 0.0065f));  // a gem
  for (int sd = -1; sd <= 1; sd += 2)
    m.add(RH_HEAD, RM_GOLD, 0.002f, ellS(hc + vec3(sd * 0.047f, -0.02f, 0.0f), vec3(0.004f, 0.009f, 0.004f)));
  for (int i = 0; i < 11; ++i) {  // a string of pearls
    float a = -1.2f + 2.4f * i / 10.0f;
    m.add(RH_CHEST, RM_LINEN, 0.0f, sphereS(vec3(0.04f * std::sin(a), 0.648f - 0.012f * std::cos(a * 0.8f), 0.012f + 0.04f * std::cos(a)), 0.0052f));
  }
  // her spear (hasta), a golden goblet and a hidden dagger
  {
    vec3 butt(j.gripR.x, 0.004f, j.gripR.z), top = butt + vec3(0, 0.98f, 0);
    m.addR(RH_ITEMR, RM_WOOD, 0.0f, cylS(butt + vec3(0, 0.004f, 0), top, 0.0068f));
    m.addR(RH_ITEMR, RM_GOLD, 0.002f, cylS(butt, butt + vec3(0, 0.03f, 0), 0.0082f));
    m.addR(RH_ITEMR, RM_GOLD, 0.002f, cylS(top - vec3(0, 0.03f, 0), top + vec3(0, 0.005f, 0), 0.009f));
    m.addR(RH_ITEMR, RM_GOLD, 0.0f, fnS([top](const vec3& p) {
             vec3 q = p - top;
             float t = glm::clamp(q.y / 0.11f, 0.0f, 1.0f);
             float w = 0.021f * std::sin(std::pow(t, 0.6f) * 3.14159f) + 0.001f;
             float th = 0.004f * (1.0f - t) + 0.001f;
             return std::max(std::max(std::fabs(q.x) - w, std::fabs(q.z) - th), std::max(-q.y, q.y - 0.11f));
           }, top + vec3(0, 0.055f, 0), 0.07f));
    vec3 rb = top - vec3(0, 0.05f, 0);  // a ribbon tied under the head
    m.addR(RH_ITEMR, RM_CLOAK, 0.004f, torusS(rb, 0.011f, 0.004f));
    m.addR(RH_ITEMR, RM_CLOAK, 0.004f, boxS(rb + vec3(0.012f, -0.04f, 0.0f), vec3(0.006f, 0.04f, 0.0015f), 0.001f, rotZm(0.15f)));
    m.addR(RH_ITEMR, RM_CLOAK, 0.004f, boxS(rb + vec3(-0.008f, -0.05f, 0.004f), vec3(0.006f, 0.05f, 0.0015f), 0.001f, rotZm(-0.1f)));
  }
  {
    vec3 g = j.gripL;  // the goblet, held by its stem
    m.addR(RH_ITEML, RM_GOLD, 0.002f, cylS(g - vec3(0, 0.026f, 0), g - vec3(0, 0.022f, 0), 0.016f));
    m.addR(RH_ITEML, RM_GOLD, 0.0f, cylS(g - vec3(0, 0.024f, 0), g + vec3(0, 0.014f, 0), 0.0035f));
    m.addR(RH_ITEML, RM_GOLD, 0.004f, fnS([g](const vec3& p) {
             float e = ellipsoid(p - (g + vec3(0, 0.034f, 0)), vec3(0.022f, 0.026f, 0.022f));
             return std::max(std::max(e, -(e + 0.002f)), p.y - (g.y + 0.046f));
           }, g + vec3(0, 0.03f, 0), 0.04f));
    m.addR(RH_ITEML, RM_WINE, 0.0f, cylS(g + vec3(0, 0.03f, 0), g + vec3(0, 0.041f, 0), 0.018f));
  }
  addPugio(m, RH_BLADE, j.gripR, glm::normalize(vec3(0, 0.3f, 1.0f)));
  return m;
}

// The emperor: a gilded muscled cuirass, leather pteruges, a purple cloak, a laurel
// wreath, the eagle sceptre and his sword at his left hip.
Model emperorModel() {
  HumanJ j = baseJ(R_KING);
  Model m;
  m.skel = &skeleton(R_KING);
  addLegs(m, j, RM_CLOAK, 0.16f);  // red (or blue) boots laced up the shins
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 ankle(j.ankleL.x * sd, j.ankleL.y, j.ankleL.z);
    m.add(sd > 0 ? RH_LSHIN : RH_RSHIN, RM_CLOAK, 0.006f, coneS(ankle + vec3(0, 0.02f, 0), ankle + vec3(0, 0.1f, 0.002f), 0.024f, 0.023f));
    m.add(sd > 0 ? RH_LSHIN : RH_RSHIN, RM_GOLD, 0.002f, torusS(ankle + vec3(0, 0.105f, 0.002f), 0.024f, 0.004f));
  }
  m.add(RH_ROOT, RM_TUNIC, 0.0f, tubeS({{0.262f, 0.094f}, {0.3f, 0.09f}, {0.4f, 0.08f}, {0.47f, 0.074f}, {0.5f, 0.07f}}, 13, 0.004f));
  // pteruges: two rows of leather strips with gold tips
  for (int row = 0; row < 2; ++row) {
    int n = 14;
    for (int i = 0; i < n; ++i) {
      float a = (i + 0.5f * row) / n * 6.2831853f;
      float r = 0.088f + 0.004f * row;
      vec3 c(r * std::sin(a), 0.4f - 0.022f * row, r * 0.8f * std::cos(a));
      mat3 F = rotYm(a) * rotXm(-0.15f);
      m.add(RH_ROOT, RM_LEATHER, 0.001f, boxS(c, vec3(0.0095f, 0.042f, 0.0025f), 0.0015f, F));
      m.add(RH_ROOT, RM_GOLD, 0.0f, boxS(c - F * vec3(0, 0.04f, 0), vec3(0.0095f, 0.004f, 0.003f), 0.0012f, F));
    }
  }
  // the cuirass: pectorals, a ridged belly, a gorgon on the chest
  Fn torso = torsoField();
  m.add(RH_CHEST, RM_BRONZE, 0.004f, fnS([torso](const vec3& p) {
          float d = torso(p) - 0.009f;
          for (int sd = -1; sd <= 1; sd += 2)
            d = smin(d, ellipsoid(p - vec3(sd * 0.036f, 0.588f, 0.045f), vec3(0.036f, 0.027f, 0.022f)), 0.012f);
          for (int r = 0; r < 3; ++r)
            for (int sd = -1; sd <= 1; sd += 2)
              d = smin(d, ellipsoid(p - vec3(sd * 0.019f, 0.535f - r * 0.024f, 0.054f - 0.002f * r), vec3(0.017f, 0.0115f, 0.011f)), 0.006f);
          return std::max(d, std::max(0.432f - p.y, p.y - 0.646f));
        }, vec3(0, 0.54f, 0), 0.17f));
  m.add(RH_CHEST, RM_GOLD, 0.003f, fnS([](const vec3& p) {  // the gilded rim
          float r = std::sqrt(p.x * p.x / (0.09f * 0.09f) + p.z * p.z / (0.07f * 0.07f));
          return glm::length(vec2((r - 1.0f) * 0.075f, p.y - 0.436f)) - 0.0065f;
        }, vec3(0, 0.436f, 0), 0.1f));
  m.add(RH_CHEST, RM_GOLD, 0.002f, ellS(vec3(0, 0.607f, 0.066f), vec3(0.015f, 0.016f, 0.006f)));
  m.add(RH_CHEST, RM_CLOAK, 0.004f, fnS([](const vec3& p) {  // a sash knotted at the waist
          float r = std::sqrt(p.x * p.x / (0.087f * 0.087f) + p.z * p.z / (0.068f * 0.068f));
          return glm::length(vec2((r - 1.0f) * 0.075f, (p.y - 0.488f) * 0.6f)) - 0.006f;
        }, vec3(0, 0.488f, 0), 0.1f));
  for (int sd = -1; sd <= 1; sd += 2) {
    m.add(RH_CHEST, RM_CLOAK, 0.004f, ellS(vec3(sd * 0.018f, 0.47f, 0.07f), vec3(0.008f, 0.03f, 0.004f), rotZm(sd * 0.35f)));
    // shoulder straps and short pteruges over the upper arm
    int ua = sd > 0 ? RH_LARM : RH_RARM;
    m.add(RH_CHEST, RM_BRONZE, 0.006f, ellS(vec3(sd * 0.072f, 0.646f, 0.0f), vec3(0.03f, 0.011f, 0.05f)));
    for (int i = 0; i < 5; ++i) {
      float a = -0.9f + 1.8f * i / 4.0f;
      vec3 d(sd * std::cos(a) * 0.4f + sd * 0.6f, 0, std::sin(a));
      vec3 c = vec3(sd * 0.11f, 0.605f, -0.004f) + glm::normalize(d) * 0.032f;
      m.add(ua, RM_LEATHER, 0.001f, boxS(c, vec3(0.0085f, 0.026f, 0.0022f), 0.0012f, rotYm(std::atan2(d.x, d.z))));
    }
  }
  addArm(m, true, j, RM_TUNIC, 0.6f);
  addArm(m, false, j, RM_TUNIC, 0.6f);
  addFist(m, RH_RFORE, j.gripR, vec3(0, 1, 0));
  addFist(m, RH_LFORE, j.gripL, vec3(0, 0, 1));
  // head: short hair with a fringe, the laurel wreath
  vec3 hc(0, 0.738f, 0.012f);
  addHead(m, RH_HEAD, hc, 1.0f, false);
  addHair(m, RH_HEAD, hc, 1.0f, 1.0f);
  for (int i = 0; i < 26; ++i) {
    float a = (i / 26.0f) * 6.2831853f;
    if (std::fabs(std::sin(a * 0.5f)) < 0.08f) continue;  // a gap at the back where it is tied
    vec3 d(std::sin(a), 0, std::cos(a));
    vec3 c = hc + vec3(0.053f * d.x, 0.028f - 0.006f * std::cos(a), 0.058f * d.z - 0.004f);
    float side = (i % 2) ? 1.0f : -1.0f;
    mat3 F = rotYm(a) * rotXm(0.5f * side) * rotZm(0.5f);
    m.add(RH_HEAD, RM_GOLD, 0.001f, ellS(c + vec3(0, 0.006f * side, 0), vec3(0.004f, 0.0105f, 0.0018f), F));
  }
  m.add(RH_HEAD, RM_GOLD, 0.002f, fnS([hc](const vec3& p) {
          vec3 q = p - hc;
          float r = std::sqrt(q.x * q.x / (0.052f * 0.052f) + (q.z + 0.004f) * (q.z + 0.004f) / (0.057f * 0.057f));
          return glm::length(vec2((r - 1.0f) * 0.055f, q.y - 0.028f + 0.006f * (q.z / 0.057f))) - 0.0025f;
        }, hc + vec3(0, 0.028f, 0), 0.07f));
  for (int sd = -1; sd <= 1; sd += 2)  // the ribbons at the back
    m.add(RH_HEAD, RM_CLOAK, 0.002f, boxS(hc + vec3(sd * 0.009f, 0.0f, -0.06f), vec3(0.004f, 0.028f, 0.0015f), 0.001f, rotZm(sd * 0.2f)));
  // the paludamentum, pinned on the right shoulder with a gold brooch
  addCloak(m, RM_PURPLE, 0.16f, 0.025f);
  m.add(RH_CHEST, RM_PURPLE, 0.01f, ellS(vec3(0.085f, 0.632f, 0.0f), vec3(0.05f, 0.022f, 0.045f), rotZm(-0.35f)));
  m.add(RH_CHEST, RM_GOLD, 0.002f, ellS(vec3(-0.084f, 0.628f, 0.03f), vec3(0.015f, 0.015f, 0.007f)));
  // the eagle sceptre
  {
    vec3 butt(j.gripR.x, 0.004f, j.gripR.z), top = butt + vec3(0, 0.84f, 0);
    m.addR(RH_ITEMR, RM_GOLD, 0.0f, cylS(butt + vec3(0, 0.004f, 0), top, 0.0068f));
    for (float y : {0.1f, 0.3f, 0.6f, 0.8f}) m.addR(RH_ITEMR, RM_GOLD, 0.002f, sphereS(butt + vec3(0, y, 0), 0.0105f));
    vec3 g = top + vec3(0, 0.016f, 0);
    m.addR(RH_ITEMR, RM_GOLD, 0.002f, sphereS(g, 0.016f));  // the globe
    vec3 e = g + vec3(0, 0.03f, 0);
    m.addR(RH_ITEMR, RM_GOLD, 0.004f, ellS(e, vec3(0.012f, 0.018f, 0.014f), rotXm(-0.4f)));
    m.addR(RH_ITEMR, RM_GOLD, 0.004f, sphereS(e + vec3(0, 0.02f, 0.008f), 0.009f));
    m.addR(RH_ITEMR, RM_GOLD, 0.002f, coneS(e + vec3(0, 0.02f, 0.014f), e + vec3(0, 0.016f, 0.024f), 0.004f, 0.002f));
    for (int sd = -1; sd <= 1; sd += 2)  // wings raised
      m.addR(RH_ITEMR, RM_GOLD, 0.003f, triS(e + vec3(sd * 0.008f, 0.01f, 0.0f), e + vec3(sd * 0.045f, 0.05f, -0.008f),
                                           e + vec3(sd * 0.03f, -0.006f, -0.006f), 0.0025f));
  }
  addGladius(m, RH_BLADE, j.gripR, vec3(0, 1, 0), 0.21f, RM_GOLD);
  addScabbard(m, j.sheath, glm::normalize(vec3(0.05f, -1.0f, 0.4f)), RH_SHEATH, RM_GOLD);
  return m;
}

// --- the chariot ---------------------------------------------------------------
Fn horseBodyField(float x) {
  using namespace cj;
  return [x](const vec3& p) {
    vec3 q = p - vec3(x, 0, 0);
    float barrel = ellipsoid(q - HU(0, 0.53f, -0.01f), vec3(0.13f, 0.15f, 0.27f) * CS);
    float chest = ellipsoid(q - HU(0, 0.54f, 0.2f), vec3(0.12f, 0.155f, 0.13f) * CS);
    float haunch = ellipsoid(q - HU(0, 0.56f, -0.21f), vec3(0.135f, 0.16f, 0.15f) * CS);
    return smin(smin(barrel, chest, 0.08f * CS), haunch, 0.08f * CS);
  };
}

Model chariotModel() {
  using namespace cj;
  Model m;
  m.skel = &skeleton(R_KNIGHT);
  // --- two horses abreast
  for (int hs = -1; hs <= 1; hs += 2) {
    float x = hs * HX;
    vec3 X(x, 0, 0);
    Fn bodyF = horseBodyField(x);
    m.add(CH_HORSES, RM_HORSE, 0.0f, fnS(bodyF, HU(0, 0.55f, 0) + X, 0.42f * CS));
    m.add(CH_NECK, RM_HORSE, 0.06f * CS, coneS(HU(0, 0.6f, 0.24f) + X, HU(0, 0.86f, 0.39f) + X, 0.095f * CS, 0.058f * CS));
    m.add(CH_HEAD, RM_HORSE, 0.04f * CS, ellS(HU(0, 0.885f, 0.43f) + X, vec3(0.05f, 0.06f, 0.07f) * CS, rotXm(0.6f)));
    m.add(CH_HEAD, RM_HORSE, 0.05f * CS, ellS(HU(0, 0.82f, 0.5f) + X, vec3(0.046f, 0.052f, 0.08f) * CS, rotXm(0.8f)));
    m.add(CH_HEAD, RM_HORSE, 0.03f * CS, ellS(HU(0, 0.755f, 0.565f) + X, vec3(0.04f, 0.045f, 0.05f) * CS));
    for (int sd = -1; sd <= 1; sd += 2) {
      m.add(CH_HEAD, RM_HORSE, 0.01f * CS, coneS(HU(sd * 0.025f, 0.94f, 0.4f) + X, HU(sd * 0.032f, 1.0f, 0.38f) + X, 0.016f * CS, 0.004f * CS));
      m.add(CH_HEAD, RM_DARK, 0.002f, sphereS(HU(sd * 0.044f, 0.875f, 0.47f) + X, 0.011f * CS));
      m.paint(RM_DARK, sphereS(HU(sd * 0.022f, 0.75f, 0.61f) + X, 0.011f * CS), RM_HORSE);
    }
    // a hogged mane, a plume between the ears, the bridle
    m.add(CH_NECK, RM_MANE, 0.015f * CS, fnS([X](const vec3& p) {
            vec3 q = vec3((p.x - X.x) * 2.6f, p.y, p.z);
            return capsule(q, HU(0, 0.95f, 0.37f), HU(0, 0.72f, 0.16f), 0.022f * CS) / 2.6f;
          }, HU(0, 0.83f, 0.27f) + X, 0.2f * CS));
    m.add(CH_HEAD, RM_BRONZE, 0.003f, cylS(HU(0, 0.95f, 0.41f) + X, HU(0, 0.99f, 0.4f) + X, 0.012f * CS));
    m.add(CH_HEAD, RM_CREST, 0.01f * CS, coneS(HU(0, 1.0f, 0.4f) + X, HU(0, 1.17f, 0.32f) + X, 0.03f * CS, 0.016f * CS));
    m.add(CH_HEAD, RM_LEATHER, 0.002f, fnS([X](const vec3& p) {  // noseband and browband
            vec3 q = p - X;
            float a = std::fabs(ellipsoid(q - HU(0, 0.79f, 0.53f), vec3(0.05f, 0.058f, 0.06f) * CS)) - 0.002f;
            float b = std::fabs(ellipsoid(q - HU(0, 0.9f, 0.43f), vec3(0.054f, 0.06f, 0.06f) * CS)) - 0.002f;
            float band1 = std::fabs(glm::dot(q - HU(0, 0.79f, 0.53f), glm::normalize(vec3(0, 0.6f, -0.8f)))) - 0.006f;
            float band2 = std::fabs(glm::dot(q - HU(0, 0.9f, 0.45f), glm::normalize(vec3(0, 0.3f, 1.0f)))) - 0.006f;
            return std::min(std::max(a, band1), std::max(b, band2));
          }, HU(0, 0.85f, 0.48f) + X, 0.12f * CS));
    // tail
    m.add(CH_TAIL, RM_MANE, 0.03f * CS, coneS(HU(0, 0.66f, -0.35f) + X, HU(0, 0.5f, -0.42f) + X, 0.03f * CS, 0.036f * CS));
    m.add(CH_TAIL, RM_MANE, 0.03f * CS, coneS(HU(0, 0.5f, -0.42f) + X, HU(0, 0.32f, -0.43f) + X, 0.036f * CS, 0.018f * CS));
    // legs
    for (int sd = -1; sd <= 1; sd += 2) {
      bool L = sd > 0;
      float lx = x + sd * 0.07f * CS;
      int bFU = L ? CH_FLU : CH_FRU, bFL = L ? CH_FLL : CH_FRL, bHU = L ? CH_HLU : CH_HRU, bHL = L ? CH_HLL : CH_HRL;
      vec3 fu(lx, flu.y, flu.z), fl(lx, fll.y, fll.z), hu(lx, hlu.y, hlu.z), hl(lx, hll.y, hll.z);
      m.add(bFU, RM_HORSE, 0.03f * CS, coneS(fu, fl, 0.05f * CS, 0.033f * CS));
      m.add(bFL, RM_HORSE, 0.02f * CS, coneS(fl, vec3(lx, 0.075f * CS, fl.z + 0.008f * CS), 0.032f * CS, 0.023f * CS));
      m.add(bFL, RM_HORSE, 0.012f * CS, ellS(vec3(lx, 0.07f * CS, fl.z + 0.012f * CS), vec3(0.029f, 0.031f, 0.033f) * CS));
      m.add(bFL, RM_DARK, 0.006f * CS, coneS(vec3(lx, 0.045f * CS, fl.z + 0.022f * CS), vec3(lx, 0.006f * CS, fl.z + 0.03f * CS), 0.026f * CS, 0.035f * CS));
      m.add(bHU, RM_HORSE, 0.04f * CS, coneS(hu + vec3(0, -0.02f, 0.03f) * CS, hl, 0.07f * CS, 0.035f * CS));
      m.add(bHL, RM_HORSE, 0.02f * CS, coneS(hl, vec3(lx, 0.075f * CS, hl.z + 0.035f * CS), 0.033f * CS, 0.023f * CS));
      m.add(bHL, RM_HORSE, 0.012f * CS, ellS(vec3(lx, 0.07f * CS, hl.z + 0.04f * CS), vec3(0.029f, 0.031f, 0.033f) * CS));
      m.add(bHL, RM_DARK, 0.006f * CS, coneS(vec3(lx, 0.045f * CS, hl.z + 0.05f * CS), vec3(lx, 0.006f * CS, hl.z + 0.058f * CS), 0.026f * CS, 0.035f * CS));
    }
    // breastband and girth
    m.add(CH_HORSES, RM_LEATHER, 0.003f, fnS([bodyF](const vec3& p) {
            float d = std::fabs(bodyF(p) - 0.003f) - 0.0025f;
            float band = std::fabs(p.z - HU(0, 0, 0.2f).z + 0.4f * (p.y - HU(0, 0.5f, 0).y)) - 0.008f;
            return std::max(d, band);
          }, HU(0, 0.55f, 0.15f) + X, 0.3f * CS));
    m.paint(RM_BRONZE, fnS([X](const vec3& p) {
              vec3 q = p - X - HU(0, 0.5f, 0.27f);
              return glm::length(vec2(q.x, q.y)) - 0.008f;
            }, HU(0, 0.5f, 0.27f) + X, 0.05f), RM_LEATHER);
    // reins from the bit to the charioteer's left hand
    HumanJ d = baseJ(R_PAWN);
    vec3 hand = DU(d.gripL);
    for (int sd = -1; sd <= 1; sd += 2) {
      vec3 bit = HU(sd * 0.04f, 0.76f, 0.56f) + X;
      m.addStretch(CH_HEAD, CH_DLFORE, RM_LEATHER, 0.002f, capsuleS(bit, hand + vec3(sd * 0.004f, 0, 0), 0.0028f), along(bit, hand));
    }
  }
  // --- the yoke over both horses' withers, and the pole down to the car
  vec3 yoke = HU(0, 0.71f, 0.15f);
  m.add(CH_HORSES, RM_WOOD, 0.004f, fnS([yoke](const vec3& p) {
          vec3 q = p - yoke;
          float bend = 0.18f * q.x * q.x;
          return capsule(vec3(q.x, q.y - bend, q.z), vec3(-0.17f, 0, 0), vec3(0.17f, 0, 0), 0.011f);
        }, yoke, 0.2f));
  for (int sd = -1; sd <= 1; sd += 2) m.add(CH_HORSES, RM_BRONZE, 0.003f, sphereS(yoke + vec3(sd * 0.175f, 0.008f, 0), 0.012f));
  {
    vec3 a(0, 0.19f, -0.13f), b = yoke + vec3(0, -0.012f, 0.0f);
    vec3 mid = (a + b) * 0.5f + vec3(0, -0.03f, 0);
    m.addStretch(CH_CAR, CH_HORSES, RM_WOOD, 0.004f, fnS([a, b, mid](const vec3& p) {
                   return std::min(capsule(p, a, mid, 0.0095f), capsule(p, mid, b, 0.009f));
                 }, mid, glm::length(b - a) * 0.6f), along(a, b));
  }
  // --- the car: floor, a curved breastwork painted in the army's colour, gilded rail
  m.add(CH_CAR, RM_WOOD, 0.004f, boxS(vec3(0, floorY - 0.008f, axleZ - 0.015f), vec3(0.128f, 0.009f, 0.105f), 0.006f));
  auto wallTop = [](float z) { return 0.29f + 0.09f * glm::clamp((z - (axleZ - 0.02f)) / 0.12f, 0.0f, 1.0f); };
  auto ell = [](const vec3& p) {
    float rx = 0.13f, rz = 0.115f;
    float r = std::sqrt(p.x * p.x / (rx * rx) + (p.z - (axleZ - 0.02f)) * (p.z - (axleZ - 0.02f)) / (rz * rz));
    return (r - 1.0f) * 0.12f;
  };
  m.add(CH_CAR, RM_SHIELD, 0.003f, fnS([ell, wallTop](const vec3& p) {
          float wall = std::fabs(ell(p)) - 0.006f;
          return std::max(std::max(wall, (axleZ - 0.03f) - p.z), std::max(floorY - 0.01f - p.y, p.y - wallTop(p.z)));
        }, vec3(0, 0.27f, axleZ + 0.04f), 0.2f));
  m.add(CH_CAR, RM_GOLD, 0.003f, fnS([ell, wallTop](const vec3& p) {
          float d = glm::length(vec2(ell(p), p.y - wallTop(p.z))) - 0.008f;
          return std::max(d, (axleZ - 0.03f) - p.z);
        }, vec3(0, 0.33f, axleZ + 0.04f), 0.2f));
  m.paint(RM_GOLD, fnS([](const vec3& p) {  // an eagle-and-laurel roundel on the front, a gilded band
            vec3 q = p - vec3(0, 0.29f, axleZ + 0.1f);
            float ring = std::fabs(glm::length(vec2(q.x, q.y)) - 0.032f) - 0.004f;
            float disc = glm::length(vec2(q.x, q.y)) - 0.016f;
            float band = std::fabs(p.y - (floorY + 0.022f)) - 0.006f;
            return std::min(std::min(ring, disc), band);
          }, vec3(0, 0.27f, axleZ + 0.1f), 0.2f), RM_SHIELD);
  // the axle, wheels with eight spokes and iron tyres, and scythe blades on the axle ends
  m.add(CH_CAR, RM_IRON, 0.002f, capsuleS(vec3(-wheelX - 0.03f, axleY, axleZ), vec3(wheelX + 0.03f, axleY, axleZ), 0.008f));
  for (int sd = -1; sd <= 1; sd += 2) {
    int wb = sd > 0 ? CH_WHEELL : CH_WHEELR;
    vec3 c(sd * wheelX, axleY, axleZ);
    mat3 F = rotZm(1.5708f);  // ring in the yz plane
    m.addR(wb, RM_WOOD, 0.002f, torusS(c, wheelR - 0.009f, 0.0085f, F));
    m.addR(wb, RM_IRON, 0.001f, torusS(c, wheelR - 0.001f, 0.005f, F));
    m.addR(wb, RM_WOOD, 0.003f, capsuleS(c - vec3(0.022f, 0, 0), c + vec3(0.022f, 0, 0), 0.02f));
    m.addR(wb, RM_BRONZE, 0.002f, capsuleS(c + vec3(sd * 0.02f, 0, 0), c + vec3(sd * 0.03f, 0, 0), 0.013f));
    for (int k = 0; k < 8; ++k) {
      float a = k * 0.785398f;
      vec3 d(0, std::cos(a), std::sin(a));
      m.addR(wb, RM_WOOD, 0.003f, coneS(c + d * 0.016f, c + d * (wheelR - 0.012f), 0.0062f, 0.0045f));
    }
    vec3 s0 = c + vec3(sd * 0.036f, 0, 0);
    m.add(CH_CAR, RM_STEEL, 0.0f, fnS([s0, sd](const vec3& p) {
            vec3 q = p - s0;
            float u = sd * q.x;  // outwards
            float curve = q.z + 0.6f * std::max(0.0f, u) * std::max(0.0f, u) / 0.09f;
            float w = 0.011f * (1.0f - glm::clamp(u / 0.11f, 0.0f, 1.0f)) + 0.001f;
            return std::max(std::max(std::fabs(curve) - w, std::fabs(q.y) - 0.0018f), std::max(-u, u - 0.11f));
          }, s0 + vec3(sd * 0.055f, 0, -0.01f), 0.07f));
  }
  // --- the charioteer: helmet and crest, a tunic bound with leather straps, a short cloak
  {
    HumanJ d = baseJ(R_PAWN);
    auto D = [](const vec3& v) { return DU(v); };
    const float s = DS;
    for (int sd = -1; sd <= 1; sd += 2) {  // legs (inside the car)
      vec3 hip = D(vec3(d.hipL.x * sd, d.hipL.y, 0)), knee = D(vec3(d.kneeL.x * sd, d.kneeL.y, 0.02f)),
           ankle = D(vec3(d.ankleL.x * sd, d.ankleL.y, 0.0f));
      m.add(CH_DRIVER, RM_SKIN, 0.006f, coneS(hip, knee, 0.036f * s, 0.027f * s));
      m.add(CH_DRIVER, RM_SKIN, 0.006f, coneS(knee, ankle, 0.027f * s, 0.019f * s));
      m.add(CH_DRIVER, RM_LEATHER, 0.006f, boxS(vec3(ankle.x, floorY + 0.012f, ankle.z + 0.012f), vec3(0.02f, 0.013f, 0.04f) * s, 0.008f));
    }
    m.add(CH_DRIVER, RM_TUNIC, 0.0f, tubeS({{0.3f * s + floorY, 0.09f * s}, {0.4f * s + floorY, 0.08f * s}, {0.5f * s + floorY, 0.07f * s}}, 11, 0.003f, 1.0f,
                                           vec3(D0.x, 0, D0.z)));
    Fn torso = torsoField();
    m.add(CH_DRIVER, RM_TUNIC, 0.02f, fnS([torso, s](const vec3& p) { return torso((p - D0) / s) * s; }, D(vec3(0, 0.56f, 0)), 0.1f));
    m.add(CH_DRIVER, RM_LEATHER, 0.003f, fnS([torso, s](const vec3& p) {  // the fasciae: straps wound round the chest
            vec3 q = (p - D0) / s;
            float band = std::fabs(std::sin(q.y * 150.0f)) - 0.35f;
            return std::max(std::max(torso(q) - 0.004f, band * 0.006f), std::max(0.47f - q.y, q.y - 0.62f)) * s;
          }, D(vec3(0, 0.55f, 0)), 0.1f));
    for (int sd = -1; sd <= 1; sd += 2) {
      bool L = sd > 0;
      vec3 sh = D(L ? d.shL : d.shR), el = D(L ? d.elL : d.elR), gr = D(L ? d.gripL : d.gripR);
      int ua = L ? CH_DLARM : CH_DRARM, fa = L ? CH_DLFORE : CH_DRFORE;
      m.add(ua, RM_SKIN, 0.012f, coneS(sh, el, 0.033f * s, 0.027f * s));
      m.add(ua, RM_TUNIC, 0.008f, coneS(sh, glm::mix(sh, el, 0.45f), 0.039f * s, 0.033f * s));
      m.add(fa, RM_SKIN, 0.008f, coneS(el, gr - glm::normalize(gr - el) * 0.012f, 0.025f * s, 0.019f * s));
      m.add(fa, RM_SKIN, 0.005f, ellS(gr, vec3(0.019f, 0.024f, 0.02f) * s, frameY(L ? vec3(0, 0.2f, 1) : vec3(0, 1, 0))));
    }
    vec3 hc = D(vec3(0, 0.738f, 0.012f));
    addHead(m, CH_DHEAD, hc, s, false);
    addHelmet(m, CH_DHEAD, hc, false, RM_BRONZE, RM_BRONZE, s);
    // a short cloak flying from his shoulders
    vec3 ct = D(vec3(0, 0.632f, -0.03f));
    m.add(CH_CLOAK, RM_CLOAK, 0.004f, fnS([ct, s](const vec3& p) {
            vec3 q = (p - ct) / s;
            float t = glm::clamp(-q.y / 0.26f, 0.0f, 1.0f);
            float rx = 0.13f + 0.04f * t, rz = 0.06f + 0.04f * t;
            float r = std::sqrt(q.x * q.x / (rx * rx) + (q.z + 0.01f) * (q.z + 0.01f) / (rz * rz));
            float shell = std::fabs((r - 1.0f) * (rx + rz) * 0.5f - 0.003f * t * std::sin(std::atan2(q.x, -q.z) * 5.0f)) - 0.0075f;
            return std::max(std::max(shell, std::max(q.y, -0.26f - q.y)), q.z + 0.02f) * s;
          }, ct + vec3(0, -0.08f, -0.03f), 0.16f));
    // his spear, held upright
    vec3 g = D(d.gripR);
    m.addR(CH_ITEM, RM_WOOD, 0.0f, cylS(g - vec3(0, 0.12f, 0), g + vec3(0, 0.36f, 0), 0.0058f));
    m.addR(CH_ITEM, RM_STEEL, 0.0f, fnS([g](const vec3& p) {
             vec3 q = p - (g + vec3(0, 0.36f, 0));
             float t = glm::clamp(q.y / 0.07f, 0.0f, 1.0f);
             float w = 0.013f * std::sin(std::pow(t, 0.7f) * 3.14159f) + 0.001f;
             return std::max(std::max(std::fabs(q.x) - w, std::fabs(q.z) - 0.0025f), std::max(-q.y, q.y - 0.07f));
           }, g + vec3(0, 0.4f, 0), 0.05f));
  }
  return m;
}

// --- the siege tower -----------------------------------------------------------
Model towerModel() {
  using namespace tj;
  Model m;
  m.skel = &skeleton(R_ROOK);
  auto halfW = [](float y) { return glm::mix(W, topW, glm::clamp((y - y0) / (y3 - y0), 0.0f, 1.0f)); };
  auto halfD = [](float y) { return glm::mix(D, topD, glm::clamp((y - y0) / (y3 - y0), 0.0f, 1.0f)); };
  // the walls: rawhide on the front and sides, planks behind; storeys marked by beams
  Fn shell = [halfW, halfD](const vec3& p) {
    float y = glm::clamp(p.y, y0, y3);
    vec2 d = vec2(std::fabs(p.x) - halfW(y), std::fabs(p.z) - halfD(y));
    float side = glm::length(glm::max(d, vec2(0.0f))) + std::min(std::max(d.x, d.y), 0.0f);
    float box = std::max(side, std::max(y0 - p.y, p.y - y3));
    return std::max(box, -(box + 0.02f));  // hollow
  };
  m.add(TW_BODY, RM_TIMBER, 0.0f, fnS(shell, vec3(0, (y0 + y3) * 0.5f, 0), 0.75f));
  // rawhide nailed over the top storey's sides against fire arrows
  m.paint(RM_HIDE, fnS([](const vec3& p) { return std::max(0.17f - std::fabs(p.x), std::max(y2 + 0.03f - p.y, p.y - y3 + 0.03f)); },
                       vec3(0, (y2 + y3) * 0.5f, 0), 0.4f), RM_TIMBER);
  // the door opening at the foot and a window for the bridge at the top
  m.sub(RM_DARK, 0.004f, boxS(vec3(0, 0.22f, 0.21f), vec3(0.1f, 0.17f, 0.04f), 0.004f));
  m.sub(RM_DARK, 0.004f, boxS(vec3(0, (y2 + y3) * 0.5f + 0.01f, 0.19f), vec3(0.095f, 0.16f, 0.04f), 0.004f));
  // arrow slits in the middle storey
  for (int sd = -1; sd <= 1; sd += 2) {
    m.sub(RM_DARK, 0.002f, boxS(vec3(sd * 0.09f, 0.61f, 0.2f), vec3(0.012f, 0.05f, 0.04f), 0.003f));
    m.sub(RM_DARK, 0.002f, boxS(vec3(sd * 0.21f, 0.61f, 0.0f), vec3(0.04f, 0.05f, 0.012f), 0.003f));
  }
  m.sub(RM_DARK, 0.002f, boxS(vec3(0, 0.61f, 0.2f), vec3(0.012f, 0.05f, 0.04f), 0.003f));
  // the frame: corner posts, storey beams, braces
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sz = -1; sz <= 1; sz += 2) {
      vec3 a(sx * (W + 0.006f), y0 - 0.01f, sz * (D + 0.006f)), b(sx * (topW + 0.006f), y3 + 0.03f, sz * (topD + 0.006f));
      m.add(TW_BODY, RM_TIMBER, 0.002f, fnS([a, b](const vec3& p) {
              vec3 ab = b - a;
              float t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
              vec3 c = a + ab * t;
              vec3 q = p - c;
              return std::max(std::max(std::fabs(q.x) - 0.017f, std::fabs(q.z) - 0.017f), std::max(a.y - p.y, p.y - b.y));
            }, (a + b) * 0.5f, 0.62f));
    }
  for (float y : {y0, y1, y2, y3}) {
    float w = halfW(y) + 0.012f, d = halfD(y) + 0.012f;
    m.add(TW_BODY, RM_TIMBER, 0.002f, fnS([w, d, y](const vec3& p) {
            vec2 e = vec2(std::fabs(p.x) - w, std::fabs(p.z) - d);
            float rect = glm::length(glm::max(e, vec2(0.0f))) + std::min(std::max(e.x, e.y), 0.0f);
            return std::max(std::fabs(rect + 0.006f) - 0.009f, std::fabs(p.y - y) - 0.014f);
          }, vec3(0, y, 0), 0.34f));
  }
  for (int sd = -1; sd <= 1; sd += 2)  // diagonal braces on the sides
    for (int k = 0; k < 2; ++k) {
      float ya = k == 0 ? y1 : y2, yb = k == 0 ? y2 : y3;
      vec3 a(sd * (halfW(ya) + 0.012f), ya + 0.02f, -halfD(ya) + 0.02f), b(sd * (halfW(yb) + 0.012f), yb - 0.02f, halfD(yb) - 0.02f);
      if (k == 1) std::swap(a.z, b.z);
      m.add(TW_BODY, RM_TIMBER, 0.002f, capsuleS(a, b, 0.0095f));
    }
  // the roof platform with a parapet of planks, and two soldiers keeping watch
  m.add(TW_BODY, RM_TIMBER, 0.003f, boxS(vec3(0, y3 + 0.005f, 0), vec3(topW + 0.03f, 0.012f, topD + 0.03f), 0.004f));
  m.add(TW_BODY, RM_TIMBER, 0.002f, fnS([](const vec3& p) {
          float w = topW + 0.022f, d = topD + 0.022f;
          vec2 e = vec2(std::fabs(p.x) - w, std::fabs(p.z) - d);
          float rect = glm::length(glm::max(e, vec2(0.0f))) + std::min(std::max(e.x, e.y), 0.0f);
          float top = y3 + 0.07f + 0.012f * (std::fabs(std::fmod(std::fabs(p.x + p.z) * 30.0f, 2.0f) - 1.0f) > 0.5f ? 1.0f : 0.0f);
          return std::max(std::fabs(rect + 0.006f) - 0.007f, std::max(y3 - p.y, p.y - top));
        }, vec3(0, y3 + 0.04f, 0), 0.33f));
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 h(sd * 0.11f, y3 + 0.085f, 0.1f);
    m.add(TW_BODY, RM_SKIN, 0.004f, sphereS(h, 0.028f));
    m.add(TW_BODY, RM_STEEL, 0.004f, fnS([h](const vec3& p) {
            float e = ellipsoid(p - h - vec3(0, 0.008f, -0.003f), vec3(0.034f, 0.032f, 0.036f));
            return std::max(e, h.y - 0.004f - p.y);
          }, h, 0.05f));
    m.add(TW_BODY, RM_CREST, 0.003f, fnS([h](const vec3& p) {
            vec3 q = p - (h + vec3(0, 0.035f, -0.003f));
            return std::max(std::max(glm::length(vec2(q.y, q.z)) - 0.034f, -q.y), std::fabs(q.x) - 0.006f);
          }, h + vec3(0, 0.05f, 0), 0.05f));
    m.add(TW_BODY, RM_TUNIC, 0.01f, ellS(h - vec3(0, 0.045f, 0.0f), vec3(0.045f, 0.03f, 0.032f)));
  }
  // the battering ram, hung on ropes in the bottom storey: a log with an iron ram's head
  m.addR(TW_RAM, RM_WOOD, 0.004f, capsuleS(vec3(0, ram.y, -0.17f), vec3(0, ram.y, 0.15f), 0.032f));
  m.addR(TW_RAM, RM_IRON, 0.004f, fnS([](const vec3& p) {
           vec3 q = p - vec3(0, ram.y, 0.17f);
           float head = ellipsoid(q, vec3(0.04f, 0.042f, 0.045f));
           for (int sd = -1; sd <= 1; sd += 2)  // curled horns
             head = smin(head, torus(vec3(q.y, q.x - sd * 0.032f, q.z + 0.005f), 0.022f, 0.009f), 0.008f);
           return head;
         }, vec3(0, ram.y, 0.17f), 0.07f));
  for (float z : {-0.1f, 0.06f}) {
    m.addR(TW_RAM, RM_IRON, 0.001f, torusS(vec3(0, ram.y, z), 0.034f, 0.005f, rotXm(1.5708f)));
    vec3 top(0, y1 - 0.01f, z), low(0, ram.y + 0.03f, z);
    m.addStretch(TW_BODY, TW_RAM, RM_ROPE, 0.002f, capsuleS(top, low, 0.0045f), along(top, low));
  }
  // the door: a ramp of planks hinged at its foot
  m.addR(TW_DOOR, RM_TIMBER, 0.002f, boxS(vec3(0, 0.22f, door.z + 0.004f), vec3(0.106f, 0.168f, 0.009f), 0.003f));
  for (float y : {0.12f, 0.31f}) m.addR(TW_DOOR, RM_IRON, 0.001f, boxS(vec3(0, y, door.z + 0.014f), vec3(0.1f, 0.008f, 0.003f), 0.002f));
  // the boarding bridge, standing up across the top window, with its chains
  m.addR(TW_BRIDGE, RM_TIMBER, 0.002f, boxS(vec3(0, bridge.y + 0.17f, bridge.z + 0.012f), vec3(0.1f, 0.17f, 0.009f), 0.003f));
  for (int k = 0; k < 4; ++k)
    m.addR(TW_BRIDGE, RM_TIMBER, 0.001f, boxS(vec3(0, bridge.y + 0.05f + k * 0.08f, bridge.z + 0.024f), vec3(0.09f, 0.006f, 0.005f), 0.002f));
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 a(sd * 0.095f, bridge.y + 0.33f, bridge.z + 0.02f), b(sd * 0.12f, y3 + 0.02f, bridge.z - 0.01f);
    m.addStretch(TW_BRIDGE, TW_BODY, RM_IRON, 0.001f, capsuleS(a, b, 0.004f), along(a, b));
  }
  // wheels: solid discs of planks with iron tyres
  for (int i = 0; i < 4; ++i) {
    int sx = (i % 2 == 0) ? 1 : -1, sz = i < 2 ? 1 : -1;
    vec3 c(sx * wheel1.x, wheel1.y, sz * wheel1.z);
    mat3 F = rotZm(1.5708f);
    int wb = TW_WHEEL1 + i;
    m.addR(wb, RM_WOOD, 0.003f, fnS([c](const vec3& p) {
             vec3 q = p - c;
             return std::max(glm::length(vec2(q.y, q.z)) - 0.072f, std::fabs(q.x) - 0.014f);
           }, c, 0.08f));
    m.addR(wb, RM_IRON, 0.002f, torusS(c, 0.072f, 0.006f, F));
    m.addR(wb, RM_IRON, 0.002f, capsuleS(c - vec3(0.024f, 0, 0), c + vec3(0.024f, 0, 0), 0.016f));
    m.paint(RM_IRON, fnS([c](const vec3& p) {  // two cleats across the planks show it turning
              vec3 q = p - c;
              return std::min(std::fabs(q.y) - 0.006f, std::fabs(q.z - 0.035f) - 0.005f);
            }, c, 0.08f), RM_WOOD);
  }
  for (int sz = -1; sz <= 1; sz += 2)
    m.add(TW_BODY, RM_IRON, 0.002f, capsuleS(vec3(-wheel1.x, wheel1.y, sz * wheel1.z), vec3(wheel1.x, wheel1.y, sz * wheel1.z), 0.009f));
  // the scorpion on the roof: a stand, two torsion springs, the arms and the bolt in its trough
  {
    vec3 s = scorpion;
    m.add(TW_BODY, RM_TIMBER, 0.003f, capsuleS(vec3(0, y3 + 0.01f, s.z), s - vec3(0, 0.01f, 0), 0.011f));
    m.addR(TW_SCORPION, RM_TIMBER, 0.002f, boxS(s + vec3(0, 0.008f, 0.0f), vec3(0.012f, 0.008f, 0.085f), 0.002f));
    m.addR(TW_SCORPION, RM_TIMBER, 0.002f, boxS(s + vec3(0, 0.02f, 0.045f), vec3(0.046f, 0.018f, 0.012f), 0.003f));
    for (int sd = -1; sd <= 1; sd += 2) {
      m.addR(TW_SCORPION, RM_ROPE, 0.003f, cylS(s + vec3(sd * 0.03f, -0.005f, 0.045f), s + vec3(sd * 0.03f, 0.048f, 0.045f), 0.0085f));
      m.addR(TW_SCORPION, RM_BRONZE, 0.001f, cylS(s + vec3(sd * 0.03f, 0.044f, 0.045f), s + vec3(sd * 0.03f, 0.05f, 0.045f), 0.011f));
      vec3 arm0 = s + vec3(sd * 0.03f, 0.02f, 0.045f), arm1 = s + vec3(sd * 0.075f, 0.02f, 0.02f);
      m.addR(TW_SCORPION, RM_WOOD, 0.002f, capsuleS(arm0, arm1, 0.0045f));
      m.addR(TW_SCORPION, RM_ROPE, 0.001f, capsuleS(arm1, s + vec3(0, 0.02f, -0.06f), 0.0018f));
    }
    m.addR(TW_LOADED, RM_WOOD, 0.0f, capsuleS(s + vec3(0, 0.02f, -0.07f), s + vec3(0, 0.02f, 0.1f), 0.0035f));
    m.addR(TW_LOADED, RM_IRON, 0.0f, coneS(s + vec3(0, 0.02f, 0.1f), s + vec3(0, 0.02f, 0.125f), 0.006f, 0.001f));
  }
  // the vexillum: a crimson (or blue) banner with a gold fringe
  {
    vec3 f = flag;
    m.add(TW_BODY, RM_WOOD, 0.002f, cylS(f, f + vec3(0, 0.25f, 0), 0.006f));
    m.addR(TW_FLAG, RM_WOOD, 0.002f, capsuleS(f + vec3(-0.045f, 0.24f, 0), f + vec3(0.045f, 0.24f, 0), 0.004f));
    m.addR(TW_FLAG, RM_CLOAK, 0.002f, fnS([f](const vec3& p) {
             vec3 q = p - (f + vec3(0, 0.19f, 0));
             float wave = 0.006f * std::sin(q.x * 60.0f);
             return std::max(std::max(std::fabs(q.x) - 0.042f, std::fabs(q.y) - 0.045f), std::fabs(q.z - wave) - 0.0025f);
           }, f + vec3(0, 0.19f, 0), 0.07f));
    m.paint(RM_GOLD, fnS([f](const vec3& p) {
              vec3 q = p - (f + vec3(0, 0.19f, 0));
              return std::min(std::fabs(q.y + 0.04f) - 0.005f, glm::length(vec2(q.x, q.y - 0.005f)) - 0.015f);
            }, f + vec3(0, 0.19f, 0), 0.07f), RM_CLOAK);
    m.add(TW_BODY, RM_GOLD, 0.002f, sphereS(f + vec3(0, 0.255f, 0), 0.01f));
  }
  return m;
}

// --- the props ---------------------------------------------------------------
Model pilumModel() {
  Model m;
  addPilum(m, 0, vec3(0), 1.0f);
  return m;
}

Model boltModel() {  // a scorpion bolt: butt at the origin, along +y
  Model m;
  m.add(0, RM_WOOD, 0.0f, cylS(vec3(0, 0.0f, 0), vec3(0, 0.27f, 0), 0.0055f));
  m.add(0, RM_IRON, 0.002f, fnS([](const vec3& p) {
          vec3 q = p - vec3(0, 0.27f, 0);
          float t = glm::clamp(q.y / 0.05f, 0.0f, 1.0f);
          float w = 0.0095f * (1.0f - t) + 0.0006f;
          return std::max(std::fabs(q.x) + std::fabs(q.z) - w, std::max(-q.y, q.y - 0.05f)) * 0.7f;
        }, vec3(0, 0.295f, 0), 0.04f));
  for (int k = 0; k < 3; ++k) {  // three vanes of wood
    float a = k * 2.0944f;
    vec3 d(std::sin(a), 0, std::cos(a));
    m.add(0, RM_WOOD, 0.001f, triS(vec3(0, 0.012f, 0) + d * 0.004f, vec3(0, 0.07f, 0) + d * 0.004f, vec3(0, 0.02f, 0) + d * 0.022f, 0.0012f));
  }
  return m;
}

Model poolModel() {  // a pool of blood with splashes round it; radius ~1 (scaled down when used)
  Model m;
  m.add(0, RM_BLOOD, 0.0f, fnS([](const vec3& p) {
          float a = std::atan2(p.x, p.z);
          float r = std::sqrt(p.x * p.x + p.z * p.z);
          float edge = 0.78f + 0.1f * std::sin(a * 3.0f + 0.7f) + 0.06f * std::sin(a * 7.0f + 2.1f) + 0.03f * std::sin(a * 13.0f);
          float d = r - edge;
          for (int k = 0; k < 7; ++k) {  // splashes
            float b = k * 0.9f + 0.4f;
            vec2 c = vec2(std::sin(b), std::cos(b)) * (0.98f + 0.13f * (k % 3));
            d = std::min(d, glm::length(vec2(p.x, p.z) - c) - (0.06f + 0.025f * (k % 4)));
          }
          float th = 0.024f * glm::clamp(-d / 0.25f + 0.25f, 0.25f, 1.0f);
          return std::max(d * 0.8f, std::fabs(p.y) - th);
        }, vec3(0), 1.2f));
  return m;
}

Model gobletModel() {  // the empress's goblet, held by its stem at the origin
  Model m;
  vec3 g(0);
  m.add(0, RM_GOLD, 0.002f, cylS(g - vec3(0, 0.026f, 0), g - vec3(0, 0.022f, 0), 0.016f));
  m.add(0, RM_GOLD, 0.0f, cylS(g - vec3(0, 0.024f, 0), g + vec3(0, 0.014f, 0), 0.0035f));
  m.add(0, RM_GOLD, 0.004f, fnS([g](const vec3& p) {
          float e = ellipsoid(p - (g + vec3(0, 0.034f, 0)), vec3(0.022f, 0.026f, 0.022f));
          return std::max(std::max(e, -(e + 0.002f)), p.y - (g.y + 0.046f));
        }, g + vec3(0, 0.03f, 0), 0.04f));
  m.add(0, RM_WINE, 0.0f, cylS(g + vec3(0, 0.03f, 0), g + vec3(0, 0.041f, 0), 0.018f));
  return m;
}

Model knifeModel() {  // the centurion's pugio on its own: grip at the origin, blade along +y
  Model m;
  addPugio(m, 0, vec3(0), vec3(0, 1, 0));
  return m;
}

// --- the lion --------------------------------------------------------------------
Model lionModel() {
  using namespace lj;
  Model m;
  m.skel = &skeleton(R_LION);
  Fn bodyF = [](const vec3& p) {
    float chest = ellipsoid(p - vec3(0, 0.31f, 0.1f), vec3(0.085f, 0.1f, 0.12f));
    float belly = ellipsoid(p - vec3(0, 0.3f, -0.05f), vec3(0.075f, 0.08f, 0.16f));
    float haunch = ellipsoid(p - vec3(0, 0.31f, -0.17f), vec3(0.08f, 0.085f, 0.09f));
    return smin(smin(chest, belly, 0.06f), haunch, 0.05f);
  };
  m.add(LN_BODY, RM_FUR, 0.0f, fnS(bodyF, vec3(0, 0.3f, -0.03f), 0.3f));
  // the mane, shaggy, round the neck and the back of the head
  m.add(LN_NECK, RM_LIONMANE, 0.03f, fnS([](const vec3& p) {
          float e = ellipsoid(p - vec3(0, 0.38f, 0.21f), vec3(0.1f, 0.12f, 0.1f));
          float a = std::atan2(p.x, p.y - 0.38f), b = std::atan2(p.z - 0.21f, p.y - 0.38f);
          return e - 0.012f * std::sin(a * 9.0f) * std::sin(b * 7.0f) - 0.006f * std::sin(a * 23.0f + b * 5.0f);
        }, vec3(0, 0.38f, 0.21f), 0.14f));
  m.add(LN_HEAD, RM_LIONMANE, 0.02f, ellS(vec3(0, 0.43f, 0.25f), vec3(0.075f, 0.06f, 0.06f)));
  // the head: skull, muzzle, nose, eyes, ears, the jaw with its teeth
  m.add(LN_HEAD, RM_FUR, 0.02f, ellS(vec3(0, 0.4f, 0.31f), vec3(0.056f, 0.052f, 0.06f)));
  m.add(LN_HEAD, RM_FUR, 0.02f, ellS(vec3(0, 0.38f, 0.37f), vec3(0.04f, 0.033f, 0.038f)));
  m.add(LN_HEAD, RM_DARK, 0.006f, ellS(vec3(0, 0.392f, 0.405f), vec3(0.017f, 0.011f, 0.009f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    m.add(LN_HEAD, RM_EYE, 0.003f, sphereS(vec3(sd * 0.025f, 0.418f, 0.355f), 0.008f));
    m.add(LN_HEAD, RM_FUR, 0.006f, ellS(vec3(sd * 0.042f, 0.45f, 0.29f), vec3(0.016f, 0.016f, 0.008f)));
  }
  m.add(LN_JAW, RM_FUR, 0.012f, ellS(vec3(0, 0.348f, 0.36f), vec3(0.032f, 0.016f, 0.04f)));
  m.add(LN_JAW, RM_DARK, 0.004f, ellS(vec3(0, 0.356f, 0.37f), vec3(0.026f, 0.006f, 0.03f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    m.add(LN_HEAD, RM_TEETH, 0.0f, coneS(vec3(sd * 0.018f, 0.368f, 0.392f), vec3(sd * 0.017f, 0.35f, 0.394f), 0.004f, 0.001f));
    m.add(LN_JAW, RM_TEETH, 0.0f, coneS(vec3(sd * 0.016f, 0.352f, 0.388f), vec3(sd * 0.016f, 0.366f, 0.39f), 0.0035f, 0.001f));
  }
  // legs and paws with claws
  for (int sd = -1; sd <= 1; sd += 2) {
    bool L = sd > 0;
    vec3 fu = L ? flu : mx(flu), fl = L ? fll : mx(fll), hu = L ? hlu : mx(hlu), hl = L ? hll : mx(hll);
    int bFU = L ? LN_FLU : LN_FRU, bFL = L ? LN_FLL : LN_FRL, bHU = L ? LN_HLU : LN_HRU, bHL = L ? LN_HLL : LN_HRL;
    m.add(bFU, RM_FUR, 0.03f, coneS(fu + vec3(0, 0.02f, 0), fl, 0.045f, 0.03f));
    m.add(bFL, RM_FUR, 0.015f, coneS(fl, vec3(fl.x, 0.03f, fl.z + 0.01f), 0.028f, 0.022f));
    m.add(bFL, RM_FUR, 0.012f, ellS(vec3(fl.x, 0.018f, fl.z + 0.022f), vec3(0.026f, 0.018f, 0.032f)));
    m.add(bHU, RM_FUR, 0.035f, coneS(hu, hl, 0.055f, 0.028f));
    m.add(bHL, RM_FUR, 0.015f, coneS(hl, vec3(hl.x, 0.03f, hl.z + 0.03f), 0.026f, 0.021f));
    m.add(bHL, RM_FUR, 0.012f, ellS(vec3(hl.x, 0.018f, hl.z + 0.045f), vec3(0.025f, 0.018f, 0.03f)));
    for (int k = -1; k <= 1; ++k) {
      m.add(bFL, RM_TEETH, 0.0f, coneS(vec3(fl.x + k * 0.011f, 0.014f, fl.z + 0.05f), vec3(fl.x + k * 0.011f, 0.004f, fl.z + 0.058f), 0.0035f, 0.001f));
    }
  }
  m.add(LN_TAIL1, RM_FUR, 0.02f, coneS(tail1, tail2, 0.016f, 0.011f));
  m.add(LN_TAIL2, RM_FUR, 0.01f, coneS(tail2, tail2 + vec3(0, -0.06f, -0.1f), 0.011f, 0.008f));
  m.add(LN_TAIL2, RM_LIONMANE, 0.008f, ellS(tail2 + vec3(0, -0.065f, -0.11f), vec3(0.018f, 0.022f, 0.026f)));
  return m;
}

}  // namespace

// ---------------------------------------------------------------------------
float pieceHeight(int kind) {
  static const float H[6] = {0.92f, 0.78f, 0.95f, 1.3f, 0.92f, 0.98f};
  return H[std::max(0, std::min(5, kind))];
}
float pieceRadius(int kind) {
  static const float R[6] = {0.21f, 0.36f, 0.21f, 0.33f, 0.2f, 0.22f};
  return R[std::max(0, std::min(5, kind))];
}
bool human(int kind) { return kind == R_PAWN || kind == R_BISHOP || kind == R_QUEEN || kind == R_KING; }
vec3 handR(int kind) {
  if (kind == R_KNIGHT) return cj::DU(baseJ(R_PAWN).gripR);
  if (human(kind)) return standJ(kind).gripR;
  return vec3(0, 0.5f, 0.1f);
}
vec3 handL(int kind) {
  if (kind == R_KNIGHT) return cj::DU(baseJ(R_PAWN).gripL);
  if (human(kind)) return standJ(kind).gripL;
  return vec3(0, 0.5f, 0.1f);
}
vec3 itemTip(int kind) {
  switch (kind) {
    case R_PAWN: return vec3(standJ(R_PAWN).gripR.x, 0.954f, standJ(R_PAWN).gripR.z);
    case R_KNIGHT: return handR(R_KNIGHT) + vec3(0, 0.43f, 0);
    case R_BISHOP: {
      HumanJ j = standJ(R_BISHOP);
      return j.gripR + glm::normalize(vec3(0.0f, 0.42f, 1.0f)) * 0.24f * j.S;
    }
    case R_QUEEN: return vec3(standJ(R_QUEEN).gripR.x, 1.094f * figScale(R_QUEEN), standJ(R_QUEEN).gripR.z);
    case R_KING: return vec3(standJ(R_KING).gripR.x, 0.92f * figScale(R_KING), standJ(R_KING).gripR.z);
    default: return handR(kind);
  }
}
vec3 bladeTip(int kind) {
  HumanJ j = standJ(human(kind) ? kind : R_PAWN);
  if (kind == R_BISHOP) return itemTip(kind);
  if (kind == R_QUEEN) return j.gripR + glm::normalize(vec3(0, 0.3f, 1.0f)) * 0.11f * j.S;
  return j.gripR + vec3(0, 0.24f, 0) * j.S;
}
vec3 headPoint(int kind) {
  if (kind == R_KNIGHT) return cj::DU(vec3(0, 0.738f, 0.012f));
  if (kind == R_ROOK) return vec3(0, 0.9f, 0);
  return vec3(0, 0.738f, 0.012f) * figScale(kind);
}
vec3 chestPoint(int kind) {
  if (kind == R_KNIGHT) return cj::DU(vec3(0, 0.56f, 0.03f));
  if (kind == R_ROOK) return vec3(0, 0.45f, 0.2f);
  return vec3(0, 0.56f, 0.04f) * figScale(kind);
}
float strideCycle(int kind) {
  switch (kind) {
    case R_KNIGHT: return 0.36f;
    case R_ROOK: return 0.5f;
    default: return 0.3f * figScale(kind);
  }
}
float wheelRadius(int kind) { return kind == R_ROOK ? 0.078f : cj::wheelR; }
vec3 scorpionPoint() { return tj::scorpion + vec3(0, 0.02f, 0.0f); }
vec3 ramTip() { return vec3(0, tj::ram.y, 0.215f + 0.32f); }
vec3 doorPoint() { return vec3(0, 0.0f, tj::door.z + 0.34f); }
vec3 lionMouth() { return vec3(0, 0.37f, 0.4f); }

void buildLibrary(Library& lib, int quality, meshgen::BuildProgress* prog) {
  quality = std::max(0, std::min(3, quality));
  if (prog) {
    prog->total = 8;
    prog->done = 0;
  }
  auto tick = [&](const char* s) {
    if (prog) {
      prog->stage = s;
      prog->done++;
    }
  };
  auto buildHuman = [&](int kind, Model m, uint32_t seed) {
    float S = figScale(kind);
    m.scaleAll(S);
    buildAsset(lib.asset[kind], m, vec3(-0.26f, -0.005f, -0.2f) * S, vec3(0.28f, kind == R_QUEEN ? 1.13f : 1.0f, 0.3f) * S, quality,
               0, seed, pieceHeight(kind), pieceRadius(kind));
  };
  buildHuman(R_PAWN, legionaryModel(), 7001u);
  tick("Arming the legionaries");
  {
    Model m = chariotModel();
    buildAsset(lib.asset[R_KNIGHT], m, vec3(-0.34f, -0.005f, -0.42f), vec3(0.34f, 0.92f, 0.53f), quality, 0, 7101u,
               pieceHeight(R_KNIGHT), pieceRadius(R_KNIGHT));
    tick("Harnessing the chariots");
  }
  buildHuman(R_BISHOP, centurionModel(), 7201u);
  tick("Promoting the centurions");
  {
    Model m = towerModel();
    buildAsset(lib.asset[R_ROOK], m, vec3(-0.3f, -0.005f, -0.27f), vec3(0.3f, 1.47f, 0.3f), quality, 16, 7301u,
               pieceHeight(R_ROOK), pieceRadius(R_ROOK));
    tick("Building the siege towers");
  }
  buildHuman(R_QUEEN, empressModel(), 7401u);
  tick("Crowning the empresses");
  buildHuman(R_KING, emperorModel(), 7501u);
  tick("Hailing the emperors");
  {
    Model a = pilumModel();
    buildAsset(lib.asset[R_PILUM], a, vec3(-0.03f, -0.01f, -0.03f), vec3(0.03f, 0.98f, 0.03f), quality, 0, 0u, 0.96f, 0.02f);
    Model b = boltModel();
    buildAsset(lib.asset[R_BOLT], b, vec3(-0.03f, -0.01f, -0.03f), vec3(0.03f, 0.34f, 0.03f), quality, 0, 0u, 0.33f, 0.02f);
    Model c = poolModel();
    buildAsset(lib.asset[R_POOL], c, vec3(-1.25f, -0.04f, -1.25f), vec3(1.25f, 0.04f, 1.25f), 0, 0, 0u, 0.03f, 1.0f);
    Model d = gobletModel();
    buildAsset(lib.asset[R_GOBLET], d, vec3(-0.03f, -0.03f, -0.03f), vec3(0.03f, 0.06f, 0.03f), quality, 0, 0u, 0.07f, 0.03f);
    Model e = knifeModel();
    buildAsset(lib.asset[R_KNIFE], e, vec3(-0.025f, -0.04f, -0.025f), vec3(0.025f, 0.12f, 0.025f), quality, 0, 0u, 0.15f, 0.02f);
    tick("Forging the pila");
  }
  {
    Model m = lionModel();
    buildAsset(lib.asset[R_LION], m, vec3(-0.15f, -0.01f, -0.6f), vec3(0.15f, 0.56f, 0.46f), quality, 0, 0u, 0.5f, 0.3f);
    tick("Waking the lions");
  }
  lib.built = true;
  if (prog) prog->finished = true;
}

}  // namespace roman
