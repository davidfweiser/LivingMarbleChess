#include "particles.h"

#include <algorithm>
#include <cmath>

using glm::vec3;

ParticleSystem::ParticleSystem() : rng_(1337) {}

vec3 ParticleSystem::rndDir() {
  float z = rnd(-1, 1), a = rnd(0, 6.2831853f);
  float r = std::sqrt(std::max(0.0f, 1 - z * z));
  return vec3(r * std::cos(a), z, r * std::sin(a));
}

void ParticleSystem::update(float dt) {
  for (size_t i = 0; i < p_.size();) {
    Particle& p = p_[i];
    p.life += dt;
    if (p.life >= p.maxLife) {
      p = p_.back();
      p_.pop_back();
      continue;
    }
    if (!p.still) {
      p.vel *= std::exp(-p.drag * dt);
      p.vel.y -= p.gravity * dt;
      p.pos += p.vel * dt;
      // only the board top catches particles; past its edge they drop away
      bool onBoard = std::fabs(p.pos.x) < 4.62f && std::fabs(p.pos.z) < 4.62f && p.pos.y > -0.1f;
      if (p.bounce && onBoard) {
        if (p.pos.y < p.size * 0.5f) {
          p.pos.y = p.size * 0.5f;
          if (p.vel.y < 0) p.vel.y *= -0.32f;
          p.vel.x *= 0.6f;
          p.vel.z *= 0.6f;
        }
      }
    }
    p.size = std::max(0.001f, p.size + p.grow * dt);
    p.rot += p.rotVel * dt;
    ++i;
  }
}

void ParticleSystem::collect(std::vector<ParticleGPU>& out) const {
  out.reserve(out.size() + p_.size());
  for (const Particle& p : p_) {
    float f = p.life / p.maxLife;
    float a = glm::smoothstep(0.0f, std::max(p.fadeIn, 1e-4f), p.life) * (1.0f - glm::smoothstep(0.55f, 1.0f, f));
    vec3 c = glm::mix(p.color, p.colorEnd, f);
    ParticleGPU g;
    g.posSize = glm::vec4(p.pos, p.size);
    if (p.opacity > 0) g.color = glm::vec4(c * p.opacity * a, p.opacity * a);
    else g.color = glm::vec4(c * a, 0.0f);
    g.velType = glm::vec4(p.vel, (float)p.type);
    g.misc = glm::vec4(p.rot, p.stretch, p.seed, f);
    out.push_back(g);
  }
}

// ---------------------------------------------------------------------------
void ParticleSystem::dust(const vec3& at, int n, float radius, float speed, const vec3& tint) {
  for (int i = 0; i < n; ++i) {
    Particle p;
    float a = rnd(0, 6.2831853f);
    vec3 d(std::cos(a), 0, std::sin(a));
    p.pos = at + d * rnd(0.0f, radius) + vec3(0, rnd(0.0f, 0.05f), 0);
    p.vel = d * speed * rnd(0.4f, 1.0f) + vec3(0, rnd(0.05f, 0.35f) * speed, 0);
    p.size = rnd(0.06f, 0.15f);
    p.grow = rnd(0.12f, 0.3f);
    p.color = tint * rnd(0.85f, 1.1f);
    p.colorEnd = p.color * 0.9f;
    p.opacity = rnd(0.18f, 0.35f);
    p.maxLife = rnd(1.2f, 2.4f);
    p.type = PT_SMOKE;
    p.drag = 2.6f;
    p.gravity = -0.05f;
    p.rot = rnd(0, 6.28f);
    p.rotVel = rnd(-0.6f, 0.6f);
    p.seed = rnd();
    p.fadeIn = 0.08f;
    add(p);
  }
}

void ParticleSystem::chips(const vec3& at, int n, float speed, bool black) {
  for (int i = 0; i < n; ++i) {
    Particle p;
    vec3 d = rndDir();
    d.y = std::fabs(d.y) * 0.8f + 0.2f;
    p.pos = at + d * 0.03f;
    p.vel = glm::normalize(d) * speed * rnd(0.3f, 1.0f);
    p.size = rnd(0.004f, 0.011f);
    float v = black ? rnd(0.02f, 0.06f) : rnd(0.55f, 0.8f);
    p.color = vec3(v, v * 0.98f, v * 0.95f);
    p.colorEnd = p.color;
    p.opacity = 1.0f;
    p.maxLife = rnd(1.0f, 2.0f);
    p.type = PT_GLOW;
    p.drag = 0.3f;
    p.gravity = 9.8f;
    p.bounce = true;
    p.fadeIn = 0.01f;
    add(p);
  }
}

void ParticleSystem::sparks(const vec3& at, const vec3& dir, int n, float speed, float spread, const vec3& col) {
  for (int i = 0; i < n; ++i) {
    Particle p;
    vec3 d = glm::normalize(dir + rndDir() * spread);
    p.pos = at;
    p.vel = d * speed * rnd(0.35f, 1.0f);
    p.size = rnd(0.006f, 0.014f);
    p.color = col * rnd(0.7f, 1.3f);
    p.colorEnd = vec3(1.2f, 0.25f, 0.05f);
    p.opacity = 0;
    p.maxLife = rnd(0.25f, 0.8f);
    p.type = PT_SPARK;
    p.stretch = 0.035f;
    p.drag = 1.2f;
    p.gravity = 7.0f;
    p.bounce = true;
    p.fadeIn = 0.0f;
    add(p);
  }
}

void ParticleSystem::smoke(const vec3& at, int n, float radius, float rise, float darkness) {
  for (int i = 0; i < n; ++i) {
    Particle p;
    p.pos = at + rndDir() * radius * rnd(0.2f, 1.0f);
    p.vel = rndDir() * 0.25f + vec3(0, rise * rnd(0.5f, 1.2f), 0);
    p.size = rnd(0.08f, 0.18f);
    p.grow = rnd(0.16f, 0.3f);
    float g = glm::mix(0.30f, 0.06f, darkness);
    p.color = vec3(g, g * 0.97f, g * 0.94f);
    p.colorEnd = p.color * 0.8f;
    p.opacity = rnd(0.16f, 0.32f);
    p.maxLife = rnd(1.3f, 2.4f);
    p.type = PT_SMOKE;
    p.drag = 1.4f;
    p.gravity = -0.12f;
    p.rot = rnd(0, 6.28f);
    p.rotVel = rnd(-0.5f, 0.5f);
    p.seed = rnd();
    p.fadeIn = 0.15f;
    add(p);
  }
}

void ParticleSystem::fireball(const vec3& at, float s) {
  flash(at, 1.4f * s, vec3(8.0f, 5.0f, 2.2f), 0.18f);
  for (int i = 0; i < 40; ++i) {
    Particle p;
    vec3 d = rndDir();
    d.y = std::fabs(d.y) * 0.9f + 0.1f;
    p.pos = at + d * 0.05f * s;
    p.vel = d * rnd(0.6f, 2.6f) * s;
    p.size = rnd(0.08f, 0.2f) * s;
    p.grow = rnd(0.3f, 0.7f) * s;
    p.color = vec3(6.0f, 2.6f, 0.7f) * rnd(0.6f, 1.2f);
    p.colorEnd = vec3(0.6f, 0.08f, 0.02f);
    p.opacity = 0;
    p.maxLife = rnd(0.35f, 0.8f);
    p.type = PT_SMOKE;
    p.drag = 3.0f;
    p.gravity = -0.6f;
    p.rot = rnd(0, 6.28f);
    p.seed = rnd();
    p.fadeIn = 0.02f;
    add(p);
  }
  smoke(at + vec3(0, 0.1f, 0), 7, 0.2f * s, 0.5f, 0.8f);
  embers(at, 30, 2.2f * s);
}

void ParticleSystem::embers(const vec3& at, int n, float speed) {
  for (int i = 0; i < n; ++i) {
    Particle p;
    vec3 d = rndDir();
    d.y = std::fabs(d.y);
    p.pos = at;
    p.vel = d * speed * rnd(0.2f, 1.0f);
    p.size = rnd(0.008f, 0.018f);
    p.color = vec3(5.0f, 1.8f, 0.4f) * rnd(0.6f, 1.4f);
    p.colorEnd = vec3(0.8f, 0.1f, 0.02f);
    p.opacity = 0;
    p.maxLife = rnd(0.8f, 2.4f);
    p.type = PT_GLOW;
    p.drag = 1.6f;
    p.gravity = 1.5f;
    p.bounce = true;
    add(p);
  }
}

void ParticleSystem::magicBurst(const vec3& at, const vec3& col, int n, float speed) {
  for (int i = 0; i < n; ++i) {
    Particle p;
    vec3 d = rndDir();
    p.pos = at + d * 0.03f;
    p.vel = d * speed * rnd(0.3f, 1.0f);
    p.size = rnd(0.012f, 0.035f);
    p.color = col * rnd(0.6f, 1.4f);
    p.colorEnd = col * 0.2f;
    p.opacity = 0;
    p.maxLife = rnd(0.4f, 1.3f);
    p.type = PT_GLOW;
    p.drag = 2.0f;
    p.gravity = -0.2f;
    add(p);
  }
}

void ParticleSystem::magicTrail(const vec3& at, const vec3& vel, const vec3& col) {
  for (int i = 0; i < 3; ++i) {
    Particle p;
    p.pos = at + rndDir() * 0.03f;
    p.vel = vel * 0.1f + rndDir() * 0.15f;
    p.size = rnd(0.015f, 0.04f);
    p.color = col * rnd(0.5f, 1.2f);
    p.colorEnd = col * 0.1f;
    p.opacity = 0;
    p.maxLife = rnd(0.25f, 0.6f);
    p.type = PT_GLOW;
    p.drag = 3.0f;
    add(p);
  }
}

void ParticleSystem::swirl(const vec3& c, float radius, const vec3& col, float t) {
  for (int i = 0; i < 2; ++i) {
    Particle p;
    float a = t * 7.0f + i * 3.14159f + rnd(-0.3f, 0.3f);
    vec3 d(std::cos(a), 0, std::sin(a));
    p.pos = c + d * radius + vec3(0, rnd(-0.3f, 0.3f), 0);
    p.vel = vec3(-d.z, 0, d.x) * 2.5f * radius + vec3(0, rnd(0.2f, 0.8f), 0) - d * 0.4f;
    p.size = rnd(0.012f, 0.03f);
    p.color = col * rnd(0.7f, 1.3f);
    p.colorEnd = col * 0.15f;
    p.opacity = 0;
    p.maxLife = rnd(0.5f, 1.0f);
    p.type = PT_SPARK;
    p.stretch = 0.03f;
    p.drag = 1.0f;
    add(p);
  }
}

void ParticleSystem::ring(const vec3& at, float size, const vec3& col, float life) {
  Particle p;
  p.pos = at;
  p.vel = vec3(0);
  p.size = size;
  p.grow = 3.5f;
  p.color = col;
  p.colorEnd = col * 0.1f;
  p.opacity = 0;
  p.maxLife = life;
  p.type = PT_RING;
  p.drag = 0;
  p.fadeIn = 0.0f;
  add(p);
}

void ParticleSystem::flash(const vec3& at, float size, const vec3& col, float life) {
  Particle p;
  p.pos = at;
  p.vel = vec3(0);
  p.size = size;
  p.grow = size * 1.5f;
  p.color = col;
  p.colorEnd = col * 0.2f;
  p.opacity = 0;
  p.maxLife = life;
  p.type = PT_GLOW;
  p.fadeIn = 0.0f;
  p.still = true;
  add(p);
}

void ParticleSystem::bolt(const vec3& a, const vec3& b, const vec3& col, float jitter, int segs) {
  std::vector<vec3> pts(segs + 1);
  vec3 ab = b - a;
  vec3 side = glm::normalize(glm::cross(ab, vec3(0, 1, 0)) + vec3(1e-4f));
  vec3 up = glm::normalize(glm::cross(side, ab));
  for (int i = 0; i <= segs; ++i) {
    float t = (float)i / segs;
    float env = std::sin(t * 3.14159f);
    pts[i] = a + ab * t + (side * rnd(-1, 1) + up * rnd(-1, 1)) * jitter * env;
  }
  for (int i = 0; i < segs; ++i) {
    vec3 m = (pts[i] + pts[i + 1]) * 0.5f;
    vec3 d = pts[i + 1] - pts[i];
    Particle p;
    p.pos = m;
    p.vel = d * 0.5f;  // used only for stretching
    p.still = true;
    p.stretch = 1.0f;
    p.size = 0.007f;
    p.color = col * rnd(0.9f, 1.4f);
    p.colorEnd = col * 0.5f;
    p.opacity = 0;
    p.maxLife = rnd(0.06f, 0.12f);
    p.type = PT_SPARK;
    p.fadeIn = 0.0f;
    add(p);
    Particle g = p;
    g.size = 0.03f;
    g.color = col * 0.12f;
    add(g);
  }
}
