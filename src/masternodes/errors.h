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
        return Res::Err("Below minimum swapable amount, must be at least %s BTC", GetDecimalString(minSwap));
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
        return Res::Err("Value/price too high (%s/%s)", GetDecimalString(amount), GetDecimalString(price)); 
    }

    static Res GovVarVerifyInt() {
        return Res::Err("Value must be an integer");
    }

    static Res GovVarVerifyPositiveNumber() {
        return Res::Err("Value must be a positive integer");
    }

    static Res GovVarInvalidNumber() {
        return Res::Err("Amount must be a valid number");
    }

    static Res GovVarVerifySplitValues() {
        return Res::Err("Two int values expected for split in id/mutliplier");
    }

    static Res GovVarVerifyMultiplier() {
        return Res::Err("Mutliplier cannot be zero");
    }

    static Res GovVarVerifyPair() {
        return Res::Err("Exactly two entires expected for currency pair");
    }

    static Res GovVarVerifyValues() {
        return Res::Err("Empty token / currency");
    }

    static Res GovVarVerifyFeeDirection() {
        return Res::Err("Fee direction value must be both, in or out");
    }

    static Res GovVarVariableLength() {
        return Res::Err("Identifier exceeds maximum length (128)");
    }

    static Res GovVarVariableNoVersion() {
        return Res::Err("Empty version");
    }

    static Res GovVarUnsupportedVersion() {
        return Res::Err("Unsupported version");
    }

    static Res GovVarVariableNumberOfKey() {
        return Res::Err("Incorrect key for <type>. Object of ['<version>/<type>/ID/<key>','value'] expected");
    }

    static Res GovVarVariableInvalidKey(const std::string &key, const std::map<std::string, uint8_t> &keys) {
        std::string error{"Unrecognised " + key + " argument provided, valid " + key + "s are:"};
        for (const auto &pair : keys) {
            error += ' ' + pair.first + ',';
        }
        return Res::Err(error);
    }

    static Res GovVarVariableUnsupportedType(const unsigned char type) {
        return Res::Err("Unsupported type {%d}", type);
    }

    static Res GovVarVariableUnsupportedDFIPType(const unsigned char type) {
        return Res::Err("Unsupported type for DFIP2206A {%d}", type);
    }

    static Res GovVarVariableUnsupportedFeatureType(const unsigned char type) {
        return Res::Err("Unsupported type for Feature {%d}", type);
    }

    static Res GovVarVariableUnsupportedFoundationType(const unsigned char type) {
        return Res::Err("Unsupported type for Foundation {%d}", type);
    }

    static Res GovVarVariableUnsupportedProposalType(const unsigned char type) {
        return Res::Err("Unsupported key for Governance Proposal section - {%d}", type);
    }

    static Res GovVarVariableUnsupportedParamType() {
        return Res::Err("Unsupported Param ID");
    }

    static Res GovVarVariableUnsupportedGovType() {
        return Res::Err("Unsupported Governance ID");
    }

    static Res GovVarVariableKeyCount(const uint32_t expected, const std::vector<std::string> &keys) {
        return Res::Err("Exact %d keys are required {%d}", expected, keys.size());
    }

    static Res GovVarImportObjectExpected() {
        return Res::Err("Object of values expected");
    }

    static Res GovVarValidateFortCanningHill() {
        return Res::Err("Cannot be set before FortCanningHill");
    }

    static Res GovVarValidateFortCanningEpilogue() {
        return Res::Err("Cannot be set before FortCanningEpilogue");
    }

    static Res GovVarValidateFortCanningRoad() {
        return Res::Err("Cannot be set before FortCanningRoad");
    }

    static Res GovVarValidateFortCanningCrunch() {
        return Res::Err("Cannot be set before FortCanningCrunch");
    }

    static Res GovVarValidateFortCanningSpring() {
        return Res::Err("Cannot be set before FortCanningSpringHeight");
    }

    static Res GovVarValidateToken(const uint32_t token) {
        return Res::Err("No such token (%d)", token);
    }

    static Res GovVarValidateTokenExist(const uint32_t token) {
        return Res::Err("Token (%d) does not exist", token);
    }

    static Res GovVarValidateLoanToken(const uint32_t token) {
        return Res::Err("No such loan token (%d)", token);
    }

    static Res GovVarValidateLoanTokenID(const uint32_t token) {
        return Res::Err("No loan token with id (%d)", token);
    }

    static Res GovVarValidateExcessAmount() {
        return Res::Err("Percentage exceeds 100%%");
    }

    static Res GovVarValidateNegativeAmount() {
        return Res::Err("Amount must be a positive value");
    }

    static Res GovVarValidateCurrencyPair() {
        return Res::Err("Fixed interval price currency pair must be set first");
    }

    static Res GovVarUnsupportedValue() {
        return Res::Err("Unsupported value");
    }

    static Res GovVarValidateUnsupportedKey() {
        return Res::Err("Unsupported key");
    }

    static Res GovVarValidateSplitDFI() {
        return Res::Err("Tokenised DFI cannot be split");
    }

    static Res GovVarValidateSplitPool() {
        return Res::Err("Pool tokens cannot be split");
    }

    static Res GovVarValidateSplitDAT() {
        return Res::Err("Only DATs can be split");
    }

    static Res GovVarApplyUnexpectedType() {
        return Res::Err("Unexpected type");
    }

    static Res GovVarApplyInvalidPool(const uint32_t pool) {
        return Res::Err("No such pool (%d)", pool);
    }

    static Res GovVarApplyInvalidFactor(const CAmount ratio) {
        return Res::Err("Factor cannot be more than or equal to the lowest scheme rate of %s", GetDecimalString(ratio * CENT));
    }

    static Res GovVarApplyDFIPActive(const std::string &str) {
        return Res::Err("Cannot set block period while %s is active", str);
    }

    static Res GovVarApplyBelowHeight() {
        return Res::Err("Cannot be set at or below current height");
    }

    static Res GovVarApplyAutoNoToken(const uint32_t token) {
        return Res::Err("Auto lock. No loan token with id (%d)", token);
    }

    static Res GovVarApplyLockFail() {
        return Res::Err("Failed to create Gov var for lock");
    }

    static Res GovVarApplyCastFail() {
        return Res::Err("Failed to cast Gov var to ATTRIBUTES");
    }

    static Res GovVarEraseLive() {
        return Res::Err("Live attribute cannot be deleted");
    }

    static Res GovVarEraseNonExist(const uint32_t type) {
        return Res::Err("Attribute {%d} not exists", type);
    }
};

#endif  // DEFI_MASTERNODES_ERRORS_H

