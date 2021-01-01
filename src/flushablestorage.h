// Copyright (c) 2019 The Wagerr developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_FLUSHABLESTORAGE_H
#define DEFI_FLUSHABLESTORAGE_H

#include <dbwrapper.h>
#include <functional>
#include <optional.h>
#include <map>

#include <boost/thread.hpp>

using TBytes = std::vector<unsigned char>;
using MapKV = std::map<TBytes, Optional<TBytes>>;

template<typename T>
static TBytes DbTypeToBytes(const T& value) {
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << value;
    return TBytes(stream.begin(), stream.end());
}

template<typename T>
static bool BytesToDbType(const TBytes& bytes, T& value) {
    try {
        CDataStream stream(bytes, SER_DISK, CLIENT_VERSION);
        stream >> value;
//        assert(stream.size() == 0); // will fail with partial key matching
    }
    catch (std::ios_base::failure&) {
        return false;
    }
    return true;
}

// Key-Value storage iterator interface
class CStorageKVIterator {
public:
    virtual ~CStorageKVIterator() {};
    virtual void Seek(const TBytes& key) = 0;
    virtual void Next() = 0;
    virtual bool Valid() = 0;
    virtual TBytes Key() = 0;
    virtual TBytes Value() = 0;
};

// Key-Value storage interface
class CStorageKV {
public:
    virtual ~CStorageKV() {}
    virtual bool Exists(const TBytes& key) const = 0;
    virtual bool Write(const TBytes& key, const TBytes& value) = 0;
    virtual bool Erase(const TBytes& key) = 0;
    virtual bool Read(const TBytes& key, TBytes& value) const = 0;
    virtual std::unique_ptr<CStorageKVIterator> NewIterator() = 0;
    virtual bool Flush() = 0;
};

// doesn't serialize/deserialize vector size
template<typename T>
struct RawTBytes {
    std::reference_wrapper<T> ref;

    template<typename Stream>
    void Serialize(Stream& os) const
    {
        auto& val = ref.get();
        os.write((char*)val.data(), val.size());
    }

    template<typename Stream>
    void Unserialize(Stream& is)
    {
        auto& val = ref.get();
        val.resize(is.size());
        is.read((char*)val.data(), is.size());
    }
};

template<typename T>
inline RawTBytes<T> refTBytes(T& val)
{
    return RawTBytes<T>{val};
}

// LevelDB glue layer Iterator
class CStorageLevelDBIterator : public CStorageKVIterator {
public:
    explicit CStorageLevelDBIterator(std::unique_ptr<CDBIterator>&& it) : it{std::move(it)} { }
    CStorageLevelDBIterator(const CStorageLevelDBIterator&) = delete;
    ~CStorageLevelDBIterator() override = default;

    void Seek(const TBytes& key) override {
        it->Seek(refTBytes(key)); // lower_bound in fact
    }
    void Next() override { it->Next(); }
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
};

// LevelDB glue layer storage
class CStorageLevelDB : public CStorageKV {
public:
    explicit CStorageLevelDB(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false, bool fDirectWrite = false)
        : db{dbName, cacheSize, fMemory, fWipe}, directWrite(fDirectWrite) {}
    ~CStorageLevelDB() override { }
    bool Exists(const TBytes& key) const override {
        return db.Exists(refTBytes(key));
    }
    bool Write(const TBytes& key, const TBytes& value) override {
        if (directWrite)
            return db.Write(refTBytes(key), refTBytes(value), true);
        BatchWrite(refTBytes(key), refTBytes(value));
        return true;
    }
    bool Erase(const TBytes& key) override {
        if (directWrite)
            return db.Erase(refTBytes(key), true);
        BatchErase(refTBytes(key));
        return true;
    }
    bool Read(const TBytes& key, TBytes& value) const override {
        auto rawVal = refTBytes(value);
        return db.Read(refTBytes(key), rawVal);
    }
    bool Flush() override { // Commit batch
        bool result = true;
        if (batch) {
            result = db.WriteBatch(*batch);
            batch.reset();
        }
        return result;
    }
    std::unique_ptr<CStorageKVIterator> NewIterator() override {
        return MakeUnique<CStorageLevelDBIterator>(std::unique_ptr<CDBIterator>(db.NewIterator()));
    }
private:
    template <typename K, typename V>
    void BatchWrite(const K& key, const V& value) {
        if (!batch) {
            batch.reset(new CDBBatch(db));
        }
        batch->Write<K,V>(key, value);
    }
    template <typename K>
    void BatchErase(const K& key) {
        if (!batch) {
            batch.reset(new CDBBatch(db));
        }
        batch->Erase<K>(key);
    }

    CDBWrapper db;
    boost::scoped_ptr<CDBBatch> batch;
    bool directWrite;
};

// Flashable storage

// Flushable Key-Value Storage Iterator
class CFlushableStorageKVIterator : public CStorageKVIterator {
public:
    explicit CFlushableStorageKVIterator(std::unique_ptr<CStorageKVIterator>&& pIt_, MapKV& map_) : pIt{std::move(pIt_)}, map(map_) {
        inited = parentOk = mapOk = useMap = false;
    }
    CFlushableStorageKVIterator(const CFlushableStorageKVIterator&) = delete;
    ~CFlushableStorageKVIterator() override = default;

    void Seek(const TBytes& key) override {
        prevKey.clear();
        pIt->Seek(key);
        parentOk = pIt->Valid();
        mIt = map.lower_bound(key);
        mapOk = mIt != map.end();
        inited = true;
        Next();
    }
    void Next() override {
        if (!inited) throw std::runtime_error("Iterator wasn't inited.");

        if (!prevKey.empty()) {
            useMap ? nextMap() : nextParent();
        }

        while (mapOk || parentOk) {
            if (mapOk) {
                while (mapOk && (!parentOk || mIt->first <= pIt->Key())) {
                    bool ok = false;

                    if (mIt->second) {
                        ok = prevKey.empty() || mIt->first > prevKey;
                    } else {
                        prevKey = mIt->first;
                    }
                    if (ok) {
                        useMap = true;
                        prevKey = mIt->first;
                        return;
                    }
                    nextMap();
                }
            }
            if (parentOk) {
                if (prevKey.empty() || pIt->Key() > prevKey) {
                    useMap = false;
                    prevKey = pIt->Key();
                    return;
                }
                nextParent();
            }
        }
    }
    bool Valid() override {
        return mapOk || parentOk;
    }
    TBytes Key() override {
        return useMap ? mIt->first : pIt->Key();
    }
    TBytes Value() override {
        return useMap ? *mIt->second : pIt->Value();
    }
private:
    void nextMap() {
        mapOk = mapOk && ++mIt != map.end();
    }
    void nextParent() {
        parentOk = parentOk && (pIt->Next(), pIt->Valid());
    }
    bool inited;
    bool useMap;
    std::unique_ptr<CStorageKVIterator> pIt;
    bool parentOk;
    MapKV& map;
    MapKV::iterator mIt;
    bool mapOk;
    TBytes prevKey;
};

// Flushable Key-Value Storage
class CFlushableStorageKV : public CStorageKV {
public:
    explicit CFlushableStorageKV(CStorageKV& db_) : db(db_) {}
    CFlushableStorageKV(const CFlushableStorageKV& db) = delete;
    ~CFlushableStorageKV() override {}
    bool Exists(const TBytes& key) const override {
        auto it = changed.find(key);
        if (it != changed.end()) {
            return (bool) it->second;
        }
        return db.Exists(key);
    }
    bool Write(const TBytes& key, const TBytes& value) override {
        changed[key] = {value};
        return true;
    }
    bool Erase(const TBytes& key) override {
        changed[key] = {};
        return true;
    }
    bool Read(const TBytes& key, TBytes& value) const override {
        auto it = changed.find(key);
        if (it == changed.end()) {
            return db.Read(key, value);
        }
        else {
            if (it->second) {
                value = it->second.get();
                return true;
            }
            else {
                return false;
            }
        }
    }
    bool Flush() override {
        for (auto it = changed.begin(); it != changed.end(); it++) {
            if (!it->second) {
                if (!db.Erase(it->first))
                    return false;
            }
            else {
                if (!db.Write(it->first, it->second.get()))
                    return false;
            }
        }
        changed.clear();
        return true;
    }
    std::unique_ptr<CStorageKVIterator> NewIterator() override {
        return MakeUnique<CFlushableStorageKVIterator>(db.NewIterator(), changed);
    }

    MapKV& GetRaw() {
        return changed;
    }

private:
    CStorageKV& db;
    MapKV changed;
};

template<typename T>
class CLazySerialize
{
    Optional<T> value;
    CStorageKVIterator& it;

public:
    CLazySerialize(const CLazySerialize&) = default;
    explicit CLazySerialize(CStorageKVIterator& it) : it(it) {}

    operator T()
    {
        return get();
    }

    const T& get()
    {
        if (!value) {
            value = T{};
            BytesToDbType(it.Value(), *value);
        }
        return *value;
    }
};

class CStorageView {
public:
    CStorageView() = default;
    CStorageView(CStorageKV * st) : storage(st) {}
    virtual ~CStorageView() = default;

    template<typename KeyType>
    bool Exists(const KeyType& key) const {
        return DB().Exists(DbTypeToBytes(key));
    }
    template<typename By, typename KeyType>
    bool ExistsBy(const KeyType& key) const {
        return Exists(std::make_pair(By::prefix, key));
    }

    template<typename KeyType, typename ValueType>
    bool Write(const KeyType& key, const ValueType& value) {
        auto vKey = DbTypeToBytes(key);
        auto vValue = DbTypeToBytes(value);
        return DB().Write(vKey, vValue);
    }
    template<typename By, typename KeyType, typename ValueType>
    bool WriteBy(const KeyType& key, const ValueType& value) {
        return Write(std::make_pair(By::prefix, key), value);
    }

    template<typename KeyType>
    bool Erase(const KeyType& key) {
        auto vKey = DbTypeToBytes(key);
        return DB().Exists(vKey) && DB().Erase(vKey);
    }
    template<typename By, typename KeyType>
    bool EraseBy(const KeyType& key) {
        return Erase(std::make_pair(By::prefix, key));
    }

    template<typename KeyType, typename ValueType>
    bool Read(const KeyType& key, ValueType& value) const {
        auto vKey = DbTypeToBytes(key);
        TBytes vValue;
        return DB().Read(vKey, vValue) && BytesToDbType(vValue, value);
    }
    template<typename By, typename KeyType, typename ValueType>
    bool ReadBy(const KeyType& key, ValueType& value) const {
        return Read(std::make_pair(By::prefix, key), value);
    }
    // second type of 'ReadBy' (may be 'GetBy'?)
    template<typename By, typename ResultType, typename KeyType>
    boost::optional<ResultType> ReadBy(KeyType const & id) const {
        ResultType result;
        if (ReadBy<By>(id, result))
            return {result};
        return {};
    }

    template<typename By, typename KeyType, typename ValueType>
    bool ForEach(std::function<bool(KeyType const &, CLazySerialize<ValueType>)> callback, KeyType const & start = KeyType()) const {
        auto& self = const_cast<CStorageView&>(*this);
        auto key = std::make_pair(By::prefix, start);

        auto it = self.DB().NewIterator();
        for(it->Seek(DbTypeToBytes(key)); it->Valid() && BytesToDbType(it->Key(), key) && key.first == By::prefix; it->Next()) {
            boost::this_thread::interruption_point();

            if (!callback(key.second, CLazySerialize<ValueType>(*it)))
                break;
        }
        return true;
    }

protected:
    CStorageKV & DB() { return *storage.get(); }
    CStorageKV const & DB() const { return *storage.get(); }
private:
    std::unique_ptr<CStorageKV> storage;
};

#endif // DEFI_FLUSHABLESTORAGE_H
