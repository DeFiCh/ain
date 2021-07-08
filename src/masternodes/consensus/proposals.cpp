// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/consensus/proposals.h>
#include <masternodes/proposals.h>
#include <masternodes/masternodes.h>

Res CProposalsConsensus::operator()(const CCreatePropMessage& obj) const {

    if (obj.type != CPropType::CommunityFundRequest)
        return Res::Err("wrong type on community fund proposal request");

    auto res = CheckProposalTx(obj.type);
    if (!res)
        return res;

    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member");

    if (obj.nAmount >= MAX_MONEY)
        return Res::Err("proposal wants to gain all money");

    if (obj.title.size() > 128)
        return Res::Err("proposal title cannot be more than 128 bytes");

    if (obj.nCycles < 1 || obj.nCycles > MAX_CYCLES)
        return Res::Err("proposal cycles can be between 1 and %d", int(MAX_CYCLES));

    return mnview.CreateProp(tx.GetHash(), height, obj, consensus.props.votingPeriod);
}

Res CProposalsConsensus::operator()(const CPropVoteMessage& obj) const {

    auto prop = mnview.GetProp(obj.propId);
    if (!prop)
        return Res::Err("proposal <%s> does not exists", obj.propId.GetHex());

    if (prop->status != CPropStatusType::Voting)
        return Res::Err("proposal <%s> is not in voting period", obj.propId.GetHex());

    auto node = mnview.GetMasternode(obj.masternodeId);
    if (!node)
        return Res::Err("masternode <%s> does not exist", obj.masternodeId.GetHex());

    auto ownerDest = node->ownerType == 1 ? CTxDestination(PKHash(node->ownerAuthAddress))
                                : CTxDestination(WitnessV0KeyHash(node->ownerAuthAddress));

    if (!HasAuth(GetScriptForDestination(ownerDest)))
        return Res::Err("tx must have at least one input from the owner");

    if (!node->IsActive(height))
        return Res::Err("masternode <%s> is not active", obj.masternodeId.GetHex());

    if (node->mintedBlocks < 1)
        return Res::Err("masternode <%s> does not mine at least one block", obj.masternodeId.GetHex());

    switch(obj.vote) {
        case CPropVoteType::VoteNo:
        case CPropVoteType::VoteYes:
        case CPropVoteType::VoteNeutral:
            break;
        default:
            return Res::Err("unsupported vote type");
    }
    auto vote = static_cast<CPropVoteType>(obj.vote);
    return mnview.AddPropVote(obj.propId, obj.masternodeId, vote);
}
