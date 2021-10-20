// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/loan_daily_reward.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res LOAN_DAILY_REWARD::Import(const UniValue & val) {
    dailyReward = AmountFromValue(val);
    return Res::Ok();
}

UniValue LOAN_DAILY_REWARD::Export() const {
    return ValueFromAmount(dailyReward);
}

Res LOAN_DAILY_REWARD::Validate(const CCustomCSView & view) const
{
    return Res::Err("Cannot be set manually.");
}

Res LOAN_DAILY_REWARD::Apply(CCustomCSView & mnview, uint32_t height)
{
    return mnview.SetLoanDailyReward(dailyReward);
}
