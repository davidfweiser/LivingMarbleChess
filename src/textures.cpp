#include "textures.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/glm.hpp>

#include "sdf.h"

namespace tex {
using glm::vec2;
using glm::vec3;

namespace {

inline uint32_t hashU(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
inline float h2(int x, int y, uint32_t seed) {
  return (hashU((uint32_t)x * 1597334677u ^ (uint32_t)y * 3812015801u ^ seed * 2654435769u) & 0xffffff) /
         float(0xffffff);
}
inline float h3(int x, int y, int z, uint32_t seed) {
  return (hashU((uint32_t)x * 1597334677u ^ (uint32_t)y * 3812015801u ^ (uint32_t)z * 2798796415u ^
                seed * 2654435769u) &
          0xffffff) /
         float(0xffffff);
}
inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

float noise2(float x, float y, uint32_t seed) {
  int ix = (int)std::floor(x), iy = (int)std::floor(y);
  float fx = x - ix, fy = y - iy;
  float u = fade(fx), v = fade(fy);
  float a = h2(ix, iy, seed), b = h2(ix + 1, iy, seed), c = h2(ix, iy + 1, seed), d = h2(ix + 1, iy + 1, seed);
  return glm::mix(glm::mix(a, b, u), glm::mix(c, d, u), v);
}
float fbm2(float x, float y, uint32_t seed, int oct = 4) {
  float s = 0, a = 0.5f, n = 0;
  for (int i = 0; i < oct; ++i) {
    s += a * noise2(x, y, seed + i * 31);
    n += a;
    x *= 2.03f;
    y *= 2.03f;
    a *= 0.5f;
  }
  return s / n;
}

// ---------------------------------------------------------------------------
// Stroke font for board coordinates (a-h, 1-8). Glyph box ~ 0.6 x 0.85.
// ---------------------------------------------------------------------------
using Poly = std::vector<vec2>;
using Glyph = std::vector<Poly>;

Poly arc(float cx, float cy, float rx, float ry, float a0, float a1, int n = 24) {
  Poly p;
  for (int i = 0; i <= n; ++i) {
    float a = glm::radians(a0 + (a1 - a0) * i / n);
    p.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
  }
  return p;
}
Poly line(std::initializer_list<vec2> l) { return Poly(l); }

const std::vector<Glyph>& glyphs() {
  static std::vector<Glyph> G = [] {
    std::vector<Glyph> g(16);
    // letters a..h
    g[0] = {arc(0.27f, 0.27f, 0.22f, 0.27f, 0, 360), line({{0.49f, 0.56f}, {0.49f, 0.0f}})};
    g[1] = {line({{0.06f, 0.86f}, {0.06f, 0.0f}}), arc(0.28f, 0.27f, 0.22f, 0.27f, 0, 360)};
    g[2] = {arc(0.29f, 0.27f, 0.235f, 0.27f, 40, 320)};
    g[3] = {arc(0.25f, 0.27f, 0.22f, 0.27f, 0, 360), line({{0.47f, 0.86f}, {0.47f, 0.0f}})};
    g[4] = {line({{0.06f, 0.28f}, {0.52f, 0.28f}}), arc(0.29f, 0.27f, 0.235f, 0.27f, 0, 318)};
    g[5] = {line({{0.20f, 0.0f}, {0.20f, 0.66f}}), arc(0.38f, 0.66f, 0.18f, 0.18f, 180, 30),
            line({{0.04f, 0.52f}, {0.40f, 0.52f}})};
    g[6] = {arc(0.25f, 0.33f, 0.215f, 0.235f, 0, 360), line({{0.465f, 0.57f}, {0.465f, -0.05f}}),
            arc(0.255f, -0.05f, 0.21f, 0.17f, 0, -165)};
    g[7] = {line({{0.06f, 0.86f}, {0.06f, 0.0f}}), arc(0.275f, 0.33f, 0.215f, 0.22f, 180, 0),
            line({{0.49f, 0.33f}, {0.49f, 0.0f}})};
    // digits 1..8
    g[8] = {line({{0.13f, 0.68f}, {0.32f, 0.85f}, {0.32f, 0.0f}}), line({{0.12f, 0.0f}, {0.52f, 0.0f}})};
    {
      Poly p = arc(0.29f, 0.62f, 0.22f, 0.22f, 165, -35);
      p.push_back({0.06f, 0.0f});
      p.push_back({0.53f, 0.0f});
      g[9] = {p};
    }
    g[10] = {arc(0.28f, 0.645f, 0.20f, 0.20f, 150, -90), arc(0.28f, 0.24f, 0.24f, 0.24f, 90, -150)};
    g[11] = {line({{0.40f, 0.0f}, {0.40f, 0.85f}, {0.04f, 0.28f}, {0.56f, 0.28f}})};
    {
      Poly p = line({{0.50f, 0.85f}, {0.11f, 0.85f}, {0.08f, 0.47f}});
      Poly q = arc(0.28f, 0.28f, 0.245f, 0.28f, 135, -150);
      g[12] = {p, q};
    }
    g[13] = {arc(0.29f, 0.27f, 0.23f, 0.27f, 0, 360), arc(0.50f, 0.30f, 0.44f, 0.55f, 180, 100)};
    g[14] = {line({{0.05f, 0.85f}, {0.53f, 0.85f}, {0.20f, 0.0f}})};
    g[15] = {arc(0.28f, 0.645f, 0.19f, 0.20f, 0, 360), arc(0.28f, 0.235f, 0.235f, 0.235f, 0, 360)};
    return g;
  }();
  return G;
}

float distToGlyph(const Glyph& g, vec2 p) {
  float best = 1e9f;
  for (const Poly& pl : g)
    for (size_t i = 0; i + 1 < pl.size(); ++i) {
      vec2 a = pl[i], b = pl[i + 1];
      vec2 pa = p - a, ba = b - a;
      float h = glm::clamp(glm::dot(pa, ba) / std::max(1e-9f, glm::dot(ba, ba)), 0.0f, 1.0f);
      best = std::min(best, glm::length(pa - ba * h));
    }
  return best;
}

struct WoodParams {
  float y0, z0, tilt, freq, angle;
  vec3 early, late;
  uint32_t seed;
};

// returns color (sRGB-ish), and outputs height and roughness
vec3 wood(const WoodParams& w, float u, float v, float& height, float& rough) {
  float ca = std::cos(w.angle), sa = std::sin(w.angle);
  float uu = ca * u - sa * v, vv = sa * u + ca * v;
  float wy = vv + w.y0 + 0.11f * (fbm2(uu * 0.55f, vv * 2.2f, w.seed) - 0.5f);
  float wz = w.z0 + uu * w.tilt + 0.06f * (fbm2(uu * 1.3f + 5.0f, vv * 3.0f, w.seed + 7) - 0.5f);
  float r = std::sqrt(wy * wy + wz * wz);
  float ring = r * w.freq + 0.35f * fbm2(uu * 0.9f, vv * 9.0f, w.seed + 13);
  ring -= std::floor(ring);
  float late = glm::smoothstep(0.50f, 0.86f, ring) * (1.0f - glm::smoothstep(0.90f, 1.0f, ring));
  float fiber = fbm2(uu * 2.5f, vv * 140.0f, w.seed + 21, 3);
  float pn = noise2(uu * 34.0f, vv * 420.0f, w.seed + 33);
  float pore = glm::smoothstep(0.70f, 0.86f, pn);
  float fig = fbm2(uu * 0.35f, vv * 0.8f, w.seed + 41, 3);
  vec3 c = glm::mix(w.early, w.late, late * 0.85f);
  c *= 0.91f + 0.14f * fiber;
  c *= 0.93f + 0.14f * fig;
  c *= 1.0f - 0.30f * pore;
  height = -0.45f * late - 0.9f * pore + 0.15f * fiber;
  rough = 0.38f + 0.18f * pore + 0.06f * late;
  return c;
}

}  // namespace

void bakeBoard(int res, float half, Image& albedo, Image& detail) {
  albedo.w = albedo.h = detail.w = detail.h = res;
  albedo.ch = detail.ch = 4;
  albedo.px.assign((size_t)res * res * 4, 0);
  detail.px.assign((size_t)res * res * 4, 0);
  std::vector<float> height((size_t)res * res, 0), metal((size_t)res * res, 0), cav((size_t)res * res, 1);

  WoodParams sq[64];
  WoodParams frame[4];
  for (int i = 0; i < 64; ++i) {
    int f = i & 7, r = i >> 3;
    bool dark = ((f + r) & 1) == 0;
    WoodParams& w = sq[i];
    w.seed = 1000 + i * 17;
    w.y0 = (h2(i, 1, 99) - 0.5f) * 4.0f;
    w.z0 = 0.25f + h2(i, 2, 99) * 0.9f;
    w.tilt = (h2(i, 3, 99) - 0.5f) * 0.16f;
    w.freq = 15.0f + h2(i, 4, 99) * 8.0f;
    w.angle = (((f + r) & 2) ? 0.0f : 1.5707963f) + (h2(i, 5, 99) - 0.5f) * 0.06f;
    float tone = 0.94f + h2(i, 6, 99) * 0.1f;
    if (dark) {
      w.early = vec3(0.40f, 0.255f, 0.165f) * tone;
      w.late = vec3(0.24f, 0.140f, 0.085f) * tone;
    } else {
      w.early = vec3(0.89f, 0.76f, 0.58f) * tone;
      w.late = vec3(0.78f, 0.61f, 0.42f) * tone;
    }
  }
  for (int i = 0; i < 4; ++i) {
    WoodParams& w = frame[i];
    w.seed = 5000 + i * 101;
    w.y0 = (h2(i, 11, 7) - 0.5f) * 3.0f;
    w.z0 = 0.4f + h2(i, 12, 7) * 1.2f;
    w.tilt = (h2(i, 13, 7) - 0.5f) * 0.05f;
    w.freq = 11.0f + h2(i, 14, 7) * 5.0f;
    w.angle = 0;
    w.early = vec3(0.33f, 0.175f, 0.10f);
    w.late = vec3(0.17f, 0.085f, 0.045f);
  }
  const auto& G = glyphs();
  const float texel = 2.0f * half / res;
  const float lineA = 4.035f, lineAw = 0.011f;
  const float lineB = 4.49f, lineBw = 0.007f;
  const float labelC = 4.30f, labelSize = 0.235f, stroke = 0.022f;

  sdf::parallelFor(res, [&](int j) {
    for (int i = 0; i < res; ++i) {
      float x = -half + (i + 0.5f) * texel;
      float z = -half + (j + 0.5f) * texel;
      float ax = std::fabs(x), az = std::fabs(z);
      float hgt = 0, rough = 0.4f, met = 0, cv = 1;
      vec3 col;
      if (ax < 4.0f && az < 4.0f) {
        int f = std::min(7, (int)std::floor(x + 4.0f));
        int r = std::min(7, (int)std::floor(4.0f - z));
        col = wood(sq[r * 8 + f], x * 1.0f, z * 1.0f, hgt, rough);
        // seams between squares
        float fx = x + 4.0f, fz = z + 4.0f;
        float sx = std::fabs(fx - std::round(fx)), sz = std::fabs(fz - std::round(fz));
        float s = std::min(sx, sz);
        float seam = 1.0f - glm::smoothstep(0.0015f, 0.0045f, s);
        col *= 1.0f - 0.55f * seam;
        hgt -= 1.6f * seam;
        cv = 1.0f - 0.7f * seam;
        rough = glm::mix(rough, 0.8f, seam);
      } else {
        int strip;
        float u, v;
        if (ax > az) {
          strip = x > 0 ? 0 : 1;
          u = z;
          v = x;
        } else {
          strip = z > 0 ? 2 : 3;
          u = x;
          v = z;
        }
        col = wood(frame[strip], u, v, hgt, rough);
        // miter seam at the corners
        float md = std::fabs(ax - az) * 0.7071f;
        float seam = (ax > 4.0f && az > 4.0f) ? 1.0f - glm::smoothstep(0.0015f, 0.004f, md) : 0.0f;
        // seam between playing field and frame
        float edge = std::max(ax, az);
        seam = std::max(seam, 1.0f - glm::smoothstep(0.0015f, 0.0045f, std::fabs(edge - 4.0f)));
        col *= 1.0f - 0.5f * seam;
        hgt -= 1.5f * seam;
        cv = 1.0f - 0.7f * seam;
        // brass inlay lines
        float la = 1.0f - glm::smoothstep(lineAw - texel, lineAw + texel, std::fabs(edge - lineA));
        float lb = 1.0f - glm::smoothstep(lineBw - texel, lineBw + texel, std::fabs(edge - lineB));
        float m = std::max(la, lb);
        // coordinate labels
        float lm = 0;
        auto label = [&](int gi, float cx, float cz, float rot) {
          // local glyph coordinates (rot = 0 for white-facing, pi for black-facing)
          float dx = x - cx, dz = z - cz;
          float c = std::cos(rot), s = std::sin(rot);
          float gx = (c * dx - s * dz) / labelSize + 0.29f;
          float gy = (-(s * dx + c * dz)) / labelSize + 0.42f;
          if (gx < -0.2f || gx > 0.8f || gy < -0.4f || gy > 1.1f) return;
          float d = distToGlyph(G[gi], vec2(gx, gy)) * labelSize;
          lm = std::max(lm, 1.0f - glm::smoothstep(stroke * 0.5f - texel, stroke * 0.5f + texel, d));
        };
        if (az > 4.05f && ax < 4.0f) {
          int f = std::min(7, (int)std::floor(x + 4.0f));
          float cx = f - 3.5f;
          if (z > 0) label(f, cx, labelC, 0.0f);
          else label(f, cx, -labelC, 3.14159265f);
        }
        if (ax > 4.05f && az < 4.0f) {
          int r = std::min(7, (int)std::floor(4.0f - z));
          float cz = 3.5f - r;
          if (x < 0) label(8 + r, -labelC, cz, 0.0f);
          else label(8 + r, labelC, cz, 3.14159265f);
        }
        m = std::max(m, lm);
        if (m > 0) {
          col = glm::mix(col, vec3(0.95f, 0.76f, 0.42f), m);
          rough = glm::mix(rough, 0.28f, m);
          hgt = glm::mix(hgt, -0.3f, m);
          met = m;
        }
      }
      size_t k = (size_t)j * res + i;
      height[k] = hgt;
      metal[k] = met;
      cav[k] = cv;
      uint8_t* a = &albedo.px[k * 4];
      a[0] = (uint8_t)glm::clamp(col.r * 255.0f + 0.5f, 0.0f, 255.0f);
      a[1] = (uint8_t)glm::clamp(col.g * 255.0f + 0.5f, 0.0f, 255.0f);
      a[2] = (uint8_t)glm::clamp(col.b * 255.0f + 0.5f, 0.0f, 255.0f);
      a[3] = (uint8_t)glm::clamp(rough * 255.0f + 0.5f, 0.0f, 255.0f);
    }
  });
  // normals from height
  const float strength = 0.0011f / texel;  // height units -> slope
  sdf::parallelFor(res, [&](int j) {
    for (int i = 0; i < res; ++i) {
      auto H = [&](int x, int y) {
        x = std::max(0, std::min(res - 1, x));
        y = std::max(0, std::min(res - 1, y));
        return height[(size_t)y * res + x];
      };
      float dx = (H(i + 1, j) - H(i - 1, j)) * 0.5f * strength;
      float dz = (H(i, j + 1) - H(i, j - 1)) * 0.5f * strength;
      vec3 n = glm::normalize(vec3(-dx, 1.0f, -dz));
      size_t k = (size_t)j * res + i;
      uint8_t* d = &detail.px[k * 4];
      d[0] = (uint8_t)glm::clamp((n.x * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f);
      d[1] = (uint8_t)glm::clamp((n.z * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f);
      d[2] = (uint8_t)glm::clamp(metal[k] * 255.0f + 0.5f, 0.0f, 255.0f);
      d[3] = (uint8_t)glm::clamp(cav[k] * 255.0f + 0.5f, 0.0f, 255.0f);
    }
  });
}

// ---------------------------------------------------------------------------
void bakeNoise3D(int res, Image& out) {
  out.w = out.h = out.d = res;
  out.ch = 4;
  out.px.assign((size_t)res * res * res * 4, 0);
  auto tileNoise = [](float x, float y, float z, int period, uint32_t seed) {
    int ix = (int)std::floor(x), iy = (int)std::floor(y), iz = (int)std::floor(z);
    float fx = x - ix, fy = y - iy, fz = z - iz;
    float u = fade(fx), v = fade(fy), w = fade(fz);
    auto H = [&](int a, int b, int c) {
      return h3(((a % period) + period) % period, ((b % period) + period) % period, ((c % period) + period) % period,
                seed);
    };
    float n000 = H(ix, iy, iz), n100 = H(ix + 1, iy, iz), n010 = H(ix, iy + 1, iz), n110 = H(ix + 1, iy + 1, iz);
    float n001 = H(ix, iy, iz + 1), n101 = H(ix + 1, iy, iz + 1), n011 = H(ix, iy + 1, iz + 1),
          n111 = H(ix + 1, iy + 1, iz + 1);
    return glm::mix(glm::mix(glm::mix(n000, n100, u), glm::mix(n010, n110, u), v),
                    glm::mix(glm::mix(n001, n101, u), glm::mix(n011, n111, u), v), w);
  };
  const int wc = 6;  // worley cells per axis
  std::vector<vec3> feat(wc * wc * wc);
  for (int i = 0; i < wc * wc * wc; ++i) feat[i] = vec3(h3(i, 0, 0, 777), h3(i, 1, 0, 777), h3(i, 2, 0, 777));
  sdf::parallelFor(res, [&](int z) {
    for (int y = 0; y < res; ++y)
      for (int x = 0; x < res; ++x) {
        float px = (x + 0.5f) / res, py = (y + 0.5f) / res, pz = (z + 0.5f) / res;
        float ch[3];
        for (int c = 0; c < 2; ++c) {
          float s = 0, a = 0.5f, n = 0;
          int per = 4;
          for (int o = 0; o < 4; ++o) {
            s += a * tileNoise(px * per, py * per, pz * per, per, 11 + c * 101 + o * 7);
            n += a;
            a *= 0.5f;
            per *= 2;
          }
          ch[c] = s / n;
        }
        ch[2] = tileNoise(px * 16, py * 16, pz * 16, 16, 555);
        // worley F2 - F1
        float f1 = 1e9f, f2 = 1e9f;
        vec3 p(px * wc, py * wc, pz * wc);
        int cx = (int)std::floor(p.x), cy = (int)std::floor(p.y), cz = (int)std::floor(p.z);
        for (int dz = -1; dz <= 1; ++dz)
          for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
              int gx = cx + dx, gy = cy + dy, gz = cz + dz;
              int wx = ((gx % wc) + wc) % wc, wy = ((gy % wc) + wc) % wc, wz = ((gz % wc) + wc) % wc;
              vec3 fp = vec3((float)gx, (float)gy, (float)gz) + feat[(wz * wc + wy) * wc + wx];
              float d = glm::length(fp - p);
              if (d < f1) {
                f2 = f1;
                f1 = d;
              } else if (d < f2) {
                f2 = d;
              }
            }
        float edge = glm::clamp((f2 - f1) * 1.2f, 0.0f, 1.0f);
        uint8_t* o = &out.px[(((size_t)z * res + y) * res + x) * 4];
        // stretch fbm contrast a bit
        auto q = [](float v) { return (uint8_t)glm::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f); };
        o[0] = q(glm::clamp((ch[0] - 0.5f) * 1.8f + 0.5f, 0.0f, 1.0f));
        o[1] = q(glm::clamp((ch[1] - 0.5f) * 1.8f + 0.5f, 0.0f, 1.0f));
        o[2] = q(ch[2]);
        o[3] = q(edge);
      }
  });
}

}  // namespace tex
