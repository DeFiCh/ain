// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <masternodes/consensus/masternodes.h>
#include <masternodes/masternodes.h>
#include <primitives/transaction.h>

Res CMasternodesConsensus::operator()(const CCreateMasterNodeMessage& obj) const {
    auto res = CheckMasternodeCreationTx();
    if (!res)
        return res;

    if (height >= static_cast<uint32_t>(consensus.EunosHeight) && !HasAuth(tx.vout[1].scriptPubKey))
        return Res::Err("masternode creation needs owner auth");

    if (height >= static_cast<uint32_t>(consensus.EunosPayaHeight)) {
        switch(obj.timelock) {
            case CMasternode::ZEROYEAR:
            case CMasternode::FIVEYEAR:
            case CMasternode::TENYEAR:
                break;
            default:
                return Res::Err("Timelock must be set to either 0, 5 or 10 years");
        }
    } else if (obj.timelock != 0)
        return Res::Err("collateral timelock cannot be set below EunosPaya");

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

    res = mnview.CreateMasternode(tx.GetHash(), node, obj.timelock);
    // Build coinage from the point of masternode creation
    if (!res)
        return res;

    if (height >= static_cast<uint32_t>(consensus.EunosPayaHeight))
        for (uint8_t i{0}; i < SUBNODE_COUNT; ++i)
            mnview.SetSubNodesBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), i, time);

    else if (height >= static_cast<uint32_t>(consensus.DakotaCrescentHeight))
        mnview.SetMasternodeLastBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), time);

    return Res::Ok();
}

Res CMasternodesConsensus::operator()(const CResignMasterNodeMessage& obj) const {
    auto res = HasCollateralAuth(obj);
    return !res ? res : mnview.ResignMasternode(obj, tx.GetHash(), height);
}

Res CMasternodesConsensus::operator()(const CSetForcedRewardAddressMessage& obj) const {
    // Temporarily disabled for 2.2
    return Res::Err("reward address change is disabled for Fort Canning");

    auto node = mnview.GetMasternode(obj.nodeId);
    if (!node)
        return Res::Err("masternode %s does not exist", obj.nodeId.ToString());

    if (!HasCollateralAuth(obj.nodeId))
        return Res::Err("%s: %s", obj.nodeId.ToString(), "tx must have at least one input from masternode owner");

    return mnview.SetForcedRewardAddress(obj.nodeId, obj.rewardAddressType, obj.rewardAddress, height);
}

Res CMasternodesConsensus::operator()(const CRemForcedRewardAddressMessage& obj) const {
    // Temporarily disabled for 2.2
    return Res::Err("reward address change is disabled for Fort Canning");

    auto node = mnview.GetMasternode(obj.nodeId);
    if (!node)
        return Res::Err("masternode %s does not exist", obj.nodeId.ToString());

    if (!HasCollateralAuth(obj.nodeId))
        return Res::Err("%s: %s", obj.nodeId.ToString(), "tx must have at least one input from masternode owner");

    return mnview.RemForcedRewardAddress(obj.nodeId, height);
}

Res CMasternodesConsensus::operator()(const CUpdateMasterNodeMessage& obj) const {
    // Temporarily disabled for 2.2
    return Res::Err("updatemasternode is disabled for Fort Canning");

    auto res = HasCollateralAuth(obj.mnId);
    return !res ? res : mnview.UpdateMasternode(obj.mnId, obj.operatorType, obj.operatorAuthAddress, height);
}
