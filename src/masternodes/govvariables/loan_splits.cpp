// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/loan_splits.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res LP_LOAN_TOKEN_SPLITS::Import(const UniValue & val)
{
    Require(val.isObject(), "object of {poolId: rate,... } expected");

    for (const std::string& key : val.getKeys()) {
        auto id = DCT_ID::FromString(key);
        Require(id);
        splits.emplace(*id, AmountFromValue(val[key]));
    }
    return Res::Ok();
}

UniValue LP_LOAN_TOKEN_SPLITS::Export() const
{
    UniValue res(UniValue::VOBJ);
    for (auto const & kv : splits) {
        res.pushKV(kv.first.ToString(), ValueFromAmount(kv.second));
    }
    return res;
}

Res LP_LOAN_TOKEN_SPLITS::Validate(const CCustomCSView & mnview) const
{
    Require(mnview.GetLastHeight() >= Params().GetConsensus().FortCanningHeight, "Cannot be set before FortCanning");

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

Res LP_LOAN_TOKEN_SPLITS::Apply(CCustomCSView & mnview, uint32_t height)
{
    mnview.ForEachPoolId([&] (DCT_ID poolId) {
        // we ought to reset previous value:
        CAmount rewardLoanPct = 0;
        auto it = splits.find(poolId);
        if (it != splits.end())
            rewardLoanPct = it->second;

        mnview.SetRewardLoanPct(poolId, height, rewardLoanPct);
        return true;
    });

    return Res::Ok();
}
