// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <dfi/accounts.h>
#include <dfi/consensus/loans.h>
#include <dfi/errors.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/loan.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

static bool IsPaybackWithCollateral(CCustomCSView &view, const std::map<DCT_ID, CBalances> &loans) {
    auto tokenDUSD = view.GetToken("DUSD");
    if (!tokenDUSD) {
        return false;
    }

    if (loans.size() == 1 && loans.count(tokenDUSD->first) &&
        loans.at(tokenDUSD->first) == CBalances{{{tokenDUSD->first, 999999999999999999LL}}}) {
        return true;
    }
    return false;
}

static Res PaybackWithCollateral(CCustomCSView &view,
                                 const CVaultData &vault,
                                 const CVaultId &vaultId,
                                 uint32_t height,
                                 uint64_t time) {
    const auto attributes = view.GetAttributes();

    const auto dUsdToken = view.GetToken("DUSD");
    if (!dUsdToken) {
        return DeFiErrors::TokenInvalidForName("DUSD");
    }

    CDataStructureV0 activeKey{AttributeTypes::Token, dUsdToken->first.v, TokenKeys::LoanPaybackCollateral};
    if (!attributes->GetValue(activeKey, false)) {
        return DeFiErrors::LoanPaybackWithCollateralDisable();
    }

    const auto collateralAmounts = view.GetVaultCollaterals(vaultId);
    if (!collateralAmounts) {
        return DeFiErrors::VaultNoCollateral();
    }

    if (!collateralAmounts->balances.count(dUsdToken->first)) {
        return DeFiErrors::VaultNoDUSDCollateral();
    }

    const auto &collateralDUSD = collateralAmounts->balances.at(dUsdToken->first);

    const auto loanAmounts = view.GetLoanTokens(vaultId);
    if (!loanAmounts) {
        return DeFiErrors::VaultNoLoans();
    }

    if (!loanAmounts->balances.count(dUsdToken->first)) {
        return DeFiErrors::VaultNoLoans("DUSD");
    }

    const auto &loanDUSD = loanAmounts->balances.at(dUsdToken->first);

    const auto rate = view.GetInterestRate(vaultId, dUsdToken->first, height);
    if (!rate) {
        return DeFiErrors::TokenInterestRateInvalid("DUSD");
    }
    const auto subInterest = TotalInterest(*rate, height);

    Res res{};
    CAmount subLoanAmount{0};
    CAmount subCollateralAmount{0};
    CAmount burnAmount{0};

    // Case where interest > collateral: decrease interest, wipe collateral.
    if (subInterest > collateralDUSD) {
        subCollateralAmount = collateralDUSD;

        res = view.SubVaultCollateral(vaultId, {dUsdToken->first, subCollateralAmount});
        if (!res) {
            return res;
        }

        res = view.DecreaseInterest(height, vaultId, vault.schemeId, dUsdToken->first, 0, subCollateralAmount);
        if (!res) {
            return res;
        }

        burnAmount = subCollateralAmount;
    } else {
        // Postive interest: Loan + interest > collateral.
        // Negative interest: Loan - abs(interest) > collateral.
        if (loanDUSD + subInterest > collateralDUSD) {
            subLoanAmount = collateralDUSD - subInterest;
            subCollateralAmount = collateralDUSD;
        } else {
            // Common case: Collateral > loans.
            subLoanAmount = loanDUSD;
            subCollateralAmount = loanDUSD + subInterest;
        }

        if (subLoanAmount > 0) {
            TrackDUSDSub(view, {dUsdToken->first, subLoanAmount});
            res = view.SubLoanToken(vaultId, {dUsdToken->first, subLoanAmount});
            if (!res) {
                return res;
            }
        }

        if (subCollateralAmount > 0) {
            res = view.SubVaultCollateral(vaultId, {dUsdToken->first, subCollateralAmount});
            if (!res) {
                return res;
            }
        }

        view.ResetInterest(height, vaultId, vault.schemeId, dUsdToken->first);
        burnAmount = subInterest;
    }

    if (burnAmount > 0) {
        res = view.AddBalance(Params().GetConsensus().burnAddress, {dUsdToken->first, burnAmount});
        if (!res) {
            return res;
        }
    } else {
        TrackNegativeInterest(view, {dUsdToken->first, std::abs(burnAmount)});
    }

    // Guard against liquidation
    const auto collaterals = view.GetVaultCollaterals(vaultId);
    const auto loans = view.GetLoanTokens(vaultId);
    if (loans) {
        if (!collaterals) {
            return DeFiErrors::VaultNeedCollateral();
        }
    }

    auto vaultAssets = view.GetVaultAssets(vaultId, *collaterals, height, time);
    if (!vaultAssets) {
        return std::move(vaultAssets);
    }

    // The check is required to do a ratio check safe guard, or the vault of ratio is unreliable.
    // This can later be removed, if all edge cases of price deviations and max collateral factor for DUSD (1.5
    // currently) can be tested for economical stability. Taking the safer approach for now.
    if (!IsVaultPriceValid(view, vaultId, height)) {
        return DeFiErrors::VaultInvalidPrice();
    }

    const auto scheme = view.GetLoanScheme(vault.schemeId);
    if (vaultAssets.val->ratio() < scheme->ratio) {
        return DeFiErrors::VaultInsufficientCollateralization(vaultAssets.val->ratio(), scheme->ratio);
    }

    if (subCollateralAmount > 0) {
        res = view.SubMintedTokens(dUsdToken->first, subCollateralAmount);
        if (!res) {
            return res;
        }
    }

    return Res::Ok();
}

bool CLoansConsensus::IsTokensMigratedToGovVar() const {
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    return static_cast<int>(height) > consensus.DF16FortCanningCrunchHeight + 1;
}

Res CLoansConsensus::operator()(const CLoanSetCollateralTokenMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member!");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    if (height >= static_cast<uint32_t>(consensus.DF16FortCanningCrunchHeight) && IsTokensMigratedToGovVar()) {
        const auto &tokenId = obj.idToken.v;

        auto attributes = mnview.GetAttributes();
        attributes->time = time;

        CDataStructureV0 collateralEnabled{AttributeTypes::Token, tokenId, TokenKeys::LoanCollateralEnabled};
        CDataStructureV0 collateralFactor{AttributeTypes::Token, tokenId, TokenKeys::LoanCollateralFactor};
        CDataStructureV0 pairKey{AttributeTypes::Token, tokenId, TokenKeys::FixedIntervalPriceId};

        auto gv = GovVariable::Create("ATTRIBUTES");
        if (!gv) {
            return Res::Err("Failed to create ATTRIBUTES Governance variable");
        }

        auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
        if (!var) {
            return Res::Err("Failed to convert ATTRIBUTES Governance variable");
        }

        var->SetValue(collateralEnabled, true);
        var->SetValue(collateralFactor, obj.factor);
        var->SetValue(pairKey, obj.fixedIntervalPriceId);

        if (auto res = attributes->Import(var->Export()); !res) {
            return res;
        }
        if (auto res = attributes->Validate(mnview); !res) {
            return res;
        }
        if (auto res = attributes->Apply(mnview, height); !res) {
            return res;
        }

        return mnview.SetVariable(*attributes);
    }

    CLoanSetCollateralTokenImplementation collToken;
    static_cast<CLoanSetCollateralToken &>(collToken) = obj;

    collToken.creationTx = tx.GetHash();
    collToken.creationHeight = height;

    auto token = mnview.GetToken(collToken.idToken);
    if (!token) {
        return Res::Err("token %s does not exist!", collToken.idToken.ToString());
    }

    if (!collToken.activateAfterBlock) {
        collToken.activateAfterBlock = height;
    }

    if (collToken.activateAfterBlock < height) {
        return Res::Err("activateAfterBlock cannot be less than current height!");
    }

    if (!OraclePriceFeed(mnview, collToken.fixedIntervalPriceId)) {
        return Res::Err("Price feed %s/%s does not belong to any oracle",
                        collToken.fixedIntervalPriceId.first,
                        collToken.fixedIntervalPriceId.second);
    }

    CFixedIntervalPrice fixedIntervalPrice;
    fixedIntervalPrice.priceFeedId = collToken.fixedIntervalPriceId;

    auto price =
        GetAggregatePrice(mnview, collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second, time);
    if (!price) {
        return price;
    }

    fixedIntervalPrice.priceRecord[1] = price;
    fixedIntervalPrice.timestamp = time;

    auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
    if (!resSetFixedPrice) {
        return resSetFixedPrice;
    }

    return mnview.CreateLoanCollateralToken(collToken);
}

Res CLoansConsensus::operator()(const CLoanSetLoanTokenMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member!");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    if (height < static_cast<uint32_t>(consensus.DF18FortCanningGreatWorldHeight)) {
        if (obj.interest < 0) {
            return Res::Err("interest rate cannot be less than 0!");
        }
    }

    CTokenImplementation token;
    auto tokenSymbol = trim_ws(obj.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    auto tokenName = trim_ws(obj.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

    token.symbol = tokenSymbol;
    token.name = tokenName;
    token.creationTx = tx.GetHash();
    token.creationHeight = height;
    token.flags = obj.mintable ? static_cast<uint8_t>(CToken::TokenFlags::Default)
                               : static_cast<uint8_t>(CToken::TokenFlags::Tradeable);
    token.flags |= static_cast<uint8_t>(CToken::TokenFlags::LoanToken) | static_cast<uint8_t>(CToken::TokenFlags::DAT);

    auto tokenId = mnview.CreateToken(token, blockCtx);
    if (!tokenId) {
        return tokenId;
    }

    if (height >= static_cast<uint32_t>(consensus.DF16FortCanningCrunchHeight) && IsTokensMigratedToGovVar()) {
        const auto &id = tokenId.val->v;

        auto attributes = mnview.GetAttributes();
        attributes->time = time;
        attributes->evmTemplate = blockCtx.GetEVMTemplate();

        CDataStructureV0 mintEnabled{AttributeTypes::Token, id, TokenKeys::LoanMintingEnabled};
        CDataStructureV0 mintInterest{AttributeTypes::Token, id, TokenKeys::LoanMintingInterest};
        CDataStructureV0 pairKey{AttributeTypes::Token, id, TokenKeys::FixedIntervalPriceId};

        auto gv = GovVariable::Create("ATTRIBUTES");
        if (!gv) {
            return Res::Err("Failed to create ATTRIBUTES Governance variable");
        }

        auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
        if (!var) {
            return Res::Err("Failed to convert ATTRIBUTES Governance variable");
        }

        var->SetValue(mintEnabled, obj.mintable);
        var->SetValue(mintInterest, obj.interest);
        var->SetValue(pairKey, obj.fixedIntervalPriceId);

        if (auto res = attributes->Import(var->Export()); !res) {
            return res;
        }
        if (auto res = attributes->Validate(mnview); !res) {
            return res;
        }
        if (auto res = attributes->Apply(mnview, height); !res) {
            return res;
        }
        return mnview.SetVariable(*attributes);
    }

    CLoanSetLoanTokenImplementation loanToken;
    static_cast<CLoanSetLoanToken &>(loanToken) = obj;

    loanToken.creationTx = tx.GetHash();
    loanToken.creationHeight = height;

    auto nextPrice = GetAggregatePrice(mnview, obj.fixedIntervalPriceId.first, obj.fixedIntervalPriceId.second, time);
    if (!nextPrice) {
        return nextPrice;
    }

    if (!OraclePriceFeed(mnview, obj.fixedIntervalPriceId)) {
        return Res::Err("Price feed %s/%s does not belong to any oracle",
                        obj.fixedIntervalPriceId.first,
                        obj.fixedIntervalPriceId.second);
    }

    CFixedIntervalPrice fixedIntervalPrice;
    fixedIntervalPrice.priceFeedId = loanToken.fixedIntervalPriceId;
    fixedIntervalPrice.priceRecord[1] = nextPrice;
    fixedIntervalPrice.timestamp = time;

    auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
    if (!resSetFixedPrice) {
        return resSetFixedPrice;
    }

    return mnview.SetLoanToken(loanToken, *(tokenId.val));
}

Res CLoansConsensus::operator()(const CLoanUpdateLoanTokenMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member!");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    const auto hash = txCtx.GetTransaction().GetHash();
    auto &mnview = blockCtx.GetView();

    if (height < static_cast<uint32_t>(consensus.DF18FortCanningGreatWorldHeight)) {
        if (obj.interest < 0) {
            return Res::Err("interest rate cannot be less than 0!");
        }
    }

    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    if (!pair) {
        return Res::Err("Loan token (%s) does not exist!", obj.tokenTx.GetHex());
    }

    auto loanToken =
        (height >= static_cast<uint32_t>(consensus.DF16FortCanningCrunchHeight) && IsTokensMigratedToGovVar())
            ? mnview.GetLoanTokenByID(pair->first)
            : mnview.GetLoanToken(obj.tokenTx);

    if (!loanToken) {
        return Res::Err("Loan token (%s) does not exist!", obj.tokenTx.GetHex());
    }

    if (obj.mintable != loanToken->mintable) {
        loanToken->mintable = obj.mintable;
    }

    if (obj.interest != loanToken->interest) {
        loanToken->interest = obj.interest;
    }

    if (obj.symbol != pair->second.symbol) {
        pair->second.symbol = trim_ws(obj.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    }

    if (obj.name != pair->second.name) {
        pair->second.name = trim_ws(obj.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    }

    if (obj.mintable != (pair->second.flags & (uint8_t)CToken::TokenFlags::Mintable)) {
        pair->second.flags ^= (uint8_t)CToken::TokenFlags::Mintable;
    }

    const auto checkSymbol = height >= static_cast<uint32_t>(consensus.DF23Height);
    UpdateTokenContext ctx{pair->second, blockCtx, true, false, checkSymbol, hash};
    if (auto res = mnview.UpdateToken(ctx); !res) {
        return res;
    }

    if (height >= static_cast<uint32_t>(consensus.DF16FortCanningCrunchHeight) && IsTokensMigratedToGovVar()) {
        const auto &id = pair->first.v;

        auto attributes = mnview.GetAttributes();
        attributes->time = time;

        CDataStructureV0 mintEnabled{AttributeTypes::Token, id, TokenKeys::LoanMintingEnabled};
        CDataStructureV0 mintInterest{AttributeTypes::Token, id, TokenKeys::LoanMintingInterest};
        CDataStructureV0 pairKey{AttributeTypes::Token, id, TokenKeys::FixedIntervalPriceId};

        auto gv = GovVariable::Create("ATTRIBUTES");
        if (!gv) {
            return Res::Err("Failed to create ATTRIBUTES Governance variable");
        }

        auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
        if (!var) {
            return Res::Err("Failed to convert ATTRIBUTES Governance variable");
        }

        var->SetValue(mintEnabled, obj.mintable);
        var->SetValue(mintInterest, obj.interest);
        var->SetValue(pairKey, obj.fixedIntervalPriceId);

        if (auto res = attributes->Import(var->Export()); !res) {
            return res;
        }
        if (auto res = attributes->Validate(mnview); !res) {
            return res;
        }
        if (auto res = attributes->Apply(mnview, height); !res) {
            return res;
        }
        return mnview.SetVariable(*attributes);
    }

    if (obj.fixedIntervalPriceId != loanToken->fixedIntervalPriceId) {
        if (!OraclePriceFeed(mnview, obj.fixedIntervalPriceId)) {
            return Res::Err("Price feed %s/%s does not belong to any oracle",
                            obj.fixedIntervalPriceId.first,
                            obj.fixedIntervalPriceId.second);
        }

        loanToken->fixedIntervalPriceId = obj.fixedIntervalPriceId;
    }

    return mnview.UpdateLoanToken(*loanToken, pair->first);
}

Res CLoansConsensus::operator()(const CLoanSchemeMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member!");
    }

    if (obj.ratio < 100) {
        return Res::Err("minimum collateral ratio cannot be less than 100");
    }

    if (obj.rate < 1000000) {
        return Res::Err("interest rate cannot be less than 0.01");
    }

    if (obj.identifier.empty() || obj.identifier.length() > 8) {
        return Res::Err("id cannot be empty or more than 8 chars long");
    }

    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    // Look for loan scheme which already has matching rate and ratio
    bool duplicateLoan = false;
    std::string duplicateID;
    mnview.ForEachLoanScheme([&](const std::string &key, const CLoanSchemeData &data) {
        // Duplicate scheme already exists
        if (data.ratio == obj.ratio && data.rate == obj.rate) {
            duplicateLoan = true;
            duplicateID = key;
            return false;
        }
        return true;
    });

    if (duplicateLoan) {
        return Res::Err("Loan scheme %s with same interestrate and mincolratio already exists", duplicateID);
    }

    // Look for delayed loan scheme which already has matching rate and ratio
    std::pair<std::string, uint64_t> duplicateKey;
    mnview.ForEachDelayedLoanScheme([&](const std::pair<std::string, uint64_t> &key, const CLoanSchemeMessage &data) {
        // Duplicate delayed loan scheme
        if (data.ratio == obj.ratio && data.rate == obj.rate) {
            duplicateLoan = true;
            duplicateKey = key;
            return false;
        }
        return true;
    });

    if (duplicateLoan) {
        return Res::Err("Loan scheme %s with same interestrate and mincolratio pending on block %d",
                        duplicateKey.first,
                        duplicateKey.second);
    }

    // New loan scheme, no duplicate expected.
    if (mnview.GetLoanScheme(obj.identifier)) {
        if (!obj.updateHeight) {
            return Res::Err("Loan scheme already exist with id %s", obj.identifier);
        }
    } else {
        if (obj.updateHeight) {
            return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);
        }
    }

    // Update set, not max uint64_t which indicates immediate update and not updated on this block.
    if (obj.updateHeight && obj.updateHeight != std::numeric_limits<uint64_t>::max() && obj.updateHeight != height) {
        if (obj.updateHeight < height) {
            return Res::Err("Update height below current block height, set future height");
        }
        return mnview.StoreDelayedLoanScheme(obj);
    }

    // If no default yet exist set this one as default.
    if (!mnview.GetDefaultLoanScheme()) {
        mnview.StoreDefaultLoanScheme(obj.identifier);
    }

    return mnview.StoreLoanScheme(obj);
}

Res CLoansConsensus::operator()(const CDefaultLoanSchemeMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }
    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member!");
    }

    if (obj.identifier.empty() || obj.identifier.length() > 8) {
        return Res::Err("id cannot be empty or more than 8 chars long");
    }

    auto &mnview = blockCtx.GetView();

    if (!mnview.GetLoanScheme(obj.identifier)) {
        return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);
    }

    if (auto currentID = mnview.GetDefaultLoanScheme()) {
        if (*currentID == obj.identifier) {
            return Res::Err("Loan scheme with id %s is already set as default", obj.identifier);
        }
    }

    const auto height = mnview.GetDestroyLoanScheme(obj.identifier);
    if (height) {
        return Res::Err("Cannot set %s as default, set to destroyed on block %d", obj.identifier, *height);
    }
    return mnview.StoreDefaultLoanScheme(obj.identifier);
}

Res CLoansConsensus::operator()(const CDestroyLoanSchemeMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member!");
    }

    if (obj.identifier.empty() || obj.identifier.length() > 8) {
        return Res::Err("id cannot be empty or more than 8 chars long");
    }

    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    if (!mnview.GetLoanScheme(obj.identifier)) {
        return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);
    }

    const auto currentID = mnview.GetDefaultLoanScheme();
    if (!currentID || *currentID == obj.identifier) {
        return Res::Err("Cannot destroy default loan scheme, set new default first");
    }

    // Update set and not updated on this block.
    if (obj.destroyHeight && obj.destroyHeight != height) {
        if (obj.destroyHeight < height) {
            return Res::Err("Destruction height below current block height, set future height");
        }
        return mnview.StoreDelayedDestroyScheme(obj);
    }

    mnview.ForEachVault([&](const CVaultId &vaultId, CVaultData vault) {
        if (vault.schemeId == obj.identifier) {
            vault.schemeId = *mnview.GetDefaultLoanScheme();
            mnview.StoreVault(vaultId, vault);
        }
        return true;
    });

    return mnview.EraseLoanScheme(obj.identifier);
}

Res CLoansConsensus::operator()(const CLoanTakeLoanMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    const auto vault = mnview.GetVault(obj.vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());
    }

    if (vault->isUnderLiquidation) {
        return Res::Err("Cannot take loan on vault under liquidation");
    }

    // vault owner auth
    if (!HasAuth(vault->ownerAddress)) {
        return Res::Err("tx must have at least one input from vault owner");
    }

    if (!IsVaultPriceValid(mnview, obj.vaultId, height)) {
        return Res::Err("Cannot take loan while any of the asset's price in the vault is not live");
    }

    auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
    if (!collaterals) {
        return Res::Err("Vault with id %s has no collaterals", obj.vaultId.GetHex());
    }

    const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);

    auto hasDUSDLoans = false;

    std::optional<CTokensView::TokenIDPair> tokenDUSD;
    if (static_cast<int>(height) >= consensus.DF15FortCanningRoadHeight) {
        tokenDUSD = mnview.GetToken("DUSD");
    }

    uint64_t totalLoansActivePrice = 0, totalLoansNextPrice = 0;
    for (const auto &[tokenId, tokenAmount] : obj.amounts.balances) {
        if (height >= static_cast<uint32_t>(consensus.DF18FortCanningGreatWorldHeight)) {
            if (tokenAmount <= 0) {
                return Res::Err("Valid loan amount required (input: %d@%d)", tokenAmount, tokenId.v);
            }
        }

        auto loanToken = mnview.GetLoanTokenByID(tokenId);
        if (!loanToken) {
            return Res::Err("Loan token with id (%s) does not exist!", tokenId.ToString());
        }

        if (!loanToken->mintable) {
            return Res::Err("Loan cannot be taken on token with id (%s) as \"mintable\" is currently false",
                            tokenId.ToString());
        }
        if (tokenDUSD && tokenId == tokenDUSD->first) {
            hasDUSDLoans = true;
        }

        // Calculate interest
        CAmount currentLoanAmount{};
        bool resetInterestToHeight{};
        auto loanAmountChange = tokenAmount;

        if (loanAmounts && loanAmounts->balances.count(tokenId)) {
            currentLoanAmount = loanAmounts->balances.at(tokenId);
            const auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
            assert(rate);
            const auto totalInterest = TotalInterest(*rate, height);

            if (totalInterest < 0) {
                loanAmountChange = currentLoanAmount > std::abs(totalInterest)
                                       ?
                                       // Interest to decrease smaller than overall existing loan amount.
                                       // So reduce interest from the borrowing principal. If this is negative,
                                       // we'll reduce from principal.
                                       tokenAmount + totalInterest
                                       :
                                       // Interest to decrease is larger than old loan amount.
                                       // We reduce from the borrowing principal. If this is negative,
                                       // we'll reduce from principal.
                                       tokenAmount - currentLoanAmount;
                resetInterestToHeight = true;
                TrackNegativeInterest(
                    mnview,
                    {tokenId,
                     currentLoanAmount > std::abs(totalInterest) ? std::abs(totalInterest) : currentLoanAmount});
            }
        }

        if (loanAmountChange > 0) {
            if (const auto token = mnview.GetToken("DUSD"); token && token->first == tokenId) {
                TrackDUSDAdd(mnview, {tokenId, loanAmountChange});
            }

            if (auto res = mnview.AddLoanToken(obj.vaultId, CTokenAmount{tokenId, loanAmountChange}); !res) {
                return res;
            }
        } else {
            const auto subAmount =
                currentLoanAmount > std::abs(loanAmountChange) ? std::abs(loanAmountChange) : currentLoanAmount;

            if (const auto token = mnview.GetToken("DUSD"); token && token->first == tokenId) {
                TrackDUSDSub(mnview, {tokenId, subAmount});
            }

            if (auto res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{tokenId, subAmount}); !res) {
                return res;
            }
        }

        if (resetInterestToHeight) {
            mnview.ResetInterest(height, obj.vaultId, vault->schemeId, tokenId);
        } else {
            if (auto res = mnview.IncreaseInterest(
                    height, obj.vaultId, vault->schemeId, tokenId, loanToken->interest, loanAmountChange);
                !res) {
                return res;
            }
        }

        const auto tokenCurrency = loanToken->fixedIntervalPriceId;

        auto priceFeed = mnview.GetFixedIntervalPrice(tokenCurrency);
        if (!priceFeed) {
            return priceFeed;
        }

        if (!priceFeed.val->isLive(mnview.GetPriceDeviation())) {
            return Res::Err("No live fixed prices for %s/%s", tokenCurrency.first, tokenCurrency.second);
        }

        for (int i = 0; i < 2; i++) {
            // check active and next price
            auto price = priceFeed.val->priceRecord[int(i > 0)];
            auto amount = MultiplyAmounts(price, tokenAmount);
            if (price > COIN) {
                if (amount < tokenAmount) {
                    return Res::Err(
                        "Value/price too high (%s/%s)", GetDecimalString(tokenAmount), GetDecimalString(price));
                }
            }
            auto &totalLoans = i > 0 ? totalLoansNextPrice : totalLoansActivePrice;
            auto prevLoans = totalLoans;
            totalLoans += amount;
            if (prevLoans > totalLoans) {
                return Res::Err("Exceed maximum loans");
            }
        }

        if (auto res = mnview.AddMintedTokens(tokenId, tokenAmount); !res) {
            return res;
        }

        const auto &address = !obj.to.empty() ? obj.to : vault->ownerAddress;
        CalculateOwnerRewards(address);
        if (auto res = mnview.AddBalance(address, CTokenAmount{tokenId, tokenAmount}); !res) {
            return res;
        }
    }

    auto scheme = mnview.GetLoanScheme(vault->schemeId);
    for (int i = 0; i < 2; i++) {
        // check ratio against current and active price
        bool useNextPrice = i > 0, requireLivePrice = true;
        auto vaultAssets =
            mnview.GetVaultAssets(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
        if (!vaultAssets) {
            return vaultAssets;
        }

        if (vaultAssets.val->ratio() < scheme->ratio) {
            return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                            vaultAssets.val->ratio(),
                            scheme->ratio);
        }

        if (auto res = CollateralPctCheck(hasDUSDLoans, vaultAssets, scheme->ratio); !res) {
            return res;
        }
    }
    return Res::Ok();
}

Res CLoansConsensus::operator()(const CLoanPaybackLoanMessage &obj) const {
    auto &mnview = blockCtx.GetView();

    std::map<DCT_ID, CBalances> loans;
    for (auto &balance : obj.amounts.balances) {
        auto id = balance.first;
        auto amount = balance.second;

        CBalances *loan;
        if (id == DCT_ID{0}) {
            auto tokenDUSD = mnview.GetToken("DUSD");
            if (!tokenDUSD) {
                return DeFiErrors::LoanTokenNotFoundForName("DUSD");
            }
            loan = &loans[tokenDUSD->first];
        } else {
            loan = &loans[id];
        }

        loan->Add({id, amount});
    }
    return (*this)(CLoanPaybackLoanV2Message{obj.vaultId, obj.from, loans});
}

Res CLoansConsensus::operator()(const CLoanPaybackLoanV2Message &obj) const {
    auto res = CheckCustomTx();
    if (!res) {
        return res;
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    const auto vault = mnview.GetVault(obj.vaultId);
    if (!vault) {
        return DeFiErrors::VaultInvalid(obj.vaultId);
    }

    if (vault->isUnderLiquidation) {
        return DeFiErrors::LoanNoPaybackOnLiquidation();
    }

    if (!mnview.GetVaultCollaterals(obj.vaultId)) {
        return DeFiErrors::VaultNoCollateral(obj.vaultId.GetHex());
    }

    if (!HasAuth(obj.from)) {
        return DeFiErrors::TXMissingInput();
    }

    if (static_cast<int>(height) < consensus.DF15FortCanningRoadHeight) {
        if (!IsVaultPriceValid(mnview, obj.vaultId, height)) {
            return DeFiErrors::LoanAssetPriceInvalid();
        }
    }

    // Handle payback with collateral special case
    if (static_cast<int>(height) >= consensus.DF19FortCanningEpilogueHeight &&
        IsPaybackWithCollateral(mnview, obj.loans)) {
        return PaybackWithCollateral(mnview, *vault, obj.vaultId, height, time);
    }

    auto shouldSetVariable = false;
    auto attributes = mnview.GetAttributes();

    for (const auto &[loanTokenId, paybackAmounts] : obj.loans) {
        const auto loanToken = mnview.GetLoanTokenByID(loanTokenId);
        if (!loanToken) {
            return DeFiErrors::LoanTokenIdInvalid(loanTokenId);
        }

        for (const auto &kv : paybackAmounts.balances) {
            const auto &paybackTokenId = kv.first;
            auto paybackAmount = kv.second;

            if (height >= static_cast<uint32_t>(consensus.DF18FortCanningGreatWorldHeight)) {
                if (paybackAmount <= 0) {
                    return DeFiErrors::LoanPaymentAmountInvalid(paybackAmount, paybackTokenId.v);
                }
            }

            CAmount paybackUsdPrice{0}, loanUsdPrice{0}, penaltyPct{COIN};

            auto paybackToken = mnview.GetToken(paybackTokenId);
            if (!paybackToken) {
                return DeFiErrors::TokenIdInvalid(paybackTokenId);
            }

            if (loanTokenId != paybackTokenId) {
                if (!IsVaultPriceValid(mnview, obj.vaultId, height)) {
                    return DeFiErrors::LoanAssetPriceInvalid();
                }

                // search in token to token
                if (paybackTokenId != DCT_ID{0}) {
                    CDataStructureV0 activeKey{
                        AttributeTypes::Token, loanTokenId.v, TokenKeys::LoanPayback, paybackTokenId.v};
                    if (!attributes->GetValue(activeKey, false)) {
                        return DeFiErrors::LoanPaybackDisabled(paybackToken->symbol);
                    }

                    CDataStructureV0 penaltyKey{
                        AttributeTypes::Token, loanTokenId.v, TokenKeys::LoanPaybackFeePCT, paybackTokenId.v};
                    penaltyPct -= attributes->GetValue(penaltyKey, CAmount{0});
                } else {
                    CDataStructureV0 activeKey{AttributeTypes::Token, loanTokenId.v, TokenKeys::PaybackDFI};
                    if (!attributes->GetValue(activeKey, false)) {
                        return DeFiErrors::LoanPaybackDisabled(paybackToken->symbol);
                    }

                    CDataStructureV0 penaltyKey{AttributeTypes::Token, loanTokenId.v, TokenKeys::PaybackDFIFeePCT};
                    penaltyPct -= attributes->GetValue(penaltyKey, COIN / 100);
                }

                // Get token price in USD
                const CTokenCurrencyPair tokenUsdPair{paybackToken->symbol, "USD"};
                bool useNextPrice{false}, requireLivePrice{true};
                const auto resVal = mnview.GetValidatedIntervalPrice(tokenUsdPair, useNextPrice, requireLivePrice);
                if (!resVal) {
                    return std::move(resVal);
                }

                paybackUsdPrice = MultiplyAmounts(*resVal.val, penaltyPct);

                // Calculate the DFI amount in DUSD
                auto usdAmount = MultiplyAmounts(paybackUsdPrice, kv.second);

                if (loanToken->symbol == "DUSD") {
                    paybackAmount = usdAmount;
                    if (paybackUsdPrice > COIN) {
                        if (paybackAmount < kv.second) {
                            return DeFiErrors::AmountOverflowAsValuePrice(kv.second, paybackUsdPrice);
                        }
                    }
                } else {
                    // Get dToken price in USD
                    const CTokenCurrencyPair dTokenUsdPair{loanToken->symbol, "USD"};
                    bool useNextPrice{false}, requireLivePrice{true};
                    const auto resVal = mnview.GetValidatedIntervalPrice(dTokenUsdPair, useNextPrice, requireLivePrice);
                    if (!resVal) {
                        return std::move(resVal);
                    }

                    loanUsdPrice = *resVal.val;

                    paybackAmount = DivideAmounts(usdAmount, loanUsdPrice);
                }
            }

            const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);
            if (!loanAmounts) {
                return DeFiErrors::LoanInvalidVault(obj.vaultId);
            }

            if (!loanAmounts->balances.count(loanTokenId)) {
                return DeFiErrors::LoanInvalidTokenForSymbol(loanToken->symbol);
            }

            const auto &currentLoanAmount = loanAmounts->balances.at(loanTokenId);

            const auto rate = mnview.GetInterestRate(obj.vaultId, loanTokenId, height);
            if (!rate) {
                return DeFiErrors::TokenInterestRateInvalid(loanToken->symbol);
            }

            auto subInterest = TotalInterest(*rate, height);

            if (subInterest < 0) {
                TrackNegativeInterest(
                    mnview,
                    {loanTokenId, currentLoanAmount > std::abs(subInterest) ? std::abs(subInterest) : subInterest});
            }

            // In the case of negative subInterest the amount ends up being added to paybackAmount
            auto subLoan = paybackAmount - subInterest;

            if (paybackAmount < subInterest) {
                subInterest = paybackAmount;
                subLoan = 0;
            } else if (currentLoanAmount - subLoan < 0) {
                subLoan = currentLoanAmount;
            }

            if (loanToken->symbol == "DUSD") {
                TrackDUSDSub(mnview, {loanTokenId, subLoan});
            }

            res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{loanTokenId, subLoan});
            if (!res) {
                return res;
            }

            // Eraseinterest. On subInterest is nil interest ITH and IPB will be updated, if
            // subInterest is negative or IPB is negative and subLoan is equal to the loan amount
            // then IPB will be updated and ITH will be wiped.
            res = mnview.DecreaseInterest(
                height,
                obj.vaultId,
                vault->schemeId,
                loanTokenId,
                subLoan,
                subInterest < 0 || (rate->interestPerBlock.negative && subLoan == currentLoanAmount)
                    ? std::numeric_limits<CAmount>::max()
                    : subInterest);
            if (!res) {
                return res;
            }

            if (height >= static_cast<uint32_t>(consensus.DF12FortCanningMuseumHeight) && subLoan < currentLoanAmount &&
                height < static_cast<uint32_t>(consensus.DF18FortCanningGreatWorldHeight)) {
                auto newRate = mnview.GetInterestRate(obj.vaultId, loanTokenId, height);
                if (!newRate) {
                    return DeFiErrors::TokenInterestRateInvalid(loanToken->symbol);
                }

                if (newRate->interestPerBlock.amount == 0) {
                    return Res::Err(
                        "Cannot payback this amount of loan for %s, either payback full amount or less than this "
                        "amount!",
                        loanToken->symbol);
                }
            }

            CalculateOwnerRewards(obj.from);

            if (paybackTokenId == loanTokenId) {
                res = mnview.SubMintedTokens(loanTokenId, subInterest > 0 ? subLoan : subLoan + subInterest);
                if (!res) {
                    return res;
                }

                // If interest was negative remove it from sub amount
                if (height >= static_cast<uint32_t>(consensus.DF19FortCanningEpilogueHeight) && subInterest < 0) {
                    subLoan += subInterest;
                }

                // Do not sub balance if negative interest fully negates the current loan amount
                if (!(subInterest < 0 && std::abs(subInterest) >= currentLoanAmount)) {
                    // If negative interest plus payback amount overpays then reduce payback amount by the
                    // difference
                    if (subInterest < 0 && paybackAmount - subInterest > currentLoanAmount) {
                        subLoan = currentLoanAmount + subInterest;
                    }

                    // subtract loan amount first, interest is burning below
                    LogPrint(BCLog::LOAN,
                             "CLoanPaybackLoanMessage(): Sub loan from balance - %lld, height - %d\n",
                             subLoan,
                             height);
                    res = mnview.SubBalance(obj.from, CTokenAmount{loanTokenId, subLoan});
                    if (!res) {
                        return res;
                    }
                }

                // burn interest Token->USD->DFI->burnAddress
                if (subInterest > 0) {
                    LogPrint(BCLog::LOAN,
                             "CLoanPaybackLoanMessage(): Swapping %s interest to DFI - %lld, height - %d\n",
                             loanToken->symbol,
                             subInterest,
                             height);
                    res = SwapToDFIorDUSD(
                        mnview, loanTokenId, subInterest, obj.from, consensus.burnAddress, height, consensus);
                    if (!res) {
                        return res;
                    }
                }
            } else {
                CAmount subInToken;
                const auto subAmount = subLoan + subInterest;

                // if payback overpay loan and interest amount
                if (paybackAmount > subAmount) {
                    if (loanToken->symbol == "DUSD") {
                        subInToken = DivideAmounts(subAmount, paybackUsdPrice);
                        if (MultiplyAmounts(subInToken, paybackUsdPrice) != subAmount) {
                            subInToken += 1;
                        }
                    } else {
                        auto tempAmount = MultiplyAmounts(subAmount, loanUsdPrice);

                        subInToken = DivideAmounts(tempAmount, paybackUsdPrice);
                        if (DivideAmounts(MultiplyAmounts(subInToken, paybackUsdPrice), loanUsdPrice) != subAmount) {
                            subInToken += 1;
                        }
                    }
                } else {
                    subInToken = kv.second;
                }

                shouldSetVariable = true;

                auto penalty = MultiplyAmounts(subInToken, COIN - penaltyPct);

                if (paybackTokenId == DCT_ID{0}) {
                    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackDFITokens};
                    auto balances = attributes->GetValue(liveKey, CBalances{});
                    balances.Add({loanTokenId, subAmount});
                    balances.Add({paybackTokenId, penalty});
                    attributes->SetValue(liveKey, balances);

                    liveKey.key = EconomyKeys::PaybackDFITokensPrincipal;
                    balances = attributes->GetValue(liveKey, CBalances{});
                    balances.Add({loanTokenId, subLoan});
                    attributes->SetValue(liveKey, balances);

                    LogPrint(BCLog::LOAN,
                             "CLoanPaybackLoanMessage(): Burning interest and loan in %s directly - total loan "
                             "%lld (%lld %s), height - %d\n",
                             paybackToken->symbol,
                             subLoan + subInterest,
                             subInToken,
                             paybackToken->symbol,
                             height);

                    res = TransferTokenBalance(paybackTokenId, subInToken, obj.from, consensus.burnAddress);
                    if (!res) {
                        return res;
                    }
                } else {
                    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackTokens};
                    auto balances = attributes->GetValue(liveKey, CTokenPayback{});

                    balances.tokensPayback.Add(CTokenAmount{loanTokenId, subAmount});
                    balances.tokensFee.Add(CTokenAmount{paybackTokenId, penalty});
                    attributes->SetValue(liveKey, balances);

                    LogPrint(BCLog::LOAN,
                             "CLoanPaybackLoanMessage(): Swapping %s to DFI and burning it - total loan %lld (%lld "
                             "%s), height - %d\n",
                             paybackToken->symbol,
                             subLoan + subInterest,
                             subInToken,
                             paybackToken->symbol,
                             height);

                    CDataStructureV0 directBurnKey{AttributeTypes::Param, ParamIDs::DFIP2206A, DFIPKeys::DUSDLoanBurn};
                    auto directLoanBurn = attributes->GetValue(directBurnKey, false);

                    res = SwapToDFIorDUSD(mnview,
                                          paybackTokenId,
                                          subInToken,
                                          obj.from,
                                          consensus.burnAddress,
                                          height,
                                          consensus,
                                          !directLoanBurn);
                    if (!res) {
                        return res;
                    }
                }
            }
        }
    }

    return shouldSetVariable ? mnview.SetVariable(*attributes) : Res::Ok();
}

Res CLoansConsensus::operator()(const CPaybackWithCollateralMessage &obj) const {
    if (auto res = CheckCustomTx(); !res) {
        return res;
    }

    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    // vault exists
    const auto vault = mnview.GetVault(obj.vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());
    }

    // vault under liquidation
    if (vault->isUnderLiquidation) {
        return Res::Err("Cannot payback vault with collateral while vault's under liquidation");
    }

    // owner auth
    if (!HasAuth(vault->ownerAddress)) {
        return Res::Err("tx must have at least one input from token owner");
    }

    return PaybackWithCollateral(mnview, *vault, obj.vaultId, height, time);
}
