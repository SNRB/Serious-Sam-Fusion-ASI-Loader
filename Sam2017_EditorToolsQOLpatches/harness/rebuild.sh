#!/bin/sh
# Builds harness (for Sam2017_SeriousEditor.exe of 7 June 2019) and harness26 (for the one of 19 November 2026: the
# same source with the editor addresses of addr_2026.txt, see apply_map.py).
cd "$(dirname "$0")"
python3 - <<'PY'
b=open('base_part.c').read().replace('#define _GNU_SOURCE\n','',1)
open('harness.c','w').write(open('head_part.c').read()+b+open('qol_part.c').read()+open('main_part.c').read())
PY
python3 apply_map.py addr_2026.txt harness.c harness26.c
gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -o harness harness.c -lm
gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -o harness26 harness26.c -lm
