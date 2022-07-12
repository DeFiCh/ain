#ifndef DEFI_GRPC_MINING_H
#define DEFI_GRPC_MINING_H

#include <libain.hpp>

// RPC calls
void GetNetworkHashPS(const Context&, NetworkHashRateInput& input, NetworkHashRateResult& result);
void GetMiningInfo(const Context&, MiningInfo& result);
void EstimateSmartFee(const Context&, SmartFeeInput& input, SmartFeeResult& result);

#endif // DEFI_GRPC_MINING_H
