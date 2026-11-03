// Choreography for the dinosaurs. Raptors run, pteranodons fly their moves,
// the four-legged plod and the great theropods stride, shaking the board. They
// don't shatter like marble: they fight like animals - claws, horns, beaks,
// jaws and tails - bleed, and fall on their sides, and the dead lie in their
// blood a moment before they fade away. Captures:
//
//   Pawn    killing claw (a leap onto the victim, kicking with the sickle claws),
//           pack attack (two more raptors come running and they take it down
//           together), dodge and bite (it slips the victim's lunge and seizes its throat)
//   Knight  dive (a stoop out of the sky, beak first), wing buffet (it rears and beats
//           its wings in the victim's face, then stabs), talon strike (it swoops in low,
//           feet first, and finishes the victim on the ground)
//   Bishop  horn charge (gored and tossed), horn lock (it takes the victim's bite on its
//           frill, drives it back and gores it), frill bash (a swing of the head knocks the
//           victim down, then it tramples it)
//   Rook    rearing stomp (it rears up and crashes down on the victim), neck swing (the
//           neck swung like a club), tail lash (the tail cracks like a whip)
//   Queen   hooked claws, snatch (seized in the long jaws, shaken and flung), tail swipe
//   King    bone-crusher (seized, lifted, shaken and thrown), the roar (and a bite from
//           above), head ram and stomp
//
// Victims fight back where they can: they threaten, lunge and snap at the attacker.
// Checkmate: a meteor.
#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "battle.h"
#include "battle_kit.h"

namespace md = medieval;
namespace dn = dino;

namespace {
using namespace bkit;

const float kTau = 6.2831853f;
const vec3 kBlood(0.3f, 0.004f, 0.003f);

float approach(float x, float target, float rate) {
  return x < target ? std::min(target, x + rate) : std::max(target, x - rate);
}
// progress through [a, b] (0 before, 1 after)
float seg(float t, float a, float b) { return glm::clamp((t - a) / (b - a), 0.0f, 1.0f); }
float bump(float t, float a, float b) { return std::sin(seg(t, a, b) * kPI); }
// eases through keys (time, value), held before the first and after the last
float kf(float t, std::initializer_list<glm::vec2> keys) {
  const glm::vec2* k = keys.begin();
  size_t n = keys.size();
  if (t <= k[0].x) return k[0].y;
  for (size_t i = 1; i < n; ++i)
    if (t <= k[i].x) return glm::mix(k[i - 1].y, k[i].y, glm::smoothstep(k[i - 1].x, k[i].x, t));
  return k[n - 1].y;
}

bool isThero(const Actor& a) { return dn::theropod(a.kind); }
bool isPtero(const Actor& a) { return a.kind == K_KNIGHT; }
bool isQuad(const Actor& a) { return dn::quadruped(a.kind); }
bool isSmall(const Actor& a) { return a.kind == K_PAWN || a.kind == K_KNIGHT; }  // can be lifted and flung
bool isHuge(const Actor& a) { return a.kind == K_ROOK; }

int headBone(const Actor& a) { return isThero(a) ? (int)dn::TH_HEAD : (isPtero(a) ? (int)dn::PT_HEAD : (int)dn::QD_HEAD); }
int chestBone(const Actor& a) { return isThero(a) ? (int)dn::TH_CHEST : (isPtero(a) ? (int)dn::PT_BODY : (int)dn::QD_BODY); }
int tailBone(const Actor& a) { return isThero(a) ? (int)dn::TH_TAIL3 : (isPtero(a) ? (int)dn::PT_BODY : (int)dn::QD_TAIL3); }

// world position of a point riding on a bone (rest-pose model coordinates)
vec3 bonePoint(const Actor& a, int bone, const vec3& rest) {
  return vec3(a.model() * md::boneMatrix(a.pose, bone) * glm::vec4(rest, 1.0f));
}
vec3 puppetPoint(const Puppet& p, int bone, const vec3& rest) {
  return vec3(p.model() * md::boneMatrix(p.pose, bone) * glm::vec4(rest, 1.0f));
}
vec3 modelPoint(const Actor& a, const vec3& rest) { return vec3(a.model() * glm::vec4(rest, 1.0f)); }

vec3 headOf(const Actor& a) { return bonePoint(a, headBone(a), dn::headPoint(a.kind)); }
vec3 mouthOf(const Actor& a) { return bonePoint(a, headBone(a), dn::mouthPoint(a.kind)); }
vec3 chestOf(const Actor& a) { return bonePoint(a, chestBone(a), dn::chestPoint(a.kind)); }
vec3 neckOf(const Actor& a) { return glm::mix(chestOf(a), headOf(a), isHuge(a) ? 0.25f : 0.45f); }
vec3 tailOf(const Actor& a) { return bonePoint(a, tailBone(a), dn::tailTip(a.kind)); }
vec3 hornsOf(const Actor& a) { return bonePoint(a, dn::QD_HEAD, dn::hornPoint(K_BISHOP)); }
// how far the snout (or the horns) reach in front of the animal's middle
float reachOf(const Actor& a) { return dn::mouthPoint(a.kind).z * a.scale; }
float halfWidthOf(const Actor& a) { return dn::bodyHalfWidth(a.kind) * a.scale; }
// centre distance at which the attacker's jaws close on the neck of a victim facing it
float biteDist(const Actor& a, const Actor& v) { return reachOf(a) + reachOf(v) * 0.4f + halfWidthOf(v) * 0.5f; }

vec3 clampBoard(vec3 p, float m = 3.35f) {
  p.x = glm::clamp(p.x, -m, m);
  p.z = glm::clamp(p.z, -m, m);
  return p;
}

// ---------------------------------------------------------------------------
// Blood, feathers and dust
// ---------------------------------------------------------------------------
void bloodSpray(World& w, const vec3& at, const vec3& dir, int n, float speed, float spread = 0.45f) {
  for (int i = 0; i < n; ++i) {
    Particle p;
    vec3 d = glm::normalize(dir + w.fx.rndDir() * spread + vec3(0, 0.15f, 0));
    p.pos = at + w.fx.rndDir() * 0.012f;
    p.vel = d * speed * w.fx.rnd(0.35f, 1.15f);
    p.size = w.fx.rnd(0.006f, 0.016f);
    p.color = kBlood * w.fx.rnd(0.7f, 1.3f);
    p.colorEnd = kBlood * 0.55f;
    p.opacity = 1.0f;
    p.maxLife = w.fx.rnd(0.5f, 1.1f);
    p.type = PT_SPARK;
    p.stretch = 0.022f;
    p.gravity = 9.8f;
    p.drag = 0.5f;
    p.fadeIn = 0.0f;
    w.fx.add(p);
  }
  for (int i = 0; i < n / 7 + 1; ++i) {  // a fine red mist
    Particle p;
    p.pos = at + w.fx.rndDir() * 0.02f;
    p.vel = glm::normalize(dir + w.fx.rndDir() * 0.6f) * w.fx.rnd(0.2f, 0.6f);
    p.size = w.fx.rnd(0.02f, 0.04f);
    p.grow = 0.12f;
    p.color = kBlood * 0.9f;
    p.colorEnd = kBlood * 0.5f;
    p.opacity = w.fx.rnd(0.25f, 0.45f);
    p.maxLife = w.fx.rnd(0.35f, 0.6f);
    p.type = PT_SMOKE;
    p.drag = 3.0f;
    p.gravity = 0.3f;
    p.rot = w.fx.rnd(0, 6.28f);
    p.seed = w.fx.rnd();
    p.fadeIn = 0.02f;
    w.fx.add(p);
  }
}

// a pool of blood spreading on the board (it fades long after the fight)
float gPoolLift = 0;  // successive pools sit a hair above each other
void bloodPool(World& w, const vec3& at, float size, float spread = 1.0f, float life = 7.0f) {
  if (!overBoard(at)) return;
  int id = w.addPuppet(dn::D_POOL, 0, 4);
  Puppet* p = w.puppet(id);
  gPoolLift = gPoolLift > 0.0012f ? 0.0f : gPoolLift + 0.00015f;
  p->pos = vec3(at.x, 0.0012f + gPoolLift, at.z);
  p->yaw = w.fx.rnd(0.0f, kTau);
  p->scale = 0.004f;
  p->growTo = size;
  p->growRate = size / std::max(0.05f, spread);
  p->life = life;
  p->fadeRate = 0.35f;
  p->shadow = false;
}
// drops and splashes thrown along 'dir'
void splatter(World& w, const vec3& at, const vec3& dir, int n, float reach = 0.4f) {
  vec3 d = flat(dir);
  for (int i = 0; i < n; ++i) {
    vec3 p = flat(at) + d * w.fx.rnd(0.05f, reach) + vec3(w.fx.rnd(-1, 1), 0, w.fx.rnd(-1, 1)) * 0.07f;
    bloodPool(w, p, w.fx.rnd(0.025f, 0.06f), 0.15f, w.fx.rnd(6.0f, 9.0f));
  }
}
enum WoundKind { W_BITE, W_CLAW, W_GORE, W_CRUSH };
// a wound: the spray, splashes on the board and the sounds of it
void wound(World& w, const vec3& at, const vec3& dir, float power, int kind) {
  bloodSpray(w, at, dir, (int)(80 * power), 2.2f + 1.6f * power);
  splatter(w, at, dir, 4 + (int)(4 * power));
  switch (kind) {
    case W_BITE:
      w.sound(SFX_SNAP, 0.7f, at, w.fx.rnd(0.9f, 1.1f));
      w.sound(SFX_CRUNCH, 0.8f, at, w.fx.rnd(0.9f, 1.1f));
      break;
    case W_CLAW: w.sound(SFX_TEAR, 0.9f, at, w.fx.rnd(0.9f, 1.15f)); break;
    case W_GORE: w.sound(SFX_GORE, 1.0f, at, w.fx.rnd(0.9f, 1.05f)); break;
    default:
      w.sound(SFX_CRUNCH, 1.0f, at, w.fx.rnd(0.8f, 0.95f));
      w.sound(SFX_QUAKE, 0.6f, at, 1.2f);
      break;
  }
  w.sound(SFX_BLOOD, 0.55f * power, at, w.fx.rnd(0.9f, 1.15f));
}
// feathers knocked out of a raptor, the colour of its army
void feathers(World& w, const Actor& v, const vec3& at, const vec3& dir, int n) {
  if (v.kind != K_PAWN) return;
  vec3 col = v.color == 0 ? vec3(0.55f, 0.42f, 0.27f) : vec3(0.05f, 0.05f, 0.055f);
  for (int i = 0; i < n; ++i) {
    Particle p;
    p.pos = at + w.fx.rndDir() * 0.03f;
    p.vel = glm::normalize(dir + w.fx.rndDir() * 0.9f + vec3(0, 0.6f, 0)) * w.fx.rnd(0.4f, 1.4f);
    p.size = w.fx.rnd(0.008f, 0.016f);
    p.color = col * w.fx.rnd(0.7f, 1.3f);
    p.colorEnd = p.color;
    p.opacity = 0.9f;
    p.maxLife = w.fx.rnd(1.2f, 2.2f);
    p.type = PT_SMOKE;
    p.drag = 3.5f;
    p.gravity = 0.6f;
    p.rot = w.fx.rnd(0, 6.28f);
    p.rotVel = w.fx.rnd(-6.0f, 6.0f);
    p.seed = w.fx.rnd();
    p.fadeIn = 0.0f;
    w.fx.add(p);
  }
}

// ---------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------
// an animal's call: a threat, a cry of triumph
void callOf(World& w, const Actor& a, float vol = 1.0f) {
  vec3 at = headOf(a);
  switch (a.kind) {
    case K_PAWN: w.sound(SFX_SCREECH, 0.8f * vol, at, w.fx.rnd(0.92f, 1.12f)); break;
    case K_KNIGHT: w.sound(SFX_SQUAWK, 0.8f * vol, at, w.fx.rnd(0.95f, 1.1f)); break;
    case K_BISHOP: w.sound(SFX_BELLOW, 0.8f * vol, at, w.fx.rnd(1.05f, 1.2f)); break;
    case K_ROOK: w.sound(SFX_BELLOW, 1.0f * vol, at, w.fx.rnd(0.55f, 0.65f)); break;
    case K_QUEEN: w.sound(SFX_REXROAR, 0.8f * vol, at, w.fx.rnd(1.2f, 1.3f)); break;
    default:
      w.sound(SFX_REXROAR, 1.0f * vol, at, w.fx.rnd(0.95f, 1.02f));
      w.shake(0.15f * vol);
      break;
  }
}
// a warning: a hiss, a growl, a snort
void warnOf(World& w, const Actor& a, float vol = 1.0f) {
  vec3 at = headOf(a);
  switch (a.kind) {
    case K_PAWN: w.sound(SFX_HISS, 0.7f * vol, at, w.fx.rnd(1.0f, 1.2f)); break;
    case K_KNIGHT: w.sound(SFX_SQUAWK, 0.5f * vol, at, w.fx.rnd(1.15f, 1.3f)); break;
    case K_BISHOP: w.sound(SFX_SNORT, 0.8f * vol, at, w.fx.rnd(0.9f, 1.05f)); break;
    case K_ROOK: w.sound(SFX_BELLOW, 0.6f * vol, at, w.fx.rnd(0.7f, 0.8f)); break;
    case K_QUEEN: w.sound(SFX_GROWL, 0.8f * vol, at, w.fx.rnd(1.15f, 1.3f)); break;
    default: w.sound(SFX_GROWL, 1.0f * vol, at, w.fx.rnd(0.8f, 0.9f)); break;
  }
}
float deathPitch(int kind) {
  static const float P[6] = {1.7f, 1.9f, 0.95f, 0.6f, 1.05f, 0.85f};
  return P[std::max(0, std::min(5, kind))];
}
// a footfall: light for the raptors, the board shaking under the giants
void footfall(World& w, const Actor& a, float vol = 1.0f) {
  vec3 p = flat(a.pos);
  switch (a.kind) {
    case K_PAWN: w.sound(SFX_STEP, 0.3f * vol, p, w.fx.rnd(1.4f, 1.65f)); break;
    case K_KNIGHT: w.sound(SFX_STEP, 0.22f * vol, p, w.fx.rnd(1.7f, 1.9f)); break;
    case K_BISHOP:
      w.sound(SFX_QUAKE, 0.25f * vol, p, w.fx.rnd(1.3f, 1.45f));
      w.fx.dust(p + a.forward() * 0.15f, 2, 0.15f, 0.3f);
      break;
    case K_ROOK:
      w.sound(SFX_QUAKE, 0.55f * vol, p, w.fx.rnd(0.85f, 1.0f));
      w.fx.dust(p + a.forward() * 0.12f, 4, 0.2f, 0.45f);
      w.shake(0.05f * vol);
      break;
    case K_QUEEN: w.sound(SFX_QUAKE, 0.3f * vol, p, w.fx.rnd(1.15f, 1.3f)); break;
    default:
      w.sound(SFX_QUAKE, 0.45f * vol, p, w.fx.rnd(0.95f, 1.1f));
      w.fx.dust(p, 2, 0.15f, 0.3f);
      w.shake(0.035f * vol);
      break;
  }
}

// ---------------------------------------------------------------------------
// Fighting poses, as functions of their progress u (0..1)
// ---------------------------------------------------------------------------
// a threat display: jaws open, head low, tail up; wings half-spread; horns lowered
void threat(Actor& a, float u) {
  md::Controls& c = a.ctl;
  if (isThero(a)) {
    c.jaw = 0.55f * u;
    c.neck = -0.15f * u;
    c.headPitch = 0.12f * u;
    c.crouch = 0.22f * u;
    c.lean = 0.1f * u;
    c.itemPitch = 0.25f * u;
    c.armL = c.armR = vec3(0.35f, 0.2f, 0.15f) * u;
  } else if (isPtero(a)) {
    c.wings = 0.55f * u;
    c.jaw = 0.6f * u;
    c.neck = 0.25f * u;
    c.lean = -0.1f * u;
  } else if (a.kind == K_BISHOP) {
    c.headPitch = 0.32f * u;
    c.neck = -0.2f * u;
    c.crouch = 0.12f * u;
  } else {
    c.neck = 0.25f * u;
    c.headPitch = -0.15f * u;
    c.jaw = 0.3f * u;
  }
}
// a lunge with the jaws (or beak): drawn back (to 0.35), the snap (0.5), pulled back
void lunge(Actor& a, float u, float depth = 1.0f) {
  md::Controls& c = a.ctl;
  c.neck = kf(u, {{0.0f, -0.1f}, {0.35f, 0.3f}, {0.52f, -1.0f * depth}, {1.0f, -0.1f}});
  c.jaw = kf(u, {{0.0f, 0.4f}, {0.35f, 0.95f}, {0.48f, 0.95f}, {0.56f, 0.05f}, {1.0f, 0.3f}});
  c.lean = kf(u, {{0.0f, 0.08f}, {0.35f, -0.08f}, {0.52f, 0.25f * depth}, {1.0f, 0.08f}});
  c.headPitch = kf(u, {{0.0f, 0.1f}, {0.35f, -0.15f}, {0.52f, 0.2f}, {1.0f, 0.1f}});
  if (isPtero(a)) c.wings = 0.55f;
}
// the victim's part: a threat, and its counter-attack (whatever it is)
void victimGuard(Actor& v, float u) { threat(v, u); }
void victimStrike(Actor& v, float u) {
  if (v.kind == K_BISHOP) {  // a hook of the horns
    v.ctl.headPitch = kf(u, {{0.0f, 0.32f}, {0.35f, 0.5f}, {0.55f, -0.35f}, {1.0f, 0.32f}});
    v.ctl.headYaw = kf(u, {{0.0f, 0.0f}, {0.35f, -0.3f}, {0.55f, 0.35f}, {1.0f, 0.0f}});
  } else if (v.kind == K_ROOK) {  // a swing of the neck
    v.ctl.headYaw = kf(u, {{0.0f, 0.0f}, {0.4f, 0.5f}, {0.6f, -0.6f}, {1.0f, 0.0f}});
    v.ctl.neck = kf(u, {{0.0f, 0.25f}, {0.4f, 0.4f}, {0.6f, -0.6f}, {1.0f, 0.25f}});
  } else {
    lunge(v, u, 0.9f);
  }
}

// ---------------------------------------------------------------------------
// Falling: a dinosaur struck down staggers, its legs give way and it falls on its
// side; a body knocked flying tumbles and crashes down. The body lies in its blood
// a moment, then fades away (a puppet: the piece is gone).
// ---------------------------------------------------------------------------
int makeCorpse(World& w, Actor& v, float life) {
  int id = w.addPuppet(v.kind, v.color, 4);
  Puppet* p = w.puppet(id);
  p->ctl = v.ctl;
  p->pose = v.pose;
  p->yaw = v.yaw;
  p->scale = v.scale;
  p->tilt = v.tilt;
  vec3 piv = v.tiltPivot * v.scale;
  p->pos = v.pos + glm::angleAxis(v.yaw, vec3(0, 1, 0)) * (piv - v.tilt * piv);
  p->texOffset = v.texOffset;
  p->life = life;
  p->fadeRate = 0.9f;
  p->interior = 1.0f;
  v.alive = false;
  v.visible = false;
  return id;
}

struct DFall {
  int actor = -1, corpse = -1;
  float t = 0, side = 1;  // +1 onto its left flank, -1 its right
  float stagger = 0.4f;   // seconds of staggering first
  vec3 push{0};           // knocked back along this (world, per second)
  bool landed = false, done = false, cry = true;
  float life = 1.8f;      // seconds the body lies there before it fades
  float bleed = 1.0f;     // the size of the pool it leaves
  float settle = 0;
  bool active() const { return actor >= 0 && !done; }

  void start(World& w, Actor& v, float sideIn, const vec3& pushVel, float staggerTime = 0.4f) {
    actor = v.id;
    side = sideIn >= 0 ? 1.0f : -1.0f;
    push = flat(pushVel);
    stagger = staggerTime;
    t = 0;
    landed = done = false;
    v.idleWeight = 0;
    v.tremble = 0;
    v.yawRate = 2.0f;
    v.ctl.wings = 0;
    v.ctl.hover = 0;
    if (cry) w.sound(SFX_DINODEATH, isHuge(v) ? 1.0f : 0.8f, headOf(v), deathPitch(v.kind) * w.fx.rnd(0.94f, 1.06f));
  }

  bool update(World& w, float dt) {
    if (done || actor < 0) return true;
    Actor& v = w.actors[actor];
    if (!v.alive) {
      done = true;
      return true;
    }
    t += dt;
    v.idleWeight = 0;
    v.tremble = 0;
    md::Controls& c = v.ctl;
    if (t < stagger) {  // staggering, the head swinging, the legs buckling
      float u = t / std::max(stagger, 1e-3f);
      v.pos += push * dt * (1.0f - 0.5f * u);
      c.crouch = approach(c.crouch, 0.35f, dt * 1.5f);
      c.neck = glm::mix(c.neck, 0.25f * std::sin(t * 9.0f), std::min(1.0f, dt * 6.0f));
      c.headYaw = glm::mix(c.headYaw, 0.35f * side * std::sin(t * 5.0f), std::min(1.0f, dt * 5.0f));
      c.jaw = approach(c.jaw, 0.45f, dt * 2.0f);
      c.stride = 0.3f * (1.0f - u);
      c.gait += dt * 5.0f;
      return false;
    }
    float h = v.height() / std::max(v.scale, 1e-3f);
    float dur = 0.45f + 0.18f * h;
    float u = glm::clamp((t - stagger) / dur, 0.0f, 1.0f);
    float ang = 1.42f * u * u;  // falling faster and faster
    if (!landed && u >= 1.0f) {
      landed = true;
      settle = 0;
      vec3 p = flat(v.pos);
      float big = std::min(1.0f, h / 1.0f);
      w.sound(SFX_BODYFALL, 0.6f + 0.4f * big, p, 1.35f - 0.5f * big);
      vec3 left(std::cos(v.yaw), 0, -std::sin(v.yaw));
      w.fx.dust(p + left * side * h * 0.3f, 10 + (int)(20 * big), 0.25f + 0.4f * big, 0.6f + 0.8f * big);
      w.shake(0.08f + 0.3f * big);
      // the blood pools under the body
      vec3 body = flat(chestOf(v)) * 0.6f + p * 0.4f;
      bloodPool(w, body, (0.17f + 0.2f * big) * bleed, 1.5f, 9.0f);
    }
    if (landed) {  // a little bounce, then still
      settle += dt;
      ang = 1.42f - 0.08f * std::exp(-settle * 9.0f) * std::sin(settle * 24.0f);
    } else {
      v.pos += push * dt * (1.0f - u) * 0.6f;
    }
    v.tiltPivot = vec3(side * dn::bodyHalfWidth(v.kind), 0, 0);
    v.tilt = glm::angleAxis(-side * ang, vec3(0, 0, 1));
    c.dead = u;
    c.crouch = 0.35f * (1.0f - u);
    c.stride = 0;
    if (landed && settle > 0.45f) {
      corpse = makeCorpse(w, v, life);
      done = true;
    }
    return done;
  }
};

// A body knocked flying: it tumbles through the air, crashes down and comes to rest on
// its side (then fades).
struct DTumble {
  int actor = -1, corpse = -1;
  vec3 vel{0};
  float spin = 0, angle = 0, target = 0, t = 0, settle = 0, hmid = 0.25f;
  bool landed = false, done = false;
  float life = 1.8f, bleed = 1.0f;
  bool active() const { return actor >= 0 && !done; }
  void launch(World& w, Actor& v, const vec3& velocity, float spinRate) {
    actor = v.id;
    vel = velocity;
    spin = spinRate;
    angle = t = settle = 0;
    landed = done = false;
    v.idleWeight = 0;
    v.tremble = 0;
    v.ctl.wings = 0;
    v.ctl.hover = 0;
    hmid = v.height() / std::max(v.scale, 1e-3f) * 0.4f;
    v.tiltPivot = vec3(0, hmid, 0);
    w.sound(SFX_DINODEATH, 0.8f, headOf(v), deathPitch(v.kind) * w.fx.rnd(1.0f, 1.12f));
  }
  bool update(World& w, float dt) {
    if (done || actor < 0) return true;
    Actor& v = w.actors[actor];
    if (!v.alive) {
      done = true;
      return true;
    }
    t += dt;
    v.idleWeight = 0;
    v.tremble = 0;
    md::Controls& c = v.ctl;
    float hw = dn::bodyHalfWidth(v.kind);
    if (!landed) {
      vel.y -= 14.0f * dt;
      v.pos += vel * dt;
      angle += spin * dt;
      v.tilt = glm::angleAxis(-angle, vec3(0, 0, 1)) * glm::angleAxis(0.15f * std::sin(t * 7.0f), vec3(1, 0, 0));
      c.dead = std::min(1.0f, 0.5f + t);
      float h = hmid * v.scale;
      float low = v.pos.y + h - (std::fabs(std::cos(angle)) * h + std::fabs(std::sin(angle)) * hw * v.scale);
      if (overBoard(v.pos) && low <= 0.0f && vel.y < 0.0f && t > 0.15f) {
        landed = true;
        float k = std::round((angle - kPI * 0.5f) / kPI);  // whichever side is nearer
        target = kPI * 0.5f + k * kPI;
        vec3 p = flat(v.pos);
        float big = std::min(1.0f, v.height());
        w.sound(SFX_BODYFALL, 0.7f + 0.3f * big, p, 1.3f - 0.4f * big);
        w.fx.dust(p, 14 + (int)(16 * big), 0.3f + 0.3f * big, 1.0f + 0.6f * big);
        w.shake(0.12f + 0.25f * big);
        bloodPool(w, p, (0.17f + 0.15f * big) * bleed, 1.4f, 9.0f);
        splatter(w, p, vel, 3, 0.35f);
      }
      if (v.pos.y < -2.5f || t > 3.5f) {  // off the edge of the board, falling away
        v.alive = false;
        v.visible = false;
        done = true;
      }
      return done;
    }
    settle += dt;
    float k = std::exp(-dt * 6.0f);
    vel.x *= k;
    vel.z *= k;
    v.pos.x += vel.x * dt;
    v.pos.z += vel.z * dt;
    angle += (target - angle) * (1.0f - std::exp(-dt * 16.0f));
    v.pos.y += ((hw - hmid) * v.scale - v.pos.y) * (1.0f - std::exp(-dt * 18.0f));
    v.tilt = glm::angleAxis(-angle, vec3(0, 0, 1));
    c.dead = 1.0f;
    if (settle > 0.45f) {
      corpse = makeCorpse(w, v, life);
      done = true;
    }
    return done;
  }
};

// ---------------------------------------------------------------------------
// Walking (and running), and the pteranodon's flight
// ---------------------------------------------------------------------------
struct Walker {
  int actor = -1;
  vec3 from{0}, to{0};
  float speed = 0.8f, accel = 1.6f, cycle = 0.3f, run = 0;
  float s = 0, v = 0, len = 0, turnWait = 0;
  int lastStep = 0;
  bool begun = false, face = true, soundOn = true;

  Walker() {}
  Walker(int a, const vec3& dest, float spd) : actor(a), to(flat(dest)), speed(spd) {}

  void begin(World& w) {
    Actor& a = w.actors[actor];
    from = flat(a.pos);
    len = glm::length(to - from);
    cycle = dn::strideCycle(a.kind) * a.scale;
    accel = speed * (a.kind == K_ROOK ? 1.4f : 2.4f);
    if (face && len > 0.05f) {
      a.yawTarget = faceAngle(to - from);
      float slow = a.kind == K_ROOK ? 0.45f : (a.kind == K_PAWN ? 0.2f : 0.3f);
      turnWait = std::min(a.kind == K_ROOK ? 1.0f : 0.6f, std::fabs(wrapAngle(a.yawTarget - a.yaw)) * slow);
    }
    lastStep = (int)std::floor(a.ctl.gait / kPI);
    begun = true;
  }

  bool update(World& w, float dt) {
    if (!begun) begin(w);
    Actor& a = w.actors[actor];
    if (turnWait > 0) {  // shuffling round on the spot
      turnWait -= dt;
      a.ctl.gait += dt * 4.0f;
      a.ctl.stride = approach(a.ctl.stride, 0.25f, dt * 2.0f);
      return false;
    }
    float rem = len - s;
    float vt = std::min(speed, std::sqrt(2.0f * accel * std::max(rem, 0.0f)));
    v = v < vt ? std::min(vt, v + accel * dt) : std::max(vt, v - accel * 2.5f * dt);
    if (rem > 1e-3f && v < 0.08f) v = 0.08f;
    s = std::min(len, s + v * dt);
    vec3 p = len > 1e-4f ? glm::mix(from, to, s / len) : to;
    a.pos.x = p.x;
    a.pos.z = p.z;
    float fast = glm::clamp((v - 0.9f) / 1.0f, 0.0f, 1.0f);
    a.ctl.gait += v * dt / cycle * kTau * (1.0f - 0.35f * fast);
    a.ctl.stride = glm::clamp(v / std::max(0.1f, std::min(speed, 0.9f) * 0.6f), 0.0f, 1.0f);
    a.ctl.run = approach(a.ctl.run, std::max(run, fast), dt * 3.0f);
    int step = (int)std::floor(a.ctl.gait / kPI);
    if (step != lastStep) {
      lastStep = step;
      if (soundOn) {
        footfall(w, a, 0.8f + 0.4f * fast);
        if (fast > 0.5f) w.fx.dust(flat(a.pos), 1, 0.08f, 0.3f);
      }
    }
    if (s >= len - 1e-4f) {
      a.ctl.stride = approach(a.ctl.stride, 0.0f, dt * 4.0f);
      a.ctl.run = approach(a.ctl.run, 0.0f, dt * 3.0f);
      return a.ctl.stride <= 0.001f;
    }
    return false;
  }
};

float paceOf(const Actor& a, float dist) {
  switch (a.kind) {
    case K_PAWN: return std::max(1.4f, dist / 1.3f);
    case K_KNIGHT: return 0.6f;
    case K_ROOK: return std::max(0.55f, dist / 3.0f);
    case K_KING: return std::max(0.8f, dist / 2.3f);
    default: return std::max(0.85f, dist / 2.1f);
  }
}

// The pteranodon's flight: it crouches, springs up with a beat of its wings, flies
// there along an arc and comes down, back-beating its wings, legs forward, and folds them.
struct Soar {
  int actor = -1;
  vec3 from{0}, to{0};
  float height = 1.0f, dur = 1.0f, t = 0, rise = 0.35f, land = 0.4f;
  int lastBeat = 0;
  bool begun = false, touched = false, soundOn = true;
  Soar() {}
  Soar(int a, const vec3& dest, float h = -1.0f) : actor(a), to(flat(dest)), height(h) {}
  void begin(World& w) {
    Actor& a = w.actors[actor];
    from = flat(a.pos);
    float d = glm::length(to - from);
    if (height < 0) height = 0.45f + 0.22f * d;
    dur = 0.7f + 0.3f * d;
    if (d > 0.05f) a.yawTarget = faceAngle(to - from);
    a.yawRate = 7.0f;
    lastBeat = (int)std::floor(a.ctl.flap / kTau);
    begun = true;
    if (soundOn) {
      w.sound(SFX_FLAP, 0.6f, a.pos, w.fx.rnd(0.95f, 1.1f));
      if (w.fx.rnd() < 0.5f) w.sound(SFX_SQUAWK, 0.3f, a.pos, w.fx.rnd(1.0f, 1.2f));
    }
  }
  // seconds left before it is down
  bool update(World& w, float dt) {
    if (!begun) begin(w);
    Actor& a = w.actors[actor];
    md::Controls& c = a.ctl;
    t += dt;
    if (t < rise) {  // crouch, wings open, and spring
      float u = t / rise;
      c.crouch = 0.4f * bump(u, 0.0f, 1.0f);
      c.wings = approach(c.wings, 1.0f, dt * 5.0f);
      c.flap += dt * 10.0f;
      c.hover = 0;
      a.pos = from;
      a.pos.y = 0;
    } else if (t < rise + dur) {
      float u = (t - rise) / dur;
      vec3 p = glm::mix(from, to, easeInOut(u) * 0.8f + u * 0.2f);
      p.y = 4.0f * u * (1.0f - u) * height + 0.05f * std::sin(u * kPI);
      a.pos = p;
      c.crouch = 0;
      c.wings = 1.0f;
      c.hover = approach(c.hover, u < 0.82f ? 1.0f : 0.0f, dt * 4.0f);
      c.flap += dt * (u < 0.3f || u > 0.75f ? 15.0f : 8.0f);
      c.tuck = seg(u, 0.72f, 1.0f);
      c.lean = -0.35f * seg(u, 0.7f, 1.0f);
    } else {
      float u = std::min(1.0f, (t - rise - dur) / land);
      a.pos = to;
      a.pos.y = 0;
      if (!touched) {
        touched = true;
        if (soundOn) {
          w.sound(SFX_STEP, 0.5f, to, 1.5f);
          w.sound(SFX_FLAP, 0.4f, to, 1.15f);
        }
        w.fx.dust(to, 6, 0.3f, 0.6f);
      }
      c.hover = 0;
      c.tuck = 1.0f - u;
      c.lean = -0.35f * (1.0f - u);
      c.wings = 1.0f - smoothstepf(u);
      c.flap += dt * 6.0f * (1.0f - u);
      c.crouch = 0.3f * bump(u, 0.0f, 0.7f);
      if (u >= 1.0f) {
        c.tuck = c.lean = c.wings = c.crouch = 0;
        a.yawRate = 9.0f;
        return true;
      }
    }
    int beat = (int)std::floor(c.flap / kTau);
    if (beat != lastBeat) {
      lastBeat = beat;
      if (soundOn && t < rise + dur) w.sound(SFX_FLAP, 0.25f, a.pos, w.fx.rnd(1.0f, 1.2f));
    }
    return false;
  }
  static float smoothstepf(float u) { return u * u * (3.0f - 2.0f * u); }
};

// ---------------------------------------------------------------------------
// Quiet moves (and castling)
// ---------------------------------------------------------------------------
class DQuietMove : public Staged {
 public:
  DQuietMove(const MovePlan& p) : plan(p) {}

 protected:
  MovePlan plan;
  Walker walker, rookWalker;
  Soar soar;
  vec3 A{0}, B{0};
  bool arcSound = false;
  int lastStep = 0;

  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    switch (phase) {
      case 0:
        if (enter()) {
          A = flat(a.pos);
          B = squareCenter(plan.move.to);
          float dist = glm::length(B - A);
          if (a.kind == K_KNIGHT) soar = Soar(plan.mover, B);
          else walker = Walker(plan.mover, B, paceOf(a, dist));
          if (plan.rook >= 0) rookWalker = Walker(plan.rook, squareCenter(plan.rookTo), 0.75f);
          lastStep = (int)std::floor(a.ctl.gait / kPI);
        }
        {
          bool d1;
          if (a.kind == K_KNIGHT) {
            d1 = soar.update(w, dt);
          } else if (plan.rook >= 0) {
            // castling: the tyrant king strides round in an arc behind the brachiosaurus (on the
            // board's edge side of his rank, clear of it and of his own army)
            float u = glm::clamp(pt / 2.1f, 0.0f, 1.0f), e = easeInOut(u);
            vec3 toEdge = vec3(0, 0, a.color == 0 ? 1.0f : -1.0f);
            vec3 W = (A + B) * 0.5f + toEdge * 1.1f;
            vec3 p = (1 - e) * (1 - e) * A + 2 * (1 - e) * e * W + e * e * B;
            vec3 prev = a.pos;
            a.pos = vec3(p.x, 0, p.z);
            vec3 d = flat(a.pos - prev);
            if (glm::length(d) > 1e-5f) a.yawTarget = faceAngle(d);
            a.ctl.gait += glm::length(d) / (dn::strideCycle(a.kind) * a.scale) * kTau;
            a.ctl.stride = u < 1.0f ? 0.8f : approach(a.ctl.stride, 0.0f, dt * 4.0f);
            int stp = (int)std::floor(a.ctl.gait / kPI);
            if (stp != lastStep) {
              lastStep = stp;
              footfall(w, a, 0.8f);
            }
            if (!arcSound) {
              arcSound = true;
              warnOf(w, a, 0.6f);
            }
            d1 = u >= 1.0f && a.ctl.stride <= 0.001f;
          } else {
            d1 = walker.update(w, dt);
          }
          bool d2 = true;
          if (plan.rook >= 0) d2 = pt > 0.2f ? rookWalker.update(w, dt) : false;
          if (d1 && d2) next();
        }
        break;
      case 1:
        if (enter()) {
          a.yawTarget = a.baseYaw;
          if (plan.rook >= 0) w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw;
        }
        if (pt > 0.3f) return true;
        break;
    }
    return false;
  }
};

// The cinematic camera for a fight: it looks at the two animals from the side and
// comes a little closer.
void fightShot(World& w, const vec3& A, const vec3& V, float heightBias, float extra, int attacker, int victim) {
  if (!w.cinematic) return;
  vec3 mid = A * 0.4f + V * 0.6f;
  vec3 dir = flatDir(A, V);
  float L = glm::length(flat(V - A));
  vec3 side(-dir.z, 0, dir.x);
  vec3 orbitCam = w.cam.orbitPose().pos;
  vec3 toViewer = glm::normalize(flat(orbitCam - mid) + vec3(1e-4f));
  vec3 target = mid + vec3(0, heightBias, 0);
  float dist = 2.9f + L * 0.4f + extra;
  float baseH = 0.85f + L * 0.12f + extra * 0.3f;
  vec3 look[3] = {A + vec3(0, 0.5f, 0), V + vec3(0, 0.4f, 0), target};
  float bestScore = 1e9f;
  vec3 best = target + side * dist;
  for (int sgn = -1; sgn <= 1; sgn += 2)
    for (int k = -4; k <= 4; ++k) {
      float ang = glm::radians(15.0f * k);
      vec3 sd = side * (float)sgn;
      vec3 offDir(std::cos(ang) * sd.x - std::sin(ang) * sd.z, 0, std::sin(ang) * sd.x + std::cos(ang) * sd.z);
      for (int e = 0; e < 3; ++e) {
        vec3 cam = target + offDir * dist * (1.0f - 0.08f * e) + vec3(0, baseH + 0.7f * e, 0);
        float occ = 0;
        for (const Actor& a : w.actors) {
          if (!a.alive || !a.visible || a.id == attacker || a.id == victim) continue;
          vec3 b0 = a.pos, b1 = a.pos + vec3(0, a.height(), 0);
          float r = a.radius() * 1.05f;
          if (glm::length(flat(cam - a.pos)) < r + 0.35f && cam.y < b1.y + 0.3f) occ += 6.0f;
          for (const vec3& p : look)
            if (segSegDist(cam, p, b0, b1) < r) {
              float near = glm::length(flat(a.pos - cam)) / std::max(0.1f, glm::length(flat(p - cam)));
              occ += near < 0.5f ? 2.5f : 1.0f;
            }
        }
        float score = occ * 4.0f + (1.0f - glm::dot(offDir, toViewer)) * 1.0f + std::fabs(glm::dot(offDir, dir)) * 2.6f + e * 0.6f;
        if (score < bestScore) {
          bestScore = score;
          best = cam;
        }
      }
    }
  CamPose p;
  p.pos = best;
  p.target = target;
  p.fov = 34.0f;
  p.focus = glm::length(best - target);
  p.aperture = 7.0f;
  w.cam.setShot(p, 0.75f);
}

// ---------------------------------------------------------------------------
// Shared prelude and epilogue for the dinosaurs' battles
// ---------------------------------------------------------------------------
class DBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;
  // Staged clears 'first' after every step; the phases set themselves up with it, so it
  // is raised again for the first frame of each new phase (see the Romans)
  bool update(World& w, float dt) override {
    int before = phase;
    bool done = BattleBase::update(w, dt);
    if (phase != before) first = true;
    return done;
  }

 protected:
  Walker walker;
  Soar soar;
  DFall fall;
  DTumble tumble;

  // the face-off, with the fight camera; the victim turns to face its attacker
  bool faceOffD(World& w, float duration = 0.5f) {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    if (enter()) {
      setup(w);
      a.yawTarget = faceAngle(V - A);
      v.yawTarget = faceAngle(A - V);
      float big = std::max(a.height(), v.height());
      fightShot(w, A, V, shotHeight + 0.25f * std::max(0.0f, big - 0.6f), shotExtra + 0.6f * std::max(0.0f, big - 0.7f), plan.mover,
                plan.victim);
    }
    v.tremble = glm::clamp(pt / duration, 0.0f, 1.0f) * 0.5f;
    v.idleWeight = 0;
    return pt >= duration;
  }
  bool moveInD(World& w, float dt, float speedScale = 1.0f) {
    Actor& a = w.actors[plan.mover];
    vec3 B = squareCenter(plan.move.to);
    if (enter()) {
      float d = glm::length(flat(B - a.pos));
      if (isPtero(a) && d > 0.45f) {
        soar = Soar(plan.mover, B, 0.35f + 0.15f * d);
        soar.soundOn = false;
      } else {
        walker = Walker(plan.mover, B, paceOf(a, d) * speedScale * (isPtero(a) ? 1.0f : 0.8f));
        soar.actor = -1;
      }
    }
    return soar.actor >= 0 ? soar.update(w, dt) : walker.update(w, dt);
  }
  // walk (or run) to a point
  bool goTo(World& w, float dt, const vec3& p, float speed, float run = 0.0f) {
    if (enter()) {
      walker = Walker(plan.mover, p, speed);
      walker.run = run;
    }
    return walker.update(w, dt);
  }
  bool finishD(World& w) {
    Actor& a = w.actors[plan.mover];
    bool done = finish(w);
    if (done) {
      a.pos = squareCenter(plan.move.to);
      a.tilt = quat(1, 0, 0, 0);
      a.yawRate = 9.0f;
      a.resetControls();
    }
    return done;
  }
  void brace(World& w, float amount) {
    Actor& v = w.actors[plan.victim];
    v.tremble = amount;
    v.idleWeight = 0;
  }
  // a dying victim falls (or tumbles through the air); true once it is down
  bool victimDown(World& w, float dt) {
    bool a = fall.actor < 0 || fall.update(w, dt);
    bool b = tumble.actor < 0 || tumble.update(w, dt);
    return a && b;
  }
  bool victimDying() const { return fall.active() || tumble.active(); }
  // knocked flying (the small ones), or bowled over (the heavy)
  void knockFlying(World& w, const vec3& vel, float spin) {
    Actor& v = w.actors[plan.victim];
    if (isSmall(v) || v.kind == K_BISHOP) {
      float k = v.kind == K_BISHOP ? 0.6f : 1.0f;
      tumble.launch(w, v, vec3(vel.x * k, vel.y * k, vel.z * k), spin * k);
    } else {
      vec3 left(std::cos(v.yaw), 0, -std::sin(v.yaw));
      kill(w, glm::dot(flat(vel), left) >= 0 ? 1.0f : -1.0f, flat(vel) * 0.3f, 0.12f);
    }
  }
  // struck down: it falls on the flank the blow drives it to (or a random one)
  void kill(World& w, float side, const vec3& push, float stagger = 0.4f) {
    Actor& v = w.actors[plan.victim];
    if (side == 0) {
      vec3 left(std::cos(v.yaw), 0, -std::sin(v.yaw));
      float d = glm::dot(flat(push), left);
      side = std::fabs(d) > 1e-3f ? (d > 0 ? 1.0f : -1.0f) : (w.fx.rnd() < 0.5f ? -1.0f : 1.0f);
    }
    fall.start(w, v, side, push, stagger);
  }
  // the victor's call: head up, jaws wide (wings spread, a rear)
  void triumph(World& w, float u) {
    Actor& a = w.actors[plan.mover];
    float e = std::min(1.0f, u * 3.0f) * (1.0f - seg(u, 0.75f, 1.0f));
    md::Controls& c = a.ctl;
    c.jaw = 0.95f * e;
    c.neck = 0.45f * e;
    c.headPitch = -0.2f * e;
    c.crouch = approach(c.crouch, 0.0f, 0.05f);
    if (isThero(a)) {
      c.lean = -0.15f * e;
      c.itemPitch = 0.2f * e;
      c.armL = c.armR = vec3(0.4f, 0.25f, 0.0f) * e;
    }
    if (isPtero(a)) {
      c.wings = e;
      c.flap += 0.08f;
    }
    if (a.kind == K_BISHOP) c.rear = 0.35f * e;
    if (a.kind == K_ROOK) c.rear = 0.25f * e;
  }
  // back to rest after the fight, eased (the controls a battle may have left)
  void relax(Actor& a, float dt, float rate = 3.0f) {
    md::Controls& c = a.ctl;
    float k = dt * rate;
    c.jaw = approach(c.jaw, 0.0f, k);
    c.neck = approach(c.neck, 0.0f, k);
    c.headPitch = approach(c.headPitch, 0.0f, k);
    c.headYaw = approach(c.headYaw, 0.0f, k);
    c.lean = approach(c.lean, 0.0f, k);
    c.crouch = approach(c.crouch, 0.0f, k);
    c.twist = approach(c.twist, 0.0f, k);
    c.itemYaw = approach(c.itemYaw, 0.0f, k);
    c.itemPitch = approach(c.itemPitch, 0.0f, k);
    c.kick = approach(c.kick, 0.0f, k);
    c.tuck = approach(c.tuck, 0.0f, k);
    c.rear = approach(c.rear, 0.0f, k);
    c.wings = approach(c.wings, 0.0f, k);
    c.armL = glm::mix(c.armL, vec3(0), std::min(1.0f, k));
    c.armR = glm::mix(c.armR, vec3(0), std::min(1.0f, k));
  }
};

// a puppet placed by a world matrix
void setPuppetMatrix(Puppet& p, const glm::mat4& M) {
  float sc = glm::length(vec3(M[0]));
  glm::mat3 R(vec3(M[0]) / sc, vec3(M[1]) / sc, vec3(M[2]) / sc);
  p.pos = vec3(M[3]);
  p.yaw = 0;
  p.tilt = glm::normalize(glm::quat_cast(R));
  p.scale = sc;
}

// A raptor of the pack (a puppet) running somewhere: true once it is there
bool runPuppet(World& w, Puppet& p, const vec3& target, float speed, float dt) {
  vec3 d = flat(target - p.pos);
  float l = glm::length(d);
  float stepLen = std::min(l, speed * dt);
  if (l > 1e-4f) {
    p.pos += d / l * stepLen;
    p.yaw = wrapAngle(p.yaw + glm::clamp(wrapAngle(faceAngle(d) - p.yaw), -dt * 8.0f, dt * 8.0f));
  }
  float before = std::floor(p.ctl.gait / kPI);
  p.ctl.gait += stepLen / (dn::strideCycle(K_PAWN) * p.scale) * kTau * 0.7f;
  if (std::floor(p.ctl.gait / kPI) != before) w.sound(SFX_STEP, 0.2f, p.pos, w.fx.rnd(1.45f, 1.7f));
  p.ctl.stride = approach(p.ctl.stride, stepLen > 1e-4f ? 1.0f : 0.0f, dt * 5.0f);
  p.ctl.run = approach(p.ctl.run, speed > 1.4f ? 1.0f : 0.0f, dt * 3.0f);
  if (speed > 1.4f && w.fx.rnd() < dt * 8.0f) w.fx.dust(p.pos, 1, 0.08f, 0.3f);
  return l < 0.02f;
}

}  // namespace

namespace {

// ===========================================================================
// PAWN - the velociraptor
// ===========================================================================
// Killing claw: a hiss, a sprint and a leap; it lands on the victim feet first and
// clings there, kicking with its sickle claws, until the victim goes down.
class DRaptorClaw : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  vec3 leap0{0}, perch{0}, offset{0}, landAt{0};
  int slashes = 0;
  bool growled = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.4f;
        if (faceOffD(w)) next();
        break;
      case 1: {  // crouched, hissing, the tail lashing; the victim answers
        if (first) warnOf(w, a);
        float u = seg(pt, 0.0f, 0.6f);
        threat(a, u);
        a.ctl.crouch = 0.35f * u;
        a.ctl.itemYaw = 0.3f * std::sin(pt * 14.0f) * u;
        victimGuard(v, seg(pt, 0.2f, 0.8f));
        if (!growled && pt > 0.35f) {
          growled = true;
          warnOf(w, v, 0.8f);
        }
        brace(w, 0.6f);
        if (pt > 0.85f) next();
        break;
      }
      case 2:  // the sprint
        if (first) {
          float leapFrom = biteDist(a, v) + 0.55f;
          walker = Walker(plan.mover, V - dir * std::min(leapFrom, std::max(0.05f, L - 0.05f)), std::max(2.2f, L / 1.0f));
          walker.run = 1.0f;
          callOf(w, a, 0.8f);
        }
        a.ctl.jaw = 0.6f;
        a.ctl.itemYaw = approach(a.ctl.itemYaw, 0.0f, dt * 3.0f);
        a.ctl.crouch = approach(a.ctl.crouch, 0.0f, dt * 3.0f);
        victimGuard(v, 1.0f);
        brace(w, 0.8f);
        if (walker.update(w, dt) || walker.s > walker.len - 0.03f) next();
        break;
      case 3: {  // the leap: feet first, claws out, arms spread
        const float dur = 0.42f;
        if (first) {
          leap0 = a.pos;
          vec3 target = neckOf(v);
          perch = flat(target) - dir * (0.13f + halfWidthOf(v) * 0.6f);
          perch.y = std::max(0.0f, target.y - 0.22f * a.scale);
          offset = perch - v.pos;
          a.yawTarget = faceAngle(dir);
          w.sound(SFX_WHOOSH, 0.6f, a.pos, 1.3f);
        }
        float u = std::min(1.0f, pt / dur);
        a.pos = glm::mix(leap0, perch, easeOut(u));
        a.pos.y += 4.0f * u * (1.0f - u) * 0.22f;
        a.ctl.tuck = std::max(a.ctl.tuck, smoothstep01(seg(u, 0.0f, 0.5f)));
        a.ctl.jaw = 0.85f;
        a.ctl.lean = -0.25f * u;
        v.ctl.headPitch = approach(v.ctl.headPitch, -0.3f, dt * 3.0f);
        brace(w, 1.0f);
        if (u >= 1.0f) {
          w.sound(SFX_THUD, 0.6f, a.pos, 1.4f);
          feathers(w, v, neckOf(v), -dir, 10);
          feathers(w, a, a.pos + vec3(0, 0.25f, 0), dir, 6);
          v.pos += dir * 0.04f;
          callOf(w, v, 0.9f);
          next();
        }
        break;
      }
      case 4: {  // clinging on, kicking: three slashes of the sickle claws, a bite
        a.pos = v.pos + offset;
        a.ctl.tuck = 0.75f + 0.1f * std::sin(pt * 9.0f);
        float k = std::fmod(pt, 0.36f) / 0.36f;
        a.ctl.kick = 0.6f * bump(k, 0.0f, 0.7f);
        a.ctl.neck = -0.35f + 0.25f * std::sin(pt * 12.0f);
        a.ctl.jaw = 0.5f + 0.4f * std::sin(pt * 12.0f + 1.0f);
        a.ctl.lean = -0.2f;
        // the victim thrashes, roaring
        v.ctl.headYaw = 0.55f * std::sin(pt * 9.0f);
        v.ctl.neck = 0.3f * std::sin(pt * 7.0f) + 0.2f;
        v.ctl.jaw = 0.8f;
        v.ctl.crouch = 0.15f * std::fabs(std::sin(pt * 6.0f));
        v.tremble = 0.4f;
        if (slashes < 3 && pt > 0.25f + 0.33f * slashes) {
          vec3 c = neckOf(v) * 0.5f + chestOf(v) * 0.5f;
          vec3 side(-dir.z, 0, dir.x);
          wound(w, c, dir + side * (slashes % 2 ? 0.6f : -0.6f) + vec3(0, 0.4f, 0), slashes == 2 ? 0.9f : 0.55f, W_CLAW);
          feathers(w, v, c, -dir, 6);
          w.shake(0.08f);
          ++slashes;
        }
        if (pt > 1.2f) next();
        break;
      }
      case 5: {  // the victim goes down; the raptor springs clear
        if (first) {
          leap0 = a.pos;
          landAt = clampBoard(flat(a.pos) - dir * 0.3f, 4.0f);
          kill(w, 0.0f, dir * 0.25f, 0.15f);
          w.slowmo(0.35f, 0.5f);
          w.shake(0.15f);
        }
        float u = std::min(1.0f, pt / 0.42f);
        a.pos = glm::mix(leap0, landAt, u);
        a.pos.y = glm::mix(leap0.y, 0.0f, u) + 4.0f * u * (1.0f - u) * 0.15f;
        a.ctl.tuck = 1.0f - u;
        a.ctl.kick = 0;
        a.ctl.lean = 0;
        victimDown(w, dt);
        if (u >= 1.0f) {
          a.pos = landAt;
          w.sound(SFX_STEP, 0.4f, landAt, 1.5f);
          next();
        }
        break;
      }
      case 6:
        relax(a, dt);
        threat(a, 0.4f);
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      case 7:
        if (first) callOf(w, a);
        triumph(w, pt / 1.0f);
        if (pt > 1.0f) next();
        break;
      case 8:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 9:
        return finishD(w);
    }
    return false;
  }
  static float smoothstep01(float x) { return x * x * (3.0f - 2.0f * x); }
};

// Pack attack: it calls, and two more raptors come running from the edges of the board;
// the victim turns on one of them, and the others are on it.
class DRaptorPack : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  int mates[2] = {-1, -1};
  vec3 from[2], flank[2], lunge0{0};
  bool answered = false, snapped = false, bit = false, kicked = false;
  float feedT = 0;
  Puppet* M(World& w, int i) { return w.puppet(mates[i]); }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 side(-dir.z, 0, dir.x);
    switch (phase) {
      case 0:
        shotExtra = 1.1f;
        shotHeight = 0.35f;
        if (faceOffD(w)) next();
        break;
      case 1: {  // the call, and the answers from the edges of the board
        if (first) {
          callOf(w, a);
          for (int i = 0; i < 2; ++i) {
            float s = i == 0 ? 1.0f : -1.0f;
            vec3 f = clampBoard(V + side * s * 3.0f + dir * 0.4f, 4.2f);
            if (glm::length(flat(f - V)) < 1.6f) f = clampBoard(V - dir * 2.6f + side * s * 1.3f, 4.2f);
            from[i] = f;
            flank[i] = clampBoard(V + side * s * (halfWidthOf(v) + 0.36f) - dir * 0.06f, 3.9f);
            mates[i] = w.addPuppet(dn::D_PAWN, a.color, 4);
            if (Puppet* p = M(w, i)) {
              p->pos = f;
              p->yaw = faceAngle(V - f);
              p->dissolve = 1.0f;
              p->texOffset = a.texOffset + vec3(1.7f + i, 0.6f * i, 2.3f);
            }
          }
        }
        if (!answered && pt > 0.35f) {
          answered = true;
          w.sound(SFX_SCREECH, 0.65f, from[0], 1.12f);
          w.sound(SFX_SCREECH, 0.6f, from[1], 0.97f);
        }
        for (int i = 0; i < 2; ++i)
          if (Puppet* p = M(w, i)) p->dissolve = std::max(0.0f, p->dissolve - dt * 2.5f);
        threat(a, seg(pt, 0.0f, 0.5f));
        victimGuard(v, seg(pt, 0.2f, 0.7f));
        v.ctl.headYaw = 0.6f * std::sin(pt * 4.0f);
        brace(w, 0.8f);
        if (pt > 0.8f) next();
        break;
      }
      case 2: {  // the pack closes in
        if (first) walker = Walker(plan.mover, V - dir * (biteDist(a, v) + 0.12f), 1.4f);
        bool d0 = walker.update(w, dt), d1 = true;
        for (int i = 0; i < 2; ++i)
          if (Puppet* p = M(w, i)) {
            p->dissolve = std::max(0.0f, p->dissolve - dt * 2.5f);
            bool there = runPuppet(w, *p, flank[i], 2.6f, dt);
            if (there) {
              p->yaw = wrapAngle(p->yaw + glm::clamp(wrapAngle(faceAngle(V - p->pos) - p->yaw), -dt * 8.0f, dt * 8.0f));
              p->ctl.stride = approach(p->ctl.stride, 0.0f, dt * 4.0f);
              p->ctl.jaw = 0.5f;
              p->ctl.crouch = 0.25f;
            }
            d1 = d1 && there;
          }
        threat(a, 1.0f);
        v.ctl.headYaw = 0.55f * std::sin(pt * 5.0f);
        brace(w, 1.0f);
        if ((d0 && d1) || pt > 3.0f) next();
        break;
      }
      case 3: {  // the victim turns on the first of them, which springs back from its jaws
        if (first) {
          v.yawTarget = faceAngle(flank[0] - V);
          v.yawRate = 6.0f;
        }
        float u = seg(pt, 0.1f, 0.85f);
        victimStrike(v, u);
        if (Puppet* p = M(w, 0)) {
          float hop = seg(pt, 0.4f, 0.65f);
          vec3 away = flatDir(V, flank[0]);
          p->pos = flank[0] + away * 0.22f * easeOut(hop);
          p->pos.y = 4.0f * hop * (1.0f - hop) * 0.1f;
          p->ctl.jaw = 0.7f;
          p->ctl.tuck = 0.3f * bump(hop, 0.0f, 1.0f);
        }
        if (!snapped && u > 0.5f) {
          snapped = true;
          w.sound(SFX_SNAP, 0.8f, headOf(v), 1.0f);
          if (Puppet* p = M(w, 0)) w.sound(SFX_HISS, 0.5f, p->pos, 1.2f);
        }
        threat(a, 1.0f);
        brace(w, 0.5f);
        if (pt > 0.9f) next();
        break;
      }
      case 4: {  // and the others are on it: a bite at the neck, a kick of the sickle claw
        if (first) {
          lunge0 = a.pos;
          w.sound(SFX_SCREECH, 0.7f, a.pos, 1.18f);
        }
        float u = seg(pt, 0.0f, 0.55f);
        lunge(a, u, 1.1f);
        a.pos = lunge0 + dir * 0.12f * bump(u, 0.2f, 1.0f);
        a.ctl.tuck = 0.3f * bump(u, 0.2f, 0.8f);
        if (Puppet* p = M(w, 1)) {
          p->ctl.kick = bump(seg(pt, 0.1f, 0.5f), 0.0f, 1.0f);
          p->ctl.lean = 0.15f;
          p->ctl.jaw = 0.8f;
        }
        if (Puppet* p = M(w, 0)) {  // back in, at the tail
          p->pos = glm::mix(p->pos, flank[0], std::min(1.0f, dt * 6.0f));
          p->ctl.neck = -0.5f * bump(pt, 0.2f, 0.6f);
          p->ctl.jaw = 0.5f + 0.4f * std::sin(pt * 14.0f);
        }
        if (!bit && pt > 0.28f) {
          bit = true;
          wound(w, neckOf(v), dir + vec3(0, 0.3f, 0), 1.0f, W_BITE);
          feathers(w, v, neckOf(v), dir, 10);
        }
        if (!kicked && pt > 0.36f) {
          kicked = true;
          wound(w, chestOf(v), side * -1.0f + vec3(0, 0.3f, 0), 0.7f, W_CLAW);
          kill(w, 0.0f, -side * 0.15f + dir * 0.1f, 0.25f);
          w.slowmo(0.35f, 0.5f);
          w.shake(0.15f);
        }
        if (kicked) victimDown(w, dt);
        if (pt > 0.65f) next();
        break;
      }
      case 5: {  // the pack feeds a moment
        bool down = victimDown(w, dt);
        feedT += dt;
        vec3 body = v.pos;
        if (Puppet* cp = w.puppet(fall.corpse)) body = cp->pos;
        float k = std::sin(pt * 9.0f);
        a.ctl.neck = -0.55f + 0.25f * k;
        a.ctl.headPitch = 0.35f;
        a.ctl.jaw = 0.5f + 0.4f * std::sin(pt * 13.0f);
        a.ctl.lean = 0.2f;
        for (int i = 0; i < 2; ++i)
          if (Puppet* p = M(w, i)) {
            p->ctl.kick = approach(p->ctl.kick, 0.0f, dt * 3.0f);
            p->ctl.neck = -0.55f + 0.25f * std::sin(pt * 8.0f + 1.5f * (i + 1));
            p->ctl.headPitch = 0.35f;
            p->ctl.jaw = 0.5f + 0.4f * std::sin(pt * 12.0f + i);
            p->ctl.lean = 0.2f;
          }
        if (std::fmod(pt, 0.45f) < dt) {
          bloodSpray(w, flat(body) + vec3(w.fx.rnd(-0.1f, 0.1f), 0.08f, w.fx.rnd(-0.1f, 0.1f)), vec3(0, 1, 0), 12, 1.4f);
          w.sound(SFX_TEAR, 0.4f, body, w.fx.rnd(1.1f, 1.3f));
        }
        if (down && pt > 1.4f) next();
        break;
      }
      case 6: {  // the pack runs off; the hunter calls after them
        if (first) callOf(w, a);
        triumph(w, pt / 1.0f);
        bool gone = true;
        for (int i = 0; i < 2; ++i)
          if (Puppet* p = M(w, i)) {
            p->ctl.neck = approach(p->ctl.neck, 0.0f, dt * 3.0f);
            p->ctl.headPitch = approach(p->ctl.headPitch, 0.0f, dt * 3.0f);
            p->ctl.jaw = approach(p->ctl.jaw, 0.0f, dt * 3.0f);
            p->ctl.lean = approach(p->ctl.lean, 0.0f, dt * 3.0f);
            if (pt > 0.25f) runPuppet(w, *p, from[i], 2.4f, dt);
            if (glm::length(flat(p->pos - from[i])) < 0.9f || pt > 1.6f) p->dissolve = std::min(1.0f, p->dissolve + dt * 2.5f);
            if (p->dissolve >= 1.0f) p->alive = false;
            gone = false;
          }
        if (gone || pt > 2.6f) {
          for (int i = 0; i < 2; ++i)
            if (Puppet* p = M(w, i)) p->alive = false;
          next();
        }
        break;
      }
      case 7:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 8:
        return finishD(w);
    }
    return false;
  }
};

// Dodge and bite: it circles, hissing; the victim lunges and it slips aside, then darts
// in and seizes the throat, hanging on as the victim thrashes, until it rips it open.
class DRaptorDodge : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  float R = 0.8f, th0 = 0, th1 = 0;
  vec3 dodge0{0}, dodgeTo{0}, seize{0}, hold{0};
  bool snapped = false, bit = false;
  float ripT = -1;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.5f;
        if (faceOffD(w)) next();
        break;
      case 1:  // in, to circling distance
        if (first) {
          R = biteDist(a, v) + 0.32f;
          walker = Walker(plan.mover, V - dir * R, 1.3f);
          warnOf(w, a);
        }
        threat(a, seg(pt, 0.0f, 0.4f));
        victimGuard(v, seg(pt, 0.0f, 0.6f));
        brace(w, 0.6f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // circling, the victim turning to keep it in front of it
        if (first) {
          th0 = faceAngle(flat(a.pos) - V);
          th1 = th0 + (w.fx.rnd() < 0.5f ? -1.0f : 1.0f) * 0.9f;
        }
        float u = seg(pt, 0.0f, 1.3f);
        float th = glm::mix(th0, th1, easeInOut(u));
        vec3 prev = a.pos;
        a.pos = V + vec3(std::sin(th), 0, std::cos(th)) * R;
        a.yawTarget = faceAngle(V - a.pos);
        a.ctl.gait += glm::length(flat(a.pos - prev)) / (dn::strideCycle(a.kind) * a.scale) * kTau;
        a.ctl.stride = u < 1.0f ? 0.6f : approach(a.ctl.stride, 0.0f, dt * 4.0f);
        threat(a, 1.0f);
        v.yawTarget = faceAngle(a.pos - V);
        v.yawRate = 4.0f;
        victimGuard(v, 1.0f);
        brace(w, 0.7f);
        if (pt > 1.4f) next();
        break;
      }
      case 3: {  // the victim lunges; the raptor springs aside and the jaws close on air
        if (first) {
          dodge0 = a.pos;
          vec3 d = flatDir(V, a.pos);
          vec3 side(-d.z, 0, d.x);
          dodgeTo = clampBoard(dodge0 + side * 0.3f + d * 0.05f, 4.0f);
          warnOf(w, v);
        }
        float u = seg(pt, 0.0f, 0.8f);
        victimStrike(v, u);
        v.pos += flatDir(V, dodge0) * dt * 0.25f * bump(u, 0.3f, 0.6f);  // it overreaches
        float hop = seg(pt, 0.22f, 0.45f);
        a.pos = glm::mix(dodge0, dodgeTo, easeOut(hop));
        a.pos.y = 4.0f * hop * (1.0f - hop) * 0.12f;
        a.ctl.tuck = 0.4f * bump(hop, 0.0f, 1.0f);
        a.yawTarget = faceAngle(V - a.pos);
        if (!snapped && u > 0.5f) {
          snapped = true;
          w.sound(SFX_SNAP, 0.9f, headOf(v), 0.95f);
          w.sound(SFX_WHOOSH, 0.4f, headOf(v), 0.8f);
        }
        if (pt > 0.85f) next();
        break;
      }
      case 4: {  // the counter: in from the side and on the throat
        if (first) {
          dodge0 = a.pos;
          vec3 n = neckOf(v);
          vec3 d = flatDir(a.pos, n);
          seize = flat(n) - d * reachOf(a) * 0.95f;
          a.yawTarget = faceAngle(d);
          a.yawRate = 14.0f;
        }
        float u = seg(pt, 0.0f, 0.32f);
        a.pos = glm::mix(dodge0, seize, easeIn(u));
        lunge(a, 0.3f + 0.25f * u, 1.0f);
        if (!bit && u >= 1.0f) {
          bit = true;
          wound(w, neckOf(v), flatDir(a.pos, V) + vec3(0, 0.4f, 0), 0.8f, W_BITE);
          feathers(w, v, neckOf(v), flatDir(a.pos, V), 8);
          callOf(w, v, 0.9f);
          hold = a.pos - v.pos;
          next();
        }
        break;
      }
      case 5: {  // hanging on by its jaws as the victim thrashes; then it rips
        float sh = std::sin(pt * 13.0f);
        v.ctl.headYaw = 0.6f * std::sin(pt * 7.0f);
        v.ctl.neck = 0.35f + 0.2f * sh;
        v.ctl.jaw = 0.8f;
        v.tremble = 0.5f;
        v.pos += flatDir(a.pos, v.pos) * dt * 0.05f * std::sin(pt * 6.0f);
        a.pos = v.pos + hold + flatDir(v.pos, a.pos) * 0.03f * sh;
        a.ctl.jaw = 0.1f;
        a.ctl.neck = -0.45f + 0.1f * sh;
        a.ctl.headYaw = 0.4f * std::sin(pt * 16.0f);
        a.ctl.crouch = 0.3f;
        a.ctl.lean = 0.15f;
        a.ctl.tuck = 0;
        if (std::fmod(pt, 0.3f) < dt) bloodSpray(w, neckOf(v), vec3(0, 0.5f, 0) + flatDir(a.pos, v.pos), 14, 1.3f);
        if (pt > 1.1f && ripT < 0) {
          ripT = pt;
          wound(w, neckOf(v), flatDir(v.pos, a.pos) + vec3(0, 0.6f, 0), 1.1f, W_CLAW);
          kill(w, 0.0f, flatDir(a.pos, v.pos) * 0.2f, 0.3f);
          w.slowmo(0.35f, 0.5f);
          w.shake(0.12f);
        }
        if (ripT >= 0) {
          float r = seg(pt - ripT, 0.0f, 0.3f);
          a.ctl.neck = glm::mix(-0.45f, 0.4f, r);
          a.ctl.headYaw = 0;
          a.pos -= flatDir(a.pos, v.pos) * dt * 0.4f * (1.0f - r);
          victimDown(w, dt);
          if (pt - ripT > 0.35f) next();
        }
        break;
      }
      case 6:
        relax(a, dt);
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      case 7:
        if (first) {
          callOf(w, a);
          a.yawRate = 9.0f;
        }
        triumph(w, pt / 1.0f);
        if (pt > 1.0f) next();
        break;
      case 8:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 9:
        return finishD(w);
    }
    return false;
  }
};

// ===========================================================================
// KNIGHT - the pteranodon
// ===========================================================================
// Flying, controls eased towards a pose in the air (the wings beating)
void airborne(Actor& a, float dt, float beat, float lean = 0.0f) {
  md::Controls& c = a.ctl;
  c.wings = approach(c.wings, 1.0f, dt * 5.0f);
  c.hover = approach(c.hover, 1.0f, dt * 4.0f);
  c.flap += dt * beat;
  c.lean = approach(c.lean, lean, dt * 3.0f);
  c.crouch = 0;
}

// A flight through the air from one point to another (heights included)
struct Glide {
  vec3 p0{0}, p1{0};
  float dur = 1.0f, arc = 0.0f, t = 0;
  void set(const vec3& a, const vec3& b, float d, float h = 0.0f) {
    p0 = a;
    p1 = b;
    dur = d;
    arc = h;
    t = 0;
  }
  bool update(Actor& a, float dt, bool face = true) {
    t += dt;
    float u = glm::clamp(t / dur, 0.0f, 1.0f), e = easeInOut(u);
    vec3 p = glm::mix(p0, p1, e);
    p.y += 4.0f * e * (1.0f - e) * arc;
    if (face && glm::length(flat(p - a.pos)) > 1e-4f) a.yawTarget = faceAngle(p - a.pos);
    a.pos = p;
    return u >= 1.0f;
  }
};

// Dive: it takes off, climbs and circles, then folds its wings and stoops on the victim,
// beak first; it pulls out of the dive and lands beyond.
class DPteroDive : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  Glide g;
  vec3 offset{0}, hitAt{0}, flightDir{0}, landAt{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 side(-dir.z, 0, dir.x);
    switch (phase) {
      case 0:
        shotExtra = 1.4f;
        shotHeight = 0.9f;
        if (faceOffD(w)) next();
        break;
      case 1:  // take off and climb
        if (first) {
          g.set(a.pos, A - dir * 0.3f + side * 0.3f + vec3(0, 1.5f, 0), 1.0f);
          callOf(w, a);
          w.sound(SFX_FLAP, 0.8f, a.pos, 0.95f);
          w.fx.dust(A, 10, 0.4f, 0.9f);
        }
        airborne(a, dt, 15.0f, -0.2f);
        g.update(a, dt, false);
        a.yawTarget = faceAngle(V - a.pos);
        victimGuard(v, seg(pt, 0.2f, 0.8f));
        v.ctl.headPitch = approach(v.ctl.headPitch, -0.35f, dt * 2.0f);
        brace(w, 0.6f);
        if (pt > 1.0f) next();
        break;
      case 2:  // round to the stooping point, high and behind
        if (first) g.set(a.pos, V - dir * 1.2f - side * 0.35f + vec3(0, 1.9f, 0), 0.9f, 0.15f);
        airborne(a, dt, 9.0f, 0.0f);
        g.update(a, dt);
        if (pt > 0.6f) {  // folding for the dive
          a.yawTarget = faceAngle(V - a.pos);
          a.ctl.wings = approach(a.ctl.wings, 0.35f, dt * 3.0f);
          a.ctl.lean = approach(a.ctl.lean, 0.8f, dt * 3.0f);
          a.ctl.neck = approach(a.ctl.neck, -0.5f, dt * 3.0f);
        }
        v.ctl.headPitch = approach(v.ctl.headPitch, -0.5f, dt * 2.0f);
        brace(w, 0.8f);
        if (pt > 0.95f) next();
        break;
      case 3: {  // the stoop
        const float dur = 0.45f;
        if (first) {
          offset = mouthOf(a) - a.pos;
          hitAt = neckOf(v) * 0.5f + headOf(v) * 0.5f;
          g.set(a.pos, hitAt - offset, dur);
          flightDir = glm::normalize(g.p1 - g.p0);
          w.sound(SFX_WHOOSH, 0.9f, a.pos, 0.7f);
          w.sound(SFX_SQUAWK, 0.5f, a.pos, 1.2f);
        }
        a.ctl.wings = 0.3f;
        a.ctl.hover = 1.0f;
        a.ctl.lean = 0.8f;
        a.ctl.neck = -0.5f;
        a.ctl.jaw = 0;
        float u = std::min(1.0f, pt / dur);
        a.pos = glm::mix(g.p0, g.p1, u * u);
        a.yawTarget = faceAngle(flightDir);
        brace(w, 1.0f);
        if (u >= 1.0f) {
          wound(w, hitAt, flightDir + vec3(0, 0.3f, 0), 1.1f, W_GORE);
          feathers(w, v, hitAt, flightDir, 12);
          kill(w, 0.0f, flat(flightDir) * 0.5f, 0.12f);
          w.slowmo(0.3f, 0.5f);
          w.shake(0.3f);
          next();
        }
        break;
      }
      case 4:  // pulling out, wings beating hard
        if (first) g.set(a.pos, flat(a.pos) + dir * 0.5f + vec3(0, 0.9f, 0), 0.55f);
        airborne(a, dt, 16.0f, -0.3f);
        a.ctl.neck = approach(a.ctl.neck, 0.0f, dt * 4.0f);
        g.update(a, dt, false);
        victimDown(w, dt);
        if (pt > 0.55f) next();
        break;
      case 5: {  // and down beyond the victim, legs forward, wings back-beating, folded
        if (first) {
          landAt = clampBoard(V + dir * 0.6f, 4.0f);
          g.set(a.pos, landAt, 0.75f, 0.1f);
          a.yawTarget = faceAngle(-dir);
        }
        bool down = g.update(a, dt, false);
        float u = seg(pt, 0.0f, 0.75f);
        a.ctl.flap += dt * (u > 0.6f ? 14.0f : 7.0f);
        a.ctl.wings = 1.0f;
        a.ctl.hover = 1.0f - seg(u, 0.55f, 1.0f);
        a.ctl.tuck = seg(u, 0.5f, 0.9f);
        a.ctl.lean = -0.35f * seg(u, 0.5f, 1.0f);
        victimDown(w, dt);
        if (down) {
          a.pos = landAt;
          w.sound(SFX_STEP, 0.5f, landAt, 1.5f);
          w.sound(SFX_FLAP, 0.5f, landAt, 1.1f);
          w.fx.dust(landAt, 8, 0.3f, 0.6f);
          next();
        }
        break;
      }
      case 6: {
        float u = seg(pt, 0.0f, 0.4f);
        a.ctl.tuck = 1.0f - u;
        a.ctl.lean = -0.35f * (1.0f - u);
        a.ctl.wings = 1.0f - u;
        a.ctl.hover = 0;
        if (victimDown(w, dt) && pt > 0.45f) next();
        break;
      }
      case 7:
        if (first) callOf(w, a);
        triumph(w, pt / 1.0f);
        if (pt > 1.0f) next();
        break;
      case 8:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 9:
        return finishD(w);
    }
    return false;
  }
};

// Wing buffet: it flies up to the victim and rears, mantling its wings; it beats them in
// the victim's face, knocks its jaws aside with a wing, and stabs - three times.
class DPteroBuffet : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  int stabs = 0;
  bool buffeted = false;
  vec3 stand{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.7f;
        shotHeight = 0.5f;
        if (faceOffD(w)) next();
        break;
      case 1:  // a hop through the air, to land in front of it
        if (first) {
          stand = V - dir * (reachOf(a) * 0.75f + reachOf(v) * 0.45f + halfWidthOf(v) + 0.1f);
          soar = Soar(plan.mover, stand, 0.45f);
        }
        victimGuard(v, seg(pt, 0.2f, 0.8f));
        brace(w, 0.7f);
        if (soar.update(w, dt)) next();
        break;
      case 2: {  // mantling: up tall, wings spread, beating them at the victim
        if (first) callOf(w, a);
        float u = seg(pt, 0.0f, 0.4f);
        a.ctl.wings = u;
        a.ctl.lean = -0.45f * u;
        a.ctl.neck = 0.35f * u;
        a.ctl.jaw = 0.8f * u;
        if (pt > 0.35f) {
          float before = std::floor(a.ctl.flap / kTau);
          a.ctl.flap += dt * 11.0f;
          if (std::floor(a.ctl.flap / kTau) != before) {
            w.sound(SFX_FLAP, 0.7f, a.pos, w.fx.rnd(0.9f, 1.0f));
            vec3 g = flat(V) - dir * 0.15f;
            for (int i = 0; i < 6; ++i) {  // the downdraught blowing dust over the victim
              Particle p;
              p.pos = g + vec3(w.fx.rnd(-0.25f, 0.25f), w.fx.rnd(0.0f, 0.1f), w.fx.rnd(-0.25f, 0.25f));
              p.vel = dir * w.fx.rnd(1.0f, 2.0f) + vec3(0, w.fx.rnd(0.1f, 0.5f), 0);
              p.size = w.fx.rnd(0.05f, 0.1f);
              p.grow = 0.3f;
              p.color = vec3(0.55f, 0.5f, 0.45f);
              p.colorEnd = p.color;
              p.opacity = w.fx.rnd(0.15f, 0.3f);
              p.maxLife = w.fx.rnd(0.6f, 1.0f);
              p.type = PT_SMOKE;
              p.drag = 2.5f;
              p.rot = w.fx.rnd(0.0f, 6.28f);
              p.seed = w.fx.rnd();
              p.fadeIn = 0.05f;
              w.fx.add(p);
            }
          }
        }
        // the victim flinches back from the wind of the wings
        v.ctl.crouch = 0.3f * seg(pt, 0.4f, 0.8f);
        v.ctl.headPitch = -0.3f * seg(pt, 0.4f, 0.8f);
        v.ctl.headYaw = 0.25f * std::sin(pt * 6.0f) * seg(pt, 0.4f, 0.8f);
        if (pt > 0.4f && pt < 1.0f) v.pos += dir * dt * 0.08f;
        brace(w, 1.0f);
        if (pt > 1.3f) next();
        break;
      }
      case 3: {  // the victim strikes back: a wing slams its jaws aside
        float u = seg(pt, 0.0f, 0.8f);
        victimStrike(v, u);
        a.ctl.flap = glm::mix(a.ctl.flap, std::ceil(a.ctl.flap / kTau) * kTau + kPI * 0.5f, std::min(1.0f, dt * 5.0f));
        if (!buffeted && u > 0.45f) {
          buffeted = true;
          w.sound(SFX_FLAP, 1.0f, headOf(v), 0.8f);
          w.sound(SFX_THUD, 0.6f, headOf(v), 1.3f);
          w.shake(0.1f);
        }
        if (buffeted) {
          v.ctl.headYaw = approach(v.ctl.headYaw, 0.8f, dt * 6.0f);
          v.ctl.neck = approach(v.ctl.neck, 0.4f, dt * 4.0f);
        }
        if (pt > 0.85f) next();
        break;
      }
      case 4: {  // three stabs of the beak
        float k = std::fmod(pt, 0.4f) / 0.4f;
        lunge(a, k, 1.15f);
        a.ctl.jaw = 0.05f;
        a.ctl.wings = approach(a.ctl.wings, 0.6f, dt * 2.0f);
        a.ctl.lean = approach(a.ctl.lean, 0.1f, dt * 3.0f);
        v.ctl.headYaw = approach(v.ctl.headYaw, 0.2f, dt * 2.0f);
        if (stabs < 3 && pt > 0.21f + 0.4f * stabs) {
          vec3 at = stabs == 2 ? neckOf(v) : headOf(v);
          wound(w, at, dir + vec3(0, 0.25f, 0), stabs == 2 ? 1.0f : 0.45f, W_GORE);
          feathers(w, v, at, dir, 6);
          v.pos += dir * 0.03f;
          v.ctl.headYaw = (stabs % 2 ? -0.5f : 0.5f);
          if (stabs == 2) {
            kill(w, 0.0f, dir * 0.3f, 0.3f);
            w.slowmo(0.35f, 0.45f);
          } else {
            callOf(w, v, 0.7f);
          }
          ++stabs;
        }
        if (stabs >= 3) victimDown(w, dt);
        if (pt > 1.25f) next();
        break;
      }
      case 5:
        relax(a, dt);
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      case 6:
        if (first) callOf(w, a);
        triumph(w, pt / 1.0f);
        if (pt > 1.0f) next();
        break;
      case 7:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 8:
        return finishD(w);
    }
    return false;
  }
};

// Talon strike: it flies a wide loop and comes in low, feet first, and hits the victim
// like an eagle; it lands, and finishes it with its beak.
class DPteroTalons : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  Glide g;
  vec3 offset{0}, hitAt{0}, flightDir{0}, landAt{0};
  int stabs = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 side(-dir.z, 0, dir.x);
    switch (phase) {
      case 0:
        shotExtra = 1.3f;
        shotHeight = 0.6f;
        if (faceOffD(w)) next();
        break;
      case 1:  // up, and out to the side
        if (first) {
          g.set(a.pos, A + side * 0.9f + dir * 0.2f + vec3(0, 1.1f, 0), 0.9f);
          w.sound(SFX_FLAP, 0.8f, a.pos, 0.95f);
          w.fx.dust(A, 10, 0.4f, 0.9f);
        }
        airborne(a, dt, 15.0f, -0.15f);
        g.update(a, dt);
        victimGuard(v, seg(pt, 0.2f, 0.8f));
        brace(w, 0.6f);
        if (pt > 0.9f) next();
        break;
      case 2:  // round behind, and down low for the run in
        if (first) g.set(a.pos, V - dir * 1.5f + side * 0.2f + vec3(0, 0.55f, 0), 1.0f, 0.2f);
        airborne(a, dt, 9.0f, 0.0f);
        g.update(a, dt);
        if (pt > 0.7f) a.yawTarget = faceAngle(V - a.pos);
        v.yawTarget = faceAngle(a.pos - V);
        if (pt > 1.0f) next();
        break;
      case 3: {  // the strike: feet thrust forward, wings back-beating at the last moment
        const float dur = 0.5f;
        if (first) {
          a.ctl.tuck = 1.0f;
          offset = bonePoint(a, dn::PT_RSHIN, dn::clawPoint(K_KNIGHT)) - a.pos;
          hitAt = chestOf(v) * 0.6f + neckOf(v) * 0.4f;
          g.set(a.pos, hitAt - offset - dir * 0.03f, dur);
          flightDir = flatDir(g.p0, g.p1);
          callOf(w, a, 0.8f);
          w.sound(SFX_WHOOSH, 0.7f, a.pos, 0.8f);
        }
        float u = std::min(1.0f, pt / dur);
        a.pos = glm::mix(g.p0, g.p1, u);
        a.yawTarget = faceAngle(flightDir);
        a.ctl.tuck = 1.0f;
        a.ctl.hover = 1.0f;
        a.ctl.wings = 1.0f;
        a.ctl.flap += dt * (u > 0.7f ? 16.0f : 4.0f);
        a.ctl.lean = -0.3f * u;
        a.ctl.jaw = 0.6f;
        brace(w, 1.0f);
        if (u >= 1.0f) {
          wound(w, hitAt, flightDir + vec3(0, 0.3f, 0), 0.8f, W_CLAW);
          feathers(w, v, hitAt, flightDir, 12);
          w.sound(SFX_THUD, 0.9f, hitAt, 1.2f);
          knockFlying(w, flightDir * 2.0f + vec3(0, 1.6f, 0), w.fx.rnd() < 0.5f ? 8.0f : -8.0f);
          w.slowmo(0.35f, 0.5f);
          w.shake(0.25f);
          next();
        }
        break;
      }
      case 4: {  // down after it
        if (first) {
          landAt = clampBoard(flat(a.pos) + flightDir * 0.55f, 4.0f);
          g.set(a.pos, landAt, 0.55f, 0.15f);
        }
        bool down = g.update(a, dt, false);
        float u = seg(pt, 0.0f, 0.55f);
        a.ctl.flap += dt * 13.0f;
        a.ctl.hover = 1.0f - seg(u, 0.4f, 1.0f);
        a.ctl.tuck = 1.0f;
        a.ctl.lean = -0.35f;
        victimDown(w, dt);
        if (down) {
          a.pos = landAt;
          w.sound(SFX_STEP, 0.5f, landAt, 1.5f);
          w.fx.dust(landAt, 8, 0.3f, 0.6f);
          next();
        }
        break;
      }
      case 5: {  // the wings folded, it stabs down at the fallen
        float u = seg(pt, 0.0f, 0.4f);
        a.ctl.tuck = 1.0f - u;
        a.ctl.lean = -0.35f * (1.0f - u);
        a.ctl.wings = 1.0f - u;
        a.ctl.hover = 0;
        bool down = victimDown(w, dt);
        vec3 body = v.pos;
        if (Puppet* cp = w.puppet(tumble.corpse >= 0 ? tumble.corpse : fall.corpse)) body = cp->pos;
        if (pt > 0.3f) a.yawTarget = faceAngle(body - a.pos);
        if (pt > 0.5f) {
          float k = std::fmod(pt - 0.5f, 0.45f) / 0.45f;
          lunge(a, k, 1.0f);
          a.ctl.headPitch += 0.5f;
          a.ctl.jaw = 0.05f;
          if (stabs < 2 && pt > 0.72f + 0.45f * stabs) {
            bloodSpray(w, flat(body) + vec3(0, 0.08f, 0), vec3(0, 1, 0) - flatDir(a.pos, body), 25, 1.6f);
            w.sound(SFX_GORE, 0.6f, body, 1.2f);
            ++stabs;
          }
        }
        if (down && stabs >= 2 && pt > 1.5f) next();
        break;
      }
      case 6:
        if (first) callOf(w, a);
        triumph(w, pt / 1.0f);
        if (pt > 1.0f) next();
        break;
      case 7:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 8:
        return finishD(w);
    }
    return false;
  }
};

// ===========================================================================
// BISHOP - the triceratops
// ===========================================================================
// Horn charge: it paws the ground and snorts, lowers its horns and charges; the victim is
// gored and tossed (the big ones bowled over).
class DTrikeCharge : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  bool snorted = false;
  float skid = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.8f;
        if (faceOffD(w)) next();
        break;
      case 1: {  // pawing the ground, snorting, the horns lowered
        if (first) warnOf(w, a);
        if (!snorted && pt > 0.6f) {
          snorted = true;
          warnOf(w, a);
        }
        threat(a, seg(pt, 0.0f, 0.5f));
        a.ctl.gait += dt * 7.0f;
        a.ctl.stride = 0.3f;
        if (std::fmod(pt, 0.3f) < dt) w.fx.dust(flat(a.pos) + a.forward() * 0.2f, 3, 0.15f, 0.5f);
        victimGuard(v, seg(pt, 0.3f, 0.9f));
        brace(w, 0.8f);
        if (pt > 1.15f) next();
        break;
      }
      case 2:  // the charge
        if (first) {
          walker = Walker(plan.mover, V + dir * 0.3f, std::max(2.5f, L / 0.8f));
          walker.run = 1.0f;
          walker.face = true;
          callOf(w, a, 0.9f);
        }
        a.ctl.stride = std::max(a.ctl.stride, 0.3f);
        threat(a, 1.0f);
        a.ctl.headPitch = 0.42f;
        walker.update(w, dt);
        brace(w, 1.0f);
        if (glm::length(flat(V - a.pos)) <= reachOf(a) * 0.85f + halfWidthOf(v) * 0.5f) {
          vec3 c = chestOf(v) * 0.5f + neckOf(v) * 0.5f;
          wound(w, c, dir + vec3(0, 0.5f, 0), 1.2f, W_GORE);
          feathers(w, v, c, dir, 14);
          knockFlying(w, dir * 2.2f + vec3(0, 3.0f, 0), w.fx.rnd() < 0.5f ? 9.0f : -9.0f);
          w.slowmo(0.3f, 0.5f);
          w.shake(0.35f);
          skid = walker.v;
          next();
        }
        break;
      case 3: {  // the toss of the head; skidding to a halt
        a.ctl.headPitch = kf(pt, {{0.0f, 0.42f}, {0.18f, -0.45f}, {0.7f, 0.0f}});
        a.ctl.neck = kf(pt, {{0.0f, -0.2f}, {0.18f, 0.4f}, {0.7f, 0.0f}});
        skid = approach(skid, 0.0f, dt * 5.0f);
        a.pos += dir * skid * dt;
        a.pos = clampBoard(a.pos, 4.0f);
        a.ctl.gait += skid * dt / (dn::strideCycle(a.kind) * a.scale) * kTau;
        a.ctl.stride = std::min(1.0f, skid);
        if (std::fmod(pt, 0.1f) < dt && skid > 0.3f) w.fx.dust(flat(a.pos), 2, 0.2f, 0.5f);
        bool down = victimDown(w, dt);
        if (down && pt > 0.8f) next();
        break;
      }
      case 4:
        if (first) callOf(w, a);
        triumph(w, pt / 1.2f);
        if (pt > 1.2f) next();
        break;
      case 5:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 6:
        return finishD(w);
    }
    return false;
  }
};

// Horn lock: the victim bites at it and its jaws close on the frill; the triceratops
// drives it back across the board, twists its head and gores it.
class DTrikeLock : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  bool blocked = false, gored = false;
  float pushDist = 0, pushed = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOffD(w)) next();
        break;
      case 1:  // in close, the horns lowered
        if (first) walker = Walker(plan.mover, V - dir * (reachOf(a) * 0.8f + reachOf(v) * 0.35f + halfWidthOf(v) * 0.3f + 0.06f), 1.0f);
        threat(a, seg(pt, 0.0f, 0.5f));
        victimGuard(v, seg(pt, 0.0f, 0.6f));
        brace(w, 0.7f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // the victim strikes; its jaws close on the frill
        float u = seg(pt, 0.0f, 0.8f);
        victimStrike(v, u);
        threat(a, 1.0f);
        a.ctl.headPitch = 0.42f + 0.1f * bump(u, 0.45f, 0.7f);
        if (!blocked && u > 0.5f) {
          blocked = true;
          vec3 f = headOf(a) + vec3(0, 0.1f, 0);
          w.sound(SFX_SNAP, 0.9f, f, 0.9f);
          w.sound(SFX_THUD, 0.7f, f, 1.2f);
          w.fx.dust(f, 4, 0.1f, 0.4f);
          a.pos -= dir * 0.03f;
          warnOf(w, a);
          w.shake(0.1f);
        }
        if (pt > 0.85f) next();
        break;
      }
      case 3: {  // the shove: it drives the victim back across the board, feet scrabbling
        if (first) {
          vec3 far = clampBoard(V + dir * 0.9f, 3.7f);
          pushDist = std::max(0.0f, glm::dot(far - V, dir));
          pushed = 0;
          callOf(w, a, 0.7f);
          warnOf(w, v, 0.8f);
        }
        float spd = 0.55f * bump(pt, 0.0f, 1.3f) + 0.1f;
        float d = std::min(spd * dt, pushDist - pushed);
        pushed += d;
        a.pos += dir * d;
        v.pos += dir * d;
        a.ctl.gait += d / (dn::strideCycle(a.kind) * a.scale) * kTau + dt * 2.0f;
        a.ctl.stride = 0.7f;
        a.ctl.lean = 0.08f;
        a.ctl.crouch = 0.2f;
        threat(a, 1.0f);
        v.ctl.gait -= dt * 12.0f;
        v.ctl.stride = 0.6f;
        v.ctl.headYaw = 0.45f * std::sin(pt * 8.0f);
        v.ctl.jaw = 0.7f;
        v.tremble = 0.6f;
        if (std::fmod(pt, 0.15f) < dt) w.fx.dust(flat(v.pos), 2, 0.2f, 0.5f);
        if (std::fmod(pt, 0.4f) < dt) footfall(w, a, 0.8f);
        if (pt > 1.3f) next();
        break;
      }
      case 4: {  // the twist, and the horns go in
        float u = seg(pt, 0.0f, 0.5f);
        a.ctl.headYaw = kf(u, {{0.0f, 0.0f}, {0.35f, -0.45f}, {0.7f, 0.6f}, {1.0f, 0.3f}});
        a.ctl.headPitch = kf(u, {{0.0f, 0.42f}, {0.6f, 0.2f}, {1.0f, -0.3f}});
        if (!gored && u > 0.5f) {
          gored = true;
          vec3 side(-dir.z, 0, dir.x);
          wound(w, chestOf(v), dir + side * 0.5f + vec3(0, 0.4f, 0), 1.2f, W_GORE);
          feathers(w, v, chestOf(v), dir, 12);
          kill(w, 0.0f, dir * 0.3f + side * 0.4f, 0.25f);
          w.slowmo(0.35f, 0.5f);
          w.shake(0.25f);
        }
        if (gored) victimDown(w, dt);
        if (pt > 0.6f) next();
        break;
      }
      case 5:
        relax(a, dt);
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      case 6:
        if (first) callOf(w, a);
        triumph(w, pt / 1.2f);
        if (pt > 1.2f) next();
        break;
      case 7:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 8:
        return finishD(w);
    }
    return false;
  }
};

// Frill bash: a swing of the great head knocks the victim off its feet; it rears and
// tramples it.
class DTrikeBash : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  bool hit = false, stamped = false;
  float swingSide = 1;
  vec3 body{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOffD(w)) next();
        break;
      case 1:
        if (first) walker = Walker(plan.mover, V - dir * (reachOf(a) * 0.55f + reachOf(v) * 0.3f + halfWidthOf(v) * 0.6f + 0.05f), 1.0f);
        threat(a, seg(pt, 0.0f, 0.5f));
        victimGuard(v, seg(pt, 0.0f, 0.6f));
        brace(w, 0.7f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // the wind-up: the head swung away
        if (first) {
          swingSide = w.fx.rnd() < 0.5f ? 1.0f : -1.0f;
          warnOf(w, a);
        }
        float u = seg(pt, 0.0f, 0.4f);
        a.ctl.headYaw = -0.7f * swingSide * easeOut(u);
        a.ctl.headPitch = 0.2f;
        a.ctl.twist = -0.15f * swingSide * u;
        victimStrike(v, seg(pt, 0.0f, 0.5f) * 0.4f);
        if (pt > 0.45f) next();
        break;
      }
      case 3: {  // the bash
        float u = seg(pt, 0.0f, 0.2f);
        a.ctl.headYaw = glm::mix(-0.7f, 0.95f, easeIn(u)) * swingSide;
        a.ctl.twist = glm::mix(-0.15f, 0.2f, u) * swingSide;
        if (!hit && u > 0.55f) {
          hit = true;
          vec3 left(std::cos(a.yaw), 0, -std::sin(a.yaw));
          vec3 sweep = left * swingSide;
          w.sound(SFX_THUD, 1.0f, headOf(v), 0.9f);
          wound(w, neckOf(v), sweep + vec3(0, 0.3f, 0), 0.6f, W_GORE);
          feathers(w, v, neckOf(v), sweep, 10);
          knockFlying(w, sweep * 1.7f + dir * 0.4f + vec3(0, 1.6f, 0), swingSide * 7.0f);
          w.shake(0.3f);
          w.slowmo(0.35f, 0.4f);
        }
        if (hit) victimDown(w, dt);
        if (pt > 0.4f) next();
        break;
      }
      case 4: {  // up to the fallen
        bool down = victimDown(w, dt);
        a.ctl.headYaw = approach(a.ctl.headYaw, 0.0f, dt * 2.0f);
        a.ctl.twist = approach(a.ctl.twist, 0.0f, dt * 2.0f);
        if (down) {
          body = v.pos;
          if (Puppet* cp = w.puppet(tumble.corpse >= 0 ? tumble.corpse : fall.corpse)) body = cp->pos;
          next();
        }
        break;
      }
      case 5:  // to the body
        if (first) walker = Walker(plan.mover, body - flatDir(a.pos, body) * 0.3f * a.scale, 1.0f);
        relax(a, dt);
        if (walker.update(w, dt)) next();
        break;
      case 6: {  // it rears, and tramples
        float u = seg(pt, 0.0f, 0.75f);
        a.ctl.rear = kf(u, {{0.0f, 0.0f}, {0.6f, 0.75f}, {0.82f, 0.0f}});
        a.ctl.jaw = 0.5f * bump(u, 0.2f, 0.8f);
        if (!stamped && u > 0.82f) {
          stamped = true;
          vec3 f = flat(a.pos) + a.forward() * 0.2f;
          w.sound(SFX_QUAKE, 0.9f, f, 1.0f);
          w.sound(SFX_CRUNCH, 0.9f, f, 0.9f);
          bloodSpray(w, body + vec3(0, 0.06f, 0), vec3(0, 1, 0), 40, 2.2f, 0.7f);
          splatter(w, body, a.forward(), 4, 0.35f);
          w.fx.dust(f, 18, 0.35f, 1.0f);
          w.shake(0.35f);
        }
        if (pt > 1.0f) next();
        break;
      }
      case 7:
        if (first) callOf(w, a);
        triumph(w, pt / 1.2f);
        if (pt > 1.2f) next();
        break;
      case 8:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 9:
        return finishD(w);
    }
    return false;
  }
};

// ===========================================================================
// ROOK - the brachiosaurus
// ===========================================================================
// Rearing stomp: it walks up to the cowering victim, rears up on its hind legs, towering
// over it, and comes crashing down on it with its forefeet.
class DBrachioStomp : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  vec3 start{0};
  float lunge = 0;
  bool bellowed = false, crashed = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.6f;
        shotHeight = 1.0f;
        if (faceOffD(w, 0.6f)) next();
        break;
      case 1:  // up to it; the victim turns side-on and cowers, looking up
        if (first) {
          walker = Walker(plan.mover, V - dir * (0.17f + halfWidthOf(v) + 0.22f), 0.7f);
          v.yawTarget = faceAngle(A - V) + (w.fx.rnd() < 0.5f ? 1.0f : -1.0f) * 1.3f;
          v.yawRate = 3.0f;
        }
        v.ctl.crouch = 0.3f * seg(pt, 0.3f, 1.0f);
        v.ctl.neck = 0.3f * seg(pt, 0.3f, 1.0f);
        v.ctl.headPitch = -0.35f * seg(pt, 0.3f, 1.0f);
        v.ctl.headYaw = 0.5f * seg(pt, 0.3f, 1.0f) * (wrapAngle(v.yawTarget - faceAngle(A - V)) > 0 ? -1.0f : 1.0f);
        brace(w, 0.8f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // rearing up, bellowing; the victim snaps up at it
        a.ctl.rear = easeInOut(seg(pt, 0.0f, 1.1f));
        a.ctl.neck = 0.35f * seg(pt, 0.0f, 1.0f);
        a.ctl.jaw = 0.6f * bump(pt, 0.3f, 1.3f);
        if (!bellowed && pt > 0.3f) {
          bellowed = true;
          callOf(w, a);
        }
        if (pt > 0.4f) victimStrike(v, seg(pt, 0.4f, 1.2f));
        v.tremble = 1.0f;
        if (pt > 1.35f) next();
        break;
      }
      case 3: {  // and down
        const float dur = 0.3f;
        if (first) {
          start = a.pos;
          lunge = halfWidthOf(v) + 0.2f;
          w.sound(SFX_WHOOSH, 0.8f, headOf(a), 0.5f);
        }
        float u = std::min(1.0f, pt / dur);
        a.ctl.rear = 1.0f - easeIn(u);
        a.pos = start + dir * lunge * easeIn(u);
        a.ctl.neck = 0.35f * (1.0f - u) - 0.2f * u;
        if (!crashed && u >= 1.0f) {
          crashed = true;
          vec3 f = flat(V);
          w.sound(SFX_QUAKE, 1.0f, f, 0.8f);
          w.sound(SFX_QUAKE, 0.8f, f, 0.95f);
          wound(w, chestOf(v), vec3(0, 1, 0), 1.2f, W_CRUSH);
          feathers(w, v, chestOf(v), vec3(0, 1, 0), 16);
          w.groundRing(f, vec3(0.8f, 0.7f, 0.55f), 3.0f, 0.6f, 0.08f);
          w.fx.dust(f, 40, 0.8f, 2.0f);
          w.shake(0.6f);
          w.slowmo(0.3f, 0.5f);
          fall.bleed = 1.4f;
          kill(w, 0.0f, dir * 0.15f, 0.04f);
          next();
        }
        break;
      }
      case 4:
        a.ctl.neck = approach(a.ctl.neck, 0.0f, dt);
        if (victimDown(w, dt) && pt > 0.5f) next();
        break;
      case 5:
        if (first) callOf(w, a);
        triumph(w, pt / 1.4f);
        if (pt > 1.4f) next();
        break;
      case 6:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 7:
        return finishD(w);
    }
    return false;
  }
};

// Neck swing: the long neck lowered and swung like a club.
class DBrachioNeck : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  float swingSide = 1;
  bool hit = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.5f;
        shotHeight = 0.8f;
        if (faceOffD(w)) next();
        break;
      case 1:
        if (first) walker = Walker(plan.mover, V - dir * 0.98f, 0.7f);
        victimGuard(v, seg(pt, 0.0f, 0.8f));
        brace(w, 0.6f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // the wind-up: the neck lowered and drawn back to one side
        if (first) {
          swingSide = w.fx.rnd() < 0.5f ? 1.0f : -1.0f;
          callOf(w, a, 0.8f);
        }
        float u = easeInOut(seg(pt, 0.0f, 0.9f));
        a.ctl.neck = -1.25f * u;
        a.ctl.headYaw = -1.35f * swingSide * u;
        a.ctl.twist = -0.15f * swingSide * u;
        a.ctl.jaw = 0.3f * u;
        victimGuard(v, 1.0f);
        v.ctl.headPitch = -0.2f;
        brace(w, 0.9f);
        if (pt > 1.0f) next();
        break;
      }
      case 3: {  // the swing
        const float dur = 0.38f;
        if (first) w.sound(SFX_WHOOSH, 0.9f, headOf(a), 0.55f);
        float u = std::min(1.0f, pt / dur);
        a.ctl.headYaw = glm::mix(-1.35f, 1.15f, easeIn(u)) * swingSide;
        a.ctl.twist = glm::mix(-0.15f, 0.2f, u) * swingSide;
        a.ctl.neck = -1.25f;
        if (!hit && u > 0.56f) {
          hit = true;
          vec3 left(std::cos(a.yaw), 0, -std::sin(a.yaw));
          vec3 sweep = left * swingSide;
          vec3 at = headOf(a);
          w.sound(SFX_THUD, 1.0f, at, 0.7f);
          wound(w, chestOf(v) * 0.5f + neckOf(v) * 0.5f, sweep + vec3(0, 0.3f, 0), 0.7f, W_CRUSH);
          feathers(w, v, at, sweep, 14);
          knockFlying(w, sweep * 2.4f + vec3(0, 2.0f, 0), swingSide * 9.0f);
          w.shake(0.4f);
          w.slowmo(0.3f, 0.5f);
        }
        if (hit) victimDown(w, dt);
        if (u >= 1.0f && pt > dur + 0.15f) next();
        break;
      }
      case 4: {  // and up again
        float u = easeInOut(seg(pt, 0.0f, 1.0f));
        a.ctl.headYaw = 1.15f * swingSide * (1.0f - u);
        a.ctl.twist = 0.2f * swingSide * (1.0f - u);
        a.ctl.neck = -1.25f * (1.0f - u);
        if (victimDown(w, dt) && pt > 1.0f) next();
        break;
      }
      case 5:
        if (first) callOf(w, a);
        triumph(w, pt / 1.4f);
        if (pt > 1.4f) next();
        break;
      case 6:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 7:
        return finishD(w);
    }
    return false;
  }
};

// Tail lash: it turns its back on the victim and cracks its tail across it like a whip.
class DBrachioTail : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  float swingSide = 1;
  bool hit = false, cracked = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.6f;
        shotHeight = 0.6f;
        if (faceOffD(w)) next();
        break;
      case 1:
        if (first) walker = Walker(plan.mover, V - dir * 0.78f, 0.7f);
        victimGuard(v, seg(pt, 0.0f, 0.8f));
        brace(w, 0.6f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // it turns its back
        if (first) {
          swingSide = w.fx.rnd() < 0.5f ? 1.0f : -1.0f;
          a.yawTarget = faceAngle(-dir) + 0.25f * swingSide;
          a.yawRate = 2.6f;
          callOf(w, a, 0.7f);
        }
        a.ctl.gait += dt * 3.0f;
        a.ctl.stride = 0.3f * (1.0f - seg(pt, 0.9f, 1.3f));
        a.ctl.headYaw = 0.8f * swingSide * seg(pt, 0.4f, 1.2f);  // looking back over its shoulder
        victimGuard(v, 1.0f);
        brace(w, 0.8f);
        if (pt > 1.35f) next();
        break;
      }
      case 3: {  // the tail drawn round to one side
        float u = easeInOut(seg(pt, 0.0f, 0.6f));
        a.ctl.itemYaw = 1.15f * swingSide * u;
        a.ctl.itemPitch = 0.3f * u;
        a.ctl.twist = -0.15f * swingSide * u;
        a.ctl.stride = 0;
        if (pt > 0.65f) next();
        break;
      }
      case 4: {  // the lash
        const float dur = 0.3f;
        if (!cracked) {
          cracked = true;
          w.sound(SFX_TAILWHIP, 1.0f, tailOf(a), 1.0f);
        }
        float u = std::min(1.0f, pt / dur);
        a.ctl.itemYaw = glm::mix(1.15f, -1.25f, easeIn(u)) * swingSide;
        a.ctl.twist = glm::mix(-0.15f, 0.15f, u) * swingSide;
        if (!hit && u > 0.62f) {
          hit = true;
          vec3 left(std::cos(a.yaw), 0, -std::sin(a.yaw));
          vec3 sweep = -left * swingSide;
          wound(w, chestOf(v), sweep + vec3(0, 0.4f, 0), 0.8f, W_CLAW);
          feathers(w, v, chestOf(v), sweep, 14);
          knockFlying(w, sweep * 2.3f + vec3(0, 1.8f, 0), -swingSide * 8.0f);
          w.shake(0.3f);
          w.slowmo(0.3f, 0.45f);
        }
        if (hit) victimDown(w, dt);
        if (u >= 1.0f && pt > dur + 0.2f) next();
        break;
      }
      case 5: {  // and round again
        if (first) {
          a.yawTarget = faceAngle(dir);
          a.yawRate = 2.6f;
        }
        float u = easeInOut(seg(pt, 0.0f, 0.8f));
        a.ctl.itemYaw = -1.25f * swingSide * (1.0f - u);
        a.ctl.itemPitch = 0.3f * (1.0f - u);
        a.ctl.twist = 0.15f * swingSide * (1.0f - u);
        a.ctl.headYaw = approach(a.ctl.headYaw, 0.0f, dt * 2.0f);
        a.ctl.gait += dt * 3.0f;
        a.ctl.stride = 0.3f * (1.0f - seg(pt, 0.9f, 1.3f));
        if (victimDown(w, dt) && pt > 1.3f) next();
        break;
      }
      case 6:
        if (first) callOf(w, a);
        triumph(w, pt / 1.4f);
        if (pt > 1.4f) next();
        break;
      case 7:
        if (first) a.yawRate = 9.0f;
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 8:
        return finishD(w);
    }
    return false;
  }
};

// ===========================================================================
// QUEEN - the spinosaurus
// ===========================================================================
// Hooked claws: she rears with her arms raised and roars; she swats the victim's jaws
// aside and rakes it with her great thumb claws.
class DSpinoClaws : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  bool swatted = false;
  int slashes = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 side(-dir.z, 0, dir.x);
    switch (phase) {
      case 0:
        if (faceOffD(w)) next();
        break;
      case 1:
        if (first) walker = Walker(plan.mover, V - dir * (reachOf(a) * 0.55f + reachOf(v) * 0.35f + halfWidthOf(v) + 0.12f), 1.0f);
        threat(a, seg(pt, 0.0f, 0.5f));
        victimGuard(v, seg(pt, 0.0f, 0.6f));
        brace(w, 0.7f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // she rears, claws raised, and roars
        if (first) callOf(w, a);
        float u = easeInOut(seg(pt, 0.0f, 0.5f));
        a.ctl.lean = -0.4f * u;
        a.ctl.neck = 0.3f * u;
        a.ctl.jaw = 0.9f * bump(pt, 0.0f, 0.9f);
        a.ctl.armL = a.ctl.armR = vec3(1.5f, 0.35f, 0.5f) * u;
        a.ctl.itemPitch = 0.2f * u;
        victimGuard(v, 1.0f);
        brace(w, 1.0f);
        if (pt > 0.9f) next();
        break;
      }
      case 3: {  // the victim lunges; she swats its jaws aside
        float u = seg(pt, 0.0f, 0.8f);
        victimStrike(v, u);
        a.ctl.armL = glm::mix(vec3(1.5f, 0.35f, 0.5f), vec3(0.4f, -0.25f, 0.1f), easeIn(seg(u, 0.35f, 0.55f)));
        a.ctl.jaw = 0.3f;
        if (!swatted && u > 0.52f) {
          swatted = true;
          w.sound(SFX_THUD, 0.8f, headOf(v), 1.2f);
          w.sound(SFX_SNAP, 0.6f, headOf(v), 1.0f);
          v.ctl.headYaw = 0.8f;
          w.shake(0.1f);
        }
        if (swatted) v.ctl.headYaw = approach(v.ctl.headYaw, 0.0f, dt * 1.5f);
        if (pt > 0.85f) next();
        break;
      }
      case 4: {  // the rakes: right, then left
        float u1 = seg(pt, 0.0f, 0.35f), u2 = seg(pt, 0.4f, 0.75f);
        a.ctl.armR = glm::mix(vec3(1.9f, 0.3f, 0.6f), vec3(0.1f, -0.3f, -0.2f), easeIn(u1));
        a.ctl.armL = glm::mix(vec3(1.9f, 0.3f, 0.6f), vec3(0.1f, -0.3f, -0.2f), easeIn(u2));
        if (pt < 0.4f) a.ctl.armL = glm::mix(a.ctl.armL, vec3(1.9f, 0.3f, 0.6f), std::min(1.0f, dt * 6.0f));
        a.ctl.lean = -0.4f + 0.55f * (bump(u1, 0.0f, 1.0f) * 0.7f + bump(u2, 0.0f, 1.0f));
        a.ctl.twist = 0.2f * (bump(u1, 0.0f, 1.0f) - bump(u2, 0.0f, 1.0f));
        a.ctl.jaw = 0.6f;
        if (slashes < 2 && (slashes == 0 ? u1 : u2) > 0.6f) {
          vec3 at = slashes == 0 ? neckOf(v) : chestOf(v);
          wound(w, at, (slashes == 0 ? -side : side) + dir * 0.6f + vec3(0, 0.2f, 0), slashes == 0 ? 0.7f : 1.1f, W_CLAW);
          feathers(w, v, at, dir, 10);
          v.pos += dir * 0.04f;
          if (slashes == 1) {
            kill(w, 0.0f, dir * 0.3f + side * 0.2f, 0.35f);
            w.slowmo(0.35f, 0.5f);
            w.shake(0.2f);
          } else {
            callOf(w, v, 0.7f);
          }
          ++slashes;
        }
        if (slashes >= 2) victimDown(w, dt);
        if (pt > 0.9f) next();
        break;
      }
      case 5:
        relax(a, dt, 2.0f);
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      case 6:
        if (first) callOf(w, a);
        triumph(w, pt / 1.2f);
        if (pt > 1.2f) next();
        break;
      case 7:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 8:
        return finishD(w);
    }
    return false;
  }
};

// Holding a victim in the jaws: a small one hangs from them, kicking; a big one is held
// by the neck and wrenched about.
struct Held {
  vec3 grab{0}, base{0};
  float baseYaw = 0;
  bool lifted = false;
  void take(Actor& a, Actor& v) {
    grab = v.pos - mouthOf(a);
    base = v.pos;
    baseYaw = v.yaw;
    lifted = isSmall(v);
  }
  void update(Actor& a, Actor& v, float t, float dt, float lift) {
    if (lifted) {
      vec3 p = mouthOf(a) + grab;
      p.y = std::max(0.0f, p.y + lift);
      v.pos = p;
      v.yaw = baseYaw + 0.9f * a.ctl.headYaw;
      v.yawTarget = v.yaw;
      v.tiltPivot = vec3(0, v.height() / std::max(v.scale, 1e-3f) * 0.6f, 0);
      v.tilt = glm::angleAxis(0.35f * std::sin(t * 9.0f), vec3(0, 0, 1)) * glm::angleAxis(0.3f, vec3(1, 0, 0));
      v.ctl.gait += dt * 14.0f;
      v.ctl.stride = 1.0f;
      v.ctl.dead = 0.25f;
    } else {
      vec3 side(std::cos(v.yaw), 0, -std::sin(v.yaw));
      v.pos = base + side * 0.05f * std::sin(t * 12.0f);
      v.yaw = baseYaw + 0.12f * std::sin(t * 10.0f);
      v.yawTarget = v.yaw;
      v.ctl.headYaw = 0.5f * std::sin(t * 8.0f);
      v.ctl.crouch = 0.25f;
    }
    v.ctl.jaw = 0.7f;
    v.tremble = 0.5f;
    v.idleWeight = 0;
  }
};

// Snatch: she stalks in low like a crocodile, seizes the victim in her long jaws, shakes
// it and flings it away.
class DSpinoSnatch : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  Held held;
  bool bit = false;
  vec3 lunge0{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 side(-dir.z, 0, dir.x);
    switch (phase) {
      case 0:
        if (faceOffD(w)) next();
        break;
      case 1:  // the stalk: low, slow
        if (first) {
          walker = Walker(plan.mover, V - dir * (reachOf(a) + reachOf(v) * 0.3f + halfWidthOf(v) * 0.5f + 0.08f), 0.55f);
          warnOf(w, a);
        }
        a.ctl.crouch = approach(a.ctl.crouch, 0.35f, dt);
        a.ctl.lean = approach(a.ctl.lean, 0.22f, dt);
        a.ctl.neck = approach(a.ctl.neck, -0.35f, dt);
        a.ctl.headPitch = approach(a.ctl.headPitch, -0.1f, dt);
        victimGuard(v, seg(pt, 0.0f, 0.8f));
        brace(w, 0.8f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // the snap
        if (first) lunge0 = a.pos;
        float u = seg(pt, 0.0f, 0.3f);
        lunge(a, 0.2f + 0.35f * u, 1.2f);
        a.pos = lunge0 + dir * 0.1f * easeIn(u);
        if (!bit && u >= 1.0f) {
          bit = true;
          wound(w, neckOf(v), side + vec3(0, 0.3f, 0), 0.8f, W_BITE);
          feathers(w, v, neckOf(v), dir, 12);
          callOf(w, v, 0.8f);
          held.take(a, v);
          next();
        }
        break;
      }
      case 3: {  // lifted and shaken
        a.ctl.jaw = 0.12f;
        a.ctl.neck = approach(a.ctl.neck, 0.35f, dt * 3.0f);
        a.ctl.lean = approach(a.ctl.lean, -0.1f, dt * 3.0f);
        a.ctl.crouch = approach(a.ctl.crouch, 0.1f, dt * 2.0f);
        a.ctl.headYaw = 0.55f * std::sin(pt * 15.0f) * seg(pt, 0.2f, 0.5f);
        a.ctl.headPitch = 0.1f * std::sin(pt * 11.0f);
        held.update(a, v, pt, dt, 0.06f * seg(pt, 0.0f, 0.3f));
        if (std::fmod(pt, 0.3f) < dt) {
          bloodSpray(w, mouthOf(a), side * std::sin(pt * 15.0f) + vec3(0, 0.3f, 0), 18, 1.6f);
          w.sound(SFX_TEAR, 0.4f, mouthOf(a), w.fx.rnd(1.0f, 1.2f));
        }
        if (pt > 1.25f) next();
        break;
      }
      case 4: {  // the fling
        if (first) {
          float s = a.ctl.headYaw >= 0 ? 1.0f : -1.0f;
          vec3 left(std::cos(a.yaw), 0, -std::sin(a.yaw));
          vec3 out = left * s;
          w.sound(SFX_WHOOSH, 0.7f, mouthOf(a), 0.8f);
          v.tilt = quat(1, 0, 0, 0);
          v.tiltPivot = vec3(0);
          if (held.lifted) {
            tumble.launch(w, v, out * 2.4f + dir * 0.5f + vec3(0, 2.2f, 0), s * 10.0f);
          } else {
            wound(w, neckOf(v), out + vec3(0, 0.4f, 0), 1.0f, W_CLAW);
            kill(w, 0.0f, out * 0.5f, 0.12f);
          }
          w.slowmo(0.35f, 0.5f);
          w.shake(0.2f);
        }
        a.ctl.headYaw = approach(a.ctl.headYaw, 0.0f, dt * 2.0f);
        a.ctl.jaw = 0.5f;
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      }
      case 5:
        if (first) callOf(w, a);
        triumph(w, pt / 1.2f);
        if (pt > 1.2f) next();
        break;
      case 6:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 7:
        return finishD(w);
    }
    return false;
  }
};

// Tail swipe: she spins round and her tail sweeps the victim off its feet; she pins it
// with a foot and bites.
class DSpinoTail : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  float spinDir = 1, yaw0 = 0;
  bool hit = false, bit = false, whooshed = false;
  vec3 body{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOffD(w)) next();
        break;
      case 1:
        if (first) walker = Walker(plan.mover, V - dir * (0.5f * a.scale + halfWidthOf(v) + 0.1f), 1.0f);
        threat(a, seg(pt, 0.0f, 0.5f));
        victimGuard(v, seg(pt, 0.0f, 0.6f));
        brace(w, 0.7f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // the wind-up: a growl, the body coiling
        if (first) {
          spinDir = w.fx.rnd() < 0.5f ? 1.0f : -1.0f;
          warnOf(w, a);
        }
        float u = easeInOut(seg(pt, 0.0f, 0.45f));
        a.ctl.twist = -0.35f * spinDir * u;
        a.ctl.itemYaw = 0.5f * spinDir * u;
        a.ctl.crouch = 0.2f * u;
        victimStrike(v, seg(pt, 0.0f, 0.5f) * 0.3f);
        if (pt > 0.5f) next();
        break;
      }
      case 3: {  // the spin: the tail sweeps through the victim's legs
        const float dur = 0.75f;
        if (first) yaw0 = a.yaw;
        float u = std::min(1.0f, pt / dur);
        float turn = kTau * easeInOut(u) * spinDir;
        a.yaw = wrapAngle(yaw0 + turn);
        a.yawTarget = a.yaw;
        a.ctl.itemYaw = -0.9f * spinDir * bump(u, 0.0f, 1.0f);
        a.ctl.twist = 0.2f * spinDir * bump(u, 0.0f, 1.0f);
        a.ctl.lean = 0.12f * bump(u, 0.0f, 1.0f);
        a.ctl.gait += dt * 9.0f;
        a.ctl.stride = 0.5f * bump(u, 0.0f, 1.0f);
        if (!whooshed && u > 0.2f) {
          whooshed = true;
          w.sound(SFX_TAILWHIP, 0.8f, a.pos, 1.15f);
        }
        if (!hit && u > 0.5f) {  // the tail comes round behind her, through the victim
          hit = true;
          vec3 tang = vec3(dir.z, 0, -dir.x) * spinDir;
          w.sound(SFX_THUD, 0.9f, chestOf(v), 1.0f);
          wound(w, chestOf(v) - vec3(0, 0.1f, 0), tang + vec3(0, 0.3f, 0), 0.5f, W_CLAW);
          feathers(w, v, chestOf(v), tang, 10);
          knockFlying(w, tang * 1.6f + dir * 0.4f + vec3(0, 1.4f, 0), -spinDir * 7.0f);
          w.shake(0.25f);
          w.slowmo(0.35f, 0.4f);
        }
        if (hit) victimDown(w, dt);
        if (u >= 1.0f) {
          a.yawTarget = faceAngle(V - a.pos);
          next();
        }
        break;
      }
      case 4: {  // to the fallen
        bool down = victimDown(w, dt);
        relax(a, dt);
        if (down) {
          body = v.pos;
          if (Puppet* cp = w.puppet(tumble.corpse >= 0 ? tumble.corpse : fall.corpse)) body = cp->pos;
          next();
        }
        break;
      }
      case 5:
        if (first) walker = Walker(plan.mover, body - flatDir(a.pos, body) * (reachOf(a) * 0.75f), 0.9f);
        relax(a, dt);
        if (walker.update(w, dt)) next();
        break;
      case 6: {  // a foot on it, and the bite
        float u = seg(pt, 0.0f, 0.6f);
        a.ctl.kick = 0.5f * bump(u, 0.0f, 0.5f);
        a.ctl.neck = kf(u, {{0.0f, 0.0f}, {0.4f, 0.25f}, {0.65f, -1.2f}, {1.0f, -0.9f}});
        a.ctl.headPitch = kf(u, {{0.0f, 0.0f}, {0.65f, 0.6f}, {1.0f, 0.5f}});
        a.ctl.jaw = kf(u, {{0.0f, 0.3f}, {0.45f, 0.95f}, {0.66f, 0.05f}, {1.0f, 0.1f}});
        a.ctl.lean = kf(u, {{0.0f, 0.0f}, {0.65f, 0.3f}, {1.0f, 0.25f}});
        if (!bit && u > 0.66f) {
          bit = true;
          bloodSpray(w, flat(body) + vec3(0, 0.08f, 0), vec3(0, 1, 0), 45, 2.0f, 0.6f);
          splatter(w, body, a.forward(), 4, 0.3f);
          w.sound(SFX_CRUNCH, 0.9f, body, 1.0f);
          w.sound(SFX_BLOOD, 0.5f, body, 1.0f);
        }
        if (pt > 1.0f) next();
        break;
      }
      case 7:
        if (first) callOf(w, a);
        relax(a, dt * 0.5f);
        triumph(w, pt / 1.2f);
        if (pt > 1.2f) next();
        break;
      case 8:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 9:
        return finishD(w);
    }
    return false;
  }
};

// ===========================================================================
// KING - the tyrannosaurus
// ===========================================================================
// Bone-crusher: it charges, seizes the victim in its jaws, lifts it, shakes it and
// throws it aside.
class DRexCrush : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  Held held;
  bool bit = false;
  vec3 lunge0{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 side(-dir.z, 0, dir.x);
    switch (phase) {
      case 0:
        shotExtra = 0.6f;
        if (faceOffD(w)) next();
        break;
      case 1:  // a growl, the head low, and the charge
        if (first) {
          walker = Walker(plan.mover, V - dir * (biteDist(a, v) + 0.12f), std::max(1.7f, L / 1.1f));
          walker.run = 1.0f;
          warnOf(w, a);
        }
        threat(a, 1.0f);
        a.ctl.jaw = glm::length(flat(V - a.pos)) < biteDist(a, v) + 0.5f ? 0.9f : 0.4f;
        victimGuard(v, seg(pt, 0.0f, 0.6f));
        brace(w, 1.0f);
        if (walker.update(w, dt) || walker.s > walker.len - 0.03f) next();
        break;
      case 2: {  // the bite
        if (first) lunge0 = a.pos;
        float u = seg(pt, 0.0f, 0.32f);
        lunge(a, 0.25f + 0.3f * u, 1.1f);
        a.pos = lunge0 + dir * 0.1f * easeIn(u);
        if (!bit && u >= 1.0f) {
          bit = true;
          wound(w, neckOf(v), dir + vec3(0, 0.5f, 0), 1.2f, W_BITE);
          feathers(w, v, neckOf(v), dir, 14);
          w.sound(SFX_CRUNCH, 1.0f, neckOf(v), 0.8f);
          w.shake(0.25f);
          w.slowmo(0.4f, 0.35f);
          held.take(a, v);
          callOf(w, v, 0.8f);
          next();
        }
        break;
      }
      case 3: {  // lifted and shaken
        a.ctl.jaw = 0.15f;
        a.ctl.neck = approach(a.ctl.neck, 0.55f, dt * 2.5f);
        a.ctl.lean = approach(a.ctl.lean, -0.12f, dt * 2.5f);
        a.ctl.crouch = approach(a.ctl.crouch, 0.15f, dt * 2.0f);
        a.ctl.headYaw = 0.5f * std::sin(pt * 12.0f) * seg(pt, 0.25f, 0.5f);
        held.update(a, v, pt, dt, 0.1f * seg(pt, 0.0f, 0.3f));
        if (!held.lifted) {  // a big one: its front hauled up
          v.tiltPivot = vec3(0, 0, -0.3f);
          v.tilt = glm::angleAxis(-0.22f * seg(pt, 0.0f, 0.4f), vec3(1, 0, 0)) *
                   glm::angleAxis(0.1f * std::sin(pt * 12.0f), vec3(0, 0, 1));
        }
        if (std::fmod(pt, 0.28f) < dt) {
          bloodSpray(w, mouthOf(a), side * std::sin(pt * 12.0f) + vec3(0, 0.3f, 0), 22, 1.8f);
          w.sound(SFX_CRUNCH, 0.4f, mouthOf(a), w.fx.rnd(1.0f, 1.25f));
        }
        if (pt > 1.35f) next();
        break;
      }
      case 4: {  // the throw
        if (first) {
          float s = a.ctl.headYaw >= 0 ? 1.0f : -1.0f;
          vec3 left(std::cos(a.yaw), 0, -std::sin(a.yaw));
          vec3 out = left * s;
          w.sound(SFX_WHOOSH, 0.8f, mouthOf(a), 0.6f);
          v.tilt = quat(1, 0, 0, 0);
          v.tiltPivot = vec3(0);
          if (held.lifted) {
            tumble.launch(w, v, out * 2.8f + dir * 0.8f + vec3(0, 2.4f, 0), s * 10.0f);
          } else {
            wound(w, neckOf(v), out + vec3(0, 0.4f, 0), 1.0f, W_CLAW);
            kill(w, 0.0f, out * 0.8f, 0.08f);
          }
          w.slowmo(0.35f, 0.5f);
          w.shake(0.3f);
        }
        a.ctl.headYaw = approach(a.ctl.headYaw, 0.0f, dt * 2.0f);
        a.ctl.jaw = 0.5f;
        if (victimDown(w, dt) && pt > 0.4f) next();
        break;
      }
      case 5:  // the roar over its kill
        if (first) {
          callOf(w, a);
          w.groundRing(flat(a.pos) + a.forward() * 0.3f, vec3(0.7f, 0.65f, 0.55f), 2.2f, 0.5f, 0.05f);
        }
        triumph(w, pt / 1.6f);
        if (pt > 1.6f) next();
        break;
      case 6:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 7:
        return finishD(w);
    }
    return false;
  }
};

// The roar: the victim snaps at it and misses; it roars into the victim's face - the
// board shakes, the dust flies, the victim cowers - and bites down on it from above.
class DRexRoar : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  bool snapped = false, roared = false, bit = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOffD(w)) next();
        break;
      case 1:  // stalking in, growling
        if (first) {
          walker = Walker(plan.mover, V - dir * (biteDist(a, v) + 0.1f), 0.8f);
          warnOf(w, a);
        }
        threat(a, seg(pt, 0.0f, 0.6f));
        victimGuard(v, seg(pt, 0.0f, 0.8f));
        brace(w, 0.8f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // the victim lunges at its face; it rears its head out of reach
        float u = seg(pt, 0.0f, 0.8f);
        victimStrike(v, u);
        a.ctl.neck = 0.45f * bump(u, 0.3f, 0.9f);
        a.ctl.headPitch = -0.2f * bump(u, 0.3f, 0.9f);
        a.ctl.jaw = 0.3f;
        if (!snapped && u > 0.5f) {
          snapped = true;
          w.sound(SFX_SNAP, 0.9f, headOf(v), 1.0f);
        }
        if (pt > 0.85f) next();
        break;
      }
      case 3: {  // THE ROAR
        if (!roared) {
          roared = true;
          w.sound(SFX_REXROAR, 1.0f, headOf(a), 0.92f);
          w.shake(0.35f);
        }
        float u = seg(pt, 0.0f, 0.3f);
        a.ctl.neck = glm::mix(0.0f, -0.35f, u);
        a.ctl.headPitch = glm::mix(0.0f, -0.12f, u);
        a.ctl.jaw = 1.0f * u * (1.0f - seg(pt, 1.6f, 1.9f));
        a.ctl.lean = 0.15f * u;
        a.ctl.headYaw = 0.06f * std::sin(pt * 25.0f) * u;
        w.trauma = std::max(w.trauma, 0.3f * (1.0f - seg(pt, 1.2f, 1.8f)));
        if (pt < 1.6f && w.fx.rnd() < dt * 40.0f) {  // breath and dust blasted at the victim
          vec3 m = mouthOf(a);
          Particle p;
          p.pos = flat(V) + vec3(w.fx.rnd(-0.3f, 0.3f), w.fx.rnd(0.0f, 0.15f), w.fx.rnd(-0.3f, 0.3f)) - dir * 0.2f;
          p.vel = glm::normalize(flat(V - m) + w.fx.rndDir() * 0.4f) * w.fx.rnd(1.5f, 3.0f) + vec3(0, w.fx.rnd(0.1f, 0.6f), 0);
          p.size = w.fx.rnd(0.05f, 0.1f);
          p.grow = 0.35f;
          p.color = vec3(0.55f, 0.5f, 0.45f);
          p.colorEnd = p.color;
          p.opacity = w.fx.rnd(0.15f, 0.3f);
          p.maxLife = w.fx.rnd(0.6f, 1.1f);
          p.type = PT_SMOKE;
          p.drag = 2.0f;
          p.rot = w.fx.rnd(0.0f, 6.28f);
          p.seed = w.fx.rnd();
          p.fadeIn = 0.05f;
          w.fx.add(p);
        }
        // the victim cowers, head turned away, trembling
        float c = seg(pt, 0.15f, 0.5f);
        v.ctl.crouch = 0.4f * c;
        v.ctl.neck = -0.3f * c;
        v.ctl.headYaw = 0.6f * c;
        v.ctl.jaw = 0.0f;
        v.ctl.headPitch = 0.2f * c;
        v.tremble = 1.0f;
        if (c > 0.2f && std::fmod(pt, 0.25f) < dt) feathers(w, v, chestOf(v), dir, 2);
        if (pt > 1.9f) next();
        break;
      }
      case 4: {  // the bite from above
        float u = seg(pt, 0.0f, 0.4f);
        a.ctl.neck = kf(u, {{0.0f, -0.35f}, {0.3f, 0.2f}, {0.62f, -1.1f}, {1.0f, -0.9f}});
        a.ctl.headPitch = kf(u, {{0.0f, -0.12f}, {0.62f, 0.55f}, {1.0f, 0.45f}});
        a.ctl.jaw = kf(u, {{0.0f, 0.8f}, {0.45f, 1.0f}, {0.64f, 0.08f}, {1.0f, 0.1f}});
        a.ctl.lean = kf(u, {{0.0f, 0.15f}, {0.62f, 0.35f}, {1.0f, 0.3f}});
        if (!bit && u > 0.64f) {
          bit = true;
          wound(w, neckOf(v) * 0.5f + chestOf(v) * 0.5f, vec3(0, 0.6f, 0) + dir * 0.5f, 1.3f, W_BITE);
          feathers(w, v, neckOf(v), dir, 14);
          fall.bleed = 1.3f;
          kill(w, 0.0f, dir * 0.15f, 0.1f);
          w.shake(0.3f);
          w.slowmo(0.35f, 0.5f);
        }
        if (bit) victimDown(w, dt);
        if (pt > 0.8f) next();
        break;
      }
      case 5: {  // it lifts its bloody head
        float u = easeInOut(seg(pt, 0.0f, 0.8f));
        a.ctl.neck = glm::mix(-0.9f, 0.2f, u);
        a.ctl.headPitch = glm::mix(0.45f, -0.1f, u);
        a.ctl.lean = glm::mix(0.3f, 0.0f, u);
        a.ctl.jaw = 0.25f * u;
        if (first) w.sound(SFX_GROWL, 0.8f, headOf(a), 0.85f);
        if (victimDown(w, dt) && pt > 1.0f) next();
        break;
      }
      case 6:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 7:
        return finishD(w);
    }
    return false;
  }
};

// Head ram and stomp: it rams the victim with its massive skull, stamps on it with a
// great foot and tears at it.
class DRexStomp : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  bool rammed = false, stamped = false, torn = false;
  vec3 lunge0{0}, body{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOffD(w)) next();
        break;
      case 1:  // head down, a short charge
        if (first) {
          walker = Walker(plan.mover, V - dir * (reachOf(a) * 0.6f + reachOf(v) * 0.3f + halfWidthOf(v) * 0.5f + 0.12f),
                          std::max(1.5f, L / 1.2f));
          walker.run = 1.0f;
          callOf(w, a, 0.7f);
        }
        a.ctl.headPitch = approach(a.ctl.headPitch, 0.3f, dt * 2.0f);
        a.ctl.neck = approach(a.ctl.neck, -0.3f, dt * 2.0f);
        victimGuard(v, seg(pt, 0.0f, 0.6f));
        brace(w, 1.0f);
        if (walker.update(w, dt) || walker.s > walker.len - 0.03f) next();
        break;
      case 2: {  // the ram
        if (first) lunge0 = a.pos;
        float u = seg(pt, 0.0f, 0.25f);
        a.ctl.neck = glm::mix(-0.3f, -0.85f, easeIn(u));
        a.ctl.lean = 0.3f * u;
        a.pos = lunge0 + dir * 0.12f * easeIn(u);
        if (!rammed && u >= 1.0f) {
          rammed = true;
          w.sound(SFX_THUD, 1.0f, headOf(a), 0.7f);
          w.sound(SFX_CRUNCH, 0.7f, headOf(a), 0.85f);
          wound(w, neckOf(v), dir + vec3(0, 0.3f, 0), 0.5f, W_CRUSH);
          feathers(w, v, neckOf(v), dir, 12);
          knockFlying(w, dir * 2.0f + vec3(0, 1.8f, 0), w.fx.rnd() < 0.5f ? 8.0f : -8.0f);
          w.shake(0.4f);
          w.slowmo(0.35f, 0.4f);
        }
        if (rammed) victimDown(w, dt);
        if (pt > 0.45f) next();
        break;
      }
      case 3: {  // waiting for it to fall
        a.ctl.neck = approach(a.ctl.neck, 0.0f, dt * 2.0f);
        a.ctl.lean = approach(a.ctl.lean, 0.0f, dt * 2.0f);
        bool down = victimDown(w, dt);
        if (down) {
          body = v.pos;
          if (Puppet* cp = w.puppet(tumble.corpse >= 0 ? tumble.corpse : fall.corpse)) body = cp->pos;
          next();
        }
        break;
      }
      case 4:  // up to the body
        if (first) walker = Walker(plan.mover, body - flatDir(a.pos, body) * 0.42f * a.scale, 0.9f);
        relax(a, dt);
        if (walker.update(w, dt)) next();
        break;
      case 5: {  // the great foot raised, and stamped down
        float u = seg(pt, 0.0f, 0.55f);
        a.ctl.kick = kf(u, {{0.0f, 0.0f}, {0.65f, 0.75f}, {0.85f, 0.0f}});
        a.ctl.lean = -0.1f * bump(u, 0.0f, 0.8f);
        if (!stamped && u > 0.85f) {
          stamped = true;
          w.sound(SFX_QUAKE, 1.0f, body, 0.85f);
          w.sound(SFX_CRUNCH, 0.9f, body, 0.85f);
          bloodSpray(w, body + vec3(0, 0.06f, 0), vec3(0, 1, 0), 45, 2.0f, 0.7f);
          splatter(w, body, a.forward(), 5, 0.35f);
          w.fx.dust(body, 20, 0.35f, 1.0f);
          w.shake(0.4f);
        }
        if (pt > 0.7f) next();
        break;
      }
      case 6: {  // and the jaws: it tears at the body
        float u = seg(pt, 0.0f, 0.9f);
        a.ctl.neck = kf(u, {{0.0f, 0.0f}, {0.35f, -1.15f}, {0.6f, -1.1f}, {0.85f, 0.25f}});
        a.ctl.headPitch = kf(u, {{0.0f, 0.0f}, {0.35f, 0.6f}, {0.85f, -0.2f}});
        a.ctl.jaw = kf(u, {{0.0f, 0.2f}, {0.25f, 0.9f}, {0.4f, 0.05f}, {0.85f, 0.15f}});
        a.ctl.lean = kf(u, {{0.0f, 0.0f}, {0.35f, 0.3f}, {0.85f, 0.0f}});
        if (!torn && u > 0.62f) {
          torn = true;
          bloodSpray(w, mouthOf(a), vec3(0, 1, 0) - a.forward() * 0.3f, 50, 2.3f, 0.5f);
          w.sound(SFX_TEAR, 1.0f, body, 0.85f);
          w.sound(SFX_BLOOD, 0.6f, body, 1.0f);
        }
        if (pt > 1.0f) next();
        break;
      }
      case 7:
        if (first) callOf(w, a);
        triumph(w, pt / 1.6f);
        if (pt > 1.6f) next();
        break;
      case 8:
        relax(a, dt);
        if (moveInD(w, dt)) next();
        break;
      case 9:
        return finishD(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Battles off: the victim falls where it stands while the attacker moves in.
// ---------------------------------------------------------------------------
class DQuickCapture : public DBattle {
 public:
  using DBattle::DBattle;

 protected:
  bool step(World& w, float dt) override {
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (enter()) setup(w);
        v.tremble = 1.0f;
        if (pt > 0.15f) {
          bloodSpray(w, chestOf(v), dir + vec3(0, 0.3f, 0), 25, 2.0f);
          w.sound(SFX_TEAR, 0.6f, v.pos);
          fall.life = 0.25f;
          fall.cry = false;
          kill(w, 0.0f, dir * 0.4f, 0.1f);
          next();
        }
        break;
      case 1: {
        bool down = victimDown(w, dt);
        bool in = moveInD(w, dt, 1.4f);
        if (down && in) next();
        break;
      }
      case 2:
        return finishD(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Checkmate: a meteor. The tyrant king looks up at a light growing in the sky and
// roars at it; it strikes the board beside him in a blinding blast that throws him
// down - the end of the dinosaurs.
// ---------------------------------------------------------------------------
class DCheckmate : public Staged {
 public:
  DCheckmate(int king, int winner) : king_(king), winner_(winner) {}

 protected:
  int king_, winner_;
  DFall fall;
  int meteor = -1;
  vec3 from{0}, impact{0};
  bool roared = false;
  bool step(World& w, float dt) override {
    Actor& k = w.actors[king_];
    const float hit = 2.2f;
    switch (phase) {
      case 0: {
        if (enter()) {
          vec3 side(std::cos(k.yaw), 0, -std::sin(k.yaw));
          impact = clampBoard(flat(k.pos) + side * 1.1f + k.forward() * 0.5f, 3.9f);
          vec3 away = glm::normalize(flat(impact - vec3(0)) + vec3(0.3f, 0, 0.2f));
          from = impact + away * 7.0f + vec3(0, 9.0f, 0);
          meteor = w.addPuppet(dn::D_METEOR, 0, 4);
          if (Puppet* p = w.puppet(meteor)) {
            p->pos = from;
            p->scale = 0.16f;
            p->shadow = false;
          }
          w.sound(SFX_METEOR, 1.0f, impact);
          w.sound(SFX_MATE, 0.6f, k.pos);
          if (w.cinematic) {
            CamPose p;
            p.target = glm::mix(k.pos, impact, 0.4f) + vec3(0, 0.9f, 0);
            vec3 player = glm::normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4f));
            vec3 back = glm::normalize(player * 1.0f - flatDir(k.pos, impact) * 0.35f + vec3(1e-4f));
            p.pos = p.target + back * 4.2f + vec3(0, 1.1f, 0);
            p.fov = 38;
            p.focus = glm::length(p.pos - p.target);
            p.aperture = 7.0f;
            w.cam.setShot(p, 0.9f);
          }
        }
        k.idleWeight = 0;
        float u = std::min(1.0f, pt / hit);
        vec3 pos = glm::mix(from, impact, u * u * (0.6f + 0.4f * u));
        if (Puppet* p = w.puppet(meteor)) {
          p->pos = pos;
          p->glow = glm::vec4(1.0f, 0.45f, 0.1f, 0.6f + 1.4f * u);
          p->yaw += dt * 3.0f;
        }
        // the fiery trail
        vec3 back = glm::normalize(from - impact);
        for (int i = 0; i < 3; ++i) {
          Particle q;
          q.pos = pos + back * w.fx.rnd(0.0f, 0.25f) + w.fx.rndDir() * 0.06f;
          q.vel = back * w.fx.rnd(0.5f, 1.5f) + w.fx.rndDir() * 0.3f;
          q.size = w.fx.rnd(0.08f, 0.2f);
          q.grow = 0.4f;
          q.color = vec3(4.0f, 1.6f, 0.4f);
          q.colorEnd = vec3(0.6f, 0.15f, 0.05f);
          q.opacity = 0;
          q.maxLife = w.fx.rnd(0.3f, 0.6f);
          q.type = PT_GLOW;
          q.drag = 1.0f;
          w.fx.add(q);
        }
        if (w.fx.rnd() < dt * 20.0f) w.fx.smoke(pos + back * 0.2f, 1, 0.1f, 0.2f, 0.6f);
        w.light(pos, vec3(6.0f, 3.0f, 1.2f) * (0.4f + u), 3.0f + 3.0f * u, 0.05f);
        // the king looks up at it, and roars
        vec3 toM = pos - headOf(k);
        k.ctl.headYaw = glm::clamp(wrapAngle(faceAngle(toM) - k.yaw), -1.0f, 1.0f) * 0.8f;
        k.ctl.neck = 0.5f * seg(pt, 0.2f, 0.8f);
        k.ctl.headPitch = -0.4f * seg(pt, 0.2f, 0.8f);
        if (!roared && pt > 0.8f) {
          roared = true;
          w.sound(SFX_REXROAR, 0.9f, headOf(k), 0.95f);
        }
        k.ctl.jaw = 0.95f * bump(pt, 0.8f, 2.0f);
        k.tremble = 0.3f * u;
        if (pt >= hit) {
          if (Puppet* p = w.puppet(meteor)) p->alive = false;
          w.fx.fireball(impact + vec3(0, 0.3f, 0), 1.6f);
          w.fx.embers(impact + vec3(0, 0.2f, 0), 60, 4.0f);
          w.fx.smoke(impact, 30, 0.8f, 1.2f, 0.75f);
          w.fx.dust(impact, 60, 1.2f, 3.0f);
          w.fx.flash(impact + vec3(0, 0.4f, 0), 3.0f, vec3(6.0f, 4.5f, 3.0f), 0.25f);
          w.addFlash(vec3(1.5f, 1.1f, 0.8f));
          w.light(impact + vec3(0, 0.5f, 0), vec3(12.0f, 6.0f, 2.0f), 7.0f, 1.2f);
          w.groundRing(impact, vec3(1.0f, 0.6f, 0.3f), 4.5f, 1.0f, 0.12f);
          w.groundRing(impact, vec3(0.8f, 0.7f, 0.55f), 2.5f, 1.4f, 0.08f);
          w.shake(1.0f);
          vec3 away = flatDir(impact, k.pos);
          vec3 left(std::cos(k.yaw), 0, -std::sin(k.yaw));
          fall.life = 6.0f;
          fall.bleed = 1.3f;
          fall.start(w, k, glm::dot(away, left) >= 0 ? 1.0f : -1.0f, away * 0.9f, 0.25f);
          bloodSpray(w, chestOf(k), away + vec3(0, 0.5f, 0), 40, 2.0f);
          w.slowmo(0.4f, 0.8f);
          next();
        }
        break;
      }
      case 1:
        if (std::fmod(pt, 0.08f) < w.lastDt) w.fx.smoke(impact + vec3(0, 0.1f, 0), 2, 0.5f, 0.8f, 0.7f);
        if (fall.update(w, dt) && pt > 1.2f) next();
        break;
      case 2:  // the winners celebrate
        for (Actor& a : w.actors)
          if (a.alive && a.color == winner_) a.selected = pt > (a.phase - std::floor(a.phase)) * 0.4f && pt < 2.4f;
        if (pt > 0.2f && pt - w.lastDt <= 0.2f) {
          for (const Actor& a : w.actors)
            if (a.alive && a.color == winner_ && a.kind == K_KING) {
              w.sound(SFX_REXROAR, 0.7f, a.pos, 1.0f);
              break;
            }
        }
        if (pt > 2.6f) {
          for (Actor& a : w.actors) a.selected = false;
          if (w.cinematic) w.cam.release(1.2f);
          return true;
        }
        break;
    }
    return false;
  }
};

const char* const kDinoStyleNames[meshgen::K_COUNT][kBattleStyles] = {
    {"Killing claw", "Pack attack", "Dodge and bite"},
    {"Dive", "Wing buffet", "Talon strike"},
    {"Horn charge", "Horn lock", "Frill bash"},
    {"Rearing stomp", "Neck swing", "Tail lash"},
    {"Hooked claws", "Snatch", "Tail swipe"},
    {"Bone-crusher", "The roar", "Head ram"},
};

}  // namespace

const char* dinoBattleStyleName(int kind, int style) {
  if (kind < 0 || kind >= meshgen::K_COUNT || style < 0 || style >= kBattleStyles) return "";
  return kDinoStyleNames[kind][style];
}

std::unique_ptr<Anim> makeDinoBattle(World& w, int kind, int style, const MovePlan& plan) {
  (void)w;
  switch (kind * kBattleStyles + style) {
    case K_PAWN * kBattleStyles + 0: return std::make_unique<DRaptorClaw>(plan);
    case K_PAWN * kBattleStyles + 1: return std::make_unique<DRaptorPack>(plan);
    case K_PAWN * kBattleStyles + 2: return std::make_unique<DRaptorDodge>(plan);
    case K_KNIGHT * kBattleStyles + 0: return std::make_unique<DPteroDive>(plan);
    case K_KNIGHT * kBattleStyles + 1: return std::make_unique<DPteroBuffet>(plan);
    case K_KNIGHT * kBattleStyles + 2: return std::make_unique<DPteroTalons>(plan);
    case K_BISHOP * kBattleStyles + 0: return std::make_unique<DTrikeCharge>(plan);
    case K_BISHOP * kBattleStyles + 1: return std::make_unique<DTrikeLock>(plan);
    case K_BISHOP * kBattleStyles + 2: return std::make_unique<DTrikeBash>(plan);
    case K_ROOK * kBattleStyles + 0: return std::make_unique<DBrachioStomp>(plan);
    case K_ROOK * kBattleStyles + 1: return std::make_unique<DBrachioNeck>(plan);
    case K_ROOK * kBattleStyles + 2: return std::make_unique<DBrachioTail>(plan);
    case K_QUEEN * kBattleStyles + 0: return std::make_unique<DSpinoClaws>(plan);
    case K_QUEEN * kBattleStyles + 1: return std::make_unique<DSpinoSnatch>(plan);
    case K_QUEEN * kBattleStyles + 2: return std::make_unique<DSpinoTail>(plan);
    case K_KING * kBattleStyles + 0: return std::make_unique<DRexCrush>(plan);
    case K_KING * kBattleStyles + 1: return std::make_unique<DRexRoar>(plan);
    default: return std::make_unique<DRexStomp>(plan);
  }
}

std::unique_ptr<Anim> makeDinoQuiet(const MovePlan& plan) { return std::make_unique<DQuietMove>(plan); }
std::unique_ptr<Anim> makeDinoQuickCapture(const MovePlan& plan) { return std::make_unique<DQuickCapture>(plan); }
std::unique_ptr<Anim> makeDinoCheckmate(int kingId, int winnerColor) {
  return std::make_unique<DCheckmate>(kingId, winnerColor);
}
