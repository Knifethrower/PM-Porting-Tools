// State that gl4es keeps itself (GLES 2 has no fixed-function state): set it, read it back with glGet*,
// push/pop it with glPushAttrib/glPopAttrib and glPushClientAttrib, and check what a draw then uses.
//   state/get/<name>        set a value, read it back
//   state/pushattrib/<bit>  change state under glPushAttrib(bit), pop, read back + draw
//   state/matrix/<op>       matrix stack operations
#include "gt.h"

typedef struct { const char *name; GLenum pname; int n; void (*set)(void); } q_t;
static void s_color(void) { glColor4f(0.1f, 0.2f, 0.3f, 0.4f); }
static void s_clear(void) { glClearColor(0.5f, 0.25f, 0.125f, 0.75f); }
static void s_blend(void) { glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_COLOR); }
static void s_blendsep(void) { p_glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_DST_COLOR); }
static void s_alpha(void) { glAlphaFunc(GL_GEQUAL, 0.625f); }
static void s_depth(void) { glDepthFunc(GL_GREATER); glDepthRange(0.25, 0.75); }
static void s_fog(void) { float c[4] = { 0.1f, 0.9f, 0.3f, 0.6f }; glFogfv(GL_FOG_COLOR, c); glFogf(GL_FOG_START, 2); glFogf(GL_FOG_END, 9); glFogi(GL_FOG_MODE, GL_EXP2); glFogf(GL_FOG_DENSITY, 0.7f); }
static void s_light(void) { float p[4] = { 1, 2, 3, 0 }; glLightfv(GL_LIGHT1, GL_POSITION, p); float d[4] = { 0.3f, 0.4f, 0.5f, 1 }; glLightfv(GL_LIGHT1, GL_DIFFUSE, d); }
static void s_mat(void) { float a[4] = { 0.7f, 0.6f, 0.5f, 0.4f }; glMaterialfv(GL_FRONT, GL_AMBIENT, a); glMaterialf(GL_FRONT, GL_SHININESS, 33); }
static void s_texenv(void) { glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE); glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGB); glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2); }
static void s_viewport(void) { glViewport(3, 5, 101, 77); glScissor(7, 9, 50, 60); }
static void s_stencil(void) { glStencilFunc(GL_NOTEQUAL, 3, 0x7f); glStencilOp(GL_INCR, GL_INVERT, GL_REPLACE); glStencilMask(0x3c); }
static void s_misc(void) { glPointSize(3.5f); glLineWidth(2.5f); glPolygonOffset(1.5f, 2.5f); glCullFace(GL_FRONT); glFrontFace(GL_CW); glShadeModel(GL_FLAT); }
static void s_mask(void) { glColorMask(1, 0, 1, 0); glDepthMask(0); }
static void s_matrix(void) { glMatrixMode(GL_MODELVIEW); glTranslatef(1, 2, 3); glRotatef(30, 1, 1, 0); glMatrixMode(GL_PROJECTION); glFrustum(-1, 1, -1, 1, 1, 10); glMatrixMode(GL_TEXTURE); glScalef(2, 3, 4); glMatrixMode(GL_MODELVIEW); }
static void s_texmat1(void) { p_glActiveTexture(GL_TEXTURE1); glMatrixMode(GL_TEXTURE); glTranslatef(0.5f, 0.25f, 0); glMatrixMode(GL_MODELVIEW); p_glActiveTexture(GL_TEXTURE0); }
static void s_enable(void) { glEnable(GL_BLEND); glEnable(GL_ALPHA_TEST); glEnable(GL_CULL_FACE); glEnable(GL_FOG); glEnable(GL_LIGHTING); glEnable(GL_LIGHT3); glEnable(GL_NORMALIZE); glEnable(GL_COLOR_MATERIAL); glEnable(GL_TEXTURE_2D); glEnable(GL_SCISSOR_TEST); glEnable(GL_POLYGON_OFFSET_FILL); glEnable(GL_TEXTURE_GEN_S); glEnable(GL_CLIP_PLANE2); }
static void s_texgen(void) { glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP); float p[4] = { 1, 2, 3, 4 }; glTexGenfv(GL_T, GL_OBJECT_PLANE, p); }
static void s_lightmodel(void) { float a[4] = { 0.3f, 0.2f, 0.1f, 1 }; glLightModelfv(GL_LIGHT_MODEL_AMBIENT, a); glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1); glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR); }
static void s_pixelstore(void) { glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ALIGNMENT, 2); glPixelStorei(GL_UNPACK_ROW_LENGTH, 17); glPixelStorei(GL_PACK_SKIP_ROWS, 3); }
static void s_active(void) { p_glActiveTexture(GL_TEXTURE1); p_glClientActiveTexture(GL_TEXTURE1); }
static void s_colormat(void) { glColorMaterial(GL_BACK, GL_SPECULAR); }
static void s_blendeq(void) { p_glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); p_glBlendColor(0.1f, 0.2f, 0.3f, 0.4f); }
static void s_curtex(void) { glTexCoord4f(0.1f, 0.2f, 0.3f, 0.4f); glNormal3f(0.5f, 0.6f, 0.7f); p_glSecondaryColor3f(0.8f, 0.7f, 0.6f); }
static void s_hint(void) { glHint(GL_FOG_HINT, GL_NICEST); glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST); }

static const q_t Q[] = {
    { "CURRENT_COLOR", GL_CURRENT_COLOR, 4, s_color }, { "COLOR_CLEAR_VALUE", GL_COLOR_CLEAR_VALUE, 4, s_clear },
    { "BLEND_SRC", GL_BLEND_SRC, 1, s_blend }, { "BLEND_DST", GL_BLEND_DST, 1, s_blend },
    { "BLEND_SRC_RGB", GL_BLEND_SRC_RGB, 1, s_blendsep }, { "BLEND_DST_ALPHA", GL_BLEND_DST_ALPHA, 1, s_blendsep },
    { "BLEND_EQUATION", GL_BLEND_EQUATION, 1, s_blendeq }, { "BLEND_COLOR", GL_BLEND_COLOR, 4, s_blendeq },
    { "ALPHA_TEST_FUNC", GL_ALPHA_TEST_FUNC, 1, s_alpha }, { "ALPHA_TEST_REF", GL_ALPHA_TEST_REF, 1, s_alpha },
    { "DEPTH_FUNC", GL_DEPTH_FUNC, 1, s_depth }, { "DEPTH_RANGE", GL_DEPTH_RANGE, 2, s_depth },
    { "FOG_COLOR", GL_FOG_COLOR, 4, s_fog }, { "FOG_START", GL_FOG_START, 1, s_fog }, { "FOG_END", GL_FOG_END, 1, s_fog },
    { "FOG_MODE", GL_FOG_MODE, 1, s_fog }, { "FOG_DENSITY", GL_FOG_DENSITY, 1, s_fog },
    { "VIEWPORT", GL_VIEWPORT, 4, s_viewport }, { "SCISSOR_BOX", GL_SCISSOR_BOX, 4, s_viewport },
    { "STENCIL_FUNC", GL_STENCIL_FUNC, 1, s_stencil }, { "STENCIL_REF", GL_STENCIL_REF, 1, s_stencil },
    { "STENCIL_VALUE_MASK", GL_STENCIL_VALUE_MASK, 1, s_stencil }, { "STENCIL_FAIL", GL_STENCIL_FAIL, 1, s_stencil },
    { "STENCIL_WRITEMASK", GL_STENCIL_WRITEMASK, 1, s_stencil },
    { "POINT_SIZE", GL_POINT_SIZE, 1, s_misc }, { "LINE_WIDTH", GL_LINE_WIDTH, 1, s_misc },
    { "POLYGON_OFFSET_FACTOR", GL_POLYGON_OFFSET_FACTOR, 1, s_misc }, { "POLYGON_OFFSET_UNITS", GL_POLYGON_OFFSET_UNITS, 1, s_misc },
    { "CULL_FACE_MODE", GL_CULL_FACE_MODE, 1, s_misc }, { "FRONT_FACE", GL_FRONT_FACE, 1, s_misc }, { "SHADE_MODEL", GL_SHADE_MODEL, 1, s_misc },
    { "COLOR_WRITEMASK", GL_COLOR_WRITEMASK, 4, s_mask }, { "DEPTH_WRITEMASK", GL_DEPTH_WRITEMASK, 1, s_mask },
    { "MODELVIEW_MATRIX", GL_MODELVIEW_MATRIX, 16, s_matrix }, { "PROJECTION_MATRIX", GL_PROJECTION_MATRIX, 16, s_matrix },
    { "TEXTURE_MATRIX", GL_TEXTURE_MATRIX, 16, s_matrix }, { "TRANSPOSE_MODELVIEW_MATRIX", GL_TRANSPOSE_MODELVIEW_MATRIX, 16, s_matrix },
    { "TEXTURE_MATRIX_unit1", GL_TEXTURE_MATRIX, 16, s_texmat1 },
    { "MODELVIEW_STACK_DEPTH", GL_MODELVIEW_STACK_DEPTH, 1, s_matrix }, { "MATRIX_MODE", GL_MATRIX_MODE, 1, s_matrix },
    { "ACTIVE_TEXTURE", GL_ACTIVE_TEXTURE, 1, s_active }, { "CLIENT_ACTIVE_TEXTURE", GL_CLIENT_ACTIVE_TEXTURE, 1, s_active },
    { "LIGHT_MODEL_AMBIENT", GL_LIGHT_MODEL_AMBIENT, 4, s_lightmodel }, { "LIGHT_MODEL_TWO_SIDE", GL_LIGHT_MODEL_TWO_SIDE, 1, s_lightmodel },
    { "LIGHT_MODEL_COLOR_CONTROL", GL_LIGHT_MODEL_COLOR_CONTROL, 1, s_lightmodel },
    { "UNPACK_ALIGNMENT", GL_UNPACK_ALIGNMENT, 1, s_pixelstore }, { "PACK_ALIGNMENT", GL_PACK_ALIGNMENT, 1, s_pixelstore },
    { "UNPACK_ROW_LENGTH", GL_UNPACK_ROW_LENGTH, 1, s_pixelstore }, { "PACK_SKIP_ROWS", GL_PACK_SKIP_ROWS, 1, s_pixelstore },
    { "COLOR_MATERIAL_FACE", GL_COLOR_MATERIAL_FACE, 1, s_colormat }, { "COLOR_MATERIAL_PARAMETER", GL_COLOR_MATERIAL_PARAMETER, 1, s_colormat },
    { "CURRENT_TEXTURE_COORDS", GL_CURRENT_TEXTURE_COORDS, 4, s_curtex }, { "CURRENT_NORMAL", GL_CURRENT_NORMAL, 3, s_curtex },
    { "CURRENT_SECONDARY_COLOR", GL_CURRENT_SECONDARY_COLOR, 4, s_curtex },
    { "FOG_HINT", GL_FOG_HINT, 1, s_hint }, { "PERSPECTIVE_CORRECTION_HINT", GL_PERSPECTIVE_CORRECTION_HINT, 1, s_hint },
};
#define NQ ((int)(sizeof Q / sizeof Q[0]))
static const struct { const char *name; GLenum cap; } CAPS[] = {
    { "BLEND", GL_BLEND }, { "ALPHA_TEST", GL_ALPHA_TEST }, { "CULL_FACE", GL_CULL_FACE }, { "FOG", GL_FOG }, { "LIGHTING", GL_LIGHTING },
    { "LIGHT3", GL_LIGHT3 }, { "NORMALIZE", GL_NORMALIZE }, { "COLOR_MATERIAL", GL_COLOR_MATERIAL }, { "TEXTURE_2D", GL_TEXTURE_2D },
    { "SCISSOR_TEST", GL_SCISSOR_TEST }, { "POLYGON_OFFSET_FILL", GL_POLYGON_OFFSET_FILL }, { "TEXTURE_GEN_S", GL_TEXTURE_GEN_S },
    { "CLIP_PLANE2", GL_CLIP_PLANE2 }, { "DEPTH_TEST", GL_DEPTH_TEST }, { "STENCIL_TEST", GL_STENCIL_TEST }, { "DITHER", GL_DITHER },
};
#define NCAPS ((int)(sizeof CAPS / sizeof CAPS[0]))

static void read_q(gt_test *t, const char *pfx, int i) {
    float v[16] = { 0 }; char k[96];
    glGetFloatv(Q[i].pname, v);
    snprintf(k, sizeof k, "%s-float", pfx); gt_vals(t, k, 1e-4, v, Q[i].n);
    GLint iv[16] = { 0 }; glGetIntegerv(Q[i].pname, iv);
    float fi[16]; for (int j = 0; j < Q[i].n; j++) fi[j] = (float)iv[j];
    // integer queries of colours are scaled to the int range: compare as info only
    snprintf(k, sizeof k, "%s-int", pfx);
    if (Q[i].pname == GL_CURRENT_COLOR || Q[i].pname == GL_COLOR_CLEAR_VALUE || Q[i].pname == GL_FOG_COLOR || Q[i].pname == GL_BLEND_COLOR
        || Q[i].pname == GL_LIGHT_MODEL_AMBIENT || Q[i].pname == GL_CURRENT_SECONDARY_COLOR || Q[i].pname == GL_CURRENT_NORMAL)
        gt_vals(t, k, 2.0 * 2147483647.0 / 255, fi, Q[i].n);
    else gt_vals(t, k, 0.5, fi, Q[i].n);
    GLboolean bv[16] = { 0 }; glGetBooleanv(Q[i].pname, bv);
    float fb[16]; for (int j = 0; j < Q[i].n; j++) fb[j] = bv[j] ? 1 : 0;
    snprintf(k, sizeof k, "%s-bool", pfx); gt_vals(t, k, 0, fb, Q[i].n);
}
static void t_get(gt_test *t) {
    int i = t->a;
    read_q(t, "default", i);
    Q[i].set();
    gt_errcheck(t, "set");
    read_q(t, "set", i);
}
static void t_caps(gt_test *t) {
    float v[NCAPS];
    for (int i = 0; i < NCAPS; i++) v[i] = glIsEnabled(CAPS[i].cap);
    gt_vals(t, "default", 0, v, NCAPS);
    s_enable();
    for (int i = 0; i < NCAPS; i++) v[i] = glIsEnabled(CAPS[i].cap);
    gt_vals(t, "enabled", 0, v, NCAPS);
    for (int i = 0; i < NCAPS; i++) { GLboolean b = 0; glGetBooleanv(CAPS[i].cap, &b); v[i] = b; }
    gt_vals(t, "getboolean", 0, v, NCAPS);
}
// glPushAttrib(bit), change everything, glPopAttrib: every query must be back to its value before the push
static const struct { const char *n; GLbitfield b; } BITS[] = {
    { "CURRENT", GL_CURRENT_BIT }, { "POINT", GL_POINT_BIT }, { "LINE", GL_LINE_BIT }, { "POLYGON", GL_POLYGON_BIT },
    { "LIGHTING", GL_LIGHTING_BIT }, { "FOG", GL_FOG_BIT }, { "DEPTH_BUFFER", GL_DEPTH_BUFFER_BIT }, { "VIEWPORT", GL_VIEWPORT_BIT },
    { "TRANSFORM", GL_TRANSFORM_BIT }, { "ENABLE", GL_ENABLE_BIT }, { "COLOR_BUFFER", GL_COLOR_BUFFER_BIT }, { "HINT", GL_HINT_BIT },
    { "STENCIL_BUFFER", GL_STENCIL_BUFFER_BIT }, { "TEXTURE", GL_TEXTURE_BIT }, { "SCISSOR", GL_SCISSOR_BIT }, { "ALL", GL_ALL_ATTRIB_BITS },
};
static void snapshot(gt_test *t, const char *pfx) {
    char k[96];
    for (int i = 0; i < NQ; i++) {
        if (Q[i].pname == GL_UNPACK_ALIGNMENT || Q[i].pname == GL_PACK_ALIGNMENT || Q[i].pname == GL_UNPACK_ROW_LENGTH || Q[i].pname == GL_PACK_SKIP_ROWS
            || Q[i].pname == GL_CLIENT_ACTIVE_TEXTURE || Q[i].pname == GL_MODELVIEW_MATRIX || Q[i].pname == GL_PROJECTION_MATRIX
            || Q[i].pname == GL_TEXTURE_MATRIX || Q[i].pname == GL_TRANSPOSE_MODELVIEW_MATRIX || Q[i].pname == GL_MODELVIEW_STACK_DEPTH) continue;
        float v[16] = { 0 }; glGetFloatv(Q[i].pname, v);
        snprintf(k, sizeof k, "%s-%s", pfx, Q[i].name); gt_vals(t, k, 1e-4, v, Q[i].n);
    }
    float v[NCAPS]; for (int i = 0; i < NCAPS; i++) v[i] = glIsEnabled(CAPS[i].cap);
    snprintf(k, sizeof k, "%s-enables", pfx); gt_vals(t, k, 0, v, NCAPS);
    float l[4]; glGetLightfv(GL_LIGHT1, GL_DIFFUSE, l); snprintf(k, sizeof k, "%s-light1-diffuse", pfx); gt_vals(t, k, 1e-4, l, 4);
    glGetMaterialfv(GL_FRONT, GL_AMBIENT, l); snprintf(k, sizeof k, "%s-mat-ambient", pfx); gt_vals(t, k, 1e-4, l, 4);
    GLint te = 0; glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &te); l[0] = (float)te;
    glGetTexEnviv(GL_TEXTURE_ENV, GL_COMBINE_RGB, &te); l[1] = (float)te; glGetTexEnvfv(GL_TEXTURE_ENV, GL_RGB_SCALE, &l[2]);
    snprintf(k, sizeof k, "%s-texenv", pfx); gt_vals(t, k, 1e-4, l, 3);
    GLint tg = 0; glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &tg); l[0] = (float)tg; snprintf(k, sizeof k, "%s-texgen", pfx); gt_vals(t, k, 0, l, 1);
}
static void set_all(void) {
    s_color(); s_clear(); s_blend(); s_blendeq(); s_alpha(); s_depth(); s_fog(); s_light(); s_mat(); s_texenv(); s_viewport();
    s_stencil(); s_misc(); s_mask(); s_enable(); s_texgen(); s_lightmodel(); s_colormat(); s_curtex(); s_hint();
}
static void t_pushattrib(gt_test *t) {
    GLbitfield bit = (GLbitfield)t->a;
    snapshot(t, "before");
    glPushAttrib(bit);
    set_all();
    glPopAttrib();
    gt_errcheck(t, "pushpop");
    snapshot(t, "after");
    // and a draw with the restored state must look like one with no push at all
    glColorMask(1, 1, 1, 1); glDepthMask(1);
    gt_pixel_space();
    glColor4f(1, 0.5f, 0.25f, 1); glBegin(GL_TRIANGLES); glVertex2f(10, 10); glVertex2f(200, 30); glVertex2f(60, 220); glEnd();
    gt_img(t, 0, 0, 256, 256, 2, 0.002);
}
static void t_pushclient(gt_test *t) {
    static float va[8] = { 10, 10, 200, 20, 100, 200, 30, 150 };
    glVertexPointer(2, GL_FLOAT, 0, va); glEnableClientState(GL_VERTEX_ARRAY);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glDisableClientState(GL_VERTEX_ARRAY); glPixelStorei(GL_UNPACK_ALIGNMENT, 8); glEnableClientState(GL_COLOR_ARRAY);
    static float other[8]; glVertexPointer(3, GL_SHORT, 4, other);
    glPopClientAttrib();
    float v[5]; GLint i;
    v[0] = glIsEnabled(GL_VERTEX_ARRAY); v[1] = glIsEnabled(GL_COLOR_ARRAY); glGetIntegerv(GL_UNPACK_ALIGNMENT, &i); v[2] = i;
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &i); v[3] = i; void *p = NULL; glGetPointerv(GL_VERTEX_ARRAY_POINTER, &p); v[4] = p == va;
    gt_vals(t, "client-state", 0, v, 5);
    glColor4f(0.2f, 1, 0.4f, 1); glDrawArrays(GL_TRIANGLE_FAN, 0, 4); glDisableClientState(GL_VERTEX_ARRAY);
    gt_img(t, 0, 0, 256, 256, 2, 0.002);
}
// matrix stack: a = op
static void t_matrix(gt_test *t) {
    float m[16];
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    switch (t->a) {
    case 0: glTranslatef(10, 20, 0); glPushMatrix(); glRotatef(45, 0, 0, 1); glScalef(2, 0.5f, 1); glPopMatrix(); break;
    case 1: { float a[16] = { 1, 0.1f, 0, 0, 0.2f, 1, 0, 0, 0, 0, 1, 0, 5, 6, 0, 1 }; glLoadMatrixf(a); glMultMatrixf(a); break; }
    case 2: { float a[16] = { 1, 0.1f, 0, 3, 0.2f, 1, 0, 4, 0, 0, 1, 0, 0, 0, 0, 1 }; p_glLoadTransposeMatrixf(a); break; }
    case 3: glOrtho(-3, 5, -2, 7, -1, 9); glFrustum(-1, 2, -1, 3, 1, 50); break;
    case 4: for (int i = 0; i < 20; i++) { glPushMatrix(); glTranslatef(1, 0, 0); } for (int i = 0; i < 10; i++) glPopMatrix(); break;
    case 5: glMatrixMode(GL_TEXTURE); p_glActiveTexture(GL_TEXTURE1); glRotatef(30, 0, 0, 1); glPushMatrix(); glLoadIdentity(); p_glActiveTexture(GL_TEXTURE0); glScalef(2, 2, 2);
            p_glActiveTexture(GL_TEXTURE1); glPopMatrix(); glGetFloatv(GL_TEXTURE_MATRIX, m); gt_vals(t, "tex1", 1e-5, m, 16); p_glActiveTexture(GL_TEXTURE0); glMatrixMode(GL_MODELVIEW); break;
    case 6: glRotatef(33, 0.3f, 0.5f, 0.8f); glScalef(-1, 2, 3); break;
    }
    glGetFloatv(GL_MODELVIEW_MATRIX, m); gt_vals(t, "modelview", 1e-4, m, 16);
    GLint d; glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &d); float fd = d; gt_vals(t, "depth", 0, &fd, 1);
    glGetFloatv(GL_PROJECTION_MATRIX, m); gt_vals(t, "projection", 1e-4, m, 16);
}
// display lists recording state and matrix changes, nested calls, glListBase/glCallLists
static void t_lists(gt_test *t) {
    GLuint base = glGenLists(3);
    glNewList(base, GL_COMPILE); glColor3f(1, 0, 0); glTranslatef(30, 0, 0); glEndList();
    glNewList(base + 1, GL_COMPILE); glPushMatrix(); glCallList(base); glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(40, 0); glVertex2f(20, 40); glEnd(); glPopMatrix(); glEndList();
    glNewList(base + 2, GL_COMPILE_AND_EXECUTE); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glColor3f(0, 0.5f, 0); glEndList();
    glTranslatef(10, 10, 0); glCallList(base + 1);
    glTranslatef(0, 60, 0); glCallList(base + 1);
    glListBase(base); GLubyte ids[3] = { 0, 1, 1 }; glCallLists(3, GL_UNSIGNED_BYTE, ids);
    glDisable(GL_BLEND);
    float c[4]; glGetFloatv(GL_CURRENT_COLOR, c); gt_vals(t, "color-after", 1e-4, c, 4);
    float m[16]; glGetFloatv(GL_MODELVIEW_MATRIX, m); gt_vals(t, "modelview-after", 1e-4, m, 16);
    gt_img(t, 0, 0, 256, 256, 2, 0.002);
    glDeleteLists(base, 3);
}
// glBitmap / glDrawPixels / glRasterPos / glCopyPixels (old 2D paths some engines still use)
static void t_raster(gt_test *t) {
    static GLubyte bm[32]; for (int i = 0; i < 32; i++) bm[i] = (GLubyte)(0xA5 ^ (i * 37));
    glColor3f(1, 1, 0);
    switch (t->a) {
    case 0: glRasterPos2i(20, 20); glBitmap(16, 16, 0, 0, 20, 0, bm); glBitmap(16, 16, 4, 4, 0, 0, bm); break;
    case 1: { uint8_t px[24 * 20 * 4]; for (int i = 0; i < (int)sizeof px; i++) px[i] = (uint8_t)(i * 13); glRasterPos2i(30, 40);
              glDrawPixels(24, 20, GL_RGBA, GL_UNSIGNED_BYTE, px); glPixelZoom(2, -1.5f); glRasterPos2i(100, 120); glDrawPixels(24, 20, GL_RGBA, GL_UNSIGNED_BYTE, px); glPixelZoom(1, 1); break; }
    case 2: { uint8_t px[20 * 10 * 3]; for (int i = 0; i < (int)sizeof px; i++) px[i] = (uint8_t)(i * 7); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
              glRasterPos2i(5, 200); glDrawPixels(20, 10, GL_BGR, GL_UNSIGNED_BYTE, px); glRasterPos2i(50, 200); glDrawPixels(20, 10, GL_LUMINANCE, GL_UNSIGNED_BYTE, px); break; }
    case 3: glBegin(GL_TRIANGLES); glColor3f(1, 0, 0); glVertex2f(10, 10); glColor3f(0, 1, 0); glVertex2f(80, 10); glColor3f(0, 0, 1); glVertex2f(40, 70); glEnd();
            glRasterPos2i(120, 100); glCopyPixels(10, 10, 70, 60, GL_COLOR); break;
    case 4: p_glWindowPos2i(60, 60); glBitmap(16, 16, 0, 0, 0, 0, bm); glRasterPos2i(-5, 30); glBitmap(16, 16, 0, 0, 0, 0, bm); break;
    }
    gt_errcheck(t, "raster");
    float rp[4]; glGetFloatv(GL_CURRENT_RASTER_POSITION, rp); gt_vals(t, "rasterpos", 1e-3, rp, 4);
    gt_img(t, 0, 0, 256, 256, 2, 0.002);
}
// strings and limits the game sees (info only: different by design) + extensions gl4es claims and must then honour
static void t_strings(gt_test *t) {
    gt_info(t, "vendor %s", glGetString(GL_VENDOR)); gt_info(t, "renderer %s", glGetString(GL_RENDERER));
    gt_info(t, "version %s", glGetString(GL_VERSION));
    const char *e = (const char *)glGetString(GL_EXTENSIONS);
    if (e) { const char *p = e; char w[96]; while (sscanf(p, "%95s", w) == 1) { gt_info(t, "ext %s", w); p = strstr(p, w) + strlen(w); } }
    static const struct { const char *n; GLenum e; } L[] = { { "MAX_TEXTURE_SIZE", GL_MAX_TEXTURE_SIZE }, { "MAX_TEXTURE_UNITS", GL_MAX_TEXTURE_UNITS },
        { "MAX_LIGHTS", GL_MAX_LIGHTS }, { "MAX_CLIP_PLANES", GL_MAX_CLIP_PLANES }, { "MAX_MODELVIEW_STACK_DEPTH", GL_MAX_MODELVIEW_STACK_DEPTH },
        { "MAX_TEXTURE_IMAGE_UNITS", GL_MAX_TEXTURE_IMAGE_UNITS }, { "MAX_VERTEX_ATTRIBS", GL_MAX_VERTEX_ATTRIBS }, { "RED_BITS", GL_RED_BITS },
        { "ALPHA_BITS", GL_ALPHA_BITS }, { "DEPTH_BITS", GL_DEPTH_BITS }, { "STENCIL_BITS", GL_STENCIL_BITS }, { "MAX_VIEWPORT_DIMS", GL_MAX_VIEWPORT_DIMS } };
    for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) { GLint v[2] = { -1, -1 }; glGetIntegerv(L[i].e, v); gt_info(t, "limit %s %d %d", L[i].n, v[0], v[1]); }
    // the framebuffer must keep alpha (everything else here reads it back)
    glClearColor(0.1f, 0.2f, 0.3f, 0.4f); glClear(GL_COLOR_BUFFER_BIT);
    gt_img(t, 0, 0, 8, 8, 1, 0);
}

void reg_state(void) {
    char nm[96];
    for (int i = 0; i < NQ; i++) { snprintf(nm, sizeof nm, "state/get/%s", Q[i].name); gt_add(nm, t_get, i, 0, 0, 0, NULL); }
    gt_add("state/caps", t_caps, 0, 0, 0, 0, NULL);
    for (unsigned i = 0; i < sizeof BITS / sizeof BITS[0]; i++) { snprintf(nm, sizeof nm, "state/pushattrib/%s", BITS[i].n); gt_add(nm, t_pushattrib, (int)BITS[i].b, 0, 0, 0, NULL); }
    gt_add("state/pushclientattrib", t_pushclient, 0, 0, 0, 0, NULL);
    for (int i = 0; i < 7; i++) { snprintf(nm, sizeof nm, "state/matrix/%d", i); gt_add(nm, t_matrix, i, 0, 0, 0, NULL); }
    gt_add("state/displaylists", t_lists, 0, 0, 0, 0, NULL);
    for (int i = 0; i < 5; i++) { snprintf(nm, sizeof nm, "state/raster/%d", i); gt_add(nm, t_raster, i, 0, 0, 0, NULL); }
    gt_add("aaa/strings", t_strings, 0, 0, 0, 0, NULL);
}
