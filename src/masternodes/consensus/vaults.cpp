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
    if (tx.vout[0].nValue != vaultCreationFee || tx.vout[0].nTokenId != DCT_ID{0})
        return Res::Err("Malformed tx vouts, creation vault fee is %s DFI", GetDecimaleString(vaultCreationFee));

    CVaultData vault{};
    static_cast<CVaultMessage&>(vault) = obj;

    // set loan scheme to default if non provided
    if (vault.schemeId.empty()) {
        auto defaultScheme = mnview.GetDefaultLoanScheme();
        if (!defaultScheme)
            return Res::Err("There is no default loan scheme");

        vault.schemeId = *defaultScheme;
    }

    // loan scheme exists
    if (!mnview.GetLoanScheme(vault.schemeId))
        return Res::Err("Cannot find existing loan scheme with id %s", vault.schemeId);

    // check loan scheme is not to be destroyed
    if (auto height = mnview.GetDestroyLoanScheme(obj.schemeId))
        return Res::Err("Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *height);

    return mnview.StoreVault(tx.GetHash(), vault);
}

Res CVaultsConsensus::operator()(const CCloseVaultMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault)
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    if (vault->isUnderLiquidation)
        return Res::Err("Cannot close vault under liquidation");

    // owner auth
    if (!HasAuth(vault->ownerAddress))
        return Res::Err("tx must have at least one input from token owner");

    if (mnview.GetLoanTokens(obj.vaultId))
        return Res::Err("Vault <%s> has loans", obj.vaultId.GetHex());

    CalculateOwnerRewards(obj.to);

    if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
        for (const auto& col : collaterals->balances) {
            auto res = mnview.AddBalance(obj.to, {col.first, col.second});
            if (!res)
                return res;
        }
    }

    // delete all interest to vault
    res = mnview.DeleteInterest(obj.vaultId, height);
    if (!res)
        return res;

    // return half fee, the rest is burned at creation
    auto feeBack = consensus.vaultCreationFee / 2;
    res = mnview.AddBalance(obj.to, {DCT_ID{0}, feeBack});
    return !res ? res : mnview.EraseVault(obj.vaultId);
}

Res CVaultsConsensus::operator()(const CUpdateVaultMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault)
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    if (vault->isUnderLiquidation)
        return Res::Err("Cannot update vault under liquidation");

    // owner auth
    if (!HasAuth(vault->ownerAddress))
        return Res::Err("tx must have at least one input from token owner");

    // loan scheme exists
    if (!mnview.GetLoanScheme(obj.schemeId))
        return Res::Err("Cannot find existing loan scheme with id %s", obj.schemeId);

    // loan scheme is not set to be destroyed
    if (auto height = mnview.GetDestroyLoanScheme(obj.schemeId))
        return Res::Err("Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *height);

    if (!IsVaultPriceValid(mnview, obj.vaultId, height))
        return Res::Err("Cannot update vault while any of the asset's price is invalid");

    // don't allow scheme change when vault is going to be in liquidation
    if (vault->schemeId != obj.schemeId)
        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
            auto scheme = mnview.GetLoanScheme(obj.schemeId);
            for (int i = 0; i < 2; i++) {
                bool useNextPrice = i > 0, requireLivePrice = true;
                auto collateralsLoans = CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice);
                if (!collateralsLoans)
                    return std::move(collateralsLoans);
            }
        }

    vault->schemeId = obj.schemeId;
    vault->ownerAddress = obj.ownerAddress;
    return mnview.UpdateVault(obj.vaultId, *vault);
}

Res CVaultsConsensus::operator()(const CDepositToVaultMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    // owner auth
    if (!HasAuth(obj.from))
        return Res::Err("tx must have at least one input from token owner");

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault)
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    if (vault->isUnderLiquidation)
        return Res::Err("Cannot deposit to vault under liquidation");

    // If collateral token exist make sure it is enabled.
    if (mnview.GetCollateralTokenFromAttributes(obj.amount.nTokenId)) {
        CDataStructureV0 collateralKey{AttributeTypes::Token, obj.amount.nTokenId.v, TokenKeys::LoanCollateralEnabled};
        if (auto attributes = mnview.GetAttributes(); !attributes->GetValue(collateralKey, false)) {
            return Res::Err("Collateral token (%d) is disabled", obj.amount.nTokenId.v);
        }
    }

    //check balance
    CalculateOwnerRewards(obj.from);
    res = mnview.SubBalance(obj.from, obj.amount);
    if (!res)
        return Res::Err("Insufficient funds: can't subtract balance of %s: %s\n", ScriptToString(obj.from), res.msg);

    res = mnview.AddVaultCollateral(obj.vaultId, obj.amount);
    if (!res)
        return res;

    bool useNextPrice = false, requireLivePrice = false;
    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    return CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice);
}

Res CVaultsConsensus::operator()(const CWithdrawFromVaultMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault)
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    if (vault->isUnderLiquidation)
        return Res::Err("Cannot withdraw from vault under liquidation");

    // owner auth
    if (!HasAuth(vault->ownerAddress))
        return Res::Err("tx must have at least one input from token owner");

    if (!IsVaultPriceValid(mnview, obj.vaultId, height))
        return Res::Err("Cannot withdraw from vault while any of the asset's price is invalid");

    res = mnview.SubVaultCollateral(obj.vaultId, obj.amount);
    if (!res)
        return res;

    if (!mnview.GetLoanTokens(obj.vaultId))
        return mnview.AddBalance(obj.to, obj.amount);

    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    if (!collaterals)
        return Res::Err("Cannot withdraw all collaterals as there are still active loans in this vault");

    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    res = CheckNextCollateralRatio(obj.vaultId, *scheme, *collaterals);
    return !res ? res : mnview.AddBalance(obj.to, obj.amount);
}

Res CVaultsConsensus::operator()(const CAuctionBidMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    // owner auth
    if (!HasAuth(obj.from))
        return Res::Err("tx must have at least one input from token owner");

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault)
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    if (!vault->isUnderLiquidation)
        return Res::Err("Cannot bid to vault which is not under liquidation");

    auto data = mnview.GetAuction(obj.vaultId, height);
    if (!data)
        return Res::Err("No auction data to vault %s", obj.vaultId.GetHex());

    auto batch = mnview.GetAuctionBatch(obj.vaultId, obj.index);
    if (!batch)
        return Res::Err("No batch to vault/index %s/%d", obj.vaultId.GetHex(), obj.index);

    if (obj.amount.nTokenId != batch->loanAmount.nTokenId)
        return Res::Err("Bid token does not match auction one");

    auto bid = mnview.GetAuctionBid(obj.vaultId, obj.index);
    if (!bid) {
        auto amount = MultiplyAmounts(batch->loanAmount.nValue, COIN + data->liquidationPenalty);
        if (amount > obj.amount.nValue)
            return Res::Err("First bid should include liquidation penalty of %d%%", data->liquidationPenalty * 100 / COIN);

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight
        && data->liquidationPenalty && obj.amount.nValue == batch->loanAmount.nValue)
            return Res::Err("First bid should be higher than batch one");

    } else {
        auto amount = MultiplyAmounts(bid->second.nValue, COIN + (COIN / 100));
        if (amount > obj.amount.nValue)
            return Res::Err("Bid override should be at least 1%% higher than current one");

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight
        && obj.amount.nValue == bid->second.nValue)
            return Res::Err("Bid override should be higher than last one");

        // immediate refund previous bid
        CalculateOwnerRewards(bid->first);
        mnview.AddBalance(bid->first, bid->second);
    }
    //check balance
    CalculateOwnerRewards(obj.from);
    res = mnview.SubBalance(obj.from, obj.amount);
    return !res ? res : mnview.StoreAuctionBid(obj.vaultId, obj.index, {obj.from, obj.amount});
}
