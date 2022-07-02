#include <libain.hpp>

#include <rpc/blockchain.h>
#include <rpc/util.h>

#include <grpc/util.h>

#include <consensus/validation.h>
#include <core_io.h>
#include <libain.hpp>
#include <masternodes/masternodes.h>
#include <sync.h>
#include <validation.h>

void GetBestBlockHash(BlockResult& result);
void GetBlock(BlockInput& block_input, BlockResult& result);
