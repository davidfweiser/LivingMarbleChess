// Turning animation controls into bone matrices: walk cycles, standing up
// from a throne, a horse's gaits, the gatehouse's legs and drawbridge, and
// the dragon's neck, wings and tail.
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "medieval.h"

namespace medieval {
using glm::mat4;
using glm::quat;
using glm::vec3;

namespace {

const float PI = 3.14159265f;
const vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
inline quat aa(float a, const vec3& axis) { return glm::angleAxis(a, axis); }
// pitch a limb that hangs down: + swings it forward (+z)
inline quat swing(float a) { return aa(-a, X); }
// bend a knee: + folds the shin backwards
inline quat knee(float a) { return aa(a, X); }
inline float smooth(float t) {
  t = glm::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

struct Builder {
  const Skeleton& s;
  quat R[kMaxBones];
  vec3 off[kMaxBones];
  float sc[kMaxBones];
  explicit Builder(const Skeleton& sk) : s(sk) {
    for (int i = 0; i < kMaxBones; ++i) {
      R[i] = quat(1, 0, 0, 0);
      off[i] = vec3(0);
      sc[i] = 1;
    }
  }
  // rotate bone b about an arbitrary pivot (in its parent's space) instead of its joint
  void rotateAbout(int b, const quat& q, const vec3& pivot) {
    vec3 j = s.joint[b];
    R[b] = q * R[b];
    off[b] += (pivot - j) - q * (pivot - j);
  }
  void finish(Pose& out) const {
    out.count = s.count;
    for (int b = 0; b < s.count; ++b) {
      const vec3& j = s.joint[b];
      mat4 L = glm::translate(mat4(1.0f), j + off[b]) * glm::mat4_cast(R[b]) * glm::scale(mat4(1.0f), vec3(sc[b])) *
               glm::translate(mat4(1.0f), -j);
      out.bone[b] = s.parent[b] >= 0 ? out.bone[s.parent[b]] * L : L;
    }
  }
};

// Men-at-arms walk; kings, queens and bishops can also get up from their thrones.
void humanPose(int asset, const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(asset);
  Builder b(sk);
  float breath = std::sin(c.idle * 1.7f);
  // --- standing up: thighs swing down, the shins straighten, the hips rise over the feet
  float up = throned(asset) ? 1.0f - glm::clamp(c.sit, 0.0f, 1.0f) : 0.0f;
  float rise = smooth(up);
  if (up > 0) {
    // the ankles sit under the knees in the seated pose: the hips end up over them
    vec3 hip = sk.joint[HB_LTHIGH], kn = sk.joint[HB_LSHIN];
    float thigh = glm::length(kn - hip);
    b.off[HB_ROOT] += vec3(0, thigh * rise, (kn.z - hip.z) * smooth(up * 1.25f));
  }
  // --- walk cycle: legs swing, knees fold on the forward swing, arms counter-swing
  float st = c.stride;
  float sL = std::sin(c.gait), cL = std::cos(c.gait);
  float hipSwing = 0.42f * st;
  float sitThigh = PI * 0.5f * rise, sitShin = -PI * 0.5f * rise;
  b.R[HB_LTHIGH] = aa(sitThigh, X) * swing(hipSwing * sL + c.crouch * 0.6f);
  b.R[HB_RTHIGH] = aa(sitThigh, X) * swing(-hipSwing * sL + c.crouch * 0.6f);
  b.R[HB_LSHIN] = aa(sitShin, X) * knee(st * (0.75f * std::max(0.0f, cL) + 0.12f) + c.crouch * 1.1f);
  b.R[HB_RSHIN] = aa(sitShin, X) * knee(st * (0.75f * std::max(0.0f, -cL) + 0.12f) + c.crouch * 1.1f);
  float bob = st * 0.012f * std::fabs(std::cos(c.gait)) - st * 0.01f - c.crouch * 0.05f * (1.0f + 0.6f * rise);
  b.off[HB_ROOT] += vec3(0, bob, 0);
  b.R[HB_ROOT] = aa(0.06f * st * sL, Y) * aa(0.03f * st * cL, Z);
  // --- torso: leans forward while getting up
  float getUpLean = 0.45f * std::sin(PI * up) * (throned(asset) ? 1.0f : 0.0f);
  b.R[HB_CHEST] = aa(c.twist, Y) * aa(c.lean + getUpLean + 0.012f * breath, X) * aa(-0.05f * st * sL, Y);
  b.R[HB_HEAD] = aa(c.headYaw, Y) * aa(c.headPitch - 0.3f * getUpLean, X);
  // --- arms: controls are offsets from the sculpted pose
  float armSwing = 0.3f * st * sL;
  b.R[HB_LARM] = aa(c.armL.y, Z) * swing(c.armL.x - armSwing);
  b.R[HB_RARM] = aa(-c.armR.y, Z) * swing(c.armR.x + armSwing);
  b.R[HB_LFORE] = swing(c.armL.z);
  b.R[HB_RFORE] = swing(c.armR.z);
  b.R[HB_ITEMR] = aa(c.itemPitch, X) * aa(c.itemYaw, Y);
  b.sc[HB_ITEMR] = c.itemR > 0.5f ? 1.0f : 0.0f;
  b.sc[HB_ITEML] = c.itemL > 0.5f ? 1.0f : 0.0f;
  b.finish(out);
}

// The horse trots (diagonal pairs) and gallops, rears, jumps and bucks; the rider
// leans, swings his arms and lowers his lance.
void horsePose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(A_KNIGHT);
  Builder b(sk);
  float st = c.stride, run = glm::clamp(c.run, 0.0f, 1.0f);
  float g = c.gait;
  // phase of each leg: trot pairs diagonals, the gallop staggers all four
  const float trot[4] = {0.0f, PI, PI, 0.0f};          // FL FR HL HR
  const float gallop[4] = {0.0f, 0.55f, 2.6f, 3.15f};
  const int upper[4] = {KB_FLU, KB_FRU, KB_HLU, KB_HRU};
  const int lower[4] = {KB_FLL, KB_FRL, KB_HLL, KB_HRL};
  for (int i = 0; i < 4; ++i) {
    float ph = g + glm::mix(trot[i], gallop[i], run);
    float s = std::sin(ph), co = std::cos(ph);
    bool front = i < 2;
    float amp = (0.42f + 0.18f * run) * st;
    float lift = std::max(0.0f, co);  // swinging forward: the leg is in the air
    if (front) {
      b.R[upper[i]] = swing(amp * s + 0.12f * lift * st);
      b.R[lower[i]] = knee((0.55f + 0.35f * run) * lift * lift * st);
    } else {
      b.R[upper[i]] = swing(amp * s * 0.9f - 0.05f * st);
      b.R[lower[i]] = knee(-(0.45f + 0.3f * run) * lift * lift * st);
    }
  }
  float bodyPitch = 0.05f * st * (0.3f + run) * std::sin(g * (run > 0.5f ? 1.0f : 2.0f));
  // the legs are never all straight while moving: the body rides a little lower
  float bob = 0.01f * st * (1.0f + run) * std::fabs(std::sin(g)) - 0.022f * st * (0.5f + run);
  b.off[KB_BODY] = vec3(0, bob, 0);
  b.R[KB_BODY] = aa(bodyPitch, X);
  b.R[KB_NECK] = aa(0.08f * st * std::sin(g + 0.8f) + c.neck * 0.5f, X);
  b.R[KB_HEAD] = aa(0.05f * std::sin(c.idle * 0.9f) + c.headPitch, X) * aa(c.headYaw, Y);
  b.R[KB_TAIL] = aa(0.25f * std::sin(c.idle * 1.3f + c.tail), Y) * aa(-0.35f * run * st, X);

  // rearing up on the hind legs: the body pivots about the hind hooves' line
  float rearA = 0;
  if (c.rear > 0.001f) {
    // the body pivots about the hind hips; the hind legs stay upright, so the hooves stay planted
    float a = 0.95f * smooth(c.rear);
    rearA = a;
    vec3 pivot(0, sk.joint[KB_HLU].y, sk.joint[KB_HLU].z);
    b.rotateAbout(KB_BODY, aa(-a, X), pivot);
    for (int i = 2; i < 4; ++i) b.R[upper[i]] = aa(a, X) * swing(0.25f * smooth(c.rear)) * b.R[upper[i]];
    b.R[KB_HLL] = knee(-0.3f * smooth(c.rear)) * b.R[KB_HLL];
    b.R[KB_HRL] = knee(-0.3f * smooth(c.rear)) * b.R[KB_HRL];
    // forelegs paw the air
    float paw = std::sin(c.idle * 9.0f);
    b.R[KB_FLU] = swing(1.2f * smooth(c.rear) + 0.3f * paw) * b.R[KB_FLU];
    b.R[KB_FRU] = swing(1.2f * smooth(c.rear) - 0.3f * paw) * b.R[KB_FRU];
    b.R[KB_FLL] = knee(1.6f * smooth(c.rear));
    b.R[KB_FRL] = knee(1.6f * smooth(c.rear));
    b.R[KB_NECK] = aa(-0.3f * smooth(c.rear), X) * b.R[KB_NECK];
  }
  // folding the legs for a jump
  if (c.tuck > 0.001f) {
    float t = smooth(c.tuck);
    b.R[KB_FLU] = swing(1.1f * t) * b.R[KB_FLU];
    b.R[KB_FRU] = swing(1.0f * t) * b.R[KB_FRU];
    b.R[KB_FLL] = knee(1.9f * t) * b.R[KB_FLL];
    b.R[KB_FRL] = knee(1.8f * t) * b.R[KB_FRL];
    b.R[KB_HLU] = swing(-0.9f * t) * b.R[KB_HLU];
    b.R[KB_HRU] = swing(-0.85f * t) * b.R[KB_HRU];
    b.R[KB_HLL] = knee(-0.4f * t) * b.R[KB_HLL];
    b.R[KB_HRL] = knee(-0.4f * t) * b.R[KB_HRL];
  }
  // bucking: weight on the forelegs, hind legs lashing out backwards
  if (c.kick > 0.001f) {
    float t = smooth(c.kick);
    vec3 pivot = vec3(0, sk.joint[KB_FLU].y, sk.joint[KB_FLU].z);
    b.rotateAbout(KB_BODY, aa(0.45f * t, X), pivot);
    rearA = -0.45f * t;
    b.R[KB_FLU] = aa(-0.45f * t, X) * b.R[KB_FLU];
    b.R[KB_FRU] = aa(-0.45f * t, X) * b.R[KB_FRU];
    b.R[KB_HLU] = swing(-1.4f * t) * b.R[KB_HLU];
    b.R[KB_HRU] = swing(-1.3f * t) * b.R[KB_HRU];
    b.R[KB_HLL] = knee(0.3f * t);
    b.R[KB_HRL] = knee(0.3f * t);
  }
  // the rider stays upright as the horse rears or bucks (and his lance with him)
  b.R[KB_RIDER] = aa(c.twist, Y) * aa(c.lean - bodyPitch * 0.6f + rearA * 0.75f + 0.01f * std::sin(c.idle * 1.6f), X);
  b.R[KB_RHEAD] = aa(c.headYaw * 0.5f, Y);
  b.R[KB_LARM] = aa(c.armL.y, Z) * swing(c.armL.x);
  b.R[KB_RARM] = aa(-c.armR.y, Z) * swing(c.armR.x);
  b.R[KB_LFORE] = swing(c.armL.z);
  b.R[KB_RFORE] = swing(c.armR.z);
  b.R[KB_LANCE] = aa(c.itemPitch, X);
  b.sc[KB_LANCE] = c.itemR > 0.5f ? 1.0f : 0.0f;
  b.finish(out);
}

// The gatehouse: stone legs, drawbridge, portcullis and its banner.
void castlePose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(A_ROOK);
  Builder b(sk);
  const float legH = 0.42f;
  float L = glm::clamp(c.legs, 0.0f, 1.0f);
  float st = c.stride;
  float sL = std::sin(c.gait), cL = std::cos(c.gait);
  // the tower rises on its legs; while they are in, they hide inside it
  float bob = st * 0.025f * std::fabs(cL) - c.crouch * 0.08f;
  b.off[CB_BODY] = vec3(0, legH * smooth(L) + bob * L, 0);
  b.R[CB_BODY] = aa(0.06f * st * sL, Z) * aa(c.lean, X) * aa(0.05f * st * sL, Y);
  for (int side = 0; side < 2; ++side) {
    int th = side == 0 ? CB_LTHIGH : CB_RTHIGH, sh = side == 0 ? CB_LSHIN : CB_RSHIN;
    float s = side == 0 ? sL : -sL, co = side == 0 ? cL : -cL;
    b.off[th] = vec3(0, legH * (1.0f - smooth(L)), 0);
    float lift = 0;
    if ((c.lift > 0 && side == 1) || (c.lift < 0 && side == 0)) lift = std::fabs(c.lift);
    // counter the body's sway so the feet stay flat
    b.R[th] = swing(0.55f * st * s + 0.9f * lift - c.crouch * 0.5f);
    b.R[sh] = knee(st * 0.8f * std::max(0.0f, co) + 1.1f * lift + c.crouch * 0.9f);
  }
  // drawbridge swings down from its hinge, portcullis slides up
  b.R[CB_BRIDGE] = aa(PI * 0.5f * smooth(c.bridge), X);
  b.off[CB_GATE] = vec3(0, 0.3f * smooth(c.gate), 0);
  b.R[CB_FLAG] = aa(0.25f * std::sin(c.idle * 1.3f) + 0.1f * std::sin(c.idle * 3.7f), Y);
  b.finish(out);
}

// The dragon walks, rears its neck back, lunges, opens its jaws, spreads and beats its wings.
void dragonPose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(A_DRAGON);
  Builder b(sk);
  float st = c.stride, g = c.gait;
  float breath = std::sin(c.idle * 1.4f);
  // walk: diagonal legs together, the body rolls a little
  b.R[DB_FL] = swing(0.5f * st * std::sin(g));
  b.R[DB_HR] = swing(0.5f * st * std::sin(g));
  b.R[DB_FR] = swing(-0.5f * st * std::sin(g));
  b.R[DB_HL] = swing(-0.5f * st * std::sin(g));
  b.off[DB_BODY] = vec3(0, 0.015f * st * std::fabs(std::cos(g)) + 0.005f * breath, 0);
  b.R[DB_BODY] = aa(0.05f * st * std::sin(g), Z) * aa(-0.12f * c.neck + c.lean, X);
  // neck: + rears back (S-curve), - lunges forward and down
  float n = c.neck;
  float sway = 0.08f * std::sin(c.idle * 0.8f);
  b.R[DB_NECK1] = aa(-0.35f * std::max(0.0f, n) + 0.25f * std::max(0.0f, -n) + c.headYaw * 0.3f, X) * aa(sway, Y);
  b.R[DB_NECK2] = aa(-0.3f * std::max(0.0f, n) + 0.2f * std::max(0.0f, -n), X) * aa(c.headYaw * 0.3f + sway, Y);
  b.R[DB_NECK3] = aa(0.25f * std::max(0.0f, n) + 0.1f * std::max(0.0f, -n), X) * aa(c.headYaw * 0.3f, Y);
  b.R[DB_HEAD] = aa(c.headPitch + 0.4f * std::max(0.0f, n) - 0.15f * std::max(0.0f, -n), X);
  b.R[DB_JAW] = aa(0.75f * glm::clamp(c.jaw, 0.0f, 1.0f), X);
  // tail: a travelling wave
  float ts = c.tail + c.idle * 1.1f;
  b.R[DB_TAIL1] = aa(0.18f * std::sin(ts), Y) * aa(-0.05f, X);
  b.R[DB_TAIL2] = aa(0.24f * std::sin(ts - 0.8f), Y);
  b.R[DB_TAIL3] = aa(0.3f * std::sin(ts - 1.6f), Y) * aa(0.08f, X);
  b.R[DB_TAIL4] = aa(0.36f * std::sin(ts - 2.4f), Y);
  // wings: folded along the flanks (0) or spread and beating (1)
  float w = glm::clamp(c.wings, 0.0f, 1.0f);
  float beat = w * 0.45f * std::sin(c.flap);
  for (int side = 0; side < 2; ++side) {
    float sd = side == 0 ? 1.0f : -1.0f;
    int w1 = side == 0 ? DB_LWING1 : DB_RWING1, w2 = side == 0 ? DB_LWING2 : DB_RWING2;
    float fold = 1.0f - w;
    // folding swings the arm back and down along the body, the fingers close onto the arm
    b.R[w1] = aa(sd * (0.75f * fold + beat - 0.25f * w), Z) * aa(sd * 0.9f * fold, Y);
    b.R[w2] = aa(sd * -1.6f * fold, Y) * aa(sd * 0.5f * fold - sd * 0.3f * beat, Z);
  }
  b.finish(out);
}

}  // namespace

void buildPose(int asset, const Controls& c, Pose& out) {
  switch (asset) {
    case A_PAWN:
    case A_BISHOP:
    case A_QUEEN:
    case A_KING: humanPose(asset, c, out); break;
    case A_KNIGHT: horsePose(c, out); break;
    case A_ROOK:
    case A_LEGS: castlePose(c, out); break;
    case A_DRAGON: dragonPose(c, out); break;
    default: out.count = 0; break;
  }
}

mat4 boneMatrix(const Pose& p, int bone) { return bone >= 0 && bone < p.count ? p.bone[bone] : mat4(1.0f); }

}  // namespace medieval
