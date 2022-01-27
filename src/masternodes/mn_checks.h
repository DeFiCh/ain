// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MN_CHECKS_H
#define DEFI_MASTERNODES_MN_CHECKS_H

#include <consensus/params.h>
#include <masternodes/masternodes.h>
#include <consensus/tx_check.h>
#include <vector>
#include <cstring>

#include <variant>

class CBlock;
class CTransaction;
class CTxMemPool;
class CCoinsViewCache;

class CCustomCSView;
class CAccountsHistoryView;
class CCustomTxVisitor;
class CVaultHistoryView;
class CHistoryWriters;
class CHistoryErasers;

static const std::vector<unsigned char> DfTxMarker = {'D', 'f', 'T', 'x'};  // 44665478

enum CustomTxErrCodes : uint32_t {
    NotSpecified = 0,
//    NotCustomTx  = 1,
    NotEnoughBalance = 1024,
    Fatal = uint32_t(1) << 31 // not allowed to fail
};

enum class CustomTxType : uint8_t
{
    None = 0,
    Reject = 1, // Invalid TX type. Returned by GuessCustomTxType on invalid custom TX.

    // masternodes:
    CreateMasternode      = 'C',
    ResignMasternode      = 'R',
    UpdateMasternode      = 'm',
    SetForcedRewardAddress = 'F',
    RemForcedRewardAddress = 'f',
    // custom tokens:
    CreateToken           = 'T',
    MintToken             = 'M',
    UpdateToken           = 'N', // previous type, only DAT flag triggers
    UpdateTokenAny        = 'n', // new type of token's update with any flags/fields possible
    // dex orders - just not to overlap in future
//    CreateOrder         = 'O',
//    DestroyOrder        = 'E',
//    MatchOrders         = 'A',
    //poolpair
    CreatePoolPair        = 'p',
    UpdatePoolPair        = 'u',
    PoolSwap              = 's',
    PoolSwapV2            = 'i',
    AddPoolLiquidity      = 'l',
    RemovePoolLiquidity   = 'r',
    // accounts
    UtxosToAccount        = 'U',
    AccountToUtxos        = 'b',
    AccountToAccount      = 'B',
    AnyAccountsToAccounts = 'a',
    SmartContract         = 'K',
    //set governance variable
    SetGovVariable        = 'G',
    SetGovVariableHeight  = 'j',
    // Auto auth TX
    AutoAuthPrep          = 'A',
    // oracles
    AppointOracle         = 'o',
    RemoveOracleAppoint   = 'h',
    UpdateOracleAppoint   = 't',
    SetOracleData         = 'y',
    // ICX
    ICXCreateOrder      = '1',
    ICXMakeOffer        = '2',
    ICXSubmitDFCHTLC    = '3',
    ICXSubmitEXTHTLC    = '4',
    ICXClaimDFCHTLC     = '5',
    ICXCloseOrder       = '6',
    ICXCloseOffer       = '7',
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
    TakeLoan               = 'X',
    PaybackLoan            = 'H',
    AuctionBid             = 'I'
};

inline CustomTxType CustomTxCodeToType(uint8_t ch) {
    auto type = static_cast<CustomTxType>(ch);
    switch(type) {
        case CustomTxType::CreateMasternode:
        case CustomTxType::ResignMasternode:
        case CustomTxType::SetForcedRewardAddress:
        case CustomTxType::RemForcedRewardAddress:
        case CustomTxType::UpdateMasternode:
        case CustomTxType::CreateToken:
        case CustomTxType::MintToken:
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
        case CustomTxType::TakeLoan:
        case CustomTxType::PaybackLoan:
        case CustomTxType::AuctionBid:
        case CustomTxType::Reject:
        case CustomTxType::None:
            return type;
    }
    return CustomTxType::None;
}

std::string ToString(CustomTxType type);

// it's disabled after Dakota height
inline bool NotAllowedToFail(CustomTxType txType, int height) {
    return (height < Params().GetConsensus().DakotaHeight
        && (txType == CustomTxType::MintToken || txType == CustomTxType::AccountToUtxos));
}

template<typename Stream>
inline void Serialize(Stream& s, CustomTxType txType) {
    Serialize(s, static_cast<unsigned char>(txType));
}

template<typename Stream>
inline void Unserialize(Stream& s, CustomTxType & txType) {
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
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nodeId);
        READWRITE(rewardAddressType);
        READWRITE(rewardAddress);
    }
};

struct CRemForcedRewardAddressMessage {
    uint256 nodeId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
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

struct CCreateTokenMessage : public CToken {
    using CToken::CToken;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CToken, *this);
    }
};

struct CUpdateTokenPreAMKMessage {
    uint256 tokenTx;
    bool isDAT;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(tokenTx);
        READWRITE(isDAT);
    }
};

struct CUpdateTokenMessage {
    uint256 tokenTx;
    CToken token;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(tokenTx);
        READWRITE(token);
    }
};

struct CMintTokensMessage : public CBalances {
    using CBalances::CBalances;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CBalances, *this);
    }
};

struct CCreatePoolPairMessage {
    CPoolPairMessage poolPair;
    std::string pairSymbol;
    CBalances rewards;
};

struct CUpdatePoolPairMessage {
    DCT_ID poolId;
    bool status;
    CAmount commission;
    CScript ownerAddress;
    CBalances rewards;
};

struct CGovernanceMessage {
    std::set<std::shared_ptr<GovVariable>> govs;
};

struct CGovernanceHeightMessage {
    std::shared_ptr<GovVariable> govVar;
    uint32_t startHeight;
};

struct CCustomTxMessageNone {};

using CCustomTxMessage = std::variant<
    CCustomTxMessageNone,
    CCreateMasterNodeMessage,
    CResignMasterNodeMessage,
    CSetForcedRewardAddressMessage,
    CRemForcedRewardAddressMessage,
    CUpdateMasterNodeMessage,
    CCreateTokenMessage,
    CUpdateTokenPreAMKMessage,
    CUpdateTokenMessage,
    CMintTokensMessage,
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
    CGovernanceMessage,
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
    CLoanTakeLoanMessage,
    CLoanPaybackLoanMessage,
    CAuctionBidMessage
>;

CCustomTxMessage customTypeToMessage(CustomTxType txType);
bool IsMempooledCustomTxCreate(const CTxMemPool& pool, const uint256& txid);
Res RpcInfo(const CTransaction& tx, uint32_t height, CustomTxType& type, UniValue& results);
Res CustomMetadataParse(uint32_t height, const Consensus::Params& consensus, const std::vector<unsigned char>& metadata, CCustomTxMessage& txMessage);
Res ApplyCustomTx(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height, uint64_t time = 0, uint32_t txn = 0, CHistoryWriters* writers = nullptr);
Res RevertCustomTx(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height,  uint32_t txn, CHistoryErasers& erasers);
Res CustomTxVisit(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, uint32_t height, const Consensus::Params& consensus, const CCustomTxMessage& txMessage, uint64_t time = 0);
ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView& mnview, const CTransaction& tx, int height, const uint256& prevStakeModifier, const std::vector<unsigned char>& metadata, const Consensus::Params& consensusParams);
ResVal<uint256> ApplyAnchorRewardTxPlus(CCustomCSView& mnview, const CTransaction& tx, int height, const std::vector<unsigned char>& metadata, const Consensus::Params& consensusParams);
ResVal<CAmount> GetAggregatePrice(CCustomCSView& view, const std::string& token, const std::string& currency, uint64_t lastBlockTime);
bool IsVaultPriceValid(CCustomCSView& mnview, const CVaultId& vaultId, uint32_t height);
Res SwapToDFIOverUSD(CCustomCSView & mnview, DCT_ID tokenId, CAmount amount, CScript const & from, CScript const & to, uint32_t height);

/*
 * Checks if given tx is probably one of 'CustomTx', returns tx type and serialized metadata in 'data'
*/
inline CustomTxType GuessCustomTxType(CTransaction const & tx, std::vector<unsigned char> & metadata, bool metadataValidation = false){
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

inline bool IsMintTokenTx(const CTransaction& tx)
{
    std::vector<unsigned char> metadata;
    return GuessCustomTxType(tx, metadata) == CustomTxType::MintToken;
}

inline std::optional<std::vector<unsigned char>> GetAccountToUtxosMetadata(const CTransaction & tx)
{
    std::vector<unsigned char> metadata;
    if (GuessCustomTxType(tx, metadata) == CustomTxType::AccountToUtxos) {
        return metadata;
    }
    return {};
}

inline std::optional<CAccountToUtxosMessage> GetAccountToUtxosMsg(const CTransaction & tx)
{
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

inline TAmounts GetNonMintedValuesOut(const CTransaction & tx)
{
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos = GetAccountToUtxosMsg(tx);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValuesOut(mintingOutputsStart);
}

inline CAmount GetNonMintedValueOut(const CTransaction & tx, DCT_ID tokenID)
{
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos = GetAccountToUtxosMsg(tx);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValueOut(mintingOutputsStart, tokenID);
}

class CPoolSwap {
    const CPoolSwapMessage& obj;
    uint32_t height;
    CAmount result{0};
    DCT_ID currentID;

public:
    std::vector<std::pair<std::string, std::string>> errors;

    CPoolSwap(const CPoolSwapMessage& obj, uint32_t height)
    : obj(obj), height(height) {}

    std::vector<DCT_ID> CalculateSwaps(CCustomCSView& view, bool testOnly = false);
    Res ExecuteSwap(CCustomCSView& view, std::vector<DCT_ID> poolIDs, bool testOnly = false);
    std::vector<std::vector<DCT_ID>> CalculatePoolPaths(CCustomCSView& view);
    CTokenAmount GetResult() { return CTokenAmount{obj.idTokenTo, result}; };
};

#endif // DEFI_MASTERNODES_MN_CHECKS_H
