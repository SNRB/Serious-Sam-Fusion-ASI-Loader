# 4 — `Sam2017_CellShaded.asi`  (editor + game)

> **Buildable again.** The parts that were lost from the conversation (`cs.c`, `CellShadedShader.psh` and the
> package-build helpers) have been recovered from the shipped `.asi`. This folder rebuilds that `.asi` **byte for
> byte** (SHA-256 below).

> **Updated for the executables of 19 November 2026.** The plugin now carries one package per executable of both builds
> (7 June 2019 and 19 November 2026, editor and game: four packages) and applies the one whose identity matches the
> running exe. The two 2019 packages inside are byte for byte the ones of the shipped `.asi`, so on the 2019
> executables nothing changes.

## Changes for the build of 19 November 2026
- `cfg/editor26.json`, `cfg/unrestricted26.json` — the addresses of every engine symbol, patch site, expected value
  and vtable entry the feature uses, in the new executables. They were found by matching the old and the new code
  (function by function, by their masked instruction bytes, and through the code and data that refer to them), and
  every entry was checked to be the same code or data as in 2019, only moved. The copied render function of the
  Standard shader is the same code in the new build (0x28C1 bytes in both executables of both builds), so the clone
  is made the same way.
- `build_pkg.py` builds the packages of the four executables (feature list: `editor`, `game`, `editor26`, `game26`).
- `src/asi_plugin.c` (shared runtime): the message for an unknown exe now names both supported builds.
- The feature source (`src/*.c`) is unchanged.

## What it does
Adds a new shader, **"CellShaded"**, next to "Standard" in the material Shader list — a copy of Standard with a
cartoon/cel look: flat lighting with a sharp terminator line, sharpened shadow edges, and a tintable/brightenable
ambient ("shaded") part, controlled by a new **"Cel shading"** material group (shade colour, shade intensity, shadow
sharpness, shadow threshold, ambient flatness). It draws with its own clone of Standard's render function and its own
pixel programs, leaving Standard untouched. Full description: `../README.txt`.

## To build
You need clang, lld-link and llvm-dlltool from LLVM 18, objdump (binutils) and Python 3. You also need the four
**original** executables: the two of 7 June 2019 in `exe/` here and the two of 19 November 2026 in `exe/2026/`, or
point the environment variables at them:
```
export SAM2017_EDITOR_EXE=/path/to/2019/Sam2017_SeriousEditor.exe
export SAM2017_GAME_EXE=/path/to/2019/Sam2017_Unrestricted.exe
export SAM2017_EDITOR_EXE_2026=/path/to/2026/Sam2017_SeriousEditor.exe
export SAM2017_GAME_EXE_2026=/path/to/2026/Sam2017_Unrestricted.exe
./build.sh                 # -> out/Sam2017_CellShaded.asi
```
`build.sh` runs `build_pkg.py out CellShaded`. That turns `src/CellShadedShader.psh` into a C string include
(`out/obj/CellShadedShader.psh.inc`), compiles `src/cs.c` and copies Standard's render function out of each
executable. It writes one package per executable plus `out/pkg_CellShaded.h`. It then compiles `src/asi_plugin.c`
with that header.

Expected result (both builds):
`0f69c1743a9cd694177afa0b261ef54913a6a23ceefc253ca046141d82f14d3f  Sam2017_CellShaded.asi`
The 2019 version of this folder, built with the two 2019 executables, gave the shipped file:
`2c8fbc505c0cb48f06a82f0621e520bd4dcd9ee4b657cdfc65208df54fccfdf8  Sam2017_CellShaded.asi`
Byte-identical output needs the same compiler generation (LLVM 18; checked with clang 18.1.3).

## Files here
- `src/cs.c` — **recovered** feature source: the shader and argument classes, metadata, constants, program
  compilation and the set-up of the render-function copy
- `src/CellShadedShader.psh` — **recovered** pixel-shader source. It is the exact text embedded in the `.asi`,
  comments included.
- `src/asi_plugin.c` — the shared plugin runtime (identical across all plugins); see the note below
- `src/common.h`, `src/win_min.h`, `src/kernel32.def` — shared support (unchanged)
- `build_pkg.py` — the package builder (as recovered; paths made relative and the shader embedding added, see below)
- `tools/coff_link.py`, `tools/patch_exe3.py`, `tools/patch_exe5.py` — **rewritten** package-build helpers.
  `patch_exe5.py` holds the function-clone logic that copies and relocates Standard's render function.
- `cfg/editor26.json`, `cfg/unrestricted26.json` — the same for the executables of 19 November 2026
- `cfg/editor5.json`, `cfg/unrestricted5.json` — **rebuilt** addresses of the engine symbols, patch sites, expected
  values and the clone description in each executable

## How faithful the recovery is
- `cs.c` compiles to exactly the code and data of the original package, for the editor and for the game. Strings,
  metadata and the shader text are original. The C code is proven equivalent by the byte-identical output.
  Identifiers, comments and layout are reconstructed, so they may read differently from the lost text.
- The three helpers and the two configs are working rewrites, not the lost files' text. They produce identical
  packages. The configs only hold the entries of MultiPathMarker and CellShaded. `build_pkg.py` still lists two other
  features (EditorScriptFunctions `sra.c`, MultiSpawnerOrder `ms.c`); they are not part of these two plugins and are
  not built here.
- `build_pkg.py` changes: the paths are now relative to its folder (`src/`, `tools/`, `cfg/`, `exe/`). The exe paths
  can be overridden by environment variables. A feature can list text files to embed; CellShaded lists its `.psh`.
  The builder writes `<file>.inc` (one quoted C string line per line) next to the object, because clang 18 has no
  `#embed`.
- `asi_plugin.c` has one fix, in `fail()`: `Text box;` is now `static Text box;`. That is what the shipped `.asi`
  files were built from. `Text` is about 4 KB, and the runtime is built with `-mno-stack-arg-probe`, so the stack
  version risked a crash whenever the error box was shown.
