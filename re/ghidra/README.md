# Ghidra headless scripts (Jython)

From Stunt Playground, where the Newton SDK vehicle code was decompiled to rebuild it on Bullet.

```bash
analyzeHeadless /tmp/ghproj proj -import game.dll -postScript ExportDecomp.py /tmp/out -scriptPath <this folder>
analyzeHeadless /tmp/ghproj proj -process game.dll -noanalysis -postScript DumpFloats.py 0x1004a2c0 0x1004a2c4 -scriptPath <this folder>
```

- `ExportDecomp.py <outdir>`: writes every function of the program as decompiled C into `<outdir>/<program>.c`.
- `DumpFloats.py <addr>...`: prints the float and uint32 at each address (constants tables, tuning values).
