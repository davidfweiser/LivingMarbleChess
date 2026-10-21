// Posing the aliens: the little green men waddle with bobbing antennae and
// shoot their tongues out, the saucer wobbles and spins its light ring, the
// psychic and the overlord float, the queen glides and swishes her tail, the
// tripod walks on three legs (inverse kinematics keeps its feet planted) and
// stands up tall, and the cow kicks its legs as it falls.
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "alien.h"
#include "pose_kit.h"

namespace alien {
using glm::mat4;
using glm::quat;
using glm::vec3;

namespace {
using namespace posekit;

// antennae: bob with the walk, perk up (+) or droop outwards (-)
void antennae(Builder& b, const Controls& c, int L, int R, float bounce) {
  float droop = std::max(0.0f, -c.perk) * 1.1f;
  float alert = std::max(0.0f, c.perk) * 0.35f;
  for (int side = 0; side < 2; ++side) {
    float sd = side == 0 ? 1.0f : -1.0f;
    float sway = 0.1f * std::sin(c.idle * 2.3f + side * 1.7f) + 0.06f * std::sin(c.idle * 5.1f + side);
    b.R[side == 0 ? L : R] = aa(-sd * droop + sd * 0.25f * alert, Z) * aa(alert + bounce + sway, X);
  }
}

void humanoidPose(int asset, const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(asset);
  Builder b(sk);
  float breath = std::sin(c.idle * 1.9f);
  bool walker = asset == AL_PAWN;
  bool floater = !walker;
  float st = c.stride, sL = std::sin(c.gait), cL = std::cos(c.gait);
  // the base: floaters bob gently
  float bobF = floater ? 0.009f * std::sin(c.idle * 2.2f) : 0.0f;
  b.off[AH_BASE] = vec3(0, c.hover + bobF, 0);
  if (asset == AL_KING) b.R[AH_BASE] = aa(0.02f * std::sin(c.idle * 1.3f), Z) * aa(0.015f * std::sin(c.idle * 1.7f), X);
  if (walker) {
    // a waddle: short legs swing, the body rocks from side to side
    float hipSwing = 0.5f * st;
    b.R[AH_LTHIGH] = swing(hipSwing * sL + c.crouch * 0.7f);
    b.R[AH_RTHIGH] = swing(-hipSwing * sL + c.crouch * 0.7f);
    b.R[AH_LSHIN] = knee(st * (0.85f * std::max(0.0f, cL) + 0.15f) + c.crouch * 1.3f);
    b.R[AH_RSHIN] = knee(st * (0.85f * std::max(0.0f, -cL) + 0.15f) + c.crouch * 1.3f);
    float bob = st * 0.016f * std::fabs(cL) - st * 0.008f - c.crouch * 0.05f;
    b.off[AH_PELVIS] = vec3(0, bob, 0);
    b.R[AH_PELVIS] = aa(0.17f * st * sL, Z) * aa(0.07f * st * cL, Y);
  } else if (asset == AL_QUEEN) {
    // gliding: the gown sways
    b.R[AH_PELVIS] = aa(0.05f * st * sL, Z) * aa(0.03f * std::sin(c.idle * 0.9f), Y) * aa(0.04f * st, X);
  }
  // torso, head
  b.R[AH_CHEST] = aa(c.twist, Y) * aa(c.lean + 0.015f * breath, X) * aa(walker ? -0.1f * st * sL : 0.0f, Z);
  float headBob = walker ? 0.08f * st * std::sin(c.gait * 2.0f) : 0.0f;
  b.R[AH_HEAD] = aa(c.headYaw, Y) * aa(c.headPitch + headBob, X) * aa(walker ? -0.07f * st * sL : 0.0f, Z);
  // arms: offsets from the sculpted pose, swinging with a walk
  float armSwing = (walker ? 0.4f : 0.0f) * st * sL;
  b.R[AH_LARM] = aa(c.armL.y, Z) * swing(c.armL.x - armSwing);
  b.R[AH_RARM] = aa(-c.armR.y, Z) * swing(c.armR.x + armSwing);
  b.R[AH_LFORE] = swing(c.armL.z);
  b.R[AH_RFORE] = swing(c.armR.z);
  b.R[AH_ITEMR] = aa(c.itemPitch, X) * aa(c.itemYaw, Y);
  b.sc[AH_ITEMR] = c.itemR > 0.5f ? 1.0f : 0.0f;
  b.sc[AH_ITEML] = c.itemL > 0.5f ? 1.0f : 0.0f;
  antennae(b, c, AH_ANTL, AH_ANTR, st * 0.3f * std::sin(c.gait * 2.0f - 0.8f));
  if (asset == AL_PAWN) {
    // the chin drops, the tongue shoots out (the shaft stretches, the tip rides on its end)
    b.R[AH_JAW] = aa(0.55f * glm::clamp(c.jaw, 0.0f, 1.0f), X);
    float t = std::max(0.0f, c.tongue);
    float len = glm::length(sk.joint[AH_TAIL2] - sk.joint[AH_TAIL1]);
    b.st[AH_TAIL1] = vec3(1, 1, 1.0f + t / len);
    b.off[AH_TAIL2] = vec3(0, 0, t);
  } else if (asset == AL_QUEEN) {
    b.R[AH_JAW] = aa(-0.1f * c.jaw, X);
    b.off[AH_JAW] = vec3(0, 0, std::max(0.0f, c.tongue));
    // the tail: a slow travelling swish; 'lift' raises it, 'kick' lashes it round sideways
    float ts = c.tail + c.idle * 1.1f;
    float raise = glm::clamp(c.lift, 0.0f, 1.0f);
    b.R[AH_TAIL1] = aa(0.22f * std::sin(ts) + 0.55f * c.kick, Y) * aa(-0.9f * raise, X);
    b.R[AH_TAIL2] = aa(0.3f * std::sin(ts - 0.9f) + 0.45f * c.kick, Y) * aa(-1.0f * raise, X);
    b.R[AH_TAIL3] = aa(0.38f * std::sin(ts - 1.8f) + 0.35f * c.kick, Y) * aa(-0.8f * raise, X);
  } else {
    b.R[AH_JAW] = aa(0.35f * glm::clamp(c.jaw, 0.0f, 1.0f), X);
  }
  b.finish(out);
}

// The saucer: hovers, wobbles, banks, spins its light ring, opens its belly, lands on three legs.
void saucerPose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(AL_KNIGHT);
  Builder b(sk);
  float bob = 0.012f * std::sin(c.idle * 2.0f);
  b.off[UB_BODY] = vec3(0, c.hover + bob, 0);
  b.R[UB_BODY] = aa(c.twist, Z) * aa(c.lean, X) * aa(0.035f * std::sin(c.idle * 1.3f), Z) *
                 aa(0.03f * std::sin(c.idle * 1.1f + 1.0f), X);
  b.R[UB_RIM] = aa(c.spin + c.idle * 1.2f, Y);
  b.R[UB_PILOT] = aa(0.05f * std::sin(c.idle * 2.6f), Z) * aa(0.6f * c.headYaw, Y);
  b.R[UB_HEAD] = aa(0.4f * c.headYaw, Y) * aa(c.headPitch, X);
  b.R[UB_LARM] = aa(c.armL.y, Z) * swing(c.armL.x + 0.1f * std::sin(c.idle * 3.1f));
  b.R[UB_RARM] = aa(-c.armR.y, Z) * swing(c.armR.x + 0.1f * std::sin(c.idle * 2.7f + 1.0f));
  float h = glm::clamp(c.hatch, 0.0f, 1.0f);
  b.off[UB_HATCH] = vec3(0, -0.028f * smooth(h), 0);
  b.sc[UB_HATCH] = 1.0f + 0.25f * smooth(h);
  // landing legs fold up flat under the hull while flying
  const float A[3] = {3.14159265f, 3.14159265f / 3.0f, -3.14159265f / 3.0f};
  float fold = 1.0f - smooth(c.legs);
  for (int i = 0; i < 3; ++i) {
    vec3 k(-std::cos(A[i]), 0, std::sin(A[i]));
    b.R[UB_LEG1 + i] = aa(fold * 1.35f, k);
  }
  antennae(b, c, UB_ANTL, UB_ANTR, 0.0f);
  b.finish(out);
}

// The tripod: three legs solved with two-bone IK so the feet stay planted while
// the pod rises, crouches, leans and walks; a turning heat-ray eye; two tentacles.
void tripodPose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(AL_ROOK);
  Builder b(sk);
  const float A[3] = {3.14159265f, 1.0471976f, -1.0471976f};
  float L = smooth(c.legs);
  float st = c.stride, g = c.gait;
  float bob = st * 0.02f * std::fabs(std::sin(g * 1.5f)) + 0.006f * std::sin(c.idle * 1.6f);
  vec3 podOff(0, 0.34f * L - c.crouch * 0.22f + bob, 0);
  quat podR = aa(c.twist, Y) * aa(c.lean, X) * aa(0.045f * st * std::sin(g), Z);
  b.off[TB_BODY] = podOff;
  b.R[TB_BODY] = podR;
  b.R[TB_HOOD] = aa(c.headYaw, Y) * aa(c.headPitch, X);
  vec3 J = sk.joint[TB_BODY];
  for (int i = 0; i < 3; ++i) {
    int up = TB_L1U + 2 * i, lo = up + 1;
    vec3 hip0 = sk.joint[up], knee0 = sk.joint[lo];
    vec3 d(std::sin(A[i]), 0, std::cos(A[i]));
    vec3 foot0(d.x * 0.34f, 0.03f, d.z * 0.34f);
    // the foot: planted, or stepping (each leg in turn), or raised high to stamp
    vec3 ft = foot0;
    float ph = g + i * 2.0943951f;
    ft.z += st * 0.11f * std::sin(ph);
    ft.y += st * 0.07f * std::max(0.0f, std::cos(ph));
    bool stamping = (c.lift < 0 && i == 1) || (c.lift > 0 && i == 2);
    if (stamping) {
      float l = std::fabs(c.lift);
      ft += vec3(0, 0.32f * l, 0) + d * 0.04f * l + vec3(0, 0, 0.06f * l);
    }
    vec3 hipW = J + podOff + podR * (hip0 - J);
    float L1 = glm::length(knee0 - hip0), L2 = glm::length(foot0 - knee0);
    vec3 toF = ft - hipW;
    float dist = glm::clamp(glm::length(toF), std::fabs(L1 - L2) + 1e-3f, L1 + L2 - 1e-3f);
    vec3 dirF = glm::normalize(toF);
    vec3 hint = glm::normalize(d + vec3(0, 1.4f, 0));
    vec3 n = glm::cross(dirF, hint);
    if (glm::length(n) < 1e-4f) n = glm::cross(dirF, vec3(0, 0, 1));
    n = glm::normalize(n);
    vec3 perp = glm::normalize(glm::cross(n, dirF));
    float ca = (L1 * L1 + dist * dist - L2 * L2) / (2.0f * L1 * dist);
    float sa = std::sqrt(std::max(0.0f, 1.0f - ca * ca));
    vec3 kneeW = hipW + dirF * (ca * L1) + perp * (sa * L1);
    quat Rup = rotationBetween(knee0 - hip0, glm::inverse(podR) * (kneeW - hipW));
    quat Rlo = rotationBetween(foot0 - knee0, glm::inverse(podR * Rup) * (ft - kneeW));
    b.R[up] = Rup;
    b.R[lo] = Rlo;
  }
  // tentacles: dangle and sway; armL / armR reach (x), swing sideways (y) and curl (z)
  for (int side = 0; side < 2; ++side) {
    int t1 = side == 0 ? TB_TL1 : TB_TR1;
    const vec3& a = side == 0 ? c.armL : c.armR;
    float sd = side == 0 ? 1.0f : -1.0f;
    float w = c.idle * 1.4f + side * 2.1f;
    b.R[t1] = aa(sd * (0.12f * std::sin(w) + a.y), Z) * swing(a.x + 0.1f * std::sin(w * 0.7f));
    b.R[t1 + 1] = swing(a.z * 0.6f + 0.15f * std::sin(w - 0.8f)) * aa(sd * 0.1f * std::sin(w * 1.3f), Z);
    b.R[t1 + 2] = swing(a.z * 0.8f + 0.2f * std::sin(w - 1.6f));
  }
  b.finish(out);
}

// The cow: walks, kicks its legs in the air ('wings' 0..1), swishes its tail.
void cowPose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(AL_COW);
  Builder b(sk);
  float st = c.stride, g = c.gait, fl = glm::clamp(c.wings, 0.0f, 1.0f);
  const float phase[4] = {0.0f, 3.14159f, 3.14159f, 0.0f};
  for (int i = 0; i < 4; ++i) {
    float walk = 0.45f * st * std::sin(g + phase[i]);
    float flail = fl * (0.7f * std::sin(c.idle * 17.0f + i * 1.9f) + (i < 2 ? 0.5f : -0.5f));
    b.R[CW_FL + i] = swing(walk + flail) * aa((i % 2 ? -1.0f : 1.0f) * fl * 0.35f, Z);
  }
  b.R[CW_HEAD] = aa(c.headYaw, Y) * aa(c.headPitch + 0.06f * std::sin(c.idle * 1.2f) - fl * 0.5f, X);
  b.R[CW_TAIL] = aa(0.4f * std::sin(c.idle * 2.0f + c.tail) + fl * 0.8f * std::sin(c.idle * 13.0f), Z) * aa(-fl * 1.2f, X);
  b.R[CW_BODY] = aa(0.04f * st * std::sin(g), Z);
  b.finish(out);
}

}  // namespace

void buildPose(int asset, const Controls& c, Pose& out) {
  switch (asset) {
    case AL_PAWN:
    case AL_BISHOP:
    case AL_JAR:
    case AL_QUEEN:
    case AL_KING: humanoidPose(asset == AL_JAR ? AL_BISHOP : asset, c, out); break;
    case AL_KNIGHT:
    case AL_CANOPY: saucerPose(c, out); break;
    case AL_ROOK: tripodPose(c, out); break;
    case AL_COW: cowPose(c, out); break;
    default: out.count = 0; break;
  }
}

}  // namespace alien
