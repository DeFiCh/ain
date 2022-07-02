#include <libain.hpp>

#include <rpc/blockchain.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/util.h>

#include <grpc/util.h>

#include <policy/fees.h>
#include <util/fees.h>

#include <pos.h>
#include <pos_kernel.h>
#include <miner.h>
#include <masternodes/masternodes.h>
#include <warnings.h>

// Utils
static double GetNetworkHashPS(int lookup = 120, int height = -1);

// RPC calls
void GetNetworkHashPerSecond(GetNetworkHashPerSecondInput& input, GetNetworkHashPerSecondResult& result);
void GetMiningInfo(MiningInfo& result);
void EstimateSmartFee(EstimateSmartFeeInput& input, EstimateSmartFeeResult& result);
