# Knulli /tmp helpers (RG Cube XX test loop)

Copy these plus `../uinput_pad.py` and `../uinput_keys.py` to `/tmp` on the device (lost on reboot):

    tar cf - -C knulli_tmp . -C .. uinput_pad.py uinput_keys.py | dev.sh 'cd /tmp && tar xf - --no-same-owner'
    dev.sh 'sh /tmp/pad_start.sh'          # own command: its kill loop matches cmdlines with uinput_pad.py

- `pad_start.sh`: virtual SDL pad on `/tmp/pad.fifo` (`echo "a:1:1 right:1:0:2" > /tmp/pad.fifo`),
  `/tmp/vpad.env` (its mapping, real pad ignored) and `/tmp/test.sed` (launcher sources vpad.env).
- `run_port.sh "<Port>.sh"`: installed launcher, front end stopped, test.sed applied; log in `/tmp/port_run.txt`.
- `hotkey_test.sh "<Port>.sh" <binary>`: Select+Start quit check, restarts the front end. Run with bash.
- `love_run.sh <dir>`: a LÖVE folder on love_11.5 before a launcher exists (`LOVE_ENV="X_FIT=1"`).

- `keys_start.sh`: FIFO virtual keyboard on `/tmp/keys.fifo` (start it before the game).
- `love_try.sh <WSL dir> <png> [ENV=1]` (PC side): copy a LÖVE folder to `/userdata/lovetry` and run it.

/tmp on Knulli is tmpfs (RAM): put games on `/userdata`. The SSH pipe does ~0.25 MB/s; for big
games copy over the guest SMB share instead (~0.45 MB/s; robocopy needs `/COPY:D /DCOPY:D`,
attribute copying fails there): `robocopy <src> \\<ip>\share\lovetry /E /COPY:D /DCOPY:D /MT:8`.

Screenshots: `../fbshot.sh <png>`. From the 2026-10-02/03 overnight batches. License: 0BSD.
