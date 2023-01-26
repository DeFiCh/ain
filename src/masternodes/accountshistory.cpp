// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <masternodes/accounts.h>
#include <masternodes/accountshistory.h>
#include <masternodes/historywriter.h>
#include <masternodes/vaulthistory.h>

static AccountHistoryKeyNew Convert(const AccountHistoryKey &key) {
    return {key.blockHeight, key.owner, key.txn};
}

static AccountHistoryKey Convert(const AccountHistoryKeyNew &key) {
    return {key.owner, key.blockHeight, key.txn};
}

void CAccountsHistoryView::CreateMultiIndexIfNeeded() {
    AccountHistoryKeyNew anyNewKey{~0u, {}, ~0u};
    if (auto it = LowerBound<ByAccountHistoryKeyNew>(anyNewKey); it.Valid()) {
        return;
    }

    LogPrintf("Adding multi index in progress...\n");

    auto startTime = GetTimeMillis();

    AccountHistoryKey startKey{{}, ~0u, ~0u};
    auto it = LowerBound<ByAccountHistoryKey>(startKey);
    for (; it.Valid(); it.Next()) {
        WriteBy<ByAccountHistoryKeyNew>(Convert(it.Key()), '\0');
    }

    Flush();

    LogPrint(BCLog::BENCH, "    - Multi index took: %dms\n", GetTimeMillis() - startTime);
}

void CAccountsHistoryView::ForEachAccountHistory(
    std::function<bool(const AccountHistoryKey &, AccountHistoryValue)> callback,
    const CScript &owner,
    uint32_t height,
    uint32_t txn) {
    if (!owner.empty()) {
        ForEach<ByAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>(callback, {owner, height, txn});
        return;
    }

    ForEach<ByAccountHistoryKeyNew, AccountHistoryKeyNew, char>(
        [&](const AccountHistoryKeyNew &newKey, char) {
            auto key   = Convert(newKey);
            auto value = ReadAccountHistory(key);
            assert(value);
            return callback(key, *value);
        },
        {height, owner, txn});
}

std::optional<AccountHistoryValue> CAccountsHistoryView::ReadAccountHistory(const AccountHistoryKey &key) const {
    return ReadBy<ByAccountHistoryKey, AccountHistoryValue>(key);
}

void CAccountsHistoryView::WriteAccountHistory(const AccountHistoryKey &key, const AccountHistoryValue &value) {
    WriteBy<ByAccountHistoryKey>(key, value);
    WriteBy<ByAccountHistoryKeyNew>(Convert(key), '\0');
}

Res CAccountsHistoryView::EraseAccountHistory(const AccountHistoryKey &key) {
    EraseBy<ByAccountHistoryKey>(key);
    EraseBy<ByAccountHistoryKeyNew>(Convert(key));
    return Res::Ok();
}

Res CAccountsHistoryView::EraseAccountHistoryHeight(uint32_t height) {
    std::vector<AccountHistoryKey> keysToDelete;

    auto it = LowerBound<ByAccountHistoryKeyNew>(AccountHistoryKeyNew{height, {}, ~0u});
    for (; it.Valid() && it.Key().blockHeight == height; it.Next()) {
        keysToDelete.push_back(Convert(it.Key()));
    }

    for (const auto &key : keysToDelete) {
        EraseAccountHistory(key);
    }
    return Res::Ok();
}

CAccountHistoryStorage::CAccountHistoryStorage(const fs::path &dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe)) {}

CBurnHistoryStorage::CBurnHistoryStorage(const fs::path &dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe)) {}

CAccountsHistoryWriter::CAccountsHistoryWriter(CCustomCSView &storage,
                                               uint32_t height,
                                               uint32_t txn,
                                               const uint256 &txid,
                                               uint8_t type)
    : CStorageView(new CFlushableStorageKV(static_cast<CStorageKV &>(storage.GetStorage()))),
      height(height),
      txn(txn),
      txid(txid),
      type(type),
      writers(storage.GetHistoryWriters()) {
}

CAccountsHistoryWriter::~CAccountsHistoryWriter() {
    writers.ClearState();
}

Res CAccountsHistoryWriter::AddBalance(const CScript &owner, CTokenAmount amount) {
    auto res = CCustomCSView::AddBalance(owner, amount);
    if (amount.nValue != 0 && res.ok) {
        writers.AddBalance(owner, amount, vaultID);
    }

    return res;
}

Res CAccountsHistoryWriter::SubBalance(const CScript &owner, CTokenAmount amount) {
    auto res = CCustomCSView::SubBalance(owner, amount);
    if (res.ok && amount.nValue != 0) {
        writers.SubBalance(owner, amount, vaultID);
    }

    return res;
}

bool CAccountsHistoryWriter::Flush() {
    writers.Flush(height, txid, txn, type, vaultID);
    return CCustomCSView::Flush();
}

std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;
