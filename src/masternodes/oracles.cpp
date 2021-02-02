#include "masternodes/oracles.h"

const unsigned char COracleView::ByName::prefix = 'O'; // the big O for Oracles

Res COracleView::AppointOracle(COracleId oracleId, const COracle& oracle) {
    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to appoint the new oracle <%s>", oracleId.GetHex());
    }
    return Res::Ok();
}

Res COracleView::SetOracleData(COracleId oracleId,  BTCTimeStamp timestamp, const CBalances& balances) {
    COracle oracle{};
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to read oracle %s from database", oracleId.GetHex());
    }

    auto &balanceMap = balances.balances; // DCT_ID -> CAmount

    for (auto &it: balanceMap) {
        const auto &tokenId = it.first;
        if (!oracle.SupportsToken(tokenId)) {
            return Res::Err(
                    "oracle <%s> is not allowed to present token <%s> price",
                    oracle.oracleId.GetHex(),
                    it.first.ToString());
        }

        oracle.SetTokenPrice(tokenId, it.second, timestamp);
    }

    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to store oracle %s to database", oracleId.GetHex());
    }

    return Res::Ok();
}

ResVal<COracle> COracleView::GetOracleData(COracleId oracleId) const {
    COracle oracle{};
    if (!ReadBy<ByName>(oracleId, oracle)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    return ResVal<COracle>(oracle, Res::Ok());
}

bool COracleView::RemoveOracle(COracleId oracleId) {
    return EraseBy<ByName>(oracleId);
}
