// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/errors.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/govvariables/icx_takerfee_per_btc.h>
#include <dfi/govvariables/loan_daily_reward.h>
#include <dfi/govvariables/loan_liquidation_penalty.h>
#include <dfi/govvariables/loan_splits.h>
#include <dfi/govvariables/lp_daily_dfi_reward.h>
#include <dfi/govvariables/lp_splits.h>
#include <dfi/govvariables/oracle_block_interval.h>
#include <dfi/govvariables/oracle_deviation.h>
#include <dfi/gv.h>

Res CGovView::SetVariable(GovVariable &var) {
    auto WriteOrEraseVar = [this](const GovVariable &var) {
        if (var.IsEmpty()) {
            EraseBy<ByName>(var.GetName());
        } else {
            WriteBy<ByName>(var.GetName(), var);
        }
        return Res::Ok();
    };
    if (var.GetName() != "ATTRIBUTES") {
        return WriteOrEraseVar(var);
    }
    auto attr = GetAttributesFromStore();
    auto &current = dynamic_cast<ATTRIBUTES &>(var);
    if (current.changed.empty()) {
        return Res::Ok();
    }
    for (auto &key : current.changed) {
        auto it = current.attributes.find(key);
        if (it == current.attributes.end()) {
            attr->attributes.erase(key);
        } else {
            attr->attributes[key] = it->second;
        }
    }

    // Wipe changed set.
    current.changed.clear();

    return WriteOrEraseVar(*attr);
}

std::shared_ptr<GovVariable> CGovView::GetVariable(const std::string &name) const {
    if (const auto var = GovVariable::Create(name)) {
        ReadBy<ByName>(var->GetName(), *var);
        return var;
    }
    return {};
}

Res CGovView::SetStoredVariables(const std::set<std::shared_ptr<GovVariable>> &govVars, const uint32_t height) {
    for (auto &item : govVars) {
        auto res = WriteBy<ByHeightVars>(GovVarKey{height, item->GetName()}, *item);
        if (!res) {
            return DeFiErrors::GovVarFailedWrite();
        }
    }

    return Res::Ok();
}

std::set<std::shared_ptr<GovVariable>> CGovView::GetStoredVariables(const uint32_t height) {
    // Populate a set of Gov vars for specified height
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

std::vector<std::pair<uint32_t, std::shared_ptr<GovVariable>>> CGovView::GetStoredVariablesRange(
    const uint32_t startHeight,
    const uint32_t endHeight) {
    // Populate a set of Gov vars for specified height
    std::vector<std::pair<uint32_t, std::shared_ptr<GovVariable>>> govVars;
    auto it = LowerBound<ByHeightVars>(GovVarKey{startHeight, {}});
    for (; it.Valid() && it.Key().height >= startHeight && it.Key().height <= endHeight; it.Next()) {
        auto var = GovVariable::Create(it.Key().name);
        if (var) {
            it.Value(*var);
            govVars.emplace_back(it.Key().height, var);
        }
    }
    return govVars;
}

std::map<std::string, std::map<uint64_t, std::shared_ptr<GovVariable>>> CGovView::GetAllStoredVariables() {
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

void CGovView::EraseStoredVariables(const uint32_t height) {
    // Retrieve map of vars at specified height
    const auto vars = GetStoredVariables(height);

    // Iterate over names at this height and erase
    for (const auto &var : vars) {
        EraseBy<ByHeightVars>(GovVarKey{height, var->GetName()});
    }
}

std::shared_ptr<ATTRIBUTES> CGovView::GetAttributesFromStore() const {
    const auto var = GetVariable("ATTRIBUTES");
    assert(var);
    auto attr = std::dynamic_pointer_cast<ATTRIBUTES>(var);
    assert(attr);
    return attr;
}
