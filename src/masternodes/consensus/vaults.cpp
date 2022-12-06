// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>
#include <masternodes/consensus/vaults.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>

extern std::string ScriptToString(CScript const& script);

Res CVaultsConsensus::operator()(const CVaultMessage &obj) const {
    auto vaultCreationFee = consensus.vaultCreationFee;
    Require(tx.vout[0].nValue == vaultCreationFee && tx.vout[0].nTokenId == DCT_ID{0},
            "Malformed tx vouts, creation vault fee is %s DFI",
            GetDecimaleString(vaultCreationFee));

    CVaultData vault{};
    static_cast<CVaultMessage &>(vault) = obj;

    // set loan scheme to default if non provided
    if (obj.schemeId.empty()) {
        auto defaultScheme = mnview.GetDefaultLoanScheme();
        Require(defaultScheme, "There is no default loan scheme");
        vault.schemeId = *defaultScheme;
    }

    // loan scheme exists
    Require(mnview.GetLoanScheme(vault.schemeId), "Cannot find existing loan scheme with id %s", vault.schemeId);

    // check loan scheme is not to be destroyed
    auto height = mnview.GetDestroyLoanScheme(obj.schemeId);
    Require(!height, "Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *height);

    auto vaultId = tx.GetHash();
    return mnview.StoreVault(vaultId, vault);
}

Res CVaultsConsensus::operator()(const CCloseVaultMessage &obj) const {
    Require(CheckCustomTx());

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(!vault->isUnderLiquidation, "Cannot close vault under liquidation");

    // owner auth
    Require(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

    if (const auto loans = mnview.GetLoanTokens(obj.vaultId)) {
        for (const auto &[tokenId, amount] : loans->balances) {
            const auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
            Require(rate, "Cannot get interest rate for this token (%d)", tokenId.v);

            const auto totalInterest = TotalInterest(*rate, height);

            Require(amount + totalInterest <= 0, "Vault <%s> has loans", obj.vaultId.GetHex());

            if (totalInterest < 0) {
                TrackNegativeInterest(
                        mnview, {tokenId, amount > std::abs(totalInterest) ? std::abs(totalInterest) : amount});
            }
        }
    }

    CalculateOwnerRewards(obj.to);
    if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId))
        for (const auto &col : collaterals->balances)
            Require(mnview.AddBalance(obj.to, {col.first, col.second}));

    // delete all interest to vault
    Require(mnview.EraseInterest(obj.vaultId, height));

    // return half fee, the rest is burned at creation
    auto feeBack = consensus.vaultCreationFee / 2;
    Require(mnview.AddBalance(obj.to, {DCT_ID{0}, feeBack}));
    return mnview.EraseVault(obj.vaultId);
}

Res CVaultsConsensus::operator()(const CUpdateVaultMessage &obj) const {
    Require(CheckCustomTx());

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(!vault->isUnderLiquidation, "Cannot update vault under liquidation");

    // owner auth
    Require(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

    // loan scheme exists
    const auto scheme = mnview.GetLoanScheme(obj.schemeId);
    Require(scheme, "Cannot find existing loan scheme with id %s", obj.schemeId);

    // loan scheme is not set to be destroyed
    auto destroyHeight = mnview.GetDestroyLoanScheme(obj.schemeId);
    Require(!destroyHeight,
            "Cannot set %s as loan scheme, set to be destroyed on block %d",
            obj.schemeId,
            *destroyHeight);

    Require(IsVaultPriceValid(mnview, obj.vaultId, height),
            "Cannot update vault while any of the asset's price is invalid");

    // don't allow scheme change when vault is going to be in liquidation
    if (vault->schemeId != obj.schemeId) {
        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
            auto scheme = mnview.GetLoanScheme(obj.schemeId);
            for (int i = 0; i < 2; i++) {
                bool useNextPrice = i > 0, requireLivePrice = true;
                auto collateralsLoans = CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice);
                if (!collateralsLoans)
                    return std::move(collateralsLoans);
            }
        }
        if (height >= static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
            if (const auto loanTokens = mnview.GetLoanTokens(obj.vaultId)) {
                for (const auto &[tokenId, tokenAmount] : loanTokens->balances) {
                    const auto loanToken = mnview.GetLoanTokenByID(tokenId);
                    assert(loanToken);
                    Require(mnview.IncreaseInterest(
                            height, obj.vaultId, obj.schemeId, tokenId, loanToken->interest, 0));
                }
            }
        }
    }

    vault->schemeId     = obj.schemeId;
    vault->ownerAddress = obj.ownerAddress;
    return mnview.UpdateVault(obj.vaultId, *vault);
}

Res CVaultsConsensus::operator()(const CDepositToVaultMessage &obj) const {
    Require(CheckCustomTx());

    // owner auth
    Require(HasAuth(obj.from), "tx must have at least one input from token owner");

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(!vault->isUnderLiquidation, "Cannot deposit to vault under liquidation");

    // If collateral token exist make sure it is enabled.
    if (mnview.GetCollateralTokenFromAttributes(obj.amount.nTokenId)) {
        CDataStructureV0 collateralKey{
                AttributeTypes::Token, obj.amount.nTokenId.v, TokenKeys::LoanCollateralEnabled};
        if (const auto attributes = mnview.GetAttributes()) {
            Require(attributes->GetValue(collateralKey, false),
                    "Collateral token (%d) is disabled",
                    obj.amount.nTokenId.v);
        }
    }

    // check balance
    CalculateOwnerRewards(obj.from);
    Require(mnview.SubBalance(obj.from, obj.amount), [&](const std::string &msg) {
        return strprintf("Insufficient funds: can't subtract balance of %s: %s\n", ScriptToString(obj.from), msg);
    });

    Require(mnview.AddVaultCollateral(obj.vaultId, obj.amount));

    bool useNextPrice = false, requireLivePrice = false;
    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    return CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice);
}

Res CVaultsConsensus::operator()(const CWithdrawFromVaultMessage &obj) const {
    Require(CheckCustomTx());

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(!vault->isUnderLiquidation, "Cannot withdraw from vault under liquidation");

    // owner auth
    Require(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

    Require(IsVaultPriceValid(mnview, obj.vaultId, height),
            "Cannot withdraw from vault while any of the asset's price is invalid");

    Require(mnview.SubVaultCollateral(obj.vaultId, obj.amount));

    auto hasDUSDLoans{false};

    std::optional<std::pair<DCT_ID, std::optional<CTokensView::CTokenImpl> > > tokenDUSD;
    if (static_cast<int>(height) >= consensus.FortCanningRoadHeight) {
        tokenDUSD = mnview.GetToken("DUSD");
    }

    if (const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId)) {
        // Update negative interest in vault
        for (const auto &[tokenId, currentLoanAmount] : loanAmounts->balances) {
            if (tokenDUSD && tokenId == tokenDUSD->first) {
                hasDUSDLoans = true;
            }

            const auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
            assert(rate);

            const auto totalInterest = TotalInterest(*rate, height);

            // Ignore positive or nil interest
            if (totalInterest >= 0) {
                continue;
            }

            const auto subAmount =
                    currentLoanAmount > std::abs(totalInterest) ? std::abs(totalInterest) : currentLoanAmount;

            if (const auto token = mnview.GetToken("DUSD"); token && tokenId == token->first) {
                TrackDUSDSub(mnview, {tokenId, subAmount});
            }

            Require(mnview.SubLoanToken(obj.vaultId, CTokenAmount{tokenId, subAmount}));

            TrackNegativeInterest(mnview, {tokenId, subAmount});

            mnview.ResetInterest(height, obj.vaultId, vault->schemeId, tokenId);
        }

        auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
        Require(collaterals, "Cannot withdraw all collaterals as there are still active loans in this vault");

        auto scheme = mnview.GetLoanScheme(vault->schemeId);
        const auto res = CheckNextCollateralRatio(obj.vaultId, *scheme, *collaterals, hasDUSDLoans);
        return !res ? res : mnview.AddBalance(obj.to, obj.amount);
    }

    return mnview.AddBalance(obj.to, obj.amount);
}

Res CVaultsConsensus::operator()(const CAuctionBidMessage &obj) const {
    Require(CheckCustomTx());

    // owner auth
    Require(HasAuth(obj.from), "tx must have at least one input from token owner");

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    // vault under liquidation
    Require(vault->isUnderLiquidation, "Cannot bid to vault which is not under liquidation");

    auto data = mnview.GetAuction(obj.vaultId, height);
    Require(data, "No auction data to vault %s", obj.vaultId.GetHex());

    auto batch = mnview.GetAuctionBatch({obj.vaultId, obj.index});
    Require(batch, "No batch to vault/index %s/%d", obj.vaultId.GetHex(), obj.index);

    Require(obj.amount.nTokenId == batch->loanAmount.nTokenId, "Bid token does not match auction one");

    auto bid = mnview.GetAuctionBid({obj.vaultId, obj.index});
    if (!bid) {
        auto amount = MultiplyAmounts(batch->loanAmount.nValue, COIN + data->liquidationPenalty);
        Require(amount <= obj.amount.nValue,
                "First bid should include liquidation penalty of %d%%",
                data->liquidationPenalty * 100 / COIN);

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight && data->liquidationPenalty &&
            obj.amount.nValue == batch->loanAmount.nValue)
            return Res::Err("First bid should be higher than batch one");
    } else {
        auto amount = MultiplyAmounts(bid->second.nValue, COIN + (COIN / 100));
        Require(amount <= obj.amount.nValue, "Bid override should be at least 1%% higher than current one");

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight &&
            obj.amount.nValue == bid->second.nValue)
            return Res::Err("Bid override should be higher than last one");

        // immediate refund previous bid
        CalculateOwnerRewards(bid->first);
        mnview.AddBalance(bid->first, bid->second);
    }
    // check balance
    CalculateOwnerRewards(obj.from);
    Require(mnview.SubBalance(obj.from, obj.amount));
    return mnview.StoreAuctionBid({obj.vaultId, obj.index}, {obj.from, obj.amount});
}
