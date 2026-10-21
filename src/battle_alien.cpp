// Choreography for the alien army. Little green men waddle, the saucer flies,
// the psychic, the queen and the overlord float, the tripod walks on its three
// legs. Captures:
//   Pawn    ray gun (pew, pew - two misses, then a hit), goo grenade (the victim
//           melts), tongue (shoots out, swallows it whole, burps up the pieces)
//   Knight  abduction (tractor beam: up it goes, spinning, and out it comes in
//           pieces), crop circle (lasers carve a ring, the victim falls in two),
//           cow drop
//   Bishop  mind blast (the victim's head swells and pops), hypnosis (it dances
//           in a daze, then blasts off like a rocket), telekinetic crush
//   Rook    heat ray, stomp (misses once), tentacle toss
//   Queen   acid spit, brood (an egg hatches a swarm), tail strike
//   King    shrink ray (and a squash), the mothership, disintegrator (it
//           sputters, he taps it, it fires far too hard)
#include <algorithm>
#include <cmath>

#include "battle.h"
#include "battle_kit.h"

namespace {
using namespace bkit;
namespace al = alien;

const float kTau = 6.2831853f;

float approach(float x, float target, float rate) {
  return x < target ? std::min(target, x + rate) : std::max(target, x - rate);
}
// world position of a point that rides on a bone (rest-pose model coordinates)
vec3 bonePoint(const Actor& a, int bone, const vec3& rest) {
  return vec3(a.model() * medieval::boneMatrix(a.pose, bone) * glm::vec4(rest, 1.0f));
}
// the army's light colour (cyan for white, magenta for black)
vec3 armyLight(int color) { return color == 0 ? vec3(0.25f, 1.0f, 0.92f) : vec3(1.0f, 0.18f, 0.62f); }
// the colour of its goo
vec3 armyGoo(int color) { return color == 0 ? vec3(0.35f, 0.9f, 0.08f) : vec3(0.55f, 0.12f, 0.8f); }

bool floats(int kind) { return kind == K_KNIGHT || kind == K_BISHOP || kind == K_QUEEN || kind == K_KING; }

// ---------------------------------------------------------------------------
// AWalker: moves an alien to 'to' - waddling, hovering or striding on three legs.
// ---------------------------------------------------------------------------
struct AWalker {
  enum Mode { WADDLE, HOVER, TRIPOD };
  int actor = -1;
  vec3 from{0}, to{0};
  Mode mode = HOVER;
  float speed = 0.8f, accel = 1.6f, cycle = 0.3f, lift = 0.04f;
  float s = 0, v = 0, len = 0, turnWait = 0;
  int lastStep = 0;
  bool begun = false, face = true, soundOn = true;

  AWalker() {}
  AWalker(int a, const vec3& dest, Mode m, float spd) : actor(a), to(flat(dest)), mode(m), speed(spd) {}

  static Mode modeFor(int kind) { return kind == K_PAWN ? WADDLE : (kind == K_ROOK ? TRIPOD : HOVER); }

  void begin(World& w) {
    Actor& a = w.actors[actor];
    from = flat(a.pos);
    len = glm::length(to - from);
    cycle = al::strideCycle(a.kind);
    accel = speed * (mode == HOVER ? 2.4f : 2.0f);
    if (face && len > 0.05f) {
      a.yawTarget = faceAngle(to - from);
      turnWait = std::min(0.45f, std::fabs(wrapAngle(a.yawTarget - a.yaw)) * 0.25f);
    }
    lastStep = (int)std::floor(a.ctl.gait / kPI);
    begun = true;
    if (mode == HOVER && soundOn && len > 0.05f) w.sound(SFX_WHOOSH, 0.35f, a.pos, a.kind == K_KING ? 0.6f : 0.85f);
  }

  void footstep(World& w, Actor& a) {
    if (!soundOn) return;
    vec3 p = flat(a.pos);
    if (mode == WADDLE) {
      w.sound(SFX_STEP, 0.4f, p, w.fx.rnd(1.35f, 1.6f));
    } else if (mode == TRIPOD) {
      w.sound(SFX_STOMP, 0.45f, p, w.fx.rnd(1.3f, 1.5f));
      w.sound(SFX_CLANG, 0.12f, p, w.fx.rnd(1.6f, 1.9f));
      w.fx.dust(p + a.forward() * 0.1f, 3, 0.25f, 0.5f);
      w.shake(0.05f);
    }
  }

  bool update(World& w, float dt) {
    if (!begun) begin(w);
    Actor& a = w.actors[actor];
    if (turnWait > 0) {
      turnWait -= dt;
      if (mode != HOVER) {  // shuffle round on the spot
        a.ctl.gait += dt * 5.0f;
        a.ctl.stride = approach(a.ctl.stride, 0.3f, dt * 2.0f);
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
    if (mode == HOVER) {
      // rise a little while moving, tip into the direction of travel
      float u = len > 1e-4f ? s / len : 1.0f;
      a.ctl.hover = lift * std::sin(kPI * u);
      a.ctl.lean = approach(a.ctl.lean, 0.12f * glm::clamp(v / std::max(speed, 0.1f), 0.0f, 1.0f) * (u < 0.85f ? 1.0f : -0.5f),
                            dt * 0.8f);
      if (a.kind == K_KNIGHT) a.ctl.spin += dt * (2.0f + 10.0f * v);
    } else {
      a.ctl.gait += v * dt / cycle * kTau;
      a.ctl.stride = glm::clamp(v / std::max(0.1f, speed * 0.6f), 0.0f, 1.0f);
      int step = (int)std::floor(a.ctl.gait / (mode == TRIPOD ? kTau / 3.0f : kPI));
      if (step != lastStep) {
        lastStep = step;
        footstep(w, a);
      }
    }
    if (s >= len - 1e-4f) {
      a.ctl.stride = approach(a.ctl.stride, 0.0f, dt * 4.0f);
      a.ctl.hover = approach(a.ctl.hover, 0.0f, dt * 0.5f);
      a.ctl.lean = approach(a.ctl.lean, 0.0f, dt * 1.0f);
      return a.ctl.stride <= 0.001f && std::fabs(a.ctl.hover) < 1e-3f && std::fabs(a.ctl.lean) < 1e-3f;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Quiet moves (and castling)
// ---------------------------------------------------------------------------
class AQuietMove : public Staged {
 public:
  AQuietMove(const MovePlan& p) : plan(p) {}

 protected:
  MovePlan plan;
  AWalker walker, rookWalker;
  vec3 A{0}, B{0};
  float dur = 1, kingDelay = 0;

  // the saucer lifts off, zooms across in an arc (over anything in the way) and settles
  bool saucerFlight(World& w, Actor& a, float dt) {
    switch (phase) {
      case 0:
        if (enter()) {
          A = flat(a.pos);
          B = squareCenter(plan.move.to);
          a.yawTarget = faceAngle(B - A);
          w.sound(SFX_CHARGE, 0.35f, A, 1.6f);
        }
        a.ctl.hover = 0.12f * easeInOut(pt / 0.35f);
        a.ctl.spin += dt * 10.0f;
        if (pt > 0.35f) {
          dur = 0.55f + 0.12f * glm::length(B - A);
          w.sound(SFX_WHOOSH, 0.6f, a.pos, 1.3f);
          next();
        }
        break;
      case 1: {
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        vec3 p = glm::mix(A, B, easeInOut(u));
        a.pos = vec3(p.x, 0, p.z);
        a.ctl.hover = 0.12f + 0.35f * std::sin(kPI * u);
        a.ctl.lean = 0.3f * std::sin(kPI * glm::clamp(u * 1.4f, 0.0f, 1.0f)) - 0.15f * std::max(0.0f, u - 0.7f) / 0.3f;
        a.ctl.twist = 0.15f * std::sin(kTau * u);
        a.ctl.spin += dt * 14.0f;
        if (w.fx.rnd() < 0.6f) stream(w, a.pos + vec3(0, al::hoverHeight(a.kind) + a.ctl.hover - 0.07f, 0),
                                      a.pos - vec3(0, 0.1f, 0), armyLight(a.color), 1, 2.0f);
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // settle with a wobble
        float u = glm::clamp(pt / 0.45f, 0.0f, 1.0f);
        a.ctl.hover = 0.12f * (1.0f - easeInOut(u));
        a.ctl.lean = -0.08f * std::sin(u * kPI * 2.0f) * (1.0f - u);
        a.ctl.twist = 0.06f * std::sin(u * kPI * 3.0f) * (1.0f - u);
        if (u >= 1.0f) {
          a.ctl.hover = a.ctl.lean = a.ctl.twist = 0;
          a.yawTarget = a.baseYaw;
          w.sound(SFX_CHIME, 0.2f, a.pos, 2.0f);
          return true;
        }
        break;
      }
    }
    return false;
  }

  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    if (a.kind == K_KNIGHT) return saucerFlight(w, a, dt);
    switch (phase) {
      case 0:
        if (enter()) {
          AWalker::Mode m = AWalker::modeFor(a.kind);
          float spd = m == AWalker::WADDLE ? 0.7f : (m == AWalker::TRIPOD ? 0.75f : (a.kind == K_KING ? 0.9f : 1.1f));
          float dist = glm::length(flat(squareCenter(plan.move.to) - a.pos));
          if (m == AWalker::HOVER) spd = std::max(spd, dist / 1.5f);  // long glides go faster
          if (m == AWalker::TRIPOD) spd = glm::clamp(dist / 3.0f, spd, 1.3f);  // and long marches stride out
          walker = AWalker(plan.mover, squareCenter(plan.move.to), m, spd);
          if (m == AWalker::HOVER) {
            walker.face = a.kind == K_QUEEN;
            walker.lift = a.kind == K_KING ? 0.06f : 0.05f;
          }
          A = flat(a.pos);
          B = squareCenter(plan.move.to);
          if (plan.rook >= 0) {
            rookWalker = AWalker(plan.rook, squareCenter(plan.rookTo), AWalker::TRIPOD, 1.1f);
            // the overlord waits until the tripod is about to clear his square
            float clear = glm::length(flat(B - w.actors[plan.rook].pos)) + 0.65f;
            kingDelay = std::max(0.0f, 0.65f + clear / 1.1f - 1.36f);
          }
        }
        {
          bool d1;
          if (plan.rook >= 0) {
            // castling: the tripod strides off first, past the overlord's square, and he
            // swings out in an arc behind it
            float u = glm::clamp((pt - kingDelay) / 1.6f, 0.0f, 1.0f), e = easeInOut(u);
            vec3 toCenter = vec3(0, 0, a.color == 0 ? -1.0f : 1.0f);
            vec3 W = (A + B) * 0.5f + toCenter * 1.6f;  // well clear of the tripod's legs
            vec3 p = (1 - e) * (1 - e) * A + 2 * (1 - e) * e * W + e * e * B;
            a.pos = vec3(p.x, 0, p.z);
            a.ctl.hover = 0.08f * std::sin(kPI * u);
            if (pt >= kingDelay && pt - dt < kingDelay + 1e-6f) w.sound(SFX_WHOOSH, 0.4f, a.pos, 0.6f);
            d1 = u >= 1.0f;
          } else {
            d1 = walker.update(w, dt);
          }
          bool d2 = true;
          if (plan.rook >= 0) d2 = rookWalker.update(w, dt);
          if (a.kind == K_QUEEN) a.ctl.tail += dt * 2.0f;
          if (d1 && d2) next();
        }
        break;
      case 1:
        if (enter()) {
          a.yawTarget = a.baseYaw;
          a.ctl.hover = 0;
          if (plan.rook >= 0) w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw;
        }
        if (pt > 0.25f) return true;
        break;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Shared prelude and epilogue for alien battles
// ---------------------------------------------------------------------------
class ABattle : public BattleBase {
 public:
  // the aliens are small: the camera comes in closer and lower
  ABattle(const MovePlan& p) : BattleBase(p) {
    shotExtra = -0.7f;
    shotHeight = 0.42f;
  }

 protected:
  AWalker walker;

  bool moveInA(World& w, float dt, float speed = 1.0f) {
    Actor& a = w.actors[plan.mover];
    if (enter()) {
      AWalker::Mode m = AWalker::modeFor(a.kind);
      walker = AWalker(plan.mover, squareCenter(plan.move.to), m, speed);
      if (m == AWalker::HOVER) walker.face = false;
    }
    return walker.update(w, dt);
  }
  bool finishA(World& w) {
    Actor& a = w.actors[plan.mover];
    bool done = finish(w);
    if (done) {
      a.pos = squareCenter(plan.move.to);
      a.resetControls();
    }
    return done;
  }
};

// ---------------------------------------------------------------------------
// Fast capture (battle animations off): a quick zap
// ---------------------------------------------------------------------------
class AQuickCapture : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  bool step(World& w, float dt) override {
    Actor& v = w.actors[plan.victim];
    Actor& a = w.actors[plan.mover];
    switch (phase) {
      case 0:
        if (enter()) {
          setup(w);
          w.sound(SFX_ZAP, 0.6f, v.pos, 1.5f);
        }
        v.tremble = 1.0f;
        v.glow = glm::vec4(armyLight(a.color), 1.5f * pt / 0.2f);
        if (pt > 0.2f) {
          w.fx.magicBurst(victimCenter(w), armyLight(a.color) * 3.0f, 30, 1.5f);
          w.shatter(v, victimCenter(w), dir * 1.5f, 1.4f, 0.8f);
          w.shake(0.2f);
          next();
        }
        break;
      case 1:
        if (moveInA(w, dt, a.kind == K_PAWN ? 1.2f : 1.8f)) next();
        break;
      case 2:
        return finishA(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Checkmate: the overlord's throne sputters and fails; he crashes and cracks.
// ---------------------------------------------------------------------------
class ACheckmate : public Staged {
 public:
  ACheckmate(int king, int winner) : king_(king), winner_(winner) {}

 protected:
  int king_, winner_;
  float fall = 0, vy = 0;
  bool step(World& w, float dt) override {
    Actor& k = w.actors[king_];
    const vec3 red(1.0f, 0.18f, 0.06f);
    switch (phase) {
      case 0: {
        if (enter()) {
          w.sound(SFX_MATE, 1.0f, k.pos);
          w.sound(SFX_CHARGE, 0.5f, k.pos, 0.5f);
          if (w.cinematic) {
            CamPose p;
            p.target = k.pos + vec3(0, 0.7f, 0) + k.forward() * 0.3f;
            vec3 player = glm::normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4f));
            vec3 back = glm::normalize(k.forward() * 1.0f + player * 0.45f + vec3(1e-4f));
            p.pos = p.target + back * 3.2f + vec3(0, 1.0f, 0);
            p.fov = 32;
            p.focus = glm::length(p.pos - p.target);
            p.aperture = 7.0f;
            w.cam.setShot(p, 0.9f);
          }
        }
        // the drive sputters: the throne bobs, sparks fly, smoke pours out
        float u = glm::clamp(pt / 1.8f, 0.0f, 1.0f);
        k.ctl.hover = 0.05f * std::sin(pt * 13.0f) * (1.0f - u) - 0.03f * u;
        k.ctl.lean = 0.08f * std::sin(pt * 7.0f) * u;
        k.ctl.headYaw = 0.5f * std::sin(pt * 4.0f) * u;  // looking about in panic
        k.ctl.perk = -u;
        k.ctl.jaw = 0.6f * u;
        k.tremble = 0.3f + 0.5f * u;
        k.crackColor = red;
        k.crack = easeIn(u) * 0.8f;
        k.glow = glm::vec4(red, 0.5f * u);
        k.idleWeight = 0;
        if (w.fx.rnd() < dt * 12.0f) {
          vec3 s = k.pos + vec3(w.fx.rnd(-0.15f, 0.15f), 0.18f, w.fx.rnd(-0.15f, 0.15f));
          w.fx.sparks(s, vec3(0, -0.5f, 0), 8, 2.0f, 0.8f, vec3(4.0f, 2.5f, 1.0f));
          w.sound(SFX_ZAP, 0.25f, s, w.fx.rnd(1.6f, 2.2f));
        }
        if (w.fx.rnd() < dt * 8.0f) w.fx.smoke(k.pos + vec3(0, 0.15f, 0), 1, 0.1f, 0.4f, 0.8f);
        w.frameLights.push_back({k.pos + vec3(0, 0.6f, 0), 3.0f, red * (3.0f + 10.0f * u)});
        if (u >= 1.0f) {
          vy = 0;
          w.sound(SFX_RUMBLE, 0.7f, k.pos, 1.3f);
          next();
        }
        break;
      }
      case 1: {  // the drive dies: down he drops
        vy -= 9.0f * dt;
        fall += vy * dt;
        k.ctl.hover = -0.03f + fall;
        float floor = -al::hoverHeight(K_KING) + 0.02f;
        if (k.ctl.hover <= floor) {
          k.ctl.hover = floor;
          vec3 top = k.pos + vec3(0, 0.6f, 0) + k.forward() * 0.1f;
          w.shatter(k, top, k.forward() * 1.5f, 1.3f, 0.8f, 6.0f);
          w.fx.dust(k.pos, 30, 0.8f, 2.0f);
          w.fx.fireball(k.pos + vec3(0, 0.2f, 0), 0.6f);
          w.groundRing(k.pos, red * 1.5f, 3.0f, 0.9f, 0.1f);
          w.light(k.pos + vec3(0, 0.5f, 0), red * 20.0f, 4.0f, 0.5f);
          w.sound(SFX_EXPLODE, 0.9f, k.pos, 1.1f);
          w.shake(0.5f);
          w.slowmo(0.35f, 0.6f);
          next();
        }
        break;
      }
      case 2:  // winners celebrate
        for (Actor& a : w.actors)
          if (a.alive && a.color == winner_) a.selected = pt > (a.phase - std::floor(a.phase)) * 0.4f && pt < 2.4f;
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

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------
// where a piece's weapon fires from (ray gun, saucer belly, brain, eye, sceptre)
vec3 muzzleOf(const Actor& a) {
  switch (a.kind) {
    case K_PAWN: return bonePoint(a, al::AH_ITEMR, al::muzzle(K_PAWN));
    case K_KNIGHT: return bonePoint(a, al::UB_HATCH, al::muzzle(K_KNIGHT));
    case K_BISHOP: return bonePoint(a, al::AH_HEAD, al::muzzle(K_BISHOP));
    case K_ROOK: return bonePoint(a, al::TB_HOOD, al::muzzle(K_ROOK));
    default: return bonePoint(a, al::AH_ITEMR, al::muzzle(a.kind));
  }
}
vec3 headOf(const Actor& a) {
  int bone = a.kind == K_KNIGHT ? (int)al::UB_HEAD : (a.kind == K_ROOK ? (int)al::TB_BODY : (int)al::AH_HEAD);
  return bonePoint(a, bone, al::headPoint(a.kind));
}

// a ray-gun bolt flying from a to b
struct Bolt {
  vec3 a{0}, b{0}, col{1};
  float t = 0, dur = 0.18f;
  bool active = false;
  void fire(const vec3& from, const vec3& to, const vec3& c) {
    a = from;
    b = to;
    col = c;
    t = 0;
    dur = std::max(0.08f, glm::length(to - from) / 9.0f);
    active = true;
  }
  // true on the frame it arrives
  bool update(World& w, float dt) {
    if (!active) return false;
    t += dt;
    float u = std::min(1.0f, t / dur);
    vec3 p = glm::mix(a, b, u), tail = glm::mix(a, b, std::max(0.0f, u - 0.25f));
    w.fx.bolt(tail, p, col * 5.0f, 0.004f, 3);
    w.frameLights.push_back({p, 1.6f, col * 8.0f});
    if (u >= 1.0f) {
      active = false;
      return true;
    }
    return false;
  }
};

// a blob lobbed in an arc (goo, acid): a puppet that wobbles as it flies
struct Lob {
  int puppet = -1;
  vec3 a{0}, b{0};
  float h = 0.5f, t = 0, dur = 0.6f, spin = 0;
  bool active = false;
  void throwAt(int p, const vec3& from, const vec3& to, float height, float duration) {
    puppet = p;
    a = from;
    b = to;
    h = height;
    dur = duration;
    t = 0;
    active = true;
  }
  bool update(World& w, float dt) {
    if (!active) return false;
    t += dt;
    float u = std::min(1.0f, t / dur);
    if (Puppet* p = w.puppet(puppet)) {
      p->pos = glm::mix(a, b, u) + vec3(0, 4.0f * h * u * (1.0f - u), 0) - vec3(0, 0.05f, 0);
      p->yaw += dt * 9.0f;
      p->tilt = glm::angleAxis(0.4f * std::sin(t * 17.0f), vec3(1, 0, 0));
      if (w.fx.rnd() < 0.5f) {
        Particle q;
        q.pos = p->pos + vec3(0, 0.05f, 0);
        q.vel = vec3(0, -0.3f, 0);
        q.size = w.fx.rnd(0.008f, 0.016f);
        q.color = armyGoo(p->color) * 0.5f;
        q.colorEnd = q.color;
        q.opacity = 0.9f;
        q.maxLife = 0.5f;
        q.type = PT_SPARK;
        q.gravity = 6.0f;
        q.stretch = 0.01f;
        w.fx.add(q);
      }
    }
    if (u >= 1.0f) {
      active = false;
      return true;
    }
    return false;
  }
};

// a continuous beam (heat ray, disintegrator, lasers)
void beamLine(World& w, const vec3& a, const vec3& b, const vec3& col, float jitter, float power) {
  w.fx.bolt(a, b, col * (3.0f + 3.0f * power), jitter, 7);
  w.fx.bolt(a, b, col * (1.5f + power), jitter * 2.5f, 5);
  w.frameLights.push_back({b + vec3(0, 0.15f, 0), 2.5f, col * (12.0f * power)});
  w.frameLights.push_back({glm::mix(a, b, 0.4f), 2.0f, col * (5.0f * power)});
}

// a splash of goo
void gooSplash(World& w, const vec3& at, int color, int n) {
  vec3 g = armyGoo(color);
  for (int i = 0; i < n; ++i) {
    Particle p;
    vec3 d = glm::normalize(w.fx.rndDir() + vec3(0, 0.8f, 0));
    p.pos = at + d * 0.03f;
    p.vel = d * w.fx.rnd(0.8f, 2.4f);
    p.size = w.fx.rnd(0.01f, 0.03f);
    p.color = g * w.fx.rnd(0.35f, 0.6f);
    p.colorEnd = p.color * 0.8f;
    p.opacity = 0.95f;
    p.maxLife = w.fx.rnd(0.5f, 1.0f);
    p.type = PT_SPARK;
    p.gravity = 9.0f;
    p.stretch = 0.012f;
    p.bounce = true;
    p.fadeIn = 0.0f;
    w.fx.add(p);
  }
  w.fx.flash(at, 0.4f, g * 2.0f, 0.12f);
}

// the tractor beam: motes streaming up a column of light, a pool of light below
void tractorBeam(World& w, const vec3& top, const vec3& bottom, float radius, const vec3& col, float dt, float power) {
  float n = 90.0f * dt * power;
  int count = (int)n + (w.fx.rnd() < n - std::floor(n) ? 1 : 0);
  for (int i = 0; i < count; ++i) {
    Particle p;
    float a = w.fx.rnd(0, kTau), r = radius * std::sqrt(w.fx.rnd());
    float h = w.fx.rnd();
    p.pos = glm::mix(bottom, top, h) + vec3(std::cos(a) * r * (0.6f + 0.4f * (1.0f - h)), 0, std::sin(a) * r);
    p.vel = vec3(0, w.fx.rnd(0.6f, 1.4f), 0);
    p.size = w.fx.rnd(0.008f, 0.02f);
    p.color = col * w.fx.rnd(1.0f, 2.5f);
    p.colorEnd = col * 0.3f;
    p.opacity = 0;
    p.maxLife = w.fx.rnd(0.4f, 0.8f);
    p.type = PT_GLOW;
    p.drag = 0;
    w.fx.add(p);
  }
  // soft glowing haze filling the column
  if (w.fx.rnd() < dt * 30.0f * power) {
    Particle g;
    g.pos = glm::mix(bottom, top, w.fx.rnd());
    g.vel = vec3(0, 0.3f, 0);
    g.size = radius * w.fx.rnd(0.9f, 1.4f);
    g.color = col * 0.18f * power;
    g.colorEnd = col * 0.02f;
    g.opacity = 0;
    g.maxLife = 0.5f;
    g.type = PT_GLOW;
    g.fadeIn = 0.15f;
    w.fx.add(g);
  }
  w.frameLights.push_back({bottom + vec3(0, 0.25f, 0), 2.2f, col * 7.0f * power});
}

// psychic rings rolling from one point to another
void psychicRings(World& w, const vec3& from, const vec3& to, const vec3& col, float dt) {
  if (w.fx.rnd() < dt * 7.0f) {
    for (int i = 1; i <= 4; ++i) w.fx.ring(glm::mix(from, to, i / 5.0f), 0.018f + 0.008f * i, col * 1.4f, 0.22f);
  }
  if (w.fx.rnd() < dt * 20.0f) stream(w, from, to, col, 1, 1.6f);
}

// a victim melting into a puddle (u: 0..1)
void melt(World& w, Actor& v, float u, const vec3& goo, float dt) {
  v.idleWeight = 0;
  v.poseSquash = 1.0f - 0.65f * easeIn(u);
  v.poseBend = vec2(0.12f * std::sin(u * 17.0f), 0.1f * std::sin(u * 13.0f)) * u;
  v.glow = glm::vec4(goo, 0.4f + 0.8f * u);
  v.dissolveColor = goo * 1.3f;
  v.dissolve = glm::clamp((u - 0.55f) / 0.45f, 0.0f, 1.0f);
  if (w.fx.rnd() < dt * 14.0f) {
    vec3 p = v.pos + vec3(w.fx.rnd(-0.12f, 0.12f), 0.02f, w.fx.rnd(-0.12f, 0.12f));
    w.fx.smoke(p, 1, 0.05f, 0.5f, 0.0f);
    gooSplash(w, p, v.color == 0 ? 1 : 0, 1);
  }
}

// a saucer hop: from where it is to 'to', rising to 'height' above its resting hover
struct SaucerHop {
  vec3 from{0}, to{0};
  float h0 = 0, h1 = 0, peak = 0.3f, t = 0, dur = 1;
  bool begun = false;
  void begin(Actor& a, const vec3& dest, float endHover, float arc, float duration) {
    from = flat(a.pos);
    to = flat(dest);
    h0 = a.ctl.hover;
    h1 = endHover;
    peak = arc;
    dur = duration;
    t = 0;
    begun = true;
  }
  bool update(World& w, Actor& a, float dt) {
    t += dt;
    float u = glm::clamp(t / dur, 0.0f, 1.0f), e = easeInOut(u);
    vec3 p = glm::mix(from, to, e);
    a.pos = vec3(p.x, 0, p.z);
    a.ctl.hover = glm::mix(h0, h1, e) + peak * std::sin(kPI * u);
    a.ctl.lean = 0.25f * std::sin(kPI * glm::clamp(u * 1.3f, 0.0f, 1.0f)) * (glm::length(to - from) > 0.1f ? 1.0f : 0.0f);
    a.ctl.spin += dt * 12.0f;
    if (w.fx.rnd() < 0.4f) stream(w, a.pos + vec3(0, al::hoverHeight(K_KNIGHT) + a.ctl.hover - 0.07f, 0),
                                  a.pos + vec3(0, al::hoverHeight(K_KNIGHT) + a.ctl.hover - 0.3f, 0), armyLight(a.color), 1, 2.0f);
    if (u >= 1.0f) {
      a.ctl.lean = 0;
      return true;
    }
    return false;
  }
};

// ===========================================================================
// PAWN - the little green man
// ===========================================================================
// Ray gun: two wild misses (the victim hops out of the way), then a hit that
// disintegrates it. He blows the smoke off the muzzle.
class PawnRayGun : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  Bolt bolt;
  int shot = 0;
  float nextShot = 0;
  bool walking = false;
  vec3 aimAt(World& w, int k) {
    Actor& v = w.actors[plan.victim];
    vec3 c = victimCenter(w);
    vec3 side(-dir.z, 0, dir.x);
    if (k == 0) return flat(c) + side * 0.45f + dir * 0.6f;   // wide to the left, into the board
    if (k == 1) return c + side * -0.32f + vec3(0, v.height() * 0.45f, 0) + dir * 0.9f;  // high and right, over the board
    return c;
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = -0.3f;
        if (faceOff(w)) next();
        break;
      case 1: {  // stroll into range
        if (!walking) {
          walking = true;
          float keep = glm::clamp(L, 1.2f, 2.3f);
          walker = AWalker(plan.mover, V - dir * keep, AWalker::WADDLE, 0.8f);
        }
        if (walker.update(w, dt)) next();
        break;
      }
      case 2: {  // draw a bead on it
        float u = easeInOut(pt / 0.35f);
        a.yawTarget = faceAngle(V - flat(a.pos));
        a.ctl.armR = vec3(1.05f * u, 0.05f * u, -0.7f * u);
        a.ctl.lean = -0.05f * u;
        a.ctl.perk = 0.6f * u;
        if (pt > 0.4f) {
          nextShot = 0.0f;
          next();
        }
        break;
      }
      case 3: {  // pew - pew - PEW
        a.ctl.armR = vec3(1.05f, 0.05f, -0.7f) + vec3(0.25f, 0, 0) * std::exp(-std::max(0.0f, pt - nextShot + 0.6f) * 12.0f);
        if (shot < 3 && pt >= nextShot) {
          vec3 m = muzzleOf(a);
          bolt.fire(m, aimAt(w, shot), armyLight(a.color));
          w.sound(SFX_PEW, 0.9f, m, shot == 2 ? 0.9f : w.fx.rnd(1.0f, 1.15f));
          w.fx.flash(m, 0.12f, armyLight(a.color) * 4.0f, 0.08f);
          shot++;
          nextShot = pt + (shot == 1 ? 0.75f : 0.85f);
        }
        if (bolt.update(w, dt)) {
          vec3 hit = bolt.b;
          if (shot < 3) {  // a miss: sparks where it lands, the victim jumps
            w.fx.sparks(hit, vec3(0, 1, 0), 18, 2.2f, 0.7f, armyLight(a.color) * 3.0f);
            w.fx.ring(flat(hit) + vec3(0, 0.01f, 0), 0.05f, armyLight(a.color) * 1.5f, 0.3f);
            w.sound(SFX_ZAP, 0.4f, hit, 1.4f);
            v.sq.v += 3.0f;
            v.poseSquash = 0.8f;
            if (shot == 2) w.sound(SFX_GIBBER, 0.55f, a.pos, 1.15f);  // grumbling
          } else {
            w.sound(SFX_ZAP, 0.9f, hit, 1.0f);
            w.sound(SFX_CRACKLE, 0.6f, hit, 1.2f);
            w.fx.magicBurst(hit, armyLight(a.color) * 3.0f, 40, 1.6f);
            v.dissolveColor = armyLight(a.color) * 1.4f;
            next();
          }
        }
        // between shots the victim hops about in alarm
        if (shot >= 1) {
          v.tremble = 0.6f;
          v.pos.y = std::max(0.0f, 0.12f * std::sin(pt * 9.0f)) * (shot < 3 ? 1.0f : 0.0f);
        }
        if (shot == 2) a.ctl.armR.z = -0.7f + 0.3f * std::sin(pt * 30.0f) * std::exp(-(pt - nextShot + 0.85f) * 4.0f);
        break;
      }
      case 4: {  // the victim fizzles away into motes
        float u = glm::clamp(pt / 0.9f, 0.0f, 1.0f);
        v.pos.y = approach(v.pos.y, 0.0f, dt);
        v.glow = glm::vec4(armyLight(a.color), 1.5f * (1.0f - u * 0.5f));
        v.dissolve = u;
        v.tremble = 0.8f;
        if (w.fx.rnd() < 0.8f) stream(w, v.pos + vec3(w.fx.rnd(-0.1f, 0.1f), w.fx.rnd(0.0f, v.height()), w.fx.rnd(-0.1f, 0.1f)),
                                      v.pos + vec3(0, v.height() + 0.8f, 0), armyLight(a.color), 2, 1.2f);
        if (u >= 1.0f) {
          v.alive = false;
          v.visible = false;
          w.fx.smoke(V + vec3(0, 0.1f, 0), 6, 0.15f, 0.4f, 0.3f);
          next();
        }
        break;
      }
      case 5: {  // blow the smoke off the muzzle
        float u = std::sin(glm::clamp(pt / 1.0f, 0.0f, 1.0f) * kPI);
        a.ctl.armR = glm::mix(vec3(1.05f, 0.05f, -0.7f), vec3(1.5f, 0.25f, 1.1f), u);
        a.ctl.headPitch = 0.25f * u;
        a.ctl.headYaw = -0.25f * u;
        if (pt > 0.35f && pt - dt <= 0.35f) w.fx.smoke(muzzleOf(a), 3, 0.02f, 0.3f, 0.1f);
        if (pt > 1.0f) {
          a.ctl.armR = vec3(0);
          a.ctl.headPitch = a.ctl.headYaw = a.ctl.perk = a.ctl.lean = 0;
          next();
        }
        break;
      }
      case 6:
        if (moveInA(w, dt, 0.8f)) next();
        break;
      case 7:
        return finishA(w);
    }
    return false;
  }
};

// Goo grenade: a blob of slime lobbed at the victim, which melts into a puddle.
class PawnGoo : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  int goo = -1;
  Lob lob;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 hand = bonePoint(a, al::AH_LFORE, vec3(0.082f, 0.198f, 0.06f) * 1.2f);
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // a blob of goo grows in his hand as he winds up
        if (enter()) {
          goo = w.addPuppet(al::AL_GOO, a.color, 2);
          w.sound(SFX_SLURP, 0.4f, a.pos, 1.4f);
        }
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.ctl.armL = vec3(-0.9f * easeInOut(u), -0.2f * u, 0.6f * u);
        a.ctl.lean = 0.06f * u;
        if (Puppet* p = w.puppet(goo)) {
          p->scale = 0.9f * easeOutBack(u);
          p->pos = hand - vec3(0, 0.05f, 0) * p->scale;
        }
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // and throws
        float u = glm::clamp(pt / 0.18f, 0.0f, 1.0f);
        a.ctl.armL = glm::mix(vec3(-0.9f, -0.2f, 0.6f), vec3(1.6f, 0.1f, -0.2f), easeOut(u));
        a.ctl.lean = glm::mix(0.06f, -0.08f, u);
        if (Puppet* p = w.puppet(goo)) p->pos = hand - vec3(0, 0.045f, 0);
        if (u >= 1.0f) {
          vec3 from = hand;
          lob.throwAt(goo, from, V + vec3(0, v.height() * 0.85f, 0), 0.45f + 0.08f * L, 0.45f + 0.08f * L);
          w.sound(SFX_WHOOSH, 0.5f, a.pos, 1.5f);
          next();
        }
        break;
      }
      case 3:
        a.ctl.armL = glm::mix(a.ctl.armL, vec3(0), std::min(1.0f, dt * 5.0f));
        if (lob.update(w, dt)) {
          if (Puppet* p = w.puppet(goo)) p->alive = false;
          vec3 hit = V + vec3(0, v.height() * 0.85f, 0);
          gooSplash(w, hit, a.color, 40);
          w.sound(SFX_SPLAT, 1.0f, hit);
          w.sound(SFX_SIZZLE, 0.7f, hit);
          v.sq.v -= 3.0f;
          next();
        }
        break;
      case 4: {  // it melts; he does a little jig
        float u = glm::clamp(pt / 2.0f, 0.0f, 1.0f);
        melt(w, v, u, armyGoo(a.color), dt);
        a.pos.y = std::max(0.0f, 0.06f * std::sin(pt * 12.0f)) * (pt > 0.4f && pt < 1.6f ? 1.0f : 0.0f);
        a.ctl.perk = 0.8f * std::sin(glm::clamp(pt / 1.8f, 0.0f, 1.0f) * kPI);
        a.ctl.armL = vec3(0.6f, 0.5f, 0.8f) * std::sin(glm::clamp(pt / 1.8f, 0.0f, 1.0f) * kPI);
        if (pt > 0.5f && pt - dt <= 0.5f) w.sound(SFX_GIBBER, 0.6f, a.pos, 1.3f);  // a giggle
        if (u >= 1.0f) {
          v.alive = false;
          v.visible = false;
          w.groundRing(V, armyGoo(a.color), 1.0f, 0.8f, 0.08f);
          w.fx.smoke(V + vec3(0, 0.05f, 0), 8, 0.2f, 0.3f, 0.2f);
          a.pos.y = 0;
          a.ctl.perk = 0;
          a.ctl.armL = vec3(0);
          next();
        }
        break;
      }
      case 5:
        if (moveInA(w, dt, 0.8f)) next();
        break;
      case 6:
        return finishA(w);
    }
    return false;
  }
};

// Tongue: it shoots out, wraps the victim, reels it in - gulp - and he burps up the pieces.
class PawnTongue : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  bool walking = false;
  float reach = 0, pitch = 0, vScale = 1;
  vec3 mouth{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1:  // waddle up close
        if (!walking) {
          walking = true;
          walker = AWalker(plan.mover, V - dir * std::min(L, 0.95f), AWalker::WADDLE, 0.8f);
        }
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // open wide and take aim
        float u = glm::clamp(pt / 0.3f, 0.0f, 1.0f);
        a.yawTarget = faceAngle(V - flat(a.pos));
        mouth = bonePoint(a, al::AH_JAW, vec3(0, 0.416f, 0.06f) * 1.2f);
        vec3 tgt = victimCenter(w) + vec3(0, v.height() * 0.1f, 0);
        float horiz = glm::length(flat(tgt - mouth));
        pitch = std::atan2(mouth.y - tgt.y, horiz);
        a.ctl.jaw = u;
        a.ctl.headPitch = (pitch - 0.55f) * u;  // (the open jaw tips the tongue down)
        a.ctl.lean = 0.05f * u;
        reach = glm::length(tgt - mouth) - 0.02f;
        if (u >= 1.0f) {
          w.sound(SFX_WHOOSH, 0.6f, mouth, 1.8f);
          next();
        }
        break;
      }
      case 3: {  // thwip!
        float u = glm::clamp(pt / 0.14f, 0.0f, 1.0f);
        a.ctl.tongue = reach * easeOut(u);
        if (u >= 1.0f) {
          w.sound(SFX_SPLAT, 0.4f, victimCenter(w), 1.6f);
          v.tremble = 1.0f;
          next();
        }
        break;
      }
      case 4: {  // reel it in, shrinking into his mouth
        float u = glm::clamp((pt - 0.15f) / 0.55f, 0.0f, 1.0f);
        if (pt <= dt + 1e-6f) w.sound(SFX_SLURP, 1.0f, a.pos);
        float e = easeIn(u);
        a.ctl.tongue = reach * (1.0f - e);
        vec3 m = bonePoint(a, al::AH_JAW, vec3(0, 0.416f, 0.06f) * 1.2f);
        vec3 tip = glm::mix(victimCenter(w), m, 0.0f);
        (void)tip;
        vScale = 1.0f - 0.85f * e;
        v.scale = vScale;
        vec3 c = glm::mix(V + vec3(0, 0.0f, 0), m - vec3(0, v.height() * 0.5f, 0), e);
        v.pos = c;
        v.yawTarget = v.yaw + 4.0f * dt;
        v.idleWeight = 0;
        if (u >= 1.0f) {
          v.visible = false;
          a.ctl.tongue = 0;
          w.sound(SFX_POP, 0.5f, m, 0.8f);
          next();
        }
        break;
      }
      case 5: {  // gulp, chew, chew...
        a.ctl.jaw = std::max(0.0f, 0.6f * std::sin(pt * 14.0f)) * (pt < 0.9f ? 1.0f : 0.0f);
        a.ctl.headPitch = approach(a.ctl.headPitch, 0.0f, dt * 2.0f);
        a.poseSquash = pt < 0.2f ? 1.2f : 0.95f;
        if (std::fmod(pt, 0.22f) < dt && pt < 0.9f) w.sound(SFX_BITE, 0.35f, a.pos, w.fx.rnd(1.4f, 1.7f));
        if (pt > 1.1f) {
          a.poseSquash = 1;
          next();
        }
        break;
      }
      case 6: {  // ...BURP - and out come the pieces
        if (enter()) {
          w.sound(SFX_BURP, 1.0f, a.pos);
          a.ctl.jaw = 1;
          a.ctl.lean = -0.1f;
        }
        if (pt > 0.25f && v.alive && !v.visible) {
          vec3 m = bonePoint(a, al::AH_JAW, vec3(0, 0.416f, 0.08f) * 1.2f);
          v.scale = 0.38f;
          v.pos = m - vec3(0, v.height() * 0.5f, 0);
          v.visible = true;
          v.dissolve = 0;
          w.shatter(v, m, a.forward() * 2.5f + vec3(0, 1.0f, 0), 0.9f, 0.8f, 8.0f);
          w.fx.ring(m, 0.06f, armyGoo(a.color) * 1.2f, 0.45f);
          w.fx.smoke(m, 5, 0.04f, 0.2f, 0.2f);
          a.ctl.lean = 0.15f;
        }
        if (pt > 0.9f) {
          a.ctl.jaw = a.ctl.lean = 0;
          next();
        }
        break;
      }
      case 7:
        if (moveInA(w, dt, 0.8f)) next();
        break;
      case 8:
        return finishA(w);
    }
    return false;
  }
};

// ===========================================================================
// KNIGHT - the flying saucer
// ===========================================================================
// Abduction: it flies over the victim and beams it up, spinning; a burp of
// noises inside, and out it comes again - in pieces.
class KnightAbduction : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  SaucerHop hop;
  float lift = 0, over = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.1f;
        shotHeight = 0.85f;
        if (faceOff(w)) next();
        break;
      case 1:  // over the victim
        if (enter()) {
          over = std::max(0.55f, v.height() + 0.25f - al::hoverHeight(K_KNIGHT) + 0.12f);
          hop.begin(a, V, over, 0.25f, 0.9f + 0.15f * L);
          w.sound(SFX_UFO, 0.8f, a.pos);
        }
        if (hop.update(w, a, dt)) next();
        break;
      case 2:  // the belly opens
        a.ctl.hatch = easeInOut(pt / 0.35f);
        a.ctl.spin += dt * 6.0f;
        if (pt > 0.35f) {
          w.sound(SFX_BEAM, 1.0f, V);
          next();
        }
        break;
      case 3: {  // the beam: up it goes, spinning and shrinking
        float u = glm::clamp(pt / 2.2f, 0.0f, 1.0f);
        vec3 top = muzzleOf(a);
        tractorBeam(w, top, V, 0.24f, armyLight(a.color), dt, 1.0f);
        a.ctl.spin += dt * 8.0f;
        lift = (top.y - 0.05f - v.height() * 0.4f) * easeIn(u);
        v.pos = vec3(V.x, lift, V.z);
        v.yawRate = 30.0f;
        v.yawTarget = v.yaw + dt * (3.0f + 10.0f * u);
        v.tilt = glm::angleAxis(0.3f * std::sin(pt * 3.0f) * u, vec3(1, 0, 0));
        v.tiltPivot = vec3(0, v.height() * 0.5f, 0);
        v.scale = 1.0f - 0.55f * easeIn(u);
        v.tremble = 0.4f;
        v.idleWeight = 0;
        v.glow = glm::vec4(armyLight(a.color), 0.6f * u);
        if (v.set == 2) v.ctl.perk = -1.0f;
        if (u >= 1.0f) {
          v.visible = false;
          a.ctl.hatch = 0;
          w.sound(SFX_POP, 0.6f, top, 0.7f);
          next();
        }
        break;
      }
      case 4: {  // noises inside
        a.ctl.hatch = approach(a.ctl.hatch, 0.0f, dt * 4.0f);
        a.ctl.twist = 0.12f * std::sin(pt * 21.0f);
        a.ctl.lean = 0.08f * std::sin(pt * 17.0f);
        a.tremble = 0.4f;
        if (std::fmod(pt, 0.3f) < dt) w.sound(w.fx.rnd() < 0.5f ? SFX_CLACK : SFX_BITE, 0.5f, a.pos, w.fx.rnd(1.2f, 1.6f));
        if (pt > 0.15f && pt - dt <= 0.15f) w.sound(SFX_GIBBER, 0.6f, a.pos, 1.4f);
        if (pt > 1.0f) {
          a.ctl.twist = a.ctl.lean = 0;
          a.tremble = 0;
          next();
        }
        break;
      }
      case 5: {  // ...and spits it out in pieces
        if (enter()) {
          a.ctl.hatch = 1;
          vec3 top = muzzleOf(a);
          v.visible = true;
          v.scale = 0.6f;
          v.tilt = quat(1, 0, 0, 0);
          v.glow = glm::vec4(0);
          v.pos = top - vec3(0, v.height() + 0.02f, 0);
          w.shatter(v, top, vec3(0, -2.0f, 0) + dir * 0.6f, 1.6f, -0.5f, 8.0f);
          w.sound(SFX_BURP, 0.6f, top, 1.6f);
          w.fx.smoke(top, 6, 0.06f, -0.2f, 0.4f);
        }
        if (pt > 0.6f) {
          a.ctl.hatch = 0;
          next();
        }
        break;
      }
      case 6:  // settle on the square
        if (enter()) hop.begin(a, squareCenter(plan.move.to), 0.0f, 0.05f, 0.9f);
        if (hop.update(w, a, dt)) next();
        break;
      case 7:
        return finishA(w);
    }
    return false;
  }
};

// Crop circle: it circles the victim, a laser carving a glowing ring into the
// board, then cuts the victim clean in two from above.
class KnightCropCircle : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  SaucerHop hop;
  float ang0 = 0, mark = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    const float R = 0.55f;
    switch (phase) {
      case 0:
        shotExtra = 0.2f;
        if (faceOff(w)) next();
        break;
      case 1:  // to the start of the circle
        if (enter()) {
          vec3 s = V - dir * R;
          ang0 = std::atan2(s.x - V.x, s.z - V.z);
          hop.begin(a, s, 0.2f, 0.2f, 0.7f + 0.12f * L);
          w.sound(SFX_UFO, 0.7f, a.pos, 1.15f);
        }
        if (hop.update(w, a, dt)) {
          w.sound(SFX_HEATRAY, 0.45f, V, 1.5f);
          next();
        }
        break;
      case 2: {  // round it goes, burning a ring into the board
        float u = glm::clamp(pt / 2.0f, 0.0f, 1.0f);
        float an = ang0 + kTau * 1.15f * easeInOut(u);
        a.pos = vec3(V.x + std::sin(an) * R, 0, V.z + std::cos(an) * R);
        a.yawTarget = an + kPI * 0.5f;
        a.ctl.twist = -0.3f;
        a.ctl.spin += dt * 14.0f;
        a.ctl.hatch = 1;
        vec3 m = muzzleOf(a);
        vec3 g = flat(a.pos) + vec3(0, 0.005f, 0);
        beamLine(w, m, g, vec3(1.0f, 0.35f, 0.12f), 0.004f, 0.7f);
        mark -= dt;
        if (mark <= 0) {  // a glowing scorch that lingers
          mark = 0.02f;
          Particle p;
          p.pos = g;
          p.vel = vec3(0);
          p.size = 0.035f;
          p.color = vec3(2.5f, 0.9f, 0.25f);
          p.colorEnd = vec3(0.3f, 0.05f, 0.0f);
          p.opacity = 0;
          p.maxLife = 2.6f;
          p.type = PT_GLOW;
          p.still = true;
          p.fadeIn = 0;
          w.fx.add(p);
          if (w.fx.rnd() < 0.3f) w.fx.smoke(g, 1, 0.02f, 0.3f, 0.6f);
        }
        v.tremble = 0.7f;
        v.yawTarget = faceAngle(flat(a.pos) - V);
        if (u >= 1.0f) {
          a.ctl.twist = 0;
          w.groundRing(V, vec3(1.0f, 0.4f, 0.1f), 0.2f, 0.8f, 0.05f);
          next();
        }
        break;
      }
      case 3:  // up over the victim
        if (enter()) hop.begin(a, V, std::max(0.4f, v.height() + 0.2f - al::hoverHeight(K_KNIGHT) + 0.12f), 0.1f, 0.6f);
        if (hop.update(w, a, dt)) {
          w.sound(SFX_ZAP, 0.9f, V, 0.8f);
          next();
        }
        break;
      case 4: {  // the cut
        float u = glm::clamp(pt / 0.5f, 0.0f, 1.0f);
        vec3 m = muzzleOf(a);
        vec3 bottom = glm::mix(m - vec3(0, 0.1f, 0), V, easeIn(u));
        beamLine(w, m, bottom, vec3(1.0f, 0.4f, 0.15f), 0.003f, 1.0f);
        w.fx.sparks(bottom, vec3(0, 1, 0), 3, 1.5f, 0.8f, vec3(4.0f, 2.0f, 0.6f));
        if (u >= 1.0f) {
          w.sound(SFX_CLANG, 0.8f, V, 1.3f);
          w.slice(v, 0.7f);
          w.fx.flash(V + vec3(0, 0.4f, 0), 0.6f, vec3(5.0f, 2.5f, 0.8f), 0.12f);
          a.ctl.hatch = 0;
          next();
        }
        break;
      }
      case 5:
        if (pt < 0.3f) break;
        if (enter()) hop.begin(a, squareCenter(plan.move.to), 0.0f, 0.15f, 0.7f);
        if (hop.update(w, a, dt)) next();
        break;
      case 6:
        return finishA(w);
    }
    return false;
  }
};

// Cow drop: a cow falls out of the saucer onto the victim, bounces, moos, and
// is beamed back up.
class KnightCowDrop : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  SaucerHop hop;
  int cow = -1;
  vec3 cp{0}, cv{0};
  float spin = 0;
  bool hit = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.5f;
        shotHeight = 1.0f;
        if (faceOff(w)) next();
        break;
      case 1:  // high over the victim
        if (enter()) {
          hop.begin(a, V, 1.05f, 0.2f, 0.9f + 0.15f * L);
          w.sound(SFX_UFO, 0.8f, a.pos, 0.9f);
        }
        if (hop.update(w, a, dt)) next();
        break;
      case 2: {  // the belly opens; a cow appears
        if (enter()) {
          cow = w.addPuppet(al::AL_COW, a.color, 2);
          w.sound(SFX_MOO, 0.9f, a.pos, 1.05f);
        }
        a.ctl.hatch = easeInOut(pt / 0.4f);
        float u = glm::clamp((pt - 0.2f) / 0.5f, 0.0f, 1.0f);
        if (Puppet* p = w.puppet(cow)) {
          p->scale = 1.3f * easeOutBack(u);
          p->pos = muzzleOf(a) - vec3(0, 0.38f * p->scale, 0);
          p->yaw = a.yaw + 0.5f;
          p->ctl.wings = u;
        }
        if (pt > 0.8f) {
          if (Puppet* p = w.puppet(cow)) cp = p->pos;
          cv = vec3(0, -0.5f, 0);
          w.sound(SFX_WHISTLE, 0.25f, a.pos, 0.6f);
          next();
        }
        break;
      }
      case 3: {  // down it comes
        a.ctl.hatch = approach(a.ctl.hatch, 0.0f, dt * 2.0f);
        cv.y -= 14.0f * dt;
        cp += cv * dt;
        spin += dt * 4.0f;
        Puppet* p = w.puppet(cow);
        if (!p) {
          next();
          break;
        }
        p->pos = cp;
        p->ctl.wings = 1;
        p->tilt = glm::angleAxis(0.4f * std::sin(spin), vec3(0, 0, 1));
        if (!hit && v.alive && cp.y <= v.pos.y + v.height() * 0.85f) {
          hit = true;
          w.shatter(v, V + vec3(0, v.height() * 0.8f, 0), vec3(0, -3.0f, 0), 1.8f, 0.4f, 9.0f);
          w.sound(SFX_THUD, 1.0f, V, 0.8f);
          w.sound(SFX_BOING, 0.9f, V);
          w.shake(0.4f);
          w.fx.dust(V, 20, 0.5f, 1.6f);
          cv = vec3(dir.x * 0.6f + 0.3f, 3.2f, dir.z * 0.6f);
        }
        if (cp.y <= 0.0f && cv.y < 0) {
          cp.y = 0;
          p->pos = cp;
          p->tilt = quat(1, 0, 0, 0);
          p->ctl.wings = 0;
          w.sound(SFX_THUD, 0.6f, cp, 1.3f);
          w.fx.dust(cp, 8, 0.25f, 0.6f);
          next();
        }
        break;
      }
      case 4: {  // it stands there, bewildered, and moos
        Puppet* p = w.puppet(cow);
        if (p) {
          p->ctl.headYaw = 0.6f * std::sin(pt * 3.0f);
          p->ctl.headPitch = -0.2f;
        }
        if (pt > 0.3f && pt - dt <= 0.3f) w.sound(SFX_MOO, 1.0f, cp, 0.95f);
        if (pt > 1.3f) {
          a.ctl.hatch = 0;
          w.sound(SFX_BEAM, 0.7f, cp, 1.2f);
          next();
        }
        break;
      }
      case 5: {  // and is beamed back up
        float u = glm::clamp(pt / 1.6f, 0.0f, 1.0f);
        a.ctl.hatch = 1;
        a.pos = vec3(glm::mix(V.x, cp.x, easeInOut(std::min(1.0f, pt / 0.4f))), 0, glm::mix(V.z, cp.z, easeInOut(std::min(1.0f, pt / 0.4f))));
        vec3 top = muzzleOf(a);
        tractorBeam(w, top, flat(cp), 0.22f, armyLight(a.color), dt, 1.0f);
        if (Puppet* p = w.puppet(cow)) {
          float e = easeIn(glm::clamp((u - 0.15f) / 0.85f, 0.0f, 1.0f));
          p->pos = glm::mix(cp, top - vec3(0, 0.1f, 0), e);
          p->scale = 1.3f * (1.0f - 0.85f * e);
          p->yaw += dt * 3.0f;
          p->ctl.wings = 0.4f;
          if (u >= 1.0f) p->alive = false;
        }
        if (u >= 1.0f) {
          a.ctl.hatch = 0;
          next();
        }
        break;
      }
      case 6:
        if (enter()) hop.begin(a, squareCenter(plan.move.to), 0.0f, 0.1f, 1.0f);
        if (hop.update(w, a, dt)) next();
        break;
      case 7:
        return finishA(w);
    }
    return false;
  }
};
// ===========================================================================
// BISHOP - the big-brained psychic
// ===========================================================================
const vec3 kPsi(1.0f, 0.42f, 0.75f);

// fingertips to the temples (u 0..1)
void temples(Actor& a, float u) {
  a.ctl.armL = vec3(2.0f, 0.15f, 1.5f) * u;
  a.ctl.armR = vec3(2.0f, 0.15f, 1.5f) * u;
  a.ctl.headPitch = -0.1f * u;
}

// Mind blast: the victim swells under the strain and pops.
class BishopMindBlast : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // concentrate
        float u = easeInOut(pt / 0.6f);
        temples(a, u);
        a.glow = glm::vec4(kPsi, 0.8f * u);
        a.ctl.hover = 0.08f * u;
        if (pt <= dt + 1e-6f) w.sound(SFX_CHARGE, 0.7f, a.pos, 0.7f);
        if (pt > 0.6f) {
          w.sound(SFX_THEREMIN, 0.7f, a.pos, 1.3f);
          next();
        }
        break;
      }
      case 2: {  // waves of thought; the victim swells, wobbling
        float u = glm::clamp(pt / 2.2f, 0.0f, 1.0f);
        temples(a, 1.0f);
        a.glow = glm::vec4(kPsi, 0.8f + 0.6f * std::sin(pt * 10.0f));
        psychicRings(w, headOf(a), victimCenter(w), kPsi, dt);
        v.scale = 1.0f + 0.5f * easeIn(u) + 0.04f * std::sin(pt * 31.0f) * u;
        v.poseSquash = 1.0f + 0.12f * std::sin(pt * 13.0f) * u;
        v.tremble = 0.4f + 0.6f * u;
        v.idleWeight = 0;
        v.glow = glm::vec4(kPsi, 0.3f + 1.2f * u);
        if (v.set == 2) {
          v.ctl.perk = 1.0f;
          v.ctl.jaw = 0.8f * u;
        }
        w.frameLights.push_back({victimCenter(w), 2.0f, kPsi * (2.0f + 6.0f * u)});
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          w.sound(SFX_POP, 1.0f, c, 0.7f);
          w.sound(SFX_EXPLODE, 0.6f, c, 1.4f);
          w.fx.magicBurst(c, kPsi * 3.0f, 70, 2.5f);
          w.fx.flash(c, 0.8f, kPsi * 5.0f, 0.15f);
          w.shatter(v, c, vec3(0, 0.5f, 0), 3.0f, 1.0f, 12.0f);
          w.shake(0.35f);
          next();
        }
        break;
      }
      case 3: {
        float u = easeInOut(pt / 0.5f);
        temples(a, 1.0f - u);
        a.glow = glm::vec4(kPsi, 0.8f * (1.0f - u));
        a.ctl.hover = 0.08f * (1.0f - u);
        if (pt > 0.5f) next();
        break;
      }
      case 4:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 5:
        return finishA(w);
    }
    return false;
  }
};

// Hypnosis: the victim dances in a daze, then blasts off into the sky - and twinkles.
class BishopHypnosis : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  float vy = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // the stare
        float u = easeInOut(pt / 0.6f);
        a.glow = glm::vec4(kPsi, 1.0f * u);
        a.ctl.armR = vec3(1.4f, 0.0f, -0.3f) * u;
        a.ctl.lean = 0.08f * u;
        if (pt <= dt + 1e-6f) w.sound(SFX_THEREMIN, 1.0f, a.pos);
        if (pt > 0.6f) next();
        break;
      }
      case 2: {  // the dance
        float t = pt;
        psychicRings(w, headOf(a), victimCenter(w), kPsi, dt * 0.5f);
        w.fx.swirl(V + vec3(0, v.height() * 0.6f, 0), 0.32f, kPsi, w.time);
        v.idleWeight = 0;
        v.tremble = 0;
        v.poseBend = vec2(0.28f * std::sin(t * 6.0f), 0.1f * std::sin(t * 3.0f));
        v.pos.y = 0.08f * std::fabs(std::sin(t * 6.0f));
        v.yawRate = 6.0f;
        v.yawTarget = faceAngle(A - V) + 0.9f * std::sin(t * 2.0f);
        if (v.set == 2) {
          v.ctl.perk = -1.0f;
          v.ctl.headYaw = 0.6f * std::sin(t * 4.0f);
          v.ctl.headPitch = 0.2f * std::sin(t * 3.0f);
          v.ctl.armL = vec3(1.2f + 0.6f * std::sin(t * 6.0f), 0.6f, 0.5f);
          v.ctl.armR = vec3(1.2f + 0.6f * std::sin(t * 6.0f + kPI), 0.6f, 0.5f);
        }
        if (std::fmod(t, 0.5f) < dt) w.sound(SFX_BOING, 0.25f, v.pos, w.fx.rnd(1.2f, 1.6f));
        if (t > 2.6f) {
          v.poseBend = vec2(0);
          v.pos.y = 0;
          next();
        }
        break;
      }
      case 3: {  // the point to the sky...
        float u = easeInOut(pt / 0.35f);
        a.ctl.armR = glm::mix(vec3(1.4f, 0.0f, -0.3f), vec3(2.6f, 0.1f, -0.3f), u);
        a.ctl.headPitch = -0.3f * u;
        v.ctl.perk = 1.0f;
        if (pt > 0.35f) {
          vy = 0.3f;
          w.sound(SFX_WHISTLE, 1.0f, v.pos);
          w.fx.dust(V, 16, 0.3f, 1.4f);
          next();
        }
        break;
      }
      case 4: {  // ...and off it goes like a rocket
        vy += 18.0f * dt;
        v.pos.y += vy * dt;
        v.pos.x += 0.4f * dt * dir.x;
        v.pos.z += 0.4f * dt * dir.z;
        v.yawTarget = v.yaw + dt * 12.0f;
        w.fx.smoke(v.pos, 1, 0.05f, -0.3f, 0.2f);
        w.fx.sparks(v.pos, vec3(0, -1, 0), 2, 2.0f, 0.4f, vec3(4.0f, 2.2f, 0.6f));
        a.ctl.headPitch = std::max(-0.7f, -0.3f - 0.1f * v.pos.y);
        if (v.pos.y > 7.0f) {
          w.fx.flash(v.pos, 0.4f, vec3(6.0f, 6.0f, 7.0f), 0.5f);
          w.fx.flash(v.pos, 0.15f, vec3(9.0f), 0.8f);
          w.sound(SFX_TWINKLE, 1.0f, v.pos);
          v.alive = false;
          v.visible = false;
          next();
        }
        break;
      }
      case 5: {  // a satisfied nod
        float u = glm::clamp(pt / 0.7f, 0.0f, 1.0f);
        a.ctl.armR = glm::mix(vec3(2.6f, 0.1f, -0.3f), vec3(0), easeInOut(u));
        a.ctl.headPitch = glm::mix(-0.7f, 0.0f, u) + 0.2f * std::sin(u * kPI * 2.0f);
        a.glow = glm::vec4(kPsi, 1.0f - u);
        a.ctl.lean = 0.08f * (1.0f - u);
        if (u >= 1.0f) next();
        break;
      }
      case 6:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 7:
        return finishA(w);
    }
    return false;
  }
};

// Telekinetic crush: lifted into the air and squeezed until it bursts.
class BishopCrush : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // hands out
        float u = easeInOut(pt / 0.5f);
        a.ctl.armL = vec3(1.3f, -0.2f, -0.4f) * u;
        a.ctl.armR = vec3(1.3f, -0.2f, -0.4f) * u;
        a.glow = glm::vec4(kPsi, 0.7f * u);
        if (pt > 0.5f) {
          w.sound(SFX_RUMBLE, 0.6f, V, 1.5f);
          next();
        }
        break;
      }
      case 2: {  // up it floats
        float u = glm::clamp(pt / 0.9f, 0.0f, 1.0f);
        v.pos.y = 0.55f * easeInOut(u);
        v.idleWeight = 0;
        v.tremble = 0.5f;
        v.glow = glm::vec4(kPsi, 0.5f * u);
        psychicRings(w, headOf(a), victimCenter(w), kPsi, dt * 0.6f);
        if (v.set == 2) v.ctl.perk = -0.6f;
        if (u >= 1.0f) next();
        break;
      }
      case 3: {  // and is squeezed - crunch, crunch
        float u = glm::clamp(pt / 1.3f, 0.0f, 1.0f);
        a.ctl.armL = vec3(1.3f, -0.2f - 0.5f * u, -0.4f);
        a.ctl.armR = vec3(1.3f, -0.2f - 0.5f * u, -0.4f);
        v.pos.y = 0.55f + 0.02f * std::sin(pt * 40.0f);
        v.poseSquash = 1.0f - 0.55f * easeIn(u);
        v.poseBend = vec2(0.1f * std::sin(pt * 25.0f), 0) * u;
        v.crack = u;
        v.crackColor = kPsi;
        v.tremble = 1.0f;
        psychicRings(w, headOf(a), victimCenter(w), kPsi, dt);
        if (std::fmod(pt, 0.3f) < dt) w.sound(SFX_CLACK, 0.6f, victimCenter(w), w.fx.rnd(0.6f, 0.9f));
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          w.sound(SFX_SHATTER, 1.0f, c, 0.9f);
          w.sound(SFX_POP, 0.7f, c, 0.6f);
          w.fx.magicBurst(c, kPsi * 3.0f, 50, 2.0f);
          w.shatter(v, c, vec3(0, -0.5f, 0), 2.2f, 0.6f, 10.0f);
          w.shake(0.3f);
          next();
        }
        break;
      }
      case 4: {
        float u = easeInOut(pt / 0.5f);
        a.ctl.armL = a.ctl.armR = vec3(1.3f, -0.7f, -0.4f) * (1.0f - u);
        a.glow = glm::vec4(kPsi, 0.7f * (1.0f - u));
        if (pt > 0.5f) next();
        break;
      }
      case 5:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 6:
        return finishA(w);
    }
    return false;
  }
};

// ===========================================================================
// ROOK - the tripod
// ===========================================================================
// the tripod's eye, aimed at a point: returns the head pitch (+ looks down)
float aimPitch(const Actor& a, const vec3& target) {
  vec3 e = muzzleOf(a);
  return std::atan2(e.y - target.y, std::max(0.1f, glm::length(flat(target - e))));
}

// Heat ray: it rises up tall, sounds its horn, and burns the victim to a cinder.
class RookHeatRay : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    const vec3 hot(1.0f, 0.55f, 0.2f);
    switch (phase) {
      case 0:
        shotExtra = 0.3f;
        shotHeight = 0.9f;
        if (faceOff(w)) next();
        break;
      case 1: {  // stand up tall; the horn
        if (pt <= dt + 1e-6f) {
          w.sound(SFX_HORN, 1.0f, a.pos);
          w.sound(SFX_GRIND, 0.4f, a.pos, 1.4f);
        }
        float u = glm::clamp(pt / 1.3f, 0.0f, 1.0f);
        a.ctl.legs = easeInOut(u);
        a.ctl.headPitch = aimPitch(a, victimCenter(w)) * u;
        if (pt > 2.0f) next();
        break;
      }
      case 2: {  // the eye glows
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.ctl.headPitch = aimPitch(a, victimCenter(w));
        a.ctl.hatch = u;
        w.frameLights.push_back({muzzleOf(a), 1.5f, hot * (3.0f + 10.0f * u)});
        if (pt <= dt + 1e-6f) w.sound(SFX_CHARGE, 0.6f, a.pos, 0.6f);
        if (u >= 1.0f) {
          w.sound(SFX_HEATRAY, 1.0f, a.pos);
          next();
        }
        break;
      }
      case 3: {  // the ray
        float u = glm::clamp(pt / 1.7f, 0.0f, 1.0f);
        vec3 m = muzzleOf(a), c = victimCenter(w);
        a.ctl.headPitch = aimPitch(a, c) + 0.01f * std::sin(pt * 40.0f);
        beamLine(w, m, c, hot, 0.006f, 1.2f);
        v.heat = easeIn(u);
        v.tremble = 0.5f + 0.5f * u;
        v.idleWeight = 0;
        v.glow = glm::vec4(1.0f, 0.4f, 0.1f, 1.2f * u);
        if (w.fx.rnd() < 0.5f) w.fx.embers(c, 2, 1.0f);
        if (w.fx.rnd() < 0.3f) w.fx.smoke(c + vec3(0, v.height() * 0.4f, 0), 1, 0.08f, 0.6f, 0.8f);
        if (v.set == 2) v.ctl.jaw = 0.8f;
        if (u >= 1.0f) {
          w.fx.fireball(c, 1.1f);
          w.sound(SFX_EXPLODE, 1.0f, c);
          w.shatter(v, c, dir * 1.0f + vec3(0, 1.0f, 0), 2.4f, 1.0f, 9.0f);
          w.light(c, vec3(30.0f, 15.0f, 4.0f), 5.0f, 0.5f);
          w.shake(0.5f);
          next();
        }
        break;
      }
      case 4: {  // sink back down
        float u = glm::clamp(pt / 1.0f, 0.0f, 1.0f);
        a.ctl.hatch = 1.0f - u;
        a.ctl.legs = 1.0f - easeInOut(u);
        a.ctl.headPitch *= 1.0f - std::min(1.0f, dt * 3.0f);
        if (pt <= dt + 1e-6f) w.sound(SFX_GRIND, 0.35f, a.pos, 1.2f);
        if (u >= 1.0f) {
          a.ctl.headPitch = 0;
          next();
        }
        break;
      }
      case 5:
        if (moveInA(w, dt, 0.8f)) next();
        break;
      case 6:
        return finishA(w);
    }
    return false;
  }
};

// Stomp: it raises a foot over the victim, which hops aside - a miss - then it
// tries again.
class RookStompA : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  vec3 stompPos{0}, from{0}, dodge{0};
  float yaw0 = 0, yaw1 = 0;
  bool walking = false;
  // where the tripod must stand (and face) so its front-left foot comes down on p
  void placeFor(const vec3& p, const vec3& facing, vec3& pos, float& yaw) {
    yaw = faceAngle(facing);
    vec3 F(std::sin(1.0471976f) * 0.34f, 0, std::cos(1.0471976f) * 0.34f);
    vec3 f(std::cos(yaw) * F.x + std::sin(yaw) * F.z, 0, -std::sin(yaw) * F.x + std::cos(yaw) * F.z);
    pos = flat(p) - f;
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.1f;
        if (faceOff(w)) next();
        break;
      case 1: {  // stride up beside it
        if (!walking) {
          walking = true;
          placeFor(V, dir, stompPos, yaw0);
          walker = AWalker(plan.mover, stompPos, AWalker::TRIPOD, 1.0f);
        }
        if (walker.update(w, dt)) {
          a.yawTarget = yaw0;
          next();
        }
        break;
      }
      case 2: {  // the foot goes up; the victim scampers aside
        float u = glm::clamp(pt / 0.55f, 0.0f, 1.0f);
        a.ctl.lift = -easeInOut(u);
        a.ctl.lean = 0.05f * u;
        if (pt <= dt + 1e-6f) {
          w.sound(SFX_GRIND, 0.4f, a.pos, 1.6f);
          vec3 side(-dir.z, 0, dir.x);
          dodge = V + side * (w.fx.rnd() < 0.5f ? 0.42f : -0.42f) + dir * 0.1f;
        }
        float d = glm::clamp((pt - 0.25f) / 0.3f, 0.0f, 1.0f);
        v.pos = glm::mix(V, dodge, easeInOut(d));
        v.pos.y = 0.15f * std::sin(d * kPI);
        v.tremble = 0.8f;
        if (pt > 0.25f && pt - dt <= 0.25f) w.sound(SFX_BOING, 0.6f, v.pos, 1.3f);
        if (u >= 1.0f && d >= 1.0f) next();
        break;
      }
      case 3: {  // STOMP - on nothing
        float u = glm::clamp(pt / 0.14f, 0.0f, 1.0f);
        a.ctl.lift = -(1.0f - easeIn(u));
        if (u >= 1.0f && pt - dt < 0.14f) {
          vec3 f = V;
          w.sound(SFX_STOMP, 1.0f, f);
          w.fx.dust(f, 20, 0.4f, 1.4f);
          w.groundRing(f, vec3(0.8f, 0.7f, 0.55f), 2.4f, 0.4f, 0.06f);
          w.shake(0.35f);
        }
        if (pt > 0.3f) next();
        break;
      }
      case 4: {  // it looks round - there it is - and shuffles over
        float u = glm::clamp(pt / 1.2f, 0.0f, 1.0f);
        a.ctl.headYaw = 0.7f * std::sin(u * kPI * 2.0f) * (1.0f - u);
        if (pt <= dt + 1e-6f) {
          from = flat(a.pos);
          placeFor(dodge, flatDir(from, dodge) + dir * 0.5f, stompPos, yaw1);
          w.sound(SFX_HORN, 0.35f, a.pos, 1.6f);
        }
        float m = glm::clamp((pt - 0.6f) / 0.6f, 0.0f, 1.0f);
        if (m > 0) {
          vec3 p = glm::mix(from, stompPos, easeInOut(m));
          a.pos = vec3(p.x, 0, p.z);
          a.yawTarget = yaw1;
          a.ctl.gait += dt * 6.0f;
          a.ctl.stride = 0.6f * std::sin(m * kPI);
        }
        v.tremble = 0.6f;
        if (u >= 1.0f) {
          a.ctl.stride = 0;
          next();
        }
        break;
      }
      case 5: {  // up again...
        float u = glm::clamp(pt / 0.4f, 0.0f, 1.0f);
        a.ctl.lift = -easeInOut(u);
        if (pt <= dt + 1e-6f) w.sound(SFX_GRIND, 0.4f, a.pos, 1.6f);
        v.tremble = 1.0f;
        if (u >= 1.0f) next();
        break;
      }
      case 6: {  // ...and down. Got it.
        float u = glm::clamp(pt / 0.12f, 0.0f, 1.0f);
        a.ctl.lift = -(1.0f - easeIn(u));
        if (u >= 1.0f && v.alive) {
          vec3 f = v.pos;
          w.shatter(v, f + vec3(0, v.height(), 0), vec3(0, -3.5f, 0), 2.0f, 0.3f, 8.0f);
          w.sound(SFX_STOMP, 1.0f, f);
          w.sound(SFX_EXPLODE, 0.5f, f, 1.5f);
          w.fx.dust(f, 30, 0.6f, 1.8f);
          w.groundRing(f, vec3(0.8f, 0.7f, 0.55f), 3.0f, 0.6f, 0.08f);
          w.shake(0.55f);
        }
        if (pt > 0.5f) next();
        break;
      }
      case 7:
        a.ctl.headYaw = 0;
        if (moveInA(w, dt, 0.8f)) next();
        break;
      case 8:
        return finishA(w);
    }
    return false;
  }
};

// Tentacle toss: the tentacles grab the victim, lift it, give it a shake and fling it away.
class RookToss : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  Flight flight;
  bool walking = false, flung = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 tipL = bonePoint(a, al::TB_TL3, vec3(0.075f, 0.36f, 0.125f));
    vec3 tipR = bonePoint(a, al::TB_TR3, vec3(-0.075f, 0.36f, 0.125f));
    vec3 hold = (tipL + tipR) * 0.5f;
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1:  // close in
        if (!walking) {
          walking = true;
          walker = AWalker(plan.mover, V - dir * std::min(L, 0.6f), AWalker::TRIPOD, 1.0f);
        }
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // reach out
        float u = easeInOut(pt / 0.5f);
        a.ctl.armL = vec3(1.25f, 0.25f, 0.5f) * u;
        a.ctl.armR = vec3(1.25f, 0.25f, 0.5f) * u;
        if (pt <= dt + 1e-6f) w.sound(SFX_WHOOSH, 0.5f, a.pos, 0.8f);
        v.tremble = 0.8f;
        if (pt > 0.5f) {
          w.sound(SFX_CLACK, 0.7f, V, 1.4f);
          next();
        }
        break;
      }
      case 3: {  // lift it
        float u = easeInOut(pt / 0.9f);
        a.ctl.armL = vec3(1.25f + 0.9f * u, 0.25f, 0.5f + 0.3f * u);
        a.ctl.armR = a.ctl.armL;
        a.ctl.legs = 0.3f * u;
        vec3 p = hold - vec3(0, v.height() * 0.55f, 0);
        v.pos = glm::mix(V, p, std::min(1.0f, pt / 0.2f));
        v.idleWeight = 0;
        v.tremble = 0.6f;
        if (v.set == 2) v.ctl.perk = -1.0f;
        if (pt <= dt + 1e-6f) w.sound(SFX_GRIND, 0.4f, a.pos, 1.5f);
        if (pt > 0.9f) next();
        break;
      }
      case 4: {  // a good shake
        float s = std::sin(pt * 22.0f);
        a.ctl.armL = vec3(2.15f, 0.25f + 0.35f * s, 0.8f);
        a.ctl.armR = a.ctl.armL;
        v.pos = hold - vec3(0, v.height() * 0.55f, 0);
        v.yawTarget = v.yaw + 0.5f * s;
        if (std::fmod(pt, 0.15f) < dt) w.sound(SFX_CLACK, 0.4f, v.pos, w.fx.rnd(1.0f, 1.5f));
        if (pt > 0.9f) next();
        break;
      }
      case 5: {  // and away it goes
        if (!flung) {
          flung = true;
          v.pos = hold - vec3(0, v.height() * 0.55f, 0);
          flight.launch(v, dir * 3.2f + vec3(0, 5.0f, 0), 11.0f);
          w.sound(SFX_WHOOSH, 0.9f, v.pos, 0.9f);
          w.sound(SFX_WHISTLE, 0.5f, v.pos, 0.8f);
        }
        float u = easeInOut(pt / 0.5f);
        a.ctl.armL = glm::mix(vec3(2.4f, 0.25f, 0.2f), vec3(0), u);
        a.ctl.armR = a.ctl.armL;
        a.ctl.legs = 0.3f * (1.0f - u);
        if (v.alive && flight.update(v, dt)) crashLanding(w, v, dir);
        if (!v.alive && pt > 0.5f) next();
        break;
      }
      case 6:
        if (moveInA(w, dt, 0.8f)) next();
        break;
      case 7:
        return finishA(w);
    }
    return false;
  }
};
// ===========================================================================
// QUEEN - the alien queen
// ===========================================================================
// Acid spit: she rears back and spits a glob of acid; the victim sizzles and melts.
class QueenAcid : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  int glob = -1;
  Lob lob;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    vec3 mouth = bonePoint(a, al::AH_JAW, vec3(0, 0.83f, 0.07f));
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // rear back, the inner jaw slides out with a hiss
        float u = easeInOut(pt / 0.6f);
        a.ctl.lean = -0.25f * u;
        a.ctl.headPitch = -0.25f * u;
        a.ctl.jaw = u;
        a.ctl.tongue = 0.05f * u;
        a.ctl.armL = vec3(0.5f, 0.4f, 0.4f) * u;
        if (pt <= dt + 1e-6f) w.sound(SFX_SIZZLE, 0.4f, a.pos, 1.5f);
        if (pt > 0.6f) next();
        break;
      }
      case 2: {  // spit!
        float u = easeOut(pt / 0.15f);
        a.ctl.lean = glm::mix(-0.25f, 0.2f, u);
        a.ctl.headPitch = glm::mix(-0.25f, 0.15f, u);
        a.ctl.tongue = 0.05f + 0.05f * u;
        if (pt >= 0.15f) {
          glob = w.addPuppet(al::AL_GOO, a.color, 2);
          if (Puppet* p = w.puppet(glob)) p->scale = 0.8f;
          lob.throwAt(glob, mouth, V + vec3(0, v.height() * 0.8f, 0), 0.25f + 0.05f * L, 0.3f + 0.07f * L);
          w.sound(SFX_SPLAT, 0.4f, mouth, 1.8f);
          w.sound(SFX_WHOOSH, 0.4f, mouth, 1.6f);
          next();
        }
        break;
      }
      case 3:
        a.ctl.lean *= 1.0f - std::min(1.0f, dt * 4.0f);
        a.ctl.headPitch *= 1.0f - std::min(1.0f, dt * 4.0f);
        a.ctl.tongue = approach(a.ctl.tongue, 0.0f, dt * 0.5f);
        a.ctl.jaw = approach(a.ctl.jaw, 0.0f, dt * 2.0f);
        if (lob.update(w, dt)) {
          if (Puppet* p = w.puppet(glob)) p->alive = false;
          vec3 hit = V + vec3(0, v.height() * 0.8f, 0);
          gooSplash(w, hit, a.color, 45);
          w.sound(SFX_SPLAT, 1.0f, hit);
          w.sound(SFX_SIZZLE, 1.0f, hit);
          next();
        }
        break;
      case 4: {  // it sizzles away in a cloud of fumes
        float u = glm::clamp(pt / 2.0f, 0.0f, 1.0f);
        melt(w, v, u, armyGoo(a.color), dt);
        if (w.fx.rnd() < dt * 20.0f) {
          Particle f;
          f.pos = v.pos + vec3(w.fx.rnd(-0.1f, 0.1f), w.fx.rnd(0.0f, v.height() * 0.7f), w.fx.rnd(-0.1f, 0.1f));
          f.vel = vec3(0, w.fx.rnd(0.2f, 0.5f), 0);
          f.size = w.fx.rnd(0.05f, 0.12f);
          f.grow = 0.2f;
          f.color = armyGoo(a.color) * 0.35f;
          f.colorEnd = f.color * 0.5f;
          f.opacity = 0.35f;
          f.maxLife = 1.2f;
          f.type = PT_SMOKE;
          f.rot = w.fx.rnd(0, 6.28f);
          f.rotVel = 1.0f;
          f.seed = w.fx.rnd();
          w.fx.add(f);
        }
        a.ctl.tongue = 0;
        a.ctl.jaw = 0;
        if (u >= 1.0f) {
          v.alive = false;
          v.visible = false;
          w.groundRing(V, armyGoo(a.color), 1.0f, 0.8f, 0.08f);
          next();
        }
        break;
      }
      case 5:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 6:
        return finishA(w);
    }
    return false;
  }
};

// Brood: she lays an egg, which hops over to the victim and hatches a swarm of
// hungry little critters.
class QueenBrood : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  int egg = -1;
  int critter[5] = {-1, -1, -1, -1, -1};
  vec3 eggPos{0}, hopFrom{0}, hopTo{0}, nest{0};
  int hops = 0, hop = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // she lays an egg
        if (enter()) {
          egg = w.addPuppet(al::AL_EGG, a.color, 2);
          eggPos = flat(a.pos) + dir * 0.3f;
          w.sound(SFX_SPLAT, 0.5f, eggPos, 0.6f);
        }
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.ctl.lean = 0.15f * std::sin(u * kPI);
        a.ctl.hover = -0.03f * std::sin(u * kPI);
        if (Puppet* p = w.puppet(egg)) {
          p->pos = eggPos;
          p->scale = easeOutBack(u);
          p->yaw = a.yaw;
        }
        if (u >= 1.0f) {
          nest = V - dir * 0.24f;
          hops = std::max(1, (int)std::round(glm::length(flat(nest - eggPos)) / 0.5f));
          hop = 0;
          hopFrom = eggPos;
          next();
        }
        break;
      }
      case 2: {  // it hops over to the victim
        float seg = 0.32f;
        float u = glm::clamp(pt / seg, 0.0f, 1.0f);
        hopTo = glm::mix(eggPos, nest, (hop + 1) / (float)hops);
        if (Puppet* p = w.puppet(egg)) {
          p->pos = glm::mix(hopFrom, hopTo, u) + vec3(0, 0.18f * std::sin(u * kPI), 0);
          p->tilt = glm::angleAxis(0.3f * std::sin(u * kPI * 2.0f), vec3(1, 0, 0));
        }
        if (u >= 1.0f) {
          w.sound(SFX_BOING, 0.4f, hopTo, 1.4f);
          hop++;
          hopFrom = hopTo;
          pt = 0;
          if (hop >= hops) next();
        }
        break;
      }
      case 3: {  // it wobbles... and hatches
        if (Puppet* p = w.puppet(egg)) p->tilt = glm::angleAxis(0.15f * std::sin(pt * 30.0f) * pt, vec3(0, 0, 1));
        v.tremble = 0.7f;
        if (pt > 0.7f) {
          if (Puppet* p = w.puppet(egg)) {
            w.shatterAsset(al::AL_EGG, a.color, p->model(), glm::angleAxis(p->yaw, vec3(0, 1, 0)), vec3(0),
                           p->texOffset, p->pos + vec3(0, 0.1f, 0), vec3(0, 0.6f, 0), 1.2f, 0.6f, 6.0f, 0.0f, vec3(1),
                           2);
            p->alive = false;
          }
          w.sound(SFX_HATCH, 1.0f, nest);
          gooSplash(w, nest + vec3(0, 0.08f, 0), a.color, 20);
          for (int i = 0; i < 5; ++i) {
            critter[i] = w.addPuppet(al::AL_CRITTER, a.color, 2);
            if (Puppet* c = w.puppet(critter[i])) {
              float an = kTau * i / 5.0f;
              c->pos = nest + vec3(std::sin(an), 0, std::cos(an)) * 0.08f;
              c->yaw = an;
              c->scale = 1.1f;
            }
          }
          next();
        }
        break;
      }
      case 4: {  // the swarm: round and up the victim, nibbling
        float u = glm::clamp(pt / 2.0f, 0.0f, 1.0f);
        for (int i = 0; i < 5; ++i) {
          Puppet* c = w.puppet(critter[i]);
          if (!c) continue;
          float an = kTau * i / 5.0f + pt * (3.0f + 0.4f * i);
          float climb = v.height() * glm::clamp(u * 1.4f - 0.15f * i, 0.0f, 0.85f);
          float r = v.radius() * 0.95f + 0.04f;
          vec3 target = V + vec3(std::sin(an) * r, climb + 0.03f * std::fabs(std::sin(pt * 20.0f + i)), std::cos(an) * r);
          c->pos = glm::mix(c->pos, target, std::min(1.0f, dt * 10.0f));
          c->yaw = an + kPI * 0.5f;
          c->tilt = glm::angleAxis(-0.6f * (climb > 0.05f ? 1.0f : 0.0f), vec3(1, 0, 0));
        }
        v.tremble = 1.0f;
        v.idleWeight = 0;
        v.crack = 0.6f * u;
        v.crackColor = armyGoo(a.color);
        if (v.set == 2) {
          v.ctl.perk = -1.0f;
          v.ctl.jaw = 0.8f;
          v.ctl.armL = vec3(1.5f + 0.5f * std::sin(pt * 15.0f), 0.5f, 0.6f);
          v.ctl.armR = vec3(1.5f + 0.5f * std::sin(pt * 15.0f + 1.0f), 0.5f, 0.6f);
        }
        if (std::fmod(pt, 0.13f) < dt) w.sound(SFX_BITE, 0.35f, V, w.fx.rnd(1.6f, 2.1f));
        if (u >= 1.0f) {
          w.shatter(v, victimCenter(w), vec3(0, 0.5f, 0), 1.8f, 0.8f, 9.0f);
          w.sound(SFX_HATCH, 0.6f, V, 1.4f);
          next();
        }
        break;
      }
      case 5: {  // and they scurry back to mother, and vanish under her gown
        float u = glm::clamp(pt / 1.0f, 0.0f, 1.0f);
        for (int i = 0; i < 5; ++i) {
          Puppet* c = w.puppet(critter[i]);
          if (!c) continue;
          vec3 home = flat(a.pos) + dir * 0.1f;
          c->pos = glm::mix(c->pos, home, std::min(1.0f, dt * (3.0f + i)));
          c->pos.y = 0.04f * std::fabs(std::sin(pt * 18.0f + i));
          c->yaw = faceAngle(home - c->pos);
          c->tilt = quat(1, 0, 0, 0);
          c->dissolve = glm::clamp((u - 0.5f) * 2.0f, 0.0f, 1.0f);
          if (u >= 1.0f) c->alive = false;
        }
        if (u >= 1.0f) next();
        break;
      }
      case 6:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 7:
        return finishA(w);
    }
    return false;
  }
};

// Tail strike: she turns her back and her tail lashes round, swatting the victim away.
class QueenTail : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  Flight flight;
  bool walking = false, struck = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1:
        if (!walking) {
          walking = true;
          walker = AWalker(plan.mover, V - dir * std::min(L, 0.62f), AWalker::HOVER, 1.0f);
        }
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // turn her back, raise the tail
        float u = easeInOut(pt / 0.6f);
        a.yawRate = 6.0f;
        a.yawTarget = faceAngle(A - V) + 0.6f;
        a.ctl.lift = u;
        a.ctl.headYaw = 0.9f * u;  // looking back over her shoulder
        a.ctl.kick = -0.6f * u;
        v.tremble = 0.7f;
        if (pt <= dt + 1e-6f) w.sound(SFX_SIZZLE, 0.3f, a.pos, 2.0f);
        if (pt > 0.7f) {
          w.sound(SFX_WHOOSH, 1.0f, a.pos, 0.7f);
          next();
        }
        break;
      }
      case 3: {  // swish!
        float u = glm::clamp(pt / 0.25f, 0.0f, 1.0f);
        a.ctl.kick = glm::mix(-0.6f, 1.3f, easeIn(u));
        a.ctl.lift = 1.0f - 0.7f * u;
        if (u >= 0.6f && !struck) {
          struck = true;
          vec3 side(-dir.z, 0, dir.x);
          flight.launch(v, dir * 1.2f + side * 3.4f + vec3(0, 4.2f, 0), 12.0f);
          w.sound(SFX_CLACK, 1.0f, victimCenter(w), 0.7f);
          w.sound(SFX_BOING, 0.6f, victimCenter(w), 0.9f);
          w.fx.sparks(victimCenter(w), side, 25, 3.0f, 0.6f, armyLight(a.color) * 3.0f);
          w.shake(0.35f);
          w.slowmo(0.3f, 0.4f);
        }
        if (v.alive && struck && flight.update(v, dt)) crashLanding(w, v, dir);
        if (u >= 1.0f) next();
        break;
      }
      case 4: {  // turn back round
        float u = easeInOut(pt / 0.6f);
        a.ctl.kick = 1.3f * (1.0f - u);
        a.ctl.lift = 0.3f * (1.0f - u);
        a.ctl.headYaw = 0.9f * (1.0f - u);
        a.yawTarget = faceAngle(V - A);
        if (v.alive && flight.update(v, dt)) crashLanding(w, v, dir);
        if (pt > 0.6f && !v.alive) next();
        break;
      }
      case 5:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 6:
        return finishA(w);
    }
    return false;
  }
};

// ===========================================================================
// KING - the overlord
// ===========================================================================
const vec3 kRoyal(1.0f, 0.85f, 0.4f);

// the sceptre raised and pointed at a target (u 0..1)
void pointSceptre(Actor& a, float u) {
  a.ctl.armR = vec3(0.7f, -0.1f, -0.5f) * u;
  a.ctl.itemPitch = -0.4f * u;
}

// Shrink ray: zap - the victim shrinks to a squeaking speck - and the throne
// floats over and sits on it.
class KingShrink : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  vec3 from{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {
        float u = easeInOut(pt / 0.5f);
        pointSceptre(a, u);
        a.glow = glm::vec4(armyLight(a.color), 0.4f * u);
        if (pt <= dt + 1e-6f) w.sound(SFX_CHARGE, 0.5f, a.pos, 1.3f);
        if (pt > 0.5f) {
          w.sound(SFX_SHRINK, 1.0f, a.pos);
          next();
        }
        break;
      }
      case 2: {  // the ray: wobbly rings; the victim dwindles
        float u = glm::clamp(pt / 1.2f, 0.0f, 1.0f);
        vec3 m = muzzleOf(a), c = victimCenter(w);
        beamLine(w, m, c, armyLight(a.color), 0.03f, 0.6f);
        if (w.fx.rnd() < dt * 12.0f) w.fx.ring(glm::mix(m, c, w.fx.rnd()), 0.04f, armyLight(a.color) * 1.5f, 0.3f);
        v.scale = 1.0f - 0.84f * easeInOut(u);
        v.tremble = 0.5f;
        v.glow = glm::vec4(armyLight(a.color), 0.8f * (1.0f - u));
        if (v.set == 2) v.ctl.perk = 1.0f;
        if (u >= 1.0f) {
          pointSceptre(a, 1.0f);
          w.sound(SFX_GIBBER, 0.7f, v.pos, 2.2f);  // a tiny squeak of protest
          next();
        }
        break;
      }
      case 3: {  // the speck hops about; the overlord chortles
        float u = glm::clamp(pt / 0.9f, 0.0f, 1.0f);
        pointSceptre(a, 1.0f - easeInOut(u));
        a.glow = glm::vec4(0);
        v.pos.y = 0.08f * std::fabs(std::sin(pt * 14.0f));
        v.yawTarget = v.yaw + dt * 8.0f;
        a.ctl.lean = -0.08f * std::fabs(std::sin(pt * 9.0f));
        a.ctl.jaw = 0.5f * std::fabs(std::sin(pt * 9.0f));
        if (pt <= dt + 1e-6f) w.sound(SFX_GIBBER, 0.8f, a.pos, 0.55f);
        if (u >= 1.0f) {
          v.pos.y = 0;
          a.ctl.jaw = a.ctl.lean = 0;
          from = flat(a.pos);
          w.sound(SFX_WHOOSH, 0.5f, a.pos, 0.6f);
          next();
        }
        break;
      }
      case 4: {  // float over it
        float u = glm::clamp(pt / 0.9f, 0.0f, 1.0f);
        vec3 p = glm::mix(from, V, easeInOut(u));
        a.pos = vec3(p.x, 0, p.z);
        a.ctl.hover = 0.18f * std::sin(u * kPI * 0.5f);
        v.tremble = 1.0f;
        if (u >= 1.0f) next();
        break;
      }
      case 5: {  // and sit down on it - hard
        float u = glm::clamp(pt / 0.14f, 0.0f, 1.0f);
        a.ctl.hover = glm::mix(0.18f, -0.06f, easeIn(u));
        if (u >= 1.0f && v.alive) {
          w.shatter(v, V + vec3(0, 0.05f, 0), vec3(0, -1.0f, 0), 1.6f, 0.2f, 6.0f);
          w.sound(SFX_SPLAT, 0.8f, V, 1.4f);
          w.sound(SFX_THUD, 0.9f, V, 0.9f);
          w.fx.dust(V, 24, 0.45f, 1.5f);
          w.shake(0.3f);
        }
        if (pt > 0.14f) a.ctl.hover = -0.06f + 0.06f * easeOutBack(glm::clamp((pt - 0.14f) / 0.4f, 0.0f, 1.0f));
        if (pt > 0.6f) {
          a.ctl.hover = 0;
          next();
        }
        break;
      }
      case 6:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 7:
        return finishA(w);
    }
    return false;
  }
};

// The mothership: he summons it; it fills the sky, beams the victim to dust and leaves.
class KingMothership : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  int ship = -1;
  vec3 shipPos{0};
  float shipYaw = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    const float S = 5.5f, low = 3.4f;
    Puppet* p = w.puppet(ship);
    if (p) {
      shipYaw += dt * 0.35f;
      p->yaw = shipYaw;
      p->pos = shipPos;
    }
    vec3 belly = shipPos + vec3(0, (0.1f - 0.09f) * S, 0);
    switch (phase) {
      case 0:
        shotExtra = 2.6f;
        shotHeight = 1.3f;
        if (faceOff(w)) next();
        break;
      case 1: {  // sceptre to the sky
        float u = easeInOut(pt / 0.5f);
        a.ctl.armR = vec3(2.5f, 0.2f, -0.6f) * u;
        a.ctl.headPitch = -0.35f * u;
        a.glow = glm::vec4(armyLight(a.color), 0.5f * u);
        if (pt > 0.5f) {
          ship = w.addPuppet(al::AL_MOTHERSHIP, a.color, 2);
          shipPos = vec3(V.x, 12.0f, V.z);
          if (Puppet* q = w.puppet(ship)) {
            q->scale = S;
            q->pos = shipPos;
            q->yaw = shipYaw;
          }
          w.sound(SFX_DRONE, 1.0f, V);
          if (w.cinematic) {
            // from low down, looking up at the sky - towards the dark side of the room,
            // away from the studio lights - with nothing standing in the way
            vec3 mid = (A + V) * 0.5f;
            CamPose s;
            float best = 1e9f;
            vec3 aim = mid + vec3(0, 1.8f, 0);
            for (int k = -2; k <= 2; ++k) {
              for (int e = 0; e < 6; ++e) {
                float ang = 2.226f + 0.157f * (float)k;  // between the fill panel and the blue one
                vec3 look(std::cos(ang), 0, std::sin(ang));
                vec3 cam = mid - look * (3.6f + 0.8f * (float)(e % 3)) + vec3(0, e < 3 ? 1.1f : 1.7f, 0);
                vec3 view = glm::normalize(aim - cam);
                float score = std::fabs(glm::dot(look, dir)) + 0.1f * (float)std::abs(k) + 0.15f * (float)e;
                for (const Actor& o : w.actors) {
                  if (!o.alive || !o.visible || o.id == plan.mover || o.id == plan.victim) continue;
                  float r = o.radius() + 0.15f;
                  vec3 c = o.pos + vec3(0, o.height() * 0.5f, 0);
                  float d = glm::length(c - cam);
                  if (glm::length(flat(o.pos - cam)) < r + 0.6f) score += 10.0f;
                  // pieces standing in the way, and big ones looming in the foreground
                  if (segSegDist(cam, mid + vec3(0, 0.4f, 0), o.pos, o.pos + vec3(0, o.height(), 0)) < r) score += 3.0f;
                  if (d < 3.2f && glm::dot((c - cam) / d, view) > 0.75f) score += 2.5f * o.height() / d;
                }
                if (score < best) {
                  best = score;
                  s.pos = cam;
                }
              }
            }
            s.target = aim;
            s.fov = 46.0f;
            s.focus = glm::length(s.pos - V);
            s.aperture = 4.0f;
            w.cam.setShot(s, 1.6f);
          }
          next();
        }
        break;
      }
      case 2: {  // it descends, filling the sky
        float u = glm::clamp(pt / 2.2f, 0.0f, 1.0f);
        shipPos.y = glm::mix(12.0f, low, easeOut(u));
        w.shake(0.04f);
        v.tremble = 0.5f;
        v.idleWeight = 0;
        if (v.set == 2) v.ctl.headPitch = -0.6f * u;  // looking up at it
        w.frameLights.push_back({belly, 6.0f, armyLight(a.color) * 5.0f * u});
        if (u >= 1.0f) {
          w.sound(SFX_BEAM, 1.0f, V, 0.7f);
          w.sound(SFX_THEREMIN, 0.6f, V, 0.8f);
          next();
        }
        break;
      }
      case 3: {  // the great beam
        float u = glm::clamp(pt / 1.8f, 0.0f, 1.0f);
        tractorBeam(w, belly, V, 0.4f, armyLight(a.color), dt, 2.0f);
        v.pos.y = 0.4f * easeIn(u);
        v.glow = glm::vec4(armyLight(a.color), 2.0f * u);
        v.dissolveColor = armyLight(a.color) * 2.0f;
        v.dissolve = glm::clamp((u - 0.35f) / 0.65f, 0.0f, 1.0f);
        v.tremble = 1.0f;
        if (w.fx.rnd() < 0.6f) stream(w, V + vec3(0, v.pos.y + v.height() * 0.5f, 0), belly, armyLight(a.color), 2, 0.5f);
        if (u >= 1.0f) {
          v.alive = false;
          v.visible = false;
          w.fx.flash(V + vec3(0, 0.5f, 0), 1.0f, armyLight(a.color) * 4.0f, 0.2f);
          next();
        }
        break;
      }
      case 4: {  // and away it goes
        float u = glm::clamp(pt / 1.4f, 0.0f, 1.0f);
        shipPos.y = glm::mix(low, 14.0f, easeIn(u));
        if (p) p->dissolve = glm::clamp((u - 0.6f) / 0.4f, 0.0f, 1.0f);
        a.ctl.armR = vec3(2.5f, 0.2f, -0.6f) * (1.0f - easeInOut(u));
        a.ctl.headPitch = -0.35f * (1.0f - u);
        a.glow = glm::vec4(armyLight(a.color), 0.5f * (1.0f - u));
        if (pt <= dt + 1e-6f) w.sound(SFX_WHOOSH, 0.8f, V, 0.4f);
        if (u >= 1.0f) {
          if (p) p->alive = false;
          next();
        }
        break;
      }
      case 5:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 6:
        return finishA(w);
    }
    return false;
  }
};

// Disintegrator: it sputters; he gives it a tap... and it fires far too hard,
// blowing the victim apart and his throne backwards.
class KingDisintegrator : public ABattle {
 public:
  using ABattle::ABattle;

 protected:
  vec3 from{0};
  float kickBack = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // aim
        float u = easeInOut(pt / 0.45f);
        pointSceptre(a, u);
        if (pt > 0.5f) {
          w.sound(SFX_SPUTTER, 0.9f, a.pos);
          next();
        }
        break;
      }
      case 2: {  // it sputters
        pointSceptre(a, 1.0f);
        vec3 m = muzzleOf(a);
        if (w.fx.rnd() < dt * 9.0f) {
          w.fx.sparks(m, vec3(0, 1, 0), 6, 1.2f, 1.0f, vec3(3.0f, 2.0f, 1.0f));
          if (w.fx.rnd() < 0.5f) w.fx.bolt(m, m + flatDir(m, V) * w.fx.rnd(0.1f, 0.3f), armyLight(a.color) * 2.0f, 0.02f, 3);
        }
        if (w.fx.rnd() < dt * 4.0f) w.fx.smoke(m, 1, 0.01f, 0.2f, 0.7f);
        a.ctl.headPitch = 0.25f * std::min(1.0f, pt / 0.4f);
        a.ctl.headYaw = -0.35f * std::min(1.0f, pt / 0.4f);
        if (pt > 1.3f) next();
        break;
      }
      case 3: {  // tap, tap, tap
        pointSceptre(a, 1.0f);
        float tap = std::max(0.0f, std::sin(pt * 14.0f));
        a.ctl.armL = vec3(1.0f + 0.25f * tap, -0.6f, 0.9f);
        if (std::fmod(pt + 0.06f, 0.449f) < dt && pt < 1.2f) w.sound(SFX_CLACK, 0.5f, a.pos, 1.8f);
        if (pt > 0.3f && pt - dt <= 0.3f) w.sound(SFX_GIBBER, 0.5f, a.pos, 0.7f);  // mutters
        if (pt > 1.35f) {
          a.ctl.armL = vec3(0);
          next();
        }
        break;
      }
      case 4: {  // aim again
        float u = easeInOut(pt / 0.35f);
        pointSceptre(a, 1.0f);
        a.ctl.headPitch = 0.25f * (1.0f - u);
        a.ctl.headYaw = -0.35f * (1.0f - u);
        a.ctl.lean = 0.05f * u;
        if (pt > 0.45f) {
          from = flat(a.pos);
          w.sound(SFX_HEATRAY, 1.0f, a.pos, 0.7f);
          w.sound(SFX_EXPLODE, 0.8f, a.pos, 1.3f);
          next();
        }
        break;
      }
      case 5: {  // KABOOM - and the recoil
        float u = glm::clamp(pt / 0.55f, 0.0f, 1.0f);
        pointSceptre(a, 1.0f);
        vec3 m = muzzleOf(a), c = victimCenter(w);
        if (u < 0.7f) beamLine(w, m, v.alive ? c : V + vec3(0, 0.4f, 0), kRoyal, 0.03f, 3.0f);
        if (v.alive && pt > 0.06f) {
          w.fx.fireball(c, 1.4f);
          w.fx.flash(c, 1.2f, vec3(8.0f, 6.0f, 3.0f), 0.2f);
          w.shatter(v, c, dir * 3.0f + vec3(0, 1.0f, 0), 3.0f, 1.2f, 12.0f);
          w.shake(0.7f);
          w.slowmo(0.35f, 0.5f);
          w.addFlash(vec3(0.3f, 0.25f, 0.15f));
        }
        kickBack = easeOut(u) * 0.55f;
        vec3 p = from - dir * kickBack;
        a.pos = vec3(p.x, 0, p.z);
        a.ctl.hover = 0.1f * std::sin(u * kPI);
        a.ctl.lean = -0.25f * std::sin(u * kPI);
        a.yawTarget = faceAngle(V - from) + 0.5f * std::sin(u * kPI * 2.0f);
        a.ctl.perk = -1.0f;
        if (u >= 1.0f) next();
        break;
      }
      case 6: {  // compose himself
        float u = glm::clamp(pt / 0.8f, 0.0f, 1.0f);
        pointSceptre(a, 1.0f - easeInOut(u));
        a.ctl.lean = 0;
        a.ctl.headPitch = 0.15f * std::sin(u * kPI * 3.0f) * (1.0f - u);
        a.ctl.perk = -1.0f + u;
        if (u >= 1.0f) next();
        break;
      }
      case 7:
        if (moveInA(w, dt, 1.0f)) next();
        break;
      case 8:
        return finishA(w);
    }
    return false;
  }
};

const char* const kAlienStyleNames[meshgen::K_COUNT][kBattleStyles] = {
    {"Ray gun", "Goo grenade", "Tongue"},
    {"Abduction", "Crop circle", "Cow drop"},
    {"Mind blast", "Hypnosis", "Telekinetic crush"},
    {"Heat ray", "Stomp", "Tentacle toss"},
    {"Acid spit", "Brood", "Tail strike"},
    {"Shrink ray", "Mothership", "Disintegrator"},
};

}  // namespace

const char* alienBattleStyleName(int kind, int style) {
  if (kind < 0 || kind >= meshgen::K_COUNT || style < 0 || style >= kBattleStyles) return "";
  return kAlienStyleNames[kind][style];
}

std::unique_ptr<Anim> makeAlienBattle(int kind, int style, const MovePlan& plan) {
  switch (kind * kBattleStyles + style) {
    case K_PAWN * kBattleStyles + 0: return std::make_unique<PawnRayGun>(plan);
    case K_PAWN * kBattleStyles + 1: return std::make_unique<PawnGoo>(plan);
    case K_PAWN * kBattleStyles + 2: return std::make_unique<PawnTongue>(plan);
    case K_KNIGHT * kBattleStyles + 0: return std::make_unique<KnightAbduction>(plan);
    case K_KNIGHT * kBattleStyles + 1: return std::make_unique<KnightCropCircle>(plan);
    case K_KNIGHT * kBattleStyles + 2: return std::make_unique<KnightCowDrop>(plan);
    case K_BISHOP * kBattleStyles + 0: return std::make_unique<BishopMindBlast>(plan);
    case K_BISHOP * kBattleStyles + 1: return std::make_unique<BishopHypnosis>(plan);
    case K_BISHOP * kBattleStyles + 2: return std::make_unique<BishopCrush>(plan);
    case K_ROOK * kBattleStyles + 0: return std::make_unique<RookHeatRay>(plan);
    case K_ROOK * kBattleStyles + 1: return std::make_unique<RookStompA>(plan);
    case K_ROOK * kBattleStyles + 2: return std::make_unique<RookToss>(plan);
    case K_QUEEN * kBattleStyles + 0: return std::make_unique<QueenAcid>(plan);
    case K_QUEEN * kBattleStyles + 1: return std::make_unique<QueenBrood>(plan);
    case K_QUEEN * kBattleStyles + 2: return std::make_unique<QueenTail>(plan);
    case K_KING * kBattleStyles + 0: return std::make_unique<KingShrink>(plan);
    case K_KING * kBattleStyles + 1: return std::make_unique<KingMothership>(plan);
    default: return std::make_unique<KingDisintegrator>(plan);
  }
}

std::unique_ptr<Anim> makeAlienQuiet(const MovePlan& plan) { return std::make_unique<AQuietMove>(plan); }
std::unique_ptr<Anim> makeAlienQuickCapture(const MovePlan& plan) { return std::make_unique<AQuickCapture>(plan); }
std::unique_ptr<Anim> makeAlienCheckmate(int kingId, int winnerColor) {
  return std::make_unique<ACheckmate>(kingId, winnerColor);
}
