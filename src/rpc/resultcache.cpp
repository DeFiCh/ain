//
// Created by pvl on 4/6/22.
//

#include <rpc/resultcache.h>
#include <rpc/util.h>
#include <logging.h>

void RPCResultCache::Init(RPCCacheMode mode) {
    CLockFreeGuard lock{syncFlag};
    this->mode = mode;
}

std::string GetKey(const JSONRPCRequest &request) {
    std::stringstream ss;
    ss << request.strMethod << '/' << request.authUser << '/' << request.params.write();
    return ss.str();
}

// Note: Use only in already synchronized context for cacheMap.
bool RPCResultCache::MayBeInvalidateCaches() {
    auto height = GetLastValidatedHeight();
    if (cacheHeight != height) {
        cacheMap.clear();
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
        CLockFreeGuard lock{syncFlag};
        if (MayBeInvalidateCaches()) return {};
        if (auto res = cacheMap.find(key); res != cacheMap.end()) {
            return res->second;
        }
    }
    return {};
}

const UniValue& RPCResultCache::Set(const JSONRPCRequest &request, const UniValue &value) {
    auto key = GetKey(request);
    {
        CLockFreeGuard lock{syncFlag};
        MayBeInvalidateCaches();
        cacheMap[key] = value;
    }
    return value;
}

// Note: We initialize all the globals in the init phase.
// So, it's state. Otherwise, static init is undefined when multiple threads init them at the same time.
RPCResultCache& GetRPCResultCache() {
    static RPCResultCache g_rpcResultCache;
    return g_rpcResultCache;
}

static std::atomic<int> g_lastValidatedHeight{0};

int GetLastValidatedHeight() {
    return g_lastValidatedHeight.load(std::memory_order_acquire);
}

void SetLastValidatedHeight(int height) {
    g_lastValidatedHeight.store(height, std::memory_order_release);
}