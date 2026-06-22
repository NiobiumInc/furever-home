# Furever Home — web app

A browser version of the demo: the FHE runs in WebAssembly (OpenFHE → Emscripten).
Two roles — **survey** (your device, holds the secret key) and **checker** (the
"server", no secret key) — usable same-device or cross-device.

**Live:** https://pet-adoption-8j98.onrender.com — to run your own copy locally,
read on. To host one, deploy the included `Dockerfile` (the relay serves the
static app + the hand-off endpoints) to any container host — run a single
instance, since the relay keeps hand-offs in memory.

## Run it

The WASM (`web/wasm/fhe.js` + `fhe.wasm`) is prebuilt and committed, so there's
nothing to compile — just start the server:

```bash
cd furever-home        # the repo root
node web/relay.js
```

It prints two URLs:
- `http://localhost:8800/web/index.html` — on this machine.
- `http://<your-LAN-IP>:8800/web/index.html` — **open this one** so the QR code
  is scannable by phones/other devices on the same Wi-Fi.

Stop it with `Ctrl-C` (or `pkill -f relay.js`). `relay.js` is plain Node — no
dependencies.

## Let someone else try it
- **Same Wi-Fi:** share the LAN URL, or have them scan the QR on the survey page.
- **Anywhere (quick tunnel):** `brew install cloudflared` then
  `cloudflared tunnel --url http://localhost:8800` → gives a public https URL.
- The relay only ever holds ciphertext + public/eval keys — never a secret key —
  so exposing it doesn't leak answers. It's a demo, though (in-memory, 15-min
  TTL, light rate-limiting).

## If the WASM is ever missing (fresh clone / cleaned build)
Needs Emscripten (`brew install emscripten`) + a clone of the OpenFHE source
([openfheorg/openfhe-development](https://github.com/openfheorg/openfhe-development)),
then:

```bash
cd web/wasm
emcmake cmake -S /path/to/openfhe-development -B openfhe-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/openfhe-install" -DGIT_SUBMOD_AUTO=OFF
emmake make -C openfhe-build -j$(sysctl -n hw.logicalcpu) install
./build_module.sh        # -> fhe.js + fhe.wasm  (the app)
```

## Add real pet photos
Drop `rex.jpg / mochi.jpg / smaug.jpg / kiwi.jpg` into `web/public/pets/`
(see that folder's README). Until then the UI shows emoji placeholders.
