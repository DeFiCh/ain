// Copyright (c) 2019 DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SPV_SPV_WRAPPER_H
#define BITCOIN_SPV_SPV_WRAPPER_H

#include <dbwrapper.h>
#include <uint256.h>

#include <spv/support/BRLargeInt.h>
//#include <spv/bitcoin/BRMerkleBlock.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/ordered_index.hpp>

//template<typename Stream>
//inline void Serialize(Stream& s, UInt256 const & hash)
//{
//    s << hash.u64[0] << hash.u64[1] << hash.u64[2] << hash.u64[3];
//}

//template<typename Stream>
//inline void Unserialize(Stream& s, UInt256 & hash) {
//    s >> hash.u64[0] >> hash.u64[1] >> hash.u64[2] >> hash.u64[3];
//}

//template<typename Stream>
//inline void Serialize(Stream& s, UInt128 const & hash)
//{
//    s << hash.u64[0] << hash.u64[1];
//}

//template<typename Stream>
//inline void Unserialize(Stream& s, UInt128 & hash) {
//    s >> hash.u64[0] >> hash.u64[1];
//}

typedef struct BRWalletStruct BRWallet;
typedef struct BRPeerManagerStruct BRPeerManager;
typedef struct BRMerkleBlockStruct BRMerkleBlock;
typedef struct BRTransactionStruct BRTransaction;
typedef struct BRPeerStruct BRPeer;

//extern "C" {
//    extern size_t BRMerkleBlockSerialize(const BRMerkleBlock *block, uint8_t *buf, size_t bufLen);
//    extern BRMerkleBlock *BRMerkleBlockParse(const uint8_t *buf, size_t bufLen);
//}

namespace spv
{

typedef std::vector<uint8_t> TBytes;

uint256 to_uint256(UInt256 const & i);

static const TBytes BtcAnchorMarker = { 'D', 'F', 'A'}; // 0x444641

struct TxInput {
    UInt256 txHash;
    int32_t index;
    uint64_t amount;
    TBytes script;
};

struct TxOutput {
    uint64_t amount;
    TBytes script;
};

/*
class CAnchorAuthIndex
{
    using CTeam = CMasternodesView::CTeam;

public:
    using Auth = CAnchorAuthMessage;

    typedef boost::multi_index_container<Auth,
        indexed_by<
            ordered_unique<
                tag<Auth::ByMsgHash>, const_mem_fun<Auth, uint256, &Auth::GetHash>
            >,
            ordered_non_unique<
                tag<Auth::ByHeight>, member<Auth, THeight, &Auth::height>
            >,
            ordered_non_unique<
                tag<Auth::ByBlockHash>, member<Auth, uint256, &Auth::blockHash>
            >,

            ordered_non_unique<
                tag<Auth::ByKey>, composite_key<
                    Auth,
                    member<Auth, THeight, &Auth::height>,
                    member<Auth, uint256, &Auth::blockHash>
                >
            >
        >
    > Auths;

    Auth const * ExistAuth(uint256 const & hash) const;
    bool ValidateAuth(Auth const & auth) const;
    bool AddAuth(Auth const & auth);

    /// dummy, unknown consensus rules yet. may be additional params needed (smth like 'height')
    /// even may be not here, but in CMasternodesView
    uint32_t GetMinAnchorQuorum(CMasternodesView::CTeam const & team) const;

    CAnchorMessage CreateBestAnchor(uint256 const & forBlock = uint256(), CScript const & rewardScript = {}) const;

    Auths auths;
};
*/

using namespace boost::multi_index;

class CSpvWrapper
{
private:
    boost::shared_ptr<CDBWrapper> db;
    boost::scoped_ptr<CDBBatch> batch;

    BRWallet *wallet;
    BRPeerManager *manager;
    std::string spv_internal_logfilename;

    using db_tx_rec    = std::pair<TBytes, std::pair<uint32_t, uint32_t>>; // serialized tx, blockHeight, timeStamp
    using db_block_rec = std::pair<TBytes, uint32_t>; // serialized block, blockHeight

    struct BtcAnchorTx {
        uint256 txHash;
        uint256 msgHash;
        uint32_t blockHeight;

        // tags for multiindex
        struct ByTxHash{};
        struct ByMsgHash{};
        struct ByHeight{};
//        struct ByKey{};         // composite, by height and txHash
    };

    typedef boost::multi_index_container<BtcAnchorTx,
        indexed_by<
            ordered_unique    < tag<BtcAnchorTx::ByTxHash>,  member<BtcAnchorTx, uint256,  &BtcAnchorTx::txHash> >,
            ordered_unique    < tag<BtcAnchorTx::ByMsgHash>, member<BtcAnchorTx, uint256,  &BtcAnchorTx::msgHash> >,
            ordered_non_unique< tag<BtcAnchorTx::ByHeight>,  member<BtcAnchorTx, uint32_t, &BtcAnchorTx::blockHeight> >
        >
    > BtcAnchorTxIndex;

    BtcAnchorTxIndex txIndex;
    mutable CCriticalSection cs_txIndex;

public:
    CSpvWrapper(bool isMainnet, size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    ~CSpvWrapper();

    void Connect();
    void Disconnect();
    bool IsConnected() const;
    bool Rescan(int height);

    BRPeerManager const * GetPeerManager() const;
    BRWallet const * GetWallet() const;
    uint32_t GetLastBlockHeight() const;
    uint32_t GetEstimatedBlockHeight() const;
    uint8_t GetPKHashPrefix() const;

    std::vector<BRTransaction *> GetWalletTxs() const;
    bool SendRawTx(TBytes rawtx);
    int GetTxConfirmations(uint256 const & txHash);

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
//    int OnNetworkIsReachable();
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

    // batched! need to commit
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
                BatchErase(key);
            } else {
                break;
            }
            pcursor->Next();
        }
        return true;
    }


protected:
    void CommitBatch();

    void WriteBlock(BRMerkleBlock const * block);
    void WritePeer(BRPeer const * peer);
    void WriteTx(BRTransaction const * tx);
    void EraseTx(uint256 const & hash);

    void IndexDeleteTx(uint256 const & hash);
};

extern std::unique_ptr<CSpvWrapper> pspv;

bool IsAnchorTx(BRTransaction *tx, uint256 & anchorMsgHash);
TBytes CreateAnchorTx(std::string const & hash, int32_t index, uint64_t inputAmount, std::string const & privkey_wif, TBytes const & meta);
TBytes CreateScriptForAddress(std::string const & address);

}
#endif // BITCOIN_SPV_SPV_WRAPPER_H
