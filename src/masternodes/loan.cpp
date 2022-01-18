
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

boost::optional<CInterestRateV2> CLoanView::GetInterestRate(const CVaultId& vaultId, DCT_ID id, uint32_t height)
{
    if (height >= Params().GetConsensus().FortCanningHillHeight) {
        return ReadBy<LoanInterestV2ByVault, CInterestRateV2>(std::make_pair(vaultId, id));
    }

    if (auto rate = ReadBy<LoanInterestByVault, CInterestRate>(std::make_pair(vaultId, id)))
        return ConvertInterestRateToV2(*rate);

    return {};
}

// precision COIN
template<typename T>
inline T InterestPerBlockCalculationV1(CAmount amount, CAmount tokenInterest, CAmount schemeInterest)
{
    auto netInterest = (tokenInterest + schemeInterest) / 100; // in %
    static const auto blocksPerYear = T(365) * Params().GetConsensus().blocksPerDay();
    return MultiplyAmounts(netInterest, amount) / blocksPerYear;
}

// precisoin COIN ^3
inline base_uint<128> InterestPerBlockCalculationV2(CAmount amount, CAmount tokenInterest, CAmount schemeInterest)
{
    arith_uint256 netInterest = (tokenInterest + schemeInterest) / 100; // in %
    static const arith_uint256 blocksPerYear = 365 * Params().GetConsensus().blocksPerDay();
    arith_uint256 result = netInterest * arith_uint256(amount) * arith_uint256(COIN) / blocksPerYear;

    return (Arith256ToBaseUInt128(result));
}

static base_uint<128> InterestPerBlockCalculation(CAmount amount, CAmount tokenInterest, CAmount schemeInterest, uint32_t height)
{
    if (int(height) >= Params().GetConsensus().FortCanningHillHeight) {
        return InterestPerBlockCalculationV2(amount, tokenInterest, schemeInterest);
    }
    if (int(height) >= Params().GetConsensus().FortCanningMuseumHeight) {
        return std::ceil(InterestPerBlockCalculationV1<float>(amount, tokenInterest, schemeInterest));
    }
    return InterestPerBlockCalculationV1<CAmount>(amount, tokenInterest, schemeInterest);
}

CAmount CeilInterest(const base_uint<128>& value, uint32_t height)
{
    if (int(height) >= Params().GetConsensus().FortCanningHillHeight) {
        CAmount amount = (value / base_uint<128>(HIGH_PRECISION_SCALER)).GetLow64();
        amount += CAmount(value != base_uint<128>(amount) * HIGH_PRECISION_SCALER);

        return amount;
    }
    return value.GetLow64();
}

base_uint<128> TotalInterestCalculation(const CInterestRateV2& rate, uint32_t height)
{
    auto interest = rate.interestToHeight + ((height - rate.height) * rate.interestPerBlock);

    LogPrint(BCLog::LOAN, "%s(): CInterestRate{.height=%d, .perBlock=%d, .toHeight=%d}, height %d - totalInterest %d\n", __func__, rate.height, InterestPerBlock(rate, height), CeilInterest(rate.interestToHeight, height), height, CeilInterest(interest, height));
    return interest;
}

static base_uint<128> ToHigherPrecision(CAmount amount, uint32_t height)
{
    base_uint<128> amountHP = amount;
    if (int(height) >= Params().GetConsensus().FortCanningHillHeight) {
        amountHP *= HIGH_PRECISION_SCALER;
    }
    return amountHP;
}

CAmount TotalInterest(const CInterestRateV2& rate, uint32_t height)
{
    return CeilInterest(TotalInterestCalculation(rate, height), height);
}

CAmount InterestPerBlock(const CInterestRateV2& rate, uint32_t height)
{
    return CeilInterest(rate.interestPerBlock, height);
}

void CLoanView::WriteInterestRate(const std::pair<CVaultId, DCT_ID>& pair, const CInterestRateV2& rate, uint32_t height)
{
    if (height >= Params().GetConsensus().FortCanningHillHeight) {
        WriteBy<LoanInterestV2ByVault>(pair, rate);
    } else {
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
    if (auto readRate = GetInterestRate(vaultId, id, height))
        rate = *readRate;

    if (rate.height > height || height == 0) {
        return Res::Err("Cannot store height in the past");
    }
    if (rate.height) {
        LogPrint(BCLog::LOAN,"%s():\n", __func__);
        rate.interestToHeight = TotalInterestCalculation(rate, height);
    }
    if (int(height) >= Params().GetConsensus().FortCanningHillHeight)
    {
        CBalances amounts;
        if (!ReadBy<CLoanView::LoanTokenAmount>(vaultId, amounts))
            return Res::Err("Cannot find current loan amount balance");

        rate.interestPerBlock = InterestPerBlockCalculation(amounts.balances[id], token->interest, scheme->rate, height);
    }
    else
        rate.interestPerBlock += InterestPerBlockCalculation(loanIncreased, token->interest, scheme->rate, height);
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
    if (auto readRate = GetInterestRate(vaultId, id, height))
        rate = *readRate;

    if (rate.height > height) {
        return Res::Err("Cannot store height in the past");
    }
    if (rate.height == 0) {
        return Res::Err("Data mismatch height == 0");
    }
    auto interestDecreasedHP = ToHigherPrecision(interestDecreased, height);
    LogPrint(BCLog::LOAN,"%s():\n", __func__);
    auto interestToHeight = TotalInterestCalculation(rate, height);
    rate.interestToHeight = interestToHeight < interestDecreasedHP ? 0
                          : interestToHeight - interestDecreasedHP;

    rate.height = height;
    if (int(height) >= Params().GetConsensus().FortCanningHillHeight)
    {
        CBalances amounts;
        if (!ReadBy<CLoanView::LoanTokenAmount>(vaultId, amounts))
            return Res::Err("Cannot find current loan amount balance");

        rate.interestPerBlock = InterestPerBlockCalculation(amounts.balances[id], token->interest, scheme->rate, height);
    }
    else
    {
        auto interestPerBlock = InterestPerBlockCalculation(loanDecreased, token->interest, scheme->rate, height);
        rate.interestPerBlock = rate.interestPerBlock < interestPerBlock ? 0
                          : rate.interestPerBlock - interestPerBlock;
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
    ForEach<LoanInterestV2ByVault, std::pair<CVaultId, DCT_ID>, CInterestRateV2>([&](const std::pair<CVaultId, DCT_ID>& pair, CInterestRateV2 rate) {
        return callback(pair.first, pair.second, rate);
    }, std::make_pair(vaultId, id));
}

Res CLoanView::DeleteInterest(const CVaultId& vaultId, uint32_t height)
{
    std::vector<std::pair<CVaultId, DCT_ID>> keysToDelete;

    if (height >= Params().GetConsensus().FortCanningHillHeight)
    {
        auto it = LowerBound<LoanInterestV2ByVault>(std::make_pair(vaultId, DCT_ID{0}));
        for (; it.Valid() && it.Key().first == vaultId; it.Next()) {
            keysToDelete.push_back(it.Key());
        }

        for (const auto& key : keysToDelete) {
            EraseBy<LoanInterestV2ByVault>(key);
        }
    }
    else
    {
        auto it = LowerBound<LoanInterestByVault>(std::make_pair(vaultId, DCT_ID{0}));
        for (; it.Valid() && it.Key().first == vaultId; it.Next()) {
            keysToDelete.push_back(it.Key());
        }

        for (const auto& key : keysToDelete) {
            EraseBy<LoanInterestByVault>(key);
        }
    }
    return Res::Ok();
}

void CLoanView::RevertInterestRateToV1()
{
    std::vector<std::pair<std::pair<CVaultId, DCT_ID>, CInterestRateV2>> rates;
    ForEach<LoanInterestByVault, std::pair<CVaultId, DCT_ID>, CInterestRateV2>([&](const std::pair<CVaultId, DCT_ID>& pair, CInterestRateV2 rate) {
        rate.interestPerBlock /= HIGH_PRECISION_SCALER;
        rate.interestToHeight /= HIGH_PRECISION_SCALER;
        rates.emplace_back(pair, std::move(rate));
        EraseBy<LoanInterestV2ByVault>(pair);

        return true;
    });

    for (auto it = rates.begin(); it != rates.end(); it = rates.erase(it)) {
        WriteBy<LoanInterestByVault>(it->first, ConvertInterestRateToV1(it->second));
    }
}

void CLoanView::MigrateInterestRateToV2(CVaultView &view, uint32_t height)
{
    std::vector<std::pair<std::pair<CVaultId, DCT_ID>, CInterestRate>> rates;
    ForEach<LoanInterestByVault, std::pair<CVaultId, DCT_ID>, CInterestRate>([&](const std::pair<CVaultId, DCT_ID>& id, CInterestRate rate) {
        rates.emplace_back(id, std::move(rate));
        return true;
    });

    for (auto it = rates.begin(); it != rates.end(); it = rates.erase(it)) {
        auto vaultId = it->first.first;
        auto tokenId = it->first.second;
        auto newRate = ConvertInterestRateToV2(it->second);

        newRate.interestPerBlock *= HIGH_PRECISION_SCALER;
        newRate.interestToHeight *= HIGH_PRECISION_SCALER;
        WriteBy<LoanInterestV2ByVault>(it->first, newRate);

        const auto vault = view.GetVault(vaultId);
        auto loanToken = this->GetLoanTokenByID(tokenId);
        StoreInterest(height, vaultId, vault->schemeId, tokenId, 0);
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
