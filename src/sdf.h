// Signed-distance-field modelling toolkit + a sparse Surface-Nets mesher.
// All chess pieces, their fracture fragments and the sword are sculpted
// procedurally from SDFs at startup - no model files are needed.
#pragma once
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace sdf {
using glm::vec2;
using glm::vec3;
using Fn = std::function<float(const vec3&)>;

// ---- primitives (Inigo Quilez style) ----------------------------------------
inline float sphere(const vec3& p, float r) { return glm::length(p) - r; }
float box(const vec3& p, const vec3& b);
float roundBox(const vec3& p, const vec3& b, float r);
float torus(const vec3& p, float R, float r);  // in xz plane
float capsule(const vec3& p, const vec3& a, const vec3& b, float r);
float roundCone(const vec3& p, const vec3& a, const vec3& b, float r1, float r2);
float ellipsoid(const vec3& p, const vec3& r);
float cylinder(const vec3& p, float halfH, float r);  // y axis
float smin(float a, float b, float k);
float smax(float a, float b, float k);
vec3 rotY(const vec3& p, float a);
vec3 rotX(const vec3& p, float a);
vec3 rotZ(const vec3& p, float a);

// Solid of revolution around +Y defined by an outline in (radius, height)
// that starts and ends on the axis. Corners are rounded by Chaikin subdivision.
// Points tagged 'sharp' (repeat a point twice) stay crisper.
struct Profile {
  struct Edge {
    vec2 a, e;
    float inv, ymin, ymax;
    bool outline;
  };
  std::vector<vec2> pts;
  std::vector<Edge> edges;
  float yMin = 0, yMax = 0, rMax = 0;
  void build(const std::vector<vec2>& outline, int chaikinIterations = 3);
  float dist2D(const vec2& p) const;
  float eval(const vec3& p) const { return dist2D(vec2(std::sqrt(p.x * p.x + p.z * p.z), p.y)); }
};

// ---- meshing ----------------------------------------------------------------
struct Vertex {
  vec3 pos;
  vec3 nrm;
  float ao;   // baked ambient occlusion (1 = open)
  float aux;  // fragment: 1 on fresh fracture faces; sword: material id
};

struct MeshData {
  std::vector<Vertex> v;
  std::vector<uint32_t> idx;
  vec3 bmin{0}, bmax{0};
  void computeBounds();
  void translate(const vec3& d);
};

struct MeshOptions {
  float cell = 0.01f;
  vec3 bmin{-1}, bmax{1};
  bool computeAO = true;
  float aoRadius = 0.12f;
  int newtonSteps = 2;
  Fn aux;             // optional per-vertex aux attribute
  Fn normalField;     // optional field used for normals/projection (defaults to f)
};

MeshData polygonize(const Fn& f, const MeshOptions& opt);

// Generic helpers for non-SDF meshes
MeshData makeUVSphere(float r, int seg, int rings);

// Parallel-for helper over [0,n)
void parallelFor(int n, const std::function<void(int)>& fn);

}  // namespace sdf
