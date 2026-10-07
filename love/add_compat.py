#!/usr/bin/env python3
"""add_compat.py <game dir>: copy compat_love010_on_11.lua into a LÖVE 0.10 game as compat.lua,
require it first in main.lua and set t.version = "11.0" in conf.lua (if conf.lua sets a version).
License: 0BSD."""
import os, re, shutil, sys

d = sys.argv[1]
shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "compat_love010_on_11.lua"),
            os.path.join(d, "compat.lua"))
p = os.path.join(d, "main.lua")
s = open(p, encoding="utf-8", errors="surrogateescape").read()
if 'require("compat")' not in s:
    nl = "\r\n" if "\r\n" in s else "\n"
    s = 'require("compat")' + nl + s
    open(p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
p = os.path.join(d, "conf.lua")
if os.path.exists(p):
    s = open(p, encoding="utf-8", errors="surrogateescape").read()
    s2 = re.sub(r'(t\.version\s*=\s*)"[0-9.]+"', r'\1"11.0"', s)
    open(p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s2)
if len(sys.argv) > 2:   # add_compat.py <dir> W H [lua options]: also fit.lua, required before the shim
    shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fit.lua"), os.path.join(d, "fit.lua"))
    p = os.path.join(d, "main.lua")
    s = open(p, encoding="utf-8", errors="surrogateescape").read()
    if 'require("fit")' not in s:
        nl = "\r\n" if "\r\n" in s else "\n"
        o = (", " + sys.argv[4]) if len(sys.argv) > 4 else ""
        s = 'require("fit").setup(%s, %s%s)' % (sys.argv[2], sys.argv[3], o) + nl + s
        open(p, "w", encoding="utf-8", errors="surrogateescape", newline="").write(s)
print("compat added to", d)
