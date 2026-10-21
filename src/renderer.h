// Physically based renderer: IBL, PCSS shadows, planar reflections, SSAO,
// MSAA HDR pipeline, depth of field, bloom and filmic tonemapping.
#pragma once
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "gl.h"
#include "medieval.h"
#include "meshgen.h"
#include "textures.h"

struct GpuMesh {
  GLuint vao = 0, vbo = 0, ebo = 0;
  GLsizei count = 0;
  void upload(const sdf::MeshData& m);
  void draw() const;
  void destroy();
};

class Shader {
 public:
  bool build(const std::string& name, const std::string& vs, const std::string& fs);
  void use() const;
  GLint loc(const char* n);
  void set(const char* n, int v);
  void set(const char* n, float v);
  void set(const char* n, const glm::vec2& v);
  void set(const char* n, const glm::vec3& v);
  void set(const char* n, const glm::vec4& v);
  void set(const char* n, const glm::mat4& v);
  void setArray(const char* n, const glm::vec4* v, int count);
  void setArray(const char* n, const glm::vec3* v, int count);
  GLuint id() const { return prog_; }

 private:
  GLuint prog_ = 0;
  std::unordered_map<std::string, GLint> locs_;
};

enum Material { MAT_WHITE = 0, MAT_BLACK = 1, MAT_SWORD = 2, MAT_IRON = 3, MAT_MEDIEVAL = 4 };

struct DrawItem {
  const GpuMesh* mesh = nullptr;
  const GpuMesh* meshLow = nullptr;
  glm::mat4 model{1.0f};
  glm::vec4 rig{0, 0, 1, 0};  // bend, bendDir, squash, twist
  float rigH = 1.0f;
  int material = MAT_WHITE;
  glm::vec3 texOffset{0};
  glm::vec4 glow{0};
  float crack = 0;
  glm::vec3 crackColor{1.0f, 0.6f, 0.25f};
  float dissolve = 0;
  glm::vec3 dissolveColor{1.0f, 0.8f, 0.4f};
  float heat = 0;
  float stone = 0;  // medieval: turned to salt
  bool castShadow = true;
  bool reflect = true;
  bool groundAO = true;
  const glm::mat4* bones = nullptr;  // skinned meshes (medieval figures)
  int boneCount = 0;
  int team = 0;                      // medieval: 0 white army, 1 black army
};

struct PointLight {
  glm::vec3 pos;
  float radius;
  glm::vec3 color;
};

// 16 floats per particle, matches the particle vertex shader layout
struct ParticleGPU {
  glm::vec4 posSize;
  glm::vec4 color;    // rgb (premultiplied), alpha (0 = additive)
  glm::vec4 velType;  // velocity (for stretching), type
  glm::vec4 misc;     // rotation, stretch, seed, life fraction
};

struct RenderSettings {
  int shadowRes = 4096;
  int msaa = 4;
  bool ssao = true;
  bool reflections = true;
  bool dof = true;
  bool bloom = true;
  bool shadows = true;
  float renderScale = 1.0f;
};

struct SceneView {
  glm::mat4 view{1.0f}, proj{1.0f};
  glm::vec3 camPos{0};
  float nearZ = 0.1f, farZ = 100.0f;
  float focusDist = 12.0f;
  float aperture = 0.0f;  // CoC scale in half-res pixels (0 disables DoF)
  float time = 0;
  float exposure = 1.0f;
  float fade = 1.0f;
  glm::vec3 flash{0};
  std::vector<DrawItem> items;
  std::vector<PointLight> lights;
  std::vector<ParticleGPU> particles;
  uint8_t boardState[64 * 4] = {};
  glm::vec4 check{0};
  std::vector<glm::vec4> occluders;  // x, z, radius, strength
  std::vector<glm::vec4> rings, ringColors;  // ground shockwaves
};

class Renderer {
 public:
  bool init(const RenderSettings& s);
  void applySettings(const RenderSettings& s);
  const RenderSettings& settings() const { return set_; }
  void setRenderScale(float s) { set_.renderScale = s; }
  void uploadLibrary(const meshgen::Library& lib);
  void uploadBoardTextures(const tex::Image& albedo, const tex::Image& detail, float half);
  void render(const SceneView& sv, int fbW, int fbH, GLuint outFbo);
  std::string glInfo() const { return info_; }

  struct PieceGpu {
    GpuMesh lod0, lod1;
    std::vector<GpuMesh> shards, halves;
  };
  PieceGpu pieces[meshgen::K_COUNT];
  GpuMesh sword, cannonball, board;
  bool libraryReady = false;
  PieceGpu medieval[medieval::A_COUNT];
  bool medievalReady = false;
  void uploadMedieval(const medieval::Library& lib);

 private:
  void buildPrograms();
  void buildIBL();
  void createTargets(int w, int h);
  void destroyTargets();
  void setLighting(Shader& s, const SceneView& sv, bool reflectionPass);
  void drawItems(Shader& s, const SceneView& sv, bool lowLod, bool shadowPass, bool reflectPass);
  void drawBoard(Shader& s);
  void fullscreen();
  void bindTex(int unit, GLenum target, GLuint tex);

  RenderSettings set_;
  std::string info_;
  Shader shShadow, shPrepass, shObject, shBoard, shBackground;
  Shader shEnv, shIrr, shPrefilter, shBrdf;
  Shader shSsao, shSsaoBlur, shBloomDown, shBloomUp, shDofCoc, shDofBokeh, shDofComp, shComposite;
  Shader shParticle;

  GLuint emptyVao = 0;
  GLuint quadVbo = 0, partVbo = 0, partVao = 0;
  size_t partCap = 0;

  GLuint noiseTex = 0, boardAlbedo = 0, boardDetail = 0, boardStateTex = 0;
  float boardHalf = 4.62f;
  GLuint envCube = 0, envNoKeyCube = 0, irrCube = 0, prefCube = 0, brdfLut = 0;
  int envRes = 256, prefRes = 128, prefMips = 6;

  GLuint shadowFbo = 0, shadowTex = 0, sampCmp = 0, sampRaw = 0;
  int shadowResAlloc = 0;
  glm::mat4 lightVP{1.0f};
  glm::vec3 lightDir{0};
  float shadowWorld = 14.0f, shadowRange = 50.0f;

  int tw = 0, th = 0;  // internal render size
  GLuint msFbo = 0, msColor = 0, msDepth = 0;
  GLuint hdrFbo = 0, hdrTex = 0;
  GLuint preFbo = 0, preNormal = 0, preDepth = 0;
  GLuint ssaoFbo = 0, ssaoTex = 0, ssaoBlurFbo = 0, ssaoBlurTex = 0;
  GLuint reflFbo = 0, reflTex = 0, reflDepth = 0;
  int rw = 0, rh = 0;
  GLuint dofCocFbo = 0, dofCocTex = 0, dofBokehFbo = 0, dofBokehTex = 0, dofOutFbo = 0, dofOutTex = 0;
  static const int kBloomLevels = 6;
  GLuint bloomFbo[kBloomLevels] = {}, bloomTex[kBloomLevels] = {};
  int bloomW[kBloomLevels] = {}, bloomH[kBloomLevels] = {};
  glm::vec3 ssaoKernel[16];
  float maxAniso = 1.0f;
};
