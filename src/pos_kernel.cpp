#include <pos_kernel.h>
#include <amount.h>
#include <arith_uint256.h>
#include <key.h>
#include <validation.h>

#include <masternodes/masternodes.h>

extern CAmount GetMnCollateralAmount(int); // from masternodes.h

namespace pos {
    uint256 CalcKernelHash(const uint256& stakeModifier, int64_t height, int64_t coinstakeTime, const uint256& masternodeID, const Consensus::Params& params) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << stakeModifier << coinstakeTime << GetMnCollateralAmount(int(height)) << masternodeID;
        return Hash(ss.begin(), ss.end());
    }

    arith_uint256 CalcCoinDayWeight(const Consensus::Params& params, const CMasternode& node, const int64_t coinstakeTime, const int64_t stakersBlockTime)
    {
        // Default to min age
        int64_t nTimeTx{params.pos.nStakeMinAge};

        // If staker has provided a previous block time use that to avoid DB lookup.
        if (stakersBlockTime)
        {
            nTimeTx = std::min(coinstakeTime - stakersBlockTime, params.pos.nStakeMaxAge);
        }
        else
        {
            // Lookup stored valid blocktime. no optional value indicate no previous staked block.
            if (auto lastBlockTime = pcustomcsview->GetMasternodeLastBlockTime(node.operatorAuthAddress))
            {
                // Choose whatever is smaller, time since last stake or max age.
                nTimeTx = std::min(coinstakeTime - *lastBlockTime, params.pos.nStakeMaxAge);
            }
            else // No record. No stake blocks or post-fork createmastnode TX, use fork time.
            {
                if (auto block = ::ChainActive()[Params().GetConsensus().DakotaCrescentHeight])
                {
                    nTimeTx = std::min(coinstakeTime - block->GetBlockTime(), params.pos.nStakeMaxAge);
                }
            }
        }

        // Raise time to min age if below it.
        nTimeTx = std::max(nTimeTx, params.pos.nStakeMinAge);

        // Calculate coinDayWeight, at min this is 1 with no impact on difficulty.
        arith_uint256 period = 6 * 60 * 60; // 6 hours

        return (nTimeTx + period) / period;
    }

    bool
    CheckKernelHash(const uint256& stakeModifier, uint32_t nBits, int64_t height, int64_t coinstakeTime, uint64_t blockHeight, const uint256& masternodeID, const Consensus::Params& params, const int64_t stakersBlockTime) {
        // Base target
        arith_uint256 targetProofOfStake;
        targetProofOfStake.SetCompact(nBits);

        const auto hashProofOfStake = UintToArith256(CalcKernelHash(stakeModifier, height, coinstakeTime, masternodeID, params));

        // New difficulty calculation to make staking easier the longer it has
        // been since a masternode staked a block.
        if (blockHeight >= static_cast<uint64_t>(Params().GetConsensus().DakotaCrescentHeight))
        {
            auto node = pcustomcsview->GetMasternode(masternodeID);
            if (!node)
            {
                return false;
            }

            arith_uint256 coinDayWeight = CalcCoinDayWeight(params, *node, coinstakeTime, stakersBlockTime);

            // Increase target by coinDayWeight.
            return (hashProofOfStake / static_cast<uint64_t>( GetMnCollateralAmount( static_cast<int>(height) ) ) ) <= targetProofOfStake * coinDayWeight;
        }

        // Now check if proof-of-stake hash meets target protocol
        return (hashProofOfStake / static_cast<uint64_t>( GetMnCollateralAmount( static_cast<int>(height) ) ) ) <= targetProofOfStake;
    }

    uint256 ComputeStakeModifier(const uint256& prevStakeModifier, const CKeyID& key) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << prevStakeModifier << key;
        return Hash(ss.begin(), ss.end());
    }
}
