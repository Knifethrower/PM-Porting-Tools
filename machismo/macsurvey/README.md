# macsurvey

Is a macOS game a candidate for a [machismo](https://github.com/bmdhacks/machismo) port?
Machismo loads Apple Silicon (arm64) Mach-O games on aarch64 Linux and redirects their dylib
imports to Linux libraries. That is how PortMaster's Crypt of the NecroDancer, Mina the Hollower
and the PUNKCAKE (Sugar engine) ports work. This tool reads a Mac build and says whether that
route can work, before anyone buys or extracts anything big.

```
python macsurvey.py PATH [PATH...] [--json] [--conf DIR] [-v | -vv]
```

`PATH` can be a `.app`, a folder (Steam depot, extracted game, several apps), a single Mach-O
file, a GOG / Apple `.pkg`, an itch.io `.zip` or a `.tar(.gz)`. Archives are read in one pass
in memory: nothing is extracted to disk. A `.dmg` has to be extracted first (`7z x game.dmg`).
Python 3.8+, standard library only, so it also runs on the device with PortMaster's python.

## What it reports (per .app)

| Line | Meaning |
|--|--|
| `VERDICT` | `GOOD CANDIDATE`, `CANDIDATE (with work)`, `HARD`, `BLOCKED`, `OTHER ROUTE` or `NO BINARY` (assets-only depot) |
| `archs` | Slices in the executable; machismo needs `arm64`. x86_64-only Mac builds are a box64 job |
| `fixups` | `chained` or `dyld_info`: machismo handles both |
| `md5` | Of the executable as shipped, for the launcher's `KNOWN_MD5` check |
| `engine` | From file names and strings: Unity, GameMaker, Godot, LÖVE, .NET/FNA, Java, HashLink, Ren'Py, Unreal, Clickteam Fusion, Sugar, ... |
| `platform` | SDL2 / SDL3 (as a dylib or static), SFML, GLFW, Allegro, or the game's own Cocoa code |
| `renderer` | bgfx (machismo's GLES bgfx trampoline), OpenGL (gl4es), SDL_Renderer, Vulkan/MoltenVK, Metal only (blocked) |
| `Objective-C` | Own classes, categories and Apple classes the game code uses (`libobjc` is only a stub) |
| `caution` | Things that need patches: Steam init, FMOD/Wwise/Bink, AppKit/GameController/AudioToolbox calls, arm64e |
| dependencies | Every library the game code links, what it becomes in `dylib_map.conf`, and how many symbols it uses (`-v` lists framework symbols, `-vv` all) |

"Game code" means the executable plus bundled libraries that have no native replacement and
so stay Mach-O (`MACHO:`). Libraries machismo replaces, such as a bundled SDL2 linking Metal,
don't count against the game.

`--conf DIR` writes a draft `dylib_map.conf` and `machismo.conf` per app (SDL / bgfx / LuaJIT
trampolines when those are linked in statically) to start a port from. `MACHO:` paths assume
the `.app` sits in the port's `gamedata/`. The drafts are a starting point: the per-version
instruction patches (`conf/patches/*.conf`) still come from reverse engineering. See
NecroDancer's and machismo's `examples/`.

## Verdict rules

- **OTHER ROUTE**: an engine with a better path (Unity → box64/unityport; LÖVE, Godot, Ren'Py,
  FNA/.NET, Java, HashLink → PortMaster runtimes; Electron/NW.js, Unreal, GameMaker, AIR).
- **BLOCKED**: no arm64 slice, FairPlay-encrypted (Mac App Store / Apple Arcade), Swift
  runtime, or a Metal-only renderer.
- **HARD**: own Cocoa window/input code, heavy Objective-C, Clickteam Fusion (Objective-C
  runner), or no renderer found.
- **CANDIDATE (with work)**: arm64 + portable platform layer + GL/bgfx/SDL renderer, but with
  cautions (store SDK init, proprietary middleware, some framework calls).
- **GOOD CANDIDATE**: none of the above.

## Tests

`tests/run_tests.sh` (WSL: clang, ld64.lld, llvm-lipo, llvm-nm, llvm-otool) builds four fake
games as real Mach-O files (fat SDL2+GL+Steam game with a bundled dylib and chained fixups;
arm64 Cocoa+Metal game with ObjC and classic dyld info; x86_64-only; Unity layout). It checks the
parser's imports and dylibs against `llvm-nm -u` / `llvm-otool -L`, then surveys the fixtures as
folder, zip (with a symlink), tar.gz, `.pkg` (gzip odc cpio) and `.pkg` (pbzx newc cpio, zlib in
xar) and expects the same verdicts from each. The Python part also runs on Windows:
`python tests/test_macsurvey.py FIXTURE_DIR [NM_DIR]`.

First real game checked: Spaghetti Celesti (itch.io Mac zip, 2026-10-01): Sugar, SDL3, OpenGL, no ObjC, CANDIDATE.
