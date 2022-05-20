// Copyright (c) 2021 The DeFi Blockchain Developers
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
        case CustomTxType::DFIP2203:
        case CustomTxType::FutureSwapExecution:
        case CustomTxType::FutureSwapRefund:
        case CustomTxType::SetGovVariable:
        case CustomTxType::UnsetGovVariable:
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
        case CustomTxType::PaybackLoanV2:
        case CustomTxType::AuctionBid:
        case CustomTxType::CreateCfp:
        case CustomTxType::CreateVoc:
        case CustomTxType::Vote:
        case CustomTxType::Reject:
        case CustomTxType::None:
            return type;
    }
    return CustomTxType::None;
}

#define CustomTxTypeString(Type)          case CustomTxType::Type: return #Type
#define CustomTxType2Strings(Type, Name)  case CustomTxType::Type: return #Name

std::string ToString(CustomTxType type) {
    switch (type) {
        CustomTxTypeString(CreateMasternode);
        CustomTxTypeString(ResignMasternode);
        CustomTxTypeString(UpdateMasternode);
        CustomTxTypeString(CreateToken);
        CustomTxTypeString(UpdateToken);
        CustomTxTypeString(UpdateTokenAny);
        CustomTxTypeString(MintToken);
        CustomTxTypeString(BurnToken);
        CustomTxTypeString(CreatePoolPair);
        CustomTxTypeString(UpdatePoolPair);
        CustomTxTypeString(PoolSwap);
        CustomTxTypeString(AddPoolLiquidity);
        CustomTxTypeString(RemovePoolLiquidity);
        CustomTxTypeString(UtxosToAccount);
        CustomTxTypeString(AccountToUtxos);
        CustomTxTypeString(AccountToAccount);
        CustomTxTypeString(AnyAccountsToAccounts);
        CustomTxTypeString(SmartContract);
        CustomTxTypeString(DFIP2203);
        CustomTxTypeString(FutureSwapExecution);
        CustomTxTypeString(FutureSwapRefund);
        CustomTxTypeString(SetGovVariable);
        CustomTxTypeString(UnsetGovVariable);
        CustomTxTypeString(SetGovVariableHeight);
        CustomTxTypeString(AppointOracle);
        CustomTxTypeString(RemoveOracleAppoint);
        CustomTxTypeString(UpdateOracleAppoint);
        CustomTxTypeString(SetOracleData);
        CustomTxTypeString(ICXCreateOrder);
        CustomTxTypeString(ICXMakeOffer);
        CustomTxTypeString(ICXSubmitDFCHTLC);
        CustomTxTypeString(ICXSubmitEXTHTLC);
        CustomTxTypeString(ICXClaimDFCHTLC);
        CustomTxTypeString(ICXCloseOrder);
        CustomTxTypeString(ICXCloseOffer);
        CustomTxTypeString(SetLoanCollateralToken);
        CustomTxTypeString(SetLoanToken);
        CustomTxTypeString(UpdateLoanToken);
        CustomTxTypeString(LoanScheme);
        CustomTxTypeString(DefaultLoanScheme);
        CustomTxTypeString(DestroyLoanScheme);
        CustomTxTypeString(Vault);
        CustomTxTypeString(CloseVault);
        CustomTxTypeString(UpdateVault);
        CustomTxTypeString(DepositToVault);
        CustomTxTypeString(WithdrawFromVault);
        CustomTxTypeString(TakeLoan);
        CustomTxTypeString(PaybackLoan);
        CustomTxTypeString(AuctionBid);
        CustomTxTypeString(CreateCfp);
        CustomTxTypeString(CreateVoc);
        CustomTxTypeString(Vote);
        CustomTxTypeString(Reject);
        CustomTxTypeString(None);
        CustomTxType2Strings(PoolSwapV2, PoolSwap);
        CustomTxType2Strings(AutoAuthPrep, AutoAuth);
        CustomTxType2Strings(PaybackLoanV2, PaybackLoan);
    }
    return "None";
}

/*
 * Checks if given tx is probably one of 'CustomTx', returns tx type and serialized metadata in 'data'
*/
CustomTxType GuessCustomTxType(CTransaction const & tx, std::vector<unsigned char> & metadata, bool metadataValidation,
                                      uint32_t height, CExpirationAndVersion* customTxParams){
    if (tx.vout.empty()) {
        return CustomTxType::None;
    }

    // Check all other vouts for DfTx marker and reject if found
    if (metadataValidation) {
        for (size_t i{1}; i < tx.vout.size(); ++i) {
            std::vector<unsigned char> dummydata;
            uint8_t dummyOpcodes{HasForks::None};
            if (ParseScriptByMarker(tx.vout[i].scriptPubKey, DfTxMarker, dummydata, dummyOpcodes)) {
                return CustomTxType::Reject;
            }
        }
    }

    uint8_t hasAdditionalOpcodes{HasForks::None};
    if (!ParseScriptByMarker(tx.vout[0].scriptPubKey, DfTxMarker, metadata, hasAdditionalOpcodes, customTxParams)) {
        return CustomTxType::None;
    }

    // If metadata contains additional opcodes mark as Reject.
    if (metadataValidation) {
        if (height < static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight) && hasAdditionalOpcodes & HasForks::FortCanning) {
            return CustomTxType::Reject;
        } else if (height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight) && hasAdditionalOpcodes & HasForks::GreatWorld) {
            return CustomTxType::Reject;
        }
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
    return (height < Params().GetConsensus().DakotaHeight
        && (txType == CustomTxType::MintToken || txType == CustomTxType::AccountToUtxos));
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
