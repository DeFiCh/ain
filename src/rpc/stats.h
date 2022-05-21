#ifndef DEFI_RPC_STATS_H
#define DEFI_RPC_STATS_H

#include <map>
#include <stdint.h>
#include <univalue.h>
#include <util/time.h>
#include <util/system.h>

#include <boost/circular_buffer.hpp>

const char * const DEFAULT_STATSFILE = "stats.log";
static const uint8_t RPC_STATS_HISTORY_SIZE = 5;
const bool DEFAULT_RPC_STATS = true;

struct MinMaxStatEntry {
    int64_t min;
    int64_t avg;
    int64_t max;

    MinMaxStatEntry() = default;
    MinMaxStatEntry(int64_t val) : MinMaxStatEntry(val, val, val) {};
    MinMaxStatEntry(int64_t min, int64_t avg, int64_t max) : min(min), avg(avg), max(max) {};
};

struct StatHistoryEntry {
    int64_t timestamp;
    int64_t latency;
    int64_t payload;
};

struct RPCStats {
    std::string name;
    int64_t lastUsedTime;
    MinMaxStatEntry latency;
    MinMaxStatEntry payload;
    int64_t count;
    boost::circular_buffer<StatHistoryEntry> history;

    RPCStats() : history(RPC_STATS_HISTORY_SIZE) {}

    RPCStats(const std::string& name, int64_t latency, int64_t payload) : name(name), latency(latency), payload(payload), history(RPC_STATS_HISTORY_SIZE) {
        lastUsedTime = GetSystemTimeInSeconds();
        count = 1;
    };

    UniValue toJSON();
    static RPCStats fromJSON(UniValue json);
};

/**
 * DeFi Blockchain RPC Stats class.
 */
class CRPCStats
{
private:
    CLockFreeMutex lock_stats;
    std::map<std::string, RPCStats> map;
    std::atomic_bool active{DEFAULT_RPC_STATS};

public:
    bool isActive() {
        return active.load();
    }
    void setActive(bool isActive) {
        active.store(isActive);
    }

    void add(const std::string& name, const int64_t latency, const int64_t payload);

    std::optional<RPCStats> get(const std::string& name) {
        CLockFreeGuard lock(lock_stats);

        auto it = map.find(name);
        if (it == map.end()) {
            return {};
        }
        return it->second;
    };
    std::map<std::string, RPCStats> getMap() {
        CLockFreeGuard lock(lock_stats);
        return map;
    };
    UniValue toJSON();

    void save() {
        fs::path statsPath = GetDataDir() / DEFAULT_STATSFILE;
        fsbridge::ofstream file(statsPath);

        file << toJSON().write() << '\n';
        file.close();
    };

    void load() {
        fs::path statsPath = GetDataDir() / DEFAULT_STATSFILE;
        fsbridge::ifstream file(statsPath);
        if (!file.is_open()) return;

        std::string line;
        file >> line;

        if (!line.size()) return;

        UniValue arr(UniValue::VARR);
        arr.read((const std::string)line);

        CLockFreeGuard lock(lock_stats);
        for (const auto &val : arr.getValues()) {
            auto name = val["name"].get_str();
            map[name] = RPCStats::fromJSON(val);
        }
        file.close();
    };
};

extern CRPCStats statsRPC;

#endif // DEFI_RPC_STATS_H
