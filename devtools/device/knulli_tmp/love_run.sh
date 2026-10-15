# love_run.sh <dir>: run a LÖVE game folder on PortMaster's love_11.5 runtime with the front end
# stopped and the virtual pad env (before a launcher exists). Extra env: LOVE_ENV="A=1 B=2". 0BSD.
/etc/init.d/S31emulationstation stop >/dev/null 2>&1; sleep 2
for p in /proc/[0-9]*; do readlink $p/exe 2>/dev/null | grep -q -e /ports/ -e love. && kill -9 ${p#/proc/}; done
controlfolder=/userdata/system/.local/share/PortMaster; DEVICE_ARCH=aarch64
. $controlfolder/runtimes/love_11.5/love.txt
mkdir -p /tmp/lovehome; export XDG_DATA_HOME=/tmp/lovehome
. /tmp/vpad.env
cd $1; env $LOVE_ENV setsid nohup $LOVE_RUN $1 > /tmp/love_run.txt 2>&1 < /dev/null &
sleep 8; tail -5 /tmp/love_run.txt
