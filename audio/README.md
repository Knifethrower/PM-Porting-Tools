# Audio tools

## vorbis_repack/ (Torus Trooper, 2026-10-01)

Old OGG files (encoded before libvorbis 1.0, 2001 and earlier) use **residue type 0**. SDL_mixer
2.8 on the CFWs (Knulli confirmed) decodes OGG with its bundled stb_vorbis, whose type-0 path drops
every partition after the first: music plays muffled, "like the wrong sample rate", while the PC
(SDL_mixer with libvorbisfile) sounds right. Knowledge §18 and `notes/TORUS-TROOPER-PORT-NOTES.md`.

- `vorbis_setup.py file.ogg`: prints the setup header (codebooks, floors, residues, mappings). A
  residue with `type 0` means the file needs repacking.
- `repack.py in.ogg out.ogg`: rewrites residue type 0 as type 1 losslessly (no decode to PCM, no
  re-encode): floors and other bits copied, each partition's VQ values regrouped and coded with
  the same books. Pure Python, no packages.
- `verify.sh original.ogg repacked.ogg`: decodes both with ffmpeg's Vorbis decoder and with
  libvorbis to float and compares bit for bit (exit 0 = identical).

## mixdump/

`mixdump.c` plays one file through SDL_mixer exactly as a game opens it and, with
`SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw`, writes the mixed output to a file: compare the
device's decode with the PC's. Prints driver, SDL_mixer version, granted format and music type.

```bash
gcc -O2 -o mixdump mixdump.c $(pkg-config --cflags --libs sdl2 SDL2_mixer)      # PC or in the arm64 chroot
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=/tmp/out.raw ./mixdump music.ogg 10 44100 2
```
