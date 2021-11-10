#include <masternodes/vaulthistory.h>

void CVaultHistoryView::ForEachVaultHistory(std::function<bool(VaultHistoryKey const &, CLazySerialize<VaultHistoryValue>)> callback, VaultHistoryKey const & start)
{
    ForEach<ByVaultHistoryKey, VaultHistoryKey, VaultHistoryValue>(callback, start);
}

Res CVaultHistoryView::WriteVaultHistory(VaultHistoryKey const & key, VaultHistoryValue const & value) {
    WriteBy<ByVaultHistoryKey>(key, value);
    return Res::Ok();
}

Res CVaultHistoryView::EraseVaultHistory(const VaultHistoryKey& key)
{
    EraseBy<ByVaultHistoryKey>(key);
    return Res::Ok();
}


CVaultHistoryStorage::CVaultHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
        : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe))
{
}

std::unique_ptr<CVaultHistoryStorage> pvaultHistoryDB;
