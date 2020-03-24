//
//  BRKeyECIES.h
//
//  Created by Aaron Voisine on 4/30/18.
//  Copyright (c) 2018 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#ifndef BRKeyECIES_h
#define BRKeyECIES_h

#include "BRKey.h"

#ifdef __cplusplus
extern "C" {
#endif
    
// ecies-aes128-sha256 as specified in SEC 1, 5.1: http://www.secg.org/SEC1-Ver-1.0.pdf
// NOTE: these are not implemented using constant time algorithms
    
size_t BRKeyECIESAES128SHA256Encrypt(BRKey *pubKey, void *out, size_t outLen, BRKey *ephemKey,
                                     const void *data, size_t dataLen);

size_t BRKeyECIESAES128SHA256Decrypt(BRKey *privKey, void *out, size_t outLen, const void *data, size_t dataLen);
    
    
// Generates a pairing key using HMAC_DRBG with the local private key as entropy and SHA256(identifier) as the nonce.
void BRKeyPigeonPairingKey(BRKey *privKey, BRKey *outPairingKey, const void *identifier, size_t identifierSize);

// chacha20-poly1305 authenticated encryption with associated data (AEAD): https://tools.ietf.org/html/rfc7539
// with shared key derived from privKey and pubKey using ECDH
// call with NULL out parameter to get the expected output size returned
size_t BRKeyPigeonEncrypt(BRKey *privKey, void *out, size_t outLen, BRKey *pubKey, const void *nonce12, const void *data, size_t dataLen);
size_t BRKeyPigeonDecrypt(BRKey *privKey, void *out, size_t outLen, BRKey *pubKey, const void *nonce12, const void *data, size_t dataLen);

    
#ifdef __cplusplus
}
#endif

#endif // BRKeyECIES_h
