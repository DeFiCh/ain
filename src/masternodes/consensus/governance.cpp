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
                    storeGovVars({name, var, height + mnview.GetIntervalBlock() - diff});
                    continue;
                }
            }
        }

        Require(var->Apply(mnview, height), errHandler);
        Require(mnview.SetVariable(*var), errHandler);
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
    Require(obj.govVar->Validate(mnview), errHandler);

    // Store pending Gov var change
    return storeGovVars(obj);
}
