// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/tx_check.h>
#include <masternodes/accounts.h>
#include <masternodes/customtx.h>

const std::vector<unsigned char> DfTxMarker = {'D', 'f', 'T', 'x'};
const std::vector<unsigned char> DfAnchorFinalizeTxMarker = {'D', 'f', 'A', 'f'};
const std::vector<unsigned char> DfAnchorFinalizeTxMarkerPlus = {'D', 'f', 'A', 'P'};
const std::vector<unsigned char> DfTokenSplitMarker = {'D', 'f', 'T', 'S'};

CustomTxType CustomTxCodeToType(uint8_t ch) {
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
        case CustomTxType::None:
            return type;
    }
    return CustomTxType::None;
}

#define CustomTxTypeString(Type)          case CustomTxType::Type: return #Type
#define CustomTxType2Strings(Type, Name)  case CustomTxType::Type: return #Name

std::string ToString(CustomTxType type) {
    switch (type) {
        CustomTxTypeString(AccountToAccount);
        CustomTxTypeString(AccountToUtxos);
        CustomTxTypeString(AddPoolLiquidity);
        CustomTxTypeString(AnyAccountsToAccounts);
        CustomTxTypeString(AppointOracle);
        CustomTxTypeString(AuctionBid);
        CustomTxType2Strings(AutoAuthPrep, AutoAuth);
        CustomTxTypeString(BurnToken);
        CustomTxTypeString(CloseVault);
        CustomTxTypeString(CreateCfp);
        CustomTxTypeString(CreateMasternode);
        CustomTxTypeString(CreatePoolPair);
        CustomTxTypeString(CreateToken);
        CustomTxTypeString(CreateVoc);
        CustomTxTypeString(DefaultLoanScheme);
        CustomTxTypeString(DestroyLoanScheme);
        CustomTxTypeString(DepositToVault);
        CustomTxTypeString(FutureSwap);
        CustomTxTypeString(FutureSwapExecution);
        CustomTxTypeString(FutureSwapRefund);
        CustomTxTypeString(ICXClaimDFCHTLC);
        CustomTxTypeString(ICXCloseOffer);
        CustomTxTypeString(ICXCloseOrder);
        CustomTxTypeString(ICXCreateOrder);
        CustomTxTypeString(ICXMakeOffer);
        CustomTxTypeString(ICXSubmitDFCHTLC);
        CustomTxTypeString(ICXSubmitEXTHTLC);
        CustomTxTypeString(LoanScheme);
        CustomTxTypeString(MintToken);
        CustomTxTypeString(None);
        CustomTxTypeString(PaybackLoan);
        CustomTxType2Strings(PaybackLoanV2, PaybackLoan);
        CustomTxTypeString(PaybackWithCollateral);
        CustomTxTypeString(PoolSwap);
        CustomTxType2Strings(PoolSwapV2, PoolSwap);
        CustomTxTypeString(ProposalFeeRedistribution);
        CustomTxTypeString(Reject);
        CustomTxTypeString(RemoveOracleAppoint);
        CustomTxTypeString(RemovePoolLiquidity);
        CustomTxTypeString(ResignMasternode);
        CustomTxTypeString(SetGovVariable);
        CustomTxTypeString(SetGovVariableHeight);
        CustomTxTypeString(SetLoanCollateralToken);
        CustomTxTypeString(SetLoanToken);
        CustomTxTypeString(SetOracleData);
        CustomTxTypeString(SmartContract);
        CustomTxTypeString(TakeLoan);
        CustomTxTypeString(TokenSplit);
        CustomTxTypeString(UnsetGovVariable);
        CustomTxTypeString(UpdateLoanToken);
        CustomTxTypeString(UpdateOracleAppoint);
        CustomTxTypeString(UpdateMasternode);
        CustomTxTypeString(UpdatePoolPair);
        CustomTxTypeString(UpdateToken);
        CustomTxTypeString(UpdateTokenAny);
        CustomTxTypeString(UpdateVault);
        CustomTxTypeString(UtxosToAccount);
        CustomTxTypeString(Vault);
        CustomTxTypeString(Vote);
        CustomTxTypeString(WithdrawFromVault);
    }
    return "None";
}

CustomTxType FromString(const std::string &str) {
    static const auto customTxTypeMap = []() {
        std::map<std::string, CustomTxType> generatedMap;
        for (auto i = 0u; i < 256; i++) {
            auto txType = static_cast<CustomTxType>(i);
            generatedMap.emplace(ToString(txType), txType);
        }
        return generatedMap;
    }();
    auto type = customTxTypeMap.find(str);
    return type == customTxTypeMap.end() ? CustomTxType::None : type->second;
}


CustomTxType GuessCustomTxType(const CTransaction &tx, std::vector<unsigned char> &metadata, bool metadataValidation) {
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

bool NotAllowedToFail(CustomTxType txType, int height) {
    return (height < Params().GetConsensus().DakotaHeight &&
            (txType == CustomTxType::MintToken || txType == CustomTxType::AccountToUtxos));
}

TAmounts GetNonMintedValuesOut(const CTransaction& tx) {
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos = GetIf<CAccountToUtxosMessage>(tx, CustomTxType::AccountToUtxos);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValuesOut(mintingOutputsStart);
}

CAmount GetNonMintedValueOut(const CTransaction& tx, DCT_ID tokenID) {
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos = GetIf<CAccountToUtxosMessage>(tx, CustomTxType::AccountToUtxos);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValueOut(mintingOutputsStart, tokenID);
}
