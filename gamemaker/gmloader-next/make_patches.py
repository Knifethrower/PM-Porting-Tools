"""The gmloader-next changes as four independent patches (each applies to upstream on its own and
they apply together in any order), written from a pristine checkout:

  01-texhack-arm32.patch   armhf texture hack hooked the wrong address (pages never used)
  02-openal-system.patch   USE_OPENAL_THUNKS=1: old runners' OpenAL calls go to the system libopenal
  03-render-size.patch     "render_size": "<w>x<h>": offscreen render target + mipmapped letterbox
  04-spritehack.patch      ASTC for sprite_add / background_add images (needs gmsprites packs)

usage: make_patches.py <pristine gmloader-next checkout> <out dir>
(03 and 04 also need letterbox.cpp / spritehack.cpp copied into gmloader/; build.sh does that.)
"""
import sys, os, re, shutil, subprocess, tempfile

src, out = sys.argv[1], sys.argv[2]

def edit(root, path, old, new, count=1):
    p = os.path.join(root, path)
    s = open(p).read()
    assert s.count(old) == count, (path, old)
    open(p, 'w', newline='\n').write(s.replace(old, new))

def texhack_arm32(r):
    # the hook was written over the local pointer variable instead of the runner's
    # LoadTextureFromPNG (texture_arm64.cpp has it right), so .pvr pages were never used on armhf
    edit(r, 'gmloader/texture_arm32.cpp', 'hook_address(mod, (uintptr_t)&LoadTextureFromPNG, ',
         'hook_address(mod, (uintptr_t)LoadTextureFromPNG, ', count=4)

def openal_system(r):
    # Older runners (GMS 1.4.1804) link libopenal.so and ship an Android OpenAL-soft (OpenSL ES /
    # JNI back ends) that crashes in Sound_Prepare under gmloader-next. With USE_OPENAL_THUNKS=1
    # their OpenAL calls go to the system libopenal (upstream's thunk table, unused since e0fc3af)
    # and the APK's libopenal.so is not loaded.
    edit(r, 'Makefile.gmloader', 'ifeq (${USE_LUA},1)\n',
         'ifeq (${USE_OPENAL_THUNKS},1)\n'
         '\tCFLAGS+=-DUSE_OPENAL_THUNKS=1\n'
         '\tCXXFLAGS+=-DUSE_OPENAL_THUNKS=1\n'
         '\tLIBS+=$(shell ${PKG_CONFIG} openal --libs)\n'
         'endif\n\n'
         'ifeq (${USE_LUA},1)\n')
    edit(r, 'gmloader/main.cpp', 'extern DynLibFunction symtable_gles2[];\n',
         'extern DynLibFunction symtable_gles2[];\n'
         '#ifdef USE_OPENAL_THUNKS\n'
         'extern DynLibFunction symtable_openal[];\n'
         '#endif\n')
    edit(r, 'gmloader/main.cpp', '    symtable_gles2,\n',
         '    symtable_gles2,\n'
         '#ifdef USE_OPENAL_THUNKS\n'
         '    symtable_openal,\n'
         '#endif\n')
    edit(r, 'loader/so_util.cpp', '        filename.starts_with("libz.so"))\n',
         '        filename.starts_with("libz.so")\n'
         '#ifdef USE_OPENAL_THUNKS\n'
         '        || filename.starts_with("libopenal.so")\n'
         '#endif\n'
         '        )\n')

def render_size(r):
    edit(r, 'gmloader/configuration.h', '    std::string force_platform;\n',
         '    std::string force_platform;\n'
         '    std::string render_size;\n')
    edit(r, 'gmloader/configuration.cpp', '    get_if_exists("force_platform", force_platform);\n',
         '    get_if_exists("force_platform", force_platform);\n'
         '    get_if_exists("render_size", render_size);\n')
    edit(r, 'gmloader/configuration.cpp',
         '    printf("config: force_platform = %s\\n", force_platform.c_str());\n',
         '    printf("config: force_platform = %s\\n", force_platform.c_str());\n'
         '    printf("config: render_size = %s\\n", render_size.c_str());\n')
    edit(r, 'gmloader/main.cpp', 'so_module *libyoyo = NULL;\n',
         'so_module *libyoyo = NULL;\n\n'
         '/* letterbox.cpp */\n'
         'void letterbox_size(int *w, int *h);\n'
         'void letterbox_present();\n')
    edit(r, 'gmloader/main.cpp',
         '    RunnerJNILib::Startup(env, 0, apk_path_arg, save_dir_arg, pkg_dir_arg, 4, 0);\n',
         '    SDL_GetWindowSize(sdl_win, &w, &h);\n'
         '    letterbox_size(&w, &h);\n'
         '    RunnerJNILib::Startup(env, 0, apk_path_arg, save_dir_arg, pkg_dir_arg, 4, 0);\n')
    edit(r, 'gmloader/main.cpp',
         '        SDL_GetWindowSize(sdl_win, &w, &h);\n'
         '        cont = RunnerJNILib::Process(env, 0, w, h, 0, 0, 0, 0, 0, 60);\n'
         '        if (RunnerJNILib::canFlip(env, 0))\n'
         '            SDL_GL_SwapWindow(sdl_win);\n',
         '        SDL_GetWindowSize(sdl_win, &w, &h);\n'
         '        letterbox_size(&w, &h);\n'
         '        cont = RunnerJNILib::Process(env, 0, w, h, 0, 0, 0, 0, 0, 60);\n'
         '        if (RunnerJNILib::canFlip(env, 0)) {\n'
         '            letterbox_present();\n'
         '            SDL_GL_SwapWindow(sdl_win);\n'
         '        }\n')
    # letterbox.cpp's overrides in the table the runner's GL imports resolve from
    edit(r, 'thunks/khronos/gles2.cpp', 'void set_gles2_shader_override_dir(const char *path, bool should_dump)\n',
         'ABI_ATTR void letterbox_glBindFramebuffer(GLenum target, GLuint framebuffer);\n'
         'ABI_ATTR void letterbox_glGetIntegerv(GLenum pname, GLint *data);\n\n'
         'void set_gles2_shader_override_dir(const char *path, bool should_dump)\n')
    edit(r, 'thunks/khronos/gles2.cpp',
         '\tglad_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)PTR_RESOLVE(glBindFramebuffer);\n',
         '\tglad_glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)PTR_RESOLVE(glBindFramebuffer);\n'
         '\tsymtable_gles2[symtable_gles2_index-1].func = (uintptr_t)letterbox_glBindFramebuffer;\n')
    edit(r, 'thunks/khronos/gles2.cpp',
         '\tglad_glGetIntegerv = (PFNGLGETINTEGERVPROC)PTR_RESOLVE(glGetIntegerv);\n',
         '\tglad_glGetIntegerv = (PFNGLGETINTEGERVPROC)PTR_RESOLVE(glGetIntegerv);\n'
         '\tsymtable_gles2[symtable_gles2_index-1].func = (uintptr_t)letterbox_glGetIntegerv;\n')

def spritehack(r):
    edit(r, 'gmloader/libyoyo.h',
         'void Function_Add_Hook(const char* f_name, routine_t func, int argc, char reg);\n',
         'void Function_Add_Hook(const char* f_name, routine_t func, int argc, char reg);\n\n'
         '/* spritehack.cpp */\n'
         'void register_spritehack_functs(so_module *mod);\n'
         'void spritehack_end_step();\n')
    edit(r, 'gmloader/libyoyo.cpp', '    Function_Add("game_change", game_change_reimpl, 2, 0);\n',
         '    Function_Add("game_change", game_change_reimpl, 2, 0);\n'
         '    register_spritehack_functs(mod);\n')
    # frees the CPU bitmaps of the sprites loaded in the previous step
    edit(r, 'gmloader/main.cpp', '        if (update_inputs(sdl_win) != 1)\n',
         '        spritehack_end_step();\n'
         '        if (update_inputs(sdl_win) != 1)\n')

FEATURES = [('01-texhack-arm32', texhack_arm32), ('02-openal-system', openal_system),
            ('03-render-size', render_size), ('04-spritehack', spritehack)]

os.makedirs(out, exist_ok=True)
for name, fn in FEATURES:
    tmp = tempfile.mkdtemp()
    a, b = os.path.join(tmp, 'a'), os.path.join(tmp, 'b')
    ignore = shutil.ignore_patterns('.git', '3rdparty', 'build')
    shutil.copytree(src, a, ignore=ignore)
    shutil.copytree(src, b, ignore=ignore)
    fn(b)
    diff = subprocess.run(['diff', '-ruN', 'a', 'b'], cwd=tmp, capture_output=True, text=True).stdout
    open(os.path.join(out, name + '.patch'), 'w', newline='\n').write(diff)
    shutil.rmtree(tmp)
    print(name, diff.count('\n+++ ') + diff.startswith('+++ '), 'files')
