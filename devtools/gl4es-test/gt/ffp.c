// Fixed-function pipeline, which gl4es emulates with generated shaders (fpe.c) and its own vertex
// array / immediate-mode / display-list machinery.
//   draw/<prim>/<path>     every primitive type through every submission path
//   ffp/<seed>             random fixed-function state; each feature it picked is logged ("feat ...")
//                          so gtrun.py can tell which features the failing seeds have in common
#include "gt.h"

typedef struct { GLenum e; const char *n; } en_t;
#define E(x) { GL_##x, #x }
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

// ---------- geometry ----------
typedef struct { float x, y, z; float r, g, b, a; float s, t; float nx, ny, nz; float sr, sg, sb; float fog; } vtx_t;
// vertices for a primitive type: a small ring/strip/grid inside (x0,y0)-(x0+s,y0+s), deterministic colours
static int make_prim(GLenum prim, float x0, float y0, float s, vtx_t *v, int salt) {
    int n = 0;
    float cx = x0 + s / 2, cy = y0 + s / 2;
    switch (prim) {
    case GL_POINTS: for (int i = 0; i < 16; i++) { v[n].x = x0 + (i % 4) * s / 4 + 4; v[n].y = y0 + (i / 4) * s / 4 + 4; n++; } break;
    case GL_LINES: case GL_LINE_STRIP: case GL_LINE_LOOP:
        for (int i = 0; i < 8; i++) { float a = i * 0.785f; v[n].x = cx + cosf(a) * s * 0.45f; v[n].y = cy + sinf(a * 1.5f) * s * 0.45f; n++; } break;
    case GL_TRIANGLES: for (int i = 0; i < 9; i++) { float a = i * 0.7f; v[n].x = cx + cosf(a) * s * (0.2f + 0.25f * (i % 3)); v[n].y = cy + sinf(a) * s * (0.45f - 0.1f * (i % 3)); n++; } break;
    case GL_TRIANGLE_STRIP: case GL_QUAD_STRIP: for (int i = 0; i < 10; i++) { v[n].x = x0 + (i / 2) * s / 4.5f; v[n].y = y0 + (i & 1 ? s * 0.9f : s * 0.1f) + (i / 2) * 2; n++; } break;
    case GL_TRIANGLE_FAN: case GL_POLYGON: v[n].x = cx; v[n].y = cy; n++;
        for (int i = 0; i < 7; i++) { float a = i * 0.9f; v[n].x = cx + cosf(a) * s * 0.45f; v[n].y = cy + sinf(a) * s * 0.45f; n++; }
        if (prim == GL_POLYGON) { n--; memmove(v, v + 1, n * sizeof *v); }   // convex ring without the centre
        break;
    case GL_QUADS: for (int q = 0; q < 3; q++) { float bx = x0 + q * s / 3, by = y0 + q * 6;
            float px[4] = { bx + 2, bx + s / 3 - 2, bx + s / 3 - 4, bx + 3 }, py[4] = { by + 2, by + 4, by + s * 0.7f, by + s * 0.6f };
            for (int k = 0; k < 4; k++) { v[n].x = px[k]; v[n].y = py[k]; n++; } } break;
    }
    for (int i = 0; i < n; i++) {
        float u = (v[i].x - x0) / s, w = (v[i].y - y0) / s;
        v[i].z = (u - w) * 0.5f; v[i].r = u; v[i].g = w; v[i].b = (float)((i * 7 + salt) % 5) / 4; v[i].a = 0.4f + 0.6f * u;
        v[i].s = u * 2 - 0.25f; v[i].t = w * 1.5f; v[i].nx = u - 0.5f; v[i].ny = w - 0.5f; v[i].nz = 0.8f;
        v[i].sr = w; v[i].sg = 0.3f; v[i].sb = 1 - u; v[i].fog = u + w;
    }
    return n;
}
// submission paths
enum { P_IMM, P_ARRAYS, P_ELEM_UB, P_ELEM_US, P_ELEM_UI, P_RANGE, P_VBO, P_VBO_ELEM, P_LIST, P_LIST_ARRAYS, P_MULTI, P_ARRAYELEMENT, P_NPATH };
static const char *PATHN[] = { "immediate", "drawarrays", "elements-ubyte", "elements-ushort", "elements-uint", "rangeelements",
                               "vbo", "vbo-elements", "displaylist", "displaylist-arrays", "multidrawarrays", "arrayelement" };
static void vtx_imm(const vtx_t *v) {
    glColor4f(v->r, v->g, v->b, v->a); glNormal3f(v->nx, v->ny, v->nz); glTexCoord2f(v->s, v->t);
    p_glMultiTexCoord2f(GL_TEXTURE1, v->t, v->s); p_glSecondaryColor3f(v->sr, v->sg, v->sb); p_glFogCoordf(v->fog);
    glVertex3f(v->x, v->y, v->z);
}
static void arrays_on(const vtx_t *v, GLuint vbo) {
    const char *base = vbo ? NULL : (const char *)v;
    size_t st = sizeof(vtx_t);
#define OFF(f) (base + offsetof(vtx_t, f))
    glEnableClientState(GL_VERTEX_ARRAY); glVertexPointer(3, GL_FLOAT, st, OFF(x));
    glEnableClientState(GL_COLOR_ARRAY); glColorPointer(4, GL_FLOAT, st, OFF(r));
    glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, st, OFF(nx));
    p_glClientActiveTexture(GL_TEXTURE0); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, st, OFF(s));
    p_glClientActiveTexture(GL_TEXTURE1); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, st, OFF(s));
    p_glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_SECONDARY_COLOR_ARRAY); p_glSecondaryColorPointer(3, GL_FLOAT, st, OFF(sr));
    glEnableClientState(GL_FOG_COORD_ARRAY); p_glFogCoordPointer(GL_FLOAT, st, OFF(fog));
#undef OFF
}
static void arrays_off(void) {
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_NORMAL_ARRAY);
    p_glClientActiveTexture(GL_TEXTURE1); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    p_glClientActiveTexture(GL_TEXTURE0); glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_SECONDARY_COLOR_ARRAY); glDisableClientState(GL_FOG_COORD_ARRAY);
}
// draws v[0..n) with prim through path; element paths use a scrambled copy + index list giving the same order
void gt_submit(GLenum prim, const vtx_t *v, int n, int path) {
    vtx_t sc[64]; uint32_t idx[64]; int perm[64];
    for (int i = 0; i < n; i++) perm[i] = (i * 7 + 3) % n;   // n is never a multiple of 7
    for (int i = 0; i < n; i++) { sc[perm[i]] = v[i]; idx[i] = perm[i]; }
    uint8_t ib[64]; uint16_t is[64]; for (int i = 0; i < n; i++) { ib[i] = (uint8_t)idx[i]; is[i] = (uint16_t)idx[i]; }
    GLuint vbo = 0, ibo = 0, list = 0;
    switch (path) {
    case P_IMM: glBegin(prim); for (int i = 0; i < n; i++) vtx_imm(&v[i]); glEnd(); break;
    case P_ARRAYS: arrays_on(v, 0); glDrawArrays(prim, 0, n); arrays_off(); break;
    case P_ELEM_UB: arrays_on(sc, 0); glDrawElements(prim, n, GL_UNSIGNED_BYTE, ib); arrays_off(); break;
    case P_ELEM_US: arrays_on(sc, 0); glDrawElements(prim, n, GL_UNSIGNED_SHORT, is); arrays_off(); break;
    case P_ELEM_UI: arrays_on(sc, 0); glDrawElements(prim, n, GL_UNSIGNED_INT, idx); arrays_off(); break;
    case P_RANGE: arrays_on(sc, 0); p_glDrawRangeElements(prim, 0, n - 1, n, GL_UNSIGNED_SHORT, is); arrays_off(); break;
    case P_VBO: case P_VBO_ELEM:
        p_glGenBuffers(1, &vbo); p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
        p_glBufferData(GL_ARRAY_BUFFER, n * sizeof(vtx_t), path == P_VBO ? v : sc, GL_STATIC_DRAW);
        arrays_on(NULL, vbo);
        if (path == P_VBO) glDrawArrays(prim, 0, n);
        else { p_glGenBuffers(1, &ibo); p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
               p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, n * 2, is, GL_STATIC_DRAW);
               glDrawElements(prim, n, GL_UNSIGNED_SHORT, NULL); p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); p_glDeleteBuffers(1, &ibo); }
        arrays_off(); p_glBindBuffer(GL_ARRAY_BUFFER, 0); p_glDeleteBuffers(1, &vbo); break;
    case P_LIST: list = glGenLists(1); glNewList(list, GL_COMPILE); glBegin(prim); for (int i = 0; i < n; i++) vtx_imm(&v[i]); glEnd(); glEndList();
        glCallList(list); glDeleteLists(list, 1); break;
    case P_LIST_ARRAYS: arrays_on(v, 0); list = glGenLists(1); glNewList(list, GL_COMPILE); glDrawArrays(prim, 0, n); glEndList(); arrays_off();
        glCallList(list); glDeleteLists(list, 1); break;
    case P_MULTI: { GLint first[2] = { 0, 0 }; GLsizei cnt[2] = { n, 0 }; arrays_on(v, 0); p_glMultiDrawArrays(prim, first, cnt, 2); arrays_off(); break; }
    case P_ARRAYELEMENT: arrays_on(sc, 0); glBegin(prim); for (int i = 0; i < n; i++) glArrayElement(idx[i]); glEnd(); arrays_off(); break;
    }
}

static const en_t PRIMS[] = { E(POINTS), E(LINES), E(LINE_STRIP), E(LINE_LOOP), E(TRIANGLES), E(TRIANGLE_STRIP), E(TRIANGLE_FAN), E(QUADS), E(QUAD_STRIP), E(POLYGON) };

// draw/<prim>/<path>/<shade>: smooth and flat shading (flat = provoking vertex rules, last vertex for quads/strips)
static void t_draw(gt_test *t) {
    vtx_t v[64]; int n = make_prim(t->a, 16, 16, 200, v, 1);
    glShadeModel(t->c ? GL_FLAT : GL_SMOOTH);
    glPointSize(5); glLineWidth(3);
    gt_submit(t->a, v, n, t->b);
    gt_errcheck(t, "draw");
    // GL lets flat-shaded quads/polygons take any vertex's colour? no: last vertex of each quad, first of a polygon
    gt_img(t, 0, 0, 256, 256, 2, 0.002);
}

// ---------- fixed-function fuzz ----------
static void feat(gt_test *t, const char *f) { gt_info(t, "feat %s", f); }
static GLuint fuzz_tex(rng_t *r, GLenum ifmt) {
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    uint8_t px[16 * 16 * 4]; float ph[4]; for (int c = 0; c < 4; c++) ph[c] = rng_f(r, 0, 6.28f);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) for (int c = 0; c < 4; c++)
        px[4 * (y * 16 + x) + c] = (uint8_t)(127.5f + 127.f * sinf(ph[c] + x * 0.3f * (c + 1) + y * 0.23f * (4 - c)));
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return tex;
}
static void fuzz_texunit(gt_test *t, rng_t *r, int unit, GLuint *tex) {
    char f[160];
    static const en_t IF[] = { E(RGBA), E(RGB), E(ALPHA), E(LUMINANCE), E(LUMINANCE_ALPHA), E(INTENSITY) };
    static const en_t MODE[] = { E(MODULATE), E(REPLACE), E(DECAL), E(BLEND), E(ADD), E(COMBINE), E(COMBINE), E(COMBINE) };
    p_glActiveTexture(GL_TEXTURE0 + unit);
    const en_t *ifm = &PICK(r, IF); *tex = fuzz_tex(r, ifm->e);
    glEnable(GL_TEXTURE_2D);
    const en_t *m = &PICK(r, MODE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, m->e);
    float ec[4] = { rng_f(r, 0, 1), rng_f(r, 0, 1), rng_f(r, 0, 1), rng_f(r, 0, 1) }; glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, ec);
    snprintf(f, sizeof f, "tex%d-%s-%s", unit, ifm->n, m->n); feat(t, f);
    if (m->e == GL_COMBINE) {
        static const en_t CRGB[] = { E(REPLACE), E(MODULATE), E(ADD), E(ADD_SIGNED), E(INTERPOLATE), E(SUBTRACT), E(DOT3_RGB), E(DOT3_RGBA) };
        static const en_t CA[] = { E(REPLACE), E(MODULATE), E(ADD), E(ADD_SIGNED), E(INTERPOLATE), E(SUBTRACT) };
        static const en_t SRC[] = { E(TEXTURE), E(CONSTANT), E(PRIMARY_COLOR), E(PREVIOUS), E(TEXTURE0), E(TEXTURE1) };
        static const en_t OPC[] = { E(SRC_COLOR), E(ONE_MINUS_SRC_COLOR), E(SRC_ALPHA), E(ONE_MINUS_SRC_ALPHA) };
        static const en_t OPA[] = { E(SRC_ALPHA), E(ONE_MINUS_SRC_ALPHA) };
        const en_t *cr = &PICK(r, CRGB), *ca = &PICK(r, CA);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, cr->e); glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, ca->e);
        snprintf(f, sizeof f, "combine-rgb-%s", cr->n); feat(t, f); snprintf(f, sizeof f, "combine-alpha-%s", ca->n); feat(t, f);
        for (int i = 0; i < 3; i++) {
            const en_t *s = &PICK(r, SRC), *o = &PICK(r, OPC), *sa = &PICK(r, SRC), *oa = &PICK(r, OPA);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB + i, s->e); glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB + i, o->e);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA + i, sa->e); glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA + i, oa->e);
            snprintf(f, sizeof f, "combine-src%d-%s-%s", i, s->n, o->n); feat(t, f);
            snprintf(f, sizeof f, "combine-asrc%d-%s-%s", i, sa->n, oa->n); feat(t, f);
        }
        float sc[] = { 1, 2, 4 }; float rs = PICK(r, sc), as = PICK(r, sc);
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, rs); glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, as);
        if (rs != 1) feat(t, "rgb-scale"); if (as != 1) feat(t, "alpha-scale");
    }
    if (rng_int(r, 4) == 0) {   // texgen
        static const en_t TG[] = { E(OBJECT_LINEAR), E(EYE_LINEAR), E(SPHERE_MAP), E(NORMAL_MAP), E(REFLECTION_MAP) };
        const en_t *g = &PICK(r, TG);
        glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, g->e); glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, g->e);
        float ps[4] = { 0.01f, 0.003f, 0.2f, 0.1f }, pt[4] = { 0.002f, 0.012f, -0.3f, 0.2f };
        glTexGenfv(GL_S, g->e == GL_EYE_LINEAR ? GL_EYE_PLANE : GL_OBJECT_PLANE, ps); glTexGenfv(GL_T, g->e == GL_EYE_LINEAR ? GL_EYE_PLANE : GL_OBJECT_PLANE, pt);
        glEnable(GL_TEXTURE_GEN_S); glEnable(GL_TEXTURE_GEN_T);
        snprintf(f, sizeof f, "texgen-%s", g->n); feat(t, f);
    }
    if (rng_int(r, 4) == 0) {
        glMatrixMode(GL_TEXTURE); glLoadIdentity(); glRotatef(rng_f(r, -40, 40), 0, 0, 1); glScalef(rng_f(r, 0.5f, 2), rng_f(r, 0.5f, 2), 1);
        glMatrixMode(GL_MODELVIEW); feat(t, "texmatrix");
    }
    p_glActiveTexture(GL_TEXTURE0);
}
static void t_ffp(gt_test *t) {
    rng_t r; rng_seed(&r, t->a * 2654435761u);
    char f[160];
    GLuint tex[2] = { 0, 0 };
    int units = rng_int(&r, 3);
    for (int u = 0; u < units; u++) fuzz_texunit(t, &r, u, &tex[u]);
    if (rng_int(&r, 3) == 0) {   // lighting
        glEnable(GL_LIGHTING); feat(t, "lighting");
        int nl = 1 + rng_int(&r, 3);
        for (int l = 0; l < nl; l++) {
            glEnable(GL_LIGHT0 + l);
            float pos[4] = { rng_f(&r, -1, 1) * 200 + 128, rng_f(&r, -1, 1) * 200 + 128, rng_f(&r, 20, 200), (float)rng_int(&r, 2) };
            glLightfv(GL_LIGHT0 + l, GL_POSITION, pos);
            float c[4] = { rng_f(&r, 0, 1), rng_f(&r, 0, 1), rng_f(&r, 0, 1), 1 };
            glLightfv(GL_LIGHT0 + l, GL_DIFFUSE, c); c[3] = rng_f(&r, 0, 1); glLightfv(GL_LIGHT0 + l, GL_SPECULAR, c);
            if (pos[3] != 0) { snprintf(f, sizeof f, "light%d-point", l); feat(t, f);
                glLightf(GL_LIGHT0 + l, GL_LINEAR_ATTENUATION, rng_f(&r, 0, 0.01f));
                glLightf(GL_LIGHT0 + l, GL_QUADRATIC_ATTENUATION, rng_f(&r, 0, 0.0001f));
                if (rng_int(&r, 2)) { float d[3] = { 128 - pos[0], 128 - pos[1], -pos[2] };
                    glLightfv(GL_LIGHT0 + l, GL_SPOT_DIRECTION, d); glLightf(GL_LIGHT0 + l, GL_SPOT_CUTOFF, rng_f(&r, 20, 89));
                    glLightf(GL_LIGHT0 + l, GL_SPOT_EXPONENT, rng_f(&r, 0, 30)); snprintf(f, sizeof f, "light%d-spot", l); feat(t, f); }
            } else { snprintf(f, sizeof f, "light%d-directional", l); feat(t, f); }
        }
        float m[4] = { rng_f(&r, 0, 1), rng_f(&r, 0, 1), rng_f(&r, 0, 1), rng_f(&r, 0.3f, 1) };
        glMaterialfv(GL_FRONT, GL_DIFFUSE, m); m[0] = 1; glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, m);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, rng_f(&r, 1, 100));
        float bm[4] = { 0.8f, 0.2f, 0.1f, 1 }; glMaterialfv(GL_BACK, GL_DIFFUSE, bm);
        float em[4] = { rng_f(&r, 0, 0.3f), 0, rng_f(&r, 0, 0.3f), 1 }; glMaterialfv(GL_FRONT, GL_EMISSION, em);
        if (rng_int(&r, 2)) { glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1); feat(t, "two-side"); }
        if (rng_int(&r, 3) == 0) { glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, 1); feat(t, "local-viewer"); }
        if (rng_int(&r, 3) == 0) { glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR); feat(t, "separate-specular"); }
        if (rng_int(&r, 2)) {
            static const en_t CM[] = { E(AMBIENT_AND_DIFFUSE), E(DIFFUSE), E(AMBIENT), E(SPECULAR), E(EMISSION) };
            static const en_t CF[] = { E(FRONT), E(BACK), E(FRONT_AND_BACK) };
            const en_t *cm = &PICK(&r, CM), *cf = &PICK(&r, CF);
            glColorMaterial(cf->e, cm->e); glEnable(GL_COLOR_MATERIAL);
            snprintf(f, sizeof f, "colormaterial-%s-%s", cf->n, cm->n); feat(t, f);
        }
        if (rng_int(&r, 2)) { glEnable(GL_NORMALIZE); feat(t, "normalize"); } else if (rng_int(&r, 2)) { glEnable(GL_RESCALE_NORMAL); feat(t, "rescale-normal"); }
    }
    if (rng_int(&r, 3) == 0) {
        glEnable(GL_COLOR_SUM); feat(t, "color-sum");
    }
    if (rng_int(&r, 3) == 0) {
        static const en_t FM[] = { E(LINEAR), E(EXP), E(EXP2) };
        const en_t *fm = &PICK(&r, FM);
        glEnable(GL_FOG); glFogi(GL_FOG_MODE, fm->e);
        float fc[4] = { rng_f(&r, 0, 1), rng_f(&r, 0, 1), rng_f(&r, 0, 1), rng_f(&r, 0, 1) }; glFogfv(GL_FOG_COLOR, fc);
        glFogf(GL_FOG_START, rng_f(&r, -0.5f, 0.3f)); glFogf(GL_FOG_END, rng_f(&r, 0.5f, 2.5f)); glFogf(GL_FOG_DENSITY, rng_f(&r, 0.2f, 2));
        int coord = rng_int(&r, 2); glFogi(GL_FOG_COORD_SRC, coord ? GL_FOG_COORD : GL_FRAGMENT_DEPTH);
        snprintf(f, sizeof f, "fog-%s-%s", fm->n, coord ? "coord" : "depth"); feat(t, f);
    }
    if (rng_int(&r, 3) == 0) {
        static const en_t AF[] = { E(NEVER), E(LESS), E(EQUAL), E(LEQUAL), E(GREATER), E(NOTEQUAL), E(GEQUAL), E(ALWAYS) };
        const en_t *af = &PICK(&r, AF); glEnable(GL_ALPHA_TEST); glAlphaFunc(af->e, rng_f(&r, 0.2f, 0.8f));
        snprintf(f, sizeof f, "alphatest-%s", af->n); feat(t, f);
    }
    if (rng_int(&r, 2)) {
        static const en_t BF[] = { E(ZERO), E(ONE), E(SRC_COLOR), E(ONE_MINUS_SRC_COLOR), E(DST_COLOR), E(ONE_MINUS_DST_COLOR), E(SRC_ALPHA),
            E(ONE_MINUS_SRC_ALPHA), E(DST_ALPHA), E(ONE_MINUS_DST_ALPHA), E(CONSTANT_COLOR), E(ONE_MINUS_CONSTANT_ALPHA), E(SRC_ALPHA_SATURATE) };
        static const en_t BE[] = { E(FUNC_ADD), E(FUNC_ADD), E(FUNC_SUBTRACT), E(FUNC_REVERSE_SUBTRACT), E(MIN), E(MAX) };
        const en_t *s = &PICK(&r, BF), *d = &PICK(&r, BF), *sa = &PICK(&r, BF), *da = &PICK(&r, BF), *be = &PICK(&r, BE);
        if (!strcmp(d->n, "SRC_ALPHA_SATURATE")) d = &BF[1]; if (!strcmp(da->n, "SRC_ALPHA_SATURATE")) da = &BF[1];
        glEnable(GL_BLEND);
        if (rng_int(&r, 2)) { p_glBlendFuncSeparate(s->e, d->e, sa->e, da->e); feat(t, "blend-separate"); } else glBlendFunc(s->e, d->e);
        p_glBlendEquation(be->e); p_glBlendColor(0.3f, 0.6f, 0.9f, 0.5f);
        snprintf(f, sizeof f, "blend-%s-%s", s->n, d->n); feat(t, f); snprintf(f, sizeof f, "blendeq-%s", be->n); feat(t, f);
    }
    if (rng_int(&r, 4) == 0) { GLboolean m[4] = { rng_int(&r, 2), rng_int(&r, 2), rng_int(&r, 2), rng_int(&r, 2) }; glColorMask(m[0], m[1], m[2], m[3]); feat(t, "colormask"); }
    int depth = rng_int(&r, 2);
    if (depth) {
        static const en_t DF[] = { E(LESS), E(LEQUAL), E(GREATER), E(GEQUAL), E(EQUAL), E(NOTEQUAL), E(ALWAYS) };
        const en_t *df = &PICK(&r, DF); glEnable(GL_DEPTH_TEST); glDepthFunc(df->e);
        snprintf(f, sizeof f, "depth-%s", df->n); feat(t, f);
        if (rng_int(&r, 3) == 0) { glDepthMask(GL_FALSE); feat(t, "depthmask-off"); }
        if (rng_int(&r, 3) == 0) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(rng_f(&r, -2, 2), rng_f(&r, -4, 4)); feat(t, "polygon-offset"); }
        if (rng_int(&r, 3) == 0) { glDepthRange(0.2, 0.7); feat(t, "depthrange"); }
    }
    if (rng_int(&r, 3) == 0) {
        static const en_t SO[] = { E(KEEP), E(ZERO), E(REPLACE), E(INCR), E(DECR), E(INVERT), E(INCR_WRAP), E(DECR_WRAP) };
        static const en_t SF[] = { E(NEVER), E(LESS), E(EQUAL), E(LEQUAL), E(GREATER), E(NOTEQUAL), E(GEQUAL), E(ALWAYS) };
        const en_t *sf = &PICK(&r, SF), *o1 = &PICK(&r, SO), *o2 = &PICK(&r, SO), *o3 = &PICK(&r, SO);
        glEnable(GL_STENCIL_TEST); glStencilFunc(sf->e, rng_int(&r, 4), 0xff); glStencilOp(o1->e, o2->e, o3->e);
        snprintf(f, sizeof f, "stencil-%s-%s-%s-%s", sf->n, o1->n, o2->n, o3->n); feat(t, f);
        if (rng_int(&r, 3) == 0) { p_glStencilOpSeparate(GL_BACK, GL_INVERT, GL_INCR, GL_DECR); feat(t, "stencil-separate"); }
    }
    if (rng_int(&r, 4) == 0) {
        static const en_t CU[] = { E(FRONT), E(BACK), E(FRONT_AND_BACK) };
        const en_t *c = &PICK(&r, CU); glEnable(GL_CULL_FACE); glCullFace(c->e); if (rng_int(&r, 2)) { glFrontFace(GL_CW); feat(t, "frontface-cw"); }
        snprintf(f, sizeof f, "cull-%s", c->n); feat(t, f);
    }
    if (rng_int(&r, 6) == 0) { glEnable(GL_CLIP_PLANE0); double p[4] = { 1, -0.7, 0, -40 }; glClipPlane(GL_CLIP_PLANE0, p); feat(t, "clipplane"); }
    if (rng_int(&r, 8) == 0) {
        static const en_t PM[] = { E(LINE), E(POINT) }; const en_t *pm = &PICK(&r, PM);
        glPolygonMode(GL_FRONT_AND_BACK, pm->e); snprintf(f, sizeof f, "polygonmode-%s", pm->n); feat(t, f);
    }
    int flat = rng_int(&r, 4) == 0; if (flat) { glShadeModel(GL_FLAT); feat(t, "flat"); }
    glPointSize(4); glLineWidth(2);
    // scene: background layer (fills depth/stencil), then the primitive under test through a random path
    int path = rng_int(&r, P_NPATH);
    const en_t *pr = &PICK(&r, PRIMS);
    snprintf(f, sizeof f, "prim-%s", pr->n); feat(t, f); snprintf(f, sizeof f, "path-%s", PATHN[path]); feat(t, f);
    vtx_t v[64]; int n;
    glPushMatrix(); glTranslatef(0, 0, 0.3f);
    n = make_prim(GL_QUADS, 10, 10, 236, v, 3); gt_submit(GL_QUADS, v, n, P_IMM);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glTranslatef(128, 128, 0); glRotatef(rng_f(&r, -30, 30), 0, 0, 1); glTranslatef(-128, -128, 0);
    n = make_prim(pr->e, 20, 20, 216, v, t->a);
    gt_submit(pr->e, v, n, path);
    gt_errcheck(t, "draw");
    glColorMask(1, 1, 1, 1);
    gt_img(t, 0, 0, 256, 256, 4, 0.01);
    glDeleteTextures(2, tex);
}

void reg_ffp(void) {
    char nm[96];
    for (int p = 0; p < N(PRIMS); p++) for (int path = 0; path < P_NPATH; path++) for (int fl = 0; fl < 2; fl++) {
        snprintf(nm, sizeof nm, "draw/%s/%s/%s", PRIMS[p].n, PATHN[path], fl ? "flat" : "smooth");
        gt_add(nm, t_draw, PRIMS[p].e, path, fl, 0, NULL);
    }
    for (int s = 1; s <= 4000; s++) { snprintf(nm, sizeof nm, "ffp/%d", s); gt_add(nm, t_ffp, s, 0, 0, 0, NULL); }
}
