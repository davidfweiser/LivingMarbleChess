#include "gl.h"
#include <cstring>

#define LM_DEFINE_GL(ret, name, args) PFN_lm_gl##name lm_gl##name = nullptr;
LM_GL_FUNCS(LM_DEFINE_GL)
#undef LM_DEFINE_GL

bool lmLoadGL(LMGLGetProc getProc, const char** missing) {
  bool ok = true;
#define LM_LOAD_GL(ret, name, args)                                       \
  lm_gl##name = reinterpret_cast<PFN_lm_gl##name>(getProc("gl" #name));   \
  if (!lm_gl##name && ok) {                                               \
    ok = false;                                                           \
    if (missing) *missing = "gl" #name;                                   \
  }
  LM_GL_FUNCS(LM_LOAD_GL)
#undef LM_LOAD_GL
  return ok;
}

bool lmHasExtension(const char* name) {
  GLint n = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &n);
  for (GLint i = 0; i < n; ++i) {
    const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, (GLuint)i));
    if (e && std::strcmp(e, name) == 0) return true;
  }
  return false;
}
