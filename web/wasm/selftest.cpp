// Stage-0 WASM spike — does OpenFHE run the Furever Home circuit in WebAssembly?
//
// Exercises exactly the primitives the real circuit needs, at our real params:
//   CKKS context N=16384, depth 2, HEStd_128_classic, FLEXIBLEAUTO/HYBRID
//   keygen + EvalMultKeyGen + EvalRotateKeyGen({1,2,4,8,16,32})
//   encrypt -> EvalMult(ct, plaintext) -> rotate-and-add (block + per-pet) -> decrypt
//
// Prints correctness checks + per-stage timings. Built to a .js/.wasm via
// Emscripten and run under node (and later a browser). Compiled by build.sh.
#include "openfhe.h"

#include <chrono>
#include <iostream>
#include <vector>

using namespace lbcrypto;
using clk = std::chrono::steady_clock;
static double ms(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    std::cout << "MAIN_START" << std::endl;
    auto t0 = clk::now();
    CCParams<CryptoContextCKKSRNS> p;
    p.SetSecretKeyDist(UNIFORM_TERNARY);
    p.SetSecurityLevel(HEStd_128_classic);
    p.SetMultiplicativeDepth(2);
    p.SetRingDim(16384);
    p.SetScalingTechnique(FLEXIBLEAUTO);
    p.SetKeySwitchTechnique(HYBRID);
    auto cc = GenCryptoContext(p);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    auto t1 = clk::now();

    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);
    cc->EvalRotateKeyGen(kp.secretKey, std::vector<int>{1, 2, 4, 8, 16, 32});
    auto t2 = clk::now();

    // 16 blocks x 16 slots: answers q%5 for q=0..11, bias 1.0 at slot 12, pad 0.
    // Per-block raw sum = (0+1+2+3+4+0+1+2+3+4+0+1) + 1 = 21 + 1 = 22.
    std::vector<double> ans(256, 0.0);
    for (int b = 0; b < 16; ++b) {
        for (int q = 0; q < 12; ++q) ans[b * 16 + q] = q % 5;
        ans[b * 16 + 12] = 1.0;
    }
    auto ct = cc->Encrypt(kp.publicKey, cc->MakeCKKSPackedPlaintext(ans));
    auto t3 = clk::now();

    // weight multiply (all-ones plaintext) + block-sum (rot 1,2,4,8)
    std::vector<double> w(256, 1.0);
    auto cats = cc->EvalMult(ct, cc->MakeCKKSPackedPlaintext(w));
    for (int k : {1, 2, 4, 8}) cats = cc->EvalAdd(cats, cc->EvalRotate(cats, k));
    // per-pet sum (rot 16,32): slot p*64 = sum of that pet's 4 block-start slots
    auto scores = cats;
    for (int k : {16, 32}) scores = cc->EvalAdd(scores, cc->EvalRotate(scores, k));
    auto t4 = clk::now();

    Plaintext catsPt, scoresPt;
    cc->Decrypt(kp.secretKey, cats, &catsPt);
    cc->Decrypt(kp.secretKey, scores, &scoresPt);
    catsPt->SetLength(256);
    scoresPt->SetLength(256);
    auto t5 = clk::now();

    double catSlot0 = catsPt->GetRealPackedValue()[0];     // expect ~22
    double overall0 = scoresPt->GetRealPackedValue()[0];   // expect ~88 (4 blocks x 22)

    std::cout << "category block-start slot0 (expect ~22): " << catSlot0 << std::endl;
    std::cout << "pet0 overall      slot0 (expect ~88): " << overall0 << std::endl;
    std::cout << "timings(ms): context=" << ms(t0, t1) << " keygen=" << ms(t1, t2)
              << " encrypt=" << ms(t2, t3) << " circuit=" << ms(t3, t4)
              << " decrypt=" << ms(t4, t5) << " total=" << ms(t0, t5) << std::endl;

    bool ok = (catSlot0 > 21.0 && catSlot0 < 23.0) &&
              (overall0 > 87.0 && overall0 < 89.0);
    std::cout << (ok ? "SELFTEST_OK" : "SELFTEST_FAIL") << std::endl;
    std::cout.flush();
    return ok ? 0 : 1;
}
