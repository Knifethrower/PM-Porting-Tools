// Desktop GLSL 1.10/1.20 that gl4es rewrites into GLSL ES 1.00 (shaderconv.c): built-in uniforms and
// attributes, fixed-function state read from shaders, desktop-only syntax. Each case is a shader pair drawn
// over a mesh with every built-in input varying.
#include "gt.h"

typedef struct { const char *name, *vs, *fs; } sh_t;
#define VS_STD "void main(){ gl_Position = ftransform(); gl_FrontColor = gl_Color; gl_TexCoord[0] = gl_MultiTexCoord0; gl_TexCoord[1] = gl_MultiTexCoord1; gl_FrontSecondaryColor = gl_SecondaryColor; gl_FogFragCoord = gl_FogCoord; }"
#define FS_STD "void main(){ gl_FragColor = gl_Color; }"
static const sh_t SH[] = {
    { "ftransform", VS_STD, FS_STD },
    { "mvp-matrix", "void main(){ gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; gl_FrontColor = gl_Color.bgra; }", FS_STD },
    { "mv-then-proj", "void main(){ vec4 e = gl_ModelViewMatrix * gl_Vertex; gl_Position = gl_ProjectionMatrix * e; gl_FrontColor = vec4(fract(e.xyz*0.01), 1.0); }", FS_STD },
    { "normal-matrix", "void main(){ gl_Position = ftransform(); vec3 n = normalize(gl_NormalMatrix * gl_Normal); gl_FrontColor = vec4(n*0.5+0.5, 1.0); }", FS_STD },
    { "inverse-transpose", "void main(){ gl_Position = ftransform(); vec4 a = gl_ModelViewMatrixInverse * vec4(gl_Normal,0.0); vec4 b = gl_ModelViewProjectionMatrixTranspose[1]; gl_FrontColor = vec4(fract(a.xy), fract(b.x*10.0+b.w), 1.0); }", FS_STD },
    { "texture-matrix", "void main(){ gl_Position = ftransform(); gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0; gl_TexCoord[1] = gl_TextureMatrix[1] * gl_MultiTexCoord1; }",
      "uniform sampler2D t0, t1; void main(){ gl_FragColor = texture2D(t0, gl_TexCoord[0].xy) * 0.6 + texture2D(t1, gl_TexCoord[1].st) * 0.4; }" },
    { "texture2DProj", VS_STD, "uniform sampler2D t0; void main(){ gl_FragColor = texture2DProj(t0, vec4(gl_TexCoord[0].xy, 0.0, 1.5)) + texture2DProj(t0, vec3(gl_TexCoord[1].xy, 2.0)) * 0.5; }" },
    { "texture2D-bias", VS_STD, "uniform sampler2D t0; void main(){ gl_FragColor = texture2D(t0, gl_TexCoord[0].xy, 1.5); }" },
    { "texture2DLod-vs", "uniform sampler2D t0; void main(){ gl_Position = ftransform(); gl_FrontColor = texture2DLod(t0, gl_MultiTexCoord0.xy, 0.0); }", FS_STD },
    { "lightsource", "void main(){ gl_Position = ftransform(); vec3 n = normalize(gl_NormalMatrix*gl_Normal); vec3 l = normalize(gl_LightSource[0].position.xyz); float d = max(dot(n,l),0.0);"
      " gl_FrontColor = gl_LightSource[0].ambient * gl_FrontMaterial.ambient + d * gl_LightSource[0].diffuse * gl_FrontMaterial.diffuse + gl_LightModel.ambient; }", FS_STD },
    { "lightproducts", "void main(){ gl_Position = ftransform(); gl_FrontColor = gl_FrontLightProduct[0].diffuse + gl_FrontLightModelProduct.sceneColor * 0.5 + vec4(gl_FrontMaterial.shininess/128.0); }", FS_STD },
    { "spot+attenuation", "void main(){ gl_Position = ftransform(); gl_FrontColor = vec4(gl_LightSource[1].spotDirection*0.5+0.5, gl_LightSource[1].spotCosCutoff) + vec4(gl_LightSource[1].linearAttenuation); }", FS_STD },
    { "fog-builtins", VS_STD, "void main(){ float f = clamp((gl_Fog.end - gl_FogFragCoord) * gl_Fog.scale, 0.0, 1.0); gl_FragColor = mix(gl_Fog.color, gl_Color, f); }" },
    { "secondary+frontfacing", VS_STD, "void main(){ gl_FragColor = gl_FrontFacing ? gl_Color + gl_SecondaryColor : vec4(1.0,0.0,1.0,1.0); }" },
    { "fragcoord", VS_STD, "void main(){ gl_FragColor = vec4(fract(gl_FragCoord.xy / 37.0), gl_FragCoord.z, 1.0); }" },
    { "fragdata0", VS_STD, "void main(){ gl_FragData[0] = gl_Color.gbra; }" },
    { "discard", VS_STD, "void main(){ if (gl_Color.r + gl_Color.g < 0.8) discard; gl_FragColor = gl_Color; }" },
    { "texenv-color+clipplane", "void main(){ gl_Position = ftransform(); gl_FrontColor = gl_TextureEnvColor[0] + vec4(gl_ClipPlane[0].xy*0.1, 0.0, 0.0); }", FS_STD },
    { "point-builtins", "void main(){ gl_Position = ftransform(); gl_FrontColor = vec4(gl_Point.size/10.0, gl_Point.distanceLinearAttenuation*10.0, gl_DepthRange.near, gl_DepthRange.far); }", FS_STD },
    { "eye-planes", "void main(){ gl_Position = ftransform(); gl_FrontColor = vec4(fract(dot(gl_EyePlaneS[0], gl_Vertex)), fract(dot(gl_ObjectPlaneT[0], gl_Vertex)), 0.5, 1.0); }", FS_STD },
    // GLSL 1.20 / desktop-only syntax
    { "v120-implicit-int-float", "#version 120\nvoid main(){ gl_Position = ftransform(); float a = 2; vec4 c = gl_Color * 1; gl_FrontColor = c / a + 0.25; }", FS_STD },
    { "v120-array-constructor", "#version 120\nvoid main(){ gl_Position = ftransform(); float w[3] = float[3](0.2, 0.5, 0.3); gl_FrontColor = vec4(w[0]*gl_Color.r, w[1], w[2]*gl_Color.b, 1.0); }", FS_STD },
    { "v120-const-array-length", "#version 120\nconst vec3 k[2] = vec3[](vec3(0.1,0.2,0.3), vec3(0.6,0.5,0.4)); void main(){ gl_Position = ftransform(); gl_FrontColor = vec4(k[1] * float(k.length()) * 0.5, 1.0); }", FS_STD },
    { "v120-mat-nonsquare", "#version 120\nvoid main(){ gl_Position = ftransform(); mat2x3 m = mat2x3(0.1,0.2,0.3, 0.4,0.5,0.6); gl_FrontColor = vec4(m * gl_Color.rg, 1.0); }", FS_STD },
    { "v120-outerproduct-transpose", "#version 120\nvoid main(){ gl_Position = ftransform(); mat3 m = outerProduct(gl_Color.rgb, vec3(0.5,0.2,0.9)); mat3 t = transpose(m); gl_FrontColor = vec4(t[0], 1.0); }", FS_STD },
    { "v120-invariant-centroid", "#version 120\ninvariant gl_Position; centroid varying vec4 c; void main(){ gl_Position = ftransform(); c = gl_Color; }", "#version 120\ncentroid varying vec4 c; void main(){ gl_FragColor = c.wzyx; }" },
    { "float-suffix-f", "void main(){ gl_Position = ftransform(); gl_FrontColor = gl_Color * 0.5f + vec4(0.25f); }", FS_STD },
    { "uniform-array+struct", "struct L { vec4 c; float k; }; uniform L lights[2]; uniform vec4 arr[3]; void main(){ gl_Position = ftransform(); gl_FrontColor = lights[1].c * lights[0].k + arr[2]; }", FS_STD },
    { "loops+functions", "float f(float x){ float s = 0.0; for (int i = 0; i < 6; i++) s += sin(x * float(i)); return s; } void main(){ gl_Position = ftransform(); gl_FrontColor = vec4(fract(f(gl_Color.r)), fract(f(gl_Color.g)), 0.5, 1.0); }", FS_STD },
    { "while+break", VS_STD, "void main(){ vec4 c = gl_Color; int n = 0; while (c.r < 2.0) { c.r += 0.3; n++; if (n > 5) break; } gl_FragColor = vec4(fract(c.r), float(n)/6.0, c.b, 1.0); }" },
    { "precision-free-fs", VS_STD, "uniform float scale; void main(){ vec3 v = gl_Color.rgb * scale; gl_FragColor = vec4(pow(v, vec3(2.2)), 1.0); }" },
    { "builtin-functions", VS_STD, "void main(){ vec4 c = gl_Color; gl_FragColor = vec4(smoothstep(0.2,0.8,c.r), step(0.5,c.g)*0.5+faceforward(vec3(1.0),vec3(c.b-0.5),vec3(0.0,0.0,1.0)).x*0.25, mod(c.b*7.0, 1.0), sign(c.a-0.5)*0.5+0.5); }" },
    { "matrix-uniform-transpose", "uniform mat4 M; void main(){ gl_Position = gl_ModelViewProjectionMatrix * (M * gl_Vertex); gl_FrontColor = gl_Color; }", FS_STD },
    { "attribute-generic", "attribute vec4 extra; void main(){ gl_Position = ftransform(); gl_FrontColor = extra; }", FS_STD },
    { "shadow2D", VS_STD, "uniform sampler2DShadow s; void main(){ gl_FragColor = vec4(shadow2D(s, vec3(gl_TexCoord[0].xy, 0.5)).r); }" },
    { "samplerCube", "void main(){ gl_Position = ftransform(); gl_TexCoord[0] = vec4(gl_Normal, 0.0); }", "uniform samplerCube c; void main(){ gl_FragColor = textureCube(c, gl_TexCoord[0].xyz); }" },
    { "gl_MaxLights-loop", "void main(){ gl_Position = ftransform(); vec4 a = vec4(0.0); for (int i = 0; i < 2; i++) a += gl_LightSource[i].diffuse; gl_FrontColor = a * 0.5 + float(gl_MaxLights)/64.0; }", FS_STD },
    { "backcolor-twoside", "void main(){ gl_Position = ftransform(); gl_FrontColor = gl_Color; gl_BackColor = vec4(1.0) - gl_Color; }", FS_STD },
};
#define NSH ((int)(sizeof SH / sizeof SH[0]))

static GLuint compile(gt_test *t, GLenum type, const char *src, int *ok) {
    GLuint s = p_glCreateShader(type); p_glShaderSource(s, 1, &src, NULL); p_glCompileShader(s);
    GLint c = 0; p_glGetShaderiv(s, GL_COMPILE_STATUS, &c);
    if (!c) { char log[300] = ""; p_glGetShaderInfoLog(s, sizeof log, NULL, log); for (char *p = log; *p; p++) if (*p == '\n') *p = ' '; gt_info(t, "compile-log %s", log); }
    *ok = c; return s;
}
static GLuint tex2d(int seed) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    uint8_t px[32 * 32 * 4]; for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) for (int c = 0; c < 4; c++)
        px[4 * (y * 32 + x) + c] = (uint8_t)(127.5f + 127.f * sinf(seed + x * 0.2f * (c + 1) + y * 0.15f * (4 - c)));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    p_glGenerateMipmapEXT(GL_TEXTURE_2D);
    return t;
}
static void t_glsl(gt_test *t) {
    const sh_t *s = &SH[t->a];
    int okv, okf; GLuint vs = compile(t, GL_VERTEX_SHADER, s->vs, &okv), fs = compile(t, GL_FRAGMENT_SHADER, s->fs, &okf);
    GLuint p = p_glCreateProgram(); p_glAttachShader(p, vs); p_glAttachShader(p, fs);
    p_glBindAttribLocation(p, 6, "extra");
    p_glLinkProgram(p);
    GLint linked = 0; p_glGetProgramiv(p, GL_LINK_STATUS, &linked);
    float st[3] = { okv, okf, linked }; gt_vals(t, "compiled-vs-fs-linked", 0, st, 3);
    if (!linked) { char log[300] = ""; p_glGetProgramInfoLog(p, sizeof log, NULL, log); for (char *q = log; *q; q++) if (*q == '\n') *q = ' '; gt_info(t, "link-log %s", log); return; }
    // state the built-ins read
    float v[4] = { 0.4f, 0.6f, 0.8f, 0 };
    glLightfv(GL_LIGHT0, GL_POSITION, v); float d[4] = { 0.9f, 0.7f, 0.5f, 1 }; glLightfv(GL_LIGHT0, GL_DIFFUSE, d);
    float a[4] = { 0.1f, 0.2f, 0.3f, 1 }; glLightfv(GL_LIGHT0, GL_AMBIENT, a); glLightfv(GL_LIGHT1, GL_DIFFUSE, a);
    float sd[3] = { 0.2f, -0.5f, -0.8f }; glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, sd); glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, 40); glLightf(GL_LIGHT1, GL_LINEAR_ATTENUATION, 0.3f);
    float m[4] = { 0.6f, 0.4f, 0.9f, 1 }; glMaterialfv(GL_FRONT, GL_DIFFUSE, m); glMaterialfv(GL_FRONT, GL_AMBIENT, m); glMaterialf(GL_FRONT, GL_SHININESS, 40);
    float lm[4] = { 0.15f, 0.1f, 0.05f, 1 }; glLightModelfv(GL_LIGHT_MODEL_AMBIENT, lm);
    float fc[4] = { 0.9f, 0.3f, 0.1f, 1 }; glFogfv(GL_FOG_COLOR, fc); glFogf(GL_FOG_START, 0.2f); glFogf(GL_FOG_END, 1.8f);
    float ec[4] = { 0.3f, 0.9f, 0.5f, 0.7f }; glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, ec);
    double cp[4] = { 1, 2, 0, 3 }; glClipPlane(GL_CLIP_PLANE0, cp);
    glPointSize(3); float att[3] = { 1, 0.05f, 0 }; p_glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, att); glDepthRange(0.1, 0.9);
    float ep[4] = { 0.01f, 0.02f, 0.0f, 0.3f }; glTexGenfv(GL_S, GL_EYE_PLANE, ep); glTexGenfv(GL_T, GL_OBJECT_PLANE, ep);
    glMatrixMode(GL_TEXTURE); glTranslatef(0.2f, 0.1f, 0); glMatrixMode(GL_MODELVIEW);
    glTranslatef(128, 128, 0); glRotatef(12, 0, 0, 1); glTranslatef(-128, -128, 0);
    GLuint t0 = tex2d(1), t1 = 0, cube = 0, shadow = 0;
    p_glActiveTexture(GL_TEXTURE1); t1 = tex2d(2); p_glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t0);
    p_glUseProgram(p);
    GLint loc;
    if ((loc = p_glGetUniformLocation(p, "t0")) >= 0) p_glUniform1i(loc, 0);
    if ((loc = p_glGetUniformLocation(p, "t1")) >= 0) p_glUniform1i(loc, 1);
    if ((loc = p_glGetUniformLocation(p, "scale")) >= 0) p_glUniform1f(loc, 1.3f);
    if ((loc = p_glGetUniformLocation(p, "lights[1].c")) >= 0) { float c[4] = { 0.5f, 0.2f, 0.7f, 1 }; p_glUniform4fv(loc, 1, c); }
    if ((loc = p_glGetUniformLocation(p, "lights[0].k")) >= 0) p_glUniform1f(loc, 0.8f);
    if ((loc = p_glGetUniformLocation(p, "arr")) >= 0) { float c[12] = { 0, 0, 0, 0, 0.1f, 0.1f, 0.1f, 0, 0.2f, 0, 0.1f, 0 }; p_glUniform4fv(loc, 3, c); }
    if ((loc = p_glGetUniformLocation(p, "M")) >= 0) { float M[16] = { 0.9f, 0.1f, 0, 0, -0.1f, 0.9f, 0, 0, 0, 0, 1, 0, 10, 5, 0, 1 }; p_glUniformMatrix4fv(loc, 1, GL_FALSE, M); }
    if ((loc = p_glGetUniformLocation(p, "c")) >= 0) {
        glGenTextures(1, &cube); glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
        for (int f = 0; f < 6; f++) { uint8_t px[8 * 8 * 4]; for (int i = 0; i < 8 * 8 * 4; i++) px[i] = (uint8_t)(f * 40 + i * 3); glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST); p_glUniform1i(loc, 0);
    }
    if ((loc = p_glGetUniformLocation(p, "s")) >= 0) {
        glGenTextures(1, &shadow); glBindTexture(GL_TEXTURE_2D, shadow);
        float dp[16 * 16]; for (int i = 0; i < 256; i++) dp[i] = (i % 16) / 15.f;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 16, 16, 0, GL_DEPTH_COMPONENT, GL_FLOAT, dp);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        p_glUniform1i(loc, 0);
    }
    gt_errcheck(t, "setup");
    // 6x6 mesh, front and back facing halves
    glBegin(GL_QUADS);
    for (int j = 0; j < 6; j++) for (int i = 0; i < 6; i++) {
        static const int cr[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int k = 0; k < 4; k++) {
            int kk = j >= 4 ? 3 - k : k;   // top rows wound clockwise: back faces
            float u = (i + cr[kk][0]) / 6.f, w = (j + cr[kk][1]) / 6.f;
            glNormal3f(u - 0.5f, w - 0.5f, 0.7f); glColor4f(u, w, 1 - u, 0.5f + 0.5f * w); p_glSecondaryColor3f(w, 0.2f, u);
            p_glFogCoordf(u * 2); p_glMultiTexCoord4f(GL_TEXTURE0, u * 1.5f, w, 0.3f, 1); p_glMultiTexCoord4f(GL_TEXTURE1, w, u * 2, 0.6f, 1.2f);
            p_glVertexAttrib4fARB(6, w, u, 0.5f, 1);
            glVertex3f(16 + u * 224, 16 + w * 224, (u - w) * 0.3f);
        }
    }
    glEnd();
    p_glUseProgram(0);
    gt_errcheck(t, "draw");
    gt_img(t, 0, 0, 256, 256, 3, 0.01);
    p_glDeleteProgram(p); p_glDeleteShader(vs); p_glDeleteShader(fs);
    glDeleteTextures(1, &t0); glDeleteTextures(1, &t1); if (cube) glDeleteTextures(1, &cube); if (shadow) glDeleteTextures(1, &shadow);
}
void reg_glsl(void) {
    char nm[96];
    for (int i = 0; i < NSH; i++) { snprintf(nm, sizeof nm, "glsl/%s", SH[i].name); gt_add(nm, t_glsl, i, 0, 0, 0, NULL); }
}
