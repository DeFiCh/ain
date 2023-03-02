#ifndef DEFI_RPC_RESULTCACHE_H
#define DEFI_RPC_RESULTCACHE_H

#include <atomic>
#include <string>
#include <map>
#include <set>
#include <optional>
#include <memory>
#include <univalue.h>
#include <masternodes/mn_rpc.h>
#include <rpc/request.h>

struct CGetBurnInfoResult {
    CAmount burntDFI{};
    CAmount burntFee{};
    CAmount auctionFee{};
    CBalances burntTokens;
    CBalances nonConsortiumTokens;
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
    std::atomic_bool syncFlag{false};
    std::set<std::string> smartModeList{};
    RPCCacheMode mode{RPCCacheMode::None};
    std::map<std::string, UniValue> cacheMap{};
    int cacheHeight{0};

};

RPCResultCache& GetRPCResultCache();

int GetLastValidatedHeight();
void SetLastValidatedHeight(int height);

struct CResultCache {
    int height;
    uint256 hash;
    std::variant<CGetBurnInfoResult> data;
};

class LastResultCache {
public:
    void Init(RPCResultCache::RPCCacheMode mode);
    CResultCache TryGet(const JSONRPCRequest &request);
    void Set(const JSONRPCRequest &request, const CResultCache &value);
private:
    std::atomic_bool syncFlag{false};
    std::map<std::string, CResultCache> cacheMap{};
    RPCResultCache::RPCCacheMode mode{RPCResultCache::RPCCacheMode::None};
};

LastResultCache& GetLastResultCache();

#endif //DEFI_RPC_RESULTCACHE_H
