// Minimal self-contained OpenGL 3.3 core loader (no external loader needed).
// Function pointers are resolved through glfwGetProcAddress at startup.
#pragma once
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define LM_APIENTRY __stdcall
#else
#define LM_APIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

// ---- constants --------------------------------------------------------------
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_ZERO 0
#define GL_ONE 1
#define GL_NO_ERROR 0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_MULTISAMPLE 0x809D
#define GL_DEPTH_CLAMP 0x864F
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_CLIP_DISTANCE0 0x3000
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_NUM_EXTENSIONS 0x821D
#define GL_MAX_SAMPLES 0x8D57
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_FUNC_ADD 0x8006
#define GL_MIN 0x8007
#define GL_MAX 0x8008

#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B

#define GL_DEPTH_COMPONENT 0x1902
#define GL_RED 0x1903
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RG 0x8227
#define GL_DEPTH_STENCIL 0x84F9
#define GL_UNSIGNED_INT_24_8 0x84FA

#define GL_R8 0x8229
#define GL_RG8 0x822B
#define GL_R16F 0x822D
#define GL_R32F 0x822E
#define GL_RG16F 0x822F
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_RGB10_A2 0x8059
#define GL_RGBA32F 0x8814
#define GL_RGBA16F 0x881A
#define GL_RGB16F 0x881B
#define GL_R11F_G11F_B10F 0x8C3A
#define GL_SRGB8 0x8C41
#define GL_SRGB8_ALPHA8 0x8C43
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_DEPTH24_STENCIL8 0x88F0

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_WRAP_R 0x8072
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

// ---- functions --------------------------------------------------------------
#define LM_GL_FUNCS(X) \
  X(void, Clear, (GLbitfield mask)) \
  X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
  X(void, ClearDepth, (GLdouble d)) \
  X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
  X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h)) \
  X(void, Enable, (GLenum cap)) \
  X(void, Disable, (GLenum cap)) \
  X(void, BlendFunc, (GLenum s, GLenum d)) \
  X(void, BlendFuncSeparate, (GLenum sr, GLenum dr, GLenum sa, GLenum da)) \
  X(void, BlendEquation, (GLenum m)) \
  X(void, DepthFunc, (GLenum f)) \
  X(void, DepthMask, (GLboolean f)) \
  X(void, CullFace, (GLenum m)) \
  X(void, FrontFace, (GLenum m)) \
  X(void, ColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
  X(void, PolygonOffset, (GLfloat f, GLfloat u)) \
  X(void, PolygonMode, (GLenum face, GLenum mode)) \
  X(GLenum, GetError, (void)) \
  X(void, GetIntegerv, (GLenum p, GLint* v)) \
  X(void, GetFloatv, (GLenum p, GLfloat* v)) \
  X(const GLubyte*, GetString, (GLenum n)) \
  X(const GLubyte*, GetStringi, (GLenum n, GLuint i)) \
  X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void* d)) \
  X(void, PixelStorei, (GLenum p, GLint v)) \
  X(void, Finish, (void)) \
  X(void, Flush, (void)) \
  X(void, DrawBuffer, (GLenum b)) \
  X(void, ReadBuffer, (GLenum b)) \
  X(void, DrawBuffers, (GLsizei n, const GLenum* b)) \
  X(void, GenTextures, (GLsizei n, GLuint* t)) \
  X(void, DeleteTextures, (GLsizei n, const GLuint* t)) \
  X(void, BindTexture, (GLenum target, GLuint t)) \
  X(void, ActiveTexture, (GLenum unit)) \
  X(void, TexImage2D, (GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void* d)) \
  X(void, TexImage3D, (GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLsizei dd, GLint b, GLenum f, GLenum ty, const void* d)) \
  X(void, TexSubImage2D, (GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void* d)) \
  X(void, TexParameteri, (GLenum t, GLenum p, GLint v)) \
  X(void, TexParameterf, (GLenum t, GLenum p, GLfloat v)) \
  X(void, TexParameterfv, (GLenum t, GLenum p, const GLfloat* v)) \
  X(void, GenerateMipmap, (GLenum t)) \
  X(void, GenBuffers, (GLsizei n, GLuint* b)) \
  X(void, DeleteBuffers, (GLsizei n, const GLuint* b)) \
  X(void, BindBuffer, (GLenum t, GLuint b)) \
  X(void, BufferData, (GLenum t, GLsizeiptr s, const void* d, GLenum u)) \
  X(void, BufferSubData, (GLenum t, GLintptr o, GLsizeiptr s, const void* d)) \
  X(void, BindBufferBase, (GLenum t, GLuint i, GLuint b)) \
  X(void, GenVertexArrays, (GLsizei n, GLuint* a)) \
  X(void, DeleteVertexArrays, (GLsizei n, const GLuint* a)) \
  X(void, BindVertexArray, (GLuint a)) \
  X(void, EnableVertexAttribArray, (GLuint i)) \
  X(void, DisableVertexAttribArray, (GLuint i)) \
  X(void, VertexAttribPointer, (GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void* p)) \
  X(void, VertexAttribIPointer, (GLuint i, GLint s, GLenum t, GLsizei st, const void* p)) \
  X(void, VertexAttribDivisor, (GLuint i, GLuint d)) \
  X(void, VertexAttrib4f, (GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w)) \
  X(void, DrawArrays, (GLenum m, GLint f, GLsizei c)) \
  X(void, DrawElements, (GLenum m, GLsizei c, GLenum t, const void* i)) \
  X(void, DrawArraysInstanced, (GLenum m, GLint f, GLsizei c, GLsizei n)) \
  X(void, DrawElementsInstanced, (GLenum m, GLsizei c, GLenum t, const void* i, GLsizei n)) \
  X(GLuint, CreateShader, (GLenum t)) \
  X(void, ShaderSource, (GLuint s, GLsizei c, const GLchar* const* str, const GLint* len)) \
  X(void, CompileShader, (GLuint s)) \
  X(void, GetShaderiv, (GLuint s, GLenum p, GLint* v)) \
  X(void, GetShaderInfoLog, (GLuint s, GLsizei m, GLsizei* l, GLchar* log)) \
  X(void, DeleteShader, (GLuint s)) \
  X(GLuint, CreateProgram, (void)) \
  X(void, AttachShader, (GLuint p, GLuint s)) \
  X(void, LinkProgram, (GLuint p)) \
  X(void, GetProgramiv, (GLuint p, GLenum n, GLint* v)) \
  X(void, GetProgramInfoLog, (GLuint p, GLsizei m, GLsizei* l, GLchar* log)) \
  X(void, DeleteProgram, (GLuint p)) \
  X(void, UseProgram, (GLuint p)) \
  X(void, BindAttribLocation, (GLuint p, GLuint i, const GLchar* n)) \
  X(void, BindFragDataLocation, (GLuint p, GLuint c, const GLchar* n)) \
  X(GLint, GetUniformLocation, (GLuint p, const GLchar* n)) \
  X(GLuint, GetUniformBlockIndex, (GLuint p, const GLchar* n)) \
  X(void, UniformBlockBinding, (GLuint p, GLuint i, GLuint b)) \
  X(void, Uniform1i, (GLint l, GLint v)) \
  X(void, Uniform1f, (GLint l, GLfloat v)) \
  X(void, Uniform2f, (GLint l, GLfloat a, GLfloat b)) \
  X(void, Uniform3f, (GLint l, GLfloat a, GLfloat b, GLfloat c)) \
  X(void, Uniform4f, (GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d)) \
  X(void, Uniform1fv, (GLint l, GLsizei c, const GLfloat* v)) \
  X(void, Uniform2fv, (GLint l, GLsizei c, const GLfloat* v)) \
  X(void, Uniform3fv, (GLint l, GLsizei c, const GLfloat* v)) \
  X(void, Uniform4fv, (GLint l, GLsizei c, const GLfloat* v)) \
  X(void, Uniform1iv, (GLint l, GLsizei c, const GLint* v)) \
  X(void, UniformMatrix3fv, (GLint l, GLsizei c, GLboolean t, const GLfloat* v)) \
  X(void, UniformMatrix4fv, (GLint l, GLsizei c, GLboolean t, const GLfloat* v)) \
  X(void, GenFramebuffers, (GLsizei n, GLuint* f)) \
  X(void, DeleteFramebuffers, (GLsizei n, const GLuint* f)) \
  X(void, BindFramebuffer, (GLenum t, GLuint f)) \
  X(void, FramebufferTexture2D, (GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l)) \
  X(void, FramebufferRenderbuffer, (GLenum t, GLenum a, GLenum rt, GLuint rb)) \
  X(GLenum, CheckFramebufferStatus, (GLenum t)) \
  X(void, GenRenderbuffers, (GLsizei n, GLuint* r)) \
  X(void, DeleteRenderbuffers, (GLsizei n, const GLuint* r)) \
  X(void, BindRenderbuffer, (GLenum t, GLuint r)) \
  X(void, RenderbufferStorage, (GLenum t, GLenum f, GLsizei w, GLsizei h)) \
  X(void, RenderbufferStorageMultisample, (GLenum t, GLsizei s, GLenum f, GLsizei w, GLsizei h)) \
  X(void, BlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield m, GLenum f)) \
  X(void, GenSamplers, (GLsizei n, GLuint* s)) \
  X(void, DeleteSamplers, (GLsizei n, const GLuint* s)) \
  X(void, BindSampler, (GLuint u, GLuint s)) \
  X(void, SamplerParameteri, (GLuint s, GLenum p, GLint v)) \
  X(void, SamplerParameterf, (GLuint s, GLenum p, GLfloat v))

#define LM_DECLARE_GL(ret, name, args) \
  typedef ret(LM_APIENTRY* PFN_lm_gl##name) args; \
  extern PFN_lm_gl##name lm_gl##name;
LM_GL_FUNCS(LM_DECLARE_GL)
#undef LM_DECLARE_GL

#define glClear lm_glClear
#define glClearColor lm_glClearColor
#define glClearDepth lm_glClearDepth
#define glViewport lm_glViewport
#define glScissor lm_glScissor
#define glEnable lm_glEnable
#define glDisable lm_glDisable
#define glBlendFunc lm_glBlendFunc
#define glBlendFuncSeparate lm_glBlendFuncSeparate
#define glBlendEquation lm_glBlendEquation
#define glDepthFunc lm_glDepthFunc
#define glDepthMask lm_glDepthMask
#define glCullFace lm_glCullFace
#define glFrontFace lm_glFrontFace
#define glColorMask lm_glColorMask
#define glPolygonOffset lm_glPolygonOffset
#define glPolygonMode lm_glPolygonMode
#define glGetError lm_glGetError
#define glGetIntegerv lm_glGetIntegerv
#define glGetFloatv lm_glGetFloatv
#define glGetString lm_glGetString
#define glGetStringi lm_glGetStringi
#define glReadPixels lm_glReadPixels
#define glPixelStorei lm_glPixelStorei
#define glFinish lm_glFinish
#define glFlush lm_glFlush
#define glDrawBuffer lm_glDrawBuffer
#define glReadBuffer lm_glReadBuffer
#define glDrawBuffers lm_glDrawBuffers
#define glGenTextures lm_glGenTextures
#define glDeleteTextures lm_glDeleteTextures
#define glBindTexture lm_glBindTexture
#define glActiveTexture lm_glActiveTexture
#define glTexImage2D lm_glTexImage2D
#define glTexImage3D lm_glTexImage3D
#define glTexSubImage2D lm_glTexSubImage2D
#define glTexParameteri lm_glTexParameteri
#define glTexParameterf lm_glTexParameterf
#define glTexParameterfv lm_glTexParameterfv
#define glGenerateMipmap lm_glGenerateMipmap
#define glGenBuffers lm_glGenBuffers
#define glDeleteBuffers lm_glDeleteBuffers
#define glBindBuffer lm_glBindBuffer
#define glBufferData lm_glBufferData
#define glBufferSubData lm_glBufferSubData
#define glBindBufferBase lm_glBindBufferBase
#define glGenVertexArrays lm_glGenVertexArrays
#define glDeleteVertexArrays lm_glDeleteVertexArrays
#define glBindVertexArray lm_glBindVertexArray
#define glEnableVertexAttribArray lm_glEnableVertexAttribArray
#define glDisableVertexAttribArray lm_glDisableVertexAttribArray
#define glVertexAttribPointer lm_glVertexAttribPointer
#define glVertexAttribIPointer lm_glVertexAttribIPointer
#define glVertexAttribDivisor lm_glVertexAttribDivisor
#define glVertexAttrib4f lm_glVertexAttrib4f
#define glDrawArrays lm_glDrawArrays
#define glDrawElements lm_glDrawElements
#define glDrawArraysInstanced lm_glDrawArraysInstanced
#define glDrawElementsInstanced lm_glDrawElementsInstanced
#define glCreateShader lm_glCreateShader
#define glShaderSource lm_glShaderSource
#define glCompileShader lm_glCompileShader
#define glGetShaderiv lm_glGetShaderiv
#define glGetShaderInfoLog lm_glGetShaderInfoLog
#define glDeleteShader lm_glDeleteShader
#define glCreateProgram lm_glCreateProgram
#define glAttachShader lm_glAttachShader
#define glLinkProgram lm_glLinkProgram
#define glGetProgramiv lm_glGetProgramiv
#define glGetProgramInfoLog lm_glGetProgramInfoLog
#define glDeleteProgram lm_glDeleteProgram
#define glUseProgram lm_glUseProgram
#define glBindAttribLocation lm_glBindAttribLocation
#define glBindFragDataLocation lm_glBindFragDataLocation
#define glGetUniformLocation lm_glGetUniformLocation
#define glGetUniformBlockIndex lm_glGetUniformBlockIndex
#define glUniformBlockBinding lm_glUniformBlockBinding
#define glUniform1i lm_glUniform1i
#define glUniform1f lm_glUniform1f
#define glUniform2f lm_glUniform2f
#define glUniform3f lm_glUniform3f
#define glUniform4f lm_glUniform4f
#define glUniform1fv lm_glUniform1fv
#define glUniform2fv lm_glUniform2fv
#define glUniform3fv lm_glUniform3fv
#define glUniform4fv lm_glUniform4fv
#define glUniform1iv lm_glUniform1iv
#define glUniformMatrix3fv lm_glUniformMatrix3fv
#define glUniformMatrix4fv lm_glUniformMatrix4fv
#define glGenFramebuffers lm_glGenFramebuffers
#define glDeleteFramebuffers lm_glDeleteFramebuffers
#define glBindFramebuffer lm_glBindFramebuffer
#define glFramebufferTexture2D lm_glFramebufferTexture2D
#define glFramebufferRenderbuffer lm_glFramebufferRenderbuffer
#define glCheckFramebufferStatus lm_glCheckFramebufferStatus
#define glGenRenderbuffers lm_glGenRenderbuffers
#define glDeleteRenderbuffers lm_glDeleteRenderbuffers
#define glBindRenderbuffer lm_glBindRenderbuffer
#define glRenderbufferStorage lm_glRenderbufferStorage
#define glRenderbufferStorageMultisample lm_glRenderbufferStorageMultisample
#define glBlitFramebuffer lm_glBlitFramebuffer
#define glGenSamplers lm_glGenSamplers
#define glDeleteSamplers lm_glDeleteSamplers
#define glBindSampler lm_glBindSampler
#define glSamplerParameteri lm_glSamplerParameteri
#define glSamplerParameterf lm_glSamplerParameterf

typedef void* (*LMGLGetProc)(const char* name);
// Returns false (and fills 'missing') if a required function failed to load.
bool lmLoadGL(LMGLGetProc getProc, const char** missing);
bool lmHasExtension(const char* name);
