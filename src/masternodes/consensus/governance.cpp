// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/consensus/governance.h>

#include <masternodes/govvariables/attributes.h>
#include <masternodes/gv.h>
#include <masternodes/masternodes.h>

Res CGovernanceConsensus::storeGovVars(const CGovernanceHeightMessage& obj, CCustomCSView& view) {

    // Retrieve any stored GovVariables at startHeight
    auto storedGovVars = view.GetStoredVariables(obj.startHeight);

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
    return view.SetStoredVariables(storedGovVars, obj.startHeight);
}

Res CGovernanceConsensus::operator()(const CGovernanceMessage& obj) const {
    //check foundation auth
    Require(HasFoundationAuth());

    for(auto [name, var] : obj.govs) {
        Require(var, "'%s': variable does not registered", name);

        auto errHandler = [name = name](const std::string& msg) {
            return strprintf("%s: %s", name, msg);
        };

        if (var->GetName() == "ATTRIBUTES") {
            // Add to existing ATTRIBUTES instead of overwriting.
            auto govVar = mnview.GetAttributes();
            Require(govVar, errHandler("Failed to get existing ATTRIBUTES"));

            govVar->time = time;

            // Validate as complete set. Check for future conflicts between key pairs.
            Require(govVar->Import(var->Export()), errHandler);
            Require(govVar->Validate(mnview), errHandler);
            Require(govVar->Apply(mnview, futureSwapView, height), errHandler);

            var = govVar;
        } else {
            // After GW, some ATTRIBUTES changes require the context of its map to validate,
            // moving this Validate() call to else statement from before this conditional.
            Require(var->Validate(mnview), errHandler);

            if (var->GetName() == "ORACLE_BLOCK_INTERVAL") {
                // Make sure ORACLE_BLOCK_INTERVAL only updates at end of interval
                const auto diff = height % mnview.GetIntervalBlock();
                if (diff != 0) {
                    // Store as pending change
                    storeGovVars({name, var, height + mnview.GetIntervalBlock() - diff}, mnview);
                    continue;
                }
            }

            Require(var->Apply(mnview, height), errHandler);
        }

        Require(mnview.SetVariable(*var), errHandler);
    }
    return Res::Ok();
}

Res CGovernanceConsensus::operator()(const CGovernanceUnsetMessage& obj) const {
    //check foundation auth
    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member");

    for(const auto& gov : obj.govs) {
        auto var = mnview.GetVariable(gov.first);
        if (!var)
            return Res::Err("'%s': variable does not registered", gov.first);

        auto res = var->Erase(mnview, height, gov.second);
        if (!res)
            return Res::Err("%s: %s", var->GetName(), res.msg);

        if (!(res = mnview.SetVariable(*var)))
            return Res::Err("%s: %s", var->GetName(), res.msg);
    }
    return Res::Ok();
}

Res CGovernanceConsensus::operator()(const CGovernanceHeightMessage& obj) const {
    //check foundation auth
    Require(HasFoundationAuth());
    Require(obj.govVar, "'%s': variable does not registered", obj.govName);
    Require(obj.startHeight > height, "startHeight must be above the current block height");
    Require(obj.govVar->GetName() != "ORACLE_BLOCK_INTERVAL", "%s: Cannot set via setgovheight.", obj.govVar->GetName());

    auto errHandler = [&](const std::string& msg) {
        return strprintf("%s: %s", obj.govVar->GetName(), msg);
    };

    // Validate GovVariables before storing
    // TODO remove GW check after fork height. No conflict expected as attrs should not been set by height before.
    if (height >= uint32_t(consensus.GreatWorldHeight) && obj.govVar->GetName() == "ATTRIBUTES") {

        auto govVar = mnview.GetAttributes();
        if (!govVar) {
            return Res::Err("%s: %s", obj.govVar->GetName(), "Failed to get existing ATTRIBUTES");
        }

        auto storedGovVars = mnview.GetStoredVariablesRange(height, obj.startHeight);
        storedGovVars.emplace_back(obj.startHeight, obj.govVar);

        Res res{};
        CCustomCSView govCache(mnview);
        CFutureSwapView futureSwapCache(futureSwapView);
        for (const auto& [varHeight, var] : storedGovVars) {
            if (var->GetName() == "ATTRIBUTES") {
                if (!(res = govVar->Import(var->Export())) ||
                    !(res = govVar->Validate(govCache)) ||
                    !(res = govVar->Apply(govCache, futureSwapCache, varHeight))) {
                    return Res::Err("%s: Cumulative application of Gov vars failed: %s", obj.govVar->GetName(), res.msg);
                }
            }
        }
    } else {
        Require(obj.govVar->Validate(mnview), errHandler);
    }

    // Store pending Gov var change
    return storeGovVars(obj, mnview);
}
