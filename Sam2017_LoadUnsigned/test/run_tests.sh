#!/bin/sh
# Verifies the generated patch table against the executables and that the
# patched sites decode to the intended jumps. Needs python3 + objdump (binutils)
# and SAM2017_BIN set to the folder with the two .exe files.
set -e
cd "$(dirname "$0")"
if [ -z "$SAM2017_BIN" ]; then echo "set SAM2017_BIN to the folder with the two exes"; exit 1; fi
python3 ../tools/sig_sites.py
python3 ../tools/verify_patch.py && echo "ALL OK"
