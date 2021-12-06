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
#include <masternodes/vault.h>
#include <uint256.h>
#include <wallet/ismine.h>

#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdint.h>

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

    enum Version : int32_t {
        PRE_FORT_CANNING = -1,
        VERSION0 = 0,
    };

    //! Minted blocks counter
    uint32_t mintedBlocks;

    //! Owner auth address == collateral address. Can be used as an ID.
    CKeyID ownerAuthAddress;
    char ownerType;

    //! Operator auth address. Can be equal to ownerAuthAddress. Can be used as an ID
    CKeyID operatorAuthAddress;
    char operatorType;

    //! Consensus-enforced address for operator rewards.
    CKeyID rewardAddress;
    char rewardAddressType{0};

    //! MN creation block height
    int32_t creationHeight;
    //! Resign height
    int32_t resignHeight;
    //! Was used to set a ban height but is now unused
    int32_t version;

    //! This fields are for transaction rollback (by disconnecting block)
    uint256 resignTx;
    uint256 banTx;

    //! empty constructor
    CMasternode();

    State GetState(int height) const;
    bool IsActive(int height) const;

    static std::string GetHumanReadableState(State state);
    static std::string GetTimelockToString(TimeLock timelock);

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
        READWRITE(version);

        READWRITE(resignTx);
        READWRITE(banTx);

        // Only available after FortCanning
        if (version > PRE_FORT_CANNING) {
            READWRITE(rewardAddress);
            READWRITE(rewardAddressType);
        }
    }

    //! equality test
    friend bool operator==(CMasternode const & a, CMasternode const & b);
    friend bool operator!=(CMasternode const & a, CMasternode const & b);
};

struct CCreateMasterNodeMessage {
    char operatorType;
    CKeyID operatorAuthAddress;
    uint16_t timelock{0};

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(operatorType);
        READWRITE(operatorAuthAddress);

        // Only available after EunosPaya
        if (!s.eof()) {
            READWRITE(timelock);
        }
    }
};

struct CResignMasterNodeMessage : public uint256 {
    using uint256::uint256;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(uint256, *this);
    }
};

struct CSetForcedRewardAddressMessage {
    uint256 nodeId;
    char rewardAddressType;
    CKeyID rewardAddress;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nodeId);
        READWRITE(rewardAddressType);
        READWRITE(rewardAddress);
    }
};

struct CRemForcedRewardAddressMessage {
    uint256 nodeId;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nodeId);
    }
};

struct CUpdateMasterNodeMessage {
    uint256 mnId;
    char operatorType;
    CKeyID operatorAuthAddress;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(mnId);
        READWRITE(operatorType);
        READWRITE(operatorAuthAddress);
    }
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

    std::optional<CMasternode> GetMasternode(uint256 const & id) const;
    std::optional<uint256> GetMasternodeIdByOperator(CKeyID const & id) const;
    std::optional<uint256> GetMasternodeIdByOwner(CKeyID const & id) const;
    void ForEachMasternode(std::function<bool(uint256 const &, CLazySerialize<CMasternode>)> callback, uint256 const & start = uint256());

    void IncrementMintedBy(const uint256& nodeId);
    void DecrementMintedBy(const uint256& nodeId);

    std::optional<std::pair<CKeyID, uint256>> AmIOperator() const;
    std::optional<std::pair<CKeyID, uint256>> AmIOwner() const;

    // Multiple operator support
    std::set<std::pair<CKeyID, uint256>> GetOperatorsMulti() const;

    Res CreateMasternode(uint256 const & nodeId, CMasternode const & node, uint16_t timelock);
    Res ResignMasternode(uint256 const & nodeId, uint256 const & txid, int height);
    Res UnCreateMasternode(uint256 const & nodeId);
    Res UnResignMasternode(uint256 const & nodeId, uint256 const & resignTx);
    Res SetForcedRewardAddress(uint256 const & nodeId, const char rewardAddressType, CKeyID const & rewardAddress, int height);
    Res RemForcedRewardAddress(uint256 const & nodeId, int height);
    Res UpdateMasternode(uint256 const & nodeId, char operatorType, const CKeyID& operatorAuthAddress, int height);

    // Get blocktimes for non-subnode and subnode with fork logic
    std::vector<int64_t> GetBlockTimes(const CKeyID& keyID, const uint32_t blockHeight, const int32_t creationHeight, const uint16_t timelock);

    // Non-subnode block times
    void SetMasternodeLastBlockTime(const CKeyID & minter, const uint32_t &blockHeight, const int64_t &time);
    std::optional<int64_t> GetMasternodeLastBlockTime(const CKeyID & minter, const uint32_t height);
    void EraseMasternodeLastBlockTime(const uint256 &minter, const uint32_t& blockHeight);
    void ForEachMinterNode(std::function<bool(MNBlockTimeKey const &, CLazySerialize<int64_t>)> callback, MNBlockTimeKey const & start = {});

    // Subnode block times
    void SetSubNodesBlockTime(const CKeyID & minter, const uint32_t &blockHeight, const uint8_t id, const int64_t& time);
    std::vector<int64_t> GetSubNodesBlockTime(const CKeyID & minter, const uint32_t height);
    void EraseSubNodesLastBlockTime(const uint256& nodeId, const uint32_t& blockHeight);
    void ForEachSubNode(std::function<bool(SubNodeBlockTimeKey const &, CLazySerialize<int64_t>)> callback, SubNodeBlockTimeKey const & start = {});

    uint16_t GetTimelock(const uint256& nodeId, const CMasternode& node, const uint64_t height) const;

    // tags
    struct ID       { static constexpr uint8_t prefix() { return 'M'; } };
    struct Operator { static constexpr uint8_t prefix() { return 'o'; } };
    struct Owner    { static constexpr uint8_t prefix() { return 'w'; } };

    // For storing last staked block time
    struct Staker   { static constexpr uint8_t prefix() { return 'X'; } };
    struct SubNode  { static constexpr uint8_t prefix() { return 'Z'; } };

    // Store long term time lock
    struct Timelock { static constexpr uint8_t prefix() { return 'K'; } };
};

class CLastHeightView : public virtual CStorageView
{
public:
    int GetLastHeight() const;
    void SetLastHeight(int height);

    struct Height { static constexpr uint8_t prefix() { return 'H'; } };
};

class CFoundationsDebtView : public virtual CStorageView
{
public:
    CAmount GetFoundationsDebt() const;
    void SetFoundationsDebt(CAmount debt);

    struct Debt { static constexpr uint8_t prefix() { return 'd'; } };
};

class CTeamView : public virtual CStorageView
{
public:
    using CTeam = CAnchorData::CTeam;

    void SetTeam(CTeam const & newTeam);
    void SetAnchorTeams(CTeam const & authTeam, CTeam const & confirmTeam, const int height);

    CTeam GetCurrentTeam() const;
    std::optional<CTeam> GetAuthTeam(int height) const;
    std::optional<CTeam> GetConfirmTeam(int height) const;

    struct AuthTeam     { static constexpr uint8_t prefix() { return 'v'; } };
    struct ConfirmTeam  { static constexpr uint8_t prefix() { return 'V'; } };
    struct CurrentTeam  { static constexpr uint8_t prefix() { return 't'; } };
};

class CAnchorRewardsView : public virtual CStorageView
{
public:
    using RewardTxHash = uint256;
    using AnchorTxHash = uint256;

    std::optional<RewardTxHash> GetRewardForAnchor(AnchorTxHash const &btcTxHash) const;

    void AddRewardForAnchor(AnchorTxHash const &btcTxHash, RewardTxHash const & rewardTxHash);
    void RemoveRewardForAnchor(AnchorTxHash const &btcTxHash);
    void ForEachAnchorReward(std::function<bool(AnchorTxHash const &, CLazySerialize<RewardTxHash>)> callback);

    struct BtcTx { static constexpr uint8_t prefix() { return 'r'; } };
};

class CAnchorConfirmsView : public virtual CStorageView
{
public:
    using AnchorTxHash = uint256;

    std::vector<CAnchorConfirmDataPlus> GetAnchorConfirmData();

    void AddAnchorConfirmData(const CAnchorConfirmDataPlus& data);
    void EraseAnchorConfirmData(const uint256 btcTxHash);
    void ForEachAnchorConfirmData(std::function<bool(const AnchorTxHash &, CLazySerialize<CAnchorConfirmDataPlus>)> callback);

    struct BtcTx { static constexpr uint8_t prefix() { return 'x'; } };
};

class CCollateralLoans { // in USD

    double calcRatio(uint64_t maxRatio) const;

public:
    uint64_t totalCollaterals;
    uint64_t totalLoans;
    std::vector<CTokenAmount> collaterals;
    std::vector<CTokenAmount> loans;

    uint32_t ratio() const;
    CAmount precisionRatio() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(totalCollaterals);
        READWRITE(totalLoans);
        READWRITE(collaterals);
        READWRITE(loans);
    }
};

template<typename T>
inline void CheckPrefix()
{
}

template<typename T1, typename T2, typename... TN>
inline void CheckPrefix()
{
    static_assert(T1::prefix() != T2::prefix(), "prefixes are equal");
    CheckPrefix<T1, TN...>();
    CheckPrefix<T2, TN...>();
}

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
    void CheckPrefixes()
    {
        CheckPrefix<
            CMasternodesView        ::  ID, Operator, Owner, Staker, SubNode, Timelock,
            CLastHeightView         ::  Height,
            CTeamView               ::  AuthTeam, ConfirmTeam, CurrentTeam,
            CFoundationsDebtView    ::  Debt,
            CAnchorRewardsView      ::  BtcTx,
            CTokensView             ::  ID, Symbol, CreationTx, LastDctId,
            CAccountsView           ::  ByBalanceKey, ByHeightKey,
            CCommunityBalancesView  ::  ById,
            CUndosView              ::  ByUndoKey,
            CPoolPairView           ::  ByID, ByPair, ByShare, ByIDPair, ByPoolSwap, ByReserves, ByRewardPct, ByRewardLoanPct,
                                        ByPoolReward, ByDailyReward, ByCustomReward, ByTotalLiquidity, ByDailyLoanReward,
                                        ByPoolLoanReward, ByTokenDexFeePct,
            CGovView                ::  ByName, ByHeightVars,
            CAnchorConfirmsView     ::  BtcTx,
            COracleView             ::  ByName, FixedIntervalBlockKey, FixedIntervalPriceKey, PriceDeviation,
            CICXOrderView           ::  ICXOrderCreationTx, ICXMakeOfferCreationTx, ICXSubmitDFCHTLCCreationTx,
                                        ICXSubmitEXTHTLCCreationTx, ICXClaimDFCHTLCCreationTx, ICXCloseOrderCreationTx,
                                        ICXCloseOfferCreationTx, ICXOrderOpenKey, ICXOrderCloseKey, ICXMakeOfferOpenKey,
                                        ICXMakeOfferCloseKey, ICXSubmitDFCHTLCOpenKey, ICXSubmitDFCHTLCCloseKey,
                                        ICXSubmitEXTHTLCOpenKey, ICXSubmitEXTHTLCCloseKey, ICXClaimDFCHTLCKey,
                                        ICXOrderStatus, ICXOfferStatus, ICXSubmitDFCHTLCStatus, ICXSubmitEXTHTLCStatus, ICXVariables,
            CLoanView               ::  LoanSetCollateralTokenCreationTx, LoanSetCollateralTokenKey, LoanSetLoanTokenCreationTx,
                                        LoanSetLoanTokenKey, LoanSchemeKey, DefaultLoanSchemeKey, DelayedLoanSchemeKey,
                                        DestroyLoanSchemeKey, LoanInterestByVault, LoanTokenAmount, LoanLiquidationPenalty, LoanInterestV2ByVault,
            CVaultView              ::  VaultKey, OwnerVaultKey, CollateralKey, AuctionBatchKey, AuctionHeightKey, AuctionBidKey
        >();
    }
private:
    Res PopulateLoansData(CCollateralLoans& result, CVaultId const& vaultId, uint32_t height, int64_t blockTime, bool useNextPrice, bool requireLivePrice);
    Res PopulateCollateralData(CCollateralLoans& result, CVaultId const& vaultId, CBalances const& collaterals, uint32_t height, int64_t blockTime, bool useNextPrice, bool requireLivePrice);

public:
    // Increase version when underlaying tables are changed
    static constexpr const int DbVersion = 1;

    CCustomCSView()
    {
        CheckPrefixes();
    }

    CCustomCSView(CStorageView&) = delete;
    CCustomCSView(const CCustomCSView&) = delete;

    CCustomCSView(std::shared_ptr<CStorageKV> st) : CStorageView(st)
    {
        CheckPrefixes();
    }

    // cache-upon-a-cache (not a copy!) constructor
    CCustomCSView(CCustomCSView & other) : CStorageView(other)
    {
        CheckPrefixes();
    }

    // cause depends on current mns:
    CTeamView::CTeam CalcNextTeam(int height, uint256 const & stakeModifier);

    // Generate auth and custom anchor teams based on current block
    void CalcAnchoringTeams(uint256 const & stakeModifier, const CBlockIndex *pindexNew);

    /// @todo newbase move to networking?
    void CreateAndRelayConfirmMessageIfNeed(const CAnchorIndex::AnchorRec* anchor, const uint256 & btcTxHash, const CKey &masternodeKey);

    void AddUndo(CCustomCSView & cache, uint256 const & txid, uint32_t height);

    // simplified version of undo, without any unnecessary undo data
    void OnUndoTx(uint256 const & txid, uint32_t height);

    bool CanSpend(const uint256 & txId, int height) const;

    bool CalculateOwnerRewards(CScript const & owner, uint32_t height);

    ResVal<CAmount> GetAmountInCurrency(CAmount amount, CTokenCurrencyPair priceFeedId, bool useNextPrice = false, bool requireLivePrice = true);

    ResVal<CCollateralLoans> GetLoanCollaterals(CVaultId const & vaultId, CBalances const & collaterals, uint32_t height, int64_t blockTime, bool useNextPrice = false, bool requireLivePrice = true);

    ResVal<CAmount> GetValidatedIntervalPrice(CTokenCurrencyPair priceFeedId, bool useNextPrice, bool requireLivePrice);

    void SetDbVersion(int version);

    int GetDbVersion() const;

    uint256 MerkleRoot();

    struct DbVersion { static constexpr uint8_t prefix() { return 'D'; } };
};

std::map<CKeyID, CKey> AmISignerNow(int height, CAnchorData::CTeam const & team);

extern std::unique_ptr<CCustomCSView> pcustomcsview;

#endif // DEFI_MASTERNODES_MASTERNODES_H
