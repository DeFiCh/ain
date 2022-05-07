// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/consensus/governance.h>

#include <masternodes/govvariables/attributes.h>
#include <masternodes/gv.h>
#include <masternodes/masternodes.h>

Res CGovernanceConsensus::storeGovVars(const CGovernanceHeightMessage& obj) const {

    // Retrieve any stored GovVariables at startHeight
    auto storedGovVars = mnview.GetStoredVariables(obj.startHeight);

    // Remove any pre-existing entry
    for (auto it = storedGovVars.begin(); it != storedGovVars.end();) {
        if ((*it)->GetName() == obj.govName)
            it = storedGovVars.erase(it);
        else
            ++it;
    }

    // Add GovVariable to set for storage
    storedGovVars.insert(obj.govVar);

    // Store GovVariable set by height
    return mnview.SetStoredVariables(storedGovVars, obj.startHeight);
}

Res CGovernanceConsensus::operator()(const CGovernanceMessage& obj) const {
    //check foundation auth
    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member");

    for(const auto& gov : obj.govs) {
        if (!gov.second)
            return Res::Err("'%s': variable does not registered", gov.first);

        auto var = gov.second;
        Res res{};

        if (var->GetName() == "ATTRIBUTES") {
            // Add to existing ATTRIBUTES instead of overwriting.
            auto govVar = mnview.GetAttributes();

            if (!govVar) {
                return Res::Err("%s: %s", var->GetName(), "Failed to get existing ATTRIBUTES");
            }

            govVar->time = time;

            // Validate as complete set. Check for future conflicts between key pairs.
            if (!(res = govVar->Import(var->Export()))
            ||  !(res = govVar->Validate(mnview))
            ||  !(res = govVar->Apply(mnview, futureSwapView, height)))
                return Res::Err("%s: %s", var->GetName(), res.msg);

            var = govVar;
        } else {
            // After GW, some ATTRIBUTES changes require the context of its map to validate,
            // moving this Validate() call to else statement from before this conditional.
            res = var->Validate(mnview);
            if (!res)
                return Res::Err("%s: %s", var->GetName(), res.msg);

            if (var->GetName() == "ORACLE_BLOCK_INTERVAL") {
                // Make sure ORACLE_BLOCK_INTERVAL only updates at end of interval
                const auto diff = height % mnview.GetIntervalBlock();
                if (diff != 0) {
                    // Store as pending change
                    storeGovVars({gov.first, var, height + mnview.GetIntervalBlock() - diff});
                    continue;
                }
            }

            res = var->Apply(mnview, height);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }
        }

        res = mnview.SetVariable(*var);
        if (!res) {
            return Res::Err("%s: %s", var->GetName(), res.msg);
        }
    }
    return Res::Ok();
}

Res CGovernanceConsensus::operator()(const CGovernanceHeightMessage& obj) const {
    //check foundation auth
    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member");

    if (!obj.govVar)
        return Res::Err("'%s': variable does not registered", obj.govName);

    if (obj.startHeight <= height)
        return Res::Err("startHeight must be above the current block height");

    if (obj.govVar->GetName() == "ORACLE_BLOCK_INTERVAL")
        return Res::Err("%s: %s", obj.govVar->GetName(), "Cannot set via setgovheight.");

    // Validate GovVariables before storing
    auto result = obj.govVar->Validate(mnview);
    if (!result)
        return Res::Err("%s: %s", obj.govVar->GetName(), result.msg);

    // Store pending Gov var change
    return storeGovVars(obj);
}
