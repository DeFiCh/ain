// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>
#include <masternodes/consensus/vaults.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/loan.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <masternodes/oracles.h>
#include <masternodes/tokens.h>
#include <masternodes/vault.h>

extern std::string ScriptToString(CScript const& script);

Res CVaultsConsensus::operator()(const CVaultMessage& obj) const {

    auto vaultCreationFee = consensus.vaultCreationFee;
    Require(tx.vout[0].nValue == vaultCreationFee && tx.vout[0].nTokenId == DCT_ID{0},
              "Malformed tx vouts, creation vault fee is %s DFI", GetDecimaleString(vaultCreationFee));

    CVaultData vault{};
    static_cast<CVaultMessage&>(vault) = obj;

    // set loan scheme to default if non provided
    if (vault.schemeId.empty()) {
        auto defaultScheme = mnview.GetDefaultLoanScheme();
        Require(defaultScheme, "There is no default loan scheme");
        vault.schemeId = *defaultScheme;
    }

    // loan scheme exists
    Require(mnview.GetLoanScheme(vault.schemeId), "Cannot find existing loan scheme with id %s", vault.schemeId);

    // check loan scheme is not to be destroyed
    Require(!mnview.GetDestroyLoanScheme(obj.schemeId), "Cannot set %s as loan scheme, set to be destroyed", obj.schemeId);

    return mnview.StoreVault(tx.GetHash(), vault);
}

Res CVaultsConsensus::operator()(const CCloseVaultMessage& obj) const {
    Require(CheckCustomTx());

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(!vault->isUnderLiquidation, "Cannot close vault under liquidation");

    // owner auth
    Require(HasAuth(vault->ownerAddress), "tx must have at least one input from vault owner");

    Require(!mnview.GetLoanTokens(obj.vaultId), "Vault <%s> has loans", obj.vaultId.GetHex());

    CalculateOwnerRewards(obj.to);

    if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId))
        for (const auto& col : collaterals->balances)
            Require(mnview.AddBalance(obj.to, {col.first, col.second}));

    // delete all interest to vault
    Require(mnview.DeleteInterest(obj.vaultId, height));

    // return half fee, the rest is burned at creation
    auto feeBack = consensus.vaultCreationFee / 2;
    Require(mnview.AddBalance(obj.to, {DCT_ID{0}, feeBack}));
    return mnview.EraseVault(obj.vaultId);
}

Res CVaultsConsensus::operator()(const CUpdateVaultMessage& obj) const {
    Require(CheckCustomTx());

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(!vault->isUnderLiquidation, "Cannot update vault under liquidation");

    // owner auth
    Require(HasAuth(vault->ownerAddress), "tx must have at least one input from vault owner");

    // loan scheme exists
    Require(mnview.GetLoanScheme(obj.schemeId), "Cannot find existing loan scheme with id %s", obj.schemeId);

    // loan scheme is not set to be destroyed
    Require(!mnview.GetDestroyLoanScheme(obj.schemeId), "Cannot set %s as loan scheme, set to be destroyed", obj.schemeId);

    Require(IsVaultPriceValid(mnview, obj.vaultId, height), "Cannot update vault while any of the asset's price is invalid");

    // don't allow scheme change when vault is going to be in liquidation
    if (vault->schemeId != obj.schemeId)
        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
            auto scheme = mnview.GetLoanScheme(obj.schemeId);
            for (int i = 0; i < 2; i++) {
                bool useNextPrice = i > 0, requireLivePrice = true;
                Require(CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice));
            }
        }

    vault->schemeId = obj.schemeId;
    vault->ownerAddress = obj.ownerAddress;
    return mnview.UpdateVault(obj.vaultId, *vault);
}

Res CVaultsConsensus::operator()(const CDepositToVaultMessage& obj) const {
    Require(CheckCustomTx());

    // owner auth
    Require(HasAuth(obj.from));

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(!vault->isUnderLiquidation, "Cannot deposit to vault under liquidation");

    // If collateral token exist make sure it is enabled.
    if (mnview.GetCollateralTokenFromAttributes(obj.amount.nTokenId)) {
        CDataStructureV0 collateralKey{AttributeTypes::Token, obj.amount.nTokenId.v, TokenKeys::LoanCollateralEnabled};
        if (auto attributes = mnview.GetAttributes())
            Require(attributes->GetValue(collateralKey, false), "Collateral token (%d) is disabled", obj.amount.nTokenId.v);
    }

    //check balance
    CalculateOwnerRewards(obj.from);
    Require(mnview.SubBalance(obj.from, obj.amount), [&](const std::string& msg) {
        return strprintf("Insufficient funds: can't subtract balance of %s: %s\n", ScriptToString(obj.from), msg);
    });

    Require(mnview.AddVaultCollateral(obj.vaultId, obj.amount));

    bool useNextPrice = false, requireLivePrice = false;
    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    return CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice);
}

Res CVaultsConsensus::operator()(const CWithdrawFromVaultMessage& obj) const {
    Require(CheckCustomTx());

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(!vault->isUnderLiquidation, "Cannot withdraw from vault under liquidation");

    // owner auth
    Require(HasAuth(vault->ownerAddress), "tx must have at least one input from vault owner");

    Require(IsVaultPriceValid(mnview, obj.vaultId, height), "Cannot withdraw from vault while any of the asset's price is invalid");

    Require(mnview.SubVaultCollateral(obj.vaultId, obj.amount));

    if (!mnview.GetLoanTokens(obj.vaultId))
        return mnview.AddBalance(obj.to, obj.amount);

    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    Require(collaterals, "Cannot withdraw all collaterals as there are still active loans in this vault");

    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    Require(CheckNextCollateralRatio(obj.vaultId, *scheme, *collaterals));
    return mnview.AddBalance(obj.to, obj.amount);
}

Res CVaultsConsensus::operator()(const CAuctionBidMessage& obj) const {
    Require(CheckCustomTx());

    // owner auth
    Require(HasAuth(obj.from));

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(vault->isUnderLiquidation, "Cannot bid to vault which is not under liquidation");

    auto data = mnview.GetAuction(obj.vaultId, height);
    Require(data, "No auction data to vault %s", obj.vaultId.GetHex());

    auto batch = mnview.GetAuctionBatch(obj.vaultId, obj.index);
    Require(batch, "No batch to vault/index %s/%d", obj.vaultId.GetHex(), obj.index);

    Require(obj.amount.nTokenId == batch->loanAmount.nTokenId, "Bid token does not match auction one");

    auto bid = mnview.GetAuctionBid(obj.vaultId, obj.index);
    if (!bid) {
        auto amount = MultiplyAmounts(batch->loanAmount.nValue, COIN + data->liquidationPenalty);
        Require(obj.amount.nValue >= amount, "First bid should include liquidation penalty of %d%%", data->liquidationPenalty * 100 / COIN);

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight && data->liquidationPenalty)
            Require(obj.amount.nValue > batch->loanAmount.nValue, "First bid should be higher than batch one");
    } else {
        auto amount = MultiplyAmounts(bid->second.nValue, COIN + (COIN / 100));
        Require(obj.amount.nValue >= amount, "Bid override should be at least 1%% higher than current one");

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight)
            Require(obj.amount.nValue > bid->second.nValue, "Bid override should be higher than last one");

        // immediate refund previous bid
        CalculateOwnerRewards(bid->first);
        mnview.AddBalance(bid->first, bid->second);
    }
    //check balance
    CalculateOwnerRewards(obj.from);
    Require(mnview.SubBalance(obj.from, obj.amount));
    return mnview.StoreAuctionBid(obj.vaultId, obj.index, {obj.from, obj.amount});
}
