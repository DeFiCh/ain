// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/tx_check.h>
#include <masternodes/accounts.h>
#include <masternodes/customtx.h>

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
        case CustomTxType::TransferDomain:
        case CustomTxType::EvmTx:
        case CustomTxType::None:
            return type;
    }
    return CustomTxType::None;
}

std::string ToString(CustomTxType type) {
    switch (type) {
        case CustomTxType::CreateMasternode:
            return "CreateMasternode";
        case CustomTxType::ResignMasternode:
            return "ResignMasternode";
        case CustomTxType::UpdateMasternode:
            return "UpdateMasternode";
        case CustomTxType::CreateToken:
            return "CreateToken";
        case CustomTxType::UpdateToken:
            return "UpdateToken";
        case CustomTxType::UpdateTokenAny:
            return "UpdateTokenAny";
        case CustomTxType::MintToken:
            return "MintToken";
        case CustomTxType::BurnToken:
            return "BurnToken";
        case CustomTxType::CreatePoolPair:
            return "CreatePoolPair";
        case CustomTxType::UpdatePoolPair:
            return "UpdatePoolPair";
        case CustomTxType::PoolSwap:
            return "PoolSwap";
        case CustomTxType::PoolSwapV2:
            return "PoolSwap";
        case CustomTxType::AddPoolLiquidity:
            return "AddPoolLiquidity";
        case CustomTxType::RemovePoolLiquidity:
            return "RemovePoolLiquidity";
        case CustomTxType::UtxosToAccount:
            return "UtxosToAccount";
        case CustomTxType::AccountToUtxos:
            return "AccountToUtxos";
        case CustomTxType::AccountToAccount:
            return "AccountToAccount";
        case CustomTxType::AnyAccountsToAccounts:
            return "AnyAccountsToAccounts";
        case CustomTxType::SmartContract:
            return "SmartContract";
        case CustomTxType::FutureSwap:
            return "DFIP2203";
        case CustomTxType::SetGovVariable:
            return "SetGovVariable";
        case CustomTxType::SetGovVariableHeight:
            return "SetGovVariableHeight";
        case CustomTxType::AppointOracle:
            return "AppointOracle";
        case CustomTxType::RemoveOracleAppoint:
            return "RemoveOracleAppoint";
        case CustomTxType::UpdateOracleAppoint:
            return "UpdateOracleAppoint";
        case CustomTxType::SetOracleData:
            return "SetOracleData";
        case CustomTxType::AutoAuthPrep:
            return "AutoAuth";
        case CustomTxType::ICXCreateOrder:
            return "ICXCreateOrder";
        case CustomTxType::ICXMakeOffer:
            return "ICXMakeOffer";
        case CustomTxType::ICXSubmitDFCHTLC:
            return "ICXSubmitDFCHTLC";
        case CustomTxType::ICXSubmitEXTHTLC:
            return "ICXSubmitEXTHTLC";
        case CustomTxType::ICXClaimDFCHTLC:
            return "ICXClaimDFCHTLC";
        case CustomTxType::ICXCloseOrder:
            return "ICXCloseOrder";
        case CustomTxType::ICXCloseOffer:
            return "ICXCloseOffer";
        case CustomTxType::SetLoanCollateralToken:
            return "SetLoanCollateralToken";
        case CustomTxType::SetLoanToken:
            return "SetLoanToken";
        case CustomTxType::UpdateLoanToken:
            return "UpdateLoanToken";
        case CustomTxType::LoanScheme:
            return "LoanScheme";
        case CustomTxType::DefaultLoanScheme:
            return "DefaultLoanScheme";
        case CustomTxType::DestroyLoanScheme:
            return "DestroyLoanScheme";
        case CustomTxType::Vault:
            return "Vault";
        case CustomTxType::CloseVault:
            return "CloseVault";
        case CustomTxType::UpdateVault:
            return "UpdateVault";
        case CustomTxType::DepositToVault:
            return "DepositToVault";
        case CustomTxType::WithdrawFromVault:
            return "WithdrawFromVault";
        case CustomTxType::PaybackWithCollateral:
            return "PaybackWithCollateral";
        case CustomTxType::TakeLoan:
            return "TakeLoan";
        case CustomTxType::PaybackLoan:
            return "PaybackLoan";
        case CustomTxType::PaybackLoanV2:
            return "PaybackLoan";
        case CustomTxType::AuctionBid:
            return "AuctionBid";
        case CustomTxType::FutureSwapExecution:
            return "FutureSwapExecution";
        case CustomTxType::FutureSwapRefund:
            return "FutureSwapRefund";
        case CustomTxType::TokenSplit:
            return "TokenSplit";
        case CustomTxType::Reject:
            return "Reject";
        case CustomTxType::CreateCfp:
            return "CreateCfp";
        case CustomTxType::ProposalFeeRedistribution:
            return "ProposalFeeRedistribution";
        case CustomTxType::CreateVoc:
            return "CreateVoc";
        case CustomTxType::Vote:
            return "Vote";
        case CustomTxType::UnsetGovVariable:
            return "UnsetGovVariable";
        case CustomTxType::TransferDomain:
            return "TransferDomain";
        case CustomTxType::EvmTx:
            return "EvmTx";
        case CustomTxType::None:
            return "None";
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

CustomTxType GuessCustomTxType(const CTransaction &tx, std::vector<unsigned char> &metadata, bool metadataValidation)
{
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

TAmounts GetNonMintedValuesOut(const CTransaction &tx) {
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos = GetIf<CAccountToUtxosMessage>(tx, CustomTxType::AccountToUtxos);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValuesOut(mintingOutputsStart);
}

CAmount GetNonMintedValueOut(const CTransaction &tx, DCT_ID tokenID) {
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos = GetIf<CAccountToUtxosMessage>(tx, CustomTxType::AccountToUtxos);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValueOut(mintingOutputsStart, tokenID);
}

bool NotAllowedToFail(CustomTxType txType, int height) {
    return (height < Params().GetConsensus().DakotaHeight &&
            (txType == CustomTxType::MintToken || txType == CustomTxType::AccountToUtxos));
}

