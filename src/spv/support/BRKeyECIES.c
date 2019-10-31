//
//  BRKeyECIES.c
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

#include "BRKeyECIES.h"
#include "BRCrypto.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ecies-aes128-sha256 as specified in SEC 1, 5.1: http://www.secg.org/SEC1-Ver-1.0.pdf
// NOTE: these are not implemented using constant time algorithms

size_t BRKeyECIESAES128SHA256Encrypt(BRKey *pubKey, void *out, size_t outLen, BRKey *ephemKey,
                                     const void *data, size_t dataLen)
{
    uint8_t *encKey, macKey[32], shared[32], iv[16], K[32], V[32], buf[36] = { 0, 0, 0, 1 };
    size_t pkLen = ephemKey ? BRKeyPubKey(ephemKey, NULL, 0) : 0;
    
    assert(pkLen > 0);
    if (! out) return pkLen + sizeof(iv) + dataLen + 32;
    if (outLen < pkLen + sizeof(iv) + dataLen + 32) return 0;

    assert(pubKey != NULL);
    size_t pubKeyLen = BRKeyPubKey(pubKey, NULL, 0);
    assert(pubKeyLen > 0);

    assert(data != NULL || dataLen == 0);

    // shared-secret = kdf(ecdh(ephemKey, pubKey))
    BRKeyECDH(ephemKey, &buf[4], pubKey);
    BRSHA256(shared, buf, sizeof(buf));
    mem_clean(buf, sizeof(buf));
    encKey = shared;
    BRSHA256(macKey, &shared[16], 16);
    
    // R = rG
    BRKeyPubKey(ephemKey, out, pkLen);

    // encrypt
    BRSHA256(buf, data, dataLen);
    BRHMACDRBG(iv, sizeof(iv), K, V, BRSHA256, 32, encKey, 16, buf, 32, NULL, 0); // generate iv
    memcpy(&out[pkLen], iv, sizeof(iv));
    BRAESCTR(&out[pkLen + sizeof(iv)], encKey, 16, iv, data, dataLen);
    mem_clean(shared, sizeof(shared));
    
    // tag with mac
    BRHMAC(&out[pkLen + sizeof(iv) + dataLen], BRSHA256, 32, macKey, 32, &out[pkLen], sizeof(iv) + dataLen);
    mem_clean(macKey, sizeof(macKey));
    return pkLen + sizeof(iv) + dataLen + 32;
}

size_t BRKeyECIESAES128SHA256Decrypt(BRKey *privKey, void *out, size_t outLen, const void *data, size_t dataLen)
{
    uint8_t *encKey, macKey[32], shared[32], mac[32], iv[16], buf[36] = { 0, 0, 0, 1 }, r = 0;
    size_t i, pkLen;
    BRKey pubKey;

    assert(data != NULL || dataLen == 0);
    pkLen = (dataLen > 0 && (((uint8_t *)data)[0] == 0x02 || ((uint8_t *)data)[0] == 0x03)) ? 33 : 65;
    if (dataLen < pkLen + sizeof(iv) + 32) return 0;
    if (BRKeySetPubKey(&pubKey, data, pkLen) == 0) return 0;
    if (! out) return dataLen - (pkLen + sizeof(iv) + 32);
    if (pkLen + sizeof(iv) + outLen + 32 < dataLen) return 0;

    assert(privKey != NULL);
    size_t priKeyLen = BRKeyPrivKey(privKey, NULL, 0);
    assert(priKeyLen  > 0);

    // shared-secret = kdf(ecdh(privKey, pubKey))
    BRKeyECDH(privKey, &buf[4], &pubKey);
    BRSHA256(shared, buf, sizeof(buf));
    mem_clean(buf, sizeof(buf));
    encKey = shared;
    BRSHA256(macKey, &shared[16], 16);
    
    // verify mac tag
    BRHMAC(mac, BRSHA256, 32, macKey, 32, &data[pkLen], dataLen - (pkLen + 32));
    mem_clean(macKey, sizeof(macKey));
    for (i = 0; i < 32; i++) r |= mac[i] ^ ((uint8_t *)data)[dataLen + i - 32]; // constant time compare
    mem_clean(mac, sizeof(mac));
    if (r != 0) return 0;
    
    // decrypt
    memcpy(iv, &data[pkLen], sizeof(iv));
    BRAESCTR(out, encKey, 16, iv, &data[pkLen + sizeof(iv)], dataLen - (pkLen + sizeof(iv) + 32));
    mem_clean(shared, sizeof(shared));
    return dataLen - (pkLen + sizeof(iv) + 32);
}

// Pigeon Encrypted Message Exchange

static void BRKeyPigeonSharedKey(BRKey *privKey, uint8_t *out32, BRKey *pubKey)
{
    uint8_t x[32];
    BRKeyECDH(privKey, x, pubKey);
    BRSHA256(out32, x, sizeof(x));
    mem_clean(x, sizeof(x));
}

void BRKeyPigeonPairingKey(BRKey *privKey, BRKey *pairingKey, const void *identifier, size_t identifierSize)
{
    uint8_t nonce[32], K[32], V[32];
    UInt256 secret;
    
    BRSHA256(nonce, identifier, identifierSize);
    BRHMACDRBG(&secret, sizeof(secret), K, V, BRSHA256, 32, privKey->secret.u8, 32, nonce, sizeof(nonce), NULL, 0);
    mem_clean(nonce, sizeof(nonce));
    mem_clean(K, sizeof(K));
    mem_clean(V, sizeof(V));
    BRKeySetSecret(pairingKey, &secret, 1);
}

size_t BRKeyPigeonEncrypt(BRKey *privKey, void *out, size_t outLen, BRKey *pubKey, const void *nonce12, const void *data, size_t dataLen)
{
    if (! out) return dataLen + 16;
    
    uint8_t sharedKey[32];
    BRKeyPigeonSharedKey(privKey, sharedKey, pubKey);
    size_t outSize = BRChacha20Poly1305AEADEncrypt(out, outLen, sharedKey, nonce12, data, dataLen, NULL, 0);
    mem_clean(sharedKey, sizeof(sharedKey));
    return outSize;
}

size_t BRKeyPigeonDecrypt(BRKey *privKey, void *out, size_t outLen, BRKey *pubKey, const void *nonce12, const void *data, size_t dataLen)
{
    if (! out) return (dataLen < 16) ? 0 : dataLen - 16;
    
    uint8_t sharedKey[32];
    BRKeyPigeonSharedKey(privKey, sharedKey, pubKey);
    size_t outSize = BRChacha20Poly1305AEADDecrypt(out, outLen, sharedKey, nonce12, data, dataLen, NULL, 0);
    mem_clean(sharedKey, sizeof(sharedKey));
    return outSize;
}
