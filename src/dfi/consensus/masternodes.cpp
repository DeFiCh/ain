// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <consensus/params.h>
#include <dfi/consensus/masternodes.h>
#include <dfi/customtx.h>
#include <dfi/errors.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

Res CMasternodesConsensus::CheckMasternodeCreationTx() const {
    const auto height = txCtx.GetHeight();
    const auto &tx = txCtx.GetTransaction();

    if (tx.vout.size() < 2 || tx.vout[0].nValue < GetMnCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0} ||
        tx.vout[1].nValue != GetMnCollateralAmount(height) || tx.vout[1].nTokenId != DCT_ID{0}) {
        return Res::Err("malformed tx vouts (wrong creation fee or collateral amount)");
    }

    return Res::Ok();
}

Res CMasternodesConsensus::operator()(const CCreateMasterNodeMessage &obj) const {
    if (auto res = CheckMasternodeCreationTx(); !res) {
        return res;
    }

    const auto &coins = txCtx.GetCoins();
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    if (height >= static_cast<uint32_t>(consensus.DF8EunosHeight)) {
        if (!HasAuth(tx.vout[1].scriptPubKey)) {
            return Res::Err("masternode creation needs owner auth");
        }
    }

    if (height >= static_cast<uint32_t>(consensus.DF10EunosPayaHeight)) {
        switch (obj.timelock) {
            case CMasternode::ZEROYEAR:
            case CMasternode::FIVEYEAR:
            case CMasternode::TENYEAR:
                break;
            default:
                return Res::Err("Timelock must be set to either 0, 5 or 10 years");
        }
    } else {
        if (obj.timelock != 0) {
            return Res::Err("collateral timelock cannot be set below EunosPaya");
        }
    }

    CMasternode node;
    CTxDestination dest;
    if (ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
        if (dest.index() == PKHashType) {
            node.ownerType = 1;
            node.ownerAuthAddress = CKeyID(std::get<PKHash>(dest));
        } else if (dest.index() == WitV0KeyHashType) {
            node.ownerType = 4;
            node.ownerAuthAddress = CKeyID(std::get<WitnessV0KeyHash>(dest));
        }
    }
    node.creationHeight = height;
    node.operatorType = obj.operatorType;
    node.operatorAuthAddress = obj.operatorAuthAddress;

    // Set masternode version2 after FC for new serialisation
    if (height >= static_cast<uint32_t>(consensus.DF11FortCanningHeight)) {
        node.version = CMasternode::VERSION0;
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
        const CKeyID storedID = CKeyID::FromOrDefaultDestination(pendingDest, KeyType::MNOwnerKeyType);
        if ((!storedID.IsNull()) && (storedID == node.ownerAuthAddress || storedID == node.operatorAuthAddress)) {
            duplicate = true;
            return false;
        }
        return true;
    });

    if (duplicate) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "Masternode exist with that owner address pending");
    }

    if (auto res = mnview.CreateMasternode(tx.GetHash(), node, obj.timelock); !res) {
        return res;
    }
    // Build coinage from the point of masternode creation

    if (height >= static_cast<uint32_t>(consensus.DF10EunosPayaHeight)) {
        for (uint8_t i{0}; i < SUBNODE_COUNT; ++i) {
            mnview.SetSubNodesBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), i, time);
        }
    }

    else if (height >= static_cast<uint32_t>(consensus.DF7DakotaCrescentHeight)) {
        mnview.SetMasternodeLastBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), time);
    }

    return Res::Ok();
}

Res CMasternodesConsensus::operator()(const CResignMasterNodeMessage &obj) const {
    const auto height = txCtx.GetHeight();
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    auto node = mnview.GetMasternode(obj);
    if (!node) {
        return DeFiErrors::MNInvalid(obj.ToString());
    }

    if (auto res = HasCollateralAuth(node->collateralTx.IsNull() ? static_cast<uint256>(obj) : node->collateralTx);
        !res) {
        return res;
    }
    return mnview.ResignMasternode(*node, obj, tx.GetHash(), height);
}

Res CMasternodesConsensus::operator()(const CUpdateMasterNodeMessage &obj) const {
    if (obj.updates.empty()) {
        return Res::Err("No update arguments provided");
    }

    if (obj.updates.size() > 3) {
        return Res::Err("Too many updates provided");
    }

    const auto &coins = txCtx.GetCoins();
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    auto node = mnview.GetMasternode(obj.mnId);
    if (!node) {
        return DeFiErrors::MNInvalidAltMsg(obj.mnId.ToString());
    }

    const auto collateralTx = node->collateralTx.IsNull() ? obj.mnId : node->collateralTx;
    if (auto res = HasCollateralAuth(collateralTx); !res) {
        return res;
    }

    auto state = node->GetState(height, mnview);
    if (state != CMasternode::ENABLED) {
        return DeFiErrors::MNStateNotEnabled(obj.mnId.ToString());
    }

    const auto attributes = mnview.GetAttributes();

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
            ExtractDestination(tx.vout[1].scriptPubKey, dest);
            const auto keyID = CKeyID::FromOrDefaultDestination(dest, KeyType::MNOwnerKeyType);
            if (keyID.IsNull()) {
                return Res::Err("Owner address must be P2PKH or P2WPKH type");
            }

            if (tx.vout[1].nValue != GetMnCollateralAmount(height)) {
                return Res::Err("Incorrect collateral amount. Found: %s Expected: %s",
                                GetDecimalString(tx.vout[1].nValue),
                                GetDecimalString(GetMnCollateralAmount(height)));
            }

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
                const CKeyID storedID = CKeyID::FromOrDefaultDestination(pendingDest, KeyType::MNOwnerKeyType);
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

            if (addressType != PKHashType && addressType != WitV0KeyHashType) {
                return Res::Err("Operator address must be P2PKH or P2WPKH type");
            }

            CKeyID keyID;
            try {
                keyID = CKeyID(uint160(rawAddress));
            } catch (...) {
                return Res::Err("Updating masternode operator address is invalid");
            }

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

            if (height < static_cast<uint32_t>(consensus.DF22MetachainHeight)) {
                if (addressType != PKHashType && addressType != WitV0KeyHashType) {
                    return Res::Err("Reward address must be P2PKH or P2WPKH type");
                }
            } else {
                if (addressType != PKHashType && addressType != ScriptHashType && addressType != WitV0KeyHashType) {
                    return Res::Err("Reward address must be P2SH, P2PKH or P2WPKH type");
                }
            }

            CKeyID keyID;
            try {
                keyID = CKeyID(uint160(rawAddress));
            } catch (...) {
                return Res::Err("Updating masternode reward address is invalid");
            }

            mnview.SetForcedRewardAddress(obj.mnId, *node, addressType, keyID, height);

            // Store history of all reward address changes. This allows us to call CalculateOwnerReward
            // on reward addresses owned by the local wallet. This can be removed some time after the
            // next hard fork as this is a workaround for the issue fixed in the following PR:
            // https://github.com/DeFiCh/ain/pull/1766
            if (auto addresses = mnview.SettingsGetRewardAddresses()) {
                const CScript rewardAddress = GetScriptForDestination(
                    FromOrDefaultKeyIDToDestination(keyID, TxDestTypeToKeyType(addressType), KeyType::MNRewardKeyType));
                addresses->insert(rewardAddress);
                mnview.SettingsSetRewardAddresses(*addresses);
            }
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
