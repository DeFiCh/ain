#pragma once

#include <uint256.h>
#include <arith_uint256.h>
#include <consensus/params.h>
#include <streams.h>
#include <amount.h>

class CWallet;

class COutPoint;

class CBlock;

class CTransaction;

class CCoinsViewCache;

class CKeyID;

class CMasternodesView;

namespace pos {

    struct CheckKernelHashRes {
        bool hashOk;
        arith_uint256 hashProofOfStake;
    };

/// Calculate PoS kernel hash
    uint256
    CalcKernelHash(uint256 stakeModifier, int64_t coinstakeTime, uint256 masternodeID, const Consensus::Params& params);

/// Check whether stake kernel meets hash target
/// Sets hashProofOfStake, hashOk is true of the kernel meets hash target
    CheckKernelHashRes
    CheckKernelHash(uint256 stakeModifier, uint32_t nBits, int64_t coinstakeTime, const Consensus::Params& params, uint256 masternodeID);

/// Stake Modifier (hash modifier of proof-of-stake)
    uint256 ComputeStakeModifier(uint256 prevStakeModifier, const CKeyID& key);
}
