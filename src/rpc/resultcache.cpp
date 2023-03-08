#include <rpc/resultcache.h>
#include <rpc/util.h>
#include <logging.h>

void RPCResultCache::Init(RPCCacheMode mode) {
    std::unique_lock l{aMutex};
    this->mode = mode;
}

std::string GetKey(const JSONRPCRequest &request) {
    std::stringstream ss;
    ss << request.strMethod << '/' << request.authUser << '/' << request.params.write();
    return ss.str();
}

bool RPCResultCache::InvalidateCaches() {
    std::unique_lock l{aMutex};
    auto height = GetLastValidatedHeight();
    if (cacheHeight != height) {
        LogPrint(BCLog::RPCCACHE, "RPCCache: clear\n");
        if (cacheMap.size()) cacheMap.clear();
        cacheHeight = height;
        return true;
    }
    return false;
}

std::optional<UniValue> RPCResultCache::TryGet(const JSONRPCRequest &request) {
    auto cacheMode = mode;
    if (cacheMode == RPCCacheMode::None) return {};
    if (cacheMode == RPCCacheMode::Smart &&
        smartModeList.find(request.strMethod) == smartModeList.end()) return {};
    auto key = GetKey(request);
    UniValue val;
    {
        std::unique_lock l{aMutex};
        if (auto res = cacheMap.find(key); res != cacheMap.end()) {
        if (LogAcceptCategory(BCLog::RPCCACHE)) {
            LogPrint(BCLog::RPCCACHE, "RPCCache: hit: key: %d/%s, val: %s\n", cacheHeight, key, res->second.write());
        }
            return res->second;
        }
    }
    return {};
}

const UniValue& RPCResultCache::Set(const JSONRPCRequest &request, const UniValue &value) {
    auto key = GetKey(request);
    {
        std::unique_lock l{aMutex};
        if (LogAcceptCategory(BCLog::RPCCACHE)) {
            LogPrint(BCLog::RPCCACHE, "RPCCache: set: key: %d/%s, val: %s\n", cacheHeight, key, value.write());
        }
        cacheMap[key] = value;
    }
    return value;
}

// Note: We initialize all the globals in the init phase. So, it's safe. Otherwise,
// static init is undefined behavior when multiple threads init them at the same time.
RPCResultCache& GetRPCResultCache() {
    static RPCResultCache g_rpcResultCache;
    return g_rpcResultCache;
}

static std::atomic<int> g_lastValidatedHeight{0};

int GetLastValidatedHeight() {
    auto res = g_lastValidatedHeight.load(std::memory_order_acquire);
    return res;
}

void SetLastValidatedHeight(int height) {
    LogPrint(BCLog::RPCCACHE, "RPCCache: set height: %d\n", height);
    g_lastValidatedHeight.store(height, std::memory_order_release);
    GetRPCResultCache().InvalidateCaches();
}

void MemoizedResultCache::Init(RPCResultCache::RPCCacheMode mode) {
    std::unique_lock l{aMutex};
    this->mode = mode;
}

CMemoizedResultValue MemoizedResultCache::GetOrDefault(const JSONRPCRequest &request) {
    if (mode == RPCResultCache::RPCCacheMode::None) return {};
    auto key = GetKey(request);
    {
        std::unique_lock l{aMutex};
        if (auto res = cacheMap.find(key); res != cacheMap.end()) {
            if (!::ChainActive().Contains(LookupBlockIndex(res->second.hash)))
                return {};

            if (LogAcceptCategory(BCLog::RPCCACHE)) {
                LogPrint(BCLog::RPCCACHE, "RPCCache: hit: key: %d/%s\n", res->second.height, key);
            }
            return res->second;
        }
    }
    return {};
}

void MemoizedResultCache::Set(const JSONRPCRequest &request, const CMemoizedResultValue &value) {
    auto key = GetKey(request);
    {
        std::unique_lock l{aMutex};
        if (LogAcceptCategory(BCLog::RPCCACHE)) {
            LogPrint(BCLog::RPCCACHE, "RPCCache: set: key: %d/%s\n", value.height, key);
        }
        cacheMap[key] = value;
    }
}

// Note: We initialize all the globals in the init phase. So, it's safe. Otherwise,
// static init is undefined behavior when multiple threads init them at the same time.
MemoizedResultCache& GetMemoizedResultCache() {
    static MemoizedResultCache g_memoizedResultCache;
    return g_memoizedResultCache;
}
