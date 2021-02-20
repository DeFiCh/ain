// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>
#include <masternodes/tokenpriceiterator.h>

#include <rpc/protocol.h>
#include <rpc/request.h>

void TokenPriceIterator::ForEach(const Visitor& visitor,
    boost::optional<TokenCurrencyPair> filter)
{
    const auto oracleIds = _view.get().GetAllOracleIds();
    if (oracleIds.empty())
        return;

    auto CheckIfAlive = [this](uint64_t time) -> bool {
        constexpr uint64_t SECONDS_PER_HOUR = 3600u;
        return std::abs(static_cast<int64_t>(time) - _lastBlockTime) < SECONDS_PER_HOUR;
    };

    for (auto& oracleId : oracleIds) {
        auto oracleRes = _view.get().GetOracleData(oracleId);
        if (!oracleRes.ok) {
            throw JSONRPCError(RPC_DATABASE_ERROR, "failed to get oracle: " + oracleId.GetHex());
        }

        auto& oracle = *oracleRes.val;
        auto& pricesMap = oracle.tokenPrices;

        if (filter.is_initialized()) {
            DCT_ID fixedTid = filter->tid;
            CURRENCY_ID fixedCid = filter->cid;
            if (pricesMap.count(fixedTid) > 0) {
                auto& map = pricesMap.at(fixedTid);
                if (map.count(fixedCid) > 0) {
                    auto& pricePoint = map.at(fixedCid);
                    auto amount = pricePoint.first;
                    auto timestamp = pricePoint.second;
                    visitor(oracle.oracleId, fixedTid, fixedCid, timestamp, amount, oracle.weightage,
                        CheckIfAlive(timestamp) ? OracleState::ALIVE : OracleState::EXPIRED);
                }
            }
        } else {
            for (auto& tPair : pricesMap) {
                auto tid = tPair.first;
                auto& map = tPair.second;
                for (auto& cPair : map) {
                    auto cid = cPair.first;
                    auto pricePoint = cPair.second;
                    CAmount amount = pricePoint.first;
                    int64_t timestamp = pricePoint.second;
                    visitor(oracle.oracleId, tid, cid, timestamp, amount, oracle.weightage,
                        CheckIfAlive(timestamp) ? OracleState::ALIVE : OracleState::EXPIRED);
                }
            }
        }
    }
}
