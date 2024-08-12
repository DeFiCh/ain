#include <dfi/snapshotmanager.h>

#include <dfi/accountshistory.h>
#include <dfi/masternodes.h>
#include <dfi/vaulthistory.h>

template <typename T>
static void CheckoutSnapshot(T &checkedOutMap, const CBlockSnapshot &snapshot) {
    const auto checkOutKey = snapshot.GetKey();
    const auto dbSnapshot = snapshot.GetLevelDBSnapshot();
    if (checkedOutMap.count(checkOutKey)) {
        ++checkedOutMap.at(checkOutKey).count;
    } else {
        checkedOutMap[checkOutKey] = {dbSnapshot, 1};
    }
}

SnapshotCollection GetSnapshots() {
    return psnapshotManager->GetSnapshots();
}

SnapshotCollection CSnapshotManager::GetSnapshots() {
    if (auto currentSnapshots = GetCurrentSnapshots()) {
        return std::move(*currentSnapshots);
    }
    return GetGlobalSnapshots();
}

std::optional<SnapshotCollection> CSnapshotManager::GetCurrentSnapshots() {
    std::unique_lock lock(mtx);

    if (!currentViewSnapshot || (historyDB && !currentHistorySnapshot) || (vaultDB && !currentVaultSnapshot)) {
        return {};
    }

    auto [changed, snapshotDB] = CheckoutViewSnapshot();
    auto viewSnapshot = std::make_unique<CCustomCSView>(snapshotDB, changed);

    std::unique_ptr<CAccountHistoryStorage> historySnapshot{};
    if (historyDB) {
        auto snapshot = CheckoutHistorySnapshot();
        historySnapshot = std::make_unique<CAccountHistoryStorage>(historyDB, snapshot);
    }

    std::unique_ptr<CVaultHistoryStorage> vaultSnapshot{};
    if (vaultDB) {
        auto snapshot = CheckoutVaultSnapshot();
        vaultSnapshot = std::make_unique<CVaultHistoryStorage>(vaultDB, snapshot);
    }

    return std::make_tuple(std::move(viewSnapshot), std::move(historySnapshot), std::move(vaultSnapshot));
}

SnapshotCollection CSnapshotManager::GetGlobalSnapshots() {
    // Same lock order as ConnectBlock
    LOCK(cs_main);
    std::unique_lock lock(mtx);

    auto [changed, snapshotDB] = GetGlobalViewSnapshot();
    auto viewSnapshot = std::make_unique<CCustomCSView>(snapshotDB, changed);

    std::unique_ptr<CAccountHistoryStorage> historySnapshot{};
    if (historyDB) {
        auto snapshot = GetGlobalHistorySnapshot();
        historySnapshot = std::make_unique<CAccountHistoryStorage>(historyDB, snapshot);
    }

    std::unique_ptr<CVaultHistoryStorage> vaultSnapshot{};
    if (vaultDB) {
        auto snapshot = GetGlobalVaultSnapshot();
        vaultSnapshot = std::make_unique<CVaultHistoryStorage>(vaultDB, snapshot);
    }

    return std::make_tuple(std::move(viewSnapshot), std::move(historySnapshot), std::move(vaultSnapshot));
}

CCheckedOutSnapshot::~CCheckedOutSnapshot() {
    // Check snapshot back in
    psnapshotManager->ReturnSnapshot(key);
}

CSnapshotManager::CSnapshotManager(std::unique_ptr<CCustomCSView> &otherViewDB,
                                   std::unique_ptr<CAccountHistoryStorage> &otherHistoryDB,
                                   std::unique_ptr<CVaultHistoryStorage> &otherVaultDB) {
    viewDB = otherViewDB->GetStorage().GetStorageLevelDB()->GetDB();

    // acindex index might be disabled
    if (otherHistoryDB) {
        historyDB = otherHistoryDB->GetStorage().GetDB();
    }

    // vault index index might be disabled
    if (otherVaultDB) {
        vaultDB = otherVaultDB->GetStorage().GetDB();
    }
}

template <typename T, typename U, typename V>
static void ReturnSnapshot(T &db, U &snapshot, V &checkedMap) {
    if (db && snapshot) {
        if (snapshot->GetLevelDBSnapshot() && (!checkedMap.count(snapshot->GetKey()) ||        // Not in map
                                               (checkedMap.count(snapshot->GetKey()) &&        // Is in map...
                                                !checkedMap.at(snapshot->GetKey()).count))) {  // ..but not in use
            checkedMap.erase(snapshot->GetKey());
            db->ReleaseSnapshot(snapshot->GetLevelDBSnapshot());
        }
        snapshot.reset();
    }
}

template <typename T, typename U>
static void SetCurrentSnapshot(T &db, U &currentSnapshot, SnapshotType type, const CBlockIndex *block) {
    if (db) {
        // Get database snapshot
        const auto snapshot = db->GetStorage().CreateLevelDBSnapshot();

        // Set current snapshot
        currentSnapshot = std::make_unique<CBlockSnapshot>(
            snapshot, MapKV{}, CBlockSnapshotKey{type, block->nHeight, block->GetBlockHash()});
    }
}

void CSnapshotManager::SetBlockSnapshots(CFlushableStorageKV &viewStorge,
                                         CAccountHistoryStorage *historyView,
                                         CVaultHistoryStorage *vaultView,
                                         const CBlockIndex *block,
                                         const bool nearTip) {
    std::unique_lock lock(mtx);

    // Return current snapshots
    ::ReturnSnapshot(viewDB, currentViewSnapshot, checkedOutViewMap);
    ::ReturnSnapshot(historyDB, currentHistorySnapshot, checkedOutHistoryMap);
    ::ReturnSnapshot(vaultDB, currentVaultSnapshot, checkedOutVaultMap);

    // Do not create current snapshots if snapshots are disabled or not near tip
    if (!gArgs.GetBoolArg("-enablesnapshots", DEFAULT_SNAPSHOT) || !nearTip) {
        return;
    }

    // Get view database snapshot and flushable storage changed map
    auto [changedView, snapshotView] = viewStorge.CreateSnapshotData();

    // Set current view snapshot
    currentViewSnapshot = std::make_unique<CBlockSnapshot>(
        snapshotView, changedView, CBlockSnapshotKey{SnapshotType::VIEW, block->nHeight, block->GetBlockHash()});

    // Set current snapshots
    ::SetCurrentSnapshot(historyView, currentHistorySnapshot, SnapshotType::HISTORY, block);
    ::SetCurrentSnapshot(vaultView, currentVaultSnapshot, SnapshotType::VAULT, block);
}

std::pair<MapKV, std::unique_ptr<CStorageLevelDB>> CSnapshotManager::GetGlobalViewSnapshot() {
    // Get database snapshot and flushable storage changed map
    auto [changedMap, snapshot] = pcustomcsview->GetStorage().CreateSnapshotData();

    // Create checked out snapshot
    const auto blockIndex = ::ChainActive().Tip();
    CBlockSnapshotKey key{SnapshotType::VIEW, blockIndex->nHeight, blockIndex->GetBlockHash()};
    auto globalSnapshot = std::make_unique<CCheckedOutSnapshot>(snapshot, key);

    // Set global as current snapshot
    currentViewSnapshot = std::make_unique<CBlockSnapshot>(globalSnapshot->GetLevelDBSnapshot(), changedMap, key);

    // Track checked out snapshot
    ::CheckoutSnapshot(checkedOutViewMap, *currentViewSnapshot);

    return {changedMap, std::make_unique<CStorageLevelDB>(viewDB, globalSnapshot)};
}

std::unique_ptr<CCheckedOutSnapshot> CSnapshotManager::GetGlobalHistorySnapshot() {
    // Get database snapshot and flushable storage changed map
    auto snapshot = paccountHistoryDB->GetStorage().CreateLevelDBSnapshot();

    const auto blockIndex = ::ChainActive().Tip();
    CBlockSnapshotKey key{SnapshotType::HISTORY, blockIndex->nHeight, blockIndex->GetBlockHash()};
    auto globalSnapshot = std::make_unique<CCheckedOutSnapshot>(snapshot, key);

    // Set global as current snapshot
    currentHistorySnapshot = std::make_unique<CBlockSnapshot>(globalSnapshot->GetLevelDBSnapshot(), MapKV{}, key);

    // Track checked out snapshot
    ::CheckoutSnapshot(checkedOutHistoryMap, *currentHistorySnapshot);

    // Create checked out snapshot
    return globalSnapshot;
}

std::unique_ptr<CCheckedOutSnapshot> CSnapshotManager::GetGlobalVaultSnapshot() {
    // Get database snapshot and flushable storage changed map
    auto snapshot = pvaultHistoryDB->GetStorage().CreateLevelDBSnapshot();

    const auto blockIndex = ::ChainActive().Tip();
    CBlockSnapshotKey key{SnapshotType::VAULT, blockIndex->nHeight, blockIndex->GetBlockHash()};
    auto globalSnapshot = std::make_unique<CCheckedOutSnapshot>(snapshot, key);

    // Set global as current snapshot
    currentVaultSnapshot = std::make_unique<CBlockSnapshot>(globalSnapshot->GetLevelDBSnapshot(), MapKV{}, key);

    // Track checked out snapshot
    ::CheckoutSnapshot(checkedOutVaultMap, *currentVaultSnapshot);

    // Create checked out snapshot
    return globalSnapshot;
}

std::pair<MapKV, std::unique_ptr<CStorageLevelDB>> CSnapshotManager::CheckoutViewSnapshot() {
    // Create checked out snapshot
    auto snapshot =
        std::make_unique<CCheckedOutSnapshot>(currentViewSnapshot->GetLevelDBSnapshot(), currentViewSnapshot->GetKey());

    // Track checked out snapshot
    ::CheckoutSnapshot(checkedOutViewMap, *currentViewSnapshot);

    return {currentViewSnapshot->GetChanged(), std::make_unique<CStorageLevelDB>(viewDB, snapshot)};
}

std::unique_ptr<CCheckedOutSnapshot> CSnapshotManager::CheckoutHistorySnapshot() {
    // Create checked out snapshot
    auto snapshot = std::make_unique<CCheckedOutSnapshot>(currentHistorySnapshot->GetLevelDBSnapshot(),
                                                          currentHistorySnapshot->GetKey());

    // Track checked out snapshot
    ::CheckoutSnapshot(checkedOutHistoryMap, *currentHistorySnapshot);

    return snapshot;
}

std::unique_ptr<CCheckedOutSnapshot> CSnapshotManager::CheckoutVaultSnapshot() {
    // Create checked out snapshot
    auto snapshot = std::make_unique<CCheckedOutSnapshot>(currentVaultSnapshot->GetLevelDBSnapshot(),
                                                          currentVaultSnapshot->GetKey());

    // Track checked out snapshot
    ::CheckoutSnapshot(checkedOutVaultMap, *currentVaultSnapshot);

    return snapshot;
}

template <typename T, typename U, typename V>
static void DestructSnapshot(const CBlockSnapshotKey &key, T &checkedOutMap, U &currentSnapshot, V &db) {
    if (checkedOutMap.count(key)) {
        --checkedOutMap.at(key).count;

        bool isCurrentKey{};
        if (currentSnapshot) {
            isCurrentKey = currentSnapshot->GetKey().hash == key.hash;
        }

        // Release if not in use and not the current block
        if (!checkedOutMap.at(key).count && !isCurrentKey) {
            db->ReleaseSnapshot(checkedOutMap.at(key).snapshot);
            checkedOutMap.erase(key);
        }
    }
}

void CSnapshotManager::ReturnSnapshot(const CBlockSnapshotKey &key) {
    std::unique_lock lock(mtx);
    ::DestructSnapshot(key, checkedOutViewMap, currentViewSnapshot, viewDB);
    ::DestructSnapshot(key, checkedOutHistoryMap, currentHistorySnapshot, historyDB);
    ::DestructSnapshot(key, checkedOutVaultMap, currentVaultSnapshot, vaultDB);
}

std::unique_ptr<CSnapshotManager> psnapshotManager;
