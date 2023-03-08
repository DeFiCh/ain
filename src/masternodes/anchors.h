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
#include <uint256.h>

#include <functional>
#include <vector>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index_container.hpp>

#include <dbwrapper.h>

class CBlockIndex;
class CKey;
class CPubkey;

namespace spv {
class CSpvWrapper;
struct BtcAnchorTx;
}  // namespace spv

namespace Consensus {
struct Params;
}

typedef uint32_t THeight;  // cause not decided yet which type to use for heights

struct CAnchorData {
    using CTeam = std::set<CKeyID>;

    uint256 previousAnchor;  ///< Previous tx-AnchorAnnouncement on BTC chain
    THeight height;          ///< Height of the anchor block (DeFi)
    uint256 blockHash;       ///< Hash of the anchor block (DeFi)
    CTeam nextTeam;          ///< Calculated based on stake modifier at {blockHash}

    uint256 GetSignHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(previousAnchor);
        READWRITE(height);
        READWRITE(blockHash);
        READWRITE(nextTeam);
    }
};

class CAnchorAuthMessage : public CAnchorData {
    using Signature = std::vector<unsigned char>;

public:
    CAnchorAuthMessage(const CAnchorData &base = CAnchorData())
        : CAnchorData(base),
          signature() {}

    Signature GetSignature() const;
    uint256 GetHash() const;
    bool SignWithKey(const CKey &key);
    bool GetPubKey(CPubKey &pubKey) const;
    CKeyID GetSigner() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CAnchorData, *this);
        READWRITE(signature);
    }

    // tags for multiindex
    struct ByMsgHash {};    // by message hash (for inv)
    struct ByBlockHash {};  // by blockhash (for locator/GETANCHORAUTHS)
    struct ByKey {};        // composite, by height and GetSignHash for anchor creation
    struct ByVote {};       // composite, by GetSignHash and signer, helps detect doublesigning

private:
    Signature signature;
};

class CAnchor : public CAnchorData {
    using Signature = std::vector<unsigned char>;
    using CTeam     = CAnchorData::CTeam;

public:
    std::vector<Signature> sigs;
    CKeyID rewardKeyID;
    char rewardKeyType;

    CAnchor(const CAnchorData &base = CAnchorData())
        : CAnchorData(base),
          sigs(),
          rewardKeyID(),
          rewardKeyType(0) {}

    static CAnchor Create(const std::vector<CAnchorAuthMessage> &auths, const CTxDestination &rewardDest);
    bool CheckAuthSigs(const CTeam &team) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CAnchorData, *this);
        READWRITE(sigs);
        READWRITE(rewardKeyID);
        READWRITE(rewardKeyType);
    }
};

using namespace boost::multi_index;

class CAnchorAuthIndex {
    using CTeam = CAnchorData::CTeam;

public:
    using Auth = CAnchorAuthMessage;

    typedef boost::multi_index_container<
        Auth,
        indexed_by<
            // index for p2p messaging (inv/getdata)
            ordered_unique<tag<Auth::ByMsgHash>, const_mem_fun<Auth, uint256, &Auth::GetHash>>,
            // index for locator/GETANCHORAUTHS
            ordered_non_unique<tag<Auth::ByBlockHash>, member<CAnchorData, uint256, &CAnchorData::blockHash>>,
            // index for quorum selection (CreateBestAnchor())
            // just to remember that there may be auths with equal blockHash, but with different prevs and teams!
            ordered_non_unique<tag<Auth::ByKey>,
                               composite_key<Auth,
                                             member<CAnchorData, THeight, &CAnchorData::height>,
                                             const_mem_fun<CAnchorData, uint256, &CAnchorData::GetSignHash>>>,
            // restriction index that helps detect doublesigning
            // it may by quite expensive to index by GetSigner on the fly, but it should happen only on insertion
            ordered_unique<tag<Auth::ByVote>,
                           composite_key<Auth,
                                         const_mem_fun<CAnchorData, uint256, &CAnchorData::GetSignHash>,
                                         const_mem_fun<Auth, CKeyID, &Auth::GetSigner>>>

            >>
        Auths;

    const Auth *GetAuth(const uint256 &msgHash) const;
    const Auth *GetVote(const uint256 &signHash, const CKeyID &signer) const;
    bool ValidateAuth(const Auth &auth) const;
    bool AddAuth(const Auth &auth);

    CAnchor CreateBestAnchor(const CTxDestination &rewardDest) const;
    void ForEachAnchorAuthByHeight(std::function<bool(const CAnchorAuthIndex::Auth &)> callback) const;
    void PruneOlderThan(THeight height);

private:
    Auths auths;
};

class CAnchorIndex {
private:
    std::unique_ptr<CDBWrapper> db;
    std::unique_ptr<CDBBatch> batch;

public:
    using Signature = std::vector<unsigned char>;
    using CTeam     = CAnchorData::CTeam;

    struct AnchorRec {
        CAnchor anchor;
        uint256 txHash;
        THeight btcHeight;

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream &s, Operation ser_action) {
            READWRITE(anchor);
            READWRITE(txHash);
            READWRITE(btcHeight);
        }

        // tags for multiindex
        struct ByBtcTxHash {};
        struct ByBtcHeight {};
        struct ByDeFiHeight {};

        THeight DeFiBlockHeight() const { return anchor.height; }
    };

    typedef boost::multi_index_container<
        AnchorRec,
        indexed_by<ordered_unique<tag<AnchorRec::ByBtcTxHash>, member<AnchorRec, uint256, &AnchorRec::txHash>>,
                   ordered_non_unique<tag<AnchorRec::ByBtcHeight>, member<AnchorRec, THeight, &AnchorRec::btcHeight>>,
                   ordered_non_unique<tag<AnchorRec::ByDeFiHeight>,
                                      const_mem_fun<AnchorRec, THeight, &AnchorRec::DeFiBlockHeight>>>>
        AnchorIndexImpl;

    CAnchorIndex(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    bool Load();

    void ForEachAnchorByBtcHeight(std::function<bool(const CAnchorIndex::AnchorRec &)> callback) const;
    const AnchorRec *GetActiveAnchor() const;
    bool ActivateBestAnchor(bool forced = false);  // rescan anchors

    const AnchorRec *GetAnchorByTx(const uint256 &hash) const;

    bool AddAnchor(const CAnchor &anchor, const uint256 &btcTxHash, THeight btcBlockHeight, bool overwrite = true);
    bool DeleteAnchorByBtcTx(const uint256 &btcTxHash);

    CAnchorData::CTeam GetNextTeam(const uint256 &btcPrevTx) const;

    const AnchorRec *GetAnchorByBtcTx(const uint256 &txHash) const;

    using UnrewardedResult = std::set<uint256>;
    UnrewardedResult GetUnrewarded() const;

    int GetAnchorConfirmations(const uint256 &txHash) const;
    int GetAnchorConfirmations(const AnchorRec *rec) const;

    void CheckActiveAnchor(uint32_t height, bool forced = false);
    void UpdateLastHeight(uint32_t height);

    // Post-fork anchor pending, requires chain context to validate. Some pending may be bogus, intentional or not.
    bool AddToAnchorPending(const AnchorRec &rec);
    bool GetPendingByBtcTx(const uint256 &txHash, AnchorRec &rec) const;
    void DeletePendingByBtcTx(const uint256 &btcTxHash);
    void ForEachPending(std::function<void(const uint256 &, AnchorRec &)> callback);

    // Used to apply chain context to post-fork anchors which get added to pending.
    void CheckPendingAnchors();

    // Store and read Bitcoin block hash by height, used in BestOfTwo calculation.
    bool WriteBlock(const uint32_t height, const uint256 &blockHash);
    uint256 ReadBlockHash(const uint32_t &height);

    const AnchorRec *GetLatestAnchorUpToDeFiHeight(THeight blockHeightDeFi) const;

private:
    AnchorIndexImpl anchors;
    const AnchorRec *top      = nullptr;
    bool possibleReActivation = false;
    uint32_t spvLastHeight    = 0;

private:
    template <typename Key, typename Value>
    bool IterateTable(char prefix, std::function<void(const Key &, Value &)> callback) {
        std::unique_ptr<CDBIterator> pcursor(const_cast<CDBWrapper *>(&*db)->NewIterator());
        pcursor->Seek(prefix);

        while (pcursor->Valid()) {
            std::pair<char, Key> key;
            if (pcursor->GetKey(key) && key.first == prefix) {
                Value value;
                if (pcursor->GetValue(value)) {
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

    bool DbExists(const uint256 &hash) const;
    bool DbWrite(const AnchorRec &anchor);
    bool DbErase(const uint256 &hash);
};

struct CAnchorConfirmData {
    uint256 btcTxHash;
    THeight anchorHeight;
    THeight prevAnchorHeight;
    CKeyID rewardKeyID;
    char rewardKeyType;

    uint256 GetSignHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(btcTxHash);
        READWRITE(anchorHeight);
        READWRITE(prevAnchorHeight);
        READWRITE(rewardKeyID);
        READWRITE(rewardKeyType);
    }
};

// derived from common struct at list to avoid serialization mistakes
struct CAnchorFinalizationMessage : public CAnchorConfirmData {
    using Signature = std::vector<unsigned char>;

    // (base data + teams + all signs)
    CAnchorData::CTeam nextTeam;
    CAnchorData::CTeam currentTeam;
    std::vector<Signature> sigs;

    CAnchorFinalizationMessage(const CAnchorConfirmData &base = CAnchorConfirmData())
        : CAnchorConfirmData(base),
          nextTeam(),
          currentTeam(),
          sigs() {}

    bool CheckConfirmSigs();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CAnchorConfirmData, *this);
        READWRITE(nextTeam);
        READWRITE(currentTeam);
        READWRITE(sigs);
    }
};

struct CAnchorConfirmDataPlus : public CAnchorConfirmData {
    uint256 dfiBlockHash;
    THeight btcTxHeight{0};

    CAnchorConfirmDataPlus(const uint256 &btcTxHash,
                           const THeight anchorHeight,
                           const THeight prevAnchorHeight,
                           const CKeyID &rewardKeyID,
                           const char rewardKeyType,
                           const uint256 &dfiBlockHash,
                           const THeight btcTxHeight)
        : CAnchorConfirmData{btcTxHash, anchorHeight, prevAnchorHeight, rewardKeyID, rewardKeyType},
          dfiBlockHash{dfiBlockHash},
          btcTxHeight{btcTxHeight} {}

    explicit CAnchorConfirmDataPlus(const CAnchorFinalizationMessage &base)
        : CAnchorConfirmData(base) {}

    CAnchorConfirmDataPlus() = default;

    uint256 GetSignHash() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CAnchorConfirmData, *this);
        READWRITE(dfiBlockHash);
        READWRITE(btcTxHeight);
    }
};

// single confirmation message
class CAnchorConfirmMessage : public CAnchorConfirmDataPlus {
public:
    using Signature = std::vector<unsigned char>;

    // (base data + single sign)
    Signature signature;

    CAnchorConfirmMessage(const CAnchorConfirmDataPlus &base = CAnchorConfirmDataPlus())
        : CAnchorConfirmDataPlus(base),
          signature() {}

    static std::optional<CAnchorConfirmMessage> CreateSigned(const CAnchor &anchor,
                                                             const THeight prevAnchorHeight,
                                                             const uint256 &btcTxHash,
                                                             const CKey &key,
                                                             const THeight btcTxHeight);
    uint256 GetHash() const;
    CKeyID GetSigner() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CAnchorConfirmDataPlus, *this);
        READWRITE(signature);
    }

    // tags for multiindex
    struct ByMsgHash {};  // by message hash (for inv)
    struct ByAnchor {};   // by btctxhash
    struct ByKey {};      // composite, by btctxhash and GetSignHash for miner/reward creation
    struct ByVote {};     // composite, by GetSignHash and signer, helps detect doublesigning
};

// derived from common struct at list to avoid serialization mistakes
struct CAnchorFinalizationMessagePlus : public CAnchorConfirmDataPlus {
    std::vector<CAnchorConfirmMessage::Signature> sigs;

    CAnchorFinalizationMessagePlus(const CAnchorConfirmDataPlus &base = CAnchorConfirmDataPlus())
        : CAnchorConfirmDataPlus(base),
          sigs() {}

    size_t CheckConfirmSigs(const uint32_t height);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CAnchorConfirmDataPlus, *this);
        READWRITE(sigs);
    }
};

class CAnchorAwaitingConfirms {
    using ConfirmMessageHash = uint256;
    using AnchorTxHash       = uint256;

private:
    using Confirm = CAnchorConfirmMessage;

    typedef boost::multi_index_container<
        Confirm,
        indexed_by<
            // index for p2p messaging (inv/getdata)
            ordered_unique<tag<Confirm::ByMsgHash>, const_mem_fun<Confirm, uint256, &Confirm::GetHash>>,
            // index for group confirms deletion
            ordered_non_unique<tag<Confirm::ByAnchor>,
                               member<CAnchorConfirmData, uint256, &CAnchorConfirmData::btcTxHash>>,
            // index for quorum selection (miner affected) Protected against double signing.
            // just to remember that there may be confirms with equal btcTxHeight, but with different teams!
            ordered_unique<tag<Confirm::ByKey>,
                           composite_key<Confirm,
                                         member<CAnchorConfirmDataPlus, THeight, &CAnchorConfirmDataPlus::btcTxHeight>,
                                         member<CAnchorConfirmData, uint256, &CAnchorConfirmData::btcTxHash>,
                                         const_mem_fun<Confirm, CKeyID, &Confirm::GetSigner>>>,
            // restriction index that helps detect doublesigning
            // it may by quite expensive to index by GetSigner on the fly, but it should happen only on insertion
            ordered_unique<
                tag<Confirm::ByVote>,
                composite_key<Confirm,
                              const_mem_fun<CAnchorConfirmDataPlus, uint256, &CAnchorConfirmDataPlus::GetSignHash>,
                              const_mem_fun<Confirm, CKeyID, &Confirm::GetSigner>>>>>
        Confirms;

    Confirms confirms;

public:
    bool EraseAnchor(const AnchorTxHash &txHash);
    const CAnchorConfirmMessage *GetConfirm(const ConfirmMessageHash &msgHash) const;
    bool Add(const CAnchorConfirmMessage &newConfirmMessage);
    bool Validate(const CAnchorConfirmMessage &confirmMessage) const;
    void Clear();
    void ReVote();
    std::vector<CAnchorConfirmMessage> GetQuorumFor(const CAnchorData::CTeam &team) const;

    void ForEachConfirm(std::function<void(const Confirm &)> callback) const;
};

template <typename TContainer>
size_t CheckSigs(const uint256 &sigHash, const TContainer &sigs, const std::set<CKeyID> &keys) {
    std::set<CPubKey> uniqueKeys;
    for (const auto &sig : sigs) {
        CPubKey pubkey;
        if (!pubkey.RecoverCompact(sigHash, sig) || keys.find(pubkey.GetID()) == keys.end())
            return false;

        uniqueKeys.insert(pubkey);
    }
    return uniqueKeys.size();
}

/// dummy, unknown consensus rules yet. may be additional params needed (smth like 'height')
/// even may be not here, but in CCustomCSView
uint32_t GetMinAnchorQuorum(const CAnchorData::CTeam &team);
CAmount GetAnchorSubsidy(int anchorHeight, int prevAnchorHeight, const Consensus::Params &consensusParams);

// thowing exceptions (not a bool due to more verbose rpc errors. may be 'status' or smth? )
/// Validates all except tx confirmations
bool ValidateAnchor(const CAnchor &anchor);

// Validate anchor in the context of the active chain. This is used for anchor auths and anchors read from Bitcoin.
bool ContextualValidateAnchor(const CAnchorData &anchor, CBlockIndex &anchorBlock, uint64_t &anchorCreationHeight);

// Get info from data embedded into CAnchorData::nextTeam
bool GetAnchorEmbeddedData(const CKeyID &data,
                           uint64_t &anchorCreationHeight,
                           std::shared_ptr<std::vector<unsigned char>> &prefix);

// Selects "best" of two anchors at the equal btc height (prevs must be checked before)
const CAnchorIndex::AnchorRec *BestOfTwo(const CAnchorIndex::AnchorRec *a1, const CAnchorIndex::AnchorRec *a2);

/** Global variables that points to the anchors and their auths (should be protected by cs_main) */
extern std::unique_ptr<CAnchorAuthIndex> panchorauths;
extern std::unique_ptr<CAnchorIndex> panchors;
extern std::unique_ptr<CAnchorAwaitingConfirms> panchorAwaitingConfirms;

namespace spv {
// Define comparator and set to hold pending anchors
using PendingOrderType = std::function<bool(const CAnchorIndex::AnchorRec &, const CAnchorIndex::AnchorRec &)>;
using PendingSet       = std::set<CAnchorIndex::AnchorRec, PendingOrderType>;
extern const PendingOrderType PendingOrder;
}  // namespace spv

#endif  // DEFI_MASTERNODES_ANCHORS_H
