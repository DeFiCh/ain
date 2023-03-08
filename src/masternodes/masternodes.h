// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MASTERNODES_H
#define DEFI_MASTERNODES_MASTERNODES_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/accounts.h>
#include <masternodes/anchors.h>
#include <masternodes/gv.h>
#include <masternodes/historywriter.h>
#include <masternodes/icxorder.h>
#include <masternodes/incentivefunding.h>
#include <masternodes/loan.h>
#include <masternodes/oracles.h>
#include <masternodes/poolpairs.h>
#include <masternodes/proposals.h>
#include <masternodes/tokens.h>
#include <masternodes/undos.h>
#include <masternodes/vault.h>
#include <pubkey.h>
#include <serialize.h>
#include <uint256.h>
#include <wallet/ismine.h>

#include <stdint.h>
#include <functional>
#include <iostream>
#include <map>
#include <set>

class CAccountHistoryStorage;
class CBlockIndex;
class CMasternodesView;
class CTransaction;
class CVaultHistoryStorage;

// Works instead of constants cause 'regtest' differs (don't want to overcharge chainparams)
int GetMnActivationDelay(int height);
int GetMnResignDelay(int height);
CAmount GetTokenCollateralAmount();
CAmount GetMnCreationFee(int height);
CAmount GetTokenCreationFee(int height);
CAmount GetMnCollateralAmount(int height);
CAmount GetProposalCreationFee(int height, const CCustomCSView &view, const CCreateProposalMessage &msg);

// Update owner rewards for MNs missing call to CalculateOwnerRewards after voter fee distributions.
// Missing call fixed in: https://github.com/DeFiCh/ain/pull/1766
void CalcMissingRewardTempFix(CCustomCSView &mnview, const uint32_t targetHeight, const CWallet &wallet);

enum class UpdateMasternodeType : uint8_t {
    None             = 0x00,
    OwnerAddress     = 0x01,
    OperatorAddress  = 0x02,
    SetRewardAddress = 0x03,
    RemRewardAddress = 0x04
};

constexpr uint8_t SUBNODE_COUNT{4};

class CMasternode {
public:
    enum State {
        PRE_ENABLED,
        ENABLED,
        PRE_RESIGNED,
        RESIGNED,
        TRANSFERRING,
        UNKNOWN  // unreachable
    };

    enum TimeLock { ZEROYEAR, FIVEYEAR = 260, TENYEAR = 520 };

    enum Version : int32_t {
        PRE_FORT_CANNING = -1,
        VERSION0         = 0,
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
    uint256 collateralTx;

    //! empty constructor
    CMasternode();

    State GetState(int height, const CMasternodesView &mnview) const;
    bool IsActive(int height, const CMasternodesView &mnview) const;

    static std::string GetHumanReadableState(State state);
    static std::string GetTimelockToString(TimeLock timelock);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(mintedBlocks);
        READWRITE(ownerAuthAddress);
        READWRITE(ownerType);
        READWRITE(operatorAuthAddress);
        READWRITE(operatorType);

        READWRITE(creationHeight);
        READWRITE(resignHeight);
        READWRITE(version);

        READWRITE(resignTx);
        READWRITE(collateralTx);

        // Only available after FortCanning
        if (version > PRE_FORT_CANNING) {
            READWRITE(rewardAddress);
            READWRITE(rewardAddressType);
        }
    }

    //! equality test
    friend bool operator==(const CMasternode &a, const CMasternode &b);
    friend bool operator!=(const CMasternode &a, const CMasternode &b);
};

struct MNBlockTimeKey {
    uint256 masternodeID;
    uint32_t blockHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
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

struct SubNodeBlockTimeKey {
    uint256 masternodeID;
    uint8_t subnode;
    uint32_t blockHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
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

struct MNNewOwnerHeightValue {
    uint32_t blockHeight;
    uint256 masternodeID;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(blockHeight);
        READWRITE(masternodeID);
    }
};

class CMasternodesView : public virtual CStorageView {
    std::map<CKeyID, std::pair<uint32_t, int64_t>> minterTimeCache;

public:
    std::optional<CMasternode> GetMasternode(const uint256 &id) const;
    std::optional<uint256> GetMasternodeIdByOperator(const CKeyID &id) const;
    std::optional<uint256> GetMasternodeIdByOwner(const CKeyID &id) const;
    void ForEachMasternode(std::function<bool(const uint256 &, CLazySerialize<CMasternode>)> callback,
                           const uint256 &start = uint256());

    void IncrementMintedBy(const uint256 &nodeId);
    void DecrementMintedBy(const uint256 &nodeId);

    std::optional<std::pair<CKeyID, uint256>> AmIOperator() const;
    std::optional<std::pair<CKeyID, uint256>> AmIOwner() const;

    // Multiple operator support
    std::set<std::pair<CKeyID, uint256>> GetOperatorsMulti() const;

    Res CreateMasternode(const uint256 &nodeId, const CMasternode &node, uint16_t timelock);
    Res ResignMasternode(CMasternode &node, const uint256 &nodeId, const uint256 &txid, int height);

    // Masternode updates
    void SetForcedRewardAddress(const uint256 &nodeId,
                                CMasternode &node,
                                const char rewardAddressType,
                                const CKeyID &rewardAddress,
                                int height);
    void RemForcedRewardAddress(const uint256 &nodeId, CMasternode &node, int height);
    void UpdateMasternodeOperator(const uint256 &nodeId,
                                  CMasternode &node,
                                  const char operatorType,
                                  const CKeyID &operatorAuthAddress,
                                  int height);
    void UpdateMasternodeOwner(const uint256 &nodeId,
                               CMasternode &node,
                               const char ownerType,
                               const CKeyID &ownerAuthAddress);
    void UpdateMasternodeCollateral(const uint256 &nodeId,
                                    CMasternode &node,
                                    const uint256 &newCollateralTx,
                                    const int height);
    [[nodiscard]] std::optional<MNNewOwnerHeightValue> GetNewCollateral(const uint256 &txid) const;
    [[nodiscard]] std::optional<uint32_t> GetPendingHeight(const CKeyID &ownerAuthAddress) const;
    void ForEachNewCollateral(std::function<bool(const uint256 &, CLazySerialize<MNNewOwnerHeightValue>)> callback);
    void ForEachPendingHeight(std::function<bool(const CKeyID &ownerAuthAddress, const uint32_t &height)> callback);
    void ErasePendingHeight(const CKeyID &ownerAuthAddress);

    // Get blocktimes for non-subnode and subnode with fork logic
    std::vector<int64_t> GetBlockTimes(const CKeyID &keyID,
                                       const uint32_t blockHeight,
                                       const int32_t creationHeight,
                                       const uint16_t timelock);

    // Non-subnode block times
    void SetMasternodeLastBlockTime(const CKeyID &minter, const uint32_t &blockHeight, const int64_t &time);
    std::optional<int64_t> GetMasternodeLastBlockTime(const CKeyID &minter, const uint32_t height);
    void EraseMasternodeLastBlockTime(const uint256 &minter, const uint32_t &blockHeight);
    void ForEachMinterNode(std::function<bool(const MNBlockTimeKey &, CLazySerialize<int64_t>)> callback,
                           const MNBlockTimeKey &start = {uint256{}, std::numeric_limits<uint32_t>::max()});

    // Subnode block times
    void SetSubNodesBlockTime(const CKeyID &minter, const uint32_t &blockHeight, const uint8_t id, const int64_t &time);
    std::vector<int64_t> GetSubNodesBlockTime(const CKeyID &minter, const uint32_t height);
    void EraseSubNodesLastBlockTime(const uint256 &nodeId, const uint32_t &blockHeight);
    void ForEachSubNode(std::function<bool(const SubNodeBlockTimeKey &, CLazySerialize<int64_t>)> callback,
                        const SubNodeBlockTimeKey &start = {uint256{},
                                                            uint8_t{},
                                                            std::numeric_limits<uint32_t>::max()});

    std::optional<uint16_t> GetTimelock(const uint256 &nodeId, const CMasternode &node, const uint64_t height) const;

    // tags
    struct ID {
        static constexpr uint8_t prefix() { return 'M'; }
    };
    struct Operator {
        static constexpr uint8_t prefix() { return 'o'; }
    };
    struct Owner {
        static constexpr uint8_t prefix() { return 'w'; }
    };
    struct NewCollateral {
        static constexpr uint8_t prefix() { return 's'; }
    };
    struct PendingHeight {
        static constexpr uint8_t prefix() { return 'E'; }
    };

    // For storing last staked block time
    struct Staker {
        static constexpr uint8_t prefix() { return 'X'; }
    };
    struct SubNode {
        static constexpr uint8_t prefix() { return 'Z'; }
    };

    // Store long term time lock
    struct Timelock {
        static constexpr uint8_t prefix() { return 'K'; }
    };
};

class CLastHeightView : public virtual CStorageView {
public:
    int GetLastHeight() const;
    void SetLastHeight(int height);

    struct Height {
        static constexpr uint8_t prefix() { return 'H'; }
    };
};

class CFoundationsDebtView : public virtual CStorageView {
public:
    CAmount GetFoundationsDebt() const;
    void SetFoundationsDebt(CAmount debt);

    struct Debt {
        static constexpr uint8_t prefix() { return 'd'; }
    };
};

class CTeamView : public virtual CStorageView {
public:
    using CTeam = CAnchorData::CTeam;

    void SetTeam(const CTeam &newTeam);
    void SetAnchorTeams(const CTeam &authTeam, const CTeam &confirmTeam, const int height);

    CTeam GetCurrentTeam() const;
    std::optional<CTeam> GetAuthTeam(int height) const;
    std::optional<CTeam> GetConfirmTeam(int height) const;

    struct AuthTeam {
        static constexpr uint8_t prefix() { return 'v'; }
    };
    struct ConfirmTeam {
        static constexpr uint8_t prefix() { return 'V'; }
    };
    struct CurrentTeam {
        static constexpr uint8_t prefix() { return 't'; }
    };
};

class CAnchorRewardsView : public virtual CStorageView {
public:
    using RewardTxHash = uint256;
    using AnchorTxHash = uint256;

    std::optional<RewardTxHash> GetRewardForAnchor(const AnchorTxHash &btcTxHash) const;

    void AddRewardForAnchor(const AnchorTxHash &btcTxHash, const RewardTxHash &rewardTxHash);
    void RemoveRewardForAnchor(const AnchorTxHash &btcTxHash);
    void ForEachAnchorReward(std::function<bool(const AnchorTxHash &, CLazySerialize<RewardTxHash>)> callback);

    struct BtcTx {
        static constexpr uint8_t prefix() { return 'r'; }
    };
};

class CAnchorConfirmsView : public virtual CStorageView {
public:
    using AnchorTxHash = uint256;

    std::vector<CAnchorConfirmDataPlus> GetAnchorConfirmData();

    void AddAnchorConfirmData(const CAnchorConfirmDataPlus &data);
    void EraseAnchorConfirmData(const uint256 btcTxHash);
    void ForEachAnchorConfirmData(
        std::function<bool(const AnchorTxHash &, CLazySerialize<CAnchorConfirmDataPlus>)> callback);

    struct BtcTx {
        static constexpr uint8_t prefix() { return 'x'; }
    };
};

class CSettingsView : public virtual CStorageView {
public:
    const std::string DEX_STATS_LAST_HEIGHT = "DexStatsLastHeight";
    const std::string DEX_STATS_ENABLED     = "DexStatsEnabled";
    const std::string MN_REWARD_ADDRESSES   = "MNRewardAddresses";

    void SetDexStatsLastHeight(int32_t height);
    std::optional<int32_t> GetDexStatsLastHeight();
    void SetDexStatsEnabled(bool enabled);
    std::optional<bool> GetDexStatsEnabled();

    std::optional<std::set<CScript>> SettingsGetRewardAddresses();
    void SettingsSetRewardAddresses(const std::set<CScript> &addresses);

    struct KVSettings {
        static constexpr uint8_t prefix() { return '0'; }
    };
};

class CCollateralLoans {  // in USD

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
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(totalCollaterals);
        READWRITE(totalLoans);
        READWRITE(collaterals);
        READWRITE(loans);
    }
};

template <typename T>
inline void CheckPrefix() {}

template <typename T1, typename T2, typename... TN>
inline void CheckPrefix() {
    static_assert(T1::prefix() != T2::prefix(), "prefixes are equal");
    CheckPrefix<T1, TN...>();
    CheckPrefix<T2, TN...>();
}

class CCustomCSView : public CMasternodesView,
                      public CLastHeightView,
                      public CTeamView,
                      public CFoundationsDebtView,
                      public CAnchorRewardsView,
                      public CTokensView,
                      public CAccountsView,
                      public CCommunityBalancesView,
                      public CUndosView,
                      public CPoolPairView,
                      public CGovView,
                      public CAnchorConfirmsView,
                      public COracleView,
                      public CICXOrderView,
                      public CLoanView,
                      public CVaultView,
                      public CSettingsView,
                      public CProposalView {
    // clang-format off
    void CheckPrefixes()
    {
        CheckPrefix<
            CMasternodesView        ::  ID, NewCollateral, PendingHeight, Operator, Owner, Staker, SubNode, Timelock,
            CLastHeightView         ::  Height,
            CTeamView               ::  AuthTeam, ConfirmTeam, CurrentTeam,
            CFoundationsDebtView    ::  Debt,
            CAnchorRewardsView      ::  BtcTx,
            CTokensView             ::  ID, Symbol, CreationTx, LastDctId,
            CAccountsView           ::  ByBalanceKey, ByHeightKey, ByFuturesSwapKey,
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
                                        LoanInterestV3ByVault,
            CVaultView              ::  VaultKey, OwnerVaultKey, CollateralKey, AuctionBatchKey, AuctionHeightKey, AuctionBidKey,
            CSettingsView           ::  KVSettings,
            CProposalView              ::  ByType, ByCycle, ByMnVote, ByStatus
        >();
    }
    // clang-format on

private:
    Res PopulateLoansData(CCollateralLoans &result,
                          const CVaultId &vaultId,
                          uint32_t height,
                          int64_t blockTime,
                          bool useNextPrice,
                          bool requireLivePrice);
    Res PopulateCollateralData(CCollateralLoans &result,
                               const CVaultId &vaultId,
                               const CBalances &collaterals,
                               uint32_t height,
                               int64_t blockTime,
                               bool useNextPrice,
                               bool requireLivePrice);

protected:
    CHistoryWriters writers;

public:
    // Increase version when underlaying tables are changed
    static constexpr const int DbVersion = 1;

    CCustomCSView();
    explicit CCustomCSView(CStorageKV &st);

    // cache-upon-a-cache (not a copy!) constructor
    CCustomCSView(CCustomCSView &other);
    CCustomCSView(CCustomCSView &other,
                  CAccountHistoryStorage *historyView,
                  CBurnHistoryStorage *burnView,
                  CVaultHistoryStorage *vaultView);

    ~CCustomCSView() = default;

    // cause depends on current mns:
    CTeamView::CTeam CalcNextTeam(int height, const uint256 &stakeModifier);

    // Generate auth and custom anchor teams based on current block
    void CalcAnchoringTeams(const uint256 &stakeModifier, const CBlockIndex *pindexNew);

    /// @todo newbase move to networking?
    void CreateAndRelayConfirmMessageIfNeed(const CAnchorIndex::AnchorRec *anchor,
                                            const uint256 &btcTxHash,
                                            const CKey &masternodeKey);

    // simplified version of undo, without any unnecessary undo data
    void OnUndoTx(const uint256 &txid, uint32_t height);

    bool CanSpend(const uint256 &txId, int height) const;

    bool CalculateOwnerRewards(const CScript &owner, uint32_t height);

    ResVal<CAmount> GetAmountInCurrency(CAmount amount,
                                        CTokenCurrencyPair priceFeedId,
                                        bool useNextPrice     = false,
                                        bool requireLivePrice = true);

    ResVal<CCollateralLoans> GetLoanCollaterals(const CVaultId &vaultId,
                                                const CBalances &collaterals,
                                                uint32_t height,
                                                int64_t blockTime,
                                                bool useNextPrice     = false,
                                                bool requireLivePrice = true);

    ResVal<CAmount> GetValidatedIntervalPrice(const CTokenCurrencyPair &priceFeedId,
                                              bool useNextPrice,
                                              bool requireLivePrice);

    [[nodiscard]] bool AreTokensLocked(const std::set<uint32_t> &tokenIds) const override;
    [[nodiscard]] std::optional<CTokenImpl> GetTokenGuessId(const std::string &str, DCT_ID &id) const override;
    [[nodiscard]] std::optional<CLoanSetLoanTokenImpl> GetLoanTokenByID(DCT_ID const &id) const override;
    [[nodiscard]] std::optional<CLoanSetLoanTokenImplementation> GetLoanTokenFromAttributes(
        const DCT_ID &id) const override;
    [[nodiscard]] std::optional<CLoanSetCollateralTokenImpl> GetCollateralTokenFromAttributes(
        const DCT_ID &id) const override;

    void SetDbVersion(int version);

    int GetDbVersion() const;

    uint256 MerkleRoot();

    //virtual CHistoryWriters& GetHistoryWriters() { return writers; }
    virtual CHistoryWriters& GetHistoryWriters() { return writers; }

    // we construct it as it
    CFlushableStorageKV &GetStorage() { return static_cast<CFlushableStorageKV &>(DB()); }

    uint32_t GetVotingPeriodFromAttributes() const override;
    uint32_t GetEmergencyPeriodFromAttributes(const CProposalType &type) const override;
    CAmount GetApprovalThresholdFromAttributes(const CProposalType &type) const override;
    CAmount GetQuorumFromAttributes(const CProposalType &type, bool emergency = false) const override;
    CAmount GetFeeBurnPctFromAttributes() const override;

    struct DbVersion {
        static constexpr uint8_t prefix() { return 'D'; }
    };
};

std::map<CKeyID, CKey> AmISignerNow(int height, const CAnchorData::CTeam &team);

/** Global DB and view that holds enhanced chainstate data (should be protected by cs_main) */
extern std::unique_ptr<CStorageLevelDB> pcustomcsDB;
extern std::unique_ptr<CCustomCSView> pcustomcsview;

#endif  // DEFI_MASTERNODES_MASTERNODES_H
