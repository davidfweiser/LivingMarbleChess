// CPU particle system: dust, smoke, sparks, embers, magic, shockwaves.
#pragma once
#include <glm/glm.hpp>
#include <random>
#include <vector>

#include "renderer.h"

enum ParticleType { PT_GLOW = 0, PT_SMOKE = 1, PT_SPARK = 2, PT_RING = 3 };

struct Particle {
  glm::vec3 pos, vel;
  float size = 0.05f, grow = 0.0f;
  glm::vec3 color{1}, colorEnd{1};
  float opacity = 1;     // >0 alpha blended (smoke/dust), 0 = additive
  float life = 0, maxLife = 1;
  int type = PT_GLOW;
  float drag = 1.0f, gravity = 0.0f;
  float rot = 0, rotVel = 0;
  float stretch = 0;
  float seed = 0;
  float fadeIn = 0.05f;
  bool bounce = false;
  bool still = false;  // keep position (used for velocity-stretched bolts)
};

class ParticleSystem {
 public:
  ParticleSystem();
  void update(float dt);
  void collect(std::vector<ParticleGPU>& out) const;
  void clear() { p_.clear(); }
  size_t size() const { return p_.size(); }
  void add(const Particle& p) {
    if (p_.size() < 6000) p_.push_back(p);
  }

  // ---- effect recipes ----
  void dust(const glm::vec3& at, int n, float radius, float speed, const glm::vec3& tint = glm::vec3(0.55f, 0.5f, 0.45f));
  void chips(const glm::vec3& at, int n, float speed, bool blackStone);
  void sparks(const glm::vec3& at, const glm::vec3& dir, int n, float speed, float spread,
              const glm::vec3& col = glm::vec3(4.0f, 2.2f, 0.8f));
  void smoke(const glm::vec3& at, int n, float radius, float rise, float darkness = 0.5f);
  void fireball(const glm::vec3& at, float scale);
  void embers(const glm::vec3& at, int n, float speed);
  void magicBurst(const glm::vec3& at, const glm::vec3& col, int n, float speed);
  void magicTrail(const glm::vec3& at, const glm::vec3& vel, const glm::vec3& col);
  void swirl(const glm::vec3& center, float radius, const glm::vec3& col, float t);
  void ring(const glm::vec3& at, float size, const glm::vec3& col, float life);
  void flash(const glm::vec3& at, float size, const glm::vec3& col, float life);
  void bolt(const glm::vec3& a, const glm::vec3& b, const glm::vec3& col, float jitter, int segs);

  float rnd() { return U_(rng_); }
  float rnd(float a, float b) { return a + (b - a) * U_(rng_); }
  glm::vec3 rndDir();

 private:
  std::vector<Particle> p_;
  std::mt19937 rng_;
  std::uniform_real_distribution<float> U_{0.0f, 1.0f};
};
