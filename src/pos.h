#ifndef DEFI_POS_H
#define DEFI_POS_H

#include <consensus/params.h>
#include <arith_uint256.h>
#include <memory>
#include <key.h>

class CBlock;

class CBlockIndex;

class CBlockHeader;

class CChainParams;

class CCoinsViewCache;

class CCustomCSView;

/// A state that's passed along between various 
/// Check functions like CheckBlocks, ContextualCheckProofOfStake,
/// CheckKernelHash, etc to maintain context across the
/// calls. This is currently mainly only used in the context of
/// subnet nodes. 
struct CheckContextState {
    uint8_t subNode = 0;
};

namespace pos {

    bool CheckStakeModifier(const CBlockIndex* pindexPrev, const CBlockHeader& blockHeader);

/// Check PoS signatures (PoS block hashes are signed with privkey of  first coinstake out pubkey)
    bool CheckHeaderSignature(const CBlockHeader& block);

/// Check kernel hash target and coinstake signature
    bool ContextualCheckProofOfStake(const CBlockHeader& blockHeader, const Consensus::Params& params, CCustomCSView* mnView, CheckContextState& ctxState, const int height);

/// Check kernel hash target and coinstake signature. Check that block coinstakeTx matches header
    bool CheckProofOfStake(const CBlockHeader& blockHeader, const CBlockIndex* pindexPrev, const Consensus::Params& params, CCustomCSView* mnView);

    unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, int64_t blockTime, const Consensus::Params& params);

    unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params::PoS& params, bool newDifficultyAdjust = false);

    std::optional<std::string> SignPosBlock(std::shared_ptr<CBlock> pblock, const CKey &key);

    std::optional<std::string> CheckSignedBlock(const std::shared_ptr<CBlock>& pblock, const CBlockIndex* pindexPrev, const CChainParams& chainparams);
}

#endif // DEFI_POS_H
