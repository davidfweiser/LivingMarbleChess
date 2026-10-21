// Building skeleton poses: per-bone rotations, offsets and scales about the rest
// joints, chained down the hierarchy (shared by the skinned armies).
#pragma once
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "medieval.h"

namespace posekit {
using glm::mat4;
using glm::quat;
using glm::vec3;
using medieval::kMaxBones;
using medieval::Pose;
using medieval::Skeleton;

constexpr float PI = 3.14159265f;
inline const vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
inline quat aa(float a, const vec3& axis) { return glm::angleAxis(a, axis); }
// pitch a limb that hangs down: + swings it forward (+z)
inline quat swing(float a) { return aa(-a, X); }
// bend a knee: + folds the shin backwards
inline quat knee(float a) { return aa(a, X); }
inline float smooth(float t) {
  t = glm::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

struct Builder {
  const Skeleton& s;
  quat R[kMaxBones];
  vec3 off[kMaxBones];
  float sc[kMaxBones];
  explicit Builder(const Skeleton& sk) : s(sk) {
    for (int i = 0; i < kMaxBones; ++i) {
      R[i] = quat(1, 0, 0, 0);
      off[i] = vec3(0);
      sc[i] = 1;
    }
  }
  // rotate bone b about an arbitrary pivot (in its parent's space) instead of its joint
  void rotateAbout(int b, const quat& q, const vec3& pivot) {
    vec3 j = s.joint[b];
    R[b] = q * R[b];
    off[b] += (pivot - j) - q * (pivot - j);
  }
  void finish(Pose& out) const {
    out.count = s.count;
    for (int b = 0; b < s.count; ++b) {
      const vec3& j = s.joint[b];
      mat4 L = glm::translate(mat4(1.0f), j + off[b]) * glm::mat4_cast(R[b]) * glm::scale(mat4(1.0f), vec3(sc[b])) *
               glm::translate(mat4(1.0f), -j);
      out.bone[b] = s.parent[b] >= 0 ? out.bone[s.parent[b]] * L : L;
    }
  }
};

}  // namespace posekit
