// Copyright (c) 2019 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ANCHORS_H
#define DEFI_MASTERNODES_ANCHORS_H

#include <masternodes/masternodes.h>
#include <script/script.h>
#include <script/standard.h>
#include <serialize.h>
#include <uint256.h>

#include <functional>
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

typedef uint32_t THeight; // cause not decided yet which type to use for heights
class CAnchorAuthMessage
{
    using Signature = std::vector<unsigned char>;
    using CTeam = CMasternodesView::CTeam;
public:
    uint256 previousAnchor;         ///< Previous tx-AnchorAnnouncement on BTC chain
    THeight height;                 ///< Height of the anchor block (DeFi)
    uint256 blockHash;              ///< Hash of the anchor block (DeFi)
    CTeam nextTeam;                 ///< Calculated based on stake modifier at {blockHash}

    CAnchorAuthMessage() {}
    CAnchorAuthMessage(uint256 const & previousAnchor, int height, uint256 const & blockHash, CTeam const & nextTeam);

    Signature GetSignature() const;
    uint256 GetHash() const;
    bool SignWithKey(const CKey& key);
    bool GetPubKey(CPubKey& pubKey) const;
    CKeyID GetSigner() const;
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
    struct ByBlockHash{};   // by blockhash (for locator/GETANCHORAUTHS)
    struct ByKey{};         // composite, by height and GetSignHash for anchor creation
    struct ByVote{};        // composite, by GetSignHash and signer, helps detect doublesigning

private:
    Signature signature;
};

class CAnchor
{
    using Signature = std::vector<unsigned char>;
    using CTeam = CMasternodesView::CTeam;

public:
    uint256 previousAnchor;
    THeight height;
    uint256 blockHash;
    CTeam nextTeam;
    std::vector<Signature> sigs;
    CKeyID rewardKeyID;
    char rewardKeyType;

public:
    CAnchor() : height(0) {}

    static CAnchor Create(std::vector<CAnchorAuthMessage> const & auths, CTxDestination const & rewardDest);
    bool CheckAuthSigs(CTeam const & team) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(previousAnchor);
        READWRITE(height);
        READWRITE(blockHash);
        READWRITE(nextTeam);
        READWRITE(sigs);
        READWRITE(rewardKeyID);
        READWRITE(rewardKeyType);
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
            // index for p2p messaging (inv/getdata)
            ordered_unique<
                tag<Auth::ByMsgHash>, const_mem_fun<Auth, uint256, &Auth::GetHash>
            >,
            // index for locator/GETANCHORAUTHS
            ordered_non_unique<
                tag<Auth::ByBlockHash>, member<Auth, uint256, &Auth::blockHash>
            >,
            // index for quorum selection (CreateBestAnchor())
            // just to remember that there may be auths with equal blockHash, but with different prevs and teams!
            ordered_non_unique<
                tag<Auth::ByKey>, composite_key<Auth,
                    member<Auth, THeight, &Auth::height>,
                    const_mem_fun<Auth, uint256, &Auth::GetSignHash>
                >
            >,
            // restriction index that helps detect doublesigning
            // it may by quite expensive to index by GetSigner on the fly, but it should happen only on insertion
            ordered_unique<
                tag<Auth::ByVote>, composite_key<Auth,
                    const_mem_fun<Auth, uint256, &Auth::GetSignHash>,
                    const_mem_fun<Auth, CKeyID, &Auth::GetSigner>
                >
            >

        >
    > Auths;

    Auth const * ExistAuth(uint256 const & msgHash) const;
    Auth const * ExistVote(uint256 const & signHash, CKeyID const & signer) const;
    bool ValidateAuth(Auth const & auth) const;
    bool AddAuth(Auth const & auth);

    CAnchor CreateBestAnchor(CTxDestination const & rewardDest) const;
    void ForEachAnchorAuthByHeight(std::function<bool(const CAnchorAuthIndex::Auth &)> callback) const;
    void PruneOlderThan(THeight height);

protected:
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

    struct AnchorRec {
        CAnchor anchor;
        uint256 txHash;
        THeight btcHeight;

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            READWRITE(anchor);
            READWRITE(txHash);
            READWRITE(btcHeight);
        }

        // tags for multiindex
        struct ByBtcTxHash{};
        struct ByBtcHeight{};
    };

    typedef boost::multi_index_container<AnchorRec,
        indexed_by<
            ordered_unique    < tag<AnchorRec::ByBtcTxHash>,  member<AnchorRec, uint256,  &AnchorRec::txHash> >,
            ordered_non_unique< tag<AnchorRec::ByBtcHeight>,  member<AnchorRec, THeight,  &AnchorRec::btcHeight> >
        >
    > AnchorIndexImpl;

    CAnchorIndex(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    bool Load();

    void ForEachAnchorByBtcHeight(std::function<void(const CAnchorIndex::AnchorRec &)> callback) const;
    AnchorRec const * GetActiveAnchor() const;
    bool ActivateBestAnchor(bool forced = false); // rescan anchors

    AnchorRec const * ExistAnchorByTx(uint256 const & hash) const;

    bool AddAnchor(CAnchor const & anchor, uint256 const & btcTxHash, THeight btcBlockHeight, bool overwrite = true);
    bool DeleteAnchorByBtcTx(uint256 const & btcTxHash);

    CMasternodesView::CTeam GetNextTeam(uint256 const & btcPrevTx) const;
    CMasternodesView::CTeam GetCurrentTeam(AnchorRec const * anchor) const;

    AnchorRec const * GetAnchorByBtcTx(uint256 const & txHash) const;

    using UnrewardedResult = std::set<uint256>;
    UnrewardedResult GetUnrewarded() const;

    int GetAnchorConfirmations(uint256 const & txHash) const;
    int GetAnchorConfirmations(AnchorRec const * rec) const;

    void CheckActiveAnchor(bool forced = false);
    void UpdateLastHeight(uint32_t height);

private:
    AnchorIndexImpl anchors;
    AnchorRec const * top = nullptr;
    bool possibleReActivation = false;
    uint32_t spvLastHeight = 0;

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

class CAnchorConfirmMessage
{
public:
    using Signature = std::vector<unsigned char>;

    uint256 btcTxHash;
    THeight anchorHeight;
    THeight prevAnchorHeight;
    CKeyID rewardKeyID;
    char rewardKeyType;
    Signature signature;

    CAnchorConfirmMessage() {}

    static CAnchorConfirmMessage Create(THeight anchorHeight, CKeyID const & rewardKeyID, char rewardKeyType, THeight prevAnchorHeight, uint256 btcTxHash);
    static CAnchorConfirmMessage Create(CAnchor const & anchor, THeight prevAnchorHeight, uint256 btcTxHash, CKey const & key);
    uint256 GetHash() const;
    uint256 GetSignHash() const;
    CKeyID GetSigner() const;
    bool CheckConfirmSigs(std::vector<Signature> const & sigs, CMasternodesView::CTeam team);
    bool isEqualDataWith(const CAnchorConfirmMessage &message) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(btcTxHash);
        READWRITE(anchorHeight);
        READWRITE(prevAnchorHeight);
        READWRITE(rewardKeyID);
        READWRITE(rewardKeyType);
        READWRITE(signature);
    }

    // tags for multiindex
    struct ByMsgHash{};     // by message hash (for inv)
    struct ByAnchor{};      // by btctxhash
    struct ByKey{};         // composite, by btctxhash and GetSignHash for miner/reward creation
    struct ByVote{};        // composite, by GetSignHash and signer, helps detect doublesigning
};

class CAnchorAwaitingConfirms
{
    using ConfirmMessageHash = uint256;
    using AnchorTxHash = uint256;

protected:
    using Confirm = CAnchorConfirmMessage;

    typedef boost::multi_index_container<Confirm,
        indexed_by<
            // index for p2p messaging (inv/getdata)
            ordered_unique<
                tag<Confirm::ByMsgHash>, const_mem_fun<Confirm, uint256, &Confirm::GetHash>
            >,
            // index for group confirms deletion
            ordered_non_unique<
                tag<Confirm::ByAnchor>, member<Confirm, uint256, &Confirm::btcTxHash>
            >,
            // index for quorum selection (miner affected)
            // just to remember that there may be confirms with equal btcTxHash, but with different teams!
            ordered_non_unique<
                tag<Confirm::ByKey>, composite_key<Confirm,
                    member<Confirm, uint256, &Confirm::btcTxHash>,
                    const_mem_fun<Confirm, uint256, &Confirm::GetSignHash>
                >
            >,
            // restriction index that helps detect doublesigning
            // it may by quite expensive to index by GetSigner on the fly, but it should happen only on insertion
            ordered_unique<
                tag<Confirm::ByVote>, composite_key<Confirm,
                    const_mem_fun<Confirm, uint256, &Confirm::GetSignHash>,
                    const_mem_fun<Confirm, CKeyID, &Confirm::GetSigner>
                >
            >
        >
    > Confirms;

    Confirms confirms;

public:
    bool EraseAnchor(AnchorTxHash const &txHash);
    const CAnchorConfirmMessage *Exist(ConfirmMessageHash const &msgHash) const;
    bool Add(CAnchorConfirmMessage const &newConfirmMessage);
    bool Validate(CAnchorConfirmMessage const &confirmMessage) const;
    void Clear();
    void ReVote();
    std::vector<CAnchorConfirmMessage> GetQuorumFor(CMasternodesView::CTeam const & team) const;

    void ForEachConfirm(std::function<void(Confirm const &)> callback) const;
};

/// dummy, unknown consensus rules yet. may be additional params needed (smth like 'height')
/// even may be not here, but in CMasternodesView
uint32_t GetMinAnchorQuorum(CMasternodesView::CTeam const & team);

// thowing exceptions (not a bool due to more verbose rpc errors. may be 'status' or smth? )
/// Validates all except tx confirmations
bool ValidateAnchor(CAnchor const & anchor, bool noThrow);

class CConnman;
class CNode;

/** Global variables that points to the anchors and their auths (should be protected by cs_main) */
extern std::unique_ptr<CAnchorAuthIndex> panchorauths;
extern std::unique_ptr<CAnchorIndex> panchors;
extern std::unique_ptr<CAnchorAwaitingConfirms> panchorAwaitingConfirms;

#endif // DEFI_MASTERNODES_ANCHORS_H
