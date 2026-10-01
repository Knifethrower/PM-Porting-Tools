/* From Torus Trooper (tools/soundtest). Build: gcc -O2 -o mixdump mixdump.c $(pkg-config --cflags --libs sdl2 SDL2_mixer) */
/* Plays one file through SDL_mixer exactly as the game opens it, for N seconds, with
 * SDL_AUDIODRIVER=disk so the mixed output lands in SDL_DISKAUDIOFILE.
 * usage: mixdump <file> <seconds> [rate] [channels] [buffer]
 * .ogg is played as music (Mix_LoadMUS), anything else as a chunk (Mix_LoadWAV). */
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s file seconds [rate] [channels] [buffer]\n", argv[0]); return 1; }
  int rate = argc > 3 ? atoi(argv[3]) : 44100;
  int ch = argc > 4 ? atoi(argv[4]) : 1;
  int buf = argc > 5 ? atoi(argv[5]) : 4096;
  if (SDL_Init(SDL_INIT_AUDIO) < 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
  if (Mix_OpenAudioDevice(rate, AUDIO_S16, ch, buf, NULL, 0xff) < 0) { fprintf(stderr, "open: %s\n", Mix_GetError()); return 1; }
  int r, c; Uint16 f;
  Mix_QuerySpec(&r, &f, &c);
  const SDL_version *v = Mix_Linked_Version();
  printf("driver %s, mixer %d.%d.%d, got %d Hz %d ch format 0x%x\n", SDL_GetCurrentAudioDriver(),
         v->major, v->minor, v->patch, r, c, f);
  const char *ext = strrchr(argv[1], '.');
  if (ext && strcmp(ext, ".ogg") == 0) {
    Mix_Music *m = Mix_LoadMUS(argv[1]);
    if (!m) { fprintf(stderr, "LoadMUS: %s\n", Mix_GetError()); return 1; }
    printf("music type %d\n", Mix_GetMusicType(m));
    Mix_PlayMusic(m, 0);
  } else {
    Mix_Chunk *k = Mix_LoadWAV(argv[1]);
    if (!k) { fprintf(stderr, "LoadWAV: %s\n", Mix_GetError()); return 1; }
    Mix_PlayChannel(0, k, 0);
  }
  SDL_Delay(atoi(argv[2]) * 1000);
  Mix_CloseAudio();
  SDL_Quit();
  return 0;
}
