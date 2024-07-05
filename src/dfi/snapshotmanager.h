#ifndef DEFI_DFI_SNAPSHOTMANAGER_H
#define DEFI_DFI_SNAPSHOTMANAGER_H

#include <uint256.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class CAccountHistoryStorage;
class CBlockIndex;
class CCustomCSView;
class CDBWrapper;
class CFlushableStorageKV;
class CSnapshotManager;
class CStorageLevelDB;
class CVaultHistoryStorage;

namespace leveldb {
    class Snapshot;
}

using TBytes = std::vector<unsigned char>;
using MapKV = std::map<TBytes, std::optional<TBytes>>;

using SnapshotCollection = std::tuple<std::unique_ptr<CCustomCSView>,
                                      std::unique_ptr<CAccountHistoryStorage>,
                                      std::unique_ptr<CVaultHistoryStorage>>;

SnapshotCollection GetSnapshots();

enum class SnapshotType : uint8_t { VIEW, HISTORY, VAULT };

struct CBlockSnapshotKey {
    SnapshotType type{};
    int64_t height{};
    uint256 hash{};

    struct Comparator {
        bool operator()(const CBlockSnapshotKey &a, const CBlockSnapshotKey &b) const {
            if (a.type < b.type) {
                return true;
            }
            if (a.type > b.type) {
                return false;
            }
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

class CBlockSnapshot {
    const leveldb::Snapshot *snapshot{};
    MapKV changed;
    CBlockSnapshotKey key;

public:
    CBlockSnapshot(const leveldb::Snapshot *otherSnapshot, const MapKV &otherChanged, const CBlockSnapshotKey &otherKey)
        : snapshot(otherSnapshot),
          changed(otherChanged),
          key(otherKey) {}

    [[nodiscard]] const leveldb::Snapshot *GetLevelDBSnapshot() const { return snapshot; }
    [[nodiscard]] const CBlockSnapshotKey &GetKey() const { return key; }
    [[nodiscard]] const MapKV &GetChanged() const { return changed; }
};

class CCheckedOutSnapshot {
    const leveldb::Snapshot *snapshot;
    CBlockSnapshotKey key;

public:
    explicit CCheckedOutSnapshot(const leveldb::Snapshot *otherSnapshot, const CBlockSnapshotKey &otherKey)
        : snapshot(otherSnapshot),
          key(otherKey) {}
    CCheckedOutSnapshot(const CCheckedOutSnapshot &) = delete;
    CCheckedOutSnapshot &operator=(const CCheckedOutSnapshot &) = delete;

    ~CCheckedOutSnapshot();

    [[nodiscard]] const leveldb::Snapshot *GetLevelDBSnapshot() const { return snapshot; }
};

class CSnapshotManager {
    std::unique_ptr<CBlockSnapshot> currentViewSnapshot;
    std::unique_ptr<CBlockSnapshot> currentHistorySnapshot;
    std::unique_ptr<CBlockSnapshot> currentVaultSnapshot;

    std::mutex mtx;
    std::shared_ptr<CDBWrapper> viewDB;
    std::shared_ptr<CDBWrapper> historyDB;
    std::shared_ptr<CDBWrapper> vaultDB;

    using CheckoutOutMap = std::map<CBlockSnapshotKey, CBlockSnapshotValue, CBlockSnapshotKey::Comparator>;
    CheckoutOutMap checkedOutViewMap;
    CheckoutOutMap checkedOutHistoryMap;
    CheckoutOutMap checkedOutVaultMap;

public:
    CSnapshotManager() = delete;
    CSnapshotManager(std::unique_ptr<CCustomCSView> &otherViewDB,
                     std::unique_ptr<CAccountHistoryStorage> &otherHistoryDB,
                     std::unique_ptr<CVaultHistoryStorage> &otherVaultDB);

    CSnapshotManager(const CSnapshotManager &other) = delete;
    CSnapshotManager &operator=(const CSnapshotManager &other) = delete;

    SnapshotCollection GetSnapshots();
    void SetBlockSnapshots(CFlushableStorageKV &viewStorge,
                           CAccountHistoryStorage *historyView,
                           CVaultHistoryStorage *vaultView,
                           const CBlockIndex *block,
                           const bool nearTip);
    void ReturnSnapshot(const CBlockSnapshotKey &key);

private:
    std::optional<SnapshotCollection> GetCurrentSnapshots();
    SnapshotCollection GetGlobalSnapshots();
    std::pair<MapKV, std::unique_ptr<CStorageLevelDB>> CheckoutViewSnapshot();
    std::unique_ptr<CCheckedOutSnapshot> CheckoutHistorySnapshot();
    std::unique_ptr<CCheckedOutSnapshot> CheckoutVaultSnapshot();
    std::pair<MapKV, std::unique_ptr<CStorageLevelDB>> GetGlobalViewSnapshot();
    std::unique_ptr<CCheckedOutSnapshot> GetGlobalHistorySnapshot();
    std::unique_ptr<CCheckedOutSnapshot> GetGlobalVaultSnapshot();
};

extern std::unique_ptr<CSnapshotManager> psnapshotManager;

#endif  // DEFI_DFI_SNAPSHOTMANAGER_H
