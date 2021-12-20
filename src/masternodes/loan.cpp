
#include <chainparams.h>
#include <masternodes/loan.h>

std::unique_ptr<CLoanView::CLoanSetCollateralTokenImpl> CLoanView::GetLoanCollateralToken(uint256 const & txid) const
{
    auto collToken = ReadBy<LoanSetCollateralTokenCreationTx,CLoanSetCollateralTokenImpl>(txid);
    if (collToken)
        return MakeUnique<CLoanSetCollateralTokenImpl>(*collToken);
    return {};
}

Res CLoanView::CreateLoanCollateralToken(CLoanSetCollateralTokenImpl const & collToken)
{
    //this should not happen, but for sure
    if (GetLoanCollateralToken(collToken.creationTx))
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

Res CLoanView::UpdateLoanCollateralToken(CLoanSetCollateralTokenImpl const & collateralToken)
{
    if (collateralToken.factor > COIN)
        return Res::Err("setCollateralToken factor must be lower or equal than %s!", GetDecimaleString(COIN));
    if (collateralToken.factor < 0)
        return Res::Err("setCollateralToken factor must not be negative!");

    CollateralTokenKey key{collateralToken.idToken, collateralToken.activateAfterBlock};
    WriteBy<LoanSetCollateralTokenKey>(key, collateralToken.creationTx);

    return Res::Ok();
}

void CLoanView::ForEachLoanCollateralToken(std::function<bool (CollateralTokenKey const &, uint256 const &)> callback, CollateralTokenKey const & start)
{
    ForEach<LoanSetCollateralTokenKey, CollateralTokenKey, uint256>(callback, start);
}

std::unique_ptr<CLoanView::CLoanSetCollateralTokenImpl> CLoanView::HasLoanCollateralToken(CollateralTokenKey const & key)
{
    auto it = LowerBound<LoanSetCollateralTokenKey>(key);
    if (it.Valid() && it.Key().id == key.id)
        return GetLoanCollateralToken(it.Value());
    return {};
}

std::unique_ptr<CLoanView::CLoanSetLoanTokenImpl> CLoanView::GetLoanToken(uint256 const & txid) const
{
    auto id = ReadBy<LoanSetLoanTokenCreationTx, DCT_ID>(txid);
    if (id)
        return GetLoanTokenByID(*id);
    return {};
}

std::unique_ptr<CLoanView::CLoanSetLoanTokenImpl> CLoanView::GetLoanTokenByID(DCT_ID const & id) const
{
    auto loanToken = ReadBy<LoanSetLoanTokenKey,CLoanSetLoanTokenImpl>(id);
    if (loanToken)
        return MakeUnique<CLoanSetLoanTokenImpl>(*loanToken);
    return {};
}

Res CLoanView::SetLoanToken(CLoanSetLoanTokenImpl const & loanToken, DCT_ID const & id)
{
    //this should not happen, but for sure
    if (GetLoanTokenByID(id))
        return Res::Err("setLoanToken with creation tx %s already exists!", loanToken.creationTx.GetHex());

    if (loanToken.interest < 0)
        return Res::Err("interest rate cannot be less than 0!");

    WriteBy<LoanSetLoanTokenKey>(id, loanToken);
    WriteBy<LoanSetLoanTokenCreationTx>(loanToken.creationTx, id);

    return Res::Ok();
}

Res CLoanView::UpdateLoanToken(CLoanSetLoanTokenImpl const & loanToken, DCT_ID const & id)
{
    if (loanToken.interest < 0)
        return Res::Err("interest rate cannot be less than 0!");

    WriteBy<LoanSetLoanTokenKey>(id, loanToken);

    return Res::Ok();
}

void CLoanView::ForEachLoanToken(std::function<bool (DCT_ID const &, CLoanSetLoanTokenImpl const &)> callback, DCT_ID const & start)
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

boost::optional<CInterestRate> CLoanView::GetInterestRate(const CVaultId& vaultId, DCT_ID id)
{
    return ReadBy<LoanInterestByVault, CInterestRate>(std::make_pair(vaultId, id));
}

boost::optional<CInterestRateV2> CLoanView::GetInterestRateV2(const CVaultId& vaultId, DCT_ID id, uint32_t height)
{
    if (height >= Params().GetConsensus().FortCanningHillHeight) {
        return ReadBy<LoanInterestByVault, CInterestRateV2>(std::make_pair(vaultId, id));
    }

    if (auto rate = GetInterestRate(vaultId, id)) {
        return ConvertInterestRateToV2(*rate);
    }

    return {};
}

inline base_uint<128> InterestPerBlock(CAmount amount, CAmount tokenInterest, CAmount schemeInterest, uint32_t height)
{
    CAmount netInterest((tokenInterest + schemeInterest) / 100); // in % (in COIN precision)

    if (height >= Params().GetConsensus().FortCanningHillHeight)
    {
        return (base_uint<128>(netInterest) * base_uint<128>(amount)) / (365 * Params().GetConsensus().blocksPerDay());
    }
    else
        return MultiplyAmounts(netInterest, amount) / (365 * Params().GetConsensus().blocksPerDay());
}

inline float InterestPerBlockFloat(CAmount amount, CAmount tokenInterest, CAmount schemeInterest)
{
    auto netInterest = (tokenInterest + schemeInterest) / 100; // in %
    return MultiplyAmounts(netInterest, amount) / (365.f * Params().GetConsensus().blocksPerDay());
}

base_uint<128> TotalInterestHighPrecision(const CInterestRateV2& rate, uint32_t height)
{
    base_uint<128> interest;
    interest = rate.interestToHeight + (base_uint<128>(height - rate.height) * rate.interestPerBlock);

    LogPrint(BCLog::LOAN, "%s(): CInterestRate{.height=%d, .perBlock=%d, .toHeight=%d}, height %d - totalInterest %d\n", __func__, rate.height, CeilAmount(rate.interestPerBlock, height), CeilAmount(rate.interestToHeight, height), height, CeilAmount(interest, height));
    return interest;
}

CAmount CeilAmount(const base_uint<128>& amount, uint32_t height)
{
    if (height >= Params().GetConsensus().FortCanningHillHeight)
    {
        CAmount div = (amount / base_uint<128>(COIN)).GetLow64();
        if (div * COIN != amount) div += 1;

        return div;
    }

    return amount.GetLow64();
}

CAmount TotalInterest(const CInterestRateV2& rate, uint32_t height)
{
    auto interest = TotalInterestHighPrecision(rate, height);

    return CeilAmount(interest, height);
}

void CLoanView::WriteInterestRate(const std::pair<CVaultId, DCT_ID>& pair, const CInterestRateV2& rate, uint32_t height)
{
    if (height >= Params().GetConsensus().FortCanningHillHeight)
    {
        WriteBy<LoanInterestByVault>(pair, rate);
    }
    else
    {
        WriteBy<LoanInterestByVault>(pair, ConvertInterestRateToV1(rate));
    }
}

Res CLoanView::StoreInterest(uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, DCT_ID id, CAmount loanIncreased)
{
    auto scheme = GetLoanScheme(loanSchemeID);
    if (!scheme) {
        return Res::Err("No such scheme id %s", loanSchemeID);
    }
    auto token = GetLoanTokenByID(id);
    if (!token) {
        return Res::Err("No such loan token id %s", id.ToString());
    }

    CInterestRateV2 rate{};
    if (auto readRate = GetInterestRateV2(vaultId, id, height))
        rate = *readRate;

    if (rate.height > height || height == 0) {
        return Res::Err("Cannot store height in the past");
    }
    if (rate.height) {
        LogPrint(BCLog::LOAN,"%s():\n", __func__);
        rate.interestToHeight = TotalInterestHighPrecision(rate, height);
    }

    if (int(height) >= Params().GetConsensus().FortCanningMuseumHeight && int(height) < Params().GetConsensus().FortCanningHillHeight) {
        rate.interestPerBlock += CAmount(std::ceil(InterestPerBlockFloat(loanIncreased, token->interest, scheme->rate)));
    } else {
        rate.interestPerBlock += InterestPerBlock(loanIncreased, token->interest, scheme->rate, height);
    }
    rate.height = height;

    WriteInterestRate(std::make_pair(vaultId, id), rate, height);
    return Res::Ok();
}

Res CLoanView::EraseInterest(uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, DCT_ID id, CAmount loanDecreased, CAmount interestDecreased)
{
    auto scheme = GetLoanScheme(loanSchemeID);
    if (!scheme) {
        return Res::Err("No such scheme id %s", loanSchemeID);
    }
    auto token = GetLoanTokenByID(id);
    if (!token) {
        return Res::Err("No such loan token id %s", id.ToString());
    }

    CInterestRateV2 rate{};
    if (auto readRate = GetInterestRateV2(vaultId, id, height))
        rate = *readRate;

    if (rate.height > height) {
        return Res::Err("Cannot store height in the past");
    }
    if (rate.height == 0) {
        return Res::Err("Data mismatch height == 0");
    }
    LogPrint(BCLog::LOAN,"%s():\n", __func__);
    base_uint<128> interestToHeight = TotalInterestHighPrecision(rate, height) - interestDecreased;
    if (interestToHeight > rate.interestToHeight)
        rate.interestToHeight = 0;
    else
        rate.interestToHeight = interestToHeight;

    rate.height = height;
    if (int(height) >= Params().GetConsensus().FortCanningMuseumHeight && int(height) < Params().GetConsensus().FortCanningHillHeight)
    {
        rate.interestPerBlock = std::max(CAmount(0), CAmount(rate.interestPerBlock.GetLow64()) - CAmount(std::ceil(InterestPerBlockFloat(loanDecreased, token->interest, scheme->rate))));
    }
    else
    {
        base_uint<128> interestPerBlock = rate.interestPerBlock - InterestPerBlock(loanDecreased, token->interest, scheme->rate, height);
        if (interestPerBlock > rate.interestPerBlock)
            rate.interestPerBlock = 0;
        else
            rate.interestPerBlock = interestPerBlock;
    }

    WriteInterestRate(std::make_pair(vaultId, id), rate, height);
    return Res::Ok();
}

void CLoanView::ForEachVaultInterest(std::function<bool(const CVaultId&, DCT_ID, CInterestRate)> callback, const CVaultId& vaultId, DCT_ID id)
{
    ForEach<LoanInterestByVault, std::pair<CVaultId, DCT_ID>, CInterestRate>([&](const std::pair<CVaultId, DCT_ID>& pair, CInterestRate rate) {
        return callback(pair.first, pair.second, rate);
    }, std::make_pair(vaultId, id));
}

void CLoanView::ForEachVaultInterestV2(std::function<bool(const CVaultId&, DCT_ID, CInterestRateV2)> callback, const CVaultId& vaultId, DCT_ID id)
{
    ForEach<LoanInterestByVault, std::pair<CVaultId, DCT_ID>, CInterestRateV2>([&](const std::pair<CVaultId, DCT_ID>& pair, CInterestRateV2 rate) {
        return callback(pair.first, pair.second, rate);
    }, std::make_pair(vaultId, id));
}

Res CLoanView::DeleteInterest(const CVaultId& vaultId)
{
    std::vector<std::pair<CVaultId, DCT_ID>> keysToDelete;

    auto it = LowerBound<LoanInterestByVault>(std::make_pair(vaultId, DCT_ID{0}));
    for (; it.Valid() && it.Key().first == vaultId; it.Next()) {
        keysToDelete.push_back(it.Key());
    }

    for (const auto& key : keysToDelete) {
        EraseBy<LoanInterestByVault>(key);
    }
    return Res::Ok();
}

void CLoanView::RevertInterestRateToV1(int height)
{
    std::vector<std::pair<std::pair<CVaultId, DCT_ID>, CInterestRateV2>> rates;
    ForEach<LoanInterestByVault, std::pair<CVaultId, DCT_ID>, CInterestRateV2>([&](const std::pair<CVaultId, DCT_ID>& pair, CInterestRateV2 rate) {
        rate.interestPerBlock /= COIN;
        rate.interestToHeight /= COIN;

        rates.emplace_back(pair, std::move(rate));
        return true;
    });

    for (auto it = rates.begin(); it != rates.end(); it = rates.erase(it)) {
        WriteBy<LoanInterestByVault>(it->first, ConvertInterestRateToV1(it->second));
    }
}

void CLoanView::MigrateInterestRateToV2(int height)
{
    std::vector<std::pair<std::pair<CVaultId, DCT_ID>, CInterestRate>> rates;
    ForEach<LoanInterestByVault, std::pair<CVaultId, DCT_ID>, CInterestRate>([&](const std::pair<CVaultId, DCT_ID>& pair, CInterestRate rate) {
        rates.emplace_back(pair, std::move(rate));
        return true;
    });

    for (auto it = rates.begin(); it != rates.end(); it = rates.erase(it)) {
        auto newRate = ConvertInterestRateToV2(it->second);
        newRate.interestPerBlock *= COIN;
        newRate.interestToHeight *= COIN;
        WriteBy<LoanInterestByVault>(it->first, newRate);
    }
}

Res CLoanView::AddLoanToken(const CVaultId& vaultId, CTokenAmount amount)
{
    if (!GetLoanTokenByID(amount.nTokenId)) {
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
    if (!GetLoanTokenByID(amount.nTokenId)) {
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
