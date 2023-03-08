#include <rpc/stats.h>
#include <rpc/server.h>
#include <rpc/util.h>

bool CRPCStats::isActive() { return active.load(); }
void CRPCStats::setActive(bool isActive) { active.store(isActive); }

std::optional<RPCStats> CRPCStats::get(const std::string& name) {
    std::unique_lock lock(lock_stats);

    auto it = map.find(name);
    if (it == map.end()) {
        return {};
    }
    return it->second;
}

std::map<std::string, RPCStats> CRPCStats::getMap() {
    std::unique_lock lock(lock_stats);
    return map;
}

void CRPCStats::save() {
    fs::path statsPath = GetDataDir() / DEFAULT_STATSFILE;
    fsbridge::ofstream file(statsPath);

    file << toJSON().write() << '\n';
    file.close();
}

void CRPCStats::load() {
    fs::path statsPath = GetDataDir() / DEFAULT_STATSFILE;
    fsbridge::ifstream file(statsPath);
    if (!file.is_open()) return;

    std::string line;
    file >> line;

    if (!line.size()) return;

    UniValue arr(UniValue::VARR);
    arr.read((const std::string)line);

    std::unique_lock lock(lock_stats);
    for (const auto &val : arr.getValues()) {
        auto name = val["name"].get_str();
        map[name] = RPCStats::fromJSON(val);
    }
    file.close();
}

UniValue RPCStats::toJSON() {
    UniValue stats(UniValue::VOBJ),
             latencyObj(UniValue::VOBJ),
             payloadObj(UniValue::VOBJ),
             historyArr(UniValue::VARR);

    latencyObj.pushKV("min", latency.min);
    latencyObj.pushKV("avg", latency.avg);
    latencyObj.pushKV("max", latency.max);

    payloadObj.pushKV("min", payload.min);
    payloadObj.pushKV("avg", payload.avg);
    payloadObj.pushKV("max", payload.max);

    for (auto const &entry : history) {
        UniValue historyObj(UniValue::VOBJ);
        historyObj.pushKV("timestamp", entry.timestamp);
        historyObj.pushKV("latency", entry.latency);
        historyObj.pushKV("payload", entry.payload);
        historyArr.push_back(historyObj);
    }

    stats.pushKV("name", name);
    stats.pushKV("count", count);
    stats.pushKV("lastUsedTime", lastUsedTime);
    stats.pushKV("latency", latencyObj);
    stats.pushKV("payload", payloadObj);
    stats.pushKV("history", historyArr);
    return stats;
}

RPCStats RPCStats::fromJSON(UniValue json) {
    RPCStats stats;

    stats.name = json["name"].get_str();
    stats.lastUsedTime = json["lastUsedTime"].get_int64();
    stats.count = json["count"].get_int64();
    if (!json["latency"].isNull()) {
        auto latencyObj  = json["latency"].get_obj();
        stats.latency = {
            latencyObj["min"].get_int64(),
            latencyObj["avg"].get_int64(),
            latencyObj["max"].get_int64()
        };
    }

    if (!json["payload"].isNull()) {
        auto payloadObj  = json["payload"].get_obj();
        stats.payload = {
            payloadObj["min"].get_int64(),
            payloadObj["avg"].get_int64(),
            payloadObj["max"].get_int64()
        };
    }

    if (!json["history"].isNull()) {
        auto historyArr = json["history"].get_array();
        for (const auto &entry : historyArr.getValues()) {
            auto historyObj = entry.get_obj();
            StatHistoryEntry historyEntry {
                historyObj["timestamp"].get_int64(),
                historyObj["latency"].get_int64(),
                historyObj["payload"].get_int64()
            };
            stats.history.push_back(historyEntry);
        }
    }
    return stats;
}

void CRPCStats::add(const std::string& name, const int64_t latency, const int64_t payload)
{
    auto stats = CRPCStats::get(name);
    if (stats) {
        stats->count++;
        stats->lastUsedTime = GetSystemTimeInSeconds();
        stats->latency = {
            std::min(latency, stats->latency.min),
            stats->latency.avg + (latency - stats->latency.avg) / stats->count,
            std::max(latency, stats->latency.max)
        };
        stats->payload = {
            std::min(payload, stats->payload.min),
            stats->payload.avg + (payload - stats->payload.avg) / stats->count,
            std::max(payload, stats->payload.max)
        };
    } else {
        stats = { name, latency, payload };
    }
    stats->history.push_back({ stats->lastUsedTime, latency, payload });

    std::unique_lock lock(lock_stats);
    map[name] = *stats;
}

UniValue CRPCStats::toJSON() {
    auto map = CRPCStats::getMap();

    UniValue ret(UniValue::VARR);
    for (auto &[_, stats] : map) {
        ret.push_back(stats.toJSON());
    }
    return ret;
}

static UniValue getrpcstats(const JSONRPCRequest& request)
{
    RPCHelpMan{"getrpcstats",
        "\nGet RPC stats for selected command.\n",
        {
            {"command", RPCArg::Type::STR, RPCArg::Optional::NO, "The command to get stats for."}
        },
        RPCResult{
            " {\n"
            "  \"name\":               (string) The RPC command name.\n"
            "  \"latency\":            (json object) Min, max and average latency.\n"
            "  \"payload\":            (json object) Min, max and average payload size in bytes.\n"
            "  \"count\":              (numeric) The number of times this command as been used.\n"
            "  \"lastUsedTime\":       (numeric) Last used time as timestamp.\n"
            "  \"history\":            (json array) History of last 5 RPC calls.\n"
            "  [\n"
            "       {\n"
            "           \"timestamp\": (numeric)\n"
            "           \"latency\":   (numeric)\n"
            "           \"payload\":   (numeric)\n"
            "       }\n"
            "  ]\n"
            "}"
        },
        RPCExamples{
            HelpExampleCli("getrpcstats", "getblockcount") +
            HelpExampleRpc("getrpcstats", "\"getblockcount\"")
        },
    }.Check(request);

    if (!statsRPC.isActive()) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Rpcstats is desactivated.");
    }

    auto command = request.params[0].get_str();
    auto stats = statsRPC.get(command);
    if (!stats) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "No stats for this command.");
    }
    return stats->toJSON();
}

static UniValue listrpcstats(const JSONRPCRequest& request)
{
    RPCHelpMan{"listrpcstats",
        "\nList used RPC commands.\n",
        {},
        RPCResult{
            "[\n"
            " {\n"
            "  \"name\":               (string) The RPC command name.\n"
            "  \"latency\":            (json object) Min, max and average latency.\n"
            "  \"payload\":            (json object) Min, max and average payload size in bytes.\n"
            "  \"count\":              (numeric) The number of times this command as been used.\n"
            "  \"lastUsedTime\":       (numeric) Last used time as timestamp.\n"
            "  \"history\":            (json array) History of last 5 RPC calls.\n"
            "  [\n"
            "       {\n"
            "           \"timestamp\": (numeric)\n"
            "           \"latency\":   (numeric)\n"
            "           \"payload\":   (numeric)\n"
            "       }\n"
            "  ]\n"
            " }\n"
            "]"
        },
        RPCExamples{
            HelpExampleCli("listrpcstats", "") +
            HelpExampleRpc("listrpcstats", "")
        },
    }.Check(request);

    if (!statsRPC.isActive()) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Rpcstats is desactivated.");
    }

    return statsRPC.toJSON();
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "stats",              "getrpcstats",            &getrpcstats,            {"command"} },
    { "stats",              "listrpcstats",           &listrpcstats,           {} },
};
// clang-format on

void RegisterStatsRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
