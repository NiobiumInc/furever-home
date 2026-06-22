#!/usr/bin/env bash
# Build the Furever Home FHE engine (fhe.cpp) to a WASM module (fhe.js + fhe.wasm)
# with an embind API: keygen / encrypt / score / decrypt. Run build_openfhe first.
set -euo pipefail
cd "$(dirname "$0")"
PREFIX="$PWD/openfhe-install"; INC="$PREFIX/include/openfhe"
[ -d "$INC" ] || { echo "missing $INC — build OpenFHE for WASM first"; exit 1; }
PKE=$(ls "$PREFIX"/lib/*pke*.a); BIN=$(ls "$PREFIX"/lib/*binfhe*.a); CORE=$(ls "$PREFIX"/lib/*core*.a)
# normalize absolute build paths out of the compiled output
MAP="-ffile-prefix-map=$HOME=/home -ffile-prefix-map=$(cd "$PREFIX" && pwd)=/openfhe -ffile-prefix-map=$PWD=app"
em++ -std=c++17 -O3 -fexceptions -lembind fhe.cpp $MAP \
  -I"$INC" -I"$INC/core" -I"$INC/pke" -I"$INC/binfhe" \
  "$PKE" "$BIN" "$CORE" \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=536870912 -sMAXIMUM_MEMORY=2147483648 \
  -sMODULARIZE=1 -sEXPORT_NAME=createFhe \
  -o fhe.js
echo "built:"; ls -lh fhe.js fhe.wasm | awk '{print $5, $NF}'
