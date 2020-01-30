//
//  BRBIP32Sequence.c
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

#include "BRBIP32Sequence.h"
#include "bitcoin/BRChainParams.h"
#include "BRCrypto.h"
#include "BRBase58.h"
#include <string.h>
#include <assert.h>

#define BIP32_SEED_KEY "Bitcoin seed"

// BIP32 is a scheme for deriving chains of addresses from a seed value
// https://github.com/b_itcoin/bips/blob/master/bip-0032.mediawiki

// Private parent key -> private child key
//
// CKDpriv((kpar, cpar), i) -> (ki, ci) computes a child extended private key from the parent extended private key:
//
// - Check whether i >= 2^31 (whether the child is a hardened key).
//     - If so (hardened child): let I = HMAC-SHA512(Key = cpar, Data = 0x00 || ser256(kpar) || ser32(i)).
//       (Note: The 0x00 pads the private key to make it 33 bytes long.)
//     - If not (normal child): let I = HMAC-SHA512(Key = cpar, Data = serP(point(kpar)) || ser32(i)).
// - Split I into two 32-byte sequences, IL and IR.
// - The returned child key ki is parse256(IL) + kpar (mod n).
// - The returned chain code ci is IR.
// - In case parse256(IL) >= n or ki = 0, the resulting key is invalid, and one should proceed with the next value for i
//   (Note: this has probability lower than 1 in 2^127.)
//
static void _CKDpriv(UInt256 *k, UInt256 *c, uint32_t i)
{
    uint8_t buf[sizeof(BRECPoint) + sizeof(i)];
    UInt512 I;
    
    if (i & BIP32_HARD) {
        buf[0] = 0;
        UInt256Set(&buf[1], *k);
    }
    else BRSecp256k1PointGen((BRECPoint *)buf, k);
    
    UInt32SetBE(&buf[sizeof(BRECPoint)], i);
    
    BRHMAC(&I, BRSHA512, sizeof(UInt512), c, sizeof(*c), buf, sizeof(buf)); // I = HMAC-SHA512(c, k|P(k) || i)
    
    BRSecp256k1ModAdd(k, (UInt256 *)&I); // k = IL + k (mod n)
    *c = *(UInt256 *)&I.u8[sizeof(UInt256)]; // c = IR
    
    var_clean(&I);
    mem_clean(buf, sizeof(buf));
}

// Public parent key -> public child key
//
// CKDpub((Kpar, cpar), i) -> (Ki, ci) computes a child extended public key from the parent extended public key.
// It is only defined for non-hardened child keys.
//
// - Check whether i >= 2^31 (whether the child is a hardened key).
//     - If so (hardened child): return failure
//     - If not (normal child): let I = HMAC-SHA512(Key = cpar, Data = serP(Kpar) || ser32(i)).
// - Split I into two 32-byte sequences, IL and IR.
// - The returned child key Ki is point(parse256(IL)) + Kpar.
// - The returned chain code ci is IR.
// - In case parse256(IL) >= n or Ki is the point at infinity, the resulting key is invalid, and one should proceed with
//   the next value for i.
//
static void _CKDpub(BRECPoint *K, UInt256 *c, uint32_t i)
{
    uint8_t buf[sizeof(*K) + sizeof(i)];
    UInt512 I;

    if ((i & BIP32_HARD) != BIP32_HARD) { // can't derive private child key from public parent key
        *(BRECPoint *)buf = *K;
        UInt32SetBE(&buf[sizeof(*K)], i);
    
        BRHMAC(&I, BRSHA512, sizeof(UInt512), c, sizeof(*c), buf, sizeof(buf)); // I = HMAC-SHA512(c, P(K) || i)
        
        *c = *(UInt256 *)&I.u8[sizeof(UInt256)]; // c = IR
        BRSecp256k1PointAdd(K, (UInt256 *)&I); // K = P(IL) + K

        var_clean(&I);
        mem_clean(buf, sizeof(buf));
    }
}

// returns the master public key for the default BIP32 wallet layout - derivation path N(m/0H)
BRMasterPubKey BRBIP32MasterPubKey(const void *seed, size_t seedLen)
{
    BRMasterPubKey mpk = BR_MASTER_PUBKEY_NONE;
    UInt512 I;
    UInt256 secret, chain;
    BRKey key;

    assert(seed != NULL || seedLen == 0);
    
    if (seed || seedLen == 0) {
        BRHMAC(&I, BRSHA512, sizeof(UInt512), BIP32_SEED_KEY, strlen(BIP32_SEED_KEY), seed, seedLen);
        secret = *(UInt256 *)&I;
        chain = *(UInt256 *)&I.u8[sizeof(UInt256)];
        var_clean(&I);
    
        BRKeySetSecret(&key, &secret, 1);
        mpk.fingerPrint = BRKeyHash160(&key).u32[0];
        
        _CKDpriv(&secret, &chain, 0 | BIP32_HARD); // path m/0H
    
        mpk.chainCode = chain;
        BRKeySetSecret(&key, &secret, 1);
        var_clean(&secret, &chain);
        BRKeyPubKey(&key, &mpk.pubKey, sizeof(mpk.pubKey)); // path N(m/0H)
        BRKeyClean(&key);
    }
    
    return mpk;
}

// writes the public key for path N(m/0H/chain/index) to pubKey
// returns number of bytes written, or pubKeyLen needed if pubKey is NULL
size_t BRBIP32PubKey(uint8_t *pubKey, size_t pubKeyLen, BRMasterPubKey mpk, uint32_t chain, uint32_t index)
{
    UInt256 chainCode = mpk.chainCode;
    
    assert(memcmp(&mpk, &BR_MASTER_PUBKEY_NONE, sizeof(mpk)) != 0);
    
    if (pubKey && sizeof(BRECPoint) <= pubKeyLen) {
        *(BRECPoint *)pubKey = *(BRECPoint *)mpk.pubKey;

        _CKDpub((BRECPoint *)pubKey, &chainCode, chain); // path N(m/0H/chain)
        _CKDpub((BRECPoint *)pubKey, &chainCode, index); // index'th key in chain
        var_clean(&chainCode);
    }
    
    return (! pubKey || sizeof(BRECPoint) <= pubKeyLen) ? sizeof(BRECPoint) : 0;
}

// sets the private key for path m/0H/chain/index to key
void BRBIP32PrivKey(BRKey *key, const void *seed, size_t seedLen, uint32_t chain, uint32_t index)
{
    BRBIP32PrivKeyPath(key, seed, seedLen, 3, 0 | BIP32_HARD, chain, index);
}

// sets the private key for path m/0H/chain/index to each element in keys
void BRBIP32PrivKeyList(BRKey keys[], size_t keysCount, const void *seed, size_t seedLen, uint32_t chain,
                        const uint32_t indexes[])
{
    UInt512 I;
    UInt256 secret, chainCode, s, c;
    
    assert(keys != NULL || keysCount == 0);
    assert(seed != NULL || seedLen == 0);
    assert(indexes != NULL || keysCount == 0);
    
    if (keys && keysCount > 0 && (seed || seedLen == 0) && indexes) {
        BRHMAC(&I, BRSHA512, sizeof(UInt512), BIP32_SEED_KEY, strlen(BIP32_SEED_KEY), seed, seedLen);
        secret = *(UInt256 *)&I;
        chainCode = *(UInt256 *)&I.u8[sizeof(UInt256)];
        var_clean(&I);

        _CKDpriv(&secret, &chainCode, 0 | BIP32_HARD); // path m/0H
        _CKDpriv(&secret, &chainCode, chain); // path m/0H/chain
    
        for (size_t i = 0; i < keysCount; i++) {
            s = secret;
            c = chainCode;
            _CKDpriv(&s, &c, indexes[i]); // index'th key in chain
            BRKeySetSecret(&keys[i], &s, 1);
        }
        
        var_clean(&secret, &chainCode, &c, &s);
    }
}

// sets the private key for the specified path to key
// depth is the number of arguments used to specify the path
void BRBIP32PrivKeyPath(BRKey *key, const void *seed, size_t seedLen, int depth, ...)
{
    va_list ap;

    va_start(ap, depth);
    BRBIP32vPrivKeyPath(key, seed, seedLen, depth, ap);
    va_end(ap);
}

// sets the private key for the path specified by vlist to key
// depth is the number of arguments in vlist
void BRBIP32vPrivKeyPath(BRKey *key, const void *seed, size_t seedLen, int depth, va_list vlist)
{
    UInt512 I;
    UInt256 secret, chainCode;
    
    assert(key != NULL);
    assert(seed != NULL || seedLen == 0);
    assert(depth >= 0);
    
    if (key && (seed || seedLen == 0)) {
        BRHMAC(&I, BRSHA512, sizeof(UInt512), BIP32_SEED_KEY, strlen(BIP32_SEED_KEY), seed, seedLen);
        secret = *(UInt256 *)&I;
        chainCode = *(UInt256 *)&I.u8[sizeof(UInt256)];
        var_clean(&I);
     
        for (int i = 0; i < depth; i++) {
            _CKDpriv(&secret, &chainCode, va_arg(vlist, uint32_t));
        }
        
        BRKeySetSecret(key, &secret, 1);
        var_clean(&secret, &chainCode);
    }
}

// helper function for serializing BIP32 master public/private keys to standard export format
size_t _BRBIP32Serialize(char *str, size_t strLen, uint8_t depth, uint32_t fingerprint, uint32_t child, UInt256 chain,
                         const void *key, size_t keyLen)
{
    size_t len, off = 0;
    uint8_t data[4 + sizeof(depth) + sizeof(fingerprint) + sizeof(child) + sizeof(chain) + 1 + keyLen];
    
    memcpy(&data[off], (keyLen < 33 ? BRGetChainParams()->bip32_xprv : BRGetChainParams()->bip32_xpub), 4);
    off += 4;
    data[off] = depth;
    off += sizeof(depth);
    UInt32SetBE(&data[off], fingerprint);
    off += sizeof(fingerprint);
    UInt32SetBE(&data[off], child);
    off += sizeof(child);
    UInt256Set(&data[off], chain);
    off += sizeof(chain);
    if (keyLen < 33) data[off++] = 0;
    memcpy(&data[off], key, keyLen);
    off += keyLen;
    len = BRBase58CheckEncode(str, strLen, data, off);
    mem_clean(data, sizeof(data));
    return len;
}

// writes the base58check encoded serialized master private key (xprv) to str
// returns number of bytes written including NULL terminator, or strLen needed if str is NULL
size_t BRBIP32SerializeMasterPrivKey(char *str, size_t strLen, const void *seed, size_t seedLen)
{
    UInt512 I;
    size_t len;
    
    assert(seed != NULL);
    assert(seedLen > 0);
    
    BRHMAC(&I, BRSHA512, sizeof(I), BIP32_SEED_KEY, strlen(BIP32_SEED_KEY), seed, seedLen);
    len = _BRBIP32Serialize(str, strLen, 0, 0, 0, *(UInt256 *)&I.u8[sizeof(UInt256)], &I, sizeof(UInt256));
    var_clean(&I);
    return len;
}

// writes the base58check encoded serialized master public key (xpub) to str
// returns number of bytes written including NULL terminator, or strLen needed if str is NULL
size_t BRBIP32SerializeMasterPubKey(char *str, size_t strLen, BRMasterPubKey mpk)
{
    return _BRBIP32Serialize(str, strLen, 1, UInt32GetBE(&mpk.fingerPrint), 0 | BIP32_HARD, mpk.chainCode,
                             mpk.pubKey, sizeof(mpk.pubKey));
}

// returns a master public key give a base58check encoded serialized master public key (xpub)
BRMasterPubKey BRBIP32ParseMasterPubKey(const char *str)
{
    BRMasterPubKey mpk = BR_MASTER_PUBKEY_NONE;
    uint8_t data[4 + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(UInt256) + 33];
    size_t dataLen = BRBase58CheckDecode(data, sizeof(data), str);
    if (dataLen == sizeof(data) && memcmp(data, BRGetChainParams()->bip32_xpub, 4) == 0) {
        mpk.fingerPrint = ((union { uint8_t u8[4]; uint32_t u32; }){ data[5], data[6], data[7], data[8] }).u32;
        mpk.chainCode = UInt256Get(&data[13]);
        memcpy(mpk.pubKey, &data[45], sizeof(mpk.pubKey));
    }
    
    return mpk;
}

// key used for authenticated API calls, i.e. bitauth: https://github.com/bitpay/bitauth - path m/1H/0
void BRBIP32APIAuthKey(BRKey *key, const void *seed, size_t seedLen)
{
    BRBIP32PrivKeyPath(key, seed, seedLen, 2, 1 | BIP32_HARD, 0);
}

// key used for BitID: https://github.com/bitid/bitid/blob/master/BIP_draft.md
void BRBIP32BitIDKey(BRKey *key, const void *seed, size_t seedLen, uint32_t index, const char *uri)
{
    assert(key != NULL);
    assert(seed != NULL || seedLen == 0);
    assert(uri != NULL);
    
    if (key && (seed || seedLen == 0) && uri) {
        UInt256 hash;
        size_t uriLen = strlen(uri);
        uint8_t data[sizeof(index) + uriLen];

        UInt32SetLE(data, index);
        memcpy(&data[sizeof(index)], uri, uriLen);
        BRSHA256(&hash, data, sizeof(data));
        BRBIP32PrivKeyPath(key, seed, seedLen, 5, 13 | BIP32_HARD, UInt32GetLE(&hash.u32[0]) | BIP32_HARD,
                           UInt32GetLE(&hash.u32[1]) | BIP32_HARD, UInt32GetLE(&hash.u32[2]) | BIP32_HARD,
                           UInt32GetLE(&hash.u32[3]) | BIP32_HARD); // path m/13H/aH/bH/cH/dH
    }
}

