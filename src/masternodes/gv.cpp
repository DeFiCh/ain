// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/gv.h>
#include <masternodes/govvariables/icx_takerfee_per_btc.h>
#include <masternodes/govvariables/loan_daily_reward.h>
#include <masternodes/govvariables/loan_splits.h>
#include <masternodes/govvariables/lp_daily_dfi_reward.h>
#include <masternodes/govvariables/lp_splits.h>
#include <masternodes/govvariables/oracle_block_interval.h>
#include <masternodes/govvariables/oracle_deviation.h>

Res CGovView::SetVariable(GovVariable const & var)
{
    return WriteBy<ByName>(var.GetName(), var) ? Res::Ok() : Res::Err("can't write to DB");
}

std::shared_ptr<GovVariable> CGovView::GetVariable(std::string const & name) const
{
    auto var = GovVariable::Create(name);
    if (var) {
        /// @todo empty or NO variable??
        ReadBy<ByName>(std::string(var->GetName()), *var);
        return var;
    }
    return {};
}

Res CGovView::SetStoredVariables(const std::set<std::shared_ptr<GovVariable>>& govVars, const uint64_t height)
{
    std::vector<std::string> govVarNames;
    for (auto& item : govVars) {
        govVarNames.push_back(item->GetName());
        if (!WriteBy<ByHeightVars>(std::pair<uint64_t, std::string>(height, item->GetName()), *item)) {
            return Res::Err("Cannot write to DB");
        }
    }
    if (!WriteBy<ByHeightNames>(height, govVarNames)) {
        return Res::Err("Cannot write to DB");
    }
    return Res::Ok();
}

std::set<std::shared_ptr<GovVariable>> CGovView::GetStoredVariables(const uint64_t height) const
{
    std::vector<std::string> govVarNames;
    if (!ReadBy<ByHeightNames>(height, govVarNames) || govVarNames.empty()) {
        return {};
    }
    std::set<std::shared_ptr<GovVariable>> govVars;
    for (const auto& name: govVarNames) {
        auto var = GovVariable::Create(name);
        if (var) {
            ReadBy<ByHeightVars>(std::pair<uint64_t, std::string>(height, name), *var);
            govVars.insert(var);
        }
    }
    return govVars;
}
