// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/undos.h>

void CUndosBaseView::ForEachUndo(std::function<bool(UndoKey const &, CLazySerialize<CUndo>)> callback, UndoKey const & start)
{
    ForEach<ByUndoKey, UndoKey, CUndo>(callback, start);
}

Res CUndosBaseView::DelUndo(UndoKey const & key)
{
    EraseBy<ByUndoKey>(key);
    return Res::Ok();
}

void CUndosView::ForEachUndo(std::function<bool(const UndoSourceKey &, CLazySerialize<CUndo>)> callback, const UndoSourceKey& start)
{
    ForEach<ByMultiUndoKey, UndoSourceKey, CUndo>(callback, start);
}

void CUndosView::AddUndo(const UndoSource key, CStorageView & source, CStorageView & cache, uint256 const & txid, uint32_t height)
{
    auto& flushable = cache.GetStorage();
    auto& rawMap = flushable.GetRaw();
    if (!rawMap.empty()) {
        SetUndo({{height, txid}, key}, CUndo::Construct(source.GetStorage(), rawMap));
    }
}

Res CUndosView::SetUndo(UndoSourceKey const & key, CUndo const & undo)
{
    WriteBy<ByMultiUndoKey>(key, undo);
    return Res::Ok();
}

void CUndosView::OnUndoTx(const UndoSource key, CStorageView & source, uint256 const & txid, uint32_t height)
{
    const auto undo = GetUndo({{height, txid}, key});
    if (!undo) {
        return; // not custom tx, or no changes done
    }
    CUndo::Revert(source.GetStorage(), *undo); // revert the changes of this tx
    DelUndo({{height, txid}, key}); // erase undo data, it served its purpose
}

std::optional<CUndo> CUndosView::GetUndo(UndoSourceKey const & key) const
{
    CUndo val;
    if (ReadBy<ByMultiUndoKey>(key, val)) {
        return val;
    }
    return {};
}

Res CUndosView::DelUndo(const UndoSourceKey & key)
{
    EraseBy<ByMultiUndoKey>(key);
    return Res::Ok();
}

std::unique_ptr<CStorageLevelDB> pundosDB;
std::unique_ptr<CUndosView> pundosView;
