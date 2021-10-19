// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/loan_splits.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res LOAN_SPLITS::Import(const UniValue & val) {
    if (!val.isObject())
        return Res::Err("object expected"); // TODO Add more explicit info on object format
    for (const std::string& key : val.getKeys()) {
        const auto id = DCT_ID::FromString(key);
        if (!id.ok) {
            return id;
        }
        splits.emplace(*id.val, AmountFromValue(val[key]));
    }
    return Res::Ok();
}

UniValue LOAN_SPLITS::Export() const {
    UniValue res(UniValue::VOBJ);
    for (auto const & kv : splits) {
        res.pushKV(kv.first.ToString(), ValueFromAmount(kv.second));
    }
    return res;
}

Res LOAN_SPLITS::Validate(const CCustomCSView & mnview) const {
    CAmount total{0};
    for (auto const & kv : splits) {
        // TODO Add validation against loans here.

        if (kv.second < 0 || kv.second > COIN)
            return Res::Err("wrong percentage for pool with id=%s, value = %s", kv.first.ToString(), std::to_string(kv.second));

        total += kv.second;
    }
    if (total != COIN)
        return Res::Err("total = %d vs expected %d", total, COIN);

    return Res::Ok();
}

Res LOAN_SPLITS::Apply(CCustomCSView & mnview, uint32_t height) {
    // TODO Apply rewardPct to loans here.

    return Res::Ok();
}

