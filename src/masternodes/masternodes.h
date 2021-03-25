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
#include <masternodes/accountshistory.h>
#include <masternodes/anchors.h>
#include <masternodes/incentivefunding.h>
#include <masternodes/tokens.h>
#include <masternodes/undos.h>
#include <masternodes/poolpairs.h>
#include <masternodes/gv.h>
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
int GetMnActivationDelay();
int GetMnResignDelay();
int GetMnHistoryFrame();
CAmount GetTokenCollateralAmount();
CAmount GetMnCreationFee(int height);
CAmount GetTokenCreationFee(int height);
CAmount GetMnCollateralAmount(int height);

class CMasternode
{
public:
    enum State {
        PRE_ENABLED,
        ENABLED,
        PRE_RESIGNED,
        RESIGNED,
        PRE_BANNED,
        BANNED,
        UNKNOWN // unreachable
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
    //! Criminal ban height
    int32_t banHeight;

    //! This fields are for transaction rollback (by disconnecting block)
    uint256 resignTx;
    uint256 banTx;

    //! empty constructor
    CMasternode();

    State GetState() const;
    State GetState(int h) const;
    bool IsActive() const;
    bool IsActive(int h) const;

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
        READWRITE(banHeight);

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

class CMasternodesView : public virtual CStorageView
{
public:
//    CMasternodesView() = default;

    boost::optional<CMasternode> GetMasternode(uint256 const & id) const;
    boost::optional<uint256> GetMasternodeIdByOperator(CKeyID const & id) const;
    boost::optional<uint256> GetMasternodeIdByOwner(CKeyID const & id) const;
    void ForEachMasternode(std::function<bool(uint256 const &, CLazySerialize<CMasternode>)> callback, uint256 const & start = uint256());

    void IncrementMintedBy(CKeyID const & minter);
    void DecrementMintedBy(CKeyID const & minter);

    bool BanCriminal(const uint256 txid, std::vector<unsigned char> & metadata, int height);
    bool UnbanCriminal(const uint256 txid, std::vector<unsigned char> & metadata);

    boost::optional<std::pair<CKeyID, uint256>> AmIOperator() const;
    boost::optional<std::pair<CKeyID, uint256>> AmIOwner() const;

    // Multiple operator support
    std::set<std::pair<CKeyID, uint256>> GetOperatorsMulti() const;

    Res CreateMasternode(uint256 const & nodeId, CMasternode const & node);
    Res ResignMasternode(uint256 const & nodeId, uint256 const & txid, int height);
//    void UnCreateMasternode(uint256 const & nodeId);
//    void UnResignMasternode(uint256 const & nodeId, uint256 const & resignTx);

    void SetMasternodeLastBlockTime(const CKeyID & minter, const uint32_t &blockHeight, const int64_t &time);
    boost::optional<int64_t> GetMasternodeLastBlockTime(const CKeyID & minter);
    void EraseMasternodeLastBlockTime(const uint256 &minter, const uint32_t& blockHeight);

    void ForEachMinterNode(std::function<bool(MNBlockTimeKey const &, CLazySerialize<int64_t>)> callback, MNBlockTimeKey const & start = {});

    // tags
    struct ID { static const unsigned char prefix; };
    struct Operator { static const unsigned char prefix; };
    struct Owner { static const unsigned char prefix; };

    // For storing last staked block time
    struct Staker { static const unsigned char prefix; };
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
        , public CAccountsHistoryView
        , public CRewardsHistoryView
        , public CCommunityBalancesView
        , public CUndosView
        , public CPoolPairView
        , public CGovView
        , public CAnchorConfirmsView
{
public:
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

    CStorageKV& GetRaw() {
        return DB();
    }
};

class CAccountsHistoryStorage : public CCustomCSView
{
    int acindex;
    const uint32_t height;
    const uint32_t txn;
    const uint256 txid;
    const uint8_t type;
    std::map<CScript, TAmounts> diffs;
public:
    CAccountsHistoryStorage(CCustomCSView & storage, uint32_t height, uint32_t txn, const uint256& txid, uint8_t type);
    Res AddBalance(CScript const & owner, CTokenAmount amount) override;
    Res SubBalance(CScript const & owner, CTokenAmount amount) override;
    bool Flush();
};

class CRewardsHistoryStorage : public CCustomCSView
{
    int acindex;
    const uint32_t height;
    std::map<std::pair<CScript, uint8_t>, std::map<DCT_ID, TAmounts>> diffs;
    using CCustomCSView::AddBalance;
public:
    CRewardsHistoryStorage(CCustomCSView & storage, uint32_t height);
    Res AddBalance(CScript const & owner, DCT_ID poolID, uint8_t type, CTokenAmount amount);
    bool Flush();
};

std::map<CKeyID, CKey> AmISignerNow(CAnchorData::CTeam const & team);

/** Global DB and view that holds enhanced chainstate data (should be protected by cs_main) */
extern std::unique_ptr<CStorageLevelDB> pcustomcsDB;
extern std::unique_ptr<CCustomCSView> pcustomcsview;

#endif // DEFI_MASTERNODES_MASTERNODES_H
