// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/undos.h>

void CUndosBaseView::ForEachUndo(std::function<bool(UndoKey const &, CLazySerialize<CUndo>)> callback, UndoKey const & start)
{
    ForEach<ByUndoKey, UndoKey, CUndo>(callback, start);
}

Res CUndosBaseView::SetUndo(UndoKey const & key, CUndo const & undo)
{
    if (!undo.before.empty()) {
        WriteBy<ByUndoKey>(key, undo);
    }
    return Res::Ok();
}

Res CUndosBaseView::DelUndo(UndoKey const & key)
{
    EraseBy<ByUndoKey>(key);
    return Res::Ok();
}

std::optional<CUndo> CUndosBaseView::GetUndo(UndoKey const & key) const
{
    CUndo val;
    bool ok = ReadBy<ByUndoKey>(key, val);
    if (ok) {
        return val;
    }
    return {};
}

void CUndosBaseView::AddUndo(CStorageView & cache, uint256 const & txid, uint32_t height)
{
    auto flushable = cache.GetStorage().GetFlushableStorage();
    assert(flushable);
    SetUndo({height, txid}, CUndo::Construct(GetStorage(), flushable->GetRaw()));
}

void CUndosBaseView::OnUndoTx(uint256 const & txid, uint32_t height)
{
    const auto undo = GetUndo(UndoKey{height, txid});
    if (!undo) {
        return; // not custom tx, or no changes done
    }
    CUndo::Revert(GetStorage(), *undo); // revert the changes of this tx
    DelUndo(UndoKey{height, txid}); // erase undo data, it served its purpose
}

void CUndosView::ForEachUndo(std::function<bool(const UndoSourceKey &, CLazySerialize<CUndo>)> callback, const UndoSourceKey& start)
{
    ForEach<ByMultiUndoKey, UndoSourceKey, CUndo>(callback, start);
}

void CUndosView::AddUndo(const UndoSource key, CStorageView & source, CStorageView & cache, uint256 const & txid, uint32_t height)
{
    LogPrintf("XXX key %d txid %d height %d\n", key, txid.ToString(), height);
    auto flushable = cache.GetStorage().GetFlushableStorage();
    assert(flushable);
    SetUndo({height, txid, key}, CUndo::Construct(source.GetStorage(), flushable->GetRaw()));
}

Res CUndosView::SetUndo(const UndoSourceKey& key, const CUndo& undo)
{
    if (!undo.before.empty()) {
        WriteBy<ByMultiUndoKey>(key, undo);
    }
    return Res::Ok();
}

void CUndosView::OnUndoTx(const UndoSource key, CStorageView & source, uint256 const & txid, uint32_t height)
{
    const auto undo = GetUndo({height, txid, key});
    if (!undo) {
        return; // not custom tx, or no changes done
    }
    CUndo::Revert(source.GetStorage(), *undo); // revert the changes of this tx
    DelUndo({height, txid, key}); // erase undo data, it served its purpose
}

std::optional<CUndo> CUndosView::GetUndo(UndoSourceKey const & key) const
{
    CUndo val{};
    ReadBy<ByMultiUndoKey>(key, val);
    return val;
}

Res CUndosView::DelUndo(const UndoSourceKey & key)
{
    EraseBy<ByMultiUndoKey>(key);
    return Res::Ok();
}

bool CUndosView::GetDBActive() {
    if (dbActive) {
        return *dbActive;
    }

    bool active{};
    Read(ByUndosDbActive::prefix(), active);

    dbActive = active;

    return active;
}

void CUndosView::SetDBActive(bool active) {
    Write(ByUndosDbActive::prefix(), active);

    dbActive = active;
}

std::unique_ptr<CUndosView> pundosView;
