// Copyright (c) 2019 DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_MN_TXDB_H
#define BITCOIN_MASTERNODES_MN_TXDB_H

#include <dbwrapper.h>
#include <masternodes/masternodes.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

class CBlockHeader;

/** Access to the masternodes database (masternodes/) */
class CMasternodesViewDB : public CMasternodesView
{
private:
    boost::shared_ptr<CDBWrapper> db;
    boost::scoped_ptr<CDBBatch> batch;

public:
    CMasternodesViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    ~CMasternodesViewDB() override {}

protected:
    // for test purposes only
    CMasternodesViewDB();

private:
    CMasternodesViewDB(CMasternodesViewDB const & other) = delete;
    CMasternodesViewDB & operator=(CMasternodesViewDB const &) = delete;

    template <typename K, typename V>
    void BatchWrite(const K& key, const V& value)
    {
        if (!batch)
        {
            batch.reset(new CDBBatch(*db));
        }
        batch->Write<K,V>(key, value);
    }
    template <typename K>
    void BatchErase(const K& key)
    {
        if (!batch)
        {
            batch.reset(new CDBBatch(*db));
        }
        batch->Erase<K>(key);
    }

    template <typename Container>
    bool LoadTable(char prefix, Container & data, std::function<void(typename Container::key_type const &, typename Container::mapped_type &)> callback = nullptr)
    {
        boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
        pcursor->Seek(prefix);

        while (pcursor->Valid())
        {
            boost::this_thread::interruption_point();
            std::pair<char, typename Container::key_type> key;
            if (pcursor->GetKey(key) && key.first == prefix)
            {
                typename Container::mapped_type value;
                if (pcursor->GetValue(value))
                {
                    data.emplace(key.second, std::move(value));
                    if (callback)
                        callback(key.second, data.at(key.second));
                } else {
                    return error("MNDB::Load() : unable to read value");
                }
            } else {
                break;
            }
            pcursor->Next();
        }
        return true;
    }

protected:
    void CommitBatch();

    bool ReadHeight(int & h);
    void WriteHeight(int h);

    void WriteMasternode(uint256 const & txid, CMasternode const & node);
    void EraseMasternode(uint256 const & txid);

    void WriteMintedBlockHeader(uint256 const & txid, uint64_t mintedBlocks, uint256 const & hash, CBlockHeader const & blockHeader);
    bool FindMintedBlockHeader(uint256 const & txid, uint64_t mintedBlocks, std::map<uint256, CBlockHeader> & blockHeaders);
    void EraseMintedBlockHeader(uint256 const & txid, uint64_t mintedBlocks, uint256 const & hash);

//    void WriteDeadIndex(int height, uint256 const & txid, char type);
//    void EraseDeadIndex(int height, uint256 const & txid);

    void WriteUndo(int height, CMnTxsUndo const & undo);
    void EraseUndo(int height);

public:
    bool Load() override;
    bool Flush() override;
};

#endif // BITCOIN_MASTERNODES_MN_TXDB_H
