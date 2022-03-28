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
    Require(SupportsPair(token, currency), "token <%s> - currency <%s> is not allowed", token, currency);

    tokenPrices[token][currency] = std::make_pair(amount, timestamp);

    return Res::Ok();
}

ResVal<CAmount> COracle::GetTokenPrice(const std::string& token, const std::string& currency)
{
    Require(SupportsPair(token, currency), "token <%s> - currency <%s> is not allowed", token, currency);

    return ResVal<CAmount>(tokenPrices[token][currency].first, Res::Ok());
}

Res COracleView::AppointOracle(const COracleId& oracleId, const COracle& oracle)
{
    Require(WriteBy<ByName>(oracleId, oracle), "failed to appoint the new oracle <%s>", oracleId.GetHex());

    return Res::Ok();
}

Res COracleView::UpdateOracle(const COracleId& oracleId, COracle&& newOracle)
{
    COracle oracle;
    Require(ReadBy<ByName>(oracleId, oracle), "oracle <%s> not found", oracleId.GetHex());

    Require(newOracle.tokenPrices.empty(), "oracle <%s> has token prices on update", oracleId.GetHex());

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
    Require(WriteBy<ByName>(oracleId, oracle), "failed to save oracle <%s>", oracleId.GetHex());

    return Res::Ok();
}

Res COracleView::RemoveOracle(const COracleId& oracleId)
{
    Require(ExistsBy<ByName>(oracleId), "oracle <%s> not found", oracleId.GetHex());

    // remove oracle
    Require(EraseBy<ByName>(oracleId), "failed to remove oracle <%s>", oracleId.GetHex());

    return Res::Ok();
}

Res COracleView::SetOracleData(const COracleId& oracleId, int64_t timestamp, const CTokenPrices& tokenPrices)
{
    COracle oracle;
    Require(ReadBy<ByName>(oracleId, oracle), "failed to read oracle %s from database", oracleId.GetHex());

    for (const auto& tokenPrice : tokenPrices) {
        const auto& token = tokenPrice.first;
        for (const auto& price : tokenPrice.second) {
            const auto& currency = price.first;
            Require(oracle.SetTokenPrice(token, currency, price.second, timestamp));
        }
    }

    Require(WriteBy<ByName>(oracleId, oracle), "failed to store oracle %s to database", oracleId.GetHex());

    return Res::Ok();
}

ResVal<COracle> COracleView::GetOracleData(const COracleId& oracleId) const
{
    COracle oracle;
    Require(ReadBy<ByName>(oracleId, oracle), "oracle <%s> not found", oracleId.GetHex());

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

    Require(WriteBy<FixedIntervalPriceKey>(fixedIntervalPrice.priceFeedId, fixedIntervalPrice),
              "failed to set new price feed <%s/%s>", fixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.second);

    LogPrint(BCLog::ORACLE, "%s(): %s/%s, active - %lld, next - %lld\n", __func__, fixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.second, fixedIntervalPrice.priceRecord[0], fixedIntervalPrice.priceRecord[1]);

    return Res::Ok();
}

ResVal<CFixedIntervalPrice> COracleView::GetFixedIntervalPrice(const CTokenCurrencyPair& fixedIntervalPriceId)
{
    CFixedIntervalPrice fixedIntervalPrice;
    Require(ReadBy<FixedIntervalPriceKey>(fixedIntervalPriceId, fixedIntervalPrice),
              "fixedIntervalPrice with id <%s/%s> not found", fixedIntervalPriceId.first, fixedIntervalPriceId.second);

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

    Require(!AreTokensLocked(loanTokens), "Fixed interval price currently disabled due to locked token");

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

uint32_t COracleView::GetIntervalBlock() const
{
    uint32_t blockInterval;
    if (Read(FixedIntervalBlockKey::prefix(), blockInterval)) {
        return blockInterval;
    }

    // Default
    return 60 * 60 / Params().GetConsensus().pos.nTargetSpacing;
}
