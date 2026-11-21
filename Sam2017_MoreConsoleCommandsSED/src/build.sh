#!/bin/sh
# Builds Sam2017_MoreConsoleCommandsSED.asi with clang + lld (no Windows SDK or C runtime needed).
# exe_expect.h is generated from the original Sam2017_SeriousEditor.exe with gen_expect.py.
set -e
cd "$(dirname "$0")"
mkdir -p out
llvm-dlltool -m i386:x86-64 -d kernel32.def -l out/kernel32.lib
clang --target=x86_64-pc-windows-msvc -std=c11 -O2 -ffreestanding -fno-stack-protector \
      -mno-stack-arg-probe -fno-asynchronous-unwind-tables -funwind-tables \
      -Wall -Wextra -Wno-unused-parameter -c Sam2017_MoreConsoleCommandsSED.c -o out/plugin.obj
lld-link /nologo /dll /entry:DllMain /nodefaultlib /subsystem:windows /machine:x64 \
         /opt:ref /opt:icf /brepro /out:out/Sam2017_MoreConsoleCommandsSED.asi \
         out/plugin.obj out/kernel32.lib
ls -l out/Sam2017_MoreConsoleCommandsSED.asi
