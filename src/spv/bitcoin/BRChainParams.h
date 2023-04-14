//
//  BRChainParams.h
//
//  Created by Aaron Voisine on 1/10/18.
//  Copyright (c) 2019 breadwallet LLC
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

#ifndef BRChainParams_h
#define BRChainParams_h

#include <assert.h>

#include <spv/bitcoin/BRMerkleBlock.h>
#include <spv/bitcoin/BRPeer.h>
#include <spv/support/BRSet.h>

typedef struct {
    uint32_t height;
    UInt256 hash;
    uint32_t timestamp;
    uint32_t target;
} BRCheckPoint;

typedef struct {
    const char * const *dnsSeeds; // NULL terminated array of dns seeds
    uint16_t standardPort;
    uint32_t magicNumber;
    uint64_t services;
    int (*verifyDifficulty)(const BRMerkleBlock *block, const BRSet *blockSet); // blockSet must have last 2016 blocks
    const BRCheckPoint *checkpoints;
    size_t checkpointsCount;
    // prefixes
    uint8_t privkey, base58_p2pkh, base58_p2sh;
    char const * const bip32_xprv;
    char const * const bip32_xpub;
    char const * const bech32;
} BRChainParams;

extern const BRChainParams *BRMainNetParams;
extern const BRChainParams *BRTestNetParams;
extern const BRChainParams *BRRegtestParams;

extern BRCheckPoint BRMainNetCheckpoints[31];
extern BRCheckPoint BRTestNetCheckpoints[18];

extern int spv_mainnet;

static inline const BRChainParams *BRGetChainParams () {
    switch(spv_mainnet) {
        case 0:
            return BRTestNetParams;
        case 2:
            return BRRegtestParams;
        default:
            return BRMainNetParams;
    }
}

#endif // BRChainParams_h
