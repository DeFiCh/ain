// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <masternodes/accounts.h>
#include <masternodes/consensus/loans.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/gv.h>
#include <masternodes/loan.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <masternodes/oracles.h>
#include <masternodes/tokens.h>
#include <primitives/transaction.h>

Res CLoansConsensus::operator()(const CLoanSetCollateralTokenMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CLoanSetCollateralTokenImplementation collToken;
    static_cast<CLoanSetCollateralToken&>(collToken) = obj;

    collToken.creationTx = tx.GetHash();
    collToken.creationHeight = height;

    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member!");

    auto token = mnview.GetToken(collToken.idToken);
    if (!token)
        return Res::Err("token %s does not exist!", collToken.idToken.ToString());

    if (!collToken.activateAfterBlock)
        collToken.activateAfterBlock = height;

    if (collToken.activateAfterBlock < height)
        return Res::Err("activateAfterBlock cannot be less than current height!");

    if (!OraclePriceFeed(collToken.fixedIntervalPriceId))
        return Res::Err("Price feed %s/%s does not belong to any oracle", collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second);

    CFixedIntervalPrice fixedIntervalPrice;
    fixedIntervalPrice.priceFeedId = collToken.fixedIntervalPriceId;

    LogPrint(BCLog::LOAN, "CLoanSetCollateralTokenMessage()->"); /* Continued */
    auto price = GetAggregatePrice(mnview, collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second, time);
    if (!price)
        return Res::Err(price.msg);

    fixedIntervalPrice.priceRecord[1] = price;
    fixedIntervalPrice.timestamp = time;

    LogPrint(BCLog::ORACLE,"CLoanSetCollateralTokenMessage()->"); /* Continued */
    auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
    if (!resSetFixedPrice)
        return Res::Err(resSetFixedPrice.msg);

    return mnview.CreateLoanCollateralToken(collToken);
}

Res CLoansConsensus::operator()(const CLoanSetLoanTokenMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CLoanSetLoanTokenImplementation loanToken;
    static_cast<CLoanSetLoanToken&>(loanToken) = obj;

    loanToken.creationTx = tx.GetHash();
    loanToken.creationHeight = height;

    CFixedIntervalPrice fixedIntervalPrice;
    fixedIntervalPrice.priceFeedId = loanToken.fixedIntervalPriceId;

    auto nextPrice = GetAggregatePrice(mnview, loanToken.fixedIntervalPriceId.first, loanToken.fixedIntervalPriceId.second, time);
    if (!nextPrice)
        return std::move(nextPrice);

    fixedIntervalPrice.priceRecord[1] = nextPrice;
    fixedIntervalPrice.timestamp = time;

    LogPrint(BCLog::ORACLE,"CLoanSetLoanTokenMessage()->"); /* Continued */
    auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
    if (!resSetFixedPrice)
        return Res::Err(resSetFixedPrice.msg);

    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member!");

    if (!OraclePriceFeed(loanToken.fixedIntervalPriceId))
        return Res::Err("Price feed %s/%s does not belong to any oracle", loanToken.fixedIntervalPriceId.first, loanToken.fixedIntervalPriceId.second);

    CTokenImplementation token;
    token.flags = loanToken.mintable ? (uint8_t)CToken::TokenFlags::Default : (uint8_t)CToken::TokenFlags::Tradeable;
    token.flags |= (uint8_t)CToken::TokenFlags::LoanToken | (uint8_t)CToken::TokenFlags::DAT;

    token.symbol = trim_ws(loanToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(loanToken.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    auto tokenId = mnview.CreateToken(token, false);
    if (!tokenId)
        return std::move(tokenId);

    return mnview.SetLoanToken(loanToken, *(tokenId.val));
}

Res CLoansConsensus::operator()(const CLoanUpdateLoanTokenMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member!");

    auto loanToken = mnview.GetLoanToken(obj.tokenTx);
    if (!loanToken)
        return Res::Err("Loan token (%s) does not exist!", obj.tokenTx.GetHex());

    if (obj.mintable != loanToken->mintable)
        loanToken->mintable = obj.mintable;

    if (obj.interest != loanToken->interest)
        loanToken->interest = obj.interest;

    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    if (!pair)
        return Res::Err("Loan token (%s) does not exist!", obj.tokenTx.GetHex());

    if (obj.symbol != pair->second.symbol)
        pair->second.symbol = trim_ws(obj.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

    if (obj.name != pair->second.name)
        pair->second.name = trim_ws(obj.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

    if (obj.fixedIntervalPriceId != loanToken->fixedIntervalPriceId) {
        if (!OraclePriceFeed(obj.fixedIntervalPriceId))
            return Res::Err("Price feed %s/%s does not belong to any oracle", obj.fixedIntervalPriceId.first, obj.fixedIntervalPriceId.second);

        loanToken->fixedIntervalPriceId = obj.fixedIntervalPriceId;
    }

    if (obj.mintable != (pair->second.flags & (uint8_t)CToken::TokenFlags::Mintable))
        pair->second.flags ^= (uint8_t)CToken::TokenFlags::Mintable;

    res = mnview.UpdateToken(pair->second.creationTx, static_cast<CToken>(pair->second), false);
    if (!res)
        return res;

    return mnview.UpdateLoanToken(*loanToken, pair->first);
}

Res CLoansConsensus::operator()(const CLoanSchemeMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member!");

    if (obj.ratio < 100)
        return Res::Err("minimum collateral ratio cannot be less than 100");

    if (obj.rate < 1000000)
        return Res::Err("interest rate cannot be less than 0.01");

    if (obj.identifier.empty() || obj.identifier.length() > 8)
        return Res::Err("id cannot be empty or more than 8 chars long");

    // Look for loan scheme which already has matching rate and ratio
    bool duplicateLoan = false;
    std::string duplicateID;
    mnview.ForEachLoanScheme([&](const std::string& key, const CLoanSchemeData& data) {
        // Duplicate scheme already exists
        if (data.ratio == obj.ratio && data.rate == obj.rate) {
            duplicateLoan = true;
            duplicateID = key;
            return false;
        }
        return true;
    });

    if (duplicateLoan)
        return Res::Err("Loan scheme %s with same interestrate and mincolratio already exists", duplicateID);

    // Look for delayed loan scheme which already has matching rate and ratio
    std::pair<std::string, uint64_t> duplicateKey;
    mnview.ForEachDelayedLoanScheme([&](const std::pair<std::string, uint64_t>& key, const CLoanSchemeMessage& data) {
        // Duplicate delayed loan scheme
        if (data.ratio == obj.ratio && data.rate == obj.rate) {
            duplicateLoan = true;
            duplicateKey = key;
            return false;
        }
        return true;
    });

    if (duplicateLoan)
        return Res::Err("Loan scheme %s with same interestrate and mincolratio pending on block %d", duplicateKey.first, duplicateKey.second);

    // New loan scheme, no duplicate expected.
    if (mnview.GetLoanScheme(obj.identifier)) {
        if (!obj.updateHeight)
            return Res::Err("Loan scheme already exist with id %s", obj.identifier);

    } else if (obj.updateHeight)
        return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);

    // Update set, not max uint64_t which indicates immediate update and not updated on this block.
    if (obj.updateHeight && obj.updateHeight != std::numeric_limits<uint64_t>::max() && obj.updateHeight != height) {
        if (obj.updateHeight < height)
            return Res::Err("Update height below current block height, set future height");

        return mnview.StoreDelayedLoanScheme(obj);
    }

    // If no default yet exist set this one as default.
    if (!mnview.GetDefaultLoanScheme())
        mnview.StoreDefaultLoanScheme(obj.identifier);

    return mnview.StoreLoanScheme(obj);
}

Res CLoansConsensus::operator()(const CDefaultLoanSchemeMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member!");

    if (obj.identifier.empty() || obj.identifier.length() > 8)
        return Res::Err("id cannot be empty or more than 8 chars long");

    if (!mnview.GetLoanScheme(obj.identifier))
        return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);

    const auto currentID = mnview.GetDefaultLoanScheme();
    if (currentID && *currentID == obj.identifier)
        return Res::Err("Loan scheme with id %s is already set as default", obj.identifier);

    if (auto height = mnview.GetDestroyLoanScheme(obj.identifier))
        return Res::Err("Cannot set %s as default, set to destroyed on block %d", obj.identifier, *height);

    return mnview.StoreDefaultLoanScheme(obj.identifier);;
}

Res CLoansConsensus::operator()(const CDestroyLoanSchemeMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member!");

    if (obj.identifier.empty() || obj.identifier.length() > 8)
        return Res::Err("id cannot be empty or more than 8 chars long");

    if (!mnview.GetLoanScheme(obj.identifier))
        return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);

    const auto currentID = mnview.GetDefaultLoanScheme();
    if (currentID && *currentID == obj.identifier)
        return Res::Err("Cannot destroy default loan scheme, set new default first");

    // Update set and not updated on this block.
    if (obj.destroyHeight && obj.destroyHeight != height) {
        if (obj.destroyHeight < height)
            return Res::Err("Destruction height below current block height, set future height");

        return mnview.StoreDelayedDestroyScheme(obj);
    }

    mnview.ForEachVault([&](const CVaultId& vaultId, CVaultData vault) {
        if (vault.schemeId == obj.identifier) {
            vault.schemeId = *mnview.GetDefaultLoanScheme();
            mnview.StoreVault(vaultId, vault);
        }
        return true;
    });

    return mnview.EraseLoanScheme(obj.identifier);
}

Res CLoansConsensus::operator()(const CLoanTakeLoanMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    const auto vault = mnview.GetVault(obj.vaultId);
    if (!vault)
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

    if (vault->isUnderLiquidation)
        return Res::Err("Cannot take loan on vault under liquidation");

    // vault owner auth
    if (!HasAuth(vault->ownerAddress))
        return Res::Err("tx must have at least one input from vault owner");

    if (!IsVaultPriceValid(mnview, obj.vaultId, height))
        return Res::Err("Cannot take loan while any of the asset's price in the vault is not live");

    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    if (!collaterals)
        return Res::Err("Vault with id %s has no collaterals", obj.vaultId.GetHex());

    uint64_t totalLoansActivePrice = 0, totalLoansNextPrice = 0;
    for (const auto& kv : obj.amounts.balances) {
        DCT_ID tokenId = kv.first;
        auto loanToken = mnview.GetLoanTokenByID(tokenId);
        if (!loanToken)
            return Res::Err("Loan token with id (%s) does not exist!", tokenId.ToString());

        if (!loanToken->mintable)
            return Res::Err("Loan cannot be taken on token with id (%s) as \"mintable\" is currently false",tokenId.ToString());

        res = mnview.AddLoanToken(obj.vaultId, CTokenAmount{kv.first, kv.second});
        if (!res)
            return res;

        res = mnview.StoreInterest(height, obj.vaultId, vault->schemeId, tokenId, kv.second);
        if (!res)
            return res;

        auto tokenCurrency = loanToken->fixedIntervalPriceId;

        LogPrint(BCLog::ORACLE,"CLoanTakeLoanMessage()->%s->", loanToken->symbol); /* Continued */
        auto priceFeed = mnview.GetFixedIntervalPrice(tokenCurrency);
        if (!priceFeed)
            return Res::Err(priceFeed.msg);

        if (!priceFeed.val->isLive(mnview.GetPriceDeviation()))
            return Res::Err("No live fixed prices for %s/%s", tokenCurrency.first, tokenCurrency.second);

        for (int i = 0; i < 2; i++) {
            // check active and next price
            auto price = priceFeed.val->priceRecord[int(i > 0)];
            auto amount = MultiplyAmounts(price, kv.second);
            if (price > COIN && amount < kv.second)
                return Res::Err("Value/price too high (%s/%s)", GetDecimaleString(kv.second), GetDecimaleString(price));

            auto& totalLoans = i > 0 ? totalLoansNextPrice : totalLoansActivePrice;
            auto prevLoans = totalLoans;
            totalLoans += amount;
            if (prevLoans > totalLoans)
                return Res::Err("Exceed maximum loans");
        }

        res = mnview.AddMintedTokens(loanToken->creationTx, kv.second);
        if (!res)
            return res;

        const auto& address = !obj.to.empty() ? obj.to : vault->ownerAddress;
        CalculateOwnerRewards(address);
        res = mnview.AddBalance(address, CTokenAmount{kv.first, kv.second});
        if (!res)
            return res;
    }

    LogPrint(BCLog::LOAN,"CLoanTakeLoanMessage():\n");
    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    return CheckNextCollateralRatio(obj.vaultId, *scheme, *collaterals);
}

Res CLoansConsensus::operator()(const CLoanPaybackLoanMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    const auto vault = mnview.GetVault(obj.vaultId);
    if (!vault)
        return Res::Err("Cannot find existing vault with id %s", obj.vaultId.GetHex());

    if (vault->isUnderLiquidation)
        return Res::Err("Cannot payback loan on vault under liquidation");

    if (!mnview.GetVaultCollaterals(obj.vaultId))
        return Res::Err("Vault with id %s has no collaterals", obj.vaultId.GetHex());

    if (!HasAuth(obj.from))
        return Res::Err("tx must have at least one input from token owner");

    if (!IsVaultPriceValid(mnview, obj.vaultId, height))
        return Res::Err("Cannot payback loan while any of the asset's price is invalid");

    auto penaltyPct = COIN;
    auto allowDFIPayback = false;
    auto shouldSetVariable = false;
    auto tokenDUSD = mnview.GetToken("DUSD");
    auto attributes = mnview.GetAttributes();
    if (tokenDUSD && attributes) {
        CDataStructureV0 activeKey{AttributeTypes::Token, tokenDUSD->first.v, TokenKeys::PaybackDFI};
        allowDFIPayback = attributes->GetValue(activeKey, false);
    }

    for (const auto& kv : obj.amounts.balances) {
        DCT_ID tokenId = kv.first;
        auto paybackAmount = kv.second;
        CAmount dfiUSDPrice{0};

        if (height >= consensus.FortCanningHillHeight && kv.first == DCT_ID{0}) {
            if (!allowDFIPayback || !tokenDUSD)
                return Res::Err("Payback of DUSD loans with DFI not currently active");

            // Get DFI price in USD
            const CTokenCurrencyPair dfiUsd{"DFI","USD"};
            bool useNextPrice{false}, requireLivePrice{true};
            const auto resVal = mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice);
            if (!resVal)
                return std::move(resVal);

            // Apply penalty
            CDataStructureV0 penaltyKey{AttributeTypes::Token, tokenDUSD->first.v, TokenKeys::PaybackDFIFeePCT};
            penaltyPct -= attributes->GetValue(penaltyKey, COIN / 100);

            dfiUSDPrice = MultiplyAmounts(*resVal.val, penaltyPct);

            // Set tokenId to DUSD
            tokenId = tokenDUSD->first;

            // Calculate the DFI amount in DUSD
            paybackAmount = MultiplyAmounts(dfiUSDPrice, kv.second);
            if (dfiUSDPrice > COIN && paybackAmount < kv.second)
                return Res::Err("Value/price too high (%s/%s)", GetDecimaleString(kv.second), GetDecimaleString(dfiUSDPrice));
        }

        auto loanToken = mnview.GetLoanTokenByID(tokenId);
        if (!loanToken)
            return Res::Err("Loan token with id (%s) does not exist!", tokenId.ToString());

        auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);
        if (!loanAmounts)
            return Res::Err("There are no loans on this vault (%s)!", obj.vaultId.GetHex());

        auto it = loanAmounts->balances.find(tokenId);
        if (it == loanAmounts->balances.end())
            return Res::Err("There is no loan on token (%s) in this vault!", loanToken->symbol);

        auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
        if (!rate)
            return Res::Err("Cannot get interest rate for this token (%s)!", loanToken->symbol);

        LogPrint(BCLog::LOAN,"CLoanPaybackLoanMessage()->%s->", loanToken->symbol); /* Continued */
        auto subInterest = TotalInterest(*rate, height);
        auto subLoan = paybackAmount - subInterest;

        if (paybackAmount < subInterest) {
            subInterest = paybackAmount;
            subLoan = 0;
        } else if (it->second - subLoan < 0) {
            subLoan = it->second;
        }

        res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{tokenId, subLoan});
        if (!res)
            return res;

        LogPrint(BCLog::LOAN,"CLoanPaybackLoanMessage()->%s->", loanToken->symbol); /* Continued */
        res = mnview.EraseInterest(height, obj.vaultId, vault->schemeId, tokenId, subLoan, subInterest);
        if (!res)
            return res;

        if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight && subLoan < it->second) {
            auto newRate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
            if (!newRate)
                return Res::Err("Cannot get interest rate for this token (%s)!", loanToken->symbol);

            if (newRate->interestPerBlock == 0)
                return Res::Err("Cannot payback this amount of loan for %s, either payback full amount or less than this amount!", loanToken->symbol);
        }

        CalculateOwnerRewards(obj.from);

        if (height < consensus.FortCanningHillHeight || kv.first != DCT_ID{0}) {
            res = mnview.SubMintedTokens(loanToken->creationTx, subLoan);
            if (!res)
                return res;

            // subtract loan amount first, interest is burning below
            LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Sub loan from balance - %lld, height - %d\n", subLoan, height);
            res = mnview.SubBalance(obj.from, CTokenAmount{tokenId, subLoan});
            if (!res)
                return res;

            // burn interest Token->USD->DFI->burnAddress
            if (subInterest) {
                LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Swapping %s interest to DFI - %lld, height - %d\n", loanToken->symbol, subInterest, height);
                res = SwapToDFIOverUSD(mnview, tokenId, subInterest, obj.from, consensus.burnAddress, height);
            }
        } else {
            CAmount subInDFI;
            auto subAmount = subLoan + subInterest;
            // if DFI payback overpay loan and interest amount
            if (paybackAmount > subAmount) {
                subInDFI = DivideAmounts(subAmount, dfiUSDPrice);
                if (MultiplyAmounts(subInDFI, dfiUSDPrice) != subAmount)
                    subInDFI += 1;
            } else {
                subInDFI = kv.second;
            }

            CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackDFITokens};
            auto balances = attributes->GetValue(liveKey, CBalances{});
            auto penaltyDFI = MultiplyAmounts(subInDFI, COIN - penaltyPct);

            balances.Add(CTokenAmount{tokenId, subAmount});
            balances.Add(CTokenAmount{DCT_ID{0}, penaltyDFI});
            attributes->attributes[liveKey] = balances;

            shouldSetVariable = true;

            LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Burning interest and loan in DFI directly - %lld (%lld DFI), height - %d\n", subLoan + subInterest, subInDFI, height);
            res = TransferTokenBalance(DCT_ID{0}, subInDFI, obj.from, consensus.burnAddress);
        }

        if (!res)
            return res;
    }

    return shouldSetVariable ? mnview.SetVariable(*attributes) : Res::Ok();
}
