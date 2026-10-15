#!/bin/bash
# export_pck3.sh <Godot 3 project dir> <out.pck>: export a Godot 3 project as a .pck for PortMaster's
# frt_3.5.2 runtime, with Godot 3.5.2 headless (~/godot352). Works on a copy: forces the GLES2 (+ ETC1 import)
# renderer (frt), adds a "Linux/X11" export preset when the project has none, imports, exports.
# PM_CURSOR=1 adds pm_cursor.gd as an autoload (software pointer). PM_STRETCH=2d|viewport sets
# stretch mode (aspect keep) for fixed-size games. PM_FPS=1 (test builds) logs the frame rate. PM_INCLUDE="*.txt" / PM_EXCLUDE set the
# preset filters (when the script adds the preset). PM_SET="section:key=value;..." overrides
# project settings (e.g. lower MSAA / shadow size for the Mali GPUs). PM_PATCH=<file> applies a
# patch (-p1) to the copy first.
# Run in WSL. From the 2026-10-04 overnight batch. License: 0BSD.
set -e
SRC=$(realpath "$1"); OUT=$(realpath -m "$2")
GODOT=~/godot352/Godot_v3.5.2-stable_linux_headless.64
W=/tmp/export_pck3/$(basename "$SRC")
rm -rf "$W"; mkdir -p "$W"
(cd "$SRC" && tar cf - --exclude=.git --exclude=.import .) | (cd "$W" && tar xf -)
# PM_PATCH: port changes to the project, applied to the copy (patch -p1, paths relative to the project)
[ -z "${PM_PATCH:-}" ] || patch -d "$W" -p1 --no-backup-if-mismatch < "$(realpath "$PM_PATCH")"
python3 - "$W/project.godot" <<'EOF'
import re, sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
s = re.sub(r'(?m)^quality/driver/driver_name=.*\n', '', s)
s = re.sub(r'(?m)^quality/driver/fallback_to_gles2=.*\n', '', s)
s = re.sub(r'(?m)^vram_compression/import_etc=.*\n', '', s)
if "[rendering]" not in s:
    s = s.rstrip("\n") + "\n\n[rendering]\n"
# GLES2 (frt) loads VRAM-compressed textures only as ETC1: import that format too
s = s.replace("[rendering]\n", '[rendering]\n\nquality/driver/driver_name="GLES2"\nvram_compression/import_etc=true\n', 1)
open(p, "w", encoding="utf-8").write(s)
EOF
if [ -n "${PM_STRETCH:-}" ]; then   # e.g. PM_STRETCH=2d: scale a fixed-size window to the screen (aspect keep)
  python3 - "$W/project.godot" "$PM_STRETCH" <<'EOF'
import re, sys
p, mode = sys.argv[1], sys.argv[2]
s = open(p, encoding="utf-8").read()
s = re.sub(r'(?m)^window/stretch/(mode|aspect)=.*\n', '', s)
if "[display]" not in s:
    s = s.rstrip("\n") + "\n\n[display]\n"
s = s.replace("[display]\n", '[display]\n\nwindow/stretch/mode="%s"\nwindow/stretch/aspect="keep"\n' % mode, 1)
open(p, "w", encoding="utf-8").write(s)
EOF
fi
if [ -n "${PM_FPS:-}" ]; then   # test builds only: pm_fps.gd prints "PMFPS n" to stderr every 2 s
  cp "$(dirname "$(realpath "$0")")/pm_fps.gd" "$W/"
  python3 - "$W/project.godot" <<'EOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
if "[autoload]" not in s:
    s = s.rstrip("\n") + "\n\n[autoload]\n"
s = s.replace("[autoload]\n", '[autoload]\n\nPMFps="*res://pm_fps.gd"\n', 1)
open(p, "w", encoding="utf-8").write(s)
EOF
fi
if [ -n "${PM_SET:-}" ]; then   # project overrides: "section:key=value;..." e.g. "rendering:quality/filters/msaa=0"
  python3 - "$W/project.godot" "$PM_SET" <<'EOF'
import re, sys
p, spec = sys.argv[1], sys.argv[2]
s = open(p, encoding="utf-8").read()
for item in filter(None, (x.strip() for x in spec.split(";"))):
    sec, kv = item.split(":", 1)
    key = kv.split("=", 1)[0]
    s = re.sub(r'(?m)^' + re.escape(key) + r'=.*\n', '', s)
    if "[%s]" % sec not in s:
        s = s.rstrip("\n") + "\n\n[%s]\n" % sec
    s = s.replace("[%s]\n" % sec, "[%s]\n\n%s\n" % (sec, kv), 1)
open(p, "w", encoding="utf-8").write(s)
EOF
fi
if [ -n "${PM_CURSOR:-}" ]; then   # software pointer (no hardware cursor on KMSDRM): pm_cursor.gd
  D=$(dirname "$(realpath "$0")")
  cp "$D/pm_cursor.gd" "$D/pm_cursor_draw.gd" "$W/"
  python3 - "$W/project.godot" <<'EOF'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
if "[autoload]" not in s:
    s = s.rstrip("\n") + "\n\n[autoload]\n"
s = s.replace("[autoload]\n", '[autoload]\n\nPMCursor="*res://pm_cursor.gd"\n', 1)
open(p, "w", encoding="utf-8").write(s)
EOF
fi
# import first (the first export after a fresh import can miss resources)
timeout 90 $GODOT --path "$W" --editor --quit >/tmp/export_pck3_import.log 2>&1 || true
if ! grep -q 'platform="Linux/X11"' "$W/export_presets.cfg" 2>/dev/null; then
# Godot reads preset.0, preset.1, ... up to the first gap: use the next index
N=$(grep -c '^\[preset\.[0-9]*\]$' "$W/export_presets.cfg" 2>/dev/null || true)
cat >> "$W/export_presets.cfg" <<'EOF'

[preset.90]

name="Linux/X11"
platform="Linux/X11"
runnable=false
custom_features=""
export_filter="all_resources"
include_filter=""
exclude_filter=""
export_path=""
script_export_mode=1
script_encryption_key=""

[preset.90.options]

custom_template/debug=""
custom_template/release=""
binary_format/64_bits=true
binary_format/embed_pck=false
texture_format/bptc=false
texture_format/s3tc=true
texture_format/etc=true
texture_format/etc2=false
texture_format/no_bptc_fallbacks=true
EOF
# PM_INCLUDE: extra non-resource files to pack (e.g. "*.txt, *.json")
sed -i "s|^include_filter=\"\"|include_filter=\"${PM_INCLUDE:-}\"|" "$W/export_presets.cfg"
sed -i "s|^exclude_filter=\"\"|exclude_filter=\"${PM_EXCLUDE:-}\"|" "$W/export_presets.cfg"
sed -i "s/^\[preset\.90\]/[preset.${N:-0}]/; s/^\[preset\.90\.options\]/[preset.${N:-0}.options]/" "$W/export_presets.cfg"
fi
PRESET=$(grep -B2 'platform="Linux/X11"' "$W/export_presets.cfg" | grep '^name=' | head -1 | sed 's/^name="\(.*\)"$/\1/')
echo "preset: $PRESET"
timeout 600 $GODOT --path "$W" --export-pack "$PRESET" "$OUT" 2>&1 | tail -5
ls -la "$OUT"
