// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_UNDOS_H
#define DEFI_MASTERNODES_UNDOS_H

#include <masternodes/undo.h>
#include <flushablestorage.h>
#include <masternodes/res.h>

class CUndosView : public virtual CStorageView {
public:
    void ForEachUndo(std::function<bool(UndoKey, CLazySerialize<CUndo>)> callback, UndoKey const & start = {}) const;

    boost::optional<CUndo> GetUndo(UndoKey key) const;
    Res SetUndo(UndoKey key, CUndo const & undo);
    Res DelUndo(UndoKey key);

    // tags
    struct ByUndoKey { static const unsigned char prefix; };
};


#endif //DEFI_MASTERNODES_UNDOS_H
