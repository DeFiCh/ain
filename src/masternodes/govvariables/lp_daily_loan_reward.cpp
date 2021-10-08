// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/lp_daily_loan_reward.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res LP_DAILY_LOAN_REWARD::Import(const UniValue & val) {
    dailyReward = AmountFromValue(val);
    return Res::Ok();
}

UniValue LP_DAILY_LOAN_REWARD::Export() const {
    return ValueFromAmount(dailyReward);
}

Res LP_DAILY_LOAN_REWARD::Validate(const CCustomCSView & view) const
{
    return Res::Err("Cannot be set manually.");
}

Res LP_DAILY_LOAN_REWARD::Apply(CCustomCSView & mnview, uint32_t height)
{
    // Do something here.
    return Res::Ok();
}
