#include "grpc/mining.h"

/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
static double GetNetworkHashPS(int lookup, int height) {
    CBlockIndex *pb = ::ChainActive().Tip();

    if (height >= 0 && height < ::ChainActive().Height())
        pb = ::ChainActive()[height];

    if (pb == nullptr || !pb->nHeight)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup <= 0)
        lookup = pb->nHeight % Params().GetConsensus().pos.DifficultyAdjustmentInterval() + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex *pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return workDiff.getdouble() / timeDiff;
}

void GetNetworkHashPerSecond(GetNetworkHashPerSecondInput& input, GetNetworkHashPerSecondResult& result)
{
    LOCK(cs_main);
    result.hashps = GetNetworkHashPS(input.nblocks, input.height);
}

void GetMiningInfo(MiningInfo& result)
{

    LOCK(cs_main);

    auto height = static_cast<int>(::ChainActive().Height());
    result.blocks = height;
    if (BlockAssembler::m_last_block_weight) result.currentblockweight = *BlockAssembler::m_last_block_weight;
    if (BlockAssembler::m_last_block_num_txs) result.currentblocktx = *BlockAssembler::m_last_block_num_txs;
    result.difficulty = GetDifficulty(::ChainActive().Tip());
    result.networkhashps = GetNetworkHashPS();
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
        masternodeInfo.mintedblocks = (uint64_t)nodePtr->mintedBlocks;

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

void EstimateSmartFee(EstimateSmartFeeInput& input, EstimateSmartFeeResult& result) {
    unsigned int max_target = ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    unsigned int conf_target = ParseConfirmTarget(input.conf_target, max_target);
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
    result.blocks = feeCalc.returnedTarget;
}
