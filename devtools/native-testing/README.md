# PC testing for native ports (WSL)

From Torus Trooper, Mu-cade, Cube and Hocus Pocus. Needs Xvfb, openbox, qemu-user-static,
ImageMagick, and an arm64 sysroot (`../../build/native-aarch64`).

| Script | What |
|--|--|
| `pm_sim_test.sh <zip> [runs]` | Fake PortMaster device: control.txt / libgl_default.txt / 7zzs, the zip unpacked as the installer leaves it (chmod 777), the binary wrapped to log the launcher's environment and run under qemu-user on Xvfb. Shows `log.txt` and the port folder after each run. |
| `replay_compare.sh <binary> <outdir>` | Runs an x86_64 build on Xvfb with the glcount probe (`../profiling/device-sampler/glcount.c`) in FAKECLOCK mode with scripted KEYS: every run draws the same frames; dumps the frames in FRAMES. `GL4ES=<dir>` runs through a Mali-profile gl4es instead of Mesa GLES. |
| `arm_replay_compare.sh <port folder> <binary> <outdir>` | Same for the aarch64 release binary inside the arm64 chroot (as root). Slow: few frames. |
| `compare_frames.sh <A> <B> <out>` | Pixel diff of two dump folders; side-by-side PNG for every frame that differs. |
| `aspect_test.sh <binary> <shots> [WxH...]` | One contact sheet per handheld screen size (Hor+/Vert+ and HUD anchoring). |
| `contact_sheet.sh <frames dir> [out.jpg]` | Every frame dump of one run on a labelled sheet (Bugdom 2). |

FAKECLOCK freezes CLOCK_REALTIME and `time()`: games that time frames with std::chrono
(libstdc++ `high_resolution_clock` = realtime) see zero deltas, and those seeding from `time()` get a fixed
seed (Bugdom 2's PC tests). The probe also counts `glDrawElements`.

Build the probe once: `gcc -O2 -shared -fPIC -o ~/glcount.so ../profiling/device-sampler/glcount.c -ldl`.
KEYS uses SDL scancodes, `"code:from-to ..."` in frames (Z 29, X 27, Enter 40, arrows R79 L80 D81 U82).
