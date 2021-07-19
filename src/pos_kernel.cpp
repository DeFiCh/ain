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

    arith_uint256 CalcCoinDayWeight(const Consensus::Params& params, const int64_t coinstakeTime, const uint16_t timelock, const int64_t stakersBlockTime)
    {
        // Calculate max age and limit to max allowed if above it.
        int64_t nTimeTx = std::min(coinstakeTime - stakersBlockTime, params.pos.nStakeMaxAge);

        // Raise time to min age if below it.
        nTimeTx = std::max(nTimeTx, params.pos.nStakeMinAge);

        // Calculate coinDayWeight, at min this is 1 with no impact on difficulty.
        static const constexpr uint64_t maxCoinAge{57};
        static const constexpr uint32_t period{6 * 60 * 60}; // 6 hours
        static const constexpr uint32_t fiveYearPeriod{135 * 60}; // 2 hours 15 minutes
        static const constexpr uint32_t tenYearPeriod{75 * 60}; // 1 hour 15 minutes

        uint64_t weight;
        if (timelock == CMasternode::FIVEYEAR) {
            weight = (nTimeTx + fiveYearPeriod) / fiveYearPeriod;
        } else if (timelock == CMasternode::TENYEAR) {
            weight = (nTimeTx + tenYearPeriod) / tenYearPeriod;
        } else {
            weight = (nTimeTx + period) / period;
        }

        weight = std::min(weight, maxCoinAge);

        return {weight};
    }

    bool
    CheckKernelHash(const uint256& stakeModifier, uint32_t nBits, int64_t creationHeight, int64_t coinstakeTime, uint64_t blockHeight,
                    const uint256& masternodeID, const Consensus::Params& params, const int64_t stakersBlockTime, const uint16_t timelock) {
        // Base target
        arith_uint256 targetProofOfStake;
        targetProofOfStake.SetCompact(nBits);

        const auto hashProofOfStake = UintToArith256(CalcKernelHash(stakeModifier, creationHeight, coinstakeTime, masternodeID, params));

        // New difficulty calculation to make staking easier the longer it has
        // been since a masternode staked a block.
        if (blockHeight >= static_cast<uint64_t>(Params().GetConsensus().DakotaCrescentHeight))
        {
            auto coinDayWeight = CalcCoinDayWeight(params, coinstakeTime, timelock, stakersBlockTime);

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
