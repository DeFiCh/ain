#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <dfi/masternodes.h>
#include <ffi/ffiexports.h>
#include <ffi/ffihelpers.h>
#include <logging.h>
#include <primitives/block.h>
#include <rpc/blockchain.h>
#include <sync.h>
#include <univalue.h>
#include <util/system.h>
#include <validation.h>

#include <chrono>
#include <cstdint>
#include <string>

bool OceanIndex(const UniValue b, uint32_t blockHeight) {
    CrossBoundaryResult result;
    ocean_index_block(result, b.write());
    if (!result.ok) {
        LogPrintf("Error indexing ocean block %d: %s\n", result.reason, blockHeight);
        ocean_invalidate_block(result, b.write());
        if (!result.ok) {
            LogPrintf("Error invalidating ocean %d block: %s\n", result.reason, blockHeight);
        }
        return false;
    }
    return true;
};

bool CatchupOceanIndexer() {
    if (!gArgs.GetBoolArg("-oceanarchive", DEFAULT_OCEAN_INDEXER_ENABLED) &&
        !gArgs.GetBoolArg("-expr-oceanarchive", DEFAULT_OCEAN_INDEXER_ENABLED)) {
        return true;
    }

    CrossBoundaryResult result;

    auto oceanBlockHeight = ocean_get_block_height(result);
    if (!result.ok) {
        LogPrintf("Error getting Ocean block height: %s\n", result.reason);
        return false;
    }

    CBlockIndex *tip = nullptr;
    {
        LOCK(cs_main);
        tip = ::ChainActive().Tip();
        if (!tip) {
            LogPrintf("Error: Cannot get chain tip\n");
            return false;
        }
    }
    const uint32_t tipHeight = tip->nHeight;

    if (tipHeight == oceanBlockHeight) {
        return true;
    }

    LogPrintf("Starting Ocean index catchup...\n");

    uint32_t currentHeight = oceanBlockHeight;

    LogPrintf("Ocean catchup: Current height=%u, Target height=%u\n", currentHeight, tipHeight);

    uint32_t remainingBlocks = tipHeight - currentHeight;
    const uint32_t startHeight = oceanBlockHeight;
    int lastProgress = -1;
    const auto startTime = std::chrono::steady_clock::now();

    CBlockIndex *pindex = nullptr;
    while (currentHeight < tipHeight) {
        if (ShutdownRequested()) {
            LogPrintf("Shutdown requested, exiting ocean catchup...\n");
            return false;
        }

        {
            LOCK(cs_main);
            pindex = ::ChainActive()[currentHeight];
            if (!pindex) {
                LogPrintf("Error: Cannot find block at height %u\n", currentHeight);
                return false;
            }
        }

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
            LogPrintf("Error: Failed to read block %s from disk\n", pindex->GetBlockHash().ToString());
            return false;
        }

        const UniValue b = blockToJSON(*pcustomcsview, block, tip, pindex, true, 2);

        if (bool isIndexed = OceanIndex(b, currentHeight); !isIndexed) {
            return false;
        }

        currentHeight++;

        uint32_t blocksProcessed = currentHeight - startHeight;
        int currentProgress = static_cast<int>((static_cast<double>(currentHeight * 100) / tipHeight));

        if (currentProgress > lastProgress || currentHeight % 10000 == 0) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();

            double blocksPerSecond = elapsed > 0 ? static_cast<double>(blocksProcessed) / elapsed : 0;

            remainingBlocks = tipHeight - currentHeight;
            int estimatedSecondsLeft = blocksPerSecond > 0 ? static_cast<int>(remainingBlocks / blocksPerSecond) : 0;

            LogPrintf(
                "Ocean indexing progress: %d%% (%u/%u blocks) - %.2f blocks/s - "
                "ETA: %d:%02d:%02d\n",
                currentProgress,
                currentHeight,
                tipHeight,
                blocksPerSecond,
                estimatedSecondsLeft / 3600,
                (estimatedSecondsLeft % 3600) / 60,
                estimatedSecondsLeft % 60);

            lastProgress = currentProgress;
        }
    }

    auto totalTime =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count();

    LogPrintf("Ocean indexes caught up to tip. Total time: %d:%02d:%02d\n",
              totalTime / 3600,
              (totalTime % 3600) / 60,
              totalTime % 60);

    return true;
}
