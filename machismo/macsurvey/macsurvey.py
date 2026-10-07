#!/usr/bin/env python3
"""macsurvey: is this macOS game a candidate for a machismo port?

Machismo (github.com/bmdhacks/machismo) runs Apple Silicon (arm64) Mach-O
games natively on aarch64 Linux by redirecting their dylib imports to Linux
libraries.  This tool reads a macOS build without extracting it to disk and
reports whether that can work: arm64 slice, imported libraries and Apple
frameworks, renderer (OpenGL / Metal / bgfx / Vulkan), Objective-C and Swift
use, engine, store SDKs, and a draft dylib_map.conf / machismo.conf.

Input: a .app bundle, a folder (Steam depot, extracted game), a single
Mach-O file, a GOG / Apple .pkg, a .zip (itch.io) or a .tar(.gz).
Standard library only (Python 3.8+).

    python macsurvey.py PATH [PATH...] [--json] [--conf DIR] [-v]
"""

import argparse
import bz2
import gzip
import hashlib
import json
import lzma
import os
import plistlib
import re
import stat
import struct
import sys
import tarfile
import zipfile
import zlib
import xml.etree.ElementTree as ET

# ---------------------------------------------------------------------------
# Byte streams (archives are read in one pass, nothing is written to disk)
# ---------------------------------------------------------------------------

CHUNK = 1 << 20


class Prepend:
    """File-like that returns `head` before the rest of `f`."""

    def __init__(self, head, f):
        self.head, self.f = head, f

    def read(self, n=-1):
        if n is None or n < 0:
            out, self.head = self.head + self.f.read(), b""
            return out
        if self.head:
            out, self.head = self.head[:n], self.head[n:]
            if len(out) < n:
                out += self.f.read(n - len(out))
            return out
        return self.f.read(n)


class Slice:
    """Read-only window [start, start+length) of a seekable file."""

    def __init__(self, f, start, length):
        self.f, self.pos, self.end = f, start, start + length

    def read(self, n=-1):
        if n is None or n < 0 or self.pos + n > self.end:
            n = self.end - self.pos
        self.f.seek(self.pos)
        out = self.f.read(n)
        self.pos += len(out)
        return out


class GenReader:
    """File-like over a generator of byte chunks."""

    def __init__(self, gen):
        self.gen, self.buf = gen, bytearray()

    def read(self, n=-1):
        while n is None or n < 0 or len(self.buf) < n:
            try:
                self.buf += next(self.gen)
            except StopIteration:
                break
        if n is None or n < 0:
            n = len(self.buf)
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out


def _zlib_chunks(f):
    d = zlib.decompressobj()
    while True:
        c = f.read(CHUNK)
        if not c:
            break
        yield d.decompress(c)
    yield d.flush()


def _pbzx_chunks(f):
    f.read(12)  # "pbzx" + flags
    while True:
        hdr = f.read(16)
        if len(hdr) < 16:
            return
        _usize, csize = struct.unpack(">QQ", hdr)
        data = f.read(csize)
        yield lzma.decompress(data) if data[:6] == b"\xfd7zXZ\x00" else data


def decode_stream(f):
    """Undo gzip / bzip2 / xz / zlib / pbzx layers until something else shows."""
    for _ in range(4):
        head = f.read(6)
        f = Prepend(head, f)
        if head[:2] == b"\x1f\x8b":
            f = gzip.GzipFile(fileobj=f)
        elif head[:3] == b"BZh":
            f = bz2.BZ2File(f)
        elif head == b"\xfd7zXZ\x00":
            f = lzma.LZMAFile(f)
        elif head[:4] == b"pbzx":
            f = GenReader(_pbzx_chunks(f))
        elif head[:1] == b"\x78" and head[1:2] in (b"\x01", b"\x5e", b"\x9c", b"\xda"):
            f = GenReader(_zlib_chunks(f))
        else:
            return f
    return f


def _skip(f, n):
    while n > 0:
        c = f.read(min(n, CHUNK))
        if not c:
            raise EOFError("archive truncated")
        n -= len(c)


def _read_exact(f, n):
    out = f.read(n)
    if len(out) != n:
        raise EOFError("archive truncated")
    return out


def iter_cpio(f):
    """Yield (path, mode, size, reader) for odc (070707) and newc (070701/2) cpio."""
    while True:
        magic = f.read(6)
        if len(magic) < 6:
            return
        if magic == b"070707":
            h = _read_exact(f, 70)
            mode = int(h[12:18], 8)
            namesize = int(h[53:59], 8)
            size = int(h[59:70], 8)
            name = _read_exact(f, namesize)[:-1].decode("utf-8", "replace")
            pad = 0
        elif magic in (b"070701", b"070702"):
            h = _read_exact(f, 104)
            fld = [int(h[i:i + 8], 16) for i in range(0, 104, 8)]
            mode, size, namesize = fld[1], fld[6], fld[11]
            name = _read_exact(f, namesize)[:-1].decode("utf-8", "replace")
            _read_exact(f, (-(110 + namesize)) % 4)
            pad = (-size) % 4
        else:
            raise ValueError("not a cpio archive (magic %r)" % magic)
        if name == "TRAILER!!!":
            return
        rd = _Limited(f, size)
        yield name, mode, size, rd
        _skip(f, rd.left)
        if pad:
            _read_exact(f, pad)


class _Limited:
    def __init__(self, f, n):
        self.f, self.left = f, n

    def read(self, n=-1):
        if n is None or n < 0 or n > self.left:
            n = self.left
        out = self.f.read(n)
        self.left -= len(out)
        return out


# ---------------------------------------------------------------------------
# Inventory: every file's path and size, plus the bytes of Mach-O files and
# Info.plists.
# ---------------------------------------------------------------------------

MACHO_MAGICS = (
    b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe",   # 64 / 32-bit little endian
    b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce",   # big endian (PowerPC)
    b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf",   # fat / fat64
)


def looks_macho(head):
    if head[:4] not in MACHO_MAGICS:
        return False
    if head[:3] == b"\xca\xfe\xba":             # Java class files share cafebabe
        return len(head) >= 8 and 0 < struct.unpack(">I", head[4:8])[0] < 20
    return True


class Inventory:
    def __init__(self, source):
        self.source = source
        self.files = {}     # path -> size
        self.machos = {}    # path -> bytes
        self.plists = {}    # path -> bytes
        self.notes = []

    def offer(self, path, size, reader):
        path = path.replace("\\", "/")
        while path.startswith("./"):
            path = path[2:]
        self.files[path] = size
        if ("/" + path.lower()).endswith("/contents/info.plist"):   # some ship info.plist
            self.plists[path] = reader.read()
            return
        if size < 64:
            return
        head = reader.read(8)
        if looks_macho(head):
            self.machos[path] = head + reader.read()


def inventory_from(path):
    inv = Inventory(path)
    low = path.lower()
    if os.path.isdir(path):
        base = os.path.dirname(os.path.abspath(path).rstrip("\\/"))
        for root, _dirs, names in os.walk(path):
            for n in names:
                full = os.path.join(root, n)
                if os.path.islink(full):
                    continue
                rel = os.path.relpath(full, base)
                with open(full, "rb") as f:
                    inv.offer(rel, os.path.getsize(full), f)
    elif low.endswith(".dmg"):
        raise SystemExit("%s: .dmg is not supported; extract it first "
                         "(7-Zip: 7z x game.dmg) and survey the folder" % path)
    elif zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if info.is_dir() or stat.S_ISLNK(info.external_attr >> 16):
                    continue
                with z.open(info) as f:
                    inv.offer(info.filename, info.file_size, f)
    elif _is_xar(path):
        _xar_inventory(path, inv)
    elif tarfile.is_tarfile(path):
        with tarfile.open(path, "r:*") as t:
            for m in t:
                if m.isfile():
                    inv.offer(m.name, m.size, t.extractfile(m))
    else:
        with open(path, "rb") as f:
            inv.offer(os.path.basename(path), os.path.getsize(path), f)
        if not inv.machos:
            raise SystemExit("%s: not a folder, archive, .pkg or Mach-O file" % path)
    return inv


def _is_xar(path):
    with open(path, "rb") as f:
        return f.read(4) == b"xar!"


def _xar_inventory(path, inv):
    with open(path, "rb") as f:
        hsize, _ver, toc_c, _toc_u = struct.unpack(">4xHHQQ", f.read(24))
        f.seek(hsize)
        toc = ET.fromstring(zlib.decompress(f.read(toc_c)))
        heap = hsize + toc_c
        payloads = []

        def walk(node, prefix):
            for fe in node.findall("file"):
                name = fe.findtext("name", "")
                full = prefix + name
                data = fe.find("data")
                if data is not None and name == "Payload":
                    style = data.find("encoding").get("style", "")
                    payloads.append((full, heap + int(data.findtext("offset")),
                                     int(data.findtext("length")), style))
                walk(fe, full + "/")

        walk(toc.find("toc"), "")
        if not payloads:
            inv.notes.append("pkg has no Payload (component pkgs missing?)")
        for full, off, length, style in payloads:
            raw = Slice(f, off, length)
            if "gzip" in style:   # xar's "x-gzip" is a zlib stream
                raw = GenReader(_zlib_chunks(raw))
            elif "bzip2" in style:
                raw = bz2.BZ2File(raw)
            stream = decode_stream(raw)
            sub = full[:-len("Payload")]
            for name, mode, size, rd in iter_cpio(stream):
                if stat.S_ISREG(mode):
                    inv.offer(sub + name, size, rd)


# ---------------------------------------------------------------------------
# Mach-O parsing
# ---------------------------------------------------------------------------

CPU = {7: "i386", 0x01000007: "x86_64", 12: "arm", 0x0100000C: "arm64",
       0x0200000C: "arm64_32", 18: "ppc", 0x01000012: "ppc64"}
FILETYPE = {1: "object", 2: "executable", 6: "dylib", 8: "bundle", 11: "dsym"}

LC_SEGMENT_64, LC_SYMTAB, LC_LOAD_DYLIB, LC_ID_DYLIB = 0x19, 0x2, 0xC, 0xD
LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB = 0x80000018, 0x8000001F, 0x20
LC_LOAD_UPWARD_DYLIB, LC_RPATH = 0x80000023, 0x8000001C
LC_DYLD_INFO, LC_DYLD_INFO_ONLY, LC_DYLD_CHAINED_FIXUPS = 0x22, 0x80000022, 0x80000034
LC_BUILD_VERSION, LC_VERSION_MIN_MACOSX = 0x32, 0x24
LC_ENCRYPTION_INFO, LC_ENCRYPTION_INFO_64 = 0x21, 0x2C
DYLIB_CMDS = {LC_LOAD_DYLIB: "load", LC_LOAD_WEAK_DYLIB: "weak", LC_REEXPORT_DYLIB: "reexport",
              LC_LAZY_LOAD_DYLIB: "lazy", LC_LOAD_UPWARD_DYLIB: "upward"}


def _cstr(buf, off):
    end = buf.find(b"\0", off)
    return buf[off:end if end >= 0 else len(buf)].decode("utf-8", "replace")


def _uleb(b, p):
    r = s = 0
    while True:
        x = b[p]
        p += 1
        r |= (x & 0x7F) << s
        s += 7
        if x < 0x80:
            return r, p


def _sleb(b, p):
    r = s = 0
    while True:
        x = b[p]
        p += 1
        r |= (x & 0x7F) << s
        s += 7
        if x < 0x80:
            if x & 0x40:
                r -= 1 << s
            return r, p


def _ver(v):
    return "%d.%d.%d" % (v >> 16, (v >> 8) & 0xFF, v & 0xFF)


def parse_macho(data):
    """Return {'archs': [...], 'slices': {arch: slice-dict}} for a Mach-O file."""
    magic = data[:4]
    out = {"archs": [], "slices": {}}
    if magic in (b"\xca\xfe\xba\xbe", b"\xca\xfe\xba\xbf"):
        n = struct.unpack(">I", data[4:8])[0]
        is64 = magic[3] == 0xBF
        ent = 32 if is64 else 20
        for i in range(n):
            p = 8 + i * ent
            if is64:
                cpu, sub, off, size = struct.unpack(">iiQQ", data[p:p + 24])
            else:
                cpu, sub, off, size = struct.unpack(">iiII", data[p:p + 16])
            _add_slice(out, data, off, size, cpu & 0xFFFFFFFF, sub)
    else:
        _add_slice(out, data, 0, len(data), None, None)
    return out


def _add_slice(out, data, off, size, cpu, sub):
    m = data[off:off + 4]
    if m in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe"):
        cpu, sub = struct.unpack_from("<ii", data, off + 4)
        cpu &= 0xFFFFFFFF
    elif m in (b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce"):
        cpu, sub = struct.unpack_from(">ii", data, off + 4)
        cpu &= 0xFFFFFFFF
    arch = CPU.get(cpu, "cpu%#x" % (cpu or 0))
    if arch == "arm64" and (sub & 0xFFFFFF) == 2:
        arch = "arm64e"
    out["archs"].append(arch)
    if m == b"\xcf\xfa\xed\xfe":
        try:
            out["slices"][arch] = _parse_thin64(memoryview(data)[off:off + size].tobytes())
        except (struct.error, IndexError, ValueError) as e:
            out["slices"][arch] = {"error": "unreadable Mach-O slice: %s" % e}


def _parse_thin64(b):
    _m, _cpu, _sub, ftype, ncmds, _sz, _flags, _r = struct.unpack_from("<IiiIIIII", b, 0)
    s = {"filetype": FILETYPE.get(ftype, str(ftype)), "dylibs": [], "rpaths": [],
         "id": None, "minos": None, "sdk": None, "encrypted": False, "fixups": "none",
         "sections": [], "imports": {}, "defined": set(), "objc_classes": 0,
         "objc_categories": 0, "swift": False, "text_size": 0, "data": b}
    symtab = dyld_info = chained = None
    p = 32
    for _ in range(ncmds):
        cmd, csize = struct.unpack_from("<II", b, p)
        body = b[p:p + csize]
        if cmd in DYLIB_CMDS:
            s["dylibs"].append((DYLIB_CMDS[cmd], _cstr(body, struct.unpack_from("<I", body, 8)[0])))
        elif cmd == LC_ID_DYLIB:
            s["id"] = _cstr(body, struct.unpack_from("<I", body, 8)[0])
        elif cmd == LC_RPATH:
            s["rpaths"].append(_cstr(body, struct.unpack_from("<I", body, 8)[0]))
        elif cmd == LC_SEGMENT_64:
            nsects = struct.unpack_from("<I", body, 64)[0]
            for i in range(nsects):
                q = 72 + i * 80
                sect = body[q:q + 16].rstrip(b"\0").decode("ascii", "replace")
                seg = body[q + 16:q + 32].rstrip(b"\0").decode("ascii", "replace")
                size = struct.unpack_from("<Q", body, q + 40)[0]
                s["sections"].append((seg, sect, size))
                if sect == "__text" and seg == "__TEXT":
                    s["text_size"] = size
                elif sect == "__objc_classlist":
                    s["objc_classes"] += size // 8
                elif sect == "__objc_catlist":
                    s["objc_categories"] += size // 8
                elif sect.startswith("__swift5"):
                    s["swift"] = True
        elif cmd == LC_SYMTAB:
            symtab = struct.unpack_from("<IIII", body, 8)
        elif cmd in (LC_DYLD_INFO, LC_DYLD_INFO_ONLY):
            dyld_info = struct.unpack_from("<10I", body, 8)
            s["fixups"] = "dyld_info"
        elif cmd == LC_DYLD_CHAINED_FIXUPS:
            chained = struct.unpack_from("<II", body, 8)
            s["fixups"] = "chained"
        elif cmd == LC_BUILD_VERSION:
            platform, minos, sdk = struct.unpack_from("<III", body, 8)
            s["minos"], s["sdk"] = _ver(minos), _ver(sdk)
            if platform != 1:
                s["platform"] = {2: "ios", 6: "maccatalyst"}.get(platform, str(platform))
        elif cmd == LC_VERSION_MIN_MACOSX:
            v, sdk = struct.unpack_from("<II", body, 8)
            s["minos"], s["sdk"] = _ver(v), _ver(sdk)
        elif cmd in (LC_ENCRYPTION_INFO, LC_ENCRYPTION_INFO_64):
            if struct.unpack_from("<I", body, 16)[0]:
                s["encrypted"] = True
        p += csize

    imports = s["imports"]
    if chained:
        _chained_imports(b, chained[0], imports)
    if dyld_info:
        for off, size in ((dyld_info[2], dyld_info[3]), (dyld_info[6], dyld_info[7])):
            _bind_imports(b, off, size, imports)
    if symtab:
        symoff, nsyms, stroff, strsize = symtab
        strtab = b[stroff:stroff + strsize]
        for i in range(nsyms):
            strx, ntype, _sect, desc, _val = struct.unpack_from("<IBBHQ", b, symoff + 16 * i)
            if ntype & 0xE0:           # debug (stab) entry
                continue
            name = _cstr(strtab, strx)
            if (ntype & 0x0E) == 0 and ntype & 1:          # undefined external
                ordinal = (desc >> 8) & 0xFF
                if imports.get(name) is None:
                    imports[name] = {0xFE: -2, 0xFF: -1}.get(ordinal, ordinal)
            elif (ntype & 0x0E) == 0x0E and name:          # defined in a section
                s["defined"].add(name)
    return s


def _chained_imports(b, off, imports):
    _ver_, _starts, imp_off, sym_off, count, fmt, sym_fmt = struct.unpack_from("<7I", b, off)
    pool = b[off + sym_off:]
    if sym_fmt == 1:
        pool = zlib.decompress(pool)
    base = off + imp_off
    for i in range(count):
        if fmt == 1:
            v = struct.unpack_from("<I", b, base + 4 * i)[0]
            ordinal, name_off = v & 0xFF, v >> 9
            ordinal = ordinal - 256 if ordinal > 0xF0 else ordinal
        elif fmt == 2:
            v = struct.unpack_from("<I", b, base + 8 * i)[0]
            ordinal, name_off = v & 0xFF, v >> 9
            ordinal = ordinal - 256 if ordinal > 0xF0 else ordinal
        elif fmt == 3:
            v = struct.unpack_from("<Q", b, base + 16 * i)[0]
            ordinal, name_off = v & 0xFFFF, v >> 32
            ordinal = ordinal - 65536 if ordinal > 0xFFF0 else ordinal
        else:
            return
        imports.setdefault(_cstr(pool, name_off), ordinal)


def _bind_imports(b, off, size, imports):
    p, end, ordinal, name = off, off + size, 0, None
    while p < end:
        x = b[p]
        p += 1
        op, imm = x & 0xF0, x & 0x0F
        if op == 0x10:
            ordinal = imm
        elif op == 0x20:
            ordinal, p = _uleb(b, p)
        elif op == 0x30:
            ordinal = (imm | 0xF0) - 256 if imm else 0
        elif op == 0x40:
            e = b.index(b"\0", p)
            name = b[p:e].decode("utf-8", "replace")
            p = e + 1
        elif op == 0x60:
            _, p = _sleb(b, p)
        elif op in (0x70, 0x80, 0xA0):
            _, p = _uleb(b, p)
        elif op == 0xC0:
            _, p = _uleb(b, p)
            _, p = _uleb(b, p)
        elif op == 0xD0 and imm == 0:
            _, p = _uleb(b, p)
        if op in (0x90, 0xA0, 0xB0, 0xC0) and name is not None:
            imports.setdefault(name, ordinal)


# ---------------------------------------------------------------------------
# Knowledge: what each dependency becomes under machismo
# ---------------------------------------------------------------------------

# (regex on the library's short name, kind, dylib_map value, note)
# kinds: shim, portable, portable-cxx, stub, skip, proprietary, engine, swift, vulkan
KNOWN_LIBS = [
    (r"^libSystem\.B$", "shim", "libs/libsystem_shim.so", "machismo libSystem shim"),
    (r"^libc\+\+\.1$", "shim", "libs/libc++.so.1", "Apple-ABI libc++ (machismo scripts/build-libcxx.sh)"),
    (r"^libc\+\+abi$", "shim", "libs/libc++abi.so.1", "Apple-ABI libc++abi"),
    (r"^libobjc", "stub", "STUB", "Objective-C runtime is a stub: real use must be patched out"),
    (r"^libswift", "swift", None, "Swift runtime: not supported by machismo"),
    (r"^libz(\.|$)", "portable", "libz.so.1", ""),
    (r"^libbz2", "portable", "libbz2.so.1.0", ""),
    (r"^libiconv", "skip", "SKIP", "glibc has iconv"),
    (r"^libresolv", "skip", "SKIP", ""),
    (r"^libcurl", "portable", "libcurl.so.4",
     "curl string options broke across ABIs in NecroDancer: plan to patch HTTP out"),
    (r"^libsqlite3", "portable", "libsqlite3.so.0", ""),
    (r"^libxml2", "portable", "libxml2.so.2", ""),
    (r"^(lib)?SDL2_mixer", "portable", "libSDL2_mixer-2.0.so.0", ""),
    (r"^(lib)?SDL2_image", "portable", "libSDL2_image-2.0.so.0", ""),
    (r"^(lib)?SDL2_ttf", "portable", "libSDL2_ttf-2.0.so.0", ""),
    (r"^(lib)?SDL2_net", "portable", "libSDL2_net-2.0.so.0", ""),
    (r"^(lib)?SDL2(-2\.0)?", "portable", "libSDL2-2.0.so.0", ""),
    (r"^(lib)?SDL3", "portable", "libSDL3.so.0", "CFWs ship SDL2 only: needs an SDL3-on-SDL2 shim"),
    (r"^libfreetype", "portable", "libfreetype.so.6", ""),
    (r"^(libopenal|OpenAL$)", "portable", "libopenal.so.1", ""),
    (r"^libvorbisfile", "portable", "libvorbisfile.so.3", ""),
    (r"^libvorbisenc", "portable", "libvorbisenc.so.2", ""),
    (r"^libvorbis", "portable", "libvorbis.so.0", ""),
    (r"^libogg", "portable", "libogg.so.0", ""),
    (r"^libFLAC", "portable", "libFLAC.so.8", ""),
    (r"^libtheoradec", "portable", "libtheoradec.so.1", ""),
    (r"^libtheora", "portable", "libtheora.so.0", ""),
    (r"^libopusfile", "portable", "libopusfile.so.0", ""),
    (r"^libopus", "portable", "libopus.so.0", ""),
    (r"^libmpg123", "portable", "libmpg123.so.0", ""),
    (r"^libsndfile", "portable", "libsndfile.so.1", ""),
    (r"^libpng", "portable", "libpng16.so.16", ""),
    (r"^libjpeg", "portable", "libjpeg.so.62", ""),
    (r"^libwebp", "portable", "libwebp.so.6", ""),
    (r"^libphysfs", "portable", "libphysfs.so.1", ""),
    (r"^libzstd", "portable", "libzstd.so.1", ""),
    (r"^libluajit", "portable", "libluajit-5.1.so.2", ""),
    (r"^liblua", "portable", None, "Lua: needs a Linux .so of the same Lua version"),
    (r"^libglfw|^GLFW$", "portable", "libglfw.so.3", "not on CFWs: build and ship it"),
    (r"^libGLEW", "portable", "libGLEW.so.2.1", "desktop GL loader: runs on gl4es"),
    (r"^liballegro", "portable", None, "Allegro 5: build for Linux and ship it"),
    (r"^libsfml-", "portable-cxx", "libs/{name}.so.2.5",
     "C++ API: build with machismo scripts/build-sfml.sh (Apple-ABI libc++), or load as MACHO:"),
    (r"^libbgfx", "portable-cxx", "libs/libbgfx-shared.so", "machismo scripts/build-bgfx.sh (GLES)"),
    (r"^libsteam_api", "stub", "STUB",
     "Steamworks: STUB, then patch the game's SteamAPI init (see machismo examples/sugar/patches)"),
    (r"^libGalaxy", "shim", "libs/libgalaxy_shim.so", "machismo GOG Galaxy shim"),
    (r"^libEOSSDK", "stub", "STUB", "Epic Online Services"),
    (r"^(lib)?discord", "stub", "STUB", "Discord SDK"),
    (r"(?i)sentry|crashpad|breakpad|crashreporter", "stub", "STUB", "crash reporter"),
    (r"^libfmod", "proprietary", None,
     "FMOD: needs the Linux arm64 FMOD of the same version (fmod.com); C API maps directly"),
    (r"(?i)^libAkSoundEngine|wwise", "proprietary", None, "Wwise: no drop-in Linux arm64 build"),
    (r"(?i)^(lib)?bink", "proprietary", None, "Bink video: no Linux arm64 drop-in"),
    (r"^libmono|^Mono$|^libcoreclr|^libhostfxr|^libclrjit|^libhostpolicy", "engine", None, ".NET / Mono runtime"),
    (r"^UnityPlayer", "engine", None, "Unity"),
    (r"^love$", "engine", None, "LÖVE"),
    (r"^libhl$", "engine", None, "HashLink"),
    (r"^libjvm|^libjli", "engine", None, "Java"),
    (r"^(Electron|nwjs) Framework", "engine", None, "Electron / NW.js"),
    (r"^libMoltenVK|^libvulkan", "vulkan", "libvulkan.so.1", "Vulkan (MoltenVK): device needs a Vulkan driver"),
]

# Apple frameworks with something better than SKIP.
FRAMEWORK_MAP = {
    "OpenGL": ("gl", "DEFERRED:OPENGL_LIB", "gl4es (machismo examples/sugar); the Shotgun King port "
               "leaves OpenGL unmapped and lets libsystem_shim send it to libGLESv2"),
    "OpenAL": ("portable", "libopenal.so.1", "Apple OpenAL is the plain C API"),
    "Metal": ("metal", "SKIP", "Metal: no translation yet (bmdhacks/airlift is early)"),
    "MetalKit": ("metal", "SKIP", "Metal: no translation yet"),
    "MetalFX": ("metal", "SKIP", "Metal: no translation yet"),
}
# Frameworks whose real use means work (input, audio, windowing).
HEAVY_FRAMEWORKS = {"AppKit", "Cocoa", "Carbon", "GameController", "AudioToolbox", "AudioUnit",
                    "CoreAudio", "AVFoundation", "AVKit", "CoreMedia", "IOKit", "ForceFeedback",
                    "CoreHaptics", "GameKit", "StoreKit", "WebKit", "SpriteKit", "SceneKit"}

# Strings in game code -> (evidence key, label)
STRING_HINTS = [
    (b"Godot Engine", "engine", "Godot"),
    (b"FEngineLoop", "engine", "Unreal"),
    (b"UnrealEngine", "engine", "Unreal"),
    (b"YoYo Games", "engine", "GameMaker"),
    (b"YYGML", "engine", "GameMaker"),
    (b"cocos2d-x", "engine", "cocos2d-x"),
    (b"hxcpp", "engine", "Haxe (hxcpp / OpenFL / Lime)"),
    (b"N5sugar", "engine", "Sugar (PUNKCAKE)"),
    (b"Clickteam", "engine", "Clickteam Fusion"),
    (b"raylib", "lib", "raylib"),
    (b"Allegro", "lib", "Allegro"),
    (b"LuaJIT 2.", "lib", "LuaJIT (static)"),
    (b"Lua 5.", "lib", "Lua (static)"),
    (b"FMOD", "lib", "FMOD (static)"),
    (b"AkSoundEngine", "lib", "Wwise (static)"),
    (b"RAD Game Tools", "lib", "Bink / RAD (static)"),
    (b"N4bgfx", "lib", "bgfx (static)"),
    (b"_bgfx_init", "lib", "bgfx (static)"),
    (b"MTLCreateSystemDefaultDevice", "gfx", "metal"),
    (b"/System/Library/Frameworks/OpenGL.framework", "gfx", "gl"),
    (b"vkCreateInstance", "gfx", "vulkan"),
    (b"libMoltenVK", "gfx", "vulkan"),
]

ENGINE_FILES = [
    (r"(^|/)UnityPlayer\.dylib$|/Resources/Data/(globalgamemanagers|mainData|data\.unity3d)$"
     r"|/Resources/Data/Managed/", "Unity"),
    (r"/game\.ios$", "GameMaker"),
    (r"\.love$|(^|/)love\.framework/", "LÖVE"),
    (r"(^|/)hlboot\.dat$|(^|/)libhl\.dylib$", "HashLink"),
    (r"(^|/)FNA\.dll$", "FNA (.NET)"),
    (r"(^|/)MonoGame\.Framework\.dll$", "MonoGame (.NET)"),
    (r"(^|/)(Mono\.framework|libmonobdwgc[^/]*\.dylib|libcoreclr\.dylib)", ".NET / Mono"),
    (r"\.jar$", "Java"),
    (r"Electron Framework\.framework/|nwjs Framework\.framework/|(^|/)app\.asar$", "Electron / NW.js"),
    (r"(^|/)renpy/|\.rpa$", "Ren'Py"),
    (r"/Content/Paks/[^/]+\.pak$", "Unreal"),
    (r"Adobe AIR\.framework/", "Adobe AIR"),
    (r"\.sgr$", "Sugar (PUNKCAKE)"),
    (r"\.ccn$", "Clickteam Fusion"),
    (r"(^|/)js/(rpg|rmmz)_core\.js$", "RPG Maker MV/MZ"),
]

OTHER_ROUTE = {
    "Unity": "Unity: use the Linux/Windows build with box64 (unity/unityport in PM-Porting-Tools)",
    "GameMaker": "GameMaker: Mac runner is Cocoa/Metal; look at the Android/Linux route",
    "LÖVE": "LÖVE: PortMaster's love runtime runs the .love directly",
    "HashLink": "HashLink: hlboot.dat runs on a native aarch64 HashLink",
    "FNA (.NET)": "FNA: PortMaster's Mono/.NET runtimes run the managed DLLs",
    "MonoGame (.NET)": "MonoGame: PortMaster's Mono/.NET runtimes",
    ".NET / Mono": ".NET: PortMaster's Mono/.NET runtimes",
    "Java": "Java: PortMaster's JRE runtime",
    "Electron / NW.js": "Electron/NW.js: web game, not native code",
    "Ren'Py": "Ren'Py: PortMaster's Ren'Py runtime",
    "Unreal": "Unreal: Mac build is Metal + Cocoa",
    "Adobe AIR": "Adobe AIR: no arm64 Linux runtime",
    "Godot": "Godot: PortMaster's Godot runtimes run the .pck",
    "RPG Maker MV/MZ": "RPG Maker MV/MZ: web game (NW.js)",
}
# Engines that can work but whose Mac runner is mostly Objective-C.
HARD_ENGINES = {
    "Clickteam Fusion": "Clickteam Fusion: the Mac runner is an Objective-C/Cocoa app (heavy AppKit use)",
}


def short_name(install_name):
    """'@rpath/SDL2.framework/Versions/A/SDL2' -> 'SDL2', '/usr/lib/libz.1.dylib' -> 'libz.1'."""
    m = re.search(r"([^/]+)\.framework/", install_name)
    if m:
        return m.group(1)
    base = install_name.rsplit("/", 1)[-1]
    return base[:-6] if base.endswith(".dylib") else base


def is_apple_framework(install_name):
    return install_name.startswith(("/System/Library/", "/usr/lib/"))


def classify(install_name):
    name = short_name(install_name)
    for rx, kind, value, note in KNOWN_LIBS:
        if re.search(rx, name):
            return kind, (value.format(name=name) if value else None), note
    if install_name.startswith("/System/Library/"):
        if name in FRAMEWORK_MAP:
            return FRAMEWORK_MAP[name]
        return "framework", "SKIP", ""
    if install_name.startswith("/usr/lib/"):
        return "framework", "SKIP", "Apple system library"
    return "bundled", None, "game-specific: load as MACHO:"


# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

PICK_ORDER = ("arm64", "arm64e")


def find_apps(inv):
    """Map app root ('Foo.app') -> Info.plist dict; outermost bundles only."""
    apps = {}
    for p in sorted(inv.plists, key=len):
        root = p[: -len("/Contents/Info.plist")] if len(p) > len("Contents/Info.plist") else ""
        if not root.endswith(".app") and root:
            continue
        if any(root.startswith(a + "/") for a in apps):
            continue
        try:
            info = plistlib.loads(inv.plists[p])
        except Exception:
            info = {}
        apps[root] = info
    # Bundles without a readable plist still count as apps
    for p in sorted(inv.machos, key=len):
        m = re.match(r"(.*?\.app)/Contents/", p)
        if m and m.group(1) not in apps and not any(m.group(1).startswith(a + "/") for a in apps):
            apps[m.group(1)] = {}
    return apps


class Binary:
    def __init__(self, path, data):
        self.path = path
        self.size = len(data)
        self.md5 = hashlib.md5(data).hexdigest()
        parsed = parse_macho(data)
        self.archs = parsed["archs"]
        self.slice = None
        self.arch = None
        for a in PICK_ORDER + tuple(parsed["slices"]):
            sl = parsed["slices"].get(a)
            if sl and "error" not in sl:
                self.arch, self.slice = a, sl
                break
        self.errors = [s["error"] for s in parsed["slices"].values() if "error" in s]

    @property
    def arm64(self):
        return self.arch in PICK_ORDER

    def imports_by_lib(self):
        """{install_name or '(flat)': [symbols]} for this binary's imports."""
        out = {}
        if not self.slice:
            return out
        dylibs = self.slice["dylibs"]
        for sym, ordinal in self.slice["imports"].items():
            if ordinal is not None and 1 <= ordinal <= len(dylibs):
                key = dylibs[ordinal - 1][1]
            else:
                key = "(flat lookup)" if ordinal in (-2, -3) else "(self)"
            out.setdefault(key, []).append(sym)
        return out


def analyse_app(inv, root, info, survey_root):
    prefix = root + "/" if root else ""
    machos = {p: d for p, d in inv.machos.items() if p.startswith(prefix)}
    files = [p for p in inv.files if p.startswith(prefix)]
    exe_name = info.get("CFBundleExecutable")
    main_path = prefix + "Contents/MacOS/" + exe_name if exe_name else None
    rep = {
        "app": root or "(loose files)",
        "bundle_id": info.get("CFBundleIdentifier"),
        "version": info.get("CFBundleShortVersionString") or info.get("CFBundleVersion"),
        "executable": main_path,
        "verdict": None, "reasons": [], "cautions": [], "notes": [],
        "evidence": {"engine": set(), "lib": set(), "gfx": set(), "platform": set()},
    }
    if not machos:
        rep["verdict"] = "NO BINARY"
        rep["reasons"].append("no Mach-O files in this bundle (assets-only depot?)")
        return rep, []
    if main_path not in machos:
        exes = [p for p in machos if "/Contents/MacOS/" in p or not root]
        if main_path:
            rep["notes"].append("Info.plist executable %s missing" % exe_name)
        if not exes:
            rep["verdict"] = "NO BINARY"
            rep["reasons"].append("main executable not found (assets-only depot?)")
            return rep, []
        main_path = sorted(exes, key=lambda p: -len(machos[p]))[0]
        rep["executable"] = main_path

    bins = {p: Binary(p, d) for p, d in machos.items()}
    main = bins[main_path]
    rep["md5"] = main.md5
    rep["archs"] = main.archs
    rep["arch_used"] = main.arch
    if main.slice:
        rep["minos"], rep["sdk"] = main.slice["minos"], main.slice["sdk"]
        rep["fixups"] = main.slice["fixups"]

    # Engine from file names
    for rx, label in ENGINE_FILES:
        if any(re.search(rx, p) for p in files):
            rep["evidence"]["engine"].add(label)
    if any(p.endswith(".metallib") for p in files):
        rep["evidence"]["gfx"].add("metal")
        rep["notes"].append("bundle ships .metallib shaders")

    # Resolve which Mach-O files the game actually runs as Mach-O ("kept"):
    # the executable plus bundled libraries that have no native replacement.
    by_short = {}
    for p in bins:
        by_short.setdefault(short_name(p), p)
        if bins[p].slice and bins[p].slice["id"]:
            by_short.setdefault(short_name(bins[p].slice["id"]), p)
    kept, deps, todo = [], {}, [main_path]
    while todo:
        p = todo.pop()
        if p in kept:
            continue
        kept.append(p)
        b = bins[p]
        if not b.slice:
            continue
        for kind_ld, inst in b.slice["dylibs"]:
            key = short_name(inst)
            d = deps.setdefault(key, {"install_name": inst, "users": [], "symbols": set(),
                                      "weak": kind_ld == "weak", "path": None})
            d["users"].append(p)
            kind, value, note = classify(inst)
            d.update(kind=kind, value=value, note=note)
            local = by_short.get(key) if not is_apple_framework(inst) else None
            if local:
                d["path"] = local
                if kind in ("bundled", "proprietary"):
                    todo.append(local)
            elif kind == "bundled":
                d["note"] = "not found in the bundle"
        for inst, syms in b.imports_by_lib().items():
            key = short_name(inst)
            if key in deps:
                deps[key]["symbols"].update(syms)
    rep["kept"] = kept

    # Unknown bundled libs: point MACHO: at their path inside the port's gamedata
    # (the .app goes straight into gamedata/, like NecroDancer)
    for d in deps.values():
        if d["kind"] == "bundled" and d["path"]:
            rel = d["path"]
            if root:
                rel = root.rsplit("/", 1)[-1] + "/" + rel[len(prefix):]
            elif survey_root and rel.startswith(survey_root + "/"):
                rel = rel[len(survey_root) + 1:]
            d["value"] = "MACHO:gamedata/" + rel
        if d["kind"] == "portable-cxx" and not any(s.startswith("__Z") for s in d["symbols"]):
            d["kind"] = "portable"

    # Evidence from game code (kept binaries only)
    ev = rep["evidence"]
    all_imports, all_defined = set(), set()
    apple_classes, objc_own, objc_cats = set(), 0, 0
    for p in kept:
        b = bins[p]
        if not b.slice:
            continue
        s = b.slice
        all_imports.update(s["imports"])
        all_defined.update(s["defined"])
        objc_own += s["objc_classes"]
        objc_cats += s["objc_categories"]
        if s["swift"]:
            rep["reasons"].append("Swift code in %s" % p.rsplit("/", 1)[-1])
        if s["encrypted"]:
            rep["reasons"].append("%s is encrypted (Mac App Store FairPlay)" % p.rsplit("/", 1)[-1])
        if s.get("platform"):
            rep["cautions"].append("built for platform %s" % s["platform"])
        data = s["data"]
        for needle, key, label in STRING_HINTS:
            if needle in data:
                ev[key].add(label)
    for key, d in deps.items():
        if d["kind"] in ("framework", "metal", "gl", "shim", "stub", "portable") and \
                is_apple_framework(d["install_name"]):
            for sym in d["symbols"]:
                if sym.startswith(("_OBJC_CLASS_$_", "_OBJC_METACLASS_$_")):
                    apple_classes.add(sym.split("$_", 1)[1])
    names = all_imports | all_defined

    def has(*prefixes):
        return any(n.startswith(prefixes) for n in names)

    # Platform layer
    if "SDL2" in deps or "libSDL2-2.0.0" in deps or any(k.startswith(("libSDL2", "SDL2")) for k in deps):
        ev["platform"].add("SDL2")
    if any(k.startswith(("libSDL3", "SDL3")) for k in deps):
        ev["platform"].add("SDL3")
    static_sdl = any(n.startswith("_SDL_") for n in all_defined) or (
        b"SDL_CreateWindow" in bins[main_path].slice["data"] if main.slice else False)
    if static_sdl and not ev["platform"] & {"SDL2", "SDL3"}:
        sdl3 = has("_SDL_CreateProperties", "_SDL_IOFromFile") or \
            (main.slice and b"SDL_CreateProperties" in main.slice["data"])
        ev["platform"].add("SDL3 (static)" if sdl3 else "SDL2 (static)")
    if any(k.startswith("libsfml-window") for k in deps) or has("__ZN2sf6Window"):
        ev["platform"].add("SFML")
    if any(k.startswith(("libglfw", "GLFW")) for k in deps) or has("_glfwCreateWindow"):
        ev["platform"].add("GLFW")
    if any(k.startswith("liballegro") for k in deps) or "Allegro" in ev["lib"]:
        ev["platform"].add("Allegro")
    if "raylib" in ev["lib"]:
        ev["platform"].add("raylib (GLFW)")
    cocoa = apple_classes & {"NSWindow", "NSApplication", "NSView", "NSOpenGLView",
                             "NSOpenGLContext", "NSEvent", "NSScreen", "MTKView"}
    if cocoa and not ev["platform"]:
        ev["platform"].add("Cocoa (own AppKit code)")

    # Graphics
    if any(d["kind"] == "gl" for d in deps.values()) or has("_SDL_GL_", "_CGL", "_glfwCreateWindow") \
            or "SFML" in ev["platform"] or apple_classes & {"NSOpenGLContext", "NSOpenGLView",
                                                            "NSOpenGLPixelFormat"}:
        ev["gfx"].add("gl")
    if any(d["kind"] == "metal" and d["symbols"] for d in deps.values()) or \
            has("_SDL_Metal_") or apple_classes & {"CAMetalLayer", "MTKView"}:
        ev["gfx"].add("metal")
    if any(d["kind"] == "vulkan" for d in deps.values()) or has("_vkCreateInstance", "_SDL_Vulkan_"):
        ev["gfx"].add("vulkan")
    if has("_SDL_CreateRenderer"):
        ev["gfx"].add("sdl-renderer")
    if any(k.startswith("libbgfx") for k in deps) or "bgfx (static)" in ev["lib"] or has("__ZN4bgfx", "_bgfx_"):
        ev["gfx"].add("bgfx")

    # Symbol names make patches survive game updates ("at sym:NAME" in machismo patch files)
    main_defined = main.slice["defined"] if main.slice else set()
    rep["defined_symbols"] = len(main_defined)
    if "Sugar (PUNKCAKE)" in ev["engine"]:
        sdl = "sdl3" if any(p.startswith("SDL3") for p in ev["platform"]) else "sdl2"
        if "__ZN5sugar5steam10init_steamEj" in main_defined:
            rep["notes"].append("sugar::steam::init_steam present: Shotgun King's symbolic Steam "
                                "patch applies as is")
        if any(n.startswith("__ZN5sugar5audio9_load_ogg") for n in main_defined):
            rep["notes"].append("sugar::audio::_load_ogg present: machismo's libsugar_patches_%s.so "
                                "(PCM cache) can hook it" % sdl)
        rep["notes"].append("closest existing port: %s" % (
            "Shotgun King (SDL3)" if sdl == "sdl3" else "The Wratch's Den (SDL2)"))
    rep["objc"] = {"own_classes": objc_own, "categories": objc_cats,
                   "apple_classes": sorted(apple_classes),
                   "msgSend": "_objc_msgSend" in all_imports}
    rep["deps"] = deps
    rep["main"] = main
    rep["bins"] = bins
    _verdict(rep)
    return rep, bins


def _verdict(rep):
    ev, reasons, cautions = rep["evidence"], rep["reasons"], rep["cautions"]
    main = rep["main"]
    if main.errors:
        cautions.extend(main.errors)
    if not main.arm64:
        reasons.append("no arm64 code (archs: %s): machismo needs an Apple Silicon build; "
                       "an x86_64 Mac build is a box64/Wine job" % ", ".join(main.archs))
    elif main.arch == "arm64e":
        cautions.append("arm64e only (pointer authentication): PAC instructions need cores with ARMv8.3")
    other = [OTHER_ROUTE[e] for e in sorted(ev["engine"]) if e in OTHER_ROUTE]
    cautions.extend(HARD_ENGINES[e] for e in sorted(ev["engine"]) if e in HARD_ENGINES)
    for d in rep["deps"].values():
        if d["kind"] == "swift":
            reasons.append("links the Swift runtime (%s)" % short_name(d["install_name"]))
        elif d["kind"] == "engine" and d["note"] in OTHER_ROUTE:
            other.append(OTHER_ROUTE[d["note"]])
        elif d["kind"] == "proprietary":
            cautions.append(d["note"])
        elif d["kind"] == "bundled" and not d["path"]:
            cautions.append("%s is linked but not in the bundle" % d["install_name"])
        elif d["kind"] == "stub" and d["value"] == "STUB" and short_name(d["install_name"]).startswith("libsteam_api"):
            init = sorted(s for s in d["symbols"] if "Init" in s)
            if init:
                cautions.append("calls %s: the STUB returns 0, patch the init path "
                                "(Shotgun King / NecroDancer patches show how)" % ", ".join(init))
    for lib in ("Wwise (static)", "Bink / RAD (static)", "FMOD (static)"):
        if lib in ev["lib"]:
            cautions.append("%s inside the game binary: its Mac audio/video backend must work "
                            "through the shims" % lib)
    gfx = ev["gfx"]
    if "bgfx" in gfx:
        rep["renderer"] = "bgfx -> machismo bgfx trampoline (native GLES build)"
    elif "gl" in gfx:
        rep["renderer"] = "OpenGL -> gl4es (DEFERRED:OPENGL_LIB)"
    elif "sdl-renderer" in gfx and "metal" not in gfx:
        rep["renderer"] = "SDL_Renderer -> SDL's GLES2 renderer"
    elif "vulkan" in gfx:
        rep["renderer"] = "Vulkan (MoltenVK) -> device Vulkan driver"
        cautions.append("Vulkan renderer: needs a Vulkan driver on the device (Panfrost/PanVK, Turnip)")
    elif "metal" in gfx:
        rep["renderer"] = "Metal only"
        reasons.append("Metal-only renderer: no Metal translation yet (bmdhacks/airlift is early)")
    else:
        rep["renderer"] = "unknown"
        cautions.append("no renderer found (dlopen'd at run time?)")
    if "sdl-renderer" in gfx and "metal" in gfx and "gl" not in gfx:
        cautions.append("SDL_Renderer plus Metal hints: check the game doesn't force the Metal renderer")

    plat = ev["platform"]
    objc = rep["objc"]
    heavy = sorted({short_name(d["install_name"]) for d in rep["deps"].values()
                    if d["kind"] == "framework" and short_name(d["install_name"]) in HEAVY_FRAMEWORKS
                    and d["symbols"]})
    if "Cocoa (own AppKit code)" in plat:
        cautions.append("own Cocoa window/input code: machismo has no AppKit, this needs "
                        "replacement code (override_lib)")
    elif not plat:
        cautions.append("no SDL/SFML/GLFW/Allegro platform layer found")
    if heavy:
        cautions.append("game code calls %s: each call site needs a stub or patch" % ", ".join(heavy))
    if objc["own_classes"] > 10 or len(objc["apple_classes"]) > 15:
        cautions.append("heavy Objective-C: %d own classes, %d Apple classes used" %
                        (objc["own_classes"], len(objc["apple_classes"])))
    if other:
        rep["verdict"] = "OTHER ROUTE"
        rep["reasons"] = sorted(set(other)) + reasons
    elif reasons:
        rep["verdict"] = "BLOCKED"
    elif any(c.startswith(("own Cocoa", "heavy Objective-C", "Clickteam")) for c in cautions) or \
            rep["renderer"] == "unknown":
        rep["verdict"] = "HARD"
    elif cautions:
        rep["verdict"] = "CANDIDATE (with work)"
    else:
        rep["verdict"] = "GOOD CANDIDATE"


# ---------------------------------------------------------------------------
# Draft machismo config
# ---------------------------------------------------------------------------

def draft_dylib_map(rep):
    lines = ["# Draft dylib map for %s (macsurvey)" % rep["app"],
             "# Paths are relative to the port directory. First substring match wins:",
             "# keep longer names (libvorbisfile) above shorter ones (libvorbis).", ""]
    order = {"shim": 0, "portable": 1, "portable-cxx": 1, "gl": 2, "vulkan": 2, "bundled": 3,
             "proprietary": 4, "stub": 5, "skip": 6, "engine": 7, "swift": 7, "metal": 8, "framework": 8}
    deps = sorted(rep["deps"].items(), key=lambda kv: (order.get(kv[1]["kind"], 9), -len(kv[0]), kv[0]))
    for key, d in deps:
        value = d["value"] or "TODO"
        comment = d["note"]
        if d["kind"] in ("framework", "metal") and d["symbols"]:
            shown = sorted(d["symbols"])[:4]
            comment = "TODO: game code uses %d symbol(s): %s%s" % (
                len(d["symbols"]), ", ".join(shown), ", ..." if len(d["symbols"]) > 4 else "")
        lines.append("%s = %s%s" % (key, value, ("  # " + comment) if comment else ""))
    return "\n".join(lines) + "\n"


def draft_machismo_conf(rep, slug):
    ev = rep["evidence"]
    out = ["# Draft machismo config for %s (macsurvey)" % rep["app"], "",
           "[general]", "dylib_map = conf/dylib_map.conf", "patches = conf/patches/%s.conf" % slug, ""]
    plat = ev["platform"]
    if "SDL2 (static)" in plat:
        out += ["[trampoline.sdl2]", "lib = libSDL2-2.0.so.0", "prefix = _SDL_", ""]
    if "SDL3 (static)" in plat:
        out += ["[trampoline.sdl3]", "lib = libSDL3.so.0", "prefix = _SDL_", ""]
    if "bgfx" in ev["gfx"] and not any(k.startswith("libbgfx") for k in rep["deps"]):
        out += ["[trampoline.bgfx]", "lib = libs/libbgfx-shared.so", "prefix = _bgfx_",
                "prefix = __ZN4bgfx", "init_wrapper = true", "renderer = opengles", ""]
    if "LuaJIT (static)" in ev["lib"] and not any(k.startswith("libluajit") for k in rep["deps"]):
        out += ["[trampoline.luajit]", "lib = libluajit-5.1.so.2", "prefix = _lua", ""]
    return "\n".join(out)


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def slugify(s):
    return re.sub(r"[^a-z0-9]+", "", s.lower().rsplit("/", 1)[-1].replace(".app", "")) or "game"


def print_report(inv, reps, verbose):
    print("=" * 78)
    print("macsurvey: %s" % inv.source)
    print("  %d files, %d Mach-O, %d app bundle(s)" % (len(inv.files), len(inv.machos), len(reps)))
    for n in inv.notes:
        print("  note: %s" % n)
    for rep in reps:
        print("-" * 78)
        title = rep["app"]
        if rep.get("bundle_id"):
            title += "  (%s%s)" % (rep["bundle_id"], " " + str(rep["version"]) if rep.get("version") else "")
        print(title)
        print("  VERDICT      %s" % rep["verdict"])
        for r in rep["reasons"]:
            print("    - %s" % r)
        if "main" not in rep:
            continue
        main = rep["main"]
        print("  executable   %s" % rep["executable"])
        print("  archs        %s (using %s)%s" % (", ".join(main.archs), main.arch or "none",
                                                  "  min macOS %s, SDK %s" % (rep["minos"], rep["sdk"])
                                                  if rep.get("minos") else ""))
        if main.slice:
            print("  fixups       %s, %.1f MB code" % (rep["fixups"], main.slice["text_size"] / 1e6))
        print("  md5          %s  (KNOWN_MD5 for the launcher)" % rep["md5"])
        ev = rep["evidence"]
        eng = sorted(ev["engine"]) or ["custom / unknown"]
        print("  engine       %s" % ", ".join(eng))
        if ev["lib"]:
            print("  inside       %s" % ", ".join(sorted(ev["lib"])))
        print("  platform     %s" % (", ".join(sorted(ev["platform"])) or "none found"))
        print("  renderer     %s  [%s]" % (rep["renderer"], ", ".join(sorted(ev["gfx"])) or "-"))
        o = rep["objc"]
        n = rep.get("defined_symbols", 0)
        print("  symbols      %d defined in the executable%s" % (
            n, ": not stripped, patches can use 'at sym:NAME'" if n > 200 else " (stripped: address patches)"))
        print("  Objective-C  %d own classes, %d categories, %d Apple classes used%s" % (
            o["own_classes"], o["categories"], len(o["apple_classes"]),
            (": " + ", ".join(o["apple_classes"][:12]) + (" ..." if len(o["apple_classes"]) > 12 else ""))
            if o["apple_classes"] else ""))
        for c in rep["cautions"]:
            print("  caution      %s" % c)
        for n in rep["notes"]:
            print("  note         %s" % n)
        print("  dependencies of the game code (%s):" % ", ".join(p.rsplit("/", 1)[-1] for p in rep["kept"]))
        for key, d in sorted(rep["deps"].items(), key=lambda kv: (kv[1]["kind"], kv[0])):
            used = len(d["symbols"])
            print("    %-28s %-12s %-28s %4d sym%s" % (
                key[:28], d["kind"], (d["value"] or "-")[:28], used, "  " + d["note"] if d["note"] else ""))
            if verbose and d["symbols"] and (d["kind"] in ("framework", "metal", "gl") or verbose > 1):
                for s in sorted(d["symbols"])[:40]:
                    print("        %s" % s)
                if len(d["symbols"]) > 40:
                    print("        ... %d more" % (len(d["symbols"]) - 40))
        others = sorted(p for p in rep["bins"] if p not in rep["kept"])
        if others:
            print("  other Mach-O files (replaced or unused): %d" % len(others))
            if verbose:
                for p in others:
                    b = rep["bins"][p]
                    print("    %s [%s]" % (p, ", ".join(b.archs)))


def to_json(inv, reps):
    def clean(rep):
        r = {k: v for k, v in rep.items() if k not in ("main", "bins", "deps", "evidence")}
        r["evidence"] = {k: sorted(v) for k, v in rep["evidence"].items()}
        if "deps" in rep:
            r["deps"] = {k: {**{kk: vv for kk, vv in d.items() if kk != "symbols"},
                             "symbols": sorted(d["symbols"])} for k, d in rep["deps"].items()}
        return r
    return {"source": inv.source, "files": len(inv.files), "machos": len(inv.machos),
            "notes": inv.notes, "apps": [clean(r) for r in reps]}


def survey(path):
    inv = inventory_from(path)
    apps = find_apps(inv)
    covered = set()
    reps = []
    survey_root = ""
    if os.path.isdir(path):
        survey_root = os.path.basename(os.path.abspath(path).rstrip("\\/"))
    for root, info in apps.items():
        rep, _ = analyse_app(inv, root, info, survey_root)
        reps.append(rep)
        covered.update(p for p in inv.machos if p.startswith(root + "/"))
    loose = [p for p in inv.machos if p not in covered]
    if loose and not apps:
        rep, _ = analyse_app(inv, "", {}, survey_root)
        reps.append(rep)
    elif loose:
        inv.notes.append("%d Mach-O file(s) outside any .app: %s" % (
            len(loose), ", ".join(sorted(loose)[:5])))
    if not reps:
        inv.notes.append("no .app bundle and no Mach-O files found")
    return inv, reps


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("paths", nargs="+", help=".app, folder, Mach-O, .pkg, .zip or .tar(.gz)")
    ap.add_argument("--json", action="store_true", help="print JSON instead of the report")
    ap.add_argument("--conf", metavar="DIR", help="write draft dylib_map.conf + machismo.conf per app")
    ap.add_argument("-v", "--verbose", action="count", default=0,
                    help="list framework symbols (-vv: every dependency's symbols)")
    args = ap.parse_args(argv)
    results = []
    for p in args.paths:
        inv, reps = survey(p)
        results.append(to_json(inv, reps))
        if not args.json:
            print_report(inv, reps, args.verbose)
        if args.conf:
            for rep in reps:
                if "deps" not in rep:
                    continue
                slug = slugify(rep["app"] if rep["app"] != "(loose files)" else p)
                d = os.path.join(args.conf, slug)
                os.makedirs(d, exist_ok=True)
                with open(os.path.join(d, "dylib_map.conf"), "w", newline="\n") as f:
                    f.write(draft_dylib_map(rep))
                with open(os.path.join(d, "machismo.conf"), "w", newline="\n") as f:
                    f.write(draft_machismo_conf(rep, slug))
                if not args.json:
                    print("  draft config  %s" % d)
    if args.json:
        json.dump(results if len(results) > 1 else results[0], sys.stdout, indent=2, default=str)
        print()


if __name__ == "__main__":
    main()
