#include <pos_kernel.h>
#include <amount.h>
#include <wallet/wallet.h>
#include <txdb.h>
#include <validation.h>
#include <arith_uint256.h>

namespace pos {
    const uint64_t COINSTAKE_AMOUNT = 1000 * COIN;

    uint256 CalcKernelHash(uint256 stakeModifier, int64_t coinstakeTime, const Consensus::Params& params) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << stakeModifier << coinstakeTime; // TODO: SS add masternode id
        return Hash(ss.begin(), ss.end());
    }

    CheckKernelHashRes
    CheckKernelHash(uint256 stakeModifier, uint32_t nBits, int64_t coinstakeTime, const Consensus::Params& params) {
        // Base target
        arith_uint256 targetProofOfStake;
        targetProofOfStake.SetCompact(nBits);

        const arith_uint256 hashProofOfStake = UintToArith256(
                CalcKernelHash(stakeModifier, coinstakeTime, params));

        // Now check if proof-of-stake hash meets target protocol
        if ((hashProofOfStake / (uint64_t) COINSTAKE_AMOUNT) > targetProofOfStake) {
            return {false, hashProofOfStake};
        }

        return {true, hashProofOfStake};
    }

    uint256 ComputeStakeModifier(uint256 prevStakeModifier, const uint256& id) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << prevStakeModifier << id;
        return Hash(ss.begin(), ss.end());
    }
}
