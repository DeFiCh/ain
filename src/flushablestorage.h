// Copyright (c) 2019 The Wagerr developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_FLUSHABLESTORAGE_H
#define DEFI_FLUSHABLESTORAGE_H

#include <dbwrapper.h>
#include <boost/optional.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>

using TBytes = std::vector<unsigned char>;
using MapKV = std::map<TBytes, boost::optional<TBytes>>;

template<typename T>
static TBytes DbTypeToBytes(const T& value) {
    CDataStream stream(SER_DISK, CLIENT_VERSION);
    stream << value;
    return TBytes(stream.begin(), stream.end());
}

template<typename T>
static void BytesToDbType(const TBytes& bytes, T& value) {
    try {
        CDataStream stream(bytes, SER_DISK, CLIENT_VERSION);
        stream >> value;
//        assert(stream.size() == 0); // will fail with partial key matching
    }
    catch (std::ios_base::failure&) {
    }
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
struct RawTBytes {
    TBytes& val;

    template<typename Stream>
    void Serialize(Stream& os) const
    {
        os.write((char*)val.data(), val.size());
    }
    template<typename Stream>
    void Unserialize(Stream& is)
    {
        val.clear();
        val.resize(is.size());
        is.read((char*)val.data(), is.size());
        /*try {
            while (true) {
                unsigned char b;
                is.read((char*)&b, sizeof(b));
                val.push_back(b);
            }
        } catch (...) {}*/
    }
};

// LevelDB glue layer Iterator
class CStorageLevelDBIterator : public CStorageKVIterator {
public:
    explicit CStorageLevelDBIterator(std::unique_ptr<CDBIterator>&& it) : it{std::move(it)} { }
    ~CStorageLevelDBIterator() override { }
    void Seek(const TBytes& key) override {
        it->Seek(RawTBytes{(TBytes&) key}); // lower_bound in fact
    }
    void Next() override { it->Next(); }
    bool Valid() override {
        return it->Valid();
    }
    TBytes Key() override {
        TBytes result;
        RawTBytes raw{result};
        return (it->GetKey(raw)) ? result : TBytes{};
    }
    TBytes Value() override {
        TBytes result;
        RawTBytes raw{result};
        return (it->GetValue(raw)) ? result : TBytes{};
    }
private:
    std::unique_ptr<CDBIterator> it;
    // No copying allowed
    CStorageLevelDBIterator(const CStorageLevelDBIterator&);
    void operator=(const CStorageLevelDBIterator&);
};

// LevelDB glue layer storage
class CStorageLevelDB : public CStorageKV {
public:
    explicit CStorageLevelDB(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false, bool fDirectWrite = false)
        : db{dbName, cacheSize, fMemory, fWipe}, directWrite(fDirectWrite) {}
    ~CStorageLevelDB() override { }
    bool Exists(const TBytes& key) const override {
        return db.Exists(RawTBytes{(TBytes&)key});
    }
    bool Write(const TBytes& key, const TBytes& value) override {
        if (directWrite)
            return db.Write(RawTBytes{(TBytes&)key}, RawTBytes{(TBytes&)value}, true);
        BatchWrite(RawTBytes{(TBytes&)key}, RawTBytes{(TBytes&)value});
        return true;
    }
    bool Erase(const TBytes& key) override {
        if (directWrite)
            return db.Erase(RawTBytes{(TBytes&)key}, true);
        BatchErase(RawTBytes{(TBytes&)key});
        return true;
    }
    bool Read(const TBytes& key, TBytes& value) const override {
        auto rawVal = RawTBytes{(TBytes&)value};
        return db.Read(RawTBytes{(TBytes&)key}, rawVal);
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
        inited = parentOk = mapOk = false;
    }
         // No copying allowed
    CFlushableStorageKVIterator(const CFlushableStorageKVIterator&) = delete;
    void operator=(const CFlushableStorageKVIterator&) = delete;
    ~CFlushableStorageKVIterator() override { }
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
        key.clear();
        value.clear();

        while (mapOk || parentOk) {
            if (mapOk) {
                while (mapOk && (!parentOk || mIt->first <= pIt->Key())) {
                    bool ok = false;

                    if (mIt->second) {
                        ok = prevKey.empty() || mIt->first > prevKey;
                    }
                    else {
                        prevKey = mIt->first;
                    }
                    if (ok) {
                        key = mIt->first;
                        value = *mIt->second;
                        prevKey = key;
                    }
                    if (mapOk) {
                        mIt++;
                        mapOk = mIt != map.end();
                    }
                    if (ok) return;
                }
            }
            if (parentOk) {
                bool ok = prevKey.empty() || pIt->Key() > prevKey;
                if (ok) {
                    key = pIt->Key();
                    value = pIt->Value();
                    prevKey = key;
                }
                if (parentOk) {
                    pIt->Next();
                    parentOk = pIt->Valid();
                }
                if (ok) return;
            }
        }
    }
    bool Valid() override {
        return !key.empty();
    }
    TBytes Key() override {
        return key;
    }
    TBytes Value() override {
        return value;
    }
private:
    bool inited;
    std::unique_ptr<CStorageKVIterator> pIt;
    bool parentOk;
    MapKV& map;
    MapKV::iterator mIt;
    bool mapOk;
    TBytes key;
    TBytes value;
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

class CStorageView {
public:
    CStorageView(CStorageKV * st) : storage(st) {}
    CStorageView() {}

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
        if (!DB().Exists(vKey))
            return false;
        return DB().Erase(vKey);
    }
    template<typename By, typename KeyType>
    bool EraseBy(const KeyType& key) {
        return Erase(std::make_pair(By::prefix, key));
    }

    template<typename KeyType, typename ValueType>
    bool Read(const KeyType& key, ValueType& value) const {
        auto vKey = DbTypeToBytes(key);
        TBytes vValue;
        if (DB().Read(vKey, vValue)) {
            BytesToDbType(vValue, value);
            return true;
        }
        return false;
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
    bool ForEach(std::function<bool(KeyType const &, ValueType &)> callback, KeyType const & start = KeyType()) const {
        auto& self = const_cast<CStorageView&>(*this);

        using pref_type = typename std::remove_const<decltype( By::prefix )>::type;
        auto key = std::make_pair<pref_type, KeyType>(pref_type(By::prefix), KeyType(start));
//            std::pair<char, KeyType> key; // failsafe

        auto it = self.DB().NewIterator();
        for(it->Seek(DbTypeToBytes(key)); it->Valid() && (BytesToDbType(it->Key(), key), key.first == By::prefix); it->Next()) {
            boost::this_thread::interruption_point();

            ValueType value;
            BytesToDbType(it->Value(), value);

            if (!callback(key.second, value))
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
