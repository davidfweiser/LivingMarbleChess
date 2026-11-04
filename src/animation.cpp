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
  else if (set == 3) roman::buildPose(asset, c, out);
  else if (set == 4) dino::buildPose(asset, c, out);
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
  float h = set == 1   ? medieval::pieceHeight(kind)
            : set == 2 ? alien::pieceHeight(kind)
            : set == 3 ? roman::pieceHeight(kind)
            : set == 4 ? dino::pieceHeight(kind)
                       : meshgen::pieceHeight(kind);
  return h * scale;
}
float Actor::radius() const {
  float r = set == 1   ? medieval::pieceRadius(kind)
            : set == 2 ? alien::pieceRadius(kind)
            : set == 3 ? roman::pieceRadius(kind)
            : set == 4 ? dino::pieceRadius(kind)
                       : meshgen::pieceRadius(kind);
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
  glm::mat4 M = glm::translate(glm::mat4(1.0f), pos + vec3(0, set == 1 || set >= 3 ? bob * 0.3f : bob, 0));
  M = glm::rotate(M, yaw, vec3(0, 1, 0));
  return glm::scale(M, vec3(scale));
}

glm::mat4 Actor::model() const {
  glm::mat4 M = glm::translate(glm::mat4(1.0f), pos + vec3(0, set == 1 || set >= 3 ? bob * 0.3f : bob, 0));
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
  if (set == 3) return rlib->asset[asset];
  if (set == 4) return dlib->asset[asset];
  return set == 2 ? alib->asset[asset] : mlib->asset[asset];
}

const meshgen::PieceAssets& World::assetsOf(const Chunk& c) const {
  return c.set != 0 ? figureAssets(c.set, c.kind) : lib->piece[c.kind];
}

void World::reset() {
  actors.clear();
  puppets.clear();
  solids.clear();
  grips.clear();
  stands.clear();
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

int World::strikeDownCaptured() {
  int n = 0;
  for (Actor& a : actors) {
    if (!a.alive || !a.visible || a.square < 0 || actorAt[a.square] == a.id) continue;
    shatter(a, a.pos + vec3(0, a.height() * 0.5f, 0), vec3(0, 1.0f, 0), 1.0f, 0.6f, 4.0f);
    ++n;
  }
  return n;
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
      if (a.set == 4) a.ctl.neck = a.ctl.itemYaw = a.ctl.wings = 0;
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
      if (a.crowded) continue;  // (a look round, a swish of the tail would hit its neighbour)
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
    if (a.set == 4) {
      // dinosaurs: a look round, a sniff of the air, a yawn and a stretch, a swish of the tail
      medieval::Controls& c = a.ctl;
      switch (a.fidgetType) {
        case 0: {  // a look round
          end = 2.4f;
          float u = std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI);
          c.headYaw = a.fidgetDir * (a.kind == meshgen::K_ROOK ? 0.9f : 0.65f) * u;
          c.twist = a.fidgetDir * 0.08f * u;
          c.itemYaw = -a.fidgetDir * 0.15f * u;
          break;
        }
        case 1: {  // sniffing the air, a snort; the pteranodon shakes out its wings
          end = 1.5f;
          float u = glm::clamp(t / end, 0.0f, 1.0f);
          if (a.kind == meshgen::K_KNIGHT) {
            c.wings = 0.45f * std::sin(u * kPI);
            c.flap = u * 18.0f;
          } else {
            c.neck = 0.25f * std::sin(u * kPI);
            c.headPitch = -0.2f * std::sin(u * kPI) + 0.05f * std::sin(u * kPI * 8.0f) * (1.0f - u);
          }
          break;
        }
        default: {  // a yawn, a stretch, a swish of the tail
          end = 1.8f;
          float u = std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI);
          c.jaw = 0.75f * u;
          c.neck = 0.3f * u;
          c.headPitch = -0.15f * u;
          c.itemYaw = a.fidgetDir * 0.35f * std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI * 2.0f);
          break;
        }
      }
      if (t >= end) {
        a.fidgetT = -1;
        c.headYaw = c.headPitch = c.lean = c.twist = c.neck = c.jaw = c.itemYaw = c.wings = 0;
      }
      continue;
    }
    if (a.set == 3) {
      // Romans: soldiers look about and shift their grip, horses toss their heads,
      // the siege tower sways and its banner catches the wind
      medieval::Controls& c = a.ctl;
      switch (a.fidgetType) {
        case 0: {  // a glance to one side
          end = 2.2f;
          float u = std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI);
          if (a.kind == meshgen::K_ROOK) {
            c.idle += dt * 3.0f;
          } else {
            c.headYaw = a.fidgetDir * 0.6f * u;
            c.twist = a.fidgetDir * 0.1f * u;
          }
          break;
        }
        case 1: {  // shift the weight, heft the spear; the horses toss their heads
          end = 1.3f;
          float u = glm::clamp(t / end, 0.0f, 1.0f);
          if (a.kind == meshgen::K_KNIGHT) {
            c.headPitch = -0.35f * std::sin(u * kPI * 2.0f) * (1.0f - u);
            c.tail = u * 6.0f;
          } else if (a.kind == meshgen::K_ROOK) {
            c.lean = 0.025f * std::sin(u * kPI * 3.0f) * (1.0f - u);
          } else {
            c.armR = vec3(0.22f * std::sin(u * kPI), 0, -0.15f * std::sin(u * kPI));
            c.lean = -0.04f * std::sin(u * kPI);
          }
          break;
        }
        default: {  // stretch, look up at the sky
          end = 1.6f;
          float u = std::sin(glm::clamp(t / end, 0.0f, 1.0f) * kPI);
          c.lean = -0.06f * u;
          c.headPitch = -0.25f * u;
          if (a.kind == meshgen::K_KNIGHT) c.headPitch = 0.3f * u;
          break;
        }
      }
      if (t >= end) {
        a.fidgetT = -1;
        c.headYaw = c.headPitch = c.lean = c.twist = 0;
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
    if (a.set == 3 && a.kind == meshgen::K_KNIGHT) k.rear = 0.45f * cs;  // the horses rear
    if (a.set == 3 && a.kind == meshgen::K_ROOK) k.bridge = 0.3f * std::fabs(std::sin(g.t * 6.0f + ph)) * cs;
    if (a.set == 3) k.cape = 0.5f * cs;
    if (a.set == 4) {  // roaring, the pteranodon flapping, the big ones rearing
      k.neck = 0.45f * cs - 0.3f * ds;
      k.jaw = 0.9f * cs;
      k.armL = k.armR = vec3(0.6f, 0.2f, 0.0f) * cs;
      k.itemYaw = 0.4f * std::sin(g.t * 7.0f + ph) * cs;
      if (a.kind == meshgen::K_KNIGHT) {
        k.wings = cs;
        k.flap += dt * 14.0f * cs;
      }
      if (a.kind == meshgen::K_ROOK || a.kind == meshgen::K_BISHOP) k.rear = (a.kind == meshgen::K_ROOK ? 0.35f : 0.6f) * cs;
    }
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

// ---------------------------------------------------------------------------
// Dinosaurs at rest on neighbouring squares: one whose head, tail or sail would reach into a
// neighbour turns a little aside (to its own right if that will do), shuffling round on the
// spot, and back again once there is room. Only an animal standing on its square that nothing
// else is turning (its target the way it faces there, as last set here) is turned.
// ---------------------------------------------------------------------------
void World::keepClear(float dt) {
  if (!dlib) return;
  auto level = [](const vec3& v) { return vec3(v.x, 0, v.z); };
  auto resting = [&](const Actor& a) {
    if (!a.alive || !a.visible || a.set != 4 || a.kind < 0 || a.kind > 5 || a.square < 0 || a.dissolve > 0.01f) return false;
    if (a.pos.y > 0.005f || std::fabs(a.tilt.w) < 0.9999f || glm::length(level(a.pos) - squareCenter(a.square)) > 0.02f)
      return false;
    if (a.pose.count <= 0 || dlib->bulk[a.kind].empty()) return false;
    for (const Solid& s : solids)
      if (!s.puppet && s.id == a.id) return false;  // (fighting)
    return std::fabs(wrapAngle(a.yawTarget - a.baseYaw - a.clearYaw)) < 1e-3f;  // (nothing else turning it)
  };
  // how far an animal's balls reach from its feet, along the board
  static float reach[6] = {-1, -1, -1, -1, -1, -1};
  for (int k = 0; k < 6; ++k)
    if (reach[k] < 0 && !dlib->bulk[k].empty()) {
      reach[k] = 0;
      for (const dino::Ball& b : dlib->bulk[k]) reach[k] = std::max(reach[k], glm::length(vec2(b.c.x, b.c.z)) + b.r);
      reach[k] += 0.1f;  // (as it moves)
    }
  if (stands.size() < actors.size()) stands.resize(actors.size());
  std::vector<int> rest;
  for (Actor& a : actors) {
    if (resting(a)) {
      rest.push_back(a.id);
    } else {
      if (a.set == 4) a.clearYaw = 0;
      a.crowded = false;
      stands[(size_t)a.id].when = -1;
    }
  }
  // measured again a few times a second (it breathes, its tail sways), or when it has moved
  auto measure = [&](int id) -> const Stand& {
    Actor& a = actors[(size_t)id];
    Stand& s = stands[(size_t)id];
    vec3 pos = a.pos + vec3(0, a.bob * 0.3f, 0);
    if (s.when >= 0 && time - s.when < 0.3f && glm::length(pos - s.pos) < 1e-3f) return s;
    const std::vector<dino::Ball>& bulk = dlib->bulk[a.kind];
    s.when = time;
    s.pos = pos;
    s.c.resize(bulk.size());
    s.r.resize(bulk.size());
    s.first.clear();
    s.run.clear();
    for (size_t i = 0; i < bulk.size(); ++i) {
      const dino::Ball& k = bulk[i];
      if (s.first.empty() || bulk[(size_t)s.first.back()].bone != k.bone || (int)i - s.first.back() >= dino::kBulkRun)
        s.first.push_back((int)i);
      s.c[i] = vec3(a.pose.bone[k.bone] * vec4(k.c, 1.0f)) * a.scale;
      s.r[i] = k.r * a.scale;
    }
    s.first.push_back((int)bulk.size());
    for (size_t g = 0; g + 1 < s.first.size(); ++g) {
      vec3 m(0);
      for (int i = s.first[g]; i < s.first[g + 1]; ++i) m += s.c[(size_t)i];
      m /= (float)(s.first[g + 1] - s.first[g]);
      float rad = 0;
      for (int i = s.first[g]; i < s.first[g + 1]; ++i) rad = std::max(rad, glm::length(s.c[(size_t)i] - m) + s.r[(size_t)i]);
      s.run.push_back(vec4(m, rad));
    }
    return s;
  };
  // how deep the two sink into each other, A turned to ya, B to yb: the most any ball of one
  // reaches into one of the other (< 0: the gap between them), looking no further than 'look'
  struct Turn {
    float c, s;
    vec3 operator()(const vec3& v) const { return vec3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c); }
  };
  std::vector<vec3> wa, ra, rb;
  auto depth = [&](const Stand& A, float ya, const Stand& B, float yb, float look) {
    Turn ta{std::cos(ya), std::sin(ya)}, tb{std::cos(yb), std::sin(yb)};
    ra.resize(A.run.size());
    rb.resize(B.run.size());
    for (size_t g = 0; g < A.run.size(); ++g) ra[g] = A.pos + ta(vec3(A.run[g]));
    for (size_t g = 0; g < B.run.size(); ++g) rb[g] = B.pos + tb(vec3(B.run[g]));
    float worst = -look;
    for (size_t ga = 0; ga < A.run.size(); ++ga) {
      bool walked = false;
      for (size_t gb = 0; gb < B.run.size(); ++gb) {
        float R = A.run[ga].w + B.run[gb].w + look;
        vec3 d = ra[ga] - rb[gb];
        if (glm::dot(d, d) > R * R) continue;
        if (!walked) {  // (A's balls in this run, turned and placed)
          walked = true;
          wa.clear();
          for (int i = A.first[ga]; i < A.first[ga + 1]; ++i) wa.push_back(A.pos + ta(A.c[(size_t)i]));
        }
        for (int j = B.first[gb]; j < B.first[gb + 1]; ++j) {
          vec3 pb = B.pos + tb(B.c[(size_t)j]);
          for (int i = A.first[ga]; i < A.first[ga + 1]; ++i) {
            float r = A.r[(size_t)i] + B.r[(size_t)j];
            vec3 e = wa[(size_t)(i - A.first[ga])] - pb;
            float d2 = glm::dot(e, e);
            if (d2 < (r + look) * (r + look)) worst = std::max(worst, r - std::sqrt(d2));
          }
        }
      }
    }
    return worst;
  };
  // a couple of them each frame, in turn: each one turned as little as will keep it clear of its
  // neighbours (as they are turned now), its own right first
  // (the balls lie a little inside the skin: a couple of centimetres of air is kept between them)
  const float gap = 0.02f, room = 0.035f, elbow = 0.06f, step = 0.15f;
  int n = (int)rest.size();
  for (int k = 0; k < std::min(n, 2); ++k) {
    Actor& a = actors[(size_t)rest[(size_t)((clearNext + k) % n)]];
    std::vector<int> near;
    for (int id : rest) {
      const Actor& b = actors[(size_t)id];
      if (id != a.id && glm::length(level(a.pos - b.pos)) < reach[a.kind] + reach[b.kind]) near.push_back(id);
    }
    if (near.empty()) {
      a.clearYaw = 0;
      a.crowded = false;
      continue;
    }
    for (int id : near) measure(id);
    const Stand& A = measure(a.id);
    auto worst = [&](float off, float look) {
      float w = -look;
      for (int id : near)
        w = std::max(w, depth(A, a.baseYaw + off, stands[(size_t)id], actors[(size_t)id].baseYaw + actors[(size_t)id].clearYaw, look));
      return w;
    };
    float now = worst(a.clearYaw, elbow);
    if (now + gap <= 0.0f) {
      // clear: back towards facing straight, if that leaves room to spare
      if (a.clearYaw != 0.0f) {
        float back = a.clearYaw - (a.clearYaw > 0 ? 1.0f : -1.0f) * std::min(std::fabs(a.clearYaw), step);
        if (worst(back, room) + room <= 0.0f) a.clearYaw = back;
      }
    } else {
      float best = a.clearYaw, bestW = 1e9f;
      for (int i = 0; i <= 10; ++i) {
        float off = (i & 1 ? -1.0f : 1.0f) * step * (float)((i + 1) / 2);  // 0, -0.15, +0.15, -0.3 ...
        float w = worst(off, gap);
        if (w < bestW - 1e-4f) {
          bestW = w;
          best = off;
        }
        if (w + gap <= 0.0f) break;
      }
      a.clearYaw = best;
      now = worst(best, elbow);
    }
    a.crowded = now + elbow > 0.0f;
  }
  if (n > 0) clearNext = (clearNext + 2) % n;
  // turning to it: slowly, shuffling round on the spot
  for (int id : rest) {
    Actor& a = actors[(size_t)id];
    a.yawTarget = a.baseYaw + a.clearYaw;
    a.yawRate = 3.0f;
    if (std::fabs(wrapAngle(a.yawTarget - a.yaw)) > 0.03f) {
      a.ctl.gait += dt * 4.0f;
      a.ctl.stride = std::min(0.25f, a.ctl.stride + dt * 2.0f);
    } else if (a.ctl.stride > 0.0f) {
      a.ctl.stride = std::max(0.0f, a.ctl.stride - dt * 2.0f);
    }
  }
}

// ---------------------------------------------------------------------------
// Animals that must not pass through each other (the dinosaurs' fights). Each is its bulk
// (balls packed inside it, riding on its bones); wherever two of them overlap they are
// shoved apart, the lighter one the more: along the board while both stand on it, any way
// at all once one of them is off the ground (leaping, flying, held in the jaws, thrown).
// ---------------------------------------------------------------------------
void World::keepApart() {
  if (solids.empty()) {
    grips.clear();
    return;
  }
  struct Group {  // a run of balls on one bone, lying close together, and their bounds
    int bone, first, end;
    vec3 lo, hi;
  };
  struct Body {
    int actor = -1;  // (-1: a puppet)
    vec3* pos = nullptr;
    float mass = 1;
    bool free = false;  // off the ground: it may be shoved up or down too
    std::vector<vec3> c;
    std::vector<float> r;
    std::vector<Group> group;
    vec3 lo{1e9f}, hi{-1e9f};  // the bounds of all of them
    vec3 off{0};               // how far it has been shoved
  };
  std::vector<Body> bodies;
  std::vector<const void*> seen;
  auto add = [&](const void* who, int actor, int kind, const glm::mat4& M, const medieval::Pose& pose, float scale,
                 vec3* pos) {
    if (!dlib || kind < 0 || kind >= 6 || pose.count <= 0) return;
    if (std::find(seen.begin(), seen.end(), who) != seen.end()) return;
    seen.push_back(who);
    const std::vector<dino::Ball>& bulk = dlib->bulk[kind];
    if (bulk.empty()) return;
    Body b;
    b.actor = actor;
    b.pos = pos;
    b.mass = dino::bodyMass(kind);
    b.free = pos->y > 0.01f;
    b.c.resize(bulk.size());
    b.r.resize(bulk.size());
    for (size_t i = 0; i < bulk.size(); ++i) {
      const dino::Ball& k = bulk[i];
      if (b.group.empty() || b.group.back().bone != k.bone || (int)i - b.group.back().first >= dino::kBulkRun) {
        if (!b.group.empty()) b.group.back().end = (int)i;
        b.group.push_back({k.bone, (int)i, (int)i, vec3(1e9f), vec3(-1e9f)});
      }
      vec3 c = vec3(M * (pose.bone[k.bone] * vec4(k.c, 1.0f)));
      float r = k.r * scale;
      b.c[i] = c;
      b.r[i] = r;
      Group& g = b.group.back();
      g.lo = glm::min(g.lo, c - vec3(r));
      g.hi = glm::max(g.hi, c + vec3(r));
    }
    b.group.back().end = (int)bulk.size();
    for (const Group& g : b.group) {
      b.lo = glm::min(b.lo, g.lo);
      b.hi = glm::max(b.hi, g.hi);
    }
    bodies.push_back(std::move(b));
  };
  for (const Solid& s : solids) {
    if (s.puppet) {
      Puppet* p = puppet(s.id);
      if (p && p->alive && p->set == 4 && p->dissolve < 0.5f) add(p, -1, p->asset, p->model(), p->pose, p->scale, &p->pos);
    } else if (s.id >= 0 && s.id < (int)actors.size()) {
      Actor& a = actors[s.id];
      if (a.alive && a.visible && a.set == 4 && a.dissolve < 0.5f) add(&a, s.id, a.kind, a.model(), a.pose, a.scale, &a.pos);
    }
  }
  solids.clear();
  auto apart = [](const vec3& alo, const vec3& ahi, const vec3& blo, const vec3& bhi) {
    return ahi.x < blo.x || ahi.y < blo.y || ahi.z < blo.z || bhi.x < alo.x || bhi.y < alo.y || bhi.z < alo.z;
  };
  for (int sweep = 0; sweep < 2; ++sweep)
    for (size_t i = 0; i < bodies.size(); ++i)
      for (size_t j = i + 1; j < bodies.size(); ++j) {
        Body& A = bodies[i];
        Body& B = bodies[j];
        // one holding the other in its jaws: those bones of the holder are left out
        int skipA[2] = {-1, -1}, skipB[2] = {-1, -1};
        for (const Grip& g : grips) {
          if (A.actor >= 0 && g.holder == A.actor && g.held == B.actor) skipA[0] = g.bone1, skipA[1] = g.bone2;
          if (B.actor >= 0 && g.holder == B.actor && g.held == A.actor) skipB[0] = g.bone1, skipB[1] = g.bone2;
        }
        for (int iter = 0; iter < 8; ++iter) {
          if (apart(A.lo + A.off, A.hi + A.off, B.lo + B.off, B.hi + B.off)) break;
          // the deepest overlap of a ball of one with a ball of the other
          float best = 0.0015f;
          vec3 delta(0);
          float reach = 0;
          for (const Group& ga : A.group) {
            if (ga.bone == skipA[0] || ga.bone == skipA[1]) continue;
            vec3 alo = ga.lo + A.off, ahi = ga.hi + A.off;
            if (apart(alo, ahi, B.lo + B.off, B.hi + B.off)) continue;
            for (const Group& gb : B.group) {
              if (gb.bone == skipB[0] || gb.bone == skipB[1]) continue;
              vec3 blo = gb.lo + B.off, bhi = gb.hi + B.off;
              if (apart(alo, ahi, blo, bhi)) continue;
              for (int a = ga.first; a < ga.end; ++a) {
                vec3 ca = A.c[a] + A.off;
                float ra = A.r[a];
                if (ca.x + ra < blo.x || ca.y + ra < blo.y || ca.z + ra < blo.z || ca.x - ra > bhi.x ||
                    ca.y - ra > bhi.y || ca.z - ra > bhi.z)
                  continue;
                for (int b = gb.first; b < gb.end; ++b) {
                  vec3 d = ca - (B.c[b] + B.off);
                  float R = ra + B.r[b];
                  float d2 = glm::dot(d, d);
                  if (d2 >= R * R) continue;
                  float pen = R - std::sqrt(d2);
                  if (pen > best) {
                    best = pen;
                    delta = d;
                    reach = R;
                  }
                }
              }
            }
          }
          if (reach <= 0) break;
          float wa = B.mass / (A.mass + B.mass);  // A's share of the shove
          vec3 ma(0), mb(0);
          if (!A.free && !B.free) {
            // both stand on the board: apart along it (from body to body if the balls are one
            // above the other), just far enough
            vec3 h(delta.x, 0, delta.z);
            if (glm::length(h) < 0.35f * glm::length(delta)) h = vec3((*A.pos + A.off) - (*B.pos + B.off)) * vec3(1, 0, 1);
            if (glm::length(h) < 1e-5f) h = vec3(1, 0, 0);
            h = glm::normalize(h);
            float p = glm::dot(delta, h);
            float s = -p + std::sqrt(std::max(0.0f, p * p - glm::dot(delta, delta) + reach * reach));
            ma = h * (s * wa);
            mb = -h * (s * (1.0f - wa));
          } else {
            float l = glm::length(delta);
            vec3 n = l > 1e-5f ? delta / l : vec3(0, 1, 0);
            vec3 push = n * (reach - l);
            ma = push * wa;
            mb = -push * (1.0f - wa);
            // one standing on the board keeps to it: the other takes all of the shove up or down
            if (!A.free) {
              mb.y = -push.y;
              ma.y = 0;
            } else if (!B.free) {
              ma.y = push.y;
              mb.y = 0;
            }
          }
          A.off += ma;
          B.off += mb;
        }
      }
  grips.clear();
  for (Body& b : bodies) {
    float l = glm::length(b.off);
    if (l > 0.3f) b.off *= 0.3f / l;
    *b.pos += b.off;
    if (b.free && b.pos->y < 0.0f) b.pos->y = 0.0f;
  }
}

void World::simulate(float dt) {
  updateFidgets(dt);
  keepClear(dt);
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
    if (p.growRate != 0.0f) {  // a pool of blood spreading
      p.scale = p.growRate > 0 ? std::min(p.growTo, p.scale + p.growRate * dt) : std::max(p.growTo, p.scale + p.growRate * dt);
      if (p.scale == p.growTo) p.growRate = 0;
    }
    if (p.life >= 0.0f) {  // left to fade away (a body, a pool of blood)
      p.life -= dt;
      if (p.life < 0.0f) {
        p.life = 0.0f;
        p.dissolve = std::min(1.0f, p.dissolve + p.fadeRate * dt);
        if (p.dissolve >= 1.0f) p.alive = false;
      }
    }
  }
  puppets.erase(std::remove_if(puppets.begin(), puppets.end(), [](const Puppet& p) { return !p.alive; }),
                puppets.end());
  keepApart();
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
    if (a.set == 1 || a.set >= 3) {
      // painted figures are not made of rubber: only a hint of the squash-and-stretch rig
      d.rig = glm::vec4(a.rig.x * 0.3f, a.rig.y, 1.0f + (a.rig.z - 1.0f) * 0.12f, a.rig.w * 0.25f);
    } else {
      // aliens are a bit rubbery (the saucer and the tripod less so)
      float k = (a.kind == meshgen::K_KNIGHT || a.kind == meshgen::K_ROOK) ? 0.4f : 0.8f;
      d.rig = glm::vec4(a.rig.x * k, a.rig.y, 1.0f + (a.rig.z - 1.0f) * k, a.rig.w * k);
    }
    d.rigH = a.height() / std::max(a.scale, 1e-3f);
    d.material = a.set == 2 ? MAT_ALIEN : (a.set == 3 ? MAT_ROMAN : (a.set == 4 ? MAT_DINO : MAT_MEDIEVAL));
    d.interior = (a.set == 3 && a.kind != meshgen::K_ROOK) || a.set == 4 ? 1.0f : 0.0f;
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
    d.material = c.set == 1   ? MAT_MEDIEVAL
                 : c.set == 2 ? MAT_ALIEN
                 : c.set == 3 ? MAT_ROMAN
                 : c.set == 4 ? MAT_DINO
                              : (c.color == 0 ? MAT_WHITE : MAT_BLACK);
    d.interior = (c.set == 3 && c.kind != roman::R_ROOK && c.kind != roman::R_BOLT && c.kind != roman::R_PILUM) || c.set == 4 ? 1.0f : 0.0f;
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
    d.material = p.set == 2 ? MAT_ALIEN : (p.set == 3 ? MAT_ROMAN : (p.set == 4 ? MAT_DINO : MAT_MEDIEVAL));
    d.team = p.color;
    d.bones = p.pose.bone;
    d.boneCount = p.pose.count;
    d.texOffset = p.texOffset;
    d.glow = p.glow;
    d.dissolve = p.dissolve;
    d.dissolveColor = p.set >= 3 ? vec3(0.0f) : vec3(1.0f, 0.5f, 0.15f);  // the Romans' and dinosaurs' dead simply fade
    d.interior = p.interior;
    if (!p.shadow) {
      d.castShadow = false;
      d.reflect = false;
      d.groundAO = false;
    }
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
