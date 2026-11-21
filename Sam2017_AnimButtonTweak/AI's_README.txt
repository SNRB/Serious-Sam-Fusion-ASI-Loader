# 5 — `Sam2017_AnimButtonTweak.asi`  (editor only)

Builds **standalone** (self-contained — no external files beyond your original editor exe).

## What it does
Reproduces your one-byte editor edit (the animation-playback button) at run time:

    virtual address 0x14186C38B   (file offset 0x186B78B)   0x66 -> 0x62

so you can run the stock editor and this plugin makes that change in memory.
## Build
```
./build.sh /path/to/original/Sam2017_SeriousEditor.exe
```
Produces `out/Sam2017_AnimButtonTweak.asi`. The exe is only read (to stamp the package with the exe's identity and to
check the target byte is `0x66`); it is not modified. If the byte isn't `0x66` (e.g. you point it at an
already-edited exe) the build stops with an error.

## How it builds without the rest of the project
A byte-patch plugin is just the shared runtime (`src/asi_plugin.c`) plus a tiny "package" describing the patch.
`make_pkg.py` here is a **self-contained** package builder (its own minimal PE reader; no other dependencies), so this
folder needs nothing from the other plugins. To change the patch, edit the `--patch VA OLD NEW` argument in
`build.sh` (`make_pkg.py` accepts several `--patch` groups, up to 8 bytes each).

## Files
- `src/asi_plugin.c` — the shared plugin runtime (identical across all plugins)
- `src/common.h`, `src/win_min.h`, `src/kernel32.def` — shared support
- `make_pkg.py` — self-contained byte-patch package builder

## Requires
`clang`, `lld-link`, `llvm-dlltool` (targeting `x86_64-pc-windows-msvc`), `python3`.
