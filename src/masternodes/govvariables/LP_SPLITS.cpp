// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/LP_SPLITS.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res LP_SPLITS::Import(const UniValue & val) {
    if (!val.isObject())
        return Res::Err("object of {poolId: rate,... } expected"); /// throw here? cause "AmountFromValue" can throw!
    for (const std::string& key : val.getKeys()) {
        splits.emplace(DCT_ID{(uint32_t)std::stoul(key)}, AmountFromValue(val[key]));//todo: AmountFromValue
    }
    return Res::Ok();
}

UniValue LP_SPLITS::Export() const {
    UniValue res(UniValue::VOBJ);
    for (auto const & kv : splits) {
        res.pushKV(kv.first.ToString(), ValueFromAmount(kv.second));
    }
    return res;
}

Res LP_SPLITS::Validate(const CCustomCSView & mnview) const {
    CAmount total{0};
    for (auto const & kv : splits) {
        auto pool = mnview.GetPoolPair(kv.first);

        /// @todo uncomment
        if (!pool)
            return Res::Err("pool with id=%s not found", kv.first.ToString());

        total += kv.second;
    }
    if (total != COIN)
        return Res::Err("total = %d vs expected %d", total, COIN);

    return Res::Ok();
}

Res LP_SPLITS::Apply(CCustomCSView & mnview) {
    for (auto const & kv : splits) {
        auto pool = mnview.GetPoolPair(kv.first);

        /// @todo uncomment
        if (!pool)
            return Res::Err("pool with id=%s not found", kv.first.ToString());

        pool->rewardPct = kv.second;
        mnview.SetPoolPair(kv.first, *pool);

    }
    return Res::Ok();
}


/// @todo dont work from "here". research
//namespace
//{

//class Registrator
//{
//public:
//    Registrator() {
//        Factory<GovVariable>::Registrate<LP_SPLITS>();
//    }
//} registrator;

//}

