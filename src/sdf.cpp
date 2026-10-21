#include "sdf.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace sdf {

using glm::clamp;
using glm::dot;
using glm::length;
using glm::max;
using glm::min;

float box(const vec3& p, const vec3& b) {
  vec3 q = glm::abs(p) - b;
  return length(glm::max(q, vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
}
float roundBox(const vec3& p, const vec3& b, float r) { return box(p, b - vec3(r)) - r; }
float torus(const vec3& p, float R, float r) {
  vec2 q(std::sqrt(p.x * p.x + p.z * p.z) - R, p.y);
  return length(q) - r;
}
float capsule(const vec3& p, const vec3& a, const vec3& b, float r) {
  vec3 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
  return length(pa - ba * h) - r;
}
float roundCone(const vec3& p, const vec3& a, const vec3& b, float r1, float r2) {
  vec3 ba = b - a;
  float l2 = dot(ba, ba);
  float rr = r1 - r2;
  float a2 = l2 - rr * rr;
  float il2 = 1.0f / l2;
  vec3 pa = p - a;
  float y = dot(pa, ba);
  float z = y - l2;
  vec3 xv = pa * l2 - ba * y;
  float x2 = dot(xv, xv);
  float y2 = y * y * l2;
  float z2 = z * z * l2;
  float k = (rr > 0 ? 1.0f : (rr < 0 ? -1.0f : 0.0f)) * rr * rr * x2;
  float sz = z > 0 ? 1.0f : (z < 0 ? -1.0f : 0.0f);
  float sy = y > 0 ? 1.0f : (y < 0 ? -1.0f : 0.0f);
  if (sz * a2 * z2 > k) return std::sqrt(x2 + z2) * il2 - r2;
  if (sy * a2 * y2 < k) return std::sqrt(x2 + y2) * il2 - r1;
  return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}
float ellipsoid(const vec3& p, const vec3& r) {
  float k0 = length(p / r);
  float k1 = length(p / (r * r));
  if (k1 < 1e-9f) return -std::min(r.x, std::min(r.y, r.z));
  return k0 * (k0 - 1.0f) / k1;
}
float cylinder(const vec3& p, float h, float r) {
  vec2 d = glm::abs(vec2(std::sqrt(p.x * p.x + p.z * p.z), p.y)) - vec2(r, h);
  return std::min(std::max(d.x, d.y), 0.0f) + length(glm::max(d, vec2(0.0f)));
}
float smin(float a, float b, float k) {
  if (k <= 0) return std::min(a, b);
  float h = std::max(k - std::fabs(a - b), 0.0f) / k;
  return std::min(a, b) - h * h * k * 0.25f;
}
float smax(float a, float b, float k) { return -smin(-a, -b, k); }
vec3 rotY(const vec3& p, float a) {
  float c = std::cos(a), s = std::sin(a);
  return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}
vec3 rotX(const vec3& p, float a) {
  float c = std::cos(a), s = std::sin(a);
  return vec3(p.x, c * p.y - s * p.z, s * p.y + c * p.z);
}
vec3 rotZ(const vec3& p, float a) {
  float c = std::cos(a), s = std::sin(a);
  return vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);
}

// ---------------------------------------------------------------------------
void Profile::build(const std::vector<vec2>& outline, int iters) {
  std::vector<vec2> p = outline;
  for (int it = 0; it < iters; ++it) {
    std::vector<vec2> q;
    q.reserve(p.size() * 2);
    q.push_back(p.front());
    for (size_t i = 0; i + 1 < p.size(); ++i) {
      vec2 a = p[i], b = p[i + 1];
      if (i > 0) q.push_back(a * 0.75f + b * 0.25f);
      if (i + 2 < p.size()) q.push_back(a * 0.25f + b * 0.75f);
    }
    q.push_back(p.back());
    p.swap(q);
  }
  pts.clear();
  for (const vec2& v : p)
    if (pts.empty() || length(v - pts.back()) > 1e-6f) pts.push_back(v);
  pts.front().x = 0;
  pts.back().x = 0;
  edges.clear();
  yMin = 1e9f;
  yMax = -1e9f;
  rMax = 0;
  size_t n = pts.size();
  for (size_t i = 0; i < n; ++i) {
    vec2 a = pts[i], b = pts[(i + 1) % n];
    Edge e;
    e.a = a;
    e.e = b - a;
    float l2 = dot(e.e, e.e);
    if (l2 < 1e-14f) continue;
    e.inv = 1.0f / l2;
    e.ymin = std::min(a.y, b.y);
    e.ymax = std::max(a.y, b.y);
    e.outline = (i + 1) < n;  // last edge closes along the axis
    edges.push_back(e);
    yMin = std::min(yMin, a.y);
    yMax = std::max(yMax, a.y);
    rMax = std::max(rMax, a.x);
  }
}

float Profile::dist2D(const vec2& p) const {
  float best2 = 1e30f, best = 1e15f;
  bool inside = false;
  for (const Edge& E : edges) {
    float ay = E.a.y, by = E.a.y + E.e.y;
    if ((ay > p.y) != (by > p.y)) {
      float t = (p.y - ay) / E.e.y;
      float xi = E.a.x + t * E.e.x;
      if (p.x < xi) inside = !inside;
    }
    if (!E.outline) continue;
    if (p.y < E.ymin - best || p.y > E.ymax + best) continue;
    vec2 w = p - E.a;
    float h = clamp(dot(w, E.e) * E.inv, 0.0f, 1.0f);
    vec2 d = w - E.e * h;
    float dd = dot(d, d);
    if (dd < best2) {
      best2 = dd;
      best = std::sqrt(dd);
    }
  }
  return inside ? -best : best;
}

// ---------------------------------------------------------------------------
void parallelFor(int n, const std::function<void(int)>& fn) {
  int nt = (int)std::max(1u, std::thread::hardware_concurrency());
  nt = std::min(nt, 16);
  if (n <= 1 || nt == 1) {
    for (int i = 0; i < n; ++i) fn(i);
    return;
  }
  std::atomic<int> next{0};
  std::vector<std::thread> th;
  for (int t = 0; t < nt; ++t)
    th.emplace_back([&]() {
      for (;;) {
        int i = next.fetch_add(1);
        if (i >= n) break;
        fn(i);
      }
    });
  for (auto& t : th) t.join();
}

void MeshData::computeBounds() {
  bmin = vec3(1e9f);
  bmax = vec3(-1e9f);
  for (const Vertex& x : v) {
    bmin = glm::min(bmin, x.pos);
    bmax = glm::max(bmax, x.pos);
  }
}
void MeshData::translate(const vec3& d) {
  for (Vertex& x : v) x.pos += d;
  bmin += d;
  bmax += d;
}

static inline vec3 gradient(const Fn& f, const vec3& p, float e) {
  return vec3(f(p + vec3(e, 0, 0)) - f(p - vec3(e, 0, 0)), f(p + vec3(0, e, 0)) - f(p - vec3(0, e, 0)),
              f(p + vec3(0, 0, e)) - f(p - vec3(0, 0, e))) /
         (2.0f * e);
}

MeshData polygonize(const Fn& f, const MeshOptions& o) {
  const float h = o.cell;
  const int B = 4;
  vec3 b0 = o.bmin - vec3(2.0f * h);
  vec3 b1 = o.bmax + vec3(2.0f * h);
  auto dimOf = [&](float lo, float hi) {
    int n = (int)std::ceil((hi - lo) / h) + 1;
    return ((n - 1 + B - 1) / B) * B + 1;
  };
  const int nx = dimOf(b0.x, b1.x), ny = dimOf(b0.y, b1.y), nz = dimOf(b0.z, b1.z);
  auto I = [&](int x, int y, int z) { return ((size_t)z * ny + y) * nx + x; };
  auto P = [&](int x, int y, int z) { return b0 + vec3((float)x, (float)y, (float)z) * h; };

  std::vector<float> F((size_t)nx * ny * nz, 1.0f);
  const int cx = (nx - 1) / B, cy = (ny - 1) / B, cz = (nz - 1) / B;
  std::vector<float> C((size_t)(cx + 1) * (cy + 1) * (cz + 1));
  auto CI = [&](int x, int y, int z) { return ((size_t)z * (cy + 1) + y) * (cx + 1) + x; };
  parallelFor(cz + 1, [&](int k) {
    for (int j = 0; j <= cy; ++j)
      for (int i = 0; i <= cx; ++i) C[CI(i, j, k)] = f(P(i * B, j * B, k * B));
  });
  const float thr = 0.8660254f * B * h * 1.6f;
  parallelFor(cz, [&](int bz) {
    for (int by = 0; by < cy; ++by)
      for (int bx = 0; bx < cx; ++bx) {
        float c[8];
        bool far = true;
        int s = 0;
        for (int q = 0; q < 8; ++q) {
          c[q] = C[CI(bx + (q & 1), by + ((q >> 1) & 1), bz + ((q >> 2) & 1))];
          int sg = c[q] < 0 ? -1 : 1;
          if (q == 0) s = sg;
          if (sg != s || std::fabs(c[q]) < thr) far = false;
        }
        int x0 = bx * B, y0 = by * B, z0 = bz * B;
        int x1 = (bx == cx - 1) ? x0 + B : x0 + B - 1;
        int y1 = (by == cy - 1) ? y0 + B : y0 + B - 1;
        int z1 = (bz == cz - 1) ? z0 + B : z0 + B - 1;
        for (int z = z0; z <= z1; ++z)
          for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) F[I(x, y, z)] = far ? (float)s * thr : f(P(x, y, z));
      }
  });

  // --- surface nets: one vertex per sign-changing cell ---
  MeshData out;
  std::vector<int> cellIdx((size_t)(nx - 1) * (ny - 1) * (nz - 1), -1);
  auto CIdx = [&](int x, int y, int z) { return ((size_t)z * (ny - 1) + y) * (nx - 1) + x; };
  static const int edgeA[12] = {0, 2, 4, 6, 0, 1, 4, 5, 0, 1, 2, 3};
  static const int edgeB[12] = {1, 3, 5, 7, 2, 3, 6, 7, 4, 5, 6, 7};
  for (int z = 0; z < nz - 1; ++z)
    for (int y = 0; y < ny - 1; ++y)
      for (int x = 0; x < nx - 1; ++x) {
        float v[8];
        int mask = 0;
        for (int q = 0; q < 8; ++q) {
          v[q] = F[I(x + (q & 1), y + ((q >> 1) & 1), z + ((q >> 2) & 1))];
          if (v[q] < 0) mask |= 1 << q;
        }
        if (mask == 0 || mask == 255) continue;
        vec3 acc(0);
        int cnt = 0;
        for (int e = 0; e < 12; ++e) {
          int a = edgeA[e], b = edgeB[e];
          if (((mask >> a) & 1) == ((mask >> b) & 1)) continue;
          float t = v[a] / (v[a] - v[b]);
          vec3 pa((float)(a & 1), (float)((a >> 1) & 1), (float)((a >> 2) & 1));
          vec3 pb((float)(b & 1), (float)((b >> 1) & 1), (float)((b >> 2) & 1));
          acc += pa + (pb - pa) * t;
          cnt++;
        }
        Vertex vt;
        vt.pos = P(x, y, z) + (acc / (float)cnt) * h;
        vt.nrm = vec3(0, 1, 0);
        vt.ao = 1;
        vt.aux = 0;
        cellIdx[CIdx(x, y, z)] = (int)out.v.size();
        out.v.push_back(vt);
      }

  // --- project vertices onto the surface, compute normals / AO / aux ---
  const Fn& nf = o.normalField ? o.normalField : f;
  const float ge = h * 0.25f;
  parallelFor((int)((out.v.size() + 255) / 256), [&](int blk) {
    size_t i0 = (size_t)blk * 256, i1 = std::min(out.v.size(), i0 + 256);
    for (size_t i = i0; i < i1; ++i) {
      Vertex& vt = out.v[i];
      vec3 p = vt.pos;
      for (int s = 0; s < o.newtonSteps; ++s) {
        float d = nf(p);
        vec3 g = gradient(nf, p, ge);
        float g2 = dot(g, g);
        if (g2 < 1e-8f) break;
        vec3 step = g * (d / g2);
        float sl = length(step);
        if (sl > h * 0.75f) step *= h * 0.75f / sl;
        p -= step;
      }
      if (length(p - vt.pos) < h * 1.5f) vt.pos = p;
      vec3 g = gradient(nf, vt.pos, ge);
      float gl = length(g);
      vt.nrm = gl > 1e-8f ? g / gl : vec3(0, 1, 0);
      if (o.computeAO) {
        static const float dist[5] = {0.05f, 0.14f, 0.3f, 0.55f, 1.0f};
        static const float w[5] = {1.0f, 0.85f, 0.65f, 0.45f, 0.3f};
        float occ = 0, wsum = 0;
        for (int s = 0; s < 5; ++s) {
          float dd = dist[s] * o.aoRadius;
          float fv = f(vt.pos + vt.nrm * dd);
          occ += w[s] * std::max(0.0f, dd - fv) / dd;
          wsum += w[s];
        }
        vt.ao = clamp(1.0f - 1.35f * occ / wsum, 0.0f, 1.0f);
      }
      if (o.aux) vt.aux = o.aux(vt.pos);
      if (o.skin) o.skin(vt);
    }
  });

  // --- quads across sign-changing grid edges ---
  auto emitQuad = [&](int a, int b, int c, int d) {
    if (a < 0 || b < 0 || c < 0 || d < 0) return;
    const vec3 &pa = out.v[a].pos, &pb = out.v[b].pos, &pc = out.v[c].pos, &pd = out.v[d].pos;
    vec3 nsum = out.v[a].nrm + out.v[b].nrm + out.v[c].nrm + out.v[d].nrm;
    // split along the shorter diagonal
    bool diagAC = dot(pa - pc, pa - pc) <= dot(pb - pd, pb - pd);
    uint32_t t[6];
    if (diagAC) {
      uint32_t tt[6] = {(uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)a, (uint32_t)c, (uint32_t)d};
      std::copy(tt, tt + 6, t);
    } else {
      uint32_t tt[6] = {(uint32_t)a, (uint32_t)b, (uint32_t)d, (uint32_t)b, (uint32_t)c, (uint32_t)d};
      std::copy(tt, tt + 6, t);
    }
    for (int k = 0; k < 6; k += 3) {
      const vec3& p0 = out.v[t[k]].pos;
      const vec3& p1 = out.v[t[k + 1]].pos;
      const vec3& p2 = out.v[t[k + 2]].pos;
      vec3 fn = glm::cross(p1 - p0, p2 - p0);
      if (dot(fn, nsum) < 0) std::swap(t[k + 1], t[k + 2]);
      out.idx.push_back(t[k]);
      out.idx.push_back(t[k + 1]);
      out.idx.push_back(t[k + 2]);
    }
  };
  for (int z = 0; z < nz; ++z)
    for (int y = 0; y < ny; ++y)
      for (int x = 0; x < nx; ++x) {
        bool in0 = F[I(x, y, z)] < 0;
        if (x < nx - 1 && in0 != (F[I(x + 1, y, z)] < 0) && y >= 1 && z >= 1 && y < ny - 1 && z < nz - 1)
          emitQuad(cellIdx[CIdx(x, y - 1, z - 1)], cellIdx[CIdx(x, y, z - 1)], cellIdx[CIdx(x, y, z)],
                   cellIdx[CIdx(x, y - 1, z)]);
        if (y < ny - 1 && in0 != (F[I(x, y + 1, z)] < 0) && x >= 1 && z >= 1 && x < nx - 1 && z < nz - 1)
          emitQuad(cellIdx[CIdx(x - 1, y, z - 1)], cellIdx[CIdx(x, y, z - 1)], cellIdx[CIdx(x, y, z)],
                   cellIdx[CIdx(x - 1, y, z)]);
        if (z < nz - 1 && in0 != (F[I(x, y, z + 1)] < 0) && x >= 1 && y >= 1 && x < nx - 1 && y < ny - 1)
          emitQuad(cellIdx[CIdx(x - 1, y - 1, z)], cellIdx[CIdx(x, y - 1, z)], cellIdx[CIdx(x, y, z)],
                   cellIdx[CIdx(x - 1, y, z)]);
      }
  out.computeBounds();
  return out;
}

MeshData makeUVSphere(float r, int seg, int rings) {
  MeshData m;
  for (int j = 0; j <= rings; ++j) {
    float v = (float)j / rings;
    float th = v * 3.14159265f;
    for (int i = 0; i <= seg; ++i) {
      float u = (float)i / seg;
      float ph = u * 6.2831853f;
      vec3 n(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
      Vertex vt;
      vt.pos = n * r;
      vt.nrm = n;
      vt.ao = 1;
      vt.aux = 0;
      vt.bones = 0;
      vt.mat = 0;
      m.v.push_back(vt);
    }
  }
  for (int j = 0; j < rings; ++j)
    for (int i = 0; i < seg; ++i) {
      uint32_t a = j * (seg + 1) + i, b = a + seg + 1;
      m.idx.insert(m.idx.end(), {a, a + 1, b, a + 1, b + 1, b});
    }
  m.computeBounds();
  return m;
}

}  // namespace sdf
