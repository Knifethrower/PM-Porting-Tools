#!/usr/bin/env python3
"""Write <release>/testing_thread.txt (the PortMaster Discord testing-thread post) from the
README.md controls table. usage: testing_thread.py <release dir> <title> <url> [instructions]
Only the RG Cube XX (Knulli, 720x720) boxes are ticked. License: 0BSD."""
import os, re, sys

rel, title, url = sys.argv[1:4]
inst = sys.argv[4] if len(sys.argv) > 4 else "Ready to run (LÖVE 11.5 runtime from PortMaster)."
readme = open(os.path.join(rel, "README.md"), encoding="utf-8").read()
m = re.search(r"\| Button \| Action \|\n\|--\|--\|\n((?:\|.*\|\n)+)", readme)
rows = "".join("|%s|%s|\n" % tuple(c.strip() for c in r.strip("|").split("|", 1))
               for r in m.group(1).splitlines())
text = f"""Instructions to install the Testing Zip:
Drop the .zip into your Portmaster autoinstall folder and run Portmaster.
or
run via ssh: harbourmaster install "zip url from discord"

Game Information
Title: {title}
URL: {url}

Instructions:
{inst}

| Button | Action |
|--|--|
{rows}
CFW Tests:
[ ] AmberELEC
[ ] dArkOS
[ ] MuOS
ROCKNIX
-> [ ] Libmali
-> [ ] Panfrost
-> [ ] Adreno (Optional)
[x] Knulli (RG Cube XX)

Resolutions:
[ ] 480x320 (Optional)
[ ] 640x480
[x] 720x720 (RGB30) (Optional) (RG Cube XX)
[ ] Higher resolutions (e.g., 1280)
"""
open(os.path.join(rel, "testing_thread.txt"), "w", encoding="utf-8", newline="\n").write(text)
print("written")
