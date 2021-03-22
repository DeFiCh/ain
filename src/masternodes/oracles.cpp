// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/oracles.h>

#include <algorithm>

#include <rpc/protocol.h>

#include <masternodes/tokenpriceiterator.h>

const unsigned char COracleView::ByName::prefix = 'O'; // the big O for Oracles

namespace {
template <typename Code>
Res makeError(Code errorCode, const std::string& msg)
{
    return Res{false, msg, static_cast<uint32_t>(errorCode)};
}

} // namespace

bool COracleId::parseHex(const std::string& str)
{
    auto oracleBytes = ParseHex(str);

    if (size() != oracleBytes.size()) {
        return false;
    }

    std::copy_n(oracleBytes.rbegin(), oracleBytes.size(), begin());
    return true;
}

Res COracle::SetTokenPrice(DCT_ID tokenId, CURRENCY_ID currencyId, CAmount amount, int64_t timestamp)
{
    if (!SupportsPair(tokenId, currencyId)) {
        return Res::Err("token <%s> - currency <%s>  is not allowed", tokenId.ToString(), currencyId.ToString());
    }

    tokenPrices[tokenId][currencyId] = std::make_pair(amount, timestamp);

    return Res::Ok();
}

Res COracleView::AppointOracle(const COracleId& oracleId, const COracle& oracle)
{
    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to appoint the new oracle <%s>", oracleId.GetHex());
    }

    return AddOracleId(oracleId);
}

Res COracleView::UpdateOracle(const COracleId& oracleId, const COracle& newOracle)
{
    if (!ExistsBy<ByName>(oracleId)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    COracle oracle{};
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    oracle.weightage = newOracle.weightage;
    oracle.oracleAddress = newOracle.oracleAddress;

    CTokenPricePoints allowedPrices{};
    for (auto &tmap: oracle.tokenPrices) {
        auto tid = tmap.first;
        auto &cmap = tmap.second;
        for (auto &cpair:cmap) {
            auto cid = cpair.first;
            if (newOracle.availablePairs.count({tid, cid}) == 0)
                continue;

            allowedPrices[tid][cid] = cpair.second;
        }
    }

    oracle.tokenPrices = std::move(allowedPrices);
    oracle.availablePairs = newOracle.availablePairs;

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

    auto res = RemoveOracleId(oracleId);
    if (!res.ok) {
        return Res::Err("failed to remove oracle id <%s> from list: %s", oracleId.GetHex(), res.msg);
    }

    // remove oracle
    if (!EraseBy<ByName>(oracleId)) {
        return Res::Err("failed to remove oracle <%s>", oracleId.GetHex());
    }

    return Res::Ok();
}

Res COracleView::SetOracleData(const COracleId& oracleId, int64_t timestamp, const CTokenPrices& tokenPrices)
{
    COracle oracle{};
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to read oracle %s from database", oracleId.GetHex());
    }

    for (auto& itToken : tokenPrices) {
        const auto& tokenId = itToken.first;
        const auto& map = itToken.second; // map: currencyId -> CAmount

        for (auto& itCurrency : map) {
            auto& currencyId = itCurrency.first;
            if (!oracle.SupportsPair(tokenId, currencyId)) {
                return Res::Err(
                    "oracle <%s> doesn't support token <%s> - currency <%s> price",
                    oracle.oracleId.GetHex(),
                    itToken.first.ToString(),
                    currencyId.ToString());
            }
            oracle.SetTokenPrice(tokenId, itCurrency.first, itCurrency.second, timestamp);
        }
    }

    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to store oracle %s to database", oracleId.GetHex());
    }

    return Res::Ok();
}

ResVal<COracle> COracleView::GetOracleData(const COracleId& oracleId) const
{
    COracle oracle{};
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    return ResVal<COracle>(oracle, Res::Ok());
}

// ----- operations with oracle ids list -----

std::vector<COracleId> COracleView::GetAllOracleIds() const
{
    std::vector<COracleId> oracles;
    if (!ReadBy<ByName>(_allOraclesKey, oracles)) {
        return {};
    }
    return oracles;
}

ResVal<std::set<TokenCurrencyPair>> COracleView::GetAllTokenCurrencyPairs() const
{
    auto oracleIds = GetAllOracleIds();

    std::set<TokenCurrencyPair> result;

    for (auto& oracleId : oracleIds) {
        auto oracleRes = GetOracleData(oracleId);
        if (!oracleRes.ok) {
            return Res::Err("failed to get data for oracle id <%s>", oracleRes.msg);
        }
        auto& oracle = *oracleRes.val;
        result.insert(oracle.availablePairs.begin(), oracle.availablePairs.end());
    }

    return {result, Res::Ok()};
}

Res COracleView::AddOracleId(const COracleId& oracleId)
{
    auto oracles = GetAllOracleIds();
    if (std::find(oracles.begin(), oracles.end(), oracleId) != std::end(oracles)) {
        return Res::Err("cannot add oracle id <%s> already exists", oracleId.GetHex());
    }
    oracles.push_back(oracleId);

    return UpdateOraclesList(oracles);
}

Res COracleView::RemoveOracleId(const COracleId& oracleId)
{
    auto oracles = GetAllOracleIds();
    auto it = std::find(oracles.begin(), oracles.end(), oracleId);
    if (it == std::end(oracles)) {
        return Res::Err("cannot remove oracle id <%s>, it doesn't exist", oracleId.GetHex());
    }
    oracles.erase(it);

    return UpdateOraclesList(oracles);
}

Res COracleView::UpdateOraclesList(const std::vector<COracleId>& oracles)
{
    if (!WriteBy<ByName>(_allOraclesKey, oracles)) {
        return Res::Err("failed to save oracle ids list");
    }
    return Res::Ok();
}

ResVal<CAmount> COracleView::GetSingleAggregatedPrice(DCT_ID tid, CURRENCY_ID cid, int64_t lastBlockTime) const
{
    auto iterator = TokenPriceIterator(*this, lastBlockTime);
    CAmount weightedSum{0};
    uint64_t sumWeights{0};
    uint32_t numLiveOracles{0};
    iterator.ForEach(
        [&weightedSum, &sumWeights, &numLiveOracles](
            const COracleId& oracleId,
            DCT_ID tokenId,
            CURRENCY_ID currencyId,
            int64_t oracleTime,
            CAmount rawPrice,
            uint8_t weightage,
            OracleState oracleState) -> Res {
            if (oracleState == OracleState::ALIVE) {
                sumWeights += static_cast<uint64_t>(weightage);
                ++numLiveOracles;

                auto mulResult = SafeMultiply(rawPrice, static_cast<uint64_t>(weightage));
                if (!mulResult.ok) {
                    return makeError(RPC_INVALID_PARAMETER, mulResult.msg);
                }
                auto addResult = SafeAdd(weightedSum, *mulResult.val);
                if (!addResult.ok) {
                    return makeError(RPC_INVALID_PARAMETER, addResult.msg);
                }
                weightedSum = *addResult.val;
            }

            return Res::Ok();
        },
        TokenCurrencyPair{tid, cid});

    if (numLiveOracles == 0) {
        return makeError(RPC_MISC_ERROR, "no live oracles for specified request");
    }

    if (sumWeights == 0) {
        return makeError(RPC_MISC_ERROR, "all live oracles which meet specified request, have zero weight");
    }

    CAmount aggregatedPrice = weightedSum / sumWeights;

    return {aggregatedPrice, Res::Ok()};
}
