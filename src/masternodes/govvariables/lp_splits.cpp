// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/lp_splits.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res LP_SPLITS::Import(const UniValue & val)
{
    Require(val.isObject(), "object of {poolId: rate,... } expected"); /// throw here? cause "AmountFromValue" can throw!

    for (const std::string& key : val.getKeys()) {
        auto id = DCT_ID::FromString(key);
        Require(id);
        splits.emplace(*id, AmountFromValue(val[key]));//todo: AmountFromValue
    }
    return Res::Ok();
}

UniValue LP_SPLITS::Export() const
{
    UniValue res(UniValue::VOBJ);
    for (auto const & kv : splits) {
        res.pushKV(kv.first.ToString(), ValueFromAmount(kv.second));
    }
    return res;
}

Res LP_SPLITS::Validate(const CCustomCSView & mnview) const
{
    CAmount total{0};
    for (auto const & kv : splits) {
        Require(mnview.HasPoolPair(kv.first), "pool with id=%s not found", kv.first.ToString());

        Require(kv.second >= 0 && kv.second <= COIN,
                    "wrong percentage for pool with id=%s, value = %s", kv.first.ToString(), std::to_string(kv.second));

        total += kv.second;
    }
    Require(total == COIN, "total = %d vs expected %d", total, COIN);

    return Res::Ok();
}

Res LP_SPLITS::Apply(CCustomCSView & mnview, uint32_t height)
{
    mnview.ForEachPoolId([&] (DCT_ID poolId) {
        // we ought to reset previous value:
        CAmount rewardPct = 0;
        auto it = splits.find(poolId);
        if (it != splits.end())
            rewardPct = it->second;

        mnview.SetRewardPct(poolId, height, rewardPct);
        return true;
    });
    return Res::Ok();
}
