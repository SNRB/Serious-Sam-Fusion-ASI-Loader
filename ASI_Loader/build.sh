#!/bin/sh
# Builds the ASI loader as BOTH msimg32.dll and winmm.dll. Install ONE of them next to the exe
# (msimg32.dll covers the editor and the game; winmm.dll is an editor-only alternative).
# Needs an LLVM toolchain targeting Windows: clang, lld-link, llvm-dlltool.
set -e
cd "$(dirname "$0")"
B=out; mkdir -p "$B"
CF="--target=x86_64-pc-windows-msvc -O2 -fno-builtin -ffreestanding -fno-stack-protector -mno-stack-arg-probe -fasynchronous-unwind-tables -Wall -Wextra -Wno-unused-function -Werror"
LF="/nodefaultlib /machine:x64 /subsystem:windows /entry:DllMain /dll /opt:ref /opt:icf /guard:no /Brepro /dynamicbase /nxcompat /highentropyva"

llvm-dlltool -m i386:x86-64 -d src/kernel32.def -l "$B/kernel32.lib"
clang $CF -c src/loader.c -o "$B/loader.obj"
lld-link $LF /def:src/msimg32.def /out:"$B/msimg32.dll" "$B/loader.obj" "$B/kernel32.lib"
clang $CF -c src/winmm_exports.c -o "$B/winmm_exports.obj"
lld-link $LF /def:src/winmm.def  /out:"$B/winmm.dll"  "$B/loader.obj" "$B/winmm_exports.obj" "$B/kernel32.lib"
rm -f "$B"/*.lib "$B"/*.exp "$B"/*.obj
ls -la "$B"/*.dll
