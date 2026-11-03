// Turning animation controls into bone matrices for the dinosaurs: theropods
// striding, lunging, kicking and pouncing; the pteranodon walking on its folded
// wings or flying; the four-legged plodding, charging and rearing; and all of
// them going limp. The lowest foot is kept on the board (a stride lowers the
// hips, a crouch sinks the body).
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "dino.h"
#include "pose_kit.h"

namespace dino {
using glm::mat4;
using glm::quat;
using glm::vec3;

namespace {
using namespace posekit;

// The tail's resting curl to one side (radians per bone): it drifts slowly from side to
// side over a couple of minutes (each animal has its own idle phase), lessens as the animal
// walks, and gives way to a swing of the tail ('itemYaw').
float restCurl(const Controls& c, float amount) {
  float side = std::sin(c.idle * 0.045f + 1.3f);
  side = glm::clamp(side * 1.6f, -1.0f, 1.0f);
  float use = 1.0f - glm::clamp(std::fabs(c.itemYaw) * 2.5f + std::fabs(c.itemPitch) * 1.5f, 0.0f, 1.0f);
  return amount * side * (1.0f - 0.7f * glm::clamp(c.stride, 0.0f, 1.0f)) * use;
}

// Moves the root so that the lowest of the ground points rests on the board
// ('weight' 0..1: how much; off in the air or when lying dead).
void groundFit(int asset, Builder& b, int root, Pose& out, float weight) {
  if (weight <= 0.001f) return;
  vec3 pts[4];
  int bones[4];
  int n = groundPoints(asset, pts, bones);
  float lo = 1e9f;
  for (int i = 0; i < n; ++i) lo = std::min(lo, (out.bone[bones[i]] * glm::vec4(pts[i], 1.0f)).y);
  if (lo > 1e8f) return;
  b.off[root].y -= lo * weight;
  b.finish(out);
}

// A theropod: raptor, spinosaurus, tyrannosaurus.
void theroPose(int asset, const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(asset);
  Builder b(sk);
  float st = c.stride, run = glm::clamp(c.run, 0.0f, 1.0f), g = c.gait;
  float dead = glm::clamp(c.dead, 0.0f, 1.0f), alive = 1.0f - dead;
  float tuck = smooth(c.tuck), kick = glm::clamp(c.kick, 0.0f, 1.0f);
  float breath = std::sin(c.idle * (asset == D_PAWN ? 2.6f : 1.4f)) * alive;
  // --- the body: pitched by 'lean' (+ nose down), swaying with the stride
  float pitch = c.lean + 0.1f * run * st - 0.25f * tuck;
  b.R[TH_ROOT] = aa(0.05f * st * std::sin(g) + c.twist * 0.3f, Y) * aa(pitch, X) * aa(0.03f * st * std::cos(g), Z);
  b.R[TH_CHEST] = aa(c.twist * 0.5f, Y) * aa(0.015f * breath, X);
  // --- the legs: thigh, shin and foot; the foot stays level unless it is folding up in a step
  float amp = (0.42f + 0.25f * run) * st;
  for (int side = 0; side < 2; ++side) {
    bool L = side == 0;
    int th = L ? TH_LTHIGH : TH_RTHIGH, sh = L ? TH_LSHIN : TH_RSHIN, ft = L ? TH_LFOOT : TH_RFOOT;
    float ph = g + (L ? 0.0f : PI);
    float s = std::sin(ph), lift = std::max(0.0f, std::cos(ph));
    float k = c.crouch;
    float kk = L ? 0.0f : kick;
    float thigh = amp * s + k * 0.55f + tuck * 1.3f + kk * 1.4f + dead * 0.35f;
    float shin = (0.75f + 0.35f * run) * lift * st + 0.06f * st + k * 0.95f + tuck * 0.15f + kk * 0.2f + dead * 0.6f;
    float foot = thigh - shin + 0.65f * lift * st - tuck * 1.0f - kk * 0.9f + dead * 0.5f;
    b.R[th] = aa(-pitch, X) * swing(thigh) * aa((L ? 1.0f : -1.0f) * (0.05f * tuck + 0.08f * dead), Z);
    b.R[sh] = knee(shin);
    b.R[ft] = aa(foot, X);
  }
  // --- the neck and head: 'neck' rears it back (+) or lunges (-); the dead throw it back
  float neckUp = c.neck * 0.6f + dead * 0.9f - 0.04f * st * std::sin(g * 2.0f);
  b.R[TH_NECK] = aa(c.headYaw * 0.45f + c.twist * 0.3f, Y) * aa(-neckUp + 0.012f * breath, X);
  b.R[TH_HEAD] = aa(c.headYaw * 0.55f, Y) * aa(c.headPitch + c.neck * 0.25f + dead * 0.3f, X) * aa(dead * 0.2f, Z);
  float jawMax = asset == D_KING ? 0.75f : (asset == D_QUEEN ? 0.7f : 0.8f);
  b.R[TH_JAW] = aa(jawMax * glm::clamp(c.jaw + 0.25f * dead, 0.0f, 1.0f), X);
  // --- the tail: a gentle curl at rest, the stride's sway, a lash to one side ('itemYaw'),
  // raised ('itemPitch'); limp when dead
  float curl = restCurl(c, asset == D_PAWN ? 0.06f : (asset == D_KING ? 0.13f : 0.15f)) * alive;
  float sway = (0.1f * std::sin(c.idle * 0.8f + c.tail) * (asset == D_PAWN ? 0.6f : 1.0f) + 0.12f * st * std::sin(g)) * alive;
  const int tb[3] = {TH_TAIL1, TH_TAIL2, TH_TAIL3};
  const float share[3] = {0.4f, 0.35f, 0.25f};
  for (int i = 0; i < 3; ++i) {
    float yaw = curl * (0.7f + 0.3f * i) + sway * (0.6f + 0.4f * i) - c.twist * 0.25f * (i == 0) + c.itemYaw * share[i];
    float up = c.itemPitch * share[i] + (i == 0 ? -c.lean * 0.5f + 0.1f * run * st + 0.3f * tuck : 0.0f) - 0.12f * dead +
               0.03f * std::sin(c.idle * 0.7f + i) * alive;
    b.R[tb[i]] = aa(yaw, Y) * aa(up, X);
  }
  // --- the arms (the human offsets): reaching out, raised for balance in a pounce
  vec3 aL = glm::mix(c.armL + tuck * vec3(0.9f, 0.4f, -0.4f), vec3(0.3f, 0.25f, -0.4f), dead);
  vec3 aR = glm::mix(c.armR + tuck * vec3(0.9f, 0.4f, -0.4f), vec3(0.3f, 0.25f, -0.4f), dead);
  float armSwing = 0.15f * st * std::sin(g);
  b.R[TH_LARM] = aa(aL.y, Z) * swing(aL.x - armSwing);
  b.R[TH_RARM] = aa(-aR.y, Z) * swing(aR.x + armSwing);
  b.R[TH_LFORE] = swing(aL.z);
  b.R[TH_RFORE] = swing(aR.z);
  b.finish(out);
  groundFit(asset, b, TH_ROOT, out, (1.0f - dead) * (1.0f - tuck));
}

// Four-legged: the triceratops and the brachiosaurus.
struct Quad {
  int upper[4], lower[4], foot[4];  // FL FR HL HR
};
// The legs walking: each foot is kept level on the board while it bears weight, and
// flexes (the sole turning back) as it is lifted. A front leg's elbow points back, a
// hind leg's knee forward; a crouch folds them that way.
void quadLegs(Builder& b, const Quad& q, const Controls& c, float pitch) {
  const float walk[4] = {0.0f, PI, PI * 1.5f, PI * 0.5f};  // a lateral-sequence walk
  const float trot[4] = {0.0f, PI, PI, 0.0f};
  float st = c.stride, run = glm::clamp(c.run, 0.0f, 1.0f);
  float dead = glm::clamp(c.dead, 0.0f, 1.0f);
  for (int i = 0; i < 4; ++i) {
    float ph = c.gait + glm::mix(walk[i], trot[i], run);
    float s = std::sin(ph), lift = std::max(0.0f, std::cos(ph));
    bool front = i < 2;
    float amp = (0.32f + 0.18f * run) * st;
    float sw = amp * s + (front ? 0.3f : -0.35f) * dead + (front ? -0.4f : 0.5f) * c.crouch;
    // the elbow / knee: (+ folds the lower leg back)
    float bend = front ? (0.3f + 0.25f * run) * lift * st - 0.8f * c.crouch - 0.15f * dead
                       : (0.7f + 0.3f * run) * lift * lift * st + 0.9f * c.crouch - 0.1f * dead;
    float flex = (front ? 0.9f : 0.6f) * lift * st + 0.5f * dead;
    b.R[q.upper[i]] = aa(-pitch, X) * swing(sw);
    b.R[q.lower[i]] = knee(bend);
    b.R[q.foot[i]] = aa(sw - bend + flex, X);  // level: undo the leg's swing and bend
  }
}

void quadPose(int asset, const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(asset);
  Builder b(sk);
  bool brachio = asset == D_ROOK;
  float st = c.stride, g = c.gait;
  float dead = glm::clamp(c.dead, 0.0f, 1.0f), alive = 1.0f - dead;
  float breath = std::sin(c.idle * (brachio ? 0.9f : 1.3f)) * alive;
  Quad q = {{QD_FLU, QD_FRU, QD_HLU, QD_HRU}, {QD_FLL, QD_FRL, QD_HLL, QD_HRL}, {QD_FLF, QD_FRF, QD_HLF, QD_HRF}};
  float pitch = c.lean + 0.02f * st * std::sin(g * 2.0f);
  b.R[QD_BODY] = aa(c.twist * 0.4f + 0.04f * st * std::sin(g), Y) * aa(pitch, X) * aa(0.035f * st * std::sin(g) + 0.008f * breath, Z);
  quadLegs(b, q, c, pitch);
  // --- the neck: rears up (+) or lunges (-), the head turning with it
  const int nb[3] = {QD_NECK1, QD_NECK2, QD_NECK3};
  float sway = (brachio ? 0.035f : 0.0f) * std::sin(c.idle * 0.45f) * alive;
  float yawShare = brachio ? 0.22f : 0.12f;
  for (int i = 0; i < 3; ++i) {
    float up = (brachio ? 0.32f : 0.15f) * c.neck + (brachio ? 0.35f : 0.12f) * dead + 0.01f * breath - 0.02f * st * std::sin(g * 2.0f);
    b.R[nb[i]] = aa(c.headYaw * yawShare + sway + c.twist * 0.15f, Y) * aa(-up, X);
  }
  float headYaw = c.headYaw * (1.0f - 3.0f * yawShare);
  b.R[QD_HEAD] = aa(headYaw, Y) * aa(c.headPitch + (brachio ? 0.25f : 0.1f) * c.neck - pitch * 0.5f + (brachio ? -0.3f : 0.2f) * dead, X);
  b.R[QD_JAW] = aa((brachio ? 0.5f : 0.45f) * glm::clamp(c.jaw + 0.2f * dead, 0.0f, 1.0f), X);
  // --- the tail: curled to one side at rest (slowly shifting from side to side), drawn
  // straight behind as it walks or swings it
  const int tb[3] = {QD_TAIL1, QD_TAIL2, QD_TAIL3};
  const float share[3] = {0.4f, 0.35f, 0.25f};
  float curl = restCurl(c, brachio ? 0.24f : 0.2f) * alive;
  float tsway = (0.08f * std::sin(c.idle * 0.6f + c.tail) + 0.1f * st * std::sin(g)) * alive;
  for (int i = 0; i < 3; ++i) {
    float yaw = curl * (0.8f + 0.2f * i) + tsway * (0.6f + 0.4f * i) + c.itemYaw * share[i] - c.twist * 0.2f * (i == 0);
    float up = c.itemPitch * share[i] + (i == 0 ? -pitch * 0.5f : 0.0f) - 0.1f * dead;
    b.R[tb[i]] = aa(yaw, Y) * aa(up, X);
  }
  // --- rearing up on the hind legs: the body pivots about the hips, the knees give a little
  // under the weight; the forelegs are drawn up, elbows back, the forefeet hanging and pawing
  if (c.rear > 0.001f) {
    float r = smooth(c.rear);
    float a = (brachio ? 1.05f : 0.45f) * r;
    const vec3& h = sk.joint[QD_HLU];
    vec3 pivot(0, h.y, h.z);
    b.rotateAbout(QD_BODY, aa(-a, X), pivot);
    const float kneeGive = 0.35f * r, thighFwd = 0.2f * r;
    for (int i = 2; i < 4; ++i) {
      b.R[q.upper[i]] = aa(a, X) * swing(thighFwd) * b.R[q.upper[i]];
      b.R[q.lower[i]] = knee(kneeGive) * b.R[q.lower[i]];
      b.R[q.foot[i]] = aa(thighFwd - kneeGive, X) * b.R[q.foot[i]];
    }
    float paw = std::sin(c.idle * 6.0f) * 0.25f * r;
    for (int i = 0; i < 2; ++i) {
      // (the body is pitched up by a: an upper arm that stays put in the body swings forward with it)
      float upper = (brachio ? 0.35f : 0.2f) * r - a + (i == 0 ? paw : -paw);
      b.R[q.upper[i]] = swing(upper) * b.R[q.upper[i]];
      b.R[q.lower[i]] = knee(-(brachio ? 0.55f : 0.45f) * r + 0.5f * paw) * b.R[q.lower[i]];
      b.R[q.foot[i]] = aa((brachio ? 0.7f : 0.5f) * r, X) * b.R[q.foot[i]];
    }
    for (int i = 0; i < 3; ++i) b.R[tb[i]] = aa(a * (i == 0 ? 0.7f : 0.15f), X) * b.R[tb[i]];  // the tail props it up
    for (int i = 0; i < 3; ++i) b.R[nb[i]] = aa(a * 0.18f, X) * b.R[nb[i]];  // the neck leans forward over it
  }
  b.finish(out);
  groundFit(asset, b, QD_BODY, out, 1.0f - dead);
}

// The pteranodon: on the ground it walks on its hands with the wings folded; in the
// air the body levels, the legs trail and the wings beat.
void pteroPose(const Controls& c, Pose& out) {
  const Skeleton& sk = skeleton(D_KNIGHT);
  Builder b(sk);
  float st = c.stride, g = c.gait;
  float spread = smooth(glm::clamp(c.wings, 0.0f, 1.0f));
  float air = smooth(glm::clamp(c.hover, 0.0f, 1.0f));
  float dead = glm::clamp(c.dead, 0.0f, 1.0f), alive = 1.0f - dead;
  float tuck = smooth(c.tuck);
  float breath = std::sin(c.idle * 2.0f) * alive;
  float pitch = c.lean + 0.6f * air + 0.15f * (1.0f - spread) * (1.0f - air);
  b.R[PT_BODY] = aa(c.twist * 0.5f, Y) * aa(pitch, X) * aa(0.04f * st * std::sin(g), Z);
  // neck and head: level in flight, stabbing ('neck' -) or drawn back (+)
  b.R[PT_NECK] = aa(c.headYaw * 0.4f, Y) * aa(-(c.neck * 0.7f) + 0.35f * air - 0.5f * dead + 0.01f * breath, X);
  b.R[PT_HEAD] = aa(c.headYaw * 0.6f, Y) * aa(c.headPitch + c.neck * 0.25f - 0.15f * air + 0.25f * dead, X);
  b.R[PT_JAW] = aa(0.55f * glm::clamp(c.jaw + 0.2f * dead, 0.0f, 1.0f), X);
  // the wings
  for (int side = 0; side < 2; ++side) {
    float s = side == 0 ? 1.0f : -1.0f;
    int ua = side == 0 ? PT_LARM : PT_RARM, fa = side == 0 ? PT_LFORE : PT_RFORE;
    int mc = side == 0 ? PT_LMETA : PT_RMETA, fg = side == 0 ? PT_LFINGER : PT_RFINGER;
    vec3 S = sk.joint[ua], E = sk.joint[fa], W = sk.joint[mc], N = sk.joint[fg];
    vec3 T = wingTip();
    T.x *= s;
    // folded: the arm out and back, the forearm forward and down, the hand straight down to
    // the board, the finger folded up and back from the knuckle beside the arm, its tip over the
    // hips; stepping as it walks
    float ph = g + (side == 0 ? 0.0f : PI);
    float step = 0.35f * st * std::sin(ph), lift = std::max(0.0f, std::cos(ph)) * st;
    vec3 dA = glm::normalize(vec3(s * 0.3f, -0.6f, -0.55f + step * 0.6f));
    vec3 dF = glm::normalize(vec3(s * 0.05f, -0.75f + 0.25f * lift, 0.66f + step));
    vec3 dM = glm::normalize(vec3(s * 0.03f, -1.0f, 0.05f + 0.6f * lift + step * 0.4f));
    vec3 dT = glm::normalize(vec3(s * 0.2f, 0.62f, -0.76f));
    quat fA = rotationBetween(E - S, dA);
    quat fF = rotationBetween(W - E, glm::inverse(fA) * dF);
    quat fM = rotationBetween(N - W, glm::inverse(fA * fF) * dM);
    quat fT = rotationBetween(T - N, glm::inverse(fA * fF * fM) * dT);
    // spread: beating (the outer joints lag behind), swept a little forward on the downstroke
    float fl = c.flap;
    float beat = alive * spread;
    quat sA = aa(s * (0.55f * std::sin(fl) * beat + 0.05f), Z) * aa(-s * 0.12f * std::cos(fl) * beat, Y);
    quat sF = aa(s * 0.2f * std::sin(fl - 0.6f) * beat, Z);
    quat sM = aa(s * 0.15f * std::sin(fl - 1.0f) * beat, Z);
    quat sT = aa(s * (0.3f * std::sin(fl - 1.4f) * beat - 0.04f), Z) * aa(s * 0.06f, Y);
    float u = spread;
    b.R[ua] = glm::slerp(fA, sA, u);
    b.R[fa] = glm::slerp(fF, sF, u);
    b.R[mc] = glm::slerp(fM, sM, u);
    b.R[fg] = glm::slerp(fT, sT, u);
  }
  // the membranes: drawn in against the bones of the wing as it folds. Each trailing-edge
  // bone squeezes the membrane towards a line through its joint (along x at rest), turned onto
  // its wing bone as it folds.
  float k = glm::mix(0.12f, 1.0f, spread), u = (1.0f - k) / 0.88f;
  for (int side = 0; side < 2; ++side) {
    bool L = side == 0;
    vec3 T = wingTip();
    if (!L) T.x = -T.x;
    const int trail[4] = {L ? PT_LTRAILA : PT_RTRAILA, L ? PT_LTRAIL : PT_RTRAIL, L ? PT_LTRAILH : PT_RTRAILH, L ? PT_LTRAIL2 : PT_RTRAIL2};
    const int lead[5] = {L ? PT_LARM : PT_RARM, L ? PT_LFORE : PT_RFORE, L ? PT_LMETA : PT_RMETA, L ? PT_LFINGER : PT_RFINGER, -1};
    for (int i = 0; i < 4; ++i) {
      vec3 a = sk.joint[lead[i]], e = lead[i + 1] >= 0 ? sk.joint[lead[i + 1]] : T;
      vec3 f = glm::normalize(e - a);
      b.st[trail[i]] = vec3(1.0f, k, k);
      b.R[trail[i]] = glm::slerp(quat(1, 0, 0, 0), rotationBetween(vec3(f.x < 0 ? -1.0f : 1.0f, 0, 0), f), u);
    }
  }
  // the legs: walking; trailed behind in flight; thrust forward, talons first ('tuck')
  for (int side = 0; side < 2; ++side) {
    int th = side == 0 ? PT_LTHIGH : PT_RTHIGH, sh = side == 0 ? PT_LSHIN : PT_RSHIN;
    float ph = g + (side == 0 ? PI : 0.0f);
    float s = std::sin(ph), lift = std::max(0.0f, std::cos(ph));
    float sw = 0.4f * st * s * (1.0f - air) - 1.3f * air * (1.0f - tuck) + 1.2f * tuck + 0.3f * dead;
    b.R[th] = aa(-pitch * (1.0f - air), X) * swing(sw);
    b.R[sh] = knee(0.6f * lift * st * (1.0f - air) + 0.15f * air - 0.2f * tuck + 0.4f * dead);
  }
  b.finish(out);
  groundFit(D_KNIGHT, b, PT_BODY, out, (1.0f - air) * (1.0f - dead) * (1.0f - tuck));
}

}  // namespace

void buildPose(int asset, const Controls& c, Pose& out) {
  switch (asset) {
    case D_PAWN:
    case D_QUEEN:
    case D_KING: theroPose(asset, c, out); break;
    case D_KNIGHT: pteroPose(c, out); break;
    case D_BISHOP:
    case D_ROOK: quadPose(asset, c, out); break;
    default: out.count = 0; break;
  }
}

}  // namespace dino
