#include <masternodes/ffi_exports.h>
#include <util/system.h>

uint64_t getChainId() {
    return Params().GetConsensus().evmChainId;
}

bool isMining() {
    return gArgs.GetBoolArg("-gen", false);
}