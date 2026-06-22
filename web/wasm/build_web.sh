#!/usr/bin/env bash
# Build the Stage-0 spike for the BROWSER (selftest_web.js + .wasm).
# Same as build_selftest.sh but targets the web (no NODERAWFS) so it loads
# over http and prints via console (mirrored onto the page by index.html).
# Run build_openfhe first so openfhe-install/ exists.
set -euo pipefail
cd "$(dirname "$0")"
PREFIX="$PWD/openfhe-install"; INC="$PREFIX/include/openfhe"
[ -d "$INC" ] || { echo "missing $INC — build OpenFHE for WASM first"; exit 1; }
PKE=$(ls "$PREFIX"/lib/*pke*.a); BIN=$(ls "$PREFIX"/lib/*binfhe*.a); CORE=$(ls "$PREFIX"/lib/*core*.a)
em++ -std=c++17 -O3 -fexceptions selftest.cpp \
  -I"$INC" -I"$INC/core" -I"$INC/pke" -I"$INC/binfhe" \
  "$PKE" "$BIN" "$CORE" \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=536870912 -sMAXIMUM_MEMORY=2147483648 \
  -sEXIT_RUNTIME=1 -sENVIRONMENT=web \
  -o selftest_web.js
echo "built:"; ls -lh selftest_web.js selftest_web.wasm | awk '{print $5, $NF}'
