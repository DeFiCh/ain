// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/govvariables/loan_daily_reward.h>

#include <core_io.h>          /// ValueFromAmount
#include <dfi/masternodes.h>  /// CCustomCSView
#include <rpc/util.h>         /// AmountFromValue

bool LP_DAILY_LOAN_TOKEN_REWARD::IsEmpty() const {
    return !dailyReward.has_value();
}

Res LP_DAILY_LOAN_TOKEN_REWARD::Import(const UniValue &val) {
    dailyReward = AmountFromValue(val);
    return Res::Ok();
}

UniValue LP_DAILY_LOAN_TOKEN_REWARD::Export() const {
    return ValueFromAmount(dailyReward.value_or(0));
}

Res LP_DAILY_LOAN_TOKEN_REWARD::Validate(const CCustomCSView &view) const {
    if (view.GetLastHeight() < Params().GetConsensus().DF11FortCanningHeight) {
        return Res::Err("Cannot be set before FortCanning");
    }
    return Res::Err("Cannot be set manually.");
}

Res LP_DAILY_LOAN_TOKEN_REWARD::Apply(CCustomCSView &mnview, uint32_t height) {
    return mnview.SetLoanDailyReward(height, dailyReward.value_or(0));
}

Res LP_DAILY_LOAN_TOKEN_REWARD::Erase(CCustomCSView &mnview, uint32_t height, const std::vector<std::string> &) {
    dailyReward.reset();
    return mnview.SetLoanDailyReward(height, 0);
}
