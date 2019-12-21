#pragma once

#include <consensus/params.h>
#include <arith_uint256.h>
#include <memory>

class CBlock;

class CBlockIndex;

class CBlockHeader;

class CCoinsViewCache;

class CMasternodesView {}; // TODO: (SS) Change to real CMasternodesView class
extern std::unique_ptr<CMasternodesView> pmasternodesview; // TODO: (SS) Change to real

namespace pos {

/// Check PoS signatures (PoS block hashes are signed with privkey of  first coinstake out pubkey)
    bool CheckHeaderSignature(const CBlockHeader& block);

/// Check kernel hash target and coinstake signature
    bool CheckProofOfStake_headerOnly(const CBlockHeader& blockHeader, const Consensus::Params& params, CMasternodesView* mnView);

/// Check kernel hash target and coinstake signature. Check that block coinstakeTx matches header
    bool CheckProofOfStake(const CBlockHeader& blockHeader, const CBlockIndex* pindexPrev, const Consensus::Params& params, CMasternodesView* mnView);

    unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params::PoS& params);

    unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params::PoS& params);
}

