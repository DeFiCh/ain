#include <pos_kernel.h>
#include <amount.h>
#include <arith_uint256.h>
#include <key.h>
#include <validation.h>

#include <masternodes/masternodes.h>

extern CAmount GetMnCollateralAmount(int); // from masternodes.h

namespace pos {
    uint256 CalcKernelHash(const uint256& stakeModifier, int64_t height, int64_t coinstakeTime, const uint256& masternodeID) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << stakeModifier << coinstakeTime << GetMnCollateralAmount(int(height)) << masternodeID;
        return Hash(ss.begin(), ss.end());
    }

    uint256 CalcKernelHashMulti(const uint256& stakeModifier, int64_t height, int64_t coinstakeTime, const uint256& masternodeID, const uint8_t subNode) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << stakeModifier << coinstakeTime << GetMnCollateralAmount(int(height)) << masternodeID << subNode;
        return Hash(ss.begin(), ss.end());
    }

    arith_uint256 CalcCoinDayWeight(const Consensus::Params& params, const int64_t coinstakeTime, const int64_t stakersBlockTime)
    {
        // Calculate max age and limit to max allowed if above it.
        int64_t nTimeTx = std::min(coinstakeTime - stakersBlockTime, params.pos.nStakeMaxAge);

        // Raise time to min age if below it.
        nTimeTx = std::max(nTimeTx, params.pos.nStakeMinAge);

        // Calculate coinDayWeight, at min this is 1 with no impact on difficulty.
        static const constexpr uint32_t period = 6 * 60 * 60; // 6 hours

        return (arith_uint256(nTimeTx) + period) / period;
    }

    bool CheckKernelHash(const uint256& stakeModifier, uint32_t nBits, int64_t creationHeight, int64_t coinstakeTime, uint64_t blockHeight,
                    const uint256& masternodeID, const Consensus::Params& params, const std::vector<int64_t> subNodesBlockTime, const uint16_t timelock, CheckContextState& ctxState) {
        // Base target
        arith_uint256 targetProofOfStake;
        targetProofOfStake.SetCompact(nBits);

        if (blockHeight >= static_cast<uint64_t>(params.EunosPayaHeight)) {
            const uint8_t loops = timelock == CMasternode::TENYEAR ? 4 : timelock == CMasternode::FIVEYEAR ? 3 : 2;

            // Check whether we meet hash for each subnode in turn
            for (uint8_t i{0}; i < loops; ++i) {
                const auto hashProofOfStake = UintToArith256(CalcKernelHashMulti(stakeModifier, creationHeight, coinstakeTime, masternodeID, i));

                auto coinDayWeight = CalcCoinDayWeight(params, coinstakeTime, subNodesBlockTime[i]);

                // Increase target by coinDayWeight.
                if ((hashProofOfStake / static_cast<uint64_t>(GetMnCollateralAmount(static_cast<int>(creationHeight)))) <= targetProofOfStake * coinDayWeight) {
                    ctxState.subNode = i;
                    return true;
                }
            }

            return false;
        }

        const auto hashProofOfStake = UintToArith256(CalcKernelHash(stakeModifier, creationHeight, coinstakeTime, masternodeID));

        // New difficulty calculation to make staking easier the longer it has
        // been since a masternode staked a block.
        if (blockHeight >= static_cast<uint64_t>(params.DakotaCrescentHeight))
        {
            auto coinDayWeight = CalcCoinDayWeight(params, coinstakeTime, subNodesBlockTime[0]);

            // Increase target by coinDayWeight.
            return (hashProofOfStake / static_cast<uint64_t>( GetMnCollateralAmount( static_cast<int>(creationHeight) ) ) ) <= targetProofOfStake * coinDayWeight;
        }

        // Now check if proof-of-stake hash meets target protocol
        return (hashProofOfStake / static_cast<uint64_t>( GetMnCollateralAmount( static_cast<int>(creationHeight) ) ) ) <= targetProofOfStake;
    }

    uint256 ComputeStakeModifier(const uint256& prevStakeModifier, const CKeyID& key) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << prevStakeModifier << key;
        return Hash(ss.begin(), ss.end());
    }
}
