#ifndef DEFI_POS_H
#define DEFI_POS_H

#include <consensus/params.h>
#include <arith_uint256.h>
#include <memory>
#include <key.h>

#include <boost/optional.hpp>

class CBlock;

class CBlockIndex;

class CBlockHeader;

class CChainParams;

class CCoinsViewCache;

class CMasternodesView;

namespace pos {

    bool CheckStakeModifier(const CBlockIndex* pindexPrev, const CBlockHeader& blockHeader);

/// Check PoS signatures (PoS block hashes are signed with privkey of  first coinstake out pubkey)
    bool CheckHeaderSignature(const CBlockHeader& block);

/// Check kernel hash target and coinstake signature
    bool ContextualCheckProofOfStake(const CBlockHeader& blockHeader, const Consensus::Params& params, CMasternodesView* mnView);

/// Check kernel hash target and coinstake signature. Check that block coinstakeTx matches header
    bool CheckProofOfStake(const CBlockHeader& blockHeader, const CBlockIndex* pindexPrev, const Consensus::Params& params, CMasternodesView* mnView);

    unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params::PoS& params);

    unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params::PoS& params);

    boost::optional<std::string> SignPosBlock(std::shared_ptr<CBlock> pblock, const CKey &key);

    boost::optional<std::string> CheckSignedBlock(const std::shared_ptr<CBlock>& pblock, const CBlockIndex* pindexPrev, const CChainParams& chainparams, CKeyID minter);
}

#endif // DEFI_POS_H
