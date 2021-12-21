// Copyright (c) 2019 The Wagerr developers
// Distributed under the MIT/X11 software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_FLUSHABLESTORAGE_H
#define DEFI_FLUSHABLESTORAGE_H

#include <dbwrapper.h>
#include <memusage.h>
#include <shutdown.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <variant>

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

// Represents an empty iterator
class CStorageKVEmptyIterator {
public:
    void Next() {}
    void Prev() {}
    void Seek(const TBytes&) {}
    TBytes Key() { return {}; }
    TBytes Value() { return {}; }
    bool Valid() { return false; }
};

class CLevelDBSnapshot {
public:
    explicit CLevelDBSnapshot(std::shared_ptr<CDBWrapper> db);
    ~CLevelDBSnapshot();
    operator const leveldb::Snapshot*() const;
private:
    std::shared_ptr<CDBWrapper> db;
    const leveldb::Snapshot* snapshot;
};

// LevelDB glue layer Iterator
class CStorageLevelDBIterator {
public:
    CStorageLevelDBIterator(CStorageLevelDBIterator&&) = default;
    CStorageLevelDBIterator(const CStorageLevelDBIterator&) = delete;
    CStorageLevelDBIterator(std::shared_ptr<CDBWrapper> db, std::shared_ptr<CLevelDBSnapshot> s);

    void Next();
    void Prev();
    bool Valid();
    TBytes Key();
    TBytes Value();
    void Seek(const TBytes& key);

private:
    std::unique_ptr<CDBIterator> it;
    std::shared_ptr<CLevelDBSnapshot> snapshot;
};

class CStorageKVIterator;

// Flushable Key-Value Storage Iterator
class CFlushableStorageKVIterator {
public:
    CFlushableStorageKVIterator(CFlushableStorageKVIterator&&) = default;
    CFlushableStorageKVIterator(const CFlushableStorageKVIterator&) = delete;
    CFlushableStorageKVIterator(std::unique_ptr<CStorageKVIterator>&& pIt, const MapKV& map);

    void Next();
    void Prev();
    bool Valid();
    TBytes Key();
    TBytes Value();
    void Seek(const TBytes& key);

private:
    template<typename TIterator, typename Compare>
    TIterator Advance(TIterator it, TIterator end, Compare comp, TBytes prevKey);

    const MapKV& map;
    MapKV::const_iterator mIt;
    std::unique_ptr<CStorageKVIterator> pIt;
    enum IteratorState { Invalid, Map, Parent } itState;
};

class CStorageKVIterator {
public:
    template<typename T>
    CStorageKVIterator(T&& t) : iterators(std::forward<T>(t)) {}
    CStorageKVIterator(CStorageKVIterator&&) = default;
    CStorageKVIterator(const CStorageKVIterator&) = delete;

    void Next();
    void Prev();
    bool Valid();
    TBytes Key();
    TBytes Value();
    void Seek(const TBytes& key);

private:
    std::variant<CStorageKVEmptyIterator,
                 CStorageLevelDBIterator,
                 CFlushableStorageKVIterator> iterators;
};

// LevelDB glue layer storage
class CStorageLevelDB {
public:
    CStorageLevelDB(const fs::path& dbName, std::size_t cacheSize,
                    bool fMemory = false, bool fWipe = false);

    bool Erase(const TBytes& key);
    bool Exists(const TBytes& key) const;
    bool Read(const TBytes& key, TBytes& value) const;
    bool Write(const TBytes& key, const TBytes& value);
    bool Flush();
    void Discard();
    bool IsEmpty() const;
    size_t SizeEstimate() const;
    CStorageKVIterator NewIterator() const;
    void Compact(const TBytes& begin, const TBytes& end);

private:
    std::shared_ptr<CDBWrapper> db;
    std::shared_ptr<CDBBatch> batch;
    std::shared_ptr<CLevelDBSnapshot> snapshot;
};

class CStorageKV;

// Flushable Key-Value Storage
class CFlushableStorageKV {
public:
    explicit CFlushableStorageKV(std::shared_ptr<CStorageKV> db);

    bool Erase(const TBytes& key);
    bool Exists(const TBytes& key) const;
    bool Read(const TBytes& key, TBytes& value) const;
    bool Write(const TBytes& key, const TBytes& value);
    bool Flush();
    void Discard();
    bool IsEmpty() const;
    const MapKV& GetRaw() const;
    size_t SizeEstimate() const;
    CStorageKVIterator NewIterator() const;
    void SetStorage(std::shared_ptr<CStorageKV> db);
    void Compact(const TBytes& begin, const TBytes& end);

private:
    std::shared_ptr<CStorageKV> db;
    MapKV changed;
};

// Key-Value storage interface
class CStorageKV {
public:
    template<typename T>
    CStorageKV(T&& db) : dbs(std::forward<T>(db)) {}

    bool Erase(const TBytes& key);
    bool Exists(const TBytes& key) const;
    bool Read(const TBytes& key, TBytes& value) const;
    bool Write(const TBytes& key, const TBytes& value);
    bool Flush();
    void Discard();
    bool IsEmpty() const;
    size_t SizeEstimate() const;
    CStorageKVIterator NewIterator() const;
    void Compact(const TBytes& begin, const TBytes& end);
    CStorageLevelDB* GetStorageLevelDB();
    CFlushableStorageKV* GetFlushableStorage();

private:
    std::variant<CStorageLevelDB, CFlushableStorageKV> dbs;
};

template<typename T>
class CLazySerialize {
    std::optional<T> value;
    CStorageKVIterator& it;

public:
    CLazySerialize(const CLazySerialize&) = default;
    explicit CLazySerialize(CStorageKVIterator& it) : it(it) {}

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
            BytesToDbType(it.Value(), *value);
        }
        return *value;
    }
};

template<typename By, typename KeyType>
class CStorageIteratorWrapper {
    bool valid = false;
    CStorageKVIterator it;
    std::pair<uint8_t, KeyType> key;

    void UpdateValidity() {
        valid = it.Valid() && BytesToDbType(it.Key(), key) && key.first == By::prefix();
    }

    class CResolver {
        CStorageKVIterator& it;

    public:
        CResolver(CStorageKVIterator& it) : it(it) {}

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
    CStorageIteratorWrapper(const CStorageIteratorWrapper&) = delete;
    explicit CStorageIteratorWrapper(CStorageKVIterator it) : it(std::move(it)) {}

    bool Valid() {
        return valid;
    }
    const KeyType& Key() {
        assert(Valid());
        return key.second;
    }
    CResolver Value() {
        assert(Valid());
        return CResolver{it};
    }
    void Next() {
        assert(Valid());
        it.Next();
        UpdateValidity();
    }
    void Prev() {
        assert(Valid());
        it.Prev();
        UpdateValidity();
    }
    void Seek(const KeyType& newKey) {
        key = std::make_pair(By::prefix(), newKey);
        it.Seek(DbTypeToBytes(key));
        UpdateValidity();
    }
    template<typename T>
    bool Value(T& value) {
        assert(Valid());
        return BytesToDbType(it.Value(), value);
    }
};

// Creates an iterator to single level key value storage
template<typename By, typename KeyType>
CStorageIteratorWrapper<By, KeyType> NewKVIterator(const KeyType& key, const MapKV& map) {
    auto emptyParent = std::make_unique<CStorageKVIterator>(CStorageKVEmptyIterator{});
    CStorageKVIterator flushableIterator = CFlushableStorageKVIterator{std::move(emptyParent), map};
    CStorageIteratorWrapper<By, KeyType> it{std::move(flushableIterator)};
    it.Seek(key);
    return it;
}

class CStorageView {
public:
    CStorageView() = default;
    CStorageView(CStorageView&&) = default;
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

    bool Flush();
    void Discard();
    bool IsEmpty() const;
    CStorageKV& GetStorage();
    size_t SizeEstimate() const;
    void Compact(const TBytes& begin, const TBytes& end);

protected:
    CStorageView(std::shared_ptr<CStorageKV> db);
    void SetBackend(CStorageView& backend);

private:
    std::shared_ptr<CStorageKV> Clone();
    std::shared_ptr<CStorageKV> db;
};

#endif // DEFI_FLUSHABLESTORAGE_H
