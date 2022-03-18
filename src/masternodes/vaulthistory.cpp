#include <masternodes/vaulthistory.h>

#include <chain.h>

void CVaultHistoryView::ForEachVaultHistory(std::function<bool(VaultHistoryKey const &, CLazySerialize<VaultHistoryValue>)> callback, VaultHistoryKey const & start)
{
    ForEach<ByVaultHistoryKey, VaultHistoryKey, VaultHistoryValue>(callback, start);
}

void CVaultHistoryView::WriteVaultHistory(VaultHistoryKey const & key, VaultHistoryValue const & value) {
    WriteBy<ByVaultHistoryKey>(key, value);
}

void CVaultHistoryView::WriteVaultScheme(VaultSchemeKey const & key, const VaultSchemeValue& value)
{
    WriteBy<ByVaultSchemeKey>(key, value);
}

void CVaultHistoryView::WriteGlobalScheme(VaultGlobalSchemeKey const & key, const VaultGlobalSchemeValue& value)
{
    WriteBy<ByVaultGlobalSchemeKey>(key, value);
}

void CVaultHistoryView::EraseVaultHistory(const uint32_t height)
{
    std::vector<VaultHistoryKey> keys;
    auto historyIt = LowerBound<ByVaultHistoryKey>(VaultHistoryKey{height});
    for (; historyIt.Valid() && historyIt.Key().blockHeight == height; historyIt.Next()) {
        keys.push_back(historyIt.Key());
    }
    for (auto& key : keys) {
        EraseBy<ByVaultHistoryKey>(key);
        const auto stateKey = VaultStateKey{key.vaultID, key.blockHeight};
        EraseBy<ByVaultStateKey>(stateKey);
        EraseBy<ByVaultSchemeKey>(stateKey);
    }

    std::vector<VaultGlobalSchemeKey> schemeKeys;
    auto schemeIt = LowerBound<ByVaultGlobalSchemeKey>(VaultGlobalSchemeKey{height, ~0u});
    for (; schemeIt.Valid() && schemeIt.Key().blockHeight == height; schemeIt.Next()) {
        schemeKeys.push_back(schemeIt.Key());
    }
    for (auto& key : schemeKeys) {
        EraseGlobalScheme(key);
    }
}

void CVaultHistoryView::ForEachVaultScheme(std::function<bool(VaultSchemeKey const &, CLazySerialize<VaultSchemeValue>)> callback, VaultSchemeKey const & start)
{
    ForEach<ByVaultSchemeKey, VaultSchemeKey, VaultSchemeValue>(callback, start);
}

void CVaultHistoryView::ForEachVaultState(std::function<bool(VaultStateKey const &, CLazySerialize<VaultStateValue>)> callback, VaultStateKey const & start)
{
    ForEach<ByVaultStateKey, VaultStateKey, VaultStateValue>(callback, start);
}

void CVaultHistoryView::ForEachGlobalScheme(std::function<bool(VaultGlobalSchemeKey const &, CLazySerialize<VaultGlobalSchemeValue>)> callback, VaultGlobalSchemeKey const & start)
{
    ForEach<ByVaultGlobalSchemeKey, VaultGlobalSchemeKey, VaultGlobalSchemeValue>(callback, start);
}

void CVaultHistoryView::WriteVaultState(CCustomCSView& mnview, const CBlockIndex& pindex, const uint256& vaultID, uint32_t ratio, const std::vector<CAuctionBatch>& batches)
{
    const auto vault = mnview.GetVault(vaultID);
    assert(vault);

    auto collaterals = mnview.GetVaultCollaterals(vaultID);
    if (!collaterals) {
        collaterals = CBalances{};
    }

    bool useNextPrice = false, requireLivePrice = false;
    auto rate = mnview.GetLoanCollaterals(vaultID, *collaterals, pindex.nHeight, pindex.nTime, useNextPrice, requireLivePrice);

    CCollateralLoans collateralLoans{0, 0, {}, {}};
    if (rate) {
        collateralLoans = *rate.val;
    }

    VaultStateValue value{collaterals->balances, collateralLoans, batches, ratio};
    WriteBy<ByVaultStateKey>(VaultStateKey{vaultID, static_cast<uint32_t>(pindex.nHeight)}, value);
}

void CVaultHistoryView::EraseGlobalScheme(const VaultGlobalSchemeKey& key)
{
    EraseBy<ByVaultGlobalSchemeKey>(key);
}

CVaultHistoryStorage::CVaultHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
        : CStorageView(std::make_shared<CStorageKV>(CStorageLevelDB{dbName, cacheSize, fMemory, fWipe}))
{
}

std::unique_ptr<CVaultHistoryStorage> pvaultHistoryDB;
