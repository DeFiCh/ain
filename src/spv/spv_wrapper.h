// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_SPV_SPV_WRAPPER_H
#define DEFI_SPV_SPV_WRAPPER_H

#include <dbwrapper.h>
#include <pubkey.h>
#include <shutdown.h>
#include <uint256.h>

#include <spv/support/BRLargeInt.h>

#include <future>
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

#include <boost/thread.hpp>

typedef struct BRWalletStruct BRWallet;
typedef struct BRPeerManagerStruct BRPeerManager;
typedef struct BRMerkleBlockStruct BRMerkleBlock;
typedef struct BRTransactionStruct BRTransaction;
typedef struct BRPeerStruct BRPeer;

class CAnchor;
class CKeyID;
class CScript;
class CWallet;
class UniValue;

enum class SPVTxType;

extern const int ENOSPV;
extern const int EPARSINGTX;
extern const int ETXNOTSIGNED;

std::string DecodeSendResult(int result);

namespace spv
{

struct HTLCDetails {
    CPubKey sellerKey;
    CPubKey buyerKey;
    uint32_t locktime{0};
    std::vector<unsigned char> hash;
};

HTLCDetails GetHTLCDetails(CScript& redeemScript);

typedef std::vector<uint8_t> TBytes;

static const TBytes BtcAnchorMarker = { 'D', 'F', 'A'}; // 0x444641

/// @todo test this amount of dust for p2wsh due to spv is very dumb and checks only for 546 (p2pkh)
uint64_t const P2WSH_DUST = 330; /// 546 p2pkh & 294 p2wpkh (330 p2wsh calc'ed manually)
uint64_t const P2PKH_DUST = 546;

extern uint64_t const DEFAULT_BTC_FEERATE;
extern uint64_t const DEFAULT_BTC_FEE_PER_KB;

using namespace boost::multi_index;

class CSpvWrapper
{
private:
    boost::shared_ptr<CDBWrapper> db;
    boost::scoped_ptr<CDBBatch> batch;

    BRPeerManager *manager = nullptr;
    std::string spv_internal_logfilename;

    using db_tx_rec    = std::pair<TBytes, std::pair<uint32_t, uint32_t>>;  // serialized tx, blockHeight, timeStamp
    using db_block_rec = std::pair<TBytes, uint32_t>;                       // serialized block, blockHeight

    bool initialSync = true;

protected:
    BRWallet *wallet = nullptr;

public:
    CSpvWrapper(bool isMainnet, size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    virtual ~CSpvWrapper();

    virtual void Connect();
    virtual void Disconnect();
    virtual bool IsConnected() const;
    virtual void CancelPendingTxs();

    bool Rescan(int height);

    BRWallet * GetWallet();

    bool IsInitialSync() const;
    virtual uint32_t GetLastBlockHeight() const;
    virtual uint32_t GetEstimatedBlockHeight() const;
    uint8_t GetPKHashPrefix() const;
    uint8_t GetP2SHPrefix() const;

    std::vector<BRTransaction *> GetWalletTxs() const;

    bool SendRawTx(TBytes rawtx, std::promise<int> * promise = nullptr);

public:
    /// Wallet callbacks
    void OnBalanceChanged(uint64_t balance);
    void OnTxAdded(BRTransaction *tx);
    void OnTxUpdated(const UInt256 txHashes[], size_t txCount, uint32_t blockHeight, uint32_t timestamp, const UInt256 &blockHash);
    void OnTxDeleted(UInt256 txHash, int notifyUser, int recommendRescan);
    /// Peermanager callbacks
    void OnSyncStarted();
    void OnSyncStopped(int error);
    void OnTxStatusUpdate();
    void OnSaveBlocks(int replace, BRMerkleBlock *blocks[], size_t blocksCount);
    void OnSavePeers(int replace, const BRPeer peers[], size_t peersCount);
    void OnThreadCleanup();

    /// Wallet notifications
    void OnBlockNotify(const UInt256& blockHash);
    void OnTxNotify(const UInt256& txHash);

    // Get time stamp of Bitcoin TX
    uint32_t ReadTxTimestamp(uint256 const & hash);

    // Get block height of Bitcoin TX
    uint32_t ReadTxBlockHeight(uint256 const & hash);

    // Bitcoin Address calls
    std::string AddBitcoinAddress(const CPubKey &new_key);
    void AddBitcoinHash(const uint160 &userHash, const bool htlc = false);
    std::string DumpBitcoinPrivKey(const CWallet* pwallet, const std::string &strAddress);
    UniValue GetAddressPubkey(const CWallet *pwallet, const char *addr); // Used in HTLC creation
    CKeyID GetAddressKeyID(const char *addr);
    UniValue GetAllAddress();
    SPVTxType IsMine(const char *address);
    UniValue ValidateAddress(const char *address);

    // Bitcoin Transaction related calls
    int64_t GetBitcoinBalance();
    std::string GetRawTransactions(uint256& hash);
    UniValue ListTransactions();
    UniValue ListReceived(int nMinDepth, std::string address);
    void RebuildBloomFilter();
    virtual UniValue SendBitcoins(CWallet* const pwallet, std::string address, int64_t amount, uint64_t feeRate);

    // Bitcoin HTLC calls
    UniValue GetHTLCReceived(const std::string &addr);
    std::string GetHTLCSeed(uint8_t* md20);
    UniValue CreateHTLCTransaction(CWallet* const pwallet, const char *scriptAddress, const char *destinationAddress, const std::string& seed, uint64_t feerate, bool seller);

private:
    virtual void OnSendRawTx(BRTransaction * tx, std::promise<int> * promise);

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
    void WriteTx(BRTransaction const * tx);
    void UpdateTx(uint256 const & hash, uint32_t blockHeight, uint32_t timestamp, const uint256 &blockHash);
    void EraseTx(uint256 const & hash);
};

// fake spv for testing (activate it with 'fakespv=1' on regtest net)
class CFakeSpvWrapper : public CSpvWrapper
{
public:
    CFakeSpvWrapper();

    void Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    void CancelPendingTxs() override;

    uint32_t GetLastBlockHeight() const override { return lastBlockHeight; }
    uint32_t GetEstimatedBlockHeight() const override { return lastBlockHeight; } // dummy

    void OnSendRawTx(BRTransaction * tx, std::promise<int> * promise) override;
    UniValue SendBitcoins(CWallet* const pwallet, std::string address, int64_t amount, uint64_t feeRate) override;

    uint32_t lastBlockHeight = 0;
    bool isConnected = false;
};


extern std::unique_ptr<CSpvWrapper> pspv;

bool IsAnchorTx(BRTransaction *tx, CAnchor & anchor);

struct TxInputData {
    std::string txhash;
    int32_t txn;
    uint64_t amount;
    std::string privkey_wif;
};

uint64_t EstimateAnchorCost(TBytes const & meta, uint64_t feerate);
std::vector<CScript> EncapsulateMeta(TBytes const & meta);
std::tuple<uint256, TBytes, uint64_t> CreateAnchorTx(std::vector<TxInputData> const & inputs, TBytes const & meta, uint64_t feerate);
TBytes CreateScriptForAddress(char const * address);

}
#endif // DEFI_SPV_SPV_WRAPPER_H
