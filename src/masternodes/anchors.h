// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_ANCHORS_H
#define BITCOIN_MASTERNODES_ANCHORS_H

#include <masternodes/masternodes.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

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

#include <dbwrapper.h>


class CKey;
class CPubkey;

namespace spv
{
    class CSpvWrapper;
    struct BtcAnchorTx;
}

typedef int32_t THeight; // cause not decided yet which type to use for heights
class CAnchorAuthMessage
{
    using Signature = std::vector<unsigned char>;
    using CTeam = CMasternodesView::CTeam;
public:
    uint256 previousAnchor;         ///< Previous tx-AnchorAnnouncement on BTC chain
    THeight height;                 ///< Height of the anchor block (DeFi)
    uint256 blockHash;              ///< Hash of the anchor block (DeFi)
    CTeam nextTeam;   ///< Calculated based on stake modifier at {blockHash}

    CAnchorAuthMessage() {}
    CAnchorAuthMessage(uint256 const & previousAnchor, int height, uint256 const & blockHash, CTeam const & nextTeam);

    Signature GetSignature() const;
    uint256 GetHash() const;
    bool SignWithKey(const CKey& key);
    bool GetPubKey(CPubKey& pubKey) const;
    uint256 GetSignHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
         READWRITE(previousAnchor);
         READWRITE(height);
         READWRITE(blockHash);
         READWRITE(nextTeam);
         READWRITE(signature);
    }

    // tags for multiindex
    struct ByMsgHash{};     // by message hash (for inv)
    struct ByHeight{};
    struct ByBlockHash{};
    struct ByKey{};         // composite, by height and blockHash

private:
    Signature signature;
};

class CAnchorMessage
{
    using Signature = std::vector<unsigned char>;
    using CTeam = CMasternodesView::CTeam;

public:
    uint256 previousAnchor;
    THeight height;
    uint256 blockHash;
    CTeam nextTeam;
    std::vector<Signature> sigs;
    CScript rewardScript;

public:
    CAnchorMessage() : height(0) {}

    static CAnchorMessage Create(std::vector<CAnchorAuthMessage> const & auths, CScript const & rewardScript);
    uint256 GetHash() const;
    bool CheckAuthSigs(CTeam const & team) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(previousAnchor);
        READWRITE(height);
        READWRITE(blockHash);
        READWRITE(nextTeam);
        READWRITE(sigs);
        READWRITE(rewardScript);
    }

};


using namespace boost::multi_index;

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

    CAnchorMessage CreateBestAnchor(uint256 const & forBlock = uint256(), CScript const & rewardScript = {}) const;

    Auths auths;
};

class CAnchorIndex
{
private:
    boost::shared_ptr<CDBWrapper> db;
    boost::scoped_ptr<CDBBatch> batch;
public:
    using Signature = std::vector<unsigned char>;
    using CTeam = CMasternodesView::CTeam;
    using ConfirmationSigs = std::set<Signature>;

    struct AnchorRec {
        CAnchorMessage anchor;
//      {
//        uint256 previousAnchor; // btc tx hash
//        THeight height;         // defi blockheight
//        uint256 blockHash;      // defi blockhash
//        CTeam nextTeam;
//        std::vector<Signature> sigs; // auth sigs
//        CScript rewardScript;
//      }
        uint256 msgHash; // dynamic ?
//        uint256 prevMsgHash; // helper, derived from anchor.previousAnchor->msgHash
        uint256 txHash;
        uint32_t btcHeight;
        uint32_t btcTxIndex;
        ConfirmationSigs confirm_sigs;

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            READWRITE(anchor);
            READWRITE(msgHash);
            READWRITE(txHash);
            READWRITE(btcHeight);
            READWRITE(btcTxIndex);
            READWRITE(confirm_sigs);
        }

        uint64_t GetBtcHeight() const { return (static_cast<uint64_t>(btcHeight) << 32) + btcTxIndex; }

        // tags for multiindex
        struct ByMsgHash{};
        struct ByBtcTxHash{};
        struct ByBtcHeight{};
    };


    typedef boost::multi_index_container<AnchorRec,
        indexed_by<
            ordered_unique    < tag<AnchorRec::ByMsgHash>, member<AnchorRec, uint256,  &AnchorRec::msgHash> >,
            ordered_unique    < tag<AnchorRec::ByBtcTxHash>,  member<AnchorRec, uint256,  &AnchorRec::txHash> >,
            ordered_unique    < tag<AnchorRec::ByBtcHeight>,  const_mem_fun<AnchorRec, uint64_t, &AnchorRec::GetBtcHeight> >
        >
    > AnchorIndexImpl;

    CAnchorIndex(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    bool Load();

    const AnchorRec * GetActiveAnchor() const;
    void ActivateBestAnchor(); // rescan confirmed anchors, but from what? txhash, msghash, btcheight???

    AnchorRec const * ExistAnchorByMsg(uint256 const & hash, bool isMainIndex = true) const;
    AnchorRec const * ExistAnchorByTx(uint256 const & hash, bool isMainIndex = true) const;

    bool AddAnchor(CAnchorMessage const & msg, spv::BtcAnchorTx const * btcTx, ConfirmationSigs const & confirm_sigs, bool isMainIndex = true);
    bool UpdateAnchorConfirmations(AnchorRec const * rec, ConfirmationSigs const & confirm_sigs);

    CMasternodesView::CTeam GetNextTeam(uint256 const & btcPrevTx) const;
    CMasternodesView::CTeam GetCurrentTeam(AnchorRec const * anchor) const;

private:

    AnchorIndexImpl anchors, orphans;
    AnchorRec * top;

private:
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
                    return error("Anchors::Load() : unable to read value");
                }
            } else {
                break;
            }
            pcursor->Next();
        }
        return true;
    }

    bool DbExists(uint256 const & hash) const;
    bool DbRead(uint256 const & hash, AnchorRec & anchor) const;
    bool DbWrite(AnchorRec const & anchor);
    bool DbErase(uint256 const & hash);
};

//bool CheckSigs(uint256 const & hash, std::vector<std::vector<unsigned char>> const & sigs, std::set<CKeyID> const & keys);


/// dummy, unknown consensus rules yet. may be additional params needed (smth like 'height')
/// even may be not here, but in CMasternodesView
uint32_t GetMinAnchorQuorum(CMasternodesView::CTeam const & team);

// thowing exceptions (not a bool due to more verbose rpc errors. may be 'status' or smth? )
/// Validate NOT ORPHANED anchor (anchor that has valid previousAnchor or null(first) )
bool ValidateAnchor(CAnchorMessage const & anchor, CAnchorIndex::ConfirmationSigs const & conf_sigs, bool noThrow);

class CConnman;
class CNode;
bool ProcessNewAnchor(CAnchorMessage const & anchor, CAnchorIndex::ConfirmationSigs const & conf_sigs, bool noThrow, CConnman& connman, CNode* skipNode);

class CAnchorConfirmMessage
{
    using Signature = std::vector<unsigned char>;
public:
    uint256 hashAnchorMessage;
    Signature signature;

//    CAnchorConfirmMessage() {}

    static CAnchorConfirmMessage Create(CAnchorMessage const & anchorMessage, CKey const & key);
    uint256 GetHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hashAnchorMessage);
        READWRITE(signature);
    }
};

class CAnchorConfirms
{
    using Hash = uint256;
private:
    std::map<Hash, CAnchorConfirmMessage> confirms;

public:
    const CAnchorConfirmMessage *Exist(uint256 const &hash);
    void Add(CAnchorConfirmMessage const &newMessage);
    bool Validate(CAnchorConfirmMessage const &message);
};


/** Global variables that points to the anchors and their auths (should be protected by cs_main) */
extern std::unique_ptr<CAnchorAuthIndex> panchorauths;
extern std::unique_ptr<CAnchorIndex> panchors;
extern std::unique_ptr<CAnchorConfirms> panchorconfirms;

#endif // BITCOIN_MASTERNODES_ANCHORS_H
