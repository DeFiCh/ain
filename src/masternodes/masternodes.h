// Copyright (c) 2019 The IntegralTeam
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODES_H
#define MASTERNODES_H

#include "amount.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "serialize.h"
#include "uint256.h"
#include "validation.h"

#include <map>
#include <set>
#include <stdint.h>
#include <iostream>

#include <boost/optional.hpp>
#include <boost/scoped_ptr.hpp>

static const std::vector<unsigned char> MnTxMarker = {'M', 'n', 'T', 'x'};  // 4d6e5478

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
int GetMnCollateralUnlockDelay();
CAmount GetMnCollateralAmount();
CAmount GetMnCreationFee(int height);

class CMasternode
{
public:
    //! Owner auth address == collateral address. Can be used as an ID.
    CKeyID ownerAuthAddress;
    //! Operator auth address. Can be equal to ownerAuthAddress. Can be used as an ID
    CKeyID operatorAuthAddress;

    //! MN creation block height
    int32_t height;
    //! Resign height
    int32_t resignHeight;

    //! This fields are for transaction rollback (by disconnecting block)
    uint256 resignTx;

    //! empty constructor
    CMasternode();

    //! constructor helper, runs without any checks
    void FromTx(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata);

    //! construct a CMasternode from a CTransaction, at a given height
    CMasternode(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata)
    {
        FromTx(tx, heightIn, metadata);
    }

    bool IsActive() const
    {
        return  height + GetMnActivationDelay() <= ::ChainActive().Height() && resignTx == uint256() && resignHeight == -1;
    }

    std::string GetHumanReadableStatus() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ownerAuthAddress);
        READWRITE(operatorAuthAddress);

        READWRITE(height);
        READWRITE(resignHeight);

        READWRITE(resignTx);
    }

    //! equality test
    friend bool operator==(CMasternode const & a, CMasternode const & b);
    friend bool operator!=(CMasternode const & a, CMasternode const & b);
};

typedef std::map<uint256, CMasternode> CMasternodes;  // nodeId -> masternode object,
//typedef std::set<uint256> CActiveMasternodes;         // just nodeId's,
typedef std::map<CKeyID, uint256> CMasternodesByAuth; // for two indexes, owner->nodeId, operator->nodeId

//struct TeamData
//{
//    int32_t joinHeight;
//    CKeyID  operatorAuth;
//};

//typedef std::map<uint256, TeamData> CTeam;   // nodeId -> <joinHeight, operatorAuth> - masternodes' team

class CMasternodesViewCache;

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
    typedef std::map<std::pair<int, uint256>, std::pair<uint256, MasternodesTxType> > CTxUndo;
//    typedef std::map<int, CTeam> CTeams;

    enum class AuthIndex { ByOwner, ByOperator };
    enum class VoteIndex { From, Against };

protected:
    int lastHeight;
    CMasternodes allNodes;
//    CActiveMasternodes activeNodes;
    CMasternodesByAuth nodesByOwner;
    CMasternodesByAuth nodesByOperator;

    CTxUndo txsUndo;
//    CTeams teams;

    CMasternodesView() : lastHeight(0) {}

    CMasternodesView(CMasternodesView const & other) = delete;

//    void Init(CMasternodesView * other)
//    {
//        lastHeight = other->lastHeight;
//        allNodes = other->allNodes;
////        activeNodes = other->activeNodes;
//        nodesByOwner = other->nodesByOwner;
//        nodesByOperator = other->nodesByOperator;

//        txsUndo = other->txsUndo;

//        // on-demand
////        teams = other->teams;
//    }

public:
    CMasternodesView & operator=(CMasternodesView const & other) = delete;

    void Clear();

    virtual ~CMasternodesView() {}

    void SetLastHeight(int h)
    {
        lastHeight = h;
    }
    int GetLastHeight()
    {
        return lastHeight;
    }

//    CMasternodes const & GetMasternodes() const
//    {
//        return allNodes;
//    }

//    CActiveMasternodes const & GetActiveMasternodes() const
//    {
//        return activeNodes;
//    }

//    CMasternodesByAuth const & GetMasternodesByOperator() const
//    {
//        return nodesByOperator;
//    }

//    CMasternodesByAuth const & GetMasternodesByOwner() const
//    {
//        return nodesByOwner;
//    }

    //! Initial load of all data
    virtual bool Load() { assert(false); }
    virtual bool Flush() { assert(false); }

    virtual boost::optional<CMasternodesByAuth::const_iterator>
    ExistMasternode(AuthIndex where, CKeyID const & auth) const;

    virtual CMasternode const * ExistMasternode(uint256 const & id) const;

    bool CanSpend(uint256 const & nodeId, int height) const;
    bool IsAnchorInvolved(uint256 const & nodeId, int height) const;

    bool OnMasternodeCreate(uint256 const & nodeId, CMasternode const & node);
    bool OnMasternodeResign(uint256 const & nodeId, uint256 const & txid, int height);
    void OnUndo(int height, uint256 const & txid);

    void PruneOlder(int height);

//    bool IsTeamMember(int height, CKeyID const & operatorAuth) const;
//    CTeam CalcNextDposTeam(CActiveMasternodes const & activeNodes, CMasternodes const & allNodes, uint256 const & blockHash, int height);
//    virtual CTeam const & ReadDposTeam(int height) const;

protected:
    virtual std::pair<uint256, MasternodesTxType> GetUndo(CTxUndo::key_type key) const;

//    virtual void WriteDposTeam(int height, CTeam const & team);


private:
    boost::optional<CMasternodeIDs> AmI(AuthIndex where) const;
public:
    boost::optional<CMasternodeIDs> AmIOperator() const;
    boost::optional<CMasternodeIDs> AmIOwner() const;
//    boost::optional<CMasternodeIDs> AmIActiveOperator() const;
//    boost::optional<CMasternodeIDs> AmIActiveOwner() const;

    friend class CMasternodesViewCache;
};

class CMasternodesViewCache : public CMasternodesView
{
private:
    CMasternodesView * base;
    CMasternodesViewCache() {}

public:
    CMasternodesViewCache(CMasternodesView * other)
        : CMasternodesView()
        , base(other)
    {
        lastHeight = base->lastHeight;
//        Init(other);

        // cached items are empty!
    }

    ~CMasternodesViewCache() override {}


    boost::optional<CMasternodesByAuth::const_iterator>
    ExistMasternode(AuthIndex where, CKeyID const & auth) const override
    {
        CMasternodesByAuth const & index = (where == AuthIndex::ByOwner) ? nodesByOwner : nodesByOperator;
        auto it = index.find(auth);
        if (it == index.end())
        {
            return base->ExistMasternode(where, auth);
        }
        return {it};
    }

    CMasternode const * ExistMasternode(uint256 const & id) const override
    {
        CMasternodes::const_iterator it = allNodes.find(id);
        return it == allNodes.end() ? base->ExistMasternode(id) : &it->second;
    }

    std::pair<uint256, MasternodesTxType> GetUndo(CTxUndo::key_type key) const override
    {
        CTxUndo::const_iterator it = txsUndo.find(key);
        return it == txsUndo.end() ? base->GetUndo(key) : it->second;
    }


    bool Flush() override
    {
        base->lastHeight = lastHeight;

        for (auto const & pair : allNodes) {
            base->allNodes[pair.first] = pair.second; // possible empty (if deleted/applyed)
        }
        allNodes.clear();

        for (auto const & pair : nodesByOwner) {
            base->nodesByOwner[pair.first] = pair.second; // possible empty (if deleted/applyed)
        }
        nodesByOwner.clear();

        for (auto const & pair : nodesByOperator) {
            base->nodesByOperator[pair.first] = pair.second; // possible empty (if deleted/applyed)
        }
        nodesByOperator.clear();

        for (auto const & pair : txsUndo) {
            base->txsUndo[pair.first] = pair.second; // possible empty (if deleted/applyed)
        }
        txsUndo.clear();

//        for (auto const & pair : teams)
//        {
//            base->WriteDposTeam(pair.first, pair.second);
//        }
//        teams.clear();
        return true;
    }

//    virtual CTeam const & ReadDposTeam(int height) const
//    {
//        auto const it = teams.find(height);
//        // return cached (new) or original value
//        return it != teams.end() ? it->second : base->ReadDposTeam(height);
//    }

};

/** Global variable that points to the CMasternodeView (should be protected by cs_main) */
extern std::unique_ptr<CMasternodesView> pmasternodesview;

//! Checks if given tx is probably one of custom 'MasternodeTx', returns tx type and serialized metadata in 'data'
MasternodesTxType GuessMasternodeTxType(CTransaction const & tx, std::vector<unsigned char> & metadata);

#endif // MASTERNODES_H
