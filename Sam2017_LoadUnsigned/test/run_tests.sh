#!/bin/sh
# Verifies the generated patch table against the executables and that the
# patched sites decode to the intended jumps. Needs python3 + objdump (binutils),
# SAM2017_BIN set to the folder with the two .exe files of 7 June 2019 and
# SAM2017_BIN_2026 to the folder with the two .exe files of 19 November 2026.
set -e
cd "$(dirname "$0")"
if [ -z "$SAM2017_BIN" ] || [ -z "$SAM2017_BIN_2026" ]; then echo "set SAM2017_BIN and SAM2017_BIN_2026 to the folders with the two exes of each build"; exit 1; fi
python3 ../tools/sig_sites.py
python3 ../tools/verify_patch.py && echo "ALL OK"
