# 5 — `Sam2017_AnimButtonTweak.asi`  (editor only)

Builds **standalone** (self-contained — no external files beyond your original editor exes).

Works with both editor builds: 7 June 2019 and 19 November 2026.

## Changes
- **19 November 2026 editor**: the plugin now carries one package per editor build and applies the one whose identity
  matches the running exe. The edited instruction (`lea r9d, [rdi+0x66]`) is the same in the new build, only at
  another address (see below). On the 2019 editor it makes exactly the same change as before.
- `src/asi_plugin.c` is now the same copy of the shared runtime as in the other package plugins. The copy that was in
  this folder was one line older than the runtime the shipped `.asi` was built with (`Text box;` instead of
  `static Text box;` in the message-box builder); the shipped 2019 `.asi` is reproduced byte for byte only with the
  newer runtime, so that is the one used now.

## What it does
Reproduces your one-byte editor edit (the animation-playback button) at run time:

    7 June 2019 editor:       virtual address 0x14186C38B   (file offset 0x186B78B)   0x66 -> 0x62
    19 November 2026 editor:  virtual address 0x1418AC2EB                             0x66 -> 0x62

so you can run the stock editor and this plugin makes that change in memory.
## Build
```
./build.sh /path/to/Sam2017_SeriousEditor.exe-of-7-June-2019 /path/to/Sam2017_SeriousEditor.exe-of-19-November-2026
```
Produces `out/Sam2017_AnimButtonTweak.asi`. The exes are only read (to stamp each package with its exe's identity and
to check the target byte is `0x66`); they are not modified. If the byte isn't `0x66` (e.g. you point it at an
already-edited exe) the build stops with an error.

## How it builds without the rest of the project
A byte-patch plugin is just the shared runtime (`src/asi_plugin.c`) plus a tiny "package" describing the patch.
`make_pkg.py` here is a **self-contained** package builder (its own minimal PE reader; no other dependencies), so this
folder needs nothing from the other plugins. To change the patch, edit the `--patch VA OLD NEW` arguments in
`build.sh`: every `--exe PATH` starts the package for that exe, and the `--patch` groups after it (up to 8 bytes each)
belong to it.

## Files
- `src/asi_plugin.c` — the shared plugin runtime (identical across all plugins)
- `src/common.h`, `src/win_min.h`, `src/kernel32.def` — shared support
- `make_pkg.py` — self-contained byte-patch package builder

## Requires
`clang`, `lld-link`, `llvm-dlltool` (targeting `x86_64-pc-windows-msvc`), `python3`.
