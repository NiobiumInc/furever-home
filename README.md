# 🐾 Furever Home

**Could you adopt this pet? Find out, without telling anyone your business.**

Adoption applications are nosy: income, rent-or-own, hours away from home, prior
pets, your landlord's name. Furever Home is a self-screening pre-check that
scores you against several pets' needs **on encrypted answers**. The checker
service computes your readiness without ever seeing what you typed, and only you
can decrypt the result.

It's a small, concrete demo of **Fully Homomorphic Encryption (FHE)**: compute on
data you can't see. Built with the [Niobium FHE DSL](https://github.com/NiobiumInc/niobium-client)
on top of OpenFHE (CKKS).

> Advisory only. It's a fun pre-check, not a real adoption decision, and FHE
> protects your *inputs*, not the correctness of someone else's computation. See
> [`DESIGN_NOTES.md`](DESIGN_NOTES.md) for the honest threat model.

**▶️ Try it live in your browser:** https://pet-adoption-8j98.onrender.com

> 💤 The live demo runs on a free tier and naps between visitors, so the first load
> can take a few seconds to wake up. That's the hosting, not the FHE.

The whole thing (encrypt, blind-score, decrypt) runs in WebAssembly, with nothing
to install. See [Trying it](#trying-it) below for the different ways to get your match.

## The pets

| | Pet | Personality |
|---|---|---|
| 🐺 | **Rex** | high-energy husky: wants space, a yard, lots of activity, real budget |
| 🐱 | **Mochi** | aloof senior cat: tolerant of small spaces, long hours away, a modest budget |
| 🦎 | **Smaug** | bearded dragon: low-attention, but needs a proper habitat, equipment budget, know-how |
| 🦜 | **Kiwi** | parrot: wants you home and engaged, a long commitment, an experienced owner |

Different answers light up different pets. The pets are just plaintext data in
[`dsl/rubric.dat`](dsl/rubric.dat); swap or retune them without recompiling. This app
can also be extended to run a shelter's real roster (photos, needs, quirks) in place
of these four demo pets, letting would-be adopters privately find the pet they're
most likely to be approved for before filling out a single form.

## The questionnaire

12 questions, each **0–4** ("how much of this resource do you have", higher =
more), grouped into four categories:

- **Housing:** stability / pets allowed · indoor space · outdoor/yard
- **Time:** hours home with the pet · daily active time · schedule stability
- **Finances:** monthly budget · emergency-vet readiness · setup/equipment
- **Experience:** prior pet ownership · experience with this species · calm home

## Trying it

The app has **two roles**:

- **📝 Survey** (your device): makes your keypair, asks the 12 questions, encrypts
  your answers, and decrypts the result when it comes back. Your **secret key never
  leaves this page**.
- **🔒 Checker** (the "server"): receives only your ciphertext and public keys,
  scores you **blind**, and sends back an encrypted result. It has no secret key, so
  it can't read your answers or the result.

A full run connects the two: your encrypted answers go to a checker, it scores them
blind, and the encrypted result comes back for you to decrypt. Pick whichever setup
suits you.

#### 📱 Just you: computer + phone (easiest)

On a computer, open the [live site](https://pet-adoption-8j98.onrender.com) and fill
in the Survey. Tap **"Send to a checker"**; it shows a QR code. Scan that QR code with
a phone and you'll land on the Checker. Score your encrypted survey there, then send
the result back to the computer to decrypt. Here your phone just stands in as the
checker.

#### 💻 Just you: one computer (no phone)

Two single-machine routes:
- *Follow the link:* fill in the Survey and tap **"Send to a checker"**.
  Copy the checker link shown under the QR into a **new browser tab** (that tab
  becomes the Checker); it scores, and the result flows back to the Survey tab.
- *Pass a file:* open the site in two tabs, click into the Survey in one and the
  Checker in the other. Use the Survey's **"Download bundle file"** to download your
  encrypted bundle, upload it to the Checker with its "use a file instead" option,
  and score. Finally, download the encrypted result and upload it back to the Survey
  to decrypt.

#### 👥 Two people

You fill in the Survey; someone else opens the Checker on their device (scan your QR,
or open a link you send), scores you blind, and the encrypted result comes back to
you. They never see your answers or hold your key.

**Curious whether it's really encrypted?** At each step the app lets you peek at
what's being handed over: high-entropy ciphertext, with your secret key visibly
*not* in the bundle. So you can check for yourself that nothing readable ever
leaves your device.

> **Doing the whole flow on a single phone** (hand-downloading and re-uploading
> bundle files) is possible but fiddly on mobile, so we'd suggest a computer for the
> full run.

## Run it yourself (no build)

Everything (keygen, encryption, blind scoring, and decryption) runs in the browser
via WebAssembly (OpenFHE via Emscripten), and the compiled engine is committed, so
there's **nothing to build**:

```bash
git clone https://github.com/sw-zzz/furever-home && cd furever-home
node web/relay.js          # prints a localhost URL, plus a LAN URL for the phone/QR
```

Open the printed URL to get the same Survey and Checker described above. To use a
phone as the Checker, open the **LAN URL** it prints (not localhost) so the phone
can reach the relay and scan the QR. More in [`web/README.md`](web/README.md), or
just watch the ~2-minute walkthrough [`demo.mp4`](demo.mp4).

## The FHE circuit

The encrypted scoring is about **ten lines** of the Niobium FHE DSL in
[`dsl/server.niob`](dsl/server.niob): multiply the encrypted answers by each pet's
plaintext weight vector and sum (`category = Σ answersᵢ·weightᵢ + offset`,
`overall = Σ categories`). [`dsl/client.niob`](dsl/client.niob) packs/encrypts the 12
answers and decrypts the returned scores; [`dsl/shared.niob`](dsl/shared.niob) holds
the constants and wire types; [`dsl/rubric.dat`](dsl/rubric.dat) is the pets as
plaintext weights.

### Read the code (and run the plaintext version)

**Runnable with no build,** the same scoring in the clear:
```bash
python3 dsl/reference/score_reference.py 4 4 4 4 4 4 4 4 4 4 4 4
```
prints the plaintext scorecard (the ground truth the encrypted result is checked
against). The twelve values are your answers, each 0-4; change them to score a
different profile.

**Test it (no build),** a full *encrypted* roundtrip checked against that reference:
```bash
node web/wasm/test_module.js 4 4 4 4 4 4 4 4 4 4 4 4
```
runs keygen → encrypt → score → decrypt in WebAssembly and prints the decrypted
scores next to the plaintext reference for the same answers, confirming they match.
Pass any 12 answers (0–4), or none for a default profile.

### Build and run it natively (CPU or Niobium FPGA)

The encrypted CLI pipeline (`key_generation → encrypt_answers → score_pets →
decrypt_result`, plus `keygen.cpp` and the `apply_weights` bridge) compiles from the
`.niob` source through the Niobium DSL toolchain in
[**niobium-client**](https://github.com/NiobiumInc/niobium-client). Once you've
built that once, a single script builds the binaries, and they run the same on a
normal CPU or on Niobium FPGA hardware via the Fog job service. Step-by-step:
[`dsl/README.md`](dsl/README.md). (No SDK to install? The web app above gives you
the full encrypted experience in your browser with nothing to build.)

### Why it's real encryption

Not a lock icon over plaintext:
- What leaves your device is high-entropy ciphertext; the same answers encrypt to
  *different* bytes each time (CKKS is randomized).
- The checker is handed only public/eval keys, **no secret key**, so it literally
  cannot decrypt your answers *or* the result it produces.
- Yet the decrypted result **exactly matches** the plaintext reference above: a
  party that couldn't read your inputs but produced the right output can only have
  computed on the ciphertext.

## How the privacy works

You hold the secret key. The client packs and encrypts your 12 answers on your
device. The checker (the "server") multiplies the ciphertext by the plaintext
rubric and sums it homomorphically: it sees only ciphertext, and has no secret key,
so it cannot decrypt. You decrypt the returned scores on your device. The shelter's
exact weights never leave the server; your answers never leave your device in the
clear.

In the **web app** this is physical: your device runs keygen/encrypt/decrypt
(secret key stays local), while the checker (in a second tab, on another device, or
on a hosted relay) only ever holds ciphertext and public keys.

## What's in here

🟢 = runs with no build · 📖 = source to read (build via the niobium-client SDK)

```
README.md            this file
DESIGN_NOTES.md      the 8-stage FHE design writeup + threat model
demo.mp4          🟢 ~2-minute narrated walkthrough (watch it)
web/              🟢 the browser app, in-browser WASM FHE  (run: node web/relay.js)
  survey.html          "your device": keygen, questions, encrypt, decrypt
  checker.html         "the checker": blind scoring, no secret key
  relay.js             dumb cross-device hand-off + static server
  vendor/qrcode.js     QR-code generator (third-party, MIT, see NOTICE)
  wasm/                the FHE engine compiled to WebAssembly (prebuilt, committed)
  wasm/test_module.js  🟢 full encrypted roundtrip test vs the plaintext reference
dsl/                 the FHE circuit (Niobium DSL)
  server.niob        📖 the encrypted scoring (the ~10-line circuit)
  client.niob, shared.niob  📖 packing, encrypt/decrypt, constants/wire types
  rubric.dat           the pets (weights + offsets), single source of truth
  reference/score_reference.py  🟢 plaintext ground-truth scorer (pure Python)
  keygen.cpp, apply_weights_*  📖 hand-rolled keygen + cipher×plaintext bridge
Dockerfile           single-service container for hosting the app
LICENSE              Apache License 2.0
NOTICE               third-party attributions (OpenFHE, Emscripten, QR-code generator)
```

## License

Apache License 2.0. See [`LICENSE`](LICENSE). This repo also bundles third-party
open-source components: the in-browser FHE engine (OpenFHE, BSD 2-Clause) compiled to
WebAssembly with Emscripten (MIT), and the QR-code generator (MIT) under `web/vendor/`.
Each is listed with its full license text in [`NOTICE`](NOTICE).
