// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/consensus/governance.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

Res CGovernanceConsensus::operator()(const CGovernanceMessage &obj) const {
    // Check foundation auth
    const auto foundationAuth = HasFoundationAuth();
    const auto governanceAuth = HasGovernanceAuth();
    if (!governanceAuth && !foundationAuth) {
        return Res::Err("tx not from foundation member");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    for (const auto &gov : obj.govs) {
        if (!gov.second) {
            return Res::Err("'%s': variable does not registered", gov.first);
        }

        auto var = gov.second;
        Res res{};

        if (var->GetName() == "ATTRIBUTES") {
            // Add to existing ATTRIBUTES instead of overwriting.
            auto govVar = mnview.GetAttributes();

            govVar->time = time;
            govVar->evmTemplate = blockCtx.GetEVMTemplate();

            auto newVar = std::dynamic_pointer_cast<ATTRIBUTES>(var);
            if (!newVar) {
                return Res::Err("Failed to cast Gov var to ATTRIBUTES");
            }

            CDataStructureV0 foundationMembers{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members};

            if (height >= static_cast<uint32_t>(consensus.DF22MetachainHeight)) {
                res = newVar->CheckKeys();
                if (!res) {
                    return res;
                }

                const auto newExport = newVar->Export();
                if (newExport.empty()) {
                    return Res::Err("Cannot export empty attribute map");
                }

                if (governanceAuth && !foundationAuth) {
                    CDataStructureV0 foundationParam{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovFoundation};
                    if (newVar->CheckPartialKey(foundationMembers.type, foundationMembers.typeId) ||
                        newVar->CheckKey(foundationParam)) {
                        return Res::Err("Foundation cannot be modified by governance");
                    }
                }
            }

            res = GovernanceMemberRemoval(*newVar, *govVar, foundationMembers);
            if (!res) {
                return res;
            }

            CDataStructureV0 governanceMembers{AttributeTypes::Param, ParamIDs::GovernanceParam, DFIPKeys::Members};
            res = GovernanceMemberRemoval(*newVar, *govVar, governanceMembers);
            if (!res) {
                return res;
            }

            // Validate as complete set. Check for future conflicts between key pairs.
            if (!(res = govVar->Import(newVar->Export())) || !(res = govVar->Validate(mnview)) ||
                !(res = govVar->Apply(mnview, height))) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }

            var = govVar;
        } else {
            // After GW, some ATTRIBUTES changes require the context of its map to validate,
            // moving this Validate() call to else statement from before this conditional.
            res = var->Validate(mnview);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }

            if (var->GetName() == "ORACLE_BLOCK_INTERVAL") {
                // Make sure ORACLE_BLOCK_INTERVAL only updates at end of interval
                const auto diff = height % mnview.GetIntervalBlock();
                if (diff != 0) {
                    // Store as pending change
                    StoreGovVars({gov.first, var, height + mnview.GetIntervalBlock() - diff}, mnview);
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

Res CGovernanceConsensus::operator()(const CGovernanceUnsetMessage &obj) const {
    // Check foundation auth
    const auto foundationAuth = HasFoundationAuth();
    const auto governanceAuth = HasGovernanceAuth();
    if (!governanceAuth && !foundationAuth) {
        return Res::Err("tx not from foundation member");
    }

    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();
    const auto attributes = mnview.GetAttributes();

    CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovUnset};
    if (!attributes->GetValue(key, false)) {
        return Res::Err("Unset Gov variables not currently enabled in attributes.");
    }

    for (const auto &[name, keys] : obj.govs) {
        if (name == "ATTRIBUTES" && !foundationAuth && governanceAuth) {
            bool error{};
            for (const auto &key : keys) {
                ATTRIBUTES::ProcessVariable(key, std::nullopt, [&](const auto &attribute, const auto &) {
                    if (const auto attrV0 = std::get_if<CDataStructureV0>(&attribute)) {
                        if ((attrV0->type == AttributeTypes::Param && attrV0->typeId == ParamIDs::Foundation &&
                             attrV0->key == DFIPKeys::Members) ||
                            (attrV0->type == AttributeTypes::Param && attrV0->typeId == ParamIDs::Feature &&
                             attrV0->key == DFIPKeys::GovFoundation)) {
                            error = true;
                        }
                    }
                    return Res::Ok();
                });
            }
            if (error) {
                return Res::Err("Foundation cannot be modified by governance");
            }
        }

        auto var = mnview.GetVariable(name);
        if (!var) {
            return Res::Err("'%s': variable does not registered", name);
        }

        auto res = var->Erase(mnview, height, keys);
        if (!res) {
            return Res::Err("%s: %s", name, res.msg);
        }

        if (!(res = mnview.SetVariable(*var))) {
            return Res::Err("%s: %s", name, res.msg);
        }
    }
    return Res::Ok();
}

Res CGovernanceConsensus::operator()(const CGovernanceHeightMessage &obj) const {
    // Check foundation auth
    const auto foundationAuth = HasFoundationAuth();
    const auto governanceAuth = HasGovernanceAuth();
    if (!governanceAuth && !foundationAuth) {
        return Res::Err("tx not from foundation member");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    if (obj.startHeight <= height) {
        return Res::Err("startHeight must be above the current block height");
    }

    if (obj.govVar->GetName() == "ORACLE_BLOCK_INTERVAL") {
        return Res::Err("%s: %s", obj.govVar->GetName(), "Cannot set via setgovheight.");
    }

    // Validate GovVariables before storing
    if (height >= static_cast<uint32_t>(consensus.DF16FortCanningCrunchHeight) &&
        obj.govVar->GetName() == "ATTRIBUTES") {
        auto govVar = mnview.GetAttributes();

        if (height >= static_cast<uint32_t>(consensus.DF22MetachainHeight)) {
            auto newVar = std::dynamic_pointer_cast<ATTRIBUTES>(obj.govVar);
            if (!newVar) {
                return Res::Err("Failed to cast Gov var to ATTRIBUTES");
            }

            auto res = newVar->CheckKeys();
            if (!res) {
                return res;
            }

            const auto newExport = newVar->Export();
            if (newExport.empty()) {
                return Res::Err("Cannot export empty attribute map");
            }

            if (governanceAuth && !foundationAuth) {
                CDataStructureV0 foundationParam{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovFoundation};
                if (newVar->CheckPartialKey(AttributeTypes::Param, ParamIDs::Foundation) ||
                    newVar->CheckKey(foundationParam)) {
                    return Res::Err("Foundation cannot be modified by governance");
                }
            }
        }

        auto storedGovVars = mnview.GetStoredVariablesRange(height, obj.startHeight);

        Res res{};
        CCustomCSView govCache(mnview);
        for (const auto &[varHeight, var] : storedGovVars) {
            if (var->GetName() == "ATTRIBUTES") {
                if (res = govVar->Import(var->Export()); !res) {
                    return Res::Err("%s: Failed to import stored vars: %s", obj.govVar->GetName(), res.msg);
                }
            }
        }

        // After GW exclude TokenSplit if split will have already been performed by startHeight
        if (height >= static_cast<uint32_t>(consensus.DF20GrandCentralHeight)) {
            if (const auto attrVar = std::dynamic_pointer_cast<ATTRIBUTES>(govVar); attrVar) {
                const auto attrMap = attrVar->GetAttributesMap();
                std::vector<CDataStructureV0> keysToErase;
                for (const auto &[key, value] : attrMap) {
                    if (const auto attrV0 = std::get_if<CDataStructureV0>(&key); attrV0) {
                        if (attrV0->type == AttributeTypes::Oracles && attrV0->typeId == OracleIDs::Splits &&
                            attrV0->key < obj.startHeight) {
                            keysToErase.push_back(*attrV0);
                        }
                    }
                }
                for (const auto &key : keysToErase) {
                    attrVar->EraseKey(key);
                }
            }
        }

        if (!(res = govVar->Import(obj.govVar->Export())) || !(res = govVar->Validate(govCache)) ||
            !(res = govVar->Apply(govCache, obj.startHeight))) {
            return Res::Err("%s: Cumulative application of Gov vars failed: %s", obj.govVar->GetName(), res.msg);
        }
    } else {
        auto result = obj.govVar->Validate(mnview);
        if (!result) {
            return Res::Err("%s: %s", obj.govVar->GetName(), result.msg);
        }
    }

    // Store pending Gov var change
    StoreGovVars(obj, mnview);

    return Res::Ok();
}
