// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <dfi/consensus/oracles.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

Res COraclesConsensus::NormalizeTokenCurrencyPair(std::set<CTokenCurrencyPair> &tokenCurrency) const {
    std::set<CTokenCurrencyPair> trimmed;
    for (const auto &pair : tokenCurrency) {
        auto token = trim_ws(pair.first).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        auto currency = trim_ws(pair.second).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        if (token.empty() || currency.empty()) {
            return Res::Err("empty token / currency");
        }
        trimmed.emplace(token, currency);
    }
    tokenCurrency = std::move(trimmed);
    return Res::Ok();
}

Res COraclesConsensus::operator()(const CAppointOracleMessage &obj) const {
    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member");
    }

    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    COracle oracle;
    static_cast<CAppointOracleMessage &>(oracle) = obj;
    auto res = NormalizeTokenCurrencyPair(oracle.availablePairs);
    return !res ? res : mnview.AppointOracle(tx.GetHash(), oracle);
}

Res COraclesConsensus::operator()(const CUpdateOracleAppointMessage &obj) const {
    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member");
    }

    auto &mnview = blockCtx.GetView();

    COracle oracle;
    static_cast<CAppointOracleMessage &>(oracle) = obj.newOracleAppoint;
    if (auto res = NormalizeTokenCurrencyPair(oracle.availablePairs); !res) {
        return res;
    }
    return mnview.UpdateOracle(obj.oracleId, std::move(oracle));
}

Res COraclesConsensus::operator()(const CRemoveOracleAppointMessage &obj) const {
    if (auto res = HasFoundationAuth(); !res) {
        return res;
    }

    auto &mnview = blockCtx.GetView();

    return mnview.RemoveOracle(obj.oracleId);
}

Res COraclesConsensus::operator()(const CSetOracleDataMessage &obj) const {
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    auto oracle = mnview.GetOracleData(obj.oracleId);
    if (!oracle) {
        return Res::Err("failed to retrieve oracle <%s> from database", obj.oracleId.GetHex());
    }
    if (!HasAuth(oracle.val->oracleAddress)) {
        return Res::Err("tx must have at least one input from account owner");
    }
    if (height >= uint32_t(consensus.DF11FortCanningHeight)) {
        for (const auto &tokenPrice : obj.tokenPrices) {
            for (const auto &price : tokenPrice.second) {
                if (price.second <= 0) {
                    return Res::Err("Amount out of range");
                }
                auto timestamp = time;
                extern bool diffInHour(int64_t time1, int64_t time2);
                if (!diffInHour(obj.timestamp, timestamp)) {
                    return Res::Err(
                        "Timestamp (%d) is out of price update window (median: %d)", obj.timestamp, timestamp);
                }
            }
        }
    }
    return mnview.SetOracleData(obj.oracleId, obj.timestamp, obj.tokenPrices);
}
