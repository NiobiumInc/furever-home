// apply_weights_bridge.cpp — server-side plaintext rubric for Furever Home.
//
// Builds the 256-slot weight+offset vector W from dsl/rubric.dat and multiplies
// the packed answer ciphertext by it (cipher x plaintext, element-wise). The
// rubric file is the single source of truth shared with the Python reference
// scorer, so swapping/retuning pets is a data edit with no recompile.
//
// For block b = pet*NUM_CATEGORIES + cat:
//   W[b*BLOCK + q]  = weight of question q   (q in 0..NUM_QUESTIONS)
//   W[b*BLOCK + NUM_QUESTIONS] = offset      (rides the constant bias slot)
//   remaining padding slots stay 0.
#include "apply_weights_openfhe.h"
#include "nb_shared.h"   // NUM_QUESTIONS, NUM_CATEGORIES, NUM_PETS, BLOCK, ACTIVE_SLOTS
#include "niobium/compiler.h"  // tag the host-built rubric plaintext for @hardware replay

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static std::string rubric_path() {
    if (const char* env = std::getenv("FUREVER_RUBRIC")) return env;
#ifdef RUBRIC_PATH
    return RUBRIC_PATH;
#else
    return "rubric.dat";
#endif
}

static std::vector<double> build_weight_vector() {
    std::vector<double> W(ACTIVE_SLOTS, 0.0);
    const std::string path = rubric_path();
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("apply_weights: cannot open rubric file: " + path);

    int pet = -1;
    int cat = 0;
    std::string line;
    while (std::getline(in, line)) {
        const size_t s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos) continue;   // blank
        if (line[s] == '#') continue;           // comment

        std::istringstream ls(line.substr(s));
        std::string first;
        ls >> first;
        if (first == "PET") {
            ++pet;
            cat = 0;
            continue;
        }

        // Category line: 12 weights + 1 offset (first value already in `first`).
        std::vector<double> nums;
        nums.push_back(std::stod(first));
        double v;
        while (ls >> v) nums.push_back(v);
        if (nums.size() != NUM_QUESTIONS + 1)
            throw std::runtime_error(
                "apply_weights: a category line in " + path +
                " has " + std::to_string(nums.size()) + " numbers, expected " +
                std::to_string(NUM_QUESTIONS + 1));
        if (pet < 0 || pet >= static_cast<int>(NUM_PETS) ||
            cat >= static_cast<int>(NUM_CATEGORIES))
            throw std::runtime_error("apply_weights: rubric shape mismatch in " + path);

        const int b = pet * static_cast<int>(NUM_CATEGORIES) + cat;
        for (uint32_t q = 0; q < NUM_QUESTIONS; ++q)
            W[b * BLOCK + q] = nums[q];
        W[b * BLOCK + NUM_QUESTIONS] = nums[NUM_QUESTIONS];   // offset -> bias slot
        ++cat;
    }
    if (pet + 1 != static_cast<int>(NUM_PETS))
        throw std::runtime_error(
            "apply_weights: found " + std::to_string(pet + 1) +
            " pets in " + path + ", expected " + std::to_string(NUM_PETS));
    return W;
}

Ciphertext<DCRTPoly> apply_weights(CryptoContext<DCRTPoly> cc,
                                   ConstCiphertext<DCRTPoly> ct) {
    static const std::vector<double> W = build_weight_vector();
    // Register the host-built rubric plaintext so its data lands in the FHETCH
    // trace. Without this, an @hardware replay reads it as uninitialized and the
    // whole cipher×plaintext multiply comes back zero. pause()/resume() keeps the
    // host-side encode out of the recorded op stream.
    niobium::compiler().pause();
    auto pt = cc->MakeCKKSPackedPlaintext(W);
    niobium::compiler().tag_input("rubric_weights", pt);
    niobium::compiler().resume();
    auto mct = std::const_pointer_cast<CiphertextImpl<DCRTPoly>>(ct);
    return cc->EvalMult(mct, pt);
}
