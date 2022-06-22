// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ACCOUNTSHISTORY_H
#define DEFI_MASTERNODES_ACCOUNTSHISTORY_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/auctionhistory.h>
#include <masternodes/masternodes.h>
#include <script/script.h>
#include <uint256.h>

class CVaultHistoryView;
class CVaultHistoryStorage;

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
    std::optional<AccountHistoryValue> ReadAccountHistory(AccountHistoryKey const & key) const;
    Res EraseAccountHistory(AccountHistoryKey const & key);
    void ForEachAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start = {});

    // tags
    struct ByAccountHistoryKey { static constexpr uint8_t prefix() { return 'h'; } };
};

class CAccountHistoryStorage : public CAccountsHistoryView
                             , public CAuctionHistoryView
{
public:
    CAccountHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

class CBurnHistoryStorage : public CAccountsHistoryView
{
public:
    CBurnHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

class CHistoryWriters {
    CAccountHistoryStorage* historyView;
    CBurnHistoryStorage* burnView;
    std::map<CScript, TAmounts> diffs;
    std::map<CScript, TAmounts> burnDiffs;
    std::map<uint256, std::map<CScript,TAmounts>> vaultDiffs;

public:
    CVaultHistoryStorage* vaultView;
    CLoanSchemeCreation globalLoanScheme;
    std::string schemeID;

    CHistoryWriters(CAccountHistoryStorage* historyView, CBurnHistoryStorage* burnView, CVaultHistoryStorage* vaultView);

    void AddBalance(const CScript& owner, const CTokenAmount amount, const uint256& vaultID);
    void AddFeeBurn(const CScript& owner, const CAmount amount);
    void SubBalance(const CScript& owner, const CTokenAmount amount, const uint256& vaultID);
    void Flush(const uint32_t height, const uint256& txid, const uint32_t txn, const uint8_t type, const uint256& vaultID);
};

class CHistoryErasers {
    CAccountHistoryStorage* historyView;
    CBurnHistoryStorage* burnView;
    std::set<CScript> accounts;
    std::set<CScript> burnAccounts;
    std::set<uint256> vaults;

public:
    CVaultHistoryStorage* vaultView;
    bool removeLoanScheme{false};
    uint256 schemeCreationTxid;

    CHistoryErasers(CAccountHistoryStorage* historyView, CBurnHistoryStorage* burnView, CVaultHistoryStorage* vaultView);

    void AddBalance(const CScript& owner, const uint256& vaultID);
    void SubFeeBurn(const CScript& owner);
    void SubBalance(const CScript& owner, const uint256& vaultID);
    void Flush(const uint32_t height, const uint32_t txn, const uint256& vaultID);
};

class CAccountsHistoryWriter : public CCustomCSView
{
    const uint32_t height;
    const uint32_t txn;
    const uint256 txid;
    const uint8_t type;
    CHistoryWriters* writers;

public:
    uint256 vaultID;

    CAccountsHistoryWriter(CCustomCSView & storage, uint32_t height, uint32_t txn, const uint256& txid, uint8_t type, CHistoryWriters* writers);
    Res AddBalance(CScript const & owner, CTokenAmount amount) override;
    Res SubBalance(CScript const & owner, CTokenAmount amount) override;
    bool Flush();
};

class CAccountsHistoryEraser : public CCustomCSView
{
    const uint32_t height;
    const uint32_t txn;
    CHistoryErasers& erasers;

public:
    uint256 vaultID;

    CAccountsHistoryEraser(CCustomCSView & storage, uint32_t height, uint32_t txn, CHistoryErasers& erasers);
    Res AddBalance(CScript const & owner, CTokenAmount amount) override;
    Res SubBalance(CScript const & owner, CTokenAmount amount) override;
    bool Flush();
};

extern std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
extern std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;

static constexpr bool DEFAULT_ACINDEX = true;

#endif //DEFI_MASTERNODES_ACCOUNTSHISTORY_H
