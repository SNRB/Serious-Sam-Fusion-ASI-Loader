#!/bin/sh
# Builds Sam2017_AnimButtonTweak.asi  (editor only).
# Usage:  ./build.sh /path/to/Sam2017_SeriousEditor.exe-of-7-June-2019 /path/to/Sam2017_SeriousEditor.exe-of-19-November-2026
# The original editor exes are read only to stamp the packages with their identity (TimeDateStamp / SizeOfImage /
# CheckSum / image base) and to confirm the target byte is as expected; they are not modified.
set -e
EXE2019="$(realpath "${1:?usage: ./build.sh <Sam2017_SeriousEditor.exe of 7 June 2019> <Sam2017_SeriousEditor.exe of 19 November 2026>}")"
EXE2026="$(realpath "${2:?usage: ./build.sh <Sam2017_SeriousEditor.exe of 7 June 2019> <Sam2017_SeriousEditor.exe of 19 November 2026>}")"
cd "$(dirname "$0")"
B=out; mkdir -p "$B"

# 1) build this plugin's byte-patch packages, one per editor build (the same instruction, lea r9d,[rdi+0x66]):
#      7 June 2019:      VA 0x14186C38B  0x66 -> 0x62
#      19 November 2026: VA 0x1418AC2EB  0x66 -> 0x62
python3 make_pkg.py --feature AnimButtonTweak --name Sam2017_SeriousEditor.exe --out "$B/pkg_AnimButtonTweak.h" \
  --exe "$EXE2019" --patch 0x14186c38b 66 62 \
  --exe "$EXE2026" --patch 0x1418ac2eb 66 62

# 2) compile the shared runtime with that package embedded
CF="--target=x86_64-pc-windows-msvc -O2 -fno-builtin -ffreestanding -fno-stack-protector -mno-stack-arg-probe -fasynchronous-unwind-tables -Wall -Wextra -Wno-unused-function -Werror"
LF="/nodefaultlib /machine:x64 /subsystem:windows /entry:DllMain /dll /opt:ref /opt:icf /guard:no /Brepro /dynamicbase /nxcompat /highentropyva"
llvm-dlltool -m i386:x86-64 -d src/kernel32.def -l "$B/kernel32.lib"
clang $CF -I"$B" -DPKG_HEADER='"pkg_AnimButtonTweak.h"' -c src/asi_plugin.c -o "$B/plugin.obj"
lld-link $LF /out:"$B/Sam2017_AnimButtonTweak.asi" "$B/plugin.obj" "$B/kernel32.lib"
rm -f "$B"/*.lib "$B"/*.exp "$B"/*.obj
ls -la "$B/Sam2017_AnimButtonTweak.asi"
