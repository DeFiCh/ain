#include <grpc/mining.h>
#include <grpc/util.h>
#include <masternodes/masternodes.h>
#include <miner.h>
#include <policy/fees.h>
#include <pos.h>
#include <pos_kernel.h>
#include <rpc/blockchain.h>
#include <rpc/mining.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/util.h>
#include <util/fees.h>
#include <warnings.h>

void GetNetworkHashPS(const Context&, NetworkHashRateInput& input, NetworkHashRateResult& result)
{
    LOCK(cs_main);
    result.hashps = GetNetworkHashPS(static_cast<int>(input.nblocks), static_cast<int>(input.height));
}

void GetMiningInfo(const Context&, MiningInfo& result)
{
    LOCK(cs_main);

    auto height = ::ChainActive().Height();
    result.blocks = static_cast<int64_t>(height);
    if (BlockAssembler::m_last_block_weight) result.currentblockweight = *BlockAssembler::m_last_block_weight;
    if (BlockAssembler::m_last_block_num_txs) result.currentblocktx = *BlockAssembler::m_last_block_num_txs;
    result.difficulty = GetDifficulty(::ChainActive().Tip());
    auto input = MakeNetworkHashRateInput();
    result.networkhashps = GetNetworkHashPS(static_cast<int>(input.nblocks), static_cast<int>(input.height));
    result.pooledtx = mempool.size();
    result.chain = Params().NetworkIDString();

    bool genCoins = gArgs.GetBoolArg("-gen", DEFAULT_GENERATE);

    // get all masternode operators
    auto mnIds = pcustomcsview->GetOperatorsMulti();
    result.isoperator = !mnIds.empty();
    for (const auto& mnId : mnIds) {
        auto masternodeInfo = MakeMasternodeInfo();

        masternodeInfo.id = mnId.second.GetHex();
        auto nodePtr = pcustomcsview->GetMasternode(mnId.second);
        if (!nodePtr) {
            //should not come here if the database has correct data.
            throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("The masternode %s does not exist", mnId.second.GetHex()));
        }
        auto state = nodePtr->GetState(height);
        CTxDestination operatorDest = nodePtr->operatorType == 1 ? CTxDestination(PKHash(nodePtr->operatorAuthAddress)) :
                                      CTxDestination(WitnessV0KeyHash(nodePtr->operatorAuthAddress));
        masternodeInfo.field_operator = EncodeDestination(operatorDest);
        masternodeInfo.state = CMasternode::GetHumanReadableState(state);
        masternodeInfo.mintedblocks = nodePtr->mintedBlocks;

        auto generate = nodePtr->IsActive(height) && genCoins;
        masternodeInfo.generate = generate;
        if (!generate) {
            masternodeInfo.lastblockcreationattempt = "0";
        } else {
            // get the last block creation attempt by the master node
            CLockFreeGuard lock(pos::Staker::cs_MNLastBlockCreationAttemptTs);
            auto lastBlockCreationAttemptTs = pos::Staker::mapMNLastBlockCreationAttemptTs[mnId.second];
            masternodeInfo.lastblockcreationattempt = (lastBlockCreationAttemptTs != 0) ? FormatISO8601DateTime(lastBlockCreationAttemptTs) : "0";
        }

        const auto timelock = pcustomcsview->GetTimelock(mnId.second, *nodePtr, height);

        // Get targetMultiplier if node is active
        if (nodePtr->IsActive(height)) {
            // Get block times
            const auto subNodesBlockTime = pcustomcsview->GetBlockTimes(nodePtr->operatorAuthAddress, height, nodePtr->creationHeight, timelock);

            const uint8_t loops = timelock == CMasternode::TENYEAR ? 4 : timelock == CMasternode::FIVEYEAR ? 3 : 2;
            for (uint8_t i{0}; i < loops; ++i) {
                masternodeInfo.target_multipliers.push_back(pos::CalcCoinDayWeight(Params().GetConsensus(), GetTime(), subNodesBlockTime[i]).getdouble());
            }
        }

        if (timelock) {
            masternodeInfo.timelock = strprintf("%d years", timelock / 52);
        }

        result.masternodes.push_back(masternodeInfo);
    }
    result.warnings = GetWarnings("statusbar");
}

void EstimateSmartFee(const Context&, SmartFeeInput& input, SmartFeeResult& result)
{
    unsigned int max_target = ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    unsigned int conf_target = ParseConfirmTarget((int)input.conf_target, max_target);
    bool conservative = true;

    FeeEstimateMode fee_mode;
    if (!FeeModeFromString(std::string(input.estimate_mode), fee_mode)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
    }
    if (fee_mode == FeeEstimateMode::ECONOMICAL) conservative = false;

    FeeCalculation feeCalc;
    CFeeRate feeRate = ::feeEstimator.estimateSmartFee(conf_target, &feeCalc, conservative);
    if (feeRate != CFeeRate(0)) {
        result.feerate = FromAmount(feeRate.GetFeePerK());
    } else {
        result.errors.push_back("Insufficient data or no feerate found");
    }
    result.blocks = (uint64_t)feeCalc.returnedTarget;
}
