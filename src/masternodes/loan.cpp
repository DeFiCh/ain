
#include <chainparams.h>
#include <masternodes/loan.h>
#include <boost/multiprecision/cpp_int.hpp>

#include <cmath>

std::unique_ptr<CLoanView::CLoanSetCollateralTokenImpl> CLoanView::GetLoanCollateralToken(uint256 const & txid) const
{
    auto collToken = ReadBy<LoanSetCollateralTokenCreationTx,CLoanSetCollateralTokenImpl>(txid);
    if (collToken)
        return std::make_unique<CLoanSetCollateralTokenImpl>(*collToken);
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
        return std::make_unique<CLoanSetLoanTokenImpl>(*loanToken);
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

std::optional<std::string> CLoanView::GetDefaultLoanScheme()
{
    std::string loanSchemeID;
    if (Read(DefaultLoanSchemeKey::prefix(), loanSchemeID)) {
        return loanSchemeID;
    }

    return {};
}

std::optional<CLoanSchemeData> CLoanView::GetLoanScheme(const std::string& loanSchemeID)
{
    return ReadBy<LoanSchemeKey, CLoanSchemeData>(loanSchemeID);
}

std::optional<uint64_t> CLoanView::GetDestroyLoanScheme(const std::string& loanSchemeID)
{
    return ReadBy<DestroyLoanSchemeKey, uint64_t>(loanSchemeID);
}

Res CLoanView::EraseLoanScheme(const std::string& loanSchemeID)
{
    // Find and delete all related loan scheme updates
    std::vector<uint64_t> loanUpdateHeights;
    ForEachDelayedLoanScheme([&](const std::pair<std::string, uint64_t>& key, const CLoanSchemeMessage&)
    {
        if (key.first == loanSchemeID)
            loanUpdateHeights.push_back(key.second);
        return true;
    });

    for (const auto& height : loanUpdateHeights)
        EraseDelayedLoanScheme(loanSchemeID, height);

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

std::optional<CInterestRateV2> CLoanView::GetInterestRate(const CVaultId& vaultId, DCT_ID id, uint32_t height)
{
    if (height >= Params().GetConsensus().FortCanningHillHeight)
        return ReadBy<LoanInterestV2ByVault, CInterestRateV2>(std::make_pair(vaultId, id));

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
    auto netInterest = (tokenInterest + schemeInterest) / 100; // in %
    static const auto blocksPerYear = 365 * Params().GetConsensus().blocksPerDay();
    return arith_uint256(amount) * netInterest * COIN / blocksPerYear;
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
    if (int(height) >= Params().GetConsensus().FortCanningHillHeight)
        amountHP *= HIGH_PRECISION_SCALER;

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
    if (height >= Params().GetConsensus().FortCanningHillHeight)
        WriteBy<LoanInterestV2ByVault>(pair, rate);
    else
        WriteBy<LoanInterestByVault>(pair, ConvertInterestRateToV1(rate));
}

Res CLoanView::StoreInterest(uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, DCT_ID id, CAmount loanIncreased)
{
    auto scheme = GetLoanScheme(loanSchemeID);
    if (!scheme)
        return Res::Err("No such scheme id %s", loanSchemeID);

    auto token = GetLoanTokenByID(id);
    if (!token)
        return Res::Err("No such loan token id %s", id.ToString());

    CInterestRateV2 rate{};
    if (auto readRate = GetInterestRate(vaultId, id, height))
        rate = *readRate;

    if (rate.height > height || height == 0)
        return Res::Err("Cannot store height in the past");

    if (rate.height) {
        LogPrint(BCLog::LOAN,"%s():\n", __func__);
        rate.interestToHeight = TotalInterestCalculation(rate, height);
    }

    if (int(height) >= Params().GetConsensus().FortCanningHillHeight) {
        CBalances amounts;
        ReadBy<LoanTokenAmount>(vaultId, amounts);
        rate.interestPerBlock = InterestPerBlockCalculationV2(amounts.balances[id], token->interest, scheme->rate);

    } else if (int(height) >= Params().GetConsensus().FortCanningMuseumHeight) {
        CAmount interestPerBlock = rate.interestPerBlock.GetLow64();
        interestPerBlock += std::ceil(InterestPerBlockCalculationV1<float>(loanIncreased, token->interest, scheme->rate));
        rate.interestPerBlock = interestPerBlock;

    } else
        rate.interestPerBlock += InterestPerBlockCalculationV1<CAmount>(loanIncreased, token->interest, scheme->rate);

    rate.height = height;

    WriteInterestRate(std::make_pair(vaultId, id), rate, height);
    return Res::Ok();
}

Res CLoanView::EraseInterest(uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, DCT_ID id, CAmount loanDecreased, CAmount interestDecreased)
{
    auto scheme = GetLoanScheme(loanSchemeID);
    if (!scheme)
        return Res::Err("No such scheme id %s", loanSchemeID);

    auto token = GetLoanTokenByID(id);
    if (!token)
        return Res::Err("No such loan token id %s", id.ToString());

    CInterestRateV2 rate{};
    if (auto readRate = GetInterestRate(vaultId, id, height))
        rate = *readRate;

    if (rate.height > height)
        return Res::Err("Cannot store height in the past");

    if (rate.height == 0)
        return Res::Err("Data mismatch height == 0");

    auto interestDecreasedHP = ToHigherPrecision(interestDecreased, height);

    LogPrint(BCLog::LOAN,"%s():\n", __func__);
    auto interestToHeight = TotalInterestCalculation(rate, height);
    rate.interestToHeight = interestToHeight < interestDecreasedHP ? 0
                          : interestToHeight - interestDecreasedHP;

    rate.height = height;

    if (int(height) >= Params().GetConsensus().FortCanningHillHeight) {
        CBalances amounts;
        ReadBy<LoanTokenAmount>(vaultId, amounts);
        rate.interestPerBlock = InterestPerBlockCalculationV2(amounts.balances[id], token->interest, scheme->rate);

    } else if (int(height) >= Params().GetConsensus().FortCanningMuseumHeight) {
        CAmount interestPerBlock = rate.interestPerBlock.GetLow64();
        CAmount newInterestPerBlock = std::ceil(InterestPerBlockCalculationV1<float>(loanDecreased, token->interest, scheme->rate));
        rate.interestPerBlock = std::max(CAmount{0}, interestPerBlock - newInterestPerBlock);

    } else {
        auto interestPerBlock = InterestPerBlockCalculationV1<CAmount>(loanDecreased, token->interest, scheme->rate);
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

template<typename BoundType>
void DeleteInterest(CLoanView& view, const CVaultId& vaultId)
{
    std::vector<std::pair<CVaultId, DCT_ID>> keysToDelete;

    auto it = view.LowerBound<BoundType>(std::make_pair(vaultId, DCT_ID{0}));
    for (; it.Valid() && it.Key().first == vaultId; it.Next())
        keysToDelete.push_back(it.Key());

    for (const auto& key : keysToDelete)
        view.EraseBy<BoundType>(key);
}

Res CLoanView::DeleteInterest(const CVaultId& vaultId, uint32_t height)
{
    if (height >= Params().GetConsensus().FortCanningHillHeight)
        ::DeleteInterest<LoanInterestV2ByVault>(*this, vaultId);
    else
        ::DeleteInterest<LoanInterestByVault>(*this, vaultId);

    return Res::Ok();
}

void CLoanView::RevertInterestRateToV1()
{
    std::vector<std::pair<CVaultId, DCT_ID>> pairs;

    ForEach<LoanInterestV2ByVault, std::pair<CVaultId, DCT_ID>, CInterestRateV2>([&](const std::pair<CVaultId, DCT_ID>& pair, CLazySerialize<CInterestRateV2>) {
        pairs.emplace_back(pair);
        return true;
    });

    for (auto it = pairs.begin(); it != pairs.end(); it = pairs.erase(it)) {
        EraseBy<LoanInterestV2ByVault>(*it);
    }
}

void CLoanView::MigrateInterestRateToV2(CVaultView &view, uint32_t height)
{
    ForEachVaultInterest([&](const CVaultId& vaultId, DCT_ID tokenId, CInterestRate rate) {
        auto newRate = ConvertInterestRateToV2(rate);
        newRate.interestPerBlock *= HIGH_PRECISION_SCALER;
        newRate.interestToHeight *= HIGH_PRECISION_SCALER;
        WriteBy<LoanInterestV2ByVault>(std::make_pair(vaultId, tokenId), newRate);

        const auto vault = view.GetVault(vaultId);
        StoreInterest(height, vaultId, vault->schemeId, tokenId, 0);
        return true;
    });
}

Res CLoanView::AddLoanToken(const CVaultId& vaultId, CTokenAmount amount)
{
    if (!GetLoanTokenByID(amount.nTokenId))
        return Res::Err("No such loan token id %s", amount.nTokenId.ToString());

    CBalances amounts;
    ReadBy<LoanTokenAmount>(vaultId, amounts);
    auto res = amounts.Add(amount);
    if (!res)
        return res;

    if (!amounts.balances.empty())
        WriteBy<LoanTokenAmount>(vaultId, amounts);

    return Res::Ok();
}

Res CLoanView::SubLoanToken(const CVaultId& vaultId, CTokenAmount amount)
{
    if (!GetLoanTokenByID(amount.nTokenId))
        return Res::Err("No such loan token id %s", amount.nTokenId.ToString());

    auto amounts = GetLoanTokens(vaultId);
    if (!amounts || !amounts->Sub(amount))
        return Res::Err("Loan token for vault <%s> not found", vaultId.GetHex());

    if (amounts->balances.empty())
        EraseBy<LoanTokenAmount>(vaultId);
    else
        WriteBy<LoanTokenAmount>(vaultId, *amounts);

    return Res::Ok();
}

std::optional<CBalances> CLoanView::GetLoanTokens(const CVaultId& vaultId)
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
    if (Read(LoanLiquidationPenalty::prefix(), penalty))
        return penalty;

    return 5 * COIN / 100;
}

std::string GetInterestPerBlockHighPrecisionString(base_uint<128> value) {
    struct HighPrecisionInterestValue {
        typedef boost::multiprecision::int128_t int128;
        typedef int64_t int64;

        int128 value;

        HighPrecisionInterestValue(base_uint<128> val) {
            value = int128("0x" + val.GetHex());
        }

        int64 GetInterestPerBlockSat() {
            return int64(value / HIGH_PRECISION_SCALER);
        }

        int64 GetInterestPerBlockSubSat() {
            return int64(value % HIGH_PRECISION_SCALER);
        }

        int64 GetInterestPerBlockMagnitude() {
            return int64(value / HIGH_PRECISION_SCALER / COIN);
        }

        int128 GetInterestPerBlockDecimal() {
            auto v = GetInterestPerBlockSat();
            return v == 0 ? value : value % (int128(HIGH_PRECISION_SCALER) * COIN);
        }

        std::string GetInterestPerBlockString() {
            std::ostringstream result;
            result << GetInterestPerBlockMagnitude() << ".";
            result << std::setw(24) << std::setfill('0') << GetInterestPerBlockDecimal();
            return result.str();
        }
    };
    return HighPrecisionInterestValue(value).GetInterestPerBlockString();
}
