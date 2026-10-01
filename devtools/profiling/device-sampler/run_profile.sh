# Runs on the device (piped through ssh): the instrumented build from a RAM copy of the port
# folder, with the menu paused, scripted inputs, for $1 seconds (default 70). Binary name $2.
SECS=${1:-70}
BIN=${2:-torustrooper.aarch64.prof}
SRC=/userdata/roms/ports/torustrooper
rm -rf /tmp/tt && mkdir -p /tmp/tt
cp -r $SRC/barrage $SRC/images $SRC/sounds $SRC/replay /tmp/tt/
cp $SRC/$BIN $SRC/glcount.so /tmp/tt/
cd /tmp/tt
chmod +x $BIN
echo "== before: temp $(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null), governor $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
killall -STOP emulationstation 2>/dev/null
LD_PRELOAD=/tmp/tt/glcount.so KEYTIME=1 EXITSECONDS=$SECS \
  KEYS="29:120-130 82:200-4500 29:200-4500 80:500-560 79:900-960 80:1500-1580 79:2200-2280 27:2600-2700 80:3000-3060 79:3600-3660" \
  ./$BIN $TTARGS 2>&1 | grep -v "^Load "
killall -CONT emulationstation 2>/dev/null
echo "== after: temp $(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null)"
ls -la prof.bin prof_maps.txt prof_info.txt 2>&1
