// gl1es.h: the OpenGL 1.x subset Cube uses, implemented on OpenGL ES 2.0 (gl1es.cpp).
// The game's gl*/glu* calls are renamed to gl1_* by the macros below; GL/gl.h only supplies the
// types and enum values. Nothing links libGL: the ES functions come from SDL_GL_GetProcAddress.

#include <GL/gl.h>
#include <GL/glext.h>

#define glBegin             gl1_Begin
#define glEnd               gl1_End
#define glVertex2i          gl1_Vertex2i
#define glVertex2f          gl1_Vertex2f
#define glVertex3f          gl1_Vertex3f
#define glVertex3d          gl1_Vertex3d
#define glTexCoord2f        gl1_TexCoord2f
#define glTexCoord2d        gl1_TexCoord2d
#define glColor3ub          gl1_Color3ub
#define glColor3f           gl1_Color3f
#define glColor3d           gl1_Color3d
#define glColor3fv          gl1_Color3fv
#define glColor4f           gl1_Color4f
#define glEnable            gl1_Enable
#define glDisable           gl1_Disable
#define glDepthMask         gl1_DepthMask
#define glDepthFunc         gl1_DepthFunc
#define glBlendFunc         gl1_BlendFunc
#define glCullFace          gl1_CullFace
#define glPolygonMode       gl1_PolygonMode
#define glLineWidth         gl1_LineWidth
#define glPolygonOffset     gl1_PolygonOffset
#define glShadeModel        gl1_ShadeModel
#define glHint              gl1_Hint
#define glClearDepth        gl1_ClearDepth
#define glClearColor        gl1_ClearColor
#define glClear             gl1_Clear
#define glViewport          gl1_Viewport
#define glMatrixMode        gl1_MatrixMode
#define glLoadIdentity      gl1_LoadIdentity
#define glPushMatrix        gl1_PushMatrix
#define glPopMatrix         gl1_PopMatrix
#define glRotatef           gl1_Rotatef
#define glRotated           gl1_Rotated
#define glTranslatef        gl1_Translatef
#define glTranslated        gl1_Translated
#define glScalef            gl1_Scalef
#define glOrtho             gl1_Ortho
#define glLoadMatrixd       gl1_LoadMatrixd
#define glGetDoublev        gl1_GetDoublev
#define glGetIntegerv       gl1_GetIntegerv
#define glGetString         gl1_GetString
#define glTexEnvi           gl1_TexEnvi
#define glTexEnvf           gl1_TexEnvf
#define glFogi              gl1_Fogi
#define glFogf              gl1_Fogf
#define glFogfv             gl1_Fogfv
#define glBindTexture       gl1_BindTexture
#define glTexParameteri     gl1_TexParameteri
#define glPixelStorei       gl1_PixelStorei
#define glReadPixels        gl1_ReadPixels
#define glEnableClientState gl1_EnableClientState
#define glVertexPointer     gl1_VertexPointer
#define glColorPointer      gl1_ColorPointer
#define glTexCoordPointer   gl1_TexCoordPointer
#define glDrawArrays        gl1_DrawArrays
#define gluPerspective      gl1u_Perspective
#define gluUnProject        gl1u_UnProject
#define gluBuild2DMipmaps   gl1u_Build2DMipmaps
#define gluScaleImage       gl1u_ScaleImage

void gl1_Begin(GLenum mode);
void gl1_End();
void gl1_Vertex2i(GLint x, GLint y);
void gl1_Vertex2f(GLfloat x, GLfloat y);
void gl1_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void gl1_Vertex3d(GLdouble x, GLdouble y, GLdouble z);
void gl1_TexCoord2f(GLfloat s, GLfloat t);
void gl1_TexCoord2d(GLdouble s, GLdouble t);
void gl1_Color3ub(GLubyte r, GLubyte g, GLubyte b);
void gl1_Color3f(GLfloat r, GLfloat g, GLfloat b);
void gl1_Color3d(GLdouble r, GLdouble g, GLdouble b);
void gl1_Color3fv(const GLfloat *v);
void gl1_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void gl1_Enable(GLenum cap);
void gl1_Disable(GLenum cap);
void gl1_DepthMask(GLboolean flag);
void gl1_DepthFunc(GLenum func);
void gl1_BlendFunc(GLenum sfactor, GLenum dfactor);
void gl1_CullFace(GLenum mode);
void gl1_PolygonMode(GLenum face, GLenum mode);
void gl1_LineWidth(GLfloat width);
void gl1_PolygonOffset(GLfloat factor, GLfloat units);
void gl1_ShadeModel(GLenum mode);
void gl1_Hint(GLenum target, GLenum mode);
void gl1_ClearDepth(GLclampd depth);
void gl1_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void gl1_Clear(GLbitfield mask);
void gl1_Viewport(GLint x, GLint y, GLsizei w, GLsizei h);
void gl1_MatrixMode(GLenum mode);
void gl1_LoadIdentity();
void gl1_PushMatrix();
void gl1_PopMatrix();
void gl1_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void gl1_Rotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void gl1_Translatef(GLfloat x, GLfloat y, GLfloat z);
void gl1_Translated(GLdouble x, GLdouble y, GLdouble z);
void gl1_Scalef(GLfloat x, GLfloat y, GLfloat z);
void gl1_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void gl1_LoadMatrixd(const GLdouble *m);
void gl1_GetDoublev(GLenum pname, GLdouble *params);
void gl1_GetIntegerv(GLenum pname, GLint *params);
const GLubyte *gl1_GetString(GLenum name);
void gl1_TexEnvi(GLenum target, GLenum pname, GLint param);
void gl1_TexEnvf(GLenum target, GLenum pname, GLfloat param);
void gl1_Fogi(GLenum pname, GLint param);
void gl1_Fogf(GLenum pname, GLfloat param);
void gl1_Fogfv(GLenum pname, const GLfloat *params);
void gl1_BindTexture(GLenum target, GLuint texture);
void gl1_TexParameteri(GLenum target, GLenum pname, GLint param);
void gl1_PixelStorei(GLenum pname, GLint param);
void gl1_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels);
void gl1_EnableClientState(GLenum array);
void gl1_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void gl1_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void gl1_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void gl1_DrawArrays(GLenum mode, GLint first, GLsizei count);
void gl1u_Perspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
GLint gl1u_UnProject(GLdouble winx, GLdouble winy, GLdouble winz, const GLdouble *model, const GLdouble *proj,
                     const GLint *view, GLdouble *objx, GLdouble *objy, GLdouble *objz);
GLint gl1u_Build2DMipmaps(GLenum target, GLint internal, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data);
GLint gl1u_ScaleImage(GLenum format, GLint wi, GLint hi, GLenum typei, const void *in, GLint wo, GLint ho, GLenum typeo, void *out);

// Not in GL 1.x: set up after the context exists; flush before a swap; depth under a window pixel.
void gl1_init();
void gl1_flush();
float gl1_readdepth(int x, int y);
