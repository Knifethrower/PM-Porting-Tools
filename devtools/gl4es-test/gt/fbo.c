// Framebuffer objects the way Unity 4 uses them: render textures with depth, grab passes
// (glCopyTexSubImage2D), switching targets, mipmapped render textures, readback and blits.
#include "gt.h"

static GLuint mk_tex(GLenum ifmt, int w, int h, GLenum fmt, GLenum type) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
static GLuint mk_rb(GLenum f, int w, int h) { GLuint r; p_glGenRenderbuffersEXT(1, &r); p_glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, r); p_glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, f, w, h); return r; }
static void scene(int w, int h, int salt) {   // depth-tested overlapping triangles in a w x h target
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, w, 0, h, -1, 1); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0.1f * salt, 0.2f, 0.3f, 0.5f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glBegin(GL_TRIANGLES);
    glColor4f(1, 0, 0, 1); glVertex3f(0, 0, 0.5f); glVertex3f(w, 0, -0.5f); glVertex3f(w / 2.f, h, 0);
    glColor4f(0, 1, 0, 0.5f); glVertex3f(0, h, -0.2f); glVertex3f(w, h * 0.8f, 0.4f); glVertex3f(w * 0.3f, 0, 0.1f);
    glColor4f(0, 0, 1, 0.25f); glVertex3f(w * 0.1f, h * 0.5f, -0.9f); glVertex3f(w * 0.4f, h * 0.45f, -0.9f); glVertex3f(w * 0.25f, h * 0.9f, -0.9f);
    glEnd();
    glDisable(GL_DEPTH_TEST);
}
static void show(GLuint tex, float x, float y, float w, float h) {
    gt_pixel_space();
    glBindTexture(GL_TEXTURE_2D, tex); glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1); gt_quad(x, y, w, h); glDisable(GL_TEXTURE_2D);
}
static void status(gt_test *t) {
    float s = p_glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT;
    gt_vals(t, "complete", 0, &s, 1);
}

static void t_fbo(gt_test *t) {
    GLuint fb, tex = 0, rb = 0, tex2 = 0, fb2 = 0;
    int w = 64, h = 64, tol = 2;
    p_glGenFramebuffersEXT(1, &fb); p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
    switch (t->a) {
    case 0: case 1: case 4: case 5: case 6: case 11:   // RGBA8 colour + depth renderbuffer (1: NPOT)
        if (t->a == 1) { w = 100; h = 60; }
        tex = mk_tex(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE); rb = mk_rb(GL_DEPTH_COMPONENT24, w, h); break;
    case 2: tex = mk_tex(GL_RGB, w, h, GL_RGB, GL_UNSIGNED_BYTE); rb = mk_rb(GL_DEPTH_COMPONENT16, w, h); break;
    case 3: tex = mk_tex(GL_RGB, w, h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5); rb = mk_rb(GL_DEPTH_COMPONENT16, w, h); tol = 9; break;
    case 7: tex = mk_tex(GL_RGBA4, w, h, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4); rb = mk_rb(GL_DEPTH_COMPONENT16, w, h); tol = 18; break;
    case 8: tex = mk_tex(GL_RGBA, w, h, GL_BGRA, GL_UNSIGNED_BYTE); rb = mk_rb(GL_DEPTH24_STENCIL8_EXT, w, h); break;
    case 9: case 10: tex = mk_tex(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE); rb = mk_rb(GL_DEPTH24_STENCIL8_EXT, w, h); break;
    case 12: tex = mk_tex(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE);   // depth texture
             tex2 = mk_tex(GL_DEPTH_COMPONENT24, w, h, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT); break;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    p_glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, tex, 0);
    if (rb) {
        p_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, rb);
        if (t->a == 8 || t->a == 9 || t->a == 10) p_glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, rb);
    }
    if (tex2) p_glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, tex2, 0);
    status(t);
    scene(w, h, 1);
    gt_errcheck(t, "render");
    if (t->a == 4) { gt_img(t, 0, 0, w, h, tol, 0); }   // readback while the FBO is bound
    if (t->a == 9) {   // stencil inside the FBO
        glEnable(GL_STENCIL_TEST); glStencilFunc(GL_ALWAYS, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glColorMask(0, 0, 0, 0); glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(64, 0); glVertex2f(0, 64); glEnd(); glColorMask(1, 1, 1, 1);
        glStencilFunc(GL_EQUAL, 1, 0xff); glColor3f(1, 1, 0); glBegin(GL_QUADS); glVertex2f(0, 0); glVertex2f(64, 0); glVertex2f(64, 64); glVertex2f(0, 64); glEnd();
        glDisable(GL_STENCIL_TEST);
    }
    if (t->a == 5) {   // grab pass: copy part of the FBO into another texture
        tex2 = mk_tex(GL_RGBA, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 8, 8, 4, 4, 40, 40);
    }
    if (t->a == 10) {   // a second FBO, ping-pong, then back to the first, with scissored clears
        p_glGenFramebuffersEXT(1, &fb2); p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb2);
        tex2 = mk_tex(GL_RGBA8, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE); glBindTexture(GL_TEXTURE_2D, 0);
        p_glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, tex2, 0);
        scene(32, 32, 3);
        p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        gt_pixel_space(); glColor3f(1, 0, 1); glBegin(GL_TRIANGLES); glVertex2f(200, 200); glVertex2f(250, 200); glVertex2f(225, 250); glEnd();
        p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
        glEnable(GL_SCISSOR_TEST); glScissor(10, 10, 20, 30); glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT); glDisable(GL_SCISSOR_TEST);
        p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        show(tex2, 120, 140, 64, 64);
    }
    p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    if (t->a == 6) {   // mipmapped render texture, minified
        glBindTexture(GL_TEXTURE_2D, tex); p_glGenerateMipmapEXT(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        show(tex, 150, 10, 16, 16); show(tex, 170, 10, 8, 8); show(tex, 180, 10, 32, 32);
    }
    if (t->a == 11) {   // read the render texture back with glGetTexImage
        uint8_t px[64 * 64 * 4]; glBindTexture(GL_TEXTURE_2D, tex); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        float v[64]; for (int i = 0; i < 64; i++) v[i] = px[i * 251 % (64 * 64 * 4)] / 255.f;
        gt_vals(t, "gettex-samples", 2.5 / 255, v, 64);
    }
    if (t->a == 12) {   // sample the depth texture as luminance
        glBindTexture(GL_TEXTURE_2D, tex2); glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE);
        show(tex2, 150, 150, 64, 64);
    }
    gt_errcheck(t, "use");
    show(tex, 10, 10, 2 * w > 128 ? w : 2 * w, 2 * w > 128 ? h : 2 * h);
    if (tex2 && t->a == 5) show(tex2, 150, 80, 64, 64);
    gt_img(t, 0, 0, 256, 256, tol, 0.002);
    p_glDeleteFramebuffersEXT(1, &fb); if (fb2) p_glDeleteFramebuffersEXT(1, &fb2);
    if (rb) p_glDeleteRenderbuffersEXT(1, &rb);
    glDeleteTextures(1, &tex); if (tex2) glDeleteTextures(1, &tex2);
}
// glBlitFramebufferEXT FBO -> window, scaled and flipped
static void t_blit(gt_test *t) {
    GLuint fb, tex; p_glGenFramebuffersEXT(1, &fb); p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb);
    tex = mk_tex(GL_RGBA8, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE); glBindTexture(GL_TEXTURE_2D, 0);
    p_glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, tex, 0);
    scene(64, 64, 2);
    if (!p_glBlitFramebufferEXT) { gt_skip(t, "no glBlitFramebufferEXT"); p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0); return; }
    p_glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, fb); p_glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, 0);
    if (t->a == 0) p_glBlitFramebufferEXT(0, 0, 64, 64, 10, 10, 74, 74, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    else p_glBlitFramebufferEXT(0, 0, 64, 64, 200, 10, 72, 170, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    p_glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    gt_errcheck(t, "blit");
    gt_img(t, 0, 0, 256, 256, 2, 0.002);
    p_glDeleteFramebuffersEXT(1, &fb); glDeleteTextures(1, &tex);
}
// glCopyTexImage2D/glCopyTexSubImage2D from the window into a texture after other draws (Unity's
// per-frame image-effect copy), then that texture used while the window keeps being drawn to
static void t_grab(gt_test *t) {
    gt_pixel_space();
    glBegin(GL_TRIANGLES); glColor3f(1, 0, 0); glVertex2f(0, 0); glColor3f(0, 1, 0); glVertex2f(256, 0); glColor3f(0, 0, 1); glVertex2f(128, 256); glEnd();
    GLenum fmt = t->a ? GL_BGRA : GL_RGBA;
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, fmt, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (int i = 0; i < 3; i++) {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 256, 256);
        glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(0.9f, 0.8f, 1, 1);
        glBegin(GL_QUADS); glTexCoord2f(0.1f, 0.1f); glVertex2f(20 + i * 10, 20); glTexCoord2f(0.9f, 0.1f); glVertex2f(236, 20 + i * 5);
        glTexCoord2f(0.9f, 0.9f); glVertex2f(236, 236); glTexCoord2f(0.1f, 0.9f); glVertex2f(20, 236 - i * 8); glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    gt_errcheck(t, "grab");
    gt_img(t, 0, 0, 256, 256, 3, 0.002);
    glDeleteTextures(1, &tex);
}

void reg_fbo(void) {
    static const char *n[] = { "rgba8-depth", "npot-100x60", "rgb-depth16", "rgb565", "readback-bound", "copytex-from-fbo", "mipmap-rt",
                               "rgba4", "bgra-depthstencil", "stencil", "pingpong-scissor", "gettex-rt", "depth-texture" };
    char nm[64];
    for (int i = 0; i < 13; i++) { snprintf(nm, sizeof nm, "fbo/%s", n[i]); gt_add(nm, t_fbo, i, 0, 0, 0, NULL); }
    gt_add("fbo/blit", t_blit, 0, 0, 0, 0, NULL); gt_add("fbo/blit-scaled-flipped", t_blit, 1, 0, 0, 0, NULL);
    gt_add("fbo/grab-rgba", t_grab, 0, 0, 0, 0, NULL); gt_add("fbo/grab-bgra", t_grab, 1, 0, 0, 0, NULL);
}
