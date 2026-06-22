// fhe.cpp — Furever Home FHE engine for the browser (Stage 1).
//
// Mirrors the four CLI binaries (keygen / encrypt / score / decrypt) as embind
// functions operating fully in memory. Serialization uses the SAME OpenFHE
// BINARY format as the CLI's .bin files, so a blob produced here is consumable
// by the CLI `score_pets` and vice-versa (the interop invariant).
//
// Blobs cross the JS<->wasm boundary as base64 strings (robust across Emscripten
// versions); answers/weights cross as plain number arrays.
#include "openfhe.h"
#include "ciphertext-ser.h"
#include "cryptocontext-ser.h"
#include "key/key-ser.h"
#include "scheme/ckksrns/ckksrns-ser.h"

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <sstream>
#include <string>
#include <vector>

using namespace lbcrypto;
using emscripten::val;

// ---- base64 ----------------------------------------------------------------
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string b64enc(const std::string& in) {
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c; bits += 8;
        while (bits >= 0) { out.push_back(B64[(val >> bits) & 0x3F]); bits -= 6; }
    }
    if (bits > -6) out.push_back(B64[((val << 8) >> (bits + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
static std::string b64dec(const std::string& in) {
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; ++i) T[(unsigned char)B64[i]] = i;
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c]; bits += 6;
        if (bits >= 0) { out.push_back(char((val >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

// ---- (de)serialization helpers (BINARY, matching the CLI) ------------------
template <typename T> static std::string ser(const T& obj) {
    std::stringstream ss;
    Serial::Serialize(obj, ss, SerType::BINARY);
    return b64enc(ss.str());
}
template <typename T> static T deser(const std::string& b64) {
    std::stringstream ss(b64dec(b64));
    T obj;
    Serial::Deserialize(obj, ss, SerType::BINARY);
    return obj;
}

static CryptoContext<DCRTPoly> makeContext(bool hardware) {
    CCParams<CryptoContextCKKSRNS> p;
    p.SetSecretKeyDist(UNIFORM_TERNARY);
    p.SetSecurityLevel(HEStd_128_classic);
    p.SetMultiplicativeDepth(2);
    p.SetRingDim(hardware ? 65536 : 16384);
    p.SetScalingTechnique(FLEXIBLEAUTO);
    p.SetKeySwitchTechnique(HYBRID);
    auto cc = GenCryptoContext(p);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    return cc;
}

// ---- the four operations ---------------------------------------------------
// keygen -> { cc, pk, sk, mk, rk }  (base64). sk must stay on the client.
static val keygen(bool hardware) {
    auto cc = makeContext(hardware);
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);
    cc->EvalRotateKeyGen(kp.secretKey, std::vector<int>{1, 2, 4, 8, 16, 32});
    std::stringstream mk, rk;
    cc->SerializeEvalMultKey(mk, SerType::BINARY);
    cc->SerializeEvalAutomorphismKey(rk, SerType::BINARY);
    val o = val::object();
    o.set("cc", ser(cc));
    o.set("pk", ser(kp.publicKey));
    o.set("sk", ser(kp.secretKey));
    o.set("mk", b64enc(mk.str()));
    o.set("rk", b64enc(rk.str()));
    return o;
}

// encrypt 12 answers -> packed/replicated 256-slot ciphertext blob (base64).
static std::string encrypt(const std::string& ccB, const std::string& pkB, const val& answersArr) {
    auto cc = deser<CryptoContext<DCRTPoly>>(ccB);
    auto pk = deser<PublicKey<DCRTPoly>>(pkB);
    auto answers = emscripten::vecFromJSArray<double>(answersArr);  // 12
    std::vector<double> packed(256, 0.0);
    for (int b = 0; b < 16; ++b) {
        for (int q = 0; q < 12 && q < (int)answers.size(); ++q) packed[b * 16 + q] = answers[q];
        packed[b * 16 + 12] = 1.0;  // bias slot
    }
    auto ct = cc->Encrypt(pk, cc->MakeCKKSPackedPlaintext(packed, 1, 0));
    return ser(ct);
}

// score on encrypted answers (NO secret key) -> { scores, categories } blobs.
// weights256 is the server-side plaintext rubric (12 weights + offset per block).
static val score(const std::string& ccB, const std::string& mkB, const std::string& rkB,
                 const std::string& ansB, const val& weightsArr) {
    auto cc = deser<CryptoContext<DCRTPoly>>(ccB);
    // The eval-key maps are static/global in OpenFHE. In the CLI each stage is a
    // separate process (fresh maps); in a single WASM instance (or if keygen ran
    // here) they may already hold this context's keys, so clear before loading.
    CryptoContextImpl<DCRTPoly>::ClearEvalMultKeys();
    CryptoContextImpl<DCRTPoly>::ClearEvalAutomorphismKeys();
    { std::stringstream mk(b64dec(mkB)); cc->DeserializeEvalMultKey(mk, SerType::BINARY); }
    { std::stringstream rk(b64dec(rkB)); cc->DeserializeEvalAutomorphismKey(rk, SerType::BINARY); }
    auto answers = deser<Ciphertext<DCRTPoly>>(ansB);
    auto w = emscripten::vecFromJSArray<double>(weightsArr);  // 256
    auto cats = cc->EvalMult(answers, cc->MakeCKKSPackedPlaintext(w, 1, 0));
    for (int k : {1, 2, 4, 8}) cats = cc->EvalAdd(cats, cc->EvalRotate(cats, k));
    auto scores = cats;
    for (int k : {16, 32}) scores = cc->EvalAdd(scores, cc->EvalRotate(scores, k));
    val o = val::object();
    o.set("scores", ser(scores));
    o.set("categories", ser(cats));
    return o;
}

// decrypt (client, with sk) -> { overall:[4], categories:[16] } readable numbers.
static val decrypt(const std::string& ccB, const std::string& skB,
                   const std::string& scoresB, const std::string& catsB) {
    auto cc = deser<CryptoContext<DCRTPoly>>(ccB);
    auto sk = deser<PrivateKey<DCRTPoly>>(skB);
    Plaintext sp, cp;
    cc->Decrypt(sk, deser<Ciphertext<DCRTPoly>>(scoresB), &sp);
    cc->Decrypt(sk, deser<Ciphertext<DCRTPoly>>(catsB), &cp);
    sp->SetLength(256);
    cp->SetLength(256);
    auto sv = sp->GetRealPackedValue();
    auto cv = cp->GetRealPackedValue();
    val overall = val::array();
    for (int p = 0; p < 4; ++p) overall.set(p, sv[p * 64]);          // slot pet*64
    val cats = val::array();
    for (int i = 0; i < 16; ++i) cats.set(i, cv[i * 16]);            // slot (pet*4+cat)*16
    val o = val::object();
    o.set("overall", overall);
    o.set("categories", cats);
    return o;
}

EMSCRIPTEN_BINDINGS(furever_fhe) {
    emscripten::function("keygen", &keygen);
    emscripten::function("encrypt", &encrypt);
    emscripten::function("score", &score);
    emscripten::function("decrypt", &decrypt);
}
