#pragma once

#include <consensus/params.h>
#include <arith_uint256.h>
#include <memory>
#include <masternodes/masternodes.h>

class CBlock;

class CBlockIndex;

class CBlockHeader;

class CCoinsViewCache;

namespace pos {

/// Check PoS signatures (PoS block hashes are signed with privkey of  first coinstake out pubkey)
    bool CheckHeaderSignature(const CBlockHeader& block);

/// Check kernel hash target and coinstake signature
    bool ContextualCheckProofOfStake(const CBlockHeader& blockHeader, const Consensus::Params& params, CMasternodesView* mnView);

/// Check kernel hash target and coinstake signature. Check that block coinstakeTx matches header
    bool CheckProofOfStake(const CBlockHeader& blockHeader, const CBlockIndex* pindexPrev, const Consensus::Params& params, CMasternodesView* mnView);

    unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params::PoS& params);

    unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params::PoS& params);
}

