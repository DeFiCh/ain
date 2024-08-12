// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_ACCOUNTSHISTORY_H
#define DEFI_DFI_ACCOUNTSHISTORY_H

#include <amount.h>
#include <dfi/auctionhistory.h>
#include <dfi/masternodes.h>
#include <flushablestorage.h>
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
                               uint32_t height = std::numeric_limits<uint32_t>::max(),
                               uint32_t txn = std::numeric_limits<uint32_t>::max());

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
    CAccountHistoryStorage(const fs::path &dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false);

    explicit CAccountHistoryStorage(std::shared_ptr<CDBWrapper> &db,
                                    std::unique_ptr<CCheckedOutSnapshot> &otherSnapshot);

    CStorageLevelDB &GetStorage() { return static_cast<CStorageLevelDB &>(DB()); }
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
    CHistoryWriters &writers;

public:
    uint256 vaultID;

    CAccountsHistoryWriter(CCustomCSView &storage, uint32_t height, uint32_t txn, const uint256 &txid, uint8_t type);
    ~CAccountsHistoryWriter() override;
    Res AddBalance(const CScript &owner, CTokenAmount amount) override;
    Res SubBalance(const CScript &owner, CTokenAmount amount) override;
    Res AddVaultCollateral(const CVaultId &vaultId, CTokenAmount amount) override;
    Res SubVaultCollateral(const CVaultId &vaultId, CTokenAmount amount) override;
    bool Flush() override;
    CHistoryWriters &GetHistoryWriters() override { return writers; }
};

extern std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
extern std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;

static constexpr bool DEFAULT_ACINDEX = true;
static constexpr bool DEFAULT_SNAPSHOT = true;

#endif  // DEFI_DFI_ACCOUNTSHISTORY_H
