
#include <flushablestorage.h>

// doesn't serialize/deserialize vector size
template<typename T>
struct RawTBytes {
    std::reference_wrapper<T> ref;

    template<typename Stream>
    void Serialize(Stream& os) const {
        auto& val = ref.get();
        os.write((char*)val.data(), val.size());
    }

    template<typename Stream>
    void Unserialize(Stream& is) {
        auto& val = ref.get();
        val.resize(is.size());
        is.read((char*)val.data(), is.size());
    }
};

template<typename T>
inline RawTBytes<T> refTBytes(T& val) {
    return RawTBytes<T>{val};
}

CLevelDBSnapshot::CLevelDBSnapshot(std::shared_ptr<CDBWrapper> db) : db(db) {
    snapshot = db->GetSnapshot();
}

CLevelDBSnapshot::~CLevelDBSnapshot() {
    db->ReleaseSnapshot(snapshot);
}

CLevelDBSnapshot::operator const leveldb::Snapshot*() const {
    return snapshot;
}

// LevelDB iterator
CStorageLevelDBIterator::CStorageLevelDBIterator(std::shared_ptr<CDBWrapper> db,
                                                 std::shared_ptr<CLevelDBSnapshot> s) : snapshot(s) {
    auto options = db->GetIterOptions();
    options.snapshot = *snapshot;
    it.reset(db->NewIterator(options));
}

void CStorageLevelDBIterator::Next() {
    it->Next();
}

void CStorageLevelDBIterator::Prev() {
    it->Prev();
}

bool CStorageLevelDBIterator::Valid() {
    return it->Valid();
}

void CStorageLevelDBIterator::Seek(const TBytes& key) {
    it->Seek(refTBytes(key));
}

TBytes CStorageLevelDBIterator::Key() {
    TBytes key;
    auto rawKey = refTBytes(key);
    return it->GetKey(rawKey) ? key : TBytes{};
}

TBytes CStorageLevelDBIterator::Value() {
    TBytes value;
    auto rawValue = refTBytes(value);
    return it->GetValue(rawValue) ? value : TBytes{};
}

// Flushable iterator
CFlushableStorageKVIterator::CFlushableStorageKVIterator(
                                std::unique_ptr<CStorageKVIterator>&& pIt, const MapKV& map)
                                : map(map), pIt(std::move(pIt)) {
    itState = Invalid;
}

void CFlushableStorageKVIterator::Next() {
    assert(Valid());
    mIt = Advance(mIt, map.end(), std::greater<TBytes>{}, Key());
}

void CFlushableStorageKVIterator::Prev() {
    assert(Valid());
    auto tmp = mIt;
    if (tmp != map.end()) {
        ++tmp;
    }
    auto it = std::reverse_iterator<decltype(tmp)>(tmp);
    auto end = Advance(it, map.rend(), std::less<TBytes>{}, Key());
    if (end == map.rend()) {
        mIt = map.begin();
    } else {
        auto offset = mIt == map.end() ? 1 : 0;
        std::advance(mIt, -std::distance(it, end) - offset);
    }
}

void CFlushableStorageKVIterator::Seek(const TBytes& key) {
    pIt->Seek(key);
    mIt = Advance(map.lower_bound(key), map.end(), std::greater<TBytes>{}, {});
}

bool CFlushableStorageKVIterator::Valid() {
    return itState != Invalid;
}

TBytes CFlushableStorageKVIterator::Key() {
    assert(Valid());
    return itState == Map ? mIt->first : pIt->Key();
}

TBytes CFlushableStorageKVIterator::Value() {
    assert(Valid());
    return itState == Map ? *mIt->second : pIt->Value();
}

template<typename TIterator, typename Compare>
TIterator CFlushableStorageKVIterator::Advance(TIterator it, TIterator end, Compare comp, TBytes prevKey) {

    while (it != end || pIt->Valid()) {
        while (it != end && (!pIt->Valid() || !comp(it->first, pIt->Key()))) {
            if (prevKey.empty() || comp(it->first, prevKey)) {
                if (it->second) {
                    itState = Map;
                    return it;
                } else {
                    prevKey = it->first;
                }
            }
            ++it;
        }
        if (pIt->Valid()) {
            if (prevKey.empty() || comp(pIt->Key(), prevKey)) {
                itState = Parent;
                return it;
            }
            if constexpr (std::is_same_v<TIterator, decltype(mIt)>) {
                pIt->Next();
            } else {
                pIt->Prev();
            }
        }
    }
    itState = Invalid;
    return it;
}

// iterator interface
void CStorageKVIterator::Next() {
    std::visit([](auto& it) { it.Next(); }, iterators);
}

void CStorageKVIterator::Prev() {
    std::visit([](auto& it) { it.Prev(); }, iterators);
}

void CStorageKVIterator::Seek(const TBytes& key) {
    std::visit([&](auto& it) { it.Seek(key); }, iterators);
}

TBytes CStorageKVIterator::Key() {
    return std::visit([](auto& it) { return it.Key(); }, iterators);
}

TBytes CStorageKVIterator::Value() {
    return std::visit([](auto& it) { return it.Value(); }, iterators);
}

bool CStorageKVIterator::Valid() {
    return std::visit([](auto& it) { return it.Valid(); }, iterators);
}

// LevelDB storage
CStorageLevelDB::CStorageLevelDB(const fs::path& dbName,
                                 std::size_t cacheSize,
                                 bool fMemory, bool fWipe)
    : db(std::make_shared<CDBWrapper>(dbName, cacheSize, fMemory, fWipe))
    , batch(std::make_shared<CDBBatch>(*db))
    , snapshot(std::make_shared<CLevelDBSnapshot>(db)) {}

bool CStorageLevelDB::Erase(const TBytes& key) {
    batch->Erase(refTBytes(key));
    return true;
}

bool CStorageLevelDB::Exists(const TBytes& key) const {
    auto snapshotEx = snapshot;
    auto options = db->GetReadOptions();
    options.snapshot = *snapshotEx;
    return db->Exists(refTBytes(key), options);
}

bool CStorageLevelDB::Read(const TBytes& key, TBytes& value) const {
    auto rawVal = refTBytes(value);
    auto snapshotEx = snapshot;
    auto options = db->GetReadOptions();
    options.snapshot = *snapshotEx;
    return db->Read(refTBytes(key), rawVal, options);
}

bool CStorageLevelDB::Write(const TBytes& key, const TBytes& value) {
    batch->Write(refTBytes(key), refTBytes(value));
    return true;
}

bool CStorageLevelDB::Flush(bool sync) {
    auto result = db->WriteBatch(*batch, sync);
    batch->Clear();
    snapshot = std::make_shared<CLevelDBSnapshot>(db);
    return result;
}

void CStorageLevelDB::Discard() {
    batch->Clear();
}

size_t CStorageLevelDB::SizeEstimate() const {
    return batch->SizeEstimate();
}

CStorageKVIterator CStorageLevelDB::NewIterator() const {
    return CStorageLevelDBIterator{db, snapshot};
}

void CStorageLevelDB::Compact(const TBytes& begin, const TBytes& end) {
    db->CompactRange(refTBytes(begin), refTBytes(end));
}

bool CStorageLevelDB::IsEmpty() const {
    return db->IsEmpty();
}

// Flushable storage
CFlushableStorageKV::CFlushableStorageKV(std::shared_ptr<CStorageKV> db) : db(db) {}

bool CFlushableStorageKV::Exists(const TBytes& key) const {
    auto it = changed.find(key);
    if (it != changed.end()) {
        return bool(it->second);
    }
    return db->Exists(key);
}

bool CFlushableStorageKV::Write(const TBytes& key, const TBytes& value) {
    changed[key] = value;
    return true;
}

bool CFlushableStorageKV::Erase(const TBytes& key) {
    changed[key] = {};
    return true;
}

bool CFlushableStorageKV::Read(const TBytes& key, TBytes& value) const {
    auto it = changed.find(key);
    if (it == changed.end()) {
        return db->Read(key, value);
    } else if (it->second) {
        value = it->second.value();
        return true;
    } else {
        return false;
    }
}

bool CFlushableStorageKV::Flush(bool) {
    for (const auto& it : changed) {
        if (!it.second) {
            db->Erase(it.first);
        } else {
            db->Write(it.first, it.second.value());
        }
    }
    changed.clear();
    return true;
}

void CFlushableStorageKV::Discard() {
    changed.clear();
}

size_t CFlushableStorageKV::SizeEstimate() const {
    return memusage::DynamicUsage(changed);
}

CStorageKVIterator CFlushableStorageKV::NewIterator() const {
    auto it = std::make_unique<CStorageKVIterator>(db->NewIterator());
    return CFlushableStorageKVIterator{std::move(it), changed};
}

void CFlushableStorageKV::SetStorage(std::shared_ptr<CStorageKV> db) {
    this->db = db;
}

const MapKV& CFlushableStorageKV::GetRaw() const {
    return changed;
}

void CFlushableStorageKV::Compact(const TBytes& begin, const TBytes& end) {
    if (changed.key_comp()(begin, end)) {
        auto first = changed.find(begin);
        if (first != changed.end()) {
            changed.erase(first, changed.upper_bound(end));
        }
    }
}

bool CFlushableStorageKV::IsEmpty() const {
    return changed.empty();
}

// Storage interface
bool CStorageKV::Erase(const TBytes& key) {
    return std::visit([&](auto& db) { return db.Erase(key); }, dbs);
}

bool CStorageKV::Exists(const TBytes& key) const {
    return std::visit([&](auto& db) { return db.Exists(key); }, dbs);
}

bool CStorageKV::Read(const TBytes& key, TBytes& value) const {
    return std::visit([&](auto& db) { return db.Read(key, value); }, dbs);
}

bool CStorageKV::Write(const TBytes& key, const TBytes& value) {
    return std::visit([&](auto& db) { return db.Write(key, value); }, dbs);
}

CStorageKVIterator CStorageKV::NewIterator() const {
    return std::visit([](auto& db) { return db.NewIterator(); }, dbs);
}

size_t CStorageKV::SizeEstimate() const {
    return std::visit([](auto& db) { return db.SizeEstimate(); }, dbs);
}

bool CStorageKV::Flush(bool sync) {
    return std::visit([sync](auto& db) { return db.Flush(sync); }, dbs);
}

void CStorageKV::Discard() {
    std::visit([](auto& db) { db.Discard(); }, dbs);
}

void CStorageKV::Compact(const TBytes& begin, const TBytes& end) {
    std::visit([&](auto& db) { db.Compact(begin, end); }, dbs);
}

bool CStorageKV::IsEmpty() const {
    return std::visit([](auto& db) { return db.IsEmpty(); }, dbs);
}

CStorageLevelDB* CStorageKV::GetStorageLevelDB() {
    return std::get_if<CStorageLevelDB>(&dbs);
}

CFlushableStorageKV* CStorageKV::GetFlushableStorage() {
    return std::get_if<CFlushableStorageKV>(&dbs);
}

// Storage abstract translator
CStorageView::CStorageView(std::shared_ptr<CStorageKV> db) : db(db) {}

void CStorageView::Discard() {
    db->Discard();
}

CStorageKV& CStorageView::GetStorage() {
    return *db;
}

bool CStorageView::IsEmpty() const {
    return db->IsEmpty();
}

size_t CStorageView::SizeEstimate() const {
    return db->SizeEstimate();
}

bool CStorageView::Flush(bool sync) {
    return db->Flush(sync);
}

void CStorageView::Compact(const TBytes& begin, const TBytes& end) {
    db->Compact(begin, end);
}

static std::shared_ptr<CStorageKV> CloneLevelDB(std::shared_ptr<CStorageKV> db) {
    if (auto ldb = db->GetStorageLevelDB()) {
        return std::make_shared<CStorageKV>(*ldb);
    }
    return db;
}

void CStorageView::SetBackend(CStorageView& backend) {
    if (auto flushable = db->GetFlushableStorage()) {
        flushable->SetStorage(CloneLevelDB(backend.db));
    } else {
        db = backend.Clone();
    }
}

std::shared_ptr<CStorageKV> CStorageView::Clone() {
    auto clone = CloneLevelDB(db);
    return std::make_shared<CStorageKV>(CFlushableStorageKV{clone});
}
