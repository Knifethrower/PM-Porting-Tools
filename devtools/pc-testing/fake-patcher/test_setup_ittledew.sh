#!/bin/bash
# usage: test_setup_ittledew.sh <case>: installer | unpacked | bad | steam
# PROJ = the port project folder in WSL (release/ittledew, gog_ittle_dew_*.sh, pctest/fakepm, pctest/setup_block.sh)
set -u
PROJ=${PROJ:?set PROJ=/mnt/c/<your port project>}
P=/tmp/ittlesetup; rm -rf $P; mkdir -p $P/ports/ittledew/gamedata $P/ports/ittledew/tools
cp "$PROJ/release/ittledew/tools/patchscript" $P/ports/ittledew/tools/
G=~/ittledew/game
case "$1" in
  installer) cp "$PROJ"/gog_ittle_dew_*.sh $P/ports/ittledew/gamedata/ ;;
  unpacked) cp $G/IttleDew.x86_64.orig $P/ports/ittledew/gamedata/IttleDew.x86_64; mkdir -p $P/ports/ittledew/gamedata/IttleDew_Data/Plugins/x86_64; cp $G/IttleDew_Data/level3.orig $P/ports/ittledew/gamedata/IttleDew_Data/level3 ;;
  bad) echo junk > $P/ports/ittledew/gamedata/gog_ittle_dew_bad.sh ;;
  steam) touch $P/ports/ittledew/gamedata/libsteam_api.so $P/ports/ittledew/gamedata/IttleDew.x86 ;;
esac
run() {
  directory=${P#/}; controlfolder="$PROJ/pctest/fakepm"; ESUDO=""; DEVICE_ARCH=x86_64
  pm_message() { echo "[pm_message] $*"; }; sleep() { :; }
  ( source "$PROJ/pctest/setup_block.sh"; echo "[launcher] setup ok -> would start the game" )
  echo "[launcher] exit $?"
}
echo "----- launch 1"; run
echo "----- launch 2"; run
D=$P/ports/ittledew/gamedata
ls -a $D | tr "\n" " "; echo; cat $D/.patched_complete 2>/dev/null
[ -f $D/IttleDew.x86_64 ] && sha1sum $D/IttleDew.x86_64 $D/IttleDew_Data/level3 | cut -c1-12
ls $D/IttleDew_Data/Plugins/x86_64 2>/dev/null
