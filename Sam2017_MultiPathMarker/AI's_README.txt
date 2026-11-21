# 2 — `Sam2017_MultiPathMarker.asi`  (editor + game)

> **Buildable again.** The parts that were lost from the conversation (`mpm.c` and the package-build helpers) have
> been recovered from the shipped `.asi`. This folder rebuilds that `.asi` **byte for byte** (SHA-256 below).

## What it does
Adds a **MultiPath marker** entity to the editor and the game — the branching-path/route marker, rebuilt for Fusion so
Serious Sam 2 worlds that use it load and behave correctly. It is a new engine class derived from the existing
path-marker class, registered into the engine's metadata at startup (a "pre-CRT" feature: it must be applied in the
loader's start-up window, before the engine registers its classes).

## To build
You need clang, lld-link and llvm-dlltool from LLVM 18, objdump (binutils) and Python 3. You also need the two
**original** executables (7 June 2019). Put them in `exe/` here, or point the environment variables at them:
```
export SAM2017_EDITOR_EXE=/path/to/Sam2017_SeriousEditor.exe
export SAM2017_GAME_EXE=/path/to/Sam2017_Unrestricted.exe
./build.sh                 # -> out/Sam2017_MultiPathMarker.asi
```
`build.sh` runs `build_pkg.py out MultiPathMarker`. That reads both executables, compiles `src/mpm.c` and writes one
package per executable plus `out/pkg_MultiPathMarker.h`. It then compiles `src/asi_plugin.c` with that header.

Expected result, identical to the shipped file:
`9b9c404a9b1be29bd71c830610520047e613074b480750b2d660714318c912d8  Sam2017_MultiPathMarker.asi`
Byte-identical output needs the same compiler generation (LLVM 18; checked with clang 18.1.3).

## Files here
- `src/mpm.c` — **recovered** feature source: the two classes, their vtables, metadata stream and registration glue
- `src/asi_plugin.c` — the shared plugin runtime (identical across all plugins); see the note below
- `src/common.h`, `src/win_min.h`, `src/kernel32.def` — shared support (unchanged)
- `build_pkg.py` — the package builder (as recovered; only its paths were made relative, see below)
- `tools/coff_link.py`, `tools/patch_exe3.py`, `tools/patch_exe5.py` — **rewritten** package-build helpers
  (COFF reader/linker, PE reader, function-clone support)
- `cfg/editor5.json`, `cfg/unrestricted5.json` — **rebuilt** addresses of the engine symbols, patch sites and expected
  values in each executable

## How faithful the recovery is
- `mpm.c` compiles to exactly the code and data of the original package. Strings and metadata are original. The C
  code is proven equivalent by the byte-identical output. Identifiers, comments and layout are reconstructed, so
  they may read differently from the lost text.
- The three helpers and the two configs are working rewrites, not the lost files' text. They produce identical
  packages. The configs only hold the entries of MultiPathMarker and CellShaded. `build_pkg.py` still lists two other
  features (EditorScriptFunctions `sra.c`, MultiSpawnerOrder `ms.c`); they are not part of these two plugins and are
  not built here.
- `build_pkg.py` changes: the paths are now relative to its folder (`src/`, `tools/`, `cfg/`, `exe/`). The exe paths
  can be overridden by environment variables. The generated shader include goes into `out/obj/` (CellShaded only).
- `asi_plugin.c` has one fix, in `fail()`: `Text box;` is now `static Text box;`. That is what the shipped `.asi`
  files were built from. `Text` is about 4 KB, and the runtime is built with `-mno-stack-arg-probe`, so the stack
  version risked a crash whenever the error box was shown.
