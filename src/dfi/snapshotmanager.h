#ifndef DEFI_DFI_SNAPSHOTMANAGER_H
#define DEFI_DFI_SNAPSHOTMANAGER_H

#include <uint256.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class CBlockIndex;
class CCustomCSView;
class CSnapshotManager;
class CStorageLevelDB;

namespace leveldb {
    class Snapshot;
}

using TBytes = std::vector<unsigned char>;
using MapKV = std::map<TBytes, std::optional<TBytes>>;

std::unique_ptr<CCustomCSView> GetViewSnapshot();

struct CBlockSnapshotKey {
    int64_t height{};
    uint256 hash{};

    struct Comparator {
        bool operator()(const CBlockSnapshotKey &a, const CBlockSnapshotKey &b) const {
            if (a.height < b.height) {
                return true;
            }
            if (a.height > b.height) {
                return false;
            }
            return a.hash < b.hash;
        }
    };
};

struct CBlockSnapshotValue {
    const leveldb::Snapshot *snapshot;
    int64_t count;
};

struct CBlockSnapshot {
    const leveldb::Snapshot *snapshot{};
    MapKV changed;
    CBlockSnapshotKey key;

    CBlockSnapshot(const leveldb::Snapshot *snapshot, MapKV changed, const CBlockSnapshotKey &key)
        : snapshot(snapshot),
          changed(std::move(changed)),
          key(key) {}
};

class CCheckedOutSnapshot {
public:
    explicit CCheckedOutSnapshot(const leveldb::Snapshot *other, const CBlockSnapshotKey &otherKey)
        : snapshot(other),
          key(otherKey) {}
    CCheckedOutSnapshot(const CCheckedOutSnapshot &) = delete;
    CCheckedOutSnapshot &operator=(const CCheckedOutSnapshot &) = delete;

    ~CCheckedOutSnapshot();

    [[nodiscard]] const leveldb::Snapshot *GetLevelDBSnapshot() const { return snapshot; }

private:
    const leveldb::Snapshot *snapshot;
    CBlockSnapshotKey key;
};

class CSnapshotManager {
    std::unique_ptr<CBlockSnapshot> currentSnapshot;
    std::mutex mtx;
    CStorageLevelDB *db;
    std::map<CBlockSnapshotKey, CBlockSnapshotValue, CBlockSnapshotKey::Comparator> checkedOutMap;

public:
    CSnapshotManager() = delete;
    explicit CSnapshotManager(CStorageLevelDB *other);

    CSnapshotManager(const CSnapshotManager &other) = delete;
    CSnapshotManager &operator=(const CSnapshotManager &other) = delete;

    void SetBlockSnapshot(CCustomCSView &view, const CBlockIndex *block);
    bool CheckoutSnapshot(MapKV &changed, std::unique_ptr<CStorageLevelDB> &snapshotDB);
    void GetGlobalSnapshot(MapKV &changed, std::unique_ptr<CStorageLevelDB> &snapshotDB);
    void ReturnSnapshot(const CBlockSnapshotKey &key);
    void EraseCurrentSnapshot();
};

extern std::unique_ptr<CSnapshotManager> psnapshotManager;

#endif  // DEFI_DFI_SNAPSHOTMANAGER_H
