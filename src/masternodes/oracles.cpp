#include "masternodes/oracles.h"

#include <algorithm>

const unsigned char COracleView::ByName::prefix = 'O'; // the big O for Oracles

bool COracleId::parseHex(const std::string &str) {
    auto oracleBytes = ParseHex(str);

    if (size() != oracleBytes.size()) {
        return false;
    }

    std::copy_n(oracleBytes.rbegin(), oracleBytes.size(), begin());
    return true;
}

Res COracle::SetTokenPrice(DCT_ID tokenId, CAmount amount, int64_t timestamp) {
    if (!SupportsToken(tokenId)) {
        return Res::Err("token <%s> is not allowed", tokenId.ToString());
    }

    tokenPrices[tokenId] = std::make_pair(amount, timestamp);

    return Res::Ok();
}

boost::optional<CPricePoint> COracle::GetTokenPrice(DCT_ID tokenId) const {
    if (SupportsToken(tokenId) && tokenPrices.find(tokenId) != tokenPrices.end()) {
        return tokenPrices.at(tokenId);
    }

    return {};
}

COracleView::COracleView(): _allOraclesKey{"THELISTOFALLORACLESINTHEDATABASE"} {}

Res COracleView::AppointOracle(COracleId oracleId, const COracle& oracle) {
    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to appoint the new oracle <%s>", oracleId.GetHex());
    }

    return AddOracleId(oracleId);
}

Res COracleView::UpdateOracle(COracleId oracleId, const COracle& oracle) {
    if (!ExistsBy<ByName>(oracleId)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    // no need to update oracles list
    if (!WriteBy<ByName>(oracleId, oracle)) {
        return Res::Err("failed to save oracle <%s>", oracleId.GetHex());
    }

    return Res::Ok();
}

Res COracleView::RemoveOracle(COracleId oracleId) {
    if (!ExistsBy<ByName>(oracleId)) {
        return Res::Err("oracle <%s> not found", oracleId.GetHex());
    }

    auto res = RemoveOracleId(oracleId);
    if (!res.ok) {
        return res;
    }

    // remove oracle
    if (!EraseBy<ByName>(oracleId)) {
        return Res::Err("failed to remove oracle id <%s> from list", oracleId.GetHex());
    }

    return Res::Ok();
}

Res COracleView::SetOracleData(COracleId oracleId,  int64_t timestamp, const CBalances& balances) {
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

// ----- operations with oracle ids list -----

std::vector<COracleId> COracleView::GetAllOracleIds() {
    std::vector<COracleId> oracles;
    if (!ReadBy<ByName>(_allOraclesKey, oracles)) {
        return {};
    }
    return oracles;
}

Res COracleView::AddOracleId(COracleId oracleId) {
    auto oracles = GetAllOracleIds();
    if (std::find(oracles.begin(), oracles.end(), oracleId) != std::end(oracles)) {
        return Res::Err("cannot add oracle id <%s> already exists", oracleId.GetHex());
    }
    oracles.push_back(oracleId);

    if (!WriteBy<ByName>(_allOraclesKey, oracles)) {
        return Res::Err("failed to save oracle ids list");
    }

    return Res::Ok();
}

Res COracleView::RemoveOracleId(COracleId oracleId) {
    auto oracles = GetAllOracleIds();
    auto it = std::find(oracles.begin(), oracles.end(), oracleId);
    if (it == std::end(oracles)) {
        return Res::Err("cannot remove oracle id <%s>, it doesn't exist", oracleId.GetHex());
    }
    oracles.erase(it);

    return UpdateOraclesList(oracles);
}

Res COracleView::UpdateOraclesList(const std::vector<COracleId>& oracles) {
    if (!WriteBy<ByName>(_allOraclesKey, oracles)) {
        return Res::Err("failed to save oracle ids list");
    }
    return Res::Ok();
}
