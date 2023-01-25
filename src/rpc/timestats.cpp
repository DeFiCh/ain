#include <rpc/timestats.h>
#include <rpc/server.h>

bool CTimeStats::isActive() { return active.load(); }
void CTimeStats::setActive(bool isActive) { active.store(isActive); }
extern size_t timeStatsOutliersSize;

void TimeStatsOutliers::push(const TimeStatsOutlier& outlier)
{
    if (members.size() < timeStatsOutliersSize) {
        members.push_back(outlier);
        members.sort(TimeCompare(max));
    } else if (auto it = max ? members.back() : members.front(); max ? outlier.time >= it.time : outlier.time <= it.time) {
        if (max) {
            members.pop_back();
            members.push_back(outlier);
        }
        else {
            members.pop_front();
            members.push_front(outlier);
        }
        members.sort(TimeCompare(max));
    }
}

std::optional<EntityTimeStats> CTimeStats::getTxType(const CustomTxType& type) {
    CLockFreeGuard lock(lock_stats);

    auto it = txStats.find(type);
    if (it == txStats.end()) {
        return {};
    }

    return it->second;
}

std::map<CustomTxType, EntityTimeStats> CTimeStats::getTxStats() {
    CLockFreeGuard lock(lock_stats);

    return txStats;
}

EntityTimeStats CTimeStats::getBlockStats() {
    CLockFreeGuard lock(lock_stats);

    return blockStats;
}

void CTimeStats::save() {
    fs::path statsPath = GetDataDir() / DEFAULT_TIME_STATSFILE;
    fsbridge::ofstream file(statsPath);

    file << toJSON().write() << '\n';
    file.close();
}

void CTimeStats::load() {
    fs::path statsPath = GetDataDir() / DEFAULT_TIME_STATSFILE;
    fsbridge::ifstream file(statsPath);
    if (!file.is_open()) return;

    std::string line;
    file >> line;

    if (!line.size()) return;

    UniValue obj(UniValue::VOBJ);
    UniValue arr(UniValue::VARR);
    if (obj.read((const std::string)line)) {
        CLockFreeGuard lock(lock_stats);
        for (const auto &val : arr.getValues()) {
            auto type = FromString(val["type"].getValStr());
            txStats[type] = EntityTimeStats::fromJSON(val);
        }

        blockStats = EntityTimeStats::fromJSON(obj["blockStats"]);
    }

    file.close();
}

void CTimeStats::add(const CustomTxType& type, const int64_t latency, const uint256& txid, const uint32_t height)
{
    auto stats = CTimeStats::getTxType(type);
    if (stats) {
        stats->count++;
        stats->latency = {
            std::min(latency, stats->latency.min),
            stats->latency.avg + (latency - stats->latency.avg) / stats->count,
            std::max(latency, stats->latency.max)
        };
    } else {
        stats = { latency };
    }
    stats->minOutliers.push({ latency, txid, height});
    stats->maxOutliers.push({ latency, txid, height});

    CLockFreeGuard lock(lock_stats);
    txStats[type] = *stats;
}

void CTimeStats::add(const int64_t latency, const uint256& hash, const uint32_t height)
{
    auto stats = CTimeStats::getBlockStats();
    stats.count++;
    stats.latency = {
        std::min(latency, stats.latency.min),
        stats.latency.avg + (latency - stats.latency.avg) / stats.count,
        std::max(latency, stats.latency.max)
    };
    stats.minOutliers.push({ latency, hash, height});
    stats.maxOutliers.push({ latency, hash, height});

    CLockFreeGuard lock(lock_stats);
    blockStats = stats;
}

UniValue EntityTimeStats::toJSON() {
    UniValue stats(UniValue::VOBJ),
             latencyObj(UniValue::VOBJ),
             maxOutliersArr(UniValue::VARR),
             minOutliersArr(UniValue::VARR);

    latencyObj.pushKV("min", latency.min);
    latencyObj.pushKV("avg", latency.avg);
    latencyObj.pushKV("max", latency.max);

    for (auto const &entry : minOutliers.members) {
        UniValue outlierObj(UniValue::VOBJ);
        outlierObj.pushKV("time", entry.time);
        outlierObj.pushKV("hash", entry.hash.GetHex());
        outlierObj.pushKV("height", static_cast<int>(entry.height));
        minOutliersArr.push_back(outlierObj);
    }
    for (auto const &entry : maxOutliers.members) {
        UniValue outlierObj(UniValue::VOBJ);
        outlierObj.pushKV("time", entry.time);
        outlierObj.pushKV("hash", entry.hash.GetHex());
        outlierObj.pushKV("height", static_cast<int>(entry.height));
        maxOutliersArr.push_back(outlierObj);
    }

    stats.pushKV("count", count);
    stats.pushKV("latency", latencyObj);
    stats.pushKV("minOutliers", minOutliersArr);
    stats.pushKV("maxOutliers", maxOutliersArr);

    return stats;
}

EntityTimeStats EntityTimeStats::fromJSON(UniValue json) {
    EntityTimeStats stats;

    stats.count = json["count"].get_int64();
    if (!json["latency"].isNull()) {
        auto latencyObj  = json["latency"].get_obj();
        stats.latency = {
            latencyObj["min"].get_int64(),
            latencyObj["avg"].get_int64(),
            latencyObj["max"].get_int64()
        };
    }
    if (!json["minOutliers"].isNull()) {
        auto outliersArr = json["minOutliers"].get_array();
        for (const auto &entry : outliersArr.getValues()) {
            auto outlierObj = entry.get_obj();
            TimeStatsOutlier outlierEntry {
                outlierObj["time"].get_int64(),
                ParseHashV(outlierObj["hash"], "hash"),
                static_cast<uint32_t>(outlierObj["height"].get_int())
            };
            stats.minOutliers.push(outlierEntry);
        }
    }
    if (!json["maxOutliers"].isNull()) {
        auto outliersArr = json["maxOutliers"].get_array();
        for (const auto &entry : outliersArr.getValues()) {
            auto outlierObj = entry.get_obj();
            TimeStatsOutlier outlierEntry {
                outlierObj["time"].get_int64(),
                ParseHashV(outlierObj["hash"], "hash"),
                static_cast<uint32_t>(outlierObj["height"].get_int())
            };
            stats.maxOutliers.push(outlierEntry);
        }
    }

    return stats;
}

UniValue CTimeStats::toJSON() {
    auto txStats = CTimeStats::getTxStats();
    auto blockStats = CTimeStats::getBlockStats();

    UniValue ret(UniValue::VOBJ);
    UniValue tx(UniValue::VARR);
    for (auto &[type, stats] : txStats) {
        auto obj = stats.toJSON();
        obj.pushKV("type", ToString(type));
        tx.push_back(obj);
    }
    ret.pushKV("txStats", tx);
    ret.pushKV("blockStats", blockStats.toJSON());

    return ret;
}

static UniValue gettimestats(const JSONRPCRequest& request)
{
    RPCHelpMan{"gettimestats",
        "\nGet transaction time stats for selected transaction type.\n",
        {
            {"stats", RPCArg::Type::STR, RPCArg::Optional::NO, "block/tx"},
            {"txType", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The type of custom transaction to get stats for."}
        },
        RPCResult{"(array) Json object with stats information\n"},
        RPCExamples{
            HelpExampleCli("gettimestats", "block") +
            HelpExampleRpc("gettimestats", "")
        },
    }.Check(request);

    if (!timeStats.isActive()) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Rpcstats is desactivated.");
    }

    auto type = request.params[0].get_str();
    if (type == "tx") {
        if (request.params[1].isNull())
            throw JSONRPCError(RPC_INVALID_REQUEST, "Type for transaction stats needs to be supplied.");

        auto txType = FromString(request.params[1].getValStr());
        auto typeStats = timeStats.getTxType(txType);
        if (typeStats) {
            auto obj = typeStats->toJSON();
            obj.pushKV("type", ToString(txType));
            return obj;
        }
    }

    return timeStats.getBlockStats().toJSON();
}

static UniValue listtimestats(const JSONRPCRequest& request)
{
    RPCHelpMan{"listtimestats",
        "\nList all time statistics.\n",
        {},
        RPCResult{"{txStats:{...},...}     (array) Json object with stats information\n"},
        RPCExamples{
            HelpExampleCli("listtimestats", "") +
            HelpExampleRpc("listtimestats", "")
        },
    }.Check(request);

    if (!timeStats.isActive()) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Time stats is desactivated.");
    }

    return timeStats.toJSON();
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "stats",              "gettimestats",            &gettimestats,            {"txType"} },
    { "stats",              "listtimestats",           &listtimestats,           {} },
};
// clang-format on

void RegisterTimeStats(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}