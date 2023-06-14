// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/consensus/proposals.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/masternodes.h>

Res CProposalsConsensus::CheckProposalTx(const CCreateProposalMessage &msg) const {
    if (tx.vout[0].nValue != GetProposalCreationFee(height, mnview, msg) || tx.vout[0].nTokenId != DCT_ID{0})
        return Res::Err("malformed tx vouts (wrong creation fee)");

    return Res::Ok();
}

Res CProposalsConsensus::IsOnChainGovernanceEnabled() const {
    CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovernanceEnabled};

    auto attributes = mnview.GetAttributes();
    Require(attributes, "Attributes unavailable");

    Require(attributes->GetValue(enabledKey, false), "Cannot create tx, on-chain governance is not enabled");

    return Res::Ok();
}

Res CProposalsConsensus::operator()(const CCreateProposalMessage &obj) const {
    auto res = IsOnChainGovernanceEnabled();
    if (!res) {
        return res;
    }

    switch (obj.type) {
        case CProposalType::CommunityFundProposal:
            if (!HasAuth(obj.address))
                return Res::Err("tx must have at least one input from proposal account");
            break;

        case CProposalType::VoteOfConfidence:
            if (obj.nAmount != 0)
                return Res::Err("proposal amount in vote of confidence");

            if (!obj.address.empty())
                return Res::Err("vote of confidence address should be empty");

            if (!(obj.options & CProposalOption::Emergency) && obj.nCycles != VOC_CYCLES)
                return Res::Err("proposal cycles should be %d", int(VOC_CYCLES));
            break;

        default:
            return Res::Err("unsupported proposal type");
    }

    res = CheckProposalTx(obj);
    if (!res)
        return res;

    if (obj.nAmount >= MAX_MONEY)
        return Res::Err("proposal wants to gain all money");

    if (obj.title.empty())
        return Res::Err("proposal title must not be empty");

    if (obj.title.size() > MAX_PROPOSAL_TITLE_SIZE)
        return Res::Err("proposal title cannot be more than %d bytes", MAX_PROPOSAL_TITLE_SIZE);

    if (obj.context.empty())
        return Res::Err("proposal context must not be empty");

    if (obj.context.size() > MAX_PROPOSAL_CONTEXT_SIZE)
        return Res::Err("proposal context cannot be more than %d bytes", MAX_PROPOSAL_CONTEXT_SIZE);

    if (obj.contextHash.size() > MAX_PROPOSAL_CONTEXT_SIZE)
        return Res::Err("proposal context hash cannot be more than %d bytes", MAX_PROPOSAL_CONTEXT_SIZE);

    auto attributes = mnview.GetAttributes();
    assert(attributes);
    CDataStructureV0 cfpMaxCycles{AttributeTypes::Governance, GovernanceIDs::Proposals, GovernanceKeys::CFPMaxCycles};
    auto maxCycles = attributes->GetValue(cfpMaxCycles, static_cast<uint32_t>(MAX_CYCLES));

    if (obj.nCycles < 1 || obj.nCycles > maxCycles )
        return Res::Err("proposal cycles can be between 1 and %d", maxCycles);

    if ((obj.options & CProposalOption::Emergency)) {
        if (obj.nCycles != 1) {
            return Res::Err("emergency proposal cycles must be 1");
        }

        if (static_cast<CProposalType>(obj.type) != CProposalType::VoteOfConfidence) {
            return Res::Err("only vote of confidence allowed with emergency option");
        }
    }

    return mnview.CreateProposal(tx.GetHash(), height, obj, tx.vout[0].nValue);
}

Res CProposalsConsensus::operator()(const CProposalVoteMessage &obj) const {
    auto res = IsOnChainGovernanceEnabled();
    if (!res) {
        return res;
    }

    auto prop = mnview.GetProposal(obj.propId);
    if (!prop)
        return Res::Err("proposal <%s> does not exist", obj.propId.GetHex());

    if (prop->status != CProposalStatusType::Voting)
        return Res::Err("proposal <%s> is not in voting period", obj.propId.GetHex());

    auto node = mnview.GetMasternode(obj.masternodeId);
    if (!node)
        return Res::Err("masternode <%s> does not exist", obj.masternodeId.GetHex());

    auto ownerDest = node->ownerType == 1 ? CTxDestination(PKHash(node->ownerAuthAddress))
                                          : CTxDestination(WitnessV0KeyHash(node->ownerAuthAddress));

    if (!HasAuth(GetScriptForDestination(ownerDest)))
        return Res::Err("tx must have at least one input from the owner");

    if (!node->IsActive(height, mnview))
        return Res::Err("masternode <%s> is not active", obj.masternodeId.GetHex());

    if (node->mintedBlocks < 1)
        return Res::Err("masternode <%s> does not mine at least one block", obj.masternodeId.GetHex());

    switch (obj.vote) {
        case CProposalVoteType::VoteNo:
        case CProposalVoteType::VoteYes:
        case CProposalVoteType::VoteNeutral:
            break;
        default:
            return Res::Err("unsupported vote type");
    }
    auto vote = static_cast<CProposalVoteType>(obj.vote);
    return mnview.AddProposalVote(obj.propId, obj.masternodeId, vote);
}
