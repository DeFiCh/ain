// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/oracles.h>

#include <masternodes/masternodes.h>

#include <algorithm>

bool COracle::SupportsPair(const std::string& token, const std::string& currency) const
{
    return availablePairs.count(std::make_pair(token, currency)) > 0;
}

Res COracle::SetTokenPrice(const std::string& token, const std::string& currency, CAmount amount, int64_t timestamp)
{
    if (!SupportsPair(token, currency)) {
        return Res::Err("token <%s> - currency <%s> is not allowed", token, currency);
    }

    tokenPrices[token][currency] = std::make_pair(amount, timestamp);

    return Res::Ok();
}

ResVal<CAmount> COracle::GetTokenPrice(const std::string& token, const std::string& currency)
{
    if (!SupportsPair(token, currency)) {
        return Res::Err("token <%s> - currency <%s> is not allowed", token, currency);
    }

    return ResVal<CAmount>(tokenPrices[token][currency].first, Res::Ok());
}

Res COracleView::AppointOracle(const COracleId& oracleId, const COracle& oracle)
{
    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to appoint the new oracle <%s>", oracleId.GetHex());
    }

    return Res::Ok();
}

Res COracleView::UpdateOracle(const COracleId& oracleId, COracle&& newOracle)
{
    COracle oracle;
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    if (!newOracle.tokenPrices.empty()) {
        return Res::Err("oracle <%s> has token prices on update", oracleId.GetHex());
    }

    oracle.weightage = newOracle.weightage;
    oracle.oracleAddress = std::move(newOracle.oracleAddress);

    CTokenPricePoints allowedPrices;
    for (const auto& tokenPrice : oracle.tokenPrices) {
        const auto& token = tokenPrice.first;
        for (const auto& price : tokenPrice.second) {
            const auto& currency = price.first;
            if (!newOracle.SupportsPair(token, currency)) {
                continue;
            }
            allowedPrices[token][currency] = price.second;
        }
    }

    oracle.tokenPrices = std::move(allowedPrices);
    oracle.availablePairs = std::move(newOracle.availablePairs);

    // no need to update oracles list
    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to save oracle <%s>", oracleId.GetHex());
    }

    return Res::Ok();
}

Res COracleView::RemoveOracle(const COracleId& oracleId)
{
    if (!ExistsBy<ByName>(oracleId)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    // remove oracle
    if (!EraseBy<ByName>(oracleId)) {
        return Res::Err("failed to remove oracle <%s>", oracleId.GetHex());
    }

    return Res::Ok();
}

Res COracleView::SetOracleData(const COracleId& oracleId, int64_t timestamp, const CTokenPrices& tokenPrices)
{
    COracle oracle;
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to read oracle %s from database", oracleId.GetHex());
    }

    for (const auto& tokenPrice : tokenPrices) {
        const auto& token = tokenPrice.first;
        for (const auto& price : tokenPrice.second) {
            const auto& currency = price.first;
            auto res = oracle.SetTokenPrice(token, currency, price.second, timestamp);
            if (!res.ok) {
                return res;
            }
        }
    }

    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to store oracle %s to database", oracleId.GetHex());
    }

    return Res::Ok();
}

ResVal<COracle> COracleView::GetOracleData(const COracleId& oracleId) const
{
    COracle oracle;
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    return ResVal<COracle>(oracle, Res::Ok());
}

void COracleView::ForEachOracle(std::function<bool(const COracleId&, CLazySerialize<COracle>)> callback, const COracleId& start)
{
    ForEach<ByName, COracleId, COracle>(callback, start);
}

bool CFixedIntervalPrice::isLive(const CAmount deviationThreshold) const
{
    return (
        priceRecord[0] > 0 &&
        priceRecord[1] > 0 &&
        (std::abs(priceRecord[1] - priceRecord[0]) < MultiplyAmounts(priceRecord[0], deviationThreshold))
    );
}

Res COracleView::SetFixedIntervalPrice(const CFixedIntervalPrice& fixedIntervalPrice){

    if (!WriteBy<FixedIntervalPriceKey>(fixedIntervalPrice.priceFeedId, fixedIntervalPrice)) {
        return Res::Err("failed to set new price feed <%s/%s>", fixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.second);
    }
    LogPrint(BCLog::ORACLE, "%s(): %s/%s, active - %lld, next - %lld\n", __func__, fixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.second, fixedIntervalPrice.priceRecord[0], fixedIntervalPrice.priceRecord[1]);

    return Res::Ok();
}

ResVal<CFixedIntervalPrice> COracleView::GetFixedIntervalPrice(const CTokenCurrencyPair& fixedIntervalPriceId, bool skipLockedCheck)
{
    CFixedIntervalPrice fixedIntervalPrice;
    if (!ReadBy<FixedIntervalPriceKey>(fixedIntervalPriceId, fixedIntervalPrice)) {
        return Res::Err("fixedIntervalPrice with id <%s/%s> not found", fixedIntervalPriceId.first, fixedIntervalPriceId.second);
    }

    DCT_ID firstID{}, secondID{};
    const auto firstToken = GetTokenGuessId(fixedIntervalPriceId.first, firstID);
    const auto secondToken = GetTokenGuessId(fixedIntervalPriceId.second, secondID);

    std::set<uint32_t> loanTokens;
    if (firstToken && GetLoanTokenByID(firstID)) {
        loanTokens.insert(firstID.v);
    }

    if (secondToken && GetLoanTokenByID(secondID)) {
        loanTokens.insert(secondID.v);
    }

    if (AreTokensLocked(loanTokens) && !skipLockedCheck) {
        return Res::Err("Fixed interval price currently disabled due to locked token");
    }

    LogPrint(BCLog::ORACLE, "%s(): %s/%s, active - %lld, next - %lld\n", __func__, fixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.second, fixedIntervalPrice.priceRecord[0], fixedIntervalPrice.priceRecord[1]);
    return ResVal<CFixedIntervalPrice>(fixedIntervalPrice, Res::Ok());
}

void COracleView::ForEachFixedIntervalPrice(std::function<bool(const CTokenCurrencyPair&, CLazySerialize<CFixedIntervalPrice>)> callback, const CTokenCurrencyPair& start)
{
    ForEach<FixedIntervalPriceKey, CTokenCurrencyPair, CFixedIntervalPrice>(callback, start);
}

Res COracleView::SetPriceDeviation(const uint32_t deviation)
{
    Write(PriceDeviation::prefix(), deviation);
    return Res::Ok();
}

Res COracleView::ErasePriceDeviation()
{
    Erase(PriceDeviation::prefix());
    return Res::Ok();
}

CAmount COracleView::GetPriceDeviation() const
{
    uint32_t deviation;
    if (Read(PriceDeviation::prefix(), deviation)) {
        return deviation;
    }

    // Default
    return 3 * COIN / 10;
}

Res COracleView::SetIntervalBlock(const uint32_t blockInterval)
{
    Write(FixedIntervalBlockKey::prefix(), blockInterval);
    return Res::Ok();
}

Res COracleView::EraseIntervalBlock()
{
    Erase(FixedIntervalBlockKey::prefix());
    return Res::Ok();
}

uint32_t COracleView::GetIntervalBlock() const
{
    uint32_t blockInterval;
    if (Read(FixedIntervalBlockKey::prefix(), blockInterval)) {
        return blockInterval;
    }

    // Default
    return 60 * 60 / Params().GetConsensus().pos.nTargetSpacing;
}
