// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <masternodes/proposals.h>

std::string CPropTypeToString(const CPropType status)
{
    switch(status) {
    case CPropType::CommunityFundProposal:  return "CommunityFundProposal";
    case CPropType::BlockRewardReallocation:return "BlockRewardReallocation";
    case CPropType::VoteOfConfidence:       return "VoteOfConfidence";
    }
    return "Unknown";
}

std::string CPropStatusToString(const CPropStatusType status)
{
    switch(status) {
    case CPropStatusType::Voting:    return "Voting";
    case CPropStatusType::Rejected:  return "Rejected";
    case CPropStatusType::Completed: return "Completed";
    }
    return "Unknown";
}

std::string CPropVoteToString(const CPropVoteType vote)
{
    switch(vote) {
        case CPropVoteType::VoteNo:      return "NO";
        case CPropVoteType::VoteYes:     return "YES";
        case CPropVoteType::VoteNeutral: return "NEUTRAL";
    }
    return "Unknown";
}

Res CPropsView::CreateProp(const CPropId& propId, uint32_t height, const CCreatePropMessage& msg)
{
    CPropObject prop;
    static_cast<CCreatePropMessage&>(prop) = msg;
    prop.creationHeight = height;
    auto key = std::make_pair(uint8_t(CPropStatusType::Voting), propId);
    WriteBy<ByStatus>(key, uint8_t(1));

    auto votingPeriod = GetVotingPeriod();
    for (uint8_t i = 1; i <= prop.nCycles; ++i) {
        height += (height % votingPeriod) + votingPeriod;
        auto key = std::make_pair(height, propId);
        WriteBy<ByCycle>(key, i);
    }
    prop.finalHeight = height;
    WriteBy<ByType>(propId, prop);
    return Res::Ok();
}

std::optional<CPropObject> CPropsView::GetProp(const CPropId& propId)
{
    auto prop = ReadBy<ByType, CPropObject>(propId);
    if (!prop)
        return prop;

    auto guessStatus = [&](CPropStatusType status) {
        auto key = std::make_pair(uint8_t(status), propId);
        if (auto cycle = ReadBy<ByStatus, uint8_t>(key)) {
            prop->cycle = *cycle;
            prop->status = status;
            return true;
        }
        return false;
    };
    guessStatus(CPropStatusType::Voting)   ||
    guessStatus(CPropStatusType::Rejected) ||
    guessStatus(CPropStatusType::Completed);
    return prop;
}

Res CPropsView::UpdatePropCycle(const CPropId& propId, uint8_t cycle)
{
    if (cycle < 1 || cycle > 3)
        return Res::Err("Cycle out of range");

    auto key = std::make_pair(uint8_t(CPropStatusType::Voting), propId);
    auto pcycle = ReadBy<ByStatus, uint8_t>(key);
    if (!pcycle)
        Res::Err("Proposal <%s> is not in voting period", propId.GetHex());

    if (*pcycle >= cycle)
        return Res::Err("New cycle should be greater than old one");

    WriteBy<ByStatus>(key, cycle);
    return Res::Ok();
}

Res CPropsView::UpdatePropStatus(const CPropId& propId, uint32_t height, CPropStatusType status)
{
    auto key = std::make_pair(uint8_t(CPropStatusType::Voting), propId);
    auto stat = ReadBy<ByStatus, uint8_t>(key);
    if (!stat)
        return Res::Err("Proposal <%s> is not in voting period", propId.GetHex());

    if (status == CPropStatusType::Voting)
        return Res::Err("Proposal <%s> is already in voting period", propId.GetHex());

    EraseBy<ByStatus>(key);

    key = std::make_pair(uint8_t(status), propId);
    WriteBy<ByStatus>(key, *stat);

    auto p_prop = GetProp(propId);
    assert(p_prop);

    uint8_t i = 0;
    auto cycles = p_prop->nCycles;
    auto ckey = std::make_pair(p_prop->creationHeight, propId);
    for (auto it = LowerBound<ByCycle>(ckey); i < cycles && it.Valid(); it.Next()) {
        if (it.Key().second == propId) {
            EraseBy<ByCycle>(it.Key());
            ++i;
        }
    }

    if (p_prop->finalHeight != height) {
        p_prop->finalHeight = height;
        WriteBy<ByType>(propId, *p_prop);
    }
    return Res::Ok();
}

Res CPropsView::AddPropVote(const CPropId& propId, const uint256& masternodeId, CPropVoteType vote)
{
    auto cycle = ReadBy<ByStatus, uint8_t>(std::make_pair(uint8_t(CPropStatusType::Voting), propId));
    if (!cycle)
        return Res::Err("Proposal <%s> is not in voting period", propId.GetHex());

    CMnVotePerCycle key{propId, *cycle, masternodeId};
    WriteBy<ByMnVote>(key, uint8_t(vote));
    return Res::Ok();
}

std::optional<CPropVoteType> CPropsView::GetPropVote(const CPropId& propId, uint8_t cycle, const uint256& masternodeId)
{
    CMnVotePerCycle key{propId, cycle, masternodeId};
    auto vote = ReadBy<ByMnVote, uint8_t>(key);
    if (!vote)
        return {};
    return static_cast<CPropVoteType>(*vote);
}

void CPropsView::ForEachProp(std::function<bool(CPropId const &, CPropObject const &)> callback, uint8_t status)
{
    ForEach<ByStatus, std::pair<uint8_t, uint256>, uint8_t>([&](const std::pair<uint8_t, uint256>& key, uint8_t i) {
        auto prop = GetProp(key.second);
        assert(prop);
        return callback(key.second, *prop);
    }, std::make_pair(status, uint256{}));
}

void CPropsView::ForEachPropVote(std::function<bool(CPropId const &, uint8_t, uint256 const &, CPropVoteType)> callback, CMnVotePerCycle const & start)
{
    ForEach<ByMnVote, CMnVotePerCycle, uint8_t>([&](const CMnVotePerCycle& key, uint8_t vote) {
        return callback(key.propId, key.cycle, key.masternodeId, static_cast<CPropVoteType>(vote));
    }, start);
}

void CPropsView::ForEachCycleProp(std::function<bool(CPropId const &, CPropObject const &)> callback, uint32_t height)
{
    ForEach<ByCycle, std::pair<uint32_t, uint256>, uint8_t>([&](const std::pair<uint32_t, uint256>& key, uint8_t i) {
        // limited to exact height
        if (key.first != height)
            return false;

        auto prop = GetProp(key.second);
        assert(prop);
        return callback(key.second, *prop);
    }, std::make_pair(height, uint256{}));
}

Res CPropsView::SetVotingPeriod(uint32_t votingPeriod)
{
    Write(ByVoting::prefix(), votingPeriod);
    return Res::Ok();
}

uint32_t CPropsView::GetVotingPeriod()
{
    uint32_t votingPeriod;
    if (Read(ByVoting::prefix(), votingPeriod)) {
        return votingPeriod;
    }
    return Params().GetConsensus().props.votingPeriod;
}
