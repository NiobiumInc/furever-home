# Furever Home: Design Notes

This app was designed with the `fhe-application-design` skill's 8-stage process.
This document records the result of each stage and the final threat model.

## Stage 1: Privacy model

- **Parties.** *Applicant* (client): holds the 12 answers about their living
  situation and the secret key. *Checker* (server): holds the pet rubric
  (weights + offsets) in plaintext and the public/evaluation keys.
- **Who encrypts?** A **single encryptor**: the applicant encrypts all of their
  own input. (No cross-party packing concerns.)
- **Who decrypts?** The applicant, with their own secret key.
- **Input privacy.** The server computes on ciphertext only; it never sees the
  answers. The rubric stays plaintext server-side and never reaches the client,
  so only aggregate scores return.
- **Output privacy.** The decryptor *is* the data owner reading their own result,
  so revealing the scores to them is by definition fine.
- **Output integrity.** The decryptor **is** the result consumer (you're checking
  your own readiness). Per the skill, that means **no integrity problem and no
  transciphering**: there is no third party who needs to trust the number. This
  is what keeps the circuit shallow and the demo honest (it's advisory).

## Stage 2: FHE feasibility

1. **Data-oblivious?** Yes. Scoring is a fixed weighted sum; the only "decision"
   (best match / clamping) happens client-side after decryption. No branching on
   encrypted values.
2. **One lane?** Pure CKKS arithmetic (adds, one plaintext multiply, rotations).
3. **Multiplicative depth?** **1.** Only the cipher × plaintext weight multiply
   consumes a level; block-sum and per-pet-sum are rotate-and-add (0 levels).
4. **SIMD parallelism?** We pack all `NUM_PETS × NUM_CATEGORIES` (pet, category)
   pairs into one ciphertext and score them in a single pass. Honest caveat:
   this is one applicant, so only 256 of N/2 slots are used. SIMD *utilization* is
   low (the demo's value is the privacy + batching pattern, not raw slot density).
   At scale, the same circuit would batch many applicants column-wise.

## Stage 3: Plaintext algorithm (ground truth)

`reference/score_reference.py`:

```
category[pet][cat] = sum_q answers[q]*weight[pet][cat][q] + offset[pet][cat]
overall[pet]       = sum_cat category[pet][cat]
```

All branchless. No non-linear functions (the linear v1 needs none). Answers are
on a 0..4 integer-ish scale; CKKS approximation is negligible at depth 1.

The **offset** is the key modeling trick: it lets a purely linear model express
"how demanding a pet is" (a forgiving pet has a high floor + gentle slope; a
demanding pet has ~0 floor + steep slope) without any comparison/min, which
would be non-linear.

## Stage 4: Scheme

**CKKS:** real-valued scores, small approximation tolerated, cheap bulk arithmetic.

## Stage 5: Homomorphic circuit

**Packing.** `NUM_BLOCKS = 16` contiguous blocks of `BLOCK = 16` slots (256
active). Block `b = pet*NUM_CATEGORIES + cat` holds the 12 answers (replicated
across all blocks by the client) in slots `b*16+0..11`, a constant `1.0` **bias
slot** at `b*16+12`, and zero padding at `+13..15`. The server's plaintext
vector `W` carries the weights in `b*16+0..11` and the **offset** in the bias
slot, so the offset is added for free during the block-sum.

**Compute.**
1. `weighted = answers ⊙ W`, cipher × plaintext (extern bridge). **[+1]**
2. Block-sum: `for k in {1,2,4,8}: weighted += rotate(weighted, k)`. After this,
   slot `b*16` = sum of block `b` = that (pet, category) score. **[+0]**
3. Per-pet sum: `for k in {16,32}: scores += rotate(scores, k)`. Offsets 16 and
   32 land exactly on the four block-start slots of each pet, so slot `pet*64`
   accumulates that pet's 4 category scores. **[+0]**

**Why no masking.** The non-block-start slots hold overlapping window garbage
after step 2, but steps 2–3 only ever *read* block-start slots into other
block-start slots, and the client only *reads* block-start slots. So no mask
multiply is needed, saving a level and a bridge.

**Slot aggregation** uses rotate-and-add (0 depth), needing rotation keys for
`{1,2,4,8,16,32}` only.

**Outputs.** `categories.bin` (category scores at slots `(pet*4+cat)*16`) and
`scores.bin` (overall at slots `pet*64`).

## Stage 6: Parameters

| | Toy (default) | Hardware |
|---|---|---|
| Ring dim N | 16384 | 65536 |
| Mult depth | 2 (1 used) | 2 |
| Security | HEStd_128_classic | HEStd_128_classic |
| Scaling / key-switch | FLEXIBLEAUTO / HYBRID | same |

N=8192 is rejected by OpenFHE's 128-bit security tables for this modulus chain,
so Toy is 16384. Measured sizes: Toy keys ≈ 18 MB (rk 15 MB), each ciphertext
≈ 0.75 MB; Hardware keys ≈ 75 MB (rk 60 MB), each ciphertext ≈ 3 MB. Far lighter
than a deep circuit (e.g. depth 12, which would need ~540 MB of rotation keys)
because depth here is 1 and only 6 rotation indices are generated.

## Stage 7: Implementation & test

Four binaries (`key_generation`, `encrypt_answers`, `score_pets`,
`decrypt_result`). Keygen is hand-rolled; the weight multiply is an extern
bridge. Every decrypted score is checked against the plaintext reference:
**max abs error 0.0000** across all tested profiles (well under the 0.5
tolerance), and the FHE best-match equals the plaintext best-match.
(`web/wasm/test_module.js` runs this same check on the WebAssembly engine.)

## Stage 8: Protocol & threat model

**Message flow.** Setup: client runs keygen, sends `cc/pk/mk/rk` to the server
(once), keeps `sk`. Per query: client → server `answers.bin`; server → client
`scores.bin`, `categories.bin`. Client decrypts.

**What each party learns.**
- *Server:* nothing about the answers (ciphertext only); it does learn that a
  query happened, its timing, and that it's a single applicant.
- *Client:* its own per-pet/per-category scores. The rubric weights are not sent;
  the client only sees aggregates.

**Intentional leakage:** the scores themselves (to the client, who owns them).

**Honest limitations (not overclaimed):**
- *No computational integrity.* FHE does not prove the server scored honestly.
  Fine here since it's an advisory self-check, and the client isn't trying to prove
  anything to anyone.
- *Rubric is confidential, not secret-against-probing.* A client could submit
  many crafted answer vectors and, from the returned scores, reverse-engineer
  the plaintext weights (the circuit is linear, so a handful of queries suffices).
  Mitigation if it mattered: query rate-limiting / coarsening. For a demo it
  doesn't.
- *Output can reflect inputs.* Trivially true (the score is a function of your
  answers) and harmless, since the decryptor is the data owner.
- *No protection against side channels / malicious server / collusion.*

**Not a real adoption decision.** It's a fun pre-check. A real adoption still
involves a shelter and, well, meeting the animal.
