#ifndef DEFI_CRYPTO_SHA3_H
#define DEFI_CRYPTO_SHA3_H

#include <vector>

bool sha3(const std::vector<unsigned char> &input, std::vector<unsigned char> &output);

#endif // DEFI_CRYPTO_SHA3_H