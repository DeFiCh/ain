// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/accounts.h>
#include <masternodes/masternodes.h>
#include <masternodes/vaulthistory.h>
#include <key_io.h>

// NOTE: The new key is used to simulate multi index support
// apparently it could be implemented globally

struct AccountHistoryKeyNew {
    uint32_t blockHeight;
    CScript owner;
    uint32_t txn; // for order in block

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(WrapBigEndianInv(blockHeight));
        READWRITE(owner);
        READWRITE(WrapBigEndianInv(txn));
    }
};

static AccountHistoryKeyNew Convert(AccountHistoryKey const & key)
{
    return {key.blockHeight, key.owner, key.txn};
}

static AccountHistoryKey Convert(AccountHistoryKeyNew const & key)
{
    return {key.owner, key.blockHeight, key.txn};
}

void CAccountsHistoryView::CreateMultiIndexIfNeeded()
{
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

    bool sync = true;
    Flush(sync);

    LogPrint(BCLog::BENCH, "    - Multi index took: %dms\n", GetTimeMillis() - startTime);
}

void CAccountsHistoryView::ForEachAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback,
                                                 CScript const & owner, uint32_t height, uint32_t txn)
{
    if (!owner.empty()) {
        ForEach<ByAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>(callback, {owner, height, txn});
        return;
    }

    ForEach<ByAccountHistoryKeyNew, AccountHistoryKeyNew, char>([&](AccountHistoryKeyNew const & newKey, CLazySerialize<char>) {
        auto key = Convert(newKey);
        return callback(key, CLazySerialize<AccountHistoryValue>{[&]() {
            auto value = ReadAccountHistory(key);
            assert(value);
            return *value;
        }});
    }, {height, owner, txn});
}

std::optional<AccountHistoryValue> CAccountsHistoryView::ReadAccountHistory(AccountHistoryKey const & key) const
{
    return ReadBy<ByAccountHistoryKey, AccountHistoryValue>(key);
}

Res CAccountsHistoryView::WriteAccountHistory(const AccountHistoryKey& key, const AccountHistoryValue& value)
{
    WriteBy<ByAccountHistoryKey>(key, value);
    WriteBy<ByAccountHistoryKeyNew>(Convert(key), '\0');
    return Res::Ok();
}

Res CAccountsHistoryView::EraseAccountHistory(const AccountHistoryKey& key)
{
    EraseBy<ByAccountHistoryKey>(key);
    EraseBy<ByAccountHistoryKeyNew>(Convert(key));
    return Res::Ok();
}

Res CAccountsHistoryView::EraseAccountHistoryHeight(uint32_t height)
{
    std::vector<AccountHistoryKey> keysToDelete;

    auto it = LowerBound<ByAccountHistoryKeyNew>(AccountHistoryKeyNew{height, {}, ~0u});
    for (; it.Valid() && it.Key().blockHeight == height; it.Next()) {
        keysToDelete.push_back(Convert(it.Key()));
    }

    for (const auto& key : keysToDelete) {
        EraseAccountHistory(key);
    }
    return Res::Ok();
}

CAccountHistoryStorage::CAccountHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(std::make_shared<CStorageKV>(CStorageLevelDB{dbName, cacheSize, fMemory, fWipe}))
{
}

CBurnHistoryStorage::CBurnHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(std::make_shared<CStorageKV>(CStorageLevelDB{dbName, cacheSize, fMemory, fWipe}))
{
}

CAccountsHistoryWriter::CAccountsHistoryWriter(CCustomCSView & storage, uint32_t height, uint32_t txn, const uint256& txid, uint8_t type,
                                               CHistoryWriters* writers)
    : CStorageView(storage), height(height), txn(txn), txid(txid), type(type), writers(writers)
{
}

Res CAccountsHistoryWriter::AddBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::AddBalance(owner, amount);
    if (writers && amount.nValue != 0 && res.ok) {
        writers->AddBalance(owner, amount);
    }

    return res;
}

Res CAccountsHistoryWriter::SubBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::SubBalance(owner, amount);
    if (writers && res.ok && amount.nValue != 0) {
        writers->SubBalance(owner, amount);
    }

    return res;
}

bool CAccountsHistoryWriter::Flush(bool sync)
{
    if (writers) {
        writers->Flush(height, txid, txn, type);
    }
    return CCustomCSView::Flush(sync);
}

CHistoryWriters::CHistoryWriters(CAccountHistoryStorage* historyView, CBurnHistoryStorage* burnView, CVaultHistoryStorage* vaultView)
    : historyView(historyView), burnView(burnView), vaultView(vaultView) {}

void CHistoryWriters::AddBalance(const CScript& owner, const CTokenAmount amount)
{
    if (historyView) {
        diffs[owner][amount.nTokenId] += amount.nValue;
    }
    if (burnView && owner == Params().GetConsensus().burnAddress) {
        burnDiffs[owner][amount.nTokenId] += amount.nValue;
    }
    if (vaultView && vaultID) {
        vaultDiffs[*vaultID][owner][amount.nTokenId] += amount.nValue;
    }
}

void CHistoryWriters::AddFeeBurn(const CScript& owner, const CAmount amount)
{
    if (burnView && amount != 0) {
        burnDiffs[owner][DCT_ID{0}] += amount;
    }
}

void CHistoryWriters::SubBalance(const CScript& owner, const CTokenAmount amount)
{
    if (historyView) {
        diffs[owner][amount.nTokenId] -= amount.nValue;
    }
    if (burnView && owner == Params().GetConsensus().burnAddress) {
        burnDiffs[owner][amount.nTokenId] -= amount.nValue;
    }
    if (vaultView && vaultID) {
        vaultDiffs[*vaultID][owner][amount.nTokenId] -= amount.nValue;
    }
}

void CHistoryWriters::AddVault(const CVaultId& vaultId, const std::string& schemeId)
{
    if (!vaultView) {
        return;
    }

    vaultID = vaultId;
    if (!schemeId.empty()) {
        schemeID = schemeId;
    }
}

void CHistoryWriters::AddLoanScheme(const CLoanSchemeMessage& loanScheme, const uint256& txid, uint32_t height, uint32_t txn)
{
    if (!vaultView) {
        return;
    }

    globalLoanScheme.emplace();
    globalLoanScheme->identifier = loanScheme.identifier;
    globalLoanScheme->ratio = loanScheme.ratio;
    globalLoanScheme->rate = loanScheme.rate;

    if (!loanScheme.updateHeight) {
        globalLoanScheme->schemeCreationTxid = txid;
        return;
    }

    vaultView->ForEachGlobalScheme([&](VaultGlobalSchemeKey const & key, VaultGlobalSchemeValue value) {
        if (value.loanScheme.identifier != globalLoanScheme->identifier) {
            return true;
        }
        globalLoanScheme->schemeCreationTxid = key.schemeCreationTxid;
        return false;
    }, {height, txn, {}});
}

extern std::string ScriptToString(CScript const& script);

void CHistoryWriters::Flush(const uint32_t height, const uint256& txid, const uint32_t txn, const uint8_t type)
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
            vaultView->WriteVaultScheme({*vaultID, height}, {type, txid, schemeID, txn});
        }
        if (globalLoanScheme && !globalLoanScheme->identifier.empty()) {
            vaultView->WriteGlobalScheme({height, txn, globalLoanScheme->schemeCreationTxid}, {*globalLoanScheme, type, txid});
        }
    }
}

std::unique_ptr<CAccountHistoryStorage> paccountHistoryDB;
std::unique_ptr<CBurnHistoryStorage> pburnHistoryDB;
