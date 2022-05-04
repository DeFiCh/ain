// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_UNDOS_H
#define DEFI_MASTERNODES_UNDOS_H

#include <masternodes/undo.h>
#include <flushablestorage.h>
#include <masternodes/res.h>

class CUndosBaseView : public virtual CStorageView {
public:
    CUndosBaseView() = default;
    CUndosBaseView(CUndosBaseView& other) = default;

    void ForEachUndo(std::function<bool(UndoKey const &, CLazySerialize<CUndo>)> callback, UndoKey const & start = {});

    Res DelUndo(const UndoKey & key);

    // tags
    struct ByUndoKey { static constexpr uint8_t prefix() { return 'u'; } };
};

class CUndosView : public CUndosBaseView
{
public:
    explicit CUndosView(std::shared_ptr<CStorageKV> st) : CStorageView(st) {}

    void ForEachUndo(std::function<bool(const UndoSourceKey &, CLazySerialize<CUndo>)> callback, const UndoSourceKey& start = {});

    [[nodiscard]] std::optional<CUndo> GetUndo(UndoSourceKey const & key) const;
    Res SetUndo(const UndoSourceKey& key, const CUndo& undo);
    Res DelUndo(const UndoSourceKey & key);

    void AddUndo(const UndoSource key, CStorageView & source, CStorageView & cache, uint256 const & txid, uint32_t height);
    void OnUndoTx(const UndoSource key, CStorageView & source, uint256 const & txid, uint32_t height);

    // tags
    struct ByMultiUndoKey { static constexpr uint8_t prefix() { return 'n'; } };

private:
    std::optional<bool> dbActive;
};

extern std::unique_ptr<CUndosView> pundosView;

#endif //DEFI_MASTERNODES_UNDOS_H
