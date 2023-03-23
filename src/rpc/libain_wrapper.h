#ifndef DEFI_RPC_LIBAIN_WRAPPER_H
#define DEFI_RPC_LIBAIN_WRAPPER_H

#include <libain_grpc.h>

void Eth_Accounts(EthAccountsResult& result);
void Eth_Call(EthCallInput& request, EthCallResult& result);
void GetBestBlockHash(BlockResult& result);

#endif // DEFI_RPC_LIBAIN_WRAPPER_H
