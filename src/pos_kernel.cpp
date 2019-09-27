#include <pos_kernel.h>
#include <amount.h>
#include <wallet/wallet.h>
#include <txdb.h>
#include <validation.h>
#include <arith_uint256.h>
#include <key.h>
#include <pos.h>

namespace pos {
    const uint64_t COINSTAKE_AMOUNT = 1000 * COIN;

    uint256 CalcKernelHash(uint256 stakeModifier, int64_t coinstakeTime, uint256 masternodeID, const Consensus::Params& params) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << stakeModifier << coinstakeTime << COINSTAKE_AMOUNT << masternodeID;
        return Hash(ss.begin(), ss.end());
    }

    CheckKernelHashRes
    CheckKernelHash(uint256 stakeModifier, uint32_t nBits, int64_t coinstakeTime, const Consensus::Params& params, uint256 masternodeID) {
        // Base target
        arith_uint256 targetProofOfStake;
        targetProofOfStake.SetCompact(nBits);

//        uint256 masternodeID = uint256S("0"); // TODO: (SS) change to masternode activation tx hash
        const arith_uint256 hashProofOfStake = UintToArith256(
                CalcKernelHash(stakeModifier, coinstakeTime, masternodeID, params));

        // Now check if proof-of-stake hash meets target protocol
        if ((hashProofOfStake / (uint64_t) COINSTAKE_AMOUNT) > targetProofOfStake) {
            return {false, hashProofOfStake};
        }

        return {true, hashProofOfStake};
    }

    uint256 ComputeStakeModifier(uint256 prevStakeModifier, const CKeyID& key) {
        // Calculate hash
        CDataStream ss(SER_GETHASH, 0);
        ss << prevStakeModifier << key;
        return Hash(ss.begin(), ss.end());
    }
}
