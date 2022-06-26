// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/lp_daily_dfi_reward.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue

bool LP_DAILY_DFI_REWARD::IsEmpty() const
{
    return !dailyReward.has_value();
}

Res LP_DAILY_DFI_REWARD::Import(const UniValue & val)
{
    CAmount amount;
    if (!AmountFromValue(val, amount)) {
        return Res::Err("Invalid amount");
    }
    dailyReward = amount;
    return Res::Ok();
}

UniValue LP_DAILY_DFI_REWARD::Export() const
{
    return ValueFromAmount(dailyReward.value_or(0));
}

Res LP_DAILY_DFI_REWARD::Validate(const CCustomCSView & view) const
{
    Require(view.GetLastHeight() < Params().GetConsensus().EunosHeight, "Cannot be set manually after Eunos hard fork");

    // nothing to do
    return Res::Ok();
}

Res LP_DAILY_DFI_REWARD::Apply(CCustomCSView & mnview, uint32_t height)
{
    return mnview.SetDailyReward(height, dailyReward.value_or(0));
}

Res LP_DAILY_DFI_REWARD::Erase(CCustomCSView & mnview, uint32_t height, std::vector<std::string> const &)
{
    dailyReward.reset();
    return mnview.SetDailyReward(height, 0);
}
