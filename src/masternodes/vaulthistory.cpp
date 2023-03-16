#include <masternodes/vaulthistory.h>

#include <chain.h>

void CVaultHistoryView::ForEachVaultHistory(
    std::function<bool(const VaultHistoryKey &, CLazySerialize<VaultHistoryValue>)> callback,
    const VaultHistoryKey &start) {
    ForEach<ByVaultHistoryKey, VaultHistoryKey, VaultHistoryValue>(callback, start);
}

void CVaultHistoryView::WriteVaultHistory(const VaultHistoryKey &key, const VaultHistoryValue &value) {
    WriteBy<ByVaultHistoryKey>(key, value);
}

void CVaultHistoryView::WriteVaultScheme(const VaultSchemeKey &key, const VaultSchemeValue &value) {
    WriteBy<ByVaultSchemeKey>(key, value);
}

void CVaultHistoryView::WriteGlobalScheme(const VaultGlobalSchemeKey &key, const VaultGlobalSchemeValue &value) {
    WriteBy<ByVaultGlobalSchemeKey>(key, value);
}

void CVaultHistoryView::EraseVaultHistory(const uint32_t height) {
    std::vector<VaultHistoryKey> keys;
    auto historyIt = LowerBound<ByVaultHistoryKey>(VaultHistoryKey{height});
    for (; historyIt.Valid() && historyIt.Key().blockHeight == height; historyIt.Next()) {
        keys.push_back(historyIt.Key());
    }
    for (auto &key : keys) {
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
    for (auto &key : schemeKeys) {
        EraseGlobalScheme(key);
    }
}

void CVaultHistoryView::ForEachVaultScheme(
    std::function<bool(const VaultSchemeKey &, CLazySerialize<VaultSchemeValue>)> callback,
    const VaultSchemeKey &start) {
    ForEach<ByVaultSchemeKey, VaultSchemeKey, VaultSchemeValue>(callback, start);
}

void CVaultHistoryView::ForEachVaultState(
    std::function<bool(const VaultStateKey &, CLazySerialize<VaultStateValue>)> callback,
    const VaultStateKey &start) {
    ForEach<ByVaultStateKey, VaultStateKey, VaultStateValue>(callback, start);
}

void CVaultHistoryView::ForEachGlobalScheme(
    std::function<bool(const VaultGlobalSchemeKey &, CLazySerialize<VaultGlobalSchemeValue>)> callback,
    const VaultGlobalSchemeKey &start) {
    ForEach<ByVaultGlobalSchemeKey, VaultGlobalSchemeKey, VaultGlobalSchemeValue>(callback, start);
}

void CVaultHistoryView::WriteVaultState(CCustomCSView &mnview,
                                        const CBlockIndex &pindex,
                                        const uint256 &vaultID,
                                        const uint32_t ratio) {
    const auto vault = mnview.GetVault(vaultID);
    assert(vault);

    auto collaterals = mnview.GetVaultCollaterals(vaultID);
    if (!collaterals) {
        collaterals = CBalances{};
    }

    bool useNextPrice = false, requireLivePrice = false;
    auto rate =
        mnview.GetVaultAssets(vaultID, *collaterals, pindex.nHeight, pindex.nTime, useNextPrice, requireLivePrice);

    CVaultAssets collateralLoans{0, 0, {}, {}};
    if (rate) {
        collateralLoans = *rate.val;
    }

    std::vector<CAuctionBatch> batches;
    if (auto data = mnview.GetAuction(vaultID, pindex.nHeight)) {
        for (uint32_t i{0}; i < data->batchCount; ++i) {
            if (auto batch = mnview.GetAuctionBatch({vaultID, i})) {
                batches.push_back(*batch);
            }
        }
    }

    VaultStateValue value{collaterals->balances, collateralLoans, batches, ratio};
    WriteBy<ByVaultStateKey>(VaultStateKey{vaultID, static_cast<uint32_t>(pindex.nHeight)}, value);
}

void CVaultHistoryView::EraseGlobalScheme(const VaultGlobalSchemeKey &key) {
    EraseBy<ByVaultGlobalSchemeKey>(key);
}

CVaultHistoryStorage::CVaultHistoryStorage(const fs::path &dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
    : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe)) {}

std::unique_ptr<CVaultHistoryStorage> pvaultHistoryDB;
