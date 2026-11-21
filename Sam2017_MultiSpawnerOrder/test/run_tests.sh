#!/bin/sh
# Native test of the selection logic (mock engine) and of the meta data stream
# built by the plugin. Needs a C compiler and python3; for the stream check set
# SAM2017_BIN to the folder with Sam2017_SeriousEditor.exe / Sam2017_Unrestricted.exe.
set -e
cd "$(dirname "$0")"
cc -O2 -Wall -Wextra -Wno-unused-function -o test_mso test_mso.c
./test_mso | tail -1
# version 1.1 must pick exactly what version 1.0 picked for every table that
# 1.0 handled (up to 512 usable rows): v1/ holds the 1.0 selection code
cc -O2 -w -I../src -o compat compat.c
./compat > compat_now.txt
cmp compat_now.txt compat_v1.txt && echo "same picks as version 1.0 for tables of up to 512 usable rows"
if [ -n "$SAM2017_BIN" ]; then
  python3 - << 'PY'
import os, sys
sys.path.insert(0, os.path.join('..', 'tools'))
from pe import PE
BIN = os.environ['SAM2017_BIN']
for w, exe, va in (('ed', 'Sam2017_SeriousEditor.exe', 0x142147d20), ('gm', 'Sam2017_Unrestricted.exe', 0x141596bb0)):
    open('orig_stream_%s.bin' % w, 'wb').write(PE(os.path.join(BIN, exe)).read(va, 253 * 8))
PY
  for w in ed gm; do
    ./test_mso orig_stream_$w.bin new_stream_$w.bin > /dev/null
    python3 check_stream.py $w new_stream_$w.bin | tail -1
  done
fi
