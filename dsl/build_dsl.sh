#!/usr/bin/env bash
# build_dsl.sh — build Furever Home's DSL pipeline against the niobium-client
# SDK, for the CPU + Fog/FPGA path. (The browser/WASM demo needs none of this.)
#
# The DSL's @hardware codegen emits the deserialize-hook "auto-tagging" recording
# mode, which is fine for stages that RETURN wire-results and do only
# cipher×cipher math — but it (a) only probes a returned value, not save()'d
# outputs, and (b) suppresses host-created plaintexts. furever's score stage does
# BOTH (it save()s categories/scores and multiplies by a host-built rubric
# plaintext), so under auto-tagging its FPGA replay comes back empty/zero.
#
# This script re-generates score_pets.cpp into the MANUAL instrumentation pattern
# documented in niobium-client's README (Entry Point 2 — OpenFHE for application
# developers):
#   capture_crypto_context(cc) -> tag_input -> tag_keys   (tag order: ctx, input, keys)
#   record on cache-miss: start() -> kernel -> probe() each saved output -> stop()
#   run on device (NBCC_FHETCH_SERVER set by `fog submit`): replay() -> result()
#     to rehydrate each probe into a proper Ciphertext (correct scale/level; raw
#     probe .ct files do NOT decode)
# It also adds furever's hand-rolled keygen target (the .niob omits the keygen stage).
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

# 2. rewrite the generated score_pets.cpp into the manual @hardware pattern
#    (idempotent). Durable fix would be to teach nbc's codegen to emit this for
#    @hardware stages that save() outputs / use host plaintexts.
"$PY" - "$OUT/score_pets.cpp" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
if "capture_crypto_context" in s:
    print("[build_dsl] score_pets.cpp already patched — skipping"); raise SystemExit

# a) std::getenv needs <cstdlib>
s = s.replace('#include "niobium/compiler.h"\n',
              '#include "niobium/compiler.h"\n#include <cstdlib>   // std::getenv\n', 1)

# b) drop the auto-tagging (deserialize-hook-only) mode — we tag explicitly
s = s.replace('  niobium::compiler().enable_auto_tagging();\n', '')

# c) rewrite the record block + kernel into: manual tags -> record-on-miss (with
#    probes) -> device-run (with result() rehydration). The save() lambdas that
#    follow this block are reused for both the CPU-record and device-run outputs.
old = (
'  if (!niobium::compiler().is_cache_valid()) {\n'
'    std::cout << "[nb] Recording OpenFHE operations" << std::endl;\n'
'    niobium::compiler().enable_hollow_mode(_nb_hollow_record);\n'
'    niobium::compiler().start();\n'
'  }\n'
'\n'
'  auto weighted = apply_weights(cc, answers);\n'
'  auto cats = weighted;\n'
'  for (auto i = 0; i < 4; i++) {\n'
'    cats = NullSafeEvalAdd(cc, cats, cc->EvalRotate(cats, (1 << i)));\n'
'  }\n'
'  auto scores = cats;\n'
'  for (auto i = 4; i < 6; i++) {\n'
'    scores = NullSafeEvalAdd(cc, scores, cc->EvalRotate(scores, (1 << i)));\n'
'  }\n')
new = (
'  // Manual @hardware instrumentation (niobium-client README, Entry Point 2).\n'
'  // Tag order: context, input, keys.\n'
'  niobium::compiler().capture_crypto_context(cc);\n'
'  niobium::compiler().tag_input("answers", answers);\n'
'  niobium::compiler().tag_keys(cc);\n'
'\n'
'  Ciphertext<DCRTPoly> cats, scores;\n'
'  const bool _nb_have_device = std::getenv("NBCC_FHETCH_SERVER") != nullptr;\n'
'\n'
'  if (!niobium::compiler().is_cache_valid()) {\n'
'    std::cout << "[nb] Recording OpenFHE operations" << std::endl;\n'
'    niobium::compiler().enable_hollow_mode(_nb_hollow_record);\n'
'    niobium::compiler().start();\n'
'    auto weighted = apply_weights(cc, answers);\n'
'    cats = weighted;\n'
'    for (auto i = 0; i < 4; i++) {\n'
'      cats = NullSafeEvalAdd(cc, cats, cc->EvalRotate(cats, (1 << i)));\n'
'    }\n'
'    scores = cats;\n'
'    for (auto i = 4; i < 6; i++) {\n'
'      scores = NullSafeEvalAdd(cc, scores, cc->EvalRotate(scores, (1 << i)));\n'
'    }\n'
'    niobium::compiler().probe("categories", cats);\n'
'    niobium::compiler().probe("scores", scores);\n'
'    niobium::compiler().stop();\n'
'    niobium::compiler().enable_hollow_mode(false);\n'
'  }\n'
'\n'
'  if (_nb_have_device) {\n'
'    std::cout << "[nb] Running on device (FOG)" << std::endl;\n'
'    if (!niobium::compiler().replay()) { std::cerr << "[ERROR] FHETCH replay failed!" << std::endl; }\n'
'    niobium::compiler().result(cc, "categories", cats);\n'
'    niobium::compiler().result(cc, "scores", scores);\n'
'  }\n')
assert old in s, "score_pets record/kernel anchor not found (did codegen change?)"
s = s.replace(old, new, 1)

# d) main(): score_pets() now owns record + device-run — collapse main's branch
old_main = (
'  const bool _nb_replaying = niobium::compiler().is_cache_valid();\n'
'  if (!_nb_replaying) {\n'
'    score_pets(cc, inst);\n'
'    niobium::compiler().stop();\n'
'    niobium::compiler().enable_hollow_mode(false);\n'
'  } else {\n'
'    std::cout << "[nb] Cached trace found — replaying (no FHE ops)" << std::endl;\n'
'    if (!niobium::compiler().replay()) {\n'
'      std::cerr << "[ERROR] FHETCH replay failed!" << std::endl;\n'
'      return 1;\n'
'    }\n'
'  }')
assert old_main in s, "score_pets main-branch anchor not found"
s = s.replace(old_main, '  score_pets(cc, inst);   // owns record + device-run (manual @hardware pattern)', 1)

open(p, 'w').write(s)
print("[build_dsl] rewrote score_pets.cpp to the manual @hardware pattern "
      "(capture_crypto_context + tag_input + tag_keys; probe on record; result() on device)")
PYEOF

# 3. the generated CMake omits the hand-rolled keygen — append it (idempotent)
if ! grep -q 'add_executable(key_generation' "$OUT/CMakeLists.txt"; then
  cat >> "$OUT/CMakeLists.txt" <<'EOF'

# furever hand-rolled keygen (the .niob omits the keygen stage — DSL issue #2)
if(DEFINED LOCAL_SRC_DIR AND EXISTS "${LOCAL_SRC_DIR}/keygen.cpp")
  add_executable(key_generation ${LOCAL_SRC_DIR}/keygen.cpp ${SHARED_SRC})
endif()
EOF
fi

# 4. configure + build (apply_weights bridge auto-discovered from LOCAL_SRC_DIR)
"$CMAKE" -S "$OUT" -B "$OUT/build" \
  -DNIOBIUM_CLIENT_ROOT="$NIOBIUM_CLIENT_ROOT" -DLOCAL_SRC_DIR="$HERE" -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build "$OUT/build" -j4

echo
echo "Built -> $OUT/build/{key_generation,encrypt_answers,score_pets,decrypt_result}"
echo "         (these run on CPU as-is, or on the Niobium FPGA via: fog submit ./score_pets 1 --target=FOG)"
