// Copyright (c) 2019 DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_txdb.h>

#include <chainparams.h>
#include <uint256.h>

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

// Prefixes to the masternodes database (masternodes/)
static const char DB_MASTERNODES = 'M';     // main masternodes table
static const char DB_MASTERNODESUNDO = 'U'; // undo table
static const char DB_MN_HEIGHT = 'H';       // single record with last processed chain height
static const char DB_PRUNE_HEIGHT = 'P';    // single record with pruned height (for validation of reachable data window)

static const char DB_MN_BLOCK_HEADERS = 'h';
static const char DB_MN_CRIMINALS = 'm';
static const char DB_MN_CURRENT_TEAM = 't';
static const char DB_MN_FOUNDERS_DEBT = 'd';

struct DBMNBlockHeadersSearchKey
{
    uint256 masternodeID;
    uint64_t mintedBlocks;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(masternodeID);
        READWRITE(mintedBlocks);
    }
};

struct DBMNBlockHeadersKey
{
    char prefix;
    DBMNBlockHeadersSearchKey searchKey;
    uint256 blockHash;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(prefix);
        READWRITE(searchKey);
        READWRITE(blockHash);
    }
};

struct DBMNBlockedCriminalCoins
{
    char prefix;
    uint256 txid;
    uint32_t index;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(prefix);
        READWRITE(txid);
        READWRITE(index);
    }
};

CMasternodesViewDB::CMasternodesViewDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : db(new CDBWrapper(GetDataDir() / "masternodes", nCacheSize, fMemory, fWipe))
{
}

// for test purposes only
CMasternodesViewDB::CMasternodesViewDB()
    : db(nullptr)
{
}

void CMasternodesViewDB::CommitBatch()
{
    if (batch)
    {
        db->WriteBatch(*batch);
        batch.reset();
    }
}

bool CMasternodesViewDB::ReadHeight(int & h)
{
    // it's a hack, cause we don't know active chain tip at the loading time
    if (!db->Read(DB_MN_HEIGHT, h))
    {
        h = 0;
    }
    return true;
}

void CMasternodesViewDB::WriteHeight(int h)
{
    BatchWrite(DB_MN_HEIGHT, h);
}

void CMasternodesViewDB::WriteMasternode(uint256 const & txid, CMasternode const & node)
{
    BatchWrite(make_pair(DB_MASTERNODES, txid), node);
}

void CMasternodesViewDB::EraseMasternode(uint256 const & txid)
{
    BatchErase(make_pair(DB_MASTERNODES, txid));
}

void CMasternodesViewDB::WriteMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash, CBlockHeader const & blockHeader, bool fIsFakeNet)
{
    if (fIsFakeNet) {
        return;
    }

    db->Write(DBMNBlockHeadersKey{DB_MN_BLOCK_HEADERS, DBMNBlockHeadersSearchKey{txid, mintedBlocks}, hash}, blockHeader);
}

bool CMasternodesViewDB::FindMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, std::map<uint256, CBlockHeader> & blockHeaders, bool fIsFakeNet)
{
    if (fIsFakeNet) {
        return false;
    }

    if (blockHeaders.size() != 0) {
        blockHeaders.clear();
    }

    pair<char, DBMNBlockHeadersSearchKey> prefix{DB_MN_BLOCK_HEADERS, DBMNBlockHeadersSearchKey{txid, mintedBlocks}};
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(prefix);

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        DBMNBlockHeadersKey key;
        if (pcursor->GetKey(key) &&
            key.prefix == DB_MN_BLOCK_HEADERS &&
            key.searchKey.masternodeID == txid &&
            key.searchKey.mintedBlocks == mintedBlocks) {
            CBlockHeader blockHeader;
            if (pcursor->GetValue(blockHeader)) {
                blockHeaders.emplace(key.blockHash, std::move(blockHeader));
            } else {
                return error("MNDB::FoundMintedBlockHeader() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    return true;
}

void CMasternodesViewDB::EraseMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash)
{
    db->Erase(DBMNBlockHeadersKey{DB_MN_BLOCK_HEADERS, DBMNBlockHeadersSearchKey{txid, mintedBlocks}, hash});
}

void CMasternodesViewDB::WriteCriminal(uint256 const & mnId, CDoubleSignFact const & doubleSignFact)
{
    db->Write(make_pair(DB_MN_CRIMINALS, mnId), doubleSignFact);
}

void CMasternodesViewDB::EraseCriminal(uint256 const & mnId)
{
    db->Erase(make_pair(DB_MN_CRIMINALS, mnId));
}

void CMasternodesViewDB::WriteCurrentTeam(std::set<CKeyID> const & currentTeam)
{
    uint32_t i = 0;
    for (std::set<CKeyID>::iterator it = currentTeam.begin(); it != currentTeam.end(); ++it) {
        db->Write(make_pair(DB_MN_CURRENT_TEAM, i++), *it);
    }
}

bool CMasternodesViewDB::LoadCurrentTeam(std::set<CKeyID> & newTeam)
{
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(DB_MN_CURRENT_TEAM);

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint32_t> key;
        if (pcursor->GetKey(key) && key.first == DB_MN_CURRENT_TEAM) {
            CKeyID mnId;
            if (pcursor->GetValue(mnId)) {
                newTeam.insert(mnId);
            } else {
                return error("MNDB::LoadCurrentTeam() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }

    return true;
}

bool CMasternodesViewDB::EraseCurrentTeam()
{
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(DB_MN_CURRENT_TEAM);

    std::vector<uint32_t> indexes{};
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint32_t> key;
        if (pcursor->GetKey(key) && key.first == DB_MN_CURRENT_TEAM) {
            CKeyID mnId;
            if (pcursor->GetValue(mnId)) {
                indexes.push_back(key.second);
            } else {
                return error("MNDB::EraseCurrentTeam() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }

    for (uint32_t i = 0; i < indexes.size(); ++i) {
        db->Erase(make_pair(DB_MN_CURRENT_TEAM, indexes[i]));
    }

    return true;
}

void CMasternodesViewDB::WriteFoundationsDebt(CAmount const foundationsDebt)
{
    db->Write(DB_MN_FOUNDERS_DEBT, foundationsDebt);
}

bool CMasternodesViewDB::LoadFoundationsDebt()
{
    foundationsDebt = -1;
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(DB_MN_FOUNDERS_DEBT);

    if (pcursor->Valid()) {
        char key;
        if (pcursor->GetKey(key)) {
            if (!pcursor->GetValue(foundationsDebt) || foundationsDebt < 0)
                return error("MNDB::LoadFoundationsDebt() : unable to read value");
        } else {
            foundationsDebt = 0;
        }
    }
    return true;
}

//void CMasternodesViewDB::WriteDeadIndex(int height, uint256 const & txid, char type)
//{
//    BatchWrite(make_pair(make_pair(DB_PRUNEDEAD, static_cast<int32_t>(height)), txid), type);
//}

//void CMasternodesViewDB::EraseDeadIndex(int height, uint256 const & txid)
//{
//    BatchErase(make_pair(make_pair(DB_PRUNEDEAD, static_cast<int32_t>(height)), txid));
//}

void CMasternodesViewDB::WriteUndo(int height, CMnTxsUndo const & undo)
{
    BatchWrite(make_pair(DB_MASTERNODESUNDO, static_cast<int32_t>(height)), undo);
}

void CMasternodesViewDB::EraseUndo(int height)
{
    BatchErase(make_pair(DB_MASTERNODESUNDO, static_cast<int32_t>(height)));
}

/*
 * Loads all data from DB, creates indexes
 */
bool CMasternodesViewDB::Load()
{
    Clear();

    bool result = true;
    result = result && ReadHeight(lastHeight);

    result = result && LoadTable(DB_MASTERNODES, allNodes, [this] (uint256 nodeId, CMasternode & node) {
        nodesByOwner.insert(std::make_pair(node.ownerAuthAddress, nodeId));
        nodesByOperator.insert(std::make_pair(node.operatorAuthAddress, nodeId));
    });
    result = result && LoadTable(DB_MASTERNODESUNDO, blocksUndo);
    result = result && LoadCurrentTeam(currentTeam);
    result = result && LoadTable(DB_MN_CRIMINALS, criminals);
    result = result && LoadFoundationsDebt();

    if (result)
        LogPrintf("MN: db loaded: last height: %d; masternodes: %d; common undo: %d\n", lastHeight, allNodes.size(), blocksUndo.size());
    else {
        LogPrintf("MN: fail to load database!\n");
    }
    return result;
}

bool CMasternodesViewDB::Flush()
{
    batch.reset();

    /// @todo optimize with new diff model of view
    int nMasternodes{0};
    for (auto && it = allNodes.begin(); it != allNodes.end(); ) {
        if (it->second == CMasternode()) {
            EraseMasternode(it->first);
            it = allNodes.erase(it);
        }
        else {
            WriteMasternode(it->first, it->second);
            ++nMasternodes;
            ++it;
        }
    }

    int nUndo{0};
    for (auto && it = blocksUndo.begin(); it != blocksUndo.end(); )
    {
        if (it->second.size() == 0) {
            EraseUndo(it->first);
            it = blocksUndo.erase(it);
        }
        else {
            WriteUndo(it->first, it->second);
            ++nUndo;
            ++it;
        }
    }

    for (auto && it = criminals.begin(); it != criminals.end(); )
    {
        if (it->second == CDoubleSignFact()) {
            EraseCriminal(it->first);
            it = criminals.erase(it);
        }
        else {
            WriteCriminal(it->first, it->second);
            ++it;
        }
    }
    WriteHeight(lastHeight);
    EraseCurrentTeam();
    WriteCurrentTeam(currentTeam);
    WriteFoundationsDebt(foundationsDebt);

    CommitBatch();
    LogPrintf("MN: db saved: last height: %d; masternodes: %d; common undo: %d\n", lastHeight, nMasternodes, nUndo);

    return true;
}
