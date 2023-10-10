// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_UNDO_H
#define DEFI_MASTERNODES_UNDO_H

#include <flushablestorage.h>
#include <serialize.h>
#include <serialize_optional.h>
#include <uint256.h>
#include <cstdint>

struct UndoKey {
    uint32_t height;  // height is there to be able to prune older undos using lexicographic iteration
    uint256 txid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(WrapBigEndian(height));
        READWRITE(txid);
    }
};

struct CUndo {
    MapKV before;

    static CUndo Construct(const CStorageKV &before, const MapKV &diff) {
        CUndo result;
        for (const auto &kv : diff) {
            const auto &beforeKey = kv.first;
            TBytes beforeVal;
            if (before.Read(beforeKey, beforeVal)) {
                result.before[beforeKey] = std::move(beforeVal);
            } else {
                result.before[beforeKey] = {};
            }
        }
        return result;
    }

    static void Revert(CStorageKV &after, const CUndo &undo) {
        for (const auto &kv : undo.before) {
            if (kv.second) {
                after.Write(kv.first, *kv.second);
            } else {
                after.Erase(kv.first);
            }
        }
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(before);
    }
};

#endif  // DEFI_MASTERNODES_UNDO_H
