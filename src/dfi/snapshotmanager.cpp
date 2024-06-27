#include <dfi/snapshotmanager.h>

#include <dfi/accountshistory.h>
#include <dfi/masternodes.h>

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

std::unique_ptr<CCustomCSView> GetViewSnapshot() {
    return psnapshotManager->GetViewSnapshot();
}

std::unique_ptr<CAccountHistoryStorage> GetHistorySnapshot() {
    return psnapshotManager->GetHistorySnapshot();
}

std::unique_ptr<CCustomCSView> CSnapshotManager::GetViewSnapshot() {
    MapKV changed;
    std::unique_ptr<CStorageLevelDB> snapshotDB;

    // Get database snapshot and flushable storage changed map
    if (const auto res = CheckoutViewSnapshot(changed, snapshotDB); !res) {
        // If snapshot not present get global snapshot
        GetGlobalViewSnapshot(changed, snapshotDB);
    }

    // Create new view using snapshot and change map
    return std::make_unique<CCustomCSView>(snapshotDB, changed);
}

std::unique_ptr<CAccountHistoryStorage> CSnapshotManager::GetHistorySnapshot() {
    // Get database snapshot and flushable storage changed map
    auto snapshot = psnapshotManager->CheckoutHistorySnapshot();

    if (!snapshot) {
        // If snapshot not present get global snapshot
        snapshot = psnapshotManager->GetGlobalHistorySnapshot();
    }

    // Create new view using snapshot and change map
    return std::make_unique<CAccountHistoryStorage>(historyDB, snapshot);
}

CCheckedOutSnapshot::~CCheckedOutSnapshot() {
    // Check snapshot back in
    psnapshotManager->ReturnSnapshot(key);
}

CSnapshotManager::CSnapshotManager(std::unique_ptr<CCustomCSView> &otherViewDB,
                                   std::unique_ptr<CAccountHistoryStorage> &otherHistoryDB) {
    viewDB = otherViewDB->GetStorage().GetStorageLevelDB()->GetDB();

    // acindex index might be disabled
    if (otherHistoryDB) {
        historyDB = otherHistoryDB->GetStorage().GetDB();
    }
}

void CSnapshotManager::SetBlockSnapshots(CFlushableStorageKV &viewStorge,
                                         CAccountHistoryStorage *historyView,
                                         const CBlockIndex *block,
                                         const bool nearTip) {
    std::unique_lock<std::mutex> lock(mtx);

    // Check current as this might have been set by an on-demand snapshot
    if (currentViewSnapshot) {
        // Release current view entry if not in use
        if (currentViewSnapshot->GetLevelDBSnapshot() &&
            (!checkedOutViewMap.count(currentViewSnapshot->GetKey()) ||        // Not in map
             (checkedOutViewMap.count(currentViewSnapshot->GetKey()) &&        // Is in map...
              !checkedOutViewMap.at(currentViewSnapshot->GetKey()).count))) {  // ..but not in use
            checkedOutViewMap.erase(currentViewSnapshot->GetKey());
            viewDB->ReleaseSnapshot(currentViewSnapshot->GetLevelDBSnapshot());
        }
        currentViewSnapshot.reset();
    }

    // Check current as this might have been set by an on-demand snapshot
    if (historyDB && currentHistorySnapshot) {
        // Release current history entry if not in use
        if (currentHistorySnapshot->GetLevelDBSnapshot() &&
            (!checkedOutHistoryMap.count(currentHistorySnapshot->GetKey()) ||        // Not in map
             (checkedOutHistoryMap.count(currentHistorySnapshot->GetKey()) &&        // Is in map...
              !checkedOutHistoryMap.at(currentHistorySnapshot->GetKey()).count))) {  // ..but not in use
            checkedOutHistoryMap.erase(currentHistorySnapshot->GetKey());
            historyDB->ReleaseSnapshot(currentHistorySnapshot->GetLevelDBSnapshot());
        }
        currentHistorySnapshot.reset();
    }

    // Do not create current snapshots if snapshots are disabled or not near tip
    if (!gArgs.GetBoolArg("-enablesnapshots", DEFAULT_SNAPSHOT) || !nearTip) {
        return;
    }

    // Get view database snapshot and flushable storage changed map
    auto [changedView, snapshotView] = viewStorge.CreateSnapshotData();

    // Set current view snapshot
    currentViewSnapshot = std::make_unique<CBlockSnapshot>(
        snapshotView, changedView, CBlockSnapshotKey{SnapshotType::VIEW, block->nHeight, block->GetBlockHash()});

    if (historyView) {
        // Get history database snapshot
        const auto snapshotHistory = historyView->GetStorage().CreateLevelDBSnapshot();

        // Set current history snapshot
        currentHistorySnapshot = std::make_unique<CBlockSnapshot>(
            snapshotHistory, MapKV{}, CBlockSnapshotKey{SnapshotType::HISTORY, block->nHeight, block->GetBlockHash()});
    }
}

void CSnapshotManager::GetGlobalViewSnapshot(MapKV &changed, std::unique_ptr<CStorageLevelDB> &snapshotDB) {
    // Same lock order as ConnectBlock
    LOCK(cs_main);
    std::unique_lock<std::mutex> lock(mtx);

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

    // Set args
    changed = changedMap;
    snapshotDB = std::make_unique<CStorageLevelDB>(viewDB, globalSnapshot);
}

std::unique_ptr<CCheckedOutSnapshot> CSnapshotManager::GetGlobalHistorySnapshot() {
    // Same lock order as ConnectBlock
    LOCK(cs_main);
    std::unique_lock<std::mutex> lock(mtx);

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

bool CSnapshotManager::CheckoutViewSnapshot(MapKV &changed, std::unique_ptr<CStorageLevelDB> &snapshotDB) {
    std::unique_lock<std::mutex> lock(mtx);

    if (!currentViewSnapshot) {
        return false;
    }

    // Create checked out snapshot
    auto snapshot =
        std::make_unique<CCheckedOutSnapshot>(currentViewSnapshot->GetLevelDBSnapshot(), currentViewSnapshot->GetKey());

    // Set args
    changed = currentViewSnapshot->GetChanged();
    snapshotDB = std::make_unique<CStorageLevelDB>(viewDB, snapshot);

    // Track checked out snapshot
    ::CheckoutSnapshot(checkedOutViewMap, *currentViewSnapshot);

    return true;
}

std::unique_ptr<CCheckedOutSnapshot> CSnapshotManager::CheckoutHistorySnapshot() {
    std::unique_lock<std::mutex> lock(mtx);

    if (!currentHistorySnapshot) {
        return {};
    }

    // Create checked out snapshot
    auto snapshot = std::make_unique<CCheckedOutSnapshot>(currentHistorySnapshot->GetLevelDBSnapshot(),
                                                          currentHistorySnapshot->GetKey());

    // Track checked out snapshot
    ::CheckoutSnapshot(checkedOutHistoryMap, *currentHistorySnapshot);

    return snapshot;
}

template <typename T, typename U, typename V>
static void ReturnSnapshot(const CBlockSnapshotKey &key, T &checkedOutMap, U &currentSnapshot, V &db) {
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
    std::unique_lock<std::mutex> lock(mtx);
    ::ReturnSnapshot(key, checkedOutViewMap, currentViewSnapshot, viewDB);
    ::ReturnSnapshot(key, checkedOutHistoryMap, currentHistorySnapshot, historyDB);
}

std::unique_ptr<CSnapshotManager> psnapshotManager;
