// CPU-baked procedural textures: inlaid wooden chessboard, 3D noise.
#pragma once
#include <cstdint>
#include <vector>

namespace tex {

struct Image {
  int w = 0, h = 0, d = 1, ch = 4;
  std::vector<uint8_t> px;
};

// Board top: 'albedo' (sRGB rgb + roughness in a) and 'detail'
// (r,g = normal xy * 0.5 + 0.5, b = gold inlay mask, a = cavity/seam).
// Covers [-half, half]^2 in world xz with +z toward white.
void bakeBoard(int res, float half, Image& albedo, Image& detail);

// Tileable 3D noise: r,g = fbm, b = high-freq value noise, a = worley edge distance.
void bakeNoise3D(int res, Image& out);

}  // namespace tex
