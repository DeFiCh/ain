// Copyright (c) 2019 The Wagerr developers
// Distributed under the MIT/X11 software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_FLUSHABLESTORAGE_H
#define DEFI_FLUSHABLESTORAGE_H

#include <shutdown.h>

#include <dbwrapper.h>
#include <functional>
#include <map>
#include <memusage.h>

#include <optional>

using TBytes = std::vector<unsigned char>;
using MapKV = std::map<TBytes, std::optional<TBytes>>;

template<typename T>
static TBytes DbTypeToBytes(const T& value) {
    TBytes bytes;
    CVectorWriter stream(SER_DISK, CLIENT_VERSION, bytes, 0);
    stream << value;
    return bytes;
}

template<typename T>
static bool BytesToDbType(const TBytes& bytes, T& value) {
    try {
        VectorReader stream(SER_DISK, CLIENT_VERSION, bytes, 0);
        stream >> value;
//        assert(stream.size() == 0); // will fail with partial key matching
    }
    catch (const std::ios_base::failure&) {
        return false;
    }
    return true;
}

// Key-Value storage iterator interface
class CStorageKVIterator {
public:
    virtual ~CStorageKVIterator() = default;
    virtual void Seek(const TBytes& key) = 0;
    virtual void Next() = 0;
    virtual void Prev() = 0;
    virtual bool Valid() = 0;
    virtual TBytes Key() = 0;
    virtual TBytes Value() = 0;
};

// Represents an empty iterator
class CStorageKVEmptyIterator : public CStorageKVIterator {
public:
    ~CStorageKVEmptyIterator() override = default;
    void Seek(const TBytes&) override {}
    void Next() override {}
    void Prev() override {}
    bool Valid() override { return false; }
    TBytes Key() override { return {}; }
    TBytes Value() override { return {}; }
};

// Key-Value storage interface
class CStorageKV {
public:
    virtual ~CStorageKV() = default;
    virtual bool Exists(const TBytes& key) const = 0;
    virtual bool Write(const TBytes& key, const TBytes& value) = 0;
    virtual bool Erase(const TBytes& key) = 0;
    virtual bool Read(const TBytes& key, TBytes& value) const = 0;
    virtual std::unique_ptr<CStorageKVIterator> NewIterator() = 0;
    virtual size_t SizeEstimate() const = 0;
    virtual void Discard() = 0;
    virtual bool Flush() = 0;
};

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

class CLevelDBSnapshot {
public:
    CLevelDBSnapshot(std::shared_ptr<CDBWrapper> db) : db(db) {
        snapshot = db->GetSnapshot();
    }
    ~CLevelDBSnapshot() {
        db->ReleaseSnapshot(snapshot);
    }
    operator const leveldb::Snapshot*() const {
        return snapshot;
    }
private:
    std::shared_ptr<CDBWrapper> db;
    const leveldb::Snapshot* snapshot;
};

// LevelDB glue layer Iterator
class CStorageLevelDBIterator : public CStorageKVIterator {
public:
    CStorageLevelDBIterator(std::shared_ptr<CDBWrapper> db, std::shared_ptr<CLevelDBSnapshot> s) : snapshot(s) {
        auto options = db->GetIterOptions();
        options.snapshot = *snapshot;
        it.reset(db->NewIterator(options));
    }

    void Seek(const TBytes& key) override {
        it->Seek(refTBytes(key)); // lower_bound in fact
    }
    void Next() override {
        it->Next();
    }
    void Prev() override {
        it->Prev();
    }
    bool Valid() override {
        return it->Valid();
    }
    TBytes Key() override {
        TBytes key;
        auto rawKey = refTBytes(key);
        return it->GetKey(rawKey) ? key : TBytes{};
    }
    TBytes Value() override {
        TBytes value;
        auto rawValue = refTBytes(value);
        return it->GetValue(rawValue) ? value : TBytes{};
    }
private:
    std::unique_ptr<CDBIterator> it;
    std::shared_ptr<CLevelDBSnapshot> snapshot;
};

// LevelDB glue layer storage
class CStorageLevelDB : public CStorageKV {
public:
    explicit CStorageLevelDB(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false)
                            : db(std::make_shared<CDBWrapper>(dbName, cacheSize, fMemory, fWipe))
                            , batch(std::make_shared<CDBBatch>(*db))
                            , snapshot(std::make_shared<CLevelDBSnapshot>(db)) {}

    bool Exists(const TBytes& key) const override {
        auto options = db->GetReadOptions();
        options.snapshot = *snapshot;
        return db->Exists(refTBytes(key), options);
    }
    bool Write(const TBytes& key, const TBytes& value) override {
        batch->Write(refTBytes(key), refTBytes(value));
        return true;
    }
    bool Erase(const TBytes& key) override {
        batch->Erase(refTBytes(key));
        return true;
    }
    bool Read(const TBytes& key, TBytes& value) const override {
        auto rawVal = refTBytes(value);
        auto options = db->GetReadOptions();
        options.snapshot = *snapshot;
        return db->Read(refTBytes(key), rawVal, options);
    }
    bool Flush() override { // Commit batch
        auto result = db->WriteBatch(*batch);
        batch->Clear();
        snapshot = std::make_shared<CLevelDBSnapshot>(db);
        return result;
    }
    void Discard() override {
        batch->Clear();
    }
    size_t SizeEstimate() const override {
        return batch->SizeEstimate();
    }
    std::unique_ptr<CStorageKVIterator> NewIterator() override {
        return std::make_unique<CStorageLevelDBIterator>(db, snapshot);
    }
    void Compact(const TBytes& begin, const TBytes& end) {
        db->CompactRange(refTBytes(begin), refTBytes(end));
    }
    bool IsEmpty() {
        return db->IsEmpty();
    }

private:
    std::shared_ptr<CDBWrapper> db;
    std::shared_ptr<CDBBatch> batch;
    std::shared_ptr<CLevelDBSnapshot> snapshot;
};

// Flashable storage

// Flushable Key-Value Storage Iterator
class CFlushableStorageKVIterator : public CStorageKVIterator {
public:
    CFlushableStorageKVIterator(const CFlushableStorageKVIterator&) = delete;
    CFlushableStorageKVIterator(std::unique_ptr<CStorageKVIterator>&& pIt, MapKV& map) : map(map), pIt(std::move(pIt)) {
        itState = Invalid;
    }

    void Seek(const TBytes& key) override {
        pIt->Seek(key);
        mIt = Advance(map.lower_bound(key), map.end(), std::greater<TBytes>{}, {});
    }
    void Next() override {
        assert(Valid());
        mIt = Advance(mIt, map.end(), std::greater<TBytes>{}, Key());
    }
    void Prev() override {
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
    bool Valid() override {
        return itState != Invalid;
    }
    TBytes Key() override {
        assert(Valid());
        return itState == Map ? mIt->first : pIt->Key();
    }
    TBytes Value() override {
        assert(Valid());
        return itState == Map ? *mIt->second : pIt->Value();
    }
private:
    template<typename TIterator, typename Compare>
    TIterator Advance(TIterator it, TIterator end, Compare comp, TBytes prevKey) {

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
    const MapKV& map;
    MapKV::const_iterator mIt;
    std::unique_ptr<CStorageKVIterator> pIt;
    enum IteratorState { Invalid, Map, Parent } itState;
};

// Flushable Key-Value Storage
class CFlushableStorageKV : public CStorageKV {
public:
    explicit CFlushableStorageKV(std::shared_ptr<CStorageKV> db) : db(db) {}
    CFlushableStorageKV(const CFlushableStorageKV&) = delete;

    bool Exists(const TBytes& key) const override {
        auto it = changed.find(key);
        if (it != changed.end()) {
            return bool(it->second);
        }
        return db->Exists(key);
    }
    bool Write(const TBytes& key, const TBytes& value) override {
        changed[key] = value;
        return true;
    }
    bool Erase(const TBytes& key) override {
        changed[key] = {};
        return true;
    }
    bool Read(const TBytes& key, TBytes& value) const override {
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
    bool Flush() override {
        for (const auto& it : changed) {
            if (!it.second) {
                if (!db->Erase(it.first)) {
                    return false;
                }
            } else if (!db->Write(it.first, it.second.value())) {
                return false;
            }
        }
        changed.clear();
        return true;
    }
    void Discard() override {
        changed.clear();
    }
    size_t SizeEstimate() const override {
        return memusage::DynamicUsage(changed);
    }
    std::unique_ptr<CStorageKVIterator> NewIterator() override {
        return std::make_unique<CFlushableStorageKVIterator>(db->NewIterator(), changed);
    }
    const MapKV& GetRaw() {
        return changed;
    }

private:
    std::shared_ptr<CStorageKV> db;
    MapKV changed;
};

template<typename T>
class CLazySerialize {
    std::optional<T> value;
    std::unique_ptr<CStorageKVIterator>& it;

public:
    CLazySerialize(const CLazySerialize&) = default;
    explicit CLazySerialize(std::unique_ptr<CStorageKVIterator>& it) : it(it) {}

    operator T() & {
        return get();
    }
    operator T() && {
        get();
        return std::move(*value);
    }
    const T& get() {
        if (!value) {
            value = T{};
            BytesToDbType(it->Value(), *value);
        }
        return *value;
    }
};

template<typename By, typename KeyType>
class CStorageIteratorWrapper {
    bool valid = false;
    std::pair<uint8_t, KeyType> key;
    std::unique_ptr<CStorageKVIterator> it;

    void UpdateValidity() {
        valid = it->Valid() && BytesToDbType(it->Key(), key) && key.first == By::prefix();
    }

    struct Resolver {
        std::unique_ptr<CStorageKVIterator>& it;

        template<typename T>
        inline operator CLazySerialize<T>() {
            return CLazySerialize<T>{it};
        }
        template<typename T>
        inline T as() {
            return CLazySerialize<T>{it};
        }
        template<typename T>
        inline operator T() {
            return as<T>();
        }
    };

public:
    CStorageIteratorWrapper(CStorageIteratorWrapper&&) = default;
    explicit CStorageIteratorWrapper(std::unique_ptr<CStorageKVIterator> it) : it(std::move(it)) {}

    bool Valid() {
        return valid;
    }
    const KeyType& Key() {
        assert(Valid());
        return key.second;
    }
    Resolver Value() {
        assert(Valid());
        return Resolver{it};
    }
    void Next() {
        assert(Valid());
        it->Next();
        UpdateValidity();
    }
    void Prev() {
        assert(Valid());
        it->Prev();
        UpdateValidity();
    }
    void Seek(const KeyType& newKey) {
        key = std::make_pair(By::prefix(), newKey);
        it->Seek(DbTypeToBytes(key));
        UpdateValidity();
    }
    template<typename T>
    bool Value(T& value) {
        assert(Valid());
        return BytesToDbType(it->Value(), value);
    }
};

// Creates an iterator to single level key value storage
template<typename By, typename KeyType>
CStorageIteratorWrapper<By, KeyType> NewKVIterator(const KeyType& key, MapKV& map) {
    auto emptyParent = std::make_unique<CStorageKVEmptyIterator>();
    auto flushableIterator = std::make_unique<CFlushableStorageKVIterator>(std::move(emptyParent), map);
    CStorageIteratorWrapper<By, KeyType> it{std::move(flushableIterator)};
    it.Seek(key);
    return it;
}

class CStorageView {
public:
    CStorageView() = default;
    CStorageView(const CStorageView&) = delete;
    CStorageView(CStorageView& o) : db(o.Clone()) {}
    virtual ~CStorageView() = default;

    template<typename KeyType>
    bool Exists(const KeyType& key) const {
        return db->Exists(DbTypeToBytes(key));
    }
    template<typename By, typename KeyType>
    bool ExistsBy(const KeyType& key) const {
        return Exists(std::make_pair(By::prefix(), key));
    }

    template<typename KeyType, typename ValueType>
    bool Write(const KeyType& key, const ValueType& value) {
        auto vKey = DbTypeToBytes(key);
        auto vValue = DbTypeToBytes(value);
        return db->Write(vKey, vValue);
    }
    template<typename By, typename KeyType, typename ValueType>
    bool WriteBy(const KeyType& key, const ValueType& value) {
        return Write(std::make_pair(By::prefix(), key), value);
    }

    template<typename KeyType>
    bool Erase(const KeyType& key) {
        auto vKey = DbTypeToBytes(key);
        return db->Exists(vKey) && db->Erase(vKey);
    }
    template<typename By, typename KeyType>
    bool EraseBy(const KeyType& key) {
        return Erase(std::make_pair(By::prefix(), key));
    }

    template<typename KeyType, typename ValueType>
    bool Read(const KeyType& key, ValueType& value) const {
        auto vKey = DbTypeToBytes(key);
        TBytes vValue;
        return db->Read(vKey, vValue) && BytesToDbType(vValue, value);
    }
    template<typename By, typename KeyType, typename ValueType>
    bool ReadBy(const KeyType& key, ValueType& value) const {
        return Read(std::make_pair(By::prefix(), key), value);
    }
    // second type of 'ReadBy' (may be 'GetBy'?)
    template<typename By, typename ResultType, typename KeyType>
    std::optional<ResultType> ReadBy(KeyType const & id) const {
        ResultType result;
        if (ReadBy<By>(id, result))
            return result;
        return {};
    }
    template<typename By, typename KeyType>
    CStorageIteratorWrapper<By, KeyType> LowerBound(KeyType const & key) {
        CStorageIteratorWrapper<By, KeyType> it{db->NewIterator()};
        it.Seek(key);
        return it;
    }
    template<typename By, typename KeyType, typename ValueType>
    void ForEach(std::function<bool(KeyType const &, CLazySerialize<ValueType>)> callback, KeyType const & start = {}) {
        for(auto it = LowerBound<By>(start); it.Valid(); it.Next()) {
            if (!callback(it.Key(), it.Value())) {
                break;
            }
        }
    }

    bool Flush() { return db->Flush(); }
    void Discard() { db->Discard(); }
    CStorageKV& GetStorage() { return *db; }
    size_t SizeEstimate() const { return db->SizeEstimate(); }

protected:
    CStorageView(std::shared_ptr<CStorageKV> db) : db(db) {}

    std::shared_ptr<CStorageKV> Clone() {
        auto clone = db;
        if (auto ldb = std::dynamic_pointer_cast<CStorageLevelDB>(db)) {
            clone = std::make_shared<CStorageLevelDB>(*ldb);
        }
        return std::make_shared<CFlushableStorageKV>(clone);
    }
private:
    std::shared_ptr<CStorageKV> db;
};

#endif // DEFI_FLUSHABLESTORAGE_H
