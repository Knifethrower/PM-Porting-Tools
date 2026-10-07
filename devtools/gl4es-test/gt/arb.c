// ARB_vertex_program / ARB_fragment_program: what every Unity 4 "opengl" shader is. gl4es translates them
// to GLSL (oldprogram.c, arbconverter.c) and combines them with its fixed-function emulation (fpe.c).
//   arb/lifecycle/*     create/delete/re-create programs (the id-recycling crash was here)
//   arb/fp/<seed>       random fragment program, fixed-function vertex stage
//   arb/vp/<seed>       random vertex program, probe fragment programs show each output
//   arb/pair/<seed>     random vertex + fragment program
//   corpus/...          real programs extracted from games (corpus.c)
#include "gt.h"
#include <stdarg.h>

// ---------- shared scene ----------
// every unit gets a 2D and a cube map texture (Unity programs sample both), units 0-3 a rectangle texture too
#define NUNITS 8
static GLuint arb_textures[NUNITS * 3];
static void make_textures(int seed) {
    rng_t r; rng_seed(&r, seed);
    glGenTextures(NUNITS * 3, arb_textures);
    uint8_t px[32 * 32 * 4];
    for (int u = 0; u < NUNITS; u++) {
        p_glActiveTexture(GL_TEXTURE0 + u);
        float ph[4]; for (int c = 0; c < 4; c++) ph[c] = rng_f(&r, 0, 6.28f);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) for (int c = 0; c < 4; c++)   // smooth: filtering-safe
            px[4 * (y * 32 + x) + c] = (uint8_t)(127.5f + 127.f * sinf(ph[c] + x * 0.21f * (c + 1) + y * 0.17f * (4 - c)));
        glBindTexture(GL_TEXTURE_2D, arb_textures[u]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_CUBE_MAP, arb_textures[NUNITS + u]);
        for (int f = 0; f < 6; f++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, px + f * 512);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        if (u < 4) {
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, arb_textures[2 * NUNITS + u]);
            glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
    }
    p_glActiveTexture(GL_TEXTURE0);
}
// non-default values for everything a program can bind with state.*
static void set_state(int seed) {
    rng_t r; rng_seed(&r, seed * 31 + 5);
    float v[4];
#define RV(lo, hi) (v[0] = rng_f(&r, lo, hi), v[1] = rng_f(&r, lo, hi), v[2] = rng_f(&r, lo, hi), v[3] = rng_f(&r, lo, hi), v)
    for (int l = 0; l < 2; l++) {
        glLightfv(GL_LIGHT0 + l, GL_AMBIENT, RV(0, 0.5f)); glLightfv(GL_LIGHT0 + l, GL_DIFFUSE, RV(0, 1));
        glLightfv(GL_LIGHT0 + l, GL_SPECULAR, RV(0, 1)); glLightfv(GL_LIGHT0 + l, GL_POSITION, RV(-1, 1));
        glLightfv(GL_LIGHT0 + l, GL_SPOT_DIRECTION, RV(-1, 1)); glLightf(GL_LIGHT0 + l, GL_SPOT_EXPONENT, rng_f(&r, 0, 20));
        glLightf(GL_LIGHT0 + l, GL_SPOT_CUTOFF, rng_f(&r, 10, 80)); glLightf(GL_LIGHT0 + l, GL_LINEAR_ATTENUATION, rng_f(&r, 0, 1));
    }
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, RV(0, 0.4f));
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, RV(0, 1)); glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, RV(0, 1));
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, RV(0, 1)); glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, RV(0, 0.3f));
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, rng_f(&r, 1, 60));
    glFogfv(GL_FOG_COLOR, RV(0, 1)); glFogf(GL_FOG_START, rng_f(&r, 0, 0.5f)); glFogf(GL_FOG_END, rng_f(&r, 0.6f, 2));
    glFogf(GL_FOG_DENSITY, rng_f(&r, 0.2f, 2));
    glFogi(GL_FOG_COORDINATE_SOURCE, GL_FOG_COORDINATE);
    for (int u = 0; u < 2; u++) {
        p_glActiveTexture(GL_TEXTURE0 + u);
        glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, RV(0, 1));
        glTexGenfv(GL_S, GL_OBJECT_PLANE, RV(-0.01f, 0.01f)); glTexGenfv(GL_T, GL_EYE_PLANE, RV(-0.01f, 0.01f));
        glMatrixMode(GL_TEXTURE); glLoadIdentity(); glTranslatef(rng_f(&r, -0.5f, 0.5f), rng_f(&r, -0.5f, 0.5f), 0);
        glRotatef(rng_f(&r, -30, 30), 0, 0, 1); glScalef(rng_f(&r, 0.5f, 2), rng_f(&r, 0.5f, 2), 1);
    }
    p_glActiveTexture(GL_TEXTURE0);
    double plane[4] = { rng_f(&r, -1, 1), rng_f(&r, -1, 1), 0, rng_f(&r, -1, 1) }; glClipPlane(GL_CLIP_PLANE0, plane);
    glPointSize(rng_f(&r, 1, 4));
    float att[3] = { 1, rng_f(&r, 0, 0.1f), 0 }; p_glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, att);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glTranslatef(128, 128, 0); glRotatef(rng_f(&r, -20, 20), 0, 0, 1); glScalef(rng_f(&r, 0.8f, 1.1f), rng_f(&r, 0.8f, 1.1f), 1);
    glTranslatef(-128, -128, 0);
    for (int i = 0; i < 16; i++) {
        p_glProgramEnvParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, i, RV(-1, 1));
        p_glProgramEnvParameter4fvARB(GL_VERTEX_PROGRAM_ARB, i, RV(-1, 1));
    }
#undef RV
}
static void set_locals(GLenum target, int seed) {
    rng_t r; rng_seed(&r, seed * 17 + target);
    for (int i = 0; i < 16; i++) { float v[4] = { rng_f(&r, -1, 1), rng_f(&r, -1, 1), rng_f(&r, -1, 1), rng_f(&r, -1, 1) }; p_glProgramLocalParameter4fvARB(target, i, v); }
}
// 6x6-quad mesh at (x,y) size s with every per-vertex input varying smoothly
static void draw_mesh(float x0, float y0, float s, const int *attribs, int nattribs) {
    const int n = 6;
    glBegin(GL_QUADS);
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
        static const int corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int k = 0; k < 4; k++) {
            float u = (float)(i + corner[k][0]) / n, v = (float)(j + corner[k][1]) / n;
            glNormal3f(u - 0.5f, v - 0.5f, 0.7f);
            p_glSecondaryColor3f(v, 1 - u, u * v);
            p_glFogCoordf(u * 1.5f);
            for (int t = 0; t < 8; t++) p_glMultiTexCoord4f(GL_TEXTURE0 + t, u * (1 + t * 0.3f), v * (1 - t * 0.1f), 0.5f + 0.25f * (u - v), 1 + 0.2f * u * t);
            for (int a = 0; a < nattribs; a++) p_glVertexAttrib4fARB(attribs[a], u - v, u * 2 - 0.3f * a, 0.4f + v, 1 - u * v);
            glColor4f(u, v, 1 - u, 0.5f + 0.5f * v);
            glVertex3f(x0 + u * s, y0 + v * s, (u - v) * 0.5f);
        }
    }
    glEnd();
}

static GLuint load_program(gt_test *t, GLenum target, const char *src, int *ok) {
    GLuint id; p_glGenProgramsARB(1, &id); p_glBindProgramARB(target, id);
    while (glGetError()) {}
    p_glProgramStringARB(target, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(src), src);
    GLenum e = glGetError();
    GLint pos = -1; glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &pos);
    float c = (e == GL_NO_ERROR && pos == -1) ? 1 : 0;
    gt_vals(t, target == GL_VERTEX_PROGRAM_ARB ? "vp-accepted" : "fp-accepted", 0, &c, 1);
    if (!c) {
        const char *es = (const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
        char line[120] = ""; if (es) { snprintf(line, sizeof line, "%s", es); for (char *p = line; *p; p++) if (*p == '\n') *p = ' '; }
        gt_info(t, "progerr %s pos=%d %s", target == GL_VERTEX_PROGRAM_ARB ? "vp" : "fp", pos, line);
    }
    if (ok) *ok = (int)c;
    return id;
}

// ---------- random program generator ----------
typedef struct { char *s; size_t n, cap; } sb_t;
static void sb(sb_t *b, const char *fmt, ...) {
    va_list ap; char tmp[1024]; va_start(ap, fmt); int k = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (b->n + k + 1 > b->cap) { b->cap = (b->n + k + 1) * 2; b->s = realloc(b->s, b->cap); }
    memcpy(b->s + b->n, tmp, k + 1); b->n += k;
}
static const char *swz(rng_t *r, int scalar) {
    static char b[8][8]; static int bi; char *o = b[bi++ & 7];
    static const char c[] = "xyzw";
    if (scalar) { snprintf(o, 8, ".%c", c[rng_int(r, 4)]); return o; }
    switch (rng_int(r, 6)) {
    case 0: case 1: return "";
    case 2: snprintf(o, 8, ".%c", c[rng_int(r, 4)]); return o;
    default: snprintf(o, 8, ".%c%c%c%c", c[rng_int(r, 4)], c[rng_int(r, 4)], c[rng_int(r, 4)], c[rng_int(r, 4)]); return o;
    }
}
static const char *mask(rng_t *r) {
    static const char *m[] = { "", "", "", ".x", ".y", ".z", ".w", ".xy", ".xz", ".yw", ".xyz", ".xyw", ".zw", ".xw" };
    return PICK(r, m);
}
#define NTEMP 6
static const char *FPIN[] = { "fragment.color", "fragment.color.secondary", "fragment.texcoord[0]", "fragment.texcoord[1]",
    "fragment.texcoord[3]", "fragment.fogcoord", "program.local[1]", "program.env[2]", "program.local[7]", "kA", "kB", "pL", "pE",
    "state.material.diffuse", "state.fog.color", "state.texenv[0].color", "state.depth.range", "state.lightprod[0].front.ambient",
    "state.fog.params", "state.light[0].diffuse", "state.lightmodel.ambient", "pArr[1]" };
static const char *VPIN[] = { "vertex.position", "vertex.color", "vertex.normal", "vertex.texcoord[0]", "vertex.texcoord[1]",
    "vertex.fogcoord", "vertex.color.secondary", "vertex.attrib[6]", "vertex.attrib[7]", "vertex.attrib[14]", "program.local[2]",
    "program.env[3]", "kA", "kB", "pL", "pE", "state.material.ambient", "state.material.shininess", "state.light[0].position",
    "state.light[1].spot.direction", "state.light[0].half", "state.lightmodel.scenecolor", "state.lightprod[1].diffuse",
    "state.fog.params", "state.clip[0].plane", "state.point.size", "state.point.attenuation", "state.texgen[0].object.s",
    "state.texgen[1].eye.t", "state.matrix.modelview.row[1]", "state.matrix.projection.row[0]", "state.matrix.mvp.inverse.row[2]",
    "state.matrix.texture[0].row[0]", "state.matrix.modelview.invtrans.row[1]", "state.matrix.texture[1].transpose.row[3]",
    "state.depth.range", "pArr[2]", "state.material.back.specular" };
// constructs the generator leaves out: GT_ARB_AVOID="a,b,..." (bindings or opcodes), default = the gaps
// arb/syntax and arb/binding already report, so random programs keep reaching gl4es's translation
static const char *DEFAULT_AVOID =
    "negscalar,SIN,SWZ,result.color.back,"                                                  // rejected / broken GLSL (sin length bug)
    "state.depth.range,state.fog.color,state.fog.params,state.texenv[0].color,state.light[0].diffuse,"   // fp: rejected or crash
    "state.clip[0].plane,state.light[0].half,state.light[0].position,state.light[1].spot.direction,"    // vp: rejected or crash
    "state.matrix.texture[0].row[0],state.matrix.texture[1].transpose.row[3],state.point.attenuation,"
    "state.point.size,state.texgen[0].object.s,state.texgen[1].eye.t";
static int avoided(const char *tok) {
    static char *list;
    if (!list) {
        const char *e = getenv("GT_ARB_AVOID");
        size_t n = strlen(e ? e : DEFAULT_AVOID) + 3; list = malloc(n);
        snprintf(list, n, ",%s,", e ? e : DEFAULT_AVOID);
    }
    char k[96]; snprintf(k, sizeof k, ",%s,", tok);
    return strstr(list, k) != NULL;
}
// a source operand: temp, named param, inline binding; 'scalar' forces a one-component swizzle
static const char *src_op(rng_t *r, int vp, int scalar) {
    static char b[16][96]; static int bi; char *o = b[bi++ & 15];
    const char *neg = rng_int(r, 4) ? "" : "-";
    if (rng_int(r, 5) < 3) { snprintf(o, 96, "%st%d%s", neg, rng_int(r, NTEMP), swz(r, scalar)); return o; }
    const char *in;
    do in = vp ? PICK(r, VPIN) : PICK(r, FPIN); while (avoided(in));
    snprintf(o, 96, "%s%s%s", neg, in, swz(r, scalar));
    return o;
}
// emit a "safe" scalar into temp t5 for ops with restricted domains
static const char *safe_pos(sb_t *b, rng_t *r, int vp) {   // |x| + 0.25
    sb(b, "ABS t5, %s;\nADD t5, t5, kEps;\n", src_op(r, vp, 0)); return "t5";
}
static const char *safe_exp(sb_t *b, rng_t *r, int vp) {   // clamp to [-4, 4]
    sb(b, "MIN t5, %s, kLim;\nMAX t5, t5, -kLim;\n", src_op(r, vp, 0)); return "t5";
}
static void gen_body(sb_t *b, rng_t *r, int vp, int n) {
    static const char c[] = "xyzw";
    for (int i = 0; i < n; i++) {
        int d = rng_int(r, NTEMP - 1);   // t5 is scratch
        const char *sat = (!vp && rng_int(r, 5) == 0) ? "_SAT" : "";
        const char *m = mask(r);
        static const char *OPN[] = { "ADD", "MUL", "MAD", "DP3", "DP4", "DPH", "MAX", "MIN", "SGE", "SLT", "SUB", "XPD", "DST", "ABS", "FLR",
            "FRC", "MOV", "LIT", "EX2", "LG2", "RCP", "RSQ", "POW", "SWZ", "EXP|CMP", "LOG|LRP", "ARL|SIN", "COS", "SCS", "TEX", "KIL" };
        int op; char opn[8];
        do { op = rng_int(r, vp ? 27 : 31); const char *o = OPN[op], *bar = strchr(o, '|');
             if (bar) snprintf(opn, sizeof opn, "%.*s", vp ? (int)(bar - o) : (int)strlen(bar + 1), vp ? o : bar + 1); else snprintf(opn, sizeof opn, "%s", o);
        } while (avoided(opn));
        switch (op) {
        case 0: sb(b, "ADD%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 1: sb(b, "MUL%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 2: sb(b, "MAD%s t%d%s, %s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 3: sb(b, "DP3%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 4: sb(b, "DP4%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 5: sb(b, "DPH%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 6: sb(b, "MAX%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 7: sb(b, "MIN%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 8: sb(b, "SGE%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 9: sb(b, "SLT%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 10: sb(b, "SUB%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 11: { const char *mm[] = { ".x", ".xy", ".xyz", ".yz", "" }; const char *x = PICK(r, mm); if (!*x) x = ".xyz";
                   sb(b, "XPD%s t%d%s, %s, %s;\n", sat, d, x, src_op(r, vp, 0), src_op(r, vp, 0)); break; }
        case 12: sb(b, "DST%s t%d%s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0)); break;
        case 13: sb(b, "ABS%s t%d%s, %s;\n", sat, d, m, src_op(r, vp, 0)); break;
        case 14: sb(b, "FLR%s t%d%s, %s;\n", sat, d, m, src_op(r, vp, 0)); break;
        case 15: sb(b, "FRC%s t%d%s, %s;\n", sat, d, m, src_op(r, vp, 0)); break;
        case 16: sb(b, "MOV%s t%d%s, %s;\n", sat, d, m, src_op(r, vp, 0)); break;
        case 17: { const char *s = safe_exp(b, r, vp); sb(b, "LIT%s t%d%s, %s;\n", sat, d, m, s); break; }
        case 18: { const char *s = safe_exp(b, r, vp); sb(b, "EX2%s t%d%s, %s.%c;\n", sat, d, m, s, c[rng_int(r, 4)]); break; }
        case 19: { const char *s = safe_pos(b, r, vp); sb(b, "LG2%s t%d%s, %s.%c;\n", sat, d, m, s, c[rng_int(r, 4)]); break; }
        case 20: { const char *s = safe_pos(b, r, vp); sb(b, "RCP%s t%d%s, %s.%c;\n", sat, d, m, s, c[rng_int(r, 4)]); break; }
        case 21: { const char *s = safe_pos(b, r, vp); sb(b, "RSQ%s t%d%s, %s.%c;\n", sat, d, m, s, c[rng_int(r, 4)]); break; }
        case 22: { int ax = rng_int(r, 4), ex = rng_int(r, 4);
                   sb(b, "ABS t4, %s;\nADD t4, t4, kEps;\nMIN t5, %s, kLim;\nMAX t5, t5, -kLim;\n", src_op(r, vp, 0), src_op(r, vp, 0));
                   sb(b, "POW%s t%d%s, t4.%c, t5.%c;\n", sat, d < 4 ? d : 0, m, c[ax], c[ex]); break; }
        case 23: {   // SWZ with 0/1 and negation (extended swizzle)
            static const char *e[] = { "x", "y", "z", "w", "0", "1", "-x", "-1", "-w" };
            sb(b, "SWZ%s t%d%s, t%d, %s,%s,%s,%s;\n", sat, d, m, rng_int(r, NTEMP), PICK(r, e), PICK(r, e), PICK(r, e), PICK(r, e)); break; }
        case 24: if (vp) { const char *s = safe_exp(b, r, vp); sb(b, "EXP t%d%s, %s.%c;\n", d, m, s, c[rng_int(r, 4)]); }
                 else sb(b, "CMP%s t%d%s, %s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0), src_op(r, vp, 0));
                 break;
        case 25: if (vp) { const char *s = safe_pos(b, r, vp); sb(b, "LOG t%d%s, %s.%c;\n", d, m, s, c[rng_int(r, 4)]); }
                 else sb(b, "LRP%s t%d%s, %s, %s, %s;\n", sat, d, m, src_op(r, vp, 0), src_op(r, vp, 0), src_op(r, vp, 0));
                 break;
        case 26: if (vp) {   // relative addressing into pArr[0..7], index kept in 1..4
                     sb(b, "FRC t5, %s;\nMUL t5, t5, kFour;\nARL A0.x, t5.%c;\nMOV t%d%s, pArr[A0.x + 1];\n", src_op(r, vp, 0), c[rng_int(r, 4)], d, m);
                 } else sb(b, "SIN%s t%d%s, %s;\n", sat, d, m, src_op(r, vp, 1));
                 break;
        case 27: sb(b, "COS%s t%d%s, %s;\n", sat, d, m, src_op(r, vp, 1)); break;
        case 28: { static const char *sm[] = { ".x", ".y", ".xy" }; sb(b, "SCS%s t%d%s, %s;\n", sat, d, PICK(r, sm), src_op(r, vp, 1)); break; }
        case 29: {   // texture lookups
            static const char *tx[] = { "TEX", "TXP", "TXB" };
            const char *ins = PICK(r, tx); int u = rng_int(r, 3);
            if (!strcmp(ins, "TXB")) sb(b, "MUL t5, %s, kQuarter;\nMOV t5.xy, %s;\n%s%s t%d%s, t5, texture[%d], 2D;\n", src_op(r, vp, 0), src_op(r, vp, 0), ins, sat, d, m, u);
            else if (!strcmp(ins, "TXP")) sb(b, "MOV t5, %s;\nABS t5.w, t5.w;\nADD t5.w, t5.w, kOne;\n%s%s t%d%s, t5, texture[%d], 2D;\n", src_op(r, vp, 0), ins, sat, d, m, u);
            else sb(b, "%s%s t%d%s, %s, texture[%d], 2D;\n", ins, sat, d, m, src_op(r, vp, 0), u);
            break; }
        case 30: sb(b, "KIL %s;\n", src_op(r, vp, 0)); break;
        }
    }
}
static char *gen_fp(int seed) {
    rng_t r; rng_seed(&r, seed); sb_t b = { 0 };
    sb(&b, "!!ARBfp1.0\n");
    static const char *opt[] = { "", "", "", "OPTION ARB_fog_linear;\n", "OPTION ARB_fog_exp;\n", "OPTION ARB_fog_exp2;\n",
                                 "OPTION ARB_precision_hint_fastest;\n", "OPTION ARB_precision_hint_nicest;\n" };
    sb(&b, "%s", PICK(&r, opt));
    // header with input-only temp init
    float k[5]; for (int i = 0; i < 5; i++) k[i] = rng_f(&r, -2, 2);
    sb(&b, "PARAM kA = {%.3f, %.3f, %.3f, %.3f};\nPARAM kB = %.3f;\n", k[0], k[1], k[2], k[3], avoided("negscalar") ? fabsf(k[4]) : k[4]);
    sb(&b, "PARAM kEps = {0.25, 0.25, 0.25, 0.25};\nPARAM kLim = {4, 4, 4, 4};\nPARAM kOne = 1.0;\nPARAM kQuarter = 0.25;\nPARAM kFour = 4.0;\n");
    sb(&b, "PARAM pL = program.local[%d];\nPARAM pE = program.env[%d];\nPARAM pArr[8] = { program.local[8..15] };\n", rng_int(&r, 8), rng_int(&r, 8));
    sb(&b, "TEMP t0, t1, t2, t3, t4, t5;\n");
    static const char *init[] = { "fragment.color", "fragment.texcoord[0]", "fragment.texcoord[1]", "fragment.color.secondary", "program.local[3]", "kA" };
    for (int i = 0; i < NTEMP; i++) {
        if (rng_int(&r, 2)) sb(&b, "TEX t%d, %s, texture[%d], 2D;\n", i, PICK(&r, init), rng_int(&r, 3));
        else sb(&b, "MOV t%d, %s;\n", i, PICK(&r, init));
    }
    gen_body(&b, &r, 0, 6 + rng_int(&r, 14));
    sb(&b, "MOV_SAT result.color, t%d;\n", rng_int(&r, NTEMP - 1));
    if (rng_int(&r, 3) == 0) sb(&b, "MOV_SAT result.color.w, t%d.%c;\n", rng_int(&r, NTEMP - 1), "xyzw"[rng_int(&r, 4)]);
    sb(&b, "END\n");
    return b.s;
}
static char *gen_vp(int seed, int *invariant) {
    rng_t r; rng_seed(&r, seed); sb_t b = { 0 };
    sb(&b, "!!ARBvp1.0\n");
    *invariant = rng_int(&r, 4) == 0;
    if (*invariant) sb(&b, "OPTION ARB_position_invariant;\n");
    float k[5]; for (int i = 0; i < 5; i++) k[i] = rng_f(&r, -2, 2);
    sb(&b, "PARAM kA = {%.3f, %.3f, %.3f, %.3f};\nPARAM kB = %.3f;\n", k[0], k[1], k[2], k[3], avoided("negscalar") ? fabsf(k[4]) : k[4]);
    sb(&b, "PARAM kEps = {0.25, 0.25, 0.25, 0.25};\nPARAM kLim = {4, 4, 4, 4};\nPARAM kOne = 1.0;\nPARAM kQuarter = 0.25;\nPARAM kFour = 4.0;\n");
    sb(&b, "PARAM pL = program.local[%d];\nPARAM pE = program.env[%d];\nPARAM pArr[8] = { program.local[8..15] };\n", rng_int(&r, 8), rng_int(&r, 8));
    int mvpstyle = rng_int(&r, 3);
    if (mvpstyle == 0) sb(&b, "PARAM mvp[4] = { state.matrix.mvp };\n");
    else if (mvpstyle == 1) sb(&b, "PARAM mv[4] = { state.matrix.modelview };\nPARAM pr[4] = { state.matrix.projection };\n");
    else sb(&b, "PARAM mvpT[4] = { state.matrix.mvp.transpose };\n");
    sb(&b, "TEMP t0, t1, t2, t3, t4, t5, pos;\nADDRESS A0;\n");
    static const char *init[] = { "vertex.color", "vertex.texcoord[0]", "vertex.normal", "vertex.attrib[6]", "vertex.color.secondary", "program.local[3]", "kA", "vertex.texcoord[1]" };
    for (int i = 0; i < NTEMP; i++) sb(&b, "MOV t%d, %s;\n", i, PICK(&r, init));
    if (!*invariant) {
        if (mvpstyle == 0) sb(&b, "DP4 result.position.x, mvp[0], vertex.position;\nDP4 result.position.y, mvp[1], vertex.position;\n"
                                  "DP4 result.position.z, mvp[2], vertex.position;\nDP4 result.position.w, mvp[3], vertex.position;\n");
        else if (mvpstyle == 1) sb(&b, "DP4 pos.x, mv[0], vertex.position;\nDP4 pos.y, mv[1], vertex.position;\nDP4 pos.z, mv[2], vertex.position;\n"
                                       "DP4 pos.w, mv[3], vertex.position;\nDP4 result.position.x, pr[0], pos;\nDP4 result.position.y, pr[1], pos;\n"
                                       "DP4 result.position.z, pr[2], pos;\nDP4 result.position.w, pr[3], pos;\n");
        else sb(&b, "MUL pos, mvpT[0], vertex.position.x;\nMAD pos, mvpT[1], vertex.position.y, pos;\nMAD pos, mvpT[2], vertex.position.z, pos;\n"
                    "MAD result.position, mvpT[3], vertex.position.w, pos;\n");
    }
    gen_body(&b, &r, 1, 6 + rng_int(&r, 14));
    sb(&b, "MOV result.color, t%d;\nMOV result.color.secondary, t%d;\n", rng_int(&r, 5), rng_int(&r, 5));
    for (int i = 0; i < 4; i++) sb(&b, "MOV result.texcoord[%d], t%d;\n", i, rng_int(&r, 5));
    sb(&b, "MOV result.fogcoord.x, t%d.%c;\n", rng_int(&r, 5), "xyzw"[rng_int(&r, 4)]);
    int back = rng_int(&r, 3) == 0, tb = rng_int(&r, 5);   // random stream identical whether avoided or not
    if (back && !avoided("result.color.back")) sb(&b, "MOV result.color.back, t%d;\n", tb);
    sb(&b, "END\n");
    return b.s;
}
// probe fragment program: shows one interpolated input smoothly (sin keeps every value in range)
static char *probe_fp(const char *input) {
    sb_t b = { 0 };
    sb(&b, "!!ARBfp1.0\nPARAM one = {1, 1, 1, 1};\nPARAM half = {0.5, 0.5, 0.5, 0.5};\nTEMP a, d, o;\nMOV a, %s;\n"
           "ABS d, a;\nADD d, d, one;\nRCP o.x, d.x;\nRCP o.y, d.y;\nRCP o.z, d.z;\nRCP o.w, d.w;\nMUL o, o, a;\n"
           "MAD_SAT result.color, o, half, half;\nEND\n", input);
    return b.s;
}
static const char *PROBES[] = { "fragment.color", "fragment.color.secondary", "fragment.texcoord[0]", "fragment.texcoord[1]",
                                "fragment.texcoord[2]", "fragment.texcoord[3]", "fragment.fogcoord" };
#define NPROBES 7

static void scene_begin(int seed) {
    make_textures(seed); set_state(seed);
    glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
}
static void scene_end(void) { glDeleteTextures(NUNITS * 3, arb_textures); }
static const int GENATTRIBS[] = { 6, 7, 14 };

// enables = 1: glEnable(GL_TEXTURE_2D) on every unit and GL_COLOR_SUM. A fragment program makes both
// meaningless, but gl4es only passes texcoords / secondary colour to it when they are on (arb/binding
// shows that bug with enables = 0); the fuzzers turn them on to get past it.
void gt_arb_fp(gt_test *t, const char *src, int seed, int enables) {   // also used by corpus.c
    scene_begin(seed);
    for (int u = 0; u < NUNITS; u++) {   // Mesa skips the texture matrix for units the program does not sample: keep it identity
        p_glActiveTexture(GL_TEXTURE0 + u); glMatrixMode(GL_TEXTURE); glLoadIdentity();
        if (enables) glEnable(GL_TEXTURE_2D);
    }
    p_glActiveTexture(GL_TEXTURE0); glMatrixMode(GL_MODELVIEW);
    if (enables) glEnable(GL_COLOR_SUM);
    int ok; GLuint fp = load_program(t, GL_FRAGMENT_PROGRAM_ARB, src, &ok);
    if (!ok) { scene_end(); return; }
    set_locals(GL_FRAGMENT_PROGRAM_ARB, seed);
    glEnable(GL_FRAGMENT_PROGRAM_ARB);
    if (strstr(src, "ARB_fog")) glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, strstr(src, "fog_exp2") ? GL_EXP2 : strstr(src, "fog_exp") ? GL_EXP : GL_LINEAR);
    draw_mesh(16, 16, 112, NULL, 0);
    // the same program again after a fixed-function draw in between (program cache / state switching)
    glDisable(GL_FRAGMENT_PROGRAM_ARB); glDisable(GL_FOG); glDisable(GL_COLOR_SUM);
    for (int u = NUNITS - 1; u >= 0; u--) { p_glActiveTexture(GL_TEXTURE0 + u); glDisable(GL_TEXTURE_2D); }
    draw_mesh(140, 16, 40, NULL, 0);
    for (int u = NUNITS - 1; u >= 0; u--) { p_glActiveTexture(GL_TEXTURE0 + u); if (enables) glEnable(GL_TEXTURE_2D); }
    if (enables) glEnable(GL_COLOR_SUM);
    glEnable(GL_FRAGMENT_PROGRAM_ARB); if (strstr(src, "ARB_fog")) glEnable(GL_FOG);
    draw_mesh(16, 136, 112, NULL, 0);
    glDisable(GL_FRAGMENT_PROGRAM_ARB); glDisable(GL_FOG);
    gt_errcheck(t, "draw");
    gt_img(t, 0, 0, 256, 256, 3, 0.03);
    p_glDeleteProgramsARB(1, &fp); scene_end();
}
void gt_arb_vp(gt_test *t, const char *src, int seed, int nattribs, const int *attribs) {
    scene_begin(seed);
    int ok; GLuint vp = load_program(t, GL_VERTEX_PROGRAM_ARB, src, &ok);
    if (!ok) { scene_end(); return; }
    set_locals(GL_VERTEX_PROGRAM_ARB, seed);
    GLuint probes[NPROBES];
    for (int i = 0; i < NPROBES; i++) { char *p = probe_fp(PROBES[i]); probes[i] = load_program(t, GL_FRAGMENT_PROGRAM_ARB, p, NULL); free(p); }
    glEnable(GL_VERTEX_PROGRAM_ARB);
    // the program's own position output for the mesh, probed tile by tile through a scissor
    glEnable(GL_FRAGMENT_PROGRAM_ARB);
    glEnable(GL_SCISSOR_TEST);
    for (int i = 0; i < NPROBES; i++) {
        p_glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, probes[i]);
        glScissor((i % 4) * 64, (i / 4) * 64, 64, 64);
        draw_mesh(0, 0, 256, attribs, nattribs);
    }
    glDisable(GL_SCISSOR_TEST);
    // fixed-function fragment stage with the vertex program (colour sum, fog, texturing from result.texcoord)
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glEnable(GL_COLOR_SUM); glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_LINEAR);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_SCISSOR_TEST); glScissor(0, 128, 256, 128);
    glPushMatrix(); glTranslatef(0, 128, 0); glScalef(0.5f, 0.5f, 1);
    draw_mesh(0, 0, 256, attribs, nattribs);
    glPopMatrix();
    glDisable(GL_SCISSOR_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_COLOR_SUM); glDisable(GL_FOG);
    glDisable(GL_VERTEX_PROGRAM_ARB);
    gt_errcheck(t, "draw");
    gt_img(t, 0, 0, 256, 256, 3, 0.03);
    p_glDeleteProgramsARB(NPROBES, probes); p_glDeleteProgramsARB(1, &vp); scene_end();
}

static void t_fp(gt_test *t) { char *s = gen_fp(t->a); gt_info(t, "seed %d", t->a); gt_arb_fp(t, s, t->a, 1); free(s); }
static void t_vp(gt_test *t) { int inv; char *s = gen_vp(t->a, &inv); gt_arb_vp(t, s, t->a, 3, GENATTRIBS); free(s); }
static void t_pair(gt_test *t) {
    int inv; char *v = gen_vp(t->a, &inv); char *f = gen_fp(t->a + 1000003);
    scene_begin(t->a);
    int ok1, ok2; GLuint vp = load_program(t, GL_VERTEX_PROGRAM_ARB, v, &ok1); GLuint fp = load_program(t, GL_FRAGMENT_PROGRAM_ARB, f, &ok2);
    if (ok1 && ok2) {
        set_locals(GL_VERTEX_PROGRAM_ARB, t->a); set_locals(GL_FRAGMENT_PROGRAM_ARB, t->a);
        glEnable(GL_VERTEX_PROGRAM_ARB); glEnable(GL_FRAGMENT_PROGRAM_ARB);
        draw_mesh(0, 0, 256, GENATTRIBS, 3);
        glDisable(GL_VERTEX_PROGRAM_ARB); glDisable(GL_FRAGMENT_PROGRAM_ARB);
        gt_img(t, 0, 0, 256, 256, 3, 0.03);
    }
    p_glDeleteProgramsARB(1, &vp); p_glDeleteProgramsARB(1, &fp); scene_end(); free(v); free(f);
}
// ---------- lifecycle ----------
static char *solid_fp(float r, float g, float b) {
    sb_t s = { 0 }; sb(&s, "!!ARBfp1.0\nPARAM c = {%.3f, %.3f, %.3f, 1};\nMUL result.color, c, fragment.color;\nEND\n", r, g, b); return s.s;
}
static char *offset_vp(float dx) {
    sb_t s = { 0 };
    sb(&s, "!!ARBvp1.0\nPARAM mvp[4] = { state.matrix.mvp };\nPARAM off = {%.1f, 0, 0, 0};\nTEMP p;\nADD p, vertex.position, off;\n"
           "DP4 result.position.x, mvp[0], p;\nDP4 result.position.y, mvp[1], p;\nDP4 result.position.z, mvp[2], p;\n"
           "DP4 result.position.w, mvp[3], p;\nMOV result.color, vertex.color;\nEND\n", dx);
    return s.s;
}
static void quad_at(int x, int y, int s) { glColor4f(1, 1, 1, 1); glBegin(GL_QUADS); glVertex2f(x, y); glVertex2f(x + s, y); glVertex2f(x + s, y + s); glVertex2f(x, y + s); glEnd(); }

// a: 0 delete + re-create with new text, 1 interleaved with fixed-function draws, 2 many programs churned like Unity scene loads
static void t_lifecycle(gt_test *t) {
    rng_t r; rng_seed(&r, 77 + t->a);
    int rounds = t->a == 2 ? 40 : 8;
    for (int k = 0; k < rounds; k++) {
        GLuint ids[2];
        char *f = solid_fp(rng_f(&r, 0, 1), rng_f(&r, 0, 1), rng_f(&r, 0, 1));
        char *v = offset_vp((float)(k % 3));
        ids[0] = load_program(t, GL_VERTEX_PROGRAM_ARB, v, NULL);
        ids[1] = load_program(t, GL_FRAGMENT_PROGRAM_ARB, f, NULL);
        glEnable(GL_VERTEX_PROGRAM_ARB); glEnable(GL_FRAGMENT_PROGRAM_ARB);
        int x = (k % 8) * 30 + 2, y = (k / 8) * 30 + 2;
        quad_at(x, y, 24);
        glDisable(GL_VERTEX_PROGRAM_ARB); glDisable(GL_FRAGMENT_PROGRAM_ARB);
        if (t->a >= 1) { glColor4f(0.2f, 0.9f, 0.3f, 1); glBegin(GL_TRIANGLES); glVertex2f(x, y + 26); glVertex2f(x + 4, y + 26); glVertex2f(x, y + 29); glEnd(); }
        p_glDeleteProgramsARB(2, ids);   // freed ids may come back from the next glGenProgramsARB
        if (t->a == 2 && k % 5 == 4) {   // a few programs kept alive across rounds
            GLuint keep; char *kf = solid_fp(0.5f, 0.5f, k / 40.f); keep = load_program(t, GL_FRAGMENT_PROGRAM_ARB, kf, NULL);
            glEnable(GL_FRAGMENT_PROGRAM_ARB); quad_at(x + 26, y, 3); glDisable(GL_FRAGMENT_PROGRAM_ARB);
            if (k % 10 == 9) p_glDeleteProgramsARB(1, &keep);
            free(kf);
        }
        free(f); free(v);
    }
    gt_errcheck(t, "draws");
    gt_img(t, 0, 0, 256, 256, 2, 0);
}
// a draw while a program that failed to load is enabled: GL says INVALID_OPERATION, nothing drawn, no crash.
// a = 0 vertex program, 1 fragment program, 2 program object never given a string
static void t_invalid(gt_test *t) {
    GLenum target = t->a == 0 ? GL_VERTEX_PROGRAM_ARB : GL_FRAGMENT_PROGRAM_ARB;
    GLuint id;
    if (t->a == 2) { p_glGenProgramsARB(1, &id); p_glBindProgramARB(target, id); }
    else id = load_program(t, target, t->a == 0 ? "!!ARBvp1.0\nMOV result.position, nonsense;\nEND\n" : "!!ARBfp1.0\nMOV result.color, nonsense;\nEND\n", NULL);
    glEnable(target);
    glColor3f(1, 0, 0); quad_at(20, 20, 100);
    gt_errcheck(t, "draw-invalid");
    glDisable(target);
    glColor3f(0, 1, 0); quad_at(140, 20, 100);   // the next draw must still work
    gt_img(t, 0, 0, 256, 256, 2, 0);
    p_glDeleteProgramsARB(1, &id);
}
// program queries (limits are implementation-specific: info only; program-specific values are compared)
static void t_queries(gt_test *t) {
    char *f = gen_fp(5);
    GLuint id = load_program(t, GL_FRAGMENT_PROGRAM_ARB, f, NULL);
    static const GLenum q[] = { GL_PROGRAM_LENGTH_ARB, GL_PROGRAM_FORMAT_ARB, GL_PROGRAM_BINDING_ARB, GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB };
    float v[4]; for (int i = 0; i < 4; i++) { GLint x = -1; p_glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB, q[i], &x); v[i] = (float)x; }
    gt_vals(t, "program-iv", 0, v, 4);
    static const GLenum ql[] = { GL_MAX_PROGRAM_INSTRUCTIONS_ARB, GL_MAX_PROGRAM_TEMPORARIES_ARB, GL_MAX_PROGRAM_PARAMETERS_ARB,
        GL_MAX_PROGRAM_LOCAL_PARAMETERS_ARB, GL_MAX_PROGRAM_ENV_PARAMETERS_ARB, GL_PROGRAM_INSTRUCTIONS_ARB, GL_PROGRAM_TEMPORARIES_ARB };
    for (int i = 0; i < 7; i++) { GLint x = -1; p_glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB, ql[i], &x); gt_info(t, "fp-iv 0x%x = %d", ql[i], x); }
    p_glDeleteProgramsARB(1, &id); free(f);
}

// one construct per program: which parts of the ARB grammar gl4es accepts and translates correctly
typedef struct { const char *name; int vp; const char *body; } syn_t;
#define FPH "!!ARBfp1.0\n"
#define VPH "!!ARBvp1.0\nPARAM mvp[4] = { state.matrix.mvp };\nDP4 result.position.x, mvp[0], vertex.position;\nDP4 result.position.y, mvp[1], vertex.position;\nDP4 result.position.z, mvp[2], vertex.position;\nDP4 result.position.w, mvp[3], vertex.position;\n"
static const syn_t SYN[] = {
    { "fp-param-scalar", 0, FPH "PARAM k = 0.5;\nMUL result.color, fragment.color, k;\nEND\n" },
    { "fp-param-scalar-integer", 0, FPH "PARAM k = 1;\nMUL result.color, fragment.color, k;\nEND\n" },
    { "fp-param-scalar-negative", 0, FPH "PARAM k = -0.5;\nMAD result.color, fragment.color, k, 1;\nEND\n" },
    { "fp-param-scalar-braced", 0, FPH "PARAM k = {0.5};\nMUL result.color, fragment.color, k;\nEND\n" },
    { "fp-param-vector-partial", 0, FPH "PARAM k = {0.25, 0.75};\nMOV result.color, k;\nEND\n" },
    { "fp-param-vector-negative", 0, FPH "PARAM k = {-0.25, 0.75, -1, 1};\nADD result.color, fragment.color, k;\nEND\n" },
    { "fp-inline-scalar", 0, FPH "ADD result.color, fragment.color, 0.25;\nEND\n" },
    { "fp-inline-scalar-negative", 0, FPH "ADD result.color, fragment.color, -0.25;\nEND\n" },
    { "fp-inline-vector", 0, FPH "ADD result.color, fragment.color, {0.5, -0.25, 0, 1};\nEND\n" },
    { "fp-inline-vector-swizzled", 0, FPH "MUL result.color, fragment.color, {0.5, 0.25, 0, 1}.yxwz;\nEND\n" },
    { "fp-param-array-mixed", 0, FPH "PARAM c[4] = { program.local[0], {0.1, 0.2, 0.3, 0.4}, state.fog.color, program.env[1..1] };\nTEMP t;\nADD t, c[0], c[1];\nMAD result.color, c[2], fragment.color, t;\nEND\n" },
    { "fp-param-array-range", 0, FPH "PARAM c[3] = { program.local[2..4] };\nTEMP t;\nADD t, c[0], c[2];\nMUL result.color, t, c[1];\nEND\n" },
    { "fp-param-array-nosize", 0, FPH "PARAM c[] = { program.local[2..4] };\nADD result.color, c[0], c[2];\nEND\n" },
    { "fp-attrib-output-alias", 0, FPH "ATTRIB col = fragment.color.primary;\nATTRIB tc = fragment.texcoord;\nOUTPUT o = result.color;\nTEMP t;\nTEX t, tc, texture, 2D;\nMUL o, t, col;\nEND\n" },
    { "fp-alias", 0, FPH "TEMP t;\nALIAS u = t;\nMOV u, fragment.color;\nMUL result.color, t, 0.5;\nEND\n" },
    { "fp-swizzle-rgba", 0, FPH "MOV result.color, fragment.color.bgra;\nEND\n" },
    { "fp-swizzle-scalar", 0, FPH "MOV result.color, fragment.color.g;\nEND\n" },
    { "fp-swz-extended", 0, FPH "TEMP t;\nSWZ t, fragment.color, -y, 1, x, 0;\nMOV result.color, t;\nEND\n" },
    { "fp-negate-param", 0, FPH "PARAM k = {0.5, 0.25, 0.75, 1};\nADD result.color, fragment.color, -k;\nEND\n" },
    { "fp-negate-swizzle", 0, FPH "ADD result.color, -fragment.color.zyxw, 1;\nEND\n" },
    { "fp-sat-ops", 0, FPH "TEMP t;\nADD_SAT t, fragment.color, fragment.color;\nMAD_SAT t, t, 2, -0.5;\nMOV_SAT result.color, t;\nEND\n" },
    { "fp-writemask-result", 0, FPH "MOV result.color, {0, 0, 0, 1};\nMOV result.color.xz, fragment.color;\nEND\n" },
    { "fp-fragment-position", 0, FPH "MUL result.color, fragment.position, 0.004;\nEND\n" },
    { "fp-fragment-fogcoord", 0, FPH "MOV result.color, fragment.fogcoord.xxxy;\nEND\n" },
    { "fp-secondary", 0, FPH "ADD result.color, fragment.color.secondary, fragment.color.primary;\nEND\n" },
    { "fp-tex-cube", 0, FPH "TEX result.color, fragment.texcoord[1], texture[2], CUBE;\nEND\n" },
    { "fp-tex-rect", 0, FPH "TEMP t;\nMUL t, fragment.texcoord[0], 32;\nTEX result.color, t, texture[1], RECT;\nEND\n" },
    { "fp-txp", 0, FPH "TXP result.color, fragment.texcoord[1], texture[0], 2D;\nEND\n" },
    { "fp-txb", 0, FPH "TEMP t;\nMOV t, fragment.texcoord[0];\nMOV t.w, 1.5;\nTXB result.color, t, texture[0], 2D;\nEND\n" },
    { "fp-kil", 0, FPH "TEMP t;\nSUB t, fragment.color, 0.5;\nKIL t.xyxy;\nMOV result.color, fragment.color;\nEND\n" },
    { "fp-result-depth", 0, FPH "MOV result.color, fragment.color;\nMOV result.depth.z, fragment.color.x;\nEND\n" },
    { "fp-state-matrix", 0, FPH "PARAM m[4] = { state.matrix.modelview.invtrans };\nTEMP t;\nDP4 t.x, m[0], fragment.color;\nDP4 t.y, m[1], fragment.color;\nMOV t.zw, fragment.color;\nMUL result.color, t, 0.01;\nEND\n" },
    { "fp-state-misc", 0, FPH "TEMP t;\nADD t, state.texenv[1].color, state.depth.range;\nADD t, t, state.fog.params;\nMUL result.color, t, state.lightprod[0].front.diffuse;\nEND\n" },
    { "fp-comments", 0, FPH "# a comment\nTEMP t; # trailing comment\nMOV t, fragment.color; #\nMOV result.color, t;\nEND\n# after end\n" },
    { "fp-lit-dst-xpd", 0, FPH "TEMP a, b, c;\nLIT a, fragment.color;\nDST b, fragment.color, fragment.texcoord[0];\nXPD c.xyz, fragment.color, fragment.texcoord[0];\nADD a, a, b;\nMAD result.color, c, 0.5, a;\nEND\n" },
    { "fp-scs-pow", 0, FPH "TEMP a;\nSCS a.xy, fragment.color.x;\nPOW a.z, fragment.color.y, 2.5;\nMOV a.w, 1;\nMAD result.color, a, 0.5, 0.5;\nEND\n" },
    { "fp-cmp-lrp-dph", 0, FPH "TEMP a, b;\nSUB a, fragment.color, 0.5;\nCMP a, a, fragment.texcoord[0], fragment.color;\nLRP b, fragment.color.w, a, fragment.texcoord[1];\nDPH b.w, b, a;\nMOV result.color, b;\nEND\n" },
    { "fp-option-fog-linear", 0, "!!ARBfp1.0\nOPTION ARB_fog_linear;\nMOV result.color, fragment.color;\nEND\n" },
    { "fp-option-fog-exp2", 0, "!!ARBfp1.0\nOPTION ARB_fog_exp2;\nMOV result.color, fragment.color;\nEND\n" },
    { "fp-option-precision", 0, "!!ARBfp1.0\nOPTION ARB_precision_hint_fastest;\nMOV result.color, fragment.color;\nEND\n" },
    { "fp-texture-unit7", 0, FPH "TEX result.color, fragment.texcoord[7], texture[7], 2D;\nEND\n" },
    { "fp-env-local-high", 0, FPH "ADD result.color, program.env[15], program.local[15];\nEND\n" },
    { "fp-same-temp-src-dst", 0, FPH "TEMP t;\nMOV t, fragment.color;\nMAD t, t.wzyx, t, t.y;\nXPD t.xyz, t, t.yzxw;\nMOV result.color, t;\nEND\n" },
    { "vp-basic", 1, VPH "MOV result.color, vertex.color;\nMOV result.texcoord[0], vertex.texcoord[0];\nEND\n" },
    { "vp-param-scalar", 1, VPH "PARAM k = 0.5;\nMUL result.color, vertex.color, k;\nEND\n" },
    { "vp-inline-constants", 1, VPH "MAD result.color, vertex.color, {0.5, 0.25, 1, 1}, 0.125;\nEND\n" },
    { "vp-arl-relative", 1, VPH "PARAM c[6] = { program.local[0..5] };\nADDRESS a;\nTEMP t;\nMUL t, vertex.texcoord[0], 3;\nARL a.x, t.x;\nMOV result.color, c[a.x + 1];\nMOV result.texcoord[0], c[a.x];\nEND\n" },
    { "vp-arl-negative-offset", 1, VPH "PARAM c[6] = { program.local[0..5] };\nADDRESS a;\nTEMP t;\nMAD t, vertex.texcoord[0], 3, 2;\nARL a.x, t.x;\nMOV result.color, c[a.x - 2];\nEND\n" },
    { "vp-generic-attribs", 1, VPH "MOV result.color, vertex.attrib[6];\nMOV result.texcoord[0], vertex.attrib[7];\nMOV result.texcoord[1], vertex.attrib[14];\nEND\n" },
    { "vp-attrib-aliases", 1, VPH "ATTRIB n = vertex.normal;\nATTRIB c = vertex.color.primary;\nOUTPUT oc = result.color;\nMAD oc, n, c, 0.2;\nEND\n" },
    { "vp-results", 1, VPH "MOV result.color.front.primary, vertex.color;\nMOV result.color.front.secondary, vertex.normal;\nMOV result.fogcoord, vertex.fogcoord;\nMOV result.pointsize, 4;\nMOV result.texcoord, vertex.texcoord[1];\nMOV result.texcoord[3], vertex.normal;\nEND\n" },
    { "vp-backcolor", 1, VPH "MOV result.color, vertex.color;\nMOV result.color.back, vertex.normal;\nEND\n" },
    { "vp-exp-log", 1, VPH "TEMP a, b;\nEXP a, vertex.texcoord[0].x;\nABS b, vertex.normal;\nADD b, b, 0.5;\nLOG b, b.y;\nMUL a, a, 0.3;\nMAD result.color, b, 0.3, a;\nEND\n" },
    { "vp-lit", 1, VPH "TEMP a;\nMUL a, vertex.color, {1, 1, 0, 8};\nLIT result.color, a;\nEND\n" },
    { "vp-matrix-rows", 1, VPH "PARAM m[2] = { state.matrix.texture[0].row[1..2] };\nTEMP t;\nDP4 t.x, m[0], vertex.texcoord[0];\nDP4 t.y, m[1], vertex.texcoord[0];\nMOV t.zw, 1;\nMOV result.color, t;\nEND\n" },
    { "vp-matrix-inverse-transpose", 1, VPH "PARAM a[4] = { state.matrix.projection.inverse };\nPARAM b[4] = { state.matrix.modelview.transpose };\nTEMP t;\nDP4 t.x, a[0], vertex.color;\nDP4 t.y, b[3], vertex.color;\nMUL t.xy, t, 0.01;\nMOV t.zw, vertex.color;\nMOV result.color, t;\nEND\n" },
    { "vp-position-invariant", 1, "!!ARBvp1.0\nOPTION ARB_position_invariant;\nMOV result.color, vertex.normal;\nEND\n" },
    { "vp-state-light", 1, VPH "TEMP t;\nADD t, state.light[0].diffuse, state.light[1].position;\nADD t, t, state.lightmodel.ambient;\nMAD result.color, state.material.diffuse, 0.5, t;\nMOV result.texcoord[0], state.light[0].half;\nMOV result.texcoord[1], state.lightprod[1].front.specular;\nEND\n" },
    { "vp-state-texgen-clip-point-fog", 1, VPH "TEMP t;\nADD t, state.texgen[0].eye.s, state.clip[0].plane;\nADD t, t, state.point.attenuation;\nMUL result.color, t, state.fog.params;\nMOV result.texcoord[0], state.texgen[1].object.q;\nEND\n" },
    { "vp-address-write-mask", 1, VPH "PARAM c[4] = { program.env[0..3] };\nADDRESS a0;\nARL a0.x, vertex.texcoord[0].y;\nMOV result.color, c[a0.x+1];\nEND\n" },
    { "vp-same-temp-src-dst", 1, VPH "TEMP t;\nMOV t, vertex.color;\nMAD t, t.wzyx, t, t.y;\nXPD t.xyz, t, t.yzxw;\nMOV result.color, t;\nEND\n" },
};
static void t_syntax(gt_test *t) {
    const syn_t *s = &SYN[t->a];
    if (s->vp) gt_arb_vp(t, s->body, 3, 3, GENATTRIBS); else gt_arb_fp(t, s->body, 3, 0);
}
// every binding the generator uses, alone
static void t_binding(gt_test *t) {
    sb_t b = { 0 };
    if (t->b) { sb(&b, "%sMUL result.color, %s, 0.25;\nEND\n", VPH, VPIN[t->a]); gt_arb_vp(t, b.s, 4, 3, GENATTRIBS); }
    else { sb(&b, "!!ARBfp1.0\nMUL result.color, %s, 0.25;\nEND\n", FPIN[t->a]); gt_arb_fp(t, b.s, 4, t->c); }
    free(b.s);
}

void reg_arb(void) {
    char nm[96];
    for (int i = 0; i < (int)(sizeof SYN / sizeof SYN[0]); i++) { snprintf(nm, sizeof nm, "arb/syntax/%s", SYN[i].name); gt_add(nm, t_syntax, i, 0, 0, 0, NULL); }
    for (int i = 0; i < (int)(sizeof FPIN / sizeof FPIN[0]); i++) if (strchr(FPIN[i], '.')) {
        snprintf(nm, sizeof nm, "arb/binding/fp/%s", FPIN[i]); gt_add(nm, t_binding, i, 0, 0, 0, NULL);
        snprintf(nm, sizeof nm, "arb/binding/fp-texenable/%s", FPIN[i]); gt_add(nm, t_binding, i, 0, 1, 0, NULL);
    }
    for (int i = 0; i < (int)(sizeof VPIN / sizeof VPIN[0]); i++) if (strchr(VPIN[i], '.')) { snprintf(nm, sizeof nm, "arb/binding/vp/%s", VPIN[i]); gt_add(nm, t_binding, i, 1, 0, 0, NULL); }
    for (int a = 0; a < 3; a++) { snprintf(nm, sizeof nm, "arb/lifecycle/%d", a); gt_add(nm, t_lifecycle, a, 0, 0, 0, NULL); }
    gt_add("arb/queries", t_queries, 0, 0, 0, 0, NULL);
    gt_add("arb/invalid-draw/vp", t_invalid, 0, 0, 0, 0, NULL); gt_add("arb/invalid-draw/fp", t_invalid, 1, 0, 0, 0, NULL);
    gt_add("arb/invalid-draw/no-string", t_invalid, 2, 0, 0, 0, NULL);
    for (int s = 1; s <= 3000; s++) { snprintf(nm, sizeof nm, "arb/fp/%d", s); gt_add(nm, t_fp, s, 0, 0, 0, NULL); }
    for (int s = 1; s <= 2000; s++) { snprintf(nm, sizeof nm, "arb/vp/%d", s); gt_add(nm, t_vp, s, 0, 0, 0, NULL); }
    for (int s = 1; s <= 1000; s++) { snprintf(nm, sizeof nm, "arb/pair/%d", s); gt_add(nm, t_pair, s, 0, 0, 0, NULL); }
}

// program text for a seed, for the report (gt -p fp|vp SEED)
char *gt_arb_text(const char *kind, int seed) {
    int inv; if (!strcmp(kind, "vp")) return gen_vp(seed, &inv);
    return gen_fp(seed);
}
