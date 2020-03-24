//
//  BRKey.h
//
//  Created by Aaron Voisine on 8/19/15.
//  Copyright (c) 2015 breadwallet LLC
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

#ifndef BRKey_h
#define BRKey_h

#include "BRInt.h"
#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BR_RAND_MAX          ((RAND_MAX > 0x7fffffff) ? 0x7fffffff : RAND_MAX)

// returns a random number less than upperBound (for non-cryptographic use only)
uint32_t BRRand(uint32_t upperBound);

    
typedef struct {
    uint8_t p[33];
} BRECPoint;

// adds 256bit big endian ints a and b (mod secp256k1 order) and stores the result in a
// returns true on success
int BRSecp256k1ModAdd(UInt256 *a, const UInt256 *b);

// multiplies 256bit big endian ints a and b (mod secp256k1 order) and stores the result in a
// returns true on success
int BRSecp256k1ModMul(UInt256 *a, const UInt256 *b);

// multiplies secp256k1 generator by 256bit big endian int i and stores the result in p
// returns true on success
int BRSecp256k1PointGen(BRECPoint *p, const UInt256 *i);

// multiplies secp256k1 generator by 256bit big endian int i and adds the result to ec-point p
// returns true on success
int BRSecp256k1PointAdd(BRECPoint *p, const UInt256 *i);

// multiplies secp256k1 ec-point p by 256bit big endian int i and stores the result in p
// returns true on success
int BRSecp256k1PointMul(BRECPoint *p, const UInt256 *i);

// returns true if privKey is a valid private key
// supported formats are wallet import format (WIF), mini private key format, or hex string
int BRPrivKeyIsValid(const char *privKey);

typedef struct {
    UInt256 secret;
    uint8_t pubKey[65];
    int compressed;
} BRKey;

// assigns secret to key and returns true on success
int BRKeySetSecret(BRKey *key, const UInt256 *secret, int compressed);

// assigns privKey to key and returns true on success
// privKey must be wallet import format (WIF), mini private key format, or hex string
int BRKeySetPrivKey(BRKey *key, const char *privKey);

// assigns DER encoded pubKey to key and returns true on success
int BRKeySetPubKey(BRKey *key, const uint8_t *pubKey, size_t pkLen);

// writes the WIF private key to privKey and returns the number of bytes writen, or pkLen needed if privKey is NULL
// returns 0 on failure
size_t BRKeyPrivKey(const BRKey *key, char *privKey, size_t pkLen);

// writes the DER encoded public key to pubKey and returns number of bytes written, or pkLen needed if pubKey is NULL
size_t BRKeyPubKey(BRKey *key, void *pubKey, size_t pkLen);

// compare public keys (generate public keys if needed) and return 1 on match or 0 otherwise
int BRKeyPubKeyMatch (BRKey *key1, BRKey *key2);

// returns the ripemd160 hash of the sha256 hash of the public key, or UINT160_ZERO on error
UInt160 BRKeyHash160(BRKey *key);

// writes the bech32 pay-to-witness-pubkey-hash bitcoin address for key to addr
// returns the number of bytes written, or addrLen needed if addr is NULL
size_t BRKeyAddress(BRKey *key, char *addr, size_t addrLen);

// writes the legacy pay-to-pubkey-hash address for key to addr
// returns the number of bytes written, or addrLen needed if addr is NULL
size_t BRKeyLegacyAddr(BRKey *key, char *addr, size_t addrLen);

// signs md with key and writes signature to sig
// returns the number of bytes written, or sigLen needed if sig is NULL
// returns 0 on failure
size_t BRKeySign(const BRKey *key, void *sig, size_t sigLen, UInt256 md);

// returns true if the signature for md is verified to have been made by key
int BRKeyVerify(BRKey *key, UInt256 md, const void *sig, size_t sigLen);

// wipes key material from key
void BRKeyClean(BRKey *key);

// Pieter Wuille's compact signature encoding used for bitcoin message signing
// to verify a compact signature, recover a public key from the signature and verify that it matches the signer's pubkey
size_t BRKeyCompactSign(const BRKey *key, void *compactSig, size_t sigLen, UInt256 md);

// assigns pubKey recovered from compactSig to key and returns true on success
int BRKeyRecoverPubKey(BRKey *key, UInt256 md, const void *compactSig, size_t sigLen);

// write a 'shared secret' for key w/ pubKey to out32
void BRKeyECDH(const BRKey *privKey, uint8_t *out32, BRKey *pubKey);

size_t BRKeyCompactSignEthereum(const BRKey *key, void *compactSig, size_t sigLen, UInt256 md);
int BRKeyRecoverPubKeyEthereum(BRKey *key, UInt256 md, const void *compactSig, size_t sigLen);


#ifdef __cplusplus
}
#endif

#endif // BRKey_h
