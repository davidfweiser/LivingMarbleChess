#include "meshgen.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace meshgen {
using glm::vec2;
using glm::vec3;
using namespace sdf;

namespace {

// ---------------------------------------------------------------------------
// tiny value noise used for fracture domain warping
// ---------------------------------------------------------------------------
float hash3(int x, int y, int z) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  h ^= h >> 16;
  return (float)(h & 0xffffff) / (float)0xffffff * 2.0f - 1.0f;
}
float vnoise(const vec3& p) {
  vec3 f = glm::floor(p);
  vec3 t = p - f;
  t = t * t * (3.0f - 2.0f * t);
  int x = (int)f.x, y = (int)f.y, z = (int)f.z;
  float n000 = hash3(x, y, z), n100 = hash3(x + 1, y, z), n010 = hash3(x, y + 1, z), n110 = hash3(x + 1, y + 1, z);
  float n001 = hash3(x, y, z + 1), n101 = hash3(x + 1, y, z + 1), n011 = hash3(x, y + 1, z + 1),
        n111 = hash3(x + 1, y + 1, z + 1);
  float a = glm::mix(glm::mix(n000, n100, t.x), glm::mix(n010, n110, t.x), t.y);
  float b = glm::mix(glm::mix(n001, n101, t.x), glm::mix(n011, n111, t.x), t.y);
  return glm::mix(a, b, t.z);
}
vec3 warp(const vec3& p, float freq, float amp) {
  return p + amp * vec3(vnoise(p * freq + vec3(11.3f)), vnoise(p * freq + vec3(-7.1f, 3.3f, 19.7f)),
                        vnoise(p * freq + vec3(5.9f, -13.4f, 2.2f)));
}

// ---------------------------------------------------------------------------
// Profiles (radius, height). All units: one board square = 1.0
// ---------------------------------------------------------------------------
std::vector<vec2> baseOutline(float R) {
  return {{0, 0},
          {R * 0.94f, 0},
          {R * 0.98f, 0.006f},
          {R, 0.022f},
          {R * 0.995f, 0.040f},
          {R * 0.955f, 0.054f},
          {R * 0.91f, 0.060f},
          {R * 0.91f, 0.068f},
          {R * 0.94f, 0.078f},
          {R * 0.942f, 0.088f},
          {R * 0.908f, 0.098f},
          {R * 0.865f, 0.104f},
          {R * 0.865f, 0.110f}};
}

void appendArc(std::vector<vec2>& o, float cy, float R, float a0deg, float a1deg, int steps) {
  for (int i = 0; i <= steps; ++i) {
    float a = glm::radians(a0deg + (a1deg - a0deg) * i / steps);
    o.push_back({R * std::cos(a), cy + R * std::sin(a)});
  }
  o.back().x = 0;
}

void appendPts(std::vector<vec2>& o, std::initializer_list<vec2> pts) { o.insert(o.end(), pts.begin(), pts.end()); }

const Profile& pawnProfile() {
  static Profile P = [] {
    std::vector<vec2> o = baseOutline(0.325f);
    appendPts(o, {{0.258f, 0.122f}, {0.226f, 0.142f}, {0.196f, 0.17f}, {0.170f, 0.21f}, {0.150f, 0.27f},
                  {0.136f, 0.34f},  {0.126f, 0.41f},  {0.120f, 0.462f}, {0.133f, 0.472f}, {0.183f, 0.484f},
                  {0.196f, 0.498f}, {0.190f, 0.513f}, {0.148f, 0.523f}, {0.106f, 0.532f}, {0.092f, 0.547f}});
    appendArc(o, 0.692f, 0.152f, -52.0f, 90.0f, 14);
    Profile p;
    p.build(o, 3);
    return p;
  }();
  return P;
}

const Profile& rookProfile() {
  static Profile P = [] {
    std::vector<vec2> o = baseOutline(0.37f);
    appendPts(o, {{0.300f, 0.124f}, {0.270f, 0.145f}, {0.252f, 0.175f}, {0.240f, 0.22f},  {0.229f, 0.40f},
                  {0.219f, 0.60f},  {0.215f, 0.70f},  {0.222f, 0.725f}, {0.250f, 0.745f}, {0.263f, 0.762f},
                  {0.259f, 0.778f}, {0.245f, 0.788f}, {0.250f, 0.795f}, {0.272f, 0.808f}, {0.278f, 0.83f},
                  {0.278f, 0.83f},  {0.280f, 0.94f},  {0.280f, 0.94f},  {0.274f, 0.955f}, {0.262f, 0.958f},
                  {0.206f, 0.958f}, {0.206f, 0.958f}, {0.199f, 0.95f},  {0.197f, 0.905f}, {0.197f, 0.905f},
                  {0.180f, 0.898f}, {0.0f, 0.898f}});
    Profile p;
    p.build(o, 3);
    return p;
  }();
  return P;
}

const Profile& knightBaseProfile() {
  static Profile P = [] {
    std::vector<vec2> o = baseOutline(0.37f);
    appendPts(o, {{0.300f, 0.124f}, {0.270f, 0.145f}, {0.252f, 0.175f}, {0.240f, 0.21f}, {0.252f, 0.224f},
                  {0.274f, 0.238f}, {0.279f, 0.255f}, {0.271f, 0.270f}, {0.242f, 0.279f}, {0.222f, 0.283f},
                  {0.200f, 0.290f}, {0.0f, 0.290f}});
    Profile p;
    p.build(o, 3);
    return p;
  }();
  return P;
}

const Profile& bishopProfile() {
  static Profile P = [] {
    std::vector<vec2> o = baseOutline(0.355f);
    appendPts(o, {{0.288f, 0.124f}, {0.254f, 0.145f}, {0.215f, 0.175f}, {0.181f, 0.22f}, {0.156f, 0.30f},
                  {0.136f, 0.42f},  {0.121f, 0.55f},  {0.113f, 0.66f},  {0.111f, 0.70f}, {0.123f, 0.712f},
                  {0.186f, 0.726f}, {0.199f, 0.740f}, {0.191f, 0.755f}, {0.136f, 0.765f}, {0.119f, 0.772f},
                  {0.151f, 0.780f}, {0.159f, 0.792f}, {0.151f, 0.802f}, {0.106f, 0.810f}, {0.086f, 0.825f},
                  {0.101f, 0.845f}, {0.136f, 0.88f},  {0.159f, 0.93f},  {0.166f, 0.98f}, {0.159f, 1.03f},
                  {0.139f, 1.08f},  {0.106f, 1.125f}, {0.069f, 1.16f},  {0.036f, 1.18f}, {0.027f, 1.19f}});
    appendArc(o, 1.226f, 0.043f, -50.0f, 90.0f, 10);
    Profile p;
    p.build(o, 3);
    return p;
  }();
  return P;
}

const Profile& queenProfile() {
  static Profile P = [] {
    std::vector<vec2> o = baseOutline(0.375f);
    appendPts(o, {{0.300f, 0.124f}, {0.265f, 0.145f}, {0.226f, 0.175f}, {0.191f, 0.22f}, {0.166f, 0.30f},
                  {0.144f, 0.44f},  {0.128f, 0.60f},  {0.119f, 0.74f},  {0.116f, 0.80f}, {0.127f, 0.812f},
                  {0.201f, 0.828f}, {0.215f, 0.843f}, {0.207f, 0.858f}, {0.146f, 0.868f}, {0.126f, 0.875f},
                  {0.166f, 0.884f}, {0.173f, 0.896f}, {0.165f, 0.906f}, {0.113f, 0.914f}, {0.099f, 0.935f},
                  {0.109f, 1.00f},  {0.131f, 1.07f},  {0.161f, 1.13f},  {0.191f, 1.18f}, {0.209f, 1.215f},
                  {0.215f, 1.235f}, {0.211f, 1.25f},  {0.186f, 1.255f}, {0.151f, 1.262f}, {0.126f, 1.275f},
                  {0.101f, 1.30f},  {0.076f, 1.325f}, {0.051f, 1.338f}, {0.033f, 1.345f}});
    appendArc(o, 1.386f, 0.049f, -48.0f, 90.0f, 10);
    Profile p;
    p.build(o, 3);
    return p;
  }();
  return P;
}

const Profile& kingProfile() {
  static Profile P = [] {
    std::vector<vec2> o = baseOutline(0.385f);
    appendPts(o, {{0.312f, 0.124f}, {0.278f, 0.145f}, {0.238f, 0.175f}, {0.201f, 0.22f}, {0.175f, 0.31f},
                  {0.153f, 0.46f},  {0.135f, 0.64f},  {0.125f, 0.80f},  {0.121f, 0.88f}, {0.132f, 0.893f},
                  {0.213f, 0.91f},  {0.228f, 0.926f}, {0.219f, 0.942f}, {0.153f, 0.952f}, {0.131f, 0.958f},
                  {0.176f, 0.968f}, {0.183f, 0.982f}, {0.173f, 0.992f}, {0.119f, 1.0f},   {0.103f, 1.02f},
                  {0.113f, 1.09f},  {0.136f, 1.16f},  {0.169f, 1.22f},  {0.196f, 1.265f}, {0.206f, 1.29f},
                  {0.206f, 1.305f}, {0.197f, 1.315f}, {0.171f, 1.32f},  {0.151f, 1.33f},  {0.126f, 1.35f},
                  {0.099f, 1.375f}, {0.071f, 1.395f}, {0.049f, 1.405f}, {0.041f, 1.41f},  {0.037f, 1.42f},
                  {0.041f, 1.43f},  {0.035f, 1.44f},  {0.0f, 1.44f}});
    Profile p;
    p.build(o, 3);
    return p;
  }();
  return P;
}

// ---------------------------------------------------------------------------
// Piece SDFs
// ---------------------------------------------------------------------------
float pawnSDF(const vec3& p) { return pawnProfile().eval(p); }

float rookSDF(const vec3& p) {
  float d = rookProfile().eval(p);
  if (p.y > 0.85f) {
    for (int k = 0; k < 3; ++k) {
      vec3 q = rotY(p, glm::radians(30.0f + 60.0f * k));
      float slot = roundBox(q - vec3(0, 1.0f, 0), vec3(0.042f, 0.087f, 0.5f), 0.01f);
      d = smax(d, -slot, 0.006f);
    }
  }
  return d;
}

float bishopSDF(const vec3& p) {
  float d = bishopProfile().eval(p);
  if (p.y > 0.84f && p.y < 1.2f) {
    vec3 n = glm::normalize(vec3(0.28f, 0.80f, 0.52f));
    float slab = std::fabs(glm::dot(p - vec3(0.0f, 1.035f, 0.0f), n)) - 0.0125f;
    float front = -(glm::dot(p, glm::normalize(vec3(0.45f, 0.0f, 1.0f))) + 0.035f);
    float cut = std::max(slab, front);
    d = smax(d, -cut, 0.008f);
  }
  return d;
}

float queenSDF(const vec3& p) {
  float d = queenProfile().eval(p);
  if (p.y > 1.12f && p.y < 1.36f) {
    float a = std::atan2(p.z, p.x);
    const float step = 6.2831853f / 8.0f;
    // scallops between the crown points
    float k = std::floor(a / step + 0.5f);
    float an = k * step;
    vec3 c(0.236f * std::cos(an), 1.268f, 0.236f * std::sin(an));
    d = smax(d, -(glm::length(p - c) - 0.054f), 0.012f);
    // small balls on the points
    float kp = std::floor((a - step * 0.5f) / step + 0.5f);
    float ap = kp * step + step * 0.5f;
    vec3 b(0.204f * std::cos(ap), 1.246f, 0.204f * std::sin(ap));
    d = smin(d, glm::length(p - b) - 0.021f, 0.008f);
  }
  return d;
}

float kingSDF(const vec3& p) {
  float d = kingProfile().eval(p);
  if (p.y > 1.38f) {
    float v = roundBox(p - vec3(0, 1.535f, 0), vec3(0.029f, 0.098f, 0.023f), 0.009f);
    float h = roundBox(p - vec3(0, 1.556f, 0), vec3(0.086f, 0.027f, 0.023f), 0.009f);
    // flared ends (cross pattee)
    float c = std::min(v, h);
    for (int s = -1; s <= 1; s += 2) {
      c = smin(c, roundBox(p - vec3(s * 0.082f, 1.556f, 0), vec3(0.010f, 0.036f, 0.023f), 0.008f), 0.012f);
    }
    c = smin(c, roundBox(p - vec3(0, 1.627f, 0), vec3(0.037f, 0.010f, 0.023f), 0.008f), 0.012f);
    d = smin(d, c, 0.018f);
  }
  return d;
}

inline vec3 narrowX(const vec3& p, float s) { return vec3(p.x / s, p.y, p.z); }

float knightSDF(const vec3& pIn) {
  float d = knightBaseProfile().eval(pIn);
  if (pIn.y < 0.22f) return d;
  // horse head built in +Z-facing local space, scaled up slightly
  const float S = 1.08f;
  vec3 p = pIn / S;
  const float sx = 0.74f;
  vec3 pn = narrowX(p, sx);
  float neck = roundCone(pn, vec3(0, 0.25f, -0.03f), vec3(0, 0.60f, -0.075f), 0.165f, 0.118f) * sx;
  float neck2 = roundCone(pn, vec3(0, 0.60f, -0.075f), vec3(0, 0.79f, -0.025f), 0.118f, 0.102f) * sx;
  float chest = ellipsoid(p - vec3(0, 0.39f, 0.065f), vec3(0.128f, 0.13f, 0.14f));
  const float shx = 0.62f;
  vec3 ph = narrowX(p, shx);
  float head = roundCone(ph, vec3(0, 0.83f, 0.03f), vec3(0, 0.655f, 0.295f), 0.122f, 0.078f) * shx;
  float cheek = ellipsoid(p - vec3(0, 0.735f, 0.045f), vec3(0.083f, 0.10f, 0.105f));
  float brow = ellipsoid(p - vec3(0, 0.83f, 0.085f), vec3(0.07f, 0.06f, 0.09f));
  float muzzle = ellipsoid(p - vec3(0, 0.648f, 0.302f), vec3(0.060f, 0.066f, 0.068f));
  float h = smin(neck, neck2, 0.06f);
  h = smin(h, chest, 0.09f);
  h = smin(h, head, 0.06f);
  h = smin(h, cheek, 0.05f);
  h = smin(h, brow, 0.04f);
  h = smin(h, muzzle, 0.035f);
  // ears
  for (int s = -1; s <= 1; s += 2) {
    float ear = roundCone(p, vec3(s * 0.038f, 0.875f, -0.012f), vec3(s * 0.05f, 0.99f, -0.05f), 0.027f, 0.006f);
    h = smin(h, ear, 0.025f);
  }
  // mane ridge running down the back of the neck
  vec3 pm = narrowX(p, 0.45f);
  float mane = capsule(pm, vec3(0, 0.33f, -0.172f), vec3(0, 0.66f, -0.152f), 0.05f) * 0.45f;
  float mane2 = capsule(pm, vec3(0, 0.66f, -0.152f), vec3(0, 0.88f, -0.055f), 0.045f) * 0.45f;
  float m = smin(mane, mane2, 0.03f);
  // carved locks of hair
  float locks = std::sin((p.y * 0.85f - p.z * 0.55f) * 70.0f + std::sin(p.x * 40.0f) * 0.8f) * 0.0026f;
  m += locks;
  h = smin(h, m, 0.03f);
  // eye sockets + eyeballs, nostrils, mouth
  for (int s = -1; s <= 1; s += 2) {
    h = smax(h, -(glm::length(p - vec3(s * 0.062f, 0.79f, 0.112f)) - 0.024f), 0.012f);
    h = smin(h, glm::length(p - vec3(s * 0.050f, 0.79f, 0.115f)) - 0.017f, 0.006f);
    h = smax(h, -(glm::length(p - vec3(s * 0.030f, 0.668f, 0.368f)) - 0.016f), 0.008f);
  }
  {
    vec3 q = rotX(p - vec3(0, 0.618f, 0.325f), glm::radians(-32.0f));
    float mouth = box(q, vec3(0.08f, 0.0045f, 0.05f));
    h = smax(h, -mouth, 0.006f);
  }
  h *= S;
  return smin(d, h, 0.03f);
}

// ---------------------------------------------------------------------------
float swordBlade(const vec3& p) {
  float y = p.y;
  float t = glm::clamp((y - 0.06f) / 0.94f, 0.0f, 1.0f);
  float tip = std::sqrt(glm::clamp((1.0f - y) / 0.17f, 0.0f, 1.0f));
  float w = 0.047f * (1.0f - 0.22f * t) * tip + 0.0015f;
  float th = 0.014f * (1.0f - 0.35f * t) * (0.4f + 0.6f * tip) + 0.002f;
  vec2 q(std::fabs(p.x), std::fabs(p.z));
  float dx = (q.x / w + q.y / th - 1.0f) * (w * th / std::sqrt(w * w + th * th));
  // fuller (groove) on both faces
  float fuller = glm::length(vec2(q.x / 0.5f, q.y - th * 1.05f)) * 0.5f - 0.0055f;
  if (y < 0.7f) dx = std::max(dx, -fuller);
  return std::max(dx, std::max(0.04f - y, y - 1.0f));
}
float swordGuard(const vec3& p) {
  float g = roundBox(p - vec3(0, 0.03f, 0), vec3(0.15f, 0.020f, 0.030f), 0.012f);
  for (int s = -1; s <= 1; s += 2) g = smin(g, glm::length(p - vec3(s * 0.165f, 0.045f, 0)) - 0.026f, 0.02f);
  g = smin(g, ellipsoid(p - vec3(0, 0.05f, 0), vec3(0.04f, 0.04f, 0.036f)), 0.02f);
  return g;
}
float swordGrip(const vec3& p) {
  float g = capsule(p, vec3(0, -0.01f, 0), vec3(0, -0.25f, 0), 0.023f);
  g += 0.0022f * std::sin(p.y * 140.0f);
  return g;
}
float swordPommel(const vec3& p) { return ellipsoid(p - vec3(0, -0.29f, 0), vec3(0.04f, 0.045f, 0.04f)); }
float swordSDF(const vec3& p) {
  float d = std::min(swordBlade(p), swordGuard(p));
  d = smin(d, swordGrip(p), 0.01f);
  d = smin(d, swordPommel(p), 0.012f);
  return d;
}
float swordMaterial(const vec3& p) {
  float b = swordBlade(p), g = swordGuard(p), gr = swordGrip(p), po = swordPommel(p);
  float m = std::min(std::min(b, g), std::min(gr, po));
  if (m == b) return 0.0f;
  if (m == gr) return 2.0f;
  return 1.0f;
}

// ---------------------------------------------------------------------------
void volumeCentroid(const MeshData& m, float& vol, vec3& c) {
  vol = 0;
  c = vec3(0);
  for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
    const vec3& a = m.v[m.idx[i]].pos;
    const vec3& b = m.v[m.idx[i + 1]].pos;
    const vec3& d = m.v[m.idx[i + 2]].pos;
    float v = glm::dot(a, glm::cross(b, d)) / 6.0f;
    vol += v;
    c += v * (a + b + d) / 4.0f;
  }
  bool ok = std::fabs(vol) > 1e-9f;
  if (ok) {
    c /= vol;
    // a nearly flat fragment can put the integral's centroid far outside it
    vec3 lo(1e9f), hi(-1e9f);
    for (const auto& x : m.v) {
      lo = glm::min(lo, x.pos);
      hi = glm::max(hi, x.pos);
    }
    ok = glm::all(glm::greaterThanEqual(c, lo - 0.01f)) && glm::all(glm::lessThanEqual(c, hi + 0.01f));
  }
  if (!ok) {
    c = vec3(0);
    for (const auto& x : m.v) c += x.pos;
    if (!m.v.empty()) c /= (float)m.v.size();
  }
  vol = std::fabs(vol);
}

Fragment finishFragment(MeshData&& mesh) {
  Fragment f;
  volumeCentroid(mesh, f.volume, f.centroid);
  mesh.translate(-f.centroid);
  f.radius = 0;
  for (const auto& x : mesh.v) f.radius = std::max(f.radius, glm::length(x.pos));
  // hull contact points: extreme vertices in 26 directions
  for (int dx = -1; dx <= 1; ++dx)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dz = -1; dz <= 1; ++dz) {
        if (!dx && !dy && !dz) continue;
        vec3 dir = glm::normalize(vec3((float)dx, (float)dy, (float)dz));
        float best = -1e9f;
        vec3 bp(0);
        for (const auto& x : mesh.v) {
          float s = glm::dot(x.pos, dir);
          if (s > best) {
            best = s;
            bp = x.pos;
          }
        }
        bool dup = false;
        for (const auto& h : f.hull)
          if (glm::length(h - bp) < 1e-4f) dup = true;
        if (!dup) f.hull.push_back(bp);
      }
  f.mesh = std::move(mesh);
  return f;
}

}  // namespace

float pieceHeight(int kind) {
  static const float H[K_COUNT] = {0.844f, 1.08f, 1.269f, 0.958f, 1.435f, 1.64f};
  return H[kind];
}
float pieceRadius(int kind) {
  static const float R[K_COUNT] = {0.325f, 0.37f, 0.355f, 0.37f, 0.375f, 0.385f};
  return R[kind];
}

Fn pieceField(int kind) {
  switch (kind) {
    case K_PAWN: return pawnSDF;
    case K_KNIGHT: return knightSDF;
    case K_BISHOP: return bishopSDF;
    case K_ROOK: return rookSDF;
    case K_QUEEN: return queenSDF;
    default: return kingSDF;
  }
}

static void pieceBounds(int kind, vec3& lo, vec3& hi) {
  float r = pieceRadius(kind) + 0.01f;
  lo = vec3(-r, -0.005f, -r);
  hi = vec3(r, pieceHeight(kind) + 0.01f, r);
  if (kind == K_KNIGHT) {
    lo.z = -0.39f;
    hi.z = 0.44f;
    hi.y = 1.10f;
  }
}

std::vector<Fragment> voronoiShatter(const Fn& F, const vec3& lo, const vec3& hi, int nSeeds, uint32_t seedVal,
                                     float cell, const std::function<void(Vertex&)>& skin) {
  std::mt19937 rng(seedVal);
  std::uniform_real_distribution<float> U(0, 1);
  std::vector<vec3> seeds;
  float minD = 0.10f;
  for (int tries = 0; tries < 20000 && (int)seeds.size() < nSeeds; ++tries) {
    // drawn in a fixed order (argument evaluation order differs between compilers)
    float ux = U(rng), uy = U(rng), uz = U(rng);
    vec3 p(glm::mix(lo.x, hi.x, ux), glm::mix(lo.y, hi.y, uy), glm::mix(lo.z, hi.z, uz));
    if (F(p) > -0.02f) continue;
    bool ok = true;
    for (const vec3& s : seeds)
      if (glm::length(s - p) < minD) ok = false;
    if (ok) seeds.push_back(p);
    if (tries % 4000 == 3999) minD *= 0.8f;
  }
  const int n = (int)seeds.size();
  auto cellDist = [&seeds, n](const vec3& pw, int i) {
    float d = -1e9f;
    for (int j = 0; j < n; ++j) {
      if (j == i) continue;
      vec3 nn = glm::normalize(seeds[j] - seeds[i]);
      vec3 m = (seeds[i] + seeds[j]) * 0.5f;
      d = std::max(d, glm::dot(pw - m, nn));
    }
    return d;
  };
  // bounding boxes of each cell
  std::vector<vec3> bl(n, vec3(1e9f)), bh(n, vec3(-1e9f));
  const float g = 0.02f;
  for (float z = lo.z; z <= hi.z; z += g)
    for (float y = lo.y; y <= hi.y; y += g)
      for (float x = lo.x; x <= hi.x; x += g) {
        vec3 p(x, y, z);
        if (F(p) > g) continue;
        vec3 pw = warp(p, 9.0f, 0.03f);
        int best = 0;
        float bd = 1e9f;
        for (int i = 0; i < n; ++i) {
          float dd = glm::length(pw - seeds[i]);
          if (dd < bd) {
            bd = dd;
            best = i;
          }
        }
        bl[best] = glm::min(bl[best], p);
        bh[best] = glm::max(bh[best], p);
      }
  std::vector<Fragment> out;
  const float gap = 0.0022f;
  for (int i = 0; i < n; ++i) {
    if (bl[i].x > bh[i].x) continue;
    Fn frag = [F, cellDist, i, gap](const vec3& p) {
      vec3 pw = warp(p, 9.0f, 0.03f);
      return std::max(F(p), cellDist(pw, i) + gap);
    };
    MeshOptions o;
    o.cell = cell;
    o.bmin = glm::max(bl[i] - vec3(0.04f), lo - vec3(0.01f));
    o.bmax = glm::min(bh[i] + vec3(0.04f), hi + vec3(0.01f));
    o.computeAO = false;
    o.newtonSteps = 2;
    o.aux = [F](const vec3& p) { return glm::clamp((-F(p) - 0.0015f) / 0.004f, 0.0f, 1.0f); };
    o.skin = skin;
    MeshData m = polygonize(frag, o);
    if (m.idx.size() < 60) continue;
    Fragment fr = finishFragment(std::move(m));
    if (fr.volume < 2e-5f) continue;
    out.push_back(std::move(fr));
  }
  return out;
}

std::vector<Fragment> sliceHalves(const Fn& F, const vec3& lo, const vec3& hi, const vec3& n, const vec3& p0,
                                  float cell, const std::function<void(Vertex&)>& skin) {
  std::vector<Fragment> out;
  for (int s = -1; s <= 1; s += 2) {
    Fn frag = [F, n, p0, s](const vec3& p) {
      float pl = glm::dot(p - p0, n) * (float)s;
      return std::max(F(p), pl + 0.0018f);
    };
    MeshOptions o;
    o.cell = cell;
    o.bmin = lo;
    o.bmax = hi;
    o.computeAO = true;
    o.aoRadius = 0.12f;
    o.aux = [F](const vec3& p) { return glm::clamp((-F(p) - 0.0015f) / 0.004f, 0.0f, 1.0f); };
    o.skin = skin;
    MeshData m = polygonize(frag, o);
    out.push_back(finishFragment(std::move(m)));
  }
  return out;
}

static MeshData buildBoard(float E, float T) {
  MeshData m;
  const float rc = 0.10f, rb = 0.045f, rbb = 0.02f;
  // perimeter of rounded rectangle (counter-clockwise seen from above: +x -> +z)
  struct PP {
    vec2 p, n;
  };
  std::vector<PP> per;
  const int cseg = 10;
  vec2 corners[4] = {{E - rc, E - rc}, {-(E - rc), E - rc}, {-(E - rc), -(E - rc)}, {E - rc, -(E - rc)}};
  float startA[4] = {0, 90, 180, 270};
  for (int c = 0; c < 4; ++c)
    for (int i = 0; i <= cseg; ++i) {
      float a = glm::radians(startA[c] + 90.0f * i / cseg);
      vec2 n(std::cos(a), std::sin(a));
      per.push_back({corners[c] + n * rc, n});
    }
  // profile: (outward offset, y, normal.out, normal.y)
  struct PR {
    float off, y, no, ny;
  };
  std::vector<PR> prof;
  const int bseg = 8;
  for (int i = 0; i <= bseg; ++i) {
    float a = glm::radians(90.0f - 90.0f * i / bseg);
    prof.push_back({-rb + rb * std::cos(a), -rb + rb * std::sin(a), std::cos(a), std::sin(a)});
  }
  prof.push_back({0, -T + rbb, 1, 0});
  for (int i = 1; i <= 4; ++i) {
    float a = glm::radians(-90.0f * i / 4);
    prof.push_back({-rbb + rbb * std::cos(a), -T + rbb + rbb * std::sin(a), std::cos(a), std::sin(a)});
  }
  // top cap
  Vertex c;
  c.pos = vec3(0, 0, 0);
  c.nrm = vec3(0, 1, 0);
  c.ao = 1;
  c.aux = 0;
  m.v.push_back(c);
  for (const PP& q : per) {
    Vertex v = c;
    vec2 pp = q.p - q.n * rb;
    v.pos = vec3(pp.x, 0, pp.y);
    m.v.push_back(v);
  }
  int np = (int)per.size();
  for (int i = 0; i < np; ++i) {
    uint32_t a = 1 + i, b = 1 + (i + 1) % np;
    m.idx.insert(m.idx.end(), {0u, b, a});
  }
  // sides
  uint32_t base = (uint32_t)m.v.size();
  for (const PP& q : per)
    for (const PR& r : prof) {
      Vertex v;
      vec2 pp = q.p + q.n * r.off;
      v.pos = vec3(pp.x, r.y, pp.y);
      v.nrm = glm::normalize(vec3(q.n.x * r.no, r.ny, q.n.y * r.no));
      v.ao = 1;
      v.aux = 0;
      m.v.push_back(v);
    }
  int nr = (int)prof.size();
  for (int i = 0; i < np; ++i) {
    int i2 = (i + 1) % np;
    for (int j = 0; j + 1 < nr; ++j) {
      uint32_t a = base + i * nr + j, b = base + i2 * nr + j, cc = base + i2 * nr + j + 1, d = base + i * nr + j + 1;
      m.idx.insert(m.idx.end(), {a, b, cc, a, cc, d});
    }
  }
  // bottom cap (the board floats, so its underside is on show)
  uint32_t bc = (uint32_t)m.v.size();
  c.pos = vec3(0, -T, 0);
  c.nrm = vec3(0, -1, 0);
  m.v.push_back(c);
  for (const PP& q : per) {
    Vertex v = c;
    vec2 pp = q.p - q.n * rbb;
    v.pos = vec3(pp.x, -T, pp.y);
    m.v.push_back(v);
  }
  for (int i = 0; i < np; ++i) {
    uint32_t a = bc + 1 + i, b = bc + 1 + (i + 1) % np;
    m.idx.insert(m.idx.end(), {bc, a, b});
  }
  // fix winding against normals
  for (size_t i = 0; i < m.idx.size(); i += 3) {
    const vec3& p0 = m.v[m.idx[i]].pos;
    const vec3& p1 = m.v[m.idx[i + 1]].pos;
    const vec3& p2 = m.v[m.idx[i + 2]].pos;
    vec3 fn = glm::cross(p1 - p0, p2 - p0);
    vec3 ns = m.v[m.idx[i]].nrm + m.v[m.idx[i + 1]].nrm + m.v[m.idx[i + 2]].nrm;
    if (glm::dot(fn, ns) < 0) std::swap(m.idx[i + 1], m.idx[i + 2]);
  }
  m.computeBounds();
  return m;
}

void buildLibrary(Library& lib, int quality, BuildProgress* prog) {
  quality = std::max(0, std::min(2, quality));
  const float c0[3] = {0.0125f, 0.0095f, 0.0074f};
  const float c1[3] = {0.022f, 0.018f, 0.015f};
  const float cs[3] = {0.016f, 0.0125f, 0.0105f};
  const int seeds[K_COUNT] = {10, 13, 13, 13, 15, 16};
  int total = K_COUNT * 4 + 3;
  if (prog) {
    prog->total = total;
    prog->done = 0;
  }
  auto tick = [&](const char* s) {
    if (prog) {
      prog->stage = s;
      prog->done++;
    }
  };
  for (int k = 0; k < K_COUNT; ++k) {
    PieceAssets& A = lib.piece[k];
    A.height = pieceHeight(k);
    A.radius = pieceRadius(k);
    Fn F = pieceField(k);
    MeshOptions o;
    pieceBounds(k, o.bmin, o.bmax);
    o.cell = c0[quality];
    o.aoRadius = 0.12f;
    A.lod0 = polygonize(F, o);
    tick("Sculpting marble");
    o.cell = c1[quality];
    o.newtonSteps = 3;
    A.lod1 = polygonize(F, o);
    tick("Sculpting marble");
    vec3 lo, hi;
    pieceBounds(k, lo, hi);
    A.shards = voronoiShatter(F, lo, hi, seeds[k], 1234u + 77u * k, cs[quality]);
    tick("Planning fractures");
    A.sliceN = glm::normalize(vec3(0.78f, 0.62f, 0.0f));
    A.slicePoint = vec3(0, A.height * 0.52f, 0);
    A.halves = sliceHalves(F, lo, hi, A.sliceN, A.slicePoint, cs[quality]);
    tick("Planning fractures");
  }
  {
    MeshOptions o;
    o.cell = quality == 0 ? 0.0055f : 0.0042f;
    o.bmin = vec3(-0.22f, -0.36f, -0.06f);
    o.bmax = vec3(0.22f, 1.02f, 0.06f);
    o.aoRadius = 0.04f;
    o.aux = swordMaterial;
    lib.sword = polygonize(swordSDF, o);
    tick("Forging steel");
  }
  lib.cannonball = makeUVSphere(0.085f, 40, 20);
  tick("Casting iron");
  lib.board = buildBoard(lib.boardHalf, lib.boardThickness);
  tick("Carving the board");
  if (prog) prog->finished = true;
}

}  // namespace meshgen
