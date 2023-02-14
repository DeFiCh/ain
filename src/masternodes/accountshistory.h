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

class CHistoryWriters;
class CVaultHistoryView;
class CVaultHistoryStorage;
struct VaultHistoryKey;
struct VaultHistoryValue;

class CAccountsHistoryView : public virtual CStorageView {
public:
    void CreateMultiIndexIfNeeded();
    Res EraseAccountHistoryHeight(uint32_t height);
    [[nodiscard]] std::optional<AccountHistoryValue> ReadAccountHistory(const AccountHistoryKey &key) const;
    void WriteAccountHistory(const AccountHistoryKey &key, const AccountHistoryValue &value);
    Res EraseAccountHistory(const AccountHistoryKey &key);
    void ForEachAccountHistory(std::function<bool(const AccountHistoryKey &, AccountHistoryValue)> callback,
                               const CScript &owner = {},
                               uint32_t height      = std::numeric_limits<uint32_t>::max(),
                               uint32_t txn         = std::numeric_limits<uint32_t>::max());
    void ForEachAccountHistoryNew(std::function<bool(const AccountHistoryKeyNew &, const AccountHistoryValue &)> callback,
                                  const AccountHistoryKeyNew &start = {std::numeric_limits<uint32_t>::max(),
                                                                       {},
                                                                       std::numeric_limits<uint32_t>::max()});

    // tags
    struct ByAccountHistoryKey {
        static constexpr uint8_t prefix() { return 'h'; }
    };
    struct ByAccountHistoryKeyNew {
        static constexpr uint8_t prefix() { return 'H'; }
    };
};

class CAccountHistoryStorage : public CAccountsHistoryView, public CAuctionHistoryView {
public:
    CAccountHistoryStorage(CAccountHistoryStorage &accountHistory)
        : CStorageView(new CFlushableStorageKV(accountHistory.DB())) {}
    CAccountHistoryStorage(const fs::path &dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

class CBurnHistoryStorage : public CAccountsHistoryView {
public:
    CBurnHistoryStorage(const fs::path &dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);
};

class CAccountsHistoryWriter : public CCustomCSView {
    const uint32_t height;
    const uint32_t txn;
    const uint256 txid;
    const uint8_t type;
    CHistoryWriters& writers;

public:
    uint256 vaultID;

    CAccountsHistoryWriter(CCustomCSView &storage,
                           uint32_t height,
                           uint32_t txn,
                           const uint256 &txid,
                           uint8_t type);
    ~CAccountsHistoryWriter() override;
    Res AddBalance(const CScript &owner, CTokenAmount amount) override;
    Res SubBalance(const CScript &owner, CTokenAmount amount) override;
    Res AddVaultCollateral(const CVaultId &vaultId, CTokenAmount amount) override;
    Res SubVaultCollateral(const CVaultId &vaultId, CTokenAmount amount) override;
    bool Flush() override;
    CHistoryWriters& GetHistoryWriters() override { return writers; }
};

extern std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
extern std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;

static constexpr bool DEFAULT_ACINDEX = true;

#endif  // DEFI_MASTERNODES_ACCOUNTSHISTORY_H
