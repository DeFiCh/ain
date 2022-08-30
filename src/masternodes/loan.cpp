
#include <chainparams.h>
#include <masternodes/loan.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/masternodes.h>

#include <boost/multiprecision/cpp_int.hpp>

std::optional<CLoanView::CLoanSetCollateralTokenImpl> CLoanView::GetLoanCollateralToken(uint256 const & txid) const
{
    return ReadBy<LoanSetCollateralTokenCreationTx,CLoanSetCollateralTokenImpl>(txid);
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

Res CLoanView::EraseLoanCollateralToken(const CLoanSetCollateralTokenImpl& collToken)
{
    CollateralTokenKey key{collToken.idToken, collToken.activateAfterBlock};
    EraseBy<LoanSetCollateralTokenKey>(key);
    EraseBy<LoanSetCollateralTokenCreationTx>(collToken.creationTx);

    return Res::Ok();
}

void CLoanView::ForEachLoanCollateralToken(std::function<bool (CollateralTokenKey const &, uint256 const &)> callback, CollateralTokenKey const & start)
{
    ForEach<LoanSetCollateralTokenKey, CollateralTokenKey, uint256>(callback, start);
}

std::optional<CLoanView::CLoanSetCollateralTokenImpl> CLoanView::HasLoanCollateralToken(CollateralTokenKey const & key)
{
    auto it = LowerBound<LoanSetCollateralTokenKey>(key);
    if (it.Valid() && it.Key().id == key.id)
        return GetLoanCollateralToken(it.Value());

    return GetCollateralTokenFromAttributes(key.id);
}

std::optional<CLoanView::CLoanSetLoanTokenImpl> CLoanView::GetLoanToken(uint256 const & txid) const
{
    auto id = ReadBy<LoanSetLoanTokenCreationTx, DCT_ID>(txid);
    if (id)
        return GetLoanTokenByID(*id);
    return {};
}

Res CLoanView::SetLoanToken(CLoanSetLoanTokenImpl const & loanToken, DCT_ID const & id)
{
    //this should not happen, but for sure
    if (GetLoanTokenByID(id))
        return Res::Err("setLoanToken with creation tx %s already exists!", loanToken.creationTx.GetHex());

    WriteBy<LoanSetLoanTokenKey>(id, loanToken);
    WriteBy<LoanSetLoanTokenCreationTx>(loanToken.creationTx, id);

    return Res::Ok();
}

Res CLoanView::UpdateLoanToken(CLoanSetLoanTokenImpl const & loanToken, DCT_ID const & id)
{
    WriteBy<LoanSetLoanTokenKey>(id, loanToken);

    return Res::Ok();
}

Res CLoanView::EraseLoanToken(const DCT_ID& id)
{
    EraseBy<LoanSetLoanTokenKey>(id);

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
    if (const auto res = ReadBy<DestroyLoanSchemeKey, uint64_t>(loanSchemeID))
        return res;
    return {};
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

std::optional<CInterestRateV3> CLoanView::GetInterestRate(const CVaultId& vaultId, const DCT_ID id, const uint32_t height)
{
    if (height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight)) {
        return ReadBy<LoanInterestV3ByVault, CInterestRateV3>(std::make_pair(vaultId, id));
    }

    if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight)) {
        if (const auto rate = ReadBy<LoanInterestV2ByVault, CInterestRateV2>(std::make_pair(vaultId, id))) {
            return ConvertInterestRateToV3(*rate);
        }
    }

    if (auto rate = ReadBy<LoanInterestByVault, CInterestRate>(std::make_pair(vaultId, id))) {
        return ConvertInterestRateToV3(*rate);
    }

    return {};
}

// Precision 64bit
template<typename T>
inline T InterestPerBlockCalculationV1(CAmount amount, CAmount tokenInterest, CAmount schemeInterest)
{
    const auto netInterest = (tokenInterest + schemeInterest) / 100; // in %
    static const auto blocksPerYear = T(365) * Params().GetConsensus().blocksPerDay();
    return MultiplyAmounts(netInterest, amount) / blocksPerYear;
}

// Precision 128bit
inline base_uint<128> InterestPerBlockCalculationV2(CAmount amount, CAmount tokenInterest, CAmount schemeInterest)
{
    const auto netInterest = (tokenInterest + schemeInterest) / 100; // in %
    static const auto blocksPerYear = 365 * Params().GetConsensus().blocksPerDay();
    return arith_uint256(amount) * netInterest * COIN / blocksPerYear;
}

// Precision 128bit with negative interest
CInterestAmount InterestPerBlockCalculationV3(CAmount amount, CAmount tokenInterest, CAmount schemeInterest)
{
    const auto netInterest = (tokenInterest + schemeInterest) / 100; // in %
    static const auto blocksPerYear = 365 * Params().GetConsensus().blocksPerDay();
    return {netInterest < 0 && amount > 0, arith_uint256(amount) * std::abs(netInterest) * COIN / blocksPerYear};
}

CAmount CeilInterest(const base_uint<128>& value, uint32_t height)
{
    if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight)) {
        CAmount amount = (value / base_uint<128>(HIGH_PRECISION_SCALER)).GetLow64();
        amount += CAmount(value != base_uint<128>(amount) * HIGH_PRECISION_SCALER);
        return amount;
    }
    return value.GetLow64();
}

CAmount FloorInterest(const base_uint<128>& value)
{
    return (value / base_uint<128>(HIGH_PRECISION_SCALER)).GetLow64();
}

static base_uint<128> ToHigherPrecision(CAmount amount, uint32_t height)
{
    base_uint<128> amountHP = amount;
    if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight))
        amountHP *= HIGH_PRECISION_SCALER;

    return amountHP;
}

const auto InterestPerBlock = [](const CInterestRateV3& rate, const uint32_t height) {
    return CeilInterest(rate.interestPerBlock.amount, height);
};

CInterestAmount TotalInterestCalculation(const CInterestRateV3& rate, const uint32_t height)
{
    const auto heightDiff = (height - rate.height);
    const auto interestAmount = rate.interestPerBlock.amount;
    const auto totalInterest =  interestAmount * heightDiff;

    if (heightDiff != 0 && totalInterest / heightDiff != interestAmount) {
        LogPrintf("WARNING: Overflow detected. This will soon be saturated. (height=%d, height-diff=%d"
                  "amount=%s, interest=%s)\n",
                  height, heightDiff, GetInterestPerBlockHighPrecisionString(rate.interestPerBlock),
                  GetInterestPerBlockHighPrecisionString({rate.interestPerBlock.negative, totalInterest})
                  );
    }

    auto interest = InterestAddition(rate.interestToHeight, {rate.interestPerBlock.negative, totalInterest});

    LogPrint(BCLog::LOAN, "%s(): CInterestRate{.height=%d, .perBlock=%d, .toHeight=%d}, height %d - totalInterest %d\n",
        __func__,
        rate.height, rate.interestPerBlock.negative ? -InterestPerBlock(rate, height) : InterestPerBlock(rate, height),
        rate.interestToHeight.negative ? -FloorInterest(rate.interestToHeight.amount) : CeilInterest(rate.interestToHeight.amount, height),
        height,
        interest.negative ? -FloorInterest(interest.amount) : CeilInterest(interest.amount, height));

    return interest;
}

CAmount TotalInterest(const CInterestRateV3& rate, const uint32_t height)
{
    const auto totalInterest = TotalInterestCalculation(rate, height);
    return totalInterest.negative ? -FloorInterest(totalInterest.amount) : CeilInterest(totalInterest.amount, height);
}

void CLoanView::WriteInterestRate(const std::pair<CVaultId, DCT_ID>& pair, const CInterestRateV3& rate, uint32_t height)
{
    if (height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight)) {
        WriteBy<LoanInterestV3ByVault>(pair, rate);
    } else if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight)) {
        WriteBy<LoanInterestV2ByVault>(pair, ConvertInterestRateToV2(rate));
    } else {
        WriteBy<LoanInterestByVault>(pair, ConvertInterestRateToV1(rate));
    }
}

Res CLoanView::IncreaseInterest(const uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, const DCT_ID id, const CAmount tokenInterest, const CAmount loanIncreased)
{
    const auto scheme = GetLoanScheme(loanSchemeID);
    if (!scheme)
        return Res::Err("No such scheme id %s", loanSchemeID);

    const auto token = GetLoanTokenByID(id);
    if (!token)
        return Res::Err("No such loan token id %s", id.ToString());

    CInterestRateV3 rate{};
    if (auto readRate = GetInterestRate(vaultId, id, height))
        rate = *readRate;

    if (rate.height > height || height == 0)
        return Res::Err("Cannot store height in the past");

    rate.interestToHeight = TotalInterestCalculation(rate, height);
    rate.height = height;

    if (height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight)) {
        CBalances amounts;
        ReadBy<LoanTokenAmount>(vaultId, amounts);

        // Use argument token interest as update from Gov var TX will not be applied to GetLoanTokenByID at this point in time.
        rate.interestPerBlock = InterestPerBlockCalculationV3(amounts.balances[id], tokenInterest, scheme->rate);

    } else if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight)) {
        CBalances amounts;
        ReadBy<LoanTokenAmount>(vaultId, amounts);
        rate.interestPerBlock = {false, InterestPerBlockCalculationV2(amounts.balances[id], token->interest, scheme->rate)};
    } else if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningMuseumHeight)) {
        CAmount interestPerBlock = rate.interestPerBlock.amount.GetLow64();
        interestPerBlock += std::ceil(InterestPerBlockCalculationV1<float>(loanIncreased, token->interest, scheme->rate));
        rate.interestPerBlock = {false, interestPerBlock};
    } else {
        rate.interestPerBlock.amount += InterestPerBlockCalculationV1<CAmount>(loanIncreased, token->interest, scheme->rate);
    }

    WriteInterestRate(std::make_pair(vaultId, id), rate, height);
    return Res::Ok();
}

Res CLoanView::DecreaseInterest(const uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, const DCT_ID id, const CAmount loanDecreased, const CAmount interestDecreased)
{
    auto scheme = GetLoanScheme(loanSchemeID);
    if (!scheme)
        return Res::Err("No such scheme id %s", loanSchemeID);

    auto token = GetLoanTokenByID(id);
    if (!token)
        return Res::Err("No such loan token id %s", id.ToString());

    CInterestRateV3 rate{};
    if (auto readRate = GetInterestRate(vaultId, id, height))
        rate = *readRate;

    if (rate.height > height)
        return Res::Err("Cannot store height in the past");

    if (rate.height == 0)
        return Res::Err("Data mismatch height == 0");

    const auto interestToHeight = TotalInterestCalculation(rate, height);
    const auto interestDecreasedHP = ToHigherPrecision(interestDecreased, height);

    rate.interestToHeight = interestToHeight.amount < interestDecreasedHP ?
                                CInterestAmount{false, 0} :
                                CInterestAmount{interestToHeight.negative, interestToHeight.amount - interestDecreasedHP};

    rate.height = height;

    if (height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight)) {
        CBalances amounts;
        ReadBy<LoanTokenAmount>(vaultId, amounts);
        rate.interestPerBlock = InterestPerBlockCalculationV3(amounts.balances[id], token->interest, scheme->rate);
    } else if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight)) {
        CBalances amounts;
        ReadBy<LoanTokenAmount>(vaultId, amounts);
        rate.interestPerBlock = {false, InterestPerBlockCalculationV2(amounts.balances[id], token->interest, scheme->rate)};
    } else if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningMuseumHeight)) {
        CAmount interestPerBlock = rate.interestPerBlock.amount.GetLow64();
        CAmount newInterestPerBlock = std::ceil(InterestPerBlockCalculationV1<float>(loanDecreased, token->interest, scheme->rate));
        rate.interestPerBlock = {false ,std::max(CAmount{0}, interestPerBlock - newInterestPerBlock)};
    } else {
        auto interestPerBlock = InterestPerBlockCalculationV1<CAmount>(loanDecreased, token->interest, scheme->rate);
        rate.interestPerBlock = rate.interestPerBlock.amount < interestPerBlock ? CInterestAmount{false, 0}
                              : CInterestAmount{false, rate.interestPerBlock.amount - interestPerBlock};
    }

    WriteInterestRate(std::make_pair(vaultId, id), rate, height);
    return Res::Ok();
}


void CLoanView::ResetInterest(const uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, const DCT_ID id)
{
    const auto scheme = GetLoanScheme(loanSchemeID);
    assert(scheme);

    const auto token = GetLoanTokenByID(id);
    assert(token);

    CBalances amounts;
    ReadBy<LoanTokenAmount>(vaultId, amounts);

    const CInterestRateV3 rate{
            height,
            InterestPerBlockCalculationV3(amounts.balances[id], token->interest, scheme->rate),
            {false, 0}
    };

    WriteInterestRate(std::make_pair(vaultId, id), rate, height);
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

void CLoanView::ForEachVaultInterestV3(std::function<bool(const CVaultId&, DCT_ID, CInterestRateV3)> callback, const CVaultId& vaultId, DCT_ID id)
{
    ForEach<LoanInterestV3ByVault, std::pair<CVaultId, DCT_ID>, CInterestRateV3>([&](const std::pair<CVaultId, DCT_ID>& pair, CInterestRateV3 rate) {
        return callback(pair.first, pair.second, rate);
    }, std::make_pair(vaultId, id));
}

template<typename BoundType>
void EraseInterest(CLoanView& view, const CVaultId& vaultId)
{
    std::vector<std::pair<CVaultId, DCT_ID>> keysToDelete;

    auto it = view.LowerBound<BoundType>(std::make_pair(vaultId, DCT_ID{0}));
    for (; it.Valid() && it.Key().first == vaultId; it.Next())
        keysToDelete.push_back(it.Key());

    for (const auto& key : keysToDelete)
        view.EraseBy<BoundType>(key);
}

Res CLoanView::EraseInterest(const CVaultId& vaultId, uint32_t height)
{
    if (height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight)) {
        ::EraseInterest<LoanInterestV3ByVault>(*this, vaultId);
    } else if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight)) {
        ::EraseInterest<LoanInterestV2ByVault>(*this, vaultId);
    } else {
        ::EraseInterest<LoanInterestByVault>(*this, vaultId);
    }

    return Res::Ok();
}

void CLoanView::EraseInterest(const CVaultId& vaultId, DCT_ID id, uint32_t height)
{
    if (height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight)) {
        EraseBy<LoanInterestV3ByVault>(std::make_pair(vaultId, id));
    } else if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight)) {
        EraseBy<LoanInterestV2ByVault>(std::make_pair(vaultId, id));
    }
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

void CLoanView::RevertInterestRateToV2()
{
    std::vector<std::pair<CVaultId, DCT_ID>> pairs;

    ForEach<LoanInterestV3ByVault, std::pair<CVaultId, DCT_ID>, CInterestRateV3>([&](const std::pair<CVaultId, DCT_ID>& pair, const CInterestRateV3&) {
        pairs.emplace_back(pair);
        return true;
    });

    for (auto it = pairs.begin(); it != pairs.end(); it = pairs.erase(it)) {
        EraseBy<LoanInterestV3ByVault>(*it);
    }
}

void CLoanView::MigrateInterestRateToV2(CVaultView &view, uint32_t height)
{
    ForEachVaultInterest([&](const CVaultId& vaultId, DCT_ID tokenId, CInterestRate rate) {
        auto newRate = ConvertInterestRateToV2(rate);
        newRate.interestPerBlock *= HIGH_PRECISION_SCALER;
        newRate.interestToHeight *= HIGH_PRECISION_SCALER;
        WriteBy<LoanInterestV2ByVault>(std::make_pair(vaultId, tokenId), newRate);

        const auto token = GetLoanTokenByID(tokenId);
        assert(token);

        const auto vault = view.GetVault(vaultId);
        assert(vault);

        IncreaseInterest(height, vaultId, vault->schemeId, tokenId, token->interest, 0);
        return true;
    });
}

void CLoanView::MigrateInterestRateToV3(CVaultView &view, uint32_t height)
{
    ForEachVaultInterestV2([&](const CVaultId& vaultId, DCT_ID tokenId, const CInterestRateV2 &rate) {
        auto newRate = ConvertInterestRateToV3(rate);
        WriteBy<LoanInterestV3ByVault>(std::make_pair(vaultId, tokenId), newRate);

        const auto token = GetLoanTokenByID(tokenId);
        assert(token);

        const auto vault = view.GetVault(vaultId);
        assert(vault);

        IncreaseInterest(height, vaultId, vault->schemeId, tokenId, token->interest, 0);
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

void CLoanView::ForEachLoanTokenAmount(std::function<bool (const CVaultId&, const CBalances&)> callback)
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

std::optional<std::string> TryGetInterestPerBlockHighPrecisionString(const CInterestAmount& value) {
    struct HighPrecisionInterestValue {
        typedef boost::multiprecision::int128_t int128;
        typedef int64_t int64;

        int128 value;
        bool negative;

        explicit HighPrecisionInterestValue(const CInterestAmount& val) {
            value = int128("0x" + val.amount.GetHex());
            negative = val.negative;
        }

        int64 GetInterestPerBlockSat() const {
            return int64(value / HIGH_PRECISION_SCALER);
        }

        int64 GetInterestPerBlockSubSat() const {
            return int64(value % HIGH_PRECISION_SCALER);
        }

        int64 GetInterestPerBlockMagnitude() const {
            return int64(value / HIGH_PRECISION_SCALER / COIN);
        }

        int128 GetInterestPerBlockDecimal() const {
            auto v = GetInterestPerBlockSat();
            return v == 0 ? value : value % (int128(HIGH_PRECISION_SCALER) * COIN);
        }

        std::optional<std::string> GetInterestPerBlockString() const {
            std::ostringstream result;
            auto mag = GetInterestPerBlockMagnitude();
            auto dec = GetInterestPerBlockDecimal();
            // While these can happen theoretically, they should be out of range of
            // operating interest. If this happens, something else went wrong.
            if (mag < 0 || dec < 0)
                return {};

            result << (negative ? "-" : "") << mag << "." << std::setw(24) << std::setfill('0') << dec;
            return result.str();
        }
    };
    return HighPrecisionInterestValue(value).GetInterestPerBlockString();
}

std::string GetInterestPerBlockHighPrecisionString(const CInterestAmount& value) {
    auto res = TryGetInterestPerBlockHighPrecisionString(value);
    if (!res) {
        LogPrintf("WARNING: High precision interest string conversion failure. Falling back to hex.\n");
        return value.negative ? "negative " + value.amount.ToString() : value.amount.ToString();
    }
    return *res;
}
