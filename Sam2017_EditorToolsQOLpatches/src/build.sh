#!/bin/sh
# Builds Sam2017_EditorToolsQOLpatches.asi with clang + lld (no Windows SDK or C runtime needed).
# exe_expect.h is generated from the original Sam2017_SeriousEditor.exe of both builds (7 June 2019 and
# 19 November 2026) with gen_expect.py.
set -e
cd "$(dirname "$0")"
mkdir -p out
llvm-dlltool -m i386:x86-64 -d kernel32.def -l out/kernel32.lib
clang --target=x86_64-pc-windows-msvc -std=c11 -O2 -ffreestanding -fno-stack-protector -fno-math-errno \
      -mno-stack-arg-probe -fno-asynchronous-unwind-tables -funwind-tables \
      -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -c Sam2017_EditorToolsQOLpatches.c -o out/plugin.obj
lld-link /nologo /dll /entry:DllMain /nodefaultlib /subsystem:windows /machine:x64 \
         /opt:ref /opt:icf /brepro /out:out/Sam2017_EditorToolsQOLpatches.asi \
         out/plugin.obj out/kernel32.lib
ls -l out/Sam2017_EditorToolsQOLpatches.asi
