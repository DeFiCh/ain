#include <pos.h>
#include <pos_kernel.h>
#include <chain.h>
#include <validation.h>
#include <key.h>
#include <wallet/wallet.h>
#include <txdb.h>

namespace pos {
static bool CheckStakeModifier(const CBlockIndex* pindexPrev, const CBlockHeader& blockHeader) {
    if (blockHeader.hashPrevBlock.IsNull())
        return blockHeader.stakeModifier.IsNull();

    /// @todo @maxb is it possible to pass minter key here, or we really need to extract it srom sig???
    CKeyID key;
    if (!blockHeader.ExtractMinterKey(key)) {
        LogPrintf("CheckStakeModifier: Can't extract minter key\n");
        return false;
    }

    return blockHeader.stakeModifier == pos::ComputeStakeModifier(pindexPrev->stakeModifier, key);
}

/// Check PoS signatures (PoS block hashes are signed with coinstake out pubkey)
bool CheckHeaderSignature(const CBlockHeader& blockHeader) {
    if (blockHeader.sig.empty()) {
        if (blockHeader.height == 0) {
            return true;
        }
        LogPrintf("CheckBlockSignature: Bad Block - PoS signature is empty\n");
        return false;
    }

    CPubKey recoveredPubKey{};
    if (!recoveredPubKey.RecoverCompact(blockHeader.GetHashToSign(), blockHeader.sig)) {
        LogPrintf("CheckBlockSignature: Bad Block - malformed signature\n");
        return false;
    }

    return true;
}

bool ContextualCheckProofOfStake(const CBlockHeader& blockHeader, const Consensus::Params& params, CMasternodesView* mnView) {

    /// @todo @maxb may be this is tooooo optimistic? need more validation?
    if (blockHeader.height == 0 && blockHeader.GetHash() == params.hashGenesisBlock) {
        return true;
    }

    // TODO: (SS) check address masternode operator in mnView
    /// @todo @maxb check 'mintedBlocks' counter for this minter at this block height!!!
    CKeyID minter;
    if (!blockHeader.ExtractMinterKey(minter)) {
        return false;
    }
    uint256 masternodeID;
    {
        // check that block minter exists and active at the height of the block
        AssertLockHeld(cs_main); /// @todo @maxb or lock it
        auto it = mnView->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, minter);
        if (!it || !mnView->ExistMasternode((*it)->second)->IsActive(blockHeader.height))
        {
            return false;
        }
        masternodeID = (*it)->second;
    }
    // checking PoS kernel is faster, so check it first
    if (!CheckKernelHash(blockHeader.stakeModifier, blockHeader.nBits, (int64_t) blockHeader.GetBlockTime(), params, masternodeID).hashOk) {
        return false;
    }

    /// @todo @maxb this is just an example how to get 'mintedBlocks' for given minter from the mn history.
    /// I can't find in te code any checks of this counter for headers, so I leave it here.
    /// But this is EXTREMELY UNOPTIMAL! Need to be moved to the upper levels to check headers in batches!!!
    {
        AssertLockHeld(cs_main); /// @todo @maxb or lock it
        CMasternodesViewHistory history(mnView);
        // minter exists and active at the height of the block - it was checked before
        if (history.GetState(blockHeader.height-1).ExistMasternode(masternodeID)->mintedBlocks + 1 != blockHeader.mintedBlocks)
        {
            return false;
        }
    }

    return CheckHeaderSignature(blockHeader);
}

bool CheckProofOfStake(const CBlockHeader& blockHeader, const CBlockIndex* pindexPrev, const Consensus::Params& params, CMasternodesView* mnView) {
    if (!pos::CheckStakeModifier(pindexPrev, blockHeader)) {
        return false;
    }

    /// @todo @max this is our own check of own minted block (just to remember)
    return ContextualCheckProofOfStake(blockHeader, params, mnView);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params::PoS& params)
{
    if (params.fNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nTargetTimespan/4)
        nActualTimespan = params.nTargetTimespan/4;
    if (nActualTimespan > params.nTargetTimespan*4)
        nActualTimespan = params.nTargetTimespan*4;

    // Retarget
    const arith_uint256 bnDiffLimit = UintToArith256(params.diffLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nTargetTimespan;

    if (bnNew > bnDiffLimit)
        bnNew = bnDiffLimit;

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params::PoS& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.diffLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return pos::CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

}
