
#include <chainparams.h>
#include <masternodes/loan.h>

std::unique_ptr<CLoanView::CLoanSetCollateralTokenImpl> CLoanView::GetLoanSetCollateralToken(uint256 const & txid) const
{
    auto collToken = ReadBy<LoanSetCollateralTokenCreationTx,CLoanSetCollateralTokenImpl>(txid);
    if (collToken)
        return MakeUnique<CLoanSetCollateralTokenImpl>(*collToken);
    return {};
}

Res CLoanView::LoanCreateSetCollateralToken(CLoanSetCollateralTokenImpl const & collToken)
{
    //this should not happen, but for sure
    if (GetLoanSetCollateralToken(collToken.creationTx))
        return Res::Err("setCollateralToken with creation tx %s already exists!", collToken.creationTx.GetHex());
    if (collToken.factor > COIN)
        return Res::Err("setCollateralToken factor must be lower or equal than %s!", GetDecimaleString(COIN));
    if (collToken.factor < 0)
        return Res::Err("setCollateralToken factor must not be negative!");

    WriteBy<LoanSetCollateralTokenCreationTx>(collToken.creationTx, collToken);

    CollateralTokenKey key{collToken.idToken, collToken.activateAfterBlock};
    WriteBy<LoanSetCollateralTokenKey>(key, collToken.creationTx);

    return Res::Ok();
}

Res CLoanView::LoanUpdateCollateralToken(CLoanSetCollateralTokenImpl const & collateralToken)
{
    if (collateralToken.factor > COIN)
        return Res::Err("setCollateralToken factor must be lower or equal than %s!", GetDecimaleString(COIN));
    if (collateralToken.factor < 0)
        return Res::Err("setCollateralToken factor must not be negative!");

    CollateralTokenKey key{collateralToken.idToken, collateralToken.activateAfterBlock};
    WriteBy<LoanSetCollateralTokenKey>(key, collateralToken.creationTx);

    return Res::Ok();
}

void CLoanView::ForEachLoanSetCollateralToken(std::function<bool (CollateralTokenKey const &, uint256 const &)> callback, CollateralTokenKey const & start)
{
    ForEach<LoanSetCollateralTokenKey, CollateralTokenKey, uint256>(callback, start);
}

std::unique_ptr<CLoanView::CLoanSetCollateralTokenImpl> CLoanView::HasLoanSetCollateralToken(CollateralTokenKey const & key)
{
    auto it = LowerBound<LoanSetCollateralTokenKey>(key);
    if (it.Valid() && it.Key().id == key.id)
        return GetLoanSetCollateralToken(it.Value());
    return {};
}

std::unique_ptr<CLoanView::CLoanSetLoanTokenImpl> CLoanView::GetLoanSetLoanToken(uint256 const & txid) const
{
    auto id = ReadBy<LoanSetLoanTokenCreationTx, DCT_ID>(txid);
    if (id)
        return GetLoanSetLoanTokenByID(*id);
    return {};
}

std::unique_ptr<CLoanView::CLoanSetLoanTokenImpl> CLoanView::GetLoanSetLoanTokenByID(DCT_ID const & id) const
{
    auto loanToken = ReadBy<LoanSetLoanTokenKey,CLoanSetLoanTokenImpl>(id);
    if (loanToken)
        return MakeUnique<CLoanSetLoanTokenImpl>(*loanToken);
    return {};
}

Res CLoanView::LoanSetLoanToken(CLoanSetLoanTokenImpl const & loanToken, DCT_ID const & id)
{
    //this should not happen, but for sure
    if (GetLoanSetLoanTokenByID(id))
        return Res::Err("setLoanToken with creation tx %s already exists!", loanToken.creationTx.GetHex());

    if (loanToken.interest < 0)
        return Res::Err("interest rate cannot be less than 0!");

    WriteBy<LoanSetLoanTokenKey>(id, loanToken);
    WriteBy<LoanSetLoanTokenCreationTx>(loanToken.creationTx, id);

    return Res::Ok();
}

Res CLoanView::LoanUpdateLoanToken(CLoanSetLoanTokenImpl const & loanToken, DCT_ID const & id)
{
    if (loanToken.interest < 0)
        return Res::Err("interest rate cannot be less than 0!");

    WriteBy<LoanSetLoanTokenKey>(id, loanToken);

    return Res::Ok();
}

void CLoanView::ForEachLoanSetLoanToken(std::function<bool (DCT_ID const &, CLoanSetLoanTokenImpl const &)> callback, DCT_ID const & start)
{
    ForEach<LoanSetLoanTokenKey, DCT_ID, CLoanSetLoanTokenImpl>(callback, start);
}

Res CLoanView::StoreLoanScheme(const CLoanSchemeMessage& loanScheme)
{
    WriteBy<LoanSchemeKey>(loanScheme.identifier, static_cast<CLoanSchemeData>(loanScheme));

    return Res::Ok();
}

Res CLoanView::StoreDelayedLoanScheme(const CLoanSchemeMessage& loanScheme)
{
    WriteBy<DelayedLoanSchemeKey>(std::pair<std::string, uint64_t>(loanScheme.identifier, loanScheme.updateHeight), loanScheme);

    return Res::Ok();
}

Res CLoanView::StoreDelayedDestroyScheme(const CDestroyLoanSchemeMessage& loanScheme)
{
    WriteBy<DestroyLoanSchemeKey>(loanScheme.identifier, loanScheme.destroyHeight);

    return Res::Ok();
}

void CLoanView::ForEachLoanScheme(std::function<bool (const std::string&, const CLoanSchemeData&)> callback)
{
    ForEach<LoanSchemeKey, std::string, CLoanSchemeData>(callback);
}

void CLoanView::ForEachDelayedLoanScheme(std::function<bool (const std::pair<std::string, uint64_t>&, const CLoanSchemeMessage&)> callback)
{
    ForEach<DelayedLoanSchemeKey, std::pair<std::string, uint64_t>, CLoanSchemeMessage>(callback);
}

void CLoanView::ForEachDelayedDestroyScheme(std::function<bool (const std::string&, const uint64_t&)> callback)
{
    ForEach<DestroyLoanSchemeKey, std::string, uint64_t>(callback);
}

Res CLoanView::StoreDefaultLoanScheme(const std::string& loanSchemeID)
{
    Write(DefaultLoanSchemeKey::prefix(), loanSchemeID);

    return Res::Ok();
}

boost::optional<std::string> CLoanView::GetDefaultLoanScheme()
{
    std::string loanSchemeID;
    if (Read(DefaultLoanSchemeKey::prefix(), loanSchemeID)) {
        return loanSchemeID;
    }

    return {};
}

boost::optional<CLoanSchemeData> CLoanView::GetLoanScheme(const std::string& loanSchemeID)
{
    return ReadBy<LoanSchemeKey, CLoanSchemeData>(loanSchemeID);
}

boost::optional<uint64_t> CLoanView::GetDestroyLoanScheme(const std::string& loanSchemeID)
{
    return ReadBy<DestroyLoanSchemeKey, uint64_t>(loanSchemeID);
}

Res CLoanView::EraseLoanScheme(const std::string& loanSchemeID)
{
    // Find and delete all related loan scheme updates
    std::vector<uint64_t> loanUpdateHeights;
    ForEachDelayedLoanScheme([&](const std::pair<std::string, uint64_t>& key, const CLoanSchemeMessage&)
    {
        if (key.first == loanSchemeID) {
            loanUpdateHeights.push_back(key.second);
        }
        return true;
    });

    for (const auto& height : loanUpdateHeights) {
        EraseDelayedLoanScheme(loanSchemeID, height);
    }

    // Delete loan scheme
    EraseBy<LoanSchemeKey>(loanSchemeID);

    return Res::Ok();
}

void CLoanView::EraseDelayedLoanScheme(const std::string& loanSchemeID, uint64_t height)
{
    EraseBy<DelayedLoanSchemeKey>(std::pair<std::string, uint64_t>(loanSchemeID, height));
}

void CLoanView::EraseDelayedDestroyScheme(const std::string& loanSchemeID)
{
    EraseBy<DestroyLoanSchemeKey>(loanSchemeID);
}

boost::optional<CInterestRate> CLoanView::GetInterestRate(const std::string& loanSchemeID, DCT_ID id)
{
    return ReadBy<LoanInterestByScheme, CInterestRate>(std::make_pair(loanSchemeID, id));
}

CAmount TotalInterest(const CInterestRate& rate, uint32_t height)
{
    return rate.interestToHeight + ((height - rate.height) * rate.interestPerBlock);
}

CAmount InterestPerBlock(CAmount amount, CAmount tokenInterest, CAmount schemeInterest)
{
    auto netInterest = (tokenInterest + schemeInterest) / 100; // in %
    return MultiplyAmounts(netInterest, amount) / (365 * Params().GetConsensus().blocksPerDay());
}

Res CLoanView::StoreInterest(uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, DCT_ID id, CAmount loanIncreased)
{
    auto scheme = GetLoanScheme(loanSchemeID);
    if (!scheme) {
        return Res::Err("No such scheme id %s", loanSchemeID);
    }
    auto token = GetLoanSetLoanTokenByID(id);
    if (!token) {
        return Res::Err("No such loan token id %s", id.ToString());
    }

    CInterestRate rate{};
    ReadBy<LoanInterestByScheme>(std::make_pair(loanSchemeID, id), rate);

    if (rate.height > height) {
        return Res::Err("Cannot store height in the past");
    }
    if (rate.height) {
        rate.interestToHeight += (height - rate.height) * rate.interestPerBlock;
    }
    rate.height = height;
    rate.interestPerBlock += InterestPerBlock(loanIncreased, token->interest, scheme->rate);

    WriteBy<LoanInterestByScheme>(std::make_pair(loanSchemeID, id), rate);
    return Res::Ok();
}

Res CLoanView::EraseInterest(uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, DCT_ID id, CAmount loanDecreased, CAmount interestDecreased)
{
    auto scheme = GetLoanScheme(loanSchemeID);
    if (!scheme) {
        return Res::Err("No such scheme id %s", loanSchemeID);
    }
    auto token = GetLoanSetLoanTokenByID(id);
    if (!token) {
        return Res::Err("No such loan token id %s", id.ToString());
    }

    CInterestRate rate{};
    ReadBy<LoanInterestByScheme>(std::make_pair(loanSchemeID, id), rate);

    if (rate.height > height) {
        return Res::Err("Cannot store height in the past");
    }
    if (rate.height == 0) {
        return Res::Err("Data mismatch height == 0");
    }
    rate.interestToHeight += (height - rate.height) * rate.interestPerBlock;
    rate.interestToHeight = std::max(CAmount{0}, rate.interestToHeight - interestDecreased);
    rate.height = height;
    rate.interestPerBlock -= InterestPerBlock(loanDecreased, token->interest, scheme->rate);
    rate.interestPerBlock = std::max(CAmount{0}, rate.interestPerBlock);

    WriteBy<LoanInterestByScheme>(std::make_pair(loanSchemeID, id), rate);
    return Res::Ok();
}

void CLoanView::ForEachSchemeInterest(std::function<bool(const std::string&, DCT_ID, CInterestRate)> callback, const std::string& loanSchemeID, DCT_ID id)
{
    ForEach<LoanInterestByScheme, std::pair<std::string, DCT_ID>, CInterestRate>([&](const std::pair<std::string, DCT_ID>& pair, CInterestRate rate) {
        return callback(pair.first, pair.second, rate);
    }, std::make_pair(loanSchemeID, id));
}

Res CLoanView::AddLoanToken(const CVaultId& vaultId, CTokenAmount amount)
{
    if (!GetLoanSetLoanTokenByID(amount.nTokenId)) {
        return Res::Err("No such loan token id %s", amount.nTokenId.ToString());
    }
    CBalances amounts;
    ReadBy<LoanTokenAmount>(vaultId, amounts);
    auto res = amounts.Add(amount);
    if (!res) {
        return res;
    }
    if (!amounts.balances.empty()) {
        WriteBy<LoanTokenAmount>(vaultId, amounts);
    }
    return Res::Ok();
}

Res CLoanView::SubLoanToken(const CVaultId& vaultId, CTokenAmount amount)
{
    if (!GetLoanSetLoanTokenByID(amount.nTokenId)) {
        return Res::Err("No such loan token id %s", amount.nTokenId.ToString());
    }
    auto amounts = GetLoanTokens(vaultId);
    if (!amounts || !amounts->Sub(amount)) {
        return Res::Err("Loan token for vault <%s> not found", vaultId.GetHex());
    }
    if (amounts->balances.empty()) {
        EraseBy<LoanTokenAmount>(vaultId);
    } else {
        WriteBy<LoanTokenAmount>(vaultId, *amounts);
    }
    return Res::Ok();
}

boost::optional<CBalances> CLoanView::GetLoanTokens(const CVaultId& vaultId)
{
    return ReadBy<LoanTokenAmount, CBalances>(vaultId);
}

void CLoanView::ForEachLoanToken(std::function<bool(const CVaultId&, const CBalances&)> callback)
{
    ForEach<LoanTokenAmount, CVaultId, CBalances>(callback);
}

Res CLoanView::SetLoanLiquidationPenalty(CAmount penalty)
{
    Write(LoanLiquidationPenalty::prefix(), penalty);
    return Res::Ok();
}

CAmount CLoanView::GetLoanLiquidationPenalty()
{
    CAmount penalty;
    if (Read(LoanLiquidationPenalty::prefix(), penalty)) {
        return penalty;
    }
    return 5 * COIN / 100;
}
