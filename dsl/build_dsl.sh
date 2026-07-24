#!/usr/bin/env bash
# build_dsl.sh — build Furever Home's DSL pipeline against the niobium-client
# SDK, for the CPU + Fog/FPGA path. (The browser/WASM demo needs none of this.)
#
# The DSL's @hardware codegen now instruments save()d outputs natively: each
# save() in an @hardware stage is probe()d during the record pass (probe name =
# the .bin filename's stem) and reconstructed via result() on a cache-valid
# replay run. Earlier codegen only probed a RETURNED wire-result, so a stage
# like furever's — which save()s categories/scores — replayed empty/zero and
# this script had to rewrite score_pets.cpp into the manual instrumentation
# pattern (niobium-client README, Entry Point 2). That rewrite is gone: the
# generated code is correct as emitted.
# The script still adds furever's hand-rolled keygen target (the .niob omits
# the keygen stage).
#
# Usage — point NIOBIUM_CLIENT_ROOT at your niobium-client checkout:
#   NIOBIUM_CLIENT_ROOT=/path/to/niobium-client ./build_dsl.sh
# On macOS also pass Homebrew's cmake/python3 (the system ones may be x86_64):
#   NIOBIUM_CLIENT_ROOT=/path/to/niobium-client \
#     CMAKE=/opt/homebrew/bin/cmake PY=/opt/homebrew/bin/python3 ./build_dsl.sh
#
# Then, from the build dir (nb_out/build), with the rubric path exported:
#   export FUREVER_RUBRIC="$PWD/../../rubric.dat"
#   CPU:  ./key_generation 1 && ./encrypt_answers 1 <12 answers> && ./score_pets 1 && ./decrypt_result 1
#   Fog:  ./key_generation 1 && ./encrypt_answers 1 <12 answers> \
#           && fog submit ./score_pets 1 --target=FOG && ./decrypt_result 1
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"          # the dsl/ dir
NIOBIUM_CLIENT_ROOT="${NIOBIUM_CLIENT_ROOT:?set NIOBIUM_CLIENT_ROOT to your niobium-client checkout}"
PY="${PY:-python3}"
CMAKE="${CMAKE:-cmake}"
OUT="$HERE/nb_out"

[ -f "$NIOBIUM_CLIENT_ROOT/dsl_fhe/xcomp/nbc.py" ] || { echo "no nbc under $NIOBIUM_CLIENT_ROOT — is it the niobium-client repo root?"; exit 1; }

# 1. compile the .niob sources (nbc is invoked as a module)
PYTHONPATH="$NIOBIUM_CLIENT_ROOT/dsl_fhe" "$PY" -m xcomp.nbc compile \
  "$HERE/shared.niob" "$HERE/client.niob" "$HERE/server.niob" --outdir "$OUT"

# 2. the generated CMake omits the hand-rolled keygen — append it (idempotent)
if ! grep -q 'add_executable(key_generation' "$OUT/CMakeLists.txt"; then
  cat >> "$OUT/CMakeLists.txt" <<'EOF'

# furever hand-rolled keygen (the .niob omits the keygen stage — DSL issue #2)
if(DEFINED LOCAL_SRC_DIR AND EXISTS "${LOCAL_SRC_DIR}/keygen.cpp")
  add_executable(key_generation ${LOCAL_SRC_DIR}/keygen.cpp ${SHARED_SRC})
endif()
EOF
fi

# 3. configure + build (apply_weights bridge auto-discovered from LOCAL_SRC_DIR)
"$CMAKE" -S "$OUT" -B "$OUT/build" \
  -DNIOBIUM_CLIENT_ROOT="$NIOBIUM_CLIENT_ROOT" -DLOCAL_SRC_DIR="$HERE" -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build "$OUT/build" -j4

echo
echo "Built -> $OUT/build/{key_generation,encrypt_answers,score_pets,decrypt_result}"
echo "         (these run on CPU as-is, or on the Niobium FPGA via: fog submit ./score_pets 1 --target=FOG)"
