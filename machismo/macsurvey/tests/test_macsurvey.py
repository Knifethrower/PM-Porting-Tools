#!/usr/bin/env python3
"""macsurvey tests on the fixtures from make_fixtures.sh.

    python test_macsurvey.py FIXTURE_DIR [NM_DIR]

NM_DIR (optional) holds <name>.<arch>.nm (llvm-nm -u) and <name>.<arch>.otool
(llvm-otool -L) files to check the Mach-O parser against LLVM.
"""
import gzip
import io
import lzma
import os
import shutil
import struct
import sys
import tarfile
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import macsurvey  # noqa: E402

FAILS = []


def check(cond, msg):
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond:
        FAILS.append(msg)


EXPECT = {
    "SDLGame.app": "CANDIDATE (with work)",
    "MetalGame.app": "BLOCKED",
    "OldGame.app": "BLOCKED",
    "UnityGame.app": "OTHER ROUTE",
}


def by_app(reps):
    return {r["app"].rsplit("/", 1)[-1]: r for r in reps}


def check_reports(label, reps):
    apps = by_app(reps)
    check(set(apps) == set(EXPECT), "%s: found apps %s" % (label, sorted(apps)))
    for name, verdict in EXPECT.items():
        if name in apps:
            check(apps[name]["verdict"] == verdict,
                  "%s: %s verdict %r" % (label, name, apps[name]["verdict"]))
    s = apps.get("SDLGame.app")
    if s:
        d = s["deps"]
        check(sorted(s["main"].archs) == ["arm64", "x86_64"] and s["main"].arch == "arm64",
              "%s: SDLGame fat arm64+x86_64, arm64 used" % label)
        check(s["fixups"] == "chained", "%s: SDLGame chained fixups" % label)
        check(d["libSDL2-2.0.0"]["kind"] == "portable" and
              d["libSDL2-2.0.0"]["value"] == "libSDL2-2.0.so.0", "%s: SDL2 -> native" % label)
        check(d["libgame"]["kind"] == "bundled" and
              d["libgame"]["value"] == "MACHO:gamedata/SDLGame.app/Contents/Frameworks/libgame.dylib",
              "%s: libgame -> %s" % (label, d["libgame"]["value"]))
        check(d["libsteam_api"]["value"] == "STUB", "%s: steam stubbed" % label)
        check("_SteamAPI_Init" in d["libsteam_api"]["symbols"], "%s: steam init import seen" % label)
        check(any("SteamAPI_Init" in c for c in s["cautions"]), "%s: steam init caution" % label)
        check(s["renderer"].startswith("OpenGL"), "%s: renderer %s" % (label, s["renderer"]))
        check(s["evidence"]["platform"] == {"SDL2"}, "%s: platform SDL2" % label)
        check("LuaJIT (static)" in s["evidence"]["lib"], "%s: LuaJIT string seen" % label)
        check(len(s["kept"]) == 2, "%s: kept exe + libgame only (%d)" % (label, len(s["kept"])))
        check("Metal" not in d, "%s: SDL's own Metal link not counted" % label)
    m = apps.get("MetalGame.app")
    if m:
        check(m["fixups"] == "dyld_info", "%s: MetalGame dyld_info" % label)
        check(m["renderer"] == "Metal only", "%s: MetalGame Metal only" % label)
        check(m["objc"]["own_classes"] == 1, "%s: one own ObjC class" % label)
        check({"NSWindow", "NSApplication"} <= set(m["objc"]["apple_classes"]),
              "%s: AppKit classes %s" % (label, m["objc"]["apple_classes"]))
        check("Cocoa (own AppKit code)" in m["evidence"]["platform"], "%s: Cocoa platform" % label)
        check("_MTLCreateSystemDefaultDevice" in m["deps"]["Metal"]["symbols"], "%s: Metal import" % label)
    o = apps.get("OldGame.app")
    if o:
        check(o["main"].archs == ["x86_64"] and "no arm64" in o["reasons"][0], "%s: OldGame x86_64" % label)
        check(o["bundle_id"] == "com.example.oldgame", "%s: lowercase info.plist read" % label)
    u = apps.get("UnityGame.app")
    if u:
        check("Unity" in u["evidence"]["engine"], "%s: Unity detected" % label)


# --- archive builders -------------------------------------------------------

def walk(root):
    base = os.path.dirname(root)
    for r, dirs, files in os.walk(root):
        for n in sorted(dirs):
            yield os.path.relpath(os.path.join(r, n), base).replace("\\", "/"), None
        for n in sorted(files):
            full = os.path.join(r, n)
            with open(full, "rb") as f:
                yield os.path.relpath(full, base).replace("\\", "/"), f.read()


SYMLINK = ("fixtures/SDLGame.app/Contents/Frameworks/Current", b"libgame.dylib")


def cpio(entries, newc):
    out = io.BytesIO()
    ino = 1
    for name, data in list(entries) + [(SYMLINK[0], "LINK"), ("TRAILER!!!", b"")]:
        if data == "LINK":
            mode, data = 0o120777, SYMLINK[1]
        elif data is None:
            mode, data = 0o040755, b""
        else:
            mode = 0o100644 if name != "TRAILER!!!" else 0
        nb = name.encode() + b"\0"
        if newc:
            out.write(b"070701" + b"".join(b"%08X" % v for v in
                      (ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(nb), 0)))
            out.write(nb + b"\0" * ((-(110 + len(nb))) % 4))
            out.write(data + b"\0" * ((-len(data)) % 4))
        else:
            out.write(b"070707" + b"%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o" %
                      (0, ino, mode, 0, 0, 1, 0, 0, len(nb), len(data)))
            out.write(nb + data)
        ino += 1
    return out.getvalue()


def pbzx(raw, chunk=1 << 16):
    out = io.BytesIO()
    out.write(b"pbzx" + struct.pack(">Q", chunk))
    for i in range(0, len(raw), chunk):
        part = raw[i:i + chunk]
        c = lzma.compress(part)
        out.write(struct.pack(">QQ", len(part), len(c)) + c)
    return out.getvalue()


def xar(path, payload, style):
    stored = zlib.compress(payload) if "gzip" in style else payload
    toc = ('<?xml version="1.0"?><xar><toc>'
           '<file id="1"><name>game.pkg</name><type>directory</type>'
           '<file id="2"><name>Payload</name><type>file</type><data>'
           '<length>%d</length><offset>0</offset><size>%d</size>'
           '<encoding style="%s"/></data></file></file>'
           '<file id="3"><name>Distribution</name><type>file</type></file>'
           '</toc></xar>' % (len(stored), len(payload), style)).encode()
    tc = zlib.compress(toc)
    with open(path, "wb") as f:
        f.write(struct.pack(">4sHHQQI", b"xar!", 28, 1, len(tc), len(toc), 0))
        f.write(tc + stored)


def build_archives(fix, tmp):
    root = os.path.join(tmp, "fixtures")
    shutil.copytree(fix, root, ignore=shutil.ignore_patterns("src", "tbd"))
    # Some real games (Spaghetti Celesti) ship a lowercase info.plist
    os.rename(os.path.join(root, "OldGame.app", "Contents", "Info.plist"),
              os.path.join(root, "OldGame.app", "Contents", "info.plist"))
    entries = list(walk(root))
    out = {}
    p = out["zip"] = os.path.join(tmp, "game.zip")
    with zipfile.ZipFile(p, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in entries:
            if data is not None:
                z.writestr(name, data)
        zi = zipfile.ZipInfo(SYMLINK[0])
        zi.external_attr = 0o120777 << 16
        z.writestr(zi, SYMLINK[1])
    p = out["tar.gz"] = os.path.join(tmp, "game.tar.gz")
    with tarfile.open(p, "w:gz") as t:
        t.add(root, arcname="fixtures")
    p = out["pkg (gzip odc cpio)"] = os.path.join(tmp, "gzip.pkg")
    xar(p, gzip.compress(cpio(entries, newc=False)), "application/octet-stream")
    p = out["pkg (pbzx newc cpio, xar zlib)"] = os.path.join(tmp, "pbzx.pkg")
    xar(p, pbzx(cpio(entries, newc=True)), "application/x-gzip")
    return root, out


def check_nm(fix, nm_dir):
    for app, exe in (("SDLGame.app", "SDLGame"), ("MetalGame.app", "MetalGame"),
                     ("OldGame.app", "OldGame")):
        path = os.path.join(fix, app, "Contents", "MacOS", exe)
        with open(path, "rb") as f:
            parsed = macsurvey.parse_macho(f.read())
        for arch, sl in parsed["slices"].items():
            nm = os.path.join(nm_dir, "%s.%s.nm" % (exe, arch))
            ot = os.path.join(nm_dir, "%s.%s.otool" % (exe, arch))
            if not os.path.exists(nm):
                continue
            want = {ln.strip() for ln in open(nm) if ln.strip()}
            got = set(sl["imports"])
            check(got == want, "%s %s imports match llvm-nm -u (%d)%s" % (
                exe, arch, len(want), "" if got == want else " got-want=%s want-got=%s" % (
                    sorted(got - want), sorted(want - got))))
            libs = [ln.split(" (")[0].strip() for ln in open(ot).read().splitlines()[1:] if ln.strip()]
            check([d[1] for d in sl["dylibs"]] == libs, "%s %s dylibs match llvm-otool -L" % (exe, arch))


def main():
    fix = sys.argv[1]
    nm_dir = sys.argv[2] if len(sys.argv) > 2 else None
    if nm_dir:
        print("parser vs LLVM")
        check_nm(fix, nm_dir)
    tmp = tempfile.mkdtemp(prefix="macsurvey-")
    try:
        root, archives = build_archives(fix, tmp)
        print("folder")
        inv, reps = macsurvey.survey(root)
        check_reports("folder", reps)
        for label, path in archives.items():
            print(label)
            inv, reps = macsurvey.survey(path)
            check_reports(label, reps)
        print("single .app and loose Mach-O")
        inv, reps = macsurvey.survey(os.path.join(root, "SDLGame.app"))
        check(len(reps) == 1 and reps[0]["verdict"] == EXPECT["SDLGame.app"], "SDLGame.app on its own")
        inv, reps = macsurvey.survey(os.path.join(root, "MetalGame.app", "Contents", "MacOS", "MetalGame"))
        check(len(reps) == 1 and reps[0]["verdict"] == "BLOCKED", "loose MetalGame binary")
        print("--conf / --json")
        conf = os.path.join(tmp, "conf")
        buf, old = io.StringIO(), sys.stdout
        sys.stdout = buf
        try:
            macsurvey.main([root, "--conf", conf, "-vv"])
            macsurvey.main([archives["zip"], "--json"])
        finally:
            sys.stdout = old
        text = open(os.path.join(conf, "sdlgame", "dylib_map.conf")).read()
        check("libSDL2-2.0.0 = libSDL2-2.0.so.0" in text, "dylib_map has SDL2 line")
        check("libSystem.B = libs/libsystem_shim.so" in text, "dylib_map has libSystem shim")
        check("libgame = MACHO:gamedata/" in text, "dylib_map has MACHO: line")
        check("libsteam_api = STUB" in text, "dylib_map has Steam STUB")
        check('"verdict": "BLOCKED"' in buf.getvalue(), "JSON output")
        check("VERDICT      GOOD CANDIDATE" not in buf.getvalue(), "no false GOOD verdict")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n%s (%d failure%s)" % ("FAILED" if FAILS else "PASSED", len(FAILS), "" if len(FAILS) == 1 else "s"))
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
