// gl1es.h: the OpenGL 1.x subset Bugdom 2 uses, implemented on OpenGL ES 2.0 (gl1es.c).
// Included by game.h after SDL_opengl.h when USE_GLES2 is set: the desktop GL headers only supply
// the types and enum values, and the macros below send the game's gl* calls to gl1_*. Nothing
// links libGL: the ES functions come from SDL_GL_GetProcAddress.

#pragma once

#include <SDL.h>
#include <SDL_opengl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GL1ES_IMPL
#define glActiveTexture         gl1_ActiveTexture
#define glAlphaFunc             gl1_AlphaFunc
#define glBegin                 gl1_Begin
#define glBindTexture           gl1_BindTexture
#define glBlendFunc             gl1_BlendFunc
#define glClear                 gl1_Clear
#define glClearColor            gl1_ClearColor
#define glClientActiveTexture   gl1_ClientActiveTexture
#define glColor3f               gl1_Color3f
#define glColor4f               gl1_Color4f
#define glColor4fv              gl1_Color4fv
#define glColorMask             gl1_ColorMask
#define glColorMaterial         gl1_ColorMaterial
#define glColorPointer          gl1_ColorPointer
#define glCullFace              gl1_CullFace
#define glDeleteTextures        gl1_DeleteTextures
#define glDepthFunc             gl1_DepthFunc
#define glDepthMask             gl1_DepthMask
#define glDisable               gl1_Disable
#define glDisableClientState    gl1_DisableClientState
#define glDrawElements          gl1_DrawElements
#define glEnable                gl1_Enable
#define glEnableClientState     gl1_EnableClientState
#define glEnd                   gl1_End
#define glFinish                gl1_Finish
#define glFogf                  gl1_Fogf
#define glFogfv                 gl1_Fogfv
#define glFogi                  gl1_Fogi
#define glFrontFace             gl1_FrontFace
#define glFrustum               gl1_Frustum
#define glGenTextures           gl1_GenTextures
#define glGetBooleanv           gl1_GetBooleanv
#define glGetError              gl1_GetError
#define glGetFloatv             gl1_GetFloatv
#define glGetIntegerv           gl1_GetIntegerv
#define glGetString             gl1_GetString
#define glHint                  gl1_Hint
#define glIsEnabled             gl1_IsEnabled
#define glLightModelfv          gl1_LightModelfv
#define glLightModeli           gl1_LightModeli
#define glLightfv               gl1_Lightfv
#define glLineWidth             gl1_LineWidth
#define glLoadIdentity          gl1_LoadIdentity
#define glLoadMatrixf           gl1_LoadMatrixf
#define glMaterialfv            gl1_Materialfv
#define glMatrixMode            gl1_MatrixMode
#define glMultMatrixf           gl1_MultMatrixf
#define glNormal3f              gl1_Normal3f
#define glNormalPointer         gl1_NormalPointer
#define glOrtho                 gl1_Ortho
#define glPixelStorei           gl1_PixelStorei
#define glPolygonMode           gl1_PolygonMode
#define glPopMatrix             gl1_PopMatrix
#define glPushMatrix            gl1_PushMatrix
#define glReadPixels            gl1_ReadPixels
#define glRotatef               gl1_Rotatef
#define glScalef                gl1_Scalef
#define glTexCoord2f            gl1_TexCoord2f
#define glTexCoord2fv           gl1_TexCoord2fv
#define glTexCoordPointer       gl1_TexCoordPointer
#define glTexEnvi               gl1_TexEnvi
#define glTexGeni               gl1_TexGeni
#define glTexImage2D            gl1_TexImage2D
#define glTexParameterf         gl1_TexParameterf
#define glTexParameteri         gl1_TexParameteri
#define glTranslatef            gl1_Translatef
#define glVertex2f              gl1_Vertex2f
#define glVertex3f              gl1_Vertex3f
#define glVertex3fv             gl1_Vertex3fv
#define glVertexPointer         gl1_VertexPointer
#define glViewport              gl1_Viewport
#define SDL_GL_SwapWindow       gl1_SwapWindow
#endif

void gl1_ActiveTexture(GLenum unit);
void gl1_AlphaFunc(GLenum func, GLclampf ref);
void gl1_Begin(GLenum mode);
void gl1_BindTexture(GLenum target, GLuint texture);
void gl1_BlendFunc(GLenum sfactor, GLenum dfactor);
void gl1_Clear(GLbitfield mask);
void gl1_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void gl1_ClientActiveTexture(GLenum unit);
void gl1_Color3f(GLfloat r, GLfloat g, GLfloat b);
void gl1_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void gl1_Color4fv(const GLfloat *v);
void gl1_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void gl1_ColorMaterial(GLenum face, GLenum mode);
void gl1_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void gl1_CullFace(GLenum mode);
void gl1_DeleteTextures(GLsizei n, const GLuint *textures);
void gl1_DepthFunc(GLenum func);
void gl1_DepthMask(GLboolean flag);
void gl1_Disable(GLenum cap);
void gl1_DisableClientState(GLenum array);
void gl1_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void gl1_Enable(GLenum cap);
void gl1_EnableClientState(GLenum array);
void gl1_End(void);
void gl1_Finish(void);
void gl1_Fogf(GLenum pname, GLfloat param);
void gl1_Fogfv(GLenum pname, const GLfloat *params);
void gl1_Fogi(GLenum pname, GLint param);
void gl1_FrontFace(GLenum mode);
void gl1_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void gl1_GenTextures(GLsizei n, GLuint *textures);
void gl1_GetBooleanv(GLenum pname, GLboolean *params);
GLenum gl1_GetError(void);
void gl1_GetFloatv(GLenum pname, GLfloat *params);
void gl1_GetIntegerv(GLenum pname, GLint *params);
const GLubyte *gl1_GetString(GLenum name);
void gl1_Hint(GLenum target, GLenum mode);
GLboolean gl1_IsEnabled(GLenum cap);
void gl1_LightModelfv(GLenum pname, const GLfloat *params);
void gl1_LightModeli(GLenum pname, GLint param);
void gl1_Lightfv(GLenum light, GLenum pname, const GLfloat *params);
void gl1_LineWidth(GLfloat width);
void gl1_LoadIdentity(void);
void gl1_LoadMatrixf(const GLfloat *m);
void gl1_Materialfv(GLenum face, GLenum pname, const GLfloat *params);
void gl1_MatrixMode(GLenum mode);
void gl1_MultMatrixf(const GLfloat *m);
void gl1_Normal3f(GLfloat x, GLfloat y, GLfloat z);
void gl1_NormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr);
void gl1_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void gl1_PixelStorei(GLenum pname, GLint param);
void gl1_PolygonMode(GLenum face, GLenum mode);
void gl1_PopMatrix(void);
void gl1_PushMatrix(void);
void gl1_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels);
void gl1_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void gl1_Scalef(GLfloat x, GLfloat y, GLfloat z);
void gl1_TexCoord2f(GLfloat s, GLfloat t);
void gl1_TexCoord2fv(const GLfloat *v);
void gl1_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void gl1_TexEnvi(GLenum target, GLenum pname, GLint param);
void gl1_TexGeni(GLenum coord, GLenum pname, GLint param);
void gl1_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                    GLenum format, GLenum type, const GLvoid *pixels);
void gl1_TexParameterf(GLenum target, GLenum pname, GLfloat param);
void gl1_TexParameteri(GLenum target, GLenum pname, GLint param);
void gl1_Translatef(GLfloat x, GLfloat y, GLfloat z);
void gl1_Vertex2f(GLfloat x, GLfloat y);
void gl1_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void gl1_Vertex3fv(const GLfloat *v);
void gl1_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void gl1_Viewport(GLint x, GLint y, GLsizei w, GLsizei h);
void gl1_SwapWindow(SDL_Window *window);

// Not in GL 1.x: set up once the context exists; is the depth buffer at window pixel (x, y)
// nearer than depth (0..1)? (ES cannot read the depth buffer.)
void gl1_init(void);
SDL_bool gl1_DepthLess(int x, int y, float depth);

#ifdef __cplusplus
}
#endif
