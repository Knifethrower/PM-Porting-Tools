#!/usr/bin/env python3
"""Build the release folder of a LÖVE port on PortMaster's love_11.5 runtime from a port.toml.

usage (in WSL, python3 >= 3.11):  make_love_release.py <port folder on C:, e.g. /mnt/c/Claude/X>
Reads <port folder>/port.toml:

    title = "KłełeAtoms"            # port.json title; launcher is "<launcher>.sh"
    launcher = "KleleAtoms"         # optional, default = title
    dir = "kleleatoms"              # port folder name on the device
    repo = "~/pr/KleleAtoms"        # git repo; HEAD is archived into <dir>/gamedata
    base = "abc123"                 # upstream commit: tools-src/0001-portmaster.patch = diff base..HEAD
    subdir = ""                     # optional: the game lives in this repo subfolder
    env = ["KLELE_HANDHELD=1"]      # exported by the launcher
    gptk = '''...'''                # full gptokeyb2 ini text (<dir>/<dir>.ini)
    licenses = { "LICENSE" = "LICENSE.kleleatoms.txt" }   # repo path -> licenses/ name (moved out of gamedata)
    drop = ["README.md"]            # removed from gamedata
    desc, genres, gameinfo_desc, releasedate, developer, publisher, genre   # metadata

Writes <port folder>/release/{<launcher>.sh, port.json, gameinfo.xml, <dir>/{gamedata,licenses,<dir>.ini}}.
README.md, screenshot.png, cover.png and testing_thread.txt are written by hand. Then pm_zip.py.
From the 2026-10-03 overnight batch. License: 0BSD.
"""
import json, os, shutil, subprocess, sys, tomllib

LAUNCHER = """#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR=/$directory/ports/@DIR@

cd $GAMEDIR

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

mkdir -p "$GAMEDIR/conf"
export XDG_DATA_HOME="$GAMEDIR/conf"
@ENV@
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

source $controlfolder/runtimes/love_11.5/love.txt

$GPTOKEYB2 "$LOVE_GPTK" -c "$GAMEDIR/@DIR@.ini" &
pm_platform_helper "$LOVE_BINARY"
$LOVE_RUN "$GAMEDIR/gamedata"

pm_finish
"""


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def main():
    port = sys.argv[1]
    cfg = tomllib.load(open(os.path.join(port, "port.toml"), "rb"))
    d, title = cfg["dir"], cfg["title"]
    launcher = cfg.get("launcher", title)
    repo = os.path.expanduser(cfg["repo"])
    rel = os.path.join(port, "release")
    g = os.path.join(rel, d)
    for sub in ("gamedata", "licenses"):
        shutil.rmtree(os.path.join(g, sub), ignore_errors=True)
        os.makedirs(os.path.join(g, sub))
    tmp = os.path.join(g, "_archive")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    arch = subprocess.run(["git", "-C", repo, "archive", "HEAD"], check=True, capture_output=True).stdout
    subprocess.run(["tar", "xf", "-", "-C", tmp], input=arch, check=True)
    # submodules (git archive leaves them out)
    # (gitlinks from the index: `submodule foreach` fails on links without a .gitmodules entry)
    links = [l.split("\t", 1)[1] for l in subprocess.run(["git", "-C", repo, "ls-files", "-s"], check=True,
             capture_output=True, text=True).stdout.splitlines() if l.startswith("160000 ")]
    for sm in links:
        if not os.path.exists(os.path.join(repo, sm, ".git")):
            print("submodule not checked out, left out:", sm)
            continue
        a = subprocess.run(["git", "-C", os.path.join(repo, sm), "archive", "--prefix=" + sm + "/", "HEAD"],
                           check=True, capture_output=True).stdout
        subprocess.run(["tar", "xf", "-", "-C", tmp], input=a, check=True)
    for src, name in cfg.get("licenses", {}).items():
        shutil.move(os.path.join(tmp, src), os.path.join(g, "licenses", name))
    game = os.path.join(tmp, cfg.get("subdir", ""))
    for p in cfg.get("drop", []):
        q = os.path.join(game, p)
        if os.path.isdir(q):
            shutil.rmtree(q)
        elif os.path.exists(q):
            os.remove(q)
    os.rmdir(os.path.join(g, "gamedata"))
    shutil.move(game, os.path.join(g, "gamedata"))
    shutil.rmtree(tmp, ignore_errors=True)

    with open(os.path.join(g, d + ".ini"), "w", newline="\n") as f:
        f.write(cfg["gptk"].lstrip("\n"))
    env = "".join("export %s\n" % e for e in cfg.get("env", []))
    with open(os.path.join(rel, launcher + ".sh"), "w", newline="\n") as f:
        f.write(LAUNCHER.replace("@DIR@", d).replace("@ENV@", env))
    pj = {"version": 4, "name": d + ".zip", "items": [launcher + ".sh", d], "items_opt": [],
          "attr": {"title": title, "porter": ["Knifethrower"], "desc": cfg["desc"], "desc_md": None,
                   "inst": "Ready to run.", "inst_md": None, "genres": cfg["genres"], "image": None,
                   "rtr": True, "exp": False, "runtime": [], "store": [], "availability": "full",
                   "reqs": [], "arch": ["aarch64"], "min_glibc": ""}}
    with open(os.path.join(rel, "port.json"), "w", newline="\n", encoding="utf-8") as f:
        f.write(json.dumps(pj, indent=2, ensure_ascii=False) + "\n")
    gi = """<?xml version="1.0" encoding="utf-8"?>
<gameList>
  <game>
    <path>./%s.sh</path>
    <name>%s</name>
    <desc>%s</desc>
    <releasedate>%s</releasedate>
    <developer>%s</developer>
    <publisher>%s</publisher>
    <genre>%s</genre>
    <image>./%s/screenshot.png</image>
  </game>
</gameList>
""" % (esc(launcher), esc(title), esc(cfg["gameinfo_desc"]), cfg["releasedate"], esc(cfg["developer"]),
       esc(cfg["publisher"]), esc(cfg["genre"]), d)
    with open(os.path.join(rel, "gameinfo.xml"), "w", newline="\n", encoding="utf-8") as f:
        f.write(gi)
    if cfg.get("base"):
        os.makedirs(os.path.join(port, "tools-src"), exist_ok=True)
        diff = subprocess.run(["git", "-C", repo, "diff", cfg["base"], "HEAD"], check=True, capture_output=True).stdout
        with open(os.path.join(port, "tools-src", "0001-portmaster.patch"), "wb") as f:
            f.write(diff)
    print("release written:", rel)


main()
