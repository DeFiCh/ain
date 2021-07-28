#ifndef DEFI_POS_KERNEL_H
#define DEFI_POS_KERNEL_H

#include <uint256.h>
#include <arith_uint256.h>
#include <consensus/params.h>
#include <streams.h>
#include <amount.h>
#include <pos.h>

class CWallet;
class COutPoint;
class CBlock;
class CTransaction;
class CCoinsViewCache;
class CKeyID;
class CCustomCSView;
class CMasternode;

namespace pos {

/// Calculate PoS kernel hash
    uint256 CalcKernelHash(const uint256& stakeModifier, int64_t height, int64_t coinstakeTime, const uint256& masternodeID);
    uint256 CalcKernelHashMulti(const uint256& stakeModifier, int64_t height, int64_t coinstakeTime, const uint256& masternodeID, const uint8_t subNode);

    // Calculate target multiplier
    arith_uint256 CalcCoinDayWeight(const Consensus::Params& params, const int64_t coinstakeTime, const int64_t stakersBlockTime);

/// Check whether stake kernel meets hash target
    bool CheckKernelHash(const uint256& stakeModifier, uint32_t nBits, int64_t creationHeight, int64_t coinstakeTime, uint64_t blockHeight,
                         const uint256& masternodeID, const Consensus::Params& params, const std::vector<int64_t> subNodesBlockTime, const uint16_t timelock, CheckContextState& ctxState);

/// Stake Modifier (hash modifier of proof-of-stake)
    uint256 ComputeStakeModifier(const uint256& prevStakeModifier, const CKeyID& key);
}

#endif // DEFI_POS_KERNEL_H
