// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <consensus/params.h>
#include <masternodes/consensus/masternodes.h>
#include <masternodes/customtx.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/masternodes.h>

Res CMasternodesConsensus::CheckMasternodeCreationTx() const {
    Require(tx.vout.size() >= 2 && tx.vout[0].nValue >= GetMnCreationFee(height) && tx.vout[0].nTokenId == DCT_ID{0} &&
            tx.vout[1].nValue == GetMnCollateralAmount(height) && tx.vout[1].nTokenId == DCT_ID{0},
            "malformed tx vouts (wrong creation fee or collateral amount)");

    return Res::Ok();
}

Res CMasternodesConsensus::operator()(const CCreateMasterNodeMessage &obj) const {
    Require(CheckMasternodeCreationTx());

    if (height >= static_cast<uint32_t>(consensus.EunosHeight))
        Require(HasAuth(tx.vout[1].scriptPubKey), "masternode creation needs owner auth");

    if (height >= static_cast<uint32_t>(consensus.EunosPayaHeight)) {
        switch (obj.timelock) {
            case CMasternode::ZEROYEAR:
            case CMasternode::FIVEYEAR:
            case CMasternode::TENYEAR:
                break;
            default:
                return Res::Err("Timelock must be set to either 0, 5 or 10 years");
        }
    } else
        Require(obj.timelock == 0, "collateral timelock cannot be set below EunosPaya");

    CMasternode node;
    CTxDestination dest;
    if (ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
        if (dest.index() == PKHashType) {
            node.ownerType        = 1;
            node.ownerAuthAddress = CKeyID(std::get<PKHash>(dest));
        } else if (dest.index() == WitV0KeyHashType) {
            node.ownerType        = 4;
            node.ownerAuthAddress = CKeyID(std::get<WitnessV0KeyHash>(dest));
        }
    }
    node.creationHeight      = height;
    node.operatorType        = obj.operatorType;
    node.operatorAuthAddress = obj.operatorAuthAddress;

    // Set masternode version2 after FC for new serialisation
    if (height >= static_cast<uint32_t>(consensus.FortCanningHeight))
        node.version = CMasternode::VERSION0;

    bool duplicate{};
    mnview.ForEachNewCollateral([&](const uint256 &key, CLazySerialize<MNNewOwnerHeightValue> valueKey) {
        const auto &value = valueKey.get();
        if (height > value.blockHeight) {
            return true;
        }
        const auto &coin = coins.AccessCoin({key, 1});
        assert(!coin.IsSpent());
        CTxDestination pendingDest;
        assert(ExtractDestination(coin.out.scriptPubKey, pendingDest));
        const CKeyID storedID = pendingDest.index() == PKHashType ? CKeyID(std::get<PKHash>(pendingDest))
                                                                  : CKeyID(std::get<WitnessV0KeyHash>(pendingDest));
        if (storedID == node.ownerAuthAddress || storedID == node.operatorAuthAddress) {
            duplicate = true;
            return false;
        }
        return true;
    });

    if (duplicate) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "Masternode exist with that owner address pending");
    }

    Require(mnview.CreateMasternode(tx.GetHash(), node, obj.timelock));
    // Build coinage from the point of masternode creation

    if (height >= static_cast<uint32_t>(consensus.EunosPayaHeight))
        for (uint8_t i{0}; i < SUBNODE_COUNT; ++i)
            mnview.SetSubNodesBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), i, time);

    else if (height >= static_cast<uint32_t>(consensus.DakotaCrescentHeight))
        mnview.SetMasternodeLastBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), time);

    return Res::Ok();
}

Res CMasternodesConsensus::operator()(const CResignMasterNodeMessage &obj) const {
    auto node = mnview.GetMasternode(obj);
    Require(node, "node %s does not exists", obj.ToString());

    Require(HasCollateralAuth(node->collateralTx.IsNull() ? static_cast<uint256>(obj) : node->collateralTx));
    return mnview.ResignMasternode(*node, obj, tx.GetHash(), height);
}

Res CMasternodesConsensus::operator()(const CUpdateMasterNodeMessage &obj) const {
    if (obj.updates.empty()) {
        return Res::Err("No update arguments provided");
    }

    if (obj.updates.size() > 3) {
        return Res::Err("Too many updates provided");
    }

    auto node = mnview.GetMasternode(obj.mnId);
    Require(node, "masternode %s does not exist", obj.mnId.ToString());

    const auto collateralTx = node->collateralTx.IsNull() ? obj.mnId : node->collateralTx;
    Require(HasCollateralAuth(collateralTx));

    auto state = node->GetState(height, mnview);
    Require(state == CMasternode::ENABLED, "Masternode %s is not in 'ENABLED' state", obj.mnId.ToString());

    const auto attributes = mnview.GetAttributes();
    assert(attributes);

    bool ownerType{}, operatorType{}, rewardType{};
    for (const auto &[type, addressPair] : obj.updates) {
        const auto &[addressType, rawAddress] = addressPair;
        if (type == static_cast<uint8_t>(UpdateMasternodeType::OwnerAddress)) {
            CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MNSetOwnerAddress};
            if (!attributes->GetValue(key, false)) {
                return Res::Err("Updating masternode owner address not currently enabled in attributes.");
            }
            if (ownerType) {
                return Res::Err("Multiple owner updates provided");
            }
            ownerType = true;
            bool collateralFound{};
            for (const auto &vin : tx.vin) {
                if (vin.prevout.hash == collateralTx && vin.prevout.n == 1) {
                    collateralFound = true;
                }
            }
            if (!collateralFound) {
                return Res::Err("Missing previous collateral from transaction inputs");
            }
            if (tx.vout.size() == 1) {
                return Res::Err("Missing new collateral output");
            }

            CTxDestination dest;
            if (!ExtractDestination(tx.vout[1].scriptPubKey, dest) ||
                (dest.index() != PKHashType && dest.index() != WitV0KeyHashType)) {
                return Res::Err("Owner address must be P2PKH or P2WPKH type");
            }

            if (tx.vout[1].nValue != GetMnCollateralAmount(height)) {
                return Res::Err("Incorrect collateral amount");
            }

            const auto keyID = dest.index() == PKHashType ? CKeyID(std::get<PKHash>(dest))
                                                          : CKeyID(std::get<WitnessV0KeyHash>(dest));
            if (mnview.GetMasternodeIdByOwner(keyID) || mnview.GetMasternodeIdByOperator(keyID)) {
                return Res::Err("Masternode with collateral address as operator or owner already exists");
            }

            bool duplicate{};
            mnview.ForEachNewCollateral([&](const uint256 &key, CLazySerialize<MNNewOwnerHeightValue> valueKey) {
                const auto &value = valueKey.get();
                if (height > value.blockHeight) {
                    return true;
                }
                const auto &coin = coins.AccessCoin({key, 1});
                assert(!coin.IsSpent());
                CTxDestination pendingDest;
                assert(ExtractDestination(coin.out.scriptPubKey, pendingDest));
                const CKeyID storedID = pendingDest.index() == PKHashType
                                        ? CKeyID(std::get<PKHash>(pendingDest))
                                        : CKeyID(std::get<WitnessV0KeyHash>(pendingDest));
                if (storedID == keyID) {
                    duplicate = true;
                    return false;
                }
                return true;
            });
            if (duplicate) {
                return Res::ErrCode(CustomTxErrCodes::Fatal,
                                    "Masternode exist with that owner address pending already");
            }

            mnview.UpdateMasternodeCollateral(obj.mnId, *node, tx.GetHash(), height);
        } else if (type == static_cast<uint8_t>(UpdateMasternodeType::OperatorAddress)) {
            CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MNSetOperatorAddress};
            if (!attributes->GetValue(key, false)) {
                return Res::Err("Updating masternode operator address not currently enabled in attributes.");
            }
            if (operatorType) {
                return Res::Err("Multiple operator updates provided");
            }
            operatorType = true;

            if (addressType != 1 && addressType != 4) {
                return Res::Err("Operator address must be P2PKH or P2WPKH type");
            }

            const auto keyID = CKeyID(uint160(rawAddress));
            if (mnview.GetMasternodeIdByOwner(keyID) || mnview.GetMasternodeIdByOperator(keyID)) {
                return Res::Err("Masternode with that operator address already exists");
            }
            mnview.UpdateMasternodeOperator(obj.mnId, *node, addressType, keyID, height);
        } else if (type == static_cast<uint8_t>(UpdateMasternodeType::SetRewardAddress)) {
            CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MNSetRewardAddress};
            if (!attributes->GetValue(key, false)) {
                return Res::Err("Updating masternode reward address not currently enabled in attributes.");
            }
            if (rewardType) {
                return Res::Err("Multiple reward address updates provided");
            }
            rewardType = true;

            if (addressType != 1 && addressType != 4) {
                return Res::Err("Reward address must be P2PKH or P2WPKH type");
            }

            const auto keyID = CKeyID(uint160(rawAddress));
            mnview.SetForcedRewardAddress(obj.mnId, *node, addressType, keyID, height);
        } else if (type == static_cast<uint8_t>(UpdateMasternodeType::RemRewardAddress)) {
            CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MNSetRewardAddress};
            if (!attributes->GetValue(key, false)) {
                return Res::Err("Updating masternode reward address not currently enabled in attributes.");
            }
            if (rewardType) {
                return Res::Err("Multiple reward address updates provided");
            }
            rewardType = true;

            mnview.RemForcedRewardAddress(obj.mnId, *node, height);
        } else {
            return Res::Err("Unknown update type provided");
        }
    }

    return Res::Ok();
}
