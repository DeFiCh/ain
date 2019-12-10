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
    bool CheckSigs(CTeam const & team) const;

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

    /// dummy, unknown consensus rules yet. may be additional params needed (smth like 'height')
    /// even may be not here, but in CMasternodesView
    uint32_t GetMinAnchorQuorum(CMasternodesView::CTeam const & team) const;

    CAnchorMessage CreateBestAnchor(uint256 const & forBlock = uint256(), CScript const & rewardScript = {}) const;

    Auths auths;
};

class CAnchorIndex
{
private:
    boost::shared_ptr<CDBWrapper> db;
    boost::scoped_ptr<CDBBatch> batch;
public:
    CAnchorIndex(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool ExistsAnchor(uint256 const & hash) const;
    bool ReadAnchor(uint256 const & hash, CAnchorMessage & anchor) const;
    bool WriteAnchor(CAnchorMessage const & anchor);
    bool EraseAnchor(uint256 const & hash);
};

// thowing exceptions (not a bool due to more verbose rpc errors. may be 'status' or smth? )
void ValidateAnchor(CAnchorMessage const & anchor);
CMasternodesView::CTeam GetNextTeamFromPrev(uint256 const & btcPrevTx);


/** Global variables that points to the anchors and their auths (should be protected by cs_main) */
extern std::unique_ptr<CAnchorAuthIndex> panchorauths;
extern std::unique_ptr<CAnchorIndex> panchors;

#endif // BITCOIN_MASTERNODES_ANCHORS_H
