// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/govvariables/lp_splits.h>

#include <core_io.h>          /// ValueFromAmount
#include <dfi/masternodes.h>  /// CCustomCSView
#include <rpc/util.h>         /// AmountFromValue

bool LP_SPLITS::IsEmpty() const {
    return splits.empty();
}

Res LP_SPLITS::Import(const UniValue &val) {
    if (!val.isObject()) {
        return Res::Err("object of {poolId: rate,... } expected");
    }

    for (const std::string &key : val.getKeys()) {
        auto id = DCT_ID::FromString(key);
        if (!id) {
            return id;
        }
        splits.emplace(*id.val, AmountFromValue(val[key]));  // todo: AmountFromValue
    }
    return Res::Ok();
}

UniValue LP_SPLITS::Export() const {
    UniValue res(UniValue::VOBJ);
    for (const auto &kv : splits) {
        res.pushKV(kv.first.ToString(), ValueFromAmount(kv.second));
    }
    return res;
}

Res LP_SPLITS::Validate(const CCustomCSView &mnview) const {
    CAmount total{0};
    for (const auto &[poolId, amount] : splits) {
        if (!mnview.HasPoolPair(poolId)) {
            return Res::Err("pool with id=%s not found", poolId.ToString());
        }

        if (amount < 0 || amount > COIN) {
            return Res::Err(
                "wrong percentage for pool with id=%s, value = %s", poolId.ToString(), std::to_string(amount));
        }

        total += amount;
    }
    if (total != COIN) {
        return Res::Err("total = %d vs expected %d", total, COIN);
    }

    return Res::Ok();
}

Res LP_SPLITS::Apply(CCustomCSView &mnview, uint32_t height) {
    mnview.ForEachPoolId([&](DCT_ID poolId) {
        // we ought to reset previous value:
        CAmount rewardPct = 0;
        auto it = splits.find(poolId);
        if (it != splits.end()) {
            rewardPct = it->second;
        }

        mnview.SetRewardPct(poolId, height, rewardPct);
        return true;
    });
    return Res::Ok();
}

Res LP_SPLITS::Erase(CCustomCSView &mnview, uint32_t height, const std::vector<std::string> &keys) {
    for (const auto &key : keys) {
        auto res = DCT_ID::FromString(key);
        if (!res) {
            return std::move(res);
        }

        auto id = *res.val;
        if (!splits.erase(id)) {
            return Res::Err("id {%d} does not exists", id.v);
        }

        mnview.SetRewardPct(id, height, 0);
    }
    return Res::Ok();
}
