// Choreography for the medieval army. Men-at-arms march, knights canter and
// leap, thrones and the gatehouse glide over the board. Captures:
//
//   Pawn    spear thrust, shield bash, javelin
//   Knight  joust (the lance shatters), trample (the horse rears and crashes
//           down), sword sweep (the rider cuts the victim in two in passing)
//   Bishop  wrath of heaven (the organ plays the Dies irae, a storm gathers
//           over the victim and lightning strikes it), brimstone (burning
//           stones rain from a red cloud), pillar of salt (a bell tolls and
//           the victim turns to salt and crumbles)
//   Rook    siege stomp (the gatehouse stands up on stone legs, walks over and
//           stamps on the victim), dragon fire and dragon's jaws (the
//           drawbridge drops and a dragon crawls out)
//   Queen   throne charge, sceptre smite, sceptre sweep
//   King    royal cleave, throne ram, sweeping blow
//
// Kings and queens fight on foot: they get up from their thrones, walk to the
// victim, strike, and the empty throne glides after them so they can sit back down.
#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

#include "battle.h"
#include "battle_kit.h"

namespace md = medieval;

namespace {
using namespace bkit;

const float kTau = 6.2831853f;
const vec3 kGold(1.0f, 0.82f, 0.45f);

float approach(float x, float target, float rate) {
  return x < target ? std::min(target, x + rate) : std::max(target, x - rate);
}
float approachAngle(float a, float target, float rate) {
  float d = wrapAngle(target - a);
  return wrapAngle(a + glm::clamp(d, -rate, rate));
}
// world position of a point that rides on a bone (rest-pose model coordinates)
vec3 bonePoint(const Actor& a, int bone, const vec3& rest) {
  return vec3(a.model() * md::boneMatrix(a.pose, bone) * glm::vec4(rest, 1.0f));
}
vec3 puppetPoint(const Puppet& p, int bone, const vec3& rest) {
  return vec3(p.model() * md::boneMatrix(p.pose, bone) * glm::vec4(rest, 1.0f));
}
// where a standing royal's body is (the actor's origin stays where the throne was)
vec3 bodyPos(const Actor& a) { return a.pos + a.forward() * md::standOffset(a.kind) * (1.0f - a.ctl.sit) * a.scale; }

Particle spark(const vec3& p, const vec3& v, const vec3& col, float size, float life) {
  Particle q;
  q.pos = p;
  q.vel = v;
  q.size = size;
  q.color = col;
  q.colorEnd = col * 0.2f;
  q.opacity = 0;
  q.maxLife = life;
  q.type = PT_SPARK;
  q.stretch = 0.03f;
  q.drag = 1.0f;
  q.gravity = 6.0f;
  q.bounce = true;
  q.fadeIn = 0.0f;
  return q;
}

// wooden splinters (a lance shattering)
void splinters(World& w, const vec3& at, const vec3& dir, int n) {
  for (int i = 0; i < n; ++i) {
    vec3 d = glm::normalize(dir + w.fx.rndDir() * 0.9f + vec3(0, 0.4f, 0));
    Particle p = spark(at, d * w.fx.rnd(1.5f, 4.5f), vec3(0.45f, 0.3f, 0.16f) * w.fx.rnd(0.6f, 1.2f), w.fx.rnd(0.006f, 0.012f),
                       w.fx.rnd(0.8f, 1.6f));
    p.colorEnd = p.color;
    p.opacity = 1.0f;
    p.stretch = 0.02f;
    p.gravity = 9.8f;
    p.drag = 0.6f;
    w.fx.add(p);
  }
}

// a jagged lightning bolt from 'a' to 'b' with side branches, a flash and thunder
void lightning(World& w, const vec3& a, const vec3& b, float power) {
  const vec3 col(2.2f, 2.6f, 4.0f);
  vec3 prev = a;
  int segs = 9;
  for (int i = 1; i <= segs; ++i) {
    float t = (float)i / segs;
    vec3 p = glm::mix(a, b, t);
    if (i < segs) p += vec3(w.fx.rnd(-1, 1), 0, w.fx.rnd(-1, 1)) * 0.09f * power;
    w.fx.bolt(prev, p, col * (2.0f + power), 0.025f, 4);
    if (i < segs - 1 && w.fx.rnd() < 0.45f) {
      vec3 br = p + vec3(w.fx.rnd(-1, 1), w.fx.rnd(-0.6f, -0.1f), w.fx.rnd(-1, 1)) * 0.35f;
      w.fx.bolt(p, br, col * 1.2f, 0.03f, 5);
    }
    prev = p;
  }
  w.fx.flash(b, 0.7f * power, vec3(5.0f, 6.0f, 9.0f), 0.12f);
  w.light(b + vec3(0, 0.5f, 0), vec3(30.0f, 36.0f, 55.0f) * power, 6.0f, 0.3f);
  w.light((a + b) * 0.5f, vec3(20.0f, 24.0f, 40.0f) * power, 6.0f, 0.18f);
  w.addFlash(vec3(0.25f, 0.28f, 0.35f) * power);
}

// a dark storm cloud: billowing smoke that slowly turns about its centre
void cloudPuffs(World& w, const vec3& c, float radius, float rate, float dt, const vec3& tint, float glowAmt) {
  float n = rate * dt;
  int count = (int)n + (w.fx.rnd() < n - std::floor(n) ? 1 : 0);
  for (int i = 0; i < count; ++i) {
    Particle p;
    float a = w.fx.rnd(0, kTau);
    float r = radius * std::sqrt(w.fx.rnd());
    vec3 d(std::cos(a), 0, std::sin(a));
    // a flat-bottomed, billowing cloud: dark underneath, paler where the tops catch the light
    float h = w.fx.rnd(-0.1f, 0.22f) * (1.0f - 0.6f * r / std::max(radius, 1e-3f));
    p.pos = c + d * r + vec3(0, h * radius / 0.6f, 0);
    p.vel = vec3(-d.z, 0, d.x) * w.fx.rnd(0.12f, 0.3f) + d * w.fx.rnd(-0.02f, 0.06f) + vec3(0, w.fx.rnd(-0.01f, 0.06f), 0);
    p.size = w.fx.rnd(0.14f, 0.34f) * radius / 0.6f;
    p.grow = w.fx.rnd(0.04f, 0.12f);
    float lit = glm::clamp((h + 0.1f) / 0.32f, 0.0f, 1.0f);
    p.color = tint * w.fx.rnd(0.6f, 1.0f) * (0.5f + 2.6f * lit * lit);
    p.colorEnd = p.color * 0.8f;
    p.opacity = w.fx.rnd(0.55f, 0.85f);
    p.maxLife = w.fx.rnd(2.2f, 3.4f);
    p.type = PT_SMOKE;
    p.drag = 0.6f;
    p.rot = w.fx.rnd(0, 6.28f);
    p.rotVel = w.fx.rnd(-0.3f, 0.3f);
    p.seed = w.fx.rnd();
    p.fadeIn = 0.5f;
    w.fx.add(p);
  }
  if (glowAmt > 0 && w.fx.rnd() < 0.3f) {
    Particle g;
    g.pos = c + w.fx.rndDir() * radius * 0.5f;
    g.vel = vec3(0);
    g.size = radius * w.fx.rnd(0.5f, 0.9f);
    g.color = vec3(1.6f, 0.5f, 0.12f) * glowAmt;
    g.colorEnd = g.color * 0.2f;
    g.opacity = 0;
    g.maxLife = 0.4f;
    g.type = PT_GLOW;
    g.fadeIn = 0.1f;
    w.fx.add(g);
  }
}

// rain streaking down from a cloud onto the board, with tiny splashes
void rain(World& w, const vec3& c, float radius, float rate, float dt) {
  float n = rate * dt;
  int count = (int)n + (w.fx.rnd() < n - std::floor(n) ? 1 : 0);
  for (int i = 0; i < count; ++i) {
    float a = w.fx.rnd(0, kTau), r = radius * std::sqrt(w.fx.rnd());
    Particle p;
    p.pos = c + vec3(std::cos(a) * r, -0.1f, std::sin(a) * r);
    p.vel = vec3(0.3f, -6.5f, 0.1f) * w.fx.rnd(0.85f, 1.15f);
    p.size = 0.004f;
    p.color = vec3(0.35f, 0.4f, 0.5f) * w.fx.rnd(0.6f, 1.2f);
    p.colorEnd = p.color;
    p.opacity = 0;
    p.maxLife = (c.y - 0.1f) / 6.5f;
    p.type = PT_SPARK;
    p.stretch = 0.012f;
    p.drag = 0.0f;
    p.fadeIn = 0.02f;
    w.fx.add(p);
    if (w.fx.rnd() < 0.3f) {
      Particle s = p;
      s.pos = vec3(p.pos.x, 0.01f, p.pos.z);
      s.vel = vec3(w.fx.rnd(-0.3f, 0.3f), w.fx.rnd(0.3f, 0.6f), w.fx.rnd(-0.3f, 0.3f));
      s.maxLife = 0.15f;
      s.gravity = 6.0f;
      s.stretch = 0.004f;
      w.fx.add(s);
    }
  }
}

// a jet of dragon fire from the mouth along 'dir'
void fireJet(World& w, const vec3& mouth, const vec3& dir, float len, float dt) {
  int n = std::max(1, (int)(dt * 260.0f));
  for (int i = 0; i < n; ++i) {
    Particle p;
    vec3 d = glm::normalize(dir + w.fx.rndDir() * 0.13f);
    p.pos = mouth + d * w.fx.rnd(0.0f, 0.05f);
    p.vel = d * w.fx.rnd(2.6f, 3.6f) * len;
    p.size = w.fx.rnd(0.03f, 0.06f);
    p.grow = w.fx.rnd(0.35f, 0.6f);
    p.color = vec3(7.0f, 3.6f, 1.1f) * w.fx.rnd(0.7f, 1.3f);
    p.colorEnd = vec3(0.7f, 0.1f, 0.02f);
    p.opacity = 0;
    p.maxLife = w.fx.rnd(0.3f, 0.45f);
    p.type = PT_SMOKE;
    p.drag = 2.2f;
    p.gravity = -0.8f;
    p.rot = w.fx.rnd(0, 6.28f);
    p.rotVel = w.fx.rnd(-2.0f, 2.0f);
    p.seed = w.fx.rnd();
    p.fadeIn = 0.02f;
    w.fx.add(p);
  }
  if (w.fx.rnd() < 0.5f) w.fx.embers(mouth + dir * 0.3f, 2, 1.5f);
  if (w.fx.rnd() < 0.25f) w.fx.smoke(mouth + dir * len * 0.9f + vec3(0, 0.2f, 0), 1, 0.1f, 0.6f, 0.8f);
}

// ---------------------------------------------------------------------------
// Walker: moves a piece from where it stands to 'to' - on foot, on horseback,
// on stone legs, or gliding (thrones and the gatehouse).
// ---------------------------------------------------------------------------
struct Walker {
  enum Mode { FOOT, HORSE, CASTLE, SLIDE };
  int actor = -1;
  vec3 from{0}, to{0};
  Mode mode = FOOT;
  float speed = 0.8f, accel = 1.6f, cycle = 0.3f, run = 0;
  float s = 0, v = 0, len = 0, turnWait = 0, trail = 0;
  int lastStep = 0;
  bool begun = false, face = true, soundOn = true;

  Walker() {}
  Walker(int a, const vec3& dest, Mode m, float spd) : actor(a), to(flat(dest)), mode(m), speed(spd) {}

  void begin(World& w) {
    Actor& a = w.actors[actor];
    from = flat(a.pos);
    len = glm::length(to - from);
    cycle = md::strideCycle(a.kind);
    accel = mode == SLIDE ? speed * 2.2f : speed * 2.0f;
    if (face && len > 0.05f) {
      a.yawTarget = faceAngle(to - from);
      turnWait = std::min(0.55f, std::fabs(wrapAngle(a.yawTarget - a.yaw)) * 0.3f);
    }
    lastStep = (int)std::floor(a.ctl.gait / kPI);
    begun = true;
    if (mode == SLIDE && soundOn && len > 0.05f) {
      if (a.kind == K_ROOK) w.sound(SFX_GRIND, 0.75f, a.pos, w.fx.rnd(0.9f, 1.05f));
      else w.sound(SFX_SLIDE, 0.6f, a.pos, a.kind == K_KING ? 0.85f : w.fx.rnd(0.95f, 1.1f));
    }
  }

  void footstep(World& w, Actor& a) {
    if (!soundOn) return;
    vec3 p = flat(a.pos);
    switch (mode) {
      case FOOT:
        if (a.kind == K_PAWN) w.sound(SFX_ARMOR, 0.45f, p, w.fx.rnd(0.9f, 1.15f));
        else w.sound(SFX_STEP, 0.4f, p, w.fx.rnd(0.85f, 0.95f));
        break;
      case HORSE:
        w.sound(SFX_HOOF, 0.5f + 0.3f * run, p, w.fx.rnd(0.9f, 1.1f));
        if (run > 0.5f) w.fx.dust(p, 2, 0.15f, 0.6f);
        break;
      case CASTLE:
        w.sound(SFX_STOMP, 0.8f, p, w.fx.rnd(0.9f, 1.1f));
        w.fx.dust(p + a.forward() * 0.05f, 4, 0.22f, 0.7f);
        w.shake(0.14f);
        break;
      default: break;
    }
  }

  bool update(World& w, float dt) {
    if (!begun) begin(w);
    Actor& a = w.actors[actor];
    if (turnWait > 0) {
      turnWait -= dt;
      if (mode == FOOT || mode == HORSE) {  // shuffle round on the spot
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
    if (mode == SLIDE) {
      trail -= dt;
      if (trail <= 0 && v > 0.2f) {
        trail = 0.06f;
        w.fx.dust(p, a.kind == K_ROOK ? 3 : 1, a.radius() * 0.8f, 0.25f,
                  a.kind == K_ROOK ? vec3(0.55f, 0.5f, 0.45f) : vec3(0.5f, 0.45f, 0.4f));
      }
    } else {
      a.ctl.gait += v * dt / cycle * kTau;
      a.ctl.stride = glm::clamp(v / std::max(0.1f, speed * 0.6f), 0.0f, 1.0f);
      if (mode == HORSE) a.ctl.run = run;
      int step = (int)std::floor(a.ctl.gait / kPI);
      if (step != lastStep) {
        lastStep = step;
        footstep(w, a);
      }
    }
    if (s >= len - 1e-4f) {
      a.ctl.stride = approach(a.ctl.stride, 0.0f, dt * 4.0f);
      if (mode == HORSE) a.ctl.run = approach(a.ctl.run, 0.0f, dt * 3.0f);
      return a.ctl.stride <= 0.001f;
    }
    return false;
  }
};

// The empty throne glides after its owner, who sits back down and turns to face the enemy.
struct ThroneReturn {
  int st = 0;
  float t = 0;
  vec3 seatFrom{0}, dest{0};
  float yawFrom = 0;
  Walker walk;
  bool update(World& w, Actor& a, const vec3& V, float dt) {
    t += dt;
    switch (st) {
      case 0:  // walk onto the square (if not there yet)
        if (t <= dt + 1e-6f) walk = Walker(a.id, V, Walker::FOOT, 0.7f);
        if (walk.update(w, dt)) {
          st = 1;
          t = 0;
          seatFrom = a.seatPos;
          yawFrom = a.seatYaw;
          dest = V;
          if (glm::length(flat(seatFrom - dest)) > 0.05f) w.sound(SFX_SLIDE, 0.7f, seatFrom, 0.9f);
        }
        break;
      case 1: {  // the throne follows
        float d = glm::length(flat(dest - seatFrom));
        float dur = glm::clamp(d * 0.5f, 0.35f, 1.4f);
        float u = glm::clamp(t / dur, 0.0f, 1.0f);
        a.seatPos = glm::mix(seatFrom, dest, easeInOut(u));
        a.seatPos.y = 0;
        a.seatYaw = yawFrom + wrapAngle(a.yaw - yawFrom) * easeInOut(u);
        if (w.fx.rnd() < 0.5f && u < 1.0f) w.fx.dust(a.seatPos, 1, 0.25f, 0.2f);
        if (u >= 1.0f) {
          st = 2;
          t = 0;
        }
        break;
      }
      case 2:  // and sits down
        a.ctl.sit = easeInOut(t / 0.75f);
        a.ctl.stride = 0;
        if (t >= 0.75f) {
          a.ctl.sit = 1;
          a.seatFree = false;
          a.yawTarget = a.baseYaw;
          a.yawRate = 3.5f;
          w.sound(SFX_THUD, 0.35f, a.pos, 1.3f);
          st = 3;
          t = 0;
        }
        break;
      case 3:  // swivel round to face the enemy again
        a.yawRate = 3.5f;
        return t > 0.7f;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Quiet moves (and castling)
// ---------------------------------------------------------------------------
class MQuietMove : public Staged {
 public:
  MQuietMove(const MovePlan& p) : plan(p) {}

 protected:
  MovePlan plan;
  Walker walker, rookWalker;
  vec3 A{0}, B{0};
  float dur = 1;
  bool slideSound = false;

  bool knightLeap(World& w, Actor& a, float dt) {
    // canter on the spot, gather, leap over whatever is in the way, land
    switch (phase) {
      case 0:
        if (enter()) {
          A = flat(a.pos);
          B = squareCenter(plan.move.to);
          a.yawTarget = faceAngle(B - A);
          w.sound(SFX_HOOF, 0.4f, A, 1.0f);
        }
        a.ctl.gait += dt * 7.0f;
        a.ctl.stride = approach(a.ctl.stride, 0.5f, dt * 2.0f);
        a.ctl.run = 0.4f;
        if (pt > 0.45f) next();
        break;
      case 1: {  // gather
        float u = glm::clamp(pt / 0.22f, 0.0f, 1.0f);
        a.ctl.stride = approach(a.ctl.stride, 0.0f, dt * 4.0f);
        a.ctl.rear = 0.25f * std::sin(u * kPI * 0.5f);
        a.ctl.crouch = u;
        if (u >= 1.0f) {
          w.sound(SFX_WHOOSH, 0.5f, a.pos, 0.7f);
          w.fx.dust(a.pos, 8, 0.3f, 0.9f);
          dur = 0.75f + 0.08f * glm::length(B - A);
          next();
        }
        break;
      }
      case 2: {  // the leap
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        vec3 p = glm::mix(A, B, easeInOut(u) * 0.8f + u * 0.2f);
        p.y = 4.0f * u * (1.0f - u) * 0.62f;
        a.pos = p;
        a.ctl.rear = 0.25f * (1.0f - u);
        a.ctl.tuck = std::sin(u * kPI);
        a.ctl.crouch = 0;
        if (u >= 1.0f) {
          a.pos = B;
          a.ctl.tuck = 0;
          w.sound(SFX_HOOF, 0.9f, B, 0.85f);
          w.sound(SFX_THUD, 0.7f, B, 1.2f);
          w.fx.dust(B, 12, 0.35f, 1.1f);
          w.shake(0.1f);
          next();
        }
        break;
      }
      case 3:  // settle, a shake of the head
        if (enter()) a.yawTarget = a.baseYaw;
        a.ctl.headPitch = -0.25f * std::sin(glm::clamp(pt / 0.5f, 0.0f, 1.0f) * kPI);
        a.ctl.gait += dt * 5.0f;
        a.ctl.stride = 0.25f * (1.0f - glm::clamp(pt / 0.5f, 0.0f, 1.0f));
        if (pt > 0.55f) {
          a.ctl.headPitch = 0;
          a.ctl.stride = 0;
          return true;
        }
        break;
    }
    return false;
  }

  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    if (a.kind == K_KNIGHT) return knightLeap(w, a, dt);
    switch (phase) {
      case 0:
        if (enter()) {
          Walker::Mode m = Walker::SLIDE;
          float spd = 1.1f;
          if (a.kind == K_PAWN) {
            m = Walker::FOOT;
            spd = 0.8f;
          } else if (a.kind == K_ROOK) {
            spd = 0.9f;
          } else if (a.kind == K_KING) {
            spd = 0.8f;
          } else if (a.kind == K_BISHOP) {
            spd = 1.2f;
          }
          float dist = glm::length(flat(squareCenter(plan.move.to) - a.pos));
          if (m == Walker::SLIDE) spd = std::max(spd, dist / 1.5f);  // long glides go faster
          walker = Walker(plan.mover, squareCenter(plan.move.to), m, spd);
          if (m == Walker::SLIDE) walker.face = false;
          A = flat(a.pos);
          B = squareCenter(plan.move.to);
          if (plan.rook >= 0) {
            rookWalker = Walker(plan.rook, squareCenter(plan.rookTo), Walker::SLIDE, 0.9f);
            rookWalker.face = false;
          }
        }
        {
          bool d1;
          if (plan.rook >= 0) {
            // castling: the king's throne swings out in an arc while the gatehouse slides past
            float u = glm::clamp(pt / 1.6f, 0.0f, 1.0f), e = easeInOut(u);
            vec3 toCenter = vec3(0, 0, a.color == 0 ? -1.0f : 1.0f);
            vec3 W = (A + B) * 0.5f + toCenter * 0.8f;
            vec3 p = (1 - e) * (1 - e) * A + 2 * (1 - e) * e * W + e * e * B;
            a.pos = vec3(p.x, 0, p.z);
            if (!slideSound) {
              slideSound = true;
              w.sound(SFX_SLIDE, 0.6f, a.pos, 0.85f);
            }
            d1 = u >= 1.0f;
          } else {
            d1 = walker.update(w, dt);
          }
          bool d2 = true;
          if (plan.rook >= 0) d2 = pt > 0.3f ? rookWalker.update(w, dt) : false;
          // a little life on the way: royals sway, bishops raise the crozier
          if (md::throned(a.kind)) a.ctl.lean = -0.06f * std::sin(glm::clamp(pt / 1.0f, 0.0f, 1.0f) * kPI);
          if (d1 && d2) next();
        }
        break;
      case 1:
        if (enter()) {
          a.yawTarget = a.baseYaw;
          a.ctl.lean = 0;
          if (plan.rook >= 0) w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw;
        }
        if (pt > 0.25f) return true;
        break;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Shared prelude and epilogue for medieval battles
// ---------------------------------------------------------------------------
class MBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  Walker walker;
  ThroneReturn ret;
  vec3 standAt{0};

  // walk / glide / gallop onto the target square
  bool moveInM(World& w, float dt, Walker::Mode m, float speed) {
    if (enter()) {
      walker = Walker(plan.mover, squareCenter(plan.move.to), m, speed);
      if (m == Walker::SLIDE) walker.face = false;
    }
    return walker.update(w, dt);
  }
  // a royal gets up from its throne (which stays where it is)
  bool standUp(World& w, float dur = 0.8f) {
    Actor& a = w.actors[plan.mover];
    if (enter()) {
      a.seatFree = true;
      a.seatPos = a.pos;
      a.seatYaw = a.yaw;
      w.sound(SFX_SLIDE, 0.3f, a.pos, 1.5f);
    }
    a.ctl.sit = 1.0f - easeInOut(pt / dur);
    return pt >= dur;
  }
  bool finishM(World& w) {
    Actor& a = w.actors[plan.mover];
    bool done = finish(w);
    if (done) {
      a.pos = squareCenter(plan.move.to);
      a.resetControls();
      a.heat = 0;
    }
    return done;
  }
};

// ===========================================================================
// PAWN - the man-at-arms
// ===========================================================================
// Spear thrust: march up, level the spear, lunge.
class PawnThrust : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  vec3 start{0};
  bool hit = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1:  // march within a spear's reach
        if (enter()) walker = Walker(plan.mover, V - dir * std::min(L, 0.98f), Walker::FOOT, 0.8f);
        v.tremble = 0.7f;
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // lower the spear
        if (enter()) a.yawTarget = faceAngle(V - a.pos);
        float u = easeInOut(pt / 0.45f);
        a.ctl.itemPitch = 1.5f * u;
        a.ctl.armR = vec3(0.45f * u, 0, 0);
        a.ctl.lean = -0.08f * u;
        a.ctl.crouch = 0.25f * u;
        v.tremble = 1.0f;
        if (pt > 0.55f) {
          start = flat(a.pos);
          w.sound(SFX_WHOOSH, 0.8f, a.pos, 1.1f);
          next();
        }
        break;
      }
      case 3: {  // lunge!
        float u = glm::clamp(pt / 0.16f, 0.0f, 1.0f);
        a.pos = start + dir * 0.22f * easeOut(u);
        a.ctl.lean = glm::mix(-0.08f, 0.32f, u);
        a.ctl.armR = vec3(glm::mix(0.45f, 0.65f, u), 0, -0.35f * u);
        a.ctl.itemPitch = 1.5f + 0.1f * u;
        a.ctl.gait = kPI * 0.5f;
        a.ctl.stride = 0.6f * u;
        if (u >= 1.0f && !hit) {
          hit = true;
          vec3 tip = victimCenter(w) - dir * v.radius() * 0.6f;
          w.fx.sparks(tip, -dir + vec3(0, 0.3f, 0), 26, 3.5f, 0.6f, vec3(3.0f, 2.6f, 2.0f));
          w.fx.flash(tip, 0.4f, vec3(2.0f, 1.8f, 1.5f), 0.08f);
          w.light(tip, vec3(6.0f, 5.0f, 4.0f), 2.5f, 0.15f);
          w.sound(SFX_CLANG, 0.8f, tip, 1.3f);
          w.shatter(v, tip, dir * 3.2f + vec3(0, 0.6f, 0), 1.2f, 0.7f, 8.0f);
          w.shake(0.35f);
          w.slowmo(0.3f, 0.35f);
          next();
        }
        break;
      }
      case 4: {  // recover, spear upright again
        float u = easeInOut(pt / 0.6f);
        a.ctl.lean = 0.32f * (1.0f - u);
        a.ctl.armR = vec3(0.65f * (1.0f - u), 0, -0.35f * (1.0f - u));
        a.ctl.itemPitch = 1.6f * (1.0f - u);
        a.ctl.stride = 0.6f * (1.0f - u);
        a.ctl.crouch = 0.25f * (1.0f - u);
        if (pt > 0.65f) next();
        break;
      }
      case 5:
        if (moveInM(w, dt, Walker::FOOT, 0.8f)) next();
        break;
      case 6:
        return finishM(w);
    }
    return false;
  }
};

// Shield bash: raise the shield, charge, and knock the victim flying.
class PawnBash : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  vec3 start{0}, stop{0};
  float dur = 0.5f;
  Flight flight;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.6f;
        if (faceOff(w)) next();
        break;
      case 1: {  // shield up, spear back, a battle cry
        float u = easeInOut(pt / 0.5f);
        a.ctl.armL = vec3(0.55f * u, 0.15f * u, -0.3f * u);
        a.ctl.lean = 0.18f * u;
        a.ctl.crouch = 0.3f * u;
        v.tremble = 0.9f;
        if (pt > 0.6f) next();
        break;
      }
      case 2: {  // charge
        if (enter()) {
          start = flat(a.pos);
          stop = V - dir * touchDist(a, v);
          dur = glm::clamp(0.25f + 0.22f * glm::length(stop - start), 0.3f, 0.85f);
          w.sound(SFX_WHOOSH, 0.7f, a.pos, 0.9f);
        }
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        float prevS = glm::length(flat(a.pos) - start);
        a.pos = glm::mix(start, stop, easeIn(u));
        a.ctl.gait += (glm::length(flat(a.pos) - start) - prevS) / 0.3f * kTau;
        a.ctl.stride = 1.0f;
        a.ctl.lean = 0.3f;
        if (w.fx.rnd() < 0.3f) w.fx.dust(flat(a.pos), 1, 0.1f, 0.4f);
        v.tremble = 1.0f;
        if (u >= 1.0f) {
          vec3 hit = V - dir * v.radius() * 0.8f + vec3(0, std::min(a.height(), v.height()) * 0.5f, 0);
          w.fx.sparks(hit, dir + vec3(0, 0.5f, 0), 30, 3.5f, 0.7f, vec3(3.0f, 2.6f, 2.0f));
          w.fx.chips(hit, 20, 2.5f, v.color == 1);
          w.light(hit, vec3(6.0f, 5.0f, 4.0f), 2.5f, 0.15f);
          w.sound(SFX_CLANG, 0.9f, hit, 0.75f);
          w.sound(SFX_THUD, 0.8f, hit, 1.2f);
          w.shake(0.35f);
          w.slowmo(0.3f, 0.35f);
          flight.launch(v, dir * 3.6f + vec3(0, 4.4f, 0), 8.0f);
          next();
        }
        break;
      }
      case 3: {  // the victim flies; the soldier stands his ground
        float u = easeInOut(pt / 0.5f);
        a.ctl.stride = 1.0f - u;
        a.ctl.lean = 0.3f * (1.0f - u);
        a.ctl.armL = vec3(0.55f, 0.15f, -0.3f) * (1.0f - u);
        a.ctl.crouch = 0.3f * (1.0f - u);
        if (v.alive && flight.update(v, dt)) crashLanding(w, v, dir);
        if (!v.alive && pt > 0.55f) next();
        break;
      }
      case 4:
        if (moveInM(w, dt, Walker::FOOT, 0.8f)) next();
        break;
      case 5:
        return finishM(w);
    }
    return false;
  }
};

// Javelin: hurl the spear from afar, then walk over and pull it out of the rubble.
class PawnJavelin : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  int spear = -1;
  vec3 p0{0}, v0{0};
  float T = 0.6f;
  const float g = 9.8f;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.6f;
        if (faceOff(w)) next();
        break;
      case 1: {  // draw back
        float u = easeInOut(pt / 0.55f);
        a.ctl.armR = vec3(2.3f * u, 0.15f * u, -0.6f * u);
        a.ctl.itemPitch = -0.55f * u;
        a.ctl.twist = 0.55f * u;
        a.ctl.lean = -0.12f * u;
        a.ctl.armL = vec3(0.5f * u, 0, 0);
        v.tremble = 0.8f;
        if (pt > 0.65f) next();
        break;
      }
      case 2: {  // throw
        float u = easeIn(pt / 0.14f);
        a.ctl.armR = vec3(glm::mix(2.3f, 1.4f, u), 0.15f, -0.6f * (1.0f - u));
        a.ctl.twist = glm::mix(0.55f, -0.3f, u);
        a.ctl.lean = glm::mix(-0.12f, 0.25f, u);
        if (pt >= 0.14f) {
          a.ctl.itemR = 0;
          spear = w.addPuppet(md::A_SPEAR, a.color);
          p0 = bonePoint(a, md::HB_ITEMR, md::handR(md::A_PAWN)) + vec3(0, 0.05f, 0);
          vec3 target = victimCenter(w);
          T = 0.32f + 0.07f * L;
          v0 = (target - p0) / T + vec3(0, 0.5f * g * T, 0);
          w.sound(SFX_WHOOSH, 0.9f, p0, 1.25f);
          next();
        }
        break;
      }
      case 3: {  // flight
        float u = easeOut(pt / 0.4f);
        a.ctl.armR = vec3(glm::mix(1.4f, 0.4f, u), 0.15f * (1.0f - u), 0);
        a.ctl.twist = -0.3f * (1.0f - u);
        a.ctl.lean = 0.25f * (1.0f - u);
        a.ctl.armL = vec3(0.5f * (1.0f - u), 0, 0);
        Puppet* s = w.puppet(spear);
        float tt = std::min(pt, T);
        if (s) {
          s->pos = p0 + v0 * tt + vec3(0, -0.5f * g * tt * tt, 0);
          vec3 vel = v0 + vec3(0, -g * tt, 0);
          s->yaw = faceAngle(vel);
          float pitch = std::atan2(glm::length(flat(vel)), vel.y);  // from straight up
          s->tilt = glm::angleAxis(pitch, vec3(1, 0, 0));
          // the spear flies tip first: shift so the shaft trails behind
          s->pos -= glm::normalize(vel) * 0.55f;
        }
        v.tremble = 1.0f;
        if (pt >= T) {
          vec3 c = victimCenter(w);
          vec3 vb = glm::normalize(v0 + vec3(0, -g * T, 0));
          w.fx.sparks(c, -vb, 24, 3.0f, 0.6f, vec3(3.0f, 2.6f, 2.0f));
          w.sound(SFX_CLACK, 1.0f, c, 0.8f);
          w.shatter(v, c - vb * 0.1f, vb * 3.0f + vec3(0, 0.8f, 0), 1.5f, 0.9f, 9.0f);
          w.shake(0.35f);
          w.slowmo(0.3f, 0.4f);
          // the spear sticks in the board, quivering
          if (s) {
            s->pos = flat(V) + vec3(0, -0.12f, 0) - vb * 0.2f;
            s->pos = flat(V) - glm::normalize(flat(vb) + vec3(1e-4f)) * 0.0f;
            s->tilt = glm::angleAxis(0.5f, vec3(1, 0, 0));
            s->yaw = faceAngle(vb);
            s->pos.y = -0.08f;
          }
          next();
        }
        break;
      }
      case 4: {  // the spear quivers in the board
        if (Puppet* s = w.puppet(spear)) {
          float q = 0.05f * std::exp(-pt * 5.0f) * std::sin(pt * 45.0f);
          s->tilt = glm::angleAxis(0.5f + q, vec3(1, 0, 0));
        }
        if (pt > 0.6f) next();
        break;
      }
      case 5:  // walk over to it
        if (enter()) walker = Walker(plan.mover, V - dir * 0.28f, Walker::FOOT, 0.8f);
        if (walker.update(w, dt)) next();
        break;
      case 6: {  // stoop and pull it out
        float u = std::sin(glm::clamp(pt / 0.8f, 0.0f, 1.0f) * kPI);
        a.ctl.crouch = 0.55f * u;
        a.ctl.lean = 0.35f * u;
        a.ctl.armR = vec3(0.6f * u, 0, -0.3f * u);
        if (pt > 0.4f && a.ctl.itemR < 0.5f) {
          a.ctl.itemR = 1;
          if (Puppet* s = w.puppet(spear)) s->alive = false;
          w.sound(SFX_CLACK, 0.4f, a.pos, 1.4f);
          w.fx.dust(V, 6, 0.2f, 0.6f);
        }
        if (pt > 0.85f) next();
        break;
      }
      case 7:
        if (moveInM(w, dt, Walker::FOOT, 0.8f)) next();
        break;
      case 8:
        return finishM(w);
    }
    return false;
  }
};

// ===========================================================================
// KNIGHT - the mounted knight
// ===========================================================================
// Joust: couch the lance and charge; the lance shatters on impact.
class KnightJoustM : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  bool hit = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.0f;
        shotHeight = 0.7f;
        if (faceOff(w)) next();
        break;
      case 1: {  // couch the lance
        if (enter()) {
          w.sound(SFX_NEIGH, 0.6f, a.pos, 1.05f);
          a.yawTarget = faceAngle(V - A);
        }
        float u = easeInOut(pt / 0.6f);
        a.ctl.itemPitch = 1.42f * u;
        a.ctl.armR = vec3(0.2f * u, 0, 0);
        a.ctl.lean = 0.12f * u;
        a.ctl.rear = 0.2f * std::sin(u * kPI);
        a.ctl.gait += dt * 6.0f;
        a.ctl.stride = 0.3f;
        v.tremble = 0.9f;
        if (pt > 0.65f) next();
        break;
      }
      case 2: {  // gallop
        if (enter()) {
          walker = Walker(plan.mover, V, Walker::HORSE, 2.4f);
          walker.run = 1.0f;
          walker.face = false;
        }
        walker.update(w, dt);
        a.ctl.stride = std::max(a.ctl.stride, 0.4f);
        vec3 tip = bonePoint(a, md::KB_LANCE, md::itemTip(md::A_KNIGHT));
        v.tremble = 1.0f;
        if (!hit && glm::dot(flat(tip - V), dir) > -v.radius() * 0.5f) {
          hit = true;
          vec3 c = victimCenter(w);
          w.fx.sparks(c - dir * 0.2f, -dir + vec3(0, 0.4f, 0), 30, 4.0f, 0.6f, vec3(4.0f, 3.0f, 1.5f));
          w.fx.flash(c, 0.8f, vec3(4.0f, 3.5f, 2.5f), 0.12f);
          w.light(c, vec3(18.0f, 14.0f, 9.0f), 4.0f, 0.3f);
          splinters(w, tip, -dir, 45);
          a.ctl.itemR = 0;  // the lance shatters
          w.sound(SFX_SPLINTER, 1.0f, tip);
          w.sound(SFX_CLACK, 0.9f, c, 0.75f);
          w.shatter(v, c - dir * 0.2f, dir * 5.0f + vec3(0, 1.0f, 0), 1.4f, 0.9f, 10.0f);
          w.shake(0.5f);
          w.slowmo(0.25f, 0.5f);
        }
        if (walker.s >= walker.len - 1e-3f) next();
        break;
      }
      case 3: {  // pull up and rear in triumph
        if (enter()) w.sound(SFX_NEIGH, 0.9f, a.pos, 0.95f);
        float u = glm::clamp(pt / 1.3f, 0.0f, 1.0f);
        walker.update(w, dt);
        a.ctl.rear = 0.85f * std::sin(u * kPI);
        a.ctl.itemPitch = 1.42f * (1.0f - easeInOut(u));
        a.ctl.armR = vec3(0.9f * std::sin(u * kPI), 0, 0);
        a.ctl.lean = -0.15f * std::sin(u * kPI);
        if (u >= 1.0f) {
          a.ctl.rear = 0;
          w.sound(SFX_HOOF, 0.8f, a.pos, 0.9f);
          w.fx.dust(a.pos, 8, 0.3f, 0.8f);
          next();
        }
        break;
      }
      case 4: {  // a fresh lance appears in his hand
        if (enter()) {
          a.ctl.itemR = 1;
          w.sound(SFX_CHIME, 0.45f, a.pos, 1.3f);
          w.fx.magicBurst(bonePoint(a, md::KB_LANCE, md::itemTip(md::A_KNIGHT)), kGold * 2.0f, 20, 1.0f);
        }
        a.ctl.stride = approach(a.ctl.stride, 0.0f, dt * 3.0f);
        if (pt > 0.4f) next();
        break;
      }
      case 5:
        return finishM(w);
    }
    return false;
  }
};

// Trample: rear up over the victim and crash down on it.
class KnightTrampleM : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  vec3 start{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.1f;
        shotHeight = 0.8f;
        if (faceOff(w)) next();
        break;
      case 1:  // canter up
        if (enter()) {
          walker = Walker(plan.mover, V - dir * 0.62f, Walker::HORSE, 1.5f);
          walker.run = 0.6f;
        }
        v.tremble = 0.8f;
        if (walker.update(w, dt)) next();
        break;
      case 2: {  // rear up, screaming
        if (enter()) {
          w.sound(SFX_NEIGH, 1.0f, a.pos, 0.9f);
          a.yawTarget = faceAngle(V - a.pos);
        }
        float u = glm::clamp(pt / 0.7f, 0.0f, 1.0f);
        a.ctl.rear = easeOut(u);
        a.ctl.lean = -0.25f * u;
        a.ctl.armR = vec3(0.6f * u, 0, 0);
        v.tremble = 1.2f;
        v.ctl.headPitch = -0.3f * u;
        if (pt > 1.0f) {
          start = flat(a.pos);
          next();
        }
        break;
      }
      case 3: {  // crash down
        float u = glm::clamp(pt / 0.16f, 0.0f, 1.0f);
        a.ctl.rear = 1.0f - easeIn(u);
        a.pos = start + dir * 0.3f * u;
        a.ctl.lean = glm::mix(-0.25f, 0.2f, u);
        if (u >= 1.0f) {
          vec3 c = V + vec3(0, v.height() * 0.4f, 0);
          w.shatter(v, c, vec3(0, -3.0f, 0) + dir * 1.0f, 2.2f, 0.3f, 8.0f);
          w.groundRing(V, vec3(1.4f, 1.1f, 0.8f), 3.2f, 0.7f, 0.1f);
          w.fx.dust(V, 26, 0.7f, 2.2f);
          w.light(c, vec3(6.0f, 5.0f, 4.0f), 3.0f, 0.2f);
          w.sound(SFX_STOMP, 1.0f, c, 1.1f);
          w.sound(SFX_EXPLODE, 0.6f, c, 1.3f);
          w.shake(0.6f);
          w.slowmo(0.28f, 0.4f);
          next();
        }
        break;
      }
      case 4:  // stamp and snort
        a.ctl.lean = 0.2f * (1.0f - glm::clamp(pt / 0.5f, 0.0f, 1.0f));
        a.ctl.armR = vec3(0.6f, 0, 0) * (1.0f - glm::clamp(pt / 0.5f, 0.0f, 1.0f));
        if (pt > 0.6f) next();
        break;
      case 5:
        if (moveInM(w, dt, Walker::HORSE, 0.9f)) next();
        break;
      case 6:
        return finishM(w);
    }
    return false;
  }
};

// Sword sweep: the rider draws his sword and cuts the victim in two as he gallops past.
class KnightSweepM : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  int sword = -1;
  bool sliced = false;
  vec3 side{0}, P1{0};
  void placeSword(World& w, float raise) {
    Actor& a = w.actors[plan.mover];
    Prop* s = w.prop(sword);
    if (!s) return;
    vec3 grip = bonePoint(a, md::KB_RFORE, md::handR(md::A_KNIGHT));
    vec3 elbow = bonePoint(a, md::KB_RFORE, md::skeleton(md::A_KNIGHT).joint[md::KB_RFORE]);
    vec3 fore = glm::normalize(grip - elbow + vec3(1e-4f));
    vec3 up = glm::normalize(glm::mix(fore, vec3(0, 1, 0), raise) + vec3(1e-4f));
    s->rot = aimY(up, a.forward());
    s->scale = 0.6f;
    s->pos = grip - up * 0.12f * s->scale;
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.2f;
        if (faceOff(w)) next();
        break;
      case 1: {  // the lance vanishes, a sword appears
        if (enter()) {
          sword = w.addProp(Prop::SWORD);
          w.sound(SFX_CHIME, 0.6f, a.pos, 1.15f);
          w.fx.magicBurst(bonePoint(a, md::KB_LANCE, md::itemTip(md::A_KNIGHT)), kGold * 2.0f, 20, 1.0f);
        }
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.ctl.itemR = 0;
        a.ctl.armR = vec3(1.8f * easeInOut(u), 0.3f * u, -0.4f * u);
        placeSword(w, 0.6f);
        if (Prop* s = w.prop(sword)) {
          s->dissolve = 1.0f - u;
          s->glow = glm::vec4(kGold, 1.2f - u);
        }
        v.tremble = 0.8f;
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // gallop past the victim, sword raised
        if (enter()) {
          side = vec3(-dir.z, 0, dir.x);
          // pass with the victim on the rider's sword (right) side
          vec3 right = -side;
          P1 = V - right * 0.42f + dir * 0.9f;
          walker = Walker(plan.mover, P1, Walker::HORSE, 2.2f);
          walker.run = 1.0f;
          w.sound(SFX_NEIGH, 0.7f, a.pos, 1.0f);
        }
        walker.update(w, dt);
        placeSword(w, 0.6f);
        vec3 rel = flat(V - a.pos);
        float along = glm::dot(rel, glm::normalize(flat(P1 - A) + vec3(1e-4f)));
        if (!sliced && along < 0.45f) {
          // swing
          float k = glm::clamp((0.45f - along) / 0.3f, 0.0f, 1.0f);
          a.ctl.armR = vec3(glm::mix(1.8f, -0.4f, k), 0.3f + 0.4f * k, -0.4f * (1.0f - k));
          a.ctl.twist = -0.5f * k;
          if (k >= 1.0f || along < 0.15f) {
            sliced = true;
            vec3 c = victimCenter(w);
            w.slice(v, 0.9f);
            w.fx.sparks(c, vec3(0, 1, 0), 40, 4.0f, 1.0f, vec3(4.0f, 3.0f, 1.5f));
            w.fx.flash(c, 0.8f, kGold * 5.0f, 0.12f);
            w.light(c, kGold * 22.0f, 4.0f, 0.3f);
            w.sound(SFX_CLANG, 1.0f, c, 1.1f);
            w.sound(SFX_SHATTER, 0.6f, c, 1.2f);
            w.shake(0.4f);
            w.slowmo(0.25f, 0.5f);
          }
        } else if (sliced) {
          a.ctl.armR = vec3(approach(a.ctl.armR.x, 0.3f, dt * 3.0f), 0.3f, 0);
          a.ctl.twist = approach(a.ctl.twist, 0.0f, dt * 2.0f);
        }
        v.tremble = 1.0f;
        if (walker.s >= walker.len - 1e-3f) next();
        break;
      }
      case 3: {  // wheel round
        walker.update(w, dt);
        a.ctl.armR = vec3(approach(a.ctl.armR.x, 0.0f, dt * 2.0f), approach(a.ctl.armR.y, 0.0f, dt), 0);
        placeSword(w, 0.6f * (1.0f - glm::clamp(pt / 0.8f, 0.0f, 1.0f)));
        if (Prop* s = w.prop(sword)) {
          float u = glm::clamp((pt - 0.4f) / 0.5f, 0.0f, 1.0f);
          s->dissolve = u;
          s->glow = glm::vec4(kGold, 0.8f * u);
          if (u >= 1.0f) s->alive = false;
        }
        if (pt > 0.9f) next();
        break;
      }
      case 4:
        if (enter()) a.ctl.itemR = 1;
        if (moveInM(w, dt, Walker::HORSE, 1.0f)) next();
        break;
      case 5:
        return finishM(w);
    }
    return false;
  }
};

// ===========================================================================
// BISHOP - seated on his cathedra
// ===========================================================================
// Raises crozier and hand to heaven (u: 0..1)
void bishopInvoke(Actor& a, float u, float t) {
  a.ctl.armL = vec3(0.9f * u, 0.1f * u, 0.4f * u);
  a.ctl.armR = vec3(0.7f * u, 0.0f, -0.2f * u);
  a.ctl.headPitch = -0.35f * u;
  a.ctl.lean = -0.08f * u + 0.02f * std::sin(t * 2.0f) * u;
}

// Wrath of heaven: the Dies irae on the organ, a storm over the victim, lightning.
class BishopWrath : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  vec3 cloud{0};
  float flick = 0, rumble = 0;
  int strikes = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    cloud = V + vec3(0, 1.9f, 0);
    switch (phase) {
      case 0:
        shotHeight = 1.0f;
        shotExtra = 1.4f;
        if (faceOff(w)) next();
        break;
      case 1: {  // the hymn begins; the bishop raises his crozier; a storm gathers
        if (enter()) {
          w.sound(SFX_HYMN, 0.95f, a.pos);
          w.sound(SFX_WIND, 0.6f, cloud, 0.9f);
        }
        float u = glm::clamp(pt / 3.6f, 0.0f, 1.0f);
        bishopInvoke(a, easeInOut(pt / 1.0f), w.time);
        a.glow = glm::vec4(kGold, 0.35f * easeInOut(pt / 1.0f));
        w.frameLights.push_back({bonePoint(a, md::HB_ITEML, md::skeleton(md::A_BISHOP).joint[md::HB_ITEML]) + vec3(0, 0.7f, 0),
                                 2.0f, kGold * 3.0f * easeInOut(pt / 1.0f)});
        cloudPuffs(w, cloud, 0.3f + 0.45f * easeOut(u), 70.0f + 150.0f * u, dt, vec3(0.05f, 0.05f, 0.06f), 0.0f);
        if (u > 0.35f) rain(w, cloud, 0.55f, 160.0f * (u - 0.35f), dt);
        flick -= dt;
        if (u > 0.3f && flick <= 0) {  // lightning inside the cloud
          flick = w.fx.rnd(0.25f, 0.6f);
          vec3 p = cloud + w.fx.rndDir() * 0.3f;
          w.fx.bolt(p, p + w.fx.rndDir() * 0.35f, vec3(2.0f, 2.4f, 3.5f), 0.04f, 5);
          w.light(p, vec3(8.0f, 9.0f, 14.0f), 3.0f, 0.12f);
          Particle g;
          g.pos = p;
          g.size = 0.6f;
          g.color = vec3(0.8f, 0.9f, 1.3f);
          g.colorEnd = g.color * 0.1f;
          g.opacity = 0;
          g.maxLife = 0.15f;
          g.type = PT_GLOW;
          g.fadeIn = 0.0f;
          g.still = true;
          w.fx.add(g);
        }
        rumble -= dt;
        if (u > 0.4f && rumble <= 0) {
          rumble = 1.4f;
          w.sound(SFX_RUMBLE, 0.6f, cloud, 0.7f);
        }
        v.tremble = 0.4f + 0.8f * u;
        v.ctl.headPitch = -0.35f * u;
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // strike!
        cloudPuffs(w, cloud, 0.75f, 150.0f, dt, vec3(0.05f, 0.05f, 0.06f), 0.0f);
        rain(w, cloud, 0.55f, 110.0f, dt);
        bishopInvoke(a, 1.0f, w.time);
        if (enter() || (strikes == 1 && pt > 0.32f)) {
          strikes++;
          vec3 top = V + vec3(0, v.alive ? v.height() * 0.95f : 0.1f, 0);
          lightning(w, cloud - vec3(0, 0.2f, 0), top, strikes == 1 ? 1.4f : 0.8f);
          w.sound(SFX_THUNDER, 1.0f, top, strikes == 1 ? 1.0f : 1.15f);
          w.shake(strikes == 1 ? 0.55f : 0.3f);
          if (strikes == 1) {
            v.crackColor = vec3(0.7f, 0.85f, 1.0f);
            v.crack = 0.8f;
            v.heat = 0.6f;
            v.glow = glm::vec4(0.6f, 0.75f, 1.0f, 1.5f);
            w.slowmo(0.3f, 0.5f);
          }
        }
        if (strikes == 1 && pt > 0.18f && v.alive) {
          vec3 c = victimCenter(w);
          w.shatter(v, c, vec3(0, 0.5f, 0), 2.6f, 1.6f, 11.0f);
          w.fx.flash(c, 1.4f, vec3(5.0f, 6.0f, 9.0f), 0.2f);
          w.fx.sparks(c, vec3(0, 1, 0), 50, 5.0f, 1.0f, vec3(3.0f, 3.5f, 5.0f));
          w.fx.smoke(c, 12, 0.3f, 0.8f, 0.9f);
          w.groundRing(V, vec3(1.2f, 1.5f, 2.4f), 3.4f, 0.7f, 0.1f);
          w.sound(SFX_EXPLODE, 0.8f, c, 1.2f);
        }
        if (pt > 0.9f) next();
        break;
      }
      case 3: {  // the storm passes, the bishop lowers his arms
        float u = glm::clamp(pt / 1.2f, 0.0f, 1.0f);
        bishopInvoke(a, 1.0f - easeInOut(u), w.time);
        a.glow = glm::vec4(kGold, 0.35f * (1.0f - u));
        if (u < 0.5f) {
          cloudPuffs(w, cloud, 0.75f, 50.0f * (1.0f - u * 2.0f), dt, vec3(0.05f, 0.05f, 0.06f), 0.0f);
          rain(w, cloud, 0.55f, 100.0f * (1.0f - u * 2.0f), dt);
        }
        if (u >= 1.0f) next();
        break;
      }
      case 4:
        if (moveInM(w, dt, Walker::SLIDE, 1.2f)) next();
        break;
      case 5:
        return finishM(w);
    }
    return false;
  }
};

// Brimstone: an organ chord, a red cloud, and burning stones raining down.
class BishopBrimstone : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  vec3 cloud{0};
  struct Rock {
    int prop = -1;
    vec3 from{0}, to{0};
    float t = -1, dur = 0.5f;
    bool lethal = false;
  };
  Rock rocks[4];
  int launched = 0, landed = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    cloud = V + vec3(0, 2.0f, 0);
    switch (phase) {
      case 0:
        shotHeight = 1.0f;
        shotExtra = 1.4f;
        if (faceOff(w)) next();
        break;
      case 1: {  // the chord; a red cloud boils up over the victim
        if (enter()) {
          w.sound(SFX_ORGAN, 1.0f, a.pos);
          w.sound(SFX_RUMBLE, 0.7f, cloud, 0.6f);
        }
        float u = glm::clamp(pt / 2.0f, 0.0f, 1.0f);
        bishopInvoke(a, easeInOut(pt / 0.8f), w.time);
        cloudPuffs(w, cloud, 0.25f + 0.4f * easeOut(u), 60.0f + 100.0f * u, dt, vec3(0.16f, 0.05f, 0.03f), u);
        w.frameLights.push_back({cloud - vec3(0, 0.3f, 0), 3.5f, vec3(6.0f, 1.8f, 0.4f) * u});
        v.tremble = 0.5f + 0.7f * u;
        v.ctl.headPitch = -0.35f * u;
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // fire and brimstone
        cloudPuffs(w, cloud, 0.65f, 140.0f, dt, vec3(0.16f, 0.05f, 0.03f), 1.0f);
        w.frameLights.push_back({cloud - vec3(0, 0.3f, 0), 3.5f, vec3(6.0f, 1.8f, 0.4f)});
        bishopInvoke(a, 1.0f, w.time);
        if (launched < 4 && pt > launched * 0.38f) {
          Rock& r = rocks[launched];
          r.prop = w.addProp(Prop::CANNONBALL);
          r.from = cloud + vec3(w.fx.rnd(-0.3f, 0.3f), -0.15f, w.fx.rnd(-0.3f, 0.3f));
          r.lethal = launched == 2;
          vec3 side(-dir.z, 0, dir.x);
          float off[4] = {0.55f, -0.6f, 0.0f, 0.45f};
          r.to = r.lethal ? V + vec3(0, v.height() * 0.6f, 0) : V + side * off[launched] + dir * w.fx.rnd(-0.4f, 0.4f);
          r.t = 0;
          r.dur = 0.42f;
          if (Prop* p = w.prop(r.prop)) {
            p->heat = 1.0f;
            p->scale = r.lethal ? 1.25f : w.fx.rnd(0.8f, 1.0f);
            p->pos = r.from;
          }
          w.sound(SFX_WHOOSH, 0.6f, r.from, 0.6f);
          launched++;
        }
        for (Rock& r : rocks) {
          if (r.t < 0) continue;
          r.t += dt;
          Prop* p = w.prop(r.prop);
          float u = glm::clamp(r.t / r.dur, 0.0f, 1.0f);
          if (p) {
            p->pos = glm::mix(r.from, r.to, easeIn(u));
            p->rot = glm::normalize(glm::angleAxis(dt * 9.0f, vec3(1, 0, 0.3f)) * p->rot);
            w.frameLights.push_back({p->pos, 1.8f, vec3(5.0f, 1.8f, 0.4f)});
            w.fx.embers(p->pos, 2, 0.6f);
            if (w.fx.rnd() < 0.7f) w.fx.smoke(p->pos, 1, 0.03f, 0.2f, 0.8f);
          }
          if (u >= 1.0f) {
            if (p) p->alive = false;
            r.t = -1;
            landed++;
            vec3 at = r.lethal ? r.to : flat(r.to);
            w.fx.fireball(at, r.lethal ? 1.1f : 0.6f);
            w.groundRing(flat(at), vec3(2.2f, 0.9f, 0.3f), 3.0f, 0.5f, 0.08f);
            w.light(at, vec3(30.0f, 12.0f, 3.0f), 4.0f, 0.4f);
            w.sound(SFX_EXPLODE, r.lethal ? 1.0f : 0.6f, at, r.lethal ? 0.9f : 1.2f);
            w.shake(r.lethal ? 0.55f : 0.25f);
            if (r.lethal && v.alive) {
              v.crackColor = vec3(1.0f, 0.5f, 0.15f);
              v.crack = 0.9f;
              w.shatter(v, at + vec3(0, 0.2f, 0), vec3(0, -1.0f, 0), 2.5f, 1.4f, 10.0f);
              w.slowmo(0.3f, 0.45f);
            } else if (v.alive) {
              v.heat = std::min(1.0f, v.heat + 0.25f);
            }
          }
        }
        v.tremble = 1.2f;
        if (landed >= 4 && pt > 2.0f) next();
        break;
      }
      case 3: {
        float u = glm::clamp(pt / 1.0f, 0.0f, 1.0f);
        bishopInvoke(a, 1.0f - easeInOut(u), w.time);
        if (u >= 1.0f) next();
        break;
      }
      case 4:
        if (moveInM(w, dt, Walker::SLIDE, 1.2f)) next();
        break;
      case 5:
        return finishM(w);
    }
    return false;
  }
};

// Pillar of salt: a bell tolls three times and the victim turns to salt and crumbles away.
class BishopSalt : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  int tolls = 0;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    const vec3 pale(1.0f, 0.96f, 0.88f);
    switch (phase) {
      case 0:
        shotHeight = 0.8f;
        shotExtra = 1.0f;
        if (faceOff(w)) next();
        break;
      case 1: {  // the bell tolls; a shaft of pale light falls on the victim, which turns to salt
        if (tolls < 3 && pt >= tolls * 1.35f) {
          w.sound(SFX_BELL, 0.9f - 0.1f * tolls, a.pos, 1.0f);
          tolls++;
        }
        float u = glm::clamp(pt / 3.6f, 0.0f, 1.0f);
        bishopInvoke(a, easeInOut(pt / 0.9f) * 0.8f, w.time);
        // light from above
        for (int i = 0; i < 3; ++i) {
          Particle p;
          float an = w.fx.rnd(0, kTau);
          float r = w.fx.rnd(0.0f, v.radius() * 1.3f);
          p.pos = V + vec3(std::cos(an) * r, w.fx.rnd(0.0f, 2.2f), std::sin(an) * r);
          p.vel = vec3(0, -w.fx.rnd(0.2f, 0.6f), 0);
          p.size = w.fx.rnd(0.008f, 0.02f);
          p.color = pale * w.fx.rnd(1.5f, 3.0f);
          p.colorEnd = pale * 0.2f;
          p.opacity = 0;
          p.maxLife = w.fx.rnd(0.6f, 1.2f);
          p.type = PT_GLOW;
          p.drag = 0.2f;
          w.fx.add(p);
        }
        w.frameLights.push_back({V + vec3(0, 1.4f, 0), 3.0f, pale * (4.0f + 10.0f * std::min(1.0f, pt))});
        v.stone = easeInOut(u);
        v.tremble = 0.8f * (1.0f - u);
        v.idleWeight = 1.0f - u;
        v.glow = glm::vec4(pale, 0.4f * u);
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // and crumbles to dust from the top down
        if (enter()) {
          v.dissolveColor = pale * 0.8f;
          w.sound(SFX_SHATTER, 0.7f, victimCenter(w), 0.7f);
          w.sound(SFX_RUMBLE, 0.5f, V, 1.4f);
        }
        float u = glm::clamp(pt / 1.6f, 0.0f, 1.0f);
        if (v.alive) {
          v.dissolve = easeIn(u);
          v.stone = 1.0f;
          float h = v.height() * (1.0f - u);
          w.fx.dust(V + vec3(0, h, 0), 2, v.radius() * 0.8f, 0.3f, vec3(0.86f, 0.84f, 0.8f));
          if (w.fx.rnd() < 0.6f) w.fx.chips(V + vec3(0, h, 0), 3, 0.8f, false);
          if (u >= 1.0f) {
            v.alive = false;
            v.visible = false;
            w.fx.dust(V, 24, 0.5f, 0.8f, vec3(0.86f, 0.84f, 0.8f));
          }
        }
        bishopInvoke(a, 0.8f * (1.0f - easeInOut(u)), w.time);
        if (u >= 1.0f && pt > 1.8f) next();
        break;
      }
      case 3:
        if (moveInM(w, dt, Walker::SLIDE, 1.2f)) next();
        break;
      case 4:
        return finishM(w);
    }
    return false;
  }
};

// ===========================================================================
// ROOK - the gatehouse
// ===========================================================================
// Siege stomp: stand up on stone legs, walk over, and stamp on the victim.
class RookStomp : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  vec3 start{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 0.8f;
        shotExtra = 1.0f;
        if (faceOff(w)) next();
        break;
      case 1: {  // the gatehouse stands up
        if (enter()) {
          w.sound(SFX_GRIND, 0.9f, a.pos, 0.7f);
          w.sound(SFX_RUMBLE, 0.8f, a.pos, 0.8f);
        }
        float u = glm::clamp(pt / 0.85f, 0.0f, 1.0f);
        a.ctl.legs = easeInOut(u);
        a.tremble = 0.4f * (1.0f - u);
        if (w.fx.rnd() < 0.5f) w.fx.dust(a.pos, 2, 0.35f, 0.5f);
        v.tremble = 0.6f;
        if (u >= 1.0f) {
          w.sound(SFX_STOMP, 0.8f, a.pos, 1.0f);
          w.shake(0.15f);
          next();
        }
        break;
      }
      case 2:  // stomp over
        if (enter()) walker = Walker(plan.mover, V - dir * std::min(L, 0.62f), Walker::CASTLE, 1.0f);
        v.tremble = 0.9f;
        v.ctl.headPitch = -0.2f;
        if (walker.update(w, dt)) next();
        break;
      case 3: {  // lift a foot high...
        if (enter()) {
          a.yawTarget = faceAngle(V - a.pos);
          w.sound(SFX_GRIND, 0.5f, a.pos, 1.3f);
          if (L > 1.5f) battleShot(w, flat(a.pos), V, 0.8f, 0.9f, plan.mover, plan.victim);
        }
        float u = easeOut(pt / 0.45f);
        a.ctl.lift = u;
        a.ctl.lean = -0.1f * u;
        v.tremble = 1.3f;
        v.ctl.headPitch = -0.4f * u;
        if (pt > 0.6f) {
          start = flat(a.pos);
          next();
        }
        break;
      }
      case 4: {  // ...and STAMP
        float u = glm::clamp(pt / 0.12f, 0.0f, 1.0f);
        a.ctl.lift = 1.0f - easeIn(u);
        a.ctl.lean = glm::mix(-0.1f, 0.12f, u);
        a.pos = start + dir * 0.14f * u;
        if (u >= 1.0f) {
          vec3 c = V + vec3(0, v.height() * 0.3f, 0);
          w.shatter(v, c, vec3(0, -3.5f, 0) + dir * 0.8f, 2.4f, 0.25f, 8.0f);
          w.groundRing(V, vec3(1.3f, 1.0f, 0.7f), 4.0f, 0.8f, 0.12f);
          w.fx.dust(V, 34, 0.8f, 2.6f);
          w.fx.chips(V, 30, 3.0f, v.color == 1);
          w.sound(SFX_STOMP, 1.0f, V, 0.8f);
          w.sound(SFX_BOOM, 0.7f, V, 1.4f);
          w.shake(0.8f);
          w.slowmo(0.28f, 0.45f);
          // everyone nearby jumps
          for (Actor& o : w.actors)
            if (o.alive && o.visible && o.id != a.id && glm::length(flat(o.pos - V)) < 2.2f) o.sq.v -= 2.0f;
          next();
        }
        break;
      }
      case 5:
        a.ctl.lean = 0.12f * (1.0f - glm::clamp(pt / 0.4f, 0.0f, 1.0f));
        if (pt > 0.45f) next();
        break;
      case 6:
        if (moveInM(w, dt, Walker::CASTLE, 0.9f)) next();
        break;
      case 7: {  // sit back down on its foundations
        if (enter()) {
          a.yawTarget = a.baseYaw;
          w.sound(SFX_GRIND, 0.7f, a.pos, 0.8f);
        }
        float u = glm::clamp(pt / 0.7f, 0.0f, 1.0f);
        a.ctl.legs = 1.0f - easeInOut(u);
        if (u >= 1.0f) {
          w.sound(SFX_THUD, 0.9f, a.pos, 0.7f);
          w.fx.dust(a.pos, 14, a.radius(), 0.9f);
          w.shake(0.2f);
          next();
        }
        break;
      }
      case 8:
        return finishM(w);
    }
    return false;
  }
};

// Dragon: the drawbridge drops, a dragon crawls out of the gate - fire or jaws.
class RookDragon : public MBattle {
 public:
  RookDragon(const MovePlan& p, bool fire) : MBattle(p), fire_(fire) {}

 protected:
  bool fire_;
  int dragon = -1;
  vec3 gate{0}, out{0}, lunge0{0};
  float growl = 0, roarT = 0, burnT = 0;
  bool bitten = false;
  float fullScale = 0.68f;
  vec3 mouth(World& w) {
    Puppet* d = w.puppet(dragon);
    return d ? puppetPoint(*d, md::DB_HEAD, vec3(0, 0.665f, 0.72f)) : vec3(0);
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    Puppet* d = w.puppet(dragon);
    switch (phase) {
      case 0:
        shotHeight = 0.7f;
        shotExtra = 1.6f;
        if (faceOff(w)) next();
        break;
      case 1:  // slide within reach of the dragon
        if (enter()) {
          float keep = fire_ ? 1.65f : 1.45f;
          walker = Walker(plan.mover, L > keep ? V - dir * keep : flat(a.pos), Walker::SLIDE, 1.5f);
          walker.face = false;
        }
        a.yawTarget = faceAngle(V - a.pos);
        a.yawRate = 4.0f;
        v.tremble = 0.6f;
        if (walker.update(w, dt) && pt > 0.4f) next();
        break;
      case 2: {  // the drawbridge drops, the portcullis rises
        if (enter()) w.sound(SFX_CHAIN, 1.0f, a.pos);
        float u = glm::clamp(pt / 0.8f, 0.0f, 1.0f);
        a.ctl.bridge = easeIn(u);
        a.ctl.gate = easeInOut(glm::clamp((pt - 0.3f) / 0.7f, 0.0f, 1.0f));
        if (u >= 1.0f && pt < 0.8f + dt) {
          vec3 lip = a.pos + a.forward() * 0.53f;
          w.sound(SFX_THUD, 1.0f, lip, 0.9f);
          w.fx.dust(lip, 14, 0.25f, 1.0f);
          w.shake(0.2f);
        }
        if (pt > 0.3f && pt - dt <= 0.3f) w.sound(SFX_GRIND, 0.6f, a.pos, 1.4f);
        if (pt > 1.05f) next();
        break;
      }
      case 3: {  // eyes glow in the dark, a growl
        if (enter()) w.sound(SFX_ROAR, 0.5f, a.pos, 0.6f);
        gate = a.pos + a.forward() * 0.12f + vec3(0, 0.14f, 0);
        vec3 side(-a.forward().z, 0, a.forward().x);
        float blink = pt < 0.3f ? pt / 0.3f : 1.0f;
        for (int s = -1; s <= 1; s += 2) w.fx.flash(gate + side * (0.028f * s), 0.04f * blink, vec3(6.0f, 3.0f, 0.5f), 0.05f);
        w.frameLights.push_back({gate + a.forward() * 0.1f, 1.2f, vec3(3.0f, 1.0f, 0.2f) * blink});
        v.tremble = 0.9f;
        if (pt > 0.6f) next();
        break;
      }
      case 4: {  // the dragon crawls out over the bridge, growing to full size
        if (enter()) {
          dragon = w.addPuppet(md::A_DRAGON, a.color);
          d = w.puppet(dragon);
          gate = flat(a.pos) + a.forward() * 0.05f;
          out = flat(a.pos) + a.forward() * 0.95f;
          if (d) {
            d->pos = gate;
            d->yaw = a.yaw;
            d->scale = 0.08f;
          }
          w.sound(SFX_ROAR, 0.6f, a.pos, 0.85f);
          // frame the dragon and its prey from the side, keeping the gatehouse out of the way
          battleShot(w, out, V, 0.75f, 1.5f, -1, plan.victim);
        }
        float u = glm::clamp(pt / 1.2f, 0.0f, 1.0f);
        if (d) {
          vec3 prev = d->pos;
          d->pos = glm::mix(gate, out, easeInOut(u));
          d->pos.y = 0.02f * std::sin(u * kPI);
          d->scale = glm::mix(0.08f, fullScale, easeOut(u));
          d->ctl.gait += glm::length(d->pos - prev) / 0.25f * kTau;
          d->ctl.stride = std::sin(u * kPI) + 0.2f;
          d->ctl.wings = 0;
          if (w.fx.rnd() < 0.3f) w.fx.smoke(d->pos + vec3(0, 0.2f, 0), 1, 0.15f, 0.3f, 0.6f);
          d->yaw = faceAngle(V - d->pos);
        }
        v.tremble = 1.0f;
        if (u >= 1.0f) next();
        break;
      }
      case 5: {  // rear up, spread the wings, ROAR
        if (enter()) w.sound(SFX_ROAR, 1.0f, d ? d->pos : a.pos, 1.0f);
        float u = glm::clamp(pt / 1.05f, 0.0f, 1.0f);
        if (d) {
          d->ctl.stride = approach(d->ctl.stride, 0.0f, dt * 3.0f);
          d->ctl.neck = std::sin(u * kPI * 0.8f) * 1.0f;
          d->ctl.jaw = std::sin(glm::clamp(u * 1.4f, 0.0f, 1.0f) * kPI) * 0.9f + 0.1f;
          d->ctl.wings = easeOut(glm::clamp(u * 1.6f, 0.0f, 1.0f));
          d->ctl.flap += dt * 9.0f;
          if (u > 0.2f && u < 0.7f) w.shake(dt * 1.2f);
        }
        v.tremble = 1.3f;
        if (u >= 1.0f) next();
        break;
      }
      case 6:
        if (fire_) {  // breathe fire
          if (enter()) {
            w.sound(SFX_FIRE, 1.0f, d ? d->pos : a.pos, 1.0f);
            burnT = 0;
          }
          if (d) {
            d->ctl.neck = approach(d->ctl.neck, -0.35f, dt * 3.0f);
            d->ctl.jaw = approach(d->ctl.jaw, 1.0f, dt * 6.0f);
            d->ctl.flap += dt * 6.0f;
            d->ctl.wings = 1.0f;
            vec3 m = mouth(w);
            vec3 tgt = victimCenter(w);
            vec3 fd = glm::normalize(tgt - m + vec3(1e-4f));
            float len = glm::length(tgt - m);
            if (pt > 0.15f) {
              fireJet(w, m, fd, len, dt);
              w.frameLights.push_back({m + fd * len * 0.5f, 3.0f, vec3(18.0f, 7.0f, 1.5f) * (0.8f + 0.4f * w.fx.rnd())});
              w.frameLights.push_back({tgt, 2.5f, vec3(14.0f, 5.0f, 1.0f)});
            }
          }
          if (v.alive) {
            float u = glm::clamp((pt - 0.25f) / 1.25f, 0.0f, 1.0f);
            v.heat = u;
            v.crackColor = vec3(1.0f, 0.45f, 0.1f);
            v.crack = u * 0.9f;
            v.tremble = 1.5f;
            if (w.fx.rnd() < 0.4f) w.fx.embers(victimCenter(w), 2, 1.2f);
            if (u >= 1.0f) {
              vec3 c = victimCenter(w);
              w.shatter(v, c, vec3(0, 0.5f, 0) + dir * 1.5f, 2.2f, 1.2f, 9.0f);
              w.fx.fireball(c, 1.0f);
              w.light(c, vec3(40.0f, 16.0f, 4.0f), 5.0f, 0.5f);
              w.groundRing(V, vec3(2.4f, 1.0f, 0.3f), 3.4f, 0.6f, 0.1f);
              w.sound(SFX_EXPLODE, 1.0f, c, 0.9f);
              w.shake(0.5f);
              w.slowmo(0.3f, 0.45f);
              // the debris keeps burning for a moment
              for (Chunk& ch : w.chunks)
                if (glm::length(flat(ch.pos - V)) < 0.8f && ch.age < 0.05f) {
                  ch.glow = 1.0f;
                  ch.glowColor = vec3(1.0f, 0.4f, 0.1f);
                }
            }
          }
          if (!v.alive && pt > 1.85f) next();
        } else {  // lunge and bite
          if (enter()) {
            lunge0 = d ? d->pos : vec3(0);
            bitten = false;
          }
          float u = glm::clamp(pt / 0.5f, 0.0f, 1.0f);
          if (d && !bitten) {
            vec3 toV = flat(V - lunge0);
            float reach = std::max(0.0f, glm::length(toV) - 0.62f);
            d->pos = lunge0 + glm::normalize(toV + vec3(1e-4f)) * reach * easeIn(u);
            d->ctl.neck = glm::mix(1.0f, -0.9f, easeIn(u));
            d->ctl.jaw = u < 0.8f ? 1.0f : 1.0f - (u - 0.8f) / 0.2f;
            d->ctl.wings = 1.0f;
            d->ctl.flap += dt * 10.0f;
            if (u >= 1.0f) {
              bitten = true;
              w.sound(SFX_BITE, 1.0f, V, 1.0f);
              w.fx.chips(victimCenter(w), 20, 2.0f, v.color == 1);
              w.shake(0.35f);
              v.crackColor = vec3(1.0f, 0.6f, 0.3f);
              v.crack = 0.6f;
            }
          }
          if (bitten && d) {
            // lift the victim in its jaws and shake it
            float t2 = pt - 0.5f;
            d->ctl.neck = glm::mix(-0.9f, 0.5f, easeInOut(t2 / 0.6f));
            d->ctl.headYaw = 0.5f * std::sin(t2 * 22.0f) * glm::clamp(t2 / 0.3f, 0.0f, 1.0f);
            d->ctl.jaw = 0.15f;
            if (v.alive) {
              vec3 m = mouth(w);
              v.pos = m - vec3(0, v.height() * 0.65f, 0);
              v.tilt = glm::angleAxis(0.6f * std::sin(t2 * 22.0f), dir);
              v.tiltPivot = vec3(0, v.height() * 0.7f, 0);
              v.tremble = 1.5f;
              v.idleWeight = 0;
              if (t2 > 1.0f) {
                vec3 c = victimCenter(w);
                w.sound(SFX_BITE, 1.0f, c, 0.8f);
                w.sound(SFX_SHATTER, 0.8f, c, 0.9f);
                w.shatter(v, c, vec3(0, -1.0f, 0) - dir * 0.5f, 1.6f, 0.3f, 9.0f);
                w.fx.dust(c, 14, 0.3f, 1.0f);
                w.shake(0.45f);
                w.slowmo(0.3f, 0.4f);
              }
            }
            if (!v.alive && t2 > 1.6f) next();
          }
        }
        break;
      case 7: {  // fold the wings and go back in, shrinking into the dark
        if (enter()) {
          lunge0 = d ? d->pos : out;
          if (d) d->yaw = faceAngle(gate - d->pos);
          w.sound(SFX_ROAR, 0.4f, lunge0, 1.3f);
        }
        float u = glm::clamp(pt / 1.15f, 0.0f, 1.0f);
        if (d) {
          vec3 prev = d->pos;
          d->ctl.wings = approach(d->ctl.wings, 0.0f, dt * 2.5f);
          d->ctl.neck = approach(d->ctl.neck, 0.0f, dt * 2.0f);
          d->ctl.headYaw = approach(d->ctl.headYaw, 0.0f, dt * 3.0f);
          d->ctl.jaw = approach(d->ctl.jaw, 0.0f, dt * 3.0f);
          d->pos = glm::mix(lunge0, gate, easeInOut(u));
          d->pos.y = 0;
          d->scale = glm::mix(fullScale, 0.05f, easeIn(u));
          d->ctl.gait += glm::length(d->pos - prev) / 0.25f * kTau;
          d->ctl.stride = std::sin(u * kPI) + 0.2f;
          if (u > 0.85f) d->dissolve = (u - 0.85f) / 0.15f;
          if (u >= 1.0f) d->alive = false;
        }
        if (u >= 1.0f) next();
        break;
      }
      case 8: {  // portcullis down and drawbridge up as the gatehouse glides onto the square
        if (enter()) {
          w.sound(SFX_CLANG, 0.6f, a.pos, 0.6f);
          w.sound(SFX_CHAIN, 0.9f, a.pos, 1.05f);
        }
        float u = glm::clamp(pt / 0.9f, 0.0f, 1.0f);
        a.ctl.gate = 1.0f - easeIn(glm::clamp(u * 2.0f, 0.0f, 1.0f));
        a.ctl.bridge = 1.0f - easeInOut(u);
        if (u >= 1.0f && pt - dt < 0.9f) w.sound(SFX_THUD, 0.6f, a.pos, 1.3f);
        bool arrived = moveInM(w, dt, Walker::SLIDE, 1.3f);
        if (arrived && u >= 1.0f) next();
        break;
      }
      case 9:
        return finishM(w);
    }
    return false;
  }
};

// ===========================================================================
// QUEEN and KING - royals on their thrones
// ===========================================================================
// Throne charge / ram: the throne rockets across the board into the victim.
class ThroneRam : public MBattle {
 public:
  ThroneRam(const MovePlan& p, bool shatterIt) : MBattle(p), shatter_(shatterIt) {}

 protected:
  bool shatter_;
  vec3 start{0}, back{0}, stop{0};
  float dur = 0.5f;
  Flight flight;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.9f;
        if (faceOff(w)) next();
        break;
      case 1: {  // draw back, grip the armrests
        if (enter()) {
          start = flat(a.pos);
          back = start - dir * 0.28f;
          w.sound(SFX_SLIDE, 0.6f, a.pos, 0.8f);
          a.yawTarget = faceAngle(V - A);
        }
        float u = easeInOut(pt / 0.6f);
        a.pos = glm::mix(start, back, u);
        a.ctl.lean = 0.3f * u;
        a.ctl.armL = vec3(0.3f * u, 0, 0);
        a.ctl.armR = vec3(0.3f * u, 0, 0);
        v.tremble = 0.9f;
        if (pt > 0.75f) next();
        break;
      }
      case 2: {  // charge
        if (enter()) {
          stop = V - dir * touchDist(a, v) * 0.95f;
          dur = glm::clamp(0.2f + 0.12f * glm::length(stop - back), 0.25f, 0.6f);
          w.sound(SFX_SLIDE, 1.0f, a.pos, 1.3f);
          w.sound(SFX_WHOOSH, 0.8f, a.pos, 0.8f);
        }
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        a.pos = glm::mix(back, stop, easeIn(u));
        w.fx.dust(flat(a.pos), 1, 0.2f, 0.4f);
        v.tremble = 1.2f;
        if (u >= 1.0f) {
          vec3 hit = V - dir * v.radius() * 0.7f + vec3(0, 0.35f, 0);
          w.fx.sparks(hit, dir + vec3(0, 0.6f, 0), 34, 4.0f, 0.7f, vec3(3.5f, 2.8f, 1.8f));
          w.fx.chips(hit, 24, 2.8f, v.color == 1);
          w.light(hit, vec3(8.0f, 6.0f, 4.0f), 3.0f, 0.2f);
          w.sound(SFX_CLACK, 1.0f, hit, 0.7f);
          w.sound(SFX_THUD, 1.0f, hit, 0.9f);
          w.shake(0.5f);
          w.slowmo(0.28f, 0.4f);
          if (shatter_) {
            w.shatter(v, hit, dir * 5.0f + vec3(0, 1.2f, 0), 1.8f, 1.0f, 10.0f);
            w.groundRing(V, vec3(1.2f, 1.0f, 0.7f), 3.0f, 0.5f, 0.08f);
          } else {
            flight.launch(v, dir * 4.2f + vec3(0, 4.6f, 0), 9.0f);
          }
          start = flat(a.pos);
          next();
        }
        break;
      }
      case 3: {  // rebound and settle
        float u = glm::clamp(pt / 0.4f, 0.0f, 1.0f);
        a.pos = start - dir * 0.08f * std::sin(u * kPI);
        a.ctl.lean = 0.3f * (1.0f - u);
        a.ctl.armL = a.ctl.armR = vec3(0.3f * (1.0f - u), 0, 0);
        if (v.alive && !shatter_ && flight.update(v, dt)) crashLanding(w, v, dir);
        if (!v.alive && pt > 0.5f) next();
        break;
      }
      case 4:
        if (moveInM(w, dt, Walker::SLIDE, 0.9f)) next();
        break;
      case 5:
        return finishM(w);
    }
    return false;
  }
};

// The royal gets up, walks to the victim and strikes it (style decides how),
// then returns to the throne.
class RoyalStrike : public MBattle {
 public:
  enum Kind { SMITE, SWEEP, CLEAVE, BLOW };
  RoyalStrike(const MovePlan& p, Kind k) : MBattle(p), kind_(k) {}

 protected:
  Kind kind_;
  Flight flight;
  bool struck = false;
  int sub = 0;
  float reach() const { return kind_ == CLEAVE ? 0.55f : 0.42f; }
  // the arm pose during the strike: u 0 = wound up, 1 = follow-through
  void strikePose(Actor& a, float wind, float u) {
    switch (kind_) {
      case SMITE:  // sceptre raised high and brought down
        a.ctl.armR = vec3(glm::mix(0.0f, 2.2f, wind) - 2.0f * u, 0.1f, -0.3f * wind);
        a.ctl.lean = -0.1f * wind + 0.35f * u;
        break;
      case SWEEP:  // backhanded sweep across the body
        a.ctl.armR = vec3(1.1f * wind, glm::mix(0.0f, 1.0f, wind) - 1.5f * u, -0.2f);
        a.ctl.twist = 0.6f * wind - 1.1f * u;
        a.ctl.lean = 0.15f * u;
        break;
      case CLEAVE:  // the sword overhead, then straight down
        a.ctl.itemYaw = -kPI * 0.5f * std::min(1.0f, wind * 2.0f + u);
        a.ctl.armR = vec3(glm::mix(0.0f, 2.6f, wind) - 2.5f * u, 0.15f * wind, -0.4f * wind + 0.2f * u);
        a.ctl.armL = vec3(glm::mix(0.0f, 2.3f, wind) - 2.2f * u, -0.1f, -0.4f * wind);
        a.ctl.lean = -0.12f * wind + 0.4f * u;
        break;
      case BLOW:  // a level sweep of the sword
        a.ctl.itemYaw = -kPI * 0.5f * std::min(1.0f, wind * 2.0f + u);
        a.ctl.armR = vec3(1.3f * std::max(wind, u * 0.8f), glm::mix(0.0f, 1.1f, wind) - 1.6f * u, -0.2f);
        a.ctl.twist = 0.7f * wind - 1.2f * u;
        a.ctl.itemPitch = 0.6f * wind;
        a.ctl.lean = 0.15f * u;
        break;
    }
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 1.0f;
        shotHeight = 0.75f;
        if (faceOff(w)) next();
        break;
      case 1:  // glide most of the way on the throne, then rise from it
        if (sub == 0) {
          if (enter()) {
            float glide = std::max(0.0f, L - 1.35f);
            walker = Walker(plan.mover, A + dir * glide, Walker::SLIDE, 1.2f);
            walker.face = false;
            a.yawTarget = faceAngle(V - A);
          }
          if (walker.update(w, dt)) {
            sub = 1;
            entered = false;
            pt = 0;
          }
        } else if (standUp(w, 0.85f)) {
          next();
        }
        v.tremble = 0.7f;
        break;
      case 2:  // walk to the victim
        if (enter()) {
          if (L > 1.6f) battleShot(w, flat(a.pos), V, shotHeight, 0.6f, plan.mover, plan.victim);
          float off = md::standOffset(a.kind) * a.scale;
          float stopAt = v.radius() + reach() + off;
          walker = Walker(plan.mover, V - dir * std::min(stopAt, L), Walker::FOOT, 0.75f);
          if (kind_ == CLEAVE) w.sound(SFX_CLANG, 0.3f, a.pos, 1.6f);
        }
        if (kind_ == CLEAVE) strikePose(a, easeInOut(pt / 0.8f) * 0.0f, 0.0f);
        v.tremble = 0.9f;
        if (walker.update(w, dt)) next();
        break;
      case 3: {  // wind up
        if (enter()) a.yawTarget = faceAngle(V - a.pos);
        float wnd = easeInOut(pt / 0.55f);
        strikePose(a, wnd, 0.0f);
        if (kind_ == SMITE) {
          vec3 orb = bonePoint(a, md::HB_ITEMR, md::itemTip(a.kind));
          w.frameLights.push_back({orb, 2.0f, kGold * 6.0f * wnd});
          if (w.fx.rnd() < 0.5f) w.fx.magicTrail(orb, vec3(0), kGold * 2.5f);
        }
        v.tremble = 1.2f;
        v.ctl.headPitch = -0.3f * wnd;
        if (pt > 0.7f) {
          w.sound(SFX_WHOOSH, 0.9f, a.pos, kind_ == SWEEP ? 0.9f : 1.05f);
          next();
        }
        break;
      }
      case 4: {  // strike
        float u = easeIn(pt / 0.16f);
        strikePose(a, 1.0f - u * 0.6f, u);
        if (pt >= 0.12f && !struck) {
          struck = true;
          vec3 c = victimCenter(w);
          vec3 hit = c - dir * v.radius() * 0.5f + vec3(0, v.height() * 0.2f, 0);
          switch (kind_) {
            case SMITE:
              w.fx.flash(hit, 1.0f, kGold * 6.0f, 0.15f);
              w.fx.magicBurst(hit, kGold * 4.0f, 60, 2.5f);
              w.light(hit, kGold * 30.0f, 4.0f, 0.4f);
              v.crackColor = vec3(1.0f, 0.85f, 0.5f);
              w.sound(SFX_CLANG, 0.9f, hit, 0.8f);
              w.sound(SFX_ZAP, 0.6f, hit, 0.8f);
              w.shatter(v, hit + vec3(0, 0.2f, 0), vec3(0, -2.0f, 0) + dir, 2.3f, 0.8f, 10.0f);
              break;
            case SWEEP: {
              vec3 side(-dir.z, 0, dir.x);
              w.fx.sparks(hit, side + vec3(0, 0.5f, 0), 30, 3.5f, 0.6f, vec3(3.0f, 2.6f, 2.0f));
              w.sound(SFX_CLACK, 1.0f, hit, 0.75f);
              flight.launch(v, dir * 2.5f + side * 3.0f + vec3(0, 4.4f, 0), 9.0f);
              break;
            }
            case CLEAVE:
              w.fx.sparks(c, vec3(0, 1, 0), 40, 4.0f, 1.0f, vec3(4.0f, 3.0f, 1.5f));
              w.fx.flash(c, 0.9f, kGold * 5.0f, 0.12f);
              w.light(c, kGold * 25.0f, 4.0f, 0.3f);
              w.sound(SFX_CLANG, 1.0f, c);
              w.sound(SFX_SHATTER, 0.6f, c, 1.2f);
              w.slice(v, 0.9f);
              break;
            case BLOW: {
              vec3 side(-dir.z, 0, dir.x);
              w.fx.sparks(hit, side + vec3(0, 0.3f, 0), 40, 4.5f, 0.6f, vec3(4.0f, 3.0f, 1.5f));
              w.fx.flash(hit, 0.7f, kGold * 4.0f, 0.1f);
              w.sound(SFX_CLANG, 1.0f, hit, 0.9f);
              w.shatter(v, hit, side * 4.0f + dir * 1.5f + vec3(0, 1.0f, 0), 1.6f, 0.8f, 12.0f);
              break;
            }
          }
          w.shake(0.45f);
          w.slowmo(0.25f, 0.5f);
        }
        if (pt > 0.3f) next();
        break;
      }
      case 5: {  // follow-through, recover
        float u = easeInOut(pt / 0.7f);
        strikePose(a, 0.4f * (1.0f - u), 1.0f - u);
        if (kind_ == CLEAVE || kind_ == BLOW) a.ctl.itemYaw = -kPI * 0.5f * (1.0f - u);
        if (v.alive && kind_ == SWEEP && flight.update(v, dt)) crashLanding(w, v, dir);
        if (pt > 0.8f && !v.alive) {
          a.ctl.armR = a.ctl.armL = vec3(0);
          a.ctl.twist = a.ctl.lean = a.ctl.itemPitch = a.ctl.itemYaw = 0;
          next();
        }
        break;
      }
      case 6:  // walk onto the square; the throne follows; sit down
        if (ret.update(w, a, squareCenter(plan.move.to), dt)) next();
        break;
      case 7:
        return finishM(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Fast capture (battle animations off)
// ---------------------------------------------------------------------------
class MQuickCapture : public MBattle {
 public:
  using MBattle::MBattle;

 protected:
  bool step(World& w, float dt) override {
    Actor& v = w.actors[plan.victim];
    Actor& a = w.actors[plan.mover];
    switch (phase) {
      case 0:
        if (enter()) setup(w);
        v.tremble = 1.0f;
        if (pt > 0.15f) {
          w.shatter(v, victimCenter(w), dir * 1.5f, 1.4f, 0.8f);
          w.shake(0.2f);
          next();
        }
        break;
      case 1: {
        Walker::Mode m = a.kind == K_PAWN ? Walker::FOOT : (a.kind == K_KNIGHT ? Walker::HORSE : Walker::SLIDE);
        if (moveInM(w, dt, m, a.kind == K_PAWN ? 1.2f : 1.8f)) next();
        break;
      }
      case 2:
        return finishM(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Checkmate: the king rises, staggers, and topples forward off his throne.
// ---------------------------------------------------------------------------
class MCheckmate : public Staged {
 public:
  MCheckmate(int king, int winner) : king_(king), winner_(winner) {}

 protected:
  int king_, winner_;
  float theta = 0, omega = 0;
  bool step(World& w, float dt) override {
    Actor& k = w.actors[king_];
    const vec3 red(1.0f, 0.18f, 0.06f);
    switch (phase) {
      case 0: {
        if (enter()) {
          w.sound(SFX_MATE, 1.0f, k.pos);
          w.sound(SFX_BELL, 0.6f, k.pos, 0.8f);
          if (w.cinematic) {
            // in front of the king (a little to the player's side), so we see his face
            CamPose p;
            p.target = k.pos + vec3(0, 0.8f, 0) + k.forward() * 0.3f;
            vec3 player = glm::normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4f));
            vec3 back = glm::normalize(k.forward() * 1.0f + player * 0.45f + vec3(1e-4f));
            p.pos = p.target + back * 3.4f + vec3(0, 1.1f, 0);
            p.fov = 32;
            p.focus = glm::length(p.pos - p.target);
            p.aperture = 7.0f;
            w.cam.setShot(p, 0.9f);
          }
          if (k.hasSeat()) {
            k.seatFree = true;
            k.seatPos = k.pos;
            k.seatYaw = k.yaw;
          }
        }
        float u = glm::clamp(pt / 1.6f, 0.0f, 1.0f);
        if (k.hasSeat()) k.ctl.sit = 1.0f - easeInOut(glm::clamp(pt / 0.9f, 0.0f, 1.0f));
        k.ctl.lean = 0.15f * std::sin(pt * 5.0f) * u;
        k.ctl.headPitch = 0.3f * u;
        k.tremble = 0.3f + 0.6f * u;
        k.crackColor = red;
        k.crack = easeIn(u) * 0.9f;
        k.glow = glm::vec4(red, 0.6f * u);
        k.idleWeight = 0;
        w.frameLights.push_back({k.pos + vec3(0, 0.8f, 0), 3.0f, red * (3.0f + 10.0f * u)});
        if (u >= 1.0f) {
          k.tiltPivot = vec3(0, 0, md::standOffset(k.kind) + 0.12f);
          omega = 1.2f;
          w.sound(SFX_RUMBLE, 0.8f, k.pos, 0.8f);
          next();
        }
        break;
      }
      case 1: {  // topple forward
        k.tremble = 0.2f;
        omega += 8.0f * std::sin(theta + 0.2f) * dt;
        theta += omega * dt;
        k.tilt = glm::angleAxis(theta, vec3(1, 0, 0));
        if (theta > 1.2f) {
          vec3 f = k.forward();
          vec3 top = k.pos + f * (k.height() * 0.7f) + vec3(0, 0.3f, 0);
          k.seatFree = false;  // the throne breaks with him
          k.seatPos = k.pos;
          k.seatYaw = k.yaw;
          w.shatter(k, top, f * omega * 0.7f, 1.2f, 0.7f, 6.0f);
          w.fx.dust(k.pos, 30, 0.8f, 2.0f);
          w.groundRing(k.pos, red * 1.5f, 3.0f, 0.9f, 0.1f);
          w.light(k.pos + vec3(0, 0.5f, 0), red * 20.0f, 4.0f, 0.5f);
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

const char* const kMedievalStyleNames[meshgen::K_COUNT][kBattleStyles] = {
    {"Spear thrust", "Shield bash", "Javelin"},
    {"Joust", "Trample", "Sword sweep"},
    {"Wrath of heaven", "Brimstone", "Pillar of salt"},
    {"Siege stomp", "Dragon fire", "Dragon's jaws"},
    {"Throne charge", "Sceptre smite", "Sceptre sweep"},
    {"Royal cleave", "Throne ram", "Sweeping blow"},
};

}  // namespace

const char* medievalBattleStyleName(int kind, int style) {
  if (kind < 0 || kind >= meshgen::K_COUNT || style < 0 || style >= kBattleStyles) return "";
  return kMedievalStyleNames[kind][style];
}

std::unique_ptr<Anim> makeMedievalBattle(int kind, int style, const MovePlan& plan) {
  switch (kind * kBattleStyles + style) {
    case K_PAWN * kBattleStyles + 0: return std::make_unique<PawnThrust>(plan);
    case K_PAWN * kBattleStyles + 1: return std::make_unique<PawnBash>(plan);
    case K_PAWN * kBattleStyles + 2: return std::make_unique<PawnJavelin>(plan);
    case K_KNIGHT * kBattleStyles + 0: return std::make_unique<KnightJoustM>(plan);
    case K_KNIGHT * kBattleStyles + 1: return std::make_unique<KnightTrampleM>(plan);
    case K_KNIGHT * kBattleStyles + 2: return std::make_unique<KnightSweepM>(plan);
    case K_BISHOP * kBattleStyles + 0: return std::make_unique<BishopWrath>(plan);
    case K_BISHOP * kBattleStyles + 1: return std::make_unique<BishopBrimstone>(plan);
    case K_BISHOP * kBattleStyles + 2: return std::make_unique<BishopSalt>(plan);
    case K_ROOK * kBattleStyles + 0: return std::make_unique<RookStomp>(plan);
    case K_ROOK * kBattleStyles + 1: return std::make_unique<RookDragon>(plan, true);
    case K_ROOK * kBattleStyles + 2: return std::make_unique<RookDragon>(plan, false);
    case K_QUEEN * kBattleStyles + 0: return std::make_unique<ThroneRam>(plan, false);
    case K_QUEEN * kBattleStyles + 1: return std::make_unique<RoyalStrike>(plan, RoyalStrike::SMITE);
    case K_QUEEN * kBattleStyles + 2: return std::make_unique<RoyalStrike>(plan, RoyalStrike::SWEEP);
    case K_KING * kBattleStyles + 0: return std::make_unique<RoyalStrike>(plan, RoyalStrike::CLEAVE);
    case K_KING * kBattleStyles + 1: return std::make_unique<ThroneRam>(plan, true);
    default: return std::make_unique<RoyalStrike>(plan, RoyalStrike::BLOW);
  }
}

std::unique_ptr<Anim> makeMedievalQuiet(const MovePlan& plan) { return std::make_unique<MQuietMove>(plan); }
std::unique_ptr<Anim> makeMedievalQuickCapture(const MovePlan& plan) { return std::make_unique<MQuickCapture>(plan); }
std::unique_ptr<Anim> makeMedievalCheckmate(int kingId, int winnerColor) {
  return std::make_unique<MCheckmate>(kingId, winnerColor);
}
