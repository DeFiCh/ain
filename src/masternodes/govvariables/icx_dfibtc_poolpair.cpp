// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/icx_dfibtc_poolpair.h>
#include <masternodes/masternodes.h> /// CCustomCSView


Res ICX_DFIBTC_POOLPAIR::Import(const UniValue & val) {
    poolPairId.v = val.get_int();
    return Res::Ok();
}

UniValue ICX_DFIBTC_POOLPAIR::Export() const {
    UniValue res(UniValue::VOBJ);

    return static_cast<int>(poolPairId.v);
}

Res ICX_DFIBTC_POOLPAIR::Validate(const CCustomCSView &mnview) const
{
    if (!mnview.HasPoolPair(poolPairId))
            return Res::Err("pool with id=%s not found", poolPairId.ToString());
    return Res::Ok();
}

Res ICX_DFIBTC_POOLPAIR::Apply(CCustomCSView & mnview, uint32_t height)
{
    return mnview.ICXSetDFIBTCPoolPairId(height, poolPairId);
}