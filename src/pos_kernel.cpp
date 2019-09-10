#include <pos_kernel.h>
#include <wallet/wallet.h>
#include <txdb.h>
#include <validation.h>
#include <arith_uint256.h>

namespace pos {

    uint256 CalcKernelHash(uint256 stakeModifier, int64_t coinstakeTime, const COutPoint& prevout,
                           const Consensus::Params& params) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << stakeModifier << coinstakeTime << prevout.hash << prevout.n;
        return Hash(ss.begin(), ss.end());
    }

    CheckKernelHashRes
    CheckKernelHash(uint256 stakeModifier, uint32_t nBits, int64_t coinstakeTime, CAmount coinstakeAmount,
                    const COutPoint& prevout, const Consensus::Params& params) {
        if (prevout.IsNull() || coinstakeAmount <= 0) {
            return {false, arith_uint256{}};
        }

        // Base target
        arith_uint256 targetProofOfStake;
        targetProofOfStake.SetCompact(nBits);

        const arith_uint256 hashProofOfStake = UintToArith256(
                CalcKernelHash(stakeModifier, coinstakeTime, prevout, params));

        // Now check if proof-of-stake hash meets target protocol
        if ((hashProofOfStake / (uint64_t) coinstakeAmount) > targetProofOfStake) {
            return {false, hashProofOfStake};
        }

        return {true, hashProofOfStake};
    }

    uint256 ComputeStakeModifier_PoS(uint256 prevStakeModifier, const COutPoint& prevout) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << prevStakeModifier << prevout.hash << prevout.n;
        return Hash(ss.begin(), ss.end());
    }
}
