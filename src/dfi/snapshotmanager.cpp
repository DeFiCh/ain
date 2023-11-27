#include <dfi/snapshotmanager.h>

#include <dfi/masternodes.h>

std::unique_ptr<CCustomCSView> GetViewSnapshot() {
    // Get database snapshot and flushable storage changed map
    auto [changed, snapshotDB] = psnapshotManager->CheckoutSnapshot();

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
    if (currentSnapshot.snapshot && !checkedOutMap.count(currentSnapshot.key)) {
        db->GetDB()->ReleaseSnapshot(currentSnapshot.snapshot);
    }

    // Set current snapshot
    currentSnapshot = {
        snapshot, changed, {block->nHeight, block->GetBlockHash()}
    };
}

std::pair<MapKV, std::unique_ptr<CStorageLevelDB>> CSnapshotManager::CheckoutSnapshot() {
    std::unique_lock<std::mutex> lock(mtx);

    // Track checked out snapshot
    if (checkedOutMap.count(currentSnapshot.key)) {
        ++checkedOutMap.at(currentSnapshot.key).count;
    } else {
        checkedOutMap[currentSnapshot.key] = {currentSnapshot.snapshot, 1};
    }

    // Create checked out snapshot
    auto snapshot = std::make_unique<CCheckedOutSnapshot>(currentSnapshot.snapshot, currentSnapshot.key);

    return {currentSnapshot.changed, std::make_unique<CStorageLevelDB>(db->GetDB(), snapshot)};
}

void CSnapshotManager::ReturnSnapshot(const CBlockSnapshotKey &key) {
    std::unique_lock<std::mutex> lock(mtx);

    assert(checkedOutMap.count(key));
    --checkedOutMap.at(key).count;

    // Release if not in use and not the current block
    if (!checkedOutMap.count(key) && currentSnapshot.key.hash != key.hash) {
        db->GetDB()->ReleaseSnapshot(checkedOutMap.at(key).snapshot);
        checkedOutMap.erase(key);
    }
}

std::unique_ptr<CSnapshotManager> psnapshotManager;
