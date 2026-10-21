// Procedural geometry: Staunton pieces, fracture shards, weapons, board.
#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "sdf.h"

namespace meshgen {

enum Kind { K_PAWN = 0, K_KNIGHT, K_BISHOP, K_ROOK, K_QUEEN, K_KING, K_COUNT };
inline int kindFromType(int chessType) { return chessType - 1; }

struct Fragment {
  sdf::MeshData mesh;              // vertices relative to 'centroid'
  glm::vec3 centroid{0};           // in piece-local space
  float volume = 0;
  float radius = 0;                // bounding radius around centroid
  std::vector<glm::vec3> hull;     // contact points (relative to centroid)
};

struct PieceAssets {
  sdf::MeshData lod0, lod1;
  std::vector<Fragment> shards;  // Voronoi fracture into chunks
  std::vector<Fragment> halves;  // two halves cut by a diagonal plane
  glm::vec3 sliceN{0.8f, 0.6f, 0};
  glm::vec3 slicePoint{0};
  float height = 1, radius = 0.37f;
};

struct Library {
  PieceAssets piece[K_COUNT];
  sdf::MeshData sword;       // aux: 0 steel, 1 gold, 2 leather
  sdf::MeshData cannonball;
  sdf::MeshData board;       // closed slab, y=0 is the playing surface
  float boardHalf = 4.62f, boardThickness = 0.32f;
};

struct BuildProgress {
  std::atomic<int> done{0};
  std::atomic<int> total{1};
  std::atomic<bool> finished{false};
  std::atomic<const char*> stage{""};  // always a string literal (thread safe)
};

// quality: 0 = fast, 1 = normal, 2 = ultra, 3 = extreme
void buildLibrary(Library& lib, int quality, BuildProgress* progress);
float pieceHeight(int kind);
float pieceRadius(int kind);
sdf::Fn pieceField(int kind);

// Fracture of any solid inside [lo, hi] (shared with the medieval set).
// 'skin' (optional) fills each vertex's material while it is still in piece space.
std::vector<Fragment> voronoiShatter(const sdf::Fn& F, const glm::vec3& lo, const glm::vec3& hi, int nSeeds,
                                     uint32_t seedVal, float cell,
                                     const std::function<void(sdf::Vertex&)>& skin = nullptr);
std::vector<Fragment> sliceHalves(const sdf::Fn& F, const glm::vec3& lo, const glm::vec3& hi, const glm::vec3& n,
                                  const glm::vec3& p0, float cell,
                                  const std::function<void(sdf::Vertex&)>& skin = nullptr);

}  // namespace meshgen
