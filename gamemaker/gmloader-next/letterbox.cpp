/*
 * "render_size": "<w>x<h>" in gmloader.json (e.g. 1280x720): the runner draws into an offscreen <w>x<h>
 * framebuffer that it takes for the window, and every frame is scaled into the largest area of
 * that shape on the screen (black bars elsewhere) with mipmaps, so detail smaller than a screen
 * pixel is averaged instead of skipped. A GMS 1.4 runner given the real window letterboxes the
 * room view but stretches its GUI layer over the whole window on other screen shapes, and draws
 * that GUI straight to the screen with bilinear sampling: on a 720x720 screen a 1280x720 GUI's
 * thin font loses whole strokes.
 */
#include <stdio.h>
#include <stdlib.h>
#include "platform.h"
#include "thunks/khronos/glad.h"
#include "configuration.h"

#ifndef GL_VERTEX_ARRAY_BINDING_OES
#define GL_VERTEX_ARRAY_BINDING_OES 0x85B5
#endif

static int rw, rh, dst_x, dst_y, dst_w, dst_h, win_w, win_h;
static GLuint fbo, tex, depth, prog, vao, vbo;

static const char *vert_src =
    "attribute vec2 aPos;\n"
    "varying vec2 vUV;\n"
    "void main() { vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
static const char *frag_src =
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "varying vec2 vUV;\n"
    "void main() { gl_FragColor = texture2D(uTex, vUV); }\n";

static void setup()
{
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rw, rh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, rw, rh);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fatal_error("render target %dx%d incomplete\n", rw, rh);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);    /* left bound: the runner never binds 0 before its first frame */

    GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &vert_src, NULL);
    glShaderSource(fs, 1, &frag_src, NULL);
    glCompileShader(vs);
    glCompileShader(fs);
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
    glUseProgram(0);

    static const GLfloat quad[] = {-1, -1, -1, 1, 1, -1, 1, 1};
    glGenVertexArraysOES(1, &vao);
    glBindVertexArrayOES(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glBindVertexArrayOES(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    warning("Rendering at %dx%d, shown at %dx%d+%d+%d\n", rw, rh, dst_w, dst_h, dst_x, dst_y);
}

/* The window size the runner is told: the render size when one is set. */
void letterbox_size(int *w, int *h)
{
    static int checked;
    if (!checked) {
        checked = 1;
        const char *s = gmloader_config.render_size.c_str();
        if (sscanf(s, "%dx%d", &rw, &rh) != 2 || rw <= 0 || rh <= 0)
            rw = rh = 0;
    }
    if (!rw)
        return;
    win_w = *w;
    win_h = *h;
    dst_w = win_w;
    dst_h = win_h;
    if ((long long)win_w * rh > (long long)win_h * rw)
        dst_w = win_h * rw / rh;
    else
        dst_h = win_w * rh / rw;
    dst_x = (win_w - dst_w) / 2;
    dst_y = (win_h - dst_h) / 2;
    if (!fbo)
        setup();
    *w = rw;
    *h = rh;
}

/* Draw the frame onto the screen (called before each swap); the runner's GL state is left as it was. */
void letterbox_present()
{
    if (!fbo)
        return;

    GLint fb, program, active, bound_tex, array_buf, vertex_array, viewport[4];
    GLfloat clear[4];
    GLboolean mask[4];
    static const GLenum caps[] = {GL_SCISSOR_TEST, GL_BLEND, GL_DEPTH_TEST, GL_CULL_FACE, GL_STENCIL_TEST, GL_DITHER};
    GLboolean enabled[6];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_tex);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buf);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING_OES, &vertex_array);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    for (int i = 0; i < 6; i++) {
        enabled[i] = glIsEnabled(caps[i]);
        glDisable(caps[i]);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, win_w, win_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(dst_x, dst_y, dst_w, dst_h);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGenerateMipmap(GL_TEXTURE_2D);
    glUseProgram(prog);
    glBindVertexArrayOES(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* start the next frame on a cleared buffer, as after a real swap: the runner does not
     * repaint all of it (a menu stayed visible under a cutscene's translucent parts) */
    GLboolean depth_mask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, rw, rh);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthMask(depth_mask);

    glBindVertexArrayOES(vertex_array);
    glBindBuffer(GL_ARRAY_BUFFER, array_buf);
    glUseProgram(program);
    glBindTexture(GL_TEXTURE_2D, bound_tex);
    glActiveTexture(active);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    for (int i = 0; i < 6; i++)
        if (enabled[i])
            glEnable(caps[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
}

/* GL overrides for libyoyo (thunks/khronos/gles2.cpp): the runner's default framebuffer is ours. */
ABI_ATTR void letterbox_glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    glBindFramebuffer(target, framebuffer == 0 ? fbo : framebuffer);
}

ABI_ATTR void letterbox_glGetIntegerv(GLenum pname, GLint *data)
{
    glGetIntegerv(pname, data);
    if (pname == GL_FRAMEBUFFER_BINDING && fbo && *data == (GLint)fbo)
        *data = 0;
}
