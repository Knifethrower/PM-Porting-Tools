// gl1es.c: the OpenGL 1.x subset Bugdom 2 uses, on OpenGL ES 2.0.
//
// The fixed-function pipeline becomes generated shaders: every combination of lighting (up to 4
// directional lights, colour material), two texture units (modulate / add, sphere-map texgen,
// texture matrix), linear fog and alpha test that the game sets gets its own program, made the
// first time it is drawn. The game's meshes (glDrawElements on its own client arrays) go to the
// GPU untransformed. glBegin/glEnd primitives are collected into one triangle (or line) list and
// drawn when a state that the GPU needs changes, so the many small quads of the HUD, text,
// sparkles and shadows cost one draw call per state instead of one each.
//
// ES cannot read the depth buffer: gl1_DepthLess() answers the lens flare's "is the sun hidden"
// with a one-pixel depth-tested draw read back as a colour.

#define GL1ES_IMPL
#include "gl1es.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- ES 2.0 entry points ----------------------------------------------------------------------

#define ESFUNCS \
	F(void, ActiveTexture, (GLenum)) \
	F(void, AttachShader, (GLuint, GLuint)) \
	F(void, BindAttribLocation, (GLuint, GLuint, const char *)) \
	F(void, BindTexture, (GLenum, GLuint)) \
	F(void, BlendFunc, (GLenum, GLenum)) \
	F(void, Clear, (GLbitfield)) \
	F(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
	F(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
	F(void, CompileShader, (GLuint)) \
	F(GLuint, CreateProgram, (void)) \
	F(GLuint, CreateShader, (GLenum)) \
	F(void, CullFace, (GLenum)) \
	F(void, DeleteTextures, (GLsizei, const GLuint *)) \
	F(void, DepthFunc, (GLenum)) \
	F(void, DepthMask, (GLboolean)) \
	F(void, Disable, (GLenum)) \
	F(void, DisableVertexAttribArray, (GLuint)) \
	F(void, DrawArrays, (GLenum, GLint, GLsizei)) \
	F(void, DrawElements, (GLenum, GLsizei, GLenum, const void *)) \
	F(void, Enable, (GLenum)) \
	F(void, EnableVertexAttribArray, (GLuint)) \
	F(void, Finish, (void)) \
	F(void, FrontFace, (GLenum)) \
	F(void, GenTextures, (GLsizei, GLuint *)) \
	F(GLenum, GetError, (void)) \
	F(void, GetIntegerv, (GLenum, GLint *)) \
	F(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
	F(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
	F(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
	F(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
	F(const GLubyte *, GetString, (GLenum)) \
	F(GLint, GetUniformLocation, (GLuint, const char *)) \
	F(void, LineWidth, (GLfloat)) \
	F(void, LinkProgram, (GLuint)) \
	F(void, PixelStorei, (GLenum, GLint)) \
	F(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
	F(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
	F(void, ShaderSource, (GLuint, GLsizei, const char *const *, const GLint *)) \
	F(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
	F(void, TexParameteri, (GLenum, GLenum, GLint)) \
	F(void, Uniform1f, (GLint, GLfloat)) \
	F(void, Uniform1i, (GLint, GLint)) \
	F(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
	F(void, Uniform3fv, (GLint, GLsizei, const GLfloat *)) \
	F(void, Uniform4fv, (GLint, GLsizei, const GLfloat *)) \
	F(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	F(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	F(void, UseProgram, (GLuint)) \
	F(void, VertexAttrib4fv, (GLuint, const GLfloat *)) \
	F(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
	F(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

#define F(ret, name, args) static ret (*es_##name) args;
ESFUNCS
#undef F

static void fatal(const char *what)
{
	SDL_LogError(SDL_LOG_CATEGORY_RENDER, "gl1es: %s", what);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "gl1es", what, NULL);
	abort();
}

// Logs a GL 1.x feature the layer does not do, once per call site.
#define UNSUPPORTED(fmt, ...) do { static int said; if (!said) { said = 1; \
	SDL_Log("gl1es: not supported: " fmt, __VA_ARGS__); } } while (0)

// ---- matrices (column-major, as GL) -------------------------------------------------------------

typedef struct { float m[16]; } Mat;

static void identity(Mat *a) { for (int i = 0; i < 16; i++) a->m[i] = (i % 5 == 0) ? 1.0f : 0.0f; }

static void mulInto(Mat *r, const Mat *a, const Mat *b)		// r = a * b
{
	Mat t;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			t.m[j*4+i] = a->m[i]*b->m[j*4] + a->m[4+i]*b->m[j*4+1] + a->m[8+i]*b->m[j*4+2] + a->m[12+i]*b->m[j*4+3];
	*r = t;
}

// inverse transpose of the upper 3x3 (the normal matrix)
static void normalMatrix(const Mat *a, float n[9])
{
	const float *m = a->m;
	float c00 = m[5]*m[10] - m[9]*m[6],  c01 = m[8]*m[6] - m[4]*m[10], c02 = m[4]*m[9] - m[8]*m[5];
	float c10 = m[9]*m[2] - m[1]*m[10],  c11 = m[0]*m[10] - m[8]*m[2], c12 = m[8]*m[1] - m[0]*m[9];
	float c20 = m[1]*m[6] - m[5]*m[2],   c21 = m[4]*m[2] - m[0]*m[6],  c22 = m[0]*m[5] - m[4]*m[1];
	float det = m[0]*c00 + m[1]*c01 + m[2]*c02;
	float d = det != 0 ? 1.0f/det : 0;
	// column-major mat3: column j = row j of the cofactor matrix / det
	n[0] = c00*d; n[1] = c01*d; n[2] = c02*d;
	n[3] = c10*d; n[4] = c11*d; n[5] = c12*d;
	n[6] = c20*d; n[7] = c21*d; n[8] = c22*d;
}

#define MV_DEPTH 32
#define PJ_DEPTH 4
#define TX_DEPTH 4

static Mat mvStack[MV_DEPTH], pjStack[PJ_DEPTH], txStack[2][TX_DEPTH];
static int mvTop, pjTop, txTop[2];
static GLenum matMode = GL_MODELVIEW;

// Every state group the shaders read has a version; a program uploads a group again only when
// the version it last saw is not the current one.
static unsigned serial = 1;
static unsigned mvVer = 1, pjVer = 1, txVer[2] = { 1, 1 }, lightVer = 1, matVer = 1, fogVer = 1, alphaVer = 1;

// ---- GL 1.x state as the game set it --------------------------------------------------------------

static struct
{
	GLboolean lighting, light[4], colorMaterial, normalize, fog, alphaTest, blend, depthTest, cull;
	GLboolean tex[2], genS[2], genT[2];
} cap;

static GLenum alphaFunc = GL_ALWAYS;
static float alphaRef = 0;
static GLenum blendSrc = GL_ONE, blendDst = GL_ZERO, depthFunc = GL_LESS, cullMode = GL_BACK, frontFace = GL_CCW;
static GLboolean depthMask = GL_TRUE, colorMask[4] = { 1, 1, 1, 1 };
static float lineWidth = 1;
static GLenum envMode[2] = { GL_MODULATE, GL_MODULATE }, combRGB[2] = { GL_MODULATE, GL_MODULATE },
	combAlpha[2] = { GL_MODULATE, GL_MODULATE };
static GLint genModeS[2] = { GL_EYE_LINEAR, GL_EYE_LINEAR }, genModeT[2] = { GL_EYE_LINEAR, GL_EYE_LINEAR };
static GLuint bound[2];
static int activeUnit, clientUnit;
static GLint viewport[4];
static GLint unpackAlign = 4;

static float sceneAmbient[4] = { .2f, .2f, .2f, 1 };
static float lightDir[4][3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
static float lightAmb[4][4] = { { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
static float lightDiff[4][4] = { { 1, 1, 1, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
static float matAmb[4] = { .2f, .2f, .2f, 1 }, matDiff[4] = { .8f, .8f, .8f, 1 }, matEmis[4] = { 0, 0, 0, 1 };
static float fogStart = 0, fogEnd = 1, fogColor[4];

static float curColor[4] = { 1, 1, 1, 1 }, curUV[2], curNormal[3] = { 0, 0, 1 };

typedef struct { GLboolean on; GLint size; GLenum type; GLsizei stride; const void *ptr; } Arr;
static Arr aVert, aColor, aNormal, aTex[2];

static SDL_bool uintIndices;

// ---- what the ES context currently has -----------------------------------------------------------

static struct
{
	GLuint prog;
	int blend, depth, dmask, cull, attrOn[5];
	GLenum bsrc, bdst, dfunc, cullMode, frontFace;
	GLboolean cmask[4];
	float lineWidth;
	int unit;
	GLuint tex[2];
} es;

static void esCap(GLenum c, int on, int *was)
{
	if (on != *was) { if (on) es_Enable(c); else es_Disable(c); *was = on; }
}

static void esUnit(int u)
{
	if (es.unit != u) { es_ActiveTexture(GL_TEXTURE0 + u); es.unit = u; }
}

static void esBind(int u, GLuint t)
{
	if (es.tex[u] != t) { esUnit(u); es_BindTexture(GL_TEXTURE_2D, t); es.tex[u] = t; }
}

static void esMasks(void)
{
	if (memcmp(es.cmask, colorMask, sizeof colorMask))
	{
		es_ColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
		memcpy(es.cmask, colorMask, sizeof colorMask);
	}
	if (es.dmask != depthMask) { es_DepthMask(depthMask); es.dmask = depthMask; }
}

static void esAttr(int loc, int on)
{
	if (es.attrOn[loc] != on)
	{
		if (on) es_EnableVertexAttribArray(loc); else es_DisableVertexAttribArray(loc);
		es.attrOn[loc] = on;
	}
}

enum { A_POS, A_NORMAL, A_COLOR, A_UV0, A_UV1 };

// ---- shader programs ------------------------------------------------------------------------------

enum { ENV_MODULATE, ENV_REPLACE, ENV_ADD, ENV_ADDALPHA };

#define K_LIGHTING   (1u << 0)
#define K_LIGHT(i)   (1u << (1 + (i)))
#define K_COLMAT     (1u << 5)
#define K_NORMALIZE  (1u << 6)
#define K_TEX(u)     (1u << (7 + 4*(u)))
#define K_SPHERE(u)  (1u << (8 + 4*(u)))
#define K_ENVSHIFT(u) (9 + 4*(u))
#define K_FOG        (1u << 15)
#define K_ALPHASHIFT 16

static int envCode(int u)
{
	switch (envMode[u])
	{
		case GL_MODULATE: return ENV_MODULATE;
		case GL_REPLACE:  return ENV_REPLACE;
		case GL_ADD:      return ENV_ADD;
		case GL_COMBINE:
			if (combRGB[u] == GL_ADD) return combAlpha[u] == GL_ADD ? ENV_ADDALPHA : ENV_ADD;
			if (combRGB[u] != GL_MODULATE || combAlpha[u] != GL_MODULATE)
				UNSUPPORTED("texture combine 0x%x / 0x%x", combRGB[u], combAlpha[u]);
			return ENV_MODULATE;
		default:
			UNSUPPORTED("texture env mode 0x%x", envMode[u]);
			return ENV_MODULATE;
	}
}

static uint32_t stateKey(void)
{
	uint32_t k = 0;
	if (cap.lighting)
	{
		k |= K_LIGHTING;
		for (int i = 0; i < 4; i++) if (cap.light[i]) k |= K_LIGHT(i);
		if (cap.colorMaterial) k |= K_COLMAT;
	}
	SDL_bool needNormal = cap.lighting;
	for (int u = 0; u < 2; u++)
	{
		if (!cap.tex[u] || !bound[u]) continue;
		k |= K_TEX(u);
		if (cap.genS[u] && cap.genT[u])
		{
			if (genModeS[u] == GL_SPHERE_MAP && genModeT[u] == GL_SPHERE_MAP) { k |= K_SPHERE(u); needNormal = SDL_TRUE; }
			else UNSUPPORTED("texgen mode 0x%x", genModeS[u]);
		}
		k |= (uint32_t) envCode(u) << K_ENVSHIFT(u);
	}
	if (needNormal && cap.normalize) k |= K_NORMALIZE;
	if (cap.fog) k |= K_FOG;
	if (cap.alphaTest && alphaFunc != GL_ALWAYS) k |= (uint32_t) (alphaFunc - GL_NEVER + 1) << K_ALPHASHIFT;
	return k;
}

typedef struct
{
	uint32_t key;
	GLuint prog;
	GLint uMVP, uMV, uNM, uTexM[2], uSceneAmb, uMatAmb, uMatDiff, uMatEmis, uLightDir, uLightAmb, uLightDiff;
	GLint uFog, uFogColor, uAlphaRef;
	unsigned mvVer, pjVer, txVer[2], lightVer, matVer, fogVer, alphaVer;
} Prog;

#define MAX_PROGS 256
static Prog progs[MAX_PROGS];
static int numProgs;

static const char *vsBody =
	"attribute vec4 aPos; attribute vec3 aNormal; attribute vec4 aColor; attribute vec2 aUV0; attribute vec2 aUV1;\n"
	"uniform mat4 uMVP;\n"
	"#if defined(LIGHTING) || defined(SPHERE0) || defined(SPHERE1)\n#define NEED_NORMAL\n#endif\n"
	"#if defined(NEED_NORMAL) || defined(FOG)\n#define NEED_EYE\nuniform mat4 uMV;\n#endif\n"
	"#ifdef NEED_NORMAL\nuniform mat3 uNM;\n#endif\n"
	"#ifdef LIGHTING\n"
	"uniform vec4 uSceneAmb, uMatAmb, uMatDiff, uMatEmis;\n"
	"uniform vec3 uLightDir[4]; uniform vec4 uLightAmb[4]; uniform vec4 uLightDiff[4];\n"
	"#endif\n"
	"#ifdef TEX0\nuniform mat4 uTex0M; varying vec2 vUV0;\n#endif\n"
	"#ifdef TEX1\nuniform mat4 uTex1M; varying vec2 vUV1;\n#endif\n"
	"#ifdef FOG\nuniform vec2 uFog; varying float vFog;\n#endif\n"
	"varying vec4 vColor;\n"
	"vec2 sphereMap(vec3 eye, vec3 n)\n"
	"{ vec3 u = normalize(eye); vec3 r = u - 2.0*dot(n, u)*n;\n"
	"  float m = 2.0*sqrt(r.x*r.x + r.y*r.y + (r.z+1.0)*(r.z+1.0)); return r.xy/m + 0.5; }\n"
	"void main()\n"
	"{\n"
	"  gl_Position = uMVP*aPos;\n"
	"#ifdef NEED_EYE\n  vec4 eye = uMV*aPos;\n#endif\n"
	"#ifdef NEED_NORMAL\n  vec3 n = uNM*aNormal;\n#ifdef NORMALIZE\n  n = normalize(n);\n#endif\n#endif\n"
	"#ifdef LIGHTING\n"
	"#ifdef COLMAT\n  vec4 ma = aColor, md = aColor;\n#else\n  vec4 ma = uMatAmb, md = uMatDiff;\n#endif\n"
	"  vec3 c = uMatEmis.rgb + uSceneAmb.rgb*ma.rgb;\n"
	"#ifdef LIGHT0\n  c += uLightAmb[0].rgb*ma.rgb + max(dot(n, uLightDir[0]), 0.0)*uLightDiff[0].rgb*md.rgb;\n#endif\n"
	"#ifdef LIGHT1\n  c += uLightAmb[1].rgb*ma.rgb + max(dot(n, uLightDir[1]), 0.0)*uLightDiff[1].rgb*md.rgb;\n#endif\n"
	"#ifdef LIGHT2\n  c += uLightAmb[2].rgb*ma.rgb + max(dot(n, uLightDir[2]), 0.0)*uLightDiff[2].rgb*md.rgb;\n#endif\n"
	"#ifdef LIGHT3\n  c += uLightAmb[3].rgb*ma.rgb + max(dot(n, uLightDir[3]), 0.0)*uLightDiff[3].rgb*md.rgb;\n#endif\n"
	"  vColor = vec4(clamp(c, 0.0, 1.0), clamp(md.a, 0.0, 1.0));\n"
	"#else\n  vColor = clamp(aColor, 0.0, 1.0);\n#endif\n"
	"#ifdef TEX0\n"
	"#ifdef SPHERE0\n  vUV0 = (uTex0M*vec4(sphereMap(eye.xyz, n), 0.0, 1.0)).xy;\n"
	"#else\n  vUV0 = (uTex0M*vec4(aUV0, 0.0, 1.0)).xy;\n#endif\n"
	"#endif\n"
	"#ifdef TEX1\n"
	"#ifdef SPHERE1\n  vUV1 = (uTex1M*vec4(sphereMap(eye.xyz, n), 0.0, 1.0)).xy;\n"
	"#else\n  vUV1 = (uTex1M*vec4(aUV1, 0.0, 1.0)).xy;\n#endif\n"
	"#endif\n"
	// the fog factor is clamped per fragment: clamped per vertex, a big triangle reaching past the
	// fog end would be fogged too much all over
	"#ifdef FOG\n  vFog = (uFog.x - abs(eye.z))*uFog.y;\n#endif\n"
	"}\n";

// Texture coordinates in high precision where the GPU has it: the terrain repeats its textures
// over large u/v ranges, which 16-bit floats would round to whole texels.
static const char *fsBody =
	"precision mediump float;\n"
	"#ifdef GL_FRAGMENT_PRECISION_HIGH\n#define UVP highp\n#else\n#define UVP mediump\n#endif\n"
	"#ifdef TEX0\nuniform sampler2D uTex0; varying UVP vec2 vUV0;\n#endif\n"
	"#ifdef TEX1\nuniform sampler2D uTex1; varying UVP vec2 vUV1;\n#endif\n"
	"#ifdef FOG\nuniform vec3 uFogColor; varying float vFog;\n#endif\n"
	"#ifdef ALPHAFUNC\nuniform float uAlphaRef;\n#endif\n"
	"varying vec4 vColor;\n"
	"void main()\n"
	"{\n"
	"  vec4 c = vColor;\n"
	"#ifdef TEX0\n  vec4 t0 = texture2D(uTex0, vUV0);\n"
	"#if ENV0 == 0\n  c *= t0;\n#elif ENV0 == 1\n  c = t0;\n"
	"#elif ENV0 == 2\n  c = vec4(min(c.rgb + t0.rgb, 1.0), c.a*t0.a);\n#else\n  c = min(c + t0, 1.0);\n#endif\n"
	"#endif\n"
	"#ifdef TEX1\n  vec4 t1 = texture2D(uTex1, vUV1);\n"
	"#if ENV1 == 0\n  c *= t1;\n#elif ENV1 == 1\n  c = t1;\n"
	"#elif ENV1 == 2\n  c = vec4(min(c.rgb + t1.rgb, 1.0), c.a*t1.a);\n#else\n  c = min(c + t1, 1.0);\n#endif\n"
	"#endif\n"
	// alpha test on the 8-bit value, as the fixed-function hardware compares it
	"#ifdef ALPHAFUNC\n  float a = floor(c.a*255.0 + 0.5);\n"
	"#if ALPHAFUNC == 1\n  discard;\n"
	"#elif ALPHAFUNC == 2\n  if (!(a < uAlphaRef)) discard;\n"
	"#elif ALPHAFUNC == 3\n  if (a != uAlphaRef) discard;\n"
	"#elif ALPHAFUNC == 4\n  if (!(a <= uAlphaRef)) discard;\n"
	"#elif ALPHAFUNC == 5\n  if (!(a > uAlphaRef)) discard;\n"
	"#elif ALPHAFUNC == 6\n  if (a == uAlphaRef) discard;\n"
	"#else\n  if (!(a >= uAlphaRef)) discard;\n#endif\n"
	"#endif\n"
	"#ifdef FOG\n  c.rgb = mix(uFogColor, c.rgb, clamp(vFog, 0.0, 1.0));\n#endif\n"
	"  gl_FragColor = c;\n"
	"}\n";

static GLuint compile(GLenum type, const char *defs, const char *body)
{
	const char *src[2] = { defs, body };
	GLuint s = es_CreateShader(type);
	es_ShaderSource(s, 2, src, NULL);
	es_CompileShader(s);
	GLint ok = 0;
	es_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[2048] = "";
		es_GetShaderInfoLog(s, sizeof log, NULL, log);
		SDL_Log("gl1es: shader:\n%s%s\n%s", defs, body, log);
		fatal("could not compile a shader");
	}
	return s;
}

static GLuint link(GLuint vs, GLuint fs)
{
	GLuint p = es_CreateProgram();
	es_AttachShader(p, vs);
	es_AttachShader(p, fs);
	es_BindAttribLocation(p, A_POS, "aPos");
	es_BindAttribLocation(p, A_NORMAL, "aNormal");
	es_BindAttribLocation(p, A_COLOR, "aColor");
	es_BindAttribLocation(p, A_UV0, "aUV0");
	es_BindAttribLocation(p, A_UV1, "aUV1");
	es_LinkProgram(p);
	GLint ok = 0;
	es_GetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[2048] = "";
		es_GetProgramInfoLog(p, sizeof log, NULL, log);
		SDL_Log("gl1es: link: %s", log);
		fatal("could not link a shader");
	}
	return p;
}

static Prog *makeProg(uint32_t k)
{
	if (numProgs == MAX_PROGS) fatal("too many shader variants");

	char defs[512];
	int n = 0;
	#define DEF(...) n += SDL_snprintf(defs + n, sizeof defs - n, __VA_ARGS__)
	if (k & K_LIGHTING) DEF("#define LIGHTING\n");
	for (int i = 0; i < 4; i++) if (k & K_LIGHT(i)) DEF("#define LIGHT%d\n", i);
	if (k & K_COLMAT) DEF("#define COLMAT\n");
	if (k & K_NORMALIZE) DEF("#define NORMALIZE\n");
	for (int u = 0; u < 2; u++)
	{
		if (!(k & K_TEX(u))) continue;
		DEF("#define TEX%d\n#define ENV%d %d\n", u, u, (int) ((k >> K_ENVSHIFT(u)) & 3));
		if (k & K_SPHERE(u)) DEF("#define SPHERE%d\n", u);
	}
	if (k & K_FOG) DEF("#define FOG\n");
	if (k >> K_ALPHASHIFT) DEF("#define ALPHAFUNC %d\n", (int) (k >> K_ALPHASHIFT));
	#undef DEF

	Prog *p = &progs[numProgs++];
	memset(p, 0, sizeof *p);
	p->key = k;
	p->prog = link(compile(GL_VERTEX_SHADER, defs, vsBody), compile(GL_FRAGMENT_SHADER, defs, fsBody));

	#define U(name) es_GetUniformLocation(p->prog, name)
	p->uMVP = U("uMVP"); p->uMV = U("uMV"); p->uNM = U("uNM");
	p->uTexM[0] = U("uTex0M"); p->uTexM[1] = U("uTex1M");
	p->uSceneAmb = U("uSceneAmb"); p->uMatAmb = U("uMatAmb"); p->uMatDiff = U("uMatDiff"); p->uMatEmis = U("uMatEmis");
	p->uLightDir = U("uLightDir"); p->uLightAmb = U("uLightAmb"); p->uLightDiff = U("uLightDiff");
	p->uFog = U("uFog"); p->uFogColor = U("uFogColor"); p->uAlphaRef = U("uAlphaRef");
	es_UseProgram(p->prog);
	es.prog = p->prog;
	GLint s0 = U("uTex0"), s1 = U("uTex1");
	if (s0 >= 0) es_Uniform1i(s0, 0);
	if (s1 >= 0) es_Uniform1i(s1, 1);
	#undef U
	return p;
}

static Prog *getProg(uint32_t k)
{
	static Prog *last;
	if (last && last->key == k) return last;
	for (int i = 0; i < numProgs; i++)
		if (progs[i].key == k) return last = &progs[i];
	return last = makeProg(k);
}

// ---- drawing --------------------------------------------------------------------------------------

static Mat mvp;
static float normalMat[9];
static unsigned mvpMV, mvpPJ, nmMV;

// Makes the ES context match the GL 1.x state for the next draw.
static Prog *prepare(GLenum prim)
{
	uint32_t k = stateKey();
	Prog *p = getProg(k);
	if (es.prog != p->prog) { es_UseProgram(p->prog); es.prog = p->prog; }

	if (p->mvVer != mvVer || p->pjVer != pjVer)
	{
		if (mvpMV != mvVer || mvpPJ != pjVer)
		{
			mulInto(&mvp, &pjStack[pjTop], &mvStack[mvTop]);
			mvpMV = mvVer; mvpPJ = pjVer;
		}
		es_UniformMatrix4fv(p->uMVP, 1, GL_FALSE, mvp.m);
		if (p->uMV >= 0) es_UniformMatrix4fv(p->uMV, 1, GL_FALSE, mvStack[mvTop].m);
		if (p->uNM >= 0)
		{
			if (nmMV != mvVer) { normalMatrix(&mvStack[mvTop], normalMat); nmMV = mvVer; }
			es_UniformMatrix3fv(p->uNM, 1, GL_FALSE, normalMat);
		}
		p->mvVer = mvVer; p->pjVer = pjVer;
	}
	for (int u = 0; u < 2; u++)
		if ((k & K_TEX(u)) && p->txVer[u] != txVer[u])
		{
			es_UniformMatrix4fv(p->uTexM[u], 1, GL_FALSE, txStack[u][txTop[u]].m);
			p->txVer[u] = txVer[u];
		}
	if (k & K_LIGHTING)
	{
		if (p->lightVer != lightVer)
		{
			es_Uniform4fv(p->uSceneAmb, 1, sceneAmbient);
			es_Uniform3fv(p->uLightDir, 4, &lightDir[0][0]);
			es_Uniform4fv(p->uLightAmb, 4, &lightAmb[0][0]);
			es_Uniform4fv(p->uLightDiff, 4, &lightDiff[0][0]);
			p->lightVer = lightVer;
		}
		if (p->matVer != matVer)
		{
			if (p->uMatAmb >= 0) es_Uniform4fv(p->uMatAmb, 1, matAmb);
			if (p->uMatDiff >= 0) es_Uniform4fv(p->uMatDiff, 1, matDiff);
			es_Uniform4fv(p->uMatEmis, 1, matEmis);
			p->matVer = matVer;
		}
	}
	if ((k & K_FOG) && p->fogVer != fogVer)
	{
		es_Uniform2f(p->uFog, fogEnd, fogEnd != fogStart ? 1.0f/(fogEnd - fogStart) : 1e30f);
		es_Uniform3fv(p->uFogColor, 1, fogColor);
		p->fogVer = fogVer;
	}
	if ((k >> K_ALPHASHIFT) && p->alphaVer != alphaVer)
	{
		float r = alphaRef < 0 ? 0 : (alphaRef > 1 ? 1 : alphaRef);
		es_Uniform1f(p->uAlphaRef, floorf(r*255.0f + 0.5f));
		p->alphaVer = alphaVer;
	}

	esCap(GL_BLEND, cap.blend, &es.blend);
	if (cap.blend && (es.bsrc != blendSrc || es.bdst != blendDst))
	{
		es_BlendFunc(blendSrc, blendDst);
		es.bsrc = blendSrc; es.bdst = blendDst;
	}
	esCap(GL_DEPTH_TEST, cap.depthTest, &es.depth);
	if (cap.depthTest && es.dfunc != depthFunc) { es_DepthFunc(depthFunc); es.dfunc = depthFunc; }
	SDL_bool tris = prim == GL_TRIANGLES;
	esCap(GL_CULL_FACE, cap.cull && tris, &es.cull);
	if (cap.cull && tris)
	{
		if (es.cullMode != cullMode) { es_CullFace(cullMode); es.cullMode = cullMode; }
		if (es.frontFace != frontFace) { es_FrontFace(frontFace); es.frontFace = frontFace; }
	}
	esMasks();
	if (prim == GL_LINES && es.lineWidth != lineWidth) { es_LineWidth(lineWidth); es.lineWidth = lineWidth; }
	for (int u = 0; u < 2; u++)
		if (k & K_TEX(u)) esBind(u, bound[u]);
	return p;
}

static void attrArray(int loc, const Arr *a, GLboolean normalized)
{
	esAttr(loc, 1);
	es_VertexAttribPointer(loc, a->size, a->type, normalized, a->stride, a->ptr);
}

static void attrConst(int loc, const float *v, int n)
{
	float c[4] = { 0, 0, 0, 1 };
	memcpy(c, v, n*sizeof(float));
	esAttr(loc, 0);
	es_VertexAttrib4fv(loc, c);
}

// ---- glBegin/glEnd: one list of triangles (or lines, or points) per run of equal state --------------

typedef struct { float x, y, z, nx, ny, nz, r, g, b, a, u, v; } ImmVert;

static ImmVert *prim, *batch;
static int primN, primCap, batchN, batchCap;
static GLenum primMode, batchPrim;
static SDL_bool inBegin;

static void flush(void)
{
	if (!batchN) return;
	int n = batchN;
	batchN = 0;
	prepare(batchPrim);
	const ImmVert *v = batch;
	esAttr(A_POS, 1);    es_VertexAttribPointer(A_POS, 3, GL_FLOAT, GL_FALSE, sizeof *v, &v->x);
	esAttr(A_NORMAL, 1); es_VertexAttribPointer(A_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof *v, &v->nx);
	esAttr(A_COLOR, 1);  es_VertexAttribPointer(A_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof *v, &v->r);
	esAttr(A_UV0, 1);    es_VertexAttribPointer(A_UV0, 2, GL_FLOAT, GL_FALSE, sizeof *v, &v->u);
	static const float zero[2];
	attrConst(A_UV1, zero, 2);
	es_DrawArrays(batchPrim, 0, n);
}

static ImmVert *reserve(GLenum p, int n)
{
	if (batchN && batchPrim != p) flush();
	batchPrim = p;
	if (batchN + n > batchCap)
	{
		batchCap = SDL_max(batchCap*2, batchN + n + 1024);
		batch = SDL_realloc(batch, batchCap*sizeof *batch);
		if (!batch) fatal("out of memory");
	}
	batchN += n;
	return batch + batchN - n;
}

void gl1_Begin(GLenum mode)
{
	primMode = mode;
	primN = 0;
	inBegin = SDL_TRUE;
}

void gl1_End(void)
{
	inBegin = SDL_FALSE;
	const ImmVert *p = prim;
	int n = primN;
	ImmVert *o;
	switch (primMode)
	{
		case GL_POINTS:
			if (n) memcpy(reserve(GL_POINTS, n), p, n*sizeof *p);
			break;
		case GL_LINES:
			n &= ~1;
			if (n) memcpy(reserve(GL_LINES, n), p, n*sizeof *p);
			break;
		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
		{
			int lines = primMode == GL_LINE_LOOP ? n : n - 1;
			if (n < 2) break;
			o = reserve(GL_LINES, lines*2);
			for (int i = 0; i < lines; i++) { *o++ = p[i]; *o++ = p[(i+1) % n]; }
			break;
		}
		case GL_TRIANGLES:
			n -= n % 3;
			if (n) memcpy(reserve(GL_TRIANGLES, n), p, n*sizeof *p);
			break;
		case GL_TRIANGLE_STRIP:
			if (n < 3) break;
			o = reserve(GL_TRIANGLES, (n-2)*3);
			for (int i = 0; i < n-2; i++)
			{
				if (i & 1) { *o++ = p[i+1]; *o++ = p[i]; }
				else { *o++ = p[i]; *o++ = p[i+1]; }
				*o++ = p[i+2];
			}
			break;
		case GL_TRIANGLE_FAN:
		case GL_POLYGON:
			if (n < 3) break;
			o = reserve(GL_TRIANGLES, (n-2)*3);
			for (int i = 0; i < n-2; i++) { *o++ = p[0]; *o++ = p[i+1]; *o++ = p[i+2]; }
			break;
		case GL_QUADS:
			// split as Mesa does with the last vertex provoking: (0 1 3) (1 2 3)
			n &= ~3;
			if (!n) break;
			o = reserve(GL_TRIANGLES, n/4*6);
			for (int i = 0; i < n; i += 4)
			{
				*o++ = p[i]; *o++ = p[i+1]; *o++ = p[i+3];
				*o++ = p[i+1]; *o++ = p[i+2]; *o++ = p[i+3];
			}
			break;
		case GL_QUAD_STRIP:
			if (n < 4) break;
			o = reserve(GL_TRIANGLES, (n/2-1)*6);
			for (int i = 0; i+3 < n; i += 2)
			{
				*o++ = p[i+2]; *o++ = p[i]; *o++ = p[i+3];
				*o++ = p[i]; *o++ = p[i+1]; *o++ = p[i+3];
			}
			break;
		default:
			UNSUPPORTED("primitive 0x%x", primMode);
	}
}

static void vertex(float x, float y, float z)
{
	if (!inBegin) return;
	if (primN == primCap)
	{
		primCap = SDL_max(64, primCap*2);
		prim = SDL_realloc(prim, primCap*sizeof *prim);
		if (!prim) fatal("out of memory");
	}
	ImmVert *v = &prim[primN++];
	v->x = x; v->y = y; v->z = z;
	v->nx = curNormal[0]; v->ny = curNormal[1]; v->nz = curNormal[2];
	v->r = curColor[0]; v->g = curColor[1]; v->b = curColor[2]; v->a = curColor[3];
	v->u = curUV[0]; v->v = curUV[1];
}

void gl1_Vertex2f(GLfloat x, GLfloat y) { vertex(x, y, 0); }
void gl1_Vertex3f(GLfloat x, GLfloat y, GLfloat z) { vertex(x, y, z); }
void gl1_Vertex3fv(const GLfloat *v) { vertex(v[0], v[1], v[2]); }
void gl1_TexCoord2f(GLfloat s, GLfloat t) { curUV[0] = s; curUV[1] = t; }
void gl1_TexCoord2fv(const GLfloat *v) { curUV[0] = v[0]; curUV[1] = v[1]; }
void gl1_Normal3f(GLfloat x, GLfloat y, GLfloat z) { curNormal[0] = x; curNormal[1] = y; curNormal[2] = z; }
void gl1_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { curColor[0] = r; curColor[1] = g; curColor[2] = b; curColor[3] = a; }
void gl1_Color3f(GLfloat r, GLfloat g, GLfloat b) { gl1_Color4f(r, g, b, 1); }
void gl1_Color4fv(const GLfloat *v) { gl1_Color4f(v[0], v[1], v[2], v[3]); }

// ---- vertex arrays ----------------------------------------------------------------------------------

static Arr *clientArr(GLenum a)
{
	switch (a)
	{
		case GL_VERTEX_ARRAY:        return &aVert;
		case GL_COLOR_ARRAY:         return &aColor;
		case GL_NORMAL_ARRAY:        return &aNormal;
		case GL_TEXTURE_COORD_ARRAY: return &aTex[clientUnit];
		default:
			UNSUPPORTED("client array 0x%x", a);
			return NULL;
	}
}

void gl1_EnableClientState(GLenum a) { Arr *p = clientArr(a); if (p) p->on = GL_TRUE; }
void gl1_DisableClientState(GLenum a) { Arr *p = clientArr(a); if (p) p->on = GL_FALSE; }

static void setArr(Arr *a, GLint size, GLenum type, GLsizei stride, const void *ptr)
{
	a->size = size; a->type = type; a->stride = stride; a->ptr = ptr;
}

void gl1_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { setArr(&aVert, size, type, stride, p); }
void gl1_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { setArr(&aColor, size, type, stride, p); }
void gl1_NormalPointer(GLenum type, GLsizei stride, const GLvoid *p) { setArr(&aNormal, 3, type, stride, p); }
void gl1_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { setArr(&aTex[clientUnit], size, type, stride, p); }

void gl1_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	flush();
	if (!aVert.on || !count) return;
	if (mode != GL_TRIANGLES && mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN && mode != GL_LINES)
	{
		UNSUPPORTED("glDrawElements mode 0x%x", mode);
		return;
	}
	prepare(mode == GL_LINES ? GL_LINES : GL_TRIANGLES);
	attrArray(A_POS, &aVert, GL_FALSE);
	if (aColor.on) attrArray(A_COLOR, &aColor, aColor.type != GL_FLOAT);
	else attrConst(A_COLOR, curColor, 4);
	if (aNormal.on) attrArray(A_NORMAL, &aNormal, aNormal.type != GL_FLOAT);
	else attrConst(A_NORMAL, curNormal, 3);
	if (aTex[0].on) attrArray(A_UV0, &aTex[0], GL_FALSE);
	else attrConst(A_UV0, curUV, 2);
	if (aTex[1].on) attrArray(A_UV1, &aTex[1], GL_FALSE);
	else { static const float zero[2]; attrConst(A_UV1, zero, 2); }

	if (type == GL_UNSIGNED_INT && !uintIndices)
	{
		static GLushort *idx;
		static int idxCap;
		if (count > idxCap) { idxCap = count; idx = SDL_realloc(idx, idxCap*sizeof *idx); if (!idx) fatal("out of memory"); }
		const GLuint *src = indices;
		for (int i = 0; i < count; i++)
		{
			if (src[i] > 0xffff) fatal("mesh too big for 16-bit indices");
			idx[i] = (GLushort) src[i];
		}
		es_DrawElements(mode, count, GL_UNSIGNED_SHORT, idx);
	}
	else es_DrawElements(mode, count, type, indices);
}

// ---- state ------------------------------------------------------------------------------------------

static GLboolean *capPtr(GLenum c)
{
	switch (c)
	{
		case GL_LIGHTING:       return &cap.lighting;
		case GL_LIGHT0:         return &cap.light[0];
		case GL_LIGHT1:         return &cap.light[1];
		case GL_LIGHT2:         return &cap.light[2];
		case GL_LIGHT3:         return &cap.light[3];
		case GL_COLOR_MATERIAL: return &cap.colorMaterial;
		case GL_NORMALIZE:      return &cap.normalize;
		case GL_FOG:            return &cap.fog;
		case GL_ALPHA_TEST:     return &cap.alphaTest;
		case GL_BLEND:          return &cap.blend;
		case GL_DEPTH_TEST:     return &cap.depthTest;
		case GL_CULL_FACE:      return &cap.cull;
		case GL_TEXTURE_2D:     return &cap.tex[activeUnit];
		case GL_TEXTURE_GEN_S:  return &cap.genS[activeUnit];
		case GL_TEXTURE_GEN_T:  return &cap.genT[activeUnit];
		default:                return NULL;
	}
}

static void setCap(GLenum c, GLboolean on)
{
	GLboolean *p = capPtr(c);
	if (p)
	{
		if (*p != on) { flush(); *p = on; }
		return;
	}
	switch (c)
	{
		case GL_RESCALE_NORMAL:		// normals are normalized in the shader when GL_NORMALIZE is on
		case GL_DITHER:
		case GL_MULTISAMPLE:
			break;
		default:
			UNSUPPORTED("capability 0x%x", c);
	}
}

void gl1_Enable(GLenum c) { setCap(c, GL_TRUE); }
void gl1_Disable(GLenum c) { setCap(c, GL_FALSE); }

GLboolean gl1_IsEnabled(GLenum c)
{
	GLboolean *p = capPtr(c);
	return p ? *p : GL_FALSE;
}

#define SET(var, val) do { if ((var) != (val)) { flush(); (var) = (val); } } while (0)

void gl1_AlphaFunc(GLenum func, GLclampf ref)
{
	if (func != alphaFunc || ref != alphaRef) { flush(); alphaFunc = func; alphaRef = ref; alphaVer = ++serial; }
}
void gl1_BlendFunc(GLenum s, GLenum d) { SET(blendSrc, s); SET(blendDst, d); }
void gl1_DepthFunc(GLenum f) { SET(depthFunc, f); }
void gl1_DepthMask(GLboolean f) { SET(depthMask, f ? GL_TRUE : GL_FALSE); }
void gl1_CullFace(GLenum m) { SET(cullMode, m); }
void gl1_FrontFace(GLenum m) { SET(frontFace, m); }
void gl1_LineWidth(GLfloat w) { SET(lineWidth, w); }
void gl1_PolygonMode(GLenum face, GLenum mode) { if (mode != GL_FILL) UNSUPPORTED("polygon mode 0x%x", mode); }
void gl1_Hint(GLenum target, GLenum mode) {}

void gl1_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
	GLboolean m[4] = { r ? 1 : 0, g ? 1 : 0, b ? 1 : 0, a ? 1 : 0 };
	if (memcmp(m, colorMask, sizeof m)) { flush(); memcpy(colorMask, m, sizeof m); }
}

void gl1_ActiveTexture(GLenum unit) { activeUnit = (unit - GL_TEXTURE0) & 1; }
void gl1_ClientActiveTexture(GLenum unit) { clientUnit = (unit - GL_TEXTURE0) & 1; }

void gl1_TexEnvi(GLenum target, GLenum pname, GLint param)
{
	int u = activeUnit;
	switch (pname)
	{
		case GL_TEXTURE_ENV_MODE: SET(envMode[u], (GLenum) param); break;
		case GL_COMBINE_RGB:      SET(combRGB[u], (GLenum) param); break;
		case GL_COMBINE_ALPHA:    SET(combAlpha[u], (GLenum) param); break;
		default: UNSUPPORTED("texture env 0x%x", pname);
	}
}

void gl1_TexGeni(GLenum coord, GLenum pname, GLint param)
{
	if (pname != GL_TEXTURE_GEN_MODE) return;
	if (coord == GL_S) SET(genModeS[activeUnit], param);
	else if (coord == GL_T) SET(genModeT[activeUnit], param);
}

// lighting

void gl1_ColorMaterial(GLenum face, GLenum mode)
{
	if (mode != GL_AMBIENT_AND_DIFFUSE) UNSUPPORTED("color material 0x%x", mode);
}

void gl1_LightModelfv(GLenum pname, const GLfloat *v)
{
	if (pname == GL_LIGHT_MODEL_AMBIENT) { flush(); memcpy(sceneAmbient, v, sizeof sceneAmbient); lightVer = ++serial; }
}

void gl1_LightModeli(GLenum pname, GLint param)
{
	if (pname == GL_LIGHT_MODEL_TWO_SIDE && param) UNSUPPORTED("two-sided lighting%s", "");
}

void gl1_Lightfv(GLenum light, GLenum pname, const GLfloat *v)
{
	int i = light - GL_LIGHT0;
	if (i < 0 || i > 3) { UNSUPPORTED("light 0x%x", light); return; }
	flush();
	switch (pname)
	{
		case GL_POSITION:
		{
			if (v[3] != 0) UNSUPPORTED("positional light%s", "");
			const float *m = mvStack[mvTop].m;		// directions live in eye space
			float x = m[0]*v[0] + m[4]*v[1] + m[8]*v[2];
			float y = m[1]*v[0] + m[5]*v[1] + m[9]*v[2];
			float z = m[2]*v[0] + m[6]*v[1] + m[10]*v[2];
			float l = sqrtf(x*x + y*y + z*z);
			if (l > 0) { x /= l; y /= l; z /= l; }
			lightDir[i][0] = x; lightDir[i][1] = y; lightDir[i][2] = z;
			break;
		}
		case GL_AMBIENT: memcpy(lightAmb[i], v, 4*sizeof(float)); break;
		case GL_DIFFUSE: memcpy(lightDiff[i], v, 4*sizeof(float)); break;
		default: return;						// specular: the game's materials have none
	}
	lightVer = ++serial;
}

void gl1_Materialfv(GLenum face, GLenum pname, const GLfloat *v)
{
	flush();
	switch (pname)
	{
		case GL_AMBIENT: memcpy(matAmb, v, sizeof matAmb); break;
		case GL_DIFFUSE: memcpy(matDiff, v, sizeof matDiff); break;
		case GL_AMBIENT_AND_DIFFUSE: memcpy(matAmb, v, sizeof matAmb); memcpy(matDiff, v, sizeof matDiff); break;
		case GL_EMISSION: memcpy(matEmis, v, sizeof matEmis); break;
		default: return;
	}
	matVer = ++serial;
}

// fog

void gl1_Fogf(GLenum pname, GLfloat param)
{
	switch (pname)
	{
		case GL_FOG_MODE: if ((GLenum) param != GL_LINEAR) UNSUPPORTED("fog mode 0x%x", (GLenum) param); return;
		case GL_FOG_START: if (fogStart == param) return; flush(); fogStart = param; break;
		case GL_FOG_END: if (fogEnd == param) return; flush(); fogEnd = param; break;
		default: return;						// density: linear fog only
	}
	fogVer = ++serial;
}

void gl1_Fogi(GLenum pname, GLint param) { gl1_Fogf(pname, (GLfloat) param); }

void gl1_Fogfv(GLenum pname, const GLfloat *v)
{
	if (pname == GL_FOG_COLOR) { flush(); memcpy(fogColor, v, sizeof fogColor); fogVer = ++serial; }
	else gl1_Fogf(pname, v[0]);
}

// ---- matrices -----------------------------------------------------------------------------------------

void gl1_MatrixMode(GLenum mode) { matMode = mode; }

static Mat *curMat(void)
{
	switch (matMode)
	{
		case GL_PROJECTION: return &pjStack[pjTop];
		case GL_TEXTURE:    return &txStack[activeUnit][txTop[activeUnit]];
		default:            return &mvStack[mvTop];
	}
}

// before the current matrix changes
static Mat *changeMat(void)
{
	flush();
	switch (matMode)
	{
		case GL_PROJECTION: pjVer = ++serial; break;
		case GL_TEXTURE:    txVer[activeUnit] = ++serial; break;
		default:            mvVer = ++serial; break;
	}
	return curMat();
}

void gl1_LoadIdentity(void) { identity(changeMat()); }
void gl1_LoadMatrixf(const GLfloat *m) { memcpy(changeMat()->m, m, sizeof(Mat)); }

static void multiply(const Mat *b)
{
	Mat *a = changeMat();
	mulInto(a, a, b);
}

void gl1_MultMatrixf(const GLfloat *m) { Mat b; memcpy(b.m, m, sizeof b.m); multiply(&b); }

void gl1_PushMatrix(void)
{
	switch (matMode)
	{
		case GL_PROJECTION:
			if (pjTop == PJ_DEPTH-1) fatal("projection matrix stack overflow");
			pjStack[pjTop+1] = pjStack[pjTop]; pjTop++;
			break;
		case GL_TEXTURE:
			if (txTop[activeUnit] == TX_DEPTH-1) fatal("texture matrix stack overflow");
			txStack[activeUnit][txTop[activeUnit]+1] = txStack[activeUnit][txTop[activeUnit]]; txTop[activeUnit]++;
			break;
		default:
			if (mvTop == MV_DEPTH-1) fatal("modelview matrix stack overflow");
			mvStack[mvTop+1] = mvStack[mvTop]; mvTop++;
			break;
	}
}

void gl1_PopMatrix(void)
{
	int *top = matMode == GL_PROJECTION ? &pjTop : (matMode == GL_TEXTURE ? &txTop[activeUnit] : &mvTop);
	if (!*top) return;
	changeMat();
	(*top)--;
}

void gl1_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
	Mat t; identity(&t);
	t.m[12] = x; t.m[13] = y; t.m[14] = z;
	multiply(&t);
}

void gl1_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
	Mat t; identity(&t);
	t.m[0] = x; t.m[5] = y; t.m[10] = z;
	multiply(&t);
}

void gl1_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	float l = sqrtf(x*x + y*y + z*z);
	if (l == 0) return;
	x /= l; y /= l; z /= l;
	float a = angle*(float) M_PI/180, c = cosf(a), s = sinf(a), t = 1 - c;
	Mat r; identity(&r);
	r.m[0] = x*x*t + c;   r.m[4] = x*y*t - z*s; r.m[8] = x*z*t + y*s;
	r.m[1] = y*x*t + z*s; r.m[5] = y*y*t + c;   r.m[9] = y*z*t - x*s;
	r.m[2] = x*z*t - y*s; r.m[6] = y*z*t + x*s; r.m[10] = z*z*t + c;
	multiply(&r);
}

void gl1_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	Mat o; identity(&o);
	o.m[0] = (float) (2/(r-l)); o.m[5] = (float) (2/(t-b)); o.m[10] = (float) (-2/(f-n));
	o.m[12] = (float) (-(r+l)/(r-l)); o.m[13] = (float) (-(t+b)/(t-b)); o.m[14] = (float) (-(f+n)/(f-n));
	multiply(&o);
}

void gl1_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	Mat p;
	memset(&p, 0, sizeof p);
	p.m[0] = (float) (2*n/(r-l)); p.m[5] = (float) (2*n/(t-b));
	p.m[8] = (float) ((r+l)/(r-l)); p.m[9] = (float) ((t+b)/(t-b)); p.m[10] = (float) (-(f+n)/(f-n)); p.m[11] = -1;
	p.m[14] = (float) (-2*f*n/(f-n));
	multiply(&p);
}

// ---- queries ----------------------------------------------------------------------------------------

GLenum gl1_GetError(void) { return es_GetError(); }
const GLubyte *gl1_GetString(GLenum name) { return es_GetString(name); }

void gl1_GetIntegerv(GLenum pname, GLint *p)
{
	switch (pname)
	{
		case GL_BLEND_SRC:   *p = blendSrc; break;
		case GL_BLEND_DST:   *p = blendDst; break;
		case GL_VIEWPORT:    memcpy(p, viewport, sizeof viewport); break;
		case GL_MATRIX_MODE: *p = matMode; break;
		default:             es_GetIntegerv(pname, p); break;
	}
}

void gl1_GetFloatv(GLenum pname, GLfloat *p)
{
	switch (pname)
	{
		case GL_CURRENT_COLOR:     memcpy(p, curColor, sizeof curColor); break;
		case GL_MODELVIEW_MATRIX:  memcpy(p, mvStack[mvTop].m, sizeof(Mat)); break;
		case GL_PROJECTION_MATRIX: memcpy(p, pjStack[pjTop].m, sizeof(Mat)); break;
		case GL_TEXTURE_MATRIX:    memcpy(p, txStack[activeUnit][txTop[activeUnit]].m, sizeof(Mat)); break;
		default: UNSUPPORTED("glGetFloatv 0x%x", pname); p[0] = 0; break;
	}
}

void gl1_GetBooleanv(GLenum pname, GLboolean *p)
{
	switch (pname)
	{
		case GL_DEPTH_WRITEMASK: *p = depthMask; break;
		case GL_COLOR_WRITEMASK: memcpy(p, colorMask, sizeof colorMask); break;
		default: UNSUPPORTED("glGetBooleanv 0x%x", pname); *p = GL_FALSE; break;
	}
}

// ---- frame --------------------------------------------------------------------------------------------

void gl1_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { es_ClearColor(r, g, b, a); }

void gl1_Clear(GLbitfield mask)
{
	flush();
	esMasks();								// GL clears through the write masks
	es_Clear(mask);
}

void gl1_Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
	flush();
	es_Viewport(x, y, w, h);
	viewport[0] = x; viewport[1] = y; viewport[2] = w; viewport[3] = h;
}

void gl1_Finish(void) { flush(); es_Finish(); }

void gl1_SwapWindow(SDL_Window *window)
{
	flush();
	SDL_GL_SwapWindow(window);
}

// ---- textures -----------------------------------------------------------------------------------------

// npot: a non-power-of-two texture, which plain ES 2.0 samples only with GL_CLAMP_TO_EDGE
typedef struct { GLint wrapS, wrapT, minF, magF, npot; } TexParams;
static SDL_bool fullNPOT;
static TexParams *texParams;
static GLuint texParamsCap;

static TexParams *params(GLuint t)
{
	if (t >= texParamsCap)
	{
		GLuint n = SDL_max(t + 1, texParamsCap*2);
		texParams = SDL_realloc(texParams, n*sizeof *texParams);
		if (!texParams) fatal("out of memory");
		memset(texParams + texParamsCap, 0xff, (n - texParamsCap)*sizeof *texParams);
		texParamsCap = n;
	}
	return &texParams[t];
}

void gl1_GenTextures(GLsizei n, GLuint *t)
{
	es_GenTextures(n, t);
	for (int i = 0; i < n; i++) memset(params(t[i]), 0xff, sizeof(TexParams));
}

void gl1_DeleteTextures(GLsizei n, const GLuint *t)
{
	flush();
	es_DeleteTextures(n, t);
	for (int i = 0; i < n; i++)
	{
		if (!t[i]) continue;
		memset(params(t[i]), 0xff, sizeof(TexParams));
		for (int u = 0; u < 2; u++)
		{
			if (bound[u] == t[i]) bound[u] = 0;
			if (es.tex[u] == t[i]) es.tex[u] = 0;
		}
	}
}

void gl1_BindTexture(GLenum target, GLuint t) { SET(bound[activeUnit], t); }

void gl1_TexParameteri(GLenum target, GLenum pname, GLint param)
{
	GLuint t = bound[activeUnit];
	TexParams *p = params(t);
	GLint *slot;
	switch (pname)
	{
		case GL_TEXTURE_WRAP_S:     slot = &p->wrapS; break;
		case GL_TEXTURE_WRAP_T:     slot = &p->wrapT; break;
		case GL_TEXTURE_MIN_FILTER: slot = &p->minF; break;
		case GL_TEXTURE_MAG_FILTER: slot = &p->magF; break;
		default: UNSUPPORTED("texture parameter 0x%x", pname); return;
	}
	if (param == GL_CLAMP || (p->npot == 1 && !fullNPOT && slot != &p->minF && slot != &p->magF))
		param = GL_CLAMP_TO_EDGE;
	if (*slot == param) return;
	flush();
	*slot = param;
	esBind(es.unit, t);
	es_TexParameteri(GL_TEXTURE_2D, pname, param);
}

void gl1_TexParameterf(GLenum target, GLenum pname, GLfloat param) { gl1_TexParameteri(target, pname, (GLint) param); }

void gl1_PixelStorei(GLenum pname, GLint param) { if (pname == GL_UNPACK_ALIGNMENT) unpackAlign = param; }

// Converts the game's pixels to what ES 2.0 takes: RGBA or RGB bytes, or RGBA 5551. The internal
// format decides whether the alpha channel counts (GL_RGB textures sample with alpha 1).
void gl1_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                    GLenum format, GLenum type, const GLvoid *pixels)
{
	flush();
	SDL_bool alpha;
	switch (internal)
	{
		case GL_RGB: case GL_RGB8: case GL_RGB5: case 3: alpha = SDL_FALSE; break;
		case GL_RGBA: case GL_RGBA8: case GL_RGB5_A1: case GL_RGBA4: case 4: alpha = SDL_TRUE; break;
		default: UNSUPPORTED("internal format 0x%x", internal); alpha = SDL_TRUE; break;
	}
	SDL_bool npot = (w & (w-1)) || (h & (h-1));
	if (npot && !fullNPOT) UNSUPPORTED("repeat on non-power-of-two textures (first: %dx%d), clamped", w, h);

	int bpp = type == GL_UNSIGNED_SHORT_1_5_5_5_REV ? 2 : (format == GL_RGB ? 3 : 4);
	int srcPitch = (w*bpp + unpackAlign - 1) / unpackAlign * unpackAlign;
	const uint8_t *src = pixels;
	void *out = NULL;
	GLenum esFormat, esType;

	if (type == GL_UNSIGNED_SHORT_1_5_5_5_REV && format == GL_BGRA)
	{
		// A1 R5 G5 B5 (alpha in the top bit) -> R5 G5 B5 A1
		uint16_t *o = out = SDL_malloc((size_t) w*h*2);
		for (int y = 0; y < h; y++)
		{
			const uint16_t *s = (const uint16_t *) (src + (size_t) y*srcPitch);
			for (int x = 0; x < w; x++)
			{
				uint16_t p = s[x];
				*o++ = (uint16_t) ((p << 1) | (alpha ? (p >> 15) : 1));
			}
		}
		esFormat = GL_RGBA; esType = GL_UNSIGNED_SHORT_5_5_5_1;
	}
	else if (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8_REV)
	{
		int r = 0, g = 1, b = 2, a = 3;
		if (format == GL_BGRA) { r = 2; b = 0; }
		else if (format != GL_RGBA && format != GL_RGB) { UNSUPPORTED("pixel format 0x%x", format); return; }
		if (type == GL_UNSIGNED_INT_8_8_8_8_REV && format != GL_BGRA) { UNSUPPORTED("8888_REV with 0x%x", format); return; }
		SDL_bool srcAlpha = format != GL_RGB;
		int outBpp = alpha && srcAlpha ? 4 : 3;
		uint8_t *o = out = SDL_malloc((size_t) w*h*outBpp);
		for (int y = 0; y < h; y++)
		{
			const uint8_t *s = src + (size_t) y*srcPitch;
			for (int x = 0; x < w; x++, s += bpp)
			{
				*o++ = s[r]; *o++ = s[g]; *o++ = s[b];
				if (outBpp == 4) *o++ = s[a];
			}
		}
		esFormat = outBpp == 4 ? GL_RGBA : GL_RGB; esType = GL_UNSIGNED_BYTE;
	}
	else { UNSUPPORTED("pixel type 0x%x", type); return; }

	if (!out) fatal("out of memory");
	GLuint t = bound[activeUnit];
	esBind(es.unit, t);
	es_TexImage2D(GL_TEXTURE_2D, level, esFormat, w, h, 0, esFormat, esType, out);
	SDL_free(out);
	if (level == 0 && params(t)->npot != (GLint) npot)
	{
		params(t)->npot = npot;
		if (npot && !fullNPOT)				// REPEAT (the default) would sample black
		{
			params(t)->wrapS = params(t)->wrapT = GL_CLAMP_TO_EDGE;
			es_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			es_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
	}
}

// ---- pixels -------------------------------------------------------------------------------------------

void gl1_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{
	flush();
	if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) { es_ReadPixels(x, y, w, h, format, type, pixels); return; }
	UNSUPPORTED("glReadPixels 0x%x (use gl1_DepthLess for depth)", format);
}

static GLuint pickProg;
static GLint pickColor, pickZ;

static void pickQuad(const uint8_t rgb[3], float z)
{
	static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	float c[4] = { rgb[0]/255.0f, rgb[1]/255.0f, rgb[2]/255.0f, 1 };
	es_Uniform4fv(pickColor, 1, c);
	es_Uniform1f(pickZ, z);
	es_VertexAttribPointer(A_POS, 2, GL_FLOAT, GL_FALSE, 0, quad);
	es_DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// The lens flare's occlusion test: draws window pixel (x, y) at the given depth with GL_LEQUAL in a
// colour it does not have, reads it back (passed = not hidden), and puts the pixel's colour back.
SDL_bool gl1_DepthLess(int x, int y, float depth)
{
	flush();
	if (!pickProg)
	{
		pickProg = link(
			compile(GL_VERTEX_SHADER, "", "attribute vec4 aPos; uniform float uZ;\n"
				"void main() { gl_Position = vec4(aPos.xy, uZ, 1.0); }\n"),
			compile(GL_FRAGMENT_SHADER, "", "precision mediump float; uniform vec4 uColor;\n"
				"void main() { gl_FragColor = uColor; }\n"));
		pickColor = es_GetUniformLocation(pickProg, "uColor");
		pickZ = es_GetUniformLocation(pickProg, "uZ");
	}
	uint8_t saved[4], seen[4];
	es_ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, saved);
	static const uint8_t magenta[3] = { 255, 0, 255 }, green[3] = { 0, 255, 0 };
	const uint8_t *mark = memcmp(saved, magenta, 3) ? magenta : green;

	es_UseProgram(pickProg); es.prog = pickProg;
	for (int i = 1; i < 5; i++) esAttr(i, 0);
	esAttr(A_POS, 1);
	esCap(GL_BLEND, 0, &es.blend);
	esCap(GL_CULL_FACE, 0, &es.cull);
	if (memcmp(es.cmask, "\1\1\1\1", 4)) { es_ColorMask(1, 1, 1, 1); memset(es.cmask, 1, 4); }
	if (es.dmask) { es_DepthMask(GL_FALSE); es.dmask = 0; }
	es_Disable(GL_DITHER);
	es_Enable(GL_SCISSOR_TEST);
	es_Scissor(x, y, 1, 1);

	esCap(GL_DEPTH_TEST, 1, &es.depth);
	if (es.dfunc != GL_LEQUAL) { es_DepthFunc(GL_LEQUAL); es.dfunc = GL_LEQUAL; }
	pickQuad(mark, 2*depth - 1);
	es_ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, seen);
	SDL_bool visible = memcmp(seen, mark, 3) == 0;

	esCap(GL_DEPTH_TEST, 0, &es.depth);
	pickQuad(saved, 0);

	es_Disable(GL_SCISSOR_TEST);
	es_Enable(GL_DITHER);
	return !visible;
}

// ---- setup --------------------------------------------------------------------------------------------

void gl1_init(void)
{
	#define F(ret, name, args) if (!(es_##name = (ret (*) args) SDL_GL_GetProcAddress("gl" #name))) fatal("missing GLES function gl" #name);
	ESFUNCS
	#undef F

	SDL_Log("gl1es: %s, %s, %s", es_GetString(GL_VENDOR), es_GetString(GL_RENDERER), es_GetString(GL_VERSION));
	// an ES 3 context (what Mesa and the Mali drivers hand out for 2.0) has both in core
	const char *version = (const char *) es_GetString(GL_VERSION);
	SDL_bool es3 = version && SDL_strncmp(version, "OpenGL ES ", 10) == 0 && version[10] >= '3';
	uintIndices = es3 || SDL_GL_ExtensionSupported("GL_OES_element_index_uint");
	fullNPOT = es3 || SDL_GL_ExtensionSupported("GL_OES_texture_npot");
	SDL_Log("gl1es: 32-bit indices %s, full non-power-of-two textures %s", uintIndices ? "yes" : "no", fullNPOT ? "yes" : "no");

	identity(&mvStack[0]);
	identity(&pjStack[0]);
	identity(&txStack[0][0]);
	identity(&txStack[1][0]);

	// the ES context starts with the GL defaults
	memset(&es, 0, sizeof es);
	es.bsrc = GL_ONE; es.bdst = GL_ZERO; es.dfunc = GL_LESS; es.dmask = 1;
	es.cullMode = GL_BACK; es.frontFace = GL_CCW; memset(es.cmask, 1, 4); es.lineWidth = 1;
	es_PixelStorei(GL_UNPACK_ALIGNMENT, 1);		// the converted pixels are packed
	es_GetIntegerv(GL_VIEWPORT, viewport);
}
