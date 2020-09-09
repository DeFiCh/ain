// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/LP_DAILY_DFI_REWARD.h>

#include <core_io.h> /// ValueFromAmount
#include <rpc/util.h> /// AmountFromValue


Res LP_DAILY_DFI_REWARD::Import(const UniValue & val) {
    dailyReward = AmountFromValue(val);
    return Res::Ok();
}

UniValue LP_DAILY_DFI_REWARD::Export() const {
    return ValueFromAmount(dailyReward);
}

Res LP_DAILY_DFI_REWARD::Validate(const CCustomCSView &) const
{
    // nothing to do
    return Res::Ok();
}

Res LP_DAILY_DFI_REWARD::Apply(CCustomCSView &)
{
    // nothing to do
    return Res::Ok();
}

//namespace
//{

//class Registrator
//{
//public:
//    Registrator() {
//        Factory<GovVariable>::Registrate<LP_DAILY_DFI_REWARD>();
//    }
//} registrator;

//}


