#include <pos.h>
#include <pos_kernel.h>
#include <chain.h>
#include <validation.h>

#include <wallet/wallet.h>
#include <txdb.h>

namespace pos {
static bool CheckStakeModifier(const CBlockIndex* pindexPrev, const CBlock& block) {
    if (block.hashPrevBlock.IsNull())
        return block.stakeModifier.IsNull();

    uint256 id = block.ExtractMasternodeID();
    return block.stakeModifier == pos::ComputeStakeModifier(pindexPrev->stakeModifier, id);
}

/// Check PoS signatures (PoS block hashes are signed with coinstake out pubkey)
bool CheckHeaderSignature(const CBlockHeader& block) {
    if (block.sig.empty()) {
        LogPrintf("CheckBlockSignature: Bad Block - PoS signature is empty\n");
        return false;
    }

    CPubKey recoveredPubKey{};
    if (!recoveredPubKey.RecoverCompact(block.GetHashToSign(), block.sig)) {
        LogPrintf("CheckBlockSignature: Bad Block - malformed signature\n");
        return false;
    }

    return true;
}

bool CheckProofOfStake_headerOnly(const CBlockHeader& block, const Consensus::Params& params) {
    const int64_t coinstakeTime = (int64_t) block.GetBlockTime();

    // checking PoS kernel is faster, so check it first
    if (!CheckKernelHash(block.stakeModifier, block.nBits, coinstakeTime, params).hashOk) {
        return false;
    }
    return CheckHeaderSignature(block);
}

bool CheckProofOfStake(const CBlock& block, const Consensus::Params& params) {
    if (!block.HasCoinstakeTx()) {
        return error("CheckProofOfStake(): called on a non-PoS block %s", block.GetHash().ToString());
    }

    {
        std::vector<CTxDestination> addressRet;
        txnouttype typeRet;
        int nRet;
        if (!ExtractDestinations(block.vtx[1]->vout[1].scriptPubKey, typeRet, addressRet, nRet))
            return error("CheckProofOfStake(): coinstakeTx scriptPubKey must be P2PKH");
        if (!(typeRet == txnouttype::TX_PUBKEYHASH && addressRet.size() == 1))
            return error("CheckProofOfStake(): coinstakeTx scriptPubKey must be P2PKH");

        // TODO: SS check address masternode operator
    }

    const int64_t coinstakeTime = (int64_t) block.GetBlockTime();

    // checking PoS kernel is faster, so check it first
    if (!CheckKernelHash(block.stakeModifier, block.nBits, coinstakeTime, params).hashOk) {
        return false;
    }
    return CheckHeaderSignature(block);
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

bool CheckBlockProof(const CBlockIndex* pindexPrev, const CBlock& block, const Consensus::Params& params) {
    if (!pos::CheckStakeModifier(pindexPrev, block)) {
        return false;
    }

    return pos::CheckProofOfStake(block, params);

}
