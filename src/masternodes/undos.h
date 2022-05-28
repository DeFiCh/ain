// Copyright (c) Bitcoin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_UNDOS_H
#define BITCOIN_MASTERNODES_UNDOS_H

#include <masternodes/undo.h>
#include <flushablestorage.h>
#include <masternodes/res.h>

class CUndosView : public virtual CStorageView {
public:
    void ForEachUndo(std::function<bool(UndoKey const &, CLazySerialize<CUndo>)> callback, UndoKey const & start = {});

    boost::optional<CUndo> GetUndo(UndoKey const & key) const;
    Res SetUndo(UndoKey const & key, CUndo const & undo);
    Res DelUndo(UndoKey const & key);

    // tags
    struct ByUndoKey { static constexpr uint8_t prefix() { return 'u'; } };
};


#endif //BITCOIN_MASTERNODES_UNDOS_H
