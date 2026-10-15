# Unity 4 donor ports

How to run a Unity 4 game that has only a Windows, Mac or 32-bit Linux build: convert its data on the device to
run on one Unity 4.7.2 Linux x86_64 player that you build yourself (the donor). Everything after the conversion
(runtime, player fixes, screen shapes, textures) is the same as for a native Linux build: see [Unity 4](unity-4.md).

## The technique

A Unity 4 game without a Linux x86_64 build can run on a Linux x86_64 player of another Unity 4 project: the
**donor**. This is the Unity 4 counterpart of [unify](https://github.com/0xf4b1/unify) (which downloads a Linux
editor; there is no Unity 4 Linux editor).

- **The donor is a project you build yourself** with the Unity 4.7.2 editor (4.7.2f1, May 2016, the last Unity 4
  release): a small game or a placeholder screen, built as a Linux x86_64 player. Never take a player from another
  game. Check the Unity licence terms of the editor version you build with before shipping its player.
- From the donor build the port uses the player executable, `Mono/` (`libmono.so` and `etc/mono/config`),
  `Managed/UnityEngine.dll` and `Resources/unity default resources`.
- The donor travels with the port, packed as `.7z` (LZMA2 `-mx9`: 7.4 MB against 10.9 MB as a zip, from 28 MB),
  unpacked in setup with PortMaster's `7zzs`, and removed after a finished setup. When setup fails, the launcher can
  run the donor itself to show a message screen.
- Setup on the device: take the game's data folder from the user's copy, put the donor's engine files in, convert
  the data to the donor's version, apply the [player fixes](unity-4.md#player-fixes), then the usual
  [texture steps](unity-4.md#textures-and-memory). The Windows exe, `mono.dll` and Windows-only plugins are deleted.
- Windows builds carry OpenGL shader programs next to the Direct3D ones, so the Linux player draws them as they
  are ([candidate checks](unity-4.md#is-the-game-a-candidate)). Windows-only native plugins still need a stub or
  a missing-library path the game survives ([Steam builds](unity-4.md#steam-builds-without-the-steam-client)).

## Platform

- Unity 4.7.2 data is the same on Linux, Windows and Mac (PlayerSettings byte-identical; `mainData` differs by the
  platform byte).
- Where the data is: Windows `X_Data` (+ `Mono/mono.dll`); Mac `X.app/Contents/Data` (Mono config in
  `Managed/etc`, `unity default resources` in `Contents/Resources`).
- Older data from Windows can have platform-specific class layouts. Usagi's 4.5 `PlayerSettings` failed on a 4.5
  Linux player ("Mismatched serialization in the builtin class 'PlayerSettings'. (Read 176 bytes but expected 200
  bytes)") and was replaced with a Linux-layout object carrying the game's strings
  ([`fix_playersettings.py`](../../unity/converters/unity4/fix_playersettings.py), 4.5 only; see
  [Reference](#reference)).
- Keep the game's own Windows `2.0/machine.config` and other Mono configuration files; take `config` and
  `libmono.so` from the donor.

## Versions

- **The native player and the managed `UnityEngine.dll` must come from the same build.** With a mismatch, Usagi's
  GUI died with `MissingMethodException ... GUIStyle:RegisterObjectForAssetGarbageCollection`. The game's own
  assemblies work against the donor's engine DLL (older for Usagi's 4.5.0 player, newer for a 4.7.2 donor).
- The player compares the data's version string exactly ("Invalid serialized file version ... Expected 4.7.2f1.
  Actual 4.7.1f1."). Either rewrite every data file's version string to the player's, or, when the layouts are the
  same, patch the 7-byte string right **after `X-Unity-Version\0`** in the player (other copies of the string are
  not the check). Ittle Dew's 4.7.1f1 data ran on a 4.7.2f1 player with that patch plus the donor's
  `UnityEngine.dll`.
- `Resources/unity_builtin_extra` is a versioned serialized file too: the player refused it until the conversion
  included it. A short PC boot test missed it; boot longer and grep the log for "Failed to read file".
- **Serialized layouts between 4.x versions**: release builds have no type trees, so every object must be
  converted to the donor's layout. The reference for all 34 Unity 4 releases is
  [AssetRipper's TypeTreeDumps](https://github.com/AssetRipper/TypeTreeDumps). Classes whose bytes differ from
  4.7.2: 4.0: 61, 4.1: 56, 4.2: 42, 4.3: 30, 4.5.x: 10-6, 4.6.0-4.6.2: 4-1, 4.6.3-4.7.2: none.
  - Fields copy by name; new fields get Unity's default (e.g. Camera stereo) or the donor's value.
  - `m_SortingLayer` index -> the TagManager's `uniqueID`; `m_AnimatePhysics` -> `m_UpdateMode`; renamed classes
    match by their fields; the empty 4.0/4.1 managers (class 30, 127) get defaults.
- **Script data rules** are in no dump (the player derives them from the assemblies). They were found by building
  one probe script with every field kind under 11 editors and diffing the output. They are identical across 4.x
  except **`[Serializable]` structs, serialized from 4.5.0**, and **`sbyte`, from 4.6.2**. Ittle Dew's Steam 4.3.4
  data crashed exactly there before the struct rule (EZ GUI's `SPRITE_FRAME` inside `UVAnimation` arrays ->
  `std::length_error`). The full rules are in [Reference](#reference).
- A converter that works: reads the .NET metadata of the game's assemblies itself, converts engine and script
  objects, reads every converted object back as a check, writes `mainData` last and skips files already at the
  donor's version (so an interrupted setup resumes), and rewrites every file's version string. A C version runs
  on the device in seconds (Ittle Dew 4.3.4, 2,423 objects: 2 s on the PC against 25 s for the Python reference).
- Open (Oct 2026): struct fields new to pre-4.5 data start at zero instead of the script's initialisers; native
  plugins still need stubs per game.

## Results (Oct 2026)

- PC: Windows 32/64-bit and Mac 4.7.2 builds; Ittle Dew GOG 4.7.1 and Steam 4.3.4 (title); Usagi Windows 4.5.3
  (title, Steamworks stub); probe builds 4.0.1 to 4.6.0 boot clean; Ascendant's Windows 4.5.2 build plays its first
  level (same result as its own 32-bit Linux build).
- RG353V (dArkOS): Ittle Dew Steam 4.3.4 converted through PortMaster's patcher in 9 s, game runs, clean log.
- RG Cube XX (Knulli): Thomas Was Alone from its Windows build: setup 21 s, levels 1-1 to 1-2, RSS 278 MB, no
  exceptions. Its 32-bit Linux 4.3.1 build converts too (10,600 objects in 30 classes) and boots clean on the PC.
- One port can cover two stores: a GOG Linux x86_64 build runs natively, a 32-bit-only Steam build goes onto the
  donor.

## Building the donor project

- Old editors run without installing: `7z x UnitySetup-4.x.y.exe` gives a working editor folder (no admin rights,
  no registry).
- Unity 4 Free has no RenderTexture (draw small, `ReadPixels`, stretch), and `BuildPipeline.BuildPlayer` from
  scripts is refused ("requires Unity PRO"), but the command-line switches `-buildLinux64Player`,
  `-buildWindows64Player` and `-buildOSXUniversalPlayer` work.
- Write shaders in Cg, not `GLSLPROGRAM` (see [gl4es and Unity 4](unity-4.md#gl4es-and-unity-4)).
- A **development** build of your donor has a player with symbols and full debug info: run a version-patched copy
  under gdb and break on the failing read (`CachedReader::OutOfBoundsError`) to name the class whose layout is
  wrong. It also gives far more detailed logs than a release player.

## Reference

- **TypeTreeDumps**: 4.0 to 4.2 name their int types `SInt32`/`UInt32`.
- **PlayerSettings (4.5)**: `fix_playersettings.py` assumes the company string at byte 24, after 6 ints (4.3 and
  4.5); 4.7 has 7 ints before it.
- **Script data rules** (all Unity 4 versions, with the two exceptions above):
  - enums are saved as their underlying type (a byte enum = 1 byte, packed in arrays; 64-bit enums are not saved);
  - `char`, `short`, `ushort`, `uint`, `long`, `ulong` fields are never saved;
  - `bool`/`byte`/`sbyte` fields, strings and arrays are aligned to 4 after them;
  - `GUIStyle` 312 bytes, `Gradient` 68 bytes (8 packed colours, 16 UInt16 times, 2 counts), `Keyframe` 4 floats.
