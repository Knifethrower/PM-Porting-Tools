#!/bin/bash
# Prepares a gl4es tree for the ports' arm64 build (called by build_aarch64.sh as the normal user):
#   prep_a64.sh <tree> <ship|fix> <gl4es-test dir> <bare repo>
set -e
S=$1; V=$2; HERE=$3; REPO=$4
rm -rf "$S"; git -C "$REPO" worktree prune
git -C "$REPO" worktree add -f -q --detach "$S" a744af14
cd "$S"
patch -s -p1 < "$HERE/patches/ship-fixes.patch"
# Teslagrad's 3-byte patch, in source: Mali only completes the main FBO with packed depth+stencil
F=src/gl/framebuffers.c
sed -i 's|gles_glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, width, height);|gles_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);|' $F
sed -i 's|GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, glstate->fbo.mainfbo_dep|GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, glstate->fbo.mainfbo_ste|' $F
grep -q 'GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);' $F
grep -q 'GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, glstate->fbo.mainfbo_ste' $F
[ "$V" = fix ] && patch -s -p1 < "$HERE/patches/candidate-fixes.patch"
# upstream debug print (shader source to stdout for every vp + fixed-function program); Westonpack's has none
sed -i 's|^if(default_fragment) printf("fpe_CustomVertexShader|//&|' src/gl/fpe_shader.c
grep -q '^//if(default_fragment) printf' src/gl/fpe_shader.c
# GLX pass-through like Westonpack's gl4es_glxpass: every glX* export forwards to crusty_glX* (glxsdl). gl4es's own
# NOX11 exports of glXQuery*/glXWait*/glXSwapInterval*/glXGetProcAddress* go (the shipped build has none of them)
cp "$HERE/aarch64/glxpass.c" src/glx/glxpass.c
sed -i 's|^\(\s*\)${CMAKE_CURRENT_SOURCE_DIR}/glx/glx_stubs.c|&\n\1${CMAKE_CURRENT_SOURCE_DIR}/glx/glxpass.c|' src/CMakeLists.txt
grep -q glxpass.c src/CMakeLists.txt
sed -i '/^AliasExport(const char\*,glXQueryExtensionsString,,/i #ifndef NOX11   /* ports: glxpass.c */' src/glx/glx.c
sed -i '/^AliasExport(void,glXReleaseBuffersMESA,,());/a #endif' src/glx/glx.c
sed -i '/^AliasExport(void\*,glXGetProcAddress,,/i #ifndef NOX11   /* ports: glxpass.c */' src/glx/lookup.c
sed -i '/^AliasExport(void\*,glXGetProcAddress,ARB,/a #endif' src/glx/lookup.c
[ "$(grep -c 'ports: glxpass.c' src/glx/glx.c src/glx/lookup.c | awk -F: '{s+=$2} END{print s}')" = 2 ]
echo "prepared $V"
