// The web page's OpenGL: WebGL 2 (OpenGL ES 3.0) through Emscripten, with the
// few desktop OpenGL 3.3 calls the game makes mapped onto it. Included by gl.h.
#pragma once
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

// Desktop-only switches. WebGL's cube maps are always seamless, and it has no
// clip planes (in the mirrored pass the object shader discards instead).
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#define GL_CLIP_DISTANCE0 0x3000
// EXT_texture_filter_anisotropic (the same values as OpenGL 4.6 core)
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF

inline bool lmWebHasCap(GLenum cap) { return cap != GL_TEXTURE_CUBE_MAP_SEAMLESS && cap != GL_CLIP_DISTANCE0; }
inline void lmWebEnable(GLenum cap) {
  if (lmWebHasCap(cap)) glEnable(cap);
}
inline void lmWebDisable(GLenum cap) {
  if (lmWebHasCap(cap)) glDisable(cap);
}
inline void lmWebDrawBuffer(GLenum b) { glDrawBuffers(1, &b); }
#define glEnable lmWebEnable
#define glDisable lmWebDisable
#define glDrawBuffer lmWebDrawBuffer
