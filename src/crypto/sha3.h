#ifndef DEFI_CRYPTO_SHA3_H
#define DEFI_CRYPTO_SHA3_H

#include <vector>

// SH3 256, but safer version, that always resizes the output to 256 bits.
// Otherwise, the internal sha3_256 call can fail. 
bool sha3_256_safe(const std::vector<unsigned char> &input, std::vector<unsigned char> &output);

#endif // DEFI_CRYPTO_SHA3_H