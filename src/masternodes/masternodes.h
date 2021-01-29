// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MASTERNODES_H
#define DEFI_MASTERNODES_MASTERNODES_H

#include <amount.h>
#include <flushablestorage.h>
#include <pubkey.h>
#include <serialize.h>
#include <masternodes/accounts.h>
#include <masternodes/accountshistory.h>
#include <masternodes/incentivefunding.h>
#include <masternodes/tokens.h>
#include <masternodes/undos.h>
#include <masternodes/poolpairs.h>
#include <masternodes/oraclesview.h>
#include <masternodes/gv.h>
#include <uint256.h>
#include <wallet/ismine.h>

#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdint.h>

#include <boost/optional.hpp>

class CTransaction;
class CAnchor;


// Works instead of constants cause 'regtest' differs (don't want to overcharge chainparams)
int GetMnActivationDelay();
int GetMnResignDelay();
int GetMnHistoryFrame();
CAmount GetMnCollateralAmount();
CAmount GetMnCreationFee(int height);
CAmount GetTokenCollateralAmount();
CAmount GetTokenCreationFee(int height);

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

    Res CreateMasternode(uint256 const & nodeId, CMasternode const & node);
    Res ResignMasternode(uint256 const & nodeId, uint256 const & txid, int height);
//    void UnCreateMasternode(uint256 const & nodeId);
//    void UnResignMasternode(uint256 const & nodeId, uint256 const & resignTx);

    // tags
    struct ID { static const unsigned char prefix; };
    struct Operator { static const unsigned char prefix; };
    struct Owner { static const unsigned char prefix; };
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
    using CTeam = std::set<CKeyID>;
    void SetTeam(CTeam const & newTeam);
    CTeam GetCurrentTeam() const;
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

// TODO (IntegralTeam Y) implement and use proper price feed validator
class CDummyPriceFeedValidator: public CPriceFeedValidator {
public:
    ~CDummyPriceFeedValidator() override = default;

    bool IsValidPriceFeedName(const std::string& priceFeed) const override {
        return true;
    }
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
        , public COraclesView
{
public:
    CCustomCSView(): COraclesView(std::make_shared<CDummyPriceFeedValidator>()) {}

    CCustomCSView(CStorageKV & st):
        CStorageView(new CFlushableStorageKV(st)),
        COraclesView(std::make_shared<CDummyPriceFeedValidator>())
    {}

    // cache-upon-a-cache (not a copy!) constructor
    CCustomCSView(CCustomCSView & other)
        : CStorageView(new CFlushableStorageKV(other.DB())),
          COraclesView(std::make_shared<CDummyPriceFeedValidator>())
    {}

    // cause depends on current mns:
    CTeamView::CTeam CalcNextTeam(uint256 const & stakeModifier);

    /// @todo newbase move to networking?
    void CreateAndRelayConfirmMessageIfNeed(const CAnchor & anchor, const uint256 & btcTxHash);

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
public:
    CRewardsHistoryStorage(CCustomCSView & storage, uint32_t height);
    Res AddBalance(CScript const & owner, DCT_ID poolID, uint8_t type, CTokenAmount amount);
    bool Flush();
};

class CWallet;
isminetype IsMineCached(CWallet const & wallet, CScript const & script);

/** Global DB and view that holds enhanced chainstate data (should be protected by cs_main) */
extern std::unique_ptr<CStorageLevelDB> pcustomcsDB;
extern std::unique_ptr<CCustomCSView> pcustomcsview;

#endif // DEFI_MASTERNODES_MASTERNODES_H
