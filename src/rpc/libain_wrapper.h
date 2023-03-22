#pragma once
#include <libain_grpc.h>

void Eth_Accounts(EthAccountsResult& result);
void Eth_Call(EthCallInput& request, EthCallResult& result);
void GetBestBlockHash(BlockResult& result);
