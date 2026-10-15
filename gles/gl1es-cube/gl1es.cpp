// gl1es.cpp: the OpenGL 1.x subset Cube uses, on OpenGL ES 2.0.
//
// The GL drivers of the handhelds (Mali blobs in particular) pay a fixed cost per draw call, and
// Cube draws every world strip, every model strip, every particle and every text glyph on its
// own: 1,000-2,000 calls a frame. So nothing is drawn when the game asks. Every vertex is
// transformed to clip space on the CPU (with its fog factor) and appended to one array, and a
// batch is drawn only when a state that the GPU needs changes (texture, blending, depth, culling,
// overbright scale, lines). The data stays in client memory for the whole frame: no buffer the
// GPU still reads is ever rewritten.
//
// The batches of a frame are also kept for gl1_readdepth(): the game aims at the depth under the
// crosshair (glReadPixels of GL_DEPTH_COMPONENT, which ES cannot do), so the batches that wrote
// depth are drawn again into a 1x1 framebuffer placed on that pixel, with a shader that stores
// the fragment's depth as a colour, and that one pixel is read back.

#include "cube.h"

// ---- ES 2.0 entry points -------------------------------------------------------------------

#define ESFUNCS \
    F(void, ActiveTexture, (GLenum)) \
    F(void, AttachShader, (GLuint, GLuint)) \
    F(void, BindAttribLocation, (GLuint, GLuint, const char *)) \
    F(void, BindFramebuffer, (GLenum, GLuint)) \
    F(void, BindRenderbuffer, (GLenum, GLuint)) \
    F(void, BindTexture, (GLenum, GLuint)) \
    F(void, BlendFunc, (GLenum, GLenum)) \
    F(void, Clear, (GLbitfield)) \
    F(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    F(void, ClearDepthf, (GLfloat)) \
    F(void, CompileShader, (GLuint)) \
    F(GLuint, CreateProgram, (void)) \
    F(GLuint, CreateShader, (GLenum)) \
    F(void, CullFace, (GLenum)) \
    F(void, DepthFunc, (GLenum)) \
    F(void, DepthMask, (GLboolean)) \
    F(void, Disable, (GLenum)) \
    F(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    F(void, Enable, (GLenum)) \
    F(void, EnableVertexAttribArray, (GLuint)) \
    F(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    F(void, GenFramebuffers, (GLsizei, GLuint *)) \
    F(void, GenRenderbuffers, (GLsizei, GLuint *)) \
    F(void, GenerateMipmap, (GLenum)) \
    F(GLenum, CheckFramebufferStatus, (GLenum)) \
    F(void, GetIntegerv, (GLenum, GLint *)) \
    F(void, GetProgramiv, (GLuint, GLenum, GLint *)) \
    F(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *)) \
    F(void, GetShaderiv, (GLuint, GLenum, GLint *)) \
    F(const GLubyte *, GetString, (GLenum)) \
    F(GLint, GetUniformLocation, (GLuint, const char *)) \
    F(void, LineWidth, (GLfloat)) \
    F(void, LinkProgram, (GLuint)) \
    F(void, PixelStorei, (GLenum, GLint)) \
    F(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    F(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    F(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    F(void, ShaderSource, (GLuint, GLsizei, const char *const *, const GLint *)) \
    F(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    F(void, TexParameteri, (GLenum, GLenum, GLint)) \
    F(void, Uniform1f, (GLint, GLfloat)) \
    F(void, Uniform1i, (GLint, GLint)) \
    F(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    F(void, UseProgram, (GLuint)) \
    F(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    F(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

#define F(ret, name, args) static ret (*es_##name) args;
ESFUNCS
#undef F

#ifndef GL_RGBA8_OES
#define GL_RGBA8_OES 0x8058
#endif

// ---- state -----------------------------------------------------------------------------------

struct mat { float m[16]; };

static void identity(mat &a) { loopi(16) a.m[i] = (i%5==0) ? 1.0f : 0.0f; };

static void mul(mat &a, const mat &b)          // a = a * b, column-major as GL
{
    mat r;
    loopi(4) loopj(4)
        r.m[j*4+i] = a.m[i]*b.m[j*4] + a.m[4+i]*b.m[j*4+1] + a.m[8+i]*b.m[j*4+2] + a.m[12+i]*b.m[j*4+3];
    a = r;
};

static mat mvstack[32], pstack[4];
static int mvtop = 0, ptop = 0;
static GLenum matmode = GL_MODELVIEW;
static mat mvp;                                 // projection * modelview, for the vertices
static bool mvpdirty = true;

static mat &cur() { return matmode==GL_MODELVIEW ? mvstack[mvtop] : pstack[ptop]; };
static void changed() { mvpdirty = true; };

struct vert { float x, y, z, w, u, v; uchar r, g, b, a; float fog; };     // 32 bytes

// Everything the GPU needs to know besides the vertices; a new batch starts when it changes.
struct batchkey
{
    GLenum prim;                                // GL_TRIANGLES or GL_LINES
    GLuint tex;                                 // 0: texturing off
    float scale;                                // GL_RGB_SCALE of texture env combine (textured only)
    bool blend, depthtest, depthmask, cull;
    GLenum bsrc, bdst, depthfunc, cullface;
    float linewidth;
    float fogr, fogg, fogb;

    bool operator==(const batchkey &o) const
    {
        return prim==o.prim && tex==o.tex && scale==o.scale && blend==o.blend && depthtest==o.depthtest
            && depthmask==o.depthmask && cull==o.cull && (!blend || (bsrc==o.bsrc && bdst==o.bdst))
            && (!depthtest || depthfunc==o.depthfunc) && (!cull || cullface==o.cullface)
            && (prim!=GL_LINES || linewidth==o.linewidth) && fogr==o.fogr && fogg==o.fogg && fogb==o.fogb;
    };
};

struct batch { batchkey k; int start, count; };

static vert *fbuf = NULL;                       // this frame's vertices (since the last depth clear)
static int flen = 0, fcap = 0;
static vector<batch> *batches = NULL;           // this frame's drawn batches
static batchkey open;                           // key of the batch being filled
static int openstart = 0;
static bool isopen = false;

// GL 1.x state as the game set it
static bool texture2d = false, blend = false, depthtest = false, cullface_on = false, fog = false;
static GLenum bsrc = GL_ONE, bdst = GL_ZERO, depthfunc = GL_LESS, cullface = GL_BACK;
static bool depthmask = true, polyline = false, offsetline = false;
static float offsetunits = 0;
static GLuint boundtex = 0;
static GLint envmode = GL_MODULATE;
static float rgbscale = 1.0f, linewidth = 1.0f;
static float fogstart = 0, fogend = 1, fogcol[4] = { 0, 0, 0, 0 };
static uchar col[4] = { 255, 255, 255, 255 };
static float tcu = 0, tcv = 0;
static GLint viewport[4] = { 0, 0, 0, 0 };
static float clearcol[4] = { 0, 0, 0, 0 };

// client arrays (the world)
static const uchar *vptr = NULL, *cptr = NULL, *tptr = NULL;
static int vstride = 0, cstride = 0, tstride = 0;
static bool varr = false, carr = false, tarr = false;

// ---- the ES side -------------------------------------------------------------------------------

static GLuint progtex, progcol, progpick;
static GLint utex_scale, utex_fog, ucol_fog;
static GLuint pickfbo = 0;

struct esstate { GLuint prog, tex; float scale, fogr, fogg, fogb; int blend, depthtest, depthmask, cull; GLenum bsrc, bdst, depthfunc, cullface; float linewidth; };
static esstate es;

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = es_CreateShader(type);
    es_ShaderSource(s, 1, &src, NULL);
    es_CompileShader(s);
    GLint ok = 0;
    es_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok)
    {
        char log[1024] = "";
        es_GetShaderInfoLog(s, sizeof(log), NULL, log);
        printf("shader: %s\n", log);
        fatal("could not compile a shader");
    };
    return s;
};

static const char *vsrc =
    "attribute vec4 pos; attribute vec2 uv; attribute vec4 col; attribute float fog;\n"
    "varying vec2 vuv; varying vec4 vcol; varying float vfog;\n"
    "void main() { gl_Position = pos; vuv = uv; vcol = col; vfog = fog; }\n";

static GLuint makeprog(const char *fsrc, const char *vs = vsrc)
{
    GLuint p = es_CreateProgram();
    es_AttachShader(p, compile(GL_VERTEX_SHADER, vs));
    es_AttachShader(p, compile(GL_FRAGMENT_SHADER, fsrc));
    es_BindAttribLocation(p, 0, "pos");
    es_BindAttribLocation(p, 1, "uv");
    es_BindAttribLocation(p, 2, "col");
    es_BindAttribLocation(p, 3, "fog");
    es_LinkProgram(p);
    GLint ok = 0;
    es_GetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok) fatal("could not link a shader");
    return p;
};

void gl1_init()
{
    #define F(ret, name, args) if(!(es_##name = (ret (*) args)SDL_GL_GetProcAddress("gl" #name))) fatal("missing GLES function gl" #name);
    ESFUNCS
    #undef F

    printf("GL: %s, %s, %s\n", es_GetString(GL_VENDOR), es_GetString(GL_RENDERER), es_GetString(GL_VERSION));

    batches = new vector<batch>;
    identity(mvstack[0]);
    identity(pstack[0]);

    // GL 1.x: the fragment is texture * colour (GL_MODULATE; combine adds GL_RGB_SCALE), then fog
    progtex = makeprog(
        "precision mediump float;\n"
        "uniform sampler2D tex; uniform float scale; uniform vec3 fogcolour;\n"
        "varying vec2 vuv; varying vec4 vcol; varying float vfog;\n"
        "void main() { vec4 c = vcol*texture2D(tex, vuv); c.rgb = mix(fogcolour, min(c.rgb*scale, 1.0), vfog); gl_FragColor = c; }\n");
    progcol = makeprog(
        "precision mediump float;\n"
        "uniform vec3 fogcolour;\n"
        "varying vec2 vuv; varying vec4 vcol; varying float vfog;\n"
        "void main() { gl_FragColor = vec4(mix(fogcolour, vcol.rgb, vfog), vcol.a); }\n");
    // depth as 24 bits in r, g, b: white (the cleared pixel) is depth 1. Not from gl_FragCoord.z:
    // ES 2.0 declares it mediump, which drivers keep in 16-bit floats; z/w of the clip position is exact.
    progpick = makeprog(
        "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"
        "varying vec2 vzw;\n"
        "void main() { float d = floor((vzw.x/vzw.y*0.5+0.5)*16777215.0+0.5);\n"
        "  float r = floor(d/65536.0); d -= r*65536.0; float g = floor(d/256.0); float b = d-g*256.0;\n"
        "  gl_FragColor = vec4(r, g, b, 255.0)/255.0; }\n",
        "attribute vec4 pos; varying vec2 vzw;\n"
        "void main() { gl_Position = pos; vzw = pos.zw; }\n");
    es_UseProgram(progtex);
    es_Uniform1i(es_GetUniformLocation(progtex, "tex"), 0);
    utex_scale = es_GetUniformLocation(progtex, "scale");
    utex_fog = es_GetUniformLocation(progtex, "fogcolour");
    ucol_fog = es_GetUniformLocation(progcol, "fogcolour");
    loopi(4) es_EnableVertexAttribArray(i);

    // GL 1.x defaults that differ from ES
    memset(&es, 0, sizeof(es));
    es.prog = progtex;
    es.scale = -1;
    es.fogr = -1;
    es.bsrc = GL_ONE; es.bdst = GL_ZERO; es.depthfunc = GL_LESS; es.cullface = GL_BACK; es.linewidth = 1;
    es.depthmask = 1;
};

static void setcap(GLenum cap, int on, int &was) { if(on!=was) { if(on) es_Enable(cap); else es_Disable(cap); was = on; }; };

static void apply(const batchkey &k, GLuint prog)
{
    if(prog!=es.prog) { es_UseProgram(prog); es.prog = prog; es.scale = -1; es.fogr = -1; };
    if(prog==progtex)
    {
        if(k.tex!=es.tex) { es_BindTexture(GL_TEXTURE_2D, k.tex); es.tex = k.tex; };
        if(k.scale!=es.scale) { es_Uniform1f(utex_scale, k.scale); es.scale = k.scale; };
    };
    if(prog!=progpick && (k.fogr!=es.fogr || k.fogg!=es.fogg || k.fogb!=es.fogb))
    {
        es_Uniform3f(prog==progtex ? utex_fog : ucol_fog, k.fogr, k.fogg, k.fogb);
        es.fogr = k.fogr; es.fogg = k.fogg; es.fogb = k.fogb;
    };
    setcap(GL_BLEND, k.blend && prog!=progpick, es.blend);
    if(k.blend && (k.bsrc!=es.bsrc || k.bdst!=es.bdst)) { es_BlendFunc(k.bsrc, k.bdst); es.bsrc = k.bsrc; es.bdst = k.bdst; };
    setcap(GL_DEPTH_TEST, k.depthtest, es.depthtest);
    GLenum df = prog==progpick ? GL_LESS : k.depthfunc;
    if(k.depthtest && df!=es.depthfunc) { es_DepthFunc(df); es.depthfunc = df; };
    int dm = k.depthmask;
    if(dm!=es.depthmask) { es_DepthMask(dm ? GL_TRUE : GL_FALSE); es.depthmask = dm; };
    setcap(GL_CULL_FACE, k.cull, es.cull);
    if(k.cull && k.cullface!=es.cullface) { es_CullFace(k.cullface); es.cullface = k.cullface; };
    if(k.prim==GL_LINES && k.linewidth!=es.linewidth) { es_LineWidth(k.linewidth); es.linewidth = k.linewidth; };
};

static void draw(const batch &b, GLuint prog)
{
    apply(b.k, prog);
    const vert *v = fbuf+b.start;
    es_VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(vert), &v->x);
    es_VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vert), &v->u);
    es_VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vert), &v->r);
    es_VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(vert), &v->fog);
    es_DrawArrays(b.k.prim, 0, b.count);
};

void gl1_flush()
{
    if(!isopen) return;
    isopen = false;
    batch b = { open, openstart, flen-openstart };
    if(!b.count) return;
    draw(b, b.k.tex ? progtex : progcol);
    batches->add(b);
};

// ---- vertices -----------------------------------------------------------------------------------

static vert prim[64];                           // the glBegin block being built (Cube's are small)
static vert *primv = prim;
static int primn = 0, primmax = 64;
static GLenum primmode;

static void currentkey(batchkey &k, GLenum p)
{
    k.prim = p;
    k.tex = texture2d ? boundtex : 0;
    k.scale = envmode==GL_COMBINE_EXT ? rgbscale : 1.0f;
    k.blend = blend; k.bsrc = bsrc; k.bdst = bdst;
    k.depthtest = depthtest; k.depthfunc = depthfunc;
    k.depthmask = depthmask && depthtest;       // GL writes no depth while the test is off
    k.cull = cullface_on && p==GL_TRIANGLES; k.cullface = cullface;
    k.linewidth = linewidth;
    k.fogr = fogcol[0]; k.fogg = fogcol[1]; k.fogb = fogcol[2];
};

static vert *reserve(GLenum p, int n)           // room for n vertices in a batch with the current state
{
    batchkey k;
    currentkey(k, p);
    if(!isopen || !(k==open))
    {
        gl1_flush();
        open = k;
        openstart = flen;
        isopen = true;
    };
    if(flen+n>fcap)
    {
        fcap = max(fcap*2, flen+n+65536);
        fbuf = (vert *)realloc(fbuf, fcap*sizeof(vert));
        if(!fbuf) fatal("out of vertex memory");
    };
    flen += n;
    return fbuf+flen-n;
};

static inline void transform(vert &o, float x, float y, float z)
{
    if(mvpdirty) { mvp = pstack[ptop]; mul(mvp, mvstack[mvtop]); mvpdirty = false; };
    const float *m = mvp.m;
    o.x = m[0]*x + m[4]*y + m[8]*z + m[12];
    o.y = m[1]*x + m[5]*y + m[9]*z + m[13];
    o.z = m[2]*x + m[6]*y + m[10]*z + m[14];
    o.w = m[3]*x + m[7]*y + m[11]*z + m[15];
    if(fog)
    {
        const float *e = mvstack[mvtop].m;
        float c = fabs(e[2]*x + e[6]*y + e[10]*z + e[14]);
        float f = fogend==fogstart ? 1.0f : (fogend-c)/(fogend-fogstart);
        o.fog = f<0 ? 0 : (f>1 ? 1 : f);
    }
    else o.fog = 1;
};

// Emits the primitive in p[0..n) as triangles (or as its outline in glPolygonMode GL_LINE).
static void emit(GLenum mode, const vert *p, int n)
{
    if(mode==GL_LINES || mode==GL_LINE_LOOP || mode==GL_LINE_STRIP)
    {
        int lines = mode==GL_LINES ? n/2 : (mode==GL_LINE_LOOP ? n : n-1);
        if(lines<=0) return;
        vert *o = reserve(GL_LINES, lines*2);
        loopi(lines)
        {
            if(mode==GL_LINES) { *o++ = p[2*i]; *o++ = p[2*i+1]; }
            else { *o++ = p[i]; *o++ = p[(i+1)%n]; };
        };
        if(polyline && offsetline)              // GL_POLYGON_OFFSET_LINE, which ES lacks: in steps of
        {                                       // the 16-bit depth buffer, times 8 for the slope term
            float dz = offsetunits*8*2.0f/65536;
            for(o -= lines*2; lines--; o += 2) { o[0].z += dz*o[0].w; o[1].z += dz*o[1].w; };
        };
        return;
    };
    if(polyline)                                // outline of each polygon
    {
        int sides = (mode==GL_QUADS) ? 4 : (mode==GL_POLYGON ? n : 3);
        if(mode==GL_QUADS || mode==GL_POLYGON)
            for(int s = 0; s+sides<=n; s += sides) emit(GL_LINE_LOOP, p+s, sides);
        else loopi(n-2)                         // strips and fans: every triangle's edges
        {
            vert t[3] = { mode==GL_TRIANGLE_FAN ? p[0] : p[i], p[i+1], p[i+2] };
            emit(GL_LINE_LOOP, t, 3);
        };
        return;
    };
    int tris;
    switch(mode)
    {
        case GL_TRIANGLES: tris = n/3; break;
        case GL_QUADS: tris = n/4*2; break;
        case GL_QUAD_STRIP: tris = n>=4 ? (n/2-1)*2 : 0; break;
        default: tris = n>=3 ? n-2 : 0; break;      // strips, fans, polygons
    };
    if(tris<=0) return;
    vert *o = reserve(GL_TRIANGLES, tris*3);
    switch(mode)
    {
        case GL_TRIANGLES: loopi(tris*3) *o++ = p[i]; break;
        case GL_QUADS:
            for(int i = 0; i+4<=n; i += 4) { *o++ = p[i]; *o++ = p[i+1]; *o++ = p[i+2]; *o++ = p[i]; *o++ = p[i+2]; *o++ = p[i+3]; };
            break;
        case GL_QUAD_STRIP:
            for(int i = 0; i+4<=n; i += 2) { *o++ = p[i]; *o++ = p[i+1]; *o++ = p[i+3]; *o++ = p[i]; *o++ = p[i+3]; *o++ = p[i+2]; };
            break;
        case GL_TRIANGLE_STRIP:
            loopi(tris)
            {
                if(i&1) { *o++ = p[i+1]; *o++ = p[i]; }
                else { *o++ = p[i]; *o++ = p[i+1]; };
                *o++ = p[i+2];
            };
            break;
        default:                                // GL_TRIANGLE_FAN, GL_POLYGON
            loopi(tris) { *o++ = p[0]; *o++ = p[i+1]; *o++ = p[i+2]; };
            break;
    };
};

void gl1_Begin(GLenum mode) { primmode = mode; primn = 0; };
void gl1_End() { emit(primmode, primv, primn); };

static inline void vertex(float x, float y, float z)
{
    if(primn==primmax)
    {
        vert *n = new vert[primmax*2];
        memcpy(n, primv, primmax*sizeof(vert));
        if(primv!=prim) delete[] primv;
        primv = n;
        primmax *= 2;
    };
    vert &v = primv[primn++];
    transform(v, x, y, z);
    v.u = tcu; v.v = tcv;
    v.r = col[0]; v.g = col[1]; v.b = col[2]; v.a = col[3];
};

void gl1_Vertex2i(GLint x, GLint y) { vertex((float)x, (float)y, 0); };
void gl1_Vertex2f(GLfloat x, GLfloat y) { vertex(x, y, 0); };
void gl1_Vertex3f(GLfloat x, GLfloat y, GLfloat z) { vertex(x, y, z); };
void gl1_Vertex3d(GLdouble x, GLdouble y, GLdouble z) { vertex((float)x, (float)y, (float)z); };
void gl1_TexCoord2f(GLfloat s, GLfloat t) { tcu = s; tcv = t; };
void gl1_TexCoord2d(GLdouble s, GLdouble t) { tcu = (float)s; tcv = (float)t; };

static uchar tobyte(double c) { return c<=0 ? 0 : (c>=1 ? 255 : (uchar)(c*255+0.5)); };
void gl1_Color3ub(GLubyte r, GLubyte g, GLubyte b) { col[0] = r; col[1] = g; col[2] = b; col[3] = 255; };
void gl1_Color3f(GLfloat r, GLfloat g, GLfloat b) { gl1_Color4f(r, g, b, 1); };
void gl1_Color3d(GLdouble r, GLdouble g, GLdouble b) { gl1_Color4f((float)r, (float)g, (float)b, 1); };
void gl1_Color3fv(const GLfloat *v) { gl1_Color4f(v[0], v[1], v[2], 1); };
void gl1_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { col[0] = tobyte(r); col[1] = tobyte(g); col[2] = tobyte(b); col[3] = tobyte(a); };

// the world: triangle strips from the game's vertex array
void gl1_EnableClientState(GLenum a)
{
    if(a==GL_VERTEX_ARRAY) varr = true;
    else if(a==GL_COLOR_ARRAY) carr = true;
    else if(a==GL_TEXTURE_COORD_ARRAY) tarr = true;
};
void gl1_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { vptr = (const uchar *)p; vstride = stride; };
void gl1_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { cptr = (const uchar *)p; cstride = stride; };
void gl1_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *p) { tptr = (const uchar *)p; tstride = stride; };

void gl1_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    // Cube: 3 floats, 4 unsigned bytes, 2 floats (struct vertex); fixed functions below assume it
    if(!varr || !vptr) return;
    primmode = mode;
    primn = 0;
    uchar save[4] = { col[0], col[1], col[2], col[3] };
    float su = tcu, sv = tcv;
    for(int i = first; i<first+count; i++)
    {
        const float *p = (const float *)(vptr+i*vstride);
        if(carr) { const uchar *c = cptr+i*cstride; col[0] = c[0]; col[1] = c[1]; col[2] = c[2]; col[3] = c[3]; };
        if(tarr) { const float *t = (const float *)(tptr+i*tstride); tcu = t[0]; tcv = t[1]; };
        vertex(p[0], p[1], p[2]);
    };
    emit(mode, primv, primn);
    memcpy(col, save, 4);                       // arrays leave the current colour alone
    tcu = su; tcv = sv;
};

// ---- state calls ----------------------------------------------------------------------------------

static void cap(GLenum c, bool on)
{
    switch(c)
    {
        case GL_TEXTURE_2D: texture2d = on; break;
        case GL_BLEND: blend = on; break;
        case GL_DEPTH_TEST: depthtest = on; break;
        case GL_CULL_FACE: cullface_on = on; break;
        case GL_FOG: fog = on; break;
        case GL_POLYGON_OFFSET_LINE: offsetline = on; break;
        default: break;                         // line smooth: nothing to do on ES
    };
};
void gl1_Enable(GLenum c) { cap(c, true); };
void gl1_Disable(GLenum c) { cap(c, false); };
void gl1_DepthMask(GLboolean f) { depthmask = f!=0; };
void gl1_DepthFunc(GLenum f) { depthfunc = f; };
void gl1_BlendFunc(GLenum s, GLenum d) { bsrc = s; bdst = d; };
void gl1_CullFace(GLenum m) { cullface = m; };
void gl1_PolygonMode(GLenum face, GLenum mode) { polyline = mode==GL_LINE; };
void gl1_LineWidth(GLfloat w) { linewidth = w; };
void gl1_PolygonOffset(GLfloat factor, GLfloat units) { offsetunits = units; };
void gl1_ShadeModel(GLenum mode) {};
void gl1_Hint(GLenum target, GLenum mode) {};
void gl1_ClearDepth(GLclampd d) { es_ClearDepthf((GLfloat)d); };
void gl1_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { clearcol[0] = r; clearcol[1] = g; clearcol[2] = b; clearcol[3] = a; es_ClearColor(r, g, b, a); };

void gl1_Clear(GLbitfield mask)
{
    gl1_flush();
    if(mask&GL_DEPTH_BUFFER_BIT) { flen = 0; batches->setsize(0); };            // a new frame
    if(mask&GL_DEPTH_BUFFER_BIT && !es.depthmask) { es_DepthMask(GL_TRUE); es.depthmask = 1; };
    es_Clear(mask);
};

void gl1_Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    gl1_flush();
    es_Viewport(x, y, w, h);
    viewport[0] = x; viewport[1] = y; viewport[2] = w; viewport[3] = h;
};

void gl1_TexEnvi(GLenum target, GLenum pname, GLint param) { if(pname==GL_TEXTURE_ENV_MODE) envmode = param; };
void gl1_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    if(pname==GL_RGB_SCALE_EXT) rgbscale = param;
    else if(pname==GL_TEXTURE_ENV_MODE) envmode = (GLint)param;
};
void gl1_Fogi(GLenum pname, GLint param) { gl1_Fogf(pname, (GLfloat)param); };
void gl1_Fogf(GLenum pname, GLfloat param)
{
    if(pname==GL_FOG_START) fogstart = param;
    else if(pname==GL_FOG_END) fogend = param;
};
void gl1_Fogfv(GLenum pname, const GLfloat *p) { if(pname==GL_FOG_COLOR) loopi(4) fogcol[i] = p[i]; };

// ---- matrices -----------------------------------------------------------------------------------

void gl1_MatrixMode(GLenum mode) { matmode = mode; };
void gl1_LoadIdentity() { identity(cur()); changed(); };
void gl1_PushMatrix()
{
    if(matmode==GL_MODELVIEW) { if(mvtop==31) fatal("modelview stack overflow"); mvstack[mvtop+1] = mvstack[mvtop]; mvtop++; }
    else { if(ptop==3) fatal("projection stack overflow"); pstack[ptop+1] = pstack[ptop]; ptop++; };
};
void gl1_PopMatrix()
{
    if(matmode==GL_MODELVIEW) { if(mvtop) mvtop--; }
    else if(ptop) ptop--;
    changed();
};

static void multiply(const mat &b) { mul(cur(), b); changed(); };

void gl1_Rotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    double l = sqrt(x*x+y*y+z*z);
    if(l==0) return;
    x /= l; y /= l; z /= l;
    double a = angle*PI/180, c = cos(a), s = sin(a), t = 1-c;
    mat r;
    identity(r);
    r.m[0] = (float)(x*x*t+c);   r.m[4] = (float)(x*y*t-z*s); r.m[8] = (float)(x*z*t+y*s);
    r.m[1] = (float)(y*x*t+z*s); r.m[5] = (float)(y*y*t+c);   r.m[9] = (float)(y*z*t-x*s);
    r.m[2] = (float)(x*z*t-y*s); r.m[6] = (float)(y*z*t+x*s); r.m[10] = (float)(z*z*t+c);
    multiply(r);
};
void gl1_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { gl1_Rotated(angle, x, y, z); };
void gl1_Translated(GLdouble x, GLdouble y, GLdouble z) { mat t; identity(t); t.m[12] = (float)x; t.m[13] = (float)y; t.m[14] = (float)z; multiply(t); };
void gl1_Translatef(GLfloat x, GLfloat y, GLfloat z) { gl1_Translated(x, y, z); };
void gl1_Scalef(GLfloat x, GLfloat y, GLfloat z) { mat t; identity(t); t.m[0] = x; t.m[5] = y; t.m[10] = z; multiply(t); };

void gl1_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    mat o;
    identity(o);
    o.m[0] = (float)(2/(r-l)); o.m[5] = (float)(2/(t-b)); o.m[10] = (float)(-2/(f-n));
    o.m[12] = (float)(-(r+l)/(r-l)); o.m[13] = (float)(-(t+b)/(t-b)); o.m[14] = (float)(-(f+n)/(f-n));
    multiply(o);
};

void gl1u_Perspective(GLdouble fovy, GLdouble aspect, GLdouble zn, GLdouble zf)
{
    double f = 1/tan(fovy*PI/360);
    mat p;
    loopi(16) p.m[i] = 0;
    p.m[0] = (float)(f/aspect); p.m[5] = (float)f;
    p.m[10] = (float)((zf+zn)/(zn-zf)); p.m[11] = -1; p.m[14] = (float)(2*zf*zn/(zn-zf));
    multiply(p);
};

void gl1_LoadMatrixd(const GLdouble *m) { loopi(16) cur().m[i] = (float)m[i]; changed(); };

void gl1_GetDoublev(GLenum pname, GLdouble *p)
{
    const mat &m = pname==GL_PROJECTION_MATRIX ? pstack[ptop] : mvstack[mvtop];
    loopi(16) p[i] = m.m[i];
};

void gl1_GetIntegerv(GLenum pname, GLint *p)
{
    if(pname==GL_VIEWPORT) loopi(4) p[i] = viewport[i];
    else es_GetIntegerv(pname, p);
};

const GLubyte *gl1_GetString(GLenum name)
{
    // overbright (texture env combine with GL_RGB_SCALE) is done in the fragment shader
    if(name==GL_EXTENSIONS) return (const GLubyte *)"GL_EXT_texture_env_combine";
    return es_GetString(name);
};

GLint gl1u_UnProject(GLdouble winx, GLdouble winy, GLdouble winz, const GLdouble *model, const GLdouble *proj,
                     const GLint *view, GLdouble *objx, GLdouble *objy, GLdouble *objz)
{
    double a[16], inv[16];
    loopi(4) loopj(4) a[j*4+i] = proj[i]*model[j*4] + proj[4+i]*model[j*4+1] + proj[8+i]*model[j*4+2] + proj[12+i]*model[j*4+3];
    // inverse by cofactors (as Mesa GLU)
    inv[0] = a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10];
    inv[4] = -a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
    inv[8] = a[4]*a[9]*a[15]-a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14]+a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];
    inv[1] = -a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10];
    inv[5] = a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
    inv[9] = -a[0]*a[9]*a[15]+a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9];
    inv[13] = a[0]*a[9]*a[14]-a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];
    inv[2] = a[1]*a[6]*a[15]-a[1]*a[7]*a[14]-a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7]-a[13]*a[3]*a[6];
    inv[6] = -a[0]*a[6]*a[15]+a[0]*a[7]*a[14]+a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7]+a[12]*a[3]*a[6];
    inv[10] = a[0]*a[5]*a[15]-a[0]*a[7]*a[13]-a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7]-a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14]+a[0]*a[6]*a[13]+a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6]+a[12]*a[2]*a[5];
    inv[3] = -a[1]*a[6]*a[11]+a[1]*a[7]*a[10]+a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]+a[9]*a[3]*a[6];
    inv[7] = a[0]*a[6]*a[11]-a[0]*a[7]*a[10]-a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]-a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11]+a[0]*a[7]*a[9]+a[4]*a[1]*a[11]-a[4]*a[3]*a[9]-a[8]*a[1]*a[7]+a[8]*a[3]*a[5];
    inv[15] = a[0]*a[5]*a[10]-a[0]*a[6]*a[9]-a[4]*a[1]*a[10]+a[4]*a[2]*a[9]+a[8]*a[1]*a[6]-a[8]*a[2]*a[5];
    double det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    if(det==0) return GL_FALSE;
    double in[4] = { (winx-view[0])*2/view[2]-1, (winy-view[1])*2/view[3]-1, 2*winz-1, 1 }, out[4];
    loopi(4) out[i] = (inv[i]*in[0] + inv[4+i]*in[1] + inv[8+i]*in[2] + inv[12+i]*in[3])/det;
    if(out[3]==0) return GL_FALSE;
    *objx = out[0]/out[3]; *objy = out[1]/out[3]; *objz = out[2]/out[3];
    return GL_TRUE;
};

// ---- textures and pixels ----------------------------------------------------------------------------

void gl1_BindTexture(GLenum target, GLuint t) { boundtex = t; es_BindTexture(GL_TEXTURE_2D, t); es.tex = t; };
void gl1_TexParameteri(GLenum target, GLenum pname, GLint param) { es_TexParameteri(target, pname, param); };
void gl1_PixelStorei(GLenum pname, GLint param) { es_PixelStorei(pname, param); };

GLint gl1u_Build2DMipmaps(GLenum target, GLint internal, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data)
{
    es_TexImage2D(target, 0, format, w, h, 0, format, type, data);      // ES: internal format = format
    es_GenerateMipmap(target);
    return 0;
};

GLint gl1u_ScaleImage(GLenum format, GLint wi, GLint hi, GLenum typei, const void *in, GLint wo, GLint ho, GLenum typeo, void *out)
{
    const uchar *s = (const uchar *)in;          // RGB bytes, box filter over the source pixels
    uchar *d = (uchar *)out;
    loop(y, ho) loop(x, wo)
    {
        int x0 = x*wi/wo, x1 = max(x0+1, (x+1)*wi/wo), y0 = y*hi/ho, y1 = max(y0+1, (y+1)*hi/ho);
        loop(c, 3)
        {
            int sum = 0;
            for(int yy = y0; yy<y1; yy++) for(int xx = x0; xx<x1; xx++) sum += s[(yy*wi+xx)*3+c];
            d[(y*wo+x)*3+c] = sum/((x1-x0)*(y1-y0));
        };
    };
    return 0;
};

void gl1_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{
    gl1_flush();
    if(format!=GL_RGB) return;                  // screenshots; depth goes through gl1_readdepth
    uchar *rgba = new uchar[w*h*4];
    es_ReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    int stride = (w*3+3)&~3;                    // GL_PACK_ALIGNMENT 4
    loop(j, h) loopi(w) loop(c, 3) ((uchar *)pixels)[j*stride+i*3+c] = rgba[(j*w+i)*4+c];
    delete[] rgba;
};

float gl1_readdepth(int x, int y)
{
    gl1_flush();
    if(!pickfbo)
    {
        GLuint rb[2];
        es_GenFramebuffers(1, &pickfbo);
        es_GenRenderbuffers(2, rb);
        es_BindRenderbuffer(GL_RENDERBUFFER, rb[0]);
        es_RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, 1, 1);
        es_BindRenderbuffer(GL_RENDERBUFFER, rb[1]);
        es_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 1, 1);
        es_BindFramebuffer(GL_FRAMEBUFFER, pickfbo);
        es_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
        es_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
        if(es_CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) fatal("depth pick framebuffer incomplete");
    }
    else es_BindFramebuffer(GL_FRAMEBUFFER, pickfbo);

    // window pixel (x, y) of the full viewport lands on the framebuffer's only pixel
    es_Viewport(viewport[0]-x, viewport[1]-y, viewport[2], viewport[3]);
    int dm = es.depthmask;
    if(!dm) es_DepthMask(GL_TRUE);
    es.depthmask = 1;
    es_ClearColor(1, 1, 1, 1);
    es_Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    es_ClearColor(clearcol[0], clearcol[1], clearcol[2], clearcol[3]);
    es_Disable(GL_DITHER);
    loopv(*batches)
    {
        batch &b = (*batches)[i];
        if(b.k.prim==GL_TRIANGLES && b.k.depthmask) draw(b, progpick);
    };
    uchar p[4] = { 255, 255, 255, 255 };
    es_ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    es_Enable(GL_DITHER);
    es_BindFramebuffer(GL_FRAMEBUFFER, 0);
    es_Viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    return (p[0]*65536 + p[1]*256 + p[2])/16777215.0f;
};
