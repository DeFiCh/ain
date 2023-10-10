// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_UNDOS_H
#define DEFI_DFI_UNDOS_H

#include <dfi/res.h>
#include <dfi/undo.h>
#include <flushablestorage.h>

class CUndosView : public virtual CStorageView {
public:
    void ForEachUndo(std::function<bool(const UndoKey &, CLazySerialize<CUndo>)> callback, const UndoKey &start = {});

    std::optional<CUndo> GetUndo(const UndoKey &key) const;
    Res SetUndo(const UndoKey &key, const CUndo &undo);
    Res DelUndo(const UndoKey &key);

    // tags
    struct ByUndoKey {
        static constexpr uint8_t prefix() { return 'u'; }
    };
};

#endif  // DEFI_DFI_UNDOS_H
