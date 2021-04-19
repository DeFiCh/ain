// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/accounts.h>
#include <masternodes/masternodes.h>
#include <key_io.h>

/// @Note it's in own database
const unsigned char CAccountsHistoryView::ByAccountHistoryKey::prefix = 'h';

void CAccountsHistoryView::ForEachAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start)
{
    ForEach<ByAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>(callback, start);
}

Res CAccountsHistoryView::WriteAccountHistory(const AccountHistoryKey& key, const AccountHistoryValue& value)
{
    WriteBy<ByAccountHistoryKey>(key, value);
    return Res::Ok();
}

Res CAccountsHistoryView::EraseAccountHistory(const AccountHistoryKey& key)
{
    EraseBy<ByAccountHistoryKey>(key);
    return Res::Ok();
}

CAccountHistoryStorage::CAccountHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe))
{
}

CAccountsHistoryWriter::CAccountsHistoryWriter(CCustomCSView & storage, uint32_t height, uint32_t txn, const uint256& txid, uint8_t type, CAccountsHistoryView* historyView)
    : CStorageView(new CFlushableStorageKV(storage.GetRaw())), height(height), txn(txn), txid(txid), type(type), historyView(historyView)
{
}

Res CAccountsHistoryWriter::AddBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::AddBalance(owner, amount);
    if (historyView && res.ok && amount.nValue != 0) {
        diffs[owner][amount.nTokenId] += amount.nValue;
    }
    return res;
}

Res CAccountsHistoryWriter::SubBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::SubBalance(owner, amount);
    if (historyView && res.ok && amount.nValue != 0) {
        diffs[owner][amount.nTokenId] -= amount.nValue;
    }
    return res;
}

bool CAccountsHistoryWriter::Flush()
{
    if (historyView) {
        for (const auto& diff : diffs) {
            historyView->WriteAccountHistory({diff.first, height, txn}, {txid, type, diff.second});
        }
    }
    return CCustomCSView::Flush();
}

CAccountsHistoryEraser::CAccountsHistoryEraser(CCustomCSView & storage, uint32_t height, uint32_t txn, CAccountsHistoryView* historyView)
    : CStorageView(new CFlushableStorageKV(storage.GetRaw())), height(height), txn(txn), historyView(historyView)
{
}

Res CAccountsHistoryEraser::AddBalance(CScript const & owner, CTokenAmount)
{
    if (historyView) {
        accounts.insert(owner);
    }
    return Res::Ok();
}

Res CAccountsHistoryEraser::SubBalance(CScript const & owner, CTokenAmount)
{
    if (historyView) {
        accounts.insert(owner);
    }
    return Res::Ok();
}

bool CAccountsHistoryEraser::Flush()
{
    if (historyView) {
        for (const auto& account : accounts) {
            historyView->EraseAccountHistory({account, height, txn});
        }
    }
    return CCustomCSView::Flush();
}

std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
