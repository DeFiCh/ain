#include <masternodes/vaulthistory.h>

#include <chain.h>

void CVaultHistoryView::ForEachVaultHistory(std::function<bool(VaultHistoryKey const &, CLazySerialize<VaultHistoryValue>)> callback, VaultHistoryKey const & start)
{
    ForEach<ByVaultHistoryKey, VaultHistoryKey, VaultHistoryValue>(callback, start);
}

void CVaultHistoryView::WriteVaultHistory(VaultHistoryKey const & key, VaultHistoryValue const & value) {
    WriteBy<ByVaultHistoryKey>(key, value);
}

void CVaultHistoryView::EraseVaultHistory(const VaultHistoryKey& key)
{
    EraseBy<ByVaultHistoryKey>(key);
}

void CVaultHistoryView::ForEachVaultState(std::function<bool(VaultStateKey const &, CLazySerialize<VaultStateValue>)> callback, VaultStateKey const & start)
{
    ForEach<ByVaultStateKey, VaultStateKey, VaultStateValue>(callback, start);
}

void CVaultHistoryView::WriteVaultState(CCustomCSView& mnview, const CBlockIndex& pindex, const uint256& vaultID, const uint32_t ratio)
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

    std::vector<CAuctionBatch> batches;
    if (auto data = mnview.GetAuction(vaultID, pindex.nHeight)) {
        for (uint32_t i{0}; i < data->batchCount; ++i) {
            if (auto batch = mnview.GetAuctionBatch(vaultID, i)) {
                batches.push_back(*batch);
            }
        }
    }

    VaultStateValue value{vault->isUnderLiquidation, collaterals->balances, collateralLoans, batches, ratio, vault->schemeId};
    WriteBy<ByVaultStateKey>(VaultStateKey{vaultID, static_cast<uint32_t>(pindex.nHeight)}, value);
}

void CVaultHistoryView::EraseVaultState(const uint32_t height)
{
    std::vector<uint256> vaultIDs;
    ForEachVaultState([&](VaultStateKey const & key, CLazySerialize<VaultStateValue> value) {
        if (key.blockHeight != height) {
            return false;
        }
        vaultIDs.push_back(key.vaultID);
        return true;
    }, {{}, height});

    for (const auto id : vaultIDs) {
        EraseBy<ByVaultStateKey>(VaultStateKey{id, height});
    }
}

CVaultHistoryStorage::CVaultHistoryStorage(const fs::path& dbName, std::size_t cacheSize, bool fMemory, bool fWipe)
        : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe))
{
}

std::unique_ptr<CVaultHistoryStorage> pvaultHistoryDB;
