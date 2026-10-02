#!/usr/bin/env python3
"""Allegro 5.2.11 SDL backend, PortMaster build: SDL on KMSDRM/desktop GL does not lose texture
contents on a window resize, but Allegro recreated every texture from its RAM backups there, and
bitmaps loaded since the last flip had no backup yet, so they came back blank (Open Surge: brick
sheets and backgrounds invisible after the fullscreen resize). Keep textures as they are, and stop
the per-flip RAM backups that only served this (they doubled the texture memory).

usage: allegro_sdl_nobackup.py <allegro5 source dir>      (edits src/sdl/sdl_display.c in place)
From Open Surge. Patches Allegro: zlib licence, like Allegro."""
import os, sys

if len(sys.argv) != 2:
    sys.exit(__doc__)
p = os.path.join(sys.argv[1], "src", "sdl", "sdl_display.c")
s = open(p).read()
old = """   SDL_GL_SwapWindow(sdl->window);

   // SDL loses texture contents, for example on resize.
   al_backup_dirty_bitmaps(d);
}"""
assert s.count(old) == 1, "swap/backup block not found (other Allegro version?)"
s = s.replace(old, """   SDL_GL_SwapWindow(sdl->window);

   /* PortMaster build: no RAM backups, textures survive resizes (see recreate_textures below) */
}""")
old = """   recreate_textures(display);

   _al_glsl_unuse_shaders();"""
assert s.count(old) == 1, "recreate_textures call not found (other Allegro version?)"
s = s.replace(old, """   /* PortMaster build: GL textures survive a resize on KMSDRM; recreating them from backups
      blanked every bitmap loaded since the last flip */

   _al_glsl_unuse_shaders();""")
open(p, "w").write(s)
print("patched", p)
