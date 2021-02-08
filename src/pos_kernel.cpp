#include <pos_kernel.h>
#include <amount.h>
#include <arith_uint256.h>
#include <key.h>

extern CAmount GetMnCollateralAmount(int); // from masternodes.h

namespace pos {
    uint256 CalcKernelHash(const uint256& stakeModifier, int64_t height, int64_t coinstakeTime, const uint256& masternodeID, const Consensus::Params& params) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << stakeModifier << coinstakeTime << GetMnCollateralAmount(int(height)) << masternodeID;
        return Hash(ss.begin(), ss.end());
    }

    CheckKernelHashRes
    CheckKernelHash(const uint256& stakeModifier, uint32_t nBits, int64_t height, int64_t coinstakeTime, const uint256& masternodeID, const Consensus::Params& params) {
        // Base target
        arith_uint256 targetProofOfStake;
        targetProofOfStake.SetCompact(nBits);

        const auto hashProofOfStake = UintToArith256(CalcKernelHash(stakeModifier, height, coinstakeTime, masternodeID, params));

        // Now check if proof-of-stake hash meets target protocol
        if ((hashProofOfStake / (uint64_t) GetMnCollateralAmount(int(height))) > targetProofOfStake) {
            return {false, hashProofOfStake};
        }

        return {true, hashProofOfStake};
    }

    uint256 ComputeStakeModifier(const uint256& prevStakeModifier, const CKeyID& key) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << prevStakeModifier << key;
        return Hash(ss.begin(), ss.end());
    }
}
