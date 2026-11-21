#!/bin/sh
cd "$(dirname "$0")"
python3 - <<'PY'
b=open('base_part.c').read().replace('#define _GNU_SOURCE\n','',1)
open('harness.c','w').write(open('head_part.c').read()+b+open('qol_part.c').read()+open('main_part.c').read())
PY
gcc -O1 -g -Wall -Wno-unused-function -Wno-unused-variable -o harness harness.c -lm
