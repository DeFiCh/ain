#ifndef DEFI_GRPC_BLOCKCHAIN_H
#define DEFI_GRPC_BLOCKCHAIN_H

#include <libain.hpp>

void GetBestBlockHash(BlockResult& result);
void GetBlock(BlockInput& block_input, BlockResult& result);

#endif // DEFI_GRPC_BLOCKCHAIN_H
