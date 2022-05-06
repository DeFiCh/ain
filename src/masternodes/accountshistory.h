// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ACCOUNTSHISTORY_H
#define DEFI_MASTERNODES_ACCOUNTSHISTORY_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/auctionhistory.h>
#include <masternodes/masternodes.h>
#include <masternodes/vault.h>
#include <script/script.h>
#include <uint256.h>

#include <optional>

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
        READWRITE(WrapBigEndianInv(blockHeight));
        READWRITE(WrapBigEndianInv(txn));
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
    void CreateMultiIndexIfNeeded();
    Res EraseAccountHistoryHeight(uint32_t height);
    std::optional<AccountHistoryValue> ReadAccountHistory(AccountHistoryKey const & key) const;
    Res WriteAccountHistory(AccountHistoryKey const & key, AccountHistoryValue const & value);
    Res EraseAccountHistory(AccountHistoryKey const & key);
    void ForEachAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback,
                               CScript const & owner = {}, uint32_t height = ~0u, uint32_t txn = ~0u);

    // tags
    struct ByAccountHistoryKey    { static constexpr uint8_t prefix() { return 'h'; } };
    struct ByAccountHistoryKeyNew { static constexpr uint8_t prefix() { return 'H'; } };
};

class CAccountHistoryStorage : public CAccountsHistoryView
                             , public CAuctionHistoryView
{
public:
    CAccountHistoryStorage(CAccountHistoryStorage& accountHistory) : CStorageView(accountHistory) {}
    CAccountHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

class CBurnHistoryStorage : public CAccountsHistoryView
{
public:
    CBurnHistoryStorage(CBurnHistoryStorage& burnHistory) : CStorageView(burnHistory) {}
    CBurnHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

class CHistoryWriters {
    CAccountHistoryStorage* historyView;
    CBurnHistoryStorage* burnView;
    CVaultHistoryStorage* vaultView;
    std::string schemeID;
    std::optional<CVaultId> vaultID;
    std::map<CScript, TAmounts> diffs;
    std::map<CScript, TAmounts> burnDiffs;
    std::optional<CLoanSchemeCreation> globalLoanScheme;
    std::map<uint256, std::map<CScript,TAmounts>> vaultDiffs;

public:
    CHistoryWriters(CAccountHistoryStorage* historyView, CBurnHistoryStorage* burnView, CVaultHistoryStorage* vaultView);

    void AddBalance(const CScript& owner, const CTokenAmount amount);
    void AddFeeBurn(const CScript& owner, const CAmount amount);
    void SubBalance(const CScript& owner, const CTokenAmount amount);
    void AddVault(const CVaultId& vaultId, const std::string& schemeId = {});
    void AddLoanScheme(const CLoanSchemeMessage& loanScheme, const uint256& txid, uint32_t height, uint32_t txn);
    void Flush(const uint32_t height, const uint256& txid, const uint32_t txn, const uint8_t type);
};

class CAccountsHistoryWriter : public CCustomCSView
{
    const uint32_t height;
    const uint32_t txn;
    const uint256& txid;
    const uint8_t type;
    CHistoryWriters* writers;

public:
    CAccountsHistoryWriter(CCustomCSView & storage, uint32_t height, uint32_t txn, const uint256& txid, uint8_t type, CHistoryWriters* writers);

    Res AddBalance(CScript const & owner, CTokenAmount amount) override;
    Res SubBalance(CScript const & owner, CTokenAmount amount) override;
    bool Flush(bool sync = false) override;
};

template<typename T, typename... Args>
inline void FlushWriters(std::unique_ptr<T>& writer, Args&... args)
{
    static_assert(std::is_base_of<CStorageView, T>::value, "T should inherit CStorageView");
    if (writer) {
        writer->Flush();
    }
    if constexpr (sizeof...(Args) != 0) {
        FlushWriters(args...);
    }
}

template<typename T, typename... Args>
inline void DiscardWriters(std::unique_ptr<T>& writer, Args&... args)
{
    static_assert(std::is_base_of<CStorageView, T>::value, "T should inherit CStorageView");
    if (writer) {
        writer->Discard();
    }
    if constexpr (sizeof...(Args) != 0) {
        DiscardWriters(args...);
    }
}

extern std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
extern std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;

static constexpr bool DEFAULT_ACINDEX = true;

#endif //DEFI_MASTERNODES_ACCOUNTSHISTORY_H
