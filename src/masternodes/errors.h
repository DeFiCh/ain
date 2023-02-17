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

    static Res CannotFindDUSD() {
        return Res::Err("Cannot find token DUSD");
    }

    static Res PaybackWithCollateralDisable() {
        return Res::Err("Payback of DUSD loan with collateral is not currently active");
    }

    static Res NoCollateral() {
        return Res::Err("Vault has no collaterals");
    }

    static Res NoDUSDCollateral() {
        return Res::Err("Vault does not have any DUSD collaterals");
    }

    static Res NoLoans() {
        return Res::Err("Vault has no loans");
    }

    static Res NoDUSDLoans() {
        return Res::Err("Vault does not have any DUSD loans");
    }

    static Res CannotGetInterestRate() {
        return Res::Err("Cannot get interest rate for this token (DUSD)!");
    }

    static Res NeedCollateral() {
        return Res::Err("Vault cannot have loans without collaterals");
    }

    static Res InvalidVaultPrice() {
        return Res::Err("Cannot payback vault with non-DUSD assets while any of the asset's price is invalid");
    }

    static Res InsufficientCollateralization(uint32_t collateralizationRatio, uint32_t schemeRatio) {
        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                        collateralizationRatio,
                        schemeRatio);
    }

};

#endif  // DEFI_MASTERNODES_ERRORS_H

