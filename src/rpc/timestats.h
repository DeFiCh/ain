#ifndef DEFI_RPC_TIMESTATS_H
#define DEFI_RPC_TIMESTATS_H

#include <masternodes/mn_checks.h>
#include <queue>
#include "stats.h"
#include <uint256.h>
#include "util.h"

static const bool DEFAULT_TIME_STATS = false;
static const uint32_t DEFAULT_TIME_STATS_OUTLIERS_SIZE = 5;
const char * const DEFAULT_TIME_STATSFILE = "txtimes.log";

struct TimeStatsOutlier {
    int64_t time;
    uint256 hash;
    uint32_t height;
};

class TimeCompare
{
    bool max;
public:
    TimeCompare(bool m): max(m) {}

    bool operator() (const TimeStatsOutlier& a, const TimeStatsOutlier& b)
    {
        return (max ? a.time > b.time : a.time < b.time);
    }
};

class TimeStatsOutliers
{
    bool max;
public:
    std::list<TimeStatsOutlier> members;

    TimeStatsOutliers(bool m): max(m) {}

    void push(const TimeStatsOutlier& outlier);
};

struct EntityTimeStats {
    MinMaxStatEntry latency;
    int64_t count;
    TimeStatsOutliers maxOutliers{true};
    TimeStatsOutliers minOutliers{false};

    EntityTimeStats(): latency(std::numeric_limits<int64_t>::max(), 0 ,std::numeric_limits<int64_t>::min()) {
        count = 0;
    }

    EntityTimeStats(int64_t latency) :
        latency(latency) {
            count = 1;
    };

    UniValue toJSON();
    static EntityTimeStats fromJSON(UniValue json);
};

class CTimeStats
{
private:
    std::atomic_bool lock_stats{false};
    std::map<CustomTxType, EntityTimeStats> txStats;
    EntityTimeStats blockStats;
    std::atomic_bool active{DEFAULT_TIME_STATS};
public:
    bool isActive();
    void setActive(bool isActive);
    void add(const CustomTxType& type, const int64_t latency, const uint256& txid, const uint32_t height);
    void add(const int64_t latency, const uint256& hash, const uint32_t height);
    std::optional<EntityTimeStats> getTxType(const CustomTxType& name);
    std::map<CustomTxType, EntityTimeStats> getTxStats();
    EntityTimeStats getBlockStats();
    UniValue toJSON();
    void save();
    void load();
};

extern CTimeStats timeStats;

#endif // DEFI_RPC_TIMESTATS_H