// Building blocks shared by the battle choreography of both armies: moving a
// piece with personality, timeline phases, the cinematic battle camera and
// victims knocked flying.
#pragma once
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "animation.h"
#include "battle.h"

namespace bkit {
using glm::quat;
using glm::vec2;
using glm::vec3;
using meshgen::K_BISHOP;
using meshgen::K_KING;
using meshgen::K_KNIGHT;
using meshgen::K_PAWN;
using meshgen::K_QUEEN;
using meshgen::K_ROOK;

enum MoveStyle { MS_HOP, MS_STOMP, MS_LEAP, MS_GLIDE, MS_REGAL };

inline MoveStyle styleFor(int kind) {
  switch (kind) {
    case K_PAWN: return MS_HOP;
    case K_KNIGHT: return MS_LEAP;
    case K_BISHOP: return MS_GLIDE;
    case K_ROOK: return MS_STOMP;
    case K_QUEEN: return MS_REGAL;
    default: return MS_HOP;
  }
}

inline vec3 flat(vec3 v) {
  v.y = 0;
  return v;
}
inline vec3 flatDir(const vec3& a, const vec3& b) {
  vec3 d = flat(b - a);
  float l = glm::length(d);
  return l > 1e-4f ? d / l : vec3(0, 0, 1);
}

// Moves one actor between two points with a per-piece personality.
struct Mover {
  int actor = -1;
  vec3 from{0}, to{0};
  MoveStyle style = MS_HOP;
  float t = 0, dur = 1, hopH = 0.1f, leapH = 0;
  int hops = 1, lastLanded = -1;
  bool begun = false, faceTravel = true;

  Mover() {}
  Mover(int a, const vec3& dest, MoveStyle s, float leapHeight = 0) : actor(a), to(dest), style(s), leapH(leapHeight) {}

  void begin(World& w) {
    Actor& a = w.actors[actor];
    from = a.pos;
    from.y = 0;
    float dist = glm::length(flat(to - from));
    if (faceTravel && dist > 0.05f) a.yawTarget = faceAngle(to - from);
    float kingSlow = a.kind == K_KING ? 1.2f : 1.0f;
    switch (style) {
      case MS_HOP:
        hops = std::max(1, (int)std::round(dist / 0.62f));
        dur = hops * 0.30f * kingSlow;
        hopH = a.kind == K_KING ? 0.10f : 0.13f;
        break;
      case MS_STOMP:
        hops = std::max(1, (int)std::round(dist / 0.78f));
        dur = hops * 0.42f;
        hopH = 0.09f;
        break;
      case MS_LEAP:
        hops = 1;
        dur = 0.78f + 0.10f * dist;
        hopH = leapH > 0 ? leapH : 0.8f + 0.16f * dist;
        break;
      default:
        hops = 1;
        dur = 0.45f + 0.21f * dist;
        hopH = 0.07f;
        break;
    }
    if (dist < 0.02f) dur = 0.05f;
    begun = true;
  }

  bool update(World& w, float dt) {
    if (!begun) begin(w);
    Actor& a = w.actors[actor];
    t += dt;
    float u = glm::clamp(t / dur, 0.0f, 1.0f);
    switch (style) {
      case MS_HOP:
      case MS_STOMP: {
        float hp = u * hops;
        int hi = std::min(hops - 1, (int)std::floor(hp));
        float lu = hp - hi;
        float air = glm::clamp((lu - 0.16f) / 0.72f, 0.0f, 1.0f);
        float prog = (hi + easeInOut(air)) / hops;
        a.pos = glm::mix(from, to, prog);
        a.pos.y = 4.0f * air * (1.0f - air) * hopH;
        a.poseSquash = lu < 0.16f ? glm::mix(1.0f, style == MS_STOMP ? 0.8f : 0.86f, lu / 0.16f) : 1.0f;
        int landed = air >= 1.0f ? hi : hi - 1;
        if (u >= 1.0f) landed = hops - 1;
        if (landed > lastLanded) {
          lastLanded = landed;
          vec3 p = a.pos;
          if (style == MS_STOMP) {
            w.sound(SFX_THUD, 0.9f, p, w.fx.rnd(0.9f, 1.05f));
            w.fx.dust(p, 8, a.radius(), 0.8f);
            w.shake(0.10f);
          } else {
            w.sound(SFX_STEP, 0.55f, p, a.kind == K_PAWN ? w.fx.rnd(1.15f, 1.3f) : w.fx.rnd(0.85f, 0.95f));
            w.fx.dust(p, 3, a.radius() * 0.8f, 0.4f);
          }
        }
        break;
      }
      case MS_LEAP: {
        float crouch = 0.2f, land = 0.9f;
        if (u < crouch) {
          float c = u / crouch;
          a.pos = from;
          a.poseSquash = glm::mix(1.0f, 0.72f, easeOut(c));
          a.poseBend = vec2(0, -0.25f * c);
        } else {
          float air = glm::clamp((u - crouch) / (land - crouch), 0.0f, 1.0f);
          float prog = easeInOut(air) * 0.85f + air * 0.15f;
          a.pos = glm::mix(from, to, prog);
          a.pos.y = 4.0f * air * (1.0f - air) * hopH;
          a.poseSquash = 1.0f;
          a.poseBend = vec2(0, glm::mix(-0.35f, 0.25f, air));
          if (air >= 1.0f && lastLanded < 0) {
            lastLanded = 0;
            w.sound(SFX_THUD, 0.8f, a.pos, 1.15f);
            w.fx.dust(a.pos, 10, a.radius(), 1.0f);
            w.shake(0.08f);
          }
          if (u >= land) a.poseBend = vec2(0);
        }
        break;
      }
      case MS_GLIDE:
      case MS_REGAL: {
        float e = easeInOut(u);
        a.pos = glm::mix(from, to, e);
        a.pos.y = hopH * std::sin(kPI * u);
        if (style == MS_REGAL) a.poseTwist = std::sin(kPI * u) * 0.9f;
        if (u >= 1.0f && lastLanded < 0) {
          lastLanded = 0;
          w.sound(SFX_STEP, 0.45f, a.pos, 0.8f);
        }
        break;
      }
    }
    if (u >= 1.0f) {
      a.pos = to;
      a.pos.y = 0;
      a.poseSquash = 1;
      a.poseBend = vec2(0);
      a.poseTwist = 0;
      return true;
    }
    return false;
  }
};

// closest distance between segments p1-q1 and p2-q2
inline float segSegDist(const vec3& p1, const vec3& q1, const vec3& p2, const vec3& q2) {
  vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
  float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
  float s = 0, t = 0;
  if (a <= 1e-8f && e <= 1e-8f) return glm::length(r);
  if (a <= 1e-8f) {
    t = glm::clamp(f / e, 0.0f, 1.0f);
  } else {
    float c = glm::dot(d1, r);
    if (e <= 1e-8f) {
      s = glm::clamp(-c / a, 0.0f, 1.0f);
    } else {
      float b = glm::dot(d1, d2), den = a * e - b * b;
      s = den > 1e-8f ? glm::clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0.0f;
      t = (b * s + f) / e;
      if (t < 0) {
        t = 0;
        s = glm::clamp(-c / a, 0.0f, 1.0f);
      } else if (t > 1) {
        t = 1;
        s = glm::clamp((b - c) / a, 0.0f, 1.0f);
      }
    }
  }
  return glm::length((p1 + d1 * s) - (p2 + d2 * t));
}

// Picks a cinematic camera that keeps both fighters in clear view.
inline void battleShot(World& w, const vec3& A, const vec3& V, float heightBias = 0.55f, float extra = 0.0f,
                int attacker = -1, int victim = -1) {
  if (!w.cinematic) return;
  vec3 mid = A * 0.42f + V * 0.58f;
  vec3 dir = flatDir(A, V);
  float L = glm::length(flat(V - A));
  vec3 side(-dir.z, 0, dir.x);
  vec3 orbitCam = w.cam.orbitPose().pos;
  vec3 toViewer = glm::normalize(flat(orbitCam - mid) + vec3(1e-4f));
  vec3 target = mid + vec3(0, heightBias, 0);
  float dist = 3.3f + L * 0.45f + extra;
  float baseH = 1.0f + L * 0.15f + extra * 0.3f;
  std::vector<vec3> look = {A + vec3(0, 0.6f, 0), V + vec3(0, 0.5f, 0), target};
  float bestScore = 1e9f;
  CamPose best;
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    for (int k = -4; k <= 4; ++k) {
      float ang = glm::radians(18.0f * k);
      vec3 sd = side * (float)sgn;
      vec3 offDir(std::cos(ang) * sd.x - std::sin(ang) * sd.z, 0, std::sin(ang) * sd.x + std::cos(ang) * sd.z);
      for (int e = 0; e < 3; ++e) {
        vec3 cam = target + offDir * dist * (1.0f - 0.08f * e) + vec3(0, baseH + 0.75f * e, 0);
        float occ = 0;
        for (const Actor& a : w.actors) {
          if (!a.alive || !a.visible || a.id == attacker || a.id == victim) continue;
          vec3 b0 = a.pos, b1 = a.pos + vec3(0, a.height(), 0);
          float r = a.radius() * 1.05f;
          if (glm::length(flat(cam - a.pos)) < r + 0.35f && cam.y < b1.y + 0.3f) occ += 6.0f;
          for (const vec3& p : look) {
            float d = segSegDist(cam, p, b0, b1);
            if (d < r) {
              // pieces close to the lens hide much more of the frame
              float near = glm::length(flat(a.pos - cam)) / std::max(0.1f, glm::length(flat(p - cam)));
              occ += near < 0.5f ? 2.5f : 1.0f;
            }
          }
        }
        float score = occ * 4.0f;
        score += (1.0f - glm::dot(offDir, toViewer)) * 1.6f;  // stay on the player's side
        score += std::fabs(glm::dot(offDir, dir)) * 1.2f;      // prefer a side-on view
        score += e * 0.6f;                                     // prefer lower, more dramatic angles
        if (score < bestScore) {
          bestScore = score;
          best.pos = cam;
        }
      }
    }
  }
  best.target = target;
  best.fov = 34.0f;
  best.focus = glm::length(best.pos - best.target);
  best.aperture = 7.0f;
  w.cam.setShot(best, 0.75f);
}

// ---------------------------------------------------------------------------
// Timeline-based helper base
// ---------------------------------------------------------------------------
class Staged : public Anim {
 public:
  bool update(World& w, float dt) override {
    if (dt <= 0) return false;
    pt += dt;
    total += dt;
    bool done = step(w, dt);
    first = false;
    return done;
  }

 protected:
  virtual bool step(World& w, float dt) = 0;
  void next() {
    phase++;
    pt = 0;
    first = true;
    entered = false;
  }
  bool enter() {
    if (!entered) {
      entered = true;
      return true;
    }
    return false;
  }
  int phase = 0;
  float pt = 0, total = 0;
  bool first = true, entered = false;
};

// ---------------------------------------------------------------------------
// Shared battle prelude / epilogue
// ---------------------------------------------------------------------------
class BattleBase : public Staged {
 public:
  BattleBase(const MovePlan& p) : plan(p) {}

 protected:
  MovePlan plan;
  vec3 A{0}, V{0}, dir{0};
  float L = 0;
  bool released = false;
  Mover advance;

  void setup(World& w) {
    A = w.actors[plan.mover].pos;
    A.y = 0;
    V = w.actors[plan.victim].pos;
    V.y = 0;
    dir = flatDir(A, V);
    L = glm::length(flat(V - A));
  }
  // returns true when the face-off is over
  float shotHeight = 0.55f, shotExtra = 0.0f;
  bool faceOff(World& w, float duration = 0.45f) {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    if (enter()) {
      setup(w);
      a.yawTarget = faceAngle(V - A);
      v.yawTarget = faceAngle(A - V);
      battleShot(w, A, V, shotHeight, shotExtra, plan.mover, plan.victim);
    }
    v.tremble = glm::clamp(pt / duration, 0.0f, 1.0f) * 0.6f;
    v.idleWeight = 0;
    return pt >= duration;
  }
  bool moveIn(World& w, float dt) {
    if (enter()) {
      MoveStyle s = styleFor(w.actors[plan.mover].kind);
      advance = Mover(plan.mover, squareCenter(plan.move.to), s);
    }
    return advance.update(w, dt);
  }
  bool finish(World& w) {
    Actor& a = w.actors[plan.mover];
    if (enter()) {
      a.yawTarget = a.baseYaw;
      if (w.cinematic) w.cam.release(1.0f);
      a.glow = glm::vec4(0);
      a.tilt = quat(1, 0, 0, 0);
      a.tiltPivot = vec3(0);
      a.poseTwist = 0;
      a.idleWeight = 1;
    }
    return pt > 0.25f;
  }
  vec3 victimCenter(World& w) {
    Actor& v = w.actors[plan.victim];
    return v.pos + vec3(0, v.height() * 0.5f, 0);
  }
};

// ===========================================================================
//  More battle styles. Each capture picks one of its piece's styles at random.
// ===========================================================================

// centre distance at which two pieces standing face to face just touch
inline float touchDist(const Actor& a, const Actor& b) { return (a.radius() + b.radius()) * 0.92f; }
inline bool overBoard(const vec3& p) { return std::fabs(p.x) < 4.5f && std::fabs(p.z) < 4.5f; }

// A victim knocked into the air: it tumbles backwards and bursts where it
// lands - or keeps falling if it has been knocked clean off the floating board.
struct Flight {
  vec3 vel{0};
  float spin = 0, angle = 0, t = 0;
  void launch(Actor& v, const vec3& velocity, float spinRate) {
    vel = velocity;
    spin = spinRate;
    angle = t = 0;
    v.tremble = 0;
    v.idleWeight = 0;
    v.tiltPivot = vec3(0, v.height() / std::max(v.scale, 1e-3f) * 0.5f, 0);
  }
  // true once it hits the board (or has dropped well below it)
  bool update(Actor& v, float dt) {
    t += dt;
    vel.y -= 14.0f * dt;
    v.pos += vel * dt;
    angle += spin * dt;
    v.tilt = glm::angleAxis(-angle, vec3(1, 0, 0));
    float hh = v.height() * 0.5f;
    float low = v.pos.y + hh - (std::fabs(std::cos(angle)) * hh + std::fabs(std::sin(angle)) * v.radius());
    if (overBoard(v.pos) && low <= 0.0f && vel.y < 0.0f) return true;
    return v.pos.y < -1.5f || t > 3.0f;
  }
};

// shatters a flying victim where it came down
inline void crashLanding(World& w, Actor& v, const vec3& dir) {
  vec3 c = v.pos + vec3(0, v.height() * 0.5f, 0);
  bool onBoard = overBoard(v.pos) && v.pos.y > -0.5f;
  w.shatter(v, c - vec3(0, 0.2f, 0), dir * 1.5f + vec3(0, -1.0f, 0), 1.5f, 0.8f, 9.0f);
  if (onBoard) {
    vec3 g = flat(v.pos);
    w.fx.dust(g, 20, 0.6f, 1.8f);
    w.groundRing(g, vec3(0.8f, 0.7f, 0.55f), 2.8f, 0.5f, 0.07f);
    w.sound(SFX_THUD, 1.0f, c, 0.8f);
    w.shake(0.35f);
  }
}

// dust whipped round in a little whirlwind
inline void whirlDust(World& w, const vec3& c, float radius) {
  for (int i = 0; i < 2; ++i) {
    Particle p;
    float ang = w.fx.rnd(0.0f, 6.2831853f);
    vec3 d(std::cos(ang), 0, std::sin(ang));
    p.pos = c + d * radius * w.fx.rnd(0.6f, 1.0f) + vec3(0, w.fx.rnd(0.0f, 0.1f), 0);
    p.vel = vec3(-d.z, 0, d.x) * w.fx.rnd(1.5f, 2.5f) + vec3(0, w.fx.rnd(0.3f, 0.9f), 0);
    p.size = w.fx.rnd(0.05f, 0.11f);
    p.grow = 0.25f;
    p.color = vec3(0.55f, 0.5f, 0.45f);
    p.colorEnd = p.color * 0.9f;
    p.opacity = w.fx.rnd(0.12f, 0.25f);
    p.maxLife = w.fx.rnd(0.6f, 1.1f);
    p.type = PT_SMOKE;
    p.drag = 2.0f;
    p.rot = w.fx.rnd(0.0f, 6.28f);
    p.rotVel = 3.0f;
    p.seed = w.fx.rnd();
    p.fadeIn = 0.08f;
    w.fx.add(p);
  }
}

// glowing motes streaming from one point towards another
inline void stream(World& w, const vec3& from, const vec3& to, const vec3& col, int n, float speed) {
  for (int i = 0; i < n; ++i) {
    Particle p;
    p.pos = from + w.fx.rndDir() * 0.06f;
    p.vel = (to - from) * speed * w.fx.rnd(0.8f, 1.2f) + w.fx.rndDir() * 0.25f;
    p.size = w.fx.rnd(0.008f, 0.02f);
    p.color = col * w.fx.rnd(0.6f, 1.4f);
    p.colorEnd = col * 0.2f;
    p.opacity = 0;
    p.maxLife = std::min(0.9f, 1.0f / speed) * w.fx.rnd(0.8f, 1.05f);
    p.type = PT_GLOW;
    p.drag = 0.2f;
    w.fx.add(p);
  }
}

// orientation that points a prop's +y axis along `along`
inline quat aimY(const vec3& along, const vec3& hint = vec3(0, 1, 0)) {
  vec3 y = glm::normalize(along);
  vec3 h = std::fabs(glm::dot(y, hint)) > 0.95f ? vec3(1, 0, 0) : hint;
  vec3 x = glm::normalize(glm::cross(y, h));
  vec3 z = glm::cross(x, y);
  return glm::quat_cast(glm::mat3(x, y, z));
}

}  // namespace bkit
