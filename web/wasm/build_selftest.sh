#!/usr/bin/env bash
# Compile the Stage-0 spike (selftest.cpp) against the WASM build of OpenFHE.
# Run build_openfhe first (emcmake configure + emmake install) so openfhe-install/ exists.
# Produces selftest.js + selftest.wasm, runnable with: node selftest.js
set -euo pipefail
cd "$(dirname "$0")"

PREFIX="$PWD/openfhe-install"
INC="$PREFIX/include/openfhe"
[ -d "$INC" ] || { echo "missing $INC — build OpenFHE for WASM first"; exit 1; }

# Link order matters for static archives: pke depends on core.
PKE=$(ls "$PREFIX"/lib/*pke*.a)
BIN=$(ls "$PREFIX"/lib/*binfhe*.a)
CORE=$(ls "$PREFIX"/lib/*core*.a)
echo "linking: $PKE $BIN $CORE"

em++ -std=c++17 -O3 -fexceptions selftest.cpp \
  -I"$INC" -I"$INC/core" -I"$INC/pke" -I"$INC/binfhe" -I"$INC/third-party/include" \
  "$PKE" "$BIN" "$CORE" \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=536870912 -sMAXIMUM_MEMORY=2147483648 \
  -sEXIT_RUNTIME=1 -sNODERAWFS=1 \
  -o selftest.js

echo "built:"; ls -lh selftest.js selftest.wasm
