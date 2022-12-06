// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/attributes.h>
#include <masternodes/consensus/governance.h>
#include <masternodes/masternodes.h>

Res CGovernanceConsensus::operator()(const CGovernanceMessage &obj) const {
    // check foundation auth
    Require(HasFoundationAuth());
    for (const auto &gov : obj.govs) {
        if (!gov.second) {
            return Res::Err("'%s': variable does not registered", gov.first);
        }

        auto var = gov.second;
        Res res{};

        if (var->GetName() == "ATTRIBUTES") {
            // Add to existing ATTRIBUTES instead of overwriting.
            auto govVar = mnview.GetAttributes();

            if (!govVar) {
                return Res::Err("%s: %s", var->GetName(), "Failed to get existing ATTRIBUTES");
            }

            govVar->time = time;

            auto newVar = std::dynamic_pointer_cast<ATTRIBUTES>(var);
            assert(newVar);

            CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members};
            auto memberRemoval = newVar->GetValue(key, std::set<std::string>{});

            if (!memberRemoval.empty()) {
                auto existingMembers = govVar->GetValue(key, std::set<CScript>{});

                for (auto &member : memberRemoval) {
                    if (member.empty()) {
                        return Res::Err("Invalid address provided");
                    }

                    if (member[0] == '-') {
                        auto memberCopy{member};
                        const auto dest = DecodeDestination(memberCopy.erase(0, 1));
                        if (!IsValidDestination(dest)) {
                            return Res::Err("Invalid address provided");
                        }
                        CScript removeMember = GetScriptForDestination(dest);
                        if (!existingMembers.count(removeMember)) {
                            return Res::Err("Member to remove not present");
                        }
                        existingMembers.erase(removeMember);
                    } else {
                        const auto dest = DecodeDestination(member);
                        if (!IsValidDestination(dest)) {
                            return Res::Err("Invalid address provided");
                        }
                        CScript addMember = GetScriptForDestination(dest);
                        if (existingMembers.count(addMember)) {
                            return Res::Err("Member to add already present");
                        }
                        existingMembers.insert(addMember);
                    }
                }

                govVar->SetValue(key, existingMembers);

                // Remove this key and apply any other changes
                newVar->EraseKey(key);
                if (!(res = govVar->Import(newVar->Export())) || !(res = govVar->Validate(mnview)) ||
                    !(res = govVar->Apply(mnview, height)))
                    return Res::Err("%s: %s", var->GetName(), res.msg);
            } else {
                // Validate as complete set. Check for future conflicts between key pairs.
                if (!(res = govVar->Import(var->Export())) || !(res = govVar->Validate(mnview)) ||
                    !(res = govVar->Apply(mnview, height)))
                    return Res::Err("%s: %s", var->GetName(), res.msg);
            }

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
    // check foundation auth
    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member");

    const auto attributes = mnview.GetAttributes();
    assert(attributes);
    CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovUnset};
    if (!attributes->GetValue(key, false)) {
        return Res::Err("Unset Gov variables not currently enabled in attributes.");
    }

    for (const auto &gov : obj.govs) {
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

Res CGovernanceConsensus::operator()(const CGovernanceHeightMessage &obj) const {
    // check foundation auth
    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member");
    }
    if (obj.startHeight <= height) {
        return Res::Err("startHeight must be above the current block height");
    }

    if (obj.govVar->GetName() == "ORACLE_BLOCK_INTERVAL") {
        return Res::Err("%s: %s", obj.govVar->GetName(), "Cannot set via setgovheight.");
    }

    // Validate GovVariables before storing
    if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) &&
        obj.govVar->GetName() == "ATTRIBUTES") {
        auto govVar = mnview.GetAttributes();
        if (!govVar) {
            return Res::Err("%s: %s", obj.govVar->GetName(), "Failed to get existing ATTRIBUTES");
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
        if (height >= static_cast<uint32_t>(consensus.GrandCentralHeight)) {
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
        if (!result)
            return Res::Err("%s: %s", obj.govVar->GetName(), result.msg);
    }

    // Store pending Gov var change
    StoreGovVars(obj, mnview);

    return Res::Ok();
}
