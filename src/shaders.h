// GLSL sources (embedded so the executable is self-contained).
#pragma once

namespace shaders {
extern const char* kCommon;         // shared constants / helpers
extern const char* kLighting;       // PBR + shadows + IBL
extern const char* kMeshVS;         // deformable mesh vertex shader (rig)
extern const char* kShadowFS;
extern const char* kPrepassFS;
extern const char* kObjectFS;       // marble, steel, iron, fragments
extern const char* kBoardFS;        // lacquered inlaid wood board
extern const char* kFullscreenVS;
extern const char* kBackgroundFS;
extern const char* kEnvGenFS;
extern const char* kIrradianceFS;
extern const char* kPrefilterFS;
extern const char* kBrdfLutFS;
extern const char* kSsaoFS;
extern const char* kSsaoBlurFS;
extern const char* kBloomDownFS;
extern const char* kBloomUpFS;
extern const char* kDofCocFS;
extern const char* kDofBokehFS;
extern const char* kDofCompositeFS;
extern const char* kCompositeFS;
extern const char* kParticleVS;
extern const char* kParticleFS;
extern const char* kMipCopyFS;
}  // namespace shaders
