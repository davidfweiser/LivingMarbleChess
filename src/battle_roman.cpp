// Choreography for the Roman army. Legionaries march, chariots gallop and leap,
// the siege tower rolls, the centurion, the empress and the emperor stride
// across the board. The Romans don't shatter like marble: they fight hand to
// hand, bleed, and fall - and the dead lie in their blood for a moment before
// they fade away. Captures:
//
//   Pawn    pilum (a javelin thrown from afar), shield and gladius (blocks the
//           victim's blow with his scutum, punches with the boss and stabs),
//           vault (pole-vaults over the victim and stabs it in the back)
//   Knight  run down (horses and wheels), scythed wheels (slashes past),
//           charioteer's leap (he somersaults out of the car onto the victim)
//   Bishop  duel (parries twice, then a spinning cut and the dagger), leaping
//           strike (a somersault ending in a downward thrust), thrown knife
//   Rook    battering ram, sally (two legionaries charge out of the door and
//           cut the victim down), scorpion (a bolt from the roof)
//   Queen   hidden dagger, poisoned cup, whirling spear (sweeps the victim's
//           legs, flips it into the air and spears it on the ground)
//   King    thumbs down (a volley of pila rains from the sky), gladius duel,
//           the lion (a lion bounds onto the board and mauls the victim)
//
// Victims fight back where they can: they raise their shields and weapons and
// strike at the attacker. A siege tower that is beaten collapses in splinters;
// a chariot overturns.
#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "battle.h"
#include "battle_kit.h"

namespace md = medieval;
namespace rm = roman;

namespace {
using namespace bkit;

const float kTau = 6.2831853f;
const vec3 kBlood(0.3f, 0.004f, 0.003f);
const vec3 kSpark(3.2f, 2.6f, 1.8f);

float approach(float x, float target, float rate) {
  return x < target ? std::min(target, x + rate) : std::max(target, x - rate);
}
// progress through [a, b] (0 before, 1 after)
float seg(float t, float a, float b) { return glm::clamp((t - a) / (b - a), 0.0f, 1.0f); }
float bump(float t, float a, float b) { return std::sin(seg(t, a, b) * kPI); }

bool isHuman(const Actor& a) { return rm::human(a.kind); }
bool isChariot(const Actor& a) { return a.kind == K_KNIGHT; }
bool isTower(const Actor& a) { return a.kind == K_ROOK; }

// world position of a point riding on a bone (rest-pose model coordinates)
vec3 bonePoint(const Actor& a, int bone, const vec3& rest) {
  return vec3(a.model() * md::boneMatrix(a.pose, bone) * glm::vec4(rest, 1.0f));
}
vec3 puppetPoint(const Puppet& p, int bone, const vec3& rest) {
  return vec3(p.model() * md::boneMatrix(p.pose, bone) * glm::vec4(rest, 1.0f));
}
vec3 modelPoint(const Actor& a, const vec3& rest) { return vec3(a.model() * glm::vec4(rest, 1.0f)); }

// where blows land, the head, the weapons
vec3 chestOf(const Actor& a) {
  if (isChariot(a)) return bonePoint(a, rm::CH_DRIVER, rm::chestPoint(K_KNIGHT));
  if (isTower(a)) return modelPoint(a, rm::chestPoint(K_ROOK));
  return bonePoint(a, rm::RH_CHEST, rm::chestPoint(a.kind));
}
vec3 headOf(const Actor& a) {
  if (isChariot(a)) return bonePoint(a, rm::CH_DHEAD, rm::headPoint(K_KNIGHT));
  if (isTower(a)) return modelPoint(a, rm::headPoint(K_ROOK));
  return bonePoint(a, rm::RH_HEAD, rm::headPoint(a.kind));
}
vec3 bladeTipOf(const Actor& a) {
  if (isChariot(a)) return bonePoint(a, rm::CH_ITEM, rm::itemTip(K_KNIGHT));
  if (a.kind == K_BISHOP) return bonePoint(a, rm::RH_ITEMR, rm::itemTip(K_BISHOP));
  if (a.ctl.draw > 0.5f) return bonePoint(a, rm::RH_BLADE, rm::bladeTip(a.kind));
  return bonePoint(a, rm::RH_ITEMR, rm::itemTip(a.kind));
}
vec3 handOf(const Actor& a, bool right) {
  if (isChariot(a)) return bonePoint(a, right ? rm::CH_ITEM : rm::CH_DLFORE, right ? rm::handR(K_KNIGHT) : rm::handL(K_KNIGHT));
  if (!isHuman(a)) return chestOf(a);
  return bonePoint(a, right ? rm::RH_ITEMR : rm::RH_ITEML, right ? rm::handR(a.kind) : rm::handL(a.kind));
}

// ---------------------------------------------------------------------------
// Blood
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
  int id = w.addPuppet(rm::R_POOL, 0, 3);
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
// a wound: the spray, splashes on the board and the sounds of it
void wound(World& w, const vec3& at, const vec3& dir, float power, bool slash = false) {
  bloodSpray(w, at, dir, (int)(80 * power), 2.2f + 1.6f * power);
  splatter(w, at, dir, 4 + (int)(4 * power));
  w.sound(slash ? SFX_SLASH : SFX_STAB, 0.9f, at, w.fx.rnd(0.9f, 1.1f));
  w.sound(SFX_BLOOD, 0.55f * power, at, w.fx.rnd(0.9f, 1.15f));
}
void sparksAt(World& w, const vec3& at, const vec3& dir, int n = 22) {
  w.fx.sparks(at, dir, n, 3.0f, 0.7f, kSpark);
  w.fx.flash(at, 0.25f, vec3(2.2f, 2.0f, 1.6f), 0.07f);
  w.light(at, vec3(5.0f, 4.2f, 3.2f), 2.0f, 0.12f);
}
// wooden splinters (a shield struck, the tower breaking up)
void splinters(World& w, const vec3& at, const vec3& dir, int n) {
  for (int i = 0; i < n; ++i) {
    vec3 d = glm::normalize(dir + w.fx.rndDir() * 0.9f + vec3(0, 0.4f, 0));
    Particle p;
    p.pos = at;
    p.vel = d * w.fx.rnd(1.5f, 4.5f);
    p.size = w.fx.rnd(0.006f, 0.013f);
    p.color = vec3(0.45f, 0.3f, 0.16f) * w.fx.rnd(0.6f, 1.2f);
    p.colorEnd = p.color;
    p.opacity = 1.0f;
    p.maxLife = w.fx.rnd(0.8f, 1.6f);
    p.type = PT_SPARK;
    p.stretch = 0.02f;
    p.gravity = 9.8f;
    p.drag = 0.6f;
    p.bounce = true;
    p.fadeIn = 0.0f;
    w.fx.add(p);
  }
}

// ---------------------------------------------------------------------------
// Poses by keyframes: a human's (or the charioteer's) arms, torso and weapon
// ---------------------------------------------------------------------------
struct Key {
  float t;
  vec3 armR, armL;
  float lean, twist, crouch, pitch;
};
inline Key K(float t, vec3 aR, vec3 aL = vec3(0), float lean = 0, float twist = 0, float crouch = 0, float pitch = 0) {
  return {t, aR, aL, lean, twist, crouch, pitch};
}
// eases between the keys (held before the first and after the last)
void play(md::Controls& c, std::initializer_list<Key> keys, float t) {
  const Key* k = keys.begin();
  size_t n = keys.size();
  size_t i = 0;
  while (i + 1 < n && t > k[i + 1].t) ++i;
  const Key& a = k[i];
  const Key& b = k[std::min(i + 1, n - 1)];
  float u = (i + 1 < n && b.t > a.t) ? glm::smoothstep(a.t, b.t, t) : (t >= b.t ? 1.0f : 0.0f);
  if (i + 1 >= n) u = 1.0f;
  c.armR = glm::mix(a.armR, b.armR, u);
  c.armL = glm::mix(a.armL, b.armL, u);
  c.lean = glm::mix(a.lean, b.lean, u);
  c.twist = glm::mix(a.twist, b.twist, u);
  c.crouch = glm::mix(a.crouch, b.crouch, u);
  c.itemPitch = glm::mix(a.pitch, b.pitch, u);
}

// A fighter's moves, as functions of their progress u (0..1).
// On guard: weapon up and forward, shield (or left hand) raised.
void guard(md::Controls& c, float u, bool shield) {
  c.armR = vec3(0.45f, 0.1f, 0.45f) * u;
  c.armL = (shield ? vec3(0.55f, 0.05f, -0.15f) : vec3(0.4f, 0.1f, 0.3f)) * u;
  c.itemPitch = 0.6f * u;
  c.crouch = 0.15f * u;
  c.lean = 0.06f * u;
}
// an overhead blow: wind up (to 0.45), strike (to 0.6), recover
void overhead(md::Controls& c, float u) {
  play(c, {K(0.0f, vec3(0.45f, 0.1f, 0.45f), vec3(0.4f, 0.1f, 0.2f), 0.06f, 0, 0.15f, 0.6f),
           K(0.42f, vec3(2.7f, 0.25f, 1.1f), vec3(0.6f, 0.2f, 0.4f), -0.12f, 0.2f, 0.05f, -0.4f),
           K(0.58f, vec3(0.75f, 0.1f, -0.25f), vec3(0.2f, 0.2f, 0.2f), 0.3f, -0.15f, 0.25f, 1.5f),
           K(1.0f, vec3(0.45f, 0.1f, 0.45f), vec3(0.4f, 0.1f, 0.2f), 0.06f, 0, 0.15f, 0.6f)},
       u);
  c.gait = kPI * 0.5f;
  c.stride = 0.5f * bump(u, 0.35f, 1.0f);
}
// a thrust: draw back (to 0.4), lunge (to 0.55), recover
void thrust(md::Controls& c, float u) {
  play(c, {K(0.0f, vec3(0.45f, 0.1f, 0.45f), vec3(0.4f, 0.1f, 0.2f), 0.06f, 0, 0.15f, 0.6f),
           K(0.4f, vec3(0.1f, 0.25f, 1.2f), vec3(0.6f, 0.0f, 0.0f), -0.05f, -0.35f, 0.2f, 1.4f),
           K(0.55f, vec3(1.25f, 0.0f, -0.55f), vec3(0.2f, 0.1f, 0.3f), 0.32f, 0.3f, 0.3f, 1.55f),
           K(1.0f, vec3(0.45f, 0.1f, 0.45f), vec3(0.4f, 0.1f, 0.2f), 0.06f, 0, 0.15f, 0.6f)},
       u);
  c.gait = kPI * 0.5f;
  c.stride = 0.7f * bump(u, 0.35f, 1.0f);
}
// a sweeping cut from right to left
void sweep(md::Controls& c, float u) {
  play(c, {K(0.0f, vec3(0.45f, 0.1f, 0.45f), vec3(0.4f, 0.1f, 0.2f), 0.06f, 0, 0.15f, 0.6f),
           K(0.4f, vec3(1.3f, 1.3f, 0.3f), vec3(0.6f, 0.3f, 0.3f), 0.0f, 0.75f, 0.2f, 1.5f),
           K(0.6f, vec3(1.4f, -0.45f, 0.1f), vec3(0.3f, 0.1f, 0.1f), 0.18f, -0.8f, 0.25f, 1.5f),
           K(1.0f, vec3(0.45f, 0.1f, 0.45f), vec3(0.4f, 0.1f, 0.2f), 0.06f, 0, 0.15f, 0.6f)},
       u);
}
// a parry: the blade raised across the body to catch a blow
void parry(md::Controls& c, float u, bool high) {
  vec3 r = high ? vec3(1.9f, 0.15f, 0.55f) : vec3(1.0f, -0.25f, 0.6f);
  c.armR = glm::mix(vec3(0.45f, 0.1f, 0.45f), r, u);
  c.itemPitch = glm::mix(0.6f, high ? 1.6f : 0.4f, u);
  c.itemYaw = (high ? 1.2f : 0.0f) * u;
  c.crouch = 0.15f + 0.1f * u;
  c.lean = 0.06f - 0.12f * u;
}
// the scutum (or the left arm) thrown up against a blow
void block(md::Controls& c, float u) {
  c.armL = glm::mix(vec3(0.55f, 0.05f, -0.15f), vec3(1.25f, 0.0f, -0.2f), u);
  c.crouch = 0.15f + 0.15f * u;
  c.lean = 0.06f - 0.1f * u;
}

// The victim's part: on guard, and striking at the attacker (whatever it is).
void victimGuard(Actor& v, float u) {
  if (isHuman(v)) {
    if (v.kind == K_PAWN) v.ctl.draw = u > 0.3f ? 1.0f : 0.0f;  // a legionary draws his sword
    if (v.kind == K_QUEEN || v.kind == K_KING) v.ctl.draw = u > 0.3f ? 1.0f : 0.0f;
    guard(v.ctl, u, v.kind == K_PAWN);
  } else if (isChariot(v)) {
    v.ctl.armR = vec3(0.6f, 0.0f, 0.4f) * u;
    v.ctl.itemPitch = 1.2f * u;
    v.ctl.headYaw = 0;
  }
}
void victimStrike(Actor& v, float u) {
  if (isHuman(v)) overhead(v.ctl, u);
  else if (isChariot(v)) {
    play(v.ctl, {K(0.0f, vec3(0.6f, 0.0f, 0.4f), vec3(0), 0, 0, 0, 1.2f), K(0.4f, vec3(0.2f, 0.0f, 0.9f), vec3(0), -0.15f, -0.2f, 0, 1.3f),
                 K(0.55f, vec3(1.0f, 0.0f, -0.3f), vec3(0), 0.3f, 0.2f, 0, 1.4f), K(1.0f, vec3(0.6f, 0.0f, 0.4f), vec3(0), 0, 0, 0, 1.2f)},
         u);
    v.ctl.rear = 0.3f * bump(u, 0.2f, 0.8f);
  }
}

// ---------------------------------------------------------------------------
// Falling: a Roman struck down staggers, his knees give way and he falls on his
// back (or on his face); a chariot overturns; a siege tower collapses. The body
// lies in its blood a moment, then fades away (a puppet: the piece is gone).
// ---------------------------------------------------------------------------
int makeCorpse(World& w, Actor& v, float life) {
  int id = w.addPuppet(v.kind, v.color, 3);
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

// the siege tower breaks up into timbers
void collapseTower(World& w, Actor& v, const vec3& impact, const vec3& push) {
  vec3 c = v.pos + vec3(0, v.height() * 0.5f, 0);
  splinters(w, c, push + vec3(0, 0.5f, 0), 70);
  w.fx.dust(flat(v.pos), 30, 0.6f, 1.6f);
  w.fx.smoke(c, 10, 0.3f, 0.4f, 0.5f);
  w.sound(SFX_SPLINTER, 1.0f, c, 0.7f);
  w.sound(SFX_CREAK, 0.8f, c, 0.8f);
  w.shatter(v, impact, push, 1.2f, 0.6f, 6.0f);
  w.shake(0.4f);
}

struct Fall {
  int actor = -1, corpse = -1;
  float t = 0, dir = 1;   // +1 on the back, -1 on the face (chariot: +1 tips to its left, -1 right)
  float stagger = 0.35f;  // seconds of staggering first
  vec3 push{0};           // knocked back along this (world, per second)
  bool landed = false, done = false, cry = true;
  float life = 1.6f;      // seconds the body lies there before it fades
  float bleed = 1.0f;     // the size of the pool it leaves
  float settle = 0;
  bool active() const { return actor >= 0 && !done; }

  void start(World& w, Actor& v, float direction, const vec3& pushVel, float staggerTime = 0.35f) {
    actor = v.id;
    dir = direction;
    push = flat(pushVel);
    stagger = staggerTime;
    t = 0;
    landed = done = false;
    v.idleWeight = 0;
    v.tremble = 0;
    v.yawRate = 2.0f;
    if (isTower(v)) {
      collapseTower(w, v, chestOf(v), push * 2.0f + vec3(0, 0.5f, 0));
      done = true;
      return;
    }
    if (cry) w.sound(isChariot(v) ? SFX_NEIGH : SFX_DEATH, isChariot(v) ? 1.0f : 0.8f, chestOf(v), w.fx.rnd(0.85f, 1.1f));
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
    if (isChariot(v)) return updateChariot(w, v, dt);
    md::Controls& c = v.ctl;
    if (t < stagger) {  // staggering: reeling back (or doubling over), arms thrown out
      float u = t / std::max(stagger, 1e-3f);
      v.pos += push * dt * (1.0f - 0.5f * u);
      c.lean = glm::mix(c.lean, dir > 0 ? -0.25f : 0.45f, std::min(1.0f, dt * 8.0f));
      c.headPitch = glm::mix(c.headPitch, dir > 0 ? -0.3f : 0.3f, std::min(1.0f, dt * 8.0f));
      c.crouch = approach(c.crouch, 0.35f, dt * 1.5f);
      c.armR = glm::mix(c.armR, vec3(0.6f, 0.5f, 0.6f), std::min(1.0f, dt * 5.0f));
      c.armL = glm::mix(c.armL, vec3(0.7f, 0.5f, 0.6f), std::min(1.0f, dt * 5.0f));
      c.stride = 0.3f * (1.0f - u);
      c.gait += dt * 6.0f;
      return false;
    }
    float ft = t - stagger;
    const float dur = 0.55f;
    float u = glm::clamp(ft / dur, 0.0f, 1.0f);
    float ang = 1.5f * u * u;  // falling faster and faster
    if (!landed && u >= 1.0f) {
      landed = true;
      settle = 0;
      vec3 p = flat(v.pos);
      w.sound(SFX_FALL, 0.9f, p, w.fx.rnd(0.9f, 1.1f));
      w.fx.dust(p - v.forward() * dir * 0.3f, 14, 0.35f, 0.9f);
      w.shake(0.12f);
      // the blood pools under the body
      vec3 chest = flat(v.pos) - v.forward() * dir * 0.3f * v.scale;
      bloodPool(w, chest, 0.27f * bleed * v.scale, 1.5f, 9.0f);
    }
    if (landed) {  // a little bounce, then still
      settle += dt;
      ang = 1.5f - 0.08f * std::exp(-settle * 9.0f) * std::sin(settle * 26.0f);
    } else {
      v.pos += push * dt * (1.0f - u) * 0.6f;
    }
    v.tiltPivot = vec3(0, 0, -dir * 0.075f);
    v.tilt = glm::angleAxis(-dir * ang, vec3(1, 0, 0));
    c.dead = u;
    c.crouch = 0.35f * (1.0f - u);
    c.lean = glm::mix(dir > 0 ? -0.25f : 0.45f, 0.0f, u);
    c.stride = 0;
    if (landed && settle > 0.4f) {
      corpse = makeCorpse(w, v, life);
      done = true;
    }
    return done;
  }

  bool updateChariot(World& w, Actor& v, float dt) {
    md::Controls& c = v.ctl;
    if (t < stagger) {  // the horses rear and scream, the car slews
      c.rear = approach(c.rear, 0.8f, dt * 3.0f);
      v.pos += push * dt;
      c.stride = 0;
      return false;
    }
    float u = glm::clamp((t - stagger) / 0.6f, 0.0f, 1.0f);
    float ang = 1.45f * u * u;
    c.rear = 0.8f * (1.0f - u);
    if (!landed && u >= 1.0f) {
      landed = true;
      settle = 0;
      vec3 p = flat(v.pos);
      w.sound(SFX_FALL, 1.0f, p, 0.8f);
      w.sound(SFX_SPLINTER, 0.8f, p, 0.9f);
      w.fx.dust(p, 26, 0.5f, 1.4f);
      splinters(w, p + vec3(0, 0.15f, 0), vec3(0, 1, 0), 30);
      w.shake(0.3f);
      bloodPool(w, p + v.forward() * 0.1f, 0.24f * bleed, 1.5f, 9.0f);
    }
    if (landed) {
      settle += dt;
      ang = 1.45f - 0.07f * std::exp(-settle * 9.0f) * std::sin(settle * 24.0f);
    } else {
      v.pos += push * dt * (1.0f - u);
    }
    v.tiltPivot = vec3(dir * 0.2f, 0, 0);
    v.tilt = glm::angleAxis(-dir * ang, vec3(0, 0, 1));
    c.dead = u;
    if (landed && settle > 0.45f) {
      corpse = makeCorpse(w, v, life);
      done = true;
    }
    return done;
  }
};

// A body knocked flying: it tumbles through the air, crashes down, slides to a
// stop on its back or its face and lies there (then fades). Spinning about its
// middle. (A chariot overturns instead, a siege tower collapses: see knockFlying.)
struct Tumble {
  int actor = -1, corpse = -1;
  vec3 vel{0};
  float spin = 0, angle = 0, target = 0, t = 0, settle = 0, hmid = 0.4f;
  bool landed = false, done = false;
  float life = 1.6f, bleed = 1.0f;
  bool active() const { return actor >= 0 && !done; }
  void launch(World& w, Actor& v, const vec3& velocity, float spinRate) {
    actor = v.id;
    vel = velocity;
    spin = spinRate;
    angle = t = settle = 0;
    landed = done = false;
    v.idleWeight = 0;
    v.tremble = 0;
    hmid = v.height() / std::max(v.scale, 1e-3f) * 0.45f;
    v.tiltPivot = vec3(0, hmid, 0);
    w.sound(SFX_DEATH, 0.8f, chestOf(v), w.fx.rnd(0.9f, 1.15f));
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
    if (!landed) {
      vel.y -= 14.0f * dt;
      v.pos += vel * dt;
      angle += spin * dt;
      v.tilt = glm::angleAxis(-angle, vec3(1, 0, 0));
      c.dead = std::min(1.0f, 0.5f + t);
      c.cape = 1.0f;
      float h = hmid * v.scale;
      float low = v.pos.y + h - (std::fabs(std::cos(angle)) * h + std::fabs(std::sin(angle)) * 0.07f * v.scale);
      if (overBoard(v.pos) && low <= 0.0f && vel.y < 0.0f && t > 0.15f) {
        landed = true;
        // lie on the back or on the face, whichever is nearer
        float k = std::round((angle - kPI * 0.5f) / kPI);
        target = kPI * 0.5f + k * kPI;
        vec3 p = flat(v.pos);
        w.sound(SFX_FALL, 1.0f, p, w.fx.rnd(0.85f, 1.0f));
        w.fx.dust(p, 18, 0.4f, 1.2f);
        w.shake(0.18f);
        bloodPool(w, p, 0.27f * bleed * v.scale, 1.4f, 9.0f);
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
    v.pos.y += ((0.065f - hmid) * v.scale - v.pos.y) * (1.0f - std::exp(-dt * 18.0f));
    v.tilt = glm::angleAxis(-angle, vec3(1, 0, 0));
    c.dead = 1.0f;
    c.cape = approach(c.cape, 0.0f, dt * 3.0f);
    if (settle > 0.45f) {
      corpse = makeCorpse(w, v, life);
      done = true;
    }
    return done;
  }
};

// ---------------------------------------------------------------------------
// Walker: on foot, at the gallop (the chariot) or rolling (the siege tower)
// ---------------------------------------------------------------------------
struct Walker {
  enum Mode { FOOT, CHARIOT, TOWER };
  int actor = -1;
  vec3 from{0}, to{0};
  Mode mode = FOOT;
  float speed = 0.8f, accel = 1.6f, cycle = 0.3f, run = 0;
  float s = 0, v = 0, len = 0, turnWait = 0, creak = 0;
  int lastStep = 0;
  bool begun = false, face = true, soundOn = true;

  Walker() {}
  Walker(int a, const vec3& dest, Mode m, float spd) : actor(a), to(flat(dest)), mode(m), speed(spd) {}

  void begin(World& w) {
    Actor& a = w.actors[actor];
    from = flat(a.pos);
    len = glm::length(to - from);
    cycle = rm::strideCycle(a.kind);
    accel = mode == TOWER ? speed * 1.6f : speed * 2.2f;
    if (face && len > 0.05f) {
      a.yawTarget = faceAngle(to - from);
      turnWait = std::min(mode == TOWER ? 0.9f : 0.55f, std::fabs(wrapAngle(a.yawTarget - a.yaw)) * 0.3f);
    }
    lastStep = (int)std::floor(a.ctl.gait / kPI);
    begun = true;
    if (!soundOn || len < 0.05f) return;
    if (mode == CHARIOT) w.sound(SFX_WHEELS, std::min(1.0f, 0.4f + 0.25f * len), a.pos, w.fx.rnd(0.9f, 1.1f));
    if (mode == TOWER) {
      w.sound(SFX_CREAK, 0.7f, a.pos, w.fx.rnd(0.85f, 1.05f));
      w.sound(SFX_GRIND, 0.35f, a.pos, 0.7f);
    }
  }

  void footstep(World& w, Actor& a) {
    if (!soundOn) return;
    vec3 p = flat(a.pos);
    if (mode == FOOT) {
      if (a.kind == K_PAWN || a.kind == K_BISHOP) w.sound(SFX_ARMOR, 0.4f, p, w.fx.rnd(0.95f, 1.2f));
      else w.sound(SFX_STEP, 0.35f, p, w.fx.rnd(0.9f, 1.05f));
      if (speed > 1.3f) w.fx.dust(p, 1, 0.08f, 0.3f);
    } else if (mode == CHARIOT) {
      w.sound(SFX_HOOF, 0.45f + 0.3f * run, p + a.forward() * 0.3f, w.fx.rnd(0.95f, 1.2f));
      if (run > 0.4f) w.fx.dust(p + a.forward() * 0.25f, 2, 0.15f, 0.6f);
    }
  }

  bool update(World& w, float dt) {
    if (!begun) begin(w);
    Actor& a = w.actors[actor];
    if (turnWait > 0) {
      turnWait -= dt;
      if (mode != TOWER) {  // shuffle round on the spot
        a.ctl.gait += dt * 4.0f;
        a.ctl.stride = approach(a.ctl.stride, 0.25f, dt * 2.0f);
      }
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
    switch (mode) {
      case FOOT:
        a.ctl.gait += v * dt / cycle * kTau * (v > 1.2f ? 0.75f : 1.0f);
        a.ctl.stride = glm::clamp(v / std::max(0.1f, std::min(speed, 0.9f) * 0.6f), 0.0f, 1.0f);
        a.ctl.lean = approach(a.ctl.lean, v > 1.2f ? 0.18f : 0.0f, dt * 1.5f);
        a.ctl.cape = approach(a.ctl.cape, v > 1.2f ? 0.6f : 0.15f * v, dt * 2.0f);
        break;
      case CHARIOT:
        a.ctl.gait += v * dt / cycle * kTau;
        a.ctl.stride = glm::clamp(v / std::max(0.1f, speed * 0.5f), 0.0f, 1.0f);
        a.ctl.run = approach(a.ctl.run, run, dt * 3.0f);
        a.ctl.spin += v * dt / rm::wheelRadius(K_KNIGHT);
        break;
      case TOWER:
        a.ctl.spin += v * dt / rm::wheelRadius(K_ROOK);
        a.ctl.gait += v * dt / 0.18f * kTau;  // rocking on its wheels
        a.ctl.stride = glm::clamp(v / std::max(0.1f, speed), 0.0f, 1.0f);
        creak -= dt;
        if (creak <= 0 && soundOn) {
          creak = w.fx.rnd(0.7f, 1.2f);
          w.sound(SFX_CREAK, 0.45f, a.pos, w.fx.rnd(0.8f, 1.1f));
        }
        if (w.fx.rnd() < dt * 8.0f) w.fx.dust(p + vec3(w.fx.rnd(-0.2f, 0.2f), 0, w.fx.rnd(-0.2f, 0.2f)), 1, 0.05f, 0.2f);
        break;
    }
    if (mode != TOWER) {
      int step = (int)std::floor(a.ctl.gait / kPI);
      if (step != lastStep) {
        lastStep = step;
        footstep(w, a);
      }
    }
    if (s >= len - 1e-4f) {
      a.ctl.stride = approach(a.ctl.stride, 0.0f, dt * 4.0f);
      a.ctl.cape = approach(a.ctl.cape, 0.0f, dt * 2.0f);
      a.ctl.lean = approach(a.ctl.lean, 0.0f, dt * 2.0f);
      if (mode == CHARIOT) a.ctl.run = approach(a.ctl.run, 0.0f, dt * 3.0f);
      return a.ctl.stride <= 0.001f;
    }
    return false;
  }
};

Walker::Mode modeOf(const Actor& a) {
  return isChariot(a) ? Walker::CHARIOT : (isTower(a) ? Walker::TOWER : Walker::FOOT);
}
float paceOf(const Actor& a, float dist) {
  switch (a.kind) {
    case K_PAWN: return std::max(0.8f, dist / 2.0f);
    case K_KNIGHT: return std::max(1.6f, dist / 1.4f);
    case K_ROOK: return std::max(0.7f, dist / 2.6f);
    case K_KING: return std::max(0.7f, dist / 2.4f);
    default: return std::max(0.85f, dist / 2.1f);
  }
}

// A somersault: the actor flies along a parabola from 'from' to 'to', turning head over
// heels 'turns' times (forwards), knees tucked in.
struct Flip {
  vec3 from{0}, to{0};
  float height = 0.6f, dur = 0.8f, turns = 1.0f, t = 0;
  bool update(Actor& a, float dt) {
    t += dt;
    float u = glm::clamp(t / dur, 0.0f, 1.0f);
    vec3 p = glm::mix(from, to, u);
    p.y = glm::mix(from.y, to.y, u) + 4.0f * u * (1.0f - u) * height;
    a.pos = p;
    float spin = turns * kTau * glm::smoothstep(0.08f, 0.92f, u);
    a.tiltPivot = vec3(0, a.height() / std::max(a.scale, 1e-3f) * 0.45f, 0);
    a.tilt = glm::angleAxis(spin, vec3(1, 0, 0));
    a.ctl.tuck = std::sin(u * kPI) * 0.9f;
    a.ctl.cape = 0.9f;
    return u >= 1.0f;
  }
};

// ---------------------------------------------------------------------------
// Quiet moves (and castling)
// ---------------------------------------------------------------------------
class RQuietMove : public Staged {
 public:
  RQuietMove(const MovePlan& p) : plan(p) {}

 protected:
  MovePlan plan;
  Walker walker, rookWalker;
  vec3 A{0}, B{0};
  float dur = 1;
  bool arcSound = false;

  // the chariot: a few strides at the gallop, then it leaps whatever is in the way
  bool chariotLeap(World& w, Actor& a, float dt) {
    switch (phase) {
      case 0:
        if (enter()) {
          A = flat(a.pos);
          B = squareCenter(plan.move.to);
          a.yawTarget = faceAngle(B - A);
          w.sound(SFX_WHIP, 0.6f, a.pos, w.fx.rnd(0.95f, 1.1f));
          w.sound(SFX_NEIGH, 0.35f, a.pos, w.fx.rnd(1.0f, 1.15f));
        }
        a.ctl.gait += dt * 8.0f;
        a.ctl.stride = approach(a.ctl.stride, 0.6f, dt * 2.0f);
        a.ctl.run = 0.6f;
        a.ctl.armR = vec3(1.2f * bump(pt, 0.0f, 0.4f), 0.2f, 0.5f);  // the spear raised: "Hyah!"
        if (pt > 0.5f) {
          dur = 0.85f + 0.07f * glm::length(B - A);
          w.sound(SFX_WHEELS, 0.8f, a.pos, 1.05f);
          w.sound(SFX_WHOOSH, 0.5f, a.pos, 0.7f);
          next();
        }
        break;
      case 1: {  // the leap: the horses gather and fly, the car bounds after them
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        vec3 p = glm::mix(A, B, easeInOut(u) * 0.75f + u * 0.25f);
        p.y = 4.0f * u * (1.0f - u) * 0.55f;
        a.pos = p;
        a.ctl.tuck = std::sin(u * kPI);
        a.ctl.gait += dt * 6.0f;
        a.ctl.stride = 0.5f;
        a.ctl.run = 1.0f;
        a.ctl.spin += dt * 9.0f;
        a.ctl.cape = 1.0f;
        a.ctl.armR = vec3(0);
        if (u >= 1.0f) {
          a.pos = B;
          a.ctl.tuck = 0;
          w.sound(SFX_HOOF, 0.9f, B, 0.85f);
          w.sound(SFX_THUD, 0.7f, B, 1.1f);
          w.sound(SFX_WHEELS, 0.5f, B, 0.9f);
          w.fx.dust(B, 14, 0.4f, 1.2f);
          w.shake(0.1f);
          next();
        }
        break;
      }
      case 2:  // pull up, toss of the heads
        if (enter()) a.yawTarget = a.baseYaw;
        a.ctl.headPitch = -0.25f * bump(pt, 0.0f, 0.6f);
        a.ctl.gait += dt * 5.0f;
        a.ctl.stride = 0.3f * (1.0f - seg(pt, 0.0f, 0.6f));
        a.ctl.run = 0.3f * (1.0f - seg(pt, 0.0f, 0.6f));
        a.ctl.spin += dt * 3.0f * (1.0f - seg(pt, 0.0f, 0.6f));
        a.ctl.cape = 1.0f - seg(pt, 0.0f, 0.6f);
        if (pt > 0.65f) {
          a.ctl.headPitch = 0;
          a.ctl.stride = a.ctl.run = a.ctl.cape = 0;
          return true;
        }
        break;
    }
    return false;
  }

  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    if (a.kind == K_KNIGHT) return chariotLeap(w, a, dt);
    switch (phase) {
      case 0:
        if (enter()) {
          A = flat(a.pos);
          B = squareCenter(plan.move.to);
          float dist = glm::length(B - A);
          walker = Walker(plan.mover, B, modeOf(a), paceOf(a, dist));
          if (plan.rook >= 0) {
            Actor& r = w.actors[plan.rook];
            rookWalker = Walker(plan.rook, squareCenter(plan.rookTo), Walker::TOWER, 0.8f);
            (void)r;
          }
        }
        {
          bool d1;
          if (plan.rook >= 0) {
            // castling: the emperor strides round in an arc behind the rolling tower (on the board's
            // edge side of his rank, clear of the tower and of his own men)
            float u = glm::clamp(pt / 1.9f, 0.0f, 1.0f), e = easeInOut(u);
            vec3 toEdge = vec3(0, 0, a.color == 0 ? 1.0f : -1.0f);
            vec3 W = (A + B) * 0.5f + toEdge * 1.1f;
            vec3 p = (1 - e) * (1 - e) * A + 2 * (1 - e) * e * W + e * e * B;
            vec3 prev = a.pos;
            a.pos = vec3(p.x, 0, p.z);
            vec3 d = flat(a.pos - prev);
            if (glm::length(d) > 1e-5f) a.yawTarget = faceAngle(d);
            a.ctl.gait += glm::length(d) / rm::strideCycle(a.kind) * kTau;
            a.ctl.stride = u < 1.0f ? 0.8f : approach(a.ctl.stride, 0.0f, dt * 4.0f);
            if (!arcSound) {
              arcSound = true;
              w.sound(SFX_CORNU, 0.35f, a.pos, 1.0f);
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
          a.ctl.lean = a.ctl.cape = 0;
          if (plan.rook >= 0) w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw;
        }
        if (pt > 0.3f) return true;
        break;
    }
    return false;
  }
};

// The cinematic camera for a fight: like battleShot, but it looks at the two
// fighters from the side (so that a blow, a parry or an offered cup is not hidden
// behind the attacker's back) and comes a little closer.
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
  vec3 look[3] = {A + vec3(0, 0.6f, 0), V + vec3(0, 0.5f, 0), target};
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
// Shared prelude and epilogue for the Romans' battles
// ---------------------------------------------------------------------------
class RBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;
  // Staged clears 'first' after every step (so on its own it only holds for the very
  // first frame); the Romans' phases set themselves up with it, so it is raised
  // again for the first frame of each new phase
  bool update(World& w, float dt) override {
    int before = phase;
    bool done = BattleBase::update(w, dt);
    if (phase != before) first = true;
    return done;
  }

 protected:
  Walker walker;
  Fall fall;
  Tumble tumble;

  // the face-off, with the fight camera
  bool faceOffR(World& w, float duration = 0.45f) {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    if (enter()) {
      setup(w);
      a.yawTarget = faceAngle(V - A);
      v.yawTarget = faceAngle(A - V);
      fightShot(w, A, V, shotHeight, shotExtra, plan.mover, plan.victim);
    }
    v.tremble = glm::clamp(pt / duration, 0.0f, 1.0f) * 0.6f;
    v.idleWeight = 0;
    return pt >= duration;
  }
  bool moveInR(World& w, float dt, float speedScale = 1.0f) {
    Actor& a = w.actors[plan.mover];
    if (enter()) {
      vec3 B = squareCenter(plan.move.to);
      walker = Walker(plan.mover, B, modeOf(a), paceOf(a, glm::length(flat(B - a.pos))) * speedScale);
      if (isChariot(a)) walker.run = 0.4f;
    }
    return walker.update(w, dt);
  }
  // walk (gallop, roll) to a point
  bool goTo(World& w, float dt, const vec3& p, float speed, float run = 0.0f) {
    Actor& a = w.actors[plan.mover];
    if (enter()) {
      walker = Walker(plan.mover, p, modeOf(a), speed);
      walker.run = run;
    }
    return walker.update(w, dt);
  }
  bool finishR(World& w) {
    Actor& a = w.actors[plan.mover];
    bool done = finish(w);
    if (done) {
      a.pos = squareCenter(plan.move.to);
      a.tilt = quat(1, 0, 0, 0);
      a.resetControls();
    }
    return done;
  }
  // the victim's part while it waits: trembling, on guard
  void brace(World& w, float amount) {
    Actor& v = w.actors[plan.victim];
    v.tremble = amount;
    v.idleWeight = 0;
  }
  // a dying victim falls (or tumbles through the air); the body fades on its own; true once it is down
  bool victimDown(World& w, float dt) {
    bool a = fall.actor < 0 || fall.update(w, dt);
    bool b = tumble.actor < 0 || tumble.update(w, dt);
    return a && b;
  }
  bool victimDying() const { return fall.active() || tumble.active(); }
  // knocked flying: a body tumbles, a chariot overturns, a siege tower collapses
  void knockFlying(World& w, const vec3& vel, float spin) {
    Actor& v = w.actors[plan.victim];
    if (isHuman(v)) {
      tumble.launch(w, v, vel, spin);
    } else {
      // (a chariot overturns away from the blow: its own left is +x)
      vec3 left(std::cos(v.yaw), 0, -std::sin(v.yaw));
      fall.start(w, v, glm::dot(flat(vel), left) >= 0 ? 1.0f : -1.0f, flat(vel) * 0.35f, 0.15f);
    }
  }
  void kill(World& w, float dir, const vec3& push, float stagger = 0.35f) {
    Actor& v = w.actors[plan.victim];
    if (isChariot(v)) {  // a chariot overturns to the side the blow drives it
      vec3 left(std::cos(v.yaw), 0, -std::sin(v.yaw));
      float d = glm::dot(flat(push), left);
      dir = std::fabs(d) > 1e-3f ? (d > 0 ? 1.0f : -1.0f) : (w.fx.rnd() < 0.5f ? -1.0f : 1.0f);
    }
    fall.start(w, v, dir, push, stagger);
  }
  // a cheer and the victor's gesture: sword (or spear) raised
  void triumph(World& w, float u) {
    Actor& a = w.actors[plan.mover];
    if (isHuman(a)) {
      a.ctl.armR = glm::mix(a.ctl.armR, vec3(2.6f, 0.25f, 0.3f), std::min(1.0f, u * 3.0f));
      a.ctl.itemPitch = glm::mix(a.ctl.itemPitch, 0.0f, std::min(1.0f, u * 3.0f));
      a.ctl.headPitch = -0.2f * std::min(1.0f, u * 3.0f);
    } else if (isChariot(a)) {
      a.ctl.armR = vec3(1.8f, 0.2f, 0.2f) * std::min(1.0f, u * 3.0f);
      a.ctl.rear = 0.5f * std::sin(glm::clamp(u, 0.0f, 1.0f) * kPI);
    }
  }
};

}  // namespace

namespace {

// a puppet placed by a world matrix
void setPuppetMatrix(Puppet& p, const glm::mat4& M) {
  float sc = glm::length(vec3(M[0]));
  glm::mat3 R(vec3(M[0]) / sc, vec3(M[1]) / sc, vec3(M[2]) / sc);
  p.pos = vec3(M[3]);
  p.yaw = 0;
  p.tilt = glm::normalize(glm::quat_cast(R));
  p.scale = sc;
}
glm::mat4 propMatrix(const vec3& butt, const vec3& along, float scale = 1.0f) {
  return glm::translate(glm::mat4(1.0f), butt) * glm::mat4_cast(aimY(along)) * glm::scale(glm::mat4(1.0f), vec3(scale));
}

// A weapon stuck in a body: it moves with it as it falls, and fades with it.
struct Stuck {
  int puppet = -1, victim = -1;
  glm::mat4 local{1.0f};
  void attach(World& w, int puppetId, const Actor& v, const glm::mat4& world) {
    puppet = puppetId;
    victim = v.id;
    local = glm::inverse(v.model()) * world;
  }
  void update(World& w, int corpse) {
    Puppet* p = w.puppet(puppet);
    if (!p || victim < 0) return;
    const Actor& v = w.actors[victim];
    if (v.alive) {
      setPuppetMatrix(*p, v.model() * local);
    } else if (Puppet* c = w.puppet(corpse)) {
      setPuppetMatrix(*p, c->model() * local);
      p->life = c->life;
      p->dissolve = c->dissolve;
      p->fadeRate = c->fadeRate;
    } else if (p->life < 0) {
      p->life = 0.0f;  // (the tower it was stuck in fell to pieces)
      p->fadeRate = 3.0f;
    }
  }
};

// a thrown weapon on a ballistic arc, its point first
struct Missile {
  int puppet = -1;
  vec3 p0{0}, v0{0};
  float T = 0.5f, g = 9.8f, len = 0.95f, t = 0;
  void launch(World& w, int asset, int color, const vec3& from, const vec3& target, float time, float length, float gravity = 9.8f) {
    puppet = w.addPuppet(asset, color, 3);
    p0 = from;
    T = time;
    len = length;
    g = gravity;
    v0 = (target - from) / T + vec3(0, 0.5f * g * T, 0);
    t = 0;
    update(w, 0.0f);
  }
  vec3 tip() const { return p0 + v0 * std::min(t, T) + vec3(0, -0.5f * g * std::min(t, T) * std::min(t, T), 0); }
  vec3 heading() const { return glm::normalize(v0 + vec3(0, -g * std::min(t, T), 0)); }
  glm::mat4 matrix() const { return propMatrix(tip() - heading() * len, heading()); }
  bool update(World& w, float dt) {
    t += dt;
    if (Puppet* p = w.puppet(puppet)) setPuppetMatrix(*p, matrix());
    return t >= T;
  }
};

// ===========================================================================
// PAWN - the legionary
// ===========================================================================
// Pilum: he hurls it from afar; it pierces shield and all, and he draws his gladius.
class PawnPilum : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  Missile pilum;
  Stuck stuck;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    if (stuck.puppet >= 0) stuck.update(w, fall.corpse >= 0 ? fall.corpse : tumble.corpse);
    switch (phase) {
      case 0:
        shotExtra = 0.7f;
        if (faceOffR(w)) next();
        break;
      case 1: {  // draw back, take aim
        float u = easeInOut(pt / 0.6f);
        a.ctl.armR = vec3(2.3f * u, 0.15f * u, -0.6f * u);
        a.ctl.itemPitch = -0.55f * u;
        a.ctl.twist = 0.5f * u;
        a.ctl.lean = -0.12f * u;
        a.ctl.armL = vec3(0.5f * u, 0, 0);
        a.ctl.gait = kPI * 1.5f;
        a.ctl.stride = 0.4f * u;
        if (isHuman(v)) victimGuard(v, seg(pt, 0.2f, 0.7f));
        brace(w, 0.7f);
        if (pt > 0.7f) next();
        break;
      }
      case 2: {  // and throw
        float u = easeIn(pt / 0.14f);
        a.ctl.armR = vec3(glm::mix(2.3f, 1.4f, u), 0.15f, -0.6f * (1.0f - u));
        a.ctl.twist = glm::mix(0.5f, -0.3f, u);
        a.ctl.lean = glm::mix(-0.12f, 0.25f, u);
        a.ctl.gait = glm::mix(kPI * 1.5f, kPI * 0.5f, u);
        if (pt >= 0.14f) {
          a.ctl.itemR = 0;
          vec3 from = bonePoint(a, rm::RH_ITEMR, rm::handR(K_PAWN)) + vec3(0, 0.5f, 0) + dir * 0.1f;
          float T = 0.3f + 0.07f * L;
          pilum.launch(w, rm::R_PILUM, a.color, from, chestOf(v), T, 0.95f);
          w.sound(SFX_WHOOSH, 0.9f, from, 1.2f);
          w.sound(SFX_GRUNT, 0.7f, a.pos, w.fx.rnd(0.95f, 1.1f));
          next();
        }
        break;
      }
      case 3: {  // in flight
        float u = easeOut(pt / 0.4f);
        a.ctl.armR = vec3(glm::mix(1.4f, 0.4f, u), 0.15f * (1.0f - u), 0);
        a.ctl.twist = -0.3f * (1.0f - u);
        a.ctl.lean = 0.25f * (1.0f - u);
        a.ctl.armL = vec3(0.5f * (1.0f - u), 0, 0);
        a.ctl.stride = 0.4f * (1.0f - u);
        brace(w, 1.0f);
        if (isHuman(v)) victimGuard(v, 1.0f);
        if (pilum.update(w, dt)) {
          vec3 c = chestOf(v);
          vec3 hd = pilum.heading();
          // it goes in deep, and stays there
          glm::mat4 M = propMatrix(pilum.tip() + hd * 0.09f - hd * 0.95f, hd);
          if (Puppet* p = w.puppet(pilum.puppet)) setPuppetMatrix(*p, M);
          stuck.attach(w, pilum.puppet, v, M);
          if (isHuman(v) && v.kind == K_PAWN) w.sound(SFX_SHIELD, 0.7f, c, 1.2f);
          wound(w, c, flat(hd) + vec3(0, 0.2f, 0), isTower(v) ? 0.0f : 1.0f);
          kill(w, 1.0f, flat(hd) * 0.7f, 0.45f);
          w.shake(0.25f);
          w.slowmo(0.35f, 0.45f);
          next();
        }
        break;
      }
      case 4: {  // he draws his gladius while his enemy goes down
        float u = seg(pt, 0.2f, 0.9f);
        a.ctl.armR = vec3(0.35f * bump(u, 0.0f, 0.6f) + 0.45f * seg(u, 0.5f, 1.0f), -0.25f * bump(u, 0.0f, 0.6f), 0.45f * u);
        a.ctl.itemPitch = 0.6f * seg(u, 0.5f, 1.0f);
        if (u > 0.45f && a.ctl.draw < 0.5f) {
          a.ctl.draw = 1;
          a.ctl.itemR = 1;
          w.sound(SFX_DRAW, 0.7f, a.pos);
        }
        bool down = victimDown(w, dt);
        if (down && pt > 1.0f) next();
        break;
      }
      case 5:  // "Roma!"
        if (enter()) w.sound(SFX_SHOUT, 0.6f, a.pos, w.fx.rnd(0.95f, 1.1f));
        triumph(w, pt / 0.7f);
        if (pt > 0.75f) next();
        break;
      case 6:
        if (first) {
          a.ctl.armR = vec3(0.45f, 0.1f, 0.45f);
          a.ctl.itemPitch = 0.6f;
          a.ctl.headPitch = 0;
        }
        if (moveInR(w, dt)) next();
        break;
      case 7:
        return finishR(w);
    }
    return false;
  }
};

// Shield and gladius: he takes the blow on his scutum, punches with the boss and
// stabs underneath it - as the legions were drilled.
class PawnShield : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  bool blocked = false, punched = false, stabbed = false;
  vec3 start{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOffR(w)) next();
        break;
      case 1: {  // draw the gladius, shield up
        float u = seg(pt, 0.0f, 0.6f);
        a.ctl.armR = vec3(0.35f * bump(u, 0.0f, 0.6f) + 0.45f * seg(u, 0.5f, 1.0f), -0.25f * bump(u, 0.0f, 0.6f), 0.45f * u);
        if (u > 0.4f && a.ctl.draw < 0.5f) {
          a.ctl.draw = 1;
          w.sound(SFX_DRAW, 0.8f, a.pos);
        }
        a.ctl.armL = vec3(0.55f, 0.05f, -0.15f) * u;
        a.ctl.itemPitch = 0.6f * seg(u, 0.5f, 1.0f);
        a.ctl.crouch = 0.15f * u;
        victimGuard(v, u);
        brace(w, 0.5f);
        if (pt > 0.7f) next();
        break;
      }
      case 2:  // close in
        if (enter()) {
          walker = Walker(plan.mover, V - dir * (touchDist(a, v) * 1.35f + 0.08f), Walker::FOOT, std::max(0.9f, L / 2.0f));
          w.sound(SFX_SHOUT, 0.55f, a.pos, w.fx.rnd(1.0f, 1.15f));
        }
        guard(a.ctl, 1.0f, true);
        victimGuard(v, 1.0f);
        if (walker.update(w, dt)) next();
        break;
      case 3: {  // the victim strikes; the blow rings off the scutum
        float u = seg(pt, 0.0f, 0.85f);
        guard(a.ctl, 1.0f, true);
        block(a.ctl, bump(u, 0.25f, 0.8f));
        if (!isTower(v)) victimStrike(v, u);
        if (!blocked && u > 0.55f) {
          blocked = true;
          vec3 s = handOf(a, false) + dir * 0.05f + vec3(0, 0.08f, 0);
          if (!isTower(v)) {
            w.sound(SFX_SHIELD, 1.0f, s, w.fx.rnd(0.9f, 1.05f));
            sparksAt(w, s, -dir + vec3(0, 0.6f, 0), 14);
            splinters(w, s, -dir, 10);
            w.shake(0.12f);
          }
        }
        if (pt > 0.95f) next();
        break;
      }
      case 4: {  // a punch with the boss: the victim reels
        if (enter()) start = flat(a.pos);
        float u = seg(pt, 0.0f, 0.45f);
        a.pos = start + dir * 0.1f * std::sin(u * kPI * 0.5f);
        a.ctl.armL = glm::mix(vec3(0.55f, 0.05f, -0.15f), vec3(1.1f, -0.1f, -0.6f), bump(u, 0.0f, 1.0f));
        a.ctl.lean = 0.06f + 0.2f * bump(u, 0.0f, 1.0f);
        if (!isTower(v)) victimGuard(v, 1.0f - 0.6f * u);
        if (!punched && u > 0.45f) {
          punched = true;
          vec3 s = handOf(a, false) + dir * 0.08f;
          w.sound(SFX_SHIELD, 0.9f, s, 0.8f);
          w.sound(SFX_GRUNT, 0.8f, a.pos, w.fx.rnd(0.9f, 1.05f));
          w.shake(0.15f);
          if (isHuman(v)) w.sound(SFX_GRUNT, 0.6f, v.pos, 0.8f);
        }
        if (punched) {
          v.pos += dir * dt * 0.35f * (1.0f - seg(pt, 0.2f, 0.6f));
          v.ctl.lean = approach(v.ctl.lean, -0.2f, dt * 2.0f);
          v.ctl.headPitch = approach(v.ctl.headPitch, -0.25f, dt * 2.0f);
        }
        if (pt > 0.6f) next();
        break;
      }
      case 5: {  // and the stab, low under the shield
        float u = seg(pt, 0.0f, 0.75f);
        thrust(a.ctl, u);
        a.ctl.armL = vec3(0.55f, 0.05f, -0.15f);
        if (!stabbed && u > 0.52f) {
          stabbed = true;
          vec3 c = chestOf(v) - vec3(0, 0.1f, 0);
          wound(w, c, dir + vec3(0, 0.1f, 0), isTower(v) ? 0.2f : 1.0f);
          kill(w, 1.0f, dir * 0.45f, 0.5f);
          w.shake(0.25f);
          w.slowmo(0.3f, 0.45f);
        }
        if (stabbed) victimDown(w, dt);
        if (pt > 0.8f) next();
        break;
      }
      case 6:  // he steps back as the body falls
        guard(a.ctl, 1.0f, true);
        a.ctl.lean = 0.06f;
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      case 7:
        if (enter()) w.sound(SFX_SHOUT, 0.6f, a.pos, w.fx.rnd(0.9f, 1.05f));
        triumph(w, pt / 0.7f);
        if (pt > 0.75f) next();
        break;
      case 8:
        if (first) guard(a.ctl, 1.0f, true);
        if (moveInR(w, dt)) next();
        break;
      case 9:
        return finishR(w);
    }
    return false;
  }
};

// Vault: he runs, plants his pilum and pole-vaults over the victim with a
// somersault, lands behind it, spins round and stabs it in the back.
class PawnVault : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  Flip flip;
  int dropped = -1;
  vec3 plant{0}, land{0};
  float dropAngle = 0;
  bool stabbed = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    if (dropped >= 0) {  // the pilum he let go of topples over
      if (Puppet* p = w.puppet(dropped)) {
        dropAngle = std::min(1.45f, dropAngle + dt * (0.6f + dropAngle * 4.0f));
        p->pos = plant;
        p->yaw = faceAngle(dir);
        p->tilt = glm::angleAxis(dropAngle, vec3(1, 0, 0));
      }
    }
    switch (phase) {
      case 0:
        shotExtra = 1.0f;
        shotHeight = 0.75f;
        if (faceOffR(w)) next();
        break;
      case 1:  // the run-up, the pilum lowered
        if (enter()) {
          walker = Walker(plan.mover, V - dir * std::min(L - 0.05f, 0.78f), Walker::FOOT, std::max(1.7f, L / 2.0f));
          w.sound(SFX_SHOUT, 0.7f, a.pos, w.fx.rnd(0.95f, 1.1f));
        }
        a.ctl.itemPitch = approach(a.ctl.itemPitch, 0.9f, dt * 2.0f);
        a.ctl.armR = vec3(0.3f, 0, 0.1f);
        victimGuard(v, seg(pt, 0.0f, 0.5f));
        brace(w, 0.8f);
        if (walker.update(w, dt) || walker.s > walker.len - 0.05f) next();
        break;
      case 2: {  // plant the pilum and vault
        if (enter()) {
          plant = flat(a.pos) + dir * 0.32f;
          land = V + dir * (v.radius() + 0.32f);
          flip.from = a.pos;
          flip.to = land;
          flip.height = std::max(0.75f, v.height() * 0.85f + 0.25f);
          flip.dur = 0.95f + 0.1f * flip.height;
          flip.turns = 1.0f;
          w.sound(SFX_GRUNT, 0.8f, a.pos, 1.05f);
          w.sound(SFX_WHOOSH, 0.8f, a.pos, 0.8f);
        }
        bool landed = flip.update(a, dt);
        if (dropped < 0 && flip.t > 0.22f * flip.dur) {  // let go of the pilum at the top of the swing
          a.ctl.itemR = 0;
          dropped = w.addPuppet(rm::R_PILUM, a.color, 3);
          if (Puppet* p = w.puppet(dropped)) {
            p->life = 2.5f;
            p->fadeRate = 1.0f;
          }
          dropAngle = 0.15f;
        }
        v.ctl.headPitch = -0.6f * bump(flip.t, 0.0f, flip.dur);  // looking up as he flies over
        brace(w, 1.0f);
        if (landed) {
          a.tilt = quat(1, 0, 0, 0);
          a.ctl.tuck = 0;
          a.pos = land;
          w.sound(SFX_THUD, 0.7f, land, 1.2f);
          w.sound(SFX_ARMOR, 0.7f, land, 1.0f);
          w.fx.dust(land, 10, 0.25f, 0.8f);
          next();
        }
        break;
      }
      case 3: {  // land, spin round, draw
        float u = seg(pt, 0.0f, 0.4f);
        if (enter()) {
          a.yawTarget = faceAngle(V - a.pos);
          a.yawRate = 14.0f;
        }
        a.ctl.crouch = 0.5f * (1.0f - u);
        a.ctl.cape = 0.9f * (1.0f - u);
        if (u > 0.3f && a.ctl.draw < 0.5f) {
          a.ctl.draw = 1;
          a.ctl.itemR = 1;
          w.sound(SFX_DRAW, 0.8f, a.pos);
        }
        guard(a.ctl, u, true);
        v.ctl.headPitch = -0.6f * (1.0f - u);
        if (pt > 0.42f) next();
        break;
      }
      case 4: {  // the stab in the back
        float u = seg(pt, 0.0f, 0.7f);
        a.yawRate = 14.0f;
        thrust(a.ctl, u);
        v.ctl.twist = -0.4f * seg(pt, 0.0f, 0.4f);  // too late to turn round
        if (!stabbed && u > 0.52f) {
          stabbed = true;
          vec3 c = chestOf(v);
          vec3 d = flatDir(a.pos, V);
          wound(w, c, d + vec3(0, 0.2f, 0), isTower(v) ? 0.2f : 1.1f);
          kill(w, -1.0f, d * 0.4f, 0.45f);
          w.shake(0.25f);
          w.slowmo(0.3f, 0.45f);
        }
        if (stabbed) victimDown(w, dt);
        if (pt > 0.8f) next();
        break;
      }
      case 5:
        guard(a.ctl, 1.0f, true);
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      case 6:
        if (enter()) w.sound(SFX_SHOUT, 0.6f, a.pos, w.fx.rnd(0.95f, 1.1f));
        triumph(w, pt / 0.7f);
        if (pt > 0.75f) next();
        break;
      case 7:
        if (first) {
          guard(a.ctl, 1.0f, true);
          a.yawRate = 9.0f;  // back to his usual pace of turning
        }
        if (moveInR(w, dt)) next();
        break;
      case 8:
        return finishR(w);
    }
    return false;
  }
};

// ===========================================================================
// KNIGHT - the chariot
// ===========================================================================
vec3 clampBoard(vec3 p, float m = 3.35f) {
  p.x = glm::clamp(p.x, -m, m);
  p.z = glm::clamp(p.z, -m, m);
  return p;
}

// Run down: a crack of the whip, a charge, and the victim goes under the hooves
// and the wheels; the chariot thunders on, wheels round and comes back.
class ChariotRunDown : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  bool hit = false;
  vec3 end{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.3f;
        shotHeight = 0.45f;
        if (faceOffR(w)) next();
        break;
      case 1: {  // the whip cracks, the horses rear
        if (enter()) {
          w.sound(SFX_WHIP, 0.9f, a.pos, 1.0f);
          w.sound(SFX_NEIGH, 0.8f, a.pos, 1.05f);
        }
        float u = seg(pt, 0.0f, 0.8f);
        a.ctl.rear = 0.45f * std::sin(u * kPI);
        a.ctl.armR = vec3(1.6f * bump(u, 0.0f, 0.6f), 0.3f, 0.6f);
        a.ctl.cape = 0.3f;
        victimGuard(v, u);
        brace(w, 0.9f);
        if (pt > 0.85f) next();
        break;
      }
      case 2: {  // the charge
        if (enter()) {
          end = clampBoard(V + dir * 1.0f);
          walker = Walker(plan.mover, end, Walker::CHARIOT, 2.7f);
          walker.run = 1.0f;
          walker.face = false;
          w.sound(SFX_WHEELS, 1.0f, a.pos, 1.0f);
          w.sound(SFX_SHOUT, 0.5f, a.pos, 1.15f);
        }
        walker.update(w, dt);
        a.ctl.lean = 0.2f;
        a.ctl.armR = vec3(0.4f, 0.2f, 0.5f);
        brace(w, 1.0f);
        vec3 front = a.pos + a.forward() * 0.42f;
        if (!hit && glm::dot(flat(front - V), dir) > -v.radius() * 0.8f) {
          hit = true;
          vec3 side(-dir.z, 0, dir.x);
          float sgn = w.fx.rnd() < 0.5f ? -1.0f : 1.0f;
          vec3 c = chestOf(v);
          w.sound(SFX_THUD, 1.0f, c, 0.8f);
          w.sound(SFX_NEIGH, 0.9f, c, 0.95f);
          if (!isTower(v)) {
            bloodSpray(w, c, dir + side * sgn + vec3(0, 0.5f, 0), 60, 3.5f);
            w.sound(SFX_BLOOD, 0.8f, c);
          }
          knockFlying(w, dir * 1.4f + side * sgn * 1.6f + vec3(0, 3.4f, 0), 9.0f);
          w.shake(0.45f);
          w.slowmo(0.3f, 0.5f);
        }
        if (hit) victimDown(w, dt);
        if (walker.s >= walker.len - 1e-3f) next();
        break;
      }
      case 3:  // rein in and turn
        if (enter()) a.yawTarget = faceAngle(V - end);
        a.ctl.lean = approach(a.ctl.lean, 0.0f, dt);
        a.ctl.gait += dt * 5.0f;
        a.ctl.stride = approach(a.ctl.stride, 0.3f, dt * 2.0f);
        victimDown(w, dt);
        if (pt > 0.7f) next();
        break;
      case 4:  // and back at a canter, over the blood
        if (enter()) {
          walker = Walker(plan.mover, V, Walker::CHARIOT, 1.3f);
          walker.run = 0.4f;
        }
        victimDown(w, dt);
        if (walker.update(w, dt) && victimDown(w, dt)) next();
        break;
      case 5:
        return finishR(w);
    }
    return false;
  }
};

// Scythed wheels: the chariot races past the victim close enough for the blades
// on its axle to cut it down.
class ChariotScythes : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  bool hit = false;
  vec3 end{0}, side{0};
  float sgn = 1;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.3f;
        shotHeight = 0.4f;
        if (faceOffR(w)) next();
        break;
      case 1: {  // the whip, and off
        if (enter()) {
          side = vec3(-dir.z, 0, dir.x);
          // pass the victim on the side with room to spare
          vec3 e1 = V + dir * 1.0f + side * 0.33f, e2 = V + dir * 1.0f - side * 0.33f;
          sgn = glm::length(clampBoard(e1) - e1) <= glm::length(clampBoard(e2) - e2) ? 1.0f : -1.0f;
          end = clampBoard(V + dir * 1.0f + side * 0.33f * sgn);
          w.sound(SFX_WHIP, 0.9f, a.pos, 1.1f);
          w.sound(SFX_NEIGH, 0.6f, a.pos, 1.1f);
        }
        a.ctl.armR = vec3(1.6f * bump(pt, 0.0f, 0.4f), 0.3f, 0.6f);
        victimGuard(v, seg(pt, 0.0f, 0.5f));
        brace(w, 0.9f);
        if (pt > 0.5f) next();
        break;
      }
      case 2: {  // racing past: the scythes cut
        if (enter()) {
          walker = Walker(plan.mover, end, Walker::CHARIOT, 2.6f);
          walker.run = 1.0f;
          w.sound(SFX_WHEELS, 1.0f, a.pos, 1.05f);
        }
        walker.update(w, dt);
        a.ctl.lean = 0.2f;
        // the blade on the victim's side of the axle
        vec3 blade = modelPoint(a, vec3(sgn * -0.29f, 0.14f, -0.26f));
        if (!hit && glm::dot(flat(blade - V), a.forward()) > 0.0f) {
          hit = true;
          vec3 hitAt = V + vec3(0, std::min(0.32f, v.height() * 0.35f), 0);
          vec3 fwd = a.forward();
          w.sound(SFX_SLASH, 1.0f, hitAt, 0.85f);
          if (isTower(v)) {
            splinters(w, hitAt, fwd, 30);
            kill(w, 1.0f, fwd * 0.5f, 0.0f);
          } else {
            wound(w, hitAt, fwd * 0.6f + side * -sgn * 0.6f, 1.3f, true);
            sparksAt(w, blade, -fwd, 10);
            v.yawRate = 7.0f;
            v.yawTarget = v.yaw + kPI * 1.3f * sgn;  // spun round by the blow
            kill(w, 1.0f, fwd * 0.6f, 0.5f);
          }
          w.shake(0.35f);
          w.slowmo(0.3f, 0.5f);
        }
        if (hit) victimDown(w, dt);
        if (walker.s >= walker.len - 1e-3f) next();
        break;
      }
      case 3:
        if (enter()) a.yawTarget = faceAngle(V - end);
        a.ctl.lean = approach(a.ctl.lean, 0.0f, dt);
        a.ctl.gait += dt * 5.0f;
        a.ctl.stride = approach(a.ctl.stride, 0.3f, dt * 2.0f);
        victimDown(w, dt);
        if (pt > 0.7f) next();
        break;
      case 4:
        if (enter()) {
          walker = Walker(plan.mover, V, Walker::CHARIOT, 1.3f);
          walker.run = 0.4f;
          triumph(w, 1.0f);
        }
        victimDown(w, dt);
        if (walker.update(w, dt) && victimDown(w, dt)) next();
        break;
      case 5:
        return finishR(w);
    }
    return false;
  }
};

// The charioteer's leap: the chariot pulls up short, the horses rear, and he
// somersaults over their heads onto the victim with his spear - then flips back
// into his car.
class ChariotLeap : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  vec3 target{0};  // where he lands, as an offset of the charioteer in the chariot's frame
  bool struck = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.0f;
        shotHeight = 0.6f;
        if (faceOffR(w)) next();
        break;
      case 1:  // gallop up and pull up short
        if (enter()) {
          walker = Walker(plan.mover, V - dir * std::min(L, 1.02f), Walker::CHARIOT, std::max(1.8f, L / 1.4f));
          walker.run = 0.8f;
          w.sound(SFX_WHIP, 0.7f, a.pos);
        }
        victimGuard(v, seg(pt, 0.0f, 0.8f));
        brace(w, 0.8f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // the horses rear; he crouches
        if (enter()) {
          w.sound(SFX_NEIGH, 1.0f, a.pos, 1.0f);
          a.yawTarget = faceAngle(V - a.pos);
          // where to land: just short of the victim, on the ground
          vec3 local = glm::inverse(glm::angleAxis(a.yaw, vec3(0, 1, 0))) * (V - dir * (v.radius() + 0.12f) - a.pos);
          vec3 rest(0, 0.417f, -0.24f);
          target = vec3(local.x - rest.x, 0.267f - rest.y, local.z - rest.z);
        }
        float u = seg(pt, 0.0f, 0.5f);
        a.ctl.rear = 0.7f * bump(pt, 0.0f, 0.9f);
        a.ctl.rider = vec3(0, -0.05f * u, 0);
        a.ctl.lean = 0.3f * u;
        a.ctl.armR = vec3(0.9f, 0.1f, 0.6f) * u;
        victimGuard(v, 1.0f);
        if (pt > 0.5f) {
          w.sound(SFX_SHOUT, 0.8f, a.pos, 1.15f);
          w.sound(SFX_WHOOSH, 0.7f, a.pos, 0.75f);
          next();
        }
        break;
      }
      case 3: {  // the somersault over the horses
        const float dur = 0.85f;
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        a.ctl.rear = 0.7f * bump(pt + 0.5f, 0.0f, 0.9f);
        vec3 r = target * u;
        r.y = glm::mix(-0.05f, target.y, u) + 4.0f * u * (1.0f - u) * 0.62f;
        a.ctl.rider = r;
        a.ctl.riderSpin = kTau * glm::smoothstep(0.1f, 0.9f, u);
        a.ctl.cape = 1.0f;
        a.ctl.lean = 0.3f * (1.0f - u);
        a.ctl.armR = vec3(0.9f + 0.6f * u, 0.1f, 0.6f);
        a.ctl.itemPitch = 1.4f * u;
        v.ctl.headPitch = -0.5f * std::sin(u * kPI);
        brace(w, 1.0f);
        if (u >= 1.0f) {
          w.sound(SFX_THUD, 0.6f, V - dir * 0.3f, 1.3f);
          w.fx.dust(V - dir * (v.radius() + 0.12f), 8, 0.2f, 0.7f);
          next();
        }
        break;
      }
      case 4: {  // and the spear goes in
        float u = seg(pt, 0.0f, 0.5f);
        play(a.ctl, {K(0.0f, vec3(1.5f, 0.1f, 0.6f), vec3(0), 0.0f, 0, 0, 1.4f), K(0.4f, vec3(0.5f, 0.1f, 1.2f), vec3(0), -0.1f, -0.2f, 0, 1.5f),
                     K(0.6f, vec3(1.3f, 0.0f, -0.4f), vec3(0), 0.35f, 0.2f, 0, 1.55f), K(1.0f, vec3(1.0f, 0.0f, 0.2f), vec3(0), 0.1f, 0, 0, 1.4f)},
             u);
        a.ctl.cape = approach(a.ctl.cape, 0.0f, dt * 2.0f);
        if (!struck && u > 0.55f) {
          struck = true;
          vec3 c = chestOf(v);
          wound(w, c, dir + vec3(0, 0.2f, 0), isTower(v) ? 0.2f : 1.1f);
          kill(w, 1.0f, dir * 0.45f, 0.45f);
          w.shake(0.25f);
          w.slowmo(0.3f, 0.45f);
        }
        if (struck) victimDown(w, dt);
        if (pt > 0.65f) next();
        break;
      }
      case 5: {  // a back somersault, home into the car
        const float dur = 0.8f;
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        if (enter()) w.sound(SFX_WHOOSH, 0.6f, a.pos, 0.8f);
        vec3 r = target * (1.0f - u);
        r.y = target.y * (1.0f - u) + 4.0f * u * (1.0f - u) * 0.6f;
        a.ctl.rider = r;
        a.ctl.riderSpin = kTau - kTau * glm::smoothstep(0.1f, 0.9f, u);
        a.ctl.armR = glm::mix(vec3(1.0f, 0.0f, 0.2f), vec3(0), u);
        a.ctl.itemPitch = 1.4f * (1.0f - u);
        a.ctl.cape = 0.8f;
        victimDown(w, dt);
        if (u >= 1.0f) {
          a.ctl.rider = vec3(0);
          a.ctl.riderSpin = 0;
          w.sound(SFX_THUD, 0.5f, a.pos, 1.4f);
          next();
        }
        break;
      }
      case 6:
        triumph(w, pt / 0.8f);
        a.ctl.cape = approach(a.ctl.cape, 0.0f, dt * 2.0f);
        if (victimDown(w, dt) && pt > 0.8f) next();
        break;
      case 7:
        if (first) a.ctl.armR = vec3(0);
        if (moveInR(w, dt)) next();
        break;
      case 8:
        return finishR(w);
    }
    return false;
  }
};

}  // namespace

namespace {

// ===========================================================================
// BISHOP - the centurion: gladius in his right hand, pugio in his left
// ===========================================================================
// the clash of two blades: sparks where they meet
void clash(World& w, const Actor& a, const Actor& v, float loud = 1.0f) {
  vec3 p = (bladeTipOf(a) * 0.6f + bladeTipOf(v) * 0.4f);
  if (isTower(v)) p = chestOf(v);
  sparksAt(w, p, vec3(0, 1, 0) + flatDir(a.pos, v.pos) * -0.5f, 26);
  w.sound(SFX_PARRY, loud, p, w.fx.rnd(0.9f, 1.12f));
  w.shake(0.1f);
}

// Duel: he parries two blows, spins round with a cut across the chest and
// finishes it with his dagger.
class CenturionDuel : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  bool c1 = false, c2 = false, cut = false, stab = false;
  float spin0 = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    bool fights = !isTower(v);
    switch (phase) {
      case 0:
        if (faceOffR(w)) next();
        break;
      case 1:  // close in, on guard
        if (enter()) walker = Walker(plan.mover, V - dir * (touchDist(a, v) * 1.5f + 0.06f), Walker::FOOT, std::max(0.9f, L / 2.0f));
        guard(a.ctl, seg(pt, 0.0f, 0.4f), false);
        a.ctl.armL = vec3(0.6f, 0.0f, 0.2f) * seg(pt, 0.0f, 0.4f);
        victimGuard(v, seg(pt, 0.0f, 0.5f));
        brace(w, 0.5f);
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // the victim's first blow: parried high
        float u = seg(pt, 0.0f, 0.85f);
        if (fights) victimStrike(v, u);
        parry(a.ctl, bump(u, 0.2f, 0.85f), true);
        if (!c1 && u > 0.56f) {
          c1 = true;
          if (fights) clash(w, a, v);
        }
        if (pt > 0.9f) next();
        break;
      }
      case 3: {  // the second, a thrust: swept aside
        float u = seg(pt, 0.0f, 0.75f);
        if (fights) {
          if (isHuman(v)) thrust(v.ctl, u);
          else victimStrike(v, u);
        }
        parry(a.ctl, bump(u, 0.2f, 0.85f), false);
        a.ctl.itemYaw = -0.8f * bump(u, 0.3f, 0.8f);
        if (!c2 && u > 0.5f) {
          c2 = true;
          if (fights) clash(w, a, v, 0.8f);
        }
        if (pt > 0.8f) next();
        break;
      }
      case 4: {  // he spins round and cuts across the chest
        if (enter()) {
          spin0 = a.yaw;
          w.sound(SFX_GRUNT, 0.8f, a.pos, 0.95f);
          w.sound(SFX_WHOOSH, 0.8f, a.pos, 1.0f);
        }
        float u = seg(pt, 0.0f, 0.6f);
        a.yaw = spin0 + kTau * glm::smoothstep(0.0f, 1.0f, u);
        a.yawTarget = a.yaw;
        a.ctl.cape = 0.8f * bump(u, 0.0f, 1.0f);
        sweep(a.ctl, 0.25f + 0.45f * u);
        a.ctl.crouch = 0.3f;
        if (isHuman(v)) victimGuard(v, 1.0f - u);
        if (!cut && u > 0.85f) {
          cut = true;
          vec3 c = chestOf(v) + vec3(0, 0.04f, 0);
          vec3 side(-dir.z, 0, dir.x);
          wound(w, c, side + dir * 0.3f + vec3(0, 0.3f, 0), isTower(v) ? 0.2f : 1.0f, true);
          v.ctl.lean = -0.2f;
          v.ctl.twist = 0.4f;
        }
        if (pt > 0.65f) {
          a.yaw = a.yawTarget = spin0;
          next();
        }
        break;
      }
      case 5: {  // the pugio, up under the ribs
        float u = seg(pt, 0.0f, 0.5f);
        a.ctl.armL = glm::mix(vec3(0.3f, 0.1f, 0.6f), vec3(1.3f, -0.15f, -0.5f), bump(u, 0.0f, 1.4f));
        a.ctl.lean = 0.06f + 0.25f * bump(u, 0.0f, 1.4f);
        a.ctl.twist = -0.3f * bump(u, 0.0f, 1.4f);
        a.ctl.gait = kPI * 1.5f;
        a.ctl.stride = 0.5f * bump(u, 0.0f, 1.4f);
        if (!stab && u > 0.55f) {
          stab = true;
          vec3 c = chestOf(v) - vec3(0, 0.05f, 0);
          wound(w, c, dir + vec3(0, 0.15f, 0), isTower(v) ? 0.2f : 1.0f);
          kill(w, 1.0f, dir * 0.3f, 0.7f);
          w.shake(0.25f);
          w.slowmo(0.3f, 0.5f);
        }
        if (stab) victimDown(w, dt);
        if (pt > 0.7f) next();
        break;
      }
      case 6: {  // a flick of the blade (the blood flies off it)
        float u = seg(pt, 0.0f, 0.5f);
        a.ctl.armL = glm::mix(a.ctl.armL, vec3(0.6f, 0.0f, 0.2f), std::min(1.0f, dt * 6.0f));
        a.ctl.lean = approach(a.ctl.lean, 0.0f, dt);
        a.ctl.twist = approach(a.ctl.twist, 0.0f, dt * 2.0f);
        a.ctl.stride = approach(a.ctl.stride, 0.0f, dt * 2.0f);
        a.ctl.armR = glm::mix(vec3(0.45f, 0.1f, 0.45f), vec3(0.9f, -0.4f, 0.0f), bump(u, 0.0f, 1.0f));
        if (pt > 0.3f && pt - dt <= 0.3f) bloodSpray(w, bladeTipOf(a), -dir + vec3(0, -0.3f, 0), 14, 2.0f, 0.3f);
        if (victimDown(w, dt) && pt > 0.5f) next();
        break;
      }
      case 7:
        if (enter()) w.sound(SFX_SHOUT, 0.6f, a.pos, 0.95f);
        triumph(w, pt / 0.7f);
        if (pt > 0.75f) next();
        break;
      case 8:
        if (first) guard(a.ctl, 1.0f, false);
        if (moveInR(w, dt)) next();
        break;
      case 9:
        return finishR(w);
    }
    return false;
  }
};

// Leaping strike: a run, a somersault, and he comes down with his sword
// driven in from above.
class CenturionLeap : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  Flip flip;
  bool struck = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.9f;
        shotHeight = 0.8f;
        if (faceOffR(w)) next();
        break;
      case 1:  // the run
        if (enter()) {
          walker = Walker(plan.mover, V - dir * std::min(L - 0.05f, 1.15f), Walker::FOOT, std::max(1.8f, L / 2.0f));
          w.sound(SFX_SHOUT, 0.7f, a.pos, 0.95f);
        }
        guard(a.ctl, 1.0f, false);
        victimGuard(v, seg(pt, 0.0f, 0.5f));
        brace(w, 0.8f);
        if (walker.update(w, dt) || walker.s > walker.len - 0.05f) next();
        break;
      case 2:  // crouch to spring
        a.ctl.crouch = 0.45f * seg(pt, 0.0f, 0.15f);
        if (pt > 0.16f) {
          flip.from = a.pos;
          flip.to = V - dir * (v.radius() + 0.1f);
          flip.height = std::max(0.7f, v.height() * 0.7f + 0.3f);
          flip.dur = 0.95f;
          flip.turns = 1.0f;
          w.sound(SFX_GRUNT, 0.8f, a.pos, 1.0f);
          w.sound(SFX_WHOOSH, 0.9f, a.pos, 0.7f);
          next();
        }
        break;
      case 3: {  // the somersault, the sword raised in both hands as he comes down
        bool landed = flip.update(a, dt);
        float u = glm::clamp(flip.t / flip.dur, 0.0f, 1.0f);
        a.ctl.crouch = 0;
        float raise = seg(u, 0.55f, 0.85f);
        a.ctl.armR = glm::mix(vec3(0.6f, 0.1f, 0.5f), vec3(2.8f, -0.1f, 0.7f), raise);
        a.ctl.armL = glm::mix(vec3(0.6f, 0.1f, 0.5f), vec3(2.7f, -0.2f, 0.7f), raise);
        a.ctl.itemPitch = glm::mix(0.6f, -0.2f, raise);
        v.ctl.headPitch = -0.5f * std::sin(u * kPI);
        if (isHuman(v)) victimGuard(v, 1.0f);
        brace(w, 1.0f);
        if (landed) {
          a.tilt = quat(1, 0, 0, 0);
          a.ctl.tuck = 0;
          a.pos = flip.to;
          w.sound(SFX_THUD, 0.8f, a.pos, 1.0f);
          w.fx.dust(a.pos, 10, 0.25f, 0.8f);
          next();
        }
        break;
      }
      case 4: {  // the blade comes down
        float u = seg(pt, 0.0f, 0.3f);
        a.ctl.armR = glm::mix(vec3(2.8f, -0.1f, 0.7f), vec3(0.8f, 0.0f, -0.3f), easeIn(u));
        a.ctl.armL = glm::mix(vec3(2.7f, -0.2f, 0.7f), vec3(0.7f, 0.1f, -0.2f), easeIn(u));
        a.ctl.itemPitch = glm::mix(-0.2f, 1.9f, easeIn(u));
        a.ctl.crouch = 0.45f * u;
        a.ctl.lean = 0.35f * u;
        a.ctl.cape = 0.6f * (1.0f - u);
        if (!struck && u >= 1.0f) {
          struck = true;
          vec3 c = chestOf(v) + vec3(0, 0.06f, 0);
          wound(w, c, vec3(0, 1.2f, 0) + dir * 0.4f, isTower(v) ? 0.3f : 1.5f);
          kill(w, 1.0f, dir * 0.2f, 0.25f);
          w.shake(0.4f);
          w.slowmo(0.3f, 0.5f);
        }
        if (struck) victimDown(w, dt);
        if (pt > 0.55f) next();
        break;
      }
      case 5:  // rise
        a.ctl.crouch = approach(a.ctl.crouch, 0.15f, dt);
        a.ctl.lean = approach(a.ctl.lean, 0.06f, dt);
        a.ctl.armR = glm::mix(a.ctl.armR, vec3(0.45f, 0.1f, 0.45f), std::min(1.0f, dt * 4.0f));
        a.ctl.armL = glm::mix(a.ctl.armL, vec3(0.6f, 0.0f, 0.2f), std::min(1.0f, dt * 4.0f));
        a.ctl.itemPitch = approach(a.ctl.itemPitch, 0.6f, dt * 3.0f);
        if (victimDown(w, dt) && pt > 0.4f) next();
        break;
      case 6:
        if (enter()) w.sound(SFX_SHOUT, 0.6f, a.pos, 0.95f);
        triumph(w, pt / 0.7f);
        if (pt > 0.75f) next();
        break;
      case 7:
        if (moveInR(w, dt)) next();
        break;
      case 8:
        return finishR(w);
    }
    return false;
  }
};

// Thrown knife: he flips his pugio up and catches it, then hurls it end over end
// into the victim's throat, and walks over to pull it out.
class CenturionKnife : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  int knife = -1;
  vec3 kp0{0}, kv{0}, kTarget{0};
  float kt = 0, T = 0.3f;
  Stuck stuck;
  bool flew = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    if (stuck.puppet >= 0) stuck.update(w, fall.corpse);
    switch (phase) {
      case 0:
        shotExtra = 0.6f;
        if (faceOffR(w)) next();
        break;
      case 1: {  // a flip of the knife into the air, and caught
        float u = seg(pt, 0.0f, 0.7f);
        a.ctl.armL = vec3(0.7f + 0.3f * bump(u, 0.0f, 0.3f), 0.0f, 0.6f);
        if (knife < 0 && pt > 0.12f && pt < 0.6f) {  // (once: it is caught at 0.62)
          a.ctl.itemL = 0;
          knife = w.addPuppet(rm::R_KNIFE, a.color, 3);
          kp0 = handOf(a, false);
          w.sound(SFX_WHOOSH, 0.3f, kp0, 1.8f);
        }
        if (Puppet* k = w.puppet(knife)) {
          float t = pt - 0.12f, T0 = 0.5f;
          float h = 4.0f * (t / T0) * (1.0f - t / T0) * 0.28f;
          k->pos = handOf(a, false) + vec3(0, std::max(0.0f, h), 0);
          k->yaw = a.yaw;
          k->tilt = glm::angleAxis(t * 22.0f, vec3(1, 0, 0));
          if (t >= T0) {
            k->alive = false;
            knife = -1;
            a.ctl.itemL = 1;
            w.sound(SFX_PARRY, 0.15f, a.pos, 2.2f);
          }
        }
        victimGuard(v, seg(pt, 0.2f, 0.7f));
        brace(w, 0.6f);
        if (pt > 0.75f && knife < 0) next();
        break;
      }
      case 2: {  // draw back the left arm
        float u = easeInOut(seg(pt, 0.0f, 0.45f));
        a.ctl.armL = glm::mix(vec3(0.7f, 0.0f, 0.6f), vec3(2.3f, 0.2f, 1.0f), u);
        a.ctl.twist = -0.45f * u;
        a.ctl.lean = -0.1f * u;
        a.ctl.gait = kPI * 0.5f;
        a.ctl.stride = 0.4f * u;
        if (pt > 0.5f) next();
        break;
      }
      case 3: {  // the throw
        float u = easeIn(seg(pt, 0.0f, 0.12f));
        a.ctl.armL = glm::mix(vec3(2.3f, 0.2f, 1.0f), vec3(1.2f, -0.1f, -0.3f), u);
        a.ctl.twist = glm::mix(-0.45f, 0.35f, u);
        a.ctl.lean = glm::mix(-0.1f, 0.25f, u);
        a.ctl.gait = glm::mix(kPI * 0.5f, kPI * 1.5f, u);
        if (u >= 1.0f && !flew) {
          flew = true;
          a.ctl.itemL = 0;
          knife = w.addPuppet(rm::R_KNIFE, a.color, 3);
          kp0 = handOf(a, false) + vec3(0, 0.05f, 0);
          kTarget = isHuman(v) ? headOf(v) - vec3(0, 0.1f, 0) * v.scale : chestOf(v);
          T = 0.22f + 0.05f * L;
          kv = (kTarget - kp0) / T + vec3(0, 0.5f * 3.0f * T, 0);
          kt = 0;
          w.sound(SFX_GRUNT, 0.7f, a.pos, 1.05f);
          next();
        }
        break;
      }
      case 4: {  // end over end through the air
        kt += dt;
        float t = std::min(kt, T);
        vec3 p = kp0 + kv * t + vec3(0, -0.5f * 3.0f * t * t, 0);
        if (Puppet* k = w.puppet(knife)) {
          k->pos = p;
          k->yaw = faceAngle(dir);
          k->tilt = glm::angleAxis(kt * 26.0f, vec3(1, 0, 0));
          if (int(kt * 12.0f) != int((kt - dt) * 12.0f)) w.sound(SFX_WHOOSH, 0.25f, p, 2.4f);
        }
        a.ctl.armL = glm::mix(a.ctl.armL, vec3(0.6f, 0.0f, 0.2f), std::min(1.0f, dt * 3.0f));
        a.ctl.twist = approach(a.ctl.twist, 0.0f, dt);
        a.ctl.lean = approach(a.ctl.lean, 0.0f, dt);
        brace(w, 1.0f);
        if (kt >= T) {
          // it sticks, point first, in the throat
          glm::mat4 M = propMatrix(kTarget - dir * 0.05f, dir + vec3(0, -0.15f, 0));
          if (Puppet* k = w.puppet(knife)) setPuppetMatrix(*k, M);
          stuck.attach(w, knife, v, M);
          wound(w, kTarget, dir + vec3(0, 0.25f, 0), isTower(v) ? 0.0f : 0.9f);
          kill(w, 1.0f, dir * 0.3f, 0.8f);
          w.slowmo(0.35f, 0.45f);
          next();
        }
        break;
      }
      case 5:  // it clutches its throat and goes down
        if (fall.active() && fall.t < fall.stagger && isHuman(v)) {
          victimDown(w, dt);
          v.ctl.armR = glm::mix(v.ctl.armR, vec3(2.0f, -0.5f, 1.7f), std::min(1.0f, dt * 8.0f));
          v.ctl.armL = glm::mix(v.ctl.armL, vec3(2.0f, -0.5f, 1.7f), std::min(1.0f, dt * 8.0f));
          if (std::fmod(pt, 0.25f) < dt) bloodSpray(w, kTarget, dir + vec3(0, 0.3f, 0), 6, 1.2f);
        } else if (victimDown(w, dt) && pt > 1.0f) {
          next();
        }
        break;
      case 6:  // he walks over and pulls his knife out
        if (enter()) walker = Walker(plan.mover, V - dir * 0.3f, Walker::FOOT, 0.8f);
        if (walker.update(w, dt)) {
          if (Puppet* k = w.puppet(knife)) {
            bloodSpray(w, k->pos, vec3(0, 1, 0), 10, 1.0f);
            k->alive = false;
          }
          stuck.puppet = -1;
          a.ctl.itemL = 1;
          a.ctl.armL = vec3(0.4f, 0.0f, 0.6f);
          w.sound(SFX_STAB, 0.4f, a.pos, 1.4f);
          next();
        }
        break;
      case 7:
        if (moveInR(w, dt)) next();
        break;
      case 8:
        return finishR(w);
    }
    return false;
  }
};

// ===========================================================================
// ROOK - the siege tower
// ===========================================================================
// A puppet soldier (the tower's garrison) running about.
struct Trooper {
  int id = -1;
  vec3 target{0};
  float speed = 1.4f;
  bool arrived = false;
  void spawn(World& w, int color, const vec3& at, float yaw) {
    id = w.addPuppet(rm::R_PAWN, color, 3);
    if (Puppet* p = w.puppet(id)) {
      p->pos = flat(at);
      p->yaw = yaw;
      p->dissolve = 1.0f;
      p->ctl.draw = 1;
      p->ctl.idle = w.fx.rnd(0.0f, 50.0f);
    }
  }
  Puppet* get(World& w) { return w.puppet(id); }
  bool run(World& w, float dt) {
    Puppet* p = get(w);
    if (!p) return true;
    vec3 d = flat(target - p->pos);
    float l = glm::length(d);
    if (l < 0.01f) {
      p->ctl.stride = approach(p->ctl.stride, 0.0f, dt * 4.0f);
      arrived = true;
      return p->ctl.stride <= 0.001f;
    }
    float step = std::min(l, speed * dt);
    p->pos += d / l * step;
    float want = faceAngle(d);
    p->yaw = wrapAngle(p->yaw + glm::clamp(wrapAngle(want - p->yaw), -dt * 10.0f, dt * 10.0f));
    p->ctl.gait += step / rm::strideCycle(K_PAWN) * kTau * 0.8f;
    p->ctl.stride = 1.0f;
    p->ctl.lean = 0.12f;
    p->ctl.cape = 0.5f;
    if (int(p->ctl.gait / kPI) != int((p->ctl.gait - step / rm::strideCycle(K_PAWN) * kTau * 0.8f) / kPI))
      w.sound(SFX_ARMOR, 0.25f, p->pos, w.fx.rnd(1.0f, 1.2f));
    return false;
  }
};

// Battering ram: the tower rolls up, the door drops, and the ram swings out.
class TowerRam : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  bool hit = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.0f;
        shotHeight = 0.5f;
        if (first) w.sound(SFX_CORNU, 0.6f, a.pos);
        if (faceOffR(w)) next();
        break;
      case 1:  // roll up within reach
        if (enter()) walker = Walker(plan.mover, V - dir * std::min(L, v.radius() + 0.47f), Walker::TOWER, std::max(0.8f, L / 2.6f));
        victimGuard(v, seg(pt, 0.0f, 0.8f));
        brace(w, 0.8f);
        if (walker.update(w, dt)) next();
        break;
      case 2:  // the door drops
        if (enter()) {
          w.sound(SFX_CHAIN, 0.8f, a.pos, 1.1f);
          w.sound(SFX_CREAK, 0.6f, a.pos, 0.9f);
        }
        a.ctl.gate = easeIn(seg(pt, 0.0f, 0.5f));
        if (pt > 0.5f) {
          w.sound(SFX_THUD, 0.6f, a.pos + dir * 0.4f, 1.1f);
          w.fx.dust(a.pos + dir * 0.45f, 10, 0.2f, 0.7f);
          next();
        }
        break;
      case 3:  // heave back...
        if (enter()) w.sound(SFX_CREAK, 0.7f, a.pos, 0.75f);
        a.ctl.kick = -0.35f * easeInOut(seg(pt, 0.0f, 0.6f));
        a.ctl.lean = -0.015f * seg(pt, 0.0f, 0.6f);
        brace(w, 1.0f);
        if (pt > 0.65f) next();
        break;
      case 4: {  // ...and swing!
        float u = easeIn(seg(pt, 0.0f, 0.2f));
        a.ctl.kick = glm::mix(-0.35f, 1.0f, u);
        a.ctl.lean = glm::mix(-0.015f, 0.02f, u);
        if (!hit && u >= 1.0f) {
          hit = true;
          vec3 c = modelPoint(a, rm::ramTip());
          w.sound(SFX_RAM, 1.0f, c);
          w.sound(SFX_GRUNT, 0.5f, a.pos + vec3(0, 0.3f, 0), 0.8f);
          w.shake(0.55f);
          w.slowmo(0.3f, 0.5f);
          w.fx.dust(flat(c), 16, 0.3f, 1.2f);
          if (!isTower(v)) {
            bloodSpray(w, chestOf(v), dir + vec3(0, 0.6f, 0), 45, 3.0f);
            w.sound(SFX_BLOOD, 0.6f, c);
          }
          knockFlying(w, dir * 2.4f + vec3(0, 2.8f, 0), 7.5f);
        }
        if (pt > 0.25f) next();
        break;
      }
      case 5:  // the ram swings back in, the door is raised
        a.ctl.kick = 1.0f - easeInOut(seg(pt, 0.0f, 0.8f));
        a.ctl.lean = 0.02f * (1.0f - seg(pt, 0.0f, 0.4f));
        a.ctl.gate = 1.0f - easeInOut(seg(pt, 0.6f, 1.2f));
        if (pt > 0.6f && pt - dt <= 0.6f) w.sound(SFX_CHAIN, 0.6f, a.pos, 1.2f);
        if (victimDown(w, dt) && pt > 1.25f) next();
        break;
      case 6:
        if (moveInR(w, dt)) next();
        break;
      case 7:
        return finishR(w);
    }
    return false;
  }
};

// Sally: the door drops and two legionaries of the garrison charge out; one takes
// the victim's blow on his shield, the other cuts it down; back they go.
class TowerSally : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  Trooper t1, t2;
  bool blocked = false, stabbed = false;
  vec3 door{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 side(-dir.z, 0, dir.x);
    switch (phase) {
      case 0:
        shotExtra = 1.1f;
        shotHeight = 0.5f;
        if (first) w.sound(SFX_CORNU, 0.6f, a.pos, 1.05f);
        if (faceOffR(w)) next();
        break;
      case 1:
        if (enter()) walker = Walker(plan.mover, V - dir * std::min(L, 1.3f), Walker::TOWER, std::max(0.8f, L / 2.6f));
        victimGuard(v, seg(pt, 0.0f, 0.8f));
        brace(w, 0.6f);
        if (walker.update(w, dt)) next();
        break;
      case 2:  // the door and the bridge drop; the garrison roars
        if (enter()) {
          w.sound(SFX_CHAIN, 0.8f, a.pos, 1.05f);
          w.sound(SFX_SHOUT, 0.7f, a.pos + vec3(0, 1.0f, 0), 0.95f);
        }
        a.ctl.gate = easeIn(seg(pt, 0.0f, 0.5f));
        a.ctl.bridge = easeIn(seg(pt, 0.1f, 0.6f));
        if (pt > 0.6f) {
          w.sound(SFX_THUD, 0.6f, a.pos + dir * 0.4f, 1.1f);
          door = modelPoint(a, rm::doorPoint());
          t1.spawn(w, a.color, door + side * 0.05f - dir * 0.15f, a.yaw);
          t2.spawn(w, a.color, door - side * 0.05f - dir * 0.15f, a.yaw);
          t1.target = V - dir * 0.43f + side * 0.2f;
          t2.target = V - dir * 0.2f - side * 0.36f;
          next();
        }
        break;
      case 3: {  // they charge out
        if (enter()) w.sound(SFX_SHOUT, 0.8f, door, 1.08f);
        for (Trooper* t : {&t1, &t2})
          if (Puppet* p = t->get(w)) p->dissolve = std::max(0.0f, p->dissolve - dt * 3.0f);
        bool d1 = t1.run(w, dt), d2 = pt > 0.15f ? t2.run(w, dt) : false;
        victimGuard(v, 1.0f);
        brace(w, 0.9f);
        if (isHuman(v)) v.yawTarget = faceAngle(t1.target - V);
        if (d1 && d2) next();
        break;
      }
      case 4: {  // the victim strikes at the first; his shield takes it; the second stabs
        Puppet* p1 = t1.get(w);
        Puppet* p2 = t2.get(w);
        float u = seg(pt, 0.0f, 0.85f);
        if (p1) {
          p1->yaw = faceAngle(V - p1->pos);
          guard(p1->ctl, 1.0f, true);
          block(p1->ctl, bump(u, 0.2f, 0.8f));
        }
        if (!isTower(v)) victimStrike(v, u);
        if (!blocked && u > 0.55f && p1) {
          blocked = true;
          vec3 s = puppetPoint(*p1, rm::RH_ITEML, rm::handL(K_PAWN)) + vec3(0, 0.08f, 0);
          if (!isTower(v)) {
            w.sound(SFX_SHIELD, 1.0f, s);
            sparksAt(w, s, vec3(0, 1, 0), 12);
          }
        }
        if (p2) {
          p2->yaw = faceAngle(V - p2->pos);
          float s2 = seg(pt, 0.45f, 1.2f);
          thrust(p2->ctl, s2);
          if (!stabbed && s2 > 0.52f) {
            stabbed = true;
            vec3 c = chestOf(v) - vec3(0, 0.06f, 0);
            vec3 d = flatDir(p2->pos, V);
            wound(w, c, d + vec3(0, 0.15f, 0), isTower(v) ? 0.2f : 1.1f);
            kill(w, 1.0f, (dir + d) * 0.25f, 0.5f);
            w.shake(0.25f);
            w.slowmo(0.3f, 0.45f);
          }
        }
        if (stabbed) victimDown(w, dt);
        if (pt > 1.25f) next();
        break;
      }
      case 5: {  // they cheer
        for (Trooper* t : {&t1, &t2})
          if (Puppet* p = t->get(w)) {
            p->ctl.armR = glm::mix(p->ctl.armR, vec3(2.6f, 0.3f, 0.3f), std::min(1.0f, dt * 5.0f));
            p->ctl.itemPitch = approach(p->ctl.itemPitch, 0.0f, dt * 3.0f);
            p->ctl.armL = glm::mix(p->ctl.armL, vec3(0.5f, 0.05f, -0.15f), std::min(1.0f, dt * 5.0f));
          }
        if (enter()) w.sound(SFX_SHOUT, 0.8f, V, 1.0f);
        if (victimDown(w, dt) && pt > 0.8f) {
          t1.target = door - dir * 0.15f;
          t2.target = door - dir * 0.15f;
          t1.arrived = t2.arrived = false;
          next();
        }
        break;
      }
      case 6: {  // and back inside
        for (Trooper* t : {&t1, &t2})
          if (Puppet* p = t->get(w)) {
            p->ctl.armR = glm::mix(p->ctl.armR, vec3(0), std::min(1.0f, dt * 4.0f));
            if (glm::length(flat(p->pos - door)) < 0.25f) p->dissolve = std::min(1.0f, p->dissolve + dt * 4.0f);
          }
        bool d1 = t1.run(w, dt), d2 = t2.run(w, dt);
        if (d1 && d2) {
          for (Trooper* t : {&t1, &t2})
            if (Puppet* p = t->get(w)) p->alive = false;
          next();
        }
        break;
      }
      case 7:  // the door goes up
        if (enter()) w.sound(SFX_CHAIN, 0.7f, a.pos, 1.15f);
        a.ctl.gate = 1.0f - easeInOut(seg(pt, 0.0f, 0.6f));
        a.ctl.bridge = 1.0f - easeInOut(seg(pt, 0.0f, 0.6f));
        if (pt > 0.65f) next();
        break;
      case 8:
        if (moveInR(w, dt)) next();
        break;
      case 9:
        return finishR(w);
    }
    return false;
  }
};

// Scorpion: the bolt-thrower on the roof swings round, takes aim and looses.
class TowerScorpion : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  Missile bolt;
  Stuck stuck;
  float yaw0 = 0, pitch0 = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    if (stuck.puppet >= 0) stuck.update(w, fall.corpse >= 0 ? fall.corpse : tumble.corpse);
    switch (phase) {
      case 0:
        shotExtra = 1.4f;
        shotHeight = 0.8f;
        if (faceOffR(w)) next();
        break;
      case 1: {  // aim
        if (enter()) {
          w.sound(SFX_CREAK, 0.6f, a.pos + vec3(0, 1.2f, 0), 1.2f);
          w.sound(SFX_SHOUT, 0.5f, a.pos + vec3(0, 1.2f, 0), 1.1f);
          vec3 s = modelPoint(a, rm::scorpionPoint());
          vec3 t = chestOf(v);
          float d = glm::length(flat(t - s));
          pitch0 = std::atan2(s.y - t.y, d);
          yaw0 = wrapAngle(faceAngle(t - s) - a.yaw);
        }
        float u = easeInOut(seg(pt, 0.0f, 0.9f));
        a.ctl.itemYaw = yaw0 * u;
        a.ctl.itemPitch = pitch0 * u + 0.05f * std::sin(pt * 30.0f) * (1.0f - u);
        victimGuard(v, seg(pt, 0.2f, 0.9f));
        brace(w, 0.9f);
        if (pt > 1.1f) next();
        break;
      }
      case 2: {  // loose!
        if (enter()) {
          a.ctl.hatch = 1;
          vec3 s = modelPoint(a, rm::scorpionPoint());
          vec3 t = chestOf(v);
          bolt.launch(w, rm::R_BOLT, a.color, s, t, glm::length(t - s) / 7.5f, 0.32f, 3.0f);
          w.sound(SFX_SCORPION, 1.0f, s);
          w.shake(0.12f);
        }
        a.ctl.lean = -0.02f * bump(pt, 0.0f, 0.3f);
        brace(w, 1.0f);
        if (bolt.update(w, dt)) {
          vec3 hd = bolt.heading();
          glm::mat4 M = propMatrix(bolt.tip() + hd * 0.1f - hd * 0.32f, hd);
          if (Puppet* p = w.puppet(bolt.puppet)) setPuppetMatrix(*p, M);
          stuck.attach(w, bolt.puppet, v, M);
          wound(w, chestOf(v), flat(hd) + vec3(0, 0.3f, 0), isTower(v) ? 0.0f : 1.2f);
          if (isHuman(v)) {
            w.sound(SFX_STAB, 0.8f, v.pos, 0.8f);
            knockFlying(w, flat(hd) * 1.6f + vec3(0, 1.6f, 0), 3.2f);
          } else {
            knockFlying(w, flat(hd) * 1.5f, 0.0f);
          }
          w.slowmo(0.3f, 0.5f);
          next();
        }
        break;
      }
      case 3:  // the crew cheers; a new bolt
        if (enter()) w.sound(SFX_SHOUT, 0.7f, a.pos + vec3(0, 1.2f, 0), 1.0f);
        a.ctl.lean = 0;
        if (pt > 1.0f) a.ctl.hatch = 0;
        a.ctl.itemYaw = yaw0 * (1.0f - easeInOut(seg(pt, 0.6f, 1.4f)));
        a.ctl.itemPitch = pitch0 * (1.0f - easeInOut(seg(pt, 0.6f, 1.4f)));
        if (victimDown(w, dt) && pt > 1.4f) next();
        break;
      case 4:
        if (moveInR(w, dt)) next();
        break;
      case 5:
        return finishR(w);
    }
    return false;
  }
};

}  // namespace

namespace {

// ===========================================================================
// QUEEN - the empress
// ===========================================================================
// Hidden dagger: she holds out her hand in friendship; as the victim bows, she
// draws a dagger from her palla and stabs, and stabs again.
class EmpressDagger : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  bool s1 = false, s2 = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOffR(w, 0.3f)) next();
        break;
      case 1:  // she walks up, smiling
        if (enter()) walker = Walker(plan.mover, V - dir * (touchDist(a, v) * 1.15f), Walker::FOOT, std::max(0.8f, L / 1.8f));
        v.tremble = 0;
        v.idleWeight = 0;
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // her hand held out; the victim bows
        float u = easeInOut(seg(pt, 0.0f, 0.6f));
        a.ctl.armL = vec3(1.0f, 0.15f, -0.25f) * u;
        a.ctl.headPitch = 0.1f * u;
        if (isHuman(v)) {
          v.ctl.lean = 0.3f * easeInOut(seg(pt, 0.3f, 0.9f));
          v.ctl.headPitch = 0.3f * easeInOut(seg(pt, 0.3f, 0.9f));
        } else if (isChariot(v)) {
          v.ctl.lean = 0.25f * easeInOut(seg(pt, 0.3f, 0.9f));
        }
        if (pt > 1.0f) next();
        break;
      }
      case 3: {  // the dagger
        if (enter()) {
          a.ctl.draw = 1;
          w.sound(SFX_DRAW, 0.4f, a.pos, 1.4f);
        }
        float u1 = seg(pt, 0.0f, 0.55f), u2 = seg(pt, 0.6f, 1.15f);
        thrust(a.ctl, pt < 0.6f ? u1 : u2);
        a.ctl.armL = vec3(1.0f, 0.15f, -0.25f) * (1.0f - u1) + vec3(0.6f, 0.0f, 0.4f) * u1;
        if (!s1 && u1 > 0.52f) {
          s1 = true;
          vec3 c = headOf(v) - vec3(0, 0.13f, 0) * v.scale;
          if (isChariot(v) || isTower(v)) c = chestOf(v);
          wound(w, c, dir + vec3(0, 0.3f, 0), isTower(v) ? 0.2f : 0.9f);
          w.sound(SFX_DEATH, 0.5f, c, 1.25f);
          v.ctl.lean = -0.1f;
          v.ctl.headPitch = -0.3f;
        }
        if (!s2 && u2 > 0.52f) {
          s2 = true;
          vec3 c = chestOf(v);
          wound(w, c, dir + vec3(0, 0.1f, 0), isTower(v) ? 0.2f : 1.0f);
          kill(w, 1.0f, dir * 0.25f, 0.8f);
          w.slowmo(0.35f, 0.45f);
        }
        if (s2) victimDown(w, dt);
        if (pt > 1.2f) next();
        break;
      }
      case 4: {  // she wipes her blade on her palla
        float u = seg(pt, 0.0f, 0.8f);
        a.ctl.armR = glm::mix(vec3(0.45f, 0.1f, 0.45f), vec3(0.5f, -0.5f, 0.8f), bump(u, 0.0f, 1.0f));
        a.ctl.armL = vec3(0.4f, 0.1f, 0.6f) * (1.0f - u);
        a.ctl.lean = 0.05f * bump(u, 0.0f, 1.0f);
        a.ctl.headPitch = 0.15f * bump(u, 0.0f, 1.0f);
        a.ctl.itemPitch = 0.6f;
        if (victimDown(w, dt) && pt > 1.0f) {
          a.ctl.draw = 0;
          next();
        }
        break;
      }
      case 5:
        a.ctl.armR = glm::mix(a.ctl.armR, vec3(0), std::min(1.0f, dt * 4.0f));
        a.ctl.itemPitch = approach(a.ctl.itemPitch, 0.0f, dt * 2.0f);
        if (moveInR(w, dt)) next();
        break;
      case 6:
        return finishR(w);
    }
    return false;
  }
};

// The poisoned cup: she offers the victim her goblet; it drinks, chokes, and falls.
class EmpressCup : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  int cup = -1;
  vec3 cupVel{0};
  bool dropped = false, gulped = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    Puppet* c = w.puppet(cup);
    if (c && !dropped) {  // in the victim's hand
      c->pos = handOf(v, true) + vec3(0, -0.005f, 0);
      c->yaw = v.yaw;
      c->tilt = glm::angleAxis(isChariot(v) ? 0.0f : -0.9f * glm::clamp(v.ctl.armR.z / 1.9f, 0.0f, 1.0f), vec3(1, 0, 0));
    } else if (c) {  // falling, and rolling on the board
      cupVel.y -= 9.8f * dt;
      c->pos += cupVel * dt;
      if (c->pos.y < 0.012f) {
        c->pos.y = 0.012f;
        if (cupVel.y < -0.5f) w.sound(SFX_PARRY, 0.25f, c->pos, 1.8f);
        cupVel = vec3(cupVel.x * 0.5f, -cupVel.y * 0.3f, cupVel.z * 0.5f);
        c->tilt = glm::angleAxis(1.5708f, vec3(0, 0, 1));
      }
    }
    switch (phase) {
      case 0:
        if (faceOffR(w, 0.3f)) next();
        break;
      case 1:
        if (enter()) walker = Walker(plan.mover, V - dir * (touchDist(a, v) * 1.25f), Walker::FOOT, std::max(0.8f, L / 1.8f));
        v.tremble = 0;
        v.idleWeight = 0;
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // she offers the cup
        a.ctl.hatch = 1;
        float u = easeInOut(seg(pt, 0.0f, 0.6f));
        a.ctl.armL = vec3(1.05f, 0.05f, -0.35f) * u;
        a.ctl.headPitch = 0.1f * u;
        if (pt > 0.8f) next();
        break;
      }
      case 3: {  // the victim takes it
        float u = easeInOut(seg(pt, 0.0f, 0.5f));
        v.ctl.itemR = 0;
        v.ctl.draw = 0;
        v.ctl.armR = vec3(0.75f, 0.0f, 0.3f) * u;
        if (cup < 0 && pt > 0.3f) {
          a.ctl.hatch = 0;
          cup = w.addPuppet(rm::R_GOBLET, a.color, 3);
          w.sound(SFX_PARRY, 0.1f, handOf(a, false), 2.4f);
        }
        a.ctl.armL = vec3(1.05f, 0.05f, -0.35f) * (1.0f - seg(pt, 0.3f, 0.6f));
        if (pt > 0.6f) next();
        break;
      }
      case 4: {  // it drinks, deep
        float u = easeInOut(seg(pt, 0.0f, 0.5f));
        v.ctl.armR = glm::mix(vec3(0.75f, 0.0f, 0.3f), vec3(1.3f, -0.25f, 1.9f), u);
        v.ctl.headPitch = -0.45f * easeInOut(seg(pt, 0.3f, 0.8f));
        v.ctl.lean = -0.08f * u;
        if (!gulped && pt > 0.5f) {
          gulped = true;
          w.sound(SFX_GULP, 0.8f, headOf(v));
        }
        a.ctl.armL = vec3(0.3f, 0.1f, 0.6f) * seg(pt, 0.0f, 0.5f);
        if (pt > 1.3f) next();
        break;
      }
      case 5: {  // the poison: the cup falls, it clutches its throat, chokes, and falls on its face
        if (!dropped && pt > 0.1f) {
          dropped = true;
          cupVel = v.forward() * 0.4f + vec3(0, 0.3f, 0);
          w.sound(SFX_GRUNT, 0.6f, v.pos, 0.7f);
        }
        float u = easeInOut(seg(pt, 0.1f, 0.4f));
        if (isHuman(v)) {
          v.ctl.armR = glm::mix(vec3(1.3f, -0.25f, 1.9f), vec3(2.0f, -0.5f, 1.7f), u);
          v.ctl.armL = glm::mix(v.ctl.armL, vec3(2.0f, -0.5f, 1.7f), std::min(1.0f, dt * 6.0f));
          v.ctl.headPitch = glm::mix(-0.45f, 0.2f, u);
          v.ctl.lean = 0.25f * u + 0.08f * std::sin(pt * 31.0f) * u;
          v.ctl.crouch = 0.3f * u;
        } else if (isChariot(v)) {
          v.ctl.armR = glm::mix(v.ctl.armR, vec3(1.2f, 0.0f, 1.8f), std::min(1.0f, dt * 6.0f));
          v.ctl.rear = 0.3f * u;
        }
        v.tremble = 1.4f * u;
        if (pt > 0.5f && std::fmod(pt, 0.35f) < dt) w.sound(SFX_GULP, 0.35f, headOf(v), 0.75f);
        if (pt > 1.5f) {
          fall.bleed = 0.5f;
          kill(w, -1.0f, dir * 0.1f, 0.25f);
          if (Puppet* cp = w.puppet(cup)) {
            cp->life = 2.0f;
            bloodPool(w, cp->pos, 0.06f, 0.4f, 6.0f);  // the wine spilled
          }
          next();
        }
        break;
      }
      case 6:
        a.ctl.armL = glm::mix(a.ctl.armL, vec3(0), std::min(1.0f, dt * 2.0f));
        if (victimDown(w, dt) && pt > 0.6f) next();
        break;
      case 7:
        if (moveInR(w, dt)) next();
        break;
      case 8:
        return finishR(w);
    }
    return false;
  }
};

// Whirling spear: she twirls her spear, sweeps the victim's legs from under it so
// that it flips through the air, and spears it where it lands.
class EmpressSpear : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  float spin = 0, whirl = 0;
  bool swept = false, speared = false;
  vec3 body{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.7f;
        if (faceOffR(w)) next();
        break;
      case 1: {  // the spear twirls; she advances
        if (enter()) walker = Walker(plan.mover, V - dir * std::min(L, 0.62f), Walker::FOOT, std::max(0.9f, L / 2.0f));
        whirl = approach(whirl, 17.0f, dt * 18.0f);
        float prev = spin;
        spin += whirl * dt;
        if (int(spin / kPI) != int(prev / kPI)) w.sound(SFX_WHOOSH, 0.45f, a.pos, 1.2f);
        a.ctl.armR = vec3(0.9f, 0.2f, 0.2f);
        a.ctl.itemPitch = 1.5708f + spin;
        victimGuard(v, seg(pt, 0.0f, 0.5f));
        brace(w, 0.8f);
        if (walker.update(w, dt) && pt > 1.0f) next();
        break;
      }
      case 2: {  // the sweep, low
        float u = seg(pt, 0.0f, 0.45f);
        spin += whirl * dt * (1.0f - u);
        a.ctl.crouch = 0.55f * bump(u, 0.0f, 1.3f);
        a.ctl.twist = glm::mix(0.6f, -0.7f, u);
        a.ctl.lean = 0.25f * bump(u, 0.0f, 1.3f);
        a.ctl.armR = glm::mix(vec3(0.9f, 0.6f, 0.2f), vec3(0.8f, -0.4f, 0.0f), u);
        a.ctl.itemPitch = glm::mix(fmodf(1.5708f + spin, kTau), 2.3f, u);
        a.ctl.itemYaw = glm::mix(1.0f, -1.0f, u);
        if (!swept && u > 0.55f) {
          swept = true;
          w.sound(SFX_WHOOSH, 0.9f, V, 0.8f);
          w.sound(SFX_THUD, 0.8f, V, 1.3f);
          w.fx.dust(V, 12, 0.25f, 0.9f);
          if (isHuman(v)) {
            tumble.life = 3.0f;
            tumble.launch(w, v, vec3(0, 3.6f, 0) + dir * 0.15f, 10.5f);  // up it goes, head over heels
          } else {
            knockFlying(w, dir * 1.0f, 0.0f);
          }
          w.slowmo(0.35f, 0.6f);
        }
        if (pt > 0.5f) next();
        break;
      }
      case 3: {  // she rises with the spear high, point down, waiting for it to land
        float u = easeInOut(seg(pt, 0.0f, 0.4f));
        a.ctl.crouch = 0;
        a.ctl.twist = -0.7f * (1.0f - u);
        a.ctl.armR = glm::mix(vec3(0.8f, -0.4f, 0.0f), vec3(2.5f, 0.0f, 0.7f), u);
        a.ctl.itemPitch = glm::mix(2.3f, 3.0f, u);
        a.ctl.itemYaw = -1.0f * (1.0f - u);
        bool down = victimDown(w, dt);
        if ((tumble.landed || down) && pt > 0.4f) {
          body = flat(v.alive ? v.pos : V);
          if (Puppet* cp = w.puppet(tumble.corpse)) body = flat(cp->pos);
          next();
        }
        break;
      }
      case 4: {  // and drives it down
        float u = easeIn(seg(pt, 0.0f, 0.22f));
        a.ctl.armR = glm::mix(vec3(2.5f, 0.0f, 0.7f), vec3(0.9f, 0.0f, -0.3f), u);
        a.ctl.lean = 0.35f * u;
        a.ctl.crouch = 0.3f * u;
        if (!speared && u >= 1.0f) {
          speared = true;
          vec3 c = body + vec3(0, 0.06f, 0);
          if (isHuman(v) || tumble.corpse >= 0) {
            wound(w, c, vec3(0, 1.3f, 0), 1.3f);
            w.sound(SFX_DEATH, 0.6f, c, 0.8f);
          }
          w.shake(0.3f);
        }
        victimDown(w, dt);
        if (pt > 0.7f) next();
        break;
      }
      case 5: {  // she pulls it out and raises it
        float u = easeInOut(seg(pt, 0.0f, 0.6f));
        a.ctl.armR = glm::mix(vec3(0.9f, 0.0f, -0.3f), vec3(2.4f, 0.2f, 0.3f), u);
        a.ctl.itemPitch = glm::mix(3.0f, 0.0f, u);
        a.ctl.lean = 0.35f * (1.0f - u);
        a.ctl.crouch = 0.3f * (1.0f - u);
        if (victimDown(w, dt) && pt > 0.8f) next();
        break;
      }
      case 6:
        if (first) {
          a.ctl.armR = vec3(0);
          a.ctl.itemPitch = 0;
        }
        if (moveInR(w, dt)) next();
        break;
      case 7:
        return finishR(w);
    }
    return false;
  }
};

// ===========================================================================
// KING - the emperor
// ===========================================================================
// Thumbs down: the crowd roars, and a volley of pila rains down on the victim.
class EmperorThumbsDown : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  static const int N = 7;
  Missile pila[N];
  Stuck stuck[N];
  float launchAt[N];
  bool launched[N] = {}, landed[N] = {};
  vec3 targets[N];
  bool killed = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    for (int i = 0; i < N; ++i)
      if (stuck[i].puppet >= 0) stuck[i].update(w, fall.corpse);
    switch (phase) {
      case 0:
        shotExtra = 1.3f;
        shotHeight = 0.7f;
        if (first) w.sound(SFX_CORNU, 0.7f, a.pos);
        if (faceOffR(w)) next();
        break;
      case 1: {  // his arm out, thumb down; the crowd roars
        if (enter()) w.sound(SFX_CROWD, 0.9f, a.pos);
        float u = easeInOut(seg(pt, 0.0f, 0.6f));
        a.ctl.armL = vec3(1.55f, 0.05f, -0.1f) * u;
        a.ctl.headPitch = -0.1f * u;
        a.ctl.lean = -0.05f * u;
        victimGuard(v, seg(pt, 0.3f, 0.9f));
        brace(w, 1.0f);
        if (pt > 1.0f) next();
        break;
      }
      case 2: {  // the volley
        if (enter()) {
          vec3 side(-dir.z, 0, dir.x);
          for (int i = 0; i < N; ++i) {
            launchAt[i] = 0.08f * i + w.fx.rnd(0.0f, 0.05f);
            if (i < 3) targets[i] = chestOf(v) + vec3(w.fx.rnd(-0.04f, 0.04f), w.fx.rnd(-0.08f, 0.06f), w.fx.rnd(-0.04f, 0.04f));
            else targets[i] = V + side * w.fx.rnd(-0.45f, 0.45f) + dir * w.fx.rnd(-0.3f, 0.45f) + vec3(0, -0.06f, 0);
          }
          w.sound(SFX_WHOOSH, 1.0f, V, 0.6f);
        }
        a.ctl.armL = vec3(1.55f, 0.05f, -0.1f);
        brace(w, 1.0f);
        if (isHuman(v) && !killed) v.ctl.headPitch = -0.5f;  // it looks up
        bool all = true;
        for (int i = 0; i < N; ++i) {
          if (!launched[i]) {
            if (pt >= launchAt[i]) {
              launched[i] = true;
              vec3 from = A - dir * 5.0f + vec3(w.fx.rnd(-1.5f, 1.5f), 5.0f, w.fx.rnd(-1.5f, 1.5f));
              pila[i].launch(w, rm::R_PILUM, a.color, from, targets[i], 0.85f, 0.95f, 9.8f);
              if (i % 2 == 0) w.sound(SFX_WHOOSH, 0.5f, targets[i], w.fx.rnd(0.7f, 0.9f));
            }
            all = false;
            continue;
          }
          if (landed[i]) continue;
          if (pila[i].update(w, dt)) {
            landed[i] = true;
            vec3 hd = pila[i].heading();
            glm::mat4 M = propMatrix(pila[i].tip() + hd * 0.08f - hd * 0.95f, hd);
            if (Puppet* p = w.puppet(pila[i].puppet)) setPuppetMatrix(*p, M);
            if (i < 3 && v.alive) {
              stuck[i].attach(w, pila[i].puppet, v, M);
              wound(w, targets[i], -flat(hd) * 0.3f + vec3(0, 0.4f, 0) + dir * 0.3f, isTower(v) ? 0.0f : 0.8f);
              if (!killed) {
                killed = true;
                kill(w, 1.0f, dir * 0.3f, 0.8f);
                w.slowmo(0.35f, 0.5f);
              }
            } else {
              if (Puppet* p = w.puppet(pila[i].puppet)) {
                p->life = 4.0f;
                p->fadeRate = 1.0f;
              }
              w.sound(SFX_THUD, 0.5f, targets[i], 1.5f);
              w.fx.dust(flat(targets[i]), 4, 0.06f, 0.5f);
            }
          } else {
            all = false;
          }
        }
        if (killed) victimDown(w, dt);
        if (all) next();
        break;
      }
      case 3:
        a.ctl.armL = vec3(1.55f, 0.05f, -0.1f) * (1.0f - easeInOut(seg(pt, 0.5f, 1.1f)));
        if (pt > 0.3f && pt - dt <= 0.3f) w.sound(SFX_CROWD, 0.7f, a.pos, 1.05f);
        if (victimDown(w, dt) && pt > 1.2f) next();
        break;
      case 4:
        if (moveInR(w, dt)) next();
        break;
      case 5:
        return finishR(w);
    }
    return false;
  }
};

// Gladius duel: he draws his sword, parries, is parried, then spins and runs the
// victim through.
class EmperorDuel : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  bool c1 = false, c2 = false, stab = false;
  float spin0 = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    bool fights = !isTower(v);
    switch (phase) {
      case 0:
        if (faceOffR(w)) next();
        break;
      case 1: {  // the cross draw from his left hip
        float u = seg(pt, 0.0f, 0.7f);
        a.ctl.armR = glm::mix(vec3(0), vec3(0.5f, -0.6f, 0.7f), bump(u, 0.0f, 1.2f)) + vec3(0.45f, 0.1f, 0.45f) * seg(u, 0.6f, 1.0f);
        a.ctl.twist = 0.25f * bump(u, 0.0f, 1.2f);
        if (u > 0.5f && a.ctl.draw < 0.5f) {
          a.ctl.draw = 1;
          w.sound(SFX_DRAW, 0.9f, a.pos, 0.9f);
        }
        a.ctl.itemPitch = 0.6f * seg(u, 0.6f, 1.0f);
        victimGuard(v, seg(pt, 0.2f, 0.8f));
        brace(w, 0.5f);
        if (pt > 0.8f) next();
        break;
      }
      case 2:
        if (enter()) walker = Walker(plan.mover, V - dir * (touchDist(a, v) * 1.5f + 0.06f), Walker::FOOT, std::max(0.85f, L / 2.0f));
        guard(a.ctl, 1.0f, false);
        if (walker.update(w, dt)) next();
        break;
      case 3: {  // its blow, parried
        float u = seg(pt, 0.0f, 0.85f);
        if (fights) victimStrike(v, u);
        parry(a.ctl, bump(u, 0.2f, 0.85f), true);
        if (!c1 && u > 0.56f) {
          c1 = true;
          if (fights) clash(w, a, v);
        }
        if (pt > 0.9f) next();
        break;
      }
      case 4: {  // his cut, parried in turn
        float u = seg(pt, 0.0f, 0.8f);
        overhead(a.ctl, u);
        if (isHuman(v)) parry(v.ctl, bump(u, 0.15f, 0.85f), true);
        if (!c2 && u > 0.56f) {
          c2 = true;
          if (fights) clash(w, a, v);
          else {
            wound(w, chestOf(v), dir, 0.0f, true);
            splinters(w, chestOf(v), -dir, 20);
          }
        }
        if (pt > 0.85f) next();
        break;
      }
      case 5: {  // a pirouette, and the thrust
        if (enter()) {
          spin0 = a.yaw;
          w.sound(SFX_WHOOSH, 0.8f, a.pos, 0.9f);
          w.sound(SFX_GRUNT, 0.8f, a.pos, 0.85f);
        }
        float u = seg(pt, 0.0f, 0.5f);
        a.yaw = spin0 + kTau * glm::smoothstep(0.0f, 1.0f, u);
        a.yawTarget = a.yaw;
        a.ctl.cape = 0.9f * bump(u, 0.0f, 1.0f);
        float t2 = seg(pt, 0.35f, 0.95f);
        thrust(a.ctl, 0.3f + 0.7f * t2);
        if (isHuman(v)) victimGuard(v, 1.0f - u);
        if (!stab && t2 > 0.34f) {
          stab = true;
          vec3 c = chestOf(v);
          wound(w, c, dir + vec3(0, 0.15f, 0), isTower(v) ? 0.2f : 1.2f);
          kill(w, 1.0f, dir * 0.3f, 0.75f);
          w.shake(0.3f);
          w.slowmo(0.3f, 0.5f);
        }
        if (u >= 1.0f) a.yaw = a.yawTarget = spin0;
        if (stab) victimDown(w, dt);
        if (pt > 1.0f) {
          a.yaw = a.yawTarget = spin0;
          next();
        }
        break;
      }
      case 6:
        guard(a.ctl, 1.0f, false);
        if (victimDown(w, dt) && pt > 0.3f) next();
        break;
      case 7:
        if (enter()) w.sound(SFX_CROWD, 0.7f, a.pos);
        triumph(w, pt / 0.8f);
        if (pt > 0.9f) next();
        break;
      case 8:
        if (moveInR(w, dt)) next();
        break;
      case 9:
        return finishR(w);
    }
    return false;
  }
};

// The lion: he points; a lion bounds onto the board, leaps on the victim and
// mauls it; it roars, and lopes away.
class EmperorLion : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  int lion = -1;
  vec3 from{0}, leap0{0}, leapTo{0}, exitTo{0};
  float run = 0;
  bool hit = false;
  Puppet* L_(World& w) { return w.puppet(lion); }
  void lionTo(World& w, const vec3& target, float speed, float dt) {
    Puppet* p = L_(w);
    if (!p) return;
    vec3 d = flat(target - p->pos);
    float l = glm::length(d);
    float step = std::min(l, speed * dt);
    if (l > 1e-4f) {
      p->pos += d / l * step;
      p->yaw = wrapAngle(p->yaw + glm::clamp(wrapAngle(faceAngle(d) - p->yaw), -dt * 6.0f, dt * 6.0f));
    }
    p->ctl.gait += step / 0.42f * kTau;
    p->ctl.stride = approach(p->ctl.stride, step > 1e-4f ? 1.0f : 0.0f, dt * 4.0f);
    p->ctl.run = approach(p->ctl.run, speed > 1.5f ? 1.0f : 0.0f, dt * 3.0f);
    if (speed > 1.5f && w.fx.rnd() < dt * 10.0f) w.fx.dust(p->pos, 1, 0.1f, 0.4f);
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.6f;
        shotHeight = 0.5f;
        if (faceOffR(w)) next();
        break;
      case 1: {  // he points; a roar from beyond the board
        float u = easeInOut(seg(pt, 0.0f, 0.5f));
        a.ctl.armR = vec3(1.4f, 0.0f, -0.3f) * u;
        a.ctl.itemPitch = 1.2f * u;
        if (enter()) {
          vec3 side(-dir.z, 0, dir.x);
          // it comes from the nearer side of the board
          vec3 s1 = V + side * 3.0f, s2 = V - side * 3.0f;
          vec3 c1 = clampBoard(s1, 4.25f), c2 = clampBoard(s2, 4.25f);
          from = glm::length(c1 - V) < glm::length(c2 - V) ? c1 : c2;
          if (glm::length(from - V) < 1.2f) from = glm::length(c1 - V) > glm::length(c2 - V) ? c1 : c2;
          lion = w.addPuppet(rm::R_LION, 0, 3);
          if (Puppet* p = L_(w)) {
            p->pos = from;
            p->yaw = faceAngle(V - from);
            p->dissolve = 1.0f;
          }
          w.sound(SFX_LION, 1.0f, from);
          w.sound(SFX_CROWD, 0.6f, a.pos, 0.95f);
        }
        if (Puppet* p = L_(w)) {
          p->dissolve = std::max(0.0f, p->dissolve - dt * 2.5f);
          p->ctl.jaw = 0.8f * bump(pt, 0.0f, 0.9f);
          p->ctl.headPitch = -0.3f * bump(pt, 0.0f, 0.9f);
        }
        victimGuard(v, seg(pt, 0.4f, 1.0f));
        brace(w, 1.0f);
        if (isHuman(v)) v.yawTarget = faceAngle(from - V);
        if (pt > 1.0f) next();
        break;
      }
      case 2: {  // it bounds across the board
        lionTo(w, V + flatDir(V, from) * 0.75f, 2.6f, dt);
        if (Puppet* p = L_(w)) {
          p->ctl.jaw = 0.2f;
          if (glm::length(flat(p->pos - V)) < 0.8f) {
            leap0 = p->pos;
            leapTo = V + flatDir(V, from) * 0.12f;
            w.sound(SFX_LION, 0.9f, p->pos, 1.1f);
            next();
          }
        }
        if (isHuman(v)) {
          v.yawTarget = faceAngle(from - V);
          victimGuard(v, 1.0f);
        }
        brace(w, 1.0f);
        break;
      }
      case 3: {  // the pounce
        float u = seg(pt, 0.0f, 0.45f);
        if (Puppet* p = L_(w)) {
          p->pos = glm::mix(leap0, leapTo, u);
          p->pos.y = 4.0f * u * (1.0f - u) * 0.32f;
          p->ctl.rear = bump(u, 0.0f, 0.7f);
          p->ctl.tuck = bump(u, 0.3f, 1.0f);
          p->ctl.jaw = 0.9f;
          p->ctl.stride = 0;
        }
        if (!hit && u > 0.6f) {
          hit = true;
          vec3 d = flatDir(from, V);
          vec3 c = chestOf(v);
          wound(w, c, d + vec3(0, 0.4f, 0), isTower(v) ? 0.0f : 1.1f, true);
          if (isHuman(v)) kill(w, 1.0f, d * 0.9f, 0.08f);
          else knockFlying(w, d * 1.2f, 0.0f);
          w.shake(0.35f);
          w.slowmo(0.35f, 0.5f);
        }
        if (hit) victimDown(w, dt);
        if (u >= 1.0f) next();
        break;
      }
      case 4: {  // the mauling
        if (Puppet* p = L_(w)) {
          vec3 body = V + flatDir(from, V) * 0.45f;
          if (Puppet* cp = w.puppet(fall.corpse)) body = flat(cp->pos) + flatDir(from, V) * 0.4f;
          vec3 want = body - flatDir(from, V) * 0.32f;
          p->pos = glm::mix(p->pos, want, std::min(1.0f, dt * 4.0f));
          p->pos.y = 0;
          p->ctl.rear = approach(p->ctl.rear, 0.0f, dt * 3.0f);
          p->ctl.tuck = approach(p->ctl.tuck, 0.0f, dt * 4.0f);
          p->ctl.crouch = 0.4f;
          p->ctl.headPitch = 0.5f + 0.3f * std::sin(pt * 13.0f);
          p->ctl.headYaw = 0.25f * std::sin(pt * 7.0f);
          p->ctl.jaw = 0.5f + 0.5f * std::sin(pt * 13.0f + 1.0f);
          if (std::fmod(pt, 0.4f) < dt && !isTower(v)) {
            vec3 m = puppetPoint(*p, rm::LN_JAW, rm::lionMouth());
            bloodSpray(w, m, vec3(w.fx.rnd(-1, 1), 1.0f, w.fx.rnd(-1, 1)), 18, 1.8f);
            w.sound(SFX_BLOOD, 0.4f, m, w.fx.rnd(0.9f, 1.2f));
          }
          if (std::fmod(pt, 0.6f) < dt) w.sound(SFX_LION, 0.4f, p->pos, 1.4f);
        }
        victimDown(w, dt);
        if (pt > 1.5f) next();
        break;
      }
      case 5: {  // a roar to the crowd
        if (enter()) {
          w.sound(SFX_LION, 1.0f, V, 0.9f);
          w.sound(SFX_CROWD, 0.8f, a.pos, 1.0f);
          w.shake(0.15f);
        }
        if (Puppet* p = L_(w)) {
          p->ctl.crouch = approach(p->ctl.crouch, 0.0f, dt * 2.0f);
          p->ctl.headPitch = -0.6f * bump(pt, 0.0f, 1.0f);
          p->ctl.jaw = bump(pt, 0.0f, 1.0f);
          p->ctl.headYaw = 0;
        }
        a.ctl.armR = glm::mix(a.ctl.armR, vec3(0), std::min(1.0f, dt * 3.0f));
        a.ctl.itemPitch = approach(a.ctl.itemPitch, 0.0f, dt * 2.0f);
        victimDown(w, dt);
        if (pt > 1.1f) {
          exitTo = from;
          next();
        }
        break;
      }
      case 6: {  // and away it goes
        lionTo(w, exitTo, 1.6f, dt);
        if (Puppet* p = L_(w)) {
          p->ctl.jaw = 0;
          if (glm::length(flat(p->pos - exitTo)) < 0.6f) p->dissolve = std::min(1.0f, p->dissolve + dt * 2.0f);
          if (p->dissolve >= 1.0f) p->alive = false;
        }
        bool gone = !L_(w);
        if (victimDown(w, dt) && (gone || pt > 2.5f)) {
          if (Puppet* p = L_(w)) p->alive = false;
          next();
        }
        break;
      }
      case 7:
        if (moveInR(w, dt)) next();
        break;
      case 8:
        return finishR(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Battles off: the victim falls where it stands while the attacker moves in.
// ---------------------------------------------------------------------------
class RQuickCapture : public RBattle {
 public:
  using RBattle::RBattle;

 protected:
  bool step(World& w, float dt) override {
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (enter()) setup(w);
        v.tremble = 1.0f;
        if (pt > 0.15f) {
          if (!isTower(v)) {
            bloodSpray(w, chestOf(v), dir + vec3(0, 0.3f, 0), 25, 2.0f);
            w.sound(SFX_STAB, 0.6f, v.pos);
          }
          fall.life = 0.25f;
          fall.cry = false;
          kill(w, 1.0f, dir * 0.4f, 0.1f);
          next();
        }
        break;
      case 1: {
        bool down = victimDown(w, dt);
        bool in = moveInR(w, dt, 1.4f);
        if (down && in) next();
        break;
      }
      case 2:
        return finishR(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Checkmate: the emperor is struck by unseen daggers (the conspirators' - et tu?),
// staggers, drops to his knees and falls.
// ---------------------------------------------------------------------------
class RCheckmate : public Staged {
 public:
  RCheckmate(int king, int winner) : king_(king), winner_(winner) {}

 protected:
  int king_, winner_;
  Fall fall;
  int stabs = 0;
  bool step(World& w, float dt) override {
    Actor& k = w.actors[king_];
    switch (phase) {
      case 0: {
        if (enter()) {
          w.sound(SFX_MATE, 1.0f, k.pos);
          w.sound(SFX_CROWD, 0.5f, k.pos, 0.8f);
          if (w.cinematic) {
            CamPose p;
            p.target = k.pos + vec3(0, 0.6f, 0) + k.forward() * 0.3f;
            vec3 player = glm::normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4f));
            vec3 back = glm::normalize(k.forward() * 1.0f + player * 0.45f + vec3(1e-4f));
            p.pos = p.target + back * 3.1f + vec3(0, 1.7f, 0);  // high enough to see over his own men
            p.fov = 32;
            p.focus = glm::length(p.pos - p.target);
            p.aperture = 7.0f;
            w.cam.setShot(p, 0.9f);
          }
        }
        k.idleWeight = 0;
        // three blows, from behind and from the side
        static const float at[3] = {0.5f, 1.0f, 1.4f};
        if (stabs < 3 && pt > at[stabs]) {
          vec3 c = vec3(k.model() * md::boneMatrix(k.pose, rm::RH_CHEST) *
                        glm::vec4(rm::chestPoint(k.kind) + vec3(stabs == 1 ? 0.06f : -0.05f, -0.03f * stabs, -0.09f), 1.0f));
          vec3 d = -k.forward() + vec3(stabs == 1 ? 0.6f : -0.4f, 0.2f, 0);
          wound(w, c, d, 0.9f);
          w.sound(SFX_GRUNT, 0.7f, c, 0.75f - 0.05f * stabs);
          k.ctl.lean = 0.2f + 0.1f * stabs;
          k.ctl.twist = stabs == 1 ? -0.3f : 0.25f;
          ++stabs;
        }
        k.ctl.lean = approach(k.ctl.lean, 0.1f, dt * 0.5f);
        k.ctl.armL = glm::mix(k.ctl.armL, vec3(1.3f, -0.3f, 1.4f), std::min(1.0f, dt * 3.0f));  // clutching his chest
        if (pt > 1.0f) k.ctl.itemR = 0;  // the sceptre falls from his hand
        k.ctl.crouch = 0.6f * seg(pt, 1.4f, 2.0f);
        k.tremble = 0.3f;
        if (pt > 2.1f) {
          fall.life = 6.0f;
          fall.bleed = 1.4f;
          fall.start(w, k, -1.0f, k.forward() * 0.1f, 0.2f);
          w.sound(SFX_RUMBLE, 0.6f, k.pos, 0.8f);
          next();
        }
        break;
      }
      case 1:
        if (fall.update(w, dt)) {
          w.slowmo(0.5f, 0.6f);
          next();
        }
        break;
      case 2:  // the winners celebrate
        for (Actor& a : w.actors)
          if (a.alive && a.color == winner_) a.selected = pt > (a.phase - std::floor(a.phase)) * 0.4f && pt < 2.4f;
        if (pt > 0.2f && pt - w.lastDt <= 0.2f) w.sound(SFX_CORNU, 0.7f, vec3(0));
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

const char* const kRomanStyleNames[meshgen::K_COUNT][kBattleStyles] = {
    {"Pilum", "Shield and gladius", "Vault"},
    {"Run down", "Scythed wheels", "Charioteer's leap"},
    {"Duel", "Leaping strike", "Thrown knife"},
    {"Battering ram", "Sally", "Scorpion"},
    {"Hidden dagger", "Poisoned cup", "Whirling spear"},
    {"Thumbs down", "Gladius duel", "The lion"},
};

}  // namespace

const char* romanBattleStyleName(int kind, int style) {
  if (kind < 0 || kind >= meshgen::K_COUNT || style < 0 || style >= kBattleStyles) return "";
  return kRomanStyleNames[kind][style];
}

std::unique_ptr<Anim> makeRomanBattle(World& w, int kind, int style, const MovePlan& plan) {
  // a siege tower cannot drink from a cup
  if (kind == K_QUEEN && style == 1 && plan.victim >= 0 && w.actors[plan.victim].kind == K_ROOK) style = 0;
  switch (kind * kBattleStyles + style) {
    case K_PAWN * kBattleStyles + 0: return std::make_unique<PawnPilum>(plan);
    case K_PAWN * kBattleStyles + 1: return std::make_unique<PawnShield>(plan);
    case K_PAWN * kBattleStyles + 2: return std::make_unique<PawnVault>(plan);
    case K_KNIGHT * kBattleStyles + 0: return std::make_unique<ChariotRunDown>(plan);
    case K_KNIGHT * kBattleStyles + 1: return std::make_unique<ChariotScythes>(plan);
    case K_KNIGHT * kBattleStyles + 2: return std::make_unique<ChariotLeap>(plan);
    case K_BISHOP * kBattleStyles + 0: return std::make_unique<CenturionDuel>(plan);
    case K_BISHOP * kBattleStyles + 1: return std::make_unique<CenturionLeap>(plan);
    case K_BISHOP * kBattleStyles + 2: return std::make_unique<CenturionKnife>(plan);
    case K_ROOK * kBattleStyles + 0: return std::make_unique<TowerRam>(plan);
    case K_ROOK * kBattleStyles + 1: return std::make_unique<TowerSally>(plan);
    case K_ROOK * kBattleStyles + 2: return std::make_unique<TowerScorpion>(plan);
    case K_QUEEN * kBattleStyles + 0: return std::make_unique<EmpressDagger>(plan);
    case K_QUEEN * kBattleStyles + 1: return std::make_unique<EmpressCup>(plan);
    case K_QUEEN * kBattleStyles + 2: return std::make_unique<EmpressSpear>(plan);
    case K_KING * kBattleStyles + 0: return std::make_unique<EmperorThumbsDown>(plan);
    case K_KING * kBattleStyles + 1: return std::make_unique<EmperorDuel>(plan);
    default: return std::make_unique<EmperorLion>(plan);
  }
}

std::unique_ptr<Anim> makeRomanQuiet(const MovePlan& plan) { return std::make_unique<RQuietMove>(plan); }
std::unique_ptr<Anim> makeRomanQuickCapture(const MovePlan& plan) { return std::make_unique<RQuickCapture>(plan); }
std::unique_ptr<Anim> makeRomanCheckmate(int kingId, int winnerColor) {
  return std::make_unique<RCheckmate>(kingId, winnerColor);
}
