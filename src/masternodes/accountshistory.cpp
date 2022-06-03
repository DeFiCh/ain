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

std::optional<AccountHistoryValue> CAccountsHistoryView::ReadAccountHistory(AccountHistoryKey const & key) const
{
    return ReadBy<ByAccountHistoryKey, AccountHistoryValue>(key);
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
                                               CHistoryWriters* writers)
    : CStorageView(new CFlushableStorageKV(static_cast<CStorageKV&>(storage.GetStorage()))), height(height), txn(txn),
    txid(txid), type(type), writers(writers)
{
}

Res CAccountsHistoryWriter::AddBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::AddBalance(owner, amount);
    if (writers && amount.nValue != 0 && res.ok) {
        writers->AddBalance(owner, amount, vaultID);
    }

    return res;
}

Res CAccountsHistoryWriter::SubBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::SubBalance(owner, amount);
    if (writers && res.ok && amount.nValue != 0) {
        writers->SubBalance(owner, amount, vaultID);
    }

    return res;
}

bool CAccountsHistoryWriter::Flush()
{
    if (writers) {
        writers->Flush(height, txid, txn, type, vaultID);
    }
    return CCustomCSView::Flush();
}

CAccountsHistoryEraser::CAccountsHistoryEraser(CCustomCSView & storage, uint32_t height, uint32_t txn, CHistoryErasers& erasers)
    : CStorageView(new CFlushableStorageKV(static_cast<CStorageKV&>(storage.GetStorage()))), height(height), txn(txn), erasers(erasers)
{
}

Res CAccountsHistoryEraser::AddBalance(CScript const & owner, CTokenAmount)
{
    erasers.AddBalance(owner, vaultID);
    return Res::Ok();
}

Res CAccountsHistoryEraser::SubBalance(CScript const & owner, CTokenAmount)
{
    erasers.SubBalance(owner, vaultID);
    return Res::Ok();
}

bool CAccountsHistoryEraser::Flush()
{
    erasers.Flush(height, txn, vaultID);
    return Res::Ok(); // makes sure no changes are applyed to underlaying view
}

CHistoryErasers::CHistoryErasers(CAccountHistoryStorage* historyView, CBurnHistoryStorage* burnView, CVaultHistoryStorage* vaultView)
    : historyView(historyView), burnView(burnView), vaultView(vaultView) {}

void CHistoryErasers::AddBalance(const CScript& owner, const uint256& vaultID)
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
}

void CHistoryErasers::SubFeeBurn(const CScript& owner)
{
    if (burnView) {
        burnAccounts.insert(owner);
    }
}

void CHistoryErasers::SubBalance(const CScript& owner, const uint256& vaultID)
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
}

void CHistoryErasers::Flush(const uint32_t height, const uint32_t txn, const uint256& vaultID)
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
}

CHistoryWriters::CHistoryWriters(CAccountHistoryStorage* historyView, CBurnHistoryStorage* burnView, CVaultHistoryStorage* vaultView)
    : historyView(historyView), burnView(burnView), vaultView(vaultView) {}

extern std::string ScriptToString(CScript const& script);

void CHistoryWriters::AddBalance(const CScript& owner, const CTokenAmount amount, const uint256& vaultID)
{
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

void CHistoryWriters::AddFeeBurn(const CScript& owner, const CAmount amount)
{
    if (burnView && amount != 0) {
        burnDiffs[owner][DCT_ID{0}] += amount;
    }
}

void CHistoryWriters::SubBalance(const CScript& owner, const CTokenAmount amount, const uint256& vaultID)
{
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

void CHistoryWriters::Flush(const uint32_t height, const uint256& txid, const uint32_t txn, const uint8_t type, const uint256& vaultID)
{
    if (historyView) {
        for (const auto& diff : diffs) {
            LogPrint(BCLog::ACCOUNTCHANGE, "AccountChange: txid=%s addr=%s change=%s\n", txid.GetHex(), ScriptToString(diff.first), (CBalances{diff.second}.ToString()));
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
                vaultView->WriteVaultHistory({height, diff.first, txn, addresses.first}, {txid, type, addresses.second});
            }
        }
        if (!schemeID.empty()) {
            vaultView->WriteVaultScheme({vaultID, height}, {type, txid, schemeID, txn});
        }
        if (!globalLoanScheme.identifier.empty()) {
            vaultView->WriteGlobalScheme({height, txn, globalLoanScheme.schemeCreationTxid}, {globalLoanScheme, type, txid});
        }
    }
}

std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;
