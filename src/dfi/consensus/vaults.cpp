// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/accounts.h>
#include <dfi/consensus/vaults.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

extern std::string ScriptToString(const CScript &script);

Res CVaultsConsensus::operator()(const CVaultMessage &obj) const {
    const auto &consensus = txCtx.GetConsensus();
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();
    auto &height = blockCtx.GetHeight();
    auto attributes = mnview.GetAttributes();

    const CDataStructureV0 creationFeeKey{AttributeTypes::Vaults, VaultIDs::Parameters, VaultKeys::CreationFee};
    const auto vaultCreationFee = attributes->GetValue(creationFeeKey, consensus.vaultCreationFee);
    if (tx.vout[0].nValue != vaultCreationFee || tx.vout[0].nTokenId != DCT_ID{0}) {
        return Res::Err("Malformed tx vouts, creation vault fee is %s DFI", GetDecimalString(vaultCreationFee));
    }

    CVaultData vault{};
    static_cast<CVaultMessage &>(vault) = obj;

    // set loan scheme to default if non provided
    if (obj.schemeId.empty()) {
        auto defaultScheme = mnview.GetDefaultLoanScheme();
        if (!defaultScheme) {
            return Res::Err("There is no default loan scheme");
        }
        vault.schemeId = *defaultScheme;
    }

    // loan scheme exists
    if (!mnview.GetLoanScheme(vault.schemeId)) {
        return Res::Err("Cannot find existing loan scheme with id %s", vault.schemeId);
    }

    // check loan scheme is not to be destroyed
    if (auto schemeHeight = mnview.GetDestroyLoanScheme(obj.schemeId); schemeHeight) {
        return Res::Err("Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *schemeHeight);
    }

    auto vaultId = tx.GetHash();

    if (height >= consensus.DF23Height) {
        if (!mnview.SetVaultCreationFee(vaultId, vaultCreationFee)) {
            return Res::Err("Failed to set vault height and fee");
        }
    }

    return mnview.StoreVault(vaultId, vault);
}

Res CVaultsConsensus::operator()(const CCloseVaultMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());
    }

    // vault under liquidation
    if (vault->isUnderLiquidation) {
        return Res::Err("Cannot close vault under liquidation");
    }

    // owner auth
    if (!HasAuth(vault->ownerAddress)) {
        return Res::Err("tx must have at least one input from token owner");
    }

    if (const auto loans = mnview.GetLoanTokens(obj.vaultId)) {
        for (const auto &[tokenId, amount] : loans->balances) {
            const auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
            if (!rate) {
                return Res::Err("Cannot get interest rate for this token (%d)", tokenId.v);
            }

            const auto totalInterest = TotalInterest(*rate, height);

            if (amount + totalInterest > 0) {
                return Res::Err("Vault <%s> has loans", obj.vaultId.GetHex());
            }

            // If there is an amount negated by interested remove it from loan tokens.
            if (amount > 0) {
                mnview.SubLoanToken(obj.vaultId, {tokenId, amount});
            }

            if (totalInterest < 0) {
                TrackNegativeInterest(mnview,
                                      {tokenId, amount > std::abs(totalInterest) ? std::abs(totalInterest) : amount});
            }
        }
    }

    CalculateOwnerRewards(obj.to);
    if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
        for (const auto &col : collaterals->balances) {
            if (auto res = mnview.AddBalance(obj.to, {col.first, col.second}); !res) {
                return res;
            }
        }
    }

    // delete all interest to vault
    if (auto res = mnview.EraseInterest(obj.vaultId, height); !res) {
        return res;
    }

    // return half fee, the rest is burned at creation
    const auto vaultCreationFee = mnview.GetVaultCreationFee(obj.vaultId);
    auto feeBack = vaultCreationFee ? *vaultCreationFee / 2 : consensus.vaultCreationFee / 2;
    if (auto res = mnview.AddBalance(obj.to, {DCT_ID{0}, feeBack}); !res) {
        return res;
    }

    if (vaultCreationFee && !mnview.EraseVaultCreationFee(obj.vaultId)) {
        return Res::Err("Failed to erase vault height and fee");
    }

    return mnview.EraseVault(obj.vaultId);
}

Res CVaultsConsensus::operator()(const CUpdateVaultMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());
    }

    // vault under liquidation
    if (vault->isUnderLiquidation) {
        return Res::Err("Cannot update vault under liquidation");
    }

    // owner auth
    if (!HasAuth(vault->ownerAddress)) {
        return Res::Err("tx must have at least one input from token owner");
    }

    // loan scheme exists
    const auto scheme = mnview.GetLoanScheme(obj.schemeId);
    if (!scheme) {
        return Res::Err("Cannot find existing loan scheme with id %s", obj.schemeId);
    }

    // loan scheme is not set to be destroyed
    auto destroyHeight = mnview.GetDestroyLoanScheme(obj.schemeId);
    if (destroyHeight) {
        return Res::Err("Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *destroyHeight);
    }

    if (!IsVaultPriceValid(mnview, obj.vaultId, height)) {
        return Res::Err("Cannot update vault while any of the asset's price is invalid");
    }

    // don't allow scheme change when vault is going to be in liquidation
    if (vault->schemeId != obj.schemeId) {
        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
            for (int i = 0; i < 2; i++) {
                bool useNextPrice = i > 0, requireLivePrice = true;
                auto collateralsLoans =
                    CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice);
                if (!collateralsLoans) {
                    return std::move(collateralsLoans);
                }
            }
        }
        if (height >= static_cast<uint32_t>(consensus.DF18FortCanningGreatWorldHeight)) {
            if (const auto loanTokens = mnview.GetLoanTokens(obj.vaultId)) {
                for (const auto &[tokenId, tokenAmount] : loanTokens->balances) {
                    const auto loanToken = mnview.GetLoanTokenByID(tokenId);
                    assert(loanToken);
                    if (auto res =
                            mnview.IncreaseInterest(height, obj.vaultId, obj.schemeId, tokenId, loanToken->interest, 0);
                        !res) {
                        return res;
                    }
                }
            }
        }
    }

    vault->schemeId = obj.schemeId;
    vault->ownerAddress = obj.ownerAddress;
    return mnview.UpdateVault(obj.vaultId, *vault);
}

Res CVaultsConsensus::operator()(const CDepositToVaultMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    // owner auth
    if (!HasAuth(obj.from)) {
        return Res::Err("tx must have at least one input from token owner");
    }

    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());
    }

    // vault under liquidation
    if (vault->isUnderLiquidation) {
        return Res::Err("Cannot deposit to vault under liquidation");
    }

    // If collateral token exist make sure it is enabled.
    if (mnview.GetCollateralTokenFromAttributes(obj.amount.nTokenId)) {
        CDataStructureV0 collateralKey{AttributeTypes::Token, obj.amount.nTokenId.v, TokenKeys::LoanCollateralEnabled};
        const auto attributes = mnview.GetAttributes();
        if (!attributes->GetValue(collateralKey, false)) {
            return Res::Err("Collateral token (%d) is disabled", obj.amount.nTokenId.v);
        }
    }

    // check balance
    CalculateOwnerRewards(obj.from);
    if (auto res = mnview.SubBalance(obj.from, obj.amount); !res) {
        return Res::Err("Insufficient funds: can't subtract balance of %s: %s\n", ScriptToString(obj.from), res.msg);
    }

    if (auto res = mnview.AddVaultCollateral(obj.vaultId, obj.amount); !res) {
        return res;
    }

    bool useNextPrice = false, requireLivePrice = false;
    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);

    auto vaultAssets = mnview.GetVaultAssets(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
    if (!vaultAssets) {
        return vaultAssets;
    }

    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    return CheckCollateralRatio(obj.vaultId, *scheme, *collaterals, useNextPrice, requireLivePrice);
}

Res CVaultsConsensus::operator()(const CWithdrawFromVaultMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());
    }

    // vault under liquidation
    if (vault->isUnderLiquidation) {
        return Res::Err("Cannot withdraw from vault under liquidation");
    }

    // owner auth
    if (!HasAuth(vault->ownerAddress)) {
        return Res::Err("tx must have at least one input from token owner");
    }

    if (!IsVaultPriceValid(mnview, obj.vaultId, height)) {
        return Res::Err("Cannot withdraw from vault while any of the asset's price is invalid");
    }

    if (auto res = mnview.SubVaultCollateral(obj.vaultId, obj.amount); !res) {
        return res;
    }

    auto hasDUSDLoans = false;

    std::optional<CTokensView::TokenIDPair> tokenDUSD;
    if (static_cast<int>(height) >= consensus.DF15FortCanningRoadHeight) {
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

            if (auto res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{tokenId, subAmount}); !res) {
                return res;
            }

            TrackNegativeInterest(mnview, {tokenId, subAmount});

            mnview.ResetInterest(height, obj.vaultId, vault->schemeId, tokenId);
        }

        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
            const auto scheme = mnview.GetLoanScheme(vault->schemeId);
            for (int i = 0; i < 2; i++) {
                // check collaterals for active and next price
                bool useNextPrice = i > 0, requireLivePrice = true;
                auto vaultAssets =
                    mnview.GetVaultAssets(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
                if (!vaultAssets) {
                    return vaultAssets;
                }

                if (vaultAssets.val->ratio() < scheme->ratio) {
                    return Res::Err(
                        "Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                        vaultAssets.val->ratio(),
                        scheme->ratio);
                }

                if (auto res = CollateralPctCheck(hasDUSDLoans, vaultAssets, scheme->ratio); !res) {
                    return res;
                }
            }
        } else {
            return Res::Err("Cannot withdraw all collaterals as there are still active loans in this vault");
        }
    }

    if (height >= static_cast<uint32_t>(consensus.DF22MetachainHeight)) {
        mnview.CalculateOwnerRewards(obj.to, height);
    }

    return mnview.AddBalance(obj.to, obj.amount);
}

Res CVaultsConsensus::operator()(const CAuctionBidMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    // owner auth
    if (!HasAuth(obj.from)) {
        return Res::Err("tx must have at least one input from token owner");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    // vault exists
    auto vault = mnview.GetVault(obj.vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());
    }

    // vault under liquidation
    if (!vault->isUnderLiquidation) {
        return Res::Err("Cannot bid to vault which is not under liquidation");
    }

    auto data = mnview.GetAuction(obj.vaultId, height);
    if (!data) {
        return Res::Err("No auction data to vault %s", obj.vaultId.GetHex());
    }

    auto batch = mnview.GetAuctionBatch({obj.vaultId, obj.index});
    if (!batch) {
        return Res::Err("No batch to vault/index %s/%d", obj.vaultId.GetHex(), obj.index);
    }

    if (obj.amount.nTokenId != batch->loanAmount.nTokenId) {
        return Res::Err("Bid token does not match auction one");
    }

    auto bid = mnview.GetAuctionBid({obj.vaultId, obj.index});
    if (!bid) {
        auto amount = MultiplyAmounts(batch->loanAmount.nValue, COIN + data->liquidationPenalty);
        if (amount > obj.amount.nValue) {
            return Res::Err("First bid should include liquidation penalty of %d%%",
                            data->liquidationPenalty * 100 / COIN);
        }

        if (static_cast<int>(height) >= consensus.DF12FortCanningMuseumHeight && data->liquidationPenalty &&
            obj.amount.nValue == batch->loanAmount.nValue) {
            return Res::Err("First bid should be higher than batch one");
        }
    } else {
        auto amount = MultiplyAmounts(bid->second.nValue, COIN + (COIN / 100));
        if (amount > obj.amount.nValue) {
            return Res::Err("Bid override should be at least 1%% higher than current one");
        }

        if (static_cast<int>(height) >= consensus.DF12FortCanningMuseumHeight &&
            obj.amount.nValue == bid->second.nValue) {
            return Res::Err("Bid override should be higher than last one");
        }

        // immediate refund previous bid
        CalculateOwnerRewards(bid->first);
        mnview.AddBalance(bid->first, bid->second);
    }
    // check balance
    CalculateOwnerRewards(obj.from);
    if (auto res = mnview.SubBalance(obj.from, obj.amount); !res) {
        return res;
    }
    return mnview.StoreAuctionBid({obj.vaultId, obj.index}, {obj.from, obj.amount});
}
