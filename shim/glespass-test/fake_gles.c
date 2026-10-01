#include <stdio.h>
#include <string.h>
void glClear(unsigned m) { printf("fake GLES glClear(0x%x)\n", m); }
const char *glGetString(unsigned n) { return "OpenGL ES 3.2 fake"; }
void glClearDepthf(float d) { printf("fake GLES glClearDepthf(%.2f)\n", d); }
void glCompileShader(unsigned s) { printf("fake compile %u\n", s); }
void glLinkProgram(unsigned p) { printf("fake link %u\n", p); }
void glGetShaderiv(unsigned s, unsigned pn, int *v) { *v = (pn == 0x8B4F) ? 0x8B30 : 0; }
void glGetShaderInfoLog(unsigned s, int n, int *l, char *b) { strcpy(b, "0:3: S0032: no default precision defined for variable"); }
void glGetShaderSource(unsigned s, int n, int *l, char *b) { strcpy(b, "#version 300 es\nvoid main(){}"); }
void glGetProgramiv(unsigned p, unsigned pn, int *v) { *v = 0; }
void glGetProgramInfoLog(unsigned p, int n, int *l, char *b) { b[0] = 0; }
void glViewport(int x, int y, int w, int h) { printf("raw GLES glViewport (WRONG: should go via crusty)\n"); }
/* framebuffer state for the swap-time OPAQUE / sampling code */
static int fb_draw = 7, scissor_on = 1; static unsigned char cmask[4] = {1, 1, 1, 1}; static float ccol[4] = {0.2f, 0.3f, 0.4f, 0.0f};
void glGetIntegerv(unsigned p, int *v) { if (p == 0x8CA6 || p == 0x8CAA) *v = fb_draw; else if (p == 0x0BA2) { v[0] = 0; v[1] = 0; v[2] = 640; v[3] = 480; } }
void glGetFloatv(unsigned p, float *v) { memcpy(v, ccol, sizeof ccol); }
void glGetBooleanv(unsigned p, unsigned char *v) { memcpy(v, cmask, 4); }
unsigned char glIsEnabled(unsigned c) { return scissor_on; }
void glEnable(unsigned c) { printf("fake enable 0x%x\n", c); scissor_on = 1; }
void glDisable(unsigned c) { printf("fake disable 0x%x\n", c); scissor_on = 0; }
void glBindFramebuffer(unsigned t, unsigned f) { printf("fake bind fb 0x%x -> %u\n", t, f); }
void glColorMask(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { printf("fake colormask %d%d%d%d\n", r, g, b, a); }
void glClearColor(float r, float g, float b, float a) { printf("fake clearcolor %.1f %.1f %.1f %.1f\n", r, g, b, a); }
void glReadPixels(int x, int y, int w, int h, unsigned f, unsigned t, void *p) { unsigned char *c = p; c[0] = 200; c[1] = 100; c[2] = 50; c[3] = 17; }
