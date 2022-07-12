#ifndef DEFI_GRPC_BLOCKCHAIN_H
#define DEFI_GRPC_BLOCKCHAIN_H

#include <libain.hpp>

void GetBestBlockHash(const Context&, BlockResult& result);
void GetBlock(const Context&, BlockInput& block_input, BlockResult& result);

#endif // DEFI_GRPC_BLOCKCHAIN_H
