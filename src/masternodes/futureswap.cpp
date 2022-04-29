// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/futureswap.h>

#include <masternodes/masternodes.h>

Res CFutureBaseView::StoreFuturesUserValues(const CFuturesUserKey& key, const CFuturesUserValue& futures)
{
    if (!WriteBy<ByFuturesSwapKey>(key, futures)) {
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

ResVal<CFuturesUserValue> CFutureBaseView::GetFuturesUserValues(const CFuturesUserKey& key) {
    CFuturesUserValue source;
    if (!ReadBy<ByFuturesSwapKey>(key, source)) {
        return Res::Err("Failed to read futures source");
    }

    return {source, Res::Ok()};
}

void CFutureSwapView::AddUndo(CFutureBaseView& cache, const uint256& txid, uint32_t height)
{
    auto flushable = cache.GetStorage().GetFlushableStorage();
    assert(flushable);
    SetUndo({height, txid}, CUndo::Construct(GetStorage(), flushable->GetRaw()));
}

void CFutureSwapView::OnUndoTx(const uint256& txid, uint32_t height)
{
    const auto undo = GetUndo(UndoKey{height, txid});
    if (!undo) {
        return;
    }
    CUndo::Revert(GetStorage(), *undo);
    DelUndo(UndoKey{height, txid});
}

bool CFutureSwapView::GetDBActive() {
    if (dbActive) {
        return *dbActive;
    }

    bool active{};
    Read(DbActive::prefix(), active);

    dbActive = active;

    return active;
}

void CFutureSwapView::SetDBActive(bool active) {
    Write(DbActive::prefix(), active);

    dbActive = active;
}

std::unique_ptr<CFutureSwapView> pfutureSwapView;
