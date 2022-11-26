// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/undos.h>

void CUndosView::ForEachUndo(std::function<bool(const UndoKey &, CLazySerialize<CUndo>)> callback,
                             const UndoKey &start) {
    ForEach<ByUndoKey, UndoKey, CUndo>(callback, start);
}

Res CUndosView::SetUndo(const UndoKey &key, const CUndo &undo) {
    WriteBy<ByUndoKey>(key, undo);
    return Res::Ok();
}

Res CUndosView::DelUndo(const UndoKey &key) {
    EraseBy<ByUndoKey>(key);
    return Res::Ok();
}

std::optional<CUndo> CUndosView::GetUndo(const UndoKey &key) const {
    CUndo val;
    bool ok = ReadBy<ByUndoKey>(key, val);
    if (ok) {
        return val;
    }
    return {};
}
