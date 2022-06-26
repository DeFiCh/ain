// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/futureswap.h>

#include <masternodes/masternodes.h>

Res CFutureSwapView::StoreFuturesUserValues(const CFuturesUserKey& key, const CFuturesUserValue& futures)
{
    if (!WriteBy<ByFuturesSwapKey>(key, futures)) {
        return Res::Err("Failed to store futures");
    }

    if (!WriteBy<ByFuturesOwnerKey>(CFuturesCScriptKey{key.owner, key.height, key.txn}, "")) {
        return Res::Err("Failed to store futures");
    }

    return Res::Ok();
}

void CFutureBaseView::ForEachFuturesUserValues(std::function<bool(const CFuturesUserKey&, const CFuturesUserValue&)> callback, const CFuturesUserKey& start)
{
    ForEach<ByFuturesSwapKey, CFuturesUserKey, CFuturesUserValue>(callback, start);
}

Res CFutureBaseView::EraseFuturesUserValues(const CFuturesUserKey& key)
{
    if (!EraseBy<ByFuturesSwapKey>(key)) {
        return Res::Err("Failed to erase futures");
    }

    return Res::Ok();
}

Res CFutureSwapView::EraseFuturesUserValues(const CFuturesUserKey& key)
{
    if (!EraseBy<ByFuturesSwapKey>(key)) {
        return Res::Err("Failed to erase futures");
    }

    if (!EraseBy<ByFuturesOwnerKey>(CFuturesCScriptKey{key.owner, key.height, key.txn})) {
        return Res::Err("Failed to erase futures");
    }

    return Res::Ok();
}

ResVal<CFuturesUserValue> CFutureBaseView::GetFuturesUserValues(const CFuturesUserKey& key) {
    CFuturesUserValue source;
    if (!ReadBy<ByFuturesSwapKey>(key, source)) {
        return Res::Err("Failed to read futures source");
    }

    return {source, Res::Ok()};
}

void CFutureBaseView::ForEachFuturesCScript(std::function<bool(const CFuturesCScriptKey&, const std::string&)> callback, const CFuturesCScriptKey& start)
{
    ForEach<ByFuturesOwnerKey, CFuturesCScriptKey, std::string>(callback, start);
}

std::unique_ptr<CFutureSwapView> pfutureSwapView;
