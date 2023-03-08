// Copyright (c) 2023 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ERRORS_H
#define DEFI_MASTERNODES_ERRORS_H

#include <amount.h>
#include <masternodes/res.h>

class DeFiErrors {
public:
    static Res MNInvalid(const std::string &nodeRefString) { 
        return Res::Err("node %s does not exists", nodeRefString);
    }

    static Res MNInvalidAltMsg(const std::string &nodeRefString) { 
        return Res::Err("masternode %s does not exist", nodeRefString);
    }

    static Res MNStateNotEnabled(const std::string &nodeRefString) { 
        return Res::Err("Masternode %s is not in 'ENABLED' state", nodeRefString);
    }

    static Res ICXBTCBelowMinSwap(const CAmount amount, const CAmount minSwap) {
        // TODO: Change error in later version to include amount. Retaining old msg for compatibility
        return Res::Err("Below minimum swapable amount, must be at least %s BTC", GetDecimaleString(minSwap));
    }

    static Res MNInvalidAttribute() {
        return Res::Err("Attributes unavailable");
    }

    static Res TokenInvalidForName(const std::string &tokenName) {
        return Res::Err("Cannot find token %s", tokenName);
    }

    static Res LoanPaybackWithCollateralDisable() {
        return Res::Err("Payback of DUSD loan with collateral is not currently active");
    }

    static Res VaultNoCollateral(const std::string &vaultId = "") {
        return vaultId.empty() ? Res::Err("Vault has no collaterals") : Res::Err("Vault with id %s has no collaterals", vaultId);
    }

    static Res VaultNoDUSDCollateral() {
        return Res::Err("Vault does not have any DUSD collaterals");
    }

    static Res LoanInvalidVault(const CVaultId &vault) {
        return Res::Err("There are no loans on this vault (%s)!", vault.GetHex());
    }

    static Res LoanInvalidTokenForSymbol(const std::string &symbol) {
        return Res::Err("There is no loan on token (%s) in this vault!", symbol);
    }

    static Res VaultNoLoans(const std::string &token = "") {
        return token.empty() ? Res::Err("Vault has no loans") : Res::Err("Vault does not have any %s loans", token);
    }

    static Res TokenInterestRateInvalid(const std::string token) {
        return Res::Err("Cannot get interest rate for this token (%s)!", token);
    }

    static Res VaultNeedCollateral() {
        return Res::Err("Vault cannot have loans without collaterals");
    }

    static Res VaultInvalidPrice() {
        return Res::Err("Cannot payback vault with non-DUSD assets while any of the asset's price is invalid");
    }

    static Res VaultInsufficientCollateralization(const uint32_t collateralizationRatio, const uint32_t schemeRatio) {
        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                        collateralizationRatio,
                        schemeRatio);
    }

    static Res LoanTokenNotFoundForName(const std::string &tokenName) {
        return Res::Err("Loan token %s does not exist!", tokenName);
    }

    static Res VaultInvalid(const CVaultId vaultId) {
        return Res::Err("Cannot find existing vault with id %s", vaultId.GetHex());
    }

    static Res VaultUnderLiquidation() {
        return Res::Err("Vault is under liquidation");
    }

    static Res LoanNoPaybackOnLiquidation() {
        return Res::Err("Cannot payback loan on vault under liquidation");
    }

    static Res TXMissingInput() {
        return Res::Err("tx must have at least one input from token owner");
    }

    static Res LoanAssetPriceInvalid() {
        return Res::Err("Cannot payback loan while any of the asset's price is invalid");
    }

    static Res LoanTokenIdInvalid(const DCT_ID &tokenId) {
        return Res::Err("Loan token with id (%s) does not exist!", tokenId.ToString());
    }

    static Res LoanPaymentAmountInvalid(const CAmount amount, const uint32_t value) {
        return Res::Err("Valid payback amount required (input: %d@%d)", amount, value);
    }

    static Res TokenIdInvalid(const DCT_ID &tokenId) {
        return Res::Err("Token with id (%s) does not exists", tokenId.ToString());
    }

    static Res LoanPaybackDisabled(const std::string &token) {
        return token.empty() ? Res::Err("Payback is not currently active") : Res::Err("Payback of loan via %s token is not currently active", token);
    }

    static Res OracleNoLivePrice(const std::string &tokenSymbol, const std::string &currency) {
        return Res::Err("No live fixed prices for %s/%s", tokenSymbol, currency);
    }

    static Res OracleNegativePrice(const std::string &tokenSymbol, const std::string &currency) {
        return Res::Err("Negative price (%s/%s)", tokenSymbol, currency);
    }

    static Res AmountOverflowAsValuePrice(const CAmount amount, const CAmount price) { 
        return Res::Err("Value/price too high (%s/%s)", GetDecimaleString(amount), GetDecimaleString(price)); 
    }
};

#endif  // DEFI_MASTERNODES_ERRORS_H

