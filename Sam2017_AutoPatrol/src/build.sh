#!/bin/sh
# Builds Sam2017_AutoPatrol.asi with clang + lld-link (no Windows SDK / CRT needed).
set -e
cd "$(dirname "$0")"
mkdir -p ../build
llvm-dlltool -m i386:x86-64 -d kernel32.def -l ../build/kernel32.lib
clang -target x86_64-pc-windows-msvc -O2 -Wall -Wextra -ffreestanding -fno-builtin \
      -fno-stack-protector -mno-stack-arg-probe -fasynchronous-unwind-tables -funwind-tables \
      -c Sam2017_AutoPatrol.c -o ../build/ap.obj
lld-link /dll /nologo /entry:DllMain /nodefaultlib /subsystem:windows /machine:x64 \
      /opt:ref /opt:icf /dynamicbase /highentropyva /nxcompat /Brepro /noimplib \
      /out:../build/Sam2017_AutoPatrol.asi ../build/ap.obj ../build/kernel32.lib
