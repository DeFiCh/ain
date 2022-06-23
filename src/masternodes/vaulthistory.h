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
    uint32_t blockHeight;
    uint256 vaultID;
    uint32_t txn; // for order in block
    CScript address;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        }
        else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }
        READWRITE(vaultID);
        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(txn));
            txn = ~txn;
        }
        else {
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
    TAmounts collaterals;
    CCollateralLoans collateralsValues;
    std::vector<CAuctionBatch> auctionBatches;
    uint32_t ratio;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(collaterals);
        READWRITE(collateralsValues);
        READWRITE(auctionBatches);
        READWRITE(ratio);
    }
};

using VaultSchemeKey = VaultStateKey;

struct VaultSchemeValue {
    unsigned char category;
    uint256 txid;
    std::string schemeID;
    uint32_t txn; // For looking up global scheme from specific place in the block

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(category);
        READWRITE(txid);
        READWRITE(schemeID);
        READWRITE(txn);
    }
};

struct VaultGlobalSchemeKey {
    uint32_t blockHeight;
    uint32_t txn;
    uint256 schemeCreationTxid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {

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

        READWRITE(schemeCreationTxid);
    }
};

struct VaultGlobalSchemeValue {
    CLoanScheme loanScheme;
    unsigned char category;
    uint256 txid;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(loanScheme);
        READWRITE(category);
        READWRITE(txid);
    }
};

class CVaultHistoryView : public virtual CStorageView
{
public:
    void WriteVaultHistory(VaultHistoryKey const & key, VaultHistoryValue const & value);
    void WriteVaultScheme(VaultSchemeKey const & key, const VaultSchemeValue& value);
    void WriteVaultState(CCustomCSView& mnview, const CBlockIndex& pindex, const uint256& vaultID, const uint32_t ratio = 0);

    void EraseVaultHistory(const uint32_t height);

    void ForEachVaultHistory(std::function<bool(VaultHistoryKey const &, CLazySerialize<VaultHistoryValue>)> callback, VaultHistoryKey const & start = {});
    void ForEachVaultScheme(std::function<bool(VaultSchemeKey const &, CLazySerialize<VaultSchemeValue>)> callback, VaultSchemeKey const & start = {});
    void ForEachVaultState(std::function<bool(VaultStateKey const &, CLazySerialize<VaultStateValue>)> callback, VaultStateKey const & start = {});

    // Loan Scheme storage
    void WriteGlobalScheme(VaultGlobalSchemeKey const & key, const VaultGlobalSchemeValue& value);
    void EraseGlobalScheme(const VaultGlobalSchemeKey& key);
    void ForEachGlobalScheme(std::function<bool(VaultGlobalSchemeKey const &, CLazySerialize<VaultGlobalSchemeValue>)> callback, VaultGlobalSchemeKey const & start = {});

    struct ByVaultHistoryKey { static constexpr uint8_t prefix() { return 0x01; } };
    struct ByVaultStateKey { static constexpr uint8_t prefix() { return 0x02; } };
    struct ByVaultSchemeKey { static constexpr uint8_t prefix() { return 0x03; } };
    struct ByVaultGlobalSchemeKey { static constexpr uint8_t prefix() { return 0x04; } };
};

class CVaultHistoryStorage : public CVaultHistoryView
{
public:
    CVaultHistoryStorage(CVaultHistoryStorage& vaultHistory) : CStorageView(new CFlushableStorageKV(vaultHistory.DB())) {}
    CVaultHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

extern std::unique_ptr<CVaultHistoryStorage> pvaultHistoryDB;

static constexpr bool DEFAULT_VAULTINDEX = false;

#endif //DEFI_MASTERNODES_VAULTHISTORY_H