# Packaging

Work on the release folder layout every port here uses:

```
release/
  Port Name.sh
  port.json  README.md  gameinfo.xml  screenshot.png  [cover.png]  [testing_thread.txt]
  portname/            (binary, libs.aarch64/, licenses/, the gptokeyb2 .ini, ...)
```

| Tool | What |
|--|--|
| `pm_zip.py <release> [out.zip]` | Builds the installable zip: the launcher at the top, everything else (metadata included, `testing_thread.txt` left out) under `portname/`. Mode 0755 for ELF files, `#!` scripts and `.so` files, CRLF to LF for text. `--keep-crlf GLOB` for data that must stay byte-exact, `--gamedata DIR` for personal builds. Replaces the per-port `build_release_zip.py` copies (same entries, modes and contents, checked on 7 ports). |
| `pm_lint.py <release or zip>` | Checks the PortMaster rules from `C:\Claude\portmaster-ai-complete-reference.md`: port.json v4 (name, items, attr keys, porter, runtime keys without `.squashfs`, plain desc/inst), the launcher's unchanged boilerplate header and banned patterns (`SDL_VIDEODRIVER=`, `SDL_AUDIODRIVER=`, `export LD_PRELOAD`, quoted `$GPTOKEYB2`), lowercase port folder, licenses/, README shape, gameinfo.xml paths, screenshot 4:3 and at least 640x480, LF endings, em dashes. Exit 1 on errors. |
| `repack_launcher.py <zip> <file> [name]` | Swaps one file (the launcher) inside an existing zip, keeping all other entries. |

Typical release step:

```bash
python pm_lint.py "C:/Claude/<Game>/release" && python pm_zip.py "C:/Claude/<Game>/release"
```

Then run the zip through `../devtools/native-testing/pm_sim_test.sh` (native ports) or
`../devtools/pc-testing/fake-patcher` (ports with a first-run setup).
