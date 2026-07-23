# 🐾 Furever Home

**Could you adopt this pet? Find out — without telling anyone your business.**

Adoption applications are nosy: income, rent-or-own, hours away from home, prior
pets, your landlord's name. Furever Home is a self-screening pre-check that
scores you against several pets' needs **on encrypted answers** — the checker
service computes your readiness without ever seeing what you typed, and only you
can decrypt the result.

It's a small, concrete demo of **Fully Homomorphic Encryption (FHE)**: compute on
data you can't see. Built with the [Niobium FHE DSL](https://github.com/NiobiumInc/niobium-client)
on top of OpenFHE (CKKS).

> Advisory only. It's a fun pre-check, not a real adoption decision — and FHE
> protects your *inputs*, not the correctness of someone else's computation. See
> [`DESIGN_NOTES.md`](DESIGN_NOTES.md) for the honest threat model.

**▶️ Try it live in your browser:** https://pet-adoption-8j98.onrender.com — the
whole thing (encrypt, blind-score, decrypt) runs client-side in WebAssembly, and
you can hand a checker off to a friend's phone via QR. Nothing to install.

## The pets

| | Pet | Personality |
|---|---|---|
| 🐺 | **Rex** | high-energy husky — wants space, a yard, lots of activity, real budget |
| 🐱 | **Mochi** | aloof senior cat — tolerant of small spaces, long hours away, a modest budget |
| 🦎 | **Smaug** | bearded dragon — low-attention, but needs a proper habitat, equipment budget, know-how |
| 🦜 | **Kiwi** | parrot — wants you home and engaged, a long commitment, an experienced owner |

Different answers light up different pets. The pets are just plaintext data in
[`dsl/rubric.dat`](dsl/rubric.dat) — swap or retune them without recompiling.

## The questionnaire

12 questions, each **0–4** ("how much of this resource do you have", higher =
more), grouped into four categories:

- **Housing** — stability / pets allowed · indoor space · outdoor/yard
- **Time** — hours home with the pet · daily active time · schedule stability
- **Finances** — monthly budget · emergency-vet readiness · setup/equipment
- **Experience** — prior pet ownership · experience with this species · calm home

## Run it in your browser (no build)

The whole app runs **entirely in the browser** — keygen, encryption, blind
scoring, and decryption are all WebAssembly (OpenFHE via Emscripten). The
compiled engine ships in the repo, so there's **nothing to build**:

```bash
git clone https://github.com/sw-zzz/furever-home && cd furever-home
node web/relay.js          # prints a localhost URL (+ a LAN URL for the phone/QR)
```

Open the printed URL. (`relay.js` is plain Node — no dependencies, no compile.)
It has two roles you can split across devices:

- **📝 Survey — your device.** Generates your keypair, asks the 12 questions, and
  encrypts your answers. Your **secret key never leaves the page**.
- **🔒 Checker — the "server" (e.g. a friend's phone).** Receives only your
  ciphertext + public keys, scores you **blind**, and returns an encrypted
  result. It has no secret key, so it can't read your answers or the result.

**Cross-device hand-off:** tap *"Send to a friend (QR)"* — your encrypted bundle
goes through a dumb relay (ciphertext + public keys only; no secret key, no
compute), your friend's device scores it in *their* browser, and the encrypted
result comes back for you to decrypt. You can also do both roles on one device,
or hand off a file with no server at all.

See [`web/README.md`](web/README.md) to run/share it. It deploys as a single Node
container (a generic `Dockerfile` is included), so you can host it on anything
that runs containers. Prefer to just watch? Here's a ~80-second narrated
walkthrough: [`demo.mp4`](demo.mp4).

## The FHE circuit

The encrypted scoring is about **ten lines** of the Niobium FHE DSL in
[`dsl/server.niob`](dsl/server.niob): multiply the encrypted answers by each pet's
plaintext weight vector and sum — `category = Σ answersᵢ·weightᵢ + offset`,
`overall = Σ categories`. [`dsl/client.niob`](dsl/client.niob) packs/encrypts the 12
answers and decrypts the returned scores; [`dsl/shared.niob`](dsl/shared.niob) holds
the constants and wire types; [`dsl/rubric.dat`](dsl/rubric.dat) is the pets as
plaintext weights.

### Read the code (and run the plaintext version)

**Runnable with no build** — the same scoring in the clear:
```bash
python3 dsl/reference/score_reference.py 4 4 4 4 4 4 4 4 4 4 4 4
```
prints the plaintext scorecard — the ground truth the encrypted result is
checked against.

**Test it (no build)** — a full *encrypted* roundtrip against that reference:
```bash
node web/wasm/test_module.js
```
runs keygen → encrypt → score → decrypt in WebAssembly and confirms the decrypted
scores match the plaintext reference (max error 0.0).

### Build and run it natively (CPU or Niobium FPGA)

The encrypted CLI pipeline
(`key_generation → encrypt_answers → score_pets → decrypt_result`, plus
`keygen.cpp` and the `apply_weights` bridge) compiles from the `.niob` source
through the Niobium DSL toolchain in
[**niobium-client**](https://github.com/NiobiumInc/niobium-client). Once you've
built that once, a single script builds the binaries, and they run the same on a
normal CPU or on Niobium FPGA hardware via the Fog job service. Step-by-step:
[`dsl/README.md`](dsl/README.md). (No SDK to install? The web app above gives you
the full encrypted experience in your browser with nothing to build.)

### Why it's real encryption

Not a lock icon over plaintext:
- What leaves your device is high-entropy ciphertext; the same answers encrypt to
  *different* bytes each time (CKKS is randomized).
- The checker is handed only public/eval keys — **no secret key** — so it
  literally cannot decrypt your answers *or* the result it produces.
- Yet the decrypted result **exactly matches** the plaintext reference above — a
  party that couldn't read your inputs but produced the right output can only have
  computed on the ciphertext.

## How the privacy works

You hold the secret key. The client packs and encrypts your 12 answers on your
device. The checker (the "server") multiplies the ciphertext by the plaintext
rubric and sums it homomorphically — it sees only ciphertext, and has no secret
key, so it cannot decrypt. You decrypt the returned scores on your device. The
shelter's exact weights never leave the server; your answers never leave your
device in the clear.

In the **web app** this is physical: your device runs keygen/encrypt/decrypt
(secret key stays local), while the checker — on a friend's device or the
deployed relay — only ever holds ciphertext and public keys.

## What's in here

🟢 = runs with no build · 📖 = source to read (build via the niobium-client SDK)

```
README.md            this file
DESIGN_NOTES.md      the 8-stage FHE design writeup + threat model
demo.mp4          🟢 ~80-second narrated walkthrough (watch it)
web/              🟢 the browser app — in-browser WASM FHE  (run: node web/relay.js)
  survey.html          "your device" — keygen, questions, encrypt, decrypt
  checker.html         "the checker" — blind scoring, no secret key
  relay.js             dumb cross-device hand-off + static server
  vendor/qrcode.js     QR-code generator (third-party, MIT — see NOTICE)
  wasm/                the FHE engine compiled to WebAssembly (prebuilt, committed)
  wasm/test_module.js  🟢 full encrypted roundtrip test vs the plaintext reference
dsl/                 the FHE circuit (Niobium DSL)
  server.niob        📖 the encrypted scoring (the ~10-line circuit)
  client.niob, shared.niob  📖 packing, encrypt/decrypt, constants/wire types
  rubric.dat           the pets (weights + offsets) — single source of truth
  reference/score_reference.py  🟢 plaintext ground-truth scorer (pure Python)
  keygen.cpp, apply_weights_*  📖 hand-rolled keygen + cipher×plaintext bridge
Dockerfile           single-service container for hosting the app
LICENSE              Apache License 2.0
NOTICE               third-party attributions (bundled QR-code generator, MIT)
```

## License

Apache License 2.0 — see [`LICENSE`](LICENSE). Third-party components bundled in
this repo (the QR-code generator under `web/vendor/`) are listed with their
licenses in [`NOTICE`](NOTICE).
