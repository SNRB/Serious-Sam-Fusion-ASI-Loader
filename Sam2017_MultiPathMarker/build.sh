#!/bin/sh
# Builds Sam2017_MultiPathMarker.asi.
#
# Needs: clang + lld-link + llvm-dlltool (LLVM 18), objdump (binutils), Python 3, and the ORIGINAL executables of both
# supported builds: the two of 7 June 2019 in exe/ next to this script (or SAM2017_EDITOR_EXE / SAM2017_GAME_EXE set to
# their paths) and the two of 19 November 2026 in exe/2026/ (or SAM2017_EDITOR_EXE_2026 / SAM2017_GAME_EXE_2026).
# The feature is src/mpm.c; tools/ holds the package-build helpers, cfg/ the addresses in the executables.
set -e
cd "$(dirname "$0")"
B=out; mkdir -p "$B"

# 1) build this feature's package  ->  out/pkg_MultiPathMarker.h
python3 build_pkg.py "$B" MultiPathMarker

# 2) compile the shared runtime with that package embedded
CF="--target=x86_64-pc-windows-msvc -O2 -fno-builtin -ffreestanding -fno-stack-protector -mno-stack-arg-probe -fasynchronous-unwind-tables -Wall -Wextra -Wno-unused-function -Werror"
LF="/nodefaultlib /machine:x64 /subsystem:windows /entry:DllMain /dll /opt:ref /opt:icf /guard:no /Brepro /dynamicbase /nxcompat /highentropyva"
llvm-dlltool -m i386:x86-64 -d src/kernel32.def -l "$B/kernel32.lib"
clang $CF -I"$B" -DPKG_HEADER='"pkg_MultiPathMarker.h"' -c src/asi_plugin.c -o "$B/plugin.obj"
lld-link $LF /out:"$B/Sam2017_MultiPathMarker.asi" "$B/plugin.obj" "$B/kernel32.lib"
rm -f "$B"/*.lib "$B"/*.exp "$B"/*.obj
ls -la "$B/Sam2017_MultiPathMarker.asi"
