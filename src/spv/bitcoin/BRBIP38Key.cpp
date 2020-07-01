//
//  BRBIP38Key.c
//
//  Created by Aaron Voisine on 9/7/15.
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

#include "BRBIP38Key.h"
#include "BRAddress.h"
#include "BRCrypto.h"
#include "BRBase58.h"
#include "BRInt.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define BIP38_NOEC_PREFIX      0x0142
#define BIP38_EC_PREFIX        0x0143
#define BIP38_NOEC_FLAG        (0x80 | 0x40)
#define BIP38_COMPRESSED_FLAG  0x20
#define BIP38_LOTSEQUENCE_FLAG 0x04
#define BIP38_INVALID_FLAG     (0x10 | 0x08 | 0x02 | 0x01)
#define BIP38_SCRYPT_N         16384
#define BIP38_SCRYPT_R         8
#define BIP38_SCRYPT_P         8
#define BIP38_SCRYPT_EC_N      1024
#define BIP38_SCRYPT_EC_R      1
#define BIP38_SCRYPT_EC_P      1

// BIP38 is a method for encrypting private keys with a passphrase
// https://github.com/bitcoin/bips/blob/master/bip-0038.mediawiki

static UInt256 _BRBIP38DerivePassfactor(uint8_t flag, const uint8_t *entropy, const char *passphrase)
{
    size_t len = strlen(passphrase);
    UInt256 prefactor, passfactor;
    
    BRScrypt(&prefactor, sizeof(prefactor), passphrase, len, entropy, (flag & BIP38_LOTSEQUENCE_FLAG) ? 4 : 8,
             BIP38_SCRYPT_N, BIP38_SCRYPT_R, BIP38_SCRYPT_P);
    
    if (flag & BIP38_LOTSEQUENCE_FLAG) { // passfactor = SHA256(SHA256(prefactor + entropy))
        uint8_t d[sizeof(prefactor) + sizeof(uint64_t)];

        memcpy(d, &prefactor, sizeof(prefactor));
        memcpy(&d[sizeof(prefactor)], entropy, sizeof(uint64_t));
        BRSHA256_2(&passfactor, d, sizeof(d));
        mem_clean(d, sizeof(d));
    }
    else passfactor = prefactor;
    
    var_clean(&len);
    var_clean(&prefactor);
    return passfactor;
}

static UInt512 _BRBIP38DeriveKey(BRECPoint passpoint, const uint8_t *addresshash, const uint8_t *entropy)
{
    UInt512 dk;
    uint8_t salt[sizeof(uint32_t) + sizeof(uint64_t)];
    
    memcpy(salt, addresshash, sizeof(uint32_t));
    memcpy(&salt[sizeof(uint32_t)], entropy, sizeof(uint64_t)); // salt = addresshash + entropy
    BRScrypt(&dk, sizeof(dk), &passpoint, sizeof(passpoint), salt, sizeof(salt), BIP38_SCRYPT_EC_N, BIP38_SCRYPT_EC_R,
             BIP38_SCRYPT_EC_P);
    mem_clean(salt, sizeof(salt));
    return dk;
}

int BRBIP38KeyIsValid(const char *bip38Key)
{
    uint8_t data[39];
    
    assert(bip38Key != NULL);
    
    if (BRBase58CheckDecode(data, sizeof(data), bip38Key) != 39) return 0; // invalid length
    
    uint16_t prefix = UInt16GetBE(data);
    uint8_t flag = data[2];
    
    if (prefix == BIP38_NOEC_PREFIX) { // non EC multiplied key
        return ((flag & BIP38_NOEC_FLAG) == BIP38_NOEC_FLAG && (flag & BIP38_LOTSEQUENCE_FLAG) == 0 &&
                (flag & BIP38_INVALID_FLAG) == 0);
    }
    else if (prefix == BIP38_EC_PREFIX) { // EC multiplied key
        return ((flag & BIP38_NOEC_FLAG) == 0 && (flag & BIP38_INVALID_FLAG) == 0);
    }
    else return 0; // invalid prefix
}

// decrypts a BIP38 key using the given passphrase and returns false if passphrase is incorrect
// passphrase must be unicode NFC normalized: http://www.unicode.org/reports/tr15/#Norm_Forms
int BRKeySetBIP38Key(BRKey *key, const char *bip38Key, const char *passphrase)
{
    int r = 1;
    uint8_t data[39];
    
    assert(key != NULL);
    assert(bip38Key != NULL);
    assert(passphrase != NULL);
    
    if (BRBase58CheckDecode(data, sizeof(data), bip38Key) != 39) return 0; // invalid length
    
    uint16_t prefix = UInt16GetBE(data);
    uint8_t flag = data[2];
    const uint8_t *addresshash = &data[3];
    size_t pwLen = strlen(passphrase);
    UInt512 derived;
    UInt256 secret, derived1, derived2, hash;
    BRAddress address = BR_ADDRESS_NONE;

    if (prefix == BIP38_NOEC_PREFIX) { // non EC multiplied key
        // data = prefix + flag + addresshash + encrypted1 + encrypted2
        UInt128 encrypted1 = UInt128Get(&data[7]), encrypted2 = UInt128Get(&data[23]);

        BRScrypt(&derived, sizeof(derived), passphrase, pwLen, addresshash, sizeof(uint32_t),
                 BIP38_SCRYPT_N, BIP38_SCRYPT_R, BIP38_SCRYPT_P);
        derived1 = *(UInt256 *)&derived, derived2 = *(UInt256 *)&derived.u8[sizeof(UInt256)];
        var_clean(&derived);
        
        BRAESECBDecrypt(&encrypted1, &derived2, sizeof(derived2));
        secret.u64[0] = encrypted1.u64[0] ^ derived1.u64[0];
        secret.u64[1] = encrypted1.u64[1] ^ derived1.u64[1];
        
        BRAESECBDecrypt(&encrypted2, &derived2, sizeof(derived2));
        secret.u64[2] = encrypted2.u64[0] ^ derived1.u64[2];
        secret.u64[3] = encrypted2.u64[1] ^ derived1.u64[3];
        var_clean(&derived1, &derived2);
        var_clean(&encrypted1, &encrypted2);
    }
    else if (prefix == BIP38_EC_PREFIX) { // EC multipled key
        // data = prefix + flag + addresshash + entropy + encrypted1[0...7] + encrypted2
        const uint8_t *entropy = &data[7];
        UInt128 encrypted1 = UINT128_ZERO, encrypted2 = UInt128Get(&data[23]);
        UInt256 passfactor = _BRBIP38DerivePassfactor(flag, entropy, passphrase), factorb;
        BRECPoint passpoint;
        uint64_t seedb[3];
        
        BRSecp256k1PointGen(&passpoint, &passfactor); // passpoint = G*passfactor
        derived = _BRBIP38DeriveKey(passpoint, addresshash, entropy);
        var_clean(&passpoint);
        derived1 = *(UInt256 *)&derived, derived2 = *(UInt256 *)&derived.u8[sizeof(UInt256)];
        var_clean(&derived);
        memcpy(&encrypted1, &data[15], sizeof(uint64_t));

        // encrypted2 = (encrypted1[8...15] + seedb[16...23]) xor derived1[16...31]
        BRAESECBDecrypt(&encrypted2, &derived2, sizeof(derived2));
        encrypted1.u64[1] = encrypted2.u64[0] ^ derived1.u64[2];
        seedb[2] = encrypted2.u64[1] ^ derived1.u64[3];

        // encrypted1 = seedb[0...15] xor derived1[0...15]
        BRAESECBDecrypt(&encrypted1, &derived2, sizeof(derived2));
        seedb[0] = encrypted1.u64[0] ^ derived1.u64[0];
        seedb[1] = encrypted1.u64[1] ^ derived1.u64[1];
        var_clean(&derived1, &derived2);
        var_clean(&encrypted1, &encrypted2);
        
        BRSHA256_2(&factorb, seedb, sizeof(seedb)); // factorb = SHA256(SHA256(seedb))
        mem_clean(seedb, sizeof(seedb));
        secret = passfactor;
        BRSecp256k1ModMul(&secret, &factorb); // secret = passfactor*factorb mod N
        var_clean(&passfactor, &factorb);
    }
    
    BRKeySetSecret(key, &secret, flag & BIP38_COMPRESSED_FLAG);
    var_clean(&secret);
    BRKeyLegacyAddr(key, address.s, sizeof(address));
    BRSHA256_2(&hash, address.s, strlen(address.s));
    if (! address.s[0] || memcmp(&hash, addresshash, sizeof(uint32_t)) != 0) r = 0;
    return r;
}

// generates an "intermediate code" for an EC multiply mode key
// salt should be 64bits of random data
// passphrase must be unicode NFC normalized
// returns number of bytes written to code including NULL terminator, or total codeLen needed if code is NULL
size_t BRKeyBIP38ItermediateCode(char *code, size_t codeLen, uint64_t salt, const char *passphrase)
{
    // TODO: XXX implement
    return 0;
}

// generates an "intermediate code" for an EC multiply mode key with a lot and sequence number
// lot must be less than 1048576, sequence must be less than 4096, and salt should be 32bits of random data
// passphrase must be unicode NFC normalized
// returns number of bytes written to code including NULL terminator, or total codeLen needed if code is NULL
size_t BRKeyBIP38ItermediateCodeLS(char *code, size_t codeLen, uint32_t lot, uint16_t sequence, uint32_t salt,
                                   const char *passphrase)
{
    // TODO: XXX implement
    return 0;
}

// generates a BIP38 key from an "intermediate code" and 24 bytes of cryptographically random data (seedb)
// compressed indicates if compressed pubKey format should be used for the bitcoin address
void BRKeySetBIP38ItermediateCode(BRKey *key, const char *code, const uint8_t *seedb, int compressed)
{
    // TODO: XXX implement
}

// encrypts key with passphrase
// passphrase must be unicode NFC normalized
// returns number of bytes written to bip38Key including NULL terminator or total bip38KeyLen needed if bip38Key is NULL
size_t BRKeyBIP38Key(BRKey *key, char *bip38Key, size_t bip38KeyLen, const char *passphrase)
{
    uint16_t prefix = BIP38_NOEC_PREFIX;
    uint8_t buf[39], flag = BIP38_NOEC_FLAG;
    uint32_t salt;
    size_t off = 0;
    BRAddress address;
    UInt512 derived;
    UInt256 hash, derived1, derived2;
    UInt128 encrypted1, encrypted2;
    
    if (! bip38Key) return 43*138/100 + 2; // 43bytes*log(256)/log(58), rounded up, plus NULL terminator

    assert(key != NULL);
    size_t priKeyLen = BRKeyPrivKey(key, NULL, 0);
    assert(priKeyLen  > 0);

    assert(passphrase != NULL);
   
    if (key->compressed) flag |= BIP38_COMPRESSED_FLAG;
    BRKeyLegacyAddr(key, address.s, sizeof(address));
    BRSHA256_2(&hash, address.s, strlen(address.s));
    salt = hash.u32[0];

    BRScrypt(&derived, sizeof(derived), passphrase, strlen(passphrase), &salt, sizeof(salt),
             BIP38_SCRYPT_N, BIP38_SCRYPT_R, BIP38_SCRYPT_P);
    derived1 = *(UInt256 *)&derived, derived2 = *(UInt256 *)&derived.u8[sizeof(UInt256)];
    var_clean(&derived);
    
    // enctryped1 = AES256Encrypt(privkey[0...15] xor derived1[0...15], derived2)
    encrypted1.u64[0] = key->secret.u64[0] ^ derived1.u64[0];
    encrypted1.u64[1] = key->secret.u64[1] ^ derived1.u64[1];
    BRAESECBEncrypt(&encrypted1, &derived2, sizeof(derived2));

    // encrypted2 = AES256Encrypt(privkey[16...31] xor derived1[16...31], derived2)
    encrypted2.u64[0] = key->secret.u64[2] ^ derived1.u64[2];
    encrypted2.u64[1] = key->secret.u64[3] ^ derived1.u64[3];
    BRAESECBEncrypt(&encrypted2, &derived2, sizeof(derived2));
    
    UInt16SetBE(&buf[off], prefix);
    off += sizeof(prefix);
    buf[off] = flag;
    off += sizeof(flag);
    UInt32SetBE(&buf[off], UInt32GetBE(&salt));
    off += sizeof(salt);
    UInt128Set(&buf[off], encrypted1);
    off += sizeof(encrypted1);
    UInt128Set(&buf[off], encrypted2);
    off += sizeof(encrypted2);
    return BRBase58CheckEncode(bip38Key, bip38KeyLen, buf, off);
}
