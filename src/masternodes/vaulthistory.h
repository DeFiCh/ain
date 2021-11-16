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

struct VaultStateKey {
    uint256 vaultID;
    uint32_t blockHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vaultID);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        }
        else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }
    }
};

struct VaultStateValue {
    bool isUnderLiquidation;
    TAmounts collaterals;
    CCollateralLoans collateralsValues;
    std::vector<CAuctionBatch> auctionBatches;
    uint32_t ratio;
    std::string schemeID;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(isUnderLiquidation);
        READWRITE(collaterals);
        READWRITE(collateralsValues);
        READWRITE(auctionBatches);
        READWRITE(ratio);
        READWRITE(schemeID);
    }
};

class CVaultHistoryView : public virtual CStorageView
{
public:
    void WriteVaultHistory(VaultHistoryKey const & key, VaultHistoryValue const & value);
    void WriteVaultState(CCustomCSView& mnview, const CBlockIndex& pindex, const uint256& vaultID, const uint32_t ratio = 0);

    void EraseVaultHistory(const VaultHistoryKey& key);
    void EraseVaultState(const uint32_t height);

    void ForEachVaultHistory(std::function<bool(VaultHistoryKey const &, CLazySerialize<VaultHistoryValue>)> callback, VaultHistoryKey const & start = {});
    void ForEachVaultState(std::function<bool(VaultStateKey const &, CLazySerialize<VaultStateValue>)> callback, VaultStateKey const & start = {});

    struct ByVaultHistoryKey { static constexpr uint8_t prefix() { return 0x01; } };
    struct ByVaultStateKey { static constexpr uint8_t prefix() { return 0x02; } };
};

class CVaultHistoryStorage : public CVaultHistoryView
{
public:
    CVaultHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

extern std::unique_ptr<CVaultHistoryStorage> pvaultHistoryDB;

static constexpr bool DEFAULT_VAULTINDEX = false;

#endif //DEFI_MASTERNODES_VAULTHISTORY_H