// keygen.cpp — hand-rolled key generation for Furever Home.
//
// This replaces the @client @stage("key_generation") that the DSL would
// normally emit. We bypass the DSL because its `requires { rotate }` capability
// is all-or-nothing: it calls EvalRotateKeyGen with every index 1..N/2-1, which
// at the hardware ring dimension N=65536 means 32,767 rotation keys (hundreds
// of GB), so we hand-roll only the indices the circuit actually uses.
//
// The score_pets circuit uses exactly six rotation indices — {1,2,4,8} for the
// per-block sum and {16,32} for the per-pet sum — so we pass that exact set to
// EvalRotateKeyGen.
//
// Usage:  key_generation <size>     size = 0|toy (N=8192, default) or 1|hw (N=65536)

#include "nb_shared.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    // Accept either an integer (0/1, matching the DSL-generated binaries) or a
    // friendly name ("toy"/"hw").
    InstanceSize size = InstanceSize::Toy;
    if (argc >= 2) {
        std::string arg = argv[1];
        if (arg == "0" || arg == "toy")      size = InstanceSize::Toy;
        else if (arg == "1" || arg == "hw")  size = InstanceSize::Hardware;
        else {
            std::cout << "Usage: " << argv[0] << " <size>\n"
                      << "  size: 0|toy  -> N=8192  (default, snappy)\n"
                      << "        1|hw   -> N=65536 (hardware ring dimension)\n";
            return arg == "-h" || arg == "--help" ? 0 : 1;
        }
    }
    auto inst = instance(size);

    // ----------------------------------------------------------------------
    // CKKS parameters.
    //   Ring dim: from the instance (8192 Toy / 65536 Hardware).
    //   Security: HEStd_128_classic (never HEStd_NotSet, even for the demo).
    //   Depth:    the circuit uses ONE multiply (cipher x plaintext weights);
    //             everything else is rotate-and-add (0 levels). Depth 2 leaves
    //             a one-level margin and keeps N=8192 comfortably 128-bit secure.
    // ----------------------------------------------------------------------
    const uint32_t MULT_DEPTH = 2;

    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetSecurityLevel(HEStd_128_classic);
    parameters.SetMultiplicativeDepth(MULT_DEPTH);
    parameters.SetRingDim(inst.ring_dim);
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetKeySwitchTechnique(HYBRID);

    auto cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);

    auto kp = cc->KeyGen();

    // ----------------------------------------------------------------------
    // Key material — scoped to exactly what score_pets uses.
    // ----------------------------------------------------------------------

    // Relinearization key. The scoring multiply is cipher x plaintext (which
    // does not need it), but the CryptoParams wire carries an eval-mult key, so
    // we generate one for completeness / forward use.
    cc->EvalMultKeyGen(kp.secretKey);

    // Rotation keys for the two rotate-and-sum stages:
    //   {1,2,4,8}  — collapse each 16-slot block to its category score
    //   {16,32}    — collapse each pet's 4 category slots to an overall score
    cc->EvalRotateKeyGen(kp.secretKey, std::vector<int>{1, 2, 4, 8, 16, 32});

    // ----------------------------------------------------------------------
    // Serialize to the layout the DSL-generated stages expect.
    // ----------------------------------------------------------------------
    auto keys_dir = keydir(inst);
    fs::create_directories(keys_dir);

    Serial::SerializeToFile(keys_dir / "cc.bin", cc, SerType::BINARY);
    Serial::SerializeToFile(keys_dir / "pk.bin", kp.publicKey, SerType::BINARY);
    Serial::SerializeToFile(keys_dir / "sk.bin", kp.secretKey, SerType::BINARY);

    {
        std::ofstream mk_file(keys_dir / "mk.bin", std::ios::out | std::ios::binary);
        cc->SerializeEvalMultKey(mk_file, SerType::BINARY);
    }
    {
        std::ofstream rk_file(keys_dir / "rk.bin", std::ios::out | std::ios::binary);
        cc->SerializeEvalAutomorphismKey(rk_file, SerType::BINARY);
    }

    std::cout << "[keygen] N=" << cc->GetRingDimension()
              << " depth=" << MULT_DEPTH
              << " security=128-classic"
              << " rotation_keys={1,2,4,8,16,32}\n";
    std::cout << "[keygen] wrote " << keys_dir << "/{cc,pk,sk,mk,rk}.bin\n";
    return 0;
}
