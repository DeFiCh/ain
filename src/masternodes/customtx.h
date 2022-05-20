
// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CUSTOMTX_H
#define DEFI_MASTERNODES_CUSTOMTX_H

#include <amount.h>
#include <vector>

class CTransaction;

enum CustomTxErrCodes : uint32_t {
    NotSpecified = 0,
    NotEnoughBalance = 1024,
    Fatal = uint32_t(1) << 31 // not allowed to fail
};

enum struct CustomTxType : uint8_t
{
    None                    = 0,
    Reject                  = 1, // Invalid TX type. Returned by GuessCustomTxType on invalid custom TX.

    // masternodes
    CreateMasternode        = 'C',
    ResignMasternode        = 'R',
    UpdateMasternode        = 'm',

    // tokens
    CreateToken             = 'T',
    MintToken               = 'M',
    UpdateToken             = 'N', // previous type, only DAT flag triggers
    UpdateTokenAny          = 'n', // new type of token's update with any flags/fields possible
    BurnToken               = 'W',

    // poolpairs
    CreatePoolPair          = 'p',
    UpdatePoolPair          = 'u',
    PoolSwap                = 's',
    PoolSwapV2              = 'i',
    AddPoolLiquidity        = 'l',
    RemovePoolLiquidity     = 'r',

    // accounts
    UtxosToAccount          = 'U',
    AccountToUtxos          = 'b',
    AccountToAccount        = 'B',
    AnyAccountsToAccounts   = 'a',
    SmartContract           = 'K',
    DFIP2203                = 'Q',
    FutureSwapExecution     = 'q',
    FutureSwapRefund        = 'w',

    // governance
    SetGovVariable          = 'G',
    UnsetGovVariable        = 'Z',
    SetGovVariableHeight    = 'j',

    // Auto auth
    AutoAuthPrep            = 'A',

    // oracles
    AppointOracle           = 'o',
    RemoveOracleAppoint     = 'h',
    UpdateOracleAppoint     = 't',
    SetOracleData           = 'y',

    // ICX
    ICXCreateOrder          = '1',
    ICXMakeOffer            = '2',
    ICXSubmitDFCHTLC        = '3',
    ICXSubmitEXTHTLC        = '4',
    ICXClaimDFCHTLC         = '5',
    ICXCloseOrder           = '6',
    ICXCloseOffer           = '7',

    // Loans
    SetLoanCollateralToken  = 'c',
    SetLoanToken            = 'g',
    UpdateLoanToken         = 'x',
    LoanScheme              = 'L',
    DefaultLoanScheme       = 'd',
    DestroyLoanScheme       = 'D',
    Vault                   = 'V',
    CloseVault              = 'e',
    UpdateVault             = 'v',
    DepositToVault          = 'S',
    WithdrawFromVault       = 'J',
    TakeLoan                = 'X',
    PaybackLoan             = 'H',
    PaybackLoanV2           = 'k',
    AuctionBid              = 'I',

    // On-Chain-Gov
    CreateCfp               = 'P',
    Vote                    = 'O',
    CreateVoc               = 'E',
};

enum class MetadataVersion : uint8_t {
    None = 0,
    One = 1,
    Two = 2,
};

CustomTxType GuessCustomTxType(CTransaction const & tx, std::vector<unsigned char> & metadata, bool metadataValidation = false,
                                      uint32_t height = 0, CExpirationAndVersion* customTxParams = nullptr);
CAmount GetNonMintedValueOut(const CTransaction& tx, DCT_ID tokenID);
TAmounts GetNonMintedValuesOut(const CTransaction& tx);
bool NotAllowedToFail(CustomTxType txType, int height);
CustomTxType CustomTxCodeToType(uint8_t ch);
std::string ToString(CustomTxType type);

template<typename T>
std::optional<T> GetIf(const CTransaction& tx, CustomTxType txType)
{
    std::vector<unsigned char> metadata;
    if (GuessCustomTxType(tx, metadata) != txType) {
        return {};
    }

    T msg{};
    try {
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> msg;
    } catch (...) {
        return {};
    }
    return msg;
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

#endif // DEFI_MASTERNODES_CUSTOMTX_H
