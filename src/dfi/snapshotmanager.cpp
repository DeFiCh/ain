#include <dfi/snapshotmanager.h>

#include <dfi/masternodes.h>

std::unique_ptr<CCustomCSView> GetViewSnapshot() {
    MapKV changed;
    std::unique_ptr<CStorageLevelDB> snapshotDB;

    // Get database snapshot and flushable storage changed map
    const auto res = psnapshotManager->CheckoutSnapshot(changed, snapshotDB);

    // If snapshot not present get global snapshot
    if (!res) {
        // Get snapshot and changed map from global
        psnapshotManager->GetGlobalSnapshot(changed, snapshotDB);

        // Create new view using snapshot and change map
        return std::make_unique<CCustomCSView>(snapshotDB, changed);
    }

    // Create new view using snapshot and change map
    return std::make_unique<CCustomCSView>(snapshotDB, changed);
}

CCheckedOutSnapshot::~CCheckedOutSnapshot() {
    // Check snapshot back in
    psnapshotManager->ReturnSnapshot(key);
}

CSnapshotManager::CSnapshotManager(CStorageLevelDB *other) {
    db = other;
}

void CSnapshotManager::SetBlockSnapshot(CCustomCSView &view, const CBlockIndex *block) {
    std::unique_lock<std::mutex> lock(mtx);

    // Get database snapshot and flushable storage changed map
    auto [changed, snapshot] = view.GetStorage().GetSnapshotData();

    // Release current entry if not in use
    if (currentSnapshot && currentSnapshot->snapshot && !checkedOutMap.count(currentSnapshot->key)) {
        db->GetDB()->ReleaseSnapshot(currentSnapshot->snapshot);
    }

    // Set current snapshot
    currentSnapshot =
        std::make_unique<CBlockSnapshot>(snapshot, changed, CBlockSnapshotKey{block->nHeight, block->GetBlockHash()});
}

void CSnapshotManager::GetGlobalSnapshot(MapKV &changed, std::unique_ptr<CStorageLevelDB> &snapshotDB) {
    LOCK(cs_main);

    // Get database snapshot and flushable storage changed map
    auto [changedMap, snapshot] = pcustomcsview->GetStorage().GetSnapshotData();

    // Create checked out snapshot
    auto checkedSnapshot = std::make_unique<CCheckedOutSnapshot>(snapshot, CBlockSnapshotKey{});

    // Set args
    changed = changedMap;
    snapshotDB = std::make_unique<CStorageLevelDB>(db->GetDB(), checkedSnapshot);
}

bool CSnapshotManager::CheckoutSnapshot(MapKV &changed, std::unique_ptr<CStorageLevelDB> &snapshotDB) {
    std::unique_lock<std::mutex> lock(mtx);

    if (!currentSnapshot) {
        return false;
    }

    // Track checked out snapshot
    if (checkedOutMap.count(currentSnapshot->key)) {
        ++checkedOutMap.at(currentSnapshot->key).count;
    } else {
        checkedOutMap[currentSnapshot->key] = {currentSnapshot->snapshot, 1};
    }

    // Create checked out snapshot
    auto snapshot = std::make_unique<CCheckedOutSnapshot>(currentSnapshot->snapshot, currentSnapshot->key);

    // Set args
    changed = currentSnapshot->changed;
    snapshotDB = std::make_unique<CStorageLevelDB>(db->GetDB(), snapshot);

    return true;
}

void CSnapshotManager::EraseCurrentSnapshot() {
    std::unique_lock<std::mutex> lock(mtx);

    if (!currentSnapshot) {
        return;
    }

    if (!checkedOutMap.count(currentSnapshot->key)) {
        db->GetDB()->ReleaseSnapshot(currentSnapshot->snapshot);
    }

    currentSnapshot.reset();
}

void CSnapshotManager::ReturnSnapshot(const CBlockSnapshotKey &key) {
    std::unique_lock<std::mutex> lock(mtx);

    if (!checkedOutMap.count(key)) {
        return;
    }

    --checkedOutMap.at(key).count;

    bool isCurrentKey{};
    if (currentSnapshot) {
        isCurrentKey = currentSnapshot->key.hash == key.hash;
    }

    // Release if not in use and not the current block
    if (!checkedOutMap.count(key) && !isCurrentKey) {
        db->GetDB()->ReleaseSnapshot(checkedOutMap.at(key).snapshot);
        checkedOutMap.erase(key);
    }
}

std::unique_ptr<CSnapshotManager> psnapshotManager;
