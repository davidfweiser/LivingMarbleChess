// Sculpting the alien army with the shared kit (sculpt.h): every figure is a
// list of SDF parts bound to bones and painted with materials, meshed by the
// surface-nets mesher and skinned per vertex.
#include <algorithm>
#include <cmath>

#include "alien.h"
#include "sculpt.h"

namespace alien {
using glm::mat3;
using glm::vec2;
using glm::vec3;
using namespace sdf;
using namespace sculpt;

namespace {

// ---------------------------------------------------------------------------
// Joint layouts
// ---------------------------------------------------------------------------
struct AJ {  // humanoid aliens (HumanoidBone)
  vec3 base{0}, pelvis{0}, chest{0}, head{0};
  vec3 hipL{0}, kneeL{0}, ankleL{0};
  vec3 shL{0}, elL{0}, wrL{0}, shR{0}, elR{0}, wrR{0};
  vec3 gripR{0}, gripL{0};
  vec3 antL{0}, jaw{0}, tail1{0}, tail2{0}, tail3{0};
  bool tongue = false;  // tail1/tail2 are the tongue (pawn) instead of a tail
};

// The little green man: sculpted 0.66 tall (to the tips of his antennae), built PS times larger.
const float PS = 1.2f;
AJ pawnJ() {
  AJ j;
  j.pelvis = vec3(0, 0.2f, 0);
  j.chest = vec3(0, 0.255f, 0);
  j.head = vec3(0, 0.375f, 0.004f);
  j.hipL = vec3(0.034f, 0.195f, 0);
  j.kneeL = vec3(0.036f, 0.108f, 0.006f);
  j.ankleL = vec3(0.037f, 0.03f, -0.002f);
  j.shL = vec3(0.07f, 0.345f, -0.004f);
  j.elL = vec3(0.086f, 0.268f, -0.012f);
  j.wrL = vec3(0.082f, 0.205f, 0.028f);
  j.shR = vec3(-0.07f, 0.345f, -0.004f);
  j.elR = vec3(-0.086f, 0.27f, -0.01f);
  j.wrR = vec3(-0.082f, 0.22f, 0.052f);
  j.gripR = vec3(-0.082f, 0.216f, 0.07f);
  j.gripL = vec3(0.082f, 0.198f, 0.04f);
  j.antL = vec3(0.03f, 0.55f, -0.012f);
  j.jaw = vec3(0, 0.425f, 0.04f);
  j.tail1 = vec3(0, 0.416f, 0.046f);  // the tongue
  j.tail2 = vec3(0, 0.416f, 0.066f);  // its tip
  j.tongue = true;
  return j;
}

// The psychic sits cross-legged on his floating disc.
const float BS = 1.15f;
AJ bishopJ() {
  AJ j;
  j.base = vec3(0, 0.16f, 0);
  j.pelvis = vec3(0, 0.25f, -0.01f);
  j.chest = vec3(0, 0.29f, -0.01f);
  j.head = vec3(0, 0.44f, 0.0f);
  j.hipL = vec3(0.04f, 0.245f, 0);
  j.kneeL = vec3(0.12f, 0.215f, 0.08f);
  j.ankleL = vec3(-0.03f, 0.205f, 0.11f);
  j.shL = vec3(0.064f, 0.405f, -0.012f);
  j.elL = vec3(0.095f, 0.33f, 0.0f);
  j.wrL = vec3(0.09f, 0.27f, 0.075f);
  j.shR = vec3(-0.064f, 0.405f, -0.012f);
  j.elR = vec3(-0.095f, 0.33f, 0.0f);
  j.wrR = vec3(-0.09f, 0.27f, 0.075f);
  j.gripR = vec3(-0.088f, 0.262f, 0.1f);
  j.gripL = vec3(0.088f, 0.262f, 0.1f);
  j.antL = vec3(0.03f, 0.7f, 0);
  j.jaw = vec3(0, 0.485f, 0.04f);
  j.tail1 = vec3(0, 0.24f, -0.06f);
  j.tail2 = vec3(0, 0.2f, -0.1f);
  j.tail3 = vec3(0, 0.18f, -0.14f);
  for (vec3* v : {&j.base, &j.pelvis, &j.chest, &j.head, &j.hipL, &j.kneeL, &j.ankleL, &j.shL, &j.elL, &j.wrL, &j.shR,
                  &j.elR, &j.wrR, &j.gripR, &j.gripL, &j.antL, &j.jaw, &j.tail1, &j.tail2, &j.tail3})
    *v *= BS;
  return j;
}

// The queen floats a little above the board in her gown; her tail trails behind.
AJ queenJ() {
  AJ j;
  j.base = vec3(0, 0, 0);
  j.pelvis = vec3(0, 0.44f, -0.01f);
  j.chest = vec3(0, 0.52f, -0.01f);
  j.head = vec3(0, 0.78f, 0.0f);
  j.hipL = vec3(0.05f, 0.42f, 0);
  j.kneeL = vec3(0.055f, 0.24f, 0.01f);
  j.ankleL = vec3(0.055f, 0.08f, 0);
  j.shL = vec3(0.088f, 0.715f, -0.014f);
  j.elL = vec3(0.118f, 0.585f, -0.022f);
  j.wrL = vec3(0.105f, 0.475f, 0.04f);
  j.shR = vec3(-0.088f, 0.715f, -0.014f);
  j.elR = vec3(-0.12f, 0.59f, -0.02f);
  j.wrR = vec3(-0.11f, 0.5f, 0.07f);
  j.gripR = vec3(-0.108f, 0.5f, 0.092f);
  j.gripL = vec3(0.1f, 0.465f, 0.06f);
  j.antL = vec3(0.025f, 0.94f, -0.06f);
  j.jaw = vec3(0, 0.83f, 0.03f);
  j.tail1 = vec3(0, 0.3f, -0.12f);
  j.tail2 = vec3(0, 0.14f, -0.24f);
  j.tail3 = vec3(0, 0.05f, -0.34f);
  return j;
}

// The overlord sits on his hover throne.
AJ kingJ() {
  AJ j;
  j.base = vec3(0, 0.12f, -0.04f);
  j.pelvis = vec3(0, 0.44f, -0.05f);
  j.chest = vec3(0, 0.49f, -0.05f);
  j.head = vec3(0, 0.71f, -0.02f);
  j.hipL = vec3(0.075f, 0.43f, -0.02f);
  j.kneeL = vec3(0.085f, 0.425f, 0.15f);
  j.ankleL = vec3(0.085f, 0.27f, 0.17f);
  j.shL = vec3(0.13f, 0.66f, -0.05f);
  j.elL = vec3(0.185f, 0.535f, -0.01f);
  j.wrL = vec3(0.18f, 0.47f, 0.11f);
  j.shR = vec3(-0.13f, 0.66f, -0.05f);
  j.elR = vec3(-0.185f, 0.54f, -0.01f);
  j.wrR = vec3(-0.165f, 0.5f, 0.12f);
  j.gripR = vec3(-0.16f, 0.5f, 0.145f);
  j.gripL = vec3(0.175f, 0.46f, 0.14f);
  j.antL = vec3(0.03f, 0.93f, -0.02f);
  j.jaw = vec3(0, 0.7f, 0.06f);
  j.tail1 = vec3(0, 0.3f, -0.15f);
  j.tail2 = vec3(0, 0.25f, -0.2f);
  j.tail3 = vec3(0, 0.2f, -0.25f);
  return j;
}

Skeleton humanoidSkel(const AJ& j) {
  return makeSkeleton({
      {-1, j.base, false},
      {AH_BASE, j.pelvis, false},
      {AH_PELVIS, j.chest, false},
      {AH_CHEST, j.head, false},
      {AH_PELVIS, j.hipL, false},
      {AH_LTHIGH, j.kneeL, false},
      {AH_PELVIS, mx(j.hipL), false},
      {AH_RTHIGH, mx(j.kneeL), false},
      {AH_CHEST, j.shL, false},
      {AH_LARM, j.elL, false},
      {AH_CHEST, j.shR, false},
      {AH_RARM, j.elR, false},
      {AH_RFORE, j.gripR, true},
      {AH_LFORE, j.gripL, true},
      {AH_HEAD, j.antL, false},
      {AH_HEAD, mx(j.antL), false},
      {AH_HEAD, j.jaw, false},
      {j.tongue ? AH_JAW : AH_PELVIS, j.tail1, j.tongue},
      {j.tongue ? AH_JAW : AH_TAIL1, j.tail2, j.tongue},
      {AH_TAIL2, j.tail3, false},
  });
}

// The saucer hovers with its hull's mid-plane at H0 (sculpted, then built US times larger).
const float H0 = 0.36f;
const float US = 1.1f;
namespace uj {
const vec3 body(0, H0, 0), rim(0, H0, 0), pilot(0, H0 + 0.07f, 0), head(0, H0 + 0.1f, 0);
const vec3 armL(0.034f, H0 + 0.092f, -0.004f), hatch(0, H0 - 0.06f, 0);
const vec3 antL(0.016f, H0 + 0.152f, -0.008f);
const float legA[3] = {3.14159265f, 3.14159265f / 3.0f, -3.14159265f / 3.0f};  // back, front-left, front-right
inline vec3 legRoot(int i) { return vec3(std::sin(legA[i]) * 0.17f, H0 - 0.035f, std::cos(legA[i]) * 0.17f); }
}  // namespace uj

// The tripod: pod at 0.8, three legs (one behind, two in front), two tentacles.
namespace tj {
const vec3 body(0, 0.8f, 0), hood(0, 0.82f, 0.15f);
const float legA[3] = {3.14159265f, 1.0471976f, -1.0471976f};
// the legs rise from the pod's sides to high knees and come down to splayed feet
inline vec3 hip(int i) { return vec3(std::sin(legA[i]) * 0.17f, 0.79f, std::cos(legA[i]) * 0.17f); }
inline vec3 knee(int i) { return vec3(std::sin(legA[i]) * 0.3f, 1.0f, std::cos(legA[i]) * 0.3f); }
inline vec3 foot(int i) { return vec3(std::sin(legA[i]) * 0.34f, 0.03f, std::cos(legA[i]) * 0.34f); }
const vec3 tl1(0.075f, 0.71f, 0.1f), tl2(0.085f, 0.6f, 0.125f), tl3(0.085f, 0.49f, 0.13f), tlTip(0.075f, 0.38f, 0.125f);
}  // namespace tj

// The cow, standing, facing +z.
namespace cj {
const vec3 body(0, 0.22f, 0), head(0, 0.29f, 0.15f), fl(0.06f, 0.18f, 0.11f), hl(0.06f, 0.18f, -0.11f),
    tail(0, 0.29f, -0.17f);
}  // namespace cj

}  // namespace

// ===========================================================================
// Skeletons
// ===========================================================================
const Skeleton& skeleton(int asset) {
  static const Skeleton pawn = scaledSkeleton(humanoidSkel(pawnJ()), PS);
  static const Skeleton bishop = humanoidSkel(bishopJ());
  static const Skeleton queen = humanoidSkel(queenJ());
  static const Skeleton king = humanoidSkel(kingJ());
  static const Skeleton saucer = [] {
    using namespace uj;
    return scaledSkeleton(makeSkeleton({
        {-1, body, false},
        {UB_BODY, rim, true},
        {UB_BODY, pilot, false},
        {UB_PILOT, head, false},
        {UB_PILOT, armL, false},
        {UB_PILOT, mx(armL), false},
        {UB_BODY, hatch, true},
        {UB_BODY, legRoot(0), true},
        {UB_BODY, legRoot(1), true},
        {UB_BODY, legRoot(2), true},
        {UB_HEAD, antL, false},
        {UB_HEAD, mx(antL), false},
    }), US);
  }();
  static const Skeleton tripod = [] {
    using namespace tj;
    return makeSkeleton({
        {-1, body, false},
        {TB_BODY, hood, false},
        {TB_BODY, hip(0), false},
        {TB_L1U, knee(0), false},
        {TB_BODY, hip(1), false},
        {TB_L2U, knee(1), false},
        {TB_BODY, hip(2), false},
        {TB_L3U, knee(2), false},
        {TB_BODY, tl1, false},
        {TB_TL1, tl2, false},
        {TB_TL2, tl3, false},
        {TB_BODY, mx(tl1), false},
        {TB_TR1, mx(tl2), false},
        {TB_TR2, mx(tl3), false},
    });
  }();
  static const Skeleton cow = [] {
    using namespace cj;
    return makeSkeleton({
        {-1, body, false},
        {CW_BODY, head, false},
        {CW_BODY, fl, false},
        {CW_BODY, mx(fl), false},
        {CW_BODY, hl, false},
        {CW_BODY, mx(hl), false},
        {CW_BODY, tail, false},
    });
  }();
  static const Skeleton none = makeSkeleton({});
  switch (asset) {
    case AL_PAWN: return pawn;
    case AL_KNIGHT:
    case AL_CANOPY: return saucer;
    case AL_BISHOP:
    case AL_JAR: return bishop;
    case AL_ROOK: return tripod;
    case AL_QUEEN: return queen;
    case AL_KING: return king;
    case AL_COW: return cow;
    default: return none;
  }
}

namespace {

// ---------------------------------------------------------------------------
// Shared parts
// ---------------------------------------------------------------------------
// An alien head facing +z: a big cranium over a small face, huge slanted eyes,
// nostril slits and a small mouth. c: centre of the cranium, s: scale.
// jaw >= 0: the chin is bound to that bone (the mouth can open).
void addAlienHead(Model& m, int bone, int jawBone, vec3 c, float s, int skin) {
  auto P = [&](float x, float y, float z) { return c + vec3(x, y, z) * s; };
  m.add(bone, skin, 0.0f, ellS(P(0, 0, 0), vec3(0.088f, 0.082f, 0.082f) * s));
  m.add(bone, skin, 0.045f * s, ellS(P(0, -0.056f, 0.024f), vec3(0.05f, 0.048f, 0.05f) * s));
  // cheekbones and a brow ridge
  m.add2(bone, bone, skin, 0.025f * s, ellS(P(0.04f, -0.046f, 0.045f), vec3(0.018f, 0.013f, 0.02f) * s));
  // the chin (opens with the jaw)
  m.add(jawBone >= 0 ? jawBone : bone, skin, 0.018f * s, ellS(P(0, -0.083f, 0.045f), vec3(0.028f, 0.016f, 0.028f) * s));
  // eyes: deep sockets with big glossy almond eyes, slanted up and outwards
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    mat3 R = rotYm(0.42f * x) * rotZm(0.4f * x);
    vec3 e = P(0.039f * x, -0.022f, 0.069f);
    m.sub(-1, 0.006f * s, ellS(e + vec3(0, 0, 0.005f) * s, vec3(0.034f, 0.0225f, 0.015f) * s, R));
    m.add(bone, AM_EYE, 0.002f * s, ellS(e + vec3(0, 0, 0.002f) * s, vec3(0.032f, 0.0205f, 0.0135f) * s, R));
  }
  // nostrils and the mouth
  for (int sd = -1; sd <= 1; sd += 2)
    m.sub(skin, 0.002f * s, ellS(P(0.0065f * sd, -0.052f, 0.076f), vec3(0.0022f, 0.004f, 0.004f) * s));
  m.add(bone, AM_DARK, 0.003f * s, ellS(P(0, -0.072f, 0.052f), vec3(0.013f, 0.006f, 0.018f) * s));
  m.paint(AM_DARK, ellS(P(0, -0.07f, 0.074f), vec3(0.017f, 0.0028f, 0.012f) * s, rotXm(-0.15f)), skin);
}

// A three-fingered hand. open = 0: a fist around a grip along 'axis'; 1: fingers spread forward.
void addHand(Model& m, int bone, vec3 wrist, vec3 dir, vec3 side, float s, float open, int mat) {
  dir = glm::normalize(dir);
  side = glm::normalize(side - dir * glm::dot(side, dir));
  vec3 up = glm::cross(dir, side);
  m.add(bone, mat, 0.008f * s, ellS(wrist + dir * 0.012f * s, vec3(0.013f, 0.009f, 0.015f) * s, mat3(side, up, dir)));
  for (int f = -1; f <= 1; ++f) {
    vec3 a = wrist + dir * 0.022f * s + side * (0.007f * f) * s;
    vec3 d = glm::normalize(dir * (0.4f + 0.6f * open) - up * (1.0f - open) + side * (0.25f * f * open));
    float len = (f == 0 ? 0.03f : 0.026f) * s;
    m.add(bone, mat, 0.004f * s, capsuleS(a, a + d * len, 0.0042f * s));
    m.add(bone, mat, 0.002f * s, sphereS(a + d * len, 0.0052f * s));  // fingertip pads
  }
  // the thumb
  m.add(bone, mat, 0.004f * s, capsuleS(wrist + dir * 0.012f * s + up * 0.006f * s,
                                        wrist + dir * 0.03f * s + up * 0.016f * s - side * 0.006f * s, 0.004f * s));
}

// A retro ray gun along +z with its grip at g.
void addRayGun(Model& m, int bone, vec3 g, float s) {
  auto P = [&](float x, float y, float z) { return g + vec3(x, y, z) * s; };
  m.addR(bone, AM_RUBBER, 0.004f * s, boxS(P(0, -0.012f, -0.004f), vec3(0.0075f, 0.017f, 0.01f) * s, 0.006f * s,
                                           rotXm(-0.25f)));
  m.addR(bone, AM_CHROME, 0.006f * s, ellS(P(0, 0.012f, 0.008f), vec3(0.015f, 0.016f, 0.026f) * s));
  m.addR(bone, AM_CHROME, 0.004f * s, coneS(P(0, 0.012f, 0.02f), P(0, 0.012f, 0.066f), 0.0085f * s, 0.0048f * s));
  for (int i = 0; i < 3; ++i)
    m.addR(bone, AM_GOLD, 0.002f * s, torusS(P(0, 0.012f, 0.032f + 0.012f * i), (0.0095f - 0.0012f * i) * s,
                                             0.0022f * s, frameY(vec3(0, 0, 1))));
  m.addR(bone, AM_LIGHT, 0.002f * s, sphereS(P(0, 0.012f, 0.069f), 0.0058f * s));
  // fins
  m.addR(bone, AM_CHROME, 0.003f * s, boxS(P(0, 0.03f, 0.0f), vec3(0.0015f, 0.008f, 0.012f) * s, 0.0012f * s));
  m.addR(bone, AM_LIGHT, 0.002f * s, sphereS(P(0, 0.012f, -0.017f), 0.006f * s));
}

// Antennae: a thin stalk and a glowing bulb.
void addAntennae(Model& m, int boneL, int boneR, vec3 rootL, vec3 tipL, float r, float bulb, int skin) {
  for (int sd = -1; sd <= 1; sd += 2) {
    int b = sd > 0 ? boneL : boneR;
    vec3 a = sd > 0 ? rootL : mx(rootL), t = sd > 0 ? tipL : mx(tipL);
    m.add(b, skin, 0.006f, sphereS(a, r * 1.9f));
    m.add(b, skin, 0.004f, capsuleS(a, t, r));
    m.add(b, AM_LIGHT, 0.003f, sphereS(t + glm::normalize(t - a) * bulb * 0.6f, bulb));
  }
}

// ---------------------------------------------------------------------------
// Pawn: the little green man
// ---------------------------------------------------------------------------
Model pawnModel() {
  AJ j = pawnJ();
  Model m;
  m.skel = &skeleton(AL_PAWN);
  // legs in the jumpsuit, chrome boots
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int th = sd > 0 ? AH_LTHIGH : AH_RTHIGH, sh = sd > 0 ? AH_LSHIN : AH_RSHIN;
    vec3 hip(j.hipL.x * x, j.hipL.y, j.hipL.z), knee(j.kneeL.x * x, j.kneeL.y, j.kneeL.z),
        ankle(j.ankleL.x * x, j.ankleL.y, j.ankleL.z);
    m.add(th, AM_SUIT, 0.0f, coneS(hip, knee, 0.025f, 0.02f));
    m.add(sh, AM_SUIT, 0.01f, coneS(knee, ankle + vec3(0, 0.03f, 0), 0.02f, 0.018f));
    m.add(sh, AM_CHROME, 0.008f, coneS(ankle + vec3(0, 0.036f, 0), ankle, 0.0205f, 0.021f));
    m.add(sh, AM_CHROME, 0.012f, boxS(vec3(ankle.x, 0.017f, 0.012f), vec3(0.019f, 0.015f, 0.034f), 0.013f));
    m.add(sh, AM_RUBBER, 0.0f, boxS(vec3(ankle.x, 0.004f, 0.012f), vec3(0.02f, 0.004f, 0.036f), 0.004f));
  }
  // body
  m.add(AH_PELVIS, AM_SUIT, 0.0f, ellS(vec3(0, 0.222f, 0.002f), vec3(0.052f, 0.046f, 0.04f)));
  m.add(AH_CHEST, AM_SUIT, 0.035f, ellS(vec3(0, 0.302f, 0), vec3(0.058f, 0.066f, 0.043f)));
  m.add2(AH_CHEST, AH_CHEST, AM_CHROME, 0.01f, ellS(vec3(0.066f, 0.35f, -0.004f), vec3(0.025f, 0.013f, 0.025f)));
  m.add(AH_CHEST, AM_CHROME, 0.006f, torusS(vec3(0, 0.36f, -0.002f), 0.029f, 0.0062f));
  m.add(AH_PELVIS, AM_CHROME, 0.004f, torusS(vec3(0, 0.244f, 0.002f), 0.054f, 0.0058f));
  m.add(AH_PELVIS, AM_LIGHT, 0.003f, boxS(vec3(0, 0.244f, 0.056f), vec3(0.011f, 0.008f, 0.004f), 0.003f));
  // chest emblem: a glowing disc in a chrome ring
  m.add(AH_CHEST, AM_CHROME, 0.004f, torusS(vec3(0, 0.318f, 0.041f), 0.013f, 0.0035f, frameY(vec3(0, 0.25f, 1))));
  m.add(AH_CHEST, AM_LIGHT, 0.003f, ellS(vec3(0, 0.318f, 0.04f), vec3(0.01f, 0.01f, 0.005f)));
  // arms: sleeves to the elbow, bare skinny forearms
  for (int sd = -1; sd <= 1; sd += 2) {
    bool L = sd > 0;
    vec3 sh = L ? j.shL : j.shR, el = L ? j.elL : j.elR, wr = L ? j.wrL : j.wrR;
    m.add(L ? AH_LARM : AH_RARM, AM_SUIT, 0.012f, coneS(sh, el, 0.0175f, 0.0145f));
    m.add(L ? AH_LFORE : AH_RFORE, AM_SKIN, 0.008f, coneS(el, wr, 0.0118f, 0.0098f));
    m.add(L ? AH_LFORE : AH_RFORE, AM_CHROME, 0.004f,
          coneS(el + glm::normalize(wr - el) * 0.008f, el + glm::normalize(wr - el) * 0.02f, 0.0152f, 0.014f));
  }
  addHand(m, AH_RFORE, j.wrR, vec3(0, -0.1f, 1), vec3(1, 0, 0), 1.0f, 0.0f, AM_SKIN);
  addHand(m, AH_LFORE, j.wrL, vec3(0, -0.6f, 1), vec3(-1, 0, 0), 1.0f, 0.8f, AM_SKIN);
  addRayGun(m, AH_ITEMR, j.gripR, 1.0f);
  // neck and the head
  m.add(AH_HEAD, AM_SKIN, 0.012f, capsuleS(vec3(0, 0.355f, 0), vec3(0, 0.41f, 0.012f), 0.019f));
  addAlienHead(m, AH_HEAD, AH_JAW, vec3(0, 0.49f, 0.008f), 1.0f, AM_SKIN);
  addAntennae(m, AH_ANTL, AH_ANTR, j.antL, vec3(0.06f, 0.635f, -0.032f), 0.0042f, 0.0125f, AM_SKIN);
  // the tongue, rolled up behind the lips (it shoots out in a battle)
  m.addR(AH_TAIL1, AM_PINK, 0.0f, capsuleS(j.tail1 - vec3(0, 0, 0.008f), j.tail2, 0.0075f));
  m.addR(AH_TAIL2, AM_PINK, 0.002f, ellS(j.tail2 + vec3(0, 0, 0.003f), vec3(0.011f, 0.0065f, 0.009f)));
  m.scaleAll(PS);
  return m;
}

// ---------------------------------------------------------------------------
// Knight: the flying saucer and its pilot
// ---------------------------------------------------------------------------
Model knightModel() {
  using namespace uj;
  Model m;
  m.skel = &skeleton(AL_KNIGHT);
  static Profile hull;
  static bool once = [] {
    hull.build({{0, -0.064f}, {0.08f, -0.06f}, {0.13f, -0.047f}, {0.2f, -0.029f}, {0.27f, -0.013f}, {0.305f, -0.004f},
                {0.318f, 0.0f}, {0.305f, 0.005f}, {0.27f, 0.013f}, {0.22f, 0.029f}, {0.17f, 0.046f}, {0.115f, 0.06f},
                {0.06f, 0.067f}, {0, 0.068f}},
               3);
    return true;
  }();
  (void)once;
  vec3 c = body;
  m.add(UB_BODY, AM_HULL, 0.0f, latheS(&hull, c));
  // the underside is polished metal, with a ring of ports and the beam emitter
  m.paint(AM_CHROME, boxS(c - vec3(0, 0.2f, 0), vec3(0.4f, 0.2f - 0.006f, 0.4f), 0.0f), AM_HULL);
  m.add(UB_BODY, AM_CHROME, 0.004f, torusS(c + vec3(0, 0.032f, 0), 0.2f, 0.0055f));
  m.add(UB_BODY, AM_CHROME, 0.004f, torusS(c - vec3(0, 0.04f, 0), 0.15f, 0.006f));
  for (int i = 0; i < 8; ++i) {
    float a = 6.2831853f * (i + 0.5f) / 8.0f;
    vec3 d(std::sin(a), 0, std::cos(a));
    m.add(UB_BODY, AM_DARK, 0.003f, ellS(c + d * 0.15f + vec3(0, 0.046f, 0), vec3(0.018f, 0.005f, 0.018f)));
    m.add(UB_BODY, AM_ENERGY, 0.003f, sphereS(c + d * 0.215f - vec3(0, 0.026f, 0), 0.0105f));
  }
  // the light ring round the rim (it spins)
  m.add(UB_RIM, AM_CHROME, 0.004f, torusS(c, 0.304f, 0.0105f));
  for (int i = 0; i < 14; ++i) {
    float a = 6.2831853f * i / 14.0f;
    m.add(UB_RIM, AM_LIGHT, 0.002f, sphereS(c + vec3(std::sin(a), 0, std::cos(a)) * 0.314f, 0.0112f));
  }
  // the beam emitter (lowers when the hatch opens)
  m.add(UB_BODY, AM_CHROME, 0.004f, torusS(c - vec3(0, 0.06f, 0), 0.058f, 0.008f));
  m.addR(UB_HATCH, AM_ENERGY, 0.004f, ellS(c - vec3(0, 0.062f, 0), vec3(0.048f, 0.013f, 0.048f)));
  m.addR(UB_HATCH, AM_CHROME, 0.002f, torusS(c - vec3(0, 0.068f, 0), 0.03f, 0.004f));
  // landing legs (sculpted out; folded up under the hull while flying)
  for (int i = 0; i < 3; ++i) {
    vec3 r = legRoot(i), d(std::sin(legA[i]), 0, std::cos(legA[i]));
    vec3 knee = r + d * 0.035f - vec3(0, 0.06f, 0), foot = r + d * 0.055f - vec3(0, 0.115f, 0);
    m.addR(UB_LEG1 + i, AM_CHROME, 0.004f, sphereS(r, 0.012f));
    m.addR(UB_LEG1 + i, AM_CHROME, 0.004f, capsuleS(r, knee, 0.0075f));
    m.addR(UB_LEG1 + i, AM_CHROME, 0.004f, capsuleS(knee, foot, 0.006f));
    m.addR(UB_LEG1 + i, AM_RUBBER, 0.004f, ellS(foot, vec3(0.02f, 0.006f, 0.02f)));
  }
  // the cockpit: chrome collar, a dark floor, the console
  m.add(UB_BODY, AM_CHROME, 0.004f, torusS(c + vec3(0, 0.066f, 0), 0.129f, 0.0105f));
  m.add(UB_BODY, AM_DARK, 0.004f, cylS(c + vec3(0, 0.06f, 0), c + vec3(0, 0.072f, 0), 0.12f));
  m.add(UB_BODY, AM_CHROME, 0.004f, boxS(c + vec3(0, 0.083f, 0.07f), vec3(0.04f, 0.012f, 0.014f), 0.006f, rotXm(-0.5f)));
  for (int i = -1; i <= 1; ++i)
    m.add(UB_BODY, i == 0 ? AM_GEM : AM_LIGHT, 0.002f, sphereS(c + vec3(0.02f * i, 0.092f, 0.072f), 0.0045f));
  // the pilot: a little green man from the waist up
  m.add(UB_PILOT, AM_SUIT, 0.0f, ellS(pilot + vec3(0, 0.008f, -0.006f), vec3(0.036f, 0.032f, 0.027f)));
  m.add(UB_PILOT, AM_CHROME, 0.004f, torusS(pilot + vec3(0, 0.035f, -0.004f), 0.018f, 0.004f));
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 sh = sd > 0 ? armL : mx(armL);
    vec3 hand(0.024f * sd, H0 + 0.083f, 0.058f);
    m.add(sd > 0 ? UB_LARM : UB_RARM, AM_SUIT, 0.008f, coneS(sh, sh + vec3(0.006f * sd, -0.022f, 0.02f), 0.0105f, 0.009f));
    m.add(sd > 0 ? UB_LARM : UB_RARM, AM_SKIN, 0.006f, coneS(sh + vec3(0.006f * sd, -0.022f, 0.02f), hand, 0.0075f, 0.0065f));
    m.add(sd > 0 ? UB_LARM : UB_RARM, AM_SKIN, 0.004f, sphereS(hand, 0.009f));
  }
  addAlienHead(m, UB_HEAD, -1, head + vec3(0, 0.02f, 0.008f), 0.5f, AM_SKIN);
  m.add(UB_HEAD, AM_SKIN, 0.008f, capsuleS(pilot + vec3(0, 0.03f, 0), head + vec3(0, 0.0f, 0.004f), 0.011f));
  addAntennae(m, UB_ANTL, UB_ANTR, antL, antL + vec3(0.01f, 0.022f, -0.005f), 0.0026f, 0.0065f, AM_SKIN);
  m.scaleAll(US);
  return m;
}

Model canopyModel() {
  Model m;
  m.skel = &skeleton(AL_KNIGHT);
  vec3 c = uj::body + vec3(0, 0.066f, 0);
  m.add(UB_BODY, AM_CHROME, 0.0f, fnS([c](const vec3& p) {
          float s = std::fabs(glm::length(p - c) - 0.127f) - 0.006f;
          return std::max(s, c.y + 0.004f - p.y);
        }, c + vec3(0, 0.065f, 0), 0.15f));
  m.scaleAll(US);
  return m;
}

// ---------------------------------------------------------------------------
// Bishop: the psychic and his brain
// ---------------------------------------------------------------------------
Model bishopModel() {
  AJ j = bishopJ();
  const float S = BS;
  Model m;
  m.skel = &skeleton(AL_BISHOP);
  auto P = [&](float x, float y, float z) { return vec3(x, y, z) * S; };
  // the floating disc
  {
    static Profile disc;
    static bool once = [] {
      disc.build({{0, -0.03f}, {0.12f, -0.026f}, {0.19f, -0.012f}, {0.205f, 0.0f}, {0.19f, 0.012f}, {0.12f, 0.016f},
                  {0, 0.017f}},
                 3);
      return true;
    }();
    (void)once;
    // (the disc is sculpted at its unscaled size and placed under the scaled figure)
    m.add(AH_BASE, AM_CHROME, 0.0f, latheS(&disc, j.base));
    m.add(AH_BASE, AM_LIGHT, 0.003f, torusS(j.base, 0.2f, 0.0065f));
    m.add(AH_BASE, AM_ENERGY, 0.004f, ellS(j.base - vec3(0, 0.03f, 0), vec3(0.08f, 0.01f, 0.08f)));
    m.add(AH_BASE, AM_HULL, 0.004f, cylS(j.base + vec3(0, 0.012f, 0), j.base + vec3(0, 0.02f, 0), 0.15f));
  }
  // legs crossed in the lotus position, a flowing robe
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int th = sd > 0 ? AH_LTHIGH : AH_RTHIGH, sh = sd > 0 ? AH_LSHIN : AH_RSHIN;
    vec3 hip = P(0.04f * x, 0.245f, 0), knee = P(0.12f * x, 0.215f, 0.08f), ankle = P(-0.035f * x, 0.205f, 0.115f);
    m.add(th, AM_SUIT, 0.02f, coneS(hip, knee, 0.034f * S, 0.026f * S));
    m.add(sh, AM_SUIT, 0.012f, coneS(knee, ankle, 0.026f * S, 0.02f * S));
    m.add(sh, AM_SKIN, 0.006f, ellS(ankle + P(-0.012f * x, 0.004f, 0.008f), P(0.026f, 0.012f, 0.016f)));
  }
  m.add(AH_PELVIS, AM_SUIT, 0.03f, ellS(P(0, 0.25f, -0.01f), P(0.075f, 0.04f, 0.06f)));
  m.add(AH_CHEST, AM_SUIT, 0.035f, ellS(P(0, 0.33f, -0.012f), P(0.06f, 0.085f, 0.044f)));
  // the psychic collar: a tall flared chrome ruff behind the head
  m.add(AH_CHEST, AM_CHROME, 0.006f, fnS([S](const vec3& p) {
          vec3 q = p - vec3(0, 0.43f, -0.035f) * S;
          float r = std::sqrt(q.x * q.x + q.z * q.z);
          float shell = std::fabs(r - (0.06f + 0.55f * std::max(0.0f, q.y)) * S) - 0.005f * S;
          float h = std::max(-q.y - 0.02f * S, q.y - 0.09f * S);
          return std::max(std::max(shell, h), q.z - 0.0f);
        }, P(0, 0.47f, -0.06f), 0.13f * S));
  m.add(AH_CHEST, AM_LIGHT, 0.004f, torusS(P(0, 0.4f, -0.012f), 0.042f * S, 0.005f * S));
  for (int sd = -1; sd <= 1; sd += 2) {
    bool L = sd > 0;
    vec3 sh = L ? j.shL : j.shR, el = L ? j.elL : j.elR, wr = L ? j.wrL : j.wrR;
    m.add(L ? AH_LARM : AH_RARM, AM_SUIT, 0.012f, coneS(sh, el, 0.017f * S, 0.015f * S));
    m.add(L ? AH_LFORE : AH_RFORE, AM_SUIT, 0.008f, coneS(el, wr, 0.015f * S, 0.019f * S));  // wide sleeves
    addHand(m, L ? AH_LFORE : AH_RFORE, wr, vec3(0, 0.15f, 1), vec3(L ? -1.0f : 1.0f, 0, 0), S, 1.0f, AM_SKIN);
  }
  // the little face under an enormous brain
  vec3 face = P(0, 0.485f, 0.03f);
  m.add(AH_HEAD, AM_SKIN, 0.012f, capsuleS(P(0, 0.42f, -0.005f), P(0, 0.47f, 0.01f), 0.02f * S));
  m.add(AH_HEAD, AM_SKIN, 0.0f, ellS(face, P(0.05f, 0.058f, 0.048f)));
  m.add(AH_JAW, AM_SKIN, 0.02f * S, ellS(face + P(0, -0.042f, 0.012f), P(0.026f, 0.02f, 0.03f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    mat3 R = rotYm(0.45f * x) * rotZm(0.4f * x);
    vec3 e = face + P(0.023f * x, 0.002f, 0.04f);
    m.sub(-1, 0.005f * S, ellS(e + P(0, 0, 0.003f), P(0.021f, 0.0125f, 0.01f), R));
    m.add(AH_HEAD, AM_EYE, 0.002f, ellS(e, P(0.0195f, 0.0115f, 0.009f), R));
  }
  m.paint(AM_DARK, ellS(face + P(0, -0.04f, 0.042f), P(0.011f, 0.002f, 0.01f)), AM_SKIN);
  vec3 bc = P(0, 0.6f, -0.01f);
  m.add(AH_HEAD, AM_BRAIN, 0.025f * S, ellS(bc, P(0.122f, 0.102f, 0.13f)));
  m.sub(AM_BRAIN, 0.008f * S, capsuleS(bc + P(0, 0.105f, -0.14f), bc + P(0, 0.1f, 0.13f), 0.008f * S));  // the fissure
  m.add(AH_HEAD, AM_GEM, 0.003f, sphereS(face + P(0, 0.035f, 0.045f), 0.009f * S));  // a third eye
  // the jar's collar
  m.add(AH_HEAD, AM_CHROME, 0.006f, torusS(P(0, 0.515f, -0.01f), 0.142f * S, 0.009f * S));
  m.add(AH_HEAD, AM_LIGHT, 0.003f, torusS(P(0, 0.515f, -0.01f), 0.152f * S, 0.0035f * S));
  return m;
}

Model jarModel() {
  Model m;
  m.skel = &skeleton(AL_BISHOP);
  vec3 c = vec3(0, 0.585f, -0.01f) * BS;
  float y0 = 0.515f * BS, R = 0.168f * BS;
  m.add(AH_HEAD, AM_CHROME, 0.0f, fnS([c, y0, R](const vec3& p) {
          float s = std::fabs(glm::length(p - c) - R) - 0.006f;
          return std::max(s, y0 - p.y);
        }, c, R + 0.01f));
  return m;
}

// ---------------------------------------------------------------------------
// Rook: the tripod
// ---------------------------------------------------------------------------
Model rookModel() {
  using namespace tj;
  Model m;
  m.skel = &skeleton(AL_ROOK);
  vec3 c = body;
  // the pod: a domed hull with a chrome belt of lights
  m.add(TB_BODY, AM_HULL, 0.0f, ellS(c, vec3(0.205f, 0.115f, 0.205f)));
  m.add(TB_BODY, AM_HULL, 0.04f, ellS(c + vec3(0, 0.075f, -0.012f), vec3(0.158f, 0.1f, 0.162f)));
  m.add(TB_BODY, AM_CHROME, 0.006f, torusS(c + vec3(0, 0.005f, 0), 0.203f, 0.0135f));
  for (int i = 0; i < 12; ++i) {
    float a = 6.2831853f * (i + 0.5f) / 12.0f;
    m.add(TB_BODY, AM_LIGHT, 0.002f, sphereS(c + vec3(std::sin(a) * 0.214f, 0.005f, std::cos(a) * 0.214f), 0.0095f));
  }
  for (int i = 0; i < 6; ++i) {  // vents on the dome
    float a = 6.2831853f * i / 6.0f + 0.5f;
    vec3 d(std::sin(a), 0, std::cos(a));
    m.add(TB_BODY, AM_DARK, 0.003f, boxS(c + d * 0.125f + vec3(0, 0.11f, 0), vec3(0.025f, 0.006f, 0.007f), 0.004f,
                                         mat3(glm::cross(vec3(0, 1, 0), d), vec3(0, 1, 0), d) * rotXm(-0.6f)));
  }
  // underside: a ribbed cone where the legs and tentacles meet
  m.add(TB_BODY, AM_CHROME, 0.02f, coneS(c - vec3(0, 0.07f, 0), c - vec3(0, 0.13f, 0), 0.12f, 0.06f));
  for (int i = 0; i < 3; ++i) m.add(TB_BODY, AM_DARK, 0.002f, torusS(c - vec3(0, 0.09f + 0.018f * i, 0), 0.105f - 0.014f * i, 0.004f));
  // an antenna dish and a beacon on top
  m.add(TB_BODY, AM_CHROME, 0.004f, capsuleS(c + vec3(0.05f, 0.15f, -0.06f), c + vec3(0.065f, 0.24f, -0.07f), 0.0045f));
  m.add(TB_BODY, AM_CHROME, 0.003f, fnS([c](const vec3& p) {
          vec3 q = p - (c + vec3(0.068f, 0.245f, -0.07f));
          q = glm::transpose(rotXm(0.6f)) * q;
          float r = std::sqrt(q.x * q.x + q.z * q.z);
          float bowl = std::fabs(q.y - 0.9f * r * r / 0.04f) - 0.002f;
          return std::max(bowl, r - 0.03f);
        }, c + vec3(0.068f, 0.25f, -0.07f), 0.04f));
  m.add(TB_BODY, AM_LIGHT, 0.004f, sphereS(c + vec3(0, 0.168f, -0.01f), 0.012f));
  // the heat-ray eye in its hood
  vec3 h = hood;
  m.add(TB_HOOD, AM_HULL, 0.02f, ellS(h + vec3(0, 0.022f, 0.02f), vec3(0.07f, 0.045f, 0.075f)));
  m.add(TB_HOOD, AM_CHROME, 0.006f, cylS(h + vec3(0, 0, 0.0f), h + vec3(0, 0, 0.1f), 0.048f));
  m.add(TB_HOOD, AM_CHROME, 0.003f, torusS(h + vec3(0, 0, 0.1f), 0.046f, 0.0075f, frameY(vec3(0, 0, 1))));
  m.add(TB_HOOD, AM_ENERGY, 0.002f, ellS(h + vec3(0, 0, 0.1f), vec3(0.038f, 0.038f, 0.012f)));
  m.add(TB_HOOD, AM_DARK, 0.002f, sphereS(h + vec3(0, 0, 0.108f), 0.012f));
  for (int sd = -1; sd <= 1; sd += 2)  // little side lamps
    m.add(TB_HOOD, AM_LIGHT, 0.002f, sphereS(h + vec3(0.058f * sd, 0.028f, 0.07f), 0.0085f));
  // legs: chrome tubes with ball joints and clawed feet
  for (int i = 0; i < 3; ++i) {
    int up = TB_L1U + 2 * i, lo = up + 1;
    vec3 a = hip(i), k = knee(i), f = foot(i), d(std::sin(legA[i]), 0, std::cos(legA[i]));
    m.add(up, AM_CHROME, 0.006f, sphereS(a, 0.038f));
    m.add(up, AM_HULL, 0.008f, coneS(a, k, 0.03f, 0.024f));
    m.add(up, AM_CHROME, 0.004f, capsuleS(glm::mix(a, k, 0.15f) + vec3(0, 0.03f, 0), glm::mix(a, k, 0.75f) + vec3(0, 0.03f, 0), 0.008f));  // piston
    m.add(lo, AM_CHROME, 0.006f, sphereS(k, 0.034f));
    m.add(lo, AM_HULL, 0.008f, coneS(k, glm::mix(k, f, 0.35f), 0.024f, 0.019f));
    m.add(lo, AM_CHROME, 0.006f, coneS(glm::mix(k, f, 0.33f), f + vec3(0, 0.03f, 0), 0.016f, 0.013f));
    for (int r = 2; r <= 5; ++r) {
      vec3 q = glm::mix(k, f, 0.14f * r);
      m.add(lo, AM_RUBBER, 0.002f, torusS(q, 0.018f - 0.0008f * r, 0.004f, frameY(k - f)));
    }
    m.add(lo, AM_CHROME, 0.005f, sphereS(f + vec3(0, 0.03f, 0), 0.02f));
    for (int t = -1; t <= 1; ++t) {  // three toes
      float ta = legA[i] + 0.9f * t;
      vec3 td(std::sin(ta), 0, std::cos(ta));
      m.add(lo, AM_CHROME, 0.004f, coneS(f + vec3(0, 0.025f, 0), f + td * 0.04f, 0.008f, 0.004f));
    }
    (void)d;
  }
  // tentacles: ribbed hoses ending in three-fingered claws
  for (int sd = -1; sd <= 1; sd += 2) {
    int b1 = sd > 0 ? TB_TL1 : TB_TR1;
    vec3 p0 = sd > 0 ? tl1 : mx(tl1), p1 = sd > 0 ? tl2 : mx(tl2), p2 = sd > 0 ? tl3 : mx(tl3),
         p3 = sd > 0 ? tlTip : mx(tlTip);
    vec3 pts[4] = {p0, p1, p2, p3};
    for (int s = 0; s < 3; ++s) {
      m.add(b1 + s, AM_RUBBER, 0.008f, coneS(pts[s], pts[s + 1], 0.019f - 0.003f * s, 0.016f - 0.003f * s));
      for (int r = 0; r < 3; ++r)
        m.add(b1 + s, AM_CHROME, 0.002f,
              torusS(glm::mix(pts[s], pts[s + 1], 0.2f + 0.3f * r), 0.0195f - 0.003f * s, 0.0042f,
                     frameY(pts[s + 1] - pts[s])));
    }
    m.add(b1 + 2, AM_CHROME, 0.004f, sphereS(p3, 0.013f));
    for (int t = 0; t < 3; ++t) {
      float a = 6.2831853f * t / 3.0f;
      vec3 o(std::sin(a) * 0.012f, 0, std::cos(a) * 0.012f);
      m.add(b1 + 2, AM_CHROME, 0.003f, coneS(p3 + o, p3 + o * 1.8f - vec3(0, 0.035f, 0), 0.0055f, 0.002f));
    }
  }
  return m;
}

// ---------------------------------------------------------------------------
// Queen: the crested alien queen
// ---------------------------------------------------------------------------
Model queenModel() {
  AJ j = queenJ();
  Model m;
  m.skel = &skeleton(AL_QUEEN);
  // the gown, floating just above the board, with a gold hem
  static Profile gown;
  static bool once = [] {
    gown.build({{0, 0.035f}, {0.2f, 0.035f}, {0.205f, 0.05f}, {0.17f, 0.15f}, {0.13f, 0.28f}, {0.095f, 0.4f},
                {0.072f, 0.47f}, {0.07f, 0.5f}, {0, 0.5f}},
               3);
    return true;
  }();
  (void)once;
  Fn gownF = [](const vec3& p) {
    float a = std::atan2(p.x, p.z);
    float folds = 0.006f * std::sin(a * 9.0f) * glm::clamp((0.36f - p.y) / 0.25f, 0.0f, 1.0f);
    return gown.eval(p) - folds;
  };
  m.add(AH_PELVIS, AM_ROBE, 0.0f, fnS(gownF, vec3(0, 0.27f, 0), 0.3f));
  m.add(AH_PELVIS, AM_GOLD, 0.006f, torusS(vec3(0, 0.05f, 0), 0.197f, 0.008f));
  m.paint(AM_GOLD, fnS([gownF](const vec3& p) { return std::max(gownF(p) - 0.004f, std::fabs(p.x) - 0.012f); },
                       vec3(0, 0.3f, 0.1f), 0.3f),
          AM_ROBE);
  // carapace bodice, spiked shoulders, jewelled collar
  m.add(AH_CHEST, AM_SHELL, 0.03f, ellS(vec3(0, 0.59f, -0.006f), vec3(0.07f, 0.11f, 0.052f)));
  m.add(AH_PELVIS, AM_SHELL, 0.03f, ellS(vec3(0, 0.49f, -0.006f), vec3(0.06f, 0.05f, 0.048f)));
  for (int i = 0; i < 4; ++i)
    m.sub(AM_SHELL, 0.004f, torusS(vec3(0, 0.5f + 0.035f * i, -0.006f), 0.063f + 0.006f * std::sin(i * 1.3f), 0.003f));
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    m.add(AH_CHEST, AM_SHELL, 0.012f, ellS(vec3(0.078f * x, 0.708f, -0.014f), vec3(0.03f, 0.018f, 0.032f)));
    m.add(AH_CHEST, AM_SHELL, 0.006f, coneS(vec3(0.082f * x, 0.72f, -0.02f), vec3(0.122f * x, 0.79f, -0.04f), 0.012f, 0.002f));
  }
  m.add(AH_CHEST, AM_GOLD, 0.006f, torusS(vec3(0, 0.69f, -0.004f), 0.042f, 0.006f, frameY(vec3(0, 1, 0.2f))));
  m.add(AH_CHEST, AM_GEM, 0.003f, ellS(vec3(0, 0.665f, 0.046f), vec3(0.013f, 0.017f, 0.008f)));
  // arms with carapace bracers and long fingers
  for (int sd = -1; sd <= 1; sd += 2) {
    bool L = sd > 0;
    vec3 sh = L ? j.shL : j.shR, el = L ? j.elL : j.elR, wr = L ? j.wrL : j.wrR;
    m.add(L ? AH_LARM : AH_RARM, AM_SKIN, 0.012f, coneS(sh, el, 0.017f, 0.0135f));
    m.add(L ? AH_LFORE : AH_RFORE, AM_SKIN, 0.008f, coneS(el, wr, 0.013f, 0.01f));
    m.add(L ? AH_LFORE : AH_RFORE, AM_SHELL, 0.004f, coneS(glm::mix(el, wr, 0.3f), glm::mix(el, wr, 0.85f), 0.0155f, 0.0125f));
    addHand(m, L ? AH_LFORE : AH_RFORE, wr, L ? vec3(0, -0.5f, 1) : vec3(0, 0.1f, 1), vec3(L ? -1.0f : 1.0f, 0, 0), 1.15f,
            L ? 0.8f : 0.0f, AM_SKIN);
  }
  // the sceptre: a gold staff with a glowing orb in a claw
  {
    vec3 g = j.gripR;
    m.addR(AH_ITEMR, AM_GOLD, 0.0f, cylS(g - vec3(0, 0.36f, 0), g + vec3(0, 0.3f, 0), 0.0075f));
    m.addR(AH_ITEMR, AM_GOLD, 0.004f, sphereS(g - vec3(0, 0.36f, 0), 0.011f));
    for (int t = 0; t < 3; ++t) {
      float a = 6.2831853f * t / 3.0f;
      vec3 o(std::sin(a), 0, std::cos(a));
      m.addR(AH_ITEMR, AM_GOLD, 0.004f, coneS(g + vec3(0, 0.29f, 0), g + vec3(0, 0.345f, 0) + o * 0.03f, 0.006f, 0.0025f));
    }
    m.addR(AH_ITEMR, AM_GEM, 0.003f, sphereS(g + vec3(0, 0.335f, 0), 0.022f));
  }
  // neck with gold rings, the face, and the great sweeping crest
  m.add(AH_HEAD, AM_SKIN, 0.012f, capsuleS(vec3(0, 0.68f, -0.006f), vec3(0, 0.8f, 0.01f), 0.021f));
  for (int i = 0; i < 3; ++i)
    m.add(AH_HEAD, AM_GOLD, 0.003f, torusS(vec3(0, 0.71f + 0.026f * i, -0.003f + 0.0035f * i), 0.0225f, 0.0045f));
  vec3 fc(0, 0.85f, 0.028f);
  m.add(AH_HEAD, AM_SKIN, 0.0f, ellS(fc, vec3(0.05f, 0.066f, 0.054f)));
  m.add(AH_HEAD, AM_SKIN, 0.03f, ellS(fc + vec3(0, -0.045f, 0.02f), vec3(0.03f, 0.03f, 0.032f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    mat3 R = rotYm(0.4f * x) * rotZm(0.45f * x);
    vec3 e = fc + vec3(0.026f * x, 0.006f, 0.045f);
    m.sub(-1, 0.005f, ellS(e + vec3(0, 0, 0.004f), vec3(0.027f, 0.016f, 0.012f), R));
    m.add(AH_HEAD, AM_EYE, 0.002f, ellS(e + vec3(0, 0, 0.0015f), vec3(0.0255f, 0.0145f, 0.0105f), R));
  }
  m.add(AH_HEAD, AM_DARK, 0.003f, ellS(fc + vec3(0, -0.048f, 0.042f), vec3(0.012f, 0.006f, 0.012f)));
  m.paint(AM_PINK, ellS(fc + vec3(0, -0.045f, 0.064f), vec3(0.014f, 0.006f, 0.01f)), AM_SKIN);
  // the inner jaw, hidden in her mouth until it shoots out
  m.addR(AH_JAW, AM_PINK, 0.0f, capsuleS(j.jaw + vec3(0, -0.003f, -0.015f), j.jaw + vec3(0, -0.003f, 0.012f), 0.008f));
  m.addR(AH_JAW, AM_TEETH, 0.002f, ellS(j.jaw + vec3(0, -0.003f, 0.017f), vec3(0.008f, 0.007f, 0.005f)));
  // the crest: a carapace crown sweeping up and back, edged with gold spikes and gems
  for (int k = -1; k <= 1; ++k) {
    float side = (float)k;
    Fn crest = [fc, side](const vec3& p) {
      vec3 c0 = fc + vec3(0.03f * side, 0.08f - 0.02f * std::fabs(side), -0.07f);
      vec3 q = glm::transpose(rotZm(-0.45f * side) * rotXm(-0.85f + 0.12f * std::fabs(side))) * (p - c0);
      float sz = side == 0 ? 1.0f : 0.72f;
      return ellipsoid(q, vec3(0.022f, 0.055f, 0.19f) * sz);
    };
    m.add(AH_HEAD, AM_SHELL, 0.02f, fnS(crest, fc + vec3(0.04f * side, 0.15f, -0.13f), 0.21f));
  }
  for (int i = 0; i < 6; ++i) {
    float t = i / 5.0f;
    vec3 base = fc + vec3(0, 0.1f + 0.15f * t, -0.03f - 0.125f * t);
    vec3 tip = base + vec3(0, 0.05f - 0.015f * t, 0.016f - 0.03f * t);
    m.add(AH_HEAD, AM_GOLD, 0.003f, coneS(base, tip, 0.011f, 0.0025f));
    m.add(AH_HEAD, AM_GEM, 0.002f, sphereS(tip, 0.0055f));
  }
  // the tail: carapace segments ending in a spade
  vec3 tp[4] = {j.tail1, j.tail2, j.tail3, j.tail3 + vec3(0, -0.02f, -0.1f)};
  for (int s = 0; s < 3; ++s) {
    m.add(AH_TAIL1 + s, AM_SHELL, 0.01f, coneS(tp[s], tp[s + 1], 0.03f - 0.007f * s, 0.024f - 0.007f * s));
    m.add(AH_TAIL1 + s, AM_DARK, 0.002f, torusS(glm::mix(tp[s], tp[s + 1], 0.5f), 0.027f - 0.007f * s, 0.0025f,
                                                 frameY(tp[s + 1] - tp[s])));
  }
  m.add(AH_TAIL3, AM_SHELL, 0.006f, fnS([tp](const vec3& p) {
          vec3 q = p - (tp[3] + vec3(0, 0.0f, -0.025f));
          float d = std::max(std::fabs(q.x) * 1.3f + std::fabs(q.z + 0.012f) - 0.04f, std::fabs(q.y) - 0.006f);
          return d;
        }, tp[3] + vec3(0, 0, -0.025f), 0.05f));
  return m;
}

// ---------------------------------------------------------------------------
// King: the overlord on his hover throne
// ---------------------------------------------------------------------------
Model kingModel() {
  AJ j = kingJ();
  Model m;
  m.skel = &skeleton(AL_KING);
  vec3 b = j.base;
  // --- the hover throne: an egg chair on a hovering drive with fans and lights
  Fn shell = [b](const vec3& p) {
    vec3 q = p - (b + vec3(0, 0.44f, -0.04f));
    float outer = ellipsoid(q, vec3(0.25f, 0.46f, 0.23f));
    float inner = ellipsoid(q - vec3(0, 0.05f, 0.075f), vec3(0.205f, 0.42f, 0.2f));
    float open = -(q.z - 0.06f + 0.18f * std::max(0.0f, q.y + 0.1f));  // cut away the front, more at the top
    return std::max(std::max(outer, -inner), -open);
  };
  m.add(AH_BASE, AM_HULL, 0.0f, fnS(shell, b + vec3(0, 0.44f, -0.06f), 0.48f));
  m.add(AH_BASE, AM_ROBE, 0.006f, fnS([b](const vec3& p) {  // the cushion
          vec3 q = p - (b + vec3(0, 0.24f, 0.03f));
          return roundBox(q, vec3(0.16f, 0.03f, 0.13f), 0.03f);
        }, b + vec3(0, 0.24f, 0.03f), 0.22f));
  for (int sd = -1; sd <= 1; sd += 2) {  // armrests
    float x = (float)sd;
    m.add(AH_BASE, AM_HULL, 0.012f, capsuleS(b + vec3(0.2f * x, 0.32f, -0.08f), b + vec3(0.2f * x, 0.32f, 0.12f), 0.032f));
    m.add(AH_BASE, AM_CHROME, 0.004f, torusS(b + vec3(0.2f * x, 0.32f, 0.12f), 0.026f, 0.006f, frameY(vec3(0, 0, 1))));
  }
  // the control panel on the right armrest: lights and a big red button
  m.add(AH_BASE, AM_CHROME, 0.004f, boxS(b + vec3(-0.2f, 0.355f, 0.06f), vec3(0.03f, 0.008f, 0.045f), 0.006f));
  m.add(AH_BASE, AM_GEM, 0.003f, ellS(b + vec3(-0.2f, 0.366f, 0.085f), vec3(0.014f, 0.007f, 0.014f)));
  for (int i = 0; i < 3; ++i)
    m.add(AH_BASE, AM_LIGHT, 0.002f, sphereS(b + vec3(-0.212f + 0.012f * i, 0.364f, 0.045f), 0.0045f));
  // the drive underneath: a chrome pod with a glowing ring and four fan jets
  m.add(AH_BASE, AM_CHROME, 0.03f, ellS(b + vec3(0, 0.07f, -0.01f), vec3(0.17f, 0.055f, 0.17f)));
  m.add(AH_BASE, AM_ENERGY, 0.004f, torusS(b + vec3(0, 0.03f, -0.01f), 0.12f, 0.01f));
  for (int i = 0; i < 4; ++i) {
    float a = 6.2831853f * (i + 0.5f) / 4.0f;
    vec3 d(std::sin(a), 0, std::cos(a));
    vec3 f = b + d * 0.17f + vec3(0, 0.05f, -0.01f);
    m.add(AH_BASE, AM_HULL, 0.006f, cylS(f - vec3(0, 0.022f, 0), f + vec3(0, 0.022f, 0), 0.04f));
    m.add(AH_BASE, AM_ENERGY, 0.002f, cylS(f - vec3(0, 0.026f, 0), f - vec3(0, 0.018f, 0), 0.03f));
  }
  // the tall back: a crest rising out of the shell, rimmed in gold round a glowing crystal
  Fn fin = [b](const vec3& p) {
    vec3 q = p - (b + vec3(0, 0.9f, -0.2f));
    float f = std::max(std::fabs(q.z) - 0.014f, ellipsoid(vec3(q.x, q.y, 0), vec3(0.12f, 0.2f, 1.0f)));
    return std::max(f, -q.y - 0.18f);
  };
  m.add(AH_BASE, AM_HULL, 0.01f, fnS(fin, b + vec3(0, 0.9f, -0.2f), 0.22f));
  m.add(AH_BASE, AM_GOLD, 0.004f, fnS([b](const vec3& p) {  // a gold rim round the crest
          vec3 q = p - (b + vec3(0, 0.9f, -0.2f));
          float e = ellipsoid(vec3(q.x, q.y, 0), vec3(0.12f, 0.2f, 1.0f));
          return std::max(std::max(std::fabs(e) - 0.007f, std::fabs(q.z) - 0.018f), -q.y - 0.12f);
        }, b + vec3(0, 0.9f, -0.2f), 0.22f));
  m.add(AH_BASE, AM_GEM, 0.004f, ellS(b + vec3(0, 0.97f, -0.18f), vec3(0.028f, 0.05f, 0.018f)));
  // --- the overlord: short legs, a big belly, a cape with a towering collar
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int th = sd > 0 ? AH_LTHIGH : AH_RTHIGH, sh = sd > 0 ? AH_LSHIN : AH_RSHIN;
    vec3 hip(j.hipL.x * x, j.hipL.y, j.hipL.z), knee(j.kneeL.x * x, j.kneeL.y, j.kneeL.z),
        ankle(j.ankleL.x * x, j.ankleL.y, j.ankleL.z);
    m.add(th, AM_SUIT, 0.0f, coneS(hip, knee, 0.042f, 0.036f));
    m.add(sh, AM_SUIT, 0.012f, coneS(knee, ankle + vec3(0, 0.02f, 0), 0.034f, 0.028f));
    m.add(sh, AM_GOLD, 0.01f, boxS(ankle + vec3(0, -0.005f, 0.02f), vec3(0.026f, 0.02f, 0.042f), 0.018f));
  }
  m.add(AH_PELVIS, AM_ROBE, 0.0f, ellS(j.pelvis + vec3(0, 0.0f, 0.03f), vec3(0.12f, 0.07f, 0.1f)));
  m.add(AH_CHEST, AM_ROBE, 0.05f, ellS(j.chest + vec3(0, 0.06f, 0.045f), vec3(0.135f, 0.12f, 0.125f)));  // the belly
  m.add(AH_CHEST, AM_ROBE, 0.04f, ellS(j.chest + vec3(0, 0.15f, -0.01f), vec3(0.13f, 0.06f, 0.08f)));
  m.paint(AM_GOLD, boxS(j.chest + vec3(0, 0.08f, 0.17f), vec3(0.016f, 0.14f, 0.06f), 0.0f), AM_ROBE);  // a gold placket
  m.add(AH_PELVIS, AM_GOLD, 0.006f, torusS(j.pelvis + vec3(0, 0.05f, 0.04f), 0.118f, 0.009f, frameY(vec3(0, 1, 0.25f))));
  m.add(AH_PELVIS, AM_GEM, 0.003f, ellS(j.pelvis + vec3(0, 0.03f, 0.158f), vec3(0.02f, 0.017f, 0.008f)));
  for (int i = 0; i < 3; ++i)  // medals
    m.add(AH_CHEST, i == 1 ? AM_GEM : AM_GOLD, 0.002f,
          cylS(j.chest + vec3(0.06f + 0.026f * i, 0.13f - 0.006f * i, 0.105f - 0.012f * i),
               j.chest + vec3(0.06f + 0.026f * i, 0.13f - 0.006f * i, 0.115f - 0.012f * i), 0.011f));
  // cape and collar
  m.add(AH_CHEST, AM_ROBE, 0.02f, fnS([j](const vec3& p) {
          vec3 q = p - (j.chest + vec3(0, 0.1f, -0.03f));
          float e = ellipsoid(q, vec3(0.155f, 0.12f, 0.115f));
          return std::max(std::max(e, -(e + 0.016f)), q.z - 0.02f + 0.3f * std::max(0.0f, -q.y));
        }, j.chest + vec3(0, 0.1f, -0.06f), 0.18f));
  m.add(AH_CHEST, AM_GOLD, 0.006f, fnS([j](const vec3& p) {
          vec3 q = p - (j.head + vec3(0, -0.02f, -0.04f));
          float r = std::sqrt(q.x * q.x + q.z * q.z);
          float shell = std::fabs(r - (0.1f + 0.5f * std::max(0.0f, q.y))) - 0.006f;
          return std::max(std::max(shell, std::max(-q.y, q.y - 0.17f)), q.z - 0.01f);
        }, j.head + vec3(0, 0.07f, -0.08f), 0.2f));
  for (int sd = -1; sd <= 1; sd += 2) {
    bool L = sd > 0;
    vec3 sh = L ? j.shL : j.shR, el = L ? j.elL : j.elR, wr = L ? j.wrL : j.wrR;
    m.add(L ? AH_LARM : AH_RARM, AM_ROBE, 0.016f, coneS(sh, el, 0.034f, 0.028f));
    m.add(L ? AH_LFORE : AH_RFORE, AM_ROBE, 0.01f, coneS(el, wr, 0.027f, 0.022f));
    m.add(L ? AH_LFORE : AH_RFORE, AM_GOLD, 0.004f, coneS(glm::mix(el, wr, 0.75f), wr, 0.026f, 0.025f));
    addHand(m, L ? AH_LFORE : AH_RFORE, wr, L ? vec3(0, -0.4f, 1) : vec3(0, 0, 1), vec3(L ? -1.0f : 1.0f, 0, 0), 1.6f,
            L ? 0.6f : 0.0f, AM_SKIN);
  }
  // the disintegrator sceptre: a gold rod with a crystal, held up like a wand
  {
    vec3 g = j.gripR;
    vec3 ax = glm::normalize(vec3(0, 0.55f, 1));
    m.addR(AH_ITEMR, AM_GOLD, 0.0f, cylS(g - ax * 0.06f, g + ax * 0.16f, 0.009f));
    m.addR(AH_ITEMR, AM_GOLD, 0.004f, torusS(g + ax * 0.16f, 0.018f, 0.005f, frameY(ax)));
    m.addR(AH_ITEMR, AM_GEM, 0.003f, ellS(g + ax * 0.185f, vec3(0.016f, 0.016f, 0.016f)));
    m.addR(AH_ITEMR, AM_GOLD, 0.003f, sphereS(g - ax * 0.065f, 0.013f));
  }
  // a huge head: double chin, veined dome, little eyes of disdain, a monocle and a tiny crown
  vec3 hc = j.head + vec3(0, 0.12f, 0.02f);
  m.add(AH_HEAD, AM_SKIN, 0.012f, capsuleS(j.head - vec3(0, 0.02f, 0), j.head + vec3(0, 0.05f, 0.02f), 0.05f));
  m.add(AH_HEAD, AM_SKIN, 0.0f, ellS(hc, vec3(0.125f, 0.118f, 0.115f)));
  m.add(AH_HEAD, AM_SKIN, 0.04f, ellS(hc + vec3(0, -0.09f, 0.045f), vec3(0.075f, 0.05f, 0.065f)));
  m.add(AH_JAW, AM_SKIN, 0.03f, ellS(hc + vec3(0, -0.135f, 0.05f), vec3(0.06f, 0.032f, 0.05f)));  // double chin
  for (int i = 0; i < 5; ++i) {  // veins on the dome
    float a = -0.8f + 0.4f * i;
    vec3 s0 = hc + vec3(std::sin(a) * 0.07f, 0.07f, std::cos(a) * 0.07f);
    vec3 s1 = hc + vec3(std::sin(a + 0.25f) * 0.105f, 0.0f, std::cos(a + 0.25f) * 0.08f);
    m.add(AH_HEAD, AM_SKIN2, 0.006f, capsuleS(s0, s1, 0.0045f));
  }
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    mat3 R = rotYm(0.35f * x) * rotZm(0.3f * x);
    vec3 e = hc + vec3(0.048f * x, -0.033f, 0.097f);
    m.sub(-1, 0.008f, ellS(e + vec3(0, 0, 0.005f), vec3(0.036f, 0.021f, 0.016f), R));
    m.add(AH_HEAD, AM_EYE, 0.003f, ellS(e + vec3(0, 0, 0.002f), vec3(0.034f, 0.019f, 0.014f), R));
    m.add(AH_HEAD, AM_SKIN, 0.006f, ellS(e + vec3(0, 0.016f, 0.006f), vec3(0.036f, 0.008f, 0.014f), R));  // heavy lids
  }
  m.add(AH_HEAD, AM_CHROME, 0.002f, torusS(hc + vec3(-0.046f, -0.035f, 0.114f), 0.027f, 0.0032f, frameY(vec3(-0.3f, 0, 1))));
  m.add(AH_HEAD, AM_GOLD, 0.002f, capsuleS(hc + vec3(-0.07f, -0.05f, 0.11f), hc + vec3(-0.085f, -0.12f, 0.09f), 0.0018f));
  m.paint(AM_DARK, ellS(hc + vec3(0, -0.098f, 0.102f), vec3(0.016f, 0.003f, 0.012f), rotZm(0.15f)), AM_SKIN);  // smirk
  {  // the tiny crown
    vec3 cc = hc + vec3(0, 0.112f, 0.0f);
    m.add(AH_HEAD, AM_GOLD, 0.003f, fnS([cc](const vec3& p) {
            vec3 q = p - cc;
            float r = std::sqrt(q.x * q.x + q.z * q.z);
            return std::max(std::fabs(r - 0.032f) - 0.004f, std::fabs(q.y) - 0.01f);
          }, cc, 0.04f));
    for (int i = 0; i < 5; ++i) {
      float a = 6.2831853f * i / 5.0f;
      vec3 d(std::sin(a), 0, std::cos(a));
      m.add(AH_HEAD, AM_GOLD, 0.002f, coneS(cc + d * 0.032f + vec3(0, 0.008f, 0), cc + d * 0.03f + vec3(0, 0.03f, 0), 0.006f, 0.002f));
      m.add(AH_HEAD, AM_GEM, 0.002f, sphereS(cc + d * 0.03f + vec3(0, 0.032f, 0), 0.004f));
    }
  }
  return m;
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------
Model cowModel() {
  using namespace cj;
  Model m;
  m.skel = &skeleton(AL_COW);
  m.add(CW_BODY, AM_HIDE, 0.0f, ellS(vec3(0, 0.235f, 0), vec3(0.095f, 0.085f, 0.165f)));
  m.add(CW_BODY, AM_HIDE, 0.05f, ellS(vec3(0, 0.255f, 0.1f), vec3(0.085f, 0.075f, 0.06f)));
  m.add(CW_BODY, AM_PINK, 0.02f, ellS(vec3(0, 0.155f, -0.065f), vec3(0.04f, 0.03f, 0.04f)));  // udder
  for (int i = 0; i < 4; ++i)
    m.add(CW_BODY, AM_PINK, 0.004f, capsuleS(vec3((i % 2 ? 0.014f : -0.014f), 0.13f, -0.05f - (i / 2) * 0.026f),
                                              vec3((i % 2 ? 0.014f : -0.014f), 0.11f, -0.05f - (i / 2) * 0.026f), 0.005f));
  // head
  m.add(CW_HEAD, AM_HIDE, 0.02f, capsuleS(head, head + vec3(0, 0.0f, 0.04f), 0.045f));
  m.add(CW_HEAD, AM_HIDE, 0.015f, ellS(vec3(0, 0.29f, 0.22f), vec3(0.042f, 0.05f, 0.065f), rotXm(0.5f)));
  m.add(CW_HEAD, AM_PINK, 0.015f, ellS(vec3(0, 0.255f, 0.27f), vec3(0.038f, 0.028f, 0.03f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    m.sub(AM_PINK, 0.002f, sphereS(vec3(0.013f * x, 0.258f, 0.298f), 0.006f));
    m.add(CW_HEAD, AM_EYE, 0.002f, sphereS(vec3(0.034f * x, 0.305f, 0.235f), 0.0095f));
    m.add(CW_HEAD, AM_HORN, 0.004f, coneS(vec3(0.03f * x, 0.335f, 0.19f), vec3(0.06f * x, 0.355f, 0.2f), 0.009f, 0.003f));
    m.add(CW_HEAD, AM_HIDE, 0.004f, ellS(vec3(0.055f * x, 0.315f, 0.18f), vec3(0.028f, 0.008f, 0.016f), rotZm(-0.4f * x)));
  }
  // a cowbell
  m.add(CW_HEAD, AM_RUBBER, 0.004f, torusS(vec3(0, 0.25f, 0.145f), 0.04f, 0.005f, frameY(vec3(0, 0.6f, 1))));
  m.add(CW_HEAD, AM_GOLD, 0.004f, coneS(vec3(0, 0.205f, 0.17f), vec3(0, 0.175f, 0.175f), 0.012f, 0.018f));
  // legs and hooves
  vec3 roots[4] = {fl, mx(fl), hl, mx(hl)};
  for (int i = 0; i < 4; ++i) {
    vec3 r = roots[i];
    m.add(CW_FL + i, AM_HIDE, 0.015f, coneS(r, vec3(r.x, 0.04f, r.z), 0.026f, 0.017f));
    m.add(CW_FL + i, AM_HORN, 0.004f, cylS(vec3(r.x, 0.0f, r.z), vec3(r.x, 0.035f, r.z), 0.018f));
  }
  m.add(CW_TAIL, AM_HIDE, 0.006f, capsuleS(tail, tail + vec3(0, -0.12f, -0.03f), 0.006f));
  m.add(CW_TAIL, AM_HIDE, 0.004f, ellS(tail + vec3(0, -0.14f, -0.035f), vec3(0.012f, 0.025f, 0.012f)));
  return m;
}

Model mothershipModel() {  // sculpted small; drawn about five times larger
  Model m;
  static Profile hull;
  static bool once = [] {
    hull.build({{0, -0.07f}, {0.15f, -0.065f}, {0.3f, -0.05f}, {0.42f, -0.03f}, {0.49f, -0.01f}, {0.5f, 0.0f},
                {0.49f, 0.012f}, {0.42f, 0.03f}, {0.3f, 0.06f}, {0.22f, 0.075f}, {0.18f, 0.11f}, {0.1f, 0.135f},
                {0, 0.14f}},
               3);
    return true;
  }();
  (void)once;
  vec3 c(0, 0.1f, 0);
  m.add(0, AM_HULL, 0.0f, latheS(&hull, c));
  m.paint(AM_CHROME, boxS(c - vec3(0, 0.3f, 0), vec3(0.6f, 0.3f - 0.004f, 0.6f), 0.0f), AM_HULL);
  for (int ring = 0; ring < 3; ++ring) {
    float R = 0.2f + 0.13f * ring;
    m.add(0, AM_CHROME, 0.004f, torusS(c + vec3(0, 0.06f - 0.026f * ring, 0), R, 0.007f));
    int n = 12 + 8 * ring;
    for (int i = 0; i < n; ++i) {
      float a = 6.2831853f * (i + 0.5f * ring) / n;
      m.add(0, AM_LIGHT, 0.002f, sphereS(c + vec3(std::sin(a) * R, -0.045f + 0.012f * ring, std::cos(a) * R), 0.012f));
    }
  }
  for (int i = 0; i < 6; ++i) {  // docking bays
    float a = 6.2831853f * i / 6.0f;
    vec3 d(std::sin(a), 0, std::cos(a));
    m.add(0, AM_DARK, 0.004f, boxS(c + d * 0.3f - vec3(0, 0.05f, 0), vec3(0.04f, 0.012f, 0.03f), 0.01f,
                                   mat3(glm::cross(vec3(0, 1, 0), d), vec3(0, 1, 0), d)));
  }
  m.add(0, AM_ENERGY, 0.006f, ellS(c - vec3(0, 0.07f, 0), vec3(0.11f, 0.02f, 0.11f)));
  m.add(0, AM_CHROME, 0.004f, torusS(c - vec3(0, 0.068f, 0), 0.12f, 0.01f));
  m.add(0, AM_LIGHT, 0.004f, torusS(c + vec3(0, 0.0f, 0), 0.5f, 0.008f));
  m.add(0, AM_GEM, 0.004f, sphereS(c + vec3(0, 0.15f, 0), 0.025f));
  return m;
}

Model critterModel() {
  Model m;
  m.add(0, AM_SHELL, 0.0f, ellS(vec3(0, 0.035f, -0.005f), vec3(0.032f, 0.026f, 0.042f)));
  m.add(0, AM_SKIN, 0.012f, ellS(vec3(0, 0.035f, 0.036f), vec3(0.024f, 0.02f, 0.02f)));
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    m.add(0, AM_SKIN, 0.004f, capsuleS(vec3(0.01f * x, 0.05f, 0.035f), vec3(0.016f * x, 0.072f, 0.04f), 0.0035f));
    m.add(0, AM_EYE, 0.002f, sphereS(vec3(0.016f * x, 0.076f, 0.042f), 0.0085f));  // eyes on stalks
    for (int k = 0; k < 3; ++k) {
      float z = 0.02f - 0.02f * k;
      m.add(0, AM_SHELL, 0.003f, capsuleS(vec3(0.025f * x, 0.03f, z), vec3(0.045f * x, 0.02f, z + 0.006f), 0.0035f));
      m.add(0, AM_SHELL, 0.003f, capsuleS(vec3(0.045f * x, 0.02f, z + 0.006f), vec3(0.052f * x, 0.0f, z + 0.01f), 0.003f));
    }
  }
  m.add(0, AM_DARK, 0.003f, ellS(vec3(0, 0.03f, 0.054f), vec3(0.012f, 0.005f, 0.006f)));
  for (int sd = -1; sd <= 1; sd += 2)
    m.add(0, AM_TEETH, 0.001f, coneS(vec3(0.006f * sd, 0.027f, 0.056f), vec3(0.006f * sd, 0.02f, 0.057f), 0.002f, 0.0008f));
  return m;
}

Model eggModel() {
  Model m;
  m.add(0, AM_EGG, 0.0f, fnS([](const vec3& p) {
          vec3 q = p - vec3(0, 0.1f, 0);
          float e = ellipsoid(q, vec3(0.075f, 0.1f, 0.075f) * (1.0f + 0.15f * glm::clamp(-q.y / 0.1f, 0.0f, 1.0f)));
          // four lobes at the top, ready to open
          float a = std::atan2(q.x, q.z);
          float seam = std::fabs(std::sin(a * 2.0f)) * 0.04f;
          return e + 0.006f * glm::clamp((q.y - 0.04f) / 0.05f, 0.0f, 1.0f) * (1.0f - seam / 0.04f);
        }, vec3(0, 0.1f, 0), 0.13f));
  m.add(0, AM_EGG, 0.02f, ellS(vec3(0, 0.012f, 0), vec3(0.06f, 0.014f, 0.06f)));
  return m;
}

Model gooModel() {
  Model m;
  m.add(0, AM_SLIME, 0.0f, sphereS(vec3(0, 0.05f, 0), 0.045f));
  m.add(0, AM_SLIME, 0.02f, sphereS(vec3(0.03f, 0.075f, -0.01f), 0.022f));
  m.add(0, AM_SLIME, 0.02f, sphereS(vec3(-0.025f, 0.03f, 0.03f), 0.024f));
  m.add(0, AM_SLIME, 0.015f, sphereS(vec3(0.0f, 0.095f, 0.015f), 0.014f));
  return m;
}

}  // namespace

// ===========================================================================
float pieceHeight(int kind) {
  static const float H[6] = {0.66f * PS, 0.6f * US, 0.95f, 1.08f, 1.12f, 1.16f};
  return H[std::max(0, std::min(5, kind))];
}
float pieceRadius(int kind) {
  static const float R[6] = {0.17f * PS, 0.33f * US, 0.25f, 0.36f, 0.22f, 0.29f};
  return R[std::max(0, std::min(5, kind))];
}
int glassAsset(int kind) { return kind == AL_KNIGHT ? AL_CANOPY : (kind == AL_BISHOP ? AL_JAR : -1); }
vec3 handR(int kind) {
  switch (kind) {
    case AL_PAWN: return pawnJ().gripR * PS;
    case AL_BISHOP: return bishopJ().gripR;
    case AL_QUEEN: return queenJ().gripR;
    case AL_KING: return kingJ().gripR;
    case AL_KNIGHT: return uj::hatch * US;
    default: return tj::hood + vec3(0, 0, 0.1f);
  }
}
vec3 muzzle(int kind) {
  switch (kind) {
    case AL_PAWN: return (pawnJ().gripR + vec3(0, 0.012f, 0.072f)) * PS;
    case AL_KNIGHT: return (uj::hatch - vec3(0, 0.012f, 0)) * US;
    case AL_BISHOP: return vec3(0, 0.6f, 0.06f) * BS;
    case AL_ROOK: return tj::hood + vec3(0, 0, 0.11f);
    case AL_QUEEN: return queenJ().gripR + vec3(0, 0.335f, 0);
    default: return kingJ().gripR + glm::normalize(vec3(0, 0.55f, 1)) * 0.19f;
  }
}
vec3 headPoint(int kind) {
  switch (kind) {
    case AL_PAWN: return vec3(0, 0.48f, 0.01f) * PS;
    case AL_KNIGHT: return (uj::head + vec3(0, 0.03f, 0)) * US;
    case AL_BISHOP: return vec3(0, 0.6f, -0.01f) * BS;
    case AL_ROOK: return tj::body + vec3(0, 0.05f, 0);
    case AL_QUEEN: return vec3(0, 0.86f, 0.02f);
    default: return kingJ().head + vec3(0, 0.12f, 0.02f);
  }
}
float strideCycle(int kind) {
  switch (kind) {
    case AL_PAWN: return 0.22f * PS;
    case AL_ROOK: return 0.5f;
    case AL_QUEEN: return 0.4f;
    default: return 0.4f;
  }
}
float hoverHeight(int kind) {
  switch (kind) {
    case AL_KNIGHT: return H0 * US;
    case AL_BISHOP: return bishopJ().base.y;
    case AL_KING: return kingJ().base.y;
    case AL_QUEEN: return 0.035f;
    default: return 0.0f;
  }
}

void buildLibrary(Library& lib, int quality, meshgen::BuildProgress* prog) {
  quality = std::max(0, std::min(2, quality));
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
  {
    Model m = pawnModel();
    buildAsset(lib.asset[AL_PAWN], m, vec3(-0.13f, -0.004f, -0.08f) * PS, vec3(0.13f, 0.67f, 0.16f) * PS, quality, 10, 7321u,
               pieceHeight(AL_PAWN), pieceRadius(AL_PAWN));
    tick("Cloning little green men");
  }
  {
    Model m = knightModel();
    buildAsset(lib.asset[AL_KNIGHT], m, vec3(-0.34f, H0 - 0.2f, -0.34f) * US, vec3(0.34f, H0 + 0.2f, 0.34f) * US, quality, 14,
               7421u, pieceHeight(AL_KNIGHT), pieceRadius(AL_KNIGHT));
    Model g = canopyModel();
    buildAsset(lib.asset[AL_CANOPY], g, vec3(-0.145f, H0 + 0.05f, -0.145f) * US, vec3(0.145f, H0 + 0.21f, 0.145f) * US, quality, 0,
               0u, 0.2f, 0.12f);
    tick("Polishing flying saucers");
  }
  {
    Model m = bishopModel();
    buildAsset(lib.asset[AL_BISHOP], m, vec3(-0.26f, 0.13f, -0.26f), vec3(0.26f, 0.95f, 0.26f), quality, 12, 7521u,
               pieceHeight(AL_BISHOP), pieceRadius(AL_BISHOP));
    Model g = jarModel();
    buildAsset(lib.asset[AL_JAR], g, vec3(-0.215f, 0.57f, -0.225f), vec3(0.215f, 0.9f, 0.205f), quality, 0, 0u, 0.3f,
               0.2f);
    tick("Growing big brains");
  }
  {
    Model m = rookModel();
    buildAsset(lib.asset[AL_ROOK], m, vec3(-0.4f, -0.005f, -0.4f), vec3(0.4f, 1.1f, 0.4f), quality, 14, 7621u,
               pieceHeight(AL_ROOK), pieceRadius(AL_ROOK));
    tick("Assembling tripods");
  }
  {
    Model m = queenModel();
    buildAsset(lib.asset[AL_QUEEN], m, vec3(-0.22f, -0.005f, -0.53f), vec3(0.22f, 1.2f, 0.22f), quality, 13, 7721u,
               pieceHeight(AL_QUEEN), pieceRadius(AL_QUEEN));
    tick("Crowning the queens");
  }
  {
    Model m = kingModel();
    buildAsset(lib.asset[AL_KING], m, vec3(-0.27f, 0.08f, -0.33f), vec3(0.27f, 1.2f, 0.34f), quality, 14, 7821u,
               pieceHeight(AL_KING), pieceRadius(AL_KING));
    tick("Enthroning the overlords");
  }
  {
    Model m = cowModel();
    buildAsset(lib.asset[AL_COW], m, vec3(-0.1f, -0.005f, -0.27f), vec3(0.1f, 0.38f, 0.33f), quality, 0, 0u, 0.38f,
               0.2f);
    Model e = eggModel();
    buildAsset(lib.asset[AL_EGG], e, vec3(-0.1f, -0.005f, -0.1f), vec3(0.1f, 0.21f, 0.1f), quality, 6, 7921u, 0.2f,
               0.09f);
    Model g = gooModel();
    buildAsset(lib.asset[AL_GOO], g, vec3(-0.07f, -0.005f, -0.07f), vec3(0.07f, 0.12f, 0.07f), quality, 0, 0u, 0.1f,
               0.05f);
    Model c = critterModel();
    buildAsset(lib.asset[AL_CRITTER], c, vec3(-0.07f, -0.005f, -0.06f), vec3(0.07f, 0.09f, 0.07f), quality, 0, 0u,
               0.08f, 0.05f);
    tick("Stealing cows");
  }
  {
    Model m = mothershipModel();
    buildAsset(lib.asset[AL_MOTHERSHIP], m, vec3(-0.52f, 0.0f, -0.52f), vec3(0.52f, 0.26f, 0.52f), quality, 0, 0u,
               0.26f, 0.5f);
    tick("Summoning the mothership");
  }
  lib.built = true;
  if (prog) prog->finished = true;
}

}  // namespace alien
