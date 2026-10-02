# Allegro 5 on handhelds

From Open Surge (static Allegro 5.2.11.3 with the SDL backend on GLES, 60 fps on the RG Cube XX,
2026-10-02). Both are zlib-licensed, like Allegro.

- `native_dialog_stub.c`: Allegro's native dialog addon needs GTK on Linux, which handhelds don't
  have. The stub provides the addon's API: message boxes print to stderr, the file chooser reports
  "cancelled", the text log is ignored. Build it as `liballegro_dialog-static.a` (or the shared
  name the game links) instead of the real addon.
- `allegro_sdl_nobackup.py <allegro5 source>`: SDL backend fix: after the fullscreen resize every
  bitmap loaded since the last flip came back blank (textures recreated from RAM backups that did
  not exist yet), and the per-flip backups doubled texture memory. Patched out; GL textures
  survive the resize on KMSDRM.
