# run_port.sh "<Port>.sh": start an installed port's launcher with the front end stopped (Knulli),
# applying the sed edits in /tmp/test.sed (e.g. source /tmp/vpad.env for the virtual pad).
# Copy to /tmp on the device. From the 2026-10-02 overnight batch. License: 0BSD.
/etc/init.d/S31emulationstation stop >/dev/null 2>&1; sleep 2
touch /tmp/test.sed
sed -f /tmp/test.sed "/userdata/roms/ports/$1" > /tmp/port_run.sh
cd /userdata/roms/ports
HOME=/userdata/system XDG_RUNTIME_DIR=/var/run setsid nohup bash /tmp/port_run.sh > /tmp/port_run.txt 2>&1 < /dev/null &
echo started "$1"
