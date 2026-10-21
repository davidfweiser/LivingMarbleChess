// Sculpting the medieval army. Every figure is a list of parts - capsules,
// cones, ellipsoids, boxes and lathe profiles - each bound to a bone and a
// material. The parts are blended into one signed distance field which the
// surface-nets mesher turns into a mesh; afterwards every vertex is skinned
// to the bone(s) of the part(s) it lies on and painted with their material.
#include <algorithm>
#include <cmath>

#include "medieval.h"

namespace medieval {
using glm::mat3;
using glm::vec2;
using glm::vec3;
using namespace sdf;

namespace {

// ---------------------------------------------------------------------------
// Shapes: a distance function plus a bounding sphere for quick rejection
// ---------------------------------------------------------------------------
struct Shape {
  Fn f;
  vec3 c{0};
  float r = -1;  // < 0: no bound
};

float cappedCylinder(const vec3& p, const vec3& a, const vec3& b, float r) {
  vec3 ba = b - a, pa = p - a;
  float baba = glm::dot(ba, ba), paba = glm::dot(pa, ba);
  float x = glm::length(pa * baba - ba * paba) - r * baba;
  float y = std::fabs(paba - baba * 0.5f) - baba * 0.5f;
  float x2 = x * x, y2 = y * y * baba;
  float d = std::max(x, y) < 0 ? -std::min(x2, y2) : ((x > 0 ? x2 : 0) + (y > 0 ? y2 : 0));
  return (d < 0 ? -1.0f : 1.0f) * std::sqrt(std::fabs(d)) / baba;
}

// orthonormal frame whose y axis points along 'up' and whose z axis is as close to 'fwd' as possible
mat3 frameY(const vec3& up, const vec3& fwd = vec3(0, 0, 1)) {
  vec3 y = glm::normalize(up);
  vec3 f = std::fabs(glm::dot(y, glm::normalize(fwd))) > 0.98f ? vec3(1, 0, 0) : fwd;
  vec3 x = glm::normalize(glm::cross(y, f));
  vec3 z = glm::cross(x, y);
  return mat3(x, y, z);
}
mat3 rotXm(float a) { return mat3(vec3(1, 0, 0), vec3(0, std::cos(a), std::sin(a)), vec3(0, -std::sin(a), std::cos(a))); }
mat3 rotYm(float a) { return mat3(vec3(std::cos(a), 0, -std::sin(a)), vec3(0, 1, 0), vec3(std::sin(a), 0, std::cos(a))); }
mat3 rotZm(float a) { return mat3(vec3(std::cos(a), std::sin(a), 0), vec3(-std::sin(a), std::cos(a), 0), vec3(0, 0, 1)); }

Shape sphereS(vec3 c, float r) { return {[c, r](const vec3& p) { return glm::length(p - c) - r; }, c, r}; }
Shape capsuleS(vec3 a, vec3 b, float r) {
  return {[a, b, r](const vec3& p) { return capsule(p, a, b, r); }, (a + b) * 0.5f, glm::length(b - a) * 0.5f + r};
}
Shape coneS(vec3 a, vec3 b, float ra, float rb) {
  return {[a, b, ra, rb](const vec3& p) { return roundCone(p, a, b, ra, rb); }, (a + b) * 0.5f,
          glm::length(b - a) * 0.5f + std::max(ra, rb)};
}
Shape cylS(vec3 a, vec3 b, float r) {
  return {[a, b, r](const vec3& p) { return cappedCylinder(p, a, b, r); }, (a + b) * 0.5f,
          std::sqrt(glm::dot(b - a, b - a) * 0.25f + r * r)};
}
// ellipsoid with local axes R (columns), centre c
Shape ellS(vec3 c, vec3 rad, mat3 R = mat3(1.0f)) {
  mat3 Rt = glm::transpose(R);
  return {[c, rad, Rt](const vec3& p) { return ellipsoid(Rt * (p - c), rad); }, c,
          std::max(rad.x, std::max(rad.y, rad.z))};
}
Shape boxS(vec3 c, vec3 half, float round, mat3 R = mat3(1.0f)) {
  mat3 Rt = glm::transpose(R);
  return {[c, half, round, Rt](const vec3& p) { return roundBox(Rt * (p - c), half, round); }, c, glm::length(half)};
}
Shape torusS(vec3 c, float R, float r, mat3 F = mat3(1.0f)) {  // ring in the frame's xz plane
  mat3 Ft = glm::transpose(F);
  return {[c, R, r, Ft](const vec3& p) { return torus(Ft * (p - c), R, r); }, c, R + r};
}
Shape latheS(const Profile* prof, vec3 base) {
  float h = prof->yMax - prof->yMin;
  return {[prof, base](const vec3& p) { return prof->eval(p - base); },
          base + vec3(0, (prof->yMin + prof->yMax) * 0.5f, 0), std::sqrt(prof->rMax * prof->rMax + 0.25f * h * h) + 0.01f};
}
Shape fnS(Fn f, vec3 c, float r) { return {std::move(f), c, r}; }
// mirror a shape across x = 0 (the other side of a symmetric figure)
Shape mirrorX(const Shape& s) {
  Fn f = s.f;
  return {[f](const vec3& p) { return f(vec3(-p.x, p.y, p.z)); }, vec3(-s.c.x, s.c.y, s.c.z), s.r};
}

// 2D kite / heater shield outline in (u across, v up), negative inside
float shield2D(vec2 q, float w, float h, float point) {
  // rounded top + taper to a point at the bottom
  float top = h * 0.5f;
  float u = std::fabs(q.x);
  float v = q.y;
  // straight-ish sides that curve into the point
  float t = glm::clamp((top - v) / h, 0.0f, 1.0f);
  float halfW = w * 0.5f * std::pow(std::max(0.0f, 1.0f - std::pow(t, point)), 0.5f);
  float side = u - halfW;
  float lid = v - top;
  float bottom = -(v + h * 0.5f);
  return std::max(std::max(side, lid), bottom) * 0.8f;
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
Fn along(vec3 a, vec3 b) {
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
Skeleton makeSkeleton(std::initializer_list<BoneDef> bones) {
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

struct HumanJ {
  vec3 root, waist, neck, hipL, kneeL, ankleL, shL, elL, wrL, shR, elR, wrR, gripR, gripL;
  float S = 1;
};

// The man-at-arms (pawn): standing, 0.84 tall.
HumanJ pawnJ() {
  HumanJ j;
  j.root = vec3(0, 0.42f, 0);
  j.waist = vec3(0, 0.48f, 0);
  j.neck = vec3(0, 0.655f, 0.005f);
  j.hipL = vec3(0.048f, 0.40f, 0);
  j.kneeL = vec3(0.05f, 0.22f, 0.008f);
  j.ankleL = vec3(0.05f, 0.05f, -0.004f);
  j.shL = vec3(0.098f, 0.618f, -0.004f);
  j.elL = vec3(0.122f, 0.478f, -0.012f);
  j.wrL = vec3(0.118f, 0.462f, 0.105f);
  j.shR = vec3(-0.098f, 0.618f, -0.004f);
  j.elR = vec3(-0.12f, 0.476f, -0.012f);
  j.wrR = vec3(-0.114f, 0.46f, 0.108f);
  j.gripR = vec3(-0.112f, 0.462f, 0.128f);
  j.gripL = vec3(0.122f, 0.466f, 0.085f);
  j.S = 1;
  return j;
}

// Throned figures, seated (the rest pose). The king is the largest; the queen and
// the bishop are the same figure a little smaller, with their own arms.
float seatedScale(int kind) { return kind == A_QUEEN ? 0.93f : (kind == A_BISHOP ? 0.95f : 1.0f); }
HumanJ seatedJ(int kind) {
  HumanJ j;
  j.root = vec3(0, 0.36f, -0.07f);
  j.waist = vec3(0, 0.44f, -0.08f);
  j.neck = vec3(0, 0.80f, -0.08f);
  j.hipL = vec3(0.068f, 0.345f, -0.06f);
  j.kneeL = vec3(0.075f, 0.335f, 0.20f);
  j.ankleL = vec3(0.08f, 0.065f, 0.215f);
  j.shL = vec3(0.13f, 0.755f, -0.09f);
  j.shR = vec3(-0.13f, 0.755f, -0.09f);
  j.elL = vec3(0.168f, 0.555f, -0.045f);
  j.wrL = vec3(0.14f, 0.445f, 0.10f);
  j.gripL = vec3(0.13f, 0.435f, 0.14f);
  if (kind == A_KING) {
    j.elR = vec3(-0.168f, 0.555f, -0.045f);
    j.wrR = vec3(-0.13f, 0.445f, 0.105f);
    j.gripR = vec3(-0.115f, 0.43f, 0.142f);
  } else if (kind == A_QUEEN) {  // sceptre held upright
    j.elR = vec3(-0.172f, 0.56f, -0.05f);
    j.wrR = vec3(-0.152f, 0.585f, 0.085f);
    j.gripR = vec3(-0.148f, 0.595f, 0.112f);
  } else {  // bishop: right hand raised in blessing, crozier in the left
    j.elR = vec3(-0.175f, 0.565f, -0.035f);
    j.wrR = vec3(-0.158f, 0.745f, 0.045f);
    j.gripR = vec3(-0.155f, 0.79f, 0.055f);
    j.elL = vec3(0.172f, 0.56f, -0.035f);
    j.wrL = vec3(0.162f, 0.5f, 0.1f);
    j.gripL = vec3(0.16f, 0.505f, 0.128f);
  }
  float S = seatedScale(kind);
  for (vec3* v : {&j.root, &j.waist, &j.neck, &j.hipL, &j.kneeL, &j.ankleL, &j.shL, &j.elL, &j.wrL, &j.shR, &j.elR,
                  &j.wrR, &j.gripR, &j.gripL})
    *v *= S;
  j.S = S;
  return j;
}

Skeleton humanSkel(const HumanJ& j) {
  return makeSkeleton({
      {-1, j.root, false},
      {HB_ROOT, j.waist, false},
      {HB_CHEST, j.neck, false},
      {HB_ROOT, j.hipL, false},
      {HB_LTHIGH, j.kneeL, false},
      {HB_ROOT, mx(j.hipL), false},
      {HB_RTHIGH, mx(j.kneeL), false},
      {HB_CHEST, j.shL, false},
      {HB_LARM, j.elL, false},
      {HB_CHEST, j.shR, false},
      {HB_RARM, j.elR, false},
      {HB_RFORE, j.gripR, true},
      {HB_LFORE, j.gripL, true},
  });
}

// The knight's horse is sculpted in "horse units" and shrunk to fit a square.
const float KS = 0.82f;
inline vec3 K(float x, float y, float z) { return vec3(x, y, z) * KS; }
namespace kj {
const vec3 body = K(0, 0.53f, 0), neck = K(0, 0.62f, 0.24f), head = K(0, 0.88f, 0.40f), tail = K(0, 0.67f, -0.35f);
const vec3 flu = K(0.075f, 0.48f, 0.22f), fll = K(0.075f, 0.27f, 0.23f);
const vec3 hlu = K(0.08f, 0.53f, -0.22f), hll = K(0.08f, 0.27f, -0.30f);
const vec3 rider = K(0, 0.78f, -0.04f), rhead = K(0, 1.03f, -0.04f);
const vec3 shR = K(-0.11f, 1.0f, -0.05f), elR = K(-0.15f, 0.87f, 0.0f), wrR = K(-0.13f, 0.83f, 0.11f);
const vec3 shL = K(0.11f, 1.0f, -0.05f), elL = K(0.15f, 0.87f, 0.0f), wrL = K(0.11f, 0.82f, 0.10f);
const vec3 gripR = K(-0.13f, 0.83f, 0.13f), gripL = K(0.10f, 0.815f, 0.12f);
}  // namespace kj

// The gatehouse. Its legs are a separate mesh sharing the skeleton; they are
// sculpted fully out (hips just under the floor of the tower).
namespace cj {
const vec3 bridge(0, 0.012f, 0.174f), gate(0, 0.0f, 0.11f), flag(0.215f, 1.2f, 0.1f);
const vec3 hipL(0.115f, 0.02f, -0.04f), kneeL(0.125f, -0.17f, 0.0f), ankleL(0.125f, -0.35f, -0.03f);
const float legH = 0.42f;  // how far the tower rises when it stands up
}  // namespace cj

// The dragon: body centred over the origin, facing +z, about 1.4 long.
namespace dj {
const vec3 body(0, 0.34f, 0), neck1(0, 0.42f, 0.2f), neck2(0, 0.5f, 0.32f), neck3(0, 0.6f, 0.42f),
    head(0, 0.67f, 0.5f), jaw(0, 0.64f, 0.56f);
const vec3 tail1(0, 0.33f, -0.22f), tail2(0, 0.27f, -0.42f), tail3(0, 0.2f, -0.6f), tail4(0, 0.13f, -0.76f);
const vec3 wingL1(0.1f, 0.46f, 0.1f), wingL2(0.38f, 0.62f, 0.02f);
const vec3 fl(0.11f, 0.3f, 0.14f), hl(0.12f, 0.3f, -0.12f);
}  // namespace dj

}  // namespace

// ===========================================================================
// Skeletons
// ===========================================================================
const Skeleton& skeleton(int asset) {
  static const Skeleton pawn = humanSkel(pawnJ());
  static const Skeleton bishop = humanSkel(seatedJ(A_BISHOP));
  static const Skeleton queen = humanSkel(seatedJ(A_QUEEN));
  static const Skeleton king = humanSkel(seatedJ(A_KING));
  static const Skeleton knight = [] {
    using namespace kj;
    return makeSkeleton({
        {-1, body, false},
        {KB_BODY, neck, false},
        {KB_NECK, head, false},
        {KB_BODY, tail, false},
        {KB_BODY, flu, false},
        {KB_FLU, fll, false},
        {KB_BODY, mx(flu), false},
        {KB_FRU, mx(fll), false},
        {KB_BODY, hlu, false},
        {KB_HLU, hll, false},
        {KB_BODY, mx(hlu), false},
        {KB_HRU, mx(hll), false},
        {KB_BODY, rider, false},
        {KB_RIDER, rhead, false},
        {KB_RIDER, shR, false},
        {KB_RARM, elR, false},
        {KB_RIDER, shL, false},
        {KB_LARM, elL, false},
        {KB_RFORE, gripR, true},
    });
  }();
  static const Skeleton castle = [] {
    using namespace cj;
    return makeSkeleton({
        {-1, vec3(0), false},
        {CB_BODY, bridge, true},
        {CB_BODY, gate, true},
        {CB_BODY, flag, true},
        {CB_BODY, hipL, false},
        {CB_LTHIGH, kneeL, false},
        {CB_BODY, mx(hipL), false},
        {CB_RTHIGH, mx(kneeL), false},
    });
  }();
  static const Skeleton dragon = [] {
    using namespace dj;
    return makeSkeleton({
        {-1, body, false},
        {DB_BODY, neck1, false},
        {DB_NECK1, neck2, false},
        {DB_NECK2, neck3, false},
        {DB_NECK3, head, false},
        {DB_HEAD, jaw, false},
        {DB_BODY, tail1, false},
        {DB_TAIL1, tail2, false},
        {DB_TAIL2, tail3, false},
        {DB_TAIL3, tail4, false},
        {DB_BODY, wingL1, false},
        {DB_LWING1, wingL2, false},
        {DB_BODY, mx(wingL1), false},
        {DB_RWING1, mx(wingL2), false},
        {DB_BODY, fl, false},
        {DB_BODY, mx(fl), false},
        {DB_BODY, hl, false},
        {DB_BODY, mx(hl), false},
    });
  }();
  static const Skeleton none = makeSkeleton({});
  switch (asset) {
    case A_PAWN: return pawn;
    case A_KNIGHT: return knight;
    case A_BISHOP: return bishop;
    case A_ROOK:
    case A_LEGS: return castle;
    case A_QUEEN: return queen;
    case A_KING: return king;
    case A_DRAGON: return dragon;
    default: return none;
  }
}

namespace {

// ---------------------------------------------------------------------------
// Shared body parts
// ---------------------------------------------------------------------------
// A head facing +z: skull, jaw, nose, brows, ears, eyes and mouth.
// beard: 0 none, 1 short, 2 full and long
void addHead(Model& m, int bone, vec3 c, float s, int beard, int hairMat) {
  m.add(bone, MT_SKIN, 0.0f, ellS(c, vec3(0.044f, 0.054f, 0.049f) * s));
  m.add(bone, MT_SKIN, 0.018f * s, ellS(c + vec3(0, -0.026f, 0.012f) * s, vec3(0.036f, 0.032f, 0.038f) * s));  // jaw
  m.add(bone, MT_SKIN, 0.008f * s,
        coneS(c + vec3(0, 0.006f, 0.046f) * s, c + vec3(0, -0.012f, 0.058f) * s, 0.006f * s, 0.009f * s));  // nose
  m.add(bone, MT_SKIN, 0.01f * s, ellS(c + vec3(0, 0.014f, 0.038f) * s, vec3(0.034f, 0.008f, 0.012f) * s));  // brow
  m.add2(bone, bone, MT_SKIN, 0.006f * s, ellS(c + vec3(0.045f, 0.0f, 0.0f) * s, vec3(0.007f, 0.014f, 0.01f) * s));
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 e = c + vec3(sd * 0.017f, 0.004f, 0.041f) * s;
    m.sub(MT_SKIN, 0.006f * s, sphereS(e + vec3(0, 0, 0.004f) * s, 0.008f * s));
    m.add(bone, MT_DARK, 0.002f * s, sphereS(e - vec3(0, 0, 0.002f) * s, 0.0065f * s));
  }
  m.paint(MT_DARK, ellS(c + vec3(0, -0.028f, 0.05f) * s, vec3(0.012f, 0.0025f, 0.02f) * s), MT_SKIN);  // mouth
  if (beard > 0) {
    float L = beard == 2 ? 1.7f : 1.0f;
    m.add(bone, hairMat, 0.012f * s,
          ellS(c + vec3(0, -0.036f - 0.012f * (L - 1), 0.022f + 0.004f * (L - 1)) * s,
               vec3(0.036f, 0.03f * L, 0.03f) * s));
    m.add(bone, hairMat, 0.006f * s,
          ellS(c + vec3(0, -0.018f, 0.047f) * s, vec3(0.02f, 0.006f, 0.008f) * s, rotXm(-0.2f)));
  }
}

// hair covering the back and top of the skull
void addHair(Model& m, int bone, vec3 c, float s, int mat, float len) {
  m.add(bone, mat, 0.01f * s, fnS([c, s](const vec3& p) {
          float e = ellipsoid(p - (c + vec3(0, 0.006f, -0.006f) * s), vec3(0.048f, 0.058f, 0.053f) * s);
          // only behind the face
          return std::max(e, (p.z - c.z) - 0.018f * s + 0.03f * std::max(0.0f, (p.y - c.y) / s - 0.01f) * s);
        }, c, 0.07f * s));
  if (len > 0)
    m.add(bone, mat, 0.02f * s, ellS(c + vec3(0, -0.04f * len, -0.03f) * s, vec3(0.05f, 0.05f * len, 0.03f) * s));
}

// A hand closed around a grip along 'axis' (fist), centred at c.
void addFist(Model& m, int bone, vec3 c, vec3 axis, float s, int mat = MT_SKIN) {
  mat3 F = frameY(axis);
  m.add(bone, mat, 0.008f * s, ellS(c, vec3(0.019f, 0.024f, 0.02f) * s, F));
}

void addArm(Model& m, bool left, const HumanJ& j, int mat, float rU, float rF, int cuffMat) {
  vec3 sh = left ? j.shL : j.shR, el = left ? j.elL : j.elR, wr = left ? j.wrL : j.wrR;
  int ua = left ? HB_LARM : HB_RARM, fa = left ? HB_LFORE : HB_RFORE;
  float S = j.S;
  vec3 d = glm::normalize(wr - el);
  m.add(ua, mat, 0.02f * S, coneS(sh, el, rU, rU * 0.82f));
  m.add(fa, mat, 0.012f * S, coneS(el, wr - d * 0.02f * S, rF, rF * 0.82f));
  if (cuffMat >= 0) m.add(fa, cuffMat, 0.004f * S, coneS(wr - d * 0.034f * S, wr - d * 0.014f * S, rF * 0.95f, rF * 0.9f));
}

// gold crown band with points (fleurons) and gems; 'arches' closes it into an imperial crown
void addCrown(Model& m, int bone, vec3 c, float r, float h, int points, bool arches) {
  m.add(bone, MT_GOLD, 0.0f, fnS([c, r, h](const vec3& p) {
          vec3 q = p - c;
          float rr = std::sqrt(q.x * q.x + q.z * q.z);
          float ring = std::fabs(rr - r) - 0.006f;
          return std::max(ring, std::fabs(q.y) - h * 0.5f);
        }, c, r + h));
  for (int i = 0; i < points; ++i) {
    float a = 6.2831853f * (i + 0.5f) / points;
    vec3 d(std::sin(a), 0, std::cos(a));
    bool tall = i % 2 == 0;
    vec3 base = c + d * r + vec3(0, h * 0.5f, 0);
    float ph = tall ? h * 1.25f : h * 0.6f;
    m.add(bone, MT_GOLD, 0.004f, coneS(base, base + vec3(0, ph, 0) - d * 0.004f, 0.012f, 0.004f));
    if (tall) {
      // fleur-de-lis: a centre petal and two curling side leaves
      vec3 top = base + vec3(0, ph, 0) - d * 0.004f;
      vec3 side(d.z, 0, -d.x);
      m.add(bone, MT_GOLD, 0.004f, sphereS(top, 0.009f));
      m.add(bone, MT_GOLD, 0.004f, capsuleS(base + vec3(0, ph * 0.45f, 0), base + vec3(0, ph * 0.7f, 0) + side * 0.016f, 0.0045f));
      m.add(bone, MT_GOLD, 0.004f, capsuleS(base + vec3(0, ph * 0.45f, 0), base + vec3(0, ph * 0.7f, 0) - side * 0.016f, 0.0045f));
    } else {
      m.add(bone, MT_GOLD, 0.003f, sphereS(base + vec3(0, ph, 0), 0.007f));
    }
    // gems on the band
    vec3 g = c + d * (r + 0.005f);
    m.add(bone, tall ? MT_GEM : MT_GOLD, 0.002f, sphereS(g, tall ? 0.0085f : 0.005f));
  }
  if (arches) {
    for (int k = 0; k < 2; ++k) {
      mat3 F = k == 0 ? mat3(1.0f) : rotYm(1.5708f);
      Fn arch = [c, r, h, F](const vec3& p) {
        vec3 q = glm::transpose(F) * (p - c - vec3(0, h * 0.5f, 0));
        float e = std::sqrt(q.x * q.x / (r * r) + q.y * q.y / (r * r * 0.81f)) * r - r;  // half ellipse over the band
        return std::max(std::max(std::fabs(e) - 0.005f, std::fabs(q.z) - 0.007f), -q.y);
      };
      m.add(bone, MT_GOLD, 0.003f, fnS(arch, c + vec3(0, h, 0), r + 0.02f));
    }
    vec3 top = c + vec3(0, h * 0.5f + r * 0.9f, 0);
    m.add(bone, MT_GOLD, 0.004f, sphereS(top + vec3(0, 0.012f, 0), 0.013f));
    m.add(bone, MT_GOLD, 0.002f, boxS(top + vec3(0, 0.04f, 0), vec3(0.0035f, 0.017f, 0.0035f), 0.001f));
    m.add(bone, MT_GOLD, 0.002f, boxS(top + vec3(0, 0.045f, 0), vec3(0.011f, 0.0035f, 0.0035f), 0.001f));
    // velvet cap inside the arches
    m.add(bone, MT_VELVET, 0.006f, fnS([c, r, h](const vec3& p) {
            float e = ellipsoid(p - (c + vec3(0, h * 0.5f, 0)), vec3(r - 0.004f, r * 0.8f, r - 0.004f));
            return std::max(e, c.y - p.y);
          }, c + vec3(0, h, 0), r));
  }
}

// ---------------------------------------------------------------------------
// The pieces
// ---------------------------------------------------------------------------
// Pawn: a man-at-arms in a kettle hat with a spear and a kite shield.
Model pawnModel() {
  HumanJ j = pawnJ();
  Model m;
  m.skel = &skeleton(A_PAWN);
  // --- legs: hose and boots
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int th = sd > 0 ? HB_LTHIGH : HB_RTHIGH, sh = sd > 0 ? HB_LSHIN : HB_RSHIN;
    vec3 hip(j.hipL.x * x, j.hipL.y, j.hipL.z), knee(j.kneeL.x * x, j.kneeL.y, j.kneeL.z),
        ankle(j.ankleL.x * x, j.ankleL.y, j.ankleL.z);
    m.add(th, MT_CLOTH2, 0.0f, coneS(hip, knee, 0.038f, 0.028f));
    m.add(sh, MT_CLOTH2, 0.012f, coneS(knee, ankle + vec3(0, 0.04f, 0), 0.028f, 0.021f));
    m.add(sh, MT_LEATHER, 0.008f, coneS(ankle + vec3(0, 0.07f, 0), ankle, 0.026f, 0.025f));
    m.add(sh, MT_LEATHER, 0.012f, boxS(vec3(ankle.x, 0.022f, 0.025f), vec3(0.024f, 0.02f, 0.058f), 0.016f));
    m.add(sh, MT_LEATHER, 0.0f, boxS(vec3(ankle.x, 0.006f, 0.022f), vec3(0.026f, 0.006f, 0.062f), 0.005f));  // sole
  }
  // --- body: quilted gambeson with a flared, pleated skirt; a tabard hangs over it as a shell
  {
    static Profile skirt;
    static bool once = [] {
      skirt.build({{0, 0.262f}, {0.094f, 0.262f}, {0.101f, 0.27f}, {0.096f, 0.30f}, {0.084f, 0.38f}, {0.078f, 0.45f},
                   {0.074f, 0.50f}, {0, 0.50f}},
                  3);
      return true;
    }();
    (void)once;
    Fn skirtF = [](const vec3& p) {
      float a = std::atan2(p.x, p.z);
      float folds = 0.0035f * std::sin(a * 13.0f) * glm::clamp((0.40f - p.y) / 0.12f, 0.0f, 1.0f);
      return skirt.eval(p) - folds;
    };
    Fn chestF = [](const vec3& p) {
      float c = ellipsoid(p - vec3(0, 0.555f, 0.002f), vec3(0.084f, 0.085f, 0.056f));
      return smin(c, ellipsoid(p - vec3(0, 0.598f, -0.004f), vec3(0.094f, 0.045f, 0.052f)), 0.03f);
    };
    m.add(HB_ROOT, MT_CLOTH2, 0.0f, fnS(skirtF, vec3(0, 0.38f, 0), 0.16f));
    m.add(HB_CHEST, MT_CLOTH2, 0.04f, fnS(chestF, vec3(0, 0.57f, 0), 0.12f));
    for (int i = 0; i < 4; ++i) {  // quilting
      float y = 0.31f + i * 0.05f;
      m.sub(-1, 0.003f, torusS(vec3(0, y, 0), 0.094f - (y > 0.42f ? 0.012f : 0.0f) + (y < 0.33f ? 0.004f : 0.0f),
                               0.0028f));
    }
    for (int side = 0; side < 2; ++side) {
      int bone = side == 0 ? HB_CHEST : HB_ROOT;
      Fn shell = [skirtF, chestF, side](const vec3& p) {
        float body = side == 0 ? chestF(p) : skirtF(p);
        float d = body - 0.0045f;
        float panel = std::fabs(p.x) - 0.064f;
        float y0 = side == 0 ? 0.43f : 0.275f, y1 = side == 0 ? 0.645f : 0.48f;
        float ylim = std::max(y0 - p.y, p.y - y1);
        return std::max(std::max(d, panel), ylim);
      };
      m.add(bone, MT_CLOTH, 0.004f, fnS(shell, vec3(0, side == 0 ? 0.56f : 0.37f, 0), 0.15f));
    }
  }
  m.paint(MT_CLOTH2, boxS(vec3(0, 0.525f, 0.08f), vec3(0.012f, 0.062f, 0.05f), 0.0f), MT_CLOTH);
  m.paint(MT_CLOTH2, boxS(vec3(0, 0.54f, 0.08f), vec3(0.046f, 0.012f, 0.05f), 0.0f), MT_CLOTH);
  m.add(HB_ROOT, MT_LEATHER, 0.004f, torusS(vec3(0, 0.445f, 0.004f), 0.083f, 0.008f));
  m.add(HB_ROOT, MT_GOLD, 0.002f, boxS(vec3(0, 0.445f, 0.09f), vec3(0.012f, 0.011f, 0.005f), 0.003f));
  m.add(HB_CHEST, MT_MAIL, 0.016f, coneS(vec3(0, 0.62f, -0.006f), vec3(0, 0.676f, -0.004f), 0.072f, 0.034f));
  // --- arms: quilted sleeves, mail cuffs, fists
  addArm(m, true, j, MT_CLOTH2, 0.033f, 0.025f, MT_MAIL);
  addArm(m, false, j, MT_CLOTH2, 0.033f, 0.025f, MT_MAIL);
  addFist(m, HB_RFORE, j.gripR, vec3(0, 1, 0), 1.0f);
  addFist(m, HB_LFORE, j.gripL, vec3(0, 0.3f, 1), 1.0f);
  // --- head with a kettle hat
  addHead(m, HB_HEAD, vec3(0, 0.728f, 0.012f), 1.0f, 1, MT_HAIR);
  m.add(HB_HEAD, MT_STEEL, 0.004f, fnS([](const vec3& p) {
          float e = ellipsoid(p - vec3(0, 0.762f, 0.006f), vec3(0.056f, 0.062f, 0.06f));
          return std::max(e, 0.772f - p.y);
        }, vec3(0, 0.79f, 0.006f), 0.07f));
  m.add(HB_HEAD, MT_STEEL, 0.006f, fnS([](const vec3& p) {
          vec3 q = p - vec3(0, 0.776f, 0.006f);
          float r = std::sqrt(q.x * q.x + q.z * q.z);
          float y = q.y + 0.12f * std::max(0.0f, r - 0.05f);
          vec2 d(std::max(0.0f, r - 0.092f), std::fabs(y) - 0.0035f);
          return std::min(std::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, vec2(0.0f))) - 0.002f;
        }, vec3(0, 0.776f, 0.006f), 0.1f));
  m.add(HB_HEAD, MT_STEEL, 0.004f, capsuleS(vec3(0, 0.79f, -0.05f), vec3(0, 0.83f, 0.03f), 0.004f));
  // --- spear (right hand), butt on the ground
  {
    vec3 a(j.gripR.x, 0.006f, j.gripR.z), b(j.gripR.x, 0.97f, j.gripR.z);
    m.addR(HB_ITEMR, MT_WOOD, 0.0f, cylS(a, b, 0.0075f));
    m.addR(HB_ITEMR, MT_IRON, 0.002f, cylS(a, a + vec3(0, 0.025f, 0), 0.0085f));
    m.addR(HB_ITEMR, MT_IRON, 0.002f, cylS(b - vec3(0, 0.02f, 0), b + vec3(0, 0.012f, 0), 0.0095f));
    m.addR(HB_ITEMR, MT_STEEL, 0.0f, fnS([b](const vec3& p) {
             vec3 q = p - (b + vec3(0, 0.01f, 0));
             float t = glm::clamp(q.y / 0.095f, 0.0f, 1.0f);
             float w = 0.019f * std::sin(std::pow(t, 0.7f) * 3.14159f) + 0.001f;
             float th = 0.0035f * (1.0f - t) + 0.001f;
             float d = std::max(std::fabs(q.x) / w + std::fabs(q.z) / th - 1.0f, 0.0f) * std::min(w, th);
             float body = std::max(std::fabs(q.x) - w, std::fabs(q.z) - th);
             return std::max(std::max(body, d), std::max(-q.y, q.y - 0.095f));
           }, b + vec3(0, 0.06f, 0), 0.07f));
  }
  // --- kite shield on the left forearm
  {
    vec3 c(0.168f, 0.4f, 0.075f);
    mat3 R = rotYm(0.62f);
    mat3 Rt = glm::transpose(R);
    Fn plate = [c, Rt](const vec3& p) {
      vec3 q = Rt * (p - c);
      float bulge = 0.025f * (q.x / 0.085f) * (q.x / 0.085f);
      float d2 = shield2D(vec2(q.x, q.y), 0.17f, 0.30f, 2.2f);
      return std::max(d2, std::fabs(q.z + bulge) - 0.007f);
    };
    m.addR(HB_ITEML, MT_CLOTH, 0.0f, fnS(plate, c, 0.17f));
    Fn rim = [c, Rt](const vec3& p) {
      vec3 q = Rt * (p - c);
      float bulge = 0.025f * (q.x / 0.085f) * (q.x / 0.085f);
      float d2 = shield2D(vec2(q.x, q.y), 0.178f, 0.31f, 2.2f);
      return std::max(std::fabs(d2 + 0.006f) - 0.006f, std::fabs(q.z + bulge) - 0.0095f);
    };
    m.addR(HB_ITEML, MT_IRON, 0.0f, fnS(rim, c, 0.18f));
    Fn chevron = [c, Rt, plate](const vec3& p) {
      vec3 q = Rt * (p - c);
      float v = q.y + 0.6f * std::fabs(q.x) - 0.02f;
      return std::max(std::fabs(v) - 0.022f, plate(p) - 0.004f);
    };
    m.paint(MT_CLOTH2, fnS(chevron, c, 0.2f), MT_CLOTH);
    m.addR(HB_ITEML, MT_GOLD, 0.003f, ellS(c + R * vec3(0, 0.045f, 0.004f), vec3(0.016f, 0.016f, 0.008f), R));
  }
  return m;
}

// --- seated royals and the bishop ------------------------------------------
// torso of a seated figure (pelvis on the ROOT bone, the rest on the CHEST)
Fn seatedChestField(const HumanJ& j) {
  float S = j.S;
  vec3 w = j.waist;
  return [w, S](const vec3& p) {
    float c = ellipsoid(p - (w + vec3(0, 0.2f, 0.0f) * S), vec3(0.122f, 0.15f, 0.085f) * S);
    float sh = ellipsoid(p - (w + vec3(0, 0.305f, -0.01f) * S), vec3(0.15f, 0.055f, 0.075f) * S);
    float wa = ellipsoid(p - (w + vec3(0, 0.03f, 0.005f) * S), vec3(0.105f, 0.075f, 0.08f) * S);
    return smin(smin(c, sh, 0.05f * S), wa, 0.05f * S);
  };
}

void seatedBody(Model& m, const HumanJ& j, int bodyMat, int legMat, int shoeMat, bool drape, int drapeMat) {
  float S = j.S;
  m.add(HB_ROOT, bodyMat, 0.0f, ellS(j.root + vec3(0, -0.005f, 0.0f) * S, vec3(0.128f, 0.075f, 0.11f) * S));
  Fn chest = seatedChestField(j);
  m.add(HB_CHEST, bodyMat, 0.05f * S, fnS(chest, j.waist + vec3(0, 0.18f, 0) * S, 0.24f * S));
  m.add(HB_HEAD, MT_SKIN, 0.01f * S, cylS(j.neck - vec3(0, 0.04f, 0.005f) * S, j.neck + vec3(0, 0.05f, 0.01f) * S, 0.036f * S));
  for (int sd = -1; sd <= 1; sd += 2) {
    bool L = sd > 0;
    vec3 hip = L ? j.hipL : mx(j.hipL), knee = L ? j.kneeL : mx(j.kneeL), ankle = L ? j.ankleL : mx(j.ankleL);
    int th = L ? HB_LTHIGH : HB_RTHIGH, sh = L ? HB_LSHIN : HB_RSHIN;
    m.add(th, bodyMat, 0.03f * S, coneS(hip, knee, 0.066f * S, 0.052f * S));
    // the lap: cloth spanning the gap between the thighs (each half follows its own thigh)
    m.add(th, bodyMat, 0.03f * S,
          boxS(vec3(hip.x * 0.5f, hip.y + 0.005f * S, (hip.z + knee.z) * 0.5f + 0.01f * S),
               vec3(0.045f, 0.045f, 0.11f) * S, 0.035f * S));
    if (drape) {
      // a gown or alb falling from the knees to the floor
      m.add(sh, drapeMat, 0.03f * S, coneS(knee, ankle + vec3(0, -0.04f, 0.0f) * S, 0.06f * S, 0.075f * S));
      m.add(sh, drapeMat, 0.03f * S,
            boxS(vec3(knee.x * 0.5f, (knee.y + 0.01f * S) * 0.5f, knee.z + 0.01f * S),
                 vec3(0.066f * S, (knee.y - 0.01f * S) * 0.5f, 0.052f * S), 0.03f * S));
      // the hem flares a little where it meets the floor
      m.add(sh, drapeMat, 0.03f * S,
            boxS(vec3(knee.x * 0.5f, 0.02f * S, knee.z + 0.03f * S), vec3(0.075f, 0.02f, 0.07f) * S, 0.02f * S));
      m.add(sh, shoeMat, 0.01f * S,
            ellS(vec3(ankle.x, 0.018f * S, ankle.z + 0.07f * S), vec3(0.03f, 0.018f, 0.05f) * S));
    } else {
      m.add(sh, legMat, 0.015f * S, coneS(knee, ankle + vec3(0, 0.03f, 0) * S, 0.046f * S, 0.034f * S));
      m.add(sh, shoeMat, 0.012f * S,
            boxS(vec3(ankle.x, 0.028f * S, ankle.z + 0.032f * S), vec3(0.033f, 0.026f, 0.07f) * S, 0.022f * S));
    }
  }
}

// a cloak over the shoulders and down the back to the seat
void addMantle(Model& m, const HumanJ& j, int mat, float offset, float bottom, float frontLimit) {
  float S = j.S;
  Fn chest = seatedChestField(j);
  vec3 shL = j.shL, shR = j.shR, elL = j.elL, elR = j.elR;
  float zc = j.waist.z;
  Fn f = [chest, S, offset, bottom, frontLimit, zc, shL, shR, elL, elR](const vec3& p) {
    float yTop = shL.y - 0.07f * S;
    vec3 q(p.x * (p.y < yTop ? 1.0f + 0.25f * (yTop - p.y) / S : 1.0f), std::max(p.y, yTop), p.z);
    float body = chest(q);
    float arms = std::min(roundCone(p, shL, elL, 0.05f * S, 0.044f * S), roundCone(p, shR, elR, 0.05f * S, 0.044f * S));
    float d = smin(body, arms, 0.04f * S) - offset * S;
    d = std::max(d, -(d + 0.012f * S));  // a shell, not a solid
    float front = (p.z - zc) - frontLimit * S;
    return std::max(std::max(d, front), bottom * S - p.y);
  };
  m.add(HB_CHEST, mat, 0.006f * S, fnS(f, j.waist + vec3(0, 0.15f, -0.05f) * S, 0.34f * S));
}

// King: crowned, bearded, in an ermine-trimmed mantle, a sword across his knees.
Model kingModel() {
  HumanJ j = seatedJ(A_KING);
  float S = j.S;
  Model m;
  m.skel = &skeleton(A_KING);
  seatedBody(m, j, MT_CLOTH, MT_CLOTH2, MT_LEATHER, false, 0);
  // gold hem of the tunic at the knees and a jewelled belt
  for (int sd = -1; sd <= 1; sd += 2)
    m.paint(MT_GOLD, boxS(vec3(sd * 0.075f, 0.335f, 0.172f) * S, vec3(0.08f, 0.08f, 0.009f) * S, 0.0f), MT_CLOTH);
  m.add(HB_ROOT, MT_GOLD, 0.006f * S, torusS(j.waist + vec3(0, -0.005f, 0.01f) * S, 0.104f * S, 0.009f * S));
  m.add(HB_ROOT, MT_GEM, 0.002f * S, sphereS(j.waist + vec3(0, -0.005f, 0.096f) * S, 0.011f * S));
  // tunic: a broad gold band down the front
  m.paint(MT_GOLD, boxS(j.waist + vec3(0, 0.18f, 0.09f) * S, vec3(0.018f, 0.2f, 0.04f) * S, 0.0f), MT_CLOTH);
  addMantle(m, j, MT_CLOTH2, 0.012f, 0.29f, 0.03f);
  // ermine shoulder cape
  m.add(HB_CHEST, MT_FUR, 0.02f * S,
        fnS([j, S](const vec3& p) {
          float e = ellipsoid(p - (j.waist + vec3(0, 0.3f, -0.012f) * S), vec3(0.172f, 0.075f, 0.105f) * S);
          return std::max(e, -(e + 0.02f * S));
        }, j.waist + vec3(0, 0.3f, 0) * S, 0.18f * S));
  addArm(m, true, j, MT_CLOTH, 0.046f * S, 0.039f * S, MT_GOLD);
  addArm(m, false, j, MT_CLOTH, 0.046f * S, 0.039f * S, MT_GOLD);
  addFist(m, HB_RFORE, j.gripR, vec3(1, 0, 0), 1.35f * S);
  addFist(m, HB_LFORE, j.gripL + vec3(0, 0.008f, 0) * S, vec3(1, 0, 0.3f), 1.35f * S);
  vec3 hc = j.neck + vec3(0, 0.078f, 0.012f) * S;
  addHead(m, HB_HEAD, hc, 1.42f * S, 2, MT_HAIR);
  addHair(m, HB_HEAD, hc, 1.42f * S, MT_HAIR, 0.6f);
  addCrown(m, HB_HEAD, hc + vec3(0, 0.056f, -0.004f) * S, 0.066f * S, 0.034f * S, 8, true);
  // the sword lies across his lap, hilt in his right hand
  {
    vec3 g = j.gripR;
    vec3 ax(1, 0, 0);
    m.addR(HB_ITEMR, MT_LEATHER, 0.0f, cylS(g - ax * 0.05f * S, g + ax * 0.03f * S, 0.011f * S));
    m.addR(HB_ITEMR, MT_GOLD, 0.004f, sphereS(g - ax * 0.065f * S, 0.019f * S));
    m.addR(HB_ITEMR, MT_GOLD, 0.004f * S, boxS(g + ax * 0.04f * S, vec3(0.009f, 0.012f, 0.075f) * S, 0.006f * S));
    m.addR(HB_ITEMR, MT_STEEL, 0.0f, fnS([g, S](const vec3& p) {
             vec3 q = p - g;
             float t = glm::clamp((q.x - 0.05f * S) / (0.43f * S), 0.0f, 1.0f);
             float w = 0.021f * S * (1.0f - 0.55f * t) * std::sqrt(std::max(0.0f, 1.0f - std::pow(t, 6.0f))) + 0.001f;
             float th = 0.006f * S * (1.0f - 0.5f * t) + 0.001f;
             float body = std::max(std::fabs(q.z) - w, std::fabs(q.y) - th);
             return std::max(body, std::max(0.05f * S - q.x, q.x - 0.48f * S));
           }, g + vec3(0.26f, 0, 0) * S, 0.25f * S));
  }
  return m;
}

// Queen: crowned and veiled, in a long gown, with a sceptre.
Model queenModel() {
  HumanJ j = seatedJ(A_QUEEN);
  float S = j.S;
  Model m;
  m.skel = &skeleton(A_QUEEN);
  seatedBody(m, j, MT_CLOTH2, MT_CLOTH2, MT_LEATHER, true, MT_CLOTH2);
  // slimmer bodice: a gold girdle and a jewelled necklace
  m.add(HB_ROOT, MT_GOLD, 0.006f * S, torusS(j.waist + vec3(0, -0.005f, 0.01f) * S, 0.1f * S, 0.007f * S));
  m.paint(MT_GOLD, torusS(j.neck + vec3(0, -0.035f, 0.02f) * S, 0.06f * S, 0.008f * S, rotXm(0.5f)), MT_CLOTH2);
  m.add(HB_CHEST, MT_GEM, 0.002f * S, sphereS(j.neck + vec3(0, -0.075f, 0.08f) * S, 0.012f * S));
  // embroidered hem of the gown
  m.paint(MT_GOLD, boxS(vec3(0, 0.03f, 0.25f) * S, vec3(0.2f, 0.03f, 0.1f) * S, 0.0f), MT_CLOTH2);
  addMantle(m, j, MT_CLOTH, 0.012f, 0.29f, 0.01f);
  addArm(m, true, j, MT_CLOTH2, 0.042f * S, 0.034f * S, MT_GOLD);
  addArm(m, false, j, MT_CLOTH2, 0.042f * S, 0.034f * S, MT_GOLD);
  addFist(m, HB_RFORE, j.gripR, vec3(0, 1, 0), 1.25f * S);
  addFist(m, HB_LFORE, j.gripL + vec3(0, 0.008f, 0) * S, vec3(1, 0, 0.3f), 1.25f * S);
  vec3 hc = j.neck + vec3(0, 0.078f, 0.012f) * S;
  addHead(m, HB_HEAD, hc, 1.36f * S, 0, MT_HAIR);
  // braided hair coiled over the ears, under a linen veil
  m.add2(HB_HEAD, HB_HEAD, MT_HAIR, 0.008f * S, ellS(hc + vec3(0.062f, -0.02f, -0.005f) * S, vec3(0.022f, 0.034f, 0.03f) * S));
  m.add(HB_HEAD, MT_LINEN, 0.012f * S, fnS([hc, S](const vec3& p) {
          float e = ellipsoid(p - (hc + vec3(0, 0.008f, -0.008f) * S), vec3(0.066f, 0.078f, 0.072f) * S);
          vec3 q(p.x, std::min(p.y, hc.y - 0.02f * S), p.z);  // falls down behind the neck
          float fall = ellipsoid(q - (hc + vec3(0, 0.008f, -0.02f) * S), vec3(0.07f, 0.08f, 0.064f) * S);
          float v = smin(e, std::max(fall, hc.y - 0.15f * S - p.y), 0.02f * S);
          return std::max(v, (p.z - hc.z) - 0.02f * S);
        }, hc + vec3(0, -0.04f, -0.02f) * S, 0.16f * S));
  addCrown(m, HB_HEAD, hc + vec3(0, 0.058f, -0.006f) * S, 0.07f * S, 0.03f * S, 6, false);
  // sceptre: a gold rod with a jewelled orb and cross
  {
    vec3 g = j.gripR;
    m.addR(HB_ITEMR, MT_GOLD, 0.0f, cylS(g - vec3(0, 0.12f, 0) * S, g + vec3(0, 0.27f, 0) * S, 0.008f * S));
    m.addR(HB_ITEMR, MT_GOLD, 0.004f, sphereS(g - vec3(0, 0.125f, 0) * S, 0.012f * S));
    vec3 t = g + vec3(0, 0.29f, 0) * S;
    m.addR(HB_ITEMR, MT_GEM, 0.003f, sphereS(t, 0.022f * S));
    m.addR(HB_ITEMR, MT_GOLD, 0.003f, torusS(t, 0.022f * S, 0.004f * S, rotXm(1.5708f)));
    m.addR(HB_ITEMR, MT_GOLD, 0.002f, boxS(t + vec3(0, 0.04f, 0) * S, vec3(0.004f, 0.02f, 0.004f) * S, 0.001f));
    m.addR(HB_ITEMR, MT_GOLD, 0.002f, boxS(t + vec3(0, 0.045f, 0) * S, vec3(0.014f, 0.004f, 0.004f) * S, 0.001f));
  }
  return m;
}

// Bishop: mitre, cope and crozier, blessing with his right hand.
Model bishopModel() {
  HumanJ j = seatedJ(A_BISHOP);
  float S = j.S;
  Model m;
  m.skel = &skeleton(A_BISHOP);
  seatedBody(m, j, MT_CLOTH, MT_LINEN, MT_CLOTH2, true, MT_LINEN);
  // chasuble: a bell of cloth over the shoulders, its gold orphrey a Y-cross
  {
    Fn chest = seatedChestField(j);
    vec3 w = j.waist;
    m.add(HB_CHEST, MT_CLOTH, 0.02f * S, fnS([chest, w, S](const vec3& p) {
            float yTop = w.y + 0.2f * S;
            float spread = std::max(0.0f, yTop - p.y) * 0.32f;
            vec3 q(p.x / (1.0f + spread / S * 1.0f), std::max(p.y, yTop - 0.06f * S), p.z);
            float d = chest(q) - 0.014f * S;
            return std::max(d, w.y - 0.02f * S - p.y);
          }, j.waist + vec3(0, 0.15f, 0) * S, 0.3f * S));
  }
  m.paint(MT_GOLD, boxS(j.waist + vec3(0, 0.1f, 0.1f) * S, vec3(0.022f, 0.16f, 0.06f) * S, 0.0f), MT_CLOTH);
  for (int sd = -1; sd <= 1; sd += 2)
    m.paint(MT_GOLD,
            boxS(j.waist + vec3(sd * 0.055f, 0.285f, 0.07f) * S, vec3(0.018f, 0.075f, 0.08f) * S, 0.0f,
                 rotZm(sd * 0.75f)),
            MT_CLOTH);
  m.paint(MT_GOLD, boxS(vec3(0, 0.335f, 0.24f) * S, vec3(0.2f, 0.02f, 0.06f) * S, 0.0f), MT_CLOTH);
  // alb sleeves, gloved hands
  addArm(m, true, j, MT_CLOTH, 0.046f * S, 0.036f * S, MT_LINEN);
  addArm(m, false, j, MT_CLOTH, 0.046f * S, 0.036f * S, MT_LINEN);
  addFist(m, HB_LFORE, j.gripL, vec3(0, 1, 0), 1.3f * S);
  {  // blessing hand: open palm, two fingers raised
    vec3 g = j.gripR;
    m.add(HB_RFORE, MT_SKIN, 0.008f * S, ellS(g + vec3(0, -0.012f, 0) * S, vec3(0.02f, 0.028f, 0.012f) * S));
    m.add(HB_RFORE, MT_SKIN, 0.004f * S, capsuleS(g + vec3(-0.004f, 0.01f, 0) * S, g + vec3(-0.006f, 0.04f, 0.002f) * S, 0.0055f * S));
    m.add(HB_RFORE, MT_SKIN, 0.004f * S, capsuleS(g + vec3(0.007f, 0.01f, 0) * S, g + vec3(0.008f, 0.038f, 0.002f) * S, 0.0055f * S));
    m.add(HB_RFORE, MT_GOLD, 0.002f * S, torusS(g + vec3(0.012f, -0.005f, 0.0f) * S, 0.007f * S, 0.0025f * S, rotZm(1.5708f)));
  }
  vec3 hc = j.neck + vec3(0, 0.078f, 0.012f) * S;
  addHead(m, HB_HEAD, hc, 1.4f * S, 2, MT_HAIR);
  addHair(m, HB_HEAD, hc, 1.4f * S, MT_HAIR, 0.0f);
  // mitre: two pointed panels, gold bands, lappets hanging behind
  {
    vec3 mc = hc + vec3(0, 0.07f, -0.006f) * S;
    Fn mitre = [mc, S](const vec3& p) {
      vec3 q = (p - mc) / S;
      float h = 0.15f;
      float t = glm::clamp(q.y / h, 0.0f, 1.0f);
      float w = 0.062f * (1.0f - std::pow(t, 2.2f)) + 0.004f;
      float dz = 0.056f * (1.0f - 0.65f * t);
      float side = std::fabs(q.x) - w;
      float depth = std::fabs(q.z) / dz * 0.05f - 0.05f;
      float d = std::max(std::max(side, depth), std::max(-q.y, q.y - h));
      // cleft between the front and back peaks
      float cleft = std::max(std::fabs(q.z) - 0.006f, 0.07f - q.y);
      return std::max(d, -cleft) * S;
    };
    m.add(HB_HEAD, MT_CLOTH, 0.004f * S, fnS(mitre, mc + vec3(0, 0.075f, 0) * S, 0.12f * S));
    m.paint(MT_GOLD, boxS(mc + vec3(0, 0.012f, 0) * S, vec3(0.08f, 0.012f, 0.08f) * S, 0.0f), MT_CLOTH);
    m.paint(MT_GOLD, boxS(mc + vec3(0, 0.07f, 0) * S, vec3(0.011f, 0.07f, 0.08f) * S, 0.0f), MT_CLOTH);
    m.add(HB_HEAD, MT_GEM, 0.002f * S, sphereS(mc + vec3(0, 0.04f, 0.054f) * S, 0.009f * S));
    for (int sd = -1; sd <= 1; sd += 2)
      m.add(HB_HEAD, MT_CLOTH, 0.004f * S,
            boxS(mc + vec3(sd * 0.03f, -0.05f, -0.055f) * S, vec3(0.012f, 0.05f, 0.003f) * S, 0.002f * S,
                 rotXm(0.2f)));
  }
  // crozier: a gold staff ending in a curled crook
  {
    vec3 g = j.gripL;
    m.addR(HB_ITEML, MT_GOLD, 0.0f, cylS(vec3(g.x, 0.02f * S, g.z), g + vec3(0, 0.62f, 0) * S, 0.0085f * S));
    m.addR(HB_ITEML, MT_GOLD, 0.004f, ellS(g + vec3(0, 0.6f, 0) * S, vec3(0.02f, 0.026f, 0.02f) * S));
    vec3 cc = g + vec3(0, 0.68f, 0.035f) * S;
    m.addR(HB_ITEML, MT_GOLD, 0.0f, fnS([cc, S](const vec3& p) {
             vec3 q = (p - cc) / S;  // crook in the y-z plane, open downward at the front
             float a = std::atan2(q.y, -q.z);
             float rr = std::sqrt(q.y * q.y + q.z * q.z);
             float spiral = 0.04f - 0.012f * glm::clamp((a + 3.14159f) / 6.28318f, 0.0f, 1.0f);
             float tube = std::sqrt((rr - spiral) * (rr - spiral) + q.x * q.x) - 0.0075f;
             float keep = (q.y < 0 && q.z > 0) ? std::max(-q.y - 0.03f, 0.0f) : 0.0f;
             return (tube + keep) * S;
           }, cc, 0.06f * S));
    m.addR(HB_ITEML, MT_GOLD, 0.0f, cylS(g + vec3(0, 0.6f, 0) * S, cc + vec3(0, -0.035f, -0.035f) * S, 0.0078f * S));
  }
  return m;
}

// --- thrones ---------------------------------------------------------------
Model throneModel(bool cathedra) {
  Model m;
  const float S = cathedra ? seatedScale(A_BISHOP) : 1.0f;
  const float W = 0.235f * S, ZF = 0.09f * S, ZB = -0.235f * S;
  auto post = [&](vec3 base, float h, float r) {
    m.add(0, MT_WOOD, 0.004f, boxS(base + vec3(0, h * 0.5f, 0), vec3(r, h * 0.5f, r), 0.006f * S));
  };
  // four posts with gold finials
  for (int sd = -1; sd <= 1; sd += 2) {
    post(vec3(sd * W, 0, ZF), 0.47f * S, 0.026f * S);
    m.add(0, MT_GOLD, 0.006f, sphereS(vec3(sd * W, 0.5f * S, ZF), 0.03f * S));
    m.add(0, MT_WOOD, 0.006f, ellS(vec3(sd * W, 0.025f * S, ZF + 0.006f), vec3(0.036f, 0.028f, 0.04f) * S));  // paw foot
    float backH = cathedra ? 1.06f : 1.0f;
    post(vec3(sd * W, 0, ZB), backH * S, 0.028f * S);
    if (cathedra) {
      // pinnacle: a slender spire with crockets
      vec3 b(sd * W, backH * S, ZB);
      m.add(0, MT_WOOD, 0.004f, coneS(b, b + vec3(0, 0.2f, 0) * S, 0.032f * S, 0.004f * S));
      for (int k = 0; k < 3; ++k)
        m.add(0, MT_GOLD, 0.004f, sphereS(b + vec3(0, (0.05f + 0.05f * k) * S, 0), (0.024f - 0.006f * k) * S));
      m.add(0, MT_GOLD, 0.004f, sphereS(b + vec3(0, 0.21f, 0) * S, 0.012f * S));
    } else {
      m.add(0, MT_GOLD, 0.006f, sphereS(vec3(sd * W, (backH + 0.035f) * S, ZB), 0.034f * S));
      m.add(0, MT_GOLD, 0.004f, cylS(vec3(sd * W, backH * S, ZB), vec3(sd * W, (backH + 0.012f) * S, ZB), 0.034f * S));
    }
    // armrest and the panel under it
    m.add(0, MT_WOOD, 0.006f, boxS(vec3(sd * W, 0.45f * S, (ZF + ZB) * 0.5f), vec3(0.03f * S, 0.016f * S, (ZF - ZB) * 0.5f), 0.01f * S));
    m.add(0, MT_WOOD, 0.004f, boxS(vec3(sd * W, 0.33f * S, (ZF + ZB) * 0.5f), vec3(0.012f * S, 0.11f * S, (ZF - ZB) * 0.5f), 0.004f * S));
    // tracery arches in the side panel
    for (int k = 0; k < 3; ++k) {
      float z = ZB + (ZF - ZB) * (k + 0.5f) / 3.0f;
      m.sub(MT_VELVET, 0.004f, fnS([sd, W, z, S](const vec3& p) {
              vec3 q = p - vec3(sd * W, 0.3f * S, z);
              float arch = std::max(std::fabs(q.z) - 0.03f * S, q.y - 0.05f * S + 0.6f * std::fabs(q.z));
              return std::max(std::max(arch, -q.y - 0.07f * S), std::fabs(q.x) - 0.009f * S);
            }, vec3(sd * W, 0.3f * S, z), 0.1f * S));
    }
  }
  // seat, apron and cushion
  m.add(0, MT_WOOD, 0.006f, boxS(vec3(0, 0.245f, (ZF + ZB) * 0.5f) * vec3(1, S, 1), vec3(W, 0.03f * S, (ZF - ZB) * 0.5f), 0.008f * S));
  m.add(0, MT_WOOD, 0.004f, boxS(vec3(0, 0.18f * S, ZF), vec3(W, 0.04f * S, 0.012f * S), 0.004f * S));
  m.add(0, MT_VELVET, 0.012f, boxS(vec3(0, 0.29f * S, (ZF + ZB) * 0.5f + 0.01f * S), vec3(W - 0.02f * S, 0.022f * S, (ZF - ZB) * 0.5f - 0.015f * S), 0.02f * S));
  // back panel
  float top = cathedra ? 1.0f : 0.93f;
  m.add(0, MT_WOOD, 0.006f, fnS([W, ZB, S, top, cathedra](const vec3& p) {
          vec3 q = p - vec3(0, 0, ZB);
          float box = std::max(std::fabs(q.x) - W, std::fabs(q.z) - 0.022f * S);
          float y = q.y / S;
          // pointed gable rising to a peak in the middle
          float peak = cathedra ? top + 0.18f - 1.2f * std::fabs(q.x) / S : top + 0.1f - 0.5f * std::fabs(q.x) / S;
          float lid = (y - std::min(peak, cathedra ? 1.18f : 1.04f)) * S;
          return std::max(std::max(box, lid), (0.24f - y) * S);
        }, vec3(0, 0.65f * S, ZB), 0.55f * S));
  // velvet inset with a gold frame
  m.add(0, MT_VELVET, 0.008f, boxS(vec3(0, 0.62f * S, ZB + 0.024f * S), vec3(0.175f * S, 0.27f * S, 0.006f * S), 0.012f * S));
  m.add(0, MT_GOLD, 0.0f, fnS([ZB, S](const vec3& p) {
          vec3 q = p - vec3(0, 0.62f * S, ZB + 0.024f * S);
          float outer = roundBox(q, vec3(0.188f, 0.283f, 0.008f) * S, 0.014f * S);
          float inner = roundBox(q, vec3(0.172f, 0.267f, 0.03f) * S, 0.012f * S);
          return std::max(outer, -inner);
        }, vec3(0, 0.62f * S, ZB), 0.36f * S));
  if (cathedra) {
    // a gold cross atop the gable and a trefoil window in it
    vec3 apex(0, 1.2f * S, ZB);
    m.add(0, MT_GOLD, 0.003f, boxS(apex + vec3(0, 0.05f, 0) * S, vec3(0.007f, 0.05f, 0.007f) * S, 0.002f));
    m.add(0, MT_GOLD, 0.003f, boxS(apex + vec3(0, 0.065f, 0) * S, vec3(0.03f, 0.007f, 0.007f) * S, 0.002f));
    for (int k = 0; k < 3; ++k) {
      float a = 6.2831853f * k / 3.0f;
      m.sub(MT_DARK, 0.004f, cylS(vec3(std::sin(a) * 0.022f, 1.03f + std::cos(a) * 0.022f, -0.3f) * vec3(S, S, 1) + vec3(0, 0, ZB + 0.3f - 0.3f),
                                  vec3(std::sin(a) * 0.022f, 1.03f + std::cos(a) * 0.022f, 0) * vec3(S, S, 1) + vec3(0, 0, ZB + 0.06f),
                                  0.016f * S));
    }
  } else {
    // royal arms: a gold crown carved on the crest
    vec3 c(0, 0.985f * S, ZB + 0.02f * S);
    m.add(0, MT_GOLD, 0.004f, boxS(c, vec3(0.06f, 0.016f, 0.012f) * S, 0.004f * S));
    for (int k = -1; k <= 1; ++k) m.add(0, MT_GOLD, 0.004f, sphereS(c + vec3(k * 0.045f, 0.03f, 0) * S, 0.012f * S));
  }
  return m;
}

// --- knight ----------------------------------------------------------------
Fn horseBodyField() {
  return [](const vec3& p) {
    float barrel = ellipsoid(p - K(0, 0.53f, -0.01f), vec3(0.135f, 0.155f, 0.27f) * KS);
    float chest = ellipsoid(p - K(0, 0.54f, 0.2f), vec3(0.125f, 0.16f, 0.13f) * KS);
    float haunch = ellipsoid(p - K(0, 0.56f, -0.21f), vec3(0.14f, 0.165f, 0.15f) * KS);
    return smin(smin(barrel, chest, 0.08f * KS), haunch, 0.08f * KS);
  };
}

Model knightModel() {
  using namespace kj;
  Model m;
  m.skel = &skeleton(A_KNIGHT);
  Fn bodyF = horseBodyField();
  m.add(KB_BODY, MT_HORSE, 0.0f, fnS(bodyF, K(0, 0.55f, 0), 0.42f * KS));
  // neck and head
  m.add(KB_NECK, MT_HORSE, 0.06f * KS, coneS(K(0, 0.6f, 0.24f), K(0, 0.86f, 0.39f), 0.1f * KS, 0.06f * KS));
  m.add(KB_HEAD, MT_HORSE, 0.04f * KS, ellS(K(0, 0.885f, 0.43f), vec3(0.05f, 0.06f, 0.07f) * KS, rotXm(0.6f)));
  m.add(KB_HEAD, MT_HORSE, 0.05f * KS, ellS(K(0, 0.82f, 0.5f), vec3(0.046f, 0.052f, 0.08f) * KS, rotXm(0.8f)));
  m.add(KB_HEAD, MT_HORSE, 0.03f * KS, ellS(K(0, 0.755f, 0.565f), vec3(0.04f, 0.045f, 0.05f) * KS));
  for (int sd = -1; sd <= 1; sd += 2) {
    m.add(KB_HEAD, MT_HORSE, 0.01f * KS, coneS(K(sd * 0.025f, 0.94f, 0.4f), K(sd * 0.032f, 1.0f, 0.38f), 0.016f * KS, 0.004f * KS));
    m.add(KB_HEAD, MT_DARK, 0.002f, sphereS(K(sd * 0.044f, 0.875f, 0.47f), 0.011f * KS));
    m.paint(MT_DARK, sphereS(K(sd * 0.022f, 0.75f, 0.61f), 0.011f * KS), MT_HORSE);  // nostrils
  }
  // mane and forelock
  m.add(KB_NECK, MT_MANE, 0.02f * KS, fnS([](const vec3& p) {
          vec3 q = vec3(p.x * 2.2f, p.y, p.z);
          return capsule(q, K(0, 0.94f, 0.37f), K(0, 0.7f, 0.17f), 0.026f * KS) / 2.2f;
        }, K(0, 0.82f, 0.27f), 0.2f * KS));
  m.add(KB_HEAD, MT_MANE, 0.01f * KS, ellS(K(0, 0.93f, 0.445f), vec3(0.015f, 0.025f, 0.02f) * KS));
  // tail
  m.add(KB_TAIL, MT_MANE, 0.03f * KS, coneS(K(0, 0.66f, -0.35f), K(0, 0.5f, -0.42f), 0.032f * KS, 0.038f * KS));
  m.add(KB_TAIL, MT_MANE, 0.03f * KS, coneS(K(0, 0.5f, -0.42f), K(0, 0.3f, -0.44f), 0.038f * KS, 0.02f * KS));
  // legs: front (forearm, knee, cannon, fetlock, hoof) and hind (gaskin, hock, cannon ...)
  for (int sd = -1; sd <= 1; sd += 2) {
    bool L = sd > 0;
    vec3 fu = L ? flu : mx(flu), fl = L ? fll : mx(fll), hu = L ? hlu : mx(hlu), hl = L ? hll : mx(hll);
    int bFU = L ? KB_FLU : KB_FRU, bFL = L ? KB_FLL : KB_FRL, bHU = L ? KB_HLU : KB_HRU, bHL = L ? KB_HLL : KB_HRL;
    float x = fu.x;
    m.add(bFU, MT_HORSE, 0.03f * KS, coneS(fu, fl, 0.05f * KS, 0.034f * KS));
    m.add(bFL, MT_HORSE, 0.02f * KS, coneS(fl, vec3(x, 0.075f * KS, fl.z + 0.008f * KS), 0.033f * KS, 0.024f * KS));
    m.add(bFL, MT_HORSE, 0.012f * KS, ellS(vec3(x, 0.07f * KS, fl.z + 0.012f * KS), vec3(0.03f, 0.032f, 0.034f) * KS));
    m.add(bFL, MT_LEATHER, 0.006f * KS, coneS(vec3(x, 0.045f * KS, fl.z + 0.022f * KS), vec3(x, 0.006f * KS, fl.z + 0.03f * KS), 0.027f * KS, 0.036f * KS));
    float hx = hu.x;
    m.add(bHU, MT_HORSE, 0.04f * KS, coneS(hu + K(0, -0.02f, 0.03f), hl, 0.07f * KS, 0.036f * KS));
    m.add(bHL, MT_HORSE, 0.02f * KS, coneS(hl, vec3(hx, 0.075f * KS, hl.z + 0.035f * KS), 0.034f * KS, 0.024f * KS));
    m.add(bHL, MT_HORSE, 0.012f * KS, ellS(vec3(hx, 0.07f * KS, hl.z + 0.04f * KS), vec3(0.03f, 0.032f, 0.034f) * KS));
    m.add(bHL, MT_LEATHER, 0.006f * KS, coneS(vec3(hx, 0.045f * KS, hl.z + 0.05f * KS), vec3(hx, 0.006f * KS, hl.z + 0.058f * KS), 0.027f * KS, 0.036f * KS));
  }
  // caparison: cloth draped over the horse down to the knees, with a dagged hem
  m.add(KB_BODY, MT_CLOTH, 0.01f * KS, fnS([bodyF](const vec3& p) {
          float yc = 0.55f * KS;
          vec3 q(p.x, std::max(p.y, yc), p.z);
          float d = bodyF(q) - 0.016f * KS;
          d = std::max(d, -(d + 0.03f * KS));
          float a = std::atan2(p.x, p.z * 0.45f);
          float hem = 0.31f * KS + 0.016f * KS * std::fabs(std::sin(a * 9.0f));
          return std::max(d, hem - p.y);
        }, K(0, 0.5f, 0), 0.44f * KS));
  m.paint(MT_CLOTH2, fnS([](const vec3& p) { return std::fabs(p.y - 0.355f * KS) - 0.022f * KS; }, K(0, 0.35f, 0), 0.5f), MT_CLOTH);
  for (int sd = -1; sd <= 1; sd += 2) {  // a great cross on each flank
    m.paint(MT_CLOTH2, boxS(K(sd * 0.16f, 0.47f, -0.02f), vec3(0.1f, 0.022f, 0.09f) * KS, 0.0f), MT_CLOTH);
    m.paint(MT_CLOTH2, boxS(K(sd * 0.16f, 0.47f, -0.02f), vec3(0.1f, 0.09f, 0.022f) * KS, 0.0f), MT_CLOTH);
  }
  // chanfron (face armour) and bridle
  m.add(KB_HEAD, MT_STEEL, 0.004f * KS, fnS([](const vec3& p) {
          vec3 c = K(0, 0.83f, 0.49f);
          float e = ellipsoid(p - c, vec3(0.056f, 0.066f, 0.105f) * KS);
          float shell = std::max(e, -(e + 0.006f * KS));
          return std::max(shell, std::max(0.79f * KS - p.y + (p.z - c.z) * 0.6f, -p.y + 0.0f));
        }, K(0, 0.85f, 0.5f), 0.14f * KS));
  m.add(KB_HEAD, MT_GOLD, 0.003f, sphereS(K(0, 0.9f, 0.475f), 0.012f * KS));
  // saddle
  m.add(KB_BODY, MT_LEATHER, 0.02f * KS, ellS(K(0, 0.705f, -0.04f), vec3(0.1f, 0.03f, 0.13f) * KS));
  m.add(KB_BODY, MT_LEATHER, 0.02f * KS, ellS(K(0, 0.74f, -0.155f), vec3(0.08f, 0.05f, 0.025f) * KS));
  m.add(KB_BODY, MT_LEATHER, 0.02f * KS, ellS(K(0, 0.73f, 0.075f), vec3(0.06f, 0.04f, 0.025f) * KS));
  // --- rider: legs clamp the horse; torso, arms and helm move on their own
  m.add(KB_RIDER, MT_MAIL, 0.03f * KS, ellS(K(0, 0.78f, -0.05f), vec3(0.09f, 0.06f, 0.08f) * KS));
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    m.add(KB_BODY, MT_MAIL, 0.02f * KS, coneS(K(x * 0.07f, 0.78f, -0.03f), K(x * 0.155f, 0.66f, 0.08f), 0.045f * KS, 0.037f * KS));
    m.add(KB_BODY, MT_STEEL, 0.006f * KS, ellS(K(x * 0.158f, 0.665f, 0.095f), vec3(0.032f, 0.034f, 0.026f) * KS));
    m.add(KB_BODY, MT_MAIL, 0.012f * KS, coneS(K(x * 0.155f, 0.66f, 0.08f), K(x * 0.16f, 0.475f, 0.035f), 0.034f * KS, 0.027f * KS));
    m.add(KB_BODY, MT_STEEL, 0.008f * KS, boxS(K(x * 0.16f, 0.45f, 0.06f), vec3(0.024f, 0.02f, 0.05f) * KS, 0.015f * KS));
    m.add(KB_BODY, MT_IRON, 0.002f, torusS(K(x * 0.16f, 0.43f, 0.06f), 0.034f * KS, 0.005f * KS, rotZm(1.5708f)));
    m.add(KB_BODY, MT_LEATHER, 0.002f, capsuleS(K(x * 0.12f, 0.7f, -0.04f), K(x * 0.16f, 0.44f, 0.06f), 0.004f * KS));
  }
  // surcoat over mail, with a cross
  m.add(KB_RIDER, MT_MAIL, 0.05f * KS, ellS(K(0, 0.93f, -0.04f), vec3(0.1f, 0.12f, 0.068f) * KS));
  m.add(KB_RIDER, MT_CLOTH, 0.04f * KS, fnS([](const vec3& p) {
          float c = ellipsoid(p - K(0, 0.925f, -0.04f), vec3(0.106f, 0.125f, 0.074f) * KS);
          float skirt = roundCone(p, K(0, 0.86f, -0.04f), K(0, 0.71f, -0.02f), 0.1f * KS, 0.16f * KS);
          float sc = std::max(skirt, -(skirt + 0.012f * KS));
          return smin(std::max(c, 0.84f * KS - p.y), std::max(sc, 0.68f * KS - p.y), 0.02f * KS);
        }, K(0, 0.85f, -0.04f), 0.25f * KS));
  m.paint(MT_CLOTH2, boxS(K(0, 0.92f, 0.04f), vec3(0.016f, 0.08f, 0.05f) * KS, 0.0f), MT_CLOTH);
  m.paint(MT_CLOTH2, boxS(K(0, 0.95f, 0.04f), vec3(0.06f, 0.016f, 0.05f) * KS, 0.0f), MT_CLOTH);
  m.add(KB_RIDER, MT_LEATHER, 0.004f, torusS(K(0, 0.835f, -0.035f), 0.088f * KS, 0.008f * KS));
  // arms in mail with steel gauntlets
  {
    for (int sd = -1; sd <= 1; sd += 2) {
      bool L = sd > 0;
      vec3 s0 = L ? shL : shR, e0 = L ? elL : elR, w0 = L ? wrL : wrR;
      int ua = L ? KB_LARM : KB_RARM, fa = L ? KB_LFORE : KB_RFORE;
      m.add(ua, MT_MAIL, 0.02f * KS, coneS(s0, e0, 0.036f * KS, 0.03f * KS));
      m.add(ua, MT_STEEL, 0.01f * KS, ellS(s0 + K(sd * 0.012f, 0.012f, 0), vec3(0.045f, 0.035f, 0.045f) * KS));  // pauldron
      m.add(fa, MT_MAIL, 0.012f * KS, coneS(e0, w0, 0.029f * KS, 0.024f * KS));
      m.add(fa, MT_STEEL, 0.008f * KS, ellS(L ? gripL : gripR, vec3(0.024f, 0.028f, 0.026f) * KS));
    }
  }
  // great helm with an eye slit, a gold cross and a crest
  {
    vec3 hc = K(0, 1.1f, -0.035f);
    m.add(KB_RHEAD, MT_STEEL, 0.008f * KS, fnS([hc](const vec3& p) {
            vec3 q = p - hc;
            float r = std::sqrt(q.x * q.x + q.z * q.z * 1.15f);
            float cyl = std::max(r - 0.058f * KS, std::fabs(q.y) - 0.07f * KS);
            float top = ellipsoid(q - vec3(0, 0.06f, 0) * KS, vec3(0.058f, 0.025f, 0.055f) * KS);
            return smin(cyl, top, 0.01f * KS);
          }, hc, 0.11f * KS));
    m.paint(MT_DARK, boxS(hc + K(0, 0.012f, 0.06f), vec3(0.05f, 0.005f, 0.05f) * KS, 0.0f), MT_STEEL);
    m.paint(MT_GOLD, boxS(hc + K(0, -0.02f, 0.06f), vec3(0.007f, 0.03f, 0.05f) * KS, 0.0f), MT_STEEL);
    m.paint(MT_GOLD, boxS(hc + K(0, 0.0f, 0.06f), vec3(0.03f, 0.006f, 0.05f) * KS, 0.0f), MT_STEEL);
    for (int k = 0; k < 3; ++k)
      for (int l = 0; l < 2; ++l)
        m.paint(MT_DARK, sphereS(hc + K(-0.03f - 0.012f * l, -0.025f - 0.014f * k, 0.058f), 0.0035f * KS), MT_STEEL);
    // a twisted torse and a plume in the army's colours
    m.add(KB_RHEAD, MT_CLOTH2, 0.004f, torusS(hc + K(0, 0.072f, 0), 0.05f * KS, 0.01f * KS));
    m.add(KB_RHEAD, MT_CLOTH, 0.01f * KS, coneS(hc + K(0, 0.085f, 0.0f), hc + K(0, 0.15f, -0.05f), 0.02f * KS, 0.006f * KS));
    m.add(KB_RHEAD, MT_MAIL, 0.02f * KS, coneS(K(0, 0.99f, -0.045f), K(0, 1.04f, -0.04f), 0.07f * KS, 0.05f * KS));
  }
  // heater shield on the left arm
  {
    vec3 c = K(0.175f, 0.86f, 0.03f);
    mat3 R = rotYm(1.15f);
    mat3 Rt = glm::transpose(R);
    Fn plate = [c, Rt](const vec3& p) {
      vec3 q = Rt * (p - c);
      float bulge = 0.02f * KS * (q.x / (0.1f * KS)) * (q.x / (0.1f * KS));
      float d2 = shield2D(vec2(q.x, q.y), 0.19f * KS, 0.25f * KS, 1.4f);
      return std::max(d2, std::fabs(q.z + bulge) - 0.007f * KS);
    };
    m.add(KB_LFORE, MT_CLOTH2, 0.0f, fnS(plate, c, 0.16f * KS));
    m.paint(MT_CLOTH, fnS([c, Rt, plate](const vec3& p) {
              vec3 q = Rt * (p - c);
              float cross = std::min(std::fabs(q.x) - 0.016f * KS, std::fabs(q.y - 0.04f * KS) - 0.016f * KS);
              return std::max(cross, plate(p) - 0.004f);
            }, c, 0.2f), MT_CLOTH2);
    m.add(KB_LFORE, MT_GOLD, 0.0f, fnS([c, Rt](const vec3& p) {
            vec3 q = Rt * (p - c);
            float bulge = 0.02f * KS * (q.x / (0.1f * KS)) * (q.x / (0.1f * KS));
            float d2 = shield2D(vec2(q.x, q.y), 0.197f * KS, 0.258f * KS, 1.4f);
            return std::max(std::fabs(d2 + 0.004f * KS) - 0.004f * KS, std::fabs(q.z + bulge) - 0.009f * KS);
          }, c, 0.17f * KS));
  }
  // reins from the bit to the left hand (they stretch between the horse's head and the hand)
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 bit = K(sd * 0.04f, 0.76f, 0.56f), hand = gripL + K(-0.02f * (sd + 1), 0, 0);
    m.addStretch(KB_HEAD, KB_LFORE, MT_LEATHER, 0.002f, capsuleS(bit, hand, 0.004f * KS), along(bit, hand));
  }
  // lance: striped shaft, steel vamplate and point, a pennon
  {
    vec3 g = gripR;
    vec3 butt = g - K(0, 0.22f, 0), tip = g + K(0, 0.74f, 0);
    m.addR(KB_LANCE, MT_WOOD, 0.0f, fnS([butt, tip](const vec3& p) {
             vec3 ab = tip - butt;
             float t = glm::clamp(glm::dot(p - butt, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
             float r = glm::mix(0.013f, 0.007f, t) * KS;
             return glm::length(p - (butt + ab * t)) - r;
           }, (butt + tip) * 0.5f, glm::length(tip - butt) * 0.5f + 0.02f));
    m.paint(MT_CLOTH2, fnS([butt, g](const vec3& p) {
              vec3 q = p - g;
              float a = std::atan2(q.x, q.z);
              float band = std::sin(q.y / KS * 60.0f + a * 2.0f);
              return std::max(-band, std::max(glm::length(vec2(q.x, q.z)) - 0.02f * KS, -(q.y - 0.12f * KS)));
            }, g + K(0, 0.4f, 0), 0.5f), MT_WOOD);
    m.addR(KB_LANCE, MT_STEEL, 0.0f, coneS(g + K(0, 0.035f, 0), g + K(0, 0.1f, 0), 0.008f * KS, 0.034f * KS));
    m.addR(KB_LANCE, MT_STEEL, 0.0f, coneS(tip, tip + K(0, 0.07f, 0), 0.011f * KS, 0.001f));
    vec3 pc = tip + K(0, -0.06f, 0);
    m.addR(KB_LANCE, MT_CLOTH2, 0.0f, fnS([pc](const vec3& p) {
             vec3 q = (p - pc) / KS;  // the pennon streams forward (+z) from the shaft
             const float len = 0.12f;
             float half = 0.03f * (1.0f - 0.5f * q.z / len);
             float notch = q.z > len * 0.6f ? 0.4f * (q.z - len * 0.6f) - std::fabs(q.y) : -1.0f;
             float d = std::max(std::max(std::fabs(q.y) - half, std::max(-q.z, q.z - len)), std::fabs(q.x) - 0.0025f);
             return std::max(d, notch) * KS;
           }, pc + K(0, 0, 0.06f), 0.08f * KS));
    m.paint(MT_CLOTH, fnS([pc](const vec3& p) { return std::fabs((p.y - pc.y) / KS) - 0.007f; }, pc, 0.15f), MT_CLOTH2);
  }
  return m;
}

// --- the gatehouse -----------------------------------------------------------
Model castleModel() {
  using namespace cj;
  Model m;
  m.skel = &skeleton(A_ROOK);
  const float TX = 0.215f, TZ = 0.1f, TR = 0.13f, TH = 0.9f;  // flanking towers
  // central block on a battered plinth
  m.add(CB_BODY, MT_STONE, 0.004f, boxS(vec3(0, 0.40f, -0.045f), vec3(0.2f, 0.4f, 0.205f), 0.006f));
  m.add(CB_BODY, MT_STONE, 0.01f, fnS([](const vec3& p) {
          vec3 q = p - vec3(0, 0, -0.045f);
          float flare = 0.03f * glm::clamp(1.0f - p.y / 0.09f, 0.0f, 1.0f);
          return std::max(std::max(std::fabs(q.x) - 0.2f - flare, std::fabs(q.z) - 0.205f - flare), std::max(-p.y, p.y - 0.09f));
        }, vec3(0, 0.05f, -0.045f), 0.32f));
  // parapet with crenels, on a row of corbels
  m.add(CB_BODY, MT_STONE, 0.004f, fnS([](const vec3& p) {
          vec3 q = p - vec3(0, 0.845f, -0.045f);
          float outer = roundBox(q, vec3(0.218f, 0.045f, 0.223f), 0.004f);
          float inner = roundBox(q - vec3(0, 0.03f, 0), vec3(0.19f, 0.05f, 0.195f), 0.002f);
          float d = std::max(outer, -inner);
          // crenels along each wall
          float u = std::fabs(q.x) > std::fabs(q.z) - 0.005f ? q.z : q.x;
          float cren = std::max(std::fabs(std::fmod(u + 10.0f, 0.075f) - 0.0375f) - 0.017f, 0.012f - q.y);
          return std::max(d, -cren);
        }, vec3(0, 0.845f, -0.045f), 0.33f));
  for (int i = -2; i <= 2; ++i) {
    m.add(CB_BODY, MT_STONE, 0.004f, boxS(vec3(i * 0.075f, 0.785f, 0.17f), vec3(0.016f, 0.02f, 0.016f), 0.003f));
    m.add(CB_BODY, MT_STONE, 0.004f, boxS(vec3(i * 0.075f, 0.785f, -0.26f), vec3(0.016f, 0.02f, 0.016f), 0.003f));
  }
  // round towers either side of the gate
  for (int sd = -1; sd <= 1; sd += 2) {
    vec3 c(sd * TX, 0, TZ);
    m.add(CB_BODY, MT_STONE, 0.01f, fnS([c, TR, TH](const vec3& p) {
            vec3 q = p - c;
            float r = std::sqrt(q.x * q.x + q.z * q.z);
            float flare = 0.03f * glm::clamp(1.0f - p.y / 0.1f, 0.0f, 1.0f);
            float body = std::max(r - TR - flare, std::max(-p.y, p.y - TH));
            // parapet ring with crenels
            float ring = std::max(std::max(r - TR - 0.014f, TR - 0.02f - r), std::max(TH - 0.09f - p.y, p.y - TH - 0.07f));
            float a = std::atan2(q.x, q.z);
            float cren = std::max(std::fabs(std::fmod(a * TR + 10.0f, 0.072f) - 0.036f) - 0.016f, TH + 0.025f - p.y);
            ring = std::max(ring, -cren);
            float top = std::max(r - TR + 0.005f, std::fabs(p.y - TH + 0.01f) - 0.01f);
            return std::min(std::min(body, ring), top);
          }, c + vec3(0, 0.5f, 0), 0.6f));
    // corbels under the tower parapet
    for (int k = 0; k < 10; ++k) {
      float a = 6.2831853f * k / 10.0f;
      vec3 d(std::sin(a), 0, std::cos(a));
      m.add(CB_BODY, MT_STONE, 0.004f, boxS(c + d * (TR + 0.004f) + vec3(0, TH - 0.105f, 0), vec3(0.012f, 0.018f, 0.012f), 0.003f,
                                            rotYm(a)));
    }
    // arrow slits (cross loops) facing out and forward
    for (int k = 0; k < 2; ++k) {
      float a = sd * (0.4f + 1.1f * k);
      vec3 d(std::sin(a), 0, std::cos(a));
      vec3 s = c + d * TR + vec3(0, 0.48f - 0.2f * k, 0);
      mat3 R = rotYm(a);
      m.sub(MT_DARK, 0.002f, boxS(s, vec3(0.006f, 0.05f, 0.03f), 0.001f, R));
      m.sub(MT_DARK, 0.002f, boxS(s + vec3(0, 0.012f, 0), vec3(0.022f, 0.006f, 0.03f), 0.001f, R));
    }
  }
  // the gate: a pointed arch, its dark passage and the portcullis inside
  Fn arch = [](const vec3& p) {
    float x = std::fabs(p.x), y = p.y;
    float w = 0.085f;
    float sides = x - w;
    // pointed arch from two circles of radius 1.3 w
    float R = 0.12f;
    float cx = w - R;
    float springY = 0.2f;
    float circ = std::sqrt((x - cx) * (x - cx) + (y - springY) * (y - springY)) - R;
    float d = y < springY ? sides : circ;
    return std::max(d, -y);
  };
  m.sub(MT_STONE, 0.002f, fnS([arch](const vec3& p) {
          return std::max(arch(p), std::fabs(p.z - 0.16f) - 0.06f);
        }, vec3(0, 0.16f, 0.16f), 0.25f));
  m.add(CB_BODY, MT_DARK, 0.0f, boxS(vec3(0, 0.17f, 0.098f), vec3(0.09f, 0.17f, 0.004f), 0.0f));
  // voussoirs: a ring of stones around the arch
  m.add(CB_BODY, MT_STONE, 0.002f, fnS([arch](const vec3& p) {
          float a = arch(p);
          return std::max(std::max(a - 0.018f, -a), std::fabs(p.z - 0.165f) - 0.006f);
        }, vec3(0, 0.18f, 0.165f), 0.24f));
  // portcullis grid (slides up into the wall)
  m.addR(CB_GATE, MT_IRON, 0.0f, fnS([arch](const vec3& p) {
           vec3 q = p - vec3(0, 0, 0.122f);
           float bars = std::min(std::fabs(std::fmod(q.x + 1.0f, 0.034f) - 0.017f), std::fabs(std::fmod(q.y + 1.0f, 0.045f) - 0.0225f)) - 0.0035f;
           float plane = std::fabs(q.z) - 0.0045f;
           float inside = arch(p) + 0.004f;
           return std::max(std::max(bars, plane), inside);
         }, vec3(0, 0.17f, 0.122f), 0.22f));
  // drawbridge (hinged at its foot), planks and iron straps
  {
    const float bw = 0.105f, bh = 0.355f, bz = 0.18f;
    m.addR(CB_BRIDGE, MT_WOOD, 0.0f, fnS([bw, bh, bz](const vec3& p) {
             vec3 q = p - vec3(0, bh * 0.5f + 0.004f, bz);
             float b = roundBox(q, vec3(bw, bh * 0.5f, 0.011f), 0.003f);
             float plank = std::fabs(std::fmod(q.x + 1.0f, 0.035f) - 0.0175f) - 0.0012f;
             return std::max(b, -std::max(plank, -(q.z - 0.009f)));
           }, vec3(0, bh * 0.5f, bz), 0.2f));
    for (int k = 0; k < 3; ++k)
      m.addR(CB_BRIDGE, MT_IRON, 0.0f, boxS(vec3(0, 0.05f + k * 0.13f, bz + 0.011f), vec3(bw - 0.006f, 0.008f, 0.003f), 0.002f));
    // chains from the top corners to the wall above the arch: they stretch as the bridge lowers
    for (int sd = -1; sd <= 1; sd += 2) {
      vec3 a(sd * 0.095f, bh - 0.01f, bz + 0.012f), b(sd * 0.11f, 0.52f, 0.155f);
      m.addStretch(CB_BRIDGE, CB_BODY, MT_IRON, 0.002f, capsuleS(a, b, 0.0045f), along(a, b));
      m.add(CB_BODY, MT_DARK, 0.002f, sphereS(b + vec3(0, 0, 0.006f), 0.01f));
    }
  }
  // flag on the left tower
  {
    vec3 base(TX, TH, TZ);
    m.addR(CB_FLAG, MT_WOOD, 0.0f, cylS(base, base + vec3(0, 0.3f, 0), 0.006f));
    m.addR(CB_FLAG, MT_GOLD, 0.002f, sphereS(base + vec3(0, 0.305f, 0), 0.011f));
    vec3 fc = base + vec3(0, 0.25f, 0);
    m.addR(CB_FLAG, MT_CLOTH2, 0.0f, fnS([fc](const vec3& p) {
             vec3 q = p - fc;  // the banner streams outward (+x) from the pole
             const float len = 0.16f;
             float wave = 0.01f * std::sin(q.x * 38.0f) * glm::clamp(q.x / 0.04f, 0.0f, 1.0f);
             float notch = q.x > len * 0.65f ? 0.5f * (q.x - len * 0.65f) - std::fabs(q.y) : -1.0f;
             float d = std::max(std::max(std::fabs(q.y) - 0.045f, std::max(-q.x, q.x - len)), std::fabs(q.z - wave) - 0.003f);
             return std::max(d, notch);
           }, fc + vec3(0.08f, 0, 0), 0.1f));
    m.paint(MT_CLOTH, fnS([fc](const vec3& p) {
              vec3 q = p - fc;
              return std::min(std::fabs(q.y) - 0.01f, std::fabs(q.x - 0.055f) - 0.01f);
            }, fc + vec3(0.06f, 0, 0), 0.12f), MT_CLOTH2);
  }
  return m;
}

// The gatehouse's stone legs, sculpted standing (hips just under the tower floor).
Model castleLegsModel() {
  using namespace cj;
  Model m;
  m.skel = &skeleton(A_LEGS);
  for (int sd = -1; sd <= 1; sd += 2) {
    bool L = sd > 0;
    vec3 hip = L ? hipL : mx(hipL), knee = L ? kneeL : mx(kneeL), ankle = L ? ankleL : mx(ankleL);
    int th = L ? CB_LTHIGH : CB_RTHIGH, sh = L ? CB_LSHIN : CB_RSHIN;
    // rough-hewn blocks, a boulder at the knee and a broad stone foot
    m.add(th, MT_STONE, 0.012f, boxS(hip * 0.6f + knee * 0.4f + vec3(0, -0.01f, 0), vec3(0.07f, 0.07f, 0.07f), 0.02f,
                                       frameY(hip - knee)));
    m.add(th, MT_STONE, 0.012f, boxS(hip * 0.15f + knee * 0.85f, vec3(0.06f, 0.05f, 0.062f), 0.018f, frameY(hip - knee)));
    m.add(sh, MT_STONE, 0.012f, sphereS(knee, 0.065f));
    m.add(sh, MT_STONE, 0.012f, boxS(knee * 0.55f + ankle * 0.45f, vec3(0.055f, 0.08f, 0.055f), 0.016f, frameY(knee - ankle)));
    m.add(sh, MT_STONE, 0.012f, boxS(ankle + vec3(0, -0.005f, 0), vec3(0.05f, 0.04f, 0.05f), 0.015f));
    m.add(sh, MT_STONE, 0.01f, boxS(vec3(ankle.x, ankle.y - 0.04f, ankle.z + 0.04f), vec3(0.058f, 0.032f, 0.105f), 0.02f));
    for (int t = -1; t <= 1; ++t)
      m.add(sh, MT_STONE, 0.008f, ellS(vec3(ankle.x + t * 0.035f, ankle.y - 0.05f, ankle.z + 0.14f), vec3(0.018f, 0.02f, 0.022f)));
  }
  return m;
}

// --- the dragon -----------------------------------------------------------------
float dot2(const vec3& v) { return glm::dot(v, v); }
float triangleDist(const vec3& p, const vec3& a, const vec3& b, const vec3& c) {
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
Shape triS(vec3 a, vec3 b, vec3 c, float t) {
  vec3 m = (a + b + c) / 3.0f;
  float r = std::max(glm::length(a - m), std::max(glm::length(b - m), glm::length(c - m))) + t;
  return {[a, b, c, t](const vec3& p) { return triangleDist(p, a, b, c) - t; }, m, r};
}

Model dragonModel() {
  using namespace dj;
  Model m;
  m.skel = &skeleton(A_DRAGON);
  // body and belly
  m.add(DB_BODY, MT_SCALE, 0.0f, ellS(vec3(0, 0.35f, -0.02f), vec3(0.155f, 0.15f, 0.28f)));
  m.add(DB_BODY, MT_SCALE, 0.08f, ellS(vec3(0, 0.38f, 0.13f), vec3(0.14f, 0.15f, 0.14f)));
  m.paint(MT_HORN, fnS([](const vec3& p) {  // ribbed belly plates
            float band = std::fabs(std::fmod(p.z + 10.0f, 0.04f) - 0.02f) - 0.016f;
            float region = std::max(std::max(p.y - 0.27f, std::fabs(p.x) - 0.085f), std::fabs(p.z - 0.04f) - 0.27f);
            return std::max(region, band);
          }, vec3(0, 0.25f, 0), 0.4f), MT_SCALE);
  // neck: three segments
  m.add(DB_NECK1, MT_SCALE, 0.05f, coneS(vec3(0, 0.4f, 0.17f), neck2, 0.1f, 0.078f));
  m.add(DB_NECK2, MT_SCALE, 0.04f, coneS(neck2, neck3, 0.078f, 0.064f));
  m.add(DB_NECK3, MT_SCALE, 0.04f, coneS(neck3, head + vec3(0, 0.02f, 0.02f), 0.062f, 0.055f));
  // head: skull, long snout, brows, horns, glowing eyes, nostrils, teeth
  m.add(DB_HEAD, MT_SCALE, 0.03f, ellS(vec3(0, 0.705f, 0.54f), vec3(0.058f, 0.05f, 0.07f)));
  m.add(DB_HEAD, MT_SCALE, 0.035f, coneS(vec3(0, 0.695f, 0.57f), vec3(0, 0.672f, 0.7f), 0.045f, 0.028f));
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    m.add(DB_HEAD, MT_SCALE, 0.015f, ellS(vec3(x * 0.038f, 0.735f, 0.57f), vec3(0.022f, 0.012f, 0.03f), rotXm(-0.3f)));
    m.add(DB_HEAD, MT_HORN, 0.008f, coneS(vec3(x * 0.035f, 0.735f, 0.5f), vec3(x * 0.07f, 0.79f, 0.4f), 0.014f, 0.006f));
    m.add(DB_HEAD, MT_HORN, 0.004f, coneS(vec3(x * 0.07f, 0.79f, 0.4f), vec3(x * 0.075f, 0.8f, 0.33f), 0.006f, 0.0015f));
    m.add(DB_HEAD, MT_HORN, 0.004f, coneS(vec3(x * 0.05f, 0.69f, 0.5f), vec3(x * 0.09f, 0.7f, 0.45f), 0.009f, 0.002f));
    m.sub(MT_SCALE, 0.004f, sphereS(vec3(x * 0.044f, 0.716f, 0.592f), 0.015f));
    m.add(DB_HEAD, MT_EYE, 0.002f, sphereS(vec3(x * 0.041f, 0.716f, 0.59f), 0.0125f));
    m.paint(MT_DARK, sphereS(vec3(x * 0.016f, 0.69f, 0.722f), 0.007f), MT_SCALE);
    for (int t = 0; t < 4; ++t) {  // upper fangs
      vec3 r(x * (0.028f - t * 0.003f), 0.655f, 0.6f + t * 0.03f);
      m.add(DB_HEAD, MT_HORN, 0.002f, coneS(r, r + vec3(0, -0.022f + t * 0.003f, 0.003f), 0.005f, 0.0012f));
    }
  }
  // lower jaw with its own teeth; the inside of the mouth is dark red
  m.add(DB_JAW, MT_SCALE, 0.02f, coneS(jaw + vec3(0, -0.005f, 0), vec3(0, 0.632f, 0.69f), 0.03f, 0.02f));
  for (int sd = -1; sd <= 1; sd += 2)
    for (int t = 0; t < 3; ++t) {
      vec3 r(sd * (0.02f - t * 0.003f), 0.648f, 0.62f + t * 0.025f);
      m.add(DB_JAW, MT_HORN, 0.002f, coneS(r, r + vec3(0, 0.018f, 0), 0.004f, 0.001f));
    }
  m.paint(MT_DARK, fnS([](const vec3& p) {
            return std::max(std::max(std::fabs(p.y - 0.652f) - 0.012f, 0.58f - p.z), std::fabs(p.x) - 0.03f);
          }, vec3(0, 0.65f, 0.65f), 0.1f), MT_SCALE);
  // spines along the back, from the head to the tail
  for (int i = 0; i < 14; ++i) {
    float t = i / 13.0f;
    vec3 a;
    if (t < 0.35f) {
      float u = t / 0.35f;
      a = glm::mix(vec3(0, 0.74f, 0.47f), vec3(0, 0.49f, 0.17f), u);
    } else {
      float u = (t - 0.35f) / 0.65f;
      a = glm::mix(vec3(0, 0.49f, 0.12f), vec3(0, 0.22f, -0.72f), u);
    }
    float h = 0.045f * (1.0f - 0.6f * std::fabs(t - 0.4f));
    int bone = t < 0.12f ? DB_NECK3 : (t < 0.24f ? DB_NECK2 : (t < 0.33f ? DB_NECK1 : (t < 0.6f ? DB_BODY : (t < 0.75f ? DB_TAIL1 : DB_TAIL2))));
    m.add(bone, MT_HORN, 0.006f, coneS(a + vec3(0, -0.02f, 0), a + vec3(0, h, -0.02f), 0.014f, 0.002f));
  }
  // tail ending in a spade
  m.add(DB_TAIL1, MT_SCALE, 0.04f, coneS(vec3(0, 0.35f, -0.2f), tail2, 0.085f, 0.06f));
  m.add(DB_TAIL2, MT_SCALE, 0.03f, coneS(tail2, tail3, 0.06f, 0.042f));
  m.add(DB_TAIL3, MT_SCALE, 0.02f, coneS(tail3, tail4, 0.042f, 0.026f));
  m.add(DB_TAIL4, MT_SCALE, 0.015f, coneS(tail4, vec3(0, 0.08f, -0.9f), 0.026f, 0.012f));
  m.add(DB_TAIL4, MT_HORN, 0.006f, fnS([](const vec3& p) {
          vec3 q = p - vec3(0, 0.075f, -0.93f);
          float diamond = std::fabs(q.x) / 0.05f + std::fabs(q.z + 0.005f) / 0.06f - 1.0f;
          return std::max(diamond * 0.035f, std::fabs(q.y) - 0.006f);
        }, vec3(0, 0.075f, -0.93f), 0.07f));
  // legs with claws
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int f = sd > 0 ? DB_FL : DB_FR, h = sd > 0 ? DB_HL : DB_HR;
    m.add(f, MT_SCALE, 0.03f, coneS(vec3(x * 0.11f, 0.32f, 0.14f), vec3(x * 0.14f, 0.15f, 0.19f), 0.058f, 0.04f));
    m.add(f, MT_SCALE, 0.02f, coneS(vec3(x * 0.14f, 0.15f, 0.19f), vec3(x * 0.14f, 0.03f, 0.2f), 0.04f, 0.032f));
    m.add(h, MT_SCALE, 0.04f, ellS(vec3(x * 0.125f, 0.3f, -0.12f), vec3(0.075f, 0.1f, 0.095f)));
    m.add(h, MT_SCALE, 0.03f, coneS(vec3(x * 0.14f, 0.24f, -0.08f), vec3(x * 0.15f, 0.12f, -0.16f), 0.05f, 0.036f));
    m.add(h, MT_SCALE, 0.02f, coneS(vec3(x * 0.15f, 0.12f, -0.16f), vec3(x * 0.15f, 0.03f, -0.12f), 0.034f, 0.028f));
    for (int legI = 0; legI < 2; ++legI) {
      int bone = legI == 0 ? f : h;
      vec3 foot = legI == 0 ? vec3(x * 0.14f, 0.022f, 0.21f) : vec3(x * 0.15f, 0.022f, -0.11f);
      m.add(bone, MT_SCALE, 0.012f, ellS(foot, vec3(0.038f, 0.022f, 0.042f)));
      for (int c = -1; c <= 1; ++c) {
        vec3 a = foot + vec3(c * 0.018f, -0.002f, 0.025f);
        m.add(bone, MT_HORN, 0.003f, coneS(a, a + vec3(c * 0.006f, -0.012f, 0.03f), 0.0065f, 0.0015f));
      }
    }
  }
  // wings: an arm, three fingers and the membrane between them
  for (int sd = -1; sd <= 1; sd += 2) {
    float x = (float)sd;
    int w1 = sd > 0 ? DB_LWING1 : DB_RWING1, w2 = sd > 0 ? DB_LWING2 : DB_RWING2;
    vec3 S0(x * 0.1f, 0.46f, 0.1f), W(x * 0.38f, 0.62f, 0.02f);
    vec3 F1(x * 0.7f, 0.6f, -0.1f), F2(x * 0.62f, 0.42f, -0.24f), F3(x * 0.44f, 0.3f, -0.3f), B(x * 0.09f, 0.38f, -0.18f);
    m.add(w1, MT_SCALE, 0.02f, coneS(S0, W, 0.03f, 0.018f));
    m.add(w2, MT_HORN, 0.004f, coneS(W + vec3(0, 0.01f, 0), W + vec3(x * 0.02f, 0.06f, 0.02f), 0.012f, 0.002f));  // thumb claw
    for (const vec3& F : {F1, F2, F3}) m.add(w2, MT_SCALE, 0.008f, coneS(W, F, 0.014f, 0.004f));
    const float t = 0.0085f;
    m.add(w1, MT_MEMBRANE, 0.006f, triS(S0, W, B, t));
    m.add(w2, MT_MEMBRANE, 0.006f, triS(W, F1, F2, t));
    m.add(w2, MT_MEMBRANE, 0.006f, triS(W, F2, F3, t));
    m.add(w2, MT_MEMBRANE, 0.006f, triS(W, F3, B, t));
  }
  return m;
}

// the man-at-arms' spear on its own: butt at the origin, pointing up +y
Model spearModel() {
  Model m;
  vec3 a(0, 0, 0), b(0, 0.964f, 0);
  m.add(0, MT_WOOD, 0.0f, cylS(a, b, 0.0075f));
  m.add(0, MT_IRON, 0.002f, cylS(a, a + vec3(0, 0.025f, 0), 0.0085f));
  m.add(0, MT_IRON, 0.002f, cylS(b - vec3(0, 0.02f, 0), b + vec3(0, 0.012f, 0), 0.0095f));
  m.add(0, MT_STEEL, 0.0f, fnS([b](const vec3& p) {
          vec3 q = p - (b + vec3(0, 0.01f, 0));
          float t = glm::clamp(q.y / 0.095f, 0.0f, 1.0f);
          float w = 0.019f * std::sin(std::pow(t, 0.7f) * 3.14159f) + 0.001f;
          float th = 0.0035f * (1.0f - t) + 0.001f;
          float d = std::max(std::fabs(q.x) / w + std::fabs(q.z) / th - 1.0f, 0.0f) * std::min(w, th);
          float body = std::max(std::fabs(q.x) - w, std::fabs(q.z) - th);
          return std::max(std::max(body, d), std::max(-q.y, q.y - 0.095f));
        }, b + vec3(0, 0.06f, 0), 0.07f));
  return m;
}

// ---------------------------------------------------------------------------
void buildAsset(meshgen::PieceAssets& A, Model& m, vec3 lo, vec3 hi, int quality, int seeds, uint32_t seed,
                float height, float radius) {
  const float c0[3] = {0.0085f, 0.0062f, 0.0048f};
  const float c1[3] = {0.016f, 0.013f, 0.011f};
  const float cs[3] = {0.014f, 0.011f, 0.009f};
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

}  // namespace

float pieceHeight(int kind) {
  static const float H[6] = {0.86f, 1.06f, 1.2f, 1.1f, 1.12f, 1.2f};
  return H[std::max(0, std::min(5, kind))];
}
float pieceRadius(int kind) {
  static const float R[6] = {0.2f, 0.32f, 0.3f, 0.36f, 0.3f, 0.31f};
  return R[std::max(0, std::min(5, kind))];
}
bool throned(int kind) { return kind == A_BISHOP || kind == A_QUEEN || kind == A_KING; }
int seatAsset(int kind) {
  if (kind == A_BISHOP) return A_CATHEDRA;
  if (kind == A_QUEEN || kind == A_KING) return A_THRONE;
  return -1;
}
float seatScale(int kind) { return kind == A_QUEEN ? seatedScale(A_QUEEN) : 1.0f; }
vec3 handR(int kind) {
  switch (kind) {
    case A_PAWN: return pawnJ().gripR;
    case A_KNIGHT: return kj::gripR;
    case A_BISHOP:
    case A_QUEEN:
    case A_KING: return seatedJ(kind).gripR;
    default: return vec3(0, 0.5f, 0.1f);
  }
}
vec3 gatePoint() { return vec3(0, 0.15f, 0.16f); }
float standOffset(int kind) {
  if (!throned(kind)) return 0;
  HumanJ j = seatedJ(kind);
  return j.kneeL.z - j.hipL.z;
}
float strideCycle(int kind) {
  switch (kind) {
    case A_PAWN: return 0.3f;
    case A_KNIGHT: return 0.62f;
    case A_ROOK: return 0.6f;
    default: return 0.42f * seatedScale(kind);
  }
}
vec3 itemTip(int kind) {
  switch (kind) {
    case A_PAWN: return pawnJ().gripR + vec3(0, 0.6f, 0);
    case A_KNIGHT: return kj::gripR + K(0, 0.81f, 0);
    case A_KING: return seatedJ(A_KING).gripR + vec3(0.48f, 0, 0);
    case A_QUEEN: return seatedJ(A_QUEEN).gripR + vec3(0, 0.29f, 0) * seatedScale(A_QUEEN);
    default: return handR(kind);
  }
}
float seatHeight(int kind) { return 0.30f * seatedScale(kind); }

void buildLibrary(Library& lib, int quality, meshgen::BuildProgress* prog) {
  quality = std::max(0, std::min(2, quality));
  if (prog) {
    prog->total = 9;
    prog->done = 0;
  }
  auto tick = [&](const char* s) {
    if (prog) {
      prog->stage = s;
      prog->done++;
    }
  };
  {
    Model m = pawnModel();
    buildAsset(lib.asset[A_PAWN], m, vec3(-0.21f, -0.005f, -0.1f), vec3(0.24f, 1.0f, 0.17f), quality, 10, 4321u,
               pieceHeight(A_PAWN), pieceRadius(A_PAWN));
    tick("Arming the men-at-arms");
  }
  {
    Model m = knightModel();
    buildAsset(lib.asset[A_KNIGHT], m, vec3(-0.25f, -0.005f, -0.42f), vec3(0.25f, 1.42f, 0.55f), quality, 14, 4421u,
               pieceHeight(A_KNIGHT), pieceRadius(A_KNIGHT));
    tick("Saddling the knights");
  }
  {
    Model m = bishopModel();
    float S = seatedScale(A_BISHOP);
    buildAsset(lib.asset[A_BISHOP], m, vec3(-0.25f, -0.005f, -0.2f) * S, vec3(0.25f, 1.3f, 0.36f) * S, quality, 12,
               4521u, pieceHeight(A_BISHOP), pieceRadius(A_BISHOP));
    tick("Consecrating the bishops");
  }
  {
    Model m = castleModel();
    buildAsset(lib.asset[A_ROOK], m, vec3(-0.37f, -0.005f, -0.29f), vec3(0.37f, 1.24f, 0.26f), quality, 14, 4621u,
               pieceHeight(A_ROOK), pieceRadius(A_ROOK));
    tick("Raising the gatehouses");
  }
  {
    Model m = queenModel();
    float S = seatedScale(A_QUEEN);
    buildAsset(lib.asset[A_QUEEN], m, vec3(-0.26f, -0.005f, -0.22f) * S, vec3(0.26f, 1.1f, 0.36f) * S, quality, 13,
               4721u, pieceHeight(A_QUEEN), pieceRadius(A_QUEEN));
    tick("Crowning the queens");
  }
  {
    Model m = kingModel();
    buildAsset(lib.asset[A_KING], m, vec3(-0.26f, -0.005f, -0.22f), vec3(0.42f, 1.14f, 0.36f), quality, 14, 4821u,
               pieceHeight(A_KING), pieceRadius(A_KING));
    tick("Crowning the kings");
  }
  {
    Model m = throneModel(false);
    buildAsset(lib.asset[A_THRONE], m, vec3(-0.3f, -0.005f, -0.3f), vec3(0.3f, 1.12f, 0.16f), quality, 12, 4921u,
               1.1f, 0.3f);
    Model c = throneModel(true);
    float S = seatedScale(A_BISHOP);
    buildAsset(lib.asset[A_CATHEDRA], c, vec3(-0.3f, -0.005f, -0.3f) * S, vec3(0.3f, 1.32f, 0.16f) * S, quality, 12,
               5021u, 1.25f, 0.3f);
    tick("Carving the thrones");
  }
  {
    Model m = dragonModel();
    buildAsset(lib.asset[A_DRAGON], m, vec3(-0.74f, -0.01f, -0.98f), vec3(0.74f, 0.86f, 0.76f), quality, 0, 0u, 0.8f,
               0.4f);
    tick("Waking the dragon");
  }
  {
    Model m = spearModel();
    buildAsset(lib.asset[A_SPEAR], m, vec3(-0.03f, -0.01f, -0.03f), vec3(0.03f, 1.08f, 0.03f), quality, 0, 0u, 1.07f,
               0.02f);
  }
  {
    Model m = castleLegsModel();
    buildAsset(lib.asset[A_LEGS], m, vec3(-0.22f, -0.43f, -0.16f), vec3(0.22f, 0.12f, 0.22f), quality, 0, 0u, 0.5f,
               0.2f);
    tick("Quarrying the stone legs");
  }
  lib.built = true;
  if (prog) prog->finished = true;
}

}  // namespace medieval
