// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MASTERNODES_H
#define DEFI_MASTERNODES_MASTERNODES_H

#include <amount.h>
#include <flushablestorage.h>
#include <pubkey.h>
#include <serialize.h>
#include <masternodes/accounts.h>
#include <masternodes/anchors.h>
#include <masternodes/gv.h>
#include <masternodes/icxorder.h>
#include <masternodes/incentivefunding.h>
#include <masternodes/loan.h>
#include <masternodes/oracles.h>
#include <masternodes/poolpairs.h>
#include <masternodes/tokens.h>
#include <masternodes/undos.h>
#include <uint256.h>
#include <wallet/ismine.h>

#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdint.h>

#include <boost/optional.hpp>

class CBlockIndex;
class CTransaction;

// Works instead of constants cause 'regtest' differs (don't want to overcharge chainparams)
int GetMnActivationDelay(int height);
int GetMnResignDelay(int height);
CAmount GetTokenCollateralAmount();
CAmount GetMnCreationFee(int height);
CAmount GetTokenCreationFee(int height);
CAmount GetMnCollateralAmount(int height);

constexpr uint8_t SUBNODE_COUNT{4};

class CMasternode
{
public:
    enum State {
        PRE_ENABLED,
        ENABLED,
        PRE_RESIGNED,
        RESIGNED,
        UNKNOWN // unreachable
    };

    enum TimeLock {
        ZEROYEAR,
        FIVEYEAR = 260,
        TENYEAR = 520
    };

    //! Minted blocks counter
    uint32_t mintedBlocks;

    //! Owner auth address == collateral address. Can be used as an ID.
    CKeyID ownerAuthAddress;
    char ownerType;

    //! Operator auth address. Can be equal to ownerAuthAddress. Can be used as an ID
    CKeyID operatorAuthAddress;
    char operatorType;

    //! MN creation block height
    int32_t creationHeight;
    //! Resign height
    int32_t resignHeight;
    //! Was used to set a ban height but is now unused
    int32_t unusedVariable;

    //! This fields are for transaction rollback (by disconnecting block)
    uint256 resignTx;
    uint256 banTx;

    //! empty constructor
    CMasternode();

    State GetState() const;
    State GetState(int height) const;
    bool IsActive() const;
    bool IsActive(int height) const;

    static std::string GetHumanReadableState(State state);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mintedBlocks);
        READWRITE(ownerAuthAddress);
        READWRITE(ownerType);
        READWRITE(operatorAuthAddress);
        READWRITE(operatorType);

        READWRITE(creationHeight);
        READWRITE(resignHeight);
        READWRITE(unusedVariable);

        READWRITE(resignTx);
        READWRITE(banTx);
    }

    //! equality test
    friend bool operator==(CMasternode const & a, CMasternode const & b);
    friend bool operator!=(CMasternode const & a, CMasternode const & b);
};


struct MNBlockTimeKey
{
    uint256 masternodeID;
    uint32_t blockHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(masternodeID);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        } else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }
    }
};

struct SubNodeBlockTimeKey
{
    uint256 masternodeID;
    uint8_t subnode;
    uint32_t blockHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(masternodeID);
        READWRITE(subnode);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        } else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }
    }
};

class CMasternodesView : public virtual CStorageView
{
    std::map<CKeyID, std::pair<uint32_t, int64_t>> minterTimeCache;

public:
//    CMasternodesView() = default;

    boost::optional<CMasternode> GetMasternode(uint256 const & id) const;
    boost::optional<uint256> GetMasternodeIdByOperator(CKeyID const & id) const;
    boost::optional<uint256> GetMasternodeIdByOwner(CKeyID const & id) const;
    void ForEachMasternode(std::function<bool(uint256 const &, CLazySerialize<CMasternode>)> callback, uint256 const & start = uint256());

    void IncrementMintedBy(CKeyID const & minter);
    void DecrementMintedBy(CKeyID const & minter);

    boost::optional<std::pair<CKeyID, uint256>> AmIOperator() const;
    boost::optional<std::pair<CKeyID, uint256>> AmIOwner() const;

    // Multiple operator support
    std::set<std::pair<CKeyID, uint256>> GetOperatorsMulti() const;

    Res CreateMasternode(uint256 const & nodeId, CMasternode const & node, uint16_t timelock);
    Res ResignMasternode(uint256 const & nodeId, uint256 const & txid, int height);
    Res UnCreateMasternode(uint256 const & nodeId);
    Res UnResignMasternode(uint256 const & nodeId, uint256 const & resignTx);

    // Get blocktimes for non-subnode and subnode with fork logic
    std::vector<int64_t> GetBlockTimes(const CKeyID& keyID, const uint32_t blockHeight, const int32_t creationHeight, const uint16_t timelock);

    // Non-subnode block times
    void SetMasternodeLastBlockTime(const CKeyID & minter, const uint32_t &blockHeight, const int64_t &time);
    boost::optional<int64_t> GetMasternodeLastBlockTime(const CKeyID & minter, const uint32_t height);
    void EraseMasternodeLastBlockTime(const uint256 &minter, const uint32_t& blockHeight);
    void ForEachMinterNode(std::function<bool(MNBlockTimeKey const &, CLazySerialize<int64_t>)> callback, MNBlockTimeKey const & start = {});

    // Subnode block times
    void SetSubNodesBlockTime(const CKeyID & minter, const uint32_t &blockHeight, const uint8_t id, const int64_t& time);
    std::vector<int64_t> GetSubNodesBlockTime(const CKeyID & minter, const uint32_t height);
    void EraseSubNodesLastBlockTime(const uint256& nodeId, const uint32_t& blockHeight);
    void ForEachSubNode(std::function<bool(SubNodeBlockTimeKey const &, CLazySerialize<int64_t>)> callback, SubNodeBlockTimeKey const & start = {});

    uint16_t GetTimelock(const uint256& nodeId, const CMasternode& node, const uint64_t height) const;

    // tags
    struct ID { static const unsigned char prefix; };
    struct Operator { static const unsigned char prefix; };
    struct Owner { static const unsigned char prefix; };

    // For storing last staked block time
    struct Staker { static const unsigned char prefix; };
    struct SubNode { static const unsigned char prefix; };

    // Store long term time lock
    struct Timelock { static const unsigned char prefix; };
};

class CLastHeightView : public virtual CStorageView
{
public:
    int GetLastHeight() const;
    void SetLastHeight(int height);
};

class CFoundationsDebtView : public virtual CStorageView
{
public:
    CAmount GetFoundationsDebt() const;
    void SetFoundationsDebt(CAmount debt);
};

class CTeamView : public virtual CStorageView
{
public:
    using CTeam = CAnchorData::CTeam;

    void SetTeam(CTeam const & newTeam);
    void SetAnchorTeams(CTeam const & authTeam, CTeam const & confirmTeam, const int height);

    CTeam GetCurrentTeam() const;
    boost::optional<CTeam> GetAuthTeam(int height) const;
    boost::optional<CTeam> GetConfirmTeam(int height) const;

    struct AuthTeam { static const unsigned char prefix; };
    struct ConfirmTeam { static const unsigned char prefix; };
};

class CAnchorRewardsView : public virtual CStorageView
{
public:
    using RewardTxHash = uint256;
    using AnchorTxHash = uint256;

    boost::optional<RewardTxHash> GetRewardForAnchor(AnchorTxHash const &btcTxHash) const;

    void AddRewardForAnchor(AnchorTxHash const &btcTxHash, RewardTxHash const & rewardTxHash);
    void RemoveRewardForAnchor(AnchorTxHash const &btcTxHash);
    void ForEachAnchorReward(std::function<bool(AnchorTxHash const &, CLazySerialize<RewardTxHash>)> callback);

    struct BtcTx { static const unsigned char prefix; };
};

class CAnchorConfirmsView : public virtual CStorageView
{
public:
    using AnchorTxHash = uint256;

    std::vector<CAnchorConfirmDataPlus> GetAnchorConfirmData();

    void AddAnchorConfirmData(const CAnchorConfirmDataPlus& data);
    void EraseAnchorConfirmData(const uint256 btcTxHash);
    void ForEachAnchorConfirmData(std::function<bool(const AnchorTxHash &, CLazySerialize<CAnchorConfirmDataPlus>)> callback);

    struct BtcTx { static const unsigned char prefix; };
};

class CCustomCSView
        : public CMasternodesView
        , public CLastHeightView
        , public CTeamView
        , public CFoundationsDebtView
        , public CAnchorRewardsView
        , public CTokensView
        , public CAccountsView
        , public CCommunityBalancesView
        , public CUndosView
        , public CPoolPairView
        , public CGovView
        , public CAnchorConfirmsView
        , public COracleView
        , public CICXOrderView
        , public CLoanView
        , public CVaultView
{
public:
    // Increase version when underlaying tables are changed
    static constexpr const int DbVersion = 1;

    CCustomCSView() = default;

    CCustomCSView(CStorageKV & st)
        : CStorageView(new CFlushableStorageKV(st))
    {}
    // cache-upon-a-cache (not a copy!) constructor
    CCustomCSView(CCustomCSView & other)
        : CStorageView(new CFlushableStorageKV(other.DB()))
    {}

    // cause depends on current mns:
    CTeamView::CTeam CalcNextTeam(uint256 const & stakeModifier);

    // Generate auth and custom anchor teams based on current block
    void CalcAnchoringTeams(uint256 const & stakeModifier, const CBlockIndex *pindexNew);

    /// @todo newbase move to networking?
    void CreateAndRelayConfirmMessageIfNeed(const CAnchorIndex::AnchorRec* anchor, const uint256 & btcTxHash, const CKey &masternodeKey);

    // simplified version of undo, without any unnecessary undo data
    void OnUndoTx(uint256 const & txid, uint32_t height);

    bool CanSpend(const uint256 & txId, int height) const;

    bool CalculateOwnerRewards(CScript const & owner, uint32_t height);

    void SetDbVersion(int version);

    int GetDbVersion() const;

    uint256 MerkleRoot();

    // we construct it as it
    CFlushableStorageKV& GetStorage() {
        return static_cast<CFlushableStorageKV&>(DB());
    }
};

std::map<CKeyID, CKey> AmISignerNow(CAnchorData::CTeam const & team);

/** Global DB and view that holds enhanced chainstate data (should be protected by cs_main) */
extern std::unique_ptr<CStorageLevelDB> pcustomcsDB;
extern std::unique_ptr<CCustomCSView> pcustomcsview;

#endif // DEFI_MASTERNODES_MASTERNODES_H
