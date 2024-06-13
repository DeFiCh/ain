// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <ain_rs_exports.h>
#include <dfi/customtx.h>
#include <uint256.h>
#include <util/system.h>

Res OceanSetTxResult(const std::optional<std::pair<CustomTxType, uint256>> &txInfo, const std::size_t result_ptr) {
    bool isOceanEnabled = gArgs.GetBoolArg("-oceanarchive", false);
    if (txInfo && isOceanEnabled) {
        const auto &[txType, txHash] = *txInfo;
        CrossBoundaryResult ffiResult;
        ocean_try_set_tx_result(ffiResult, static_cast<uint8_t>(txType), txHash.GetByteArrayBE(), result_ptr);
    }

    return Res::Ok();
}
