// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>  /// ValueFromAmount
#include <masternodes/govvariables/icx_takerfee_per_btc.h>
#include <masternodes/masternodes.h>  /// CCustomCSView
#include <rpc/util.h>                 /// AmountFromValue

bool ICX_TAKERFEE_PER_BTC::IsEmpty() const {
    return !takerFeePerBTC.has_value();
}

Res ICX_TAKERFEE_PER_BTC::Import(const UniValue &val) {
    takerFeePerBTC = AmountFromValue(val);
    return Res::Ok();
}

UniValue ICX_TAKERFEE_PER_BTC::Export() const {
    return ValueFromAmount(takerFeePerBTC.value_or(0));
}

Res ICX_TAKERFEE_PER_BTC::Validate(const CCustomCSView &mnview) const {
    Require(takerFeePerBTC > 0, "takerFeePerBTC cannot be 0 or less");
    return Res::Ok();
}

Res ICX_TAKERFEE_PER_BTC::Apply(CCustomCSView &mnview, uint32_t) {
    return mnview.ICXSetTakerFeePerBTC(takerFeePerBTC.value_or(0));
}

Res ICX_TAKERFEE_PER_BTC::Erase(CCustomCSView &mnview, uint32_t, const std::vector<std::string> &) {
    takerFeePerBTC.reset();
    return mnview.ICXEraseTakerFeePerBTC();
}
