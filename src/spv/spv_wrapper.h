// Copyright (c) 2019 DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SPV_SPV_WRAPPER_H
#define BITCOIN_SPV_SPV_WRAPPER_H

#include <dbwrapper.h>

#include <spv/support/BRLargeInt.h>
//#include <spv/bitcoin/BRMerkleBlock.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

template<typename Stream>
inline void Serialize(Stream& s, UInt256 const & hash)
{
    s << hash.u64[0] << hash.u64[1] << hash.u64[2] << hash.u64[3];
}

template<typename Stream>
inline void Unserialize(Stream& s, UInt256 & hash) {
    s >> hash.u64[0] >> hash.u64[1] >> hash.u64[2] >> hash.u64[3];
}

template<typename Stream>
inline void Serialize(Stream& s, UInt128 const & hash)
{
    s << hash.u64[0] << hash.u64[1];
}

template<typename Stream>
inline void Unserialize(Stream& s, UInt128 & hash) {
    s >> hash.u64[0] >> hash.u64[1];
}


typedef struct BRWalletStruct BRWallet;
typedef struct BRPeerManagerStruct BRPeerManager;
typedef struct BRMerkleBlockStruct BRMerkleBlock;
typedef struct BRTransactionStruct BRTransaction;
typedef struct BRPeerStruct BRPeer;

//extern "C" {
//    extern size_t BRMerkleBlockSerialize(const BRMerkleBlock *block, uint8_t *buf, size_t bufLen);
//    extern BRMerkleBlock *BRMerkleBlockParse(const uint8_t *buf, size_t bufLen);
//}


class CSpvWrapper
{
private:
    boost::shared_ptr<CDBWrapper> db;
    boost::scoped_ptr<CDBBatch> batch;

    BRWallet *wallet;
    BRPeerManager *manager;
    std::string spv_internal_logfilename;


public:
    CSpvWrapper(bool isMainnet, std::string const & xpub, size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    ~CSpvWrapper();

    void Connect();
    void Disconnect();

public:
    /// Wallet callbacks
    void OnBalanceChanged(uint64_t balance);
    void OnTxAdded(BRTransaction *tx);
    void OnTxUpdated(const UInt256 txHashes[], size_t txCount, uint32_t blockHeight, uint32_t timestamp);
    void OnTxDeleted(UInt256 txHash, int notifyUser, int recommendRescan);
    /// Peermanager callbacks
    void OnSyncStarted();
    void OnSyncStopped(int error);
    void OnTxStatusUpdate();
    void OnSaveBlocks(int replace, BRMerkleBlock *blocks[], size_t blocksCount);
    void OnSavePeers(int replace, const BRPeer peers[], size_t peersCount);
    int OnNetworkIsReachable();
    void OnThreadCleanup();


private:

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

    template <typename Key, typename Value>
    bool IterateTable(char prefix, std::function<void(Key const &, Value &)> callback)
    {
        boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
        pcursor->Seek(prefix);

        while (pcursor->Valid())
        {
            boost::this_thread::interruption_point();
            std::pair<char, Key> key;
            if (pcursor->GetKey(key) && key.first == prefix)
            {
                Value value;
                if (pcursor->GetValue(value))
                {
                    callback(key.second, value);
                } else {
                    return error("SBV::Load() : unable to read value");
                }
            } else {
                break;
            }
            pcursor->Next();
        }
        return true;
    }

    template <typename Key>
    bool DeleteTable(char prefix)
    {
        boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
        pcursor->Seek(prefix);

        while (pcursor->Valid())
        {
            boost::this_thread::interruption_point();
            std::pair<char, Key> key;
            if (pcursor->GetKey(key) && key.first == prefix)
            {
                db->Erase(key);
            } else {
                break;
            }
            pcursor->Next();
        }
        return true;
    }


    void LoadBlocks();

protected:
//    void CommitBatch();

    void WriteBlock(BRMerkleBlock const * block);
    void WritePeer(BRPeer const * peer);


//public:
//    bool Load() override;
//    bool Flush() override;
};

extern std::unique_ptr<CSpvWrapper> pspv;

#endif // BITCOIN_SPV_SPV_WRAPPER_H
