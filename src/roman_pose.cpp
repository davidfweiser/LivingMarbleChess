// Turning animation controls into bone matrices for the Romans: soldiers
// marching, fighting, kicking, tucking into a somersault and falling limp,
// their cloaks swinging; the chariot's horses and wheels and its charioteer;
// the siege tower's wheels, ram, bridge, door and scorpion; the lion.
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "pose_kit.h"
#include "roman.h"

namespace roman {
using glm::mat4;
using glm::quat;
using glm::vec3;

namespace {
using namespace posekit;

void humanPose(int asset, const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(asset);
  Builder b(sk);
  float breath = std::sin(c.idle * 1.7f) * (1.0f - glm::clamp(c.dead, 0.0f, 1.0f));
  float st = c.stride;
  float sL = std::sin(c.gait), cL = std::cos(c.gait);
  float hipSwing = 0.42f * st;
  float dead = glm::clamp(c.dead, 0.0f, 1.0f);
  float tuck = smooth(c.tuck);
  float kick = glm::clamp(c.kick, 0.0f, 1.0f);
  // --- legs: the walk cycle, a crouch, a front kick, knees to the chest, limp
  b.R[RH_LTHIGH] = aa(dead * 0.14f, Z) * swing(hipSwing * sL + c.crouch * 0.6f + tuck * 1.75f + dead * 0.3f);
  b.R[RH_RTHIGH] = aa(-dead * 0.1f, Z) * swing(-hipSwing * sL + c.crouch * 0.6f + tuck * 1.75f + kick * 1.4f + dead * 0.1f);
  b.R[RH_LSHIN] = knee(st * (0.75f * std::max(0.0f, cL) + 0.12f) + c.crouch * 1.1f + tuck * 2.1f + dead * 0.45f);
  b.R[RH_RSHIN] =
      knee(st * (0.75f * std::max(0.0f, -cL) + 0.12f) + c.crouch * 1.1f + tuck * 2.1f + dead * 0.2f + 1.5f * kick * (1.0f - kick));
  float bob = st * 0.012f * std::fabs(std::cos(c.gait)) - st * 0.01f - c.crouch * 0.05f;
  b.off[RH_ROOT] += vec3(0, bob, 0);
  b.R[RH_ROOT] = aa(0.06f * st * sL, Y) * aa(0.03f * st * cL, Z);
  // --- torso and head
  float chestPitch = c.lean + 0.012f * breath + tuck * 0.5f - kick * 0.2f;
  b.R[RH_CHEST] = aa(c.twist, Y) * aa(chestPitch, X) * aa(-0.05f * st * sL, Y);
  b.R[RH_HEAD] = aa(c.headYaw, Y) * aa(c.headPitch - dead * 0.4f + tuck * 0.3f, X) * aa(dead * 0.3f, Z);
  // --- arms: offsets from the sculpted pose; tucked in for a somersault, flung out when limp
  float armSwing = 0.3f * st * sL;
  // (limp: the arms fall out to the sides, straight - lying on the board when the body is down)
  vec3 aL = glm::mix(c.armL + tuck * vec3(0.7f, 0.1f, 0.9f), vec3(0.1f, 1.25f, -1.3f), dead);
  vec3 aR = glm::mix(c.armR + tuck * vec3(0.7f, 0.1f, 0.9f), vec3(0.15f, 1.1f, -1.2f), dead);
  b.R[RH_LARM] = aa(aL.y, Z) * swing(aL.x - armSwing);
  b.R[RH_RARM] = aa(-aR.y, Z) * swing(aR.x + armSwing);
  b.R[RH_LFORE] = swing(aL.z);
  b.R[RH_RFORE] = swing(aR.z);
  // --- what the hands hold: the right hand's item, or the blade drawn instead of it
  quat ip = aa(c.itemPitch, X) * aa(c.itemYaw, Y);
  b.R[RH_ITEMR] = ip;
  b.R[RH_BLADE] = ip;
  bool drawn = c.draw > 0.5f;
  b.sc[RH_ITEMR] = c.itemR > 0.5f && !drawn ? 1.0f : 0.0f;
  b.sc[RH_BLADE] = c.itemR > 0.5f && drawn ? 1.0f : 0.0f;
  b.sc[RH_SHEATH] = drawn ? 0.0f : 1.0f;
  bool left = c.itemL > 0.5f && (asset != R_QUEEN || c.hatch > 0.5f);  // the empress's goblet only when she offers it
  b.sc[RH_ITEML] = left ? 1.0f : 0.0f;
  // --- the cloak hangs down whichever way the body leans, swings with the stride and streams out
  float hang = std::max(0.0f, -chestPitch);
  float wind = glm::clamp(c.cape + 0.25f * st, 0.0f, 1.2f);
  float alive = 1.0f - dead;
  b.R[RH_CLOAK] = aa((hang * 0.9f + 0.06f * st + 0.55f * wind + 0.02f * std::sin(c.idle * 1.3f)) * alive - 0.12f * dead, X) *
                  aa(0.05f * st * sL, Z);
  b.R[RH_HEM] = aa((0.1f * st * std::fabs(sL) + 0.5f * wind + 0.08f * wind * std::sin(c.idle * 11.0f) +
                    0.03f * std::sin(c.idle * 1.1f + 1.0f) + 0.3f * tuck) * alive - 0.1f * dead,
                   X);
  b.finish(out);
}

// Legs of a four-legged animal for a walk / trot / gallop phase.
struct Quad {
  int upper[4], lower[4];  // FL FR HL HR
};
void quadLegs(Builder& b, const Quad& q, const Controls& c, float frontKnee, float hindKnee) {
  const float trot[4] = {0.0f, PI, PI, 0.0f};
  const float gallop[4] = {0.0f, 0.55f, 2.6f, 3.15f};
  float st = c.stride, run = glm::clamp(c.run, 0.0f, 1.0f);
  for (int i = 0; i < 4; ++i) {
    float ph = c.gait + glm::mix(trot[i], gallop[i], run);
    float s = std::sin(ph), co = std::cos(ph);
    bool front = i < 2;
    float amp = (0.42f + 0.18f * run) * st;
    float lift = std::max(0.0f, co);
    if (front) {
      b.R[q.upper[i]] = swing(amp * s + 0.12f * lift * st);
      b.R[q.lower[i]] = knee(frontKnee * (0.55f + 0.35f * run) * lift * lift * st);
    } else {
      b.R[q.upper[i]] = swing(amp * s * 0.9f - 0.05f * st);
      b.R[q.lower[i]] = knee(-hindKnee * (0.45f + 0.3f * run) * lift * lift * st);
    }
  }
}

// The chariot: the horses walk, gallop, rear and leap; the wheels roll; the
// charioteer leans into the gallop, swings his spear - or leaps out of the car.
void chariotPose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(R_KNIGHT);
  Builder b(sk);
  float st = c.stride, run = glm::clamp(c.run, 0.0f, 1.0f), g = c.gait;
  Quad q = {{CH_FLU, CH_FRU, CH_HLU, CH_HRU}, {CH_FLL, CH_FRL, CH_HLL, CH_HRL}};
  quadLegs(b, q, c, 1.0f, 1.0f);
  float bodyPitch = 0.05f * st * (0.3f + run) * std::sin(g * (run > 0.5f ? 1.0f : 2.0f));
  float bob = 0.008f * st * (1.0f + run) * std::fabs(std::sin(g)) - 0.014f * st * (0.5f + run);
  b.off[CH_HORSES] = vec3(0, bob, 0);
  b.R[CH_HORSES] = aa(bodyPitch, X);
  b.R[CH_NECK] = aa(0.08f * st * std::sin(g + 0.8f) + c.neck * 0.5f, X);
  b.R[CH_HEAD] = aa(0.05f * std::sin(c.idle * 0.9f) + c.headPitch, X);
  b.R[CH_TAIL] = aa(0.25f * std::sin(c.idle * 1.3f + c.tail), Y) * aa(-0.35f * run * st, X);
  float rearA = 0;
  if (c.rear > 0.001f) {  // the horses rear: their bodies pivot about the hind hips
    float a = 0.85f * smooth(c.rear);
    rearA = a;
    vec3 pivot(0, sk.joint[CH_HLU].y, sk.joint[CH_HLU].z);
    b.rotateAbout(CH_HORSES, aa(-a, X), pivot);
    for (int i = 2; i < 4; ++i) b.R[q.upper[i]] = aa(a, X) * swing(0.25f * smooth(c.rear)) * b.R[q.upper[i]];
    float paw = std::sin(c.idle * 9.0f);
    b.R[CH_FLU] = swing(1.2f * smooth(c.rear) + 0.3f * paw) * b.R[CH_FLU];
    b.R[CH_FRU] = swing(1.2f * smooth(c.rear) - 0.3f * paw) * b.R[CH_FRU];
    b.R[CH_FLL] = knee(1.6f * smooth(c.rear));
    b.R[CH_FRL] = knee(1.6f * smooth(c.rear));
    b.R[CH_NECK] = aa(-0.3f * smooth(c.rear), X) * b.R[CH_NECK];
  }
  if (c.tuck > 0.001f) {  // legs folded for a leap
    float t = smooth(c.tuck);
    b.R[CH_FLU] = swing(1.1f * t) * b.R[CH_FLU];
    b.R[CH_FRU] = swing(1.0f * t) * b.R[CH_FRU];
    b.R[CH_FLL] = knee(1.9f * t) * b.R[CH_FLL];
    b.R[CH_FRL] = knee(1.8f * t) * b.R[CH_FRL];
    b.R[CH_HLU] = swing(-0.9f * t) * b.R[CH_HLU];
    b.R[CH_HRU] = swing(-0.85f * t) * b.R[CH_HRU];
  }
  // the car jolts along; the wheels roll
  b.off[CH_CAR] = vec3(0, 0.005f * st * run * std::fabs(std::sin(g * 2.0f)), 0);
  b.R[CH_CAR] = aa(0.015f * st * run * std::sin(g * 2.0f + 1.0f), Z);
  b.R[CH_WHEELL] = aa(c.spin, X);
  b.R[CH_WHEELR] = aa(c.spin, X);
  // the charioteer
  b.off[CH_DRIVER] = c.rider;
  b.R[CH_DRIVER] = aa(c.riderSpin, X) * aa(c.twist, Y) *
                   aa(c.lean + 0.18f * run * st - 0.3f * rearA + 0.01f * std::sin(c.idle * 1.6f), X);
  b.R[CH_DHEAD] = aa(c.headYaw * 0.8f, Y);
  b.R[CH_DLARM] = aa(c.armL.y, Z) * swing(c.armL.x + 0.2f * run * st);
  b.R[CH_DRARM] = aa(-c.armR.y, Z) * swing(c.armR.x);
  b.R[CH_DLFORE] = swing(c.armL.z);
  b.R[CH_DRFORE] = swing(c.armR.z);
  b.R[CH_ITEM] = aa(c.itemPitch, X);
  b.sc[CH_ITEM] = c.itemR > 0.5f ? 1.0f : 0.0f;
  float wind = glm::clamp(c.cape + st * run, 0.0f, 1.2f);
  b.R[CH_CLOAK] = aa(0.05f + 0.75f * wind + 0.1f * wind * std::sin(c.idle * 12.0f), X);
  b.finish(out);
}

// The siege tower rolls and rocks; its ram swings out, its bridge and door drop,
// its scorpion turns, aims and shoots.
void towerPose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(R_ROOK);
  Builder b(sk);
  float st = c.stride;
  b.off[TW_BODY] = vec3(0, 0.004f * st * std::fabs(std::sin(c.gait * 2.0f)), 0);
  b.R[TW_BODY] = aa(c.lean, X) * aa(0.014f * st * std::sin(c.gait * 2.0f) + c.twist * 0.0f, Z);
  for (int w = TW_WHEEL1; w <= TW_WHEEL4; ++w) b.R[w] = aa(c.spin, X);
  float k = c.kick;
  b.off[TW_RAM] = vec3(0, 0.035f * k * k, 0.32f * k);
  b.R[TW_RAM] = aa(-0.08f * k, X);
  b.R[TW_BRIDGE] = aa(PI * 0.5f * smooth(c.bridge), X);
  b.R[TW_DOOR] = aa(PI * 0.52f * smooth(c.gate), X);
  b.R[TW_SCORPION] = aa(c.itemYaw, Y) * aa(c.itemPitch, X);
  b.sc[TW_LOADED] = c.hatch > 0.5f ? 0.0f : 1.0f;
  b.R[TW_FLAG] = aa(0.25f * std::sin(c.idle * 1.3f) + 0.1f * std::sin(c.idle * 3.7f), Y);
  b.finish(out);
}

// The lion prowls, bounds, rears up to pounce and roars.
void lionPose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(R_LION);
  Builder b(sk);
  Quad q = {{LN_FLU, LN_FRU, LN_HLU, LN_HRU}, {LN_FLL, LN_FRL, LN_HLL, LN_HRL}};
  quadLegs(b, q, c, 1.1f, 1.0f);
  float st = c.stride, run = glm::clamp(c.run, 0.0f, 1.0f), g = c.gait;
  float flex = 0.08f * run * st * std::sin(g);  // the spine flexes in the bound
  b.off[LN_BODY] = vec3(0, 0.012f * st * (1.0f + run) * std::fabs(std::sin(g)) - 0.05f * c.crouch, 0);
  b.R[LN_BODY] = aa(flex + c.lean, X);
  b.R[LN_NECK] = aa(-flex * 0.8f + 0.1f * c.crouch, X);
  b.R[LN_HEAD] = aa(c.headYaw, Y) * aa(c.headPitch + 0.04f * std::sin(c.idle * 0.8f), X);
  b.R[LN_JAW] = aa(0.75f * glm::clamp(c.jaw, 0.0f, 1.0f), X);
  float ts = c.tail + c.idle * 1.2f;
  b.R[LN_TAIL1] = aa(0.3f * std::sin(ts), Y) * aa(0.4f - 0.5f * run * st, X);
  b.R[LN_TAIL2] = aa(0.4f * std::sin(ts - 1.0f), Y) * aa(-0.3f, X);
  for (int i = 0; i < 4; ++i) {  // crouching: the legs fold under it
    if (c.crouch <= 0.001f) break;
    bool front = i < 2;
    b.R[q.upper[i]] = swing((front ? -0.5f : 0.6f) * c.crouch) * b.R[q.upper[i]];
    b.R[q.lower[i]] = knee((front ? 0.9f : -0.9f) * c.crouch) * b.R[q.lower[i]];
  }
  if (c.rear > 0.001f) {  // rearing to pounce: the forepaws reach up and out, claws first
    float a = 0.9f * smooth(c.rear);
    vec3 pivot(0, sk.joint[LN_HLU].y, sk.joint[LN_HLU].z);
    b.rotateAbout(LN_BODY, aa(-a, X), pivot);
    b.R[LN_HLU] = aa(a, X) * b.R[LN_HLU];
    b.R[LN_HRU] = aa(a, X) * b.R[LN_HRU];
    b.R[LN_FLU] = swing(1.5f * smooth(c.rear)) * b.R[LN_FLU];
    b.R[LN_FRU] = swing(1.4f * smooth(c.rear)) * b.R[LN_FRU];
    b.R[LN_FLL] = knee(-0.3f * smooth(c.rear));
    b.R[LN_FRL] = knee(-0.3f * smooth(c.rear));
    b.R[LN_NECK] = aa(0.35f * smooth(c.rear), X) * b.R[LN_NECK];
  }
  if (c.tuck > 0.001f) {  // stretched out in a leap
    float t = smooth(c.tuck);
    b.R[LN_FLU] = swing(1.2f * t) * b.R[LN_FLU];
    b.R[LN_FRU] = swing(1.1f * t) * b.R[LN_FRU];
    b.R[LN_HLU] = swing(-1.0f * t) * b.R[LN_HLU];
    b.R[LN_HRU] = swing(-1.0f * t) * b.R[LN_HRU];
  }
  b.finish(out);
}

}  // namespace

void buildPose(int asset, const Controls& c, Pose& out) {
  switch (asset) {
    case R_PAWN:
    case R_BISHOP:
    case R_QUEEN:
    case R_KING: humanPose(asset, c, out); break;
    case R_KNIGHT: chariotPose(c, out); break;
    case R_ROOK: towerPose(c, out); break;
    case R_LION: lionPose(c, out); break;
    default: out.count = 0; break;
  }
}

}  // namespace roman
