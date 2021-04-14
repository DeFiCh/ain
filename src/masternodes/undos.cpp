// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/undos.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char CUndosView::ByUndoKey::prefix = 'u';

void CUndosView::ForEachUndo(std::function<bool(UndoKey const &, CLazySerialize<CUndo>)> callback, UndoKey const & start)
{
    ForEach<ByUndoKey, UndoKey, CUndo>(callback, start);
}

Res CUndosView::SetUndo(UndoKey const & key, CUndo const & undo)
{
    WriteBy<ByUndoKey>(key, undo);
    return Res::Ok();
}

Res CUndosView::DelUndo(UndoKey const & key)
{
    EraseBy<ByUndoKey>(key);
    return Res::Ok();
}

boost::optional<CUndo> CUndosView::GetUndo(UndoKey const & key) const
{
    CUndo val;
    bool ok = ReadBy<ByUndoKey>(key, val);
    if (ok) {
        return val;
    }
    return {};
}
