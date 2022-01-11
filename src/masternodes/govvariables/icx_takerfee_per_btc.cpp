// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/icx_takerfee_per_btc.h>
#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue

Res ICX_TAKERFEE_PER_BTC::Import(const UniValue & val)
{
    takerFeePerBTC = AmountFromValue(val);
    return Res::Ok();
}

UniValue ICX_TAKERFEE_PER_BTC::Export() const
{
    return ValueFromAmount(takerFeePerBTC);
}

Res ICX_TAKERFEE_PER_BTC::Validate(const CCustomCSView &mnview) const
{
    if (takerFeePerBTC <= 0)
        return Res::Err("takerFeePerBTC cannot be 0 or less");

    return Res::Ok();
}

Res ICX_TAKERFEE_PER_BTC::Apply(CCustomCSView & mnview, uint32_t)
{
    return mnview.ICXSetTakerFeePerBTC(takerFeePerBTC);
}
