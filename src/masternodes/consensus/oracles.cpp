// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <masternodes/consensus/oracles.h>
#include <masternodes/masternodes.h>
#include <masternodes/oracles.h>

Res COraclesConsensus::operator()(const CAppointOracleMessage& obj) const {
    Require(HasFoundationAuth());

    COracle oracle;
    static_cast<CAppointOracleMessage&>(oracle) = obj;
    Require(NormalizeTokenCurrencyPair(oracle.availablePairs));
    return mnview.AppointOracle(tx.GetHash(), oracle);
}

Res COraclesConsensus::operator()(const CUpdateOracleAppointMessage& obj) const {
    Require(HasFoundationAuth());

    COracle oracle;
    static_cast<CAppointOracleMessage&>(oracle) = obj.newOracleAppoint;
    Require(NormalizeTokenCurrencyPair(oracle.availablePairs));
    return mnview.UpdateOracle(obj.oracleId, std::move(oracle));
}

Res COraclesConsensus::operator()(const CRemoveOracleAppointMessage& obj) const {
    Require(HasFoundationAuth());
    return mnview.RemoveOracle(obj.oracleId);
}

Res COraclesConsensus::operator()(const CSetOracleDataMessage& obj) const {
    auto oracle = mnview.GetOracleData(obj.oracleId);
    Require(oracle, "failed to retrieve oracle <%s> from database", obj.oracleId.GetHex());

    Require(HasAuth(oracle.val->oracleAddress));

    if (height >= uint32_t(consensus.FortCanningHeight)) {
        for (const auto& tokenPrice : obj.tokenPrices)
            for (const auto& price : tokenPrice.second) {
                Require(price.second > 0, "Amount out of range");

                auto timestamp = time;
                extern bool diffInHour(int64_t time1, int64_t time2);
                Require(diffInHour(obj.timestamp, timestamp), "Timestamp (%d) is out of price update window (median: %d)",
                                     obj.timestamp, timestamp);
            }
    }
    return mnview.SetOracleData(obj.oracleId, obj.timestamp, obj.tokenPrices);
}
