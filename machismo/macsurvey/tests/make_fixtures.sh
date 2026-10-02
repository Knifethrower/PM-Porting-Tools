#!/bin/bash
# Build synthetic macOS game bundles for the macsurvey tests (run in WSL).
# Needs clang, ld64.lld, llvm-lipo (LLVM 18 names are found automatically).
# Usage: make_fixtures.sh OUTDIR
set -euo pipefail
OUT=${1:?usage: make_fixtures.sh OUTDIR}
LLD=$(command -v ld64.lld || command -v ld64.lld-18)
LIPO=$(command -v llvm-lipo || command -v llvm-lipo-18)
rm -rf "$OUT"; mkdir -p "$OUT/tbd" "$OUT/src"
cd "$OUT"

tbd() {  # tbd FILE INSTALL_NAME "symbols" ["objc-classes"]
    cat > "tbd/$1" <<EOF
--- !tapi-tbd
tbd-version:     4
targets:         [ arm64-macos, x86_64-macos ]
install-name:    '$2'
current-version: 0
exports:
  - targets:         [ arm64-macos, x86_64-macos ]
    symbols:         [ $3 ]
${4:+    objc-classes:    [ $4 ]}
...
EOF
}
tbd libSystem.tbd /usr/lib/libSystem.B.dylib "_printf, _puts, _malloc, _free, dyld_stub_binder"
tbd libobjc.tbd /usr/lib/libobjc.A.dylib "_objc_msgSend, _objc_alloc, _objc_alloc_init, _objc_opt_new, __objc_empty_cache" "NSObject"
tbd SDL2.tbd @rpath/libSDL2-2.0.0.dylib "_SDL_Init, _SDL_CreateWindow, _SDL_GL_CreateContext, _SDL_GL_SwapWindow, _SDL_CreateRenderer"
tbd steam.tbd @loader_path/libsteam_api.dylib "_SteamAPI_Init, _SteamAPI_RunCallbacks"
tbd Metal.tbd /System/Library/Frameworks/Metal.framework/Versions/A/Metal "_MTLCreateSystemDefaultDevice"
tbd AppKit.tbd /System/Library/Frameworks/AppKit.framework/Versions/C/AppKit "_NSApp" "NSWindow, NSApplication"
tbd Foundation.tbd /System/Library/Frameworks/Foundation.framework/Versions/C/Foundation "_NSLog" "NSString"
tbd Unity.tbd @executable_path/../Frameworks/UnityPlayer.dylib "_PlayerMain"

CC=(clang -O1 -fno-stack-protector "--ld-path=$LLD" -nostdlib -Wno-unused-command-line-argument)
mcc() { local a=$1 v=$2; shift 2; [ "${v#*.}" = "$v" ] && v=$v.0; "${CC[@]}" -target $a-apple-macos$v -Wl,-arch,$a -Wl,-platform_version,macos,$v,$v "$@"; }

# --- bundled game-specific library and fake SDL2 / Steam / Unity dylibs ---
cat > src/libgame.c <<'EOF'
extern int printf(const char *, ...);
int game_tick(int n) { printf("tick %d\n", n); return n + 1; }
EOF
cat > src/fakesdl.c <<'EOF'
extern void *MTLCreateSystemDefaultDevice(void);
int SDL_Init(unsigned f) { return MTLCreateSystemDefaultDevice() != 0; }
void *SDL_CreateWindow(const char *t, int x, int y, int w, int h, unsigned f) { return 0; }
void *SDL_GL_CreateContext(void *w) { return 0; }
void SDL_GL_SwapWindow(void *w) {}
void *SDL_CreateRenderer(void *w, int i, unsigned f) { return 0; }
EOF
cat > src/fakesteam.c <<'EOF'
int SteamAPI_Init(void) { return 1; }
void SteamAPI_RunCallbacks(void) {}
EOF
cat > src/player.c <<'EOF'
int PlayerMain(int argc, char **argv) { return 0; }
EOF
for a in arm64 x86_64; do
    mcc $a 11 -dynamiclib -install_name @rpath/libgame.dylib \
        src/libgame.c tbd/libSystem.tbd -o libgame.$a.dylib
    mcc $a 11 -dynamiclib -install_name @rpath/libSDL2-2.0.0.dylib \
        src/fakesdl.c tbd/libSystem.tbd tbd/Metal.tbd -o sdl.$a.dylib
    mcc $a 11 -dynamiclib -install_name @loader_path/libsteam_api.dylib \
        src/fakesteam.c tbd/libSystem.tbd -o steam.$a.dylib
done

# --- 1. SDLGame.app: fat arm64 + x86_64, SDL2 + GL, Steam, own dylib, chained fixups ---
cat > src/sdlgame.c <<'EOF'
extern int SDL_Init(unsigned);
extern void *SDL_CreateWindow(const char *, int, int, int, int, unsigned);
extern void *SDL_GL_CreateContext(void *);
extern void SDL_GL_SwapWindow(void *);
extern int SteamAPI_Init(void);
extern int game_tick(int);
extern int printf(const char *, ...);
static const char banner[] = "LuaJIT 2.1.0-beta3";
int main(void) {
    SDL_Init(0x20);
    void *w = SDL_CreateWindow("game", 0, 0, 640, 480, 2);
    SDL_GL_CreateContext(w);
    if (!SteamAPI_Init()) printf("%s\n", banner);
    for (int i = 0; i < 3; i = game_tick(i)) SDL_GL_SwapWindow(w);
    return 0;
}
EOF
A=SDLGame.app/Contents
mkdir -p $A/MacOS $A/Frameworks $A/Resources
for a in arm64 x86_64; do
    mcc $a 11 -Wl,-fixup_chains -Wl,-rpath,@executable_path/../Frameworks \
        src/sdlgame.c tbd/libSystem.tbd tbd/SDL2.tbd tbd/steam.tbd libgame.$a.dylib -o sdlgame.$a
done
"$LIPO" -create sdlgame.arm64 sdlgame.x86_64 -output $A/MacOS/SDLGame
"$LIPO" -create sdl.arm64.dylib sdl.x86_64.dylib -output $A/Frameworks/libSDL2-2.0.0.dylib
"$LIPO" -create libgame.arm64.dylib libgame.x86_64.dylib -output $A/Frameworks/libgame.dylib
"$LIPO" -create steam.arm64.dylib steam.x86_64.dylib -output $A/MacOS/libsteam_api.dylib
echo "level data" > $A/Resources/level1.dat

plist() {  # plist APPDIR EXE ID
    cat > "$1/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>$2</string>
<key>CFBundleIdentifier</key><string>$3</string>
<key>CFBundleShortVersionString</key><string>1.2.3</string>
</dict></plist>
EOF
}
plist SDLGame.app SDLGame com.example.sdlgame

# --- 2. MetalGame.app: arm64 only, ObjC + AppKit + Metal, classic dyld info ---
cat > src/metalgame.m <<'EOF'
__attribute__((objc_root_class)) @interface NSObject { void *isa; } + (id)alloc; - (id)init; @end
@interface NSWindow : NSObject @end
@interface NSApplication : NSObject + (id)sharedApplication; @end
@interface GameView : NSObject @end
@implementation GameView @end
extern void *MTLCreateSystemDefaultDevice(void);
int main(void) {
    [NSApplication sharedApplication];
    id w = [[NSWindow alloc] init];
    return MTLCreateSystemDefaultDevice() == 0 && w == 0;
}
EOF
B=MetalGame.app/Contents
mkdir -p $B/MacOS $B/Resources
mcc arm64 11 -Wl,-no_fixup_chains -fobjc-runtime=macosx-11 \
    src/metalgame.m tbd/libSystem.tbd tbd/libobjc.tbd tbd/AppKit.tbd tbd/Metal.tbd \
    tbd/Foundation.tbd -o $B/MacOS/MetalGame
printf 'MTLB' > $B/Resources/default.metallib
plist MetalGame.app MetalGame com.example.metalgame

# --- 3. OldGame.app: x86_64 only ---
C=OldGame.app/Contents
mkdir -p $C/MacOS
mcc x86_64 10.13 -Wl,-rpath,@executable_path/../Frameworks \
    src/sdlgame.c tbd/libSystem.tbd tbd/SDL2.tbd tbd/steam.tbd libgame.x86_64.dylib -o $C/MacOS/OldGame
plist OldGame.app OldGame com.example.oldgame

# --- 4. UnityGame.app: Unity player layout ---
D=UnityGame.app/Contents
mkdir -p $D/MacOS $D/Frameworks $D/Resources/Data/Managed
cat > src/unity.c <<'EOF'
extern int PlayerMain(int, char **);
int main(int c, char **v) { return PlayerMain(c, v); }
EOF
mcc arm64 11 -dynamiclib -install_name @executable_path/../Frameworks/UnityPlayer.dylib \
    src/player.c tbd/libSystem.tbd -o $D/Frameworks/UnityPlayer.dylib
mcc arm64 11 src/unity.c tbd/libSystem.tbd tbd/Unity.tbd -o $D/MacOS/UnityGame
echo x > $D/Resources/Data/globalgamemanagers
plist UnityGame.app UnityGame com.example.unitygame

rm -f ./*.dylib sdlgame.*
echo "fixtures in $OUT: $(ls -d ./*.app | tr '\n' ' ')"
