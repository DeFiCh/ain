// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <masternodes/masternodes.h>
#include <masternodes/proposals.h>

std::string CProposalTypeToString(const CProposalType status) {
    switch (status) {
        case CProposalType::CommunityFund:
            return "CommunityFundProposal";
        case CProposalType::VoteOfConfidence:
            return "VoteOfConfidence";
    }
    return "Unknown";
}

std::string CProposalOptionToString(const CProposalOption option) {
    switch (option) {
        case CProposalOption::Emergency:
            return "Emergency";
    }
    return "Unknown";
}

std::string CProposalStatusToString(const CProposalStatusType status) {
    switch (status) {
        case CProposalStatusType::Voting:
            return "Voting";
        case CProposalStatusType::Rejected:
            return "Rejected";
        case CProposalStatusType::Completed:
            return "Completed";
    }
    return "Unknown";
}

std::string CProposalVoteToString(const CProposalVoteType vote) {
    switch (vote) {
        case CProposalVoteType::VoteNo:
            return "NO";
        case CProposalVoteType::VoteYes:
            return "YES";
        case CProposalVoteType::VoteNeutral:
            return "NEUTRAL";
    }
    return "Unknown";
}

Res CProposalView::CreateProposal(const CProposalId &propId,
                                  uint32_t height,
                                  const CCreateProposalMessage &msg,
                                  const CAmount fee) {
    CProposalObject prop{msg};
    bool emergency = prop.options & CProposalOption::Emergency;
    auto type      = static_cast<CProposalType>(prop.type);

    prop.creationHeight    = height;
    prop.votingPeriod      = (emergency ? GetEmergencyPeriodFromAttributes(type) : GetVotingPeriodFromAttributes());
    prop.approvalThreshold = GetApprovalThresholdFromAttributes(type);
    prop.quorum            = GetQuorumFromAttributes(type, emergency);
    prop.fee               = fee;
    prop.feeBurnAmount     = MultiplyAmounts(fee, GetFeeBurnPctFromAttributes());

    auto key = std::make_pair(uint8_t(CProposalStatusType::Voting), propId);
    WriteBy<ByStatus>(key, static_cast<uint8_t>(1));
    if (emergency) {
        height += prop.votingPeriod;
        WriteBy<ByCycle>(std::make_pair(height, propId), static_cast<uint8_t>(1));
    } else {
        height = height + (prop.votingPeriod - height % prop.votingPeriod);
        for (uint8_t i = 1; i <= prop.nCycles; ++i) {
            height += prop.votingPeriod;
            auto keyPair = std::make_pair(height, propId);
            WriteBy<ByCycle>(keyPair, i);
        }
    }
    prop.proposalEndHeight = height;
    WriteBy<ByType>(propId, prop);
    return Res::Ok();
}

std::optional<CProposalObject> CProposalView::GetProposal(const CProposalId &propId) {
    auto prop = ReadBy<ByType, CProposalObject>(propId);
    if (!prop)
        return prop;

    auto guessStatus = [&](CProposalStatusType status) {
        auto key = std::make_pair(uint8_t(status), propId);
        if (auto cycle = ReadBy<ByStatus, uint8_t>(key)) {
            prop->cycle          = *cycle;
            prop->status         = status;
            prop->cycleEndHeight = prop->creationHeight +
                                   (prop->votingPeriod - prop->creationHeight % prop->votingPeriod) +
                                   prop->votingPeriod * *cycle;
            return true;
        }
        return false;
    };
    guessStatus(CProposalStatusType::Voting) || guessStatus(CProposalStatusType::Rejected) ||
        guessStatus(CProposalStatusType::Completed);
    return prop;
}

Res CProposalView::UpdateProposalCycle(const CProposalId &propId, uint8_t cycle) {
    if (cycle < 1 || cycle > MAX_CYCLES)
        return Res::Err("Cycle out of range");

    auto key    = std::make_pair(uint8_t(CProposalStatusType::Voting), propId);
    auto pcycle = ReadBy<ByStatus, uint8_t>(key);
    if (!pcycle)
        Res::Err("Proposal <%s> is not in voting period", propId.GetHex());

    if (*pcycle >= cycle)
        return Res::Err("New cycle should be greater than old one");

    WriteBy<ByStatus>(key, cycle);

    // Update values from attributes on each cycle
    auto prop = GetProposal(propId);
    assert(prop);
    bool emergency = prop->options & CProposalOption::Emergency;
    auto type      = static_cast<CProposalType>(prop->type);

    prop->approvalThreshold = GetApprovalThresholdFromAttributes(type);
    prop->quorum            = GetQuorumFromAttributes(type, emergency);
    WriteBy<ByType>(propId, *prop);

    return Res::Ok();
}

Res CProposalView::UpdateProposalStatus(const CProposalId &propId, uint32_t height, CProposalStatusType status) {
    auto key  = std::make_pair(uint8_t(CProposalStatusType::Voting), propId);
    auto stat = ReadBy<ByStatus, uint8_t>(key);
    if (!stat)
        return Res::Err("Proposal <%s> is not in voting period", propId.GetHex());

    if (status == CProposalStatusType::Voting)
        return Res::Err("Proposal <%s> is already in voting period", propId.GetHex());

    EraseBy<ByStatus>(key);

    key = std::make_pair(uint8_t(status), propId);
    WriteBy<ByStatus>(key, *stat);

    auto p_prop = GetProposal(propId);
    assert(p_prop);

    uint8_t i   = 0;
    auto cycles = p_prop->nCycles;
    auto ckey   = std::make_pair(p_prop->creationHeight, propId);
    for (auto it = LowerBound<ByCycle>(ckey); i < cycles && it.Valid(); it.Next()) {
        if (it.Key().second == propId) {
            EraseBy<ByCycle>(it.Key());
            ++i;
        }
    }

    if (p_prop->proposalEndHeight != height) {
        p_prop->proposalEndHeight = height;
        WriteBy<ByType>(propId, *p_prop);
    }
    return Res::Ok();
}

Res CProposalView::AddProposalVote(const CProposalId &propId, const uint256 &masternodeId, CProposalVoteType vote) {
    auto cycle = ReadBy<ByStatus, uint8_t>(std::make_pair(uint8_t(CProposalStatusType::Voting), propId));
    if (!cycle)
        return Res::Err("Proposal <%s> is not in voting period", propId.GetHex());

    CMnVotePerCycle key{propId, *cycle, masternodeId};
    WriteBy<ByMnVote>(key, uint8_t(vote));
    return Res::Ok();
}

std::optional<CProposalVoteType> CProposalView::GetProposalVote(const CProposalId &propId,
                                                                uint8_t cycle,
                                                                const uint256 &masternodeId) {
    CMnVotePerCycle key{propId, cycle, masternodeId};
    auto vote = ReadBy<ByMnVote, uint8_t>(key);
    if (!vote)
        return {};
    return static_cast<CProposalVoteType>(*vote);
}

void CProposalView::ForEachProposal(std::function<bool(const CProposalId &, const CProposalObject &)> callback,
                                    uint8_t status) {
    ForEach<ByStatus, std::pair<uint8_t, uint256>, uint8_t>(
        [&](const std::pair<uint8_t, uint256> &key, uint8_t i) {
            auto prop = GetProposal(key.second);
            assert(prop);
            return callback(key.second, *prop);
        },
        std::make_pair(status, uint256{}));
}

void CProposalView::ForEachProposalVote(
    std::function<bool(const CProposalId &, uint8_t, const uint256 &, CProposalVoteType)> callback,
    const CMnVotePerCycle &start) {
    ForEach<ByMnVote, CMnVotePerCycle, uint8_t>(
        [&](const CMnVotePerCycle &key, uint8_t vote) {
            return callback(key.propId, key.cycle, key.masternodeId, static_cast<CProposalVoteType>(vote));
        },
        start);
}

void CProposalView::ForEachCycleProposal(std::function<bool(const CProposalId &, const CProposalObject &)> callback,
                                         uint32_t height) {
    ForEach<ByCycle, std::pair<uint32_t, uint256>, uint8_t>(
        [&](const std::pair<uint32_t, uint256> &key, uint8_t i) {
            // limited to exact height
            if (key.first != height)
                return false;

            auto prop = GetProposal(key.second);
            assert(prop);
            return callback(key.second, *prop);
        },
        std::make_pair(height, uint256{}));
}
