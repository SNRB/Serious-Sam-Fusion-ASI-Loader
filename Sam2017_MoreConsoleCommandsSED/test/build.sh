#!/bin/sh
# Builds harness (for Sam2017_SeriousEditor.exe of 7 June 2019) and harness26 (for the one of 19 November 2026:
# harness.c with the editor addresses of addr_2026.txt, see apply_map.py).
cd "$(dirname "$0")"
python3 apply_map.py addr_2026.txt harness.c harness26.c
gcc -O1 -o harness harness.c -lm
gcc -O1 -o harness26 harness26.c -lm
