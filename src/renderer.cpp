#include "renderer.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <numeric>
#include <random>

#include "shaders.h"

// ---------------------------------------------------------------------------
// GpuMesh
// ---------------------------------------------------------------------------
void GpuMesh::upload(const sdf::MeshData& m) {
  destroy();
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(m.v.size() * sizeof(sdf::Vertex)), m.v.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(m.idx.size() * sizeof(uint32_t)), m.idx.data(), GL_STATIC_DRAW);
  const GLsizei st = sizeof(sdf::Vertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)12);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, st, (void*)24);
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*)28);
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, st, (void*)32);
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, st, (void*)36);
  glBindVertexArray(0);
  count = (GLsizei)m.idx.size();
}
void GpuMesh::draw() const {
  if (!vao) return;
  glBindVertexArray(vao);
  glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}
void GpuMesh::destroy() {
  if (vbo) glDeleteBuffers(1, &vbo);
  if (ebo) glDeleteBuffers(1, &ebo);
  if (vao) glDeleteVertexArrays(1, &vao);
  vao = vbo = ebo = 0;
  count = 0;
}

// ---------------------------------------------------------------------------
// Shader
// ---------------------------------------------------------------------------
static const std::pair<const char*, int> kSamplerUnits[] = {
    {"uShadowCmp", 0},   {"uShadowRaw", 1}, {"uIrradiance", 2},  {"uPrefilter", 3}, {"uBrdfLut", 4},
    {"uSSAO", 5},        {"uNoise", 6},     {"uBoardAlbedo", 7}, {"uBoardDetail", 8}, {"uReflection", 9},
    {"uBoardState", 10}, {"uDepth", 11},    {"uEnvMap", 12},     {"uSrc", 13},      {"uColor", 13},
    {"uScene", 13},      {"uSharp", 13},    {"uAO", 13},         {"uNormal", 14},   {"uBloom", 14},
    {"uBlur", 14},       {"uCoc", 15}};

static GLuint compileStage(GLenum type, const std::string& src, const std::string& name) {
  GLuint s = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
    std::string log(std::max(len, 1), '\0');
    glGetShaderInfoLog(s, len, nullptr, &log[0]);
    std::fprintf(stderr, "[shader] %s (%s) compile error:\n%s\n", name.c_str(),
                 type == GL_VERTEX_SHADER ? "vs" : "fs", log.c_str());
    glDeleteShader(s);
    return 0;
  }
  return s;
}

bool Shader::build(const std::string& name, const std::string& vs, const std::string& fs) {
  GLuint v = compileStage(GL_VERTEX_SHADER, vs, name);
  GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, name);
  if (!v || !f) return false;
  prog_ = glCreateProgram();
  glAttachShader(prog_, v);
  glAttachShader(prog_, f);
  glLinkProgram(prog_);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint len = 0;
    glGetProgramiv(prog_, GL_INFO_LOG_LENGTH, &len);
    std::string log(std::max(len, 1), '\0');
    glGetProgramInfoLog(prog_, len, nullptr, &log[0]);
    std::fprintf(stderr, "[shader] %s link error:\n%s\n", name.c_str(), log.c_str());
    return false;
  }
  glUseProgram(prog_);
  for (const auto& su : kSamplerUnits) {
    GLint l = glGetUniformLocation(prog_, su.first);
    if (l >= 0) glUniform1i(l, su.second);
  }
  return true;
}
void Shader::use() const { glUseProgram(prog_); }
GLint Shader::loc(const char* n) {
  auto it = locs_.find(n);
  if (it != locs_.end()) return it->second;
  GLint l = glGetUniformLocation(prog_, n);
  locs_[n] = l;
  return l;
}
void Shader::set(const char* n, int v) { glUniform1i(loc(n), v); }
void Shader::set(const char* n, float v) { glUniform1f(loc(n), v); }
void Shader::set(const char* n, const glm::vec2& v) { glUniform2f(loc(n), v.x, v.y); }
void Shader::set(const char* n, const glm::vec3& v) { glUniform3f(loc(n), v.x, v.y, v.z); }
void Shader::set(const char* n, const glm::vec4& v) { glUniform4f(loc(n), v.x, v.y, v.z, v.w); }
void Shader::set(const char* n, const glm::mat4& v) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, glm::value_ptr(v)); }
void Shader::setArray(const char* n, const glm::vec4* v, int c) {
  if (c > 0) glUniform4fv(loc(n), c, glm::value_ptr(v[0]));
}
void Shader::setArray(const char* n, const glm::vec3* v, int c) {
  if (c > 0) glUniform3fv(loc(n), c, glm::value_ptr(v[0]));
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static GLuint makeTex2D(int w, int h, GLint ifmt, GLenum fmt, GLenum type, GLint filter, GLint wrap = GL_CLAMP_TO_EDGE,
                        const void* data = nullptr) {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter == GL_LINEAR_MIPMAP_LINEAR ? GL_LINEAR : filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  return t;
}
static GLuint makeFbo(GLuint color, GLuint depthTex = 0, GLuint depthRb = 0) {
  GLuint f;
  glGenFramebuffers(1, &f);
  glBindFramebuffer(GL_FRAMEBUFFER, f);
  if (color) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  if (depthTex) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
  if (depthRb) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);
  if (!color) {
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
  }
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (st != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[gl] framebuffer incomplete: 0x%x\n", st);
  return f;
}
static void delTex(GLuint& t) {
  if (t) glDeleteTextures(1, &t);
  t = 0;
}
static void delFbo(GLuint& f) {
  if (f) glDeleteFramebuffers(1, &f);
  f = 0;
}
static void delRb(GLuint& r) {
  if (r) glDeleteRenderbuffers(1, &r);
  r = 0;
}

void Renderer::bindTex(int unit, GLenum target, GLuint t) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(target, t);
}
void Renderer::fullscreen() {
  glBindVertexArray(emptyVao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ---------------------------------------------------------------------------
void Renderer::buildPrograms() {
  const std::string H = "#version 330 core\n";
  const std::string C = shaders::kCommon;
  const std::string L = shaders::kLighting;
  const std::string meshVS = H + shaders::kMeshVS;
  const std::string fsVS = H + shaders::kFullscreenVS;
  bool ok = true;
  ok &= shShadow.build("shadow", meshVS, H + C + shaders::kShadowFS);
  ok &= shPrepass.build("prepass", meshVS, H + C + shaders::kPrepassFS);
  ok &= shObject.build("object", meshVS, H + C + L + shaders::kObjectFS);
  ok &= shBoard.build("board", meshVS, H + C + L + shaders::kBoardFS);
  ok &= shBackground.build("background", fsVS, H + C + shaders::kBackgroundFS);
  ok &= shEnv.build("envgen", fsVS, H + C + shaders::kEnvGenFS);
  ok &= shIrr.build("irradiance", fsVS, H + C + shaders::kIrradianceFS);
  ok &= shPrefilter.build("prefilter", fsVS, H + C + shaders::kPrefilterFS);
  ok &= shBrdf.build("brdf", fsVS, H + C + shaders::kBrdfLutFS);
  ok &= shSsao.build("ssao", fsVS, H + C + shaders::kSsaoFS);
  ok &= shSsaoBlur.build("ssaoblur", fsVS, H + C + shaders::kSsaoBlurFS);
  ok &= shBloomDown.build("bloomdown", fsVS, H + C + shaders::kBloomDownFS);
  ok &= shBloomUp.build("bloomup", fsVS, H + C + shaders::kBloomUpFS);
  ok &= shDofCoc.build("dofcoc", fsVS, H + C + shaders::kDofCocFS);
  ok &= shDofBokeh.build("dofbokeh", fsVS, H + C + shaders::kDofBokehFS);
  ok &= shDofComp.build("dofcomp", fsVS, H + C + shaders::kDofCompositeFS);
  ok &= shComposite.build("composite", fsVS, H + C + shaders::kCompositeFS);
  ok &= shParticle.build("particle", H + shaders::kParticleVS, H + C + shaders::kParticleFS);
  if (!ok) std::fprintf(stderr, "[renderer] some shaders failed to build\n");
}

bool Renderer::init(const RenderSettings& s) {
  set_ = s;
  char buf[512];
  std::snprintf(buf, sizeof(buf), "%s | %s | %s", (const char*)glGetString(GL_VENDOR),
                (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
  info_ = buf;
  if (lmHasExtension("GL_EXT_texture_filter_anisotropic") || lmHasExtension("GL_ARB_texture_filter_anisotropic"))
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  glGenVertexArrays(1, &emptyVao);
  buildPrograms();

  // particle quad + instance buffers
  const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
  glGenVertexArrays(1, &partVao);
  glBindVertexArray(partVao);
  glGenBuffers(1, &quadVbo);
  glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
  glGenBuffers(1, &partVbo);
  glBindBuffer(GL_ARRAY_BUFFER, partVbo);
  partCap = 4096;
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(partCap * sizeof(ParticleGPU)), nullptr, GL_STREAM_DRAW);
  for (int i = 0; i < 4; ++i) {
    glEnableVertexAttribArray(1 + i);
    glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleGPU), (void*)(intptr_t)(i * 16));
    glVertexAttribDivisor(1 + i, 1);
  }
  glBindVertexArray(0);

  // 3D noise
  {
    tex::Image n;
    tex::bakeNoise3D(64, n);
    glGenTextures(1, &noiseTex);
    glBindTexture(GL_TEXTURE_3D, noiseTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, n.w, n.h, n.d, 0, GL_RGBA, GL_UNSIGNED_BYTE, n.px.data());
    glGenerateMipmap(GL_TEXTURE_3D);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
  }
  boardStateTex = makeTex2D(8, 8, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
  // placeholder board textures until the real ones are baked
  {
    uint8_t a[4] = {120, 80, 50, 100}, d[4] = {128, 128, 0, 255};
    boardAlbedo = makeTex2D(1, 1, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, GL_CLAMP_TO_EDGE, a);
    boardDetail = makeTex2D(1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, GL_CLAMP_TO_EDGE, d);
  }

  // SSAO kernel
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> U(0, 1);
  for (int i = 0; i < 16; ++i) {
    glm::vec3 k(U(rng) * 2 - 1, U(rng) * 2 - 1, U(rng));
    k = glm::normalize(k) * U(rng);
    float sc = (float)i / 16.0f;
    sc = 0.1f + 0.9f * sc * sc;
    ssaoKernel[i] = k * sc;
  }

  // light
  lightDir = glm::normalize(glm::vec3(-0.78f, 1.0f, 0.32f));
  glm::vec3 center(0, 0.3f, 0);
  float R = 7.0f;
  glm::mat4 lv = glm::lookAt(center + lightDir * 25.0f, center, glm::vec3(0, 1, 0));
  glm::mat4 lp = glm::ortho(-R, R, -R, R, 1.0f, 51.0f);
  lightVP = lp * lv;
  shadowWorld = 2 * R;
  shadowRange = 50.0f;
  glGenSamplers(1, &sampCmp);
  glSamplerParameteri(sampCmp, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glSamplerParameteri(sampCmp, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glSamplerParameteri(sampCmp, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(sampCmp, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(sampCmp, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glSamplerParameteri(sampCmp, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glGenSamplers(1, &sampRaw);
  glSamplerParameteri(sampRaw, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glSamplerParameteri(sampRaw, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glSamplerParameteri(sampRaw, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(sampRaw, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  buildIBL();
  applySettings(s);
  return true;
}

void Renderer::applySettings(const RenderSettings& s) {
  set_ = s;
  GLint maxSamples = 1;
  glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
  set_.msaa = std::min(set_.msaa, (int)maxSamples);
  if (shadowResAlloc != set_.shadowRes) {
    delFbo(shadowFbo);
    delTex(shadowTex);
    GLint maxTex = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    set_.shadowRes = std::min(set_.shadowRes, (int)maxTex);
    shadowTex = makeTex2D(set_.shadowRes, set_.shadowRes, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                          GL_NEAREST);
    shadowFbo = makeFbo(0, shadowTex);
    shadowResAlloc = set_.shadowRes;
  }
  tw = th = 0;  // force target re-creation
}

void Renderer::buildIBL() {
  struct FB {
    glm::vec3 f, r, u;
  };
  const FB faces[6] = {{{1, 0, 0}, {0, 0, -1}, {0, -1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
                       {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},   {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
                       {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}},  {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}}};
  auto makeCube = [](int res, bool mips) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_CUBE_MAP, t);
    for (int f = 0; f < 6; ++f)
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F, res, res, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    if (mips) glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    return t;
  };
  GLuint fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  auto renderFaces = [&](Shader& sh, GLuint cube, int res, int mip) {
    int r = std::max(1, res >> mip);
    glViewport(0, 0, r, r);
    for (int f = 0; f < 6; ++f) {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, cube, mip);
      sh.set("uFwd", faces[f].f);
      sh.set("uRight", faces[f].r);
      sh.set("uUp", faces[f].u);
      fullscreen();
    }
  };
  envCube = makeCube(envRes, true);
  shEnv.use();
  shEnv.set("uIncludeKey", 1);
  shEnv.set("uKeyDir", lightDir);
  renderFaces(shEnv, envCube, envRes, 0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

  envNoKeyCube = makeCube(128, true);
  shEnv.use();
  shEnv.set("uIncludeKey", 0);
  renderFaces(shEnv, envNoKeyCube, 128, 0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, envNoKeyCube);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

  irrCube = makeCube(32, false);
  shIrr.use();
  bindTex(12, GL_TEXTURE_CUBE_MAP, envNoKeyCube);
  renderFaces(shIrr, irrCube, 32, 0);

  prefCube = makeCube(prefRes, true);
  glBindTexture(GL_TEXTURE_CUBE_MAP, prefCube);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, prefMips - 1);
  shPrefilter.use();
  bindTex(12, GL_TEXTURE_CUBE_MAP, envCube);
  shPrefilter.set("uEnvRes", (float)envRes);
  for (int m = 0; m < prefMips; ++m) {
    shPrefilter.set("uRoughness", (float)m / (float)(prefMips - 1));
    renderFaces(shPrefilter, prefCube, prefRes, m);
  }

  brdfLut = makeTex2D(128, 128, GL_RG16F, GL_RG, GL_FLOAT, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLut, 0);
  glViewport(0, 0, 128, 128);
  shBrdf.use();
  fullscreen();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
}

void Renderer::uploadLibrary(const meshgen::Library& lib) {
  for (int k = 0; k < meshgen::K_COUNT; ++k) {
    pieces[k].lod0.upload(lib.piece[k].lod0);
    pieces[k].lod1.upload(lib.piece[k].lod1);
    pieces[k].shards.resize(lib.piece[k].shards.size());
    for (size_t i = 0; i < lib.piece[k].shards.size(); ++i) pieces[k].shards[i].upload(lib.piece[k].shards[i].mesh);
    pieces[k].halves.resize(lib.piece[k].halves.size());
    for (size_t i = 0; i < lib.piece[k].halves.size(); ++i) pieces[k].halves[i].upload(lib.piece[k].halves[i].mesh);
  }
  sword.upload(lib.sword);
  cannonball.upload(lib.cannonball);
  board.upload(lib.board);
  boardHalf = lib.boardHalf;
  libraryReady = true;
}

static void uploadFigures(Renderer::PieceGpu* gpu, const meshgen::PieceAssets* assets, int n) {
  for (int k = 0; k < n; ++k) {
    const meshgen::PieceAssets& A = assets[k];
    Renderer::PieceGpu& G = gpu[k];
    G.lod0.upload(A.lod0);
    G.lod1.upload(A.lod1);
    G.shards.resize(A.shards.size());
    for (size_t i = 0; i < A.shards.size(); ++i) G.shards[i].upload(A.shards[i].mesh);
    G.halves.resize(A.halves.size());
    for (size_t i = 0; i < A.halves.size(); ++i) G.halves[i].upload(A.halves[i].mesh);
  }
}

void Renderer::uploadMedieval(const medieval::Library& lib) {
  uploadFigures(medieval, lib.asset, medieval::A_COUNT);
  medievalReady = true;
}

void Renderer::uploadAlien(const alien::Library& lib) {
  uploadFigures(alien, lib.asset, alien::AL_COUNT);
  alienReady = true;
}

void Renderer::uploadBoardTextures(const tex::Image& albedo, const tex::Image& detail, float half) {
  delTex(boardAlbedo);
  delTex(boardDetail);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  boardAlbedo = makeTex2D(albedo.w, albedo.h, GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR_MIPMAP_LINEAR,
                          GL_CLAMP_TO_EDGE, albedo.px.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  if (maxAniso > 1.0f) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.0f, maxAniso));
  boardDetail = makeTex2D(detail.w, detail.h, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR_MIPMAP_LINEAR,
                          GL_CLAMP_TO_EDGE, detail.px.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  if (maxAniso > 1.0f) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.0f, maxAniso));
  boardHalf = half;
}

// ---------------------------------------------------------------------------
void Renderer::destroyTargets() {
  delFbo(msFbo);
  delRb(msColor);
  delRb(msDepth);
  delFbo(hdrFbo);
  delTex(hdrTex);
  delFbo(preFbo);
  delTex(preNormal);
  delTex(preDepth);
  delFbo(ssaoFbo);
  delTex(ssaoTex);
  delFbo(ssaoBlurFbo);
  delTex(ssaoBlurTex);
  delFbo(reflFbo);
  delTex(reflTex);
  delRb(reflDepth);
  delFbo(dofCocFbo);
  delTex(dofCocTex);
  delFbo(dofBokehFbo);
  delTex(dofBokehTex);
  delFbo(dofOutFbo);
  delTex(dofOutTex);
  for (int i = 0; i < kBloomLevels; ++i) {
    delFbo(bloomFbo[i]);
    delTex(bloomTex[i]);
  }
}

void Renderer::createTargets(int w, int h) {
  destroyTargets();
  tw = w;
  th = h;
  int samples = std::max(1, set_.msaa);
  glGenRenderbuffers(1, &msColor);
  glBindRenderbuffer(GL_RENDERBUFFER, msColor);
  if (samples > 1) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, w, h);
  else glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA16F, w, h);
  glGenRenderbuffers(1, &msDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
  if (samples > 1) glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
  else glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glGenFramebuffers(1, &msFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "[gl] msaa fbo incomplete\n");

  hdrTex = makeTex2D(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  hdrFbo = makeFbo(hdrTex);

  preNormal = makeTex2D(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST);
  preDepth = makeTex2D(w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST);
  preFbo = makeFbo(preNormal, preDepth);

  int hw = std::max(1, w / 2), hh = std::max(1, h / 2);
  ssaoTex = makeTex2D(hw, hh, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR);
  ssaoFbo = makeFbo(ssaoTex);
  ssaoBlurTex = makeTex2D(hw, hh, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR);
  ssaoBlurFbo = makeFbo(ssaoBlurTex);

  rw = hw;
  rh = hh;
  reflTex = makeTex2D(rw, rh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR_MIPMAP_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);
  glGenRenderbuffers(1, &reflDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, reflDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, rw, rh);
  reflFbo = makeFbo(reflTex, 0, reflDepth);

  dofCocTex = makeTex2D(hw, hh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  dofCocFbo = makeFbo(dofCocTex);
  dofBokehTex = makeTex2D(hw, hh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  dofBokehFbo = makeFbo(dofBokehTex);
  dofOutTex = makeTex2D(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  dofOutFbo = makeFbo(dofOutTex);

  int bw = hw, bh = hh;
  for (int i = 0; i < kBloomLevels; ++i) {
    bloomW[i] = std::max(1, bw);
    bloomH[i] = std::max(1, bh);
    bloomTex[i] = makeTex2D(bloomW[i], bloomH[i], GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    bloomFbo[i] = makeFbo(bloomTex[i]);
    bw /= 2;
    bh /= 2;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
void Renderer::setLighting(Shader& s, const SceneView& sv, bool reflectionPass) {
  glm::vec3 cam = sv.camPos;
  if (reflectionPass) cam.y = -cam.y;
  s.set("uCamPos", cam);
  s.set("uLightDir", lightDir);
  s.set("uLightColor", glm::vec3(1.0f, 0.92f, 0.82f) * 4.0f);
  s.set("uLightVP", lightVP);
  s.set("uShadowTexel", 1.0f / (float)shadowResAlloc);
  s.set("uShadowWorld", shadowWorld);
  s.set("uShadowRange", shadowRange);
  s.set("uShadowsOn", set_.shadows ? 1 : 0);
  s.set("uPrefilterMaxLod", (float)(prefMips - 1));
  s.set("uAmbient", 0.72f);
  int nl = std::min(8, (int)sv.lights.size());
  glm::vec4 lp[8], lc[8];
  for (int i = 0; i < nl; ++i) {
    lp[i] = glm::vec4(sv.lights[i].pos, sv.lights[i].radius);
    lc[i] = glm::vec4(sv.lights[i].color, 0);
  }
  s.set("uNumLights", nl);
  s.setArray("uLightPos", lp, nl);
  s.setArray("uLightCol", lc, nl);
  s.set("uSSAOOn", (!reflectionPass && set_.ssao) ? 1 : 0);
  s.set("uInvScreen", reflectionPass ? glm::vec2(1.0f / rw, 1.0f / rh) : glm::vec2(1.0f / tw, 1.0f / th));
  s.set("uTime", sv.time);
  s.set("uBoardHalf", boardHalf);
}

void Renderer::drawItems(Shader& s, const SceneView& sv, bool lowLod, bool shadowPass, bool reflectPass, bool glass) {
  for (const DrawItem& it : sv.items) {
    if (it.glass != glass || it.ghost > 0.0f) continue;
    if (shadowPass && !it.castShadow) continue;
    if (reflectPass && !it.reflect) continue;
    drawItem(s, it, lowLod, shadowPass);
  }
}

void Renderer::drawItem(Shader& s, const DrawItem& it, bool lowLod, bool shadowPass) {
  {
    const GpuMesh* m = (lowLod && it.meshLow) ? it.meshLow : it.mesh;
    if (!m || !m->vao) return;
    s.set("uModel", it.model);
    s.set("uRig", it.rig);
    s.set("uRigH", it.rigH);
    s.set("uTexOffset", it.texOffset);
    s.set("uDissolve", it.dissolve);
    int nb = it.bones ? std::min(it.boneCount, medieval::kMaxBones) : 0;
    s.set("uBoneCount", nb);
    if (nb > 0) glUniformMatrix4fv(s.loc("uBones"), nb, GL_FALSE, glm::value_ptr(it.bones[0]));
    if (!shadowPass) {
      s.set("uMat", it.material);
      s.set("uTeam", it.team);
      s.set("uGlow", it.glow);
      s.set("uCrack", it.crack);
      s.set("uCrackColor", it.crackColor);
      s.set("uDissolveColor", it.dissolveColor);
      s.set("uHeat", it.heat);
      s.set("uStone", it.stone);
      s.set("uGroundAO", it.groundAO ? 1.0f : 0.0f);
      s.set("uGhost", it.ghost);
    }
    m->draw();
  }
}

// See-through items (glass canopies): blended over everything solid, without
// writing depth, inner (back) faces first.
void Renderer::drawGlass(Shader& s, const SceneView& sv, bool lowLod, bool reflectPass, bool cull) {
  bool any = false;
  for (const DrawItem& it : sv.items) any = any || it.glass;
  if (!any) return;
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);
  drawItems(s, sv, lowLod, false, reflectPass, true);
  glCullFace(GL_BACK);
  drawItems(s, sv, lowLod, false, reflectPass, true);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  if (!cull) glDisable(GL_CULL_FACE);
}

// Ghosts: see-through, but each showing only its nearest surface - its depth
// first, then its colour blended on top - the farthest ghost first.
void Renderer::drawGhosts(Shader& s, const SceneView& sv) {
  std::vector<std::pair<float, int>> order;  // (distance, ghost id)
  for (const DrawItem& it : sv.items) {
    if (it.ghost <= 0.0f) continue;
    bool seen = false;
    for (const auto& o : order) seen = seen || o.second == it.ghostId;
    if (!seen) order.push_back({glm::length(glm::vec3(it.model[3]) - sv.camPos), it.ghostId});
  }
  if (order.empty()) return;
  std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  for (const auto& o : order) {
    glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    for (const DrawItem& it : sv.items)
      if (it.ghost > 0.0f && it.ghostId == o.second) drawItem(s, it, false, false);
    glEnable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    for (const DrawItem& it : sv.items)
      if (it.ghost > 0.0f && it.ghostId == o.second) drawItem(s, it, false, false);
  }
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
}

void Renderer::drawBoard(Shader& s) {
  s.set("uBoneCount", 0);
  s.set("uModel", glm::mat4(1.0f));
  s.set("uRig", glm::vec4(0, 0, 1, 0));
  s.set("uRigH", 1.0f);
  s.set("uDissolve", 0.0f);
  board.draw();
}

void Renderer::render(const SceneView& sv, int fbW, int fbH, GLuint outFbo) {
  int w = std::max(16, (int)(fbW * set_.renderScale));
  int h = std::max(16, (int)(fbH * set_.renderScale));
  if (w != tw || h != th) createTargets(w, h);
  const glm::mat4 viewProj = sv.proj * sv.view;

  glBindTexture(GL_TEXTURE_2D, boardStateTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, sv.boardState);

  // shared textures
  bindTex(0, GL_TEXTURE_2D, shadowTex);
  glBindSampler(0, sampCmp);
  bindTex(1, GL_TEXTURE_2D, shadowTex);
  glBindSampler(1, sampRaw);
  bindTex(2, GL_TEXTURE_CUBE_MAP, irrCube);
  bindTex(3, GL_TEXTURE_CUBE_MAP, prefCube);
  bindTex(4, GL_TEXTURE_2D, brdfLut);
  bindTex(5, GL_TEXTURE_2D, 0);
  bindTex(6, GL_TEXTURE_3D, noiseTex);
  bindTex(7, GL_TEXTURE_2D, boardAlbedo);
  bindTex(8, GL_TEXTURE_2D, boardDetail);
  bindTex(9, GL_TEXTURE_2D, 0);
  bindTex(10, GL_TEXTURE_2D, boardStateTex);
  bindTex(11, GL_TEXTURE_2D, 0);
  bindTex(12, GL_TEXTURE_CUBE_MAP, envCube);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);

  // ---- 1. shadow map ----
  if (set_.shadows) {
    // the shadow texture must not be sampled while it is the render target
    bindTex(0, GL_TEXTURE_2D, 0);
    bindTex(1, GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glViewport(0, 0, shadowResAlloc, shadowResAlloc);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    shShadow.use();
    shShadow.set("uViewProj", lightVP);
    shShadow.set("uClip", 0);
    drawItems(shShadow, sv, true, true, false);
    shShadow.set("uModel", glm::mat4(1.0f));
    shShadow.set("uBoneCount", 0);
    shShadow.set("uRig", glm::vec4(0, 0, 1, 0));
    shShadow.set("uDissolve", 0.0f);
    board.draw();
    glDisable(GL_POLYGON_OFFSET_FILL);
    bindTex(0, GL_TEXTURE_2D, shadowTex);
    bindTex(1, GL_TEXTURE_2D, shadowTex);
  }

  // ---- 2. planar reflection of everything above the board ----
  // (the lacquered top can only be seen while the camera is above it)
  const bool reflect = set_.reflections && sv.camPos.y > 0.0f;
  if (reflect) {
    glBindFramebuffer(GL_FRAMEBUFFER, reflFbo);
    glViewport(0, 0, rw, rh);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glm::mat4 mirror = glm::scale(glm::mat4(1.0f), glm::vec3(1, -1, 1));
    glEnable(GL_CLIP_DISTANCE0);
    glFrontFace(GL_CW);
    shObject.use();
    setLighting(shObject, sv, true);
    shObject.set("uViewProj", sv.proj * sv.view * mirror);
    shObject.set("uClip", 1);
    drawItems(shObject, sv, true, false, true);
    drawGlass(shObject, sv, true, true, true);
    shObject.set("uClip", 0);
    glFrontFace(GL_CCW);
    glDisable(GL_CLIP_DISTANCE0);
    // on its own unit: the active one may still hold the shadow map
    bindTex(9, GL_TEXTURE_2D, reflTex);
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  // ---- 3. depth / normal prepass ----
  glBindFramebuffer(GL_FRAMEBUFFER, preFbo);
  glViewport(0, 0, tw, th);
  glClearColor(0, 0, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  shPrepass.use();
  shPrepass.set("uViewProj", viewProj);
  shPrepass.set("uView", sv.view);
  shPrepass.set("uClip", 0);
  drawItems(shPrepass, sv, false, false, false);
  shPrepass.set("uModel", glm::mat4(1.0f));
  shPrepass.set("uBoneCount", 0);
  shPrepass.set("uRig", glm::vec4(0, 0, 1, 0));
  shPrepass.set("uDissolve", 0.0f);
  board.draw();

  // ---- 4. SSAO ----
  bindTex(11, GL_TEXTURE_2D, preDepth);
  bindTex(14, GL_TEXTURE_2D, preNormal);
  if (set_.ssao) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFbo);
    glViewport(0, 0, std::max(1, tw / 2), std::max(1, th / 2));
    shSsao.use();
    shSsao.set("uProj", sv.proj);
    shSsao.set("uInvProj", glm::inverse(sv.proj));
    shSsao.setArray("uKernel", ssaoKernel, 16);
    shSsao.set("uRadius", 0.22f);
    fullscreen();
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoBlurFbo);
    shSsaoBlur.use();
    bindTex(13, GL_TEXTURE_2D, ssaoTex);
    shSsaoBlur.set("uTexel", glm::vec2(2.0f / tw, 2.0f / th));
    shSsaoBlur.set("uNearFar", glm::vec2(sv.nearZ, sv.farZ));
    fullscreen();
    bindTex(5, GL_TEXTURE_2D, ssaoBlurTex);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
  }
  if (reflect) bindTex(9, GL_TEXTURE_2D, reflTex);

  // ---- 5. main HDR pass ----
  glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
  glViewport(0, 0, tw, th);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  shBoard.use();
  setLighting(shBoard, sv, false);
  shBoard.set("uViewProj", viewProj);
  shBoard.set("uClip", 0);
  shBoard.set("uReflectOn", reflect ? 1 : 0);
  shBoard.set("uReflectLod", 0.9f);
  shBoard.set("uCheck", sv.check);
  int no = std::min(32, (int)sv.occluders.size());
  shBoard.set("uNumOcc", no);
  if (no) shBoard.setArray("uOcc", sv.occluders.data(), no);
  int nr = std::min(6, (int)sv.rings.size());
  shBoard.set("uNumRings", nr);
  if (nr) {
    shBoard.setArray("uRings", sv.rings.data(), nr);
    shBoard.setArray("uRingCol", sv.ringColors.data(), nr);
  }
  drawBoard(shBoard);
  shObject.use();
  setLighting(shObject, sv, false);
  shObject.set("uViewProj", viewProj);
  shObject.set("uClip", 0);
  drawItems(shObject, sv, false, false, false);

  // background
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  shBackground.use();
  shBackground.set("uInvViewProj", glm::inverse(viewProj));
  shBackground.set("uCamPos", sv.camPos);
  shBackground.set("uBgLod", 3.2f);
  shBackground.set("uBgIntensity", 0.35f);
  fullscreen();

  // glass, then the ghosts
  shObject.use();
  drawGlass(shObject, sv, false, false, false);
  drawGhosts(shObject, sv);
  glDepthMask(GL_FALSE);

  // particles
  if (!sv.particles.empty()) {
    std::vector<int> order(sv.particles.size());
    std::iota(order.begin(), order.end(), 0);
    std::vector<float> depth(sv.particles.size());
    for (size_t i = 0; i < sv.particles.size(); ++i) {
      glm::vec4 v = sv.view * glm::vec4(glm::vec3(sv.particles[i].posSize), 1.0f);
      depth[i] = v.z;
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return depth[a] < depth[b]; });
    std::vector<ParticleGPU> sorted;
    sorted.reserve(order.size());
    for (int i : order) sorted.push_back(sv.particles[i]);
    glBindBuffer(GL_ARRAY_BUFFER, partVbo);
    if (sorted.size() > partCap) {
      partCap = sorted.size() * 2;
      glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(partCap * sizeof(ParticleGPU)), nullptr, GL_STREAM_DRAW);
    } else {
      glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(partCap * sizeof(ParticleGPU)), nullptr, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sorted.size() * sizeof(ParticleGPU)), sorted.data());
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    shParticle.use();
    shParticle.set("uView", sv.view);
    shParticle.set("uProj", sv.proj);
    shParticle.set("uInvScreen", glm::vec2(1.0f / tw, 1.0f / th));
    shParticle.set("uNearFar", glm::vec2(sv.nearZ, sv.farZ));
    glBindVertexArray(partVao);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)sorted.size());
    glDisable(GL_BLEND);
  }
  glDepthMask(GL_TRUE);

  // ---- 6. resolve ----
  glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdrFbo);
  glBlitFramebuffer(0, 0, tw, th, 0, 0, tw, th, GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  GLuint sceneTex = hdrTex;
  // ---- 7. depth of field ----
  if (set_.dof && sv.aperture > 0.01f) {
    int hw = std::max(1, tw / 2), hh = std::max(1, th / 2);
    float maxCoc = 11.0f * (th / 720.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, dofCocFbo);
    glViewport(0, 0, hw, hh);
    shDofCoc.use();
    bindTex(13, GL_TEXTURE_2D, hdrTex);
    shDofCoc.set("uNearFar", glm::vec2(sv.nearZ, sv.farZ));
    shDofCoc.set("uFocus", sv.focusDist);
    shDofCoc.set("uAperture", sv.aperture * (th / 720.0f));
    shDofCoc.set("uMaxCoc", maxCoc);
    shDofCoc.set("uSrcTexel", glm::vec2(1.0f / tw, 1.0f / th));
    fullscreen();
    glBindFramebuffer(GL_FRAMEBUFFER, dofBokehFbo);
    shDofBokeh.use();
    bindTex(13, GL_TEXTURE_2D, dofCocTex);
    shDofBokeh.set("uTexel", glm::vec2(1.0f / hw, 1.0f / hh));
    shDofBokeh.set("uMaxCoc", maxCoc);
    fullscreen();
    glBindFramebuffer(GL_FRAMEBUFFER, dofOutFbo);
    glViewport(0, 0, tw, th);
    shDofComp.use();
    bindTex(13, GL_TEXTURE_2D, hdrTex);
    bindTex(14, GL_TEXTURE_2D, dofBokehTex);
    bindTex(15, GL_TEXTURE_2D, dofCocTex);
    fullscreen();
    sceneTex = dofOutTex;
  }

  // ---- 8. bloom ----
  if (set_.bloom) {
    shBloomDown.use();
    for (int i = 0; i < kBloomLevels; ++i) {
      glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i]);
      glViewport(0, 0, bloomW[i], bloomH[i]);
      if (i == 0) {
        bindTex(13, GL_TEXTURE_2D, sceneTex);
        shBloomDown.set("uSrcTexel", glm::vec2(1.0f / tw, 1.0f / th));
      } else {
        bindTex(13, GL_TEXTURE_2D, bloomTex[i - 1]);
        shBloomDown.set("uSrcTexel", glm::vec2(1.0f / bloomW[i - 1], 1.0f / bloomH[i - 1]));
      }
      shBloomDown.set("uFirst", i == 0 ? 1 : 0);
      fullscreen();
    }
    shBloomUp.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = kBloomLevels - 1; i > 0; --i) {
      glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i - 1]);
      glViewport(0, 0, bloomW[i - 1], bloomH[i - 1]);
      bindTex(13, GL_TEXTURE_2D, bloomTex[i]);
      shBloomUp.set("uRadius", glm::vec2(1.0f / bloomW[i], 1.0f / bloomH[i]));
      fullscreen();
    }
    glDisable(GL_BLEND);
  }

  // ---- 9. tonemap + composite to the output ----
  glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
  glViewport(0, 0, fbW, fbH);
  shComposite.use();
  bindTex(13, GL_TEXTURE_2D, sceneTex);
  bindTex(14, GL_TEXTURE_2D, bloomTex[0]);
  shComposite.set("uBloomOn", set_.bloom ? 1 : 0);
  shComposite.set("uBloomStrength", 0.045f);
  shComposite.set("uExposure", sv.exposure);
  shComposite.set("uVignette", 0.55f);
  shComposite.set("uGrain", 0.012f);
  shComposite.set("uTime", sv.time);
  shComposite.set("uFade", sv.fade);
  shComposite.set("uFlash", sv.flash);
  fullscreen();

  // leave a clean state for the UI
  glBindSampler(0, 0);
  glBindSampler(1, 0);
  for (int u = 15; u >= 0; --u) {
    glActiveTexture(GL_TEXTURE0 + u);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glActiveTexture(GL_TEXTURE0);
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
}
