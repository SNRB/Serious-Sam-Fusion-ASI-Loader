# 1 — ASI Loader (`msimg32.dll` / `winmm.dll`)

Builds **standalone** — no executable or package needed.

Unchanged for the editor and game of 19 November 2026: the loader does not depend on the exe build (it hooks whatever
entry point the exe has), and both new exes import `msimg32.dll` (`AlphaBlend`) and the same five `winmm` functions
(editor) as the exes of 7 June 2019, so `msimg32.dll` still works for both and `winmm.dll` still for the editor only.
The source here is the original one and builds the same `msimg32.dll` byte for byte.

## What it is
A proxy DLL the game/editor already imports at launch. It forwards the real exports to the system DLL and hooks the
exe's entry point so every `*.asi` next to the exe (and in `scripts\` / `plugins\`) is loaded before the C runtime
starts. Full description in the source header of `src/loader.c`.

## Build
```
./build.sh
```
Produces `out/msimg32.dll` and `out/winmm.dll`. **Install one**, next to the exe:
- `msimg32.dll` — works for the editor **and** the game (both import it). Recommended.
- `winmm.dll` — editor-only alternative (the game needs winmm functions this proxy doesn't forward).

## Files
- `src/loader.c` — the loader (entry-point hook + plugin loading)
- `src/winmm_exports.c` — the five winmm pass-through exports (only used by the winmm build)
- `src/common.h`, `src/win_min.h` — shared helpers and Win32 declarations
- `src/kernel32.def`, `src/msimg32.def`, `src/winmm.def` — import/export definitions

## Requires
An LLVM toolchain that can target `x86_64-pc-windows-msvc`: `clang`, `lld-link`, `llvm-dlltool`.
