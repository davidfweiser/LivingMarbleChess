// The sculpting kit shared by the skinned armies (medieval, aliens): shapes with
// bounding spheres, a Model of parts blended into one signed distance field and
// bound to bones and materials, skin weights, and meshing a model into its LODs,
// fracture shards and slice halves.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "medieval.h"

namespace sculpt {
using glm::mat3;
using glm::vec2;
using glm::vec3;
using medieval::kMaxBones;
using medieval::Skeleton;
using namespace sdf;

// ---------------------------------------------------------------------------
// Shapes: a distance function plus a bounding sphere for quick rejection
// ---------------------------------------------------------------------------
struct Shape {
  Fn f;
  vec3 c{0};
  float r = -1;  // < 0: no bound
};

inline float cappedCylinder(const vec3& p, const vec3& a, const vec3& b, float r) {
  vec3 ba = b - a, pa = p - a;
  float baba = glm::dot(ba, ba), paba = glm::dot(pa, ba);
  float x = glm::length(pa * baba - ba * paba) - r * baba;
  float y = std::fabs(paba - baba * 0.5f) - baba * 0.5f;
  float x2 = x * x, y2 = y * y * baba;
  float d = std::max(x, y) < 0 ? -std::min(x2, y2) : ((x > 0 ? x2 : 0) + (y > 0 ? y2 : 0));
  return (d < 0 ? -1.0f : 1.0f) * std::sqrt(std::fabs(d)) / baba;
}

// orthonormal frame whose y axis points along 'up' and whose z axis is as close to 'fwd' as possible
inline mat3 frameY(const vec3& up, const vec3& fwd = vec3(0, 0, 1)) {
  vec3 y = glm::normalize(up);
  vec3 f = std::fabs(glm::dot(y, glm::normalize(fwd))) > 0.98f ? vec3(1, 0, 0) : fwd;
  vec3 x = glm::normalize(glm::cross(y, f));
  vec3 z = glm::cross(x, y);
  return mat3(x, y, z);
}
inline mat3 rotXm(float a) { return mat3(vec3(1, 0, 0), vec3(0, std::cos(a), std::sin(a)), vec3(0, -std::sin(a), std::cos(a))); }
inline mat3 rotYm(float a) { return mat3(vec3(std::cos(a), 0, -std::sin(a)), vec3(0, 1, 0), vec3(std::sin(a), 0, std::cos(a))); }
inline mat3 rotZm(float a) { return mat3(vec3(std::cos(a), std::sin(a), 0), vec3(-std::sin(a), std::cos(a), 0), vec3(0, 0, 1)); }

inline Shape sphereS(vec3 c, float r) { return {[c, r](const vec3& p) { return glm::length(p - c) - r; }, c, r}; }
inline Shape capsuleS(vec3 a, vec3 b, float r) {
  return {[a, b, r](const vec3& p) { return capsule(p, a, b, r); }, (a + b) * 0.5f, glm::length(b - a) * 0.5f + r};
}
inline Shape coneS(vec3 a, vec3 b, float ra, float rb) {
  return {[a, b, ra, rb](const vec3& p) { return roundCone(p, a, b, ra, rb); }, (a + b) * 0.5f,
          glm::length(b - a) * 0.5f + std::max(ra, rb)};
}
inline Shape cylS(vec3 a, vec3 b, float r) {
  return {[a, b, r](const vec3& p) { return cappedCylinder(p, a, b, r); }, (a + b) * 0.5f,
          std::sqrt(glm::dot(b - a, b - a) * 0.25f + r * r)};
}
// ellipsoid with local axes R (columns), centre c
inline Shape ellS(vec3 c, vec3 rad, mat3 R = mat3(1.0f)) {
  mat3 Rt = glm::transpose(R);
  return {[c, rad, Rt](const vec3& p) { return ellipsoid(Rt * (p - c), rad); }, c,
          std::max(rad.x, std::max(rad.y, rad.z))};
}
inline Shape boxS(vec3 c, vec3 half, float round, mat3 R = mat3(1.0f)) {
  mat3 Rt = glm::transpose(R);
  return {[c, half, round, Rt](const vec3& p) { return roundBox(Rt * (p - c), half, round); }, c, glm::length(half)};
}
inline Shape torusS(vec3 c, float R, float r, mat3 F = mat3(1.0f)) {  // ring in the frame's xz plane
  mat3 Ft = glm::transpose(F);
  return {[c, R, r, Ft](const vec3& p) { return torus(Ft * (p - c), R, r); }, c, R + r};
}
inline Shape latheS(const Profile* prof, vec3 base) {
  float h = prof->yMax - prof->yMin;
  return {[prof, base](const vec3& p) { return prof->eval(p - base); },
          base + vec3(0, (prof->yMin + prof->yMax) * 0.5f, 0), std::sqrt(prof->rMax * prof->rMax + 0.25f * h * h) + 0.01f};
}
inline Shape fnS(Fn f, vec3 c, float r) { return {std::move(f), c, r}; }
// mirror a shape across x = 0 (the other side of a symmetric figure)
inline Shape mirrorX(const Shape& s) {
  Fn f = s.f;
  return {[f](const vec3& p) { return f(vec3(-p.x, p.y, p.z)); }, vec3(-s.c.x, s.c.y, s.c.z), s.r};
}

// ---------------------------------------------------------------------------
// Model: parts blended into one field, with bones and materials
// ---------------------------------------------------------------------------
struct Model {
  enum Op { ADD, SUB, PAINT };
  struct Part {
    Op op;
    Shape s;
    int bone, mat;
    float k;
    int only;           // PAINT: only over this material (-1 any)
    bool rigid;         // never blended with a neighbouring bone
    int bone2;          // >= 0: stretches between 'bone' and 'bone2'
    Fn w;               // ... with this weight of 'bone' (chains, reins)
  };
  std::vector<Part> parts;
  const Skeleton* skel = nullptr;
  float skinBlend = 0.012f;

  void add(int bone, int mat, float k, const Shape& s) {
    parts.push_back({ADD, s, bone, mat, k, -1, false, -1, nullptr});
  }
  void addR(int bone, int mat, float k, const Shape& s) {
    parts.push_back({ADD, s, bone, mat, k, -1, true, -1, nullptr});
  }
  void addStretch(int bone, int bone2, int mat, float k, const Shape& s, Fn w) {
    parts.push_back({ADD, s, bone, mat, k, -1, false, bone2, std::move(w)});
  }
  // symmetric pair: the shape as given plus its mirror image on the other side
  void add2(int boneL, int boneR, int mat, float k, const Shape& s) {
    add(boneL, mat, k, s);
    add(boneR, mat, k, mirrorX(s));
  }
  void sub(int mat, float k, const Shape& s) { parts.push_back({SUB, s, -1, mat, k, -1, false, -1, nullptr}); }
  void paint(int mat, const Shape& s, int only = -1) {
    parts.push_back({PAINT, s, -1, mat, 0, only, false, -1, nullptr});
  }
  void paint2(int mat, const Shape& s, int only = -1) {
    paint(mat, s, only);
    paint(mat, mirrorX(s), only);
  }

  // Scales the whole model about the origin (its skeleton must be scaled to match).
  void scaleAll(float s) {
    for (Part& q : parts) {
      Fn f = q.s.f;
      q.s.f = [f, s](const vec3& p) { return f(p / s) * s; };
      q.s.c *= s;
      if (q.s.r > 0) q.s.r *= s;
      q.k *= s;
      if (q.w) {
        Fn w = q.w;
        q.w = [w, s](const vec3& p) { return w(p / s); };
      }
    }
    skinBlend *= s;
  }

  // Grows any bounding sphere that does not contain its whole shape (cheap safety net:
  // a sphere that is too small would cull the part where it should still show).
  void fitBounds() {
    for (Part& q : parts) {
      if (q.s.r <= 0) continue;
      for (int iter = 0; iter < 4; ++iter) {
        const int N = 20;
        float ext = q.s.r * 1.7f, far = 0;
        for (int k = 0; k <= N; ++k)
          for (int j = 0; j <= N; ++j)
            for (int i = 0; i <= N; ++i) {
              vec3 p = q.s.c + vec3(i * 2.0f / N - 1, j * 2.0f / N - 1, k * 2.0f / N - 1) * ext;
              if (q.s.f(p) < 0.004f) far = std::max(far, glm::length(p - q.s.c));
            }
        if (far <= q.s.r) break;
        q.s.r = far + ext * 2.0f / N + 0.004f;
      }
    }
  }

  float eval(const vec3& p) const {
    float d = 1e9f;
    for (const Part& q : parts) {
      if (q.op == PAINT) continue;
      if (q.s.r > 0) {
        float bd = glm::length(p - q.s.c) - q.s.r;
        if (q.op == ADD && bd > d + q.k) continue;
        if (q.op == SUB && bd > q.k - d) continue;
      }
      float x = q.s.f(p);
      d = q.op == ADD ? smin(d, x, q.k) : smax(d, -x, q.k);
    }
    return d;
  }
  Fn field() const {
    return [this](const vec3& p) { return eval(p); };
  }

  int materialAt(const vec3& p, float* boneDist, int* nearest) const {
    float best = 1e9f;
    int mat = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
      const Part& q = parts[i];
      if (q.op != ADD) continue;
      float x = q.s.f(p);
      if (x < best) {
        best = x;
        mat = q.mat;
        if (nearest) *nearest = (int)i;
      }
      if (boneDist && q.bone >= 0 && x < boneDist[q.bone]) boneDist[q.bone] = x;
    }
    for (const Part& q : parts) {
      if (q.op != SUB || q.mat < 0) continue;
      if (std::fabs(q.s.f(p)) < 0.006f) mat = q.mat;  // on the carved surface
    }
    for (const Part& q : parts) {
      if (q.op != PAINT) continue;
      if (q.only >= 0 && q.only != mat) continue;
      if (q.s.f(p) < 0) mat = q.mat;
    }
    return mat;
  }
  bool adjacent(int a, int b) const { return skel && (skel->parent[a] == b || skel->parent[b] == a); }
  void skin(Vertex& v) const {
    float bd[kMaxBones];
    for (float& x : bd) x = 1e9f;
    int np = -1;
    int mat = materialAt(v.pos, bd, &np);
    int b0 = 0, b1 = 0;
    float w0 = 1;
    if (skel && skel->count > 0) {
      const Part* near = np >= 0 ? &parts[np] : nullptr;
      if (near && near->bone2 >= 0) {
        b0 = near->bone;
        b1 = near->bone2;
        w0 = glm::clamp(near->w(v.pos), 0.0f, 1.0f);
      } else if (near && near->rigid) {
        b0 = b1 = near->bone;
      } else {
        for (int b = 1; b < skel->count; ++b)
          if (bd[b] < bd[b0]) b0 = b;
        b1 = b0;
        float d1 = 1e9f;
        if (!skel->rigid[b0]) {
          for (int b = 0; b < skel->count; ++b)
            if (b != b0 && !skel->rigid[b] && adjacent(b0, b) && bd[b] < d1) {
              d1 = bd[b];
              b1 = b;
            }
        }
        if (b1 != b0) w0 = 1.0f - 0.5f * std::exp(-std::max(0.0f, d1 - bd[b0]) / skinBlend);
      }
    }
    v.bones = (float)(b0 + 64 * b1);
    v.mat = (float)mat + 0.998f * w0;
  }
  void paintOnly(Vertex& v) const { v.mat = (float)materialAt(v.pos, nullptr, nullptr) + 0.998f; }
};

// weight of the first bone along a segment a->b (1 at a, 0 at b)
inline Fn along(vec3 a, vec3 b) {
  return [a, b](const vec3& p) {
    vec3 ab = b - a;
    return 1.0f - glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
  };
}

// ---------------------------------------------------------------------------
// Joint layouts (the skeletons and the sculpting share them)
// ---------------------------------------------------------------------------
struct BoneDef {
  int parent;
  vec3 joint;
  bool rigid;
};
inline Skeleton makeSkeleton(std::initializer_list<BoneDef> bones) {
  Skeleton s;
  s.count = 0;
  for (const BoneDef& b : bones) {
    s.parent[s.count] = b.parent;
    s.joint[s.count] = b.joint;
    s.rigid[s.count] = b.rigid;
    s.count++;
  }
  return s;
}
inline vec3 mx(const vec3& v) { return vec3(-v.x, v.y, v.z); }
inline Skeleton scaledSkeleton(Skeleton s, float k) {
  for (int b = 0; b < s.count; ++b) s.joint[b] *= k;
  return s;
}

inline float dot2(const vec3& v) { return glm::dot(v, v); }
inline float triangleDist(const vec3& p, const vec3& a, const vec3& b, const vec3& c) {
  vec3 ba = b - a, pa = p - a, cb = c - b, pb = p - b, ac = a - c, pc = p - c;
  vec3 nor = glm::cross(ba, ac);
  auto sgn = [](float x) { return x > 0 ? 1.0f : (x < 0 ? -1.0f : 0.0f); };
  float s = sgn(glm::dot(glm::cross(ba, nor), pa)) + sgn(glm::dot(glm::cross(cb, nor), pb)) +
            sgn(glm::dot(glm::cross(ac, nor), pc));
  float d2;
  if (s < 2.0f)
    d2 = std::min(std::min(dot2(ba * glm::clamp(glm::dot(ba, pa) / dot2(ba), 0.0f, 1.0f) - pa),
                           dot2(cb * glm::clamp(glm::dot(cb, pb) / dot2(cb), 0.0f, 1.0f) - pb)),
                  dot2(ac * glm::clamp(glm::dot(ac, pc) / dot2(ac), 0.0f, 1.0f) - pc));
  else
    d2 = glm::dot(nor, pa) * glm::dot(nor, pa) / dot2(nor);
  return std::sqrt(d2);
}
inline Shape triS(vec3 a, vec3 b, vec3 c, float t) {
  vec3 m = (a + b + c) / 3.0f;
  float r = std::max(glm::length(a - m), std::max(glm::length(b - m), glm::length(c - m))) + t;
  return {[a, b, c, t](const vec3& p) { return triangleDist(p, a, b, c) - t; }, m, r};
}

// Meshes a model: LOD 0 and 1 (skinned and painted), and when seeds > 0 the
// fracture shards and slice halves (painted, rigid).
inline void buildAsset(meshgen::PieceAssets& A, Model& m, vec3 lo, vec3 hi, int quality, int seeds, uint32_t seed,
                float height, float radius) {
  const float c0[4] = {0.0085f, 0.0062f, 0.0048f, 0.0034f};
  const float c1[4] = {0.016f, 0.013f, 0.011f, 0.0085f};
  const float cs[4] = {0.014f, 0.011f, 0.009f, 0.007f};
  m.fitBounds();
  Fn F = m.field();
  MeshOptions o;
  o.bmin = lo;
  o.bmax = hi;
  o.cell = c0[quality];
  o.aoRadius = 0.06f;
  const Model* mp = &m;
  o.skin = [mp](Vertex& v) { mp->skin(v); };
  A.lod0 = polygonize(F, o);
  o.cell = c1[quality];
  o.newtonSteps = 3;
  A.lod1 = polygonize(F, o);
  auto paint = [mp](Vertex& v) { mp->paintOnly(v); };
  if (seeds > 0) {
    A.shards = meshgen::voronoiShatter(F, lo, hi, seeds, seed, cs[quality], paint);
    A.sliceN = glm::normalize(vec3(0.78f, 0.62f, 0.0f));
    A.slicePoint = vec3(0, height * 0.5f, 0);
    A.halves = meshgen::sliceHalves(F, lo, hi, A.sliceN, A.slicePoint, cs[quality], paint);
  }
  A.height = height;
  A.radius = radius;
}

}  // namespace sculpt
