#pragma once

#include <consensus/params.h>
#include <arith_uint256.h>

class CBlock;

class CBlockIndex;

class CBlockHeader;

class CCoinsViewCache;

namespace pos {

/// Check PoS signatures (PoS block hashes are signed with privkey of  first coinstake out pubkey)
    bool CheckHeaderSignature(const CBlockHeader& block);

/// Check kernel hash target and coinstake signature
    bool CheckProofOfStake_headerOnly(const CBlockHeader& block, const Consensus::Params& params);

/// Check kernel hash target and coinstake signature. Check that block coinstakeTx matches header
    bool CheckProofOfStake(const CBlock& block, const Consensus::Params& params);

    unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params::PoS& params);

    unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params::PoS& params);
}

