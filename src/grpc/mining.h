#include <libain.hpp>

// Utils
static double GetNetworkHashPS(int lookup = 120, int height = -1);

// RPC calls
void GetNetworkHashPerSecond(GetNetworkHashPerSecondInput& input, GetNetworkHashPerSecondResult& result);
void GetMiningInfo(MiningInfo& result);
void EstimateSmartFee(EstimateSmartFeeInput& input, EstimateSmartFeeResult& result);
