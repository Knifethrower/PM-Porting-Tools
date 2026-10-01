#!/bin/bash
# EXAMPLE from a finished port (paths, names and DEVICE_IP are that project's): copy and adapt.
# usage: pctest/device_shot.sh <name> [frames]   (from Git Bash in the project folder)
# Copies the release binary to the RG Cube XX, runs the SP_AUTO test (logo -> editor -> add a
# prop and move it) for <frames> frames and fetches the final frame to Test Results/<name>.png.
N=$1; F=${2:-400}
cd "$(dirname "$0")/.."
pctest/dev.sh 'kill $(pidof stuntpg.aarch64) 2>/dev/null; sleep 2; rm -f /tmp/sp.png'
wsl.exe -e bash -c 'scp -o ControlPath=/tmp/device.sock -o BatchMode=yes "/mnt/c/Claude/Stunt Playground/release/stuntplayground/stuntpg.aarch64" root@DEVICE_IP:/userdata/roms/ports/stuntplayground/'
pctest/dev.sh "bash -s start ${AUTO:-auto} --frames $F --shot /tmp/sp.png" < pctest/device_run.sh
pctest/dev.sh 'for i in $(seq 40); do [ -f /tmp/sp.png ] && break; sleep 1; done; sleep 1; ls -la /tmp/sp.png'
wsl.exe -e bash -c "scp -o ControlPath=/tmp/device.sock -o BatchMode=yes root@DEVICE_IP:/tmp/sp.png '/mnt/c/Claude/Stunt Playground/Test Results/$N.png'"
