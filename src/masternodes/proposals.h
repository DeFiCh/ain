// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_PROPOSALS_H
#define DEFI_MASTERNODES_PROPOSALS_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/res.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

using CPropId = uint256;
constexpr const uint8_t VOC_CYCLES = 2;
constexpr const uint8_t MAX_CYCLES = 100;
constexpr const uint16_t MAX_PROP_TITLE_SIZE = 128;
constexpr const uint16_t MAX_PROP_CONTEXT_SIZE = 512;

enum CPropType : uint8_t {
    CommunityFundProposal   = 0x01,
    VoteOfConfidence        = 0x02,
};

enum CPropOption : uint8_t {
    Emergency   = 0x01,
};

enum CPropStatusType : uint8_t {
    Voting    = 0x01,
    Rejected  = 0x02,
    Completed = 0x03,
};

enum CPropVoteType : uint8_t {
    VoteYes     = 0x01,
    VoteNo      = 0x02,
    VoteNeutral = 0x03,
};

std::string CPropTypeToString(const CPropType status);
std::string CPropOptionToString(const CPropOption option);
std::string CPropVoteToString(const CPropVoteType status);
std::string CPropStatusToString(const CPropStatusType status);

struct CCreatePropMessage {
    uint8_t type;
    CScript address;
    CAmount nAmount;
    uint8_t nCycles;
    std::string title;
    std::string context;
    std::string contextHash;
    uint8_t options;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(type);
        READWRITE(address);
        READWRITE(nAmount);
        READWRITE(nCycles);
        READWRITE(title);
        READWRITE(context);
        READWRITE(contextHash);
        READWRITE(options);
    }
};

struct CPropVoteMessage {
    CPropId propId;
    uint256 masternodeId;
    uint8_t vote;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(propId);
        READWRITE(masternodeId);
        READWRITE(vote);
    }
};

struct CPropObject : public CCreatePropMessage {
    CPropObject() = default;
    explicit CPropObject(const CCreatePropMessage& other) : CCreatePropMessage(other) {}

    uint32_t creationHeight{};
    uint32_t finalHeight{};

    uint32_t votingPeriod;
    CAmount majority;
    CAmount minVoters;
    CAmount fee;
    CAmount feeBurnAmount;


    // memory only
    CPropStatusType status{};
    uint8_t cycle{};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITEAS(CCreatePropMessage, *this);
        READWRITE(creationHeight);
        READWRITE(finalHeight);
        READWRITE(votingPeriod);
        READWRITE(majority);
        READWRITE(minVoters);
        READWRITE(fee);
        READWRITE(feeBurnAmount);
    }
};

struct CMnVotePerCycle {
    CPropId propId;
    uint8_t cycle;
    uint256 masternodeId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(propId);
        READWRITE(cycle);
        READWRITE(masternodeId);
    }
};

/// View for managing proposals and their data
class CPropsView : public virtual CStorageView
{
public:

    Res CreateProp(const CPropId& propId, uint32_t height, const CCreatePropMessage& prop, const CAmount fee);
    std::optional<CPropObject> GetProp(const CPropId& propId);
    Res UpdatePropCycle(const CPropId& propId, uint8_t cycle);
    Res UpdatePropStatus(const CPropId& propId, uint32_t height, CPropStatusType status);
    Res AddPropVote(const CPropId& propId, const uint256& masternodeId, CPropVoteType vote);
    std::optional<CPropVoteType> GetPropVote(const CPropId& propId, uint8_t cycle, const uint256& masternodeId);

    void ForEachProp(std::function<bool(CPropId const &, CPropObject const &)> callback, uint8_t status = 0);
    void ForEachPropVote(std::function<bool(CPropId const &, uint8_t, uint256 const &, CPropVoteType)> callback, CMnVotePerCycle const & start = {});
    void ForEachCycleProp(std::function<bool(CPropId const &, CPropObject const &)> callback, uint32_t height);

    virtual uint32_t GetVotingPeriodFromAttributes() const = 0;
    virtual uint32_t GetEmergencyPeriodFromAttributes(const CPropType& type) const = 0;
    virtual CAmount GetMajorityFromAttributes(const CPropType& type) const = 0;
    virtual CAmount GetMinVotersFromAttributes() const = 0;
    virtual CAmount GetFeeBurnPctFromAttributes() const = 0;

    struct ByType   { static constexpr uint8_t prefix() { return 0x2B; } };
    struct ByCycle  { static constexpr uint8_t prefix() { return 0x2C; } };
    struct ByMnVote { static constexpr uint8_t prefix() { return 0x2D; } };
    struct ByStatus { static constexpr uint8_t prefix() { return 0x2E; } };
    struct ByVoting { static constexpr uint8_t prefix() { return 0x2F; } };
};

#endif // DEFI_MASTERNODES_PROPOSALS_H
