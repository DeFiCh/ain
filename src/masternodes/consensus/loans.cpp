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
    Require(CheckCustomTx());
    Require(HasFoundationAuth());

    CLoanSetCollateralTokenImplementation collToken;
    static_cast<CLoanSetCollateralToken&>(collToken) = obj;

    collToken.creationTx = tx.GetHash();
    collToken.creationHeight = height;

    auto token = mnview.GetToken(collToken.idToken);
    Require(token, "token %s does not exist!", collToken.idToken.ToString());

    if (!collToken.activateAfterBlock)
        collToken.activateAfterBlock = height;

    Require(collToken.activateAfterBlock >= height, "activateAfterBlock cannot be less than current height!");
    Require(OraclePriceFeed(mnview, collToken.fixedIntervalPriceId), "Price feed %s/%s does not belong to any oracle", collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second);

    CFixedIntervalPrice fixedIntervalPrice;
    fixedIntervalPrice.priceFeedId = collToken.fixedIntervalPriceId;

    auto price = GetAggregatePrice(mnview, collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second, time);
    Require(price);

    fixedIntervalPrice.priceRecord[1] = price;
    fixedIntervalPrice.timestamp = time;

    Require(mnview.SetFixedIntervalPrice(fixedIntervalPrice));
    return mnview.CreateLoanCollateralToken(collToken);
}

Res CLoansConsensus::operator()(const CLoanSetLoanTokenMessage& obj) const {
    Require(CheckCustomTx());
    Require(HasFoundationAuth());

    CLoanSetLoanTokenImplementation loanToken;
    static_cast<CLoanSetLoanToken&>(loanToken) = obj;

    loanToken.creationTx = tx.GetHash();
    loanToken.creationHeight = height;

    CFixedIntervalPrice fixedIntervalPrice;
    fixedIntervalPrice.priceFeedId = loanToken.fixedIntervalPriceId;

    auto nextPrice = GetAggregatePrice(mnview, loanToken.fixedIntervalPriceId.first, loanToken.fixedIntervalPriceId.second, time);
    Require(nextPrice);

    fixedIntervalPrice.priceRecord[1] = nextPrice;
    fixedIntervalPrice.timestamp = time;

    Require(mnview.SetFixedIntervalPrice(fixedIntervalPrice));
    Require(OraclePriceFeed(mnview, loanToken.fixedIntervalPriceId), "Price feed %s/%s does not belong to any oracle", loanToken.fixedIntervalPriceId.first, loanToken.fixedIntervalPriceId.second);

    CTokenImplementation token;
    token.flags = loanToken.mintable ? (uint8_t)CToken::TokenFlags::Default : (uint8_t)CToken::TokenFlags::Tradeable;
    token.flags |= (uint8_t)CToken::TokenFlags::DeprecatedLoanToken | (uint8_t)CToken::TokenFlags::DAT;

    token.symbol = trim_ws(loanToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(loanToken.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    auto tokenId = mnview.CreateToken(token);
    Require(tokenId);

    return mnview.SetLoanToken(loanToken, *(tokenId.val));
}

Res CLoansConsensus::operator()(const CLoanUpdateLoanTokenMessage& obj) const {
    Require(CheckCustomTx());
    Require(HasFoundationAuth());

    auto loanToken = mnview.GetLoanToken(obj.tokenTx);
    Require(loanToken, "Loan token (%s) does not exist!", obj.tokenTx.GetHex());

    if (obj.mintable != loanToken->mintable)
        loanToken->mintable = obj.mintable;

    if (obj.interest != loanToken->interest)
        loanToken->interest = obj.interest;

    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    Require(pair, "Loan token (%s) does not exist!", obj.tokenTx.GetHex());

    if (obj.symbol != pair->second.symbol)
        pair->second.symbol = trim_ws(obj.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

    if (obj.name != pair->second.name)
        pair->second.name = trim_ws(obj.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

    if (obj.fixedIntervalPriceId != loanToken->fixedIntervalPriceId) {
        Require(OraclePriceFeed(mnview, obj.fixedIntervalPriceId), "Price feed %s/%s does not belong to any oracle", obj.fixedIntervalPriceId.first, obj.fixedIntervalPriceId.second);
        loanToken->fixedIntervalPriceId = obj.fixedIntervalPriceId;
    }

    if (obj.mintable != (pair->second.flags & (uint8_t)CToken::TokenFlags::Mintable))
        pair->second.flags ^= (uint8_t)CToken::TokenFlags::Mintable;

    Require(mnview.UpdateToken(pair->second));
    return mnview.UpdateLoanToken(*loanToken, pair->first);
}

Res CLoansConsensus::operator()(const CLoanSchemeMessage& obj) const {
    Require(CheckCustomTx());
    Require(HasFoundationAuth());

    Require(obj.ratio >= 100, "minimum collateral ratio cannot be less than 100");
    Require(obj.rate >= 1000000, "interest rate cannot be less than 0.01");
    Require(!obj.identifier.empty() && obj.identifier.length() <= 8, "id cannot be empty or more than 8 chars long");

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

    Require(!duplicateLoan, "Loan scheme %s with same interestrate and mincolratio already exists", duplicateID);

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

    Require(!duplicateLoan, "Loan scheme %s with same interestrate and mincolratio pending on block %d", duplicateKey.first, duplicateKey.second);

    // New loan scheme, no duplicate expected.
    if (mnview.GetLoanScheme(obj.identifier))
        Require(obj.updateHeight, "Loan scheme already exist with id %s", obj.identifier);
    else
        Require(!obj.updateHeight, "Cannot find existing loan scheme with id %s", obj.identifier);

    // Update set, not max uint64_t which indicates immediate update and not updated on this block.
    if (obj.updateHeight && obj.updateHeight != std::numeric_limits<uint64_t>::max() && obj.updateHeight != height) {
        Require(obj.updateHeight >= height, "Update height below current block height, set future height");
        return mnview.StoreDelayedLoanScheme(obj);
    }

    // If no default yet exist set this one as default.
    if (!mnview.GetDefaultLoanScheme())
        mnview.StoreDefaultLoanScheme(obj.identifier);

    return mnview.StoreLoanScheme(obj);
}

Res CLoansConsensus::operator()(const CDefaultLoanSchemeMessage& obj) const {
    Require(CheckCustomTx());
    Require(HasFoundationAuth());

    Require(!obj.identifier.empty() && obj.identifier.length() <= 8, "id cannot be empty or more than 8 chars long");
    Require(mnview.GetLoanScheme(obj.identifier), "Cannot find existing loan scheme with id %s", obj.identifier);

    if (auto currentID = mnview.GetDefaultLoanScheme())
        Require(*currentID != obj.identifier, "Loan scheme with id %s is already set as default", obj.identifier);

    Require(!mnview.GetDestroyLoanScheme(obj.identifier), "Cannot set %s as default, set to destroyed", obj.identifier);
    return mnview.StoreDefaultLoanScheme(obj.identifier);
}

Res CLoansConsensus::operator()(const CDestroyLoanSchemeMessage& obj) const {
    Require(CheckCustomTx());
    Require(HasFoundationAuth());

    Require(!obj.identifier.empty() && obj.identifier.length() <= 8, "id cannot be empty or more than 8 chars long");
    Require(mnview.GetLoanScheme(obj.identifier), "Cannot find existing loan scheme with id %s", obj.identifier);

    if (auto currentID = mnview.GetDefaultLoanScheme())
        Require(*currentID != obj.identifier, "Cannot destroy default loan scheme, set new default first");

    // Update set and not updated on this block.
    if (obj.destroyHeight && obj.destroyHeight != height) {
        Require(obj.destroyHeight >= height, "Destruction height below current block height, set future height");
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
    Require(CheckCustomTx());

    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

    Require(!vault->isUnderLiquidation, "Cannot take loan on vault under liquidation");

    // vault owner auth
    Require(HasAuth(vault->ownerAddress), "tx must have at least one input from vault owner");

    Require(IsVaultPriceValid(mnview, obj.vaultId, height), "Cannot take loan while any of the asset's price in the vault is not live");

    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    Require(collaterals, "Vault with id %s has no collaterals", obj.vaultId.GetHex());

    uint64_t totalLoansActivePrice = 0, totalLoansNextPrice = 0;
    for (const auto& kv : obj.amounts.balances) {
        DCT_ID tokenId = kv.first;

        auto loanToken = mnview.GetLoanTokenByID(tokenId);
        Require(loanToken, "Loan token with id (%s) does not exist!", tokenId.ToString());
        Require(loanToken->mintable, "Loan cannot be taken on token with id (%s) as \"mintable\" is currently false",tokenId.ToString());

        Require(mnview.AddLoanToken(obj.vaultId, CTokenAmount{kv.first, kv.second}));
        Require(mnview.StoreInterest(height, obj.vaultId, vault->schemeId, tokenId, kv.second));

        auto tokenCurrency = loanToken->fixedIntervalPriceId;

        LogPrint(BCLog::ORACLE,"CLoanTakeLoanMessage()->%s->", loanToken->symbol); /* Continued */
        auto priceFeed = mnview.GetFixedIntervalPrice(tokenCurrency);
        Require(priceFeed);

        Require(priceFeed->isLive(mnview.GetPriceDeviation()), "No live fixed prices for %s/%s", tokenCurrency.first, tokenCurrency.second);

        for (int i = 0; i < 2; i++) {
            // check active and next price
            auto price = priceFeed->priceRecord[int(i > 0)];
            auto amount = MultiplyAmounts(price, kv.second);
            if (price > COIN)
                Require(amount > kv.second, "Value/price too high (%s/%s)", GetDecimaleString(kv.second), GetDecimaleString(price));

            auto& totalLoans = i > 0 ? totalLoansNextPrice : totalLoansActivePrice;
            auto sumLoans = SafeAdd<uint64_t>(totalLoans, amount);
            Require(sumLoans, "Exceed maximum loans");
            totalLoans = sumLoans;
        }

        Require(mnview.AddMintedTokens(tokenId, kv.second));

        const auto& address = !obj.to.empty() ? obj.to : vault->ownerAddress;
        CalculateOwnerRewards(address);
        Require(mnview.AddBalance(address, CTokenAmount{kv.first, kv.second}));
    }

    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    return CheckNextCollateralRatio(obj.vaultId, *scheme, *collaterals);
}

Res CLoansConsensus::operator()(const CLoanPaybackLoanMessage& obj) const {
    std::map<DCT_ID, CBalances> loans;
    for (auto& balance: obj.amounts.balances) {
        auto id = balance.first;
        auto amount = balance.second;

        if (id == DCT_ID{0}) {
            auto tokenDUSD = mnview.GetToken("DUSD");
            Require(tokenDUSD, "Loan token DUSD does not exist!");
            loans[tokenDUSD->first].Add({id, amount});
        } else
            loans[id].Add({id, amount});
    }
    return (*this)(
        CLoanPaybackLoanV2Message{
            obj.vaultId,
            obj.from,
            loans
        });
}

Res CLoansConsensus::operator()(const CLoanPaybackLoanV2Message& obj) const {
    Require(CheckCustomTx());

    auto vault = mnview.GetVault(obj.vaultId);
    Require(vault, "Cannot find existing vault with id %s", obj.vaultId.GetHex());

    Require(!vault->isUnderLiquidation, "Cannot payback loan on vault under liquidation");
    Require(mnview.GetVaultCollaterals(obj.vaultId), "Vault with id %s has no collaterals", obj.vaultId.GetHex());

    Require(HasAuth(obj.from), "tx must have at least one input from token owner");

    if (static_cast<int>(height) < consensus.FortCanningRoadHeight)
        Require(IsVaultPriceValid(mnview, obj.vaultId, height), "Cannot payback loan while any of the asset's price is invalid");

    auto shouldSetVariable = false;
    auto attributes = mnview.GetAttributes();

    for (const auto& idx : obj.loans)
    {
        DCT_ID loanTokenId = idx.first;
        auto loanToken = mnview.GetLoanTokenByID(loanTokenId);
        Require(loanToken, "Loan token with id (%s) does not exist!", loanTokenId.ToString());

        for (const auto& kv : idx.second.balances)
        {
            DCT_ID paybackTokenId = kv.first;
            auto paybackAmount = kv.second;
            CAmount paybackUsdPrice{0}, loanUsdPrice{0}, penaltyPct{COIN};

            auto paybackToken = mnview.GetToken(paybackTokenId);
            Require(paybackToken, "Token with id (%s) does not exists", paybackTokenId.ToString());

            if (loanTokenId != paybackTokenId)
            {
                Require(IsVaultPriceValid(mnview, obj.vaultId, height), "Cannot payback loan while any of the asset's price is invalid");
                Require(attributes, "Payback is not currently active");

                auto defaultPenaltyPct = COIN / 100;
                CDataStructureV0 activeKey, penaltyKey;

                // search in token to token
                if (paybackTokenId != DCT_ID{0})
                {
                    activeKey = {AttributeTypes::Token, loanTokenId.v, TokenKeys::LoanPayback, paybackTokenId.v};
                    penaltyKey = {AttributeTypes::Token, loanTokenId.v, TokenKeys::LoanPaybackFeePCT, paybackTokenId.v};
                    defaultPenaltyPct = 0;
                }
                else
                {
                    activeKey = {AttributeTypes::Token, loanTokenId.v, TokenKeys::PaybackDFI};
                    penaltyKey = {AttributeTypes::Token, loanTokenId.v, TokenKeys::PaybackDFIFeePCT};
                }

                Require(attributes->GetValue(activeKey, false), "Payback of loan via %s token is not currently active", paybackToken->symbol);
                penaltyPct -= attributes->GetValue(penaltyKey, defaultPenaltyPct);

                // Get token price in USD
                const CTokenCurrencyPair tokenUsdPair{paybackToken->symbol, "USD"};
                bool useNextPrice{false}, requireLivePrice{true};
                auto resVal = mnview.GetValidatedIntervalPrice(tokenUsdPair, useNextPrice, requireLivePrice);
                Require(resVal);

                paybackUsdPrice = MultiplyAmounts(*resVal, penaltyPct);

                // Calculate the DFI amount in DUSD
                auto usdAmount = MultiplyAmounts(paybackUsdPrice, kv.second);

                if (loanToken->symbol == "DUSD")
                {
                    paybackAmount = usdAmount;
                    if (paybackUsdPrice > COIN)
                        Require(paybackAmount > kv.second, "Value/price too high (%s/%s)", GetDecimaleString(kv.second), GetDecimaleString(paybackUsdPrice));
                }
                else
                {
                    // Get dToken price in USD
                    const CTokenCurrencyPair dTokenUsdPair{loanToken->symbol, "USD"};
                    auto resVal = mnview.GetValidatedIntervalPrice(dTokenUsdPair, useNextPrice, requireLivePrice);
                    Require(resVal);

                    loanUsdPrice = *resVal;
                    paybackAmount = DivideAmounts(usdAmount, loanUsdPrice);
                }
            }

            auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);
            Require(loanAmounts, "There are no loans on this vault (%s)!", obj.vaultId.GetHex());

            auto it = loanAmounts->balances.find(loanTokenId);
            Require(it != loanAmounts->balances.end(), "There is no loan on token (%s) in this vault!", loanToken->symbol);

            auto rate = mnview.GetInterestRate(obj.vaultId, loanTokenId, height);
            Require(rate, "Cannot get interest rate for this token (%s)!", loanToken->symbol);

            LogPrint(BCLog::LOAN,"CLoanPaybackLoanMessage()->%s->", loanToken->symbol); /* Continued */
            auto subInterest = TotalInterest(*rate, height);
            auto subLoan = paybackAmount - subInterest;

            if (paybackAmount < subInterest)
            {
                subInterest = paybackAmount;
                subLoan = 0;
            }
            else if (it->second - subLoan < 0)
                subLoan = it->second;

            Require(mnview.SubLoanToken(obj.vaultId, CTokenAmount{loanTokenId, subLoan}));

            LogPrint(BCLog::LOAN,"CLoanPaybackLoanMessage()->%s->", loanToken->symbol); /* Continued */
            Require(mnview.EraseInterest(height, obj.vaultId, vault->schemeId, loanTokenId, subLoan, subInterest));

            if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight && subLoan < it->second)
            {
                auto newRate = mnview.GetInterestRate(obj.vaultId, loanTokenId, height);
                Require(newRate, "Cannot get interest rate for this token (%s)!", loanToken->symbol);
                Require(newRate->interestPerBlock > 0, "Cannot payback this amount of loan for %s, either payback full amount or less than this amount!", loanToken->symbol);
            }

            CalculateOwnerRewards(obj.from);

            if (paybackTokenId == loanTokenId)
            {
                Require(mnview.SubMintedTokens(loanTokenId, subLoan));

                // subtract loan amount first, interest is burning below
                LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Sub loan from balance - %lld, height - %d\n", subLoan, height);
                Require(mnview.SubBalance(obj.from, CTokenAmount{loanTokenId, subLoan}));

                // burn interest Token->USD->DFI->burnAddress
                if (subInterest)
                {
                    LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Swapping %s interest to DFI - %lld, height - %d\n", loanToken->symbol, subInterest, height);
                    Require(SwapToDFIOverUSD(mnview, loanTokenId, subInterest, obj.from, consensus.burnAddress, height));
                }
            }
            else
            {
                CAmount subInToken;
                auto subAmount = subLoan + subInterest;

                // if payback overpay loan and interest amount
                if (paybackAmount > subAmount)
                {
                    if (loanToken->symbol == "DUSD")
                    {
                        subInToken = DivideAmounts(subAmount, paybackUsdPrice);
                        if (MultiplyAmounts(subInToken, paybackUsdPrice) != subAmount)
                            subInToken += 1;
                    }
                    else
                    {
                        auto tempAmount = MultiplyAmounts(subAmount, loanUsdPrice);
                        subInToken = DivideAmounts(tempAmount, paybackUsdPrice);
                        if (DivideAmounts(MultiplyAmounts(subInToken, paybackUsdPrice), loanUsdPrice) != subAmount)
                            subInToken += 1;
                    }
                }
                else
                    subInToken = kv.second;

                auto penalty = MultiplyAmounts(subInToken, COIN - penaltyPct);

                if (paybackTokenId == DCT_ID{0})
                {
                    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackDFITokens};
                    auto balances = attributes->GetValue(liveKey, CBalances{});

                    balances.Add(CTokenAmount{loanTokenId, subAmount});
                    balances.Add(CTokenAmount{paybackTokenId, penalty});
                    attributes->SetValue(liveKey, std::move(balances));

                    LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Burning interest and loan in %s directly - total loan %lld (%lld %s), height - %d\n", paybackToken->symbol, subLoan + subInterest, subInToken, paybackToken->symbol, height);

                    Require(TransferTokenBalance(paybackTokenId, subInToken, obj.from, consensus.burnAddress));
                }
                else
                {
                    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackTokens};
                    auto balances = attributes->GetValue(liveKey, CTokenPayback{});

                    balances.tokensPayback.Add(CTokenAmount{loanTokenId, subAmount});
                    balances.tokensFee.Add(CTokenAmount{paybackTokenId, penalty});
                    attributes->SetValue(liveKey, balances);

                    LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Swapping %s to DFI and burning it - total loan %lld (%lld %s), height - %d\n", paybackToken->symbol, subLoan + subInterest, subInToken, paybackToken->symbol, height);

                    Require(SwapToDFIOverUSD(mnview, paybackTokenId, subInToken, obj.from, consensus.burnAddress, height));
                }

                shouldSetVariable = true;
            }
        }
    }

    return shouldSetVariable ? mnview.SetVariable(*attributes) : Res::Ok();
}
