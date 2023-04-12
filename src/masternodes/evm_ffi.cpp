#include <masternodes/evm_ffi.h>

uint64_t getChainId() {
    return Params().GetConsensus().evmChainId;
}
