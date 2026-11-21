#!/bin/sh
# Native test of the plugin logic (mock engine) and of the meta data stream
# built by the plugin. Needs a C compiler and python3; for the stream check set
# SAM2017_BIN to the folder with Sam2017_SeriousEditor.exe / Sam2017_Unrestricted.exe.
set -e
cd "$(dirname "$0")"
cc -O2 -Wall -Wextra -Wno-unused-function -o test_ap test_ap.c -lm
./test_ap | tail -1
if [ -n "$SAM2017_BIN" ]; then
  python3 ../tools/extract_stream.py ed "$SAM2017_BIN/Sam2017_SeriousEditor.exe" orig_stream_ed.bin
  python3 ../tools/extract_stream.py gm "$SAM2017_BIN/Sam2017_Unrestricted.exe" orig_stream_gm.bin
  for w in ed gm; do
    ./test_ap orig_stream_$w.bin new_stream_$w.bin > /dev/null
    python3 check_stream.py $w new_stream_$w.bin | tail -1
  done
fi
