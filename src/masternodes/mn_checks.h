// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MN_CHECKS_H
#define DEFI_MASTERNODES_MN_CHECKS_H

#include <consensus/params.h>
#include <consensus/tx_check.h>
#include <masternodes/evm.h>
#include <masternodes/masternodes.h>
#include <cstring>
#include <vector>

#include <variant>

class CBlock;
class CTransaction;
class CTxMemPool;
class CCoinsViewCache;

class CCustomCSView;
class CCustomTxVisitor {
protected:
    uint32_t height;
    CCustomCSView &mnview;
    const CTransaction &tx;
    const CCoinsViewCache &coins;
    const Consensus::Params &consensus;

public:
    CCustomTxVisitor(const CTransaction &tx,
                     uint32_t height,
                     const CCoinsViewCache &coins,
                     CCustomCSView &mnview,
                     const Consensus::Params &consensus);

protected:
    Res HasAuth(const CScript &auth) const;
    Res HasCollateralAuth(const uint256 &collateralTx) const;
    Res HasFoundationAuth() const;
    Res CheckMasternodeCreationTx() const;
    Res CheckProposalTx(const CCreateProposalMessage &msg) const;
    Res CheckTokenCreationTx() const;
    Res CheckCustomTx() const;
    Res TransferTokenBalance(DCT_ID id, CAmount amount, const CScript &from, const CScript &to) const;
    DCT_ID FindTokenByPartialSymbolName(const std::string &symbol) const;
    CPoolPair GetBTCDFIPoolPair() const;
    CAmount CalculateTakerFee(CAmount amount) const;
    ResVal<CScript> MintableToken(DCT_ID id, const CTokenImplementation &token, bool anybodyCanMint) const;
    Res EraseEmptyBalances(TAmounts &balances) const;
    Res SetShares(const CScript &owner, const TAmounts &balances) const;
    Res DelShares(const CScript &owner, const TAmounts &balances) const;
    void CalculateOwnerRewards(const CScript &owner) const;
    Res SubBalanceDelShares(const CScript &owner, const CBalances &balance) const;
    Res AddBalanceSetShares(const CScript &owner, const CBalances &balance) const;
    Res AddBalancesSetShares(const CAccounts &accounts) const;
    Res SubBalancesDelShares(const CAccounts &accounts) const;
    Res NormalizeTokenCurrencyPair(std::set<CTokenCurrencyPair> &tokenCurrency) const;
    bool IsTokensMigratedToGovVar() const;
    Res IsOnChainGovernanceEnabled() const;
};

constexpr uint8_t MAX_POOL_SWAPS = 3;

enum CustomTxErrCodes : uint32_t {
    NotSpecified = 0,
    //    NotCustomTx  = 1,
    NotEnoughBalance = 1024,
    Fatal            = uint32_t(1) << 31  // not allowed to fail
};

enum class CustomTxType : uint8_t {
    None   = 0,
    Reject = 1,  // Invalid TX type. Returned by GuessCustomTxType on invalid custom TX.

    // masternodes:
    CreateMasternode = 'C',
    ResignMasternode = 'R',
    UpdateMasternode = 'm',
    // custom tokens:
    CreateToken    = 'T',
    MintToken      = 'M',
    BurnToken      = 'F',
    UpdateToken    = 'N',  // previous type, only DAT flag triggers
    UpdateTokenAny = 'n',  // new type of token's update with any flags/fields possible
    // poolpair
    CreatePoolPair      = 'p',
    UpdatePoolPair      = 'u',
    PoolSwap            = 's',
    PoolSwapV2          = 'i',
    AddPoolLiquidity    = 'l',
    RemovePoolLiquidity = 'r',
    // accounts
    UtxosToAccount        = 'U',
    AccountToUtxos        = 'b',
    AccountToAccount      = 'B',
    AnyAccountsToAccounts = 'a',
    SmartContract         = 'K',
    FutureSwap            = 'Q',
    // set governance variable
    SetGovVariable       = 'G',
    SetGovVariableHeight = 'j',
    // Auto auth TX
    AutoAuthPrep = 'A',
    // oracles
    AppointOracle       = 'o',
    RemoveOracleAppoint = 'h',
    UpdateOracleAppoint = 't',
    SetOracleData       = 'y',
    // ICX
    ICXCreateOrder   = '1',
    ICXMakeOffer     = '2',
    ICXSubmitDFCHTLC = '3',
    ICXSubmitEXTHTLC = '4',
    ICXClaimDFCHTLC  = '5',
    ICXCloseOrder    = '6',
    ICXCloseOffer    = '7',
    // Loans
    SetLoanCollateralToken = 'c',
    SetLoanToken           = 'g',
    UpdateLoanToken        = 'x',
    LoanScheme             = 'L',
    DefaultLoanScheme      = 'd',
    DestroyLoanScheme      = 'D',
    Vault                  = 'V',
    CloseVault             = 'e',
    UpdateVault            = 'v',
    DepositToVault         = 'S',
    WithdrawFromVault      = 'J',
    PaybackWithCollateral  = 'W',
    TakeLoan               = 'X',
    PaybackLoan            = 'H',
    PaybackLoanV2          = 'k',
    AuctionBid             = 'I',
    // Marker TXs
    FutureSwapExecution = 'q',
    FutureSwapRefund    = 'w',
    TokenSplit          = 'P',
    // On-Chain-Gov
    CreateCfp                 = 'z',
    Vote                      = 'O',  // NOTE: Check whether this overlapping with CreateOrder above is fine
    CreateVoc                 = 'E',  // NOTE: Check whether this overlapping with DestroyOrder above is fine
    ProposalFeeRedistribution = 'Y',
    UnsetGovVariable          = 'Z',
    // EVM
    TransferBalance                  = '8',
    EvmTx                     = '9',
};

inline CustomTxType CustomTxCodeToType(uint8_t ch) {
    auto type = static_cast<CustomTxType>(ch);
    switch (type) {
        case CustomTxType::CreateMasternode:
        case CustomTxType::ResignMasternode:
        case CustomTxType::UpdateMasternode:
        case CustomTxType::CreateToken:
        case CustomTxType::MintToken:
        case CustomTxType::BurnToken:
        case CustomTxType::UpdateToken:
        case CustomTxType::UpdateTokenAny:
        case CustomTxType::CreatePoolPair:
        case CustomTxType::UpdatePoolPair:
        case CustomTxType::PoolSwap:
        case CustomTxType::PoolSwapV2:
        case CustomTxType::AddPoolLiquidity:
        case CustomTxType::RemovePoolLiquidity:
        case CustomTxType::UtxosToAccount:
        case CustomTxType::AccountToUtxos:
        case CustomTxType::AccountToAccount:
        case CustomTxType::AnyAccountsToAccounts:
        case CustomTxType::SmartContract:
        case CustomTxType::FutureSwap:
        case CustomTxType::SetGovVariable:
        case CustomTxType::SetGovVariableHeight:
        case CustomTxType::AutoAuthPrep:
        case CustomTxType::AppointOracle:
        case CustomTxType::RemoveOracleAppoint:
        case CustomTxType::UpdateOracleAppoint:
        case CustomTxType::SetOracleData:
        case CustomTxType::ICXCreateOrder:
        case CustomTxType::ICXMakeOffer:
        case CustomTxType::ICXSubmitDFCHTLC:
        case CustomTxType::ICXSubmitEXTHTLC:
        case CustomTxType::ICXClaimDFCHTLC:
        case CustomTxType::ICXCloseOrder:
        case CustomTxType::ICXCloseOffer:
        case CustomTxType::SetLoanCollateralToken:
        case CustomTxType::SetLoanToken:
        case CustomTxType::UpdateLoanToken:
        case CustomTxType::LoanScheme:
        case CustomTxType::DefaultLoanScheme:
        case CustomTxType::DestroyLoanScheme:
        case CustomTxType::Vault:
        case CustomTxType::CloseVault:
        case CustomTxType::UpdateVault:
        case CustomTxType::DepositToVault:
        case CustomTxType::WithdrawFromVault:
        case CustomTxType::PaybackWithCollateral:
        case CustomTxType::TakeLoan:
        case CustomTxType::PaybackLoan:
        case CustomTxType::PaybackLoanV2:
        case CustomTxType::AuctionBid:
        case CustomTxType::FutureSwapExecution:
        case CustomTxType::FutureSwapRefund:
        case CustomTxType::TokenSplit:
        case CustomTxType::Reject:
        case CustomTxType::CreateCfp:
        case CustomTxType::ProposalFeeRedistribution:
        case CustomTxType::Vote:
        case CustomTxType::CreateVoc:
        case CustomTxType::UnsetGovVariable:
        case CustomTxType::TransferBalance:
        case CustomTxType::EvmTx:
        case CustomTxType::None:
            return type;
    }
    return CustomTxType::None;
}

std::string ToString(CustomTxType type);
CustomTxType FromString(const std::string &str);

// it's disabled after Dakota height
inline bool NotAllowedToFail(CustomTxType txType, int height) {
    return (height < Params().GetConsensus().DakotaHeight &&
            (txType == CustomTxType::MintToken || txType == CustomTxType::AccountToUtxos));
}

template <typename Stream>
inline void Serialize(Stream &s, CustomTxType txType) {
    Serialize(s, static_cast<unsigned char>(txType));
}

template <typename Stream>
inline void Unserialize(Stream &s, CustomTxType &txType) {
    unsigned char ch;
    Unserialize(s, ch);

    txType = CustomTxCodeToType(ch);
}

struct CCreateMasterNodeMessage {
    char operatorType;
    CKeyID operatorAuthAddress;
    uint16_t timelock{0};

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
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
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(uint256, *this);
    }
};

struct CUpdateMasterNodeMessage {
    uint256 mnId;
    std::vector<std::pair<uint8_t, std::pair<char, std::vector<unsigned char>>>> updates;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(mnId);
        READWRITE(updates);
    }
};

struct CCreateTokenMessage : public CToken {
    using CToken::CToken;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CToken, *this);
    }
};

struct CUpdateTokenPreAMKMessage {
    uint256 tokenTx;
    bool isDAT;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(tokenTx);
        READWRITE(isDAT);
    }
};

struct CUpdateTokenMessage {
    uint256 tokenTx;
    CToken token;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(tokenTx);
        READWRITE(token);
    }
};

struct CMintTokensMessage : public CBalances {
    using CBalances::CBalances;
    CScript to;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CBalances, *this);

        if (!s.eof()) {
            READWRITE(to);
        }
    }
};

struct CBurnTokensMessage {
    enum BurnType : uint8_t {
        TokenBurn = 0,
    };

    CBalances amounts;
    CScript from;
    BurnType burnType;
    std::variant<CScript> context;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(amounts);
        READWRITE(from);
        READWRITE(static_cast<uint8_t>(burnType));
        READWRITE(context);
    }
};

struct CGovernanceMessage {
    std::unordered_map<std::string, std::shared_ptr<GovVariable>> govs;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        std::string name;
        while(!s.empty()) {
            s >> name;
            auto& gov = govs[name];
            auto var = GovVariable::Create(name);
            if (!var) break;
            s >> *var;
            gov = std::move(var);
        }
    }
};

struct CGovernanceHeightMessage {
    std::string govName;
    std::shared_ptr<GovVariable> govVar;
    uint32_t startHeight;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (!s.empty()) {
            s >> govName;
            if ((govVar = GovVariable::Create(govName))) {
                s >> *govVar;
                s >> startHeight;
            }
        }
    }
};

struct CGovernanceUnsetMessage {
    std::map<std::string, std::vector<std::string>> govs;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(govs);
    }
};

struct CCustomTxMessageNone {};

using CCustomTxMessage = std::variant<CCustomTxMessageNone,
                                      CCreateMasterNodeMessage,
                                      CResignMasterNodeMessage,
                                      CUpdateMasterNodeMessage,
                                      CCreateTokenMessage,
                                      CUpdateTokenPreAMKMessage,
                                      CUpdateTokenMessage,
                                      CMintTokensMessage,
                                      CBurnTokensMessage,
                                      CCreatePoolPairMessage,
                                      CUpdatePoolPairMessage,
                                      CPoolSwapMessage,
                                      CPoolSwapMessageV2,
                                      CLiquidityMessage,
                                      CRemoveLiquidityMessage,
                                      CUtxosToAccountMessage,
                                      CAccountToUtxosMessage,
                                      CAccountToAccountMessage,
                                      CAnyAccountsToAccountsMessage,
                                      CSmartContractMessage,
                                      CFutureSwapMessage,
                                      CGovernanceMessage,
                                      CGovernanceUnsetMessage,
                                      CGovernanceHeightMessage,
                                      CAppointOracleMessage,
                                      CRemoveOracleAppointMessage,
                                      CUpdateOracleAppointMessage,
                                      CSetOracleDataMessage,
                                      CICXCreateOrderMessage,
                                      CICXMakeOfferMessage,
                                      CICXSubmitDFCHTLCMessage,
                                      CICXSubmitEXTHTLCMessage,
                                      CICXClaimDFCHTLCMessage,
                                      CICXCloseOrderMessage,
                                      CICXCloseOfferMessage,
                                      CLoanSetCollateralTokenMessage,
                                      CLoanSetLoanTokenMessage,
                                      CLoanUpdateLoanTokenMessage,
                                      CLoanSchemeMessage,
                                      CDefaultLoanSchemeMessage,
                                      CDestroyLoanSchemeMessage,
                                      CVaultMessage,
                                      CCloseVaultMessage,
                                      CUpdateVaultMessage,
                                      CDepositToVaultMessage,
                                      CWithdrawFromVaultMessage,
                                      CPaybackWithCollateralMessage,
                                      CLoanTakeLoanMessage,
                                      CLoanPaybackLoanMessage,
                                      CLoanPaybackLoanV2Message,
                                      CAuctionBidMessage,
                                      CCreateProposalMessage,
                                      CProposalVoteMessage,
                                      CTransferBalanceMessage,
                                      CEvmTxMessage>;

CCustomTxMessage customTypeToMessage(CustomTxType txType);
bool IsMempooledCustomTxCreate(const CTxMemPool &pool, const uint256 &txid);
Res RpcInfo(const CTransaction &tx, uint32_t height, CustomTxType &type, UniValue &results);
Res CustomMetadataParse(uint32_t height,
                        const Consensus::Params &consensus,
                        const std::vector<unsigned char> &metadata,
                        CCustomTxMessage &txMessage);
Res ApplyCustomTx(CCustomCSView &mnview,
                  const CCoinsViewCache &coins,
                  const CTransaction &tx,
                  const Consensus::Params &consensus,
                  uint32_t height,
                  uint64_t time            = 0,
                  uint256 *canSpend        = nullptr,
                  uint32_t txn             = 0,
                  const uint64_t evmContext = 0);
Res CustomTxVisit(CCustomCSView &mnview,
                  const CCoinsViewCache &coins,
                  const CTransaction &tx,
                  uint32_t height,
                  const Consensus::Params &consensus,
                  const CCustomTxMessage &txMessage,
                  uint64_t time,
                  uint32_t txn = 0,
                  const uint64_t evmContext = 0);
ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView &mnview,
                                    const CTransaction &tx,
                                    int height,
                                    const uint256 &prevStakeModifier,
                                    const std::vector<unsigned char> &metadata,
                                    const Consensus::Params &consensusParams);
ResVal<uint256> ApplyAnchorRewardTxPlus(CCustomCSView &mnview,
                                        const CTransaction &tx,
                                        int height,
                                        const std::vector<unsigned char> &metadata,
                                        const Consensus::Params &consensusParams);
ResVal<CAmount> GetAggregatePrice(CCustomCSView &view,
                                  const std::string &token,
                                  const std::string &currency,
                                  uint64_t lastBlockTime);
bool IsVaultPriceValid(CCustomCSView &mnview, const CVaultId &vaultId, uint32_t height);
bool IsPaybackWithCollateral(CCustomCSView &mnview, const std::map<DCT_ID, CBalances> &loans);
Res PaybackWithCollateral(CCustomCSView &view,
                          const CVaultData &vault,
                          const CVaultId &vaultId,
                          uint32_t height,
                          uint64_t time);
Res SwapToDFIorDUSD(CCustomCSView &mnview,
                    DCT_ID tokenId,
                    CAmount amount,
                    const CScript &from,
                    const CScript &to,
                    uint32_t height,
                    bool forceLoanSwap = false);
Res storeGovVars(const CGovernanceHeightMessage &obj, CCustomCSView &view);
bool IsTestNetwork();
bool IsEVMEnabled(const int height, const CCustomCSView &view);

inline bool OraclePriceFeed(CCustomCSView &view, const CTokenCurrencyPair &priceFeed) {
    // Allow hard coded DUSD/USD
    if (priceFeed.first == "DUSD" && priceFeed.second == "USD") {
        return true;
    }
    bool found = false;
    view.ForEachOracle([&](const COracleId &, COracle oracle) {
        return !(found = oracle.SupportsPair(priceFeed.first, priceFeed.second));
    });
    return found;
}

/*
 * Checks if given tx is probably one of 'CustomTx', returns tx type and serialized metadata in 'data'
 */
inline CustomTxType GuessCustomTxType(const CTransaction &tx,
                                      std::vector<unsigned char> &metadata,
                                      bool metadataValidation = false) {
    if (tx.vout.empty()) {
        return CustomTxType::None;
    }

    // Check all other vouts for DfTx marker and reject if found
    if (metadataValidation) {
        for (size_t i{1}; i < tx.vout.size(); ++i) {
            std::vector<unsigned char> dummydata;
            bool dummyOpcodes{false};
            if (ParseScriptByMarker(tx.vout[i].scriptPubKey, DfTxMarker, dummydata, dummyOpcodes)) {
                return CustomTxType::Reject;
            }
        }
    }

    bool hasAdditionalOpcodes{false};
    if (!ParseScriptByMarker(tx.vout[0].scriptPubKey, DfTxMarker, metadata, hasAdditionalOpcodes)) {
        return CustomTxType::None;
    }

    // If metadata contains additional opcodes mark as Reject.
    if (metadataValidation && hasAdditionalOpcodes) {
        return CustomTxType::Reject;
    }

    auto txType = CustomTxCodeToType(metadata[0]);
    metadata.erase(metadata.begin());
    // Reject if marker has been found but no known type or None explicitly set.
    if (txType == CustomTxType::None) {
        return CustomTxType::Reject;
    }
    return txType;
}

inline bool IsMintTokenTx(const CTransaction &tx) {
    std::vector<unsigned char> metadata;
    return GuessCustomTxType(tx, metadata) == CustomTxType::MintToken;
}

inline std::optional<std::vector<unsigned char>> GetAccountToUtxosMetadata(const CTransaction &tx) {
    std::vector<unsigned char> metadata;
    if (GuessCustomTxType(tx, metadata) == CustomTxType::AccountToUtxos) {
        return metadata;
    }
    return {};
}

inline std::optional<CAccountToUtxosMessage> GetAccountToUtxosMsg(const CTransaction &tx) {
    const auto metadata = GetAccountToUtxosMetadata(tx);
    if (metadata) {
        CAccountToUtxosMessage msg;
        try {
            CDataStream ss(*metadata, SER_NETWORK, PROTOCOL_VERSION);
            ss >> msg;
        } catch (...) {
            return {};
        }
        return msg;
    }
    return {};
}

inline TAmounts GetNonMintedValuesOut(const CTransaction &tx) {
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos    = GetAccountToUtxosMsg(tx);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValuesOut(mintingOutputsStart);
}

inline CAmount GetNonMintedValueOut(const CTransaction &tx, DCT_ID tokenID) {
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos    = GetAccountToUtxosMsg(tx);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValueOut(mintingOutputsStart, tokenID);
}


class CPoolSwap {
    const CPoolSwapMessage &obj;
    uint32_t height;
    CAmount result{0};
    DCT_ID currentID;

public:
    std::vector<std::pair<std::string, std::string>> errors;

    CPoolSwap(const CPoolSwapMessage &obj, uint32_t height)
        : obj(obj),
          height(height) {}

    std::vector<DCT_ID> CalculateSwaps(CCustomCSView &view, bool testOnly = false);
    Res ExecuteSwap(CCustomCSView &view, std::vector<DCT_ID> poolIDs, bool testOnly = false);
    std::vector<std::vector<DCT_ID>> CalculatePoolPaths(CCustomCSView &view);
    CTokenAmount GetResult() { return CTokenAmount{obj.idTokenTo, result}; };
};

#endif  // DEFI_MASTERNODES_MN_CHECKS_H
