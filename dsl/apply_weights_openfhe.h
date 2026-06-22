// apply_weights_openfhe.h — library header for the pet-adoption scoring rubric.
// Required by extern_call("apply_weights", ...) in server.niob.
#ifndef FUREVER_APPLY_WEIGHTS_OPENFHE_H_
#define FUREVER_APPLY_WEIGHTS_OPENFHE_H_

#include "openfhe.h"

using namespace lbcrypto;

// Multiply the packed answer ciphertext element-wise by the 256-slot plaintext
// weight+offset vector built from rubric.dat. Returns a ciphertext whose
// slot i holds answers[i] * W[i].
ConstCiphertext<DCRTPoly> apply_weights(CryptoContext<DCRTPoly> cc,
                                        ConstCiphertext<DCRTPoly> ct);

#endif  // FUREVER_APPLY_WEIGHTS_OPENFHE_H_
