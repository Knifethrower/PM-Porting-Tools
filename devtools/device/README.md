# Testing on the handheld

No passwords or keys: the user opens one shared SSH connection per boot, and everything goes
through it (knowledge §16.14). Ask before starting or stopping anything on a device the user also
plays on. Run test copies from `/tmp` (RAM); the data partition on Knulli is exFAT/FUSE.

```bash
# user, once per boot (Git Bash or PowerShell):
wsl ssh -M -S /tmp/device.sock -N -o ServerAliveInterval=30 root@<device ip>
# then
export DEV_HOST=root@<device ip> DEV_SOCK=/tmp/device.sock
```

| Tool | Where it runs | What |
|--|--|--|
| `dev.sh '<cmd>'` | PC | Runs a command over the shared connection; scripts go in on stdin (`dev.sh "bash -s args" < script.sh`). |
| `device_run.sh start/status/stop` | device | Starts an installed port's launcher (a /tmp copy, optionally edited with sed) with the front end stopped; status (RSS, threads, MemAvailable, log tail); stop and restart the front end. Knulli/Batocera, dArkOS, ROCKNIX. |
| `uinput_keys.py <spec>...` | device | Scripted key presses on a virtual uinput keyboard (as gptokeyb does): get from the title into the game unattended. Copy to /tmp first. |
| `fbshot.sh <out.png>` | PC | Screenshot from `/dev/fb0` (size/depth from sysfs), converted in WSL. Not yet run on a device in this generic form (Ittle Dew's 720x720 BGRA version worked on the Cube XX). |
| `fbburst.sh <outdir> <count> <interval> ['<device cmd>']` | PC | A burst of screenshots taken on the device, `<interval>` apart (fbshot.sh needs seconds per frame over SSH); the optional command (e.g. a pad FIFO press) starts right before the first frame. 32 bpp framebuffers. |
| `knulli_tmp/` | device | Knulli test loop helpers copied to `/tmp`: virtual pad and keyboard on FIFOs, run an installed port with the front end stopped, Select+Start quit check, run a LÖVE folder on love_11.5 before it has a launcher. README there. |
| `thread_sample.sh <process>` | device | Busiest threads, their state and kernel wait channel, page faults, free memory: hangs and stalls without gdb. |
| `smaps_summary.py <pid>` | device | Rss per mapping (game files, heap, anon, libraries). |
| `install_smb.py <share ports folder> <zip> [<game files> <subfolder>]` | PC (Windows) | Unpacks the port zip (and the user's game files) onto the device's SMB share, checks sizes and the MD5 of executables and libraries. From Tummy Bonbons. |
| `examples/` | | Complete sessions from finished ports: Cube's map-tour benchmark, Ittle Dew's step-by-step driving session (keys, screenshot, fps per step), Stunt Playground's copy-run-fetch screenshot. |

Profiling on the device: `../profiling/device-sampler/`; box64 games: `../profiling/fpslog/`.
