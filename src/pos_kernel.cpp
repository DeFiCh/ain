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

    bool
    CheckKernelHash(const uint256& stakeModifier, uint32_t nBits, int64_t height, int64_t coinstakeTime, uint64_t blockHeight, const uint256& masternodeID, const Consensus::Params& params) {
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

            // Default to min age
            int64_t nTimeTx{params.pos.nStakeMinAge};

            if (node->mintedBlocks > 0)
            {
                LOCK(cs_main);

                const CBlockIndex* stakerBlock{nullptr};

                // Loop until we find last staked block or hit max stake age.
                for (const CBlockIndex* tip = ::ChainActive().Tip() ; tip && coinstakeTime - tip->GetBlockTime() < params.pos.nStakeMaxAge; tip = tip->pprev)
                {
                    CKeyID staker;
                    if (tip->GetBlockHeader().ExtractMinterKey(staker))
                    {
                        auto id = pcustomcsview->GetMasternodeIdByOperator(staker);
                        if (id && id == masternodeID)
                        {
                            // Found the last staked block
                            stakerBlock = tip;
                            break;
                        }
                    }
                }

                if (stakerBlock)
                {
                    // Choose whatever is bigger, time since last stake or min age.
                    nTimeTx = std::max(coinstakeTime - stakerBlock->GetBlockTime(), params.pos.nStakeMinAge);
                }
                else
                {
                    // Did not find staker block within max age so set to max age.
                    nTimeTx = params.pos.nStakeMaxAge;
                }
            }

            // Calculate coinDayWeight, at min this is 1 with no impact on difficulty.
            arith_uint256 coinDayWeight = nTimeTx / (24 * 60 * 60);

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
