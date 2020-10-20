// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_UNDOS_H
#define DEFI_MASTERNODES_UNDOS_H

#include <masternodes/undo.h>
#include <flushablestorage.h>
#include <masternodes/res.h>

class CUndosView : public virtual CStorageView {
public:
    void ForEachUndo(std::function<bool(UndoKey key, CUndo const & Undo)> callback, UndoKey start) const;

    boost::optional<CUndo> GetUndo(UndoKey key) const;
    Res SetUndo(UndoKey key, CUndo const & undo);
    Res DelUndo(UndoKey key);

    // tags
    struct ByUndoKey { static const unsigned char prefix; };
};


#endif //DEFI_MASTERNODES_UNDOS_H
