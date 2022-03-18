// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/gv.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/govvariables/icx_takerfee_per_btc.h>
#include <masternodes/govvariables/loan_daily_reward.h>
#include <masternodes/govvariables/loan_liquidation_penalty.h>
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

Res CGovView::SetStoredVariables(const std::set<std::shared_ptr<GovVariable>>& govVars, const uint32_t height)
{
    for (auto& item : govVars) {
        if (!WriteBy<ByHeightVars>(GovVarKey{height, item->GetName()}, *item)) {
            return Res::Err("Cannot write to DB");
        }
    }

    return Res::Ok();
}

std::set<std::shared_ptr<GovVariable>> CGovView::GetStoredVariables(const uint32_t height)
{
    // Popualte a set of Gov vars for specified height
    std::set<std::shared_ptr<GovVariable>> govVars;
    auto it = LowerBound<ByHeightVars>(GovVarKey{height, {}});
    for (; it.Valid() && it.Key().height == height; it.Next()) {
        auto var = GovVariable::Create(it.Key().name);
        if (var) {
            it.Value(*var);
            govVars.insert(var);
        }
    }
    return govVars;
}

std::map<std::string, std::map<uint64_t, std::shared_ptr<GovVariable>>> CGovView::GetAllStoredVariables()
{
    // Populate map by var name as key and map of height and Gov vars as elements.
    std::map<std::string, std::map<uint64_t, std::shared_ptr<GovVariable>>> govVars;
    auto it = LowerBound<ByHeightVars>(GovVarKey{std::numeric_limits<uint32_t>::min(), {}});
    for (; it.Valid(); it.Next()) {
        auto var = GovVariable::Create(it.Key().name);
        if (var) {
            it.Value(*var);
            govVars[it.Key().name][it.Key().height] = var;
        }
    }

    return govVars;
}

void CGovView::EraseStoredVariables(const uint32_t height)
{
    // Retrieve map of vars at specified height
    const auto vars = GetStoredVariables(height);

    // Iterate over names at this height and erase
    for (const auto& var : vars) {
        EraseBy<ByHeightVars>(GovVarKey{height, var->GetName()});
    }
}

std::shared_ptr<ATTRIBUTES> CGovView::GetAttributes() const {
    if (const auto var = GetVariable("ATTRIBUTES")) {
        return std::dynamic_pointer_cast<ATTRIBUTES>(var);
    }
    return {};
}
