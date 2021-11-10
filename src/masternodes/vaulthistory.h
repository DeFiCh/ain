// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_VAULTHISTORY_H
#define DEFI_MASTERNODES_VAULTHISTORY_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/masternodes.h>
#include <masternodes/vault.h>
#include <script/script.h>

struct VaultHistoryKey {
    uint256 vaultID;
    uint32_t blockHeight;
    uint32_t txn; // for order in block
    CScript address;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vaultID);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
            READWRITE(WrapBigEndian(txn));
            txn = ~txn;
        }
        else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
            uint32_t txn_ = ~txn;
            READWRITE(WrapBigEndian(txn_));
        }

        READWRITE(address);
    }
};

struct VaultHistoryValue {
    uint256 txid;
    unsigned char category;
    TAmounts diff;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(category);
        READWRITE(diff);
    }
};

class CVaultHistoryView : public virtual CStorageView
{
public:
    Res WriteVaultHistory(VaultHistoryKey const & key, VaultHistoryValue const & value);
    Res EraseVaultHistory(const VaultHistoryKey& key);
    void ForEachVaultHistory(std::function<bool(VaultHistoryKey const &, CLazySerialize<VaultHistoryValue>)> callback, VaultHistoryKey const & start = {});

    struct ByVaultHistoryKey { static constexpr uint8_t prefix() { return 0x01; } };
};

class CVaultHistoryStorage : public CVaultHistoryView
{
public:
    CVaultHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

extern std::unique_ptr<CVaultHistoryStorage> pvaultHistoryDB;

static constexpr bool DEFAULT_VAULTINDEX = true;

#endif //DEFI_MASTERNODES_VAULTHISTORY_H