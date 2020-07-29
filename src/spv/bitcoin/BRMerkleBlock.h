//
//  BRMerkleBlock.h
//
//  Created by Aaron Voisine on 8/6/15.
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

#ifndef BRMerkleBlock_h
#define BRMerkleBlock_h

#include "BRLargeInt.h"
#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLOCK_DIFFICULTY_INTERVAL 2016 // number of blocks between difficulty target adjustments
#define BLOCK_UNKNOWN_HEIGHT      INT32_MAX
#define BLOCK_MAX_TIME_DRIFT      (2*60*60) // the furthest in the future a block is allowed to be timestamped

struct BRMerkleBlockStruct {
    UInt256 blockHash;
    uint32_t version;
    UInt256 prevBlock;
    UInt256 merkleRoot;
    uint32_t timestamp; // time interval since unix epoch
    uint32_t target;
    uint32_t nonce;
    uint32_t totalTx;
    UInt256 *hashes;
    size_t hashesCount;
    uint8_t *flags;
    size_t flagsLen;
    uint32_t height;
};
typedef struct BRMerkleBlockStruct BRMerkleBlock;

#define BR_MERKLE_BLOCK_NONE ((const BRMerkleBlock) { UINT256_ZERO, 0, UINT256_ZERO, UINT256_ZERO, 0, 0, 0, 0, NULL, 0,\
                                                      NULL, 0, 0 })

// returns a newly allocated merkle block struct that must be freed by calling BRMerkleBlockFree()
BRMerkleBlock *BRMerkleBlockNew(void);

// returns a deep copy of block and that must be freed by calling BRMerkleBlockFree()
BRMerkleBlock *BRMerkleBlockCopy(const BRMerkleBlock *block);

// buf must contain either a serialized merkleblock or header
// returns a merkle block struct that must be freed by calling BRMerkleBlockFree()
BRMerkleBlock *BRMerkleBlockParse(const uint8_t *buf, size_t bufLen);

// returns number of bytes written to buf, or total bufLen needed if buf is NULL (block->height is not serialized)
size_t BRMerkleBlockSerialize(const BRMerkleBlock *block, uint8_t *buf, size_t bufLen);

// populates txHashes with the matched tx hashes in the block
// returns number of tx hashes written, or the total hashesCount needed if txHashes is NULL
size_t BRMerkleBlockTxHashes(const BRMerkleBlock *block, UInt256 *txHashes, size_t hashesCount);

// sets the hashes and flags fields for a block created with BRMerkleBlockNew()
void BRMerkleBlockSetTxHashes(BRMerkleBlock *block, const UInt256 hashes[], size_t hashesCount,
                              const uint8_t *flags, size_t flagsLen);

// true if merkle tree and timestamp are valid, and proof-of-work matches the stated difficulty target
// NOTE: this only checks if the block difficulty matches the difficulty target in the header, it does not check if the
// target is correct for the block's height in the chain - use BRMerkleBlockVerifyDifficulty() for that
int BRMerkleBlockIsValid(const BRMerkleBlock *block, uint32_t currentTime);

// true if the given tx hash is known to be included in the block
int BRMerkleBlockContainsTxHash(const BRMerkleBlock *block, UInt256 txHash);

// verifies the block difficulty target is correct for the block's position in the chain
// transitionTime is the timestamp of the block at the previous difficulty transition
// transitionTime may be 0 if block->height is not a multiple of BLOCK_DIFFICULTY_INTERVAL
int BRMerkleBlockVerifyDifficulty(const BRMerkleBlock *block, const BRMerkleBlock *previous, uint32_t transitionTime);

// returns a hash value for block suitable for use in a hashtable
inline static size_t BRMerkleBlockHash(const void *block)
{
    return (size_t)((const BRMerkleBlock *)block)->blockHash.u32[0];
}

// true if block and otherBlock have equal blockHash values
inline static int BRMerkleBlockEq(const void *block, const void *otherBlock)
{
    return (block == otherBlock ||
            UInt256Eq(((const BRMerkleBlock *)block)->blockHash, ((const BRMerkleBlock *)otherBlock)->blockHash));
}

// frees memory allocated for block
void BRMerkleBlockFree(BRMerkleBlock *block);

#ifdef __cplusplus
}
#endif

#endif // BRMerkleBlock_h
