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

using CProposalId                              = uint256;
constexpr const uint8_t VOC_CYCLES             = 1;
constexpr const uint8_t MAX_CYCLES             = 100;
constexpr const uint16_t MAX_PROP_TITLE_SIZE   = 128;
constexpr const uint16_t MAX_PROP_CONTEXT_SIZE = 512;

enum CProposalType : uint8_t {
    CommunityFund    = 0x01,
    VoteOfConfidence = 0x02,
};

enum CProposalOption : uint8_t {
    Emergency = 0x01,
};

enum CProposalStatusType : uint8_t {
    Voting    = 0x01,
    Rejected  = 0x02,
    Completed = 0x03,
};

enum CProposalVoteType : uint8_t {
    VoteYes     = 0x01,
    VoteNo      = 0x02,
    VoteNeutral = 0x03,
};

std::string CProposalTypeToString(const CProposalType status);
std::string CProposalOptionToString(const CProposalOption option);
std::string CProposalVoteToString(const CProposalVoteType status);
std::string CProposalStatusToString(const CProposalStatusType status);

struct CCreateProposalMessage {
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
    inline void SerializationOp(Stream &s, Operation ser_action) {
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

struct CProposalVoteMessage {
    CProposalId propId;
    uint256 masternodeId;
    uint8_t vote;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(propId);
        READWRITE(masternodeId);
        READWRITE(vote);
    }
};

struct CProposalObject : public CCreateProposalMessage {
    CProposalObject() = default;
    explicit CProposalObject(const CCreateProposalMessage &other)
        : CCreateProposalMessage(other) {}

    uint32_t creationHeight{};
    uint32_t proposalEndHeight{};

    uint32_t votingPeriod;
    CAmount approvalThreshold;
    CAmount quorum;
    CAmount fee;
    CAmount feeBurnAmount;

    // memory only
    CProposalStatusType status{};
    uint8_t cycle{};
    uint32_t cycleEndHeight{};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CCreateProposalMessage, *this);
        READWRITE(creationHeight);
        READWRITE(proposalEndHeight);
        READWRITE(votingPeriod);
        READWRITE(approvalThreshold);
        READWRITE(quorum);
        READWRITE(fee);
        READWRITE(feeBurnAmount);
    }
};

struct CMnVotePerCycle {
    CProposalId propId;
    uint8_t cycle;
    uint256 masternodeId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(propId);
        READWRITE(cycle);
        READWRITE(masternodeId);
    }
};

/// View for managing proposals and their data
class CProposalView : public virtual CStorageView {
public:
    Res CreateProposal(const CProposalId &propId,
                       uint32_t height,
                       const CCreateProposalMessage &prop,
                       const CAmount fee);
    std::optional<CProposalObject> GetProposal(const CProposalId &propId);
    Res UpdateProposalCycle(const CProposalId &propId, uint8_t cycle);
    Res UpdateProposalStatus(const CProposalId &propId, uint32_t height, CProposalStatusType status);
    Res AddProposalVote(const CProposalId &propId, const uint256 &masternodeId, CProposalVoteType vote);
    std::optional<CProposalVoteType> GetProposalVote(const CProposalId &propId,
                                                     uint8_t cycle,
                                                     const uint256 &masternodeId);

    void ForEachProposal(std::function<bool(const CProposalId &, const CProposalObject &)> callback,
                         uint8_t status = 0);
    void ForEachProposalVote(
        std::function<bool(const CProposalId &, uint8_t, const uint256 &, CProposalVoteType)> callback,
        const CMnVotePerCycle &start = {});
    void ForEachCycleProposal(std::function<bool(const CProposalId &, const CProposalObject &)> callback,
                              uint32_t height);

    virtual uint32_t GetVotingPeriodFromAttributes() const                                           = 0;
    virtual uint32_t GetEmergencyPeriodFromAttributes(const CProposalType &type) const               = 0;
    virtual CAmount GetApprovalThresholdFromAttributes(const CProposalType &type) const              = 0;
    virtual CAmount GetQuorumFromAttributes(const CProposalType &type, bool emergency = false) const = 0;
    virtual CAmount GetFeeBurnPctFromAttributes() const                                              = 0;

    struct ByType {
        static constexpr uint8_t prefix() { return 0x2B; }
    };
    struct ByCycle {
        static constexpr uint8_t prefix() { return 0x2C; }
    };
    struct ByMnVote {
        static constexpr uint8_t prefix() { return 0x2D; }
    };
    struct ByStatus {
        static constexpr uint8_t prefix() { return 0x2E; }
    };
    struct ByVoting {
        static constexpr uint8_t prefix() { return 0x2F; }
    };
};

#endif  // DEFI_MASTERNODES_PROPOSALS_H
