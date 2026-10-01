#!/bin/bash
# Diagnostic: start Unity's libmono.so under box64 WITHOUT Westonpack/crusty/Unity.
# Writes ports/nightinthewoods/monotest.log. Tells us whether the Mono crash is box64-on-device
# or something in the full game environment.
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then controlfolder="$XDG_DATA_HOME/PortMaster"
else controlfolder="/roms/ports/PortMaster"; fi
source $controlfolder/control.txt
GAMEDIR="/$directory/ports/nightinthewoods"
DATADIR="$GAMEDIR/gamedata/NITW_Data"
[ -d "$DATADIR" ] || DATADIR="$GAMEDIR/gamedata/Night in the Woods_Data"
LOG="$GAMEDIR/monotest.log"
cd "$GAMEDIR/tests"
chmod +x "$GAMEDIR/box64" monotest
{
  echo "=== monotest: $(date) data=$DATADIR"
  for opts in "" "BOX64_UNITY=1 BOX64_DYNAREC_BIGBLOCK=0" "BOX64_DYNAREC=0"; do
    echo; echo "##### run with: ${opts:-<defaults>}"
    env $opts BOX64_LOG=1 BOX64_SHOWSEGV=1 BOX64_LD_LIBRARY_PATH="$GAMEDIR/libs.x64/" \
      timeout 300 "$GAMEDIR/box64" ./monotest "$DATADIR" 2>&1 | grep -vE "Call to dlsym|TLSDESC"
    echo "##### exit code: ${PIPESTATUS[0]}"
  done
} > "$LOG" 2>&1
pm_finish 2>/dev/null
