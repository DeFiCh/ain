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

    static Res TokenInvalid(std::string token) {
        return Res::Err("Cannot find token %s", token);
    }

    static Res LoanPaybackWithCollateralDisable() {
        return Res::Err("Payback of DUSD loan with collateral is not currently active");
    }

    static Res VaultNoCollateral(const std::string vaultId = "") {
        return vaultId.empty() ? Res::Err("Vault has no collaterals") : Res::Err("Vault with id %s has no collaterals", vaultId);
    }

    static Res VaultNoDUSDCollateral() {
        return Res::Err("Vault does not have any DUSD collaterals");
    }

    static Res LoanInvalid(std::string vault = "") {
        return vault.empty() ? Res::Err("Vault has no loans") : Res::Err("There are no loans on this vault (%s)!", vault);
    }

    static Res VaultNoLoans(std::string token) {
        return Res::Err("Vault does not have any %s loans", token);
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

    static Res LoanTokenInvalid(std::string token) {
        return Res::Err("Loan token %s does not exist!", token);
    }

    static Res VaultInvalid(const std::string vaultId) {
        return Res::Err("Cannot find existing vault with id %s", vaultId);
    }

    static Res VaultUnderLiquidation() {
        return Res::Err("Cannot payback loan on vault under liquidation");
    }

    static Res TXMissingInput() {
        return Res::Err("tx must have at least one input from token owner");
    }

    static Res LoanAssetPriceInvalid() {
        return Res::Err("Cannot payback loan while any of the asset's price is invalid");
    }

    static Res LoanTokenIdInvalid(const std::string token) {
        return Res::Err("Loan token with id (%s) does not exist!", token);
    }

    static Res LoanPaymentAmountInvalid(const long amount, const uint32_t value) {
        return Res::Err("Valid payback amount required (input: %d@%d)", amount, value);
    }

    static Res TokenIdInvalid(const std::string token) {
        return Res::Err("Token with id (%s) does not exists", token);
    }

    static Res LoanPaybackDisabled(const std::string token) {
        return token.empty() ? Res::Err("Payback is not currently active") : Res::Err("Payback of loan via %s token is not currently active", token);
    }

    static Res LoanPriceInvalid(const std::string kv, const std::string payback) {
        return Res::Err("Value/price too high (%s/%s)", kv, payback);
    }
};

#endif  // DEFI_MASTERNODES_ERRORS_H

