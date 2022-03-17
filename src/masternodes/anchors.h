// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ANCHORS_H
#define DEFI_MASTERNODES_ANCHORS_H

#include <pubkey.h>
#include <script/script.h>
#include <script/standard.h>
#include <serialize.h>
#include <shutdown.h>
#include <sync.h>
#include <uint256.h>

#include <functional>
#include <vector>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <dbwrapper.h>

class CBlockIndex;
class CKey;
class CPubkey;

extern RecursiveMutex cs_main;

namespace spv
{
    class CSpvWrapper;
    struct BtcAnchorTx;
}

namespace Consensus
{
    struct Params;
}

typedef uint32_t THeight; // cause not decided yet which type to use for heights

struct CAnchorData
{
    using CTeam = std::set<CKeyID>;

    uint256 previousAnchor;         ///< Previous tx-AnchorAnnouncement on BTC chain
    THeight height;                 ///< Height of the anchor block (DeFi)
    uint256 blockHash;              ///< Hash of the anchor block (DeFi)
    CTeam heightAndHash;            ///< Single entry with anchor marker, height and block hash prefix

    uint256 GetSignHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
         READWRITE(previousAnchor);
         READWRITE(height);
         READWRITE(blockHash);
         READWRITE(heightAndHash);
    }
};

class CAnchorAuthMessage : public CAnchorData
{
    using Signature = std::vector<unsigned char>;
public:

    CAnchorAuthMessage(CAnchorData const & base = CAnchorData())
        : CAnchorData(base)
        , signature()
    {}

    Signature GetSignature() const;
    uint256 GetHash() const;
    bool SignWithKey(const CKey& key);
    bool GetPubKey(CPubKey& pubKey) const;
    CKeyID GetSigner() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
         READWRITEAS(CAnchorData, *this);
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

class CAnchor : public CAnchorData
{
    using Signature = std::vector<unsigned char>;
    using CTeam = CAnchorData::CTeam;

public:
    std::vector<Signature> sigs;
    CKeyID rewardKeyID;
    char rewardKeyType;

    CAnchor(CAnchorData const & base = CAnchorData())
        : CAnchorData(base)
        , sigs()
        , rewardKeyID()
        , rewardKeyType(0)
    {}

    static CAnchor Create(std::vector<CAnchorAuthMessage> const & auths, CTxDestination const & rewardDest);
    bool CheckAuthSigs(CTeam const & team) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CAnchorData, *this);
        READWRITE(sigs);
        READWRITE(rewardKeyID);
        READWRITE(rewardKeyType);
    }
};


using namespace boost::multi_index;

class CAnchorAuthIndex
{
    using CTeam = CAnchorData::CTeam;

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
                tag<Auth::ByBlockHash>, member<CAnchorData, uint256, &CAnchorData::blockHash>
            >,
            // index for quorum selection (CreateBestAnchor())
            // just to remember that there may be auths with equal blockHash, but with different prevs and teams!
            ordered_non_unique<
                tag<Auth::ByKey>, composite_key<Auth,
                    member<CAnchorData, THeight, &CAnchorData::height>,
                    const_mem_fun<CAnchorData, uint256, &CAnchorData::GetSignHash>
                >
            >,
            // restriction index that helps detect doublesigning
            // it may by quite expensive to index by GetSigner on the fly, but it should happen only on insertion
            ordered_unique<
                tag<Auth::ByVote>, composite_key<Auth,
                    const_mem_fun<CAnchorData, uint256, &CAnchorData::GetSignHash>,
                    const_mem_fun<Auth, CKeyID, &Auth::GetSigner>
                >
            >

        >
    > Auths;

    Auth const * GetAuth(uint256 const & msgHash) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    Auth const * GetVote(uint256 const & signHash, CKeyID const & signer) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool ValidateAuth(Auth const & auth) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool AddAuth(Auth const & auth) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    CAnchor CreateBestAnchor(CTxDestination const & rewardDest) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void ForEachAnchorAuthByHeight(std::function<bool(const CAnchorAuthIndex::Auth &)> callback) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void PruneOlderThan(THeight height) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    Auths auths;
};

class CAnchorIndex
{
private:
    std::shared_ptr<CDBWrapper> db;
    std::unique_ptr<CDBBatch> batch;
public:
    using Signature = std::vector<unsigned char>;
    using CTeam = CAnchorData::CTeam;

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
        struct ByDeFiHeight{};

        THeight DeFiBlockHeight() const {return anchor.height;}
    };

    typedef boost::multi_index_container<AnchorRec,
        indexed_by<
            ordered_unique    < tag<AnchorRec::ByBtcTxHash>,  member<AnchorRec, uint256,  &AnchorRec::txHash> >,
            ordered_non_unique< tag<AnchorRec::ByBtcHeight>,  member<AnchorRec, THeight,  &AnchorRec::btcHeight> >,
            ordered_non_unique< tag<AnchorRec::ByDeFiHeight>, const_mem_fun<AnchorRec, THeight, &AnchorRec::DeFiBlockHeight> >
        >
    > AnchorIndexImpl;

    CAnchorIndex(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    bool Load() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    void ForEachAnchorByBtcHeight(std::function<bool(const CAnchorIndex::AnchorRec &)> callback) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    AnchorRec const * GetActiveAnchor() const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool ActivateBestAnchor(bool forced = false) EXCLUSIVE_LOCKS_REQUIRED(cs_main); // rescan anchors

    AnchorRec const * GetAnchorByTx(uint256 const & hash) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool AddAnchor(CAnchor const & anchor, uint256 const & btcTxHash, THeight btcBlockHeight, bool overwrite = true) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool DeleteAnchorByBtcTx(uint256 const & btcTxHash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    AnchorRec const * GetAnchorByBtcTx(uint256 const & txHash) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    using UnrewardedResult = std::set<uint256>;
    UnrewardedResult GetUnrewarded() const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    int GetAnchorConfirmations(uint256 const & txHash) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    int GetAnchorConfirmations(AnchorRec const * rec) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    void CheckActiveAnchor(uint32_t height, bool forced = false);
    void UpdateLastHeight(uint32_t height) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    // Post-fork anchor pending, requires chain context to validate. Some pending may be bogus, intentional or not.
    bool AddToAnchorPending(CAnchor const & anchor, uint256 const & btcTxHash, THeight btcBlockHeight, bool overwrite = false);
    bool GetPendingByBtcTx(uint256 const & txHash, AnchorRec & rec) const;
    bool DeletePendingByBtcTx(uint256 const & btcTxHash);
    void ForEachPending(std::function<void (const uint256 &, AnchorRec &)> callback);

    // Used to apply chain context to post-fork anchors which get added to pending.
    void CheckPendingAnchors(uint32_t height) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    // Store and read Bitcoin block hash by height, used in BestOfTwo calculation.
    bool WriteBlock(const uint32_t height, const uint256& blockHash);
    uint256 ReadBlockHash(const uint32_t& height);

    AnchorRec const * GetLatestAnchorUpToDeFiHeight(THeight blockHeightDeFi) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    AnchorIndexImpl anchors;
    AnchorRec const * top = nullptr;
    bool possibleReActivation = false;
    uint32_t spvLastHeight = 0;

private:
    template <typename Key, typename Value>
    bool IterateTable(char prefix, std::function<void(Key const &, Value &)> callback)
    {
        std::unique_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
        pcursor->Seek(prefix);

        while (pcursor->Valid())
        {
            if (ShutdownRequested()) break;
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

struct CAnchorConfirmData
{
    uint256 btcTxHash;
    THeight anchorHeight;
    THeight prevAnchorHeight;
    CKeyID rewardKeyID;
    char rewardKeyType;
    uint256 dfiBlockHash;
    THeight btcTxHeight;

    uint256 GetSignHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(btcTxHash);
        READWRITE(anchorHeight);
        READWRITE(prevAnchorHeight);
        READWRITE(rewardKeyID);
        READWRITE(rewardKeyType);
        READWRITE(dfiBlockHash);
        READWRITE(btcTxHeight);
    }
};

// single confirmation message
class CAnchorConfirmMessage : public CAnchorConfirmData
{
public:
    using Signature = std::vector<unsigned char>;

    // (base data + single sign)
    Signature signature;

    CAnchorConfirmMessage(CAnchorConfirmData const & base = CAnchorConfirmData())
        : CAnchorConfirmData(base)
        , signature()
    {}

    static std::optional<CAnchorConfirmMessage> CreateSigned(const CAnchor &anchor, const THeight prevAnchorHeight,
                                                               const uint256 &btcTxHash, CKey const & key, const THeight btcTxHeight);
    uint256 GetHash() const;
    CKeyID GetSigner() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CAnchorConfirmData, *this);
        READWRITE(signature);
    }

    // tags for multiindex
    struct ByMsgHash{};     // by message hash (for inv)
    struct ByAnchor{};      // by btctxhash
    struct ByKey{};         // composite, by btctxhash and GetSignHash for miner/reward creation
    struct ByVote{};        // composite, by GetSignHash and signer, helps detect doublesigning
};

// derived from common struct at list to avoid serialization mistakes
struct CAnchorFinalizationMessage : public CAnchorConfirmData
{
    std::vector<CAnchorConfirmMessage::Signature> sigs;

    CAnchorFinalizationMessage(CAnchorConfirmData const & base = CAnchorConfirmData())
        : CAnchorConfirmData(base)
        , sigs()
    {}

    size_t CheckConfirmSigs(CAnchorData::CTeam const & team, const uint32_t height);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CAnchorConfirmData, *this);
        READWRITE(sigs);
    }
};

class CAnchorAwaitingConfirms
{
    using ConfirmMessageHash = uint256;
    using AnchorTxHash = uint256;

private:
    using Confirm = CAnchorConfirmMessage;

    typedef boost::multi_index_container<Confirm,
        indexed_by<
            // index for p2p messaging (inv/getdata)
            ordered_unique<
                tag<Confirm::ByMsgHash>, const_mem_fun<Confirm, uint256, &Confirm::GetHash>
            >,
            // index for group confirms deletion
            ordered_non_unique<
                tag<Confirm::ByAnchor>, member<CAnchorConfirmData, uint256, &CAnchorConfirmData::btcTxHash>
            >,
            // index for quorum selection (miner affected) Protected against double signing.
            // just to remember that there may be confirms with equal btcTxHeight, but with different teams!
            ordered_unique<
                tag<Confirm::ByKey>, composite_key<Confirm,
                    member<CAnchorConfirmData, THeight, &CAnchorConfirmData::btcTxHeight>,
                    member<CAnchorConfirmData, uint256, &CAnchorConfirmData::btcTxHash>,
                    const_mem_fun<Confirm, CKeyID, &Confirm::GetSigner>
                >
            >,
            // restriction index that helps detect doublesigning
            // it may by quite expensive to index by GetSigner on the fly, but it should happen only on insertion
            ordered_unique<
                tag<Confirm::ByVote>, composite_key<Confirm,
                    const_mem_fun<CAnchorConfirmData, uint256, &CAnchorConfirmData::GetSignHash>,
                    const_mem_fun<Confirm, CKeyID, &Confirm::GetSigner>
                >
            >
        >
    > Confirms;

    Confirms confirms;

public:
    bool EraseAnchor(AnchorTxHash const &txHash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    const CAnchorConfirmMessage *GetConfirm(ConfirmMessageHash const &msgHash) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool Add(CAnchorConfirmMessage const &newConfirmMessage) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool Validate(CAnchorConfirmMessage const &confirmMessage) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void Clear() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void ReVote() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    std::vector<CAnchorConfirmMessage> GetQuorumFor(CAnchorData::CTeam const & team) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    void ForEachConfirm(std::function<void(Confirm const &)> callback) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
};

template <typename TContainer>
size_t CheckSigs(uint256 const & sigHash, TContainer const & sigs, std::set<CKeyID> const & keys)
{
    std::set<CPubKey> uniqueKeys;
    for (auto const & sig : sigs) {
        CPubKey pubkey;
        if (!pubkey.RecoverCompact(sigHash, sig) || keys.find(pubkey.GetID()) == keys.end())
            return false;

        uniqueKeys.insert(pubkey);
    }
    return uniqueKeys.size();
}

/// dummy, unknown consensus rules yet. may be additional params needed (smth like 'height')
/// even may be not here, but in CCustomCSView
uint32_t GetMinAnchorQuorum(CAnchorData::CTeam const & team);
CAmount GetAnchorSubsidy(int anchorHeight, int prevAnchorHeight, const Consensus::Params& consensusParams);

// thowing exceptions (not a bool due to more verbose rpc errors. may be 'status' or smth? )
/// Validates all except tx confirmations
bool ValidateAnchor(CAnchor const & anchor) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

// Validate anchor in the context of the active chain. This is used for anchor auths and anchors read from Bitcoin.
bool ContextualValidateAnchor(const CAnchorData& anchor, CBlockIndex &anchorBlock, uint64_t &anchorCreationHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

// Get info from data embedded into CAnchorData::heightAndHash
bool GetAnchorEmbeddedData(const CKeyID& data, uint64_t& anchorCreationHeight, std::shared_ptr<std::vector<unsigned char>>& prefix);

void CreateAndRelayConfirmMessageIfNeed(const CAnchorIndex::AnchorRec *anchor, const uint256 & btcTxHash, const CKey& masternodeKey) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

std::map<CKeyID, CKey> AmISignerNow(int height, CAnchorData::CTeam const & team) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

// Selects "best" of two anchors at the equal btc height (prevs must be checked before)
CAnchorIndex::AnchorRec const* BestOfTwo(CAnchorIndex::AnchorRec const* a1, CAnchorIndex::AnchorRec const* a2);

/** Global variables that points to the anchors and their auths (should be protected by cs_main) */
extern std::unique_ptr<CAnchorAuthIndex> panchorauths;
extern std::unique_ptr<CAnchorIndex> panchors;
extern std::unique_ptr<CAnchorAwaitingConfirms> panchorAwaitingConfirms;

namespace spv
{
// Define comparator and set to hold pending anchors
using PendingOrderType = std::function<bool (const CAnchorIndex::AnchorRec&, const CAnchorIndex::AnchorRec&)>;
using PendingSet = std::set<CAnchorIndex::AnchorRec, PendingOrderType>;
extern const PendingOrderType PendingOrder;
}

#endif // DEFI_MASTERNODES_ANCHORS_H
