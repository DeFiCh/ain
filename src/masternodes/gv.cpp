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
    // Retrieve map of heights to Gov var changes
    std::map<uint64_t, std::set<std::string>> govVarNames;
    Read(ByHeightNames::prefix(), govVarNames);

    for (auto& item : govVars) {
        govVarNames[height].insert(item->GetName());
        if (!WriteBy<ByHeightVars>(std::pair<uint64_t, std::string>(height, item->GetName()), *item)) {
            return Res::Err("Cannot write to DB");
        }
    }

    if (!Write(ByHeightNames::prefix(), govVarNames)) {
        return Res::Err("Cannot write to DB");
    }
    return Res::Ok();
}

std::set<std::shared_ptr<GovVariable>> CGovView::GetStoredVariables(const uint64_t height) const
{
    // Retrieve map of heights to Gov var changes
    std::map<uint64_t, std::set<std::string>> govVarNames;
    if (!Read(ByHeightNames::prefix(), govVarNames)) {
        return {};
    }

    // Popualte a set of Gov vars for specified height
    std::set<std::shared_ptr<GovVariable>> govVars;
    for (const auto& name: govVarNames[height]) {
        auto var = GovVariable::Create(name);
        if (var) {
            ReadBy<ByHeightVars>(std::make_pair(height, name), *var);
            govVars.insert(var);
        }
    }
    return govVars;
}

std::map<std::string, std::map<uint64_t, std::shared_ptr<GovVariable>>> CGovView::GetAllStoredVariables() const
{
    // Retrieve map of heights to Gov var changes
    std::map<uint64_t, std::set<std::string>> govVarNames;
    if (!Read(ByHeightNames::prefix(), govVarNames)) {
        return {};
    }

    // Populate map by var name as key and map of height and Gov vars as elements.
    std::map<std::string, std::map<uint64_t, std::shared_ptr<GovVariable>>> govVars;
    for (const auto& items : govVarNames) {
        for (const auto& name : items.second) {
            auto var = GovVariable::Create(name);
            if (var) {
                ReadBy<ByHeightVars>(std::make_pair(items.first, name), *var);
                govVars[name][items.first] = var;
            }
        }
    }

    return govVars;
}

void CGovView::EraseStoredVariables(const uint64_t height)
{
    // Retrieve map of heights to Gov var changes
    std::map<uint64_t, std::set<std::string>> govVarNames;
    Read(ByHeightNames::prefix(), govVarNames);

    if (govVarNames[height].empty()) {
        return;
    }

    // Iterate over names at this height and erase
    for (const auto& items : govVarNames[height]) {
        for (const auto& name : items) {
            EraseBy<ByHeightVars>(std::make_pair(height, name));
        }
    }

    // Erase from map and store
    govVarNames.erase(height);
    Write(ByHeightNames::prefix(), govVarNames);
}
