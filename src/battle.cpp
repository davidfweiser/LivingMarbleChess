#include "battle.h"

#include "battle_kit.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <random>

using glm::quat;
using glm::vec2;
using glm::vec3;
using meshgen::K_BISHOP;
using meshgen::K_KING;
using meshgen::K_KNIGHT;
using meshgen::K_PAWN;
using meshgen::K_QUEEN;
using meshgen::K_ROOK;

namespace {
using namespace bkit;

// ---------------------------------------------------------------------------
class QuietMove : public Staged {
 public:
  QuietMove(const MovePlan& p) : plan(p) {}

 protected:
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    switch (phase) {
      case 0:
        if (enter()) {
          mover = Mover(plan.mover, squareCenter(plan.move.to), styleFor(a.kind));
          if (plan.rook >= 0) {
            rookMover = Mover(plan.rook, squareCenter(plan.rookTo), MS_LEAP, 0.95f);
          }
        }
        {
          bool d1 = mover.update(w, dt);
          bool d2 = true;
          if (plan.rook >= 0) {
            if (pt > 0.25f) d2 = rookMover.update(w, dt);
            else d2 = false;
          }
          if (d1 && d2) next();
        }
        break;
      case 1:
        if (enter()) {
          a.yawTarget = a.baseYaw;
          if (plan.rook >= 0) w.actors[plan.rook].yawTarget = w.actors[plan.rook].baseYaw;
        }
        if (pt > 0.12f) return true;
        break;
    }
    return false;
  }
  MovePlan plan;
  Mover mover, rookMover;
};

// ---------------------------------------------------------------------------
// Pawn: hop up, wind up, head-butt. The victim topples and bursts on the board.
// ---------------------------------------------------------------------------
class PawnBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  Mover approach;
  float theta = 0, omega = 0;
  vec3 victimStart{0};
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1:  // approach
        if (enter()) approach = Mover(plan.mover, V - dir * 0.68f, MS_HOP);
        v.tremble = 0.7f;
        if (approach.update(w, dt)) next();
        break;
      case 2:  // wind up
        if (enter()) a.yawTarget = faceAngle(V - a.pos);
        a.poseSquash = glm::mix(1.0f, 0.78f, easeOut(pt / 0.35f));
        a.poseBend = vec2(0, -0.38f * easeOut(pt / 0.35f));
        v.tremble = 1.0f;
        if (pt > 0.38f) next();
        break;
      case 3: {  // strike
        float u = glm::clamp(pt / 0.11f, 0.0f, 1.0f);
        a.poseSquash = glm::mix(0.78f, 1.12f, u);
        a.poseBend = vec2(0, glm::mix(-0.38f, 0.8f, easeIn(u)));
        a.pos = V - dir * glm::mix(0.68f, 0.56f, u);
        if (u >= 1.0f) {
          vec3 hit = V - dir * 0.24f + vec3(0, std::min(v.height(), a.height()) * 0.62f, 0);
          w.fx.sparks(hit, -dir + vec3(0, 0.4f, 0), 26, 3.5f, 0.6f, vec3(3.0f, 2.6f, 2.0f));
          w.fx.chips(hit, 18, 2.5f, v.color == 1);
          w.fx.flash(hit, 0.35f, vec3(2.0f, 1.8f, 1.5f), 0.08f);
          w.light(hit, vec3(6.0f, 5.0f, 4.0f), 2.5f, 0.15f);
          w.sound(SFX_CLACK, 1.0f, hit, 0.9f);
          w.shake(0.28f);
          w.slowmo(0.3f, 0.3f);
          v.tremble = 0;
          v.tiltPivot = vec3(0, 0, -v.radius() / v.scale * 0.92f);
          omega = 3.6f;
          theta = 0;
          victimStart = v.pos;
          next();
        }
        break;
      }
      case 4: {  // topple
        a.poseBend = vec2(0, 0.25f);
        a.poseSquash = 1.0f;
        omega += 11.0f * std::sin(theta + 0.25f) * dt;
        theta += omega * dt;
        v.tilt = glm::angleAxis(-theta, vec3(1, 0, 0));
        v.pos = victimStart + dir * 0.18f * easeOut(pt / 0.4f);
        if (theta > 1.22f) {
          vec3 top = v.pos + dir * v.height() * 0.7f + vec3(0, v.height() * 0.3f, 0);
          w.shatter(v, top, dir * (omega * 0.6f) + vec3(0, -1.0f, 0), 1.1f, 0.6f, 7.0f);
          w.fx.dust(v.pos + dir * 0.6f, 12, 0.5f, 1.4f);
          w.groundRing(v.pos + dir * 0.5f, vec3(0.8f, 0.7f, 0.55f), 2.5f, 0.45f, 0.06f);
          w.shake(0.3f);
          next();
        }
        break;
      }
      case 5:
        a.poseBend = vec2(0);
        if (pt > 0.55f) next();
        break;
      case 6:
        if (moveIn(w, dt)) next();
        break;
      case 7:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Knight: leaps high over the board and tramples the victim from above.
// ---------------------------------------------------------------------------
class KnightBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  float vy = 0, landH = 0, dur = 1;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 0.85f;
        shotExtra = 1.3f;
        if (faceOff(w)) next();
        break;
      case 1:  // crouch
        a.poseSquash = glm::mix(1.0f, 0.68f, easeOut(pt / 0.3f));
        a.poseBend = vec2(0, -0.3f * easeOut(pt / 0.3f));
        v.tremble = 0.8f;
        if (pt > 0.3f) {
          w.sound(SFX_WHOOSH, 0.6f, a.pos, 0.8f);
          w.fx.dust(a.pos, 8, 0.4f, 1.0f);
          next();
        }
        break;
      case 2: {  // leap
        if (enter()) {
          dur = 0.85f + 0.08f * L;
          landH = v.height() * 0.88f;
        }
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        float apex = 1.2f + 0.1f * L;
        vec3 p = glm::mix(A, V, easeInOut(u) * 0.8f + u * 0.2f);
        p.y = 4.0f * u * (1.0f - u) * apex + u * u * landH;
        a.pos = p;
        a.poseSquash = 1.0f;
        a.poseBend = vec2(0, glm::mix(-0.25f, -0.55f, u));  // rearing up
        v.poseBend = vec2(0, -0.35f * u);                     // victim looks up
        v.tremble = 1.0f;
        if (u >= 1.0f) {
          vec3 c = V + vec3(0, landH, 0);
          w.shatter(v, c, vec3(0, -2.5f, 0), 2.6f, 0.25f, 8.0f);
          w.groundRing(V, vec3(1.6f, 1.3f, 0.9f), 3.2f, 0.7f, 0.1f);
          w.fx.dust(V, 26, 0.7f, 2.4f);
          w.fx.sparks(c, vec3(0, 1, 0), 20, 3.0f, 1.0f);
          w.light(c, vec3(6.0f, 5.0f, 4.0f), 3.0f, 0.2f);
          w.sound(SFX_EXPLODE, 0.7f, c, 1.3f);
          w.shake(0.55f);
          w.slowmo(0.28f, 0.4f);
          vy = -1.0f;
          next();
        }
        break;
      }
      case 3: {  // drop to the board
        vy -= 14.0f * dt;
        a.pos.y = std::max(0.0f, a.pos.y + vy * dt);
        a.poseBend = vec2(0, 0.2f);
        if (a.pos.y <= 0.0f) {
          w.sound(SFX_THUD, 1.0f, a.pos, 0.85f);
          w.fx.dust(a.pos, 14, 0.5f, 1.6f);
          w.shake(0.3f);
          next();
        }
        break;
      }
      case 4:  // triumphant rear
        a.poseBend = vec2(0, -0.45f * std::sin(kPI * glm::clamp(pt / 0.6f, 0.0f, 1.0f)));
        if (pt > 0.7f) {
          a.poseBend = vec2(0);
          next();
        }
        break;
      case 5:
        if (moveIn(w, dt)) next();  // already there; settles yaw
        break;
      case 6:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Bishop: rises, gathers holy light and smites; the victim cracks with light.
// ---------------------------------------------------------------------------
class BishopBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  vec3 orb{0}, orbFrom{0}, orbTo{0};
  float orbDur = 0.4f;
  const vec3 gold{1.0f, 0.82f, 0.48f};
  vec3 head(World& w) {
    Actor& a = w.actors[plan.mover];
    return a.pos + vec3(0, a.height() * 0.97f, 0) + a.forward() * 0.05f;
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // rise and charge
        if (enter()) w.sound(SFX_CHARGE, 0.9f, a.pos);
        float u = glm::clamp(pt / 1.0f, 0.0f, 1.0f);
        a.pos.y = 0.34f * easeInOut(u) + 0.02f * std::sin(w.time * 6.0f);
        a.glow = glm::vec4(gold, 1.3f * u);
        a.poseBend = vec2(0, -0.12f * u);
        a.idleWeight = 0;
        w.frameLights.push_back({head(w), 3.0f, gold * (2.0f + 14.0f * u)});
        w.fx.swirl(head(w), 0.22f * (1.2f - u), gold * 3.0f, w.time);
        v.tremble = 0.5f + 0.5f * u;
        if (u >= 1.0f) {
          orbFrom = head(w);
          orbTo = victimCenter(w);
          orbDur = glm::clamp(glm::length(orbTo - orbFrom) / 8.0f, 0.18f, 0.7f);
          w.sound(SFX_ZAP, 1.0f, orbFrom);
          w.fx.magicBurst(orbFrom, gold * 4.0f, 30, 2.0f);
          next();
        }
        break;
      }
      case 2: {  // bolt of light
        a.poseBend = vec2(0, 0.22f);
        a.pos.y = 0.34f + 0.02f * std::sin(w.time * 6.0f);
        float u = glm::clamp(pt / orbDur, 0.0f, 1.0f);
        vec3 prev = orb;
        orb = glm::mix(orbFrom, orbTo, u);
        w.frameLights.push_back({orb, 3.5f, gold * 22.0f});
        w.fx.flash(orb, 0.16f, gold * 6.0f, 0.06f);
        w.fx.magicTrail(orb, (orb - prev) / std::max(dt, 1e-4f), gold * 4.0f);
        w.fx.bolt(orbFrom, orb, gold * 3.0f, 0.03f, 6);
        if (u >= 1.0f) {
          w.fx.magicBurst(orb, gold * 5.0f, 40, 2.5f);
          w.fx.flash(orb, 0.8f, gold * 6.0f, 0.15f);
          w.sound(SFX_CRACKLE, 0.8f, orb, 1.3f);
          w.shake(0.2f);
          v.crackColor = vec3(1.0f, 0.86f, 0.55f);
          next();
        }
        break;
      }
      case 3: {  // light bursts through cracks
        float u = glm::clamp(pt / 0.8f, 0.0f, 1.0f);
        if (enter()) w.sound(SFX_RUMBLE, 0.7f, V, 1.4f);
        v.crack = easeIn(u);
        v.glow = glm::vec4(gold, 0.8f * u);
        v.tremble = 1.4f;
        a.glow = glm::vec4(gold, 1.3f * (1.0f - u * 0.6f));
        w.frameLights.push_back({victimCenter(w), 3.0f, gold * (4.0f + 16.0f * u)});
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          w.shatter(v, c, vec3(0), 2.7f, 1.3f, 9.0f);
          w.fx.flash(c, 1.6f, gold * 8.0f, 0.25f);
          w.fx.magicBurst(c, gold * 5.0f, 90, 3.0f);
          w.groundRing(V, gold * 1.6f, 3.0f, 0.7f, 0.08f);
          w.light(c, gold * 40.0f, 5.0f, 0.5f);
          w.addFlash(gold * 0.15f);
          w.sound(SFX_EXPLODE, 0.6f, c, 1.5f);
          w.shake(0.4f);
          w.slowmo(0.33f, 0.4f);
          next();
        }
        break;
      }
      case 4: {  // descend
        float u = glm::clamp(pt / 0.55f, 0.0f, 1.0f);
        a.pos.y = 0.34f * (1.0f - easeInOut(u));
        a.glow = glm::vec4(gold, 0.5f * (1.0f - u));
        a.poseBend = vec2(0, 0.22f * (1.0f - u));
        if (u >= 1.0f) {
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 5:
        if (moveIn(w, dt)) next();
        break;
      case 6:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Rook: the tower is a cannon. Boom, iron ball, explosion.
// ---------------------------------------------------------------------------
class RookBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  int ball = -1;
  vec3 p0{0}, v0{0};
  float T = 0.6f;
  const float g = 9.8f;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // rumble
        if (enter()) w.sound(SFX_RUMBLE, 0.9f, a.pos);
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.tremble = 0.6f * u;
        a.poseSquash = glm::mix(1.0f, 0.9f, u);
        if (w.fx.rnd() < 0.4f) w.fx.smoke(a.pos + vec3(0, a.height(), 0), 1, 0.05f, 0.4f, 0.4f);
        v.tremble = 0.8f;
        if (u >= 1.0f) {
          a.tremble = 0;
          p0 = a.pos + vec3(0, a.height() * 1.02f, 0) + dir * 0.05f;
          vec3 target = victimCenter(w);
          T = 0.48f + 0.07f * L;
          v0 = (target - p0) / T + vec3(0, 0.5f * g * T, 0);
          ball = w.addProp(Prop::CANNONBALL);
          Prop* b = w.prop(ball);
          b->pos = p0;
          b->heat = 1.0f;
          // muzzle blast
          w.fx.flash(p0, 0.9f, vec3(8.0f, 4.5f, 1.8f), 0.12f);
          w.fx.smoke(p0, 7, 0.12f, 0.6f, 0.65f);
          w.fx.sparks(p0, glm::normalize(v0), 30, 4.0f, 0.5f);
          w.fx.embers(p0, 12, 1.5f);
          w.light(p0, vec3(30.0f, 15.0f, 5.0f), 4.0f, 0.2f);
          w.sound(SFX_BOOM, 1.0f, p0);
          w.shake(0.35f);
          a.sq.v -= 4.0f;  // recoil squash
          next();
        }
        break;
      }
      case 2: {  // flight
        float u = pt;
        a.pos = A - dir * 0.12f * std::exp(-u * 4.0f) * std::sin(std::min(u * 20.0f, kPI));
        a.poseSquash = 1.0f;
        Prop* b = w.prop(ball);
        if (b) {
          vec3 prev = b->pos;
          float tt = std::min(u, T);
          b->pos = p0 + v0 * tt + vec3(0, -0.5f * g * tt * tt, 0);
          vec3 vel = (b->pos - prev) / std::max(dt, 1e-4f);
          b->rot = glm::normalize(glm::angleAxis(dt * 12.0f, glm::normalize(glm::cross(vel, vec3(0, 1, 0)) + vec3(1e-4f))) * b->rot);
          b->heat = glm::mix(1.0f, 0.55f, u / T);
          w.frameLights.push_back({b->pos, 1.5f, vec3(3.0f, 1.0f, 0.25f)});
          if (w.fx.rnd() < 0.8f) w.fx.smoke(b->pos, 1, 0.02f, 0.05f, 0.7f);
          w.fx.embers(b->pos, 1, 0.3f);
        }
        v.tremble = 1.0f;
        if (u >= T) {
          vec3 c = victimCenter(w);
          vec3 vb = glm::normalize(v0 + vec3(0, -g * T, 0));
          if (b) b->alive = false;
          w.shatter(v, c - vb * 0.1f, vb * 3.4f + vec3(0, 1.0f, 0), 1.7f, 1.5f, 10.0f);
          w.fx.fireball(c, 1.0f);
          w.groundRing(V, vec3(2.4f, 1.1f, 0.35f), 3.5f, 0.6f, 0.1f);
          w.light(c, vec3(40.0f, 18.0f, 6.0f), 5.0f, 0.55f);
          w.addFlash(vec3(0.25f, 0.12f, 0.04f));
          w.sound(SFX_EXPLODE, 1.0f, c);
          w.shake(0.6f);
          w.slowmo(0.3f, 0.4f);
          next();
        }
        break;
      }
      case 3:
        a.pos = A;
        if (pt > 0.6f) next();
        break;
      case 4:
        if (moveIn(w, dt)) next();
        break;
      case 5:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Queen: telekinesis. Lifts the victim in a storm of lightning and crushes it.
// ---------------------------------------------------------------------------
class QueenBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  const vec3 violet{0.72f, 0.38f, 1.0f};
  float boltT = 0, crackleT = 0;
  vec3 crown(World& w) {
    Actor& a = w.actors[plan.mover];
    return a.pos + vec3(0, a.height() * 0.93f, 0);
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 0.95f;
        shotExtra = 0.9f;
        if (faceOff(w)) next();
        break;
      case 1: {  // raise hands (rise + twirl)
        if (enter()) w.sound(SFX_CHARGE, 0.8f, a.pos, 0.8f);
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.pos.y = 0.18f * easeInOut(u);
        a.poseTwist = std::sin(u * kPI) * 1.4f;
        a.glow = glm::vec4(violet, 1.1f * u);
        a.idleWeight = 0;
        w.frameLights.push_back({crown(w), 3.0f, violet * (3.0f + 8.0f * u)});
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // levitate the victim
        float u = glm::clamp(pt / 1.5f, 0.0f, 1.0f);
        a.pos.y = 0.18f + 0.02f * std::sin(w.time * 5.0f);
        a.poseBend = vec2(0, -0.15f);
        v.pos.y = 1.05f * easeOut(u);
        v.yawTarget = v.yaw + 1.8f;
        v.yawRate = 3.0f;
        v.tremble = 0.6f + 0.8f * u;
        v.glow = glm::vec4(violet, 0.5f + 0.5f * u);
        v.idleWeight = 0;
        v.tilt = glm::angleAxis(0.25f * std::sin(w.time * 3.0f) * u, glm::normalize(vec3(1, 0, 0.4f)));
        v.tiltPivot = vec3(0, v.height() * 0.5f / v.scale, 0);
        boltT -= dt;
        if (boltT <= 0) {
          boltT = w.fx.rnd(0.04f, 0.09f);
          w.fx.bolt(crown(w), victimCenter(w) + w.fx.rndDir() * 0.12f, violet * 7.0f, 0.16f, 16);
          if (w.fx.rnd() < 0.5f) w.fx.bolt(crown(w), victimCenter(w) + w.fx.rndDir() * 0.2f, violet * 4.0f, 0.22f, 14);
        }
        crackleT -= dt;
        if (crackleT <= 0) {
          crackleT = w.fx.rnd(0.25f, 0.4f);
          w.sound(SFX_CRACKLE, 0.6f, victimCenter(w), w.fx.rnd(0.9f, 1.2f));
        }
        float flick = 0.6f + 0.4f * w.fx.rnd();
        w.frameLights.push_back({crown(w), 3.5f, violet * 10.0f * flick});
        w.frameLights.push_back({victimCenter(w), 3.5f, violet * 12.0f * flick});
        w.fx.swirl(victimCenter(w), v.radius() * 1.4f, violet * 3.0f, w.time);
        if (u >= 1.0f) {
          v.crackColor = vec3(0.85f, 0.55f, 1.0f);
          next();
        }
        break;
      }
      case 3: {  // crush
        float u = glm::clamp(pt / 0.2f, 0.0f, 1.0f);
        v.poseSquash = glm::mix(1.0f, 0.55f, easeIn(u));
        v.crack = u;
        a.poseBend = vec2(0, 0.3f * u);
        w.frameLights.push_back({victimCenter(w), 3.5f, violet * 20.0f});
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          v.poseSquash = 1;
          w.shatter(v, c, vec3(0), 2.1f, 0.5f, 10.0f);
          w.fx.flash(c, 1.3f, violet * 6.0f, 0.2f);
          w.fx.magicBurst(c, violet * 5.0f, 80, 3.0f);
          w.groundRing(V, violet * 1.5f, 3.0f, 0.8f, 0.08f);
          w.light(c, violet * 35.0f, 5.0f, 0.5f);
          w.addFlash(violet * 0.12f);
          w.sound(SFX_EXPLODE, 0.7f, c, 1.2f);
          w.shake(0.45f);
          w.slowmo(0.33f, 0.45f);
          next();
        }
        break;
      }
      case 4: {  // descend
        float u = glm::clamp(pt / 0.5f, 0.0f, 1.0f);
        a.pos.y = 0.18f * (1.0f - easeInOut(u));
        a.glow = glm::vec4(violet, 0.9f * (1.0f - u));
        a.poseBend = vec2(0, 0.3f * (1.0f - u));
        if (u >= 1.0f) {
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 5:
        if (moveIn(w, dt)) next();
        break;
      case 6:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// King: summons a golden blade and cleaves the victim clean in two.
// ---------------------------------------------------------------------------
class KingBattle : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  int sword = -1;
  Mover approach;
  float theta = 0;
  bool sliced = false;
  const vec3 gold{1.0f, 0.8f, 0.42f};
  void placeSword(World& w, float th) {
    Actor& v = w.actors[plan.victim];
    Prop* s = w.prop(sword);
    if (!s) return;
    const meshgen::PieceAssets& P = w.lib->piece[v.kind];
    vec3 n = P.sliceN;
    vec3 t = glm::normalize(vec3(-n.y, n.x, 0));
    float hm = P.slicePoint.y;
    vec3 b = std::cos(th) * vec3(0, 0, -1) + std::sin(th) * t;
    vec3 y = glm::normalize(b), z = n, x = glm::normalize(glm::cross(y, z));
    glm::mat3 Rl(x, y, z);
    glm::quat Rv = glm::angleAxis(v.yaw, vec3(0, 1, 0));
    float dz = std::max(0.62f, glm::length(flat(w.actors[plan.mover].pos - v.pos)) * 0.62f);
    vec3 hiltLocal = vec3(0, hm + 0.08f, dz);
    s->pos = v.pos + Rv * (hiltLocal * v.scale);
    s->rot = Rv * glm::quat_cast(Rl);
    s->scale = 1.35f;
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1:
        if (enter()) approach = Mover(plan.mover, A, MS_HOP);
        v.tremble = 0.8f;
        if (approach.update(w, dt)) {
          v.yawTarget = faceAngle(a.pos - v.pos);
          v.yaw = v.yawTarget;
          next();
        }
        break;
      case 2: {  // summon the blade
        if (enter()) {
          sword = w.addProp(Prop::SWORD);
          w.sound(SFX_CHIME, 0.8f, a.pos, 0.9f);
        }
        float u = glm::clamp(pt / 0.7f, 0.0f, 1.0f);
        theta = glm::radians(95.0f);
        placeSword(w, theta);
        if (Prop* s = w.prop(sword)) {
          s->dissolve = 1.0f - u;
          s->glow = glm::vec4(gold, 1.6f - u);
          w.fx.magicTrail(s->pos + s->rot * vec3(0, 0.5f, 0), vec3(0), gold * 3.0f);
          w.frameLights.push_back({s->pos + s->rot * vec3(0, 0.5f, 0), 2.5f, gold * (6.0f * (1.2f - u))});
        }
        a.glow = glm::vec4(gold, 0.5f * u);
        v.tremble = 1.0f;
        if (u >= 1.0f) next();
        break;
      }
      case 3: {  // wind up
        float u = glm::clamp(pt / 0.35f, 0.0f, 1.0f);
        theta = glm::radians(glm::mix(95.0f, 128.0f, easeOut(u)));
        placeSword(w, theta);
        a.poseBend = vec2(0, -0.22f * u);
        if (Prop* s = w.prop(sword)) s->glow = glm::vec4(gold, 0.6f);
        if (u >= 1.0f) {
          w.sound(SFX_WHOOSH, 1.0f, a.pos, 1.1f);
          next();
        }
        break;
      }
      case 4: {  // swing
        float u = glm::clamp(pt / 0.22f, 0.0f, 1.0f);
        float prevTheta = theta;
        theta = glm::radians(glm::mix(128.0f, -105.0f, easeIn(u)));
        placeSword(w, theta);
        a.poseBend = vec2(0, glm::mix(-0.22f, 0.35f, u));
        if (Prop* s = w.prop(sword)) {
          s->glow = glm::vec4(gold, 0.9f);
          w.fx.magicTrail(s->pos + s->rot * vec3(0, 0.85f, 0), vec3(0), gold * 2.0f);
        }
        if (!sliced && prevTheta > glm::radians(-4.0f) && theta <= glm::radians(-4.0f)) {
          sliced = true;
          vec3 c = victimCenter(w);
          w.slice(v, 0.9f);
          w.fx.sparks(c, vec3(0, 1, 0), 40, 4.0f, 1.0f, vec3(4.0f, 3.0f, 1.5f));
          w.fx.flash(c, 0.9f, gold * 5.0f, 0.12f);
          w.light(c, gold * 25.0f, 4.0f, 0.3f);
          w.sound(SFX_CLANG, 1.0f, c);
          w.sound(SFX_SHATTER, 0.6f, c, 1.2f);
          w.shake(0.45f);
          w.slowmo(0.22f, 0.6f);
        }
        if (u >= 1.0f) next();
        break;
      }
      case 5: {  // the blade fades away
        float u = glm::clamp(pt / 0.8f, 0.0f, 1.0f);
        if (Prop* s = w.prop(sword)) {
          s->dissolve = easeIn(u);
          s->glow = glm::vec4(gold, 0.9f * (1.0f - u));
          if (u >= 1.0f) s->alive = false;
        }
        a.poseBend = vec2(0, 0.35f * (1.0f - u));
        a.glow = glm::vec4(gold, 0.5f * (1.0f - u));
        if (u >= 1.0f) next();
        break;
      }
      case 6:
        if (moveIn(w, dt)) next();
        break;
      case 7:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Pawn: bull charge. Paws the ground, charges and sends the victim flying.
// ---------------------------------------------------------------------------
class PawnCharge : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  vec3 start{0}, stop{0};
  float dur = 0.5f, dustT = 0;
  int kicks = 0;
  Flight flight;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.8f;
        if (faceOff(w)) next();
        break;
      case 1: {  // head down, paw the ground
        if (enter()) start = a.pos;
        a.poseBend = vec2(0, 0.3f * easeOut(pt / 0.3f));
        a.poseSquash = 0.9f;
        float sp = std::fmod(pt, 0.3f) / 0.3f;
        a.pos = start - dir * 0.07f * std::sin(sp * kPI);
        if (kicks < 3 && pt > 0.15f + 0.3f * kicks) {
          kicks++;
          w.fx.dust(a.pos - dir * a.radius(), 5, 0.15f, 0.9f);
          w.sound(SFX_STEP, 0.7f, a.pos, 0.75f);
        }
        v.tremble = 0.9f;
        if (pt > 0.95f) next();
        break;
      }
      case 2: {  // charge!
        if (enter()) {
          start = a.pos;
          stop = V - dir * touchDist(a, v);
          dur = glm::clamp(0.22f + 0.09f * glm::length(flat(stop - start)), 0.25f, 0.7f);
          w.sound(SFX_WHOOSH, 0.8f, a.pos, 0.9f);
        }
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        a.pos = glm::mix(start, stop, easeIn(u));
        a.pos.y = std::fabs(std::sin(u * kPI * 3.0f)) * 0.05f;
        a.poseBend = vec2(0, 0.45f);
        a.poseSquash = 1.0f;
        dustT -= dt;
        if (dustT <= 0) {
          dustT = 0.04f;
          w.fx.dust(flat(a.pos), 2, 0.1f, 0.5f);
        }
        v.tremble = 1.0f;
        if (u >= 1.0f) {
          a.pos.y = 0;
          vec3 hit = V - dir * v.radius() * 0.8f + vec3(0, std::min(a.height(), v.height()) * 0.5f, 0);
          w.fx.sparks(hit, dir + vec3(0, 0.5f, 0), 30, 3.5f, 0.7f, vec3(3.0f, 2.6f, 2.0f));
          w.fx.chips(hit, 20, 2.5f, v.color == 1);
          w.fx.flash(hit, 0.4f, vec3(2.0f, 1.8f, 1.5f), 0.08f);
          w.light(hit, vec3(6.0f, 5.0f, 4.0f), 2.5f, 0.15f);
          w.sound(SFX_CLACK, 1.0f, hit, 0.8f);
          w.sound(SFX_THUD, 0.8f, hit, 1.2f);
          w.shake(0.35f);
          w.slowmo(0.3f, 0.35f);
          flight.launch(v, dir * 4.0f + vec3(0, 4.2f, 0), 9.0f);
          a.sq.v -= 3.0f;
          next();
        }
        break;
      }
      case 3: {  // the victim flies, the pawn bounces back off the impact
        float u = glm::clamp(pt / 0.3f, 0.0f, 1.0f);
        a.pos = stop - dir * 0.1f * std::sin(u * kPI);
        a.poseBend = vec2(0, glm::mix(-0.3f, 0.0f, u));
        if (v.alive && flight.update(v, dt)) crashLanding(w, v, dir);
        if (!v.alive && pt > 0.5f) next();
        break;
      }
      case 4:  // stands proud
        a.poseBend = vec2(0, -0.15f * std::sin(kPI * glm::clamp(pt / 0.5f, 0.0f, 1.0f)));
        if (pt > 0.5f) next();
        break;
      case 5:
        if (moveIn(w, dt)) next();
        break;
      case 6:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Pawn: whirlwind. Spins up like a top and grinds the victim apart.
// ---------------------------------------------------------------------------
class PawnWhirl : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  vec3 start{0}, stop{0};
  float spinRate = 0, soundT = 0;
  void spin(Actor& a, float dt) {
    a.yaw = wrapAngle(a.yaw + spinRate * dt);
    a.yawTarget = a.yaw;
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // spin up
        float u = glm::clamp(pt / 0.8f, 0.0f, 1.0f);
        spinRate = 32.0f * easeIn(u);
        spin(a, dt);
        a.poseSquash = 0.92f;
        a.pos.y = 0.04f * u;
        a.idleWeight = 0;
        whirlDust(w, flat(a.pos), a.radius() * 1.3f);
        soundT -= dt;
        if (soundT <= 0) {
          soundT = 0.32f - 0.15f * u;
          w.sound(SFX_WHOOSH, 0.35f + 0.3f * u, a.pos, 0.8f + 0.6f * u);
        }
        v.tremble = 0.8f;
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // drill in
        if (enter()) {
          start = a.pos;
          stop = V - dir * touchDist(a, v) + vec3(0, 0.04f, 0);
        }
        float u = glm::clamp(pt / 0.45f, 0.0f, 1.0f);
        spin(a, dt);
        a.pos = glm::mix(start, stop, easeInOut(u));
        whirlDust(w, flat(a.pos), a.radius() * 1.3f);
        soundT -= dt;
        if (soundT <= 0) {
          soundT = 0.17f;
          w.sound(SFX_WHOOSH, 0.65f, a.pos, 1.4f);
        }
        v.tremble = 1.0f;
        if (u >= 1.0f) next();
        break;
      }
      case 3: {  // grind
        if (enter()) w.sound(SFX_RUMBLE, 0.6f, V, 1.5f);
        float u = glm::clamp(pt / 0.7f, 0.0f, 1.0f);
        spin(a, dt);
        a.pos = stop + dir * 0.05f * u;
        vec3 side(-dir.z, 0, dir.x);
        vec3 contact = V - dir * v.radius() + vec3(0, std::min(a.height(), v.height()) * 0.45f, 0);
        w.fx.sparks(contact, side * (w.fx.rnd() < 0.5f ? 1.0f : -1.0f) + vec3(0, 0.4f, 0), 4, 4.0f, 0.4f);
        if (w.fx.rnd() < 0.5f) w.fx.chips(contact, 2, 2.0f, v.color == 1);
        w.frameLights.push_back({contact, 1.0f, vec3(1.6f, 0.9f, 0.3f)});
        soundT -= dt;
        if (soundT <= 0) {
          soundT = 0.07f;
          w.sound(SFX_CLACK, 0.45f, contact, w.fx.rnd(1.2f, 1.6f));
        }
        v.crack = 0.85f * u;
        v.tremble = 1.5f;
        v.poseTwist = 1.2f * u;
        v.idleWeight = 0;
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          w.shatter(v, contact, side * 3.5f + dir * 1.5f, 1.6f, 1.0f, 14.0f);
          w.fx.dust(V, 18, 0.5f, 1.8f);
          w.groundRing(V, vec3(0.8f, 0.7f, 0.55f), 3.0f, 0.5f, 0.07f);
          w.light(c, vec3(10.0f, 6.0f, 3.0f), 3.0f, 0.25f);
          w.sound(SFX_CLACK, 1.0f, c, 0.8f);
          w.shake(0.4f);
          w.slowmo(0.3f, 0.35f);
          next();
        }
        break;
      }
      case 4: {  // spin down... dizzy!
        float u = glm::clamp(pt / 1.2f, 0.0f, 1.0f);
        spinRate *= std::exp(-dt * 3.5f);
        if (spinRate > 1.5f) spin(a, dt);
        else a.yawTarget = faceAngle(dir);
        a.pos.y = 0.04f * (1.0f - u);
        a.poseSquash = 1.0f;
        float wob = 0.18f * (1.0f - u) * std::min(1.0f, pt * 3.0f);
        a.poseBend = vec2(std::cos(pt * 9.0f), std::sin(pt * 9.0f)) * wob;
        if (u >= 1.0f) {
          a.poseBend = vec2(0);
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 5:
        if (moveIn(w, dt)) next();
        break;
      case 6:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Knight: buck kick. Turns its back, rocks forward and kicks the victim sky-high.
// ---------------------------------------------------------------------------
class KnightKick : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  Mover approach;
  Flight flight;
  float phi = 0;
  bool snorted = false;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 0.9f;
        shotExtra = 1.3f;
        if (faceOff(w)) next();
        break;
      case 1:  // trot up
        if (enter()) approach = Mover(plan.mover, V - dir * touchDist(a, v) * 1.05f, MS_HOP);
        v.tremble = 0.7f;
        if (approach.update(w, dt)) next();
        break;
      case 2:  // turn around, snort
        if (enter()) a.yawTarget = faceAngle(-dir);
        if (!snorted && pt > 0.3f) {
          snorted = true;
          vec3 nose = a.pos - dir * 0.35f + vec3(0, 0.85f, 0);
          w.fx.smoke(nose, 3, 0.03f, 0.2f, 0.15f);
          w.sound(SFX_WHOOSH, 0.4f, nose, 0.55f);
        }
        v.tremble = 0.9f;
        if (pt > 0.5f) next();
        break;
      case 3: {  // gather
        float u = glm::clamp(pt / 0.25f, 0.0f, 1.0f);
        a.poseSquash = glm::mix(1.0f, 0.85f, easeOut(u));
        a.poseBend = vec2(0, -0.2f * easeOut(u));
        if (u >= 1.0f) {
          // rock forward over the front edge so the hind end swings up
          a.tiltPivot = vec3(0, 0, a.radius() / a.scale * 0.9f);
          w.sound(SFX_WHOOSH, 0.8f, a.pos, 1.0f);
          next();
        }
        break;
      }
      case 4: {  // KICK
        float u = glm::clamp(pt / 0.1f, 0.0f, 1.0f);
        phi = 0.55f * easeOut(u);
        a.tilt = glm::angleAxis(phi, vec3(1, 0, 0));
        a.poseSquash = 1.0f;
        a.poseBend = vec2(0, 0.35f * u);
        if (u >= 1.0f) {
          vec3 hit = V - dir * v.radius() + vec3(0, 0.35f, 0);
          w.fx.sparks(hit, dir + vec3(0, 0.8f, 0), 30, 3.5f, 0.6f, vec3(3.0f, 2.6f, 2.0f));
          w.fx.chips(hit, 18, 2.5f, v.color == 1);
          w.fx.flash(hit, 0.45f, vec3(2.0f, 1.8f, 1.5f), 0.08f);
          w.light(hit, vec3(6.0f, 5.0f, 4.0f), 2.5f, 0.15f);
          w.fx.dust(V, 10, 0.4f, 1.2f);
          w.sound(SFX_CLACK, 1.0f, hit, 0.75f);
          w.sound(SFX_THUD, 0.9f, hit, 0.7f);
          w.shake(0.4f);
          w.slowmo(0.25f, 0.45f);
          flight.launch(v, dir * 2.0f + vec3(0, 5.0f, 0), 7.0f);
          next();
        }
        break;
      }
      case 5: {  // settle back down and watch it fly
        float u = glm::clamp(pt / 0.5f, 0.0f, 1.0f);
        phi = 0.55f * (1.0f - easeInOut(u));
        a.tilt = glm::angleAxis(phi, vec3(1, 0, 0));
        a.poseBend = vec2(0, 0.35f * (1.0f - u));
        if (u >= 1.0f) {
          a.tilt = quat(1, 0, 0, 0);
          a.tiltPivot = vec3(0);
          a.yawTarget = faceAngle(dir);
        }
        if (v.alive && flight.update(v, dt)) crashLanding(w, v, dir);
        if (!v.alive && u >= 1.0f) next();
        break;
      }
      case 6:  // triumphant rear
        a.poseBend = vec2(0, -0.45f * std::sin(kPI * glm::clamp(pt / 0.6f, 0.0f, 1.0f)));
        if (pt > 0.7f) {
          a.poseBend = vec2(0);
          next();
        }
        break;
      case 7:
        if (moveIn(w, dt)) next();
        break;
      case 8:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Knight: joust. Summons a golden lance and gallops straight through the victim.
// ---------------------------------------------------------------------------
class KnightJoust : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  int lance = -1;
  vec3 start{0}, stop{0};
  float dur = 0.55f, dustT = 0;
  const vec3 gold{1.0f, 0.8f, 0.42f};
  const float kLanceScale = 1.15f, kHilt = 0.1f;
  void placeLance(World& w) {
    Actor& a = w.actors[plan.mover];
    Prop* s = w.prop(lance);
    if (!s) return;
    s->rot = aimY(dir);
    s->pos = a.pos + vec3(0, a.height() * 0.48f, 0) + dir * kHilt;
    s->scale = kLanceScale;
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotExtra = 0.8f;
        if (faceOff(w)) next();
        break;
      case 1: {  // summon the lance
        if (enter()) {
          lance = w.addProp(Prop::SWORD);
          w.sound(SFX_CHIME, 0.8f, a.pos, 1.2f);
        }
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.poseBend = vec2(0, -0.25f * std::sin(u * kPI));
        placeLance(w);
        if (Prop* s = w.prop(lance)) {
          s->dissolve = 1.0f - u;
          s->glow = glm::vec4(gold, 1.5f - u);
          w.frameLights.push_back({s->pos + dir * 0.6f, 2.0f, gold * (5.0f * (1.2f - u))});
        }
        v.tremble = 0.8f;
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // gallop
        if (enter()) {
          start = a.pos;
          stop = V - dir * (kHilt + kLanceScale - v.radius() * 0.8f);
          dur = glm::clamp(0.3f + 0.15f * glm::length(flat(stop - start)), 0.35f, 0.75f);
          w.sound(SFX_WHOOSH, 0.8f, a.pos, 0.8f);
        }
        float u = glm::clamp(pt / dur, 0.0f, 1.0f);
        a.pos = glm::mix(start, stop, easeIn(u));
        a.pos.y = std::fabs(std::sin(u * kPI * 4.0f)) * 0.1f;
        a.poseBend = vec2(0, 0.15f * std::sin(u * kPI * 8.0f));
        dustT -= dt;
        if (dustT <= 0) {
          dustT = 0.05f;
          w.fx.dust(flat(a.pos), 2, 0.15f, 0.5f);
        }
        placeLance(w);
        v.tremble = 1.0f;
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          w.fx.sparks(c - dir * 0.2f, dir + vec3(0, 0.3f, 0), 40, 4.5f, 0.6f, vec3(4.0f, 3.0f, 1.5f));
          w.fx.flash(c, 0.9f, gold * 5.0f, 0.12f);
          w.light(c, gold * 25.0f, 4.0f, 0.3f);
          w.sound(SFX_CLANG, 1.0f, c, 0.9f);
          w.shatter(v, c - dir * 0.2f, dir * 4.5f + vec3(0, 0.8f, 0), 1.3f, 0.8f, 10.0f);
          w.shake(0.45f);
          w.slowmo(0.25f, 0.5f);
          stop = a.pos;
          next();
        }
        break;
      }
      case 3: {  // skid and rear
        float u = glm::clamp(pt / 0.7f, 0.0f, 1.0f);
        a.pos = stop + dir * 0.18f * easeOut(u);
        a.poseBend = vec2(0, -0.45f * std::sin(u * kPI));
        if (enter()) w.fx.dust(flat(a.pos), 8, 0.3f, 1.0f);
        placeLance(w);
        if (Prop* s = w.prop(lance)) s->glow = glm::vec4(gold, 0.8f * (1.0f - u));
        if (u >= 1.0f) next();
        break;
      }
      case 4: {  // the lance fades away
        float u = glm::clamp(pt / 0.5f, 0.0f, 1.0f);
        placeLance(w);
        if (Prop* s = w.prop(lance)) {
          s->dissolve = easeIn(u);
          if (u >= 1.0f) s->alive = false;
        }
        if (u >= 1.0f) next();
        break;
      }
      case 5:
        if (moveIn(w, dt)) next();
        break;
      case 6:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Bishop: judgment beam. A pillar of light from above dissolves the victim.
// ---------------------------------------------------------------------------
class BishopBeam : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  const vec3 gold{1.0f, 0.86f, 0.55f};
  float crackleT = 0;
  vec3 head(World& w) {
    Actor& a = w.actors[plan.mover];
    return a.pos + vec3(0, a.height() * 0.97f, 0);
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    const vec3 sky = V + vec3(0, 3.2f, 0);
    switch (phase) {
      case 0:
        shotHeight = 1.1f;
        shotExtra = 1.2f;
        if (faceOff(w)) next();
        break;
      case 1: {  // prayer: light gathers above the victim
        if (enter()) w.sound(SFX_CHARGE, 0.9f, a.pos, 0.85f);
        float u = glm::clamp(pt / 1.1f, 0.0f, 1.0f);
        a.poseBend = vec2(0, 0.28f * std::sin(u * kPI));
        a.glow = glm::vec4(gold, 1.2f * u);
        a.idleWeight = 0;
        w.frameLights.push_back({head(w), 2.5f, gold * (2.0f + 8.0f * u)});
        w.frameLights.push_back({sky, 5.0f, gold * (2.0f + 18.0f * u)});
        w.fx.swirl(sky, 0.5f * (1.2f - u), gold * 3.0f, w.time);
        stream(w, sky, victimCenter(w), gold * 2.0f, 1, 1.4f);
        v.tremble = 0.4f + 0.6f * u;
        v.glow = glm::vec4(gold, 0.3f * u);
        if (u >= 1.0f) {
          w.sound(SFX_ZAP, 1.0f, V, 0.7f);
          w.sound(SFX_EXPLODE, 0.5f, V, 1.6f);
          w.fx.flash(V + vec3(0, v.height(), 0), 1.0f, gold * 6.0f, 0.15f);
          w.groundRing(V, gold * 1.4f, 2.5f, 0.8f, 0.08f);
          w.addFlash(gold * 0.12f);
          w.shake(0.25f);
          v.dissolveColor = gold;
          next();
        }
        break;
      }
      case 2: {  // the beam
        float u = glm::clamp(pt / 1.2f, 0.0f, 1.0f);
        vec3 base = V + vec3(0, 0.02f, 0);
        w.fx.bolt(sky + vec3(0, 2.0f, 0), base, gold * 10.0f, 0.015f, 10);
        for (int i = 0; i < 10; ++i)
          w.fx.flash(base + vec3(0, i * 0.5f + w.fx.rnd(0.0f, 0.5f), 0), 0.2f, gold * 1.3f, 0.05f);
        w.frameLights.push_back({victimCenter(w), 4.0f, gold * 30.0f});
        w.frameLights.push_back({sky, 5.0f, gold * 12.0f});
        if (w.fx.rnd() < 0.6f) w.fx.magicBurst(base + w.fx.rndDir() * 0.2f, gold * 3.0f, 3, 1.2f);
        crackleT -= dt;
        if (crackleT <= 0) {
          crackleT = 0.15f;
          w.sound(SFX_CRACKLE, 0.5f, V, w.fx.rnd(1.2f, 1.5f));
        }
        a.poseBend = vec2(0, -0.15f);
        a.glow = glm::vec4(gold, 1.2f);
        v.dissolve = easeIn(u);
        v.glow = glm::vec4(gold, 0.6f + u);
        v.pos.y = 0.3f * easeIn(u);
        v.tremble = 1.2f * (1.0f - u);
        v.idleWeight = 0;
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          v.alive = false;
          v.visible = false;
          w.fx.magicBurst(c, gold * 5.0f, 80, 2.0f);
          w.fx.flash(c, 1.2f, gold * 6.0f, 0.2f);
          w.light(c, gold * 30.0f, 4.0f, 0.5f);
          w.sound(SFX_CHIME, 1.0f, c, 1.4f);
          next();
        }
        break;
      }
      case 3: {  // afterglow
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.glow = glm::vec4(gold, 1.2f * (1.0f - u));
        a.poseBend = vec2(0, -0.15f * (1.0f - u));
        if (u >= 1.0f) {
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 4:
        if (moveIn(w, dt)) next();
        break;
      case 5:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Bishop: seraph orbs. Five orbs of light circle the victim, then strike as one.
// ---------------------------------------------------------------------------
class BishopOrbs : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  static constexpr int kOrbs = 5;
  const vec3 gold{1.0f, 0.82f, 0.48f};
  float spinA = 0, radius = 0.75f, crackleT = 0;
  int released = 0;
  vec3 head(World& w) {
    Actor& a = w.actors[plan.mover];
    return a.pos + vec3(0, a.height() * 0.97f, 0) + a.forward() * 0.05f;
  }
  vec3 orbit(World& w, int i) {
    float ang = spinA + i * 2.0f * kPI / kOrbs;
    return victimCenter(w) + vec3(std::cos(ang) * radius, 0.12f * std::sin(ang * 2.0f + w.time * 3.0f), std::sin(ang) * radius);
  }
  float releaseAt(int i) const { return 0.25f + 0.15f * i; }
  void drawOrb(World& w, const vec3& p) {
    w.fx.flash(p, 0.16f, gold * 3.0f, 0.04f);
    w.fx.flash(p, 0.06f, vec3(6.0f, 5.5f, 4.5f), 0.04f);
    w.fx.magicTrail(p, vec3(0), gold * 2.5f);
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 0.8f;
        if (faceOff(w)) next();
        break;
      case 1: {  // rise and release the orbs one by one
        if (enter()) w.sound(SFX_CHARGE, 0.8f, a.pos, 1.1f);
        a.pos.y = 0.25f * easeInOut(pt / 0.5f) + 0.02f * std::sin(w.time * 6.0f);
        a.glow = glm::vec4(gold, std::min(1.2f, pt * 2.0f));
        a.idleWeight = 0;
        spinA += dt * 2.0f;
        while (released < kOrbs && pt >= releaseAt(released)) {
          w.fx.magicBurst(head(w), gold * 3.0f, 10, 1.2f);
          w.sound(SFX_ZAP, 0.45f, head(w), 1.5f + 0.12f * released);
          released++;
        }
        for (int i = 0; i < released; ++i) {
          float ui = glm::clamp((pt - releaseAt(i)) / 0.45f, 0.0f, 1.0f);
          vec3 p = glm::mix(head(w), orbit(w, i), easeInOut(ui)) + vec3(0, 0.4f * std::sin(ui * kPI), 0);
          drawOrb(w, p);
        }
        w.frameLights.push_back({victimCenter(w), 3.0f, gold * (2.0f * released)});
        v.tremble = 0.6f;
        if (pt >= releaseAt(kOrbs - 1) + 0.8f) next();
        break;
      }
      case 2: {  // tighten
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        spinA += dt * (2.0f + 9.0f * u);
        radius = glm::mix(0.75f, 0.45f, u);
        for (int i = 0; i < kOrbs; ++i) drawOrb(w, orbit(w, i));
        w.frameLights.push_back({victimCenter(w), 3.0f, gold * (10.0f + 10.0f * u)});
        crackleT -= dt;
        if (crackleT <= 0) {
          crackleT = 0.18f;
          w.sound(SFX_CRACKLE, 0.4f, V, w.fx.rnd(1.3f, 1.6f));
        }
        v.crackColor = vec3(1.0f, 0.86f, 0.55f);
        v.crack = 0.3f * u;
        v.tremble = 0.8f + 0.6f * u;
        if (u >= 1.0f) next();
        break;
      }
      case 3: {  // converge
        float u = glm::clamp(pt / 0.16f, 0.0f, 1.0f);
        spinA += dt * 11.0f;
        radius = 0.45f * (1.0f - easeIn(u));
        for (int i = 0; i < kOrbs; ++i) drawOrb(w, orbit(w, i));
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          v.crack = 1.0f;
          w.shatter(v, c, vec3(0), 2.8f, 1.2f, 9.0f);
          w.fx.flash(c, 1.5f, gold * 8.0f, 0.25f);
          w.fx.magicBurst(c, gold * 5.0f, 100, 3.0f);
          w.fx.ring(c, 0.3f, gold * 3.0f, 0.4f);
          w.groundRing(V, gold * 1.6f, 3.0f, 0.7f, 0.08f);
          w.light(c, gold * 40.0f, 5.0f, 0.5f);
          w.addFlash(gold * 0.15f);
          w.sound(SFX_EXPLODE, 0.6f, c, 1.5f);
          w.shake(0.4f);
          w.slowmo(0.3f, 0.4f);
          next();
        }
        break;
      }
      case 4: {  // descend
        float u = glm::clamp(pt / 0.55f, 0.0f, 1.0f);
        a.pos.y = 0.25f * (1.0f - easeInOut(u));
        a.glow = glm::vec4(gold, 1.2f * (1.0f - u));
        if (u >= 1.0f) {
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 5:
        if (moveIn(w, dt)) next();
        break;
      case 6:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Rook: timber! The tower topples onto the victim, then springs back up.
// ---------------------------------------------------------------------------
class RookTopple : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  Mover approach;
  float phi = 0, omega = 0;
  bool crushed = false;
  const float kFlat = kPI * 0.5f - 0.03f;
  void crush(World& w, Actor& v, const vec3& at) {
    crushed = true;
    w.shatter(v, at, vec3(0, -3.0f, 0) + dir * 1.2f, 1.8f, 0.4f, 9.0f);
    w.fx.dust(V, 16, 0.5f, 1.6f);
    w.sound(SFX_CLACK, 1.0f, at, 0.7f);
    w.shake(0.35f);
    w.slowmo(0.3f, 0.35f);
    omega *= 0.6f;
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // stomp within reach
        if (enter()) {
          const float reach = 1.12f;
          approach = Mover(plan.mover, L > reach ? V - dir * reach : A, MS_STOMP);
        }
        v.tremble = 0.8f;
        if (approach.update(w, dt)) {
          a.yawTarget = faceAngle(dir);
          next();
        }
        break;
      }
      case 2: {  // teeter...
        if (enter()) w.sound(SFX_RUMBLE, 0.7f, a.pos, 0.55f);
        float u = glm::clamp(pt / 0.7f, 0.0f, 1.0f);
        a.poseBend = vec2(0.04f * std::sin(u * kPI * 3.0f), -0.12f * std::sin(u * kPI * 2.0f));
        v.tremble = 1.0f;
        v.poseBend = vec2(0, -0.25f * u);  // the victim looks up
        if (u >= 1.0f) {
          a.poseBend = vec2(0);
          a.tiltPivot = vec3(0, 0, a.radius() / a.scale * 0.95f);
          phi = 0.02f;
          omega = 0.4f;
          w.sound(SFX_WHOOSH, 0.7f, a.pos, 0.6f);
          next();
        }
        break;
      }
      case 3: {  // ...and fall
        omega += 9.0f * std::sin(phi + 0.15f) * dt;
        phi = std::min(kFlat, phi + omega * dt);
        a.tilt = glm::angleAxis(phi, vec3(1, 0, 0));
        vec3 top = vec3(a.model() * glm::vec4(0, meshgen::pieceHeight(a.kind) * 0.95f, 0, 1));
        if (!crushed && v.alive) {
          bool reached = top.y <= v.height() * 0.95f && glm::length(flat(top - V)) < v.radius() + 0.2f;
          if (reached || phi > 1.2f) crush(w, v, top);
        }
        if (phi >= kFlat) {
          for (int i = 1; i <= 4; ++i)
            w.fx.dust(flat(glm::mix(a.pos, top, i / 4.0f)), 6, 0.2f, 1.2f);
          w.sound(SFX_THUD, 1.0f, top, 0.65f);
          w.sound(SFX_BOOM, 0.4f, top, 1.6f);
          w.groundRing(flat(top), vec3(0.8f, 0.7f, 0.55f), 2.5f, 0.5f, 0.08f);
          w.shake(0.45f);
          next();
        }
        break;
      }
      case 4:  // lie there a moment
        if (pt > 0.35f) next();
        break;
      case 5: {  // spring back up
        float u = glm::clamp(pt / 0.55f, 0.0f, 1.0f);
        phi = kFlat * (1.0f - easeInOut(u));
        a.tilt = glm::angleAxis(phi, vec3(1, 0, 0));
        a.pos.y = 0.15f * std::sin(u * kPI);
        if (u >= 1.0f) {
          a.tilt = quat(1, 0, 0, 0);
          a.tiltPivot = vec3(0);
          a.pos.y = 0;
          a.sq.v -= 3.0f;
          w.fx.dust(a.pos, 8, a.radius(), 0.8f);
          w.sound(SFX_THUD, 0.8f, a.pos, 0.9f);
          next();
        }
        break;
      }
      case 6:
        if (moveIn(w, dt)) next();
        break;
      case 7:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Rook: earthquake. Leaps and slams down; a shockwave races across the board
// and erupts under the victim.
// ---------------------------------------------------------------------------
class RookQuake : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  float front = 0, nextPuff = 0, nextRing = 0;
  std::vector<int> jolted;
  Flight flight;
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 0.8f;
        shotExtra = 0.7f;
        if (faceOff(w)) next();
        break;
      case 1: {  // crouch
        if (enter()) w.sound(SFX_RUMBLE, 0.7f, a.pos, 0.8f);
        a.poseSquash = glm::mix(1.0f, 0.75f, easeOut(pt / 0.3f));
        v.tremble = 0.6f;
        if (pt > 0.3f) {
          w.sound(SFX_WHOOSH, 0.7f, a.pos, 0.7f);
          w.fx.dust(a.pos, 8, a.radius(), 0.8f);
          next();
        }
        break;
      }
      case 2: {  // leap
        float u = glm::clamp(pt / 0.45f, 0.0f, 1.0f);
        a.pos.y = 0.85f * easeOut(u);
        a.poseSquash = glm::mix(1.12f, 1.0f, u);
        if (u >= 1.0f) next();
        break;
      }
      case 3: {  // SLAM
        float u = glm::clamp(pt / 0.12f, 0.0f, 1.0f);
        a.pos.y = 0.85f * (1.0f - easeIn(u));
        if (u >= 1.0f) {
          a.pos.y = 0;
          a.sq.v -= 5.0f;
          w.sound(SFX_BOOM, 1.0f, A, 0.6f);
          w.sound(SFX_THUD, 1.0f, A, 0.7f);
          w.fx.dust(A, 30, 0.6f, 2.4f);
          w.groundRing(A, vec3(1.2f, 0.9f, 0.6f), 4.0f, 0.8f, 0.12f);
          w.shake(0.7f);
          front = nextPuff = a.radius();
          nextRing = front + 0.6f;
          next();
        }
        break;
      }
      case 4: {  // the shockwave races across the board
        if (enter()) w.sound(SFX_RUMBLE, 1.0f, (A + V) * 0.5f, 0.7f);
        front += 6.5f * dt;
        float end = L - v.radius() * 0.5f;
        while (nextPuff <= std::min(front, end)) {
          vec3 p = A + dir * nextPuff;
          w.fx.dust(p, 4, 0.12f, 1.2f, vec3(0.5f, 0.42f, 0.34f));
          w.fx.embers(p, 2, 1.5f);
          w.fx.chips(p + vec3(0, 0.02f, 0), 2, 1.8f, w.fx.rnd() < 0.5f);
          nextPuff += 0.22f;
        }
        if (front >= nextRing && front < end) {
          w.groundRing(A + dir * front, vec3(1.4f, 0.7f, 0.25f), 2.0f, 0.35f, 0.06f);
          nextRing += 0.6f;
        }
        // pieces near the fault line get jolted
        vec3 wave = A + dir * std::min(front, end);
        for (Actor& o : w.actors) {
          if (!o.alive || !o.visible || o.id == plan.mover || o.id == plan.victim) continue;
          if (glm::length(flat(o.pos - wave)) > 0.6f) continue;
          if (std::find(jolted.begin(), jolted.end(), o.id) != jolted.end()) continue;
          jolted.push_back(o.id);
          o.sq.v -= 2.5f;
        }
        v.tremble = 1.2f;
        if (front >= end) {
          // eruption!
          w.fx.dust(V, 30, 0.4f, 2.5f, vec3(0.5f, 0.42f, 0.34f));
          w.fx.smoke(V + vec3(0, 0.2f, 0), 10, 0.25f, 1.2f, 0.7f);
          w.fx.embers(V, 30, 3.0f);
          w.fx.fireball(V + vec3(0, 0.1f, 0), 0.6f);
          w.groundRing(V, vec3(2.0f, 1.0f, 0.35f), 3.5f, 0.6f, 0.1f);
          w.light(V + vec3(0, 0.3f, 0), vec3(30.0f, 14.0f, 5.0f), 4.0f, 0.5f);
          w.sound(SFX_EXPLODE, 1.0f, V, 0.8f);
          w.shake(0.5f);
          w.slowmo(0.3f, 0.6f);
          flight.launch(v, vec3(0, 4.2f, 0), 6.0f);
          next();
        }
        break;
      }
      case 5:  // tossed into the air, bursts at the top
        if (v.alive) {
          bool landed = flight.update(v, dt);
          if (landed || flight.vel.y <= 0.3f) {
            vec3 c = victimCenter(w);
            w.shatter(v, c, vec3(0), 2.4f, 0.5f, 10.0f);
            w.fx.flash(c, 0.8f, vec3(6.0f, 3.5f, 1.5f), 0.12f);
            w.sound(SFX_CLACK, 1.0f, c, 0.7f);
            w.shake(0.3f);
          }
        }
        if (!v.alive && pt > 0.4f) next();
        break;
      case 6:
        if (moveIn(w, dt)) next();
        break;
      case 7:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Queen: frost nova. Freezes the victim solid, then shatters it like ice.
// ---------------------------------------------------------------------------
class QueenFrost : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  const vec3 ice{0.55f, 0.85f, 1.0f};
  float yaw0 = 0, crackleT = 0;
  vec3 crown(World& w) {
    Actor& a = w.actors[plan.mover];
    return a.pos + vec3(0, a.height() * 0.93f, 0);
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 0.9f;
        shotExtra = 0.6f;
        if (faceOff(w)) next();
        break;
      case 1: {  // twirl, gathering frost
        if (enter()) {
          yaw0 = a.yaw;
          w.sound(SFX_CHARGE, 0.8f, a.pos, 1.4f);
        }
        float u = glm::clamp(pt / 0.9f, 0.0f, 1.0f);
        a.yaw = wrapAngle(yaw0 + 2.0f * kPI * easeInOut(u));
        a.yawTarget = a.yaw;
        a.pos.y = 0.15f * easeInOut(u);
        a.poseTwist = std::sin(u * kPI) * 1.0f;
        a.glow = glm::vec4(ice, 0.5f * u);
        a.idleWeight = 0;
        w.fx.swirl(a.pos + vec3(0, 0.7f, 0), 0.45f, ice * 2.5f, w.time);
        w.frameLights.push_back({crown(w), 3.0f, ice * (1.0f + 3.0f * u)});
        v.tremble = 0.6f;
        if (u >= 1.0f) {
          a.yawTarget = faceAngle(V - A);
          next();
        }
        break;
      }
      case 2: {  // blizzard
        if (enter()) w.groundRing(V, ice * 0.9f, 1.0f, 1.6f, 0.12f);
        float u = glm::clamp(pt / 1.2f, 0.0f, 1.0f);
        a.pos.y = 0.15f + 0.02f * std::sin(w.time * 5.0f);
        a.poseBend = vec2(0, 0.15f);
        stream(w, crown(w), victimCenter(w) + w.fx.rndDir() * 0.15f, ice * 3.0f, 4, 2.2f);
        if (w.fx.rnd() < 0.3f) w.fx.dust(V, 1, 0.3f, 0.3f, vec3(0.7f, 0.8f, 0.9f));
        w.frameLights.push_back({crown(w), 3.0f, ice * 4.0f});
        w.frameLights.push_back({victimCenter(w), 3.0f, ice * (4.0f + 10.0f * u)});
        crackleT -= dt;
        if (crackleT <= 0) {
          crackleT = 0.2f;
          w.sound(SFX_CRACKLE, 0.45f, V, w.fx.rnd(1.6f, 1.9f));
        }
        v.crackColor = ice * 1.2f;
        v.crack = 0.55f * u;
        v.glow = glm::vec4(ice, 0.8f * u);
        v.tremble = 1.0f - u;  // frozen stiff
        v.idleWeight = 0;
        if (u >= 1.0f) {
          v.tremble = 0;
          w.sound(SFX_CHIME, 0.7f, V, 1.9f);
          next();
        }
        break;
      }
      case 3: {  // frozen... wind up
        float u = glm::clamp(pt / 0.45f, 0.0f, 1.0f);
        v.glow = glm::vec4(ice, 0.8f + 0.3f * std::sin(pt * 20.0f));
        a.poseBend = vec2(0, glm::mix(0.15f, -0.2f, easeOut(u)));
        if (u >= 1.0f) next();
        break;
      }
      case 4: {  // SNAP
        float u = glm::clamp(pt / 0.12f, 0.0f, 1.0f);
        a.poseBend = vec2(0, glm::mix(-0.2f, 0.3f, u));
        a.poseTwist = -0.8f * u;
        if (u >= 1.0f) {
          vec3 c = victimCenter(w);
          w.shatter(v, c, dir * 1.0f, 2.3f, 0.9f, 7.0f);
          w.fx.magicBurst(c, ice * 4.0f, 70, 2.5f);
          w.fx.sparks(c, vec3(0, 1, 0), 40, 3.5f, 1.0f, ice * 3.0f);
          w.fx.flash(c, 1.2f, ice * 5.0f, 0.15f);
          w.groundRing(V, ice * 1.5f, 3.0f, 0.7f, 0.08f);
          w.light(c, ice * 30.0f, 4.5f, 0.45f);
          w.addFlash(ice * 0.1f);
          w.sound(SFX_SHATTER, 0.8f, c, 1.5f);
          w.sound(SFX_CHIME, 0.6f, c, 2.0f);
          w.shake(0.4f);
          w.slowmo(0.3f, 0.45f);
          next();
        }
        break;
      }
      case 5: {  // descend
        float u = glm::clamp(pt / 0.5f, 0.0f, 1.0f);
        a.pos.y = 0.15f * (1.0f - easeInOut(u));
        a.glow = glm::vec4(ice, 0.5f * (1.0f - u));
        a.poseBend = vec2(0, 0.3f * (1.0f - u));
        a.poseTwist = -0.8f * (1.0f - u);
        if (u >= 1.0f) {
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 6:
        if (moveIn(w, dt)) next();
        break;
      case 7:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Queen: vortex. Spins the victim down to a point - then it bursts back out.
// ---------------------------------------------------------------------------
class QueenVortex : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  const vec3 violet{0.75f, 0.32f, 1.0f};
  float spinRate = 0, whooshT = 0, boltT = 0;
  vec3 crown(World& w) {
    Actor& a = w.actors[plan.mover];
    return a.pos + vec3(0, a.height() * 0.93f, 0);
  }
  vec3 eye(World& w) {  // centre of the vortex
    Actor& v = w.actors[plan.victim];
    return V + vec3(0, 0.25f + meshgen::pieceHeight(v.kind) * 0.5f, 0);
  }
  // shrink about the vortex eye rather than the piece's base
  void setScale(World& w, float s, float lift) {
    Actor& v = w.actors[plan.victim];
    v.scale = s;
    v.pos.y = lift + (1.0f - s) * meshgen::pieceHeight(v.kind) * 0.5f;
  }
  void inflow(World& w, const vec3& c) {
    for (int i = 0; i < 3; ++i) {
      Particle p;
      float ang = w.fx.rnd(0.0f, 6.2831853f);
      vec3 d(std::cos(ang), 0, std::sin(ang));
      float r = w.fx.rnd(0.8f, 1.3f);
      p.pos = c + d * r + vec3(0, w.fx.rnd(-0.4f, 0.4f), 0);
      p.vel = vec3(-d.z, 0, d.x) * 2.6f - d * 1.6f;
      p.size = w.fx.rnd(0.01f, 0.025f);
      p.color = violet * w.fx.rnd(1.5f, 3.5f);
      p.colorEnd = violet * 0.3f;
      p.opacity = 0;
      p.maxLife = w.fx.rnd(0.4f, 0.6f);
      p.type = PT_SPARK;
      p.stretch = 0.03f;
      p.drag = 0.0f;
      w.fx.add(p);
    }
    Particle s;
    float ang = w.fx.rnd(0.0f, 6.2831853f);
    vec3 d(std::cos(ang), 0, std::sin(ang));
    s.pos = c + d * 1.1f + vec3(0, w.fx.rnd(-0.3f, 0.3f), 0);
    s.vel = vec3(-d.z, 0, d.x) * 1.8f - d * 1.2f;
    s.size = w.fx.rnd(0.1f, 0.18f);
    s.color = vec3(0.05f, 0.03f, 0.07f);
    s.colorEnd = s.color;
    s.opacity = 0.3f;
    s.maxLife = 0.8f;
    s.type = PT_SMOKE;
    s.drag = 0.4f;
    s.rot = w.fx.rnd(0.0f, 6.28f);
    s.rotVel = 4.0f;
    s.seed = w.fx.rnd();
    s.fadeIn = 0.2f;
    w.fx.add(s);
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 0.95f;
        shotExtra = 0.9f;
        if (faceOff(w)) next();
        break;
      case 1: {  // raise hands
        if (enter()) w.sound(SFX_CHARGE, 0.8f, a.pos, 0.6f);
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.pos.y = 0.18f * easeInOut(u);
        a.poseTwist = std::sin(u * kPI) * 1.4f;
        a.glow = glm::vec4(violet, 1.1f * u);
        a.idleWeight = 0;
        w.frameLights.push_back({crown(w), 3.0f, violet * (3.0f + 8.0f * u)});
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // the vortex opens
        if (enter()) {
          w.sound(SFX_RUMBLE, 0.9f, V, 0.6f);
          w.groundRing(V, violet * 1.2f, 1.5f, 1.5f, 0.1f);
        }
        float u = glm::clamp(pt / 1.5f, 0.0f, 1.0f);
        a.pos.y = 0.18f + 0.02f * std::sin(w.time * 5.0f);
        a.poseBend = vec2(0, -0.15f);
        spinRate = 26.0f * easeIn(u);
        v.yaw = wrapAngle(v.yaw + spinRate * dt);
        v.yawTarget = v.yaw;
        setScale(w, glm::mix(1.0f, 0.3f, easeIn(u)), 0.25f * easeInOut(u));
        v.glow = glm::vec4(violet, 0.3f + 0.9f * u);
        v.tremble = 0.6f;
        v.idleWeight = 0;
        inflow(w, eye(w));
        boltT -= dt;
        if (boltT <= 0) {
          boltT = w.fx.rnd(0.08f, 0.16f);
          w.fx.bolt(crown(w), eye(w) + w.fx.rndDir() * 0.1f, violet * 5.0f, 0.12f, 14);
        }
        whooshT -= dt;
        if (whooshT <= 0) {
          whooshT = 0.35f - 0.2f * u;
          w.sound(SFX_WHOOSH, 0.5f, V, 0.7f + 0.8f * u);
        }
        w.frameLights.push_back({crown(w), 3.0f, violet * 8.0f});
        w.frameLights.push_back({eye(w), 3.5f, violet * (6.0f + 14.0f * u)});
        if (u >= 1.0f) next();
        break;
      }
      case 3: {  // collapse to a point...
        float u = glm::clamp(pt / 0.15f, 0.0f, 1.0f);
        v.yaw = wrapAngle(v.yaw + spinRate * dt);
        v.yawTarget = v.yaw;
        setScale(w, glm::mix(0.3f, 0.05f, easeIn(u)), 0.25f);
        w.fx.flash(eye(w), 0.1f + 0.5f * u, violet * 6.0f, 0.05f);
        if (u >= 1.0f) {
          // ...and burst back out at full size
          setScale(w, 1.0f, 0.25f);
          v.crack = 0.6f;
          v.crackColor = violet;
          vec3 c = eye(w);
          w.shatter(v, c, vec3(0), 3.2f, 0.8f, 12.0f);
          w.fx.magicBurst(c, violet * 5.0f, 100, 3.5f);
          w.fx.flash(c, 1.4f, violet * 6.0f, 0.2f);
          w.fx.ring(c, 0.3f, violet * 3.0f, 0.4f);
          w.groundRing(V, violet * 1.5f, 3.0f, 0.8f, 0.08f);
          w.light(c, violet * 40.0f, 5.0f, 0.5f);
          w.addFlash(violet * 0.12f);
          w.sound(SFX_EXPLODE, 0.9f, c, 0.7f);
          w.shake(0.55f);
          w.slowmo(0.3f, 0.5f);
          next();
        }
        break;
      }
      case 4: {  // descend
        float u = glm::clamp(pt / 0.5f, 0.0f, 1.0f);
        a.pos.y = 0.18f * (1.0f - easeInOut(u));
        a.glow = glm::vec4(violet, 0.9f * (1.0f - u));
        a.poseBend = vec2(0, -0.15f * (1.0f - u));
        if (u >= 1.0f) {
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 5:
        if (moveIn(w, dt)) next();
        break;
      case 6:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// King: royal decree. The victim is made to bow - then crumbles to dust.
// ---------------------------------------------------------------------------
class KingDecree : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  const vec3 gold{1.0f, 0.8f, 0.42f};
  vec3 crown(World& w) {
    Actor& a = w.actors[plan.mover];
    return a.pos + vec3(0, a.height(), 0);
  }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        if (faceOff(w)) next();
        break;
      case 1: {  // draw himself up to full height
        if (enter()) w.sound(SFX_CHIME, 0.9f, a.pos, 0.55f);
        float u = glm::clamp(pt / 0.8f, 0.0f, 1.0f);
        a.poseSquash = 1.0f + 0.08f * easeOut(u);
        a.poseBend = vec2(0, -0.12f * easeOut(u));
        a.glow = glm::vec4(gold, 0.45f * u);
        a.idleWeight = 0;
        w.frameLights.push_back({crown(w), 3.0f, gold * (1.0f + 4.0f * u)});
        w.fx.swirl(crown(w) - vec3(0, 0.3f, 0), 0.3f, gold * 2.0f, w.time);
        v.tremble = 0.6f;
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // "Kneel."
        if (enter()) {
          w.sound(SFX_WHOOSH, 0.7f, a.pos, 0.6f);
          w.fx.ring(crown(w), 0.2f, gold * 3.0f, 0.4f);
          w.groundRing(A, gold * 1.4f, 4.0f, 0.8f, 0.08f);
          w.addFlash(gold * 0.06f);
        }
        float u = glm::clamp(pt / 0.25f, 0.0f, 1.0f);
        a.poseSquash = glm::mix(1.08f, 1.0f, u);
        a.poseBend = vec2(0, glm::mix(-0.12f, 0.3f, easeOut(u)));
        if (u >= 1.0f) {
          v.tiltPivot = vec3(0, 0, v.radius() / v.scale * 0.9f);  // front edge, facing the king
          next();
        }
        break;
      }
      case 3: {  // the victim bows
        float u = glm::clamp(pt / 1.0f, 0.0f, 1.0f);
        a.poseBend = vec2(0, glm::mix(0.3f, 0.1f, u));
        v.tilt = glm::angleAxis(0.5f * easeInOut(u), vec3(1, 0, 0));
        v.poseBend = vec2(0, 0.3f * easeInOut(u));
        v.tremble = 0.5f;
        v.glow = glm::vec4(gold, 0.25f * u);
        v.idleWeight = 0;
        w.frameLights.push_back({victimCenter(w), 2.5f, gold * 4.0f * u});
        if (u >= 1.0f) {
          w.sound(SFX_THUD, 0.6f, V, 1.3f);
          w.fx.dust(V + dir * v.radius(), 6, 0.2f, 0.6f);
          next();
        }
        break;
      }
      case 4: {  // and crumbles away
        if (enter()) w.sound(SFX_RUMBLE, 0.6f, V, 1.2f);
        float u = glm::clamp(pt / 0.9f, 0.0f, 1.0f);
        v.crackColor = gold * 0.4f;
        v.crack = easeIn(u);
        v.tremble = 0.8f + u;
        vec3 tint = v.color == 0 ? vec3(0.62f, 0.60f, 0.57f) : vec3(0.22f, 0.21f, 0.2f);
        if (w.fx.rnd() < 0.7f)
          w.fx.dust(V + w.fx.rndDir() * 0.2f + vec3(0, w.fx.rnd(0.0f, v.height()), 0), 1, 0.05f, 0.2f, tint);
        if (u >= 1.0f) {
          w.shatter(v, victimCenter(w), vec3(0, -1.5f, 0), 0.5f, 0.15f, 3.0f);
          w.fx.dust(V, 40, 0.8f, 1.5f, tint);
          w.fx.smoke(V + vec3(0, 0.3f, 0), 10, 0.3f, 0.4f, v.color == 0 ? 0.1f : 0.6f);
          w.groundRing(V, vec3(0.8f, 0.7f, 0.55f), 2.0f, 0.6f, 0.08f);
          w.sound(SFX_THUD, 0.8f, V, 0.6f);
          w.shake(0.25f);
          next();
        }
        break;
      }
      case 5: {  // a satisfied nod
        float u = glm::clamp(pt / 0.6f, 0.0f, 1.0f);
        a.poseBend = vec2(0, 0.1f * (1.0f - u) + 0.15f * std::sin(u * kPI));
        a.glow = glm::vec4(gold, 0.45f * (1.0f - u));
        if (u >= 1.0f) {
          a.poseBend = vec2(0);
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 6:
        if (moveIn(w, dt)) next();
        break;
      case 7:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// King: rain of blades. Three golden swords plunge into the victim from above.
// ---------------------------------------------------------------------------
class KingBlades : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  static constexpr int kBlades = 3;
  const vec3 gold{1.0f, 0.8f, 0.42f};
  const float kScale = 1.2f;
  int blade[kBlades] = {-1, -1, -1};
  vec3 hover[kBlades], plunge[kBlades];
  quat aim[kBlades];
  int struck = 0;
  float dropAt(int i) const { return 0.1f + 0.32f * i; }
  bool step(World& w, float dt) override {
    Actor& a = w.actors[plan.mover];
    Actor& v = w.actors[plan.victim];
    switch (phase) {
      case 0:
        shotHeight = 1.2f;
        shotExtra = 1.4f;
        if (faceOff(w)) next();
        break;
      case 1: {  // the blades appear above the victim
        if (enter()) {
          w.sound(SFX_CHIME, 0.9f, V, 1.0f);
          float a0 = w.fx.rnd(0.0f, 6.28f);
          for (int i = 0; i < kBlades; ++i) {
            blade[i] = w.addProp(Prop::SWORD);
            float ang = a0 + i * 2.0f * kPI / kBlades;
            vec3 out(std::cos(ang), 0, std::sin(ang));
            vec3 down = glm::normalize(vec3(0, -1, 0) - out * 0.3f);  // leaning in
            vec3 tip = V + out * 0.08f + vec3(0, v.height() * 0.35f, 0);
            plunge[i] = tip - down * kScale;
            hover[i] = plunge[i] + vec3(0, 1.0f, 0) + out * 0.3f;
            aim[i] = aimY(down, out);
          }
        }
        float u = glm::clamp(pt / 0.9f, 0.0f, 1.0f);
        a.poseBend = vec2(0, -0.2f * easeOut(u));
        a.glow = glm::vec4(gold, 0.6f * u);
        a.idleWeight = 0;
        for (int i = 0; i < kBlades; ++i) {
          Prop* s = w.prop(blade[i]);
          if (!s) continue;
          float ui = glm::clamp((pt - 0.15f * i) / 0.5f, 0.0f, 1.0f);
          s->pos = hover[i] + vec3(0, 0.04f * std::sin(w.time * 3.0f + i), 0);
          s->rot = aim[i];
          s->scale = kScale;
          s->dissolve = 1.0f - ui;
          s->glow = glm::vec4(gold, 1.6f - ui);
          w.frameLights.push_back({s->pos, 2.0f, gold * 4.0f});
        }
        v.tremble = 0.9f;
        if (u >= 1.0f) next();
        break;
      }
      case 2: {  // one after another they fall
        for (int i = 0; i < kBlades; ++i) {
          Prop* s = w.prop(blade[i]);
          if (!s) continue;
          float ud = glm::clamp((pt - dropAt(i)) / 0.13f, 0.0f, 1.0f);
          s->pos = glm::mix(hover[i] + vec3(0, 0.04f * std::sin(w.time * 3.0f + i), 0), plunge[i], easeIn(ud));
          s->glow = glm::vec4(gold, ud < 1.0f ? 0.6f : 0.9f);
          if (ud >= 1.0f && struck == i) {
            struck++;
            vec3 hit = plunge[i] + (s->rot * vec3(0, 1, 0)) * kScale * 0.6f;
            w.fx.sparks(hit, vec3(0, 1, 0), 25, 3.5f, 0.8f, vec3(4.0f, 3.0f, 1.5f));
            w.fx.chips(hit, 10, 2.0f, v.color == 1);
            w.fx.flash(hit, 0.5f, gold * 5.0f, 0.1f);
            w.light(hit, gold * 15.0f, 3.0f, 0.2f);
            w.sound(SFX_CLANG, 0.9f, hit, 0.9f + 0.15f * i);
            w.shake(0.2f);
            v.crackColor = gold;
            v.crack = 0.3f * struck;
            v.sq.v -= 3.0f;
            a.sq.v -= 1.5f;
          }
        }
        v.tremble = struck > 0 ? 0.0f : 0.9f;  // pinned
        v.idleWeight = 0;
        a.poseBend = vec2(0, glm::mix(-0.2f, 0.25f, std::min(1.0f, pt / dropAt(kBlades - 1))));
        if (struck == kBlades && pt > dropAt(kBlades - 1) + 0.4f) {
          vec3 c = victimCenter(w);
          w.shatter(v, c, vec3(0), 2.2f, 1.0f, 9.0f);
          w.fx.flash(c, 1.2f, gold * 6.0f, 0.2f);
          w.fx.magicBurst(c, gold * 4.0f, 60, 2.5f);
          w.groundRing(V, gold * 1.5f, 3.0f, 0.7f, 0.08f);
          w.light(c, gold * 30.0f, 4.5f, 0.4f);
          w.sound(SFX_EXPLODE, 0.7f, c, 1.2f);
          w.shake(0.45f);
          w.slowmo(0.3f, 0.45f);
          next();
        }
        break;
      }
      case 3: {  // the blades fade
        float u = glm::clamp(pt / 0.8f, 0.0f, 1.0f);
        for (int i = 0; i < kBlades; ++i) {
          Prop* s = w.prop(blade[i]);
          if (!s) continue;
          s->dissolve = easeIn(u);
          s->glow = glm::vec4(gold, 0.9f * (1.0f - u));
          if (u >= 1.0f) s->alive = false;
        }
        a.poseBend = vec2(0, 0.25f * (1.0f - u));
        a.glow = glm::vec4(gold, 0.6f * (1.0f - u));
        if (u >= 1.0f) {
          a.idleWeight = 1;
          next();
        }
        break;
      }
      case 4:
        if (moveIn(w, dt)) next();
        break;
      case 5:
        return finish(w);
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// Promotion: a pillar of light turns the pawn into its new self.
// ---------------------------------------------------------------------------
class PromotionAnim : public Staged {
 public:
  PromotionAnim(int pawn, int square, int kind) : pawn_(pawn), sq_(square), kind_(kind) {}

 protected:
  int pawn_, sq_, kind_, newId_ = -1;
  bool step(World& w, float dt) override {
    vec3 c = squareCenter(sq_);
    const vec3 gold(1.0f, 0.84f, 0.5f);
    float u = total;
    w.frameLights.push_back({c + vec3(0, 1.2f, 0), 4.0f, gold * 14.0f * std::sin(glm::clamp(u / 2.0f, 0.0f, 1.0f) * kPI)});
    if (u < 1.4f) {
      for (int i = 0; i < 3; ++i) {
        Particle p;
        float a = w.fx.rnd(0, 6.28f);
        p.pos = c + vec3(std::cos(a) * 0.35f, w.fx.rnd(0.0f, 0.2f), std::sin(a) * 0.35f);
        p.vel = vec3(0, w.fx.rnd(1.0f, 2.5f), 0);
        p.size = w.fx.rnd(0.01f, 0.03f);
        p.color = gold * w.fx.rnd(2.0f, 5.0f);
        p.colorEnd = gold * 0.3f;
        p.opacity = 0;
        p.maxLife = w.fx.rnd(0.6f, 1.2f);
        p.type = PT_SPARK;
        p.stretch = 0.04f;
        p.drag = 0.5f;
        w.fx.add(p);
      }
    }
    if (first) w.sound(SFX_CHIME, 1.0f, c);
    Actor& p = w.actors[pawn_];
    if (p.alive) {
      p.glow = glm::vec4(gold, std::min(1.5f, u * 2.0f));
      if (u > 0.5f) p.dissolve = glm::clamp((u - 0.5f) / 0.7f, 0.0f, 1.0f);
      p.dissolveColor = gold;
      if (p.dissolve >= 1.0f) p.alive = false;
    }
    if (u > 0.85f && newId_ < 0) {
      Actor n = p;
      n.id = (int)w.actors.size();
      n.kind = kind_;
      if (n.set != 0) n.resetControls();
      n.alive = true;
      n.visible = true;
      n.dissolve = 1.0f;
      n.dissolveVel = -1.1f;
      n.dissolveColor = gold;
      n.first = true;
      n.glow = glm::vec4(gold, 1.5f);
      n.square = sq_;
      newId_ = n.id;
      w.actors.push_back(n);
      w.actorAt[sq_] = newId_;
      w.fx.magicBurst(c + vec3(0, 0.6f, 0), gold * 4.0f, 60, 2.0f);
    }
    if (newId_ >= 0) {
      Actor& n = w.actors[newId_];
      n.glow = glm::vec4(gold, glm::max(0.0f, 1.5f - (u - 0.85f) * 1.2f));
    }
    if (u > 2.4f) {
      if (newId_ >= 0) w.actors[newId_].glow = glm::vec4(0);
      w.actors[pawn_].alive = false;
      return true;
    }
    return false;
  }
};

class SeqAnim : public Anim {
 public:
  void add(std::unique_ptr<Anim> a) {
    if (a) list_.push_back(std::move(a));
  }
  bool update(World& w, float dt) override {
    while (i_ < list_.size()) {
      if (!list_[i_]->update(w, dt)) return false;
      ++i_;
      dt = 1e-5f;
    }
    return true;
  }

 private:
  std::vector<std::unique_ptr<Anim>> list_;
  size_t i_ = 0;
};

// ---------------------------------------------------------------------------
// Fast capture (theatrics disabled): quick strike + shatter
// ---------------------------------------------------------------------------
class QuickCapture : public BattleBase {
 public:
  using BattleBase::BattleBase;

 protected:
  bool step(World& w, float dt) override {
    Actor& v = w.actors[plan.victim];
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
      case 1:
        if (moveIn(w, dt)) next();
        break;
      case 2:
        if (enter()) w.actors[plan.mover].yawTarget = w.actors[plan.mover].baseYaw;
        return pt > 0.1f;
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
class CheckmateAnim : public Staged {
 public:
  CheckmateAnim(int king, int winner) : king_(king), winner_(winner) {}

 protected:
  int king_, winner_;
  float theta = 0, omega = 0;
  vec3 tdir{0, 0, 1};
  bool step(World& w, float dt) override {
    Actor& k = w.actors[king_];
    const vec3 red(1.0f, 0.18f, 0.06f);
    switch (phase) {
      case 0: {
        if (enter()) {
          w.sound(SFX_MATE, 1.0f, k.pos);
          if (w.cinematic) {
            CamPose p;
            p.target = k.pos + vec3(0, 0.8f, 0);
            vec3 back = glm::normalize(flat(w.cam.orbitPose().pos - k.pos) + vec3(1e-4f));
            p.pos = p.target + back * 3.6f + vec3(0, 1.3f, 0);
            p.fov = 32;
            p.focus = glm::length(p.pos - p.target);
            p.aperture = 7.0f;
            w.cam.setShot(p, 0.9f);
          }
        }
        float u = glm::clamp(pt / 1.5f, 0.0f, 1.0f);
        k.tremble = 0.4f + u;
        k.crackColor = red;
        k.crack = easeIn(u) * 0.9f;
        k.glow = glm::vec4(red, 0.6f * u);
        k.idleWeight = 0;
        w.frameLights.push_back({k.pos + vec3(0, 0.8f, 0), 3.0f, red * (3.0f + 10.0f * u)});
        if (u >= 1.0f) {
          tdir = glm::normalize(vec3(std::sin(k.yaw), 0, std::cos(k.yaw)));
          k.tiltPivot = vec3(0, 0, -k.radius() / k.scale * 0.9f);
          omega = 1.5f;
          w.sound(SFX_RUMBLE, 0.8f, k.pos, 0.8f);
          next();
        }
        break;
      }
      case 1: {
        k.tremble = 0.3f;
        omega += 9.0f * std::sin(theta + 0.2f) * dt;
        theta += omega * dt;
        k.tilt = glm::angleAxis(-theta, vec3(1, 0, 0));
        if (theta > 1.25f) {
          vec3 top = k.pos - tdir * k.height() * 0.7f + vec3(0, 0.3f, 0);
          w.shatter(k, top, -tdir * omega * 0.7f, 1.2f, 0.7f, 6.0f);
          w.fx.dust(k.pos, 30, 0.8f, 2.0f);
          w.groundRing(k.pos, red * 1.5f, 3.0f, 0.9f, 0.1f);
          w.light(k.pos + vec3(0, 0.5f, 0), red * 20.0f, 4.0f, 0.5f);
          w.shake(0.5f);
          w.slowmo(0.35f, 0.6f);
          next();
        }
        break;
      }
      case 2: {  // winners celebrate
        for (Actor& a : w.actors)
          if (a.alive && a.color == winner_) a.selected = pt > (a.phase - std::floor(a.phase)) * 0.4f && pt < 2.4f;
        if (pt > 2.6f) {
          for (Actor& a : w.actors) a.selected = false;
          if (w.cinematic) w.cam.release(1.2f);
          return true;
        }
        break;
      }
    }
    return false;
  }
};

class StalemateAnim : public Staged {
 protected:
  bool step(World& w, float dt) override {
    for (Actor& a : w.actors) {
      if (!a.alive) continue;
      float u = glm::clamp(pt / 2.0f, 0.0f, 1.0f);
      a.poseTwist = std::sin(pt * 7.0f + a.phase) * 0.35f * std::sin(u * kPI);
    }
    if (pt > 2.0f) {
      for (Actor& a : w.actors) a.poseTwist = 0;
      return true;
    }
    return false;
  }
};

const char* const kStyleNames[meshgen::K_COUNT][kBattleStyles] = {
    {"Head-butt", "Bull charge", "Whirlwind"},
    {"Trample", "Buck kick", "Joust"},
    {"Holy bolt", "Judgment beam", "Seraph orbs"},
    {"Cannon", "Timber!", "Earthquake"},
    {"Telekinesis", "Frost nova", "Vortex"},
    {"Royal blade", "Royal decree", "Rain of blades"},
};

// A random style for this piece type - never the same one twice in a row.
int pickStyle(int kind, int forced) {
  static std::mt19937 rng{std::random_device{}()};
  static int last[meshgen::K_COUNT] = {-1, -1, -1, -1, -1, -1};
  int s = forced >= 0 ? forced % kBattleStyles : (int)(rng() % kBattleStyles);
  if (forced < 0 && s == last[kind]) s = (s + 1 + (int)(rng() % (kBattleStyles - 1))) % kBattleStyles;
  last[kind] = s;
  return s;
}

std::unique_ptr<Anim> makeBattle(int kind, int style, const MovePlan& plan) {
  switch (kind * kBattleStyles + style) {
    case K_PAWN * kBattleStyles + 1: return std::make_unique<PawnCharge>(plan);
    case K_PAWN * kBattleStyles + 2: return std::make_unique<PawnWhirl>(plan);
    case K_KNIGHT * kBattleStyles + 0: return std::make_unique<KnightBattle>(plan);
    case K_KNIGHT * kBattleStyles + 1: return std::make_unique<KnightKick>(plan);
    case K_KNIGHT * kBattleStyles + 2: return std::make_unique<KnightJoust>(plan);
    case K_BISHOP * kBattleStyles + 0: return std::make_unique<BishopBattle>(plan);
    case K_BISHOP * kBattleStyles + 1: return std::make_unique<BishopBeam>(plan);
    case K_BISHOP * kBattleStyles + 2: return std::make_unique<BishopOrbs>(plan);
    case K_ROOK * kBattleStyles + 0: return std::make_unique<RookBattle>(plan);
    case K_ROOK * kBattleStyles + 1: return std::make_unique<RookTopple>(plan);
    case K_ROOK * kBattleStyles + 2: return std::make_unique<RookQuake>(plan);
    case K_QUEEN * kBattleStyles + 0: return std::make_unique<QueenBattle>(plan);
    case K_QUEEN * kBattleStyles + 1: return std::make_unique<QueenFrost>(plan);
    case K_QUEEN * kBattleStyles + 2: return std::make_unique<QueenVortex>(plan);
    case K_KING * kBattleStyles + 0: return std::make_unique<KingBattle>(plan);
    case K_KING * kBattleStyles + 1: return std::make_unique<KingDecree>(plan);
    case K_KING * kBattleStyles + 2: return std::make_unique<KingBlades>(plan);
    default: return std::make_unique<PawnBattle>(plan);
  }
}

}  // namespace

const char* battleStyleName(int kind, int style, int set) {
  if (set == 1) return medievalBattleStyleName(kind, style);
  if (set == 2) return alienBattleStyleName(kind, style);
  if (kind < 0 || kind >= meshgen::K_COUNT || style < 0 || style >= kBattleStyles) return "";
  return kStyleNames[kind][style];
}

std::unique_ptr<Anim> makeMoveAnim(World& w, const MovePlan& plan) {
  auto seq = std::make_unique<SeqAnim>();
  int set = w.actors[plan.mover].set;
  bool med = set == 1, ali = set == 2;
  if (plan.victim >= 0) {
    if (plan.fast) {
      if (med) seq->add(makeMedievalQuickCapture(plan));
      else if (ali) seq->add(makeAlienQuickCapture(plan));
      else seq->add(std::make_unique<QuickCapture>(plan));
    } else {
      int kind = w.actors[plan.mover].kind;
      int style = pickStyle(kind, plan.style);
      seq->add(med ? makeMedievalBattle(kind, style, plan)
                   : (ali ? makeAlienBattle(kind, style, plan) : makeBattle(kind, style, plan)));
    }
  } else {
    if (med) seq->add(makeMedievalQuiet(plan));
    else if (ali) seq->add(makeAlienQuiet(plan));
    else seq->add(std::make_unique<QuietMove>(plan));
  }
  if (plan.promoKind >= 0) seq->add(std::make_unique<PromotionAnim>(plan.mover, plan.promoSquare, plan.promoKind));
  return seq;
}

std::unique_ptr<Anim> makeCheckmateAnim(World& w, int kingId, int winnerColor) {
  if (w.actors[kingId].set == 1) return makeMedievalCheckmate(kingId, winnerColor);
  if (w.actors[kingId].set == 2) return makeAlienCheckmate(kingId, winnerColor);
  return std::make_unique<CheckmateAnim>(kingId, winnerColor);
}
std::unique_ptr<Anim> makeStalemateAnim(World& w) { return std::make_unique<StalemateAnim>(); }

std::unique_ptr<Anim> makeDemoBattle(World& w, int attackerId, int victimId) {
  MovePlan p;
  p.mover = attackerId;
  p.victim = victimId;
  p.move.from = (uint8_t)w.actors[attackerId].square;
  p.move.to = (uint8_t)w.actors[victimId].square;
  p.move.flags = chess::MF_CAPTURE;
  return makeMoveAnim(w, p);
}
