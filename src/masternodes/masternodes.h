// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_MASTERNODES_H
#define BITCOIN_MASTERNODES_MASTERNODES_H

#include <amount.h>
#include <pubkey.h>
#include <serialize.h>
#include <uint256.h>

#include <map>
#include <set>
#include <stdint.h>
#include <iostream>

#include <boost/optional.hpp>
#include <boost/scoped_ptr.hpp>

class CTransaction;
class CBlockHeader;

static const std::vector<unsigned char> MnTxMarker = {'M', 'n', 'T', 'x'};  // 4d6e5478
static const std::vector<unsigned char> MnCriminalTxMarker = {'M', 'n', 'C', 'r'};

static const unsigned int DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL = 100;


enum class MasternodesTxType : unsigned char
{
    None = 0,
    CreateMasternode    = 'C',
    ResignMasternode    = 'R'
};

template<typename Stream>
inline void Serialize(Stream& s, MasternodesTxType txType)
{
    Serialize(s, static_cast<unsigned char>(txType));
}

template<typename Stream>
inline void Unserialize(Stream& s, MasternodesTxType & txType) {
    unsigned char ch;
    Unserialize(s, ch);
    txType = ch == 'C' ? MasternodesTxType::CreateMasternode :
             ch == 'R' ? MasternodesTxType::ResignMasternode :
                         MasternodesTxType::None;
}

// Works instead of constants cause 'regtest' differs (don't want to overcharge chainparams)
int GetMnActivationDelay();
int GetMnResignDelay();
int GetMnCollateralUnlockDelay();
int GetMnHistoryFrame();
CAmount GetMnCollateralAmount();
CAmount GetMnCreationFee(int height);

class CMasternode
{
public:
    //! Minted blocks counter
    uint32_t mintedBlocks;

    //! Owner auth address == collateral address. Can be used as an ID.
    CKeyID ownerAuthAddress;
    char ownerType;

    //! Operator auth address. Can be equal to ownerAuthAddress. Can be used as an ID
    CKeyID operatorAuthAddress;
    char operatorType;

    //! MN creation block height
    int32_t creationHeight;
    //! Resign height
    int32_t resignHeight;

    //! This fields are for transaction rollback (by disconnecting block)
    uint256 resignTx;

    //! empty constructor
    CMasternode();
    //! construct a CMasternode from a CTransaction, at a given height
    CMasternode(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata);

    //! constructor helper, runs without any checks
    void FromTx(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata);

    bool IsActive() const;
    bool IsActive(int h) const;

    std::string GetHumanReadableStatus() const;
    std::string GetHumanReadableStatus(int h) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mintedBlocks);
        READWRITE(ownerAuthAddress);
        READWRITE(ownerType);
        READWRITE(operatorAuthAddress);
        READWRITE(operatorType);

        READWRITE(creationHeight);
        READWRITE(resignHeight);

        READWRITE(resignTx);
    }

    //! equality test
    friend bool operator==(CMasternode const & a, CMasternode const & b);
    friend bool operator!=(CMasternode const & a, CMasternode const & b);
};

typedef std::map<uint256, CMasternode> CMasternodes;  // nodeId -> masternode object,
typedef std::map<CKeyID, uint256> CMasternodesByAuth; // for two indexes, owner->nodeId, operator->nodeId

class CMasternodesViewCache;
class CMasternodesViewHistory;

class CMasternodesView
{
public:
    // Block of typedefs
    struct CMasternodeIDs
    {
        uint256 id;
        CKeyID operatorAuthAddress;
        CKeyID ownerAuthAddress;
    };
    typedef std::map<int, std::pair<uint256, MasternodesTxType> > CMnTxsUndo; // txn, undoRec
    typedef std::map<int, CMnTxsUndo> CMnBlocksUndo;
    typedef std::map<uint256, std::pair<CBlockHeader, CBlockHeader>> CMnCriminals;
//    typedef std::map<int, CTeam> CTeams;

    enum class AuthIndex { ByOwner, ByOperator };

protected:
    int lastHeight;
    CMasternodes allNodes;
    CMasternodesByAuth nodesByOwner;
    CMasternodesByAuth nodesByOperator;

    CMnCriminals criminals;

    CMnBlocksUndo blocksUndo;

    CMasternodesView() : lastHeight(0) {}

public:
    CMasternodesView & operator=(CMasternodesView const & other) = delete;

    void ApplyCache(CMasternodesView const * cache);
    void Clear();

    bool IsEmpty() const
    {
        return allNodes.empty() && nodesByOwner.empty() && nodesByOperator.empty() && blocksUndo.empty();
    }

    virtual ~CMasternodesView() {}

    void SetLastHeight(int h)
    {
        lastHeight = h;
    }
    int GetLastHeight() const
    {
        return lastHeight;
    }

    void IncrementMintedBy(CKeyID const & minter)
    {
        auto it = ExistMasternode(AuthIndex::ByOperator, minter);
        assert(it);
        auto const & nodeId = (*it)->second;
        auto nodePtr = ExistMasternode(nodeId);
        assert(nodePtr);
        auto & node = allNodes[nodeId] = *nodePtr; // cause may be cached!!
        ++node.mintedBlocks;
    }

    void DecrementMintedBy(CKeyID const & minter)
    {
        auto it = ExistMasternode(AuthIndex::ByOperator, minter);
        assert(it);
        auto const & nodeId = (*it)->second;
        auto nodePtr = ExistMasternode(nodeId);
        assert(nodePtr);
        auto & node = allNodes[nodeId] = *nodePtr; // cause may be cached!!
        --node.mintedBlocks;
    }

    virtual CMasternodes GetMasternodes() const
    {
        /// for tests now, will be changed
        return allNodes;
    }

    virtual CMnCriminals GetCriminals() const
    {
        return criminals;
    }

    //! Initial load of all data
    virtual bool Load() { assert(false); }
    virtual bool Flush() { assert(false); }

    virtual boost::optional<CMasternodesByAuth::const_iterator>
    ExistMasternode(AuthIndex where, CKeyID const & auth) const;

    virtual CMasternode const * ExistMasternode(uint256 const & id) const;

    virtual void WriteMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash, CBlockHeader const & blockHeader)  { assert(false); }
    virtual bool FindMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, std::map<uint256, CBlockHeader> & blockHeaders)  { assert(false); }
    virtual void EraseMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash)  { assert(false); }

    bool CanSpend(uint256 const & nodeId, int height) const;
    bool IsAnchorInvolved(uint256 const & nodeId, int height) const;

    bool OnMasternodeCreate(uint256 const & nodeId, CMasternode const & node, int txn);
    bool OnMasternodeResign(uint256 const & nodeId, uint256 const & txid, int height, int txn);
    CMasternodesViewCache OnUndoBlock(int height);

    void PruneOlder(int height);

//    bool IsTeamMember(int height, CKeyID const & operatorAuth) const;
//    CTeam CalcNextDposTeam(CActiveMasternodes const & activeNodes, CMasternodes const & allNodes, uint256 const & blockHash, int height);
//    virtual CTeam const & ReadDposTeam(int height) const;

    bool CheckDoubleSignProof(CBlockHeader const & oneHeader, CBlockHeader const & twoHeader);
    void MarkMasternodeAsCriminals(uint256 const & id, CBlockHeader const & blockHeader, CBlockHeader const & conflictBlockHeader);
    CMasternodesView::CMnCriminals::iterator RemoveMasternodeFromCriminals(CMnCriminals::iterator it);

protected:
    virtual CMnBlocksUndo::mapped_type const & GetBlockUndo(CMnBlocksUndo::key_type key) const;

private:
    boost::optional<CMasternodeIDs> AmI(AuthIndex where) const;
public:
    boost::optional<CMasternodeIDs> AmIOperator() const;
    boost::optional<CMasternodeIDs> AmIOwner() const;

    friend class CMasternodesViewCache;
    friend class CMasternodesViewHistory;
};


class CMasternodesViewCache : public CMasternodesView
{
protected:
    CMasternodesView * base;

public:
    CMasternodesViewCache(CMasternodesView * other)
        : CMasternodesView()
        , base(other)
    {
        assert(base);
        lastHeight = base->lastHeight;
        // cached items are empty!
    }

    ~CMasternodesViewCache() override {}

    CMasternodes GetMasternodes() const override
    {
        auto const baseNodes = base->GetMasternodes();
        CMasternodes result(allNodes);
        result.insert(baseNodes.begin(), baseNodes.end());
        return result;
    }

    boost::optional<CMasternodesByAuth::const_iterator>
    ExistMasternode(AuthIndex where, CKeyID const & auth) const override
    {
        CMasternodesByAuth const & index = (where == AuthIndex::ByOwner) ? nodesByOwner : nodesByOperator;
        auto it = index.find(auth);
        if (it == index.end())
        {
            return base->ExistMasternode(where, auth);
        }
        if (it->second.IsNull())
        {
            return {};
        }
        return {it};
    }

    CMasternode const * ExistMasternode(uint256 const & id) const override
    {
        CMasternodes::const_iterator it = allNodes.find(id);
        return it == allNodes.end() ? base->ExistMasternode(id) : it->second != CMasternode() ? &it->second : nullptr;
    }

    CMnBlocksUndo::mapped_type const & GetBlockUndo(CMnBlocksUndo::key_type key) const override
    {
        CMnBlocksUndo::const_iterator it = blocksUndo.find(key);
        return it == blocksUndo.end() ? base->GetBlockUndo(key) : it->second;
    }

    bool Flush() override
    {
        base->ApplyCache(this);
        Clear();

        return true;
    }
};


class CMasternodesViewHistory : public CMasternodesViewCache
{
protected:
    std::map<int, CMasternodesViewCache> historyDiff;

public:
    CMasternodesViewHistory(CMasternodesView * top) : CMasternodesViewCache(top) { assert(top); }

    bool Flush() override { assert(false); } // forbidden!!!

    CMasternodesViewHistory & GetState(int targetHeight);
};

/** Global variable that points to the CMasternodeView (should be protected by cs_main) */
extern std::unique_ptr<CMasternodesView> pmasternodesview;

//! Checks if given tx is probably one of custom 'MasternodeTx', returns tx type and serialized metadata in 'data'
MasternodesTxType GuessMasternodeTxType(CTransaction const & tx, std::vector<unsigned char> & metadata);

#endif // BITCOIN_MASTERNODES_MASTERNODES_H
