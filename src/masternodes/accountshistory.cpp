// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/accounts.h>
#include <masternodes/masternodes.h>
#include <masternodes/vaulthistory.h>
#include <key_io.h>

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

CBurnHistoryStorage::CBurnHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe))
{
}

CAccountsHistoryWriter::CAccountsHistoryWriter(CCustomCSView & storage, uint32_t height, uint32_t txn, const uint256& txid, uint8_t type,
                                               CAccountsHistoryView* historyView, CAccountsHistoryView* burnView, CVaultHistoryView* vaultView, const uint256& vaultID)
    : CStorageView(new CFlushableStorageKV(static_cast<CStorageKV&>(storage.GetStorage()))), height(height), txn(txn),
    txid(txid), type(type), historyView(historyView), burnView(burnView), vaultView(vaultView), vaultID(vaultID)
{
}

Res CAccountsHistoryWriter::AddBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::AddBalance(owner, amount);
    if (amount.nValue != 0 && res.ok) {
        if (historyView) {
            diffs[owner][amount.nTokenId] += amount.nValue;
        }
        if (burnView && owner == Params().GetConsensus().burnAddress) {
            burnDiffs[owner][amount.nTokenId] += amount.nValue;
        }
        if (vaultView && !vaultID.IsNull()) {
            vaultDiffs[vaultID][owner][amount.nTokenId] += amount.nValue;
        }
    }

    return res;
}

Res CAccountsHistoryWriter::AddFeeBurn(CScript const & owner, CAmount amount)
{
    if (burnView && amount != 0) {
        burnDiffs[owner][DCT_ID{0}] += amount;
    }
    return Res::Ok();
}

Res CAccountsHistoryWriter::SubBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::SubBalance(owner, amount);
    if (res.ok && amount.nValue != 0) {
        if (historyView) {
            diffs[owner][amount.nTokenId] -= amount.nValue;
        }
        if (burnView && owner == Params().GetConsensus().burnAddress) {
            burnDiffs[owner][amount.nTokenId] -= amount.nValue;
        }
        if (vaultView && !vaultID.IsNull()) {
            vaultDiffs[vaultID][owner][amount.nTokenId] -= amount.nValue;
        }
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
    if (burnView) {
        for (const auto& diff : burnDiffs) {
            burnView->WriteAccountHistory({diff.first, height, txn}, {txid, type, diff.second});
        }
    }
    if (vaultView) {
        for (const auto& diff : vaultDiffs) {
            for (const auto& addresses : diff.second) {
                vaultView->WriteVaultHistory({diff.first, height, txn, addresses.first}, {txid, type, addresses.second});
            }
        }
    }
    return CCustomCSView::Flush();
}

CAccountsHistoryEraser::CAccountsHistoryEraser(CCustomCSView & storage, uint32_t height, uint32_t txn, CAccountsHistoryView* historyView,
                                               CAccountsHistoryView* burnView, CVaultHistoryView* vaultView, const uint256& vaultID)
    : CStorageView(new CFlushableStorageKV(static_cast<CStorageKV&>(storage.GetStorage()))), height(height), txn(txn),
    historyView(historyView), burnView(burnView), vaultView(vaultView),  vaultID(vaultID)
{
}

Res CAccountsHistoryEraser::AddBalance(CScript const & owner, CTokenAmount)
{
    if (historyView) {
        accounts.insert(owner);
    }
    if (burnView && owner == Params().GetConsensus().burnAddress) {
        burnAccounts.insert(owner);
    }
    if (vaultView && !vaultID.IsNull()) {
        vaults.insert(vaultID);
    }
    return Res::Ok();
}

Res CAccountsHistoryEraser::SubBalance(CScript const & owner, CTokenAmount)
{
    if (historyView) {
        accounts.insert(owner);
    }
    if (burnView && owner == Params().GetConsensus().burnAddress) {
        burnAccounts.insert(owner);
    }
    if (vaultView && !vaultID.IsNull()) {
        vaults.insert(vaultID);
    }
    return Res::Ok();
}

Res CAccountsHistoryEraser::SubFeeBurn(CScript const & owner)
{
    if (burnView) {
        burnAccounts.insert(owner);
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
    if (burnView) {
        for (const auto& account : burnAccounts) {
            burnView->EraseAccountHistory({account, height, txn});
        }
    }
    if (vaultView) {
        for (const auto& vault : vaults) {
            vaultView->EraseVaultHistory({vault, height, txn});
        }
    }
    return Res::Ok(); // makes sure no changes are applyed to underlaying view
}

std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;
