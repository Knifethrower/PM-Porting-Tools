# MODE1/2352 .bin (one data track) -> .iso: keeps the 2048 user-data bytes of each raw sector
# (skips the 16-byte sync/header and the 288-byte EDC/ECC). Usage: bin2iso.py in.bin out.iso
import sys

src, dst = sys.argv[1], sys.argv[2]
n = 0
with open(src, "rb") as f, open(dst, "wb") as o:
    while True:
        sec = f.read(2352)
        if len(sec) < 2352:
            break
        if sec[15] != 1:
            sys.exit(f"sector {n}: mode {sec[15]}, expected MODE1")
        o.write(sec[16:16 + 2048])
        n += 1
print(f"{n} sectors -> {dst} ({n * 2048} bytes)")
