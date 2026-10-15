# Replaces prefix files that are byte-identical to a Wine builtin with relative symlinks to
# <prefix>/.wine_runtime/lib/wine/<arch>-windows/<name>; wine.sh points .wine_runtime at the runtime.
# Usage: python3 link_prefix.py <prefix> <wine dir>
import filecmp, os, sys

prefix, wine = sys.argv[1], sys.argv[2]
builtins = {}
for arch in ("x86_64-windows", "i386-windows"):
    for name in os.listdir(os.path.join(wine, "lib/wine", arch)):
        builtins.setdefault(name.lower(), []).append(arch)

linked = saved = 0
for root, _, files in os.walk(os.path.join(prefix, "drive_c")):
    for name in files:
        path = os.path.join(root, name)
        if os.path.islink(path):
            continue
        for arch in builtins.get(name.lower(), []):
            src = os.path.join(wine, "lib/wine", arch, name)
            if os.path.exists(src) and filecmp.cmp(path, src, shallow=False):
                saved += os.path.getsize(path)
                up = os.path.relpath(prefix, root)
                os.remove(path)
                os.symlink(os.path.join(up, ".wine_runtime/lib/wine", arch, name), path)
                linked += 1
                break
print(f"linked {linked} builtin copies, {saved / 1e6:.0f} MB")
