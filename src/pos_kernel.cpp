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
        // Increase stake time by freezer multiplier
        int64_t freezerTime{coinstakeTime - stakersBlockTime};
        if (timelock) {
            // Timelock in weeks, 260 or 520, divide into 52 weeks to get 5 or 10 years.
            // Divide that by 10 to get half or whole extra freezer time added.
            // 5 years fives 1.5x bonus and 10 years gives 2x bonus.
            freezerTime += timelock / 52.0 / 10 * freezerTime;
        }

        // Calculate max age and limit to max allowed if above it.
        int64_t nTimeTx = std::min(freezerTime, params.pos.nStakeMaxAge);

        // Raise time to min age if below it.
        nTimeTx = std::max(nTimeTx, params.pos.nStakeMinAge);

        // Calculate coinDayWeight, at min this is 1 with no impact on difficulty.
        static const constexpr uint32_t period = 6 * 60 * 60; // 6 hours

        return (arith_uint256(nTimeTx) + period) / period;
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
