#ifndef DEFI_RPC_RESULTCACHE_H
#define DEFI_RPC_RESULTCACHE_H

#include <atomic>
#include <dfi/balances.h>
#include <map>
#include <memory>
#include <optional>
#include <rpc/request.h>
#include <string>
#include <set>
#include <uint256.h>
#include <univalue.h>

struct CGetBurnInfoResult {
    CAmount burntDFI{};
    CAmount burntFee{};
    CAmount auctionFee{};
    CBalances burntTokens;
    CBalances dexfeeburn;
    CBalances paybackFee;
};

class RPCResultCache {
public:
    enum RPCCacheMode {
        None,
        Smart,
        All
    };

    void Init(RPCCacheMode mode);
    std::optional<UniValue> TryGet(const JSONRPCRequest &request);
    const UniValue& Set(const JSONRPCRequest &request, const UniValue &value);
    bool InvalidateCaches();

private:
    AtomicMutex aMutex;
    std::set<std::string> smartModeList{};
    RPCCacheMode mode{RPCCacheMode::None};
    std::map<std::string, UniValue> cacheMap{};
    int cacheHeight{0};

};

RPCResultCache& GetRPCResultCache();

int GetLastValidatedHeight();
void SetLastValidatedHeight(int height);

struct CMemoizedResultValue {
    int height;
    uint256 hash;
    std::variant<CGetBurnInfoResult> data;
};

class MemoizedResultCache {
public:
    void Init(RPCResultCache::RPCCacheMode mode);
    CMemoizedResultValue GetOrDefault(const JSONRPCRequest &request);
    void Set(const JSONRPCRequest &request, const CMemoizedResultValue &value);
private:
    AtomicMutex aMutex;
    std::map<std::string, CMemoizedResultValue> cacheMap{};
    RPCResultCache::RPCCacheMode mode{RPCResultCache::RPCCacheMode::None};
};

MemoizedResultCache& GetMemoizedResultCache();

#endif //DEFI_RPC_RESULTCACHE_H
