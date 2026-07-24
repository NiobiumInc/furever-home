# Furever Home: the DSL pipeline (run on CPU or the Niobium FPGA)

This folder holds the **encrypted scoring written in the Niobium FHE DSL** and a
one-command build that produces a working CLI pipeline you can run **on a normal
CPU** or **on Niobium FPGA hardware** (via the Fog job service).

> **Just want to try the app?** You don't need any of this. The whole thing runs
> in your browser with nothing to build; see the top-level
> [`README.md`](../README.md) ("Run it yourself"). This folder is the "graduate to
> the SDK" path: real OpenFHE C++ compiled from the `.niob` source, the same
> circuit, run natively.

> **Optional:** you're welcome to enlist an AI coding agent (for example Claude
> Code) to help you work through the build and the runs below. It's a normal
> from-source build either way.

## What's here

| File | What it is |
|---|---|
| `shared.niob`, `client.niob`, `server.niob` | the FHE program (constants/wire types, client encrypt+decrypt, server blind scoring) |
| `rubric.dat` | the pets, as plaintext weight vectors (data, not code, so you can retune without recompiling) |
| `apply_weights_bridge.cpp` / `.h`, `apply_weights_openfhe.h` | C++ bridge for the cipher×plaintext rubric multiply |
| `keygen.cpp` | hand-rolled key generation (only the rotation keys the circuit uses) |
| `reference/score_reference.py` | the plaintext scorer, the ground truth the encrypted result is checked against |
| `build_dsl.sh` | compiles the `.niob` and builds the four CLI binaries |
| `nb_out/` | generated C++ + build output (git-ignored; recreated by `build_dsl.sh`) |

## Prerequisites

A built **[niobium-client](https://github.com/NiobiumInc/niobium-client)** checkout.
It carries the DSL compiler (`nbc`), the FHETCH client runtime, and its own
vendored OpenFHE, so you do **not** need a separate OpenFHE install for this path.
Build it once:

```bash
git clone https://github.com/NiobiumInc/niobium-client
cd niobium-client
make sync && make release      # builds OpenFHE + libnbfhetch + the runtime libs
```

If the build complains about missing system prerequisites (a C++17 toolchain,
CMake, OpenSSL, Python 3, and on macOS an `OPENSSL_ROOT_DIR` export),
niobium-client's **Fog quickstart → "Install build prerequisites"** walks through
them per platform; refer back there if `make release` fails.

Point `NIOBIUM_CLIENT_ROOT` at that checkout when you build furever below.

> **Version note.** Needs a niobium-client that includes the native `@hardware`
> `save()` instrumentation (NiobiumInc/niobium-client#244 or later).

> **macOS note:** use Homebrew's toolchain; the system `cmake`/`python3` may be
> x86_64 and will fail on Apple Silicon. Pass `CMAKE=/opt/homebrew/bin/cmake` and
> `PY=/opt/homebrew/bin/python3` (see the build command).

## Build

```bash
cd dsl
NIOBIUM_CLIENT_ROOT=/path/to/niobium-client ./build_dsl.sh
# macOS:
# NIOBIUM_CLIENT_ROOT=/path/to/niobium-client \
#   CMAKE=/opt/homebrew/bin/cmake PY=/opt/homebrew/bin/python3 ./build_dsl.sh
```

`build_dsl.sh` compiles the three `.niob` files with `nbc`, adds the hand-rolled
keygen target, and builds four binaries into **`nb_out/build/`**:

```
key_generation   encrypt_answers   score_pets   decrypt_result
```

The **same binaries** run on CPU as-is, or on the FPGA via `fog submit`; there is
no separate "CPU build" vs "hardware build."

## Run on CPU

From the build dir, tell the scorer where the rubric lives, then run the four
stages. Answers are the 12 questionnaire values (each `0-4`); use **size `1`**
(the Niobium ring dimension, N=65536):

```bash
cd nb_out/build
export FUREVER_RUBRIC="$PWD/../../rubric.dat"

./key_generation 1
./encrypt_answers 1  3 4 1 1 0 3 3 2 4 2 4 3     # your 12 answers
./score_pets 1                                    # blind scoring on ciphertext
./decrypt_result 1                                # prints the scorecard
```

**Check it:** the decrypted scores must match the plaintext reference for the same
answers:

```bash
python3 ../../reference/score_reference.py 3 4 1 1 0 3 3 2 4 2 4 3
```

For this profile that's **Rex 22.85, Mochi 22.95, Smaug 28.60, Kiwi 20.70**, best
match 🦎 Smaug. The decrypted FHE result should agree to within CKKS approximation
error.

## Run on the Niobium FPGA (Fog)

Fog runs the encrypted `score_pets` stage on real Niobium hardware as a job. Two
one-time setup steps are required. See
[niobium-client](https://github.com/NiobiumInc/niobium-client#fog-quickstart)'s
Fog quickstart for detailed instructions:

1. **Get a Fog account** (access is gated, so request it early). Request one at
   [console.niobium.co/request-account](https://console.niobium.co/request-account),
   then `fog login -u you@example.com` (mints an API key cached under `~/.fog`).
2. **Install the `fog` CLI.** From your niobium-client checkout, `make install-cli`
   installs `fog` + its replay transport to `~/.local/bin`; make sure that's on
   your `PATH`.

Then the flow is identical to CPU, except you **submit `score_pets` to the
device**; keygen, encryption, and decryption stay local (your secret key never
leaves your machine):

```bash
cd nb_out/build
export FUREVER_RUBRIC="$PWD/../../rubric.dat"

./key_generation 1
./encrypt_answers 1  3 4 1 1 0 3 3 2 4 2 4 3
fog submit ./score_pets 1 --target=FOG            # blind scoring on the FPGA
./decrypt_result 1                                # same expected scorecard
```

The first local run of `score_pets` records a hardware trace; `fog submit` replays
that trace on the device and returns the encrypted result, which `decrypt_result`
reads directly. The result decrypts to the **same** scorecard as CPU and the
plaintext reference; the FPGA computed on ciphertext it could never read.
