#include <pos.h>
#include <pos_kernel.h>
#include <pow.h>
#include <chain.h>
#include <validation.h>

#include <wallet/wallet.h>
#include <txdb.h>

static bool CheckStakeModifier(const CBlockIndex* pindexPrev, const CBlock& block, const Consensus::Params& params) {
    if (block.hashPrevBlock.IsNull())
        return block.stakeModifier.IsNull();

    if (block.IsProofOfStake())
        return block.stakeModifier == pos::ComputeStakeModifier_PoS(pindexPrev->stakeModifier,
                                                                    block.proofOfStakeBody->coinstakePrevout);
    return block.stakeModifier == pos::ComputeStakeModifier_PoW(pindexPrev->stakeModifier, block.hashPrevBlock);
}

bool CheckBlockProof_headerOnly(const CBlockHeader& block, const Consensus::Params& params) {
    return pos::CheckProofOfStake_headerOnly(block, params);
}

bool CheckBlockProof(const CBlockIndex* pindexPrev, const CBlock& block, CCoinsViewCache& view,
                     const Consensus::Params& params) {
    if (!CheckStakeModifier(pindexPrev, block, params)) {
        return false;
    }

    return pos::CheckProofOfStake(pindexPrev, block, view, params);

}

namespace pos {

/// Check PoS signatures (PoS block hashes are signed with coinstake out pubkey)
    bool CheckHeaderSignature(const CBlockHeader& block) {
        if (!block.IsProofOfStake()) {
            return true;
        }

        if (block.proofOfStakeBody->sig.empty()) {
            LogPrintf("CheckBlockSignature: Bad Block - PoS signature is empty\n");
            return false;
        }

        CPubKey recoveredPubKey{};
        if (!recoveredPubKey.RecoverCompact(block.GetHashToSign(), block.proofOfStakeBody->sig)) {
            LogPrintf("CheckBlockSignature: Bad Block - malformed signature\n");
            return false;
        }

        if (recoveredPubKey.GetID() != block.proofOfStakeBody->pubKeyHash) {
            LogPrintf("CheckBlockSignature: Bad Block - wrong signature\n");
            return false;
        }
        return true;
    }

    bool CheckProofOfStake_headerOnly(const CBlockHeader& block, const Consensus::Params& params) {
        if (!block.IsProofOfStake()) {
            return error("CheckProofOfStake_headerOnly(): called on non-PoS %s", block.GetHash().ToString());
        }

        const int64_t coinstakeTime = (int64_t) block.GetBlockTime();
        const auto& body = *block.proofOfStakeBody; // checked to exist after IsProofOfStake

        // checking PoS kernel is faster, so check it first
        if (!CheckKernelHash(block.stakeModifier, block.nBits, coinstakeTime, body.coinstakeAmount,
                             body.coinstakePrevout, params).hashOk) {
            return false;
        }
        return CheckHeaderSignature(block);
    }

    bool CheckProofOfStake(const CBlockIndex* pindexPrev, const CBlock& block, CCoinsViewCache& view,
                           const Consensus::Params& params) {
        if (block.IsProofOfStake() != block.HasCoinstakeTx()) {
            return false; // block claimed it's PoS, but doesn't have coinstakeTx
        }
        if (!block.IsCompleteProofOfStake()) {
            return error("CheckProofOfStake(): called on a non-PoS block %s", block.GetHash().ToString());
        }

        const auto& body = *block.proofOfStakeBody; // checked to exist after IsProofOfStake

        if (body.coinstakePrevout != block.vtx[1]->vin[0].prevout)
            return error("CheckProofOfStake(): block claimed PoS prevout doesn't match coinstakeTx (%s != %s)", body.coinstakePrevout.ToString(), block.vtx[1]->vin[0].prevout.ToString());

        // check staker's pubKeyHash
        {
            std::vector<CTxDestination> addressRet;
            txnouttype typeRet;
            int nRet;
            if (!ExtractDestinations(block.vtx[1]->vout[1].scriptPubKey, typeRet, addressRet, nRet))
                return error("CheckProofOfStake(): coinstakeTx scriptPubKey must be P2PKH");
            if (!(typeRet == txnouttype::TX_PUBKEYHASH && addressRet.size() == 1))
                return error("CheckProofOfStake(): coinstakeTx scriptPubKey must be P2PKH");

            CKeyID keyID(boost::get<PKHash>(addressRet[0]));
            if (keyID != body.pubKeyHash)
                return error("CheckProofOfStake(): coinstakeTx scriptPubKey and block pubKeyHash mismatch");
        }

        // check staker's coin
        {
            const Coin& stakeCoin = view.AccessCoin(body.coinstakePrevout);
            if (stakeCoin.IsSpent())
                return error("CheckProofOfStake : Could not find previous transaction for PoS %s\n",
                             body.coinstakePrevout.hash.ToString());
            if ((pindexPrev->nHeight - stakeCoin.nHeight) < params.pos.coinstakeMaturity)
                return error("CheckProofOfStake(): coinstakeTx input must have at least 100 confirmations");

            if (body.coinstakeAmount != stakeCoin.out.nValue)
                return error("CheckProofOfStake(): coinstakeTx amount and block coinstakeAmount mismatch");
            // it's vital for security that we use the same scriptPubKey
            if (block.vtx[1]->vout[1].scriptPubKey != stakeCoin.out.scriptPubKey)
                return error("CheckProofOfStake(): coinstakeTx scriptPubKey and prev. scriptPubKey mismatch");
        }

        const int64_t coinstakeTime = (int64_t) block.GetBlockTime();

        // checking PoS kernel is faster, so check it first
        if (!CheckKernelHash(block.stakeModifier, block.nBits, coinstakeTime, body.coinstakeAmount, body.coinstakePrevout,
                             params).hashOk) {
            return false;
        }
        return CheckHeaderSignature(block);
    }

    uint32_t GetNextTargetRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock,
                                   const Consensus::Params& params) {
        return ::GetNextWorkRequired(pindexLast, pblock, params);
    }

}
