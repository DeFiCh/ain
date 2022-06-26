// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>

#include <coins.h>
#include <consensus/params.h>
#include <masternodes/consensus/masternodes.h>
#include <masternodes/customtx.h>
#include <primitives/transaction.h>

Res CMasternodesConsensus::operator()(const CCreateMasterNodeMessage& obj) const {
    Require(CheckMasternodeCreationTx());

    if (height >= static_cast<uint32_t>(consensus.EunosHeight))
        Require(HasAuth(tx.vout[1].scriptPubKey), "masternode creation needs owner auth");

    if (height >= static_cast<uint32_t>(consensus.EunosPayaHeight))
        switch(obj.timelock) {
            case CMasternode::ZEROYEAR:
            case CMasternode::FIVEYEAR:
            case CMasternode::TENYEAR:
                break;
            default:
                return Res::Err("Timelock must be set to either 0, 5 or 10 years");
        }
    else
        Require(obj.timelock == 0, "collateral timelock cannot be set below EunosPaya");

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
    if (height >= static_cast<uint32_t>(consensus.FortCanningHeight))
        node.version = CMasternode::VERSION0;

    Require(mnview.CreateMasternode(tx.GetHash(), node, obj.timelock));
    // Build coinage from the point of masternode creation

    if (height >= static_cast<uint32_t>(consensus.EunosPayaHeight))
        for (uint8_t i{0}; i < SUBNODE_COUNT; ++i)
            mnview.SetSubNodesBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), i, time);

    else if (height >= static_cast<uint32_t>(consensus.DakotaCrescentHeight))
        mnview.SetMasternodeLastBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), time);

    return Res::Ok();
}

Res CMasternodesConsensus::operator()(const CResignMasterNodeMessage& obj) const {
    auto node = mnview.GetMasternode(obj);
    Require(node, "masternode %s does not exists", obj.ToString());
    Require(HasCollateralAuth(node->collateralTx.IsNull() ? static_cast<uint256>(obj) : node->collateralTx));
    return mnview.ResignMasternode(*node, obj, tx.GetHash(), height);
}

Res CMasternodesConsensus::operator()(const CUpdateMasterNodeMessage& obj) const {
    if (obj.updates.empty()) {
        return Res::Err("No update arguments provided");
    }

    if (obj.updates.size() > 3) {
        return Res::Err("Too many updates provided");
    }
    auto node = mnview.GetMasternode(obj.mnId);
    Require(node, "masternode %s does not exists", obj.mnId.ToString());

    const auto collateralTx = node->collateralTx.IsNull() ? obj.mnId : node->collateralTx;
    Require(HasCollateralAuth(collateralTx));

    auto state = node->GetState(height, mnview);
    if (state != CMasternode::ENABLED) {
        return Res::Err("Masternode %s is not in 'ENABLED' state", obj.mnId.ToString());
    }

    bool ownerType{false}, operatorType{false}, rewardType{false};
    for (const auto& item : obj.updates) {
        if (item.first == static_cast<uint8_t>(UpdateMasternodeType::OwnerAddress)) {
            if (ownerType) {
                return Res::Err("Multiple owner updates provided");
            }
            ownerType = true;
            bool collateralFound{false};
            for (const auto& vin : tx.vin) {
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
            if (!HasAuth(tx.vout[1].scriptPubKey)) {
                return Res::Err("Missing auth input for new masternode owner");
            }

            CTxDestination dest;
            if (!ExtractDestination(tx.vout[1].scriptPubKey, dest) || (dest.index() != PKHashType && dest.index() != WitV0KeyHashType)) {
                return Res::Err("Owner address must be P2PKH or P2WPKH type");
            }

            if (tx.vout[1].nValue != GetMnCollateralAmount(height)) {
                return Res::Err("Incorrect collateral amount");
            }

            const auto keyID = dest.index() == PKHashType ? CKeyID(std::get<PKHash>(dest)) : CKeyID(std::get<WitnessV0KeyHash>(dest));
            if (mnview.GetMasternodeIdByOwner(keyID) || mnview.GetMasternodeIdByOperator(keyID)) {
                return Res::Err("Masternode with that owner address already exists");
            }

            bool duplicate{false};
            mnview.ForEachNewCollateral([&](const uint256& key, CLazySerialize<MNNewOwnerHeightValue> valueKey) {
                const auto& value = valueKey.get();
                if (height > value.blockHeight) {
                    return true;
                }
                const auto& coin = coins.AccessCoin({key, 1});
                assert(!coin.IsSpent());
                CTxDestination pendingDest;
                assert(ExtractDestination(coin.out.scriptPubKey, pendingDest));
                const CKeyID storedID = pendingDest.index() == PKHashType ? CKeyID(std::get<PKHash>(pendingDest)) : CKeyID(std::get<WitnessV0KeyHash>(pendingDest));
                if (storedID == keyID) {
                    duplicate = true;
                    return false;
                }
                return true;
            });
            if (duplicate) {
                return Res::ErrCode(CustomTxErrCodes::Fatal, "Masternode exist with that owner address pending already");
            }

            mnview.UpdateMasternodeCollateral(obj.mnId, *node, tx.GetHash(), height);
        } else if (item.first == static_cast<uint8_t>(UpdateMasternodeType::OperatorAddress)) {
            if (operatorType) {
                return Res::Err("Multiple operator updates provided");
            }
            operatorType = true;

            if (item.second.first != 1 && item.second.first != 4) {
                return Res::Err("Operator address must be P2PKH or P2WPKH type");
            }

            const auto keyID = CKeyID(uint160(item.second.second));
            if (mnview.GetMasternodeIdByOwner(keyID) || mnview.GetMasternodeIdByOperator(keyID)) {
                return Res::Err("Masternode with that operator address already exists");
            }
            mnview.UpdateMasternodeOperator(obj.mnId, *node, item.second.first, keyID, height);
        } else if (item.first == static_cast<uint8_t>(UpdateMasternodeType::SetRewardAddress)) {
            if (rewardType) {
                return Res::Err("Multiple reward address updates provided");
            }
            rewardType = true;

            if (item.second.first != 1 && item.second.first != 4) {
                return Res::Err("Reward address must be P2PKH or P2WPKH type");
            }

            const auto keyID = CKeyID(uint160(item.second.second));
            mnview.SetForcedRewardAddress(obj.mnId, *node, item.second.first, keyID, height);
        } else if (item.first == static_cast<uint8_t>(UpdateMasternodeType::RemRewardAddress)) {
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
