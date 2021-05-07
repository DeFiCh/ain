// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ACCOUNTSHISTORY_H
#define DEFI_MASTERNODES_ACCOUNTSHISTORY_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/masternodes.h>
#include <script/script.h>
#include <uint256.h>

struct AccountHistoryKey {
    CScript owner;
    uint32_t blockHeight;
    uint32_t txn; // for order in block

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(owner);

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
    }
};

struct AccountHistoryValue {
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

class CAccountsHistoryView : public virtual CStorageView
{
public:
    Res WriteAccountHistory(AccountHistoryKey const & key, AccountHistoryValue const & value);
    Res EraseAccountHistory(AccountHistoryKey const & key);
    void ForEachAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start = {});

    // tags
    struct ByAccountHistoryKey { static const unsigned char prefix; };
};

class CAccountHistoryStorage : public CAccountsHistoryView
{
public:
    CAccountHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

class CBurnHistoryStorage : public CAccountsHistoryView
{
public:
    CBurnHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

class CAccountsHistoryWriter : public CCustomCSView
{
    const uint32_t height;
    const uint32_t txn;
    const uint256 txid;
    const uint8_t type;
    std::map<CScript, TAmounts> diffs;
    std::map<CScript, TAmounts> burnDiffs;
    CAccountsHistoryView* historyView;
    CAccountsHistoryView* burnView;

public:
    CAccountsHistoryWriter(CCustomCSView & storage, uint32_t height, uint32_t txn, const uint256& txid, uint8_t type, CAccountsHistoryView* historyView, CAccountsHistoryView* burnView);
    Res AddBalance(CScript const & owner, CTokenAmount amount) override;
    Res SubBalance(CScript const & owner, CTokenAmount amount) override;
    Res AddFeeBurn(CScript const & owner, CAmount amount);
    bool Flush();
};

class CAccountsHistoryEraser : public CCustomCSView
{
    const uint32_t height;
    const uint32_t txn;
    std::set<CScript> accounts;
    std::set<CScript> burnAccounts;
    CAccountsHistoryView* historyView;
    CAccountsHistoryView* burnView;

public:
    CAccountsHistoryEraser(CCustomCSView & storage, uint32_t height, uint32_t txn, CAccountsHistoryView* historyView, CAccountsHistoryView* burnView);
    Res AddBalance(CScript const & owner, CTokenAmount amount) override;
    Res SubBalance(CScript const & owner, CTokenAmount amount) override;
    bool Flush();
};

extern std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
extern std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;

static constexpr bool DEFAULT_ACINDEX = true;

#endif //DEFI_MASTERNODES_ACCOUNTSHISTORY_H
