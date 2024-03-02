// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/consensus/governance.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

Res CGovernanceConsensus::operator()(const CGovernanceMessage &obj) const {
    // check foundation auth
    if (auto res = HasFoundationAuth(); !res) {
        return res;
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    for (const auto &[name, var] : obj.govs) {
        if (!var) {
            return Res::Err("'%s': variable does not registered", name);
        }

        Res res{};

        if (var->GetName() == "ATTRIBUTES") {
            // Add to existing ATTRIBUTES instead of overwriting.
            mnview.SetAttributesMembers(time, blockCtx.GetEVMTemplate());

            auto newVar = std::dynamic_pointer_cast<ATTRIBUTES>(var);
            if (!newVar) {
                return Res::Err("Failed to cast Gov var to ATTRIBUTES");
            }

            if (height >= static_cast<uint32_t>(consensus.DF22MetachainHeight)) {
                res = newVar->CheckKeys();
                if (!res) {
                    return res;
                }

                const auto newExport = newVar->Export();
                if (newExport.empty()) {
                    return Res::Err("Cannot export empty attribute map");
                }
            }

            CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members};
            auto memberRemoval = newVar->GetValue(key, std::set<std::string>{});

            if (!memberRemoval.empty()) {
                auto existingMembers = mnview.GetValue(key, std::set<CScript>{});

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

                mnview.SetValue(key, existingMembers);

                // Remove this key and apply any other changes
                newVar->EraseKey(key);
                if (!(res = mnview.ImportAttributes(newVar->Export())) || !(res = mnview.ValidateAttributes()) ||
                    !(res = mnview.ApplyAttributes(height))) {
                    return Res::Err("%s: %s", var->GetName(), res.msg);
                }
            } else {
                // Validate as complete set. Check for future conflicts between key pairs.
                if (!(res = mnview.ImportAttributes(var->Export())) || !(res = mnview.ValidateAttributes()) ||
                    !(res = mnview.ApplyAttributes(height))) {
                    return Res::Err("%s: %s", var->GetName(), res.msg);
                }
            }
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
                    StoreGovVars({name, var, height + mnview.GetIntervalBlock() - diff}, mnview);
                    continue;
                }
            }

            res = var->Apply(mnview, height);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }

            res = mnview.SetVariable(*var);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }
        }
    }
    return Res::Ok();
}

Res CGovernanceConsensus::operator()(const CGovernanceUnsetMessage &obj) const {
    // check foundation auth
    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member");
    }

    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovUnset};
    if (!mnview.GetValue(key, false)) {
        return Res::Err("Unset Gov variables not currently enabled in attributes.");
    }

    for (const auto &[name, keys] : obj.govs) {
        auto var = mnview.GetVariable(name);
        if (!var) {
            return Res::Err("'%s': variable does not registered", name);
        }

        Res res = Res::Ok();
        if (var->GetName() == "ATTRIBUTES") {
            res = mnview.EraseAttributes(height, keys);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }
        } else {
            res = var->Erase(mnview, height, keys);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }
            if (!(res = mnview.SetVariable(*var))) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }
        }
    }
    return Res::Ok();
}

Res CGovernanceConsensus::operator()(const CGovernanceHeightMessage &obj) const {
    // check foundation auth
    if (!HasFoundationAuth()) {
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
        }

        Res res{};
        CCustomCSView discardCache(mnview);
        auto storedGovVars = discardCache.GetStoredVariablesRange(height, obj.startHeight);

        for (const auto &[varHeight, var] : storedGovVars) {
            if (var->GetName() == "ATTRIBUTES") {
                if (res = discardCache.ImportAttributes(var->Export()); !res) {
                    return Res::Err("%s: Failed to import stored vars: %s", obj.govVar->GetName(), res.msg);
                }
            }
        }

        // After GW exclude TokenSplit if split will have already been performed by startHeight
        if (height >= static_cast<uint32_t>(consensus.DF20GrandCentralHeight)) {
            std::vector<CDataStructureV0> keysToErase;
            discardCache.ForEachAttribute(
                [&](const CDataStructureV0 &attr, const CAttributeValue &value) {
                    if (attr.type != AttributeTypes::Oracles) {
                        return false;
                    }

                    if (attr.typeId == OracleIDs::Splits && attr.key < obj.startHeight) {
                        keysToErase.push_back(attr);
                    }

                    return true;
                },
                CDataStructureV0{AttributeTypes::Oracles});

            for (const auto &key : keysToErase) {
                discardCache.EraseKey(key);
            }
        }

        if (!(res = discardCache.ImportAttributes(obj.govVar->Export())) ||
            !(res = discardCache.ValidateAttributes()) || !(res = discardCache.ApplyAttributes(obj.startHeight))) {
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
