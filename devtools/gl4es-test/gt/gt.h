// gt: differential GL tests. The same binary runs on Mesa desktop GL (the reference) and through gl4es;
// run.py compares what each test wrote. A test draws into the 256x256 back buffer and saves pixels or values.
#pragma once
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define W 256
#define H 256

// GL entry points beyond 1.1, fetched with glXGetProcAddress (Mesa's libGL exports few of them)
#define GT_PROCS \
 X(PFNGLACTIVETEXTUREPROC, glActiveTexture) X(PFNGLCLIENTACTIVETEXTUREARBPROC, glClientActiveTexture) \
 X(PFNGLMULTITEXCOORD4FARBPROC, glMultiTexCoord4f) X(PFNGLMULTITEXCOORD2FARBPROC, glMultiTexCoord2f) \
 X(PFNGLGENPROGRAMSARBPROC, glGenProgramsARB) X(PFNGLBINDPROGRAMARBPROC, glBindProgramARB) \
 X(PFNGLPROGRAMSTRINGARBPROC, glProgramStringARB) X(PFNGLDELETEPROGRAMSARBPROC, glDeleteProgramsARB) \
 X(PFNGLPROGRAMENVPARAMETER4FVARBPROC, glProgramEnvParameter4fvARB) \
 X(PFNGLPROGRAMLOCALPARAMETER4FVARBPROC, glProgramLocalParameter4fvARB) \
 X(PFNGLGETPROGRAMIVARBPROC, glGetProgramivARB) \
 X(PFNGLVERTEXATTRIB4FARBPROC, glVertexAttrib4fARB) \
 X(PFNGLVERTEXATTRIBPOINTERARBPROC, glVertexAttribPointerARB) \
 X(PFNGLENABLEVERTEXATTRIBARRAYARBPROC, glEnableVertexAttribArrayARB) \
 X(PFNGLDISABLEVERTEXATTRIBARRAYARBPROC, glDisableVertexAttribArrayARB) \
 X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) \
 X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
 X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
 X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
 X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
 X(PFNGLUSEPROGRAMPROC, glUseProgram) X(PFNGLDELETESHADERPROC, glDeleteShader) \
 X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
 X(PFNGLUNIFORM1IPROC, glUniform1i) X(PFNGLUNIFORM4FVPROC, glUniform4fv) X(PFNGLUNIFORM1FPROC, glUniform1f) \
 X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv) X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
 X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
 X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
 X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
 X(PFNGLGENFRAMEBUFFERSEXTPROC, glGenFramebuffersEXT) X(PFNGLBINDFRAMEBUFFEREXTPROC, glBindFramebufferEXT) \
 X(PFNGLFRAMEBUFFERTEXTURE2DEXTPROC, glFramebufferTexture2DEXT) \
 X(PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC, glFramebufferRenderbufferEXT) \
 X(PFNGLGENRENDERBUFFERSEXTPROC, glGenRenderbuffersEXT) X(PFNGLBINDRENDERBUFFEREXTPROC, glBindRenderbufferEXT) \
 X(PFNGLRENDERBUFFERSTORAGEEXTPROC, glRenderbufferStorageEXT) \
 X(PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC, glCheckFramebufferStatusEXT) \
 X(PFNGLDELETEFRAMEBUFFERSEXTPROC, glDeleteFramebuffersEXT) \
 X(PFNGLDELETERENDERBUFFERSEXTPROC, glDeleteRenderbuffersEXT) X(PFNGLGENERATEMIPMAPEXTPROC, glGenerateMipmapEXT) \
 X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer) X(PFNGLBUFFERDATAPROC, glBufferData) \
 X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
 X(PFNGLMAPBUFFERPROC, glMapBuffer) X(PFNGLUNMAPBUFFERPROC, glUnmapBuffer) \
 X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) X(PFNGLBLENDEQUATIONPROC, glBlendEquation) \
 X(PFNGLBLENDCOLORPROC, glBlendColor) X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) \
 X(PFNGLDRAWRANGEELEMENTSPROC, glDrawRangeElements) X(PFNGLMULTIDRAWARRAYSPROC, glMultiDrawArrays) \
 X(PFNGLMULTIDRAWELEMENTSPROC, glMultiDrawElements) X(PFNGLPOINTPARAMETERFPROC, glPointParameterf) \
 X(PFNGLPOINTPARAMETERFVPROC, glPointParameterfv) X(PFNGLSECONDARYCOLOR3FPROC, glSecondaryColor3f) \
 X(PFNGLSECONDARYCOLORPOINTERPROC, glSecondaryColorPointer) X(PFNGLFOGCOORDFPROC, glFogCoordf) \
 X(PFNGLFOGCOORDPOINTERPROC, glFogCoordPointer) X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D) \
 X(PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC, glCompressedTexSubImage2D) \
 X(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate) X(PFNGLSTENCILFUNCSEPARATEPROC, glStencilFuncSeparate) \
 X(PFNGLLOADTRANSPOSEMATRIXFARBPROC, glLoadTransposeMatrixf) X(PFNGLWINDOWPOS2IPROC, glWindowPos2i) \
 X(PFNGLBLITFRAMEBUFFEREXTPROC, glBlitFramebufferEXT)
#define X(t, n) extern t p_##n;
GT_PROCS
#undef X

typedef struct gt_test gt_test;
typedef void (*gt_fn)(gt_test *t);
struct gt_test {
    char name[160];
    gt_fn fn;
    int a, b, c, d;          // test parameters (formats, seeds ...)
    const void *p;           // optional data (corpus program text)
    // filled while running
    FILE *out;
    int nimg;
};

void gt_add(const char *name, gt_fn fn, int a, int b, int c, int d, const void *p);

// output helpers
void gt_img(gt_test *t, int x, int y, int w, int h, int tol, double maxfrac);  // read back + save region
void gt_vals(gt_test *t, const char *key, double tol, const float *v, int n);
void gt_info(gt_test *t, const char *fmt, ...);  // recorded, compared, reported but never a failure
void gt_skip(gt_test *t, const char *why);
void gt_errcheck(gt_test *t, const char *where); // glGetError -> info line

// common setup
void gt_pixel_space(void);    // viewport 256x256, ortho 1 unit = 1 pixel, identity modelview
void gt_quad(float x, float y, float w, float h);  // textured quad, tc 0..1
const char *gt_enum(GLenum e);

// deterministic random numbers
typedef struct { uint64_t s; } rng_t;
static inline uint32_t rng_u32(rng_t *r) { r->s ^= r->s << 13; r->s ^= r->s >> 7; r->s ^= r->s << 17; return (uint32_t)(r->s >> 11); }
static inline void rng_seed(rng_t *r, uint64_t seed) { r->s = seed * 0x9E3779B97F4A7C15ull + 0x1234567ull; for (int i = 0; i < 4; i++) rng_u32(r); }
static inline int rng_int(rng_t *r, int n) { return (int)(rng_u32(r) % (uint32_t)n); }
static inline float rng_f(rng_t *r, float lo, float hi) { return lo + (hi - lo) * (rng_u32(r) & 0xffffff) / 16777215.0f; }
#define PICK(r, arr) (arr)[rng_int(r, (int)(sizeof(arr) / sizeof((arr)[0])))]

// pixel transfer helpers (pixels.c)
int gt_fmt_ncomp(GLenum fmt);
int gt_type_packed(GLenum type, int *fields, int *nfields, int *lsb_first);  // returns bytes, 0 if not packed
int gt_type_size(GLenum type);   // element size for unpacked types
int gt_pixel_bytes(GLenum fmt, GLenum type);
size_t gt_image_size(int w, int h, GLenum fmt, GLenum type, int align, int rowlen, int skippix, int skiprows, size_t *stride);
int gt_decode(GLenum fmt, GLenum type, const uint8_t *px, float *out);  // -> ncomp normalized floats
int gt_type_bits(GLenum type);  // smallest field bits (precision) of a type

// registrations
void reg_pixels(void);
void reg_arb(void);
void reg_ffp(void);
void reg_state(void);
void reg_fbo(void);
void reg_glsl(void);
void reg_corpus(const char *dir);
