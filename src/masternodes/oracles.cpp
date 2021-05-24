// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>

#include <masternodes/oracles.h>
#include <rpc/protocol.h>

const unsigned char COracleView::ByName::prefix = 'O'; // the big O for Oracles

COracle::COracle(CAppointOracleMessage const& msg) : CAppointOracleMessage(msg)
{
}

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

Res COracleView::AppointOracle(const COracleId& oracleId, const COracle& oracle)
{
    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to appoint the new oracle <%s>", oracleId.GetHex());
    }

    return Res::Ok();
}

Res COracleView::UpdateOracle(const COracleId& oracleId, const COracle& newOracle)
{
    COracle oracle(CAppointOracleMessage{});
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
    COracle oracle(CAppointOracleMessage{});
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
    COracle oracle(CAppointOracleMessage{});
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    return ResVal<COracle>(oracle, Res::Ok());
}

void COracleView::ForEachOracle(std::function<bool(const COracleId&, CLazySerialize<COracle>)> callback, const COracleId& start)
{
    ForEach<ByName, COracleId, COracle>(callback, start);
}
