// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/loan_splits.h>

#include <core_io.h>                  /// ValueFromAmount
#include <masternodes/masternodes.h>  /// CCustomCSView
#include <rpc/util.h>                 /// AmountFromValue

bool LP_LOAN_TOKEN_SPLITS::IsEmpty() const {
    return splits.empty();
}

Res LP_LOAN_TOKEN_SPLITS::Import(const UniValue &val) {
    if (!val.isObject())
        return Res::Err("object of {poolId: rate,... } expected");

    for (const std::string &key : val.getKeys()) {
        const auto id = DCT_ID::FromString(key);
        if (!id)
            return std::move(id);
        splits.emplace(*id.val, AmountFromValue(val[key]));
    }
    return Res::Ok();
}

UniValue LP_LOAN_TOKEN_SPLITS::Export() const {
    UniValue res(UniValue::VOBJ);
    for (const auto &kv : splits) {
        res.pushKV(kv.first.ToString(), ValueFromAmount(kv.second));
    }
    return res;
}

Res LP_LOAN_TOKEN_SPLITS::Validate(const CCustomCSView &mnview) const {
    if (mnview.GetLastHeight() < Params().GetConsensus().FortCanningHeight)
        return Res::Err("Cannot be set before FortCanning");

    CAmount total{0};
    for (const auto &kv : splits) {
        if (!mnview.HasPoolPair(kv.first))
            return Res::Err("pool with id=%s not found", kv.first.ToString());

        if (kv.second < 0 || kv.second > COIN)
            return Res::Err(
                "wrong percentage for pool with id=%s, value = %s", kv.first.ToString(), std::to_string(kv.second));

        total += kv.second;
    }
    if (total != COIN)
        return Res::Err("total = %d vs expected %d", total, COIN);

    return Res::Ok();
}

Res LP_LOAN_TOKEN_SPLITS::Apply(CCustomCSView &mnview, uint32_t height) {
    mnview.ForEachPoolId([&](DCT_ID poolId) {
        // we ought to reset previous value:
        CAmount rewardLoanPct = 0;
        auto it               = splits.find(poolId);
        if (it != splits.end())
            rewardLoanPct = it->second;

        mnview.SetRewardLoanPct(poolId, height, rewardLoanPct);
        return true;
    });

    return Res::Ok();
}

Res LP_LOAN_TOKEN_SPLITS::Erase(CCustomCSView &mnview, uint32_t height, const std::vector<std::string> &keys) {
    for (const auto &key : keys) {
        auto res = DCT_ID::FromString(key);
        if (!res)
            return std::move(res);

        auto id = *res.val;
        if (!splits.erase(id))
            return Res::Err("id {%d} does not exists", id.v);

        mnview.SetRewardLoanPct(id, height, 0);
    }
    return Res::Ok();
}
