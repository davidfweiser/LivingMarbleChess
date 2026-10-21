#include "animation.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

using glm::quat;
using glm::vec2;
using glm::vec3;
using glm::vec4;

float wrapAngle(float a) {
  while (a > kPI) a -= 2 * kPI;
  while (a < -kPI) a += 2 * kPI;
  return a;
}
float easeInOut(float t) {
  t = glm::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}
float easeOut(float t) {
  t = glm::clamp(t, 0.0f, 1.0f);
  return 1 - (1 - t) * (1 - t);
}
float easeIn(float t) {
  t = glm::clamp(t, 0.0f, 1.0f);
  return t * t;
}
float easeOutBack(float t, float s) {
  t = glm::clamp(t, 0.0f, 1.0f) - 1.0f;
  return t * t * ((s + 1) * t + s) + 1;
}

void Spring1::step(float target, float k, float zeta, float dt) {
  float c = 2.0f * zeta * std::sqrt(k);
  int n = std::max(1, (int)std::ceil(dt * 240.0f));
  float h = dt / n;
  for (int i = 0; i < n; ++i) {
    float a = k * (target - x) - c * v;
    v += a * h;
    x += v * h;
  }
}

// ---------------------------------------------------------------------------
// Actor
// ---------------------------------------------------------------------------
void Actor::update(float dt, float t, Audio* audio) {
  if (dt <= 0) return;
  if (first) {
    prevPos = pos;
    vel = vec3(0);
    accS = vec3(0);
    sq.x = 1;
    first = false;
  }
  float prevY = prevPos.y;
  vec3 nv = (pos - prevPos) / dt;
  vec3 acc = (nv - vel) / dt;
  vel = nv;
  prevPos = pos;
  float k = 1.0f - std::exp(-dt * 22.0f);
  accS += (glm::clamp(acc, vec3(-80.0f), vec3(80.0f)) - accS) * k;

  vec3 side(std::cos(yaw), 0, -std::sin(yaw)), fwd(std::sin(yaw), 0, std::cos(yaw));
  vec2 accL(glm::dot(accS, side), glm::dot(accS, fwd));
  vec2 inertia = -accL * 0.011f;
  float il = glm::length(inertia);
  if (il > 0.5f) inertia *= 0.5f / il;
  vec2 idle = vec2(std::sin(t * 0.71f + phase), std::sin(t * 0.53f + phase * 1.7f)) * 0.012f * idleWeight;
  float breath = 1.0f + 0.009f * std::sin(t * 1.8f + phase) * idleWeight;
  vec2 trem(0);
  if (tremble > 0)
    trem = vec2(std::sin(t * 47.0f + phase), std::sin(t * 41.0f + phase * 2.0f)) * 0.035f * tremble;

  // eager hopping while selected
  if (selected) {
    selectT += dt;
    float s = std::sin(selectT * 5.5f);
    float target = 0.07f * std::fabs(s);
    bob += (target - bob) * (1.0f - std::exp(-dt * 30.0f));
    if (prevSelS * s < 0 && selectT > 0.05f) sq.v -= 1.6f;
    prevSelS = s;
  } else {
    selectT = 0;
    prevSelS = 0;
    bob *= std::exp(-dt * 12.0f);
  }

  vec2 bt = poseBend + idle + inertia + trem;
  bx.step(bt.x, 150.0f, 0.30f, dt);
  bz.step(bt.y, 150.0f, 0.30f, dt);
  float vy = std::fabs(vel.y);
  float sqT = poseSquash * breath * (1.0f + std::min(vy * 0.025f, 0.12f));
  if (prevY > 0.012f && pos.y <= 0.006f && vel.y < -0.8f) sq.v -= std::min(-vel.y * 1.1f, 5.0f);
  sq.step(sqT, 260.0f, 0.22f, dt);
  tw.step(poseTwist, 120.0f, 0.45f, dt);
  float d = wrapAngle(yawTarget - yaw);
  yaw = wrapAngle(yaw + d * (1.0f - std::exp(-dt * yawRate)));
  if (dissolveVel != 0) {
    dissolve = glm::clamp(dissolve + dissolveVel * dt, 0.0f, 1.0f);
    if (dissolve <= 0.0f || dissolve >= 1.0f) dissolveVel = 0;
  }
  vec2 b(bx.x, bz.x);
  rig = vec4(glm::length(b), std::atan2(b.y, b.x), glm::clamp(sq.x, 0.35f, 1.9f), tw.x);
  if (set != 0) {
    ctl.idle += dt;
    figurePose(set, kind, ctl, pose);
    if (!seatFree) {
      seatPos = pos;
      seatYaw = yaw;
    }
  }
  (void)audio;
}

void figurePose(int set, int asset, const medieval::Controls& c, medieval::Pose& out) {
  if (set == 2) alien::buildPose(asset, c, out);
  else medieval::buildPose(asset, c, out);
}

void Actor::resetControls() {
  float idle = ctl.idle;
  ctl = medieval::Controls();
  ctl.idle = idle;
  ctl.sit = set == 1 && medieval::throned(kind) ? 1.0f : 0.0f;
  seatFree = false;
  seatPos = pos;
  seatYaw = yaw;
  heat = 0;
  figurePose(set, kind, ctl, pose);
}

float Actor::height() const {
  float h = set == 1 ? medieval::pieceHeight(kind) : (set == 2 ? alien::pieceHeight(kind) : meshgen::pieceHeight(kind));
  return h * scale;
}
float Actor::radius() const {
  float r = set == 1 ? medieval::pieceRadius(kind) : (set == 2 ? alien::pieceRadius(kind) : meshgen::pieceRadius(kind));
  return r * scale;
}

glm::mat4 Puppet::model() const {
  glm::mat4 M = glm::translate(glm::mat4(1.0f), pos);
  M = glm::rotate(M, yaw, vec3(0, 1, 0));
  M = M * glm::mat4_cast(tilt);
  return glm::scale(M, vec3(scale));
}

void Actor::neutralize() {
  bx = bz = tw = Spring1();
  sq.x = 1;
  sq.v = 0;
  rig = vec4(0, 0, 1, 0);
  bob = 0;
}

quat Actor::rotation() const { return glm::angleAxis(yaw, vec3(0, 1, 0)) * tilt; }

glm::mat4 Actor::rigidModel() const {
  glm::mat4 M = glm::translate(glm::mat4(1.0f), pos + vec3(0, set == 1 ? bob * 0.3f : bob, 0));
  M = glm::rotate(M, yaw, vec3(0, 1, 0));
  return glm::scale(M, vec3(scale));
}

glm::mat4 Actor::model() const {
  glm::mat4 M = glm::translate(glm::mat4(1.0f), pos + vec3(0, set == 1 ? bob * 0.3f : bob, 0));
  M = glm::rotate(M, yaw, vec3(0, 1, 0));
  if (std::fabs(tilt.w) < 0.99999f) {
    M = glm::translate(M, tiltPivot * scale);
    M = M * glm::mat4_cast(tilt);
    M = glm::translate(M, -tiltPivot * scale);
  }
  return glm::scale(M, vec3(scale));
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
void CameraDirector::orbit(float dy, float dp, float dd) {
  yaw = wrapAngle(yaw + dy);
  pitch = glm::clamp(pitch + dp, -1.45f, 1.45f);  // the board floats: look at it from above or below
  dist = glm::clamp(dist * std::exp(dd), 5.0f, 24.0f);
}
void CameraDirector::setSide(int color, bool instant) {
  yaw = color == 0 ? 0.0f : kPI;
  if (instant) yawS_ = yaw;
}
void CameraDirector::setShot(const CamPose& p, float blendTime) {
  shot_ = p;
  shotOn_ = true;
  shotT_ = 0;
  blendSpeed_ = 1.0f / std::max(0.05f, blendTime);
}
void CameraDirector::release(float blendTime) {
  shotOn_ = false;
  blendSpeed_ = 1.0f / std::max(0.05f, blendTime);
}
CamPose CameraDirector::orbitPose() const {
  CamPose p;
  p.target = target;
  p.pos = target + distS_ * vec3(std::sin(yawS_) * std::cos(pitchS_), std::sin(pitchS_), std::cos(yawS_) * std::cos(pitchS_));
  p.fov = 38.0f;
  p.focus = glm::length(p.pos - p.target);
  p.aperture = 2.2f;
  return p;
}
CamPose CameraDirector::evaluate(float dt, float trauma, float time) {
  if (!init_) {
    yawS_ = yaw;
    pitchS_ = pitch;
    distS_ = dist;
    init_ = true;
  }
  if (autoSpin != 0) yaw = wrapAngle(yaw + autoSpin * dt);
  float k = 1.0f - std::exp(-dt * 5.0f);
  yawS_ = wrapAngle(yawS_ + wrapAngle(yaw - yawS_) * k);
  pitchS_ += (pitch - pitchS_) * k;
  distS_ += (dist - distS_) * k;
  CamPose o = orbitPose();
  shotT_ += dt;
  blend_ = glm::clamp(blend_ + (shotOn_ ? 1.0f : -1.0f) * blendSpeed_ * dt, 0.0f, 1.0f);
  float e = easeInOut(blend_);
  CamPose r = o;
  if (e > 0) {
    CamPose s = shot_;
    // slow cinematic drift around the action
    vec3 off = s.pos - s.target;
    float a = shotT_ * 0.07f;
    off = vec3(std::cos(a) * off.x + std::sin(a) * off.z, off.y, -std::sin(a) * off.x + std::cos(a) * off.z);
    s.pos = s.target + off;
    vec3 oo = o.pos - o.target, so = s.pos - s.target;
    float lo = glm::length(oo), ls = glm::length(so);
    vec3 dir = glm::normalize(glm::mix(oo / lo, so / ls, e) + vec3(1e-5f));
    r.target = glm::mix(o.target, s.target, e);
    r.pos = r.target + dir * glm::mix(lo, ls, e);
    r.fov = glm::mix(o.fov, s.fov, e);
    r.focus = glm::length(r.pos - r.target);
    r.aperture = glm::mix(o.aperture, s.aperture, e);
  }
  float amt = trauma * trauma * 0.10f;
  if (amt > 0) {
    vec3 n(std::sin(time * 37.0f) + std::sin(time * 23.0f + 1.3f), std::sin(time * 31.0f + 2.1f) + std::sin(time * 19.0f),
           std::sin(time * 29.0f + 4.2f) + std::sin(time * 41.0f + 0.7f));
    r.pos += n * amt;
    r.target += n * amt * 0.4f;
  }
  current = r;
  return r;
}

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
Puppet* World::puppet(int id) {
  for (Puppet& p : puppets)
    if (p.id == id) return &p;
  return nullptr;
}
int World::addPuppet(int asset, int color, int set) {
  Puppet p;
  p.asset = asset;
  p.color = color;
  p.set = set;
  p.id = nextPuppetId++;
  p.texOffset = vec3(fx.rnd(-20, 20), fx.rnd(-20, 20), fx.rnd(-20, 20));
  figurePose(set, asset, p.ctl, p.pose);
  puppets.push_back(p);
  return p.id;
}

glm::mat4 World::seatModel(const Actor& a) const {
  glm::mat4 M = glm::translate(glm::mat4(1.0f), a.seatPos);
  M = glm::rotate(M, a.seatYaw, vec3(0, 1, 0));
  return glm::scale(M, vec3(a.scale * medieval::seatScale(a.kind)));
}

const meshgen::PieceAssets& World::figureAssets(int set, int asset) const {
  return set == 2 ? alib->asset[asset] : mlib->asset[asset];
}

const meshgen::PieceAssets& World::assetsOf(const Chunk& c) const {
  return c.set != 0 ? figureAssets(c.set, c.kind) : lib->piece[c.kind];
}

void World::reset() {
  actors.clear();
  puppets.clear();
  for (int& a : actorAt) a = -1;
  fx.clear();
  chunks.clear();
  props.clear();
  lights.clear();
  rings.clear();
  frameLights.clear();
  trauma = 0;
  timeScale = slowTarget = 1;
  slowTimer = 0;
  flash = vec3(0);
  ghosts.clear();
  ghostFocus = vec3(0);
}

void World::spawnFromBoard(const uint8_t sq[64], bool materialize) {
  actors.clear();
  for (int& a : actorAt) a = -1;
  for (int s = 0; s < 64; ++s) {
    int p = sq[s];
    if (!p) continue;
    Actor a;
    a.id = (int)actors.size();
    a.kind = (p & 7) - 1;
    a.color = p >> 3;
    a.square = s;
    a.set = pieceSet;
    a.pos = squareCenter(s);
    a.baseYaw = a.color == 0 ? kPI : 0.0f;
    a.yaw = a.yawTarget = a.baseYaw;
    a.phase = fx.rnd(0, 100);
    a.ctl.idle = fx.rnd(0, 100);
    a.resetControls();
    a.texOffset = vec3(fx.rnd(-20, 20), fx.rnd(-20, 20), fx.rnd(-20, 20));
    if (materialize) {
      a.dissolve = 1.0f;
      a.dissolveVel = -fx.rnd(0.9f, 1.4f);
      a.dissolveColor = vec3(1.0f, 0.75f, 0.35f);
    }
    actorAt[s] = a.id;
    actors.push_back(a);
  }
}

void World::sound(Sfx s, float vol, const vec3& at, float pitch) {
  if (!audio) return;
  // pan relative to the camera's right vector
  vec3 f = glm::normalize(cam.current.target - cam.current.pos);
  vec3 right = glm::normalize(glm::cross(f, vec3(0, 1, 0)) + vec3(1e-5f));
  float pan = glm::clamp(glm::dot(at - cam.current.target, right) / 5.0f, -0.8f, 0.8f);
  audio->play(s, vol, pan, pitch * (timeScale < 0.9f ? glm::mix(0.75f, 1.0f, timeScale) : 1.0f));
}

Prop* World::prop(int id) {
  for (Prop& p : props)
    if (p.id == id) return &p;
  return nullptr;
}
int World::addProp(Prop::Type t) {
  Prop p;
  p.type = t;
  p.id = nextPropId++;
  props.push_back(p);
  return p.id;
}

void World::shatterAsset(int asset, int color, const glm::mat4& M, const quat& R, const vec3& vel,
                         const vec3& texOffset, const vec3& impact, const vec3& impulse, float explode, float upBias,
                         float spin, float glow, const vec3& glowColor, int set) {
  const meshgen::PieceAssets& A = figureAssets(set, asset);
  float sc = glm::length(vec3(M[0]));
  vec3 center = vec3(M * vec4(0, A.height * 0.5f, 0, 1));
  for (size_t i = 0; i < A.shards.size(); ++i) {
    const meshgen::Fragment& f = A.shards[i];
    Chunk c;
    c.set = set;
    c.kind = asset;
    c.color = color;
    c.index = (int)i;
    c.pos = vec3(M * vec4(f.centroid, 1));
    c.rot = R;
    c.radius = f.radius * sc;
    c.scale = sc;
    c.mass = std::max(f.volume, 1e-4f);
    c.inertia = 0.4f * c.mass * c.radius * c.radius;
    vec3 dir = glm::normalize(c.pos - center + fx.rndDir() * 0.05f);
    float fall = std::exp(-glm::length(c.pos - impact) * 2.2f);
    c.v = vel * 0.5f + dir * explode * fx.rnd(0.5f, 1.2f) + impulse * fall * fx.rnd(0.7f, 1.3f) +
          vec3(0, upBias * fx.rnd(0.4f, 1.2f), 0);
    c.w = fx.rndDir() * spin * fx.rnd(0.3f, 1.0f);
    c.texOffset = texOffset;
    c.glow = glow;
    c.glowColor = glowColor;
    chunks.push_back(c);
  }
}

// glittering glass shards flying from an alien's canopy or jar
void World::glassBurst(const Actor& a) {
  vec3 c = vec3(a.model() * glm::vec4(alien::headPoint(a.kind), 1.0f));
  for (int i = 0; i < 70; ++i) {
    Particle p;
    vec3 d = fx.rndDir();
    p.pos = c + d * 0.12f * a.scale;
    p.vel = d * fx.rnd(1.0f, 3.2f) + vec3(0, fx.rnd(0.5f, 2.0f), 0);
    p.size = fx.rnd(0.006f, 0.014f);
    p.color = vec3(1.4f, 1.6f, 1.8f) * fx.rnd(0.6f, 1.4f);
    p.colorEnd = p.color * 0.4f;
    p.opacity = 0;
    p.maxLife = fx.rnd(0.7f, 1.6f);
    p.type = PT_SPARK;
    p.stretch = 0.012f;
    p.gravity = 9.0f;
    p.drag = 0.5f;
    p.bounce = true;
    p.fadeIn = 0.0f;
    fx.add(p);
  }
  sound(SFX_CHIME, 0.5f, c, 2.4f);
  sound(SFX_SHATTER, 0.5f, c, 1.6f);
}

void World::shatter(Actor& a, const vec3& impact, const vec3& impulse, float explode, float upBias, float spin) {
  if (a.set != 0) {
    a.neutralize();
    glm::mat4 M = a.model();
    quat R = a.rotation();
    float glow = a.crack > 0.05f ? 1.0f : 0.0f;
    shatterAsset(a.kind, a.color, M, R, a.vel, a.texOffset, impact, impulse, explode, upBias, spin, glow, a.crackColor,
                 a.set);
    // the saucer's canopy and the psychic's jar burst into glass
    if (a.set == 2 && alien::glassAsset(a.kind) >= 0) glassBurst(a);
    if (a.hasSeat() && !a.seatFree)
      shatterAsset(medieval::seatAsset(a.kind), a.color, seatModel(a), glm::angleAxis(a.seatYaw, vec3(0, 1, 0)),
                   a.vel, a.texOffset + vec3(3.1f), impact, impulse, explode * 0.8f, upBias * 0.7f, spin * 0.7f, glow,
                   a.crackColor);
    a.alive = false;
    a.visible = false;
    vec3 center = vec3(M * vec4(0, a.height() * 0.5f / std::max(a.scale, 1e-3f), 0, 1));
    vec3 tint = a.color == 0 ? vec3(0.62f, 0.60f, 0.57f) : vec3(0.22f, 0.21f, 0.2f);
    fx.dust(vec3(center.x, 0.05f, center.z), 16, 0.3f, 1.2f, tint);
    fx.smoke(center, 6, 0.2f, 0.3f, a.color == 0 ? 0.1f : 0.6f);
    fx.chips(center, 45, 3.2f, a.color == 1);
    sound(SFX_SHATTER, 1.0f, center, fx.rnd(0.92f, 1.08f));
    return;
  }
  const meshgen::PieceAssets& A = lib->piece[a.kind];
  a.neutralize();
  glm::mat4 M = a.model();
  quat R = a.rotation();
  vec3 center = vec3(M * vec4(0, A.height * 0.5f, 0, 1));
  for (size_t i = 0; i < A.shards.size(); ++i) {
    const meshgen::Fragment& f = A.shards[i];
    Chunk c;
    c.kind = a.kind;
    c.color = a.color;
    c.index = (int)i;
    c.pos = vec3(M * vec4(f.centroid, 1));
    c.rot = R;
    c.radius = f.radius * a.scale;
    c.scale = a.scale;
    c.mass = std::max(f.volume, 1e-4f);
    c.inertia = 0.4f * c.mass * c.radius * c.radius;
    vec3 dir = glm::normalize(c.pos - center + fx.rndDir() * 0.05f);
    float fall = std::exp(-glm::length(c.pos - impact) * 2.2f);
    c.v = a.vel * 0.5f + dir * explode * fx.rnd(0.5f, 1.2f) + impulse * fall * fx.rnd(0.7f, 1.3f) +
          vec3(0, upBias * fx.rnd(0.4f, 1.2f), 0);
    c.w = fx.rndDir() * spin * fx.rnd(0.3f, 1.0f);
    c.texOffset = a.texOffset + f.centroid * 1.25f;
    c.glow = a.crack > 0.05f ? 1.0f : 0.0f;
    c.glowColor = a.crackColor;
    chunks.push_back(c);
  }
  a.alive = false;
  a.visible = false;
  vec3 tint = a.color == 0 ? vec3(0.62f, 0.60f, 0.57f) : vec3(0.22f, 0.21f, 0.2f);
  fx.dust(vec3(center.x, 0.05f, center.z), 16, 0.3f, 1.2f, tint);
  fx.smoke(center, 6, 0.2f, 0.3f, a.color == 0 ? 0.1f : 0.6f);
  fx.chips(center, 45, 3.2f, a.color == 1);
  sound(SFX_SHATTER, 1.0f, center, fx.rnd(0.92f, 1.08f));
}

void World::slice(Actor& a, float sep) {
  const meshgen::PieceAssets& A = a.set != 0 ? figureAssets(a.set, a.kind) : lib->piece[a.kind];
  a.neutralize();
  glm::mat4 M = a.model();
  quat R = a.rotation();
  vec3 nW = R * A.sliceN;
  vec3 down(0, -1, 0);
  vec3 td = down - nW * glm::dot(down, nW);
  td = glm::length(td) > 1e-3f ? glm::normalize(td) : vec3(0, -1, 0);
  for (size_t i = 0; i < A.halves.size(); ++i) {
    const meshgen::Fragment& f = A.halves[i];
    Chunk c;
    c.set = a.set;
    c.kind = a.kind;
    c.color = a.color;
    c.index = (int)i;
    c.half = true;
    c.pos = vec3(M * vec4(f.centroid, 1));
    c.rot = R;
    c.radius = f.radius * a.scale;
    c.scale = a.scale;
    c.mass = std::max(f.volume, 1e-4f);
    c.inertia = 0.4f * c.mass * c.radius * c.radius;
    if (i == 0) {
      c.v = nW * sep + td * 0.8f + vec3(0, 0.6f, 0);
      c.w = glm::cross(nW, td) * 1.8f + fx.rndDir() * 0.5f;
    } else {
      c.v = -nW * sep * 0.25f;
      c.w = fx.rndDir() * 0.6f;
    }
    c.texOffset = a.texOffset + f.centroid * 1.25f;
    c.glow = 1.0f;
    c.glowColor = vec3(1.0f, 0.75f, 0.35f);
    chunks.push_back(c);
  }
  // a throne under a figure that is cut in two breaks apart as well
  if (a.hasSeat() && !a.seatFree)
    shatterAsset(medieval::seatAsset(a.kind), a.color, seatModel(a), glm::angleAxis(a.seatYaw, vec3(0, 1, 0)), a.vel,
                 a.texOffset + vec3(3.1f), a.pos + vec3(0, 0.5f, 0), vec3(0, -1.0f, 0), 1.0f, 0.5f, 5.0f, 0.0f,
                 vec3(1.0f));
  // ... and a glass canopy or jar bursts
  if (a.set == 2 && alien::glassAsset(a.kind) >= 0) glassBurst(a);
  a.alive = false;
  a.visible = false;
}

void World::updateChunks(float dt) {
  const float g = 14.0f;
  for (Chunk& c : chunks) {
    c.age += dt;
    c.glow *= std::exp(-dt * 1.6f);
    if (c.age > 3.2f && (c.sleeping || c.age > 6.0f)) {
      if (c.dissolve == 0) fx.dust(c.pos, 2, 0.05f, 0.3f, c.color == 0 ? vec3(0.6f) : vec3(0.2f));
      c.dissolve = std::min(1.0f, c.dissolve + dt * 0.85f);
    }
    // standing pieces act as cylinders and shove debris (even resting debris) aside
    for (const Actor& a : actors) {
      if (!a.alive || !a.visible) continue;
      float h = a.pos.y + a.height();
      if (c.pos.y - c.radius * 0.5f > h || c.pos.y + c.radius < a.pos.y) continue;
      vec2 d(c.pos.x - a.pos.x, c.pos.z - a.pos.z);
      float dist = glm::length(d);
      float minD = a.radius() * 0.85f + c.radius * 0.55f;
      if (dist < minD) {
        vec2 n = dist > 1e-4f ? d / dist : vec2(1, 0);
        float push = minD - dist;
        c.pos.x += n.x * push;
        c.pos.z += n.y * push;
        float vn = c.v.x * n.x + c.v.z * n.y;
        float shove = std::min(2.5f, glm::length(vec2(a.vel.x, a.vel.z)) * 1.2f);
        if (vn < shove) {
          c.v.x += n.x * (shove - vn);
          c.v.z += n.y * (shove - vn);
        }
        if (c.sleeping && push > 0.004f) {
          c.sleeping = false;
          c.still = 0;
          c.w += vec3(n.y, 0, -n.x) * 3.0f;
        }
      }
    }
    if (c.sleeping) continue;
    c.v.y -= g * dt;
    c.pos += c.v * dt;
    quat wq(0, c.w.x, c.w.y, c.w.z);
    c.rot = glm::normalize(c.rot + (wq * c.rot) * (0.5f * dt));
    // the board floats in the air: debris that leaves it tumbles away below
    bool onBoard = std::fabs(c.pos.x) < 4.62f && std::fabs(c.pos.z) < 4.62f && c.pos.y > -0.1f;
    if (!onBoard) continue;
    const float floorY = 0.0f;
    const meshgen::Fragment& F = c.half ? assetsOf(c).halves[c.index] : assetsOf(c).shards[c.index];
    float maxPen = 0;
    bool contact = false;
    for (const vec3& hp : F.hull) {
      vec3 r = c.rot * (hp * c.scale);
      vec3 p = c.pos + r;
      float pen = floorY - p.y;
      if (pen <= 0) continue;
      contact = true;
      maxPen = std::max(maxPen, pen);
      vec3 vp = c.v + glm::cross(c.w, r);
      if (vp.y >= 0) continue;
      const vec3 n(0, 1, 0);
      float invM = 1.0f / c.mass, invI = 1.0f / c.inertia;
      vec3 rn = glm::cross(r, n);
      float e = std::fabs(vp.y) > 1.0f ? 0.28f : 0.0f;
      float j = -(1 + e) * vp.y / (invM + glm::dot(rn, rn) * invI);
      c.v += n * j * invM;
      c.w += invI * glm::cross(r, n * j);
      vec3 vt = vp - n * vp.y;
      float vtl = glm::length(vt);
      if (vtl > 1e-4f) {
        vec3 t = -vt / vtl;
        vec3 rt = glm::cross(r, t);
        float jt = std::min(0.55f * j, vtl / (invM + glm::dot(rt, rt) * invI));
        c.v += t * jt * invM;
        c.w += invI * glm::cross(r, t * jt);
      }
      if (-vp.y > 1.4f && c.age - c.lastHit > 0.12f) {
        c.lastHit = c.age;
        sound(SFX_DEBRIS, std::min(1.0f, -vp.y / 6.0f) * 0.7f, c.pos, fx.rnd(0.8f, 1.3f) * (1.0f / (0.6f + c.radius * 3)));
        if (-vp.y > 3.0f) fx.chips(p, 3, 1.2f, c.color == 1);
      }
    }
    if (contact) {
      c.pos.y += maxPen;
      c.w *= std::exp(-dt * 2.0f);
      if (glm::length(c.v) < 0.06f && glm::length(c.w) < 0.4f) {
        c.still += dt;
        if (c.still > 0.35f) {
          c.sleeping = true;
          c.v = c.w = vec3(0);
        }
      } else {
        c.still = 0;
      }
    }
  }
  // chunk vs chunk separation
  for (size_t i = 0; i < chunks.size(); ++i)
    for (size_t j = i + 1; j < chunks.size(); ++j) {
      Chunk &a = chunks[i], &b = chunks[j];
      vec3 d = b.pos - a.pos;
      float r = (a.radius + b.radius) * 0.5f;
      float l2 = glm::dot(d, d);
      if (l2 >= r * r || l2 < 1e-8f) continue;
      float l = std::sqrt(l2);
      vec3 n = d / l;
      float pen = r - l;
      float wa = b.mass / (a.mass + b.mass), wb = 1 - wa;
      if (!a.sleeping) a.pos -= n * pen * wa;
      if (!b.sleeping) b.pos += n * pen * wb;
      float rv = glm::dot(b.v - a.v, n);
      if (rv < 0) {
        a.v += n * rv * 0.5f * wa;
        b.v -= n * rv * 0.5f * wb;
      }
      if (pen > 0.02f) {
        a.sleeping = b.sleeping = false;
        a.still = b.still = 0;
      }
    }
  chunks.erase(std::remove_if(chunks.begin(), chunks.end(), [](const Chunk& c) { return c.dissolve >= 1.0f; }),
               chunks.end());
}

float World::beginFrame(float dtReal) {
  if (slowTimer > 0) slowTimer -= dtReal;
  else slowTarget = 1;
  timeScale += (slowTarget - timeScale) * (1.0f - std::exp(-dtReal * 14.0f));
  float dt = dtReal * timeScale;
  lastDt = dt;
  time += dt;
  trauma = std::max(0.0f, trauma - dtReal * 1.5f);
  flash *= std::exp(-dtReal * 7.0f);
  frameLights.clear();
  return dt;
}

void World::stopFidgets() {
  for (Actor& a : actors) {
    if (a.fidgetT < 0) continue;
    if (a.set != 0) {
      a.ctl.headYaw = a.ctl.headPitch = a.ctl.lean = a.ctl.twist = 0;
      a.ctl.armL = a.ctl.armR = vec3(0);
      a.ctl.perk = a.ctl.jaw = a.ctl.tongue = a.ctl.hover = a.ctl.crouch = a.ctl.hatch = 0;
    }
    a.fidgetT = -1;
    a.yawTarget = a.baseYaw;
    a.poseSquash = 1;
    a.poseBend = vec2(0);
    if (a.fidgetType == 1) a.pos.y = 0;
  }
}

// Idle life: now and then a piece glances around, hops on the spot or stretches.
void World::updateFidgets(float dt) {
  if (!fidgetAllowed) {
    stopFidgets();
    return;
  }
  fidgetTimer -= dt;
  if (fidgetTimer <= 0 && !actors.empty()) {
    fidgetTimer = fx.rnd(0.7f, 2.0f);
    int tries = 6;
    while (tries--) {
      Actor& a = actors[(size_t)(fx.rnd() * actors.size()) % actors.size()];
      if (!a.alive || !a.visible || a.fidgetT >= 0 || a.id == excludeFidget || a.dissolve > 0.01f) continue;
      a.fidgetT = 0;
      float r = fx.rnd();
      a.fidgetType = r < 0.5f ? 0 : (r < 0.78f ? 1 : 2);
      a.fidgetDir = fx.rnd() < 0.5f ? -1.0f : 1.0f;
      break;
    }
  }
  for (Actor& a : actors) {
    if (a.fidgetT < 0 || !a.alive) continue;
    a.fidgetT += dt;
    float t = a.fidgetT, end = 1.0f;
    if (a.set == 1) {
      // medieval figures don't hop: they look about, shift their weight, toss their heads
      medieval::Controls& c = a.ctl;
      switch (a.fidgetType) {
        case 0: {  // a glance to one side
          end = 2.2f;
          float u = std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI);
          if (a.kind == meshgen::K_KNIGHT) {
            c.headYaw = a.fidgetDir * 0.5f * u;
            c.neck = -0.2f * u;
          } else if (a.kind == meshgen::K_ROOK) {
            c.idle += dt * 3.0f;  // the banner catches a gust
          } else {
            c.headYaw = a.fidgetDir * 0.6f * u;
            c.twist = a.fidgetDir * 0.12f * u;
          }
          break;
        }
        case 1: {  // horse: toss of the head and a pawing hoof; others: shift weight, adjust grip
          end = 1.3f;
          float u = glm::clamp(t / end, 0.0f, 1.0f);
          if (a.kind == meshgen::K_KNIGHT) {
            c.headPitch = -0.35f * std::sin(u * kPI * 2.0f) * (1.0f - u);
            c.tail = u * 6.0f;
          } else if (a.kind == meshgen::K_PAWN) {
            c.armR = vec3(0.25f * std::sin(u * kPI), 0, 0);
            c.lean = -0.04f * std::sin(u * kPI);
          } else {
            c.lean = 0.05f * std::sin(u * kPI);
            c.headPitch = 0.12f * std::sin(u * kPI);
          }
          break;
        }
        default: {  // stretch, look up
          end = 1.6f;
          float u = std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI);
          c.lean = -0.06f * u;
          c.headPitch = -0.25f * u;
          if (a.kind == meshgen::K_KNIGHT) c.neck = 0.3f * u;
          break;
        }
      }
      if (t >= end) {
        a.fidgetT = -1;
        c.headYaw = c.headPitch = c.lean = c.twist = c.neck = 0;
        c.armR = vec3(0);
      }
      continue;
    }
    if (a.set == 2) {
      // aliens: antennae twitch, a look round with a blink of the lights, a nervous hop
      medieval::Controls& c = a.ctl;
      switch (a.fidgetType) {
        case 0: {  // look about, antennae swivel
          end = 2.0f;
          float u = std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI);
          c.headYaw = a.fidgetDir * 0.7f * u;
          c.perk = 0.6f * u;
          if (a.kind == meshgen::K_KNIGHT) c.spin += dt * 6.0f * u;  // the light ring whirls
          if (a.kind == meshgen::K_ROOK) c.hatch = 0.5f * u;        // the eye peers about
          break;
        }
        case 1: {  // a quick hop (the saucer bobs, the tripod shifts its weight)
          end = 0.7f;
          float u = glm::clamp(t / 0.4f, 0.0f, 1.0f);
          if (a.kind == meshgen::K_ROOK) {
            c.crouch = 0.3f * std::sin(u * kPI);
          } else if (a.kind == meshgen::K_KNIGHT || a.kind == meshgen::K_BISHOP || a.kind == meshgen::K_KING) {
            c.hover = 0.06f * std::sin(u * kPI);
          } else {
            a.pos.y = 0.08f * std::sin(u * kPI);
            a.poseSquash = t < 0.07f ? 0.85f : 1.0f;
            c.perk = -0.8f * std::sin(u * kPI);
          }
          break;
        }
        default: {  // stretch, a big blink of the eyes / a scratch of the head
          end = 1.5f;
          float u = std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI);
          c.lean = -0.08f * u;
          c.headPitch = -0.3f * u;
          c.armL = vec3(0.6f * u, 0.2f * u, 0.4f * u);
          if (a.kind == meshgen::K_PAWN) c.jaw = 0.5f * u;  // a yawn
          break;
        }
      }
      if (t >= end) {
        a.fidgetT = -1;
        c.headYaw = c.headPitch = c.lean = c.twist = c.perk = c.jaw = c.hover = c.crouch = c.hatch = 0;
        c.armL = c.armR = vec3(0);
        a.poseSquash = 1;
        if (a.fidgetType == 1) a.pos.y = 0;
      }
      continue;
    }
    switch (a.fidgetType) {
      case 0: {  // glance to one side and back
        end = 1.8f;
        float u = glm::clamp(t / end, 0.0f, 1.0f);
        float k = a.kind == meshgen::K_KNIGHT ? 0.75f : 0.45f;
        a.yawTarget = a.baseYaw + a.fidgetDir * k * std::sin(u * kPI);
        a.poseBend = vec2(a.fidgetDir * 0.05f * std::sin(u * kPI), 0);
        break;
      }
      case 1: {  // a little hop
        end = 0.6f;
        float u = glm::clamp(t / 0.34f, 0.0f, 1.0f);
        a.pos.y = 0.065f * std::sin(u * kPI);
        a.poseSquash = t < 0.06f ? 0.9f : 1.0f;
        break;
      }
      default: {  // stretch
        end = 1.4f;
        float u = glm::clamp(t / end, 0.0f, 1.0f);
        a.poseSquash = 1.0f + 0.06f * std::sin(u * kPI);
        a.poseBend = vec2(0, -0.06f * std::sin(u * kPI));
        a.poseTwist = 0.15f * a.fidgetDir * std::sin(u * kPI * 2.0f);
        break;
      }
    }
    if (t >= end) {
      a.fidgetT = -1;
      a.yawTarget = a.baseYaw;
      a.poseSquash = 1;
      a.poseBend = vec2(0);
      a.poseTwist = 0;
      if (a.fidgetType == 1) a.pos.y = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Ghosts: the captured pieces float in a row at their army's side of the
// board (White's along the a-file, Black's along the h-file, each starting
// from its own end), watching the game and cheering their army on.
// ---------------------------------------------------------------------------
namespace {
const float kGhostScale = 0.7f;
const vec3 kGhostLight(0.9f, 0.93f, 1.0f);  // the pale light of their wisps and sparkles
}  // namespace

vec3 World::ghostSpot(int color, int slot) {
  float side = color == 0 ? -1.0f : 1.0f;
  float row = (float)(slot / 15), i = (float)(slot % 15);
  return vec3(side * (5.1f + 0.6f * row), 0.12f, -side * (3.64f - 0.52f * i));
}

void World::addGhost(int kind, int color, float delay, bool instant) {
  Ghost g;
  for (const Ghost& o : ghosts)
    if (o.body.color == color) ++g.slot;
  Actor& a = g.body;
  a.kind = kind;
  a.color = color;
  a.set = pieceSet;
  a.pos = ghostSpot(color, g.slot);
  a.baseYaw = color == 0 ? 0.5f * kPI : -0.5f * kPI;  // facing the board
  a.yaw = a.yawTarget = a.baseYaw;
  a.scale = kGhostScale;
  a.phase = fx.rnd(0, 100);
  a.ctl.idle = fx.rnd(0, 100);
  a.resetControls();
  a.texOffset = vec3(fx.rnd(-20, 20), fx.rnd(-20, 20), fx.rnd(-20, 20));
  g.delay = delay;
  g.appear = instant ? 1.0f : 0.0f;
  g.nextWave = fx.rnd(4.0f, 10.0f);
  ghosts.push_back(g);
}

void World::cheerGhosts(int color, float power, float seconds, bool aloud) {
  int n = 0;
  vec3 at(0);
  for (Ghost& g : ghosts) {
    if (g.body.color != color) continue;
    g.cheer = std::max(g.cheer, seconds * fx.rnd(0.8f, 1.1f));  // they don't all stop at once
    g.cheerPower = power;
    g.droop = 0;
    if (g.appear > 0.3f) {
      ++n;
      at += g.body.pos;
    }
  }
  // a soft cheer from the sidelines (only when their side takes a piece)
  if (aloud && n > 0) sound(SFX_CHEER, std::min(0.08f + 0.02f * (float)n, 0.18f), at / (float)n, fx.rnd(0.95f, 1.08f));
}

void World::droopGhosts(int color, float seconds) {
  for (Ghost& g : ghosts) {
    if (g.body.color != color) continue;
    g.droop = std::max(g.droop, seconds);
    g.cheer = 0;
  }
}

void World::updateGhosts(float dt) {
  if (ghosts.empty()) return;
  // they watch whoever is on the move
  vec3 target = ghostFocus;
  float best = 0.25f;
  for (const Actor& a : actors) {
    if (!a.alive || !a.visible) continue;
    float v = glm::length(glm::vec2(a.vel.x, a.vel.z));
    if (v > best) {
      best = v;
      target = a.pos;
    }
  }
  ghostFocus += (target - ghostFocus) * (1.0f - std::exp(-dt * 2.5f));
  for (Ghost& g : ghosts) {
    Actor& a = g.body;
    if (a.set != pieceSet) {  // the armies were switched
      a.set = pieceSet;
      a.resetControls();
    }
    vec3 home = ghostSpot(a.color, g.slot);
    if (g.delay > 0) {  // it appears without a sound
      g.delay -= dt;
      continue;
    }
    g.t += dt;
    if (g.appear < 1.0f) {
      // it takes shape slowly, out of rising wisps
      g.appear = std::min(1.0f, g.appear + dt / 4.5f);
      if (fx.rnd() < dt * 22.0f * (1.0f - g.appear)) {
        Particle p;
        float px = fx.rnd(-0.2f, 0.2f), py = fx.rnd(0.0f, 0.55f), pz = fx.rnd(-0.2f, 0.2f);
        p.pos = home + vec3(px, py, pz);
        p.vel = vec3(0, fx.rnd(0.15f, 0.4f), 0);
        p.size = fx.rnd(0.01f, 0.024f);
        p.color = kGhostLight * fx.rnd(0.8f, 1.6f);
        p.colorEnd = kGhostLight * 0.2f;
        p.opacity = 0;
        p.maxLife = fx.rnd(0.8f, 1.6f);
        p.type = PT_GLOW;
        p.drag = 0.5f;
        p.fadeIn = 0.3f;
        fx.add(p);
      }
    }
    // cheering, moping and the odd "come on!", eased in and out
    g.cheer = std::max(0.0f, g.cheer - dt);
    g.droop = std::max(0.0f, g.droop - dt);
    if (g.cheer <= 0 && g.droop <= 0 && g.appear >= 1.0f) {
      g.nextWave -= dt;
      if (g.nextWave <= 0) {
        g.wave = 1.6f;
        g.nextWave = fx.rnd(5.0f, 12.0f);
      }
    }
    g.wave = std::max(0.0f, g.wave - dt);
    float kf = 1.0f - std::exp(-dt * 6.0f), ks = 1.0f - std::exp(-dt * 3.0f);
    g.cheerS += ((g.cheer > 0 ? g.cheerPower : 0.0f) - g.cheerS) * kf;
    g.droopS += ((g.droop > 0 ? 1.0f : 0.0f) - g.droopS) * ks;
    g.waveS += ((g.wave > 0 ? 1.0f : 0.0f) - g.waveS) * kf;
    float cs = g.cheerS, ds = g.droopS, ws = g.waveS;
    float ph = a.phase;
    // float, bob, and jump for joy
    float rise = 1.0f - easeOut(g.appear);
    float bob = 0.035f * std::sin(g.t * 1.4f + ph);
    float hop = 0.16f * std::fabs(std::sin(g.t * 8.0f + ph)) * cs + 0.05f * std::fabs(std::sin(g.t * 5.0f + ph)) * ws;
    a.pos = home + vec3(0, bob - 0.4f * rise + hop - 0.06f * ds, 0);
    // turn to watch the action (but stay facing the board)
    float off = glm::clamp(wrapAngle(faceAngle(ghostFocus - home) - a.baseYaw), -1.2f, 1.2f);
    a.yawTarget = a.baseYaw + off;
    a.yawRate = 3.0f;
    // the marble rig: a wiggle when cheering, a bow when moping
    a.poseSquash = 1.0f + 0.1f * std::sin(g.t * 16.0f + ph) * cs;
    a.poseTwist = 0.35f * std::sin(g.t * 7.0f + ph) * cs;
    a.poseBend = glm::vec2(0.12f * std::sin(g.t * 4.0f + ph) * (cs + ws), 0.25f * ds);
    // the figures: arms up, mouths open, antennae up - or heads down
    medieval::Controls& k = a.ctl;
    float wv = std::sin(g.t * 11.0f + ph);
    k.armL = vec3(2.7f - 0.25f * wv, 0.35f, 0.3f) * cs;
    k.armR = vec3(2.7f + 0.25f * wv, 0.35f, 0.3f) * cs + vec3(2.5f, 0.4f + 0.3f * std::sin(g.t * 9.0f), 0.4f) * ws * (1.0f - cs);
    k.headPitch = -0.25f * cs + 0.45f * ds;
    k.lean = -0.1f * cs + 0.25f * ds;
    k.jaw = 0.7f * cs;
    k.perk = cs - ds;
    if (a.set == 1 && a.kind == meshgen::K_KNIGHT) k.rear = 0.5f * cs;  // the horse rears
    if (a.set == 1 && a.kind == meshgen::K_ROOK) k.bridge = 0.35f * std::fabs(std::sin(g.t * 6.0f + ph)) * cs;
    if (a.set == 2 && a.kind == meshgen::K_KNIGHT) k.spin += dt * (2.0f + 14.0f * cs);
    if (a.set == 2 && a.kind == meshgen::K_ROOK) k.crouch = 0.3f * std::fabs(std::sin(g.t * 8.0f + ph)) * cs;
    // a faint pale glow, brighter when cheering, and it materialises out of the wisps
    a.glow = glm::vec4(kGhostLight, 0.1f + 0.45f * cs + 0.15f * ws);
    a.dissolve = (1.0f - g.appear) * 0.85f;
    a.dissolveColor = kGhostLight * 1.5f;
    if (cs > 0.3f && fx.rnd() < dt * 5.0f * cs) {
      Particle p;
      float px = fx.rnd(-0.12f, 0.12f), pz = fx.rnd(-0.12f, 0.12f);
      p.pos = a.pos + vec3(px, a.height() * 0.9f, pz);
      p.vel = vec3(0, fx.rnd(0.3f, 0.7f), 0);
      p.size = fx.rnd(0.008f, 0.018f);
      p.color = kGhostLight * 2.0f;
      p.colorEnd = kGhostLight * 0.3f;
      p.opacity = 0;
      p.maxLife = fx.rnd(0.6f, 1.1f);
      p.type = PT_GLOW;
      p.drag = 0.8f;
      p.fadeIn = 0.1f;
      fx.add(p);
    }
    a.update(dt, time, audio);
  }
}

void World::simulate(float dt) {
  updateFidgets(dt);
  // dust motes drifting through the pool of light
  if (motes) {
    moteTimer -= dt;
    while (moteTimer < 0) {
      moteTimer += 0.09f;
      Particle p;
      p.pos = vec3(fx.rnd(-5.5f, 5.5f), fx.rnd(0.15f, 3.2f), fx.rnd(-5.5f, 5.5f));
      p.vel = fx.rndDir() * 0.025f + vec3(0, 0.006f, 0);
      p.size = fx.rnd(0.0025f, 0.006f);
      p.color = vec3(1.0f, 0.9f, 0.76f) * fx.rnd(0.25f, 0.8f);
      p.colorEnd = p.color;
      p.opacity = 0;
      p.maxLife = fx.rnd(7.0f, 13.0f);
      p.type = PT_GLOW;
      p.drag = 0.0f;
      p.fadeIn = 2.0f;
      fx.add(p);
    }
  }
  for (Actor& a : actors)
    if (a.alive) a.update(dt, time, audio);
  // the fallen come back as ghosts; their army mourns, the other cheers
  for (Actor& a : actors) {
    if (!a.ghostOnDeath || a.alive) continue;
    a.ghostOnDeath = false;
    addGhost(a.kind, a.color, 1.4f);
    cheerGhosts(a.color ^ 1, 1.0f, 2.6f, true);
    droopGhosts(a.color, 3.0f);
  }
  updateGhosts(dt);
  for (Puppet& p : puppets) {
    p.ctl.idle += dt;
    figurePose(p.set, p.asset, p.ctl, p.pose);
  }
  puppets.erase(std::remove_if(puppets.begin(), puppets.end(), [](const Puppet& p) { return !p.alive; }),
                puppets.end());
  fx.update(dt);
  updateChunks(dt);
  for (TimedLight& l : lights) l.life -= dt;
  for (GroundRing& r : rings) r.life -= dt;
  rings.erase(std::remove_if(rings.begin(), rings.end(), [](const GroundRing& r) { return r.life <= 0; }), rings.end());
  lights.erase(std::remove_if(lights.begin(), lights.end(), [](const TimedLight& l) { return l.life <= 0; }),
               lights.end());
  props.erase(std::remove_if(props.begin(), props.end(), [](const Prop& p) { return !p.alive; }), props.end());
}

// The draw items of one figure (a chessman or a ghost).
void World::figureItems(Renderer& r, const Actor& a, std::vector<DrawItem>& out) const {
  if (a.set != 0) {
    const Renderer::PieceGpu* G = r.figures(a.set);
    if (!G) return;
    DrawItem d;
    d.mesh = &G[a.kind].lod0;
    d.meshLow = &G[a.kind].lod1;
    d.model = a.model();
    if (a.set == 1) {
      // painted figures are not made of rubber: only a hint of the squash-and-stretch rig
      d.rig = glm::vec4(a.rig.x * 0.3f, a.rig.y, 1.0f + (a.rig.z - 1.0f) * 0.12f, a.rig.w * 0.25f);
    } else {
      // aliens are a bit rubbery (the saucer and the tripod less so)
      float k = (a.kind == meshgen::K_KNIGHT || a.kind == meshgen::K_ROOK) ? 0.4f : 0.8f;
      d.rig = glm::vec4(a.rig.x * k, a.rig.y, 1.0f + (a.rig.z - 1.0f) * k, a.rig.w * k);
    }
    d.rigH = a.height() / std::max(a.scale, 1e-3f);
    d.material = a.set == 2 ? MAT_ALIEN : MAT_MEDIEVAL;
    d.team = a.color;
    d.bones = a.pose.bone;
    d.boneCount = a.pose.count;
    d.texOffset = a.texOffset;
    d.glow = a.glow;
    d.crack = a.crack;
    d.crackColor = a.crackColor;
    d.dissolve = a.dissolve;
    d.dissolveColor = a.dissolveColor;
    d.heat = a.heat;
    d.stone = a.stone;
    out.push_back(d);
    int ga = a.set == 2 ? alien::glassAsset(a.kind) : -1;
    if (ga >= 0) {  // the saucer's canopy, the psychic's jar
      DrawItem g = d;
      g.mesh = &G[ga].lod0;
      g.meshLow = &G[ga].lod1;
      g.material = MAT_GLASS;
      g.glass = true;
      g.castShadow = false;
      out.push_back(g);
    }
    if (a.set == 1 && a.kind == meshgen::K_ROOK && a.ctl.legs > 0.001f) {
      DrawItem l = d;
      l.mesh = &r.medieval[medieval::A_LEGS].lod0;
      l.meshLow = &r.medieval[medieval::A_LEGS].lod1;
      l.rig = glm::vec4(0, 0, 1, 0);
      out.push_back(l);
    }
    if (a.hasSeat()) {
      int sa = medieval::seatAsset(a.kind);
      DrawItem t = d;
      t.mesh = &r.medieval[sa].lod0;
      t.meshLow = &r.medieval[sa].lod1;
      t.model = seatModel(a);
      t.rig = a.seatFree ? glm::vec4(0, 0, 1, 0) : d.rig;
      t.bones = nullptr;
      t.boneCount = 0;
      t.texOffset = a.texOffset + vec3(3.1f);
      if (a.seatFree) t.glow = glm::vec4(0);
      out.push_back(t);
    }
    return;
  }
  DrawItem d;
  d.mesh = &r.pieces[a.kind].lod0;
  d.meshLow = &r.pieces[a.kind].lod1;
  d.model = a.model();
  d.rig = a.rig;
  d.rigH = meshgen::pieceHeight(a.kind);
  d.material = a.color == 0 ? MAT_WHITE : MAT_BLACK;
  d.texOffset = a.texOffset;
  d.glow = a.glow;
  d.crack = a.crack;
  d.crackColor = a.crackColor;
  d.dissolve = a.dissolve;
  d.dissolveColor = a.dissolveColor;
  out.push_back(d);
}

void World::buildDrawItems(Renderer& r, std::vector<DrawItem>& out) const {
  for (const Actor& a : actors) {
    if (!a.alive || !a.visible || a.dissolve >= 0.999f) continue;
    figureItems(r, a, out);
  }
  for (size_t i = 0; i < ghosts.size(); ++i) {
    const Ghost& g = ghosts[i];
    if (g.appear <= 0.0f) continue;
    size_t first = out.size();
    figureItems(r, g.body, out);
    float alpha = std::max(0.85f * easeInOut(g.appear), 1e-3f);
    for (size_t k = first; k < out.size(); ++k) {
      DrawItem& d = out[k];
      d.ghost = alpha;
      d.ghostId = (int)i;
      d.castShadow = false;
      d.reflect = false;
      d.groundAO = false;
      d.team = g.body.color;
    }
    // no glass on a ghost: its canopy or jar would hide what is inside
    out.erase(std::remove_if(out.begin() + (long)first, out.end(), [](const DrawItem& d) { return d.glass; }),
              out.end());
  }
  for (const Chunk& c : chunks) {
    const Renderer::PieceGpu* G = c.set != 0 ? r.figures(c.set) : r.pieces;
    if (!G) continue;
    const auto& v = c.half ? G[c.kind].halves : G[c.kind].shards;
    if (c.index >= (int)v.size()) continue;
    DrawItem d;
    d.mesh = &v[c.index];
    d.meshLow = &v[c.index];
    d.model = glm::translate(glm::mat4(1.0f), c.pos) * glm::mat4_cast(c.rot) * glm::scale(glm::mat4(1.0f), vec3(c.scale));
    d.material = c.set == 1 ? MAT_MEDIEVAL : (c.set == 2 ? MAT_ALIEN : (c.color == 0 ? MAT_WHITE : MAT_BLACK));
    if (c.set == 2 && (c.kind == alien::AL_CANOPY || c.kind == alien::AL_JAR)) {
      d.material = MAT_GLASS;
      d.glass = true;
      d.castShadow = false;
    }
    d.team = c.color;
    d.texOffset = c.texOffset;
    d.glow = vec4(c.glowColor, c.glow * 1.2f);
    d.dissolve = c.dissolve;
    d.dissolveColor = c.color == 0 ? vec3(0.9f, 0.85f, 0.75f) : vec3(0.6f, 0.5f, 0.4f);
    out.push_back(d);
  }
  for (const Puppet& p : puppets) {
    const Renderer::PieceGpu* G = r.figures(p.set);
    if (!G || p.dissolve >= 0.999f) continue;
    DrawItem d;
    d.mesh = &G[p.asset].lod0;
    d.meshLow = &G[p.asset].lod1;
    d.model = p.model();
    d.rigH = 1.0f;
    d.material = p.set == 2 ? MAT_ALIEN : MAT_MEDIEVAL;
    d.team = p.color;
    d.bones = p.pose.bone;
    d.boneCount = p.pose.count;
    d.texOffset = p.texOffset;
    d.glow = p.glow;
    d.dissolve = p.dissolve;
    d.dissolveColor = vec3(1.0f, 0.5f, 0.15f);
    out.push_back(d);
  }
  for (const Prop& p : props) {
    DrawItem d;
    d.model = glm::translate(glm::mat4(1.0f), p.pos) * glm::mat4_cast(p.rot) * glm::scale(glm::mat4(1.0f), vec3(p.scale));
    d.dissolve = p.dissolve;
    d.dissolveColor = vec3(1.0f, 0.75f, 0.3f);
    d.glow = p.glow;
    d.groundAO = false;
    if (p.type == Prop::SWORD) {
      d.mesh = d.meshLow = &r.sword;
      d.material = MAT_SWORD;
      d.rigH = 1.0f;
    } else {
      d.mesh = d.meshLow = &r.cannonball;
      d.material = MAT_IRON;
      d.heat = p.heat;
    }
    out.push_back(d);
  }
}

void World::collectLights(std::vector<PointLight>& out) const {
  for (const PointLight& l : frameLights) out.push_back(l);
  for (const TimedLight& l : lights) {
    float f = l.life / l.maxLife;
    out.push_back({l.pos, l.radius, l.color * f * f});
  }
  // brightest first, renderer keeps 8
  std::sort(out.begin(), out.end(), [](const PointLight& a, const PointLight& b) {
    return glm::dot(a.color, vec3(1)) > glm::dot(b.color, vec3(1));
  });
  if (out.size() > 8) out.resize(8);
}

void World::collectRings(std::vector<vec4>& r, std::vector<vec4>& c) const {
  for (const GroundRing& g : rings) {
    if (r.size() >= 6) break;
    float age = g.maxLife - g.life;
    float f = g.life / g.maxLife;
    r.push_back(vec4(g.pos.x, g.pos.z, 0.15f + age * g.speed, f * f));
    c.push_back(vec4(g.color, g.width * (1.0f + age * 1.5f)));
  }
}

void World::collectOccluders(std::vector<vec4>& out) const {
  for (const Actor& a : actors) {
    if (!a.alive || !a.visible) continue;
    float s = std::exp(-std::max(0.0f, a.pos.y + a.bob) / 0.12f) * (1.0f - a.dissolve);
    out.push_back(vec4(a.pos.x, a.pos.z, a.radius() * 1.05f, s));
    if (a.hasSeat() && a.seatFree) out.push_back(vec4(a.seatPos.x, a.seatPos.z, 0.3f, 1.0f - a.dissolve));
  }
  for (const Chunk& c : chunks) {
    if (out.size() >= 32) break;
    if (c.pos.y < -0.1f) continue;  // fallen off the board
    float s = std::exp(-std::max(0.0f, c.pos.y - c.radius) / 0.1f) * 0.6f * (1.0f - c.dissolve);
    out.push_back(vec4(c.pos.x, c.pos.z, c.radius * 0.9f, s));
  }
  if (out.size() > 32) out.resize(32);
}
