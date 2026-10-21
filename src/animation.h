// Living pieces: a spring-driven squash/bend rig for every chessman, rigid
// body debris for shattered pieces, weapon props, dynamic lights and the
// cinematic camera.
#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <vector>

#include "audio.h"
#include "meshgen.h"
#include "particles.h"
#include "renderer.h"

constexpr float kPI = 3.14159265358979f;

inline glm::vec3 squareCenter(int sq) { return glm::vec3((sq & 7) - 3.5f, 0.0f, 3.5f - (sq >> 3)); }
inline float faceAngle(const glm::vec3& d) { return std::atan2(d.x, d.z); }
float wrapAngle(float a);
float easeInOut(float t);
float easeOut(float t);
float easeIn(float t);
float easeOutBack(float t, float s = 1.7f);

struct Spring1 {
  float x = 0, v = 0;
  void step(float target, float k, float zeta, float dt);
};

struct Actor {
  int id = -1, kind = 0, color = 0, square = -1;
  bool alive = true, visible = true;
  glm::vec3 pos{0};
  float yaw = 0, yawTarget = 0, baseYaw = 0, yawRate = 9.0f;
  // explicit pose (owned by animations)
  glm::vec2 poseBend{0};  // local (x = right, y = forward), radians
  float poseSquash = 1, poseTwist = 0;
  glm::quat tilt{1, 0, 0, 0};
  glm::vec3 tiltPivot{0};
  float scale = 1;
  // look
  glm::vec4 glow{0};
  float crack = 0;
  glm::vec3 crackColor{1.0f, 0.6f, 0.25f};
  float dissolve = 0;
  glm::vec3 dissolveColor{1.0f, 0.8f, 0.4f};
  glm::vec3 texOffset{0};
  // behaviour
  float idleWeight = 1;
  float tremble = 0;
  bool selected = false, inCheck = false;
  float phase = 0;
  float selectT = 0, bob = 0, prevSelS = 0;
  float dissolveVel = 0;  // automatic dissolve change per second (materialise / vanish)
  float fidgetT = -1, fidgetDir = 1;
  int fidgetType = 0;
  // secondary motion
  glm::vec3 prevPos{0}, vel{0}, accS{0};
  bool first = true;
  Spring1 bx, bz, sq, tw;
  glm::vec4 rig{0, 0, 1, 0};

  void update(float dt, float time, Audio* audio);
  void neutralize();  // snap the rig to rest (before shattering)
  glm::mat4 model() const;
  glm::mat4 rigidModel() const;  // without tilt
  glm::quat rotation() const;
  float height() const { return meshgen::pieceHeight(kind) * scale; }
  float radius() const { return meshgen::pieceRadius(kind) * scale; }
  glm::vec3 forward() const { return glm::vec3(std::sin(yaw), 0, std::cos(yaw)); }
};

struct Chunk {
  int kind = 0, color = 0, index = 0;
  bool half = false;
  glm::vec3 pos{0}, v{0}, w{0};
  glm::quat rot{1, 0, 0, 0};
  float radius = 0.1f, mass = 1, inertia = 1;
  float age = 0, still = 0, dissolve = 0;
  bool sleeping = false;
  glm::vec3 texOffset{0};
  float glow = 0;
  glm::vec3 glowColor{1, 0.6f, 0.2f};
  float lastHit = 0;
};

struct Prop {
  enum Type { SWORD, CANNONBALL } type = SWORD;
  glm::vec3 pos{0};
  glm::quat rot{1, 0, 0, 0};
  float scale = 1;
  float dissolve = 0;
  glm::vec4 glow{0};
  float heat = 0;
  bool alive = true;
  int id = 0;
};

struct GroundRing {
  glm::vec3 pos, color;
  float speed, life, maxLife, width;
};

struct TimedLight {
  glm::vec3 pos, color;
  float radius, life, maxLife;
};

struct CamPose {
  glm::vec3 pos{0, 9, 10}, target{0};
  float fov = 38.0f, focus = 12.0f, aperture = 2.5f;
};

class CameraDirector {
 public:
  float yaw = 0, pitch = 0.80f, dist = 12.6f;
  glm::vec3 target{0, 0.15f, 0};
  float autoSpin = 0;  // idle orbit (attract mode)
  CamPose current;

  void orbit(float dYaw, float dPitch, float dDist);
  void setSide(int color, bool instant = false);
  void setShot(const CamPose& p, float blendTime);
  void release(float blendTime);
  bool inShot() const { return shotOn_; }
  CamPose orbitPose() const;
  CamPose evaluate(float dt, float trauma, float time);

 private:
  float yawS_ = 0, pitchS_ = 0.8f, distS_ = 12.6f;
  bool init_ = false;
  bool shotOn_ = false;
  CamPose shot_;
  float blend_ = 0, blendSpeed_ = 2.0f;
  float shotT_ = 0;
};

class World;

// A choreographed sequence (move, battle, promotion, ...).
class Anim {
 public:
  virtual ~Anim() {}
  virtual bool update(World& w, float dt) = 0;  // true when finished
};

class World {
 public:
  const meshgen::Library* lib = nullptr;
  Audio* audio = nullptr;
  std::vector<Actor> actors;
  int actorAt[64];
  ParticleSystem fx;
  std::vector<Chunk> chunks;
  std::vector<Prop> props;
  std::vector<TimedLight> lights;
  std::vector<GroundRing> rings;
  std::vector<PointLight> frameLights;  // cleared every frame
  CameraDirector cam;
  float trauma = 0;
  float timeScale = 1, slowTarget = 1, slowTimer = 0;
  glm::vec3 flash{0};
  float time = 0;
  float lastDt = 0;  // scaled (slow-motion aware) step of the last update
  bool cinematic = true;
  int nextPropId = 1;
  float moteTimer = 0;
  bool motes = true;
  bool fidgetAllowed = false;
  float fidgetTimer = 2.0f;
  int excludeFidget = -1;
  void stopFidgets();

  void reset();
  void spawnFromBoard(const uint8_t sq[64], bool materialize);
  Actor* at(int square) { return actorAt[square] >= 0 ? &actors[actorAt[square]] : nullptr; }
  void shake(float amount) { trauma = std::min(1.0f, trauma + amount); }
  void slowmo(float scale, float duration) {
    slowTarget = scale;
    slowTimer = duration;
  }
  void light(const glm::vec3& p, const glm::vec3& c, float radius, float life) {
    lights.push_back({p, c, radius, life, life});
  }
  void addFlash(const glm::vec3& c) { flash += c; }
  void groundRing(const glm::vec3& p, const glm::vec3& c, float speed = 3.0f, float life = 0.6f, float width = 0.08f) {
    rings.push_back({p, c, speed, life, life, width});
  }
  void collectRings(std::vector<glm::vec4>& r, std::vector<glm::vec4>& c) const;
  void sound(Sfx s, float vol = 1.0f, const glm::vec3& at = glm::vec3(0), float pitch = 1.0f);
  Prop* prop(int id);
  int addProp(Prop::Type t);

  // shatter an actor into Voronoi chunks
  void shatter(Actor& a, const glm::vec3& impact, const glm::vec3& impulse, float explode, float upBias,
               float spin = 6.0f);
  // slice an actor in two along its slice plane
  void slice(Actor& a, float separation);
  float beginFrame(float dtReal);  // slow-motion, decays; returns the scaled dt
  void simulate(float dt);         // physics, particles, lights, actors
  void buildDrawItems(Renderer& r, std::vector<DrawItem>& out) const;
  void collectLights(std::vector<PointLight>& out) const;
  void collectOccluders(std::vector<glm::vec4>& out) const;

 private:
  void updateChunks(float dt);
  void updateFidgets(float dt);
};
