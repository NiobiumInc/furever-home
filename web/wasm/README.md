# web/wasm — the WASM FHE engine (+ original spike)

This was the gate before the web app got built: proving **OpenFHE compiles to
WebAssembly and runs the Furever Home circuit correctly and fast, in a browser.**
The web app is now built and deployed (see [`../README.md`](../README.md)) and
ships `fhe.js` + `fhe.wasm`; `selftest.cpp` here remains a minimal standalone
WASM sanity check.

`selftest.cpp` runs the exact circuit primitives at our real params (CKKS
N=16384, depth 2, 128-bit): keygen + rotation keys {1,2,4,8,16,32}, encrypt →
`EvalMult(ct, plaintext)` → block-sum rotations → per-pet rotations → decrypt,
and checks the decrypted slots (block-start ≈ 22, per-pet overall ≈ 88).

## Result (measured)

- **Correct** in both node and Chrome: slot 22 / overall 88 (exact).
- **Fast**: ~280 ms end-to-end (context ~25 / keygen ~135 / encrypt ~11 /
  circuit ~82 / decrypt ~28 ms).
- **Small**: `selftest.wasm` ≈ 2.0 MB (downloaded once, cached).
- OpenFHE has upstream `if(EMSCRIPTEN)` support — not a hack.

## Build & run

Prereqs: Emscripten (`emcc`/`emcmake` on PATH), a clone of the OpenFHE source
([openfheorg/openfhe-development](https://github.com/openfheorg/openfhe-development)),
python3, node, a browser.

```bash
# 1. Build OpenFHE -> WASM static libs (one-time, ~minutes):
emcmake cmake -S /path/to/openfhe-development -B openfhe-build \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/openfhe-install" -DGIT_SUBMOD_AUTO=OFF
emmake make -C openfhe-build -j$(sysctl -n hw.logicalcpu) install

# 2a. node check (fastest):
./build_selftest.sh && node selftest.js

# 2b. browser check:
./build_web.sh
./serve.sh           # then open http://localhost:8782/index.html
```

The engine `fhe.js` + `fhe.wasm` **are committed** (so the repo runs as-cloned);
the OpenFHE build trees (`openfhe-build/`, `openfhe-install/`) and the `selftest*`
artifacts are gitignored.
