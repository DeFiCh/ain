// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>

#include <chainparams.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <validation.h>

#include <algorithm>
#include <functional>

std::unique_ptr<CMasternodesView> pmasternodesview;

static const std::map<char, MasternodesTxType> MasternodesTxTypeToCode =
{
    {'C', MasternodesTxType::CreateMasternode },
    {'R', MasternodesTxType::ResignMasternode }
};

//extern CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams); // in main.cpp

int GetMnActivationDelay()
{
    return Params().GetConsensus().mn.activationDelay;
}

int GetMnResignDelay()
{
    return Params().GetConsensus().mn.resignDelay;
}

int GetMnCollateralUnlockDelay()
{
    return Params().GetConsensus().mn.collateralUnlockDelay;
}

int GetMnHistoryFrame()
{
    return Params().GetConsensus().mn.historyFrame;
}


CAmount GetMnCollateralAmount()
{
    return Params().GetConsensus().mn.collateralAmount;
}

CAmount GetMnCreationFee(int height)
{
    return Params().GetConsensus().mn.creationFee;

//    CAmount blockSubsidy = GetBlockSubsidy(height, Params().GetConsensus());
}

CMasternode::CMasternode()
    : mintedBlocks(0)
    , ownerAuthAddress()
    , ownerType(0)
    , operatorAuthAddress()
    , operatorType(0)
    , creationHeight(0)
    , resignHeight(-1)
    , resignTx()
{
}

CMasternode::CMasternode(const CTransaction & tx, int heightIn, const std::vector<unsigned char> & metadata)
{
    FromTx(tx, heightIn, metadata);
}

void CMasternode::FromTx(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata)
{
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> operatorType;
    ss >> operatorAuthAddress;

    ownerType = 0;
    ownerAuthAddress = {};

    CTxDestination dest;
    if (ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
        if (dest.which() == 1) {
            ownerType = 1;
            ownerAuthAddress = CKeyID(*boost::get<PKHash>(&dest));
        }
        else if (dest.which() == 4) {
            ownerType = 4;
            ownerAuthAddress = CKeyID(*boost::get<WitnessV0KeyHash>(&dest));
        }
    }

    creationHeight = heightIn;
    resignHeight = -1;

    resignTx = {};
    mintedBlocks = 0;
}

bool CMasternode::IsActive() const
{
    return IsActive(::ChainActive().Height());
}

bool CMasternode::IsActive(int h) const
{
    // Special case for genesis block
    if (creationHeight == 0)
        return resignHeight == -1 || resignHeight + GetMnResignDelay() > h;

    return  creationHeight + GetMnActivationDelay() <= h && (resignHeight == -1 || resignHeight + GetMnResignDelay() > h);
}

std::string CMasternode::GetHumanReadableStatus() const
{
    return GetHumanReadableStatus(::ChainActive().Height());
}

std::string CMasternode::GetHumanReadableStatus(int h) const
{
    std::string status;
    if (IsActive(h))
    {
        return "active";
    }
    status += ((creationHeight == 0 || creationHeight + GetMnActivationDelay() <= h)) ? "activated" : "created";
    if (resignHeight != -1 && resignHeight + GetMnResignDelay() > h)
    {
        status += ", resigned";
    }
    return status;
}

bool operator==(CMasternode const & a, CMasternode const & b)
{
    return (a.mintedBlocks == b.mintedBlocks &&
            a.ownerType == b.ownerType &&
            a.ownerAuthAddress == b.ownerAuthAddress &&
            a.operatorType == b.operatorType &&
            a.operatorAuthAddress == b.operatorAuthAddress &&
            a.creationHeight == b.creationHeight &&
            a.resignHeight == b.resignHeight &&
            a.resignTx == b.resignTx
            );
}

bool operator!=(CMasternode const & a, CMasternode const & b)
{
    return !(a == b);
}


/*
 * Searching MN index 'nodesByOwner' or 'nodesByOperator' for given 'auth' key
 */
boost::optional<CMasternodesByAuth::const_iterator>
CMasternodesView::ExistMasternode(CMasternodesView::AuthIndex where, CKeyID const & auth) const
{
    CMasternodesByAuth const & index = (where == AuthIndex::ByOwner) ? nodesByOwner : nodesByOperator;
    auto it = index.find(auth);
    if (it == index.end() || it->second.IsNull())
    {
        return {};
    }
    return {it};
}

/*
 * Searching all masternodes for given 'id'
 */
CMasternode const * CMasternodesView::ExistMasternode(uint256 const & id) const
{
    CMasternodes::const_iterator it = allNodes.find(id);
    return it != allNodes.end() && it->second != CMasternode() ? &it->second : nullptr;
}

/*
 * Check that given tx is not a masternode id or masternode was resigned enough time in the past
 */
bool CMasternodesView::CanSpend(const uint256 & nodeId, int height) const
{
    auto nodePtr = ExistMasternode(nodeId);
    // if not exist or (resigned && delay passed)
    return !nodePtr || (nodePtr->resignHeight != -1 && nodePtr->resignHeight + GetMnCollateralUnlockDelay() <= height);
}

/*
 * Check that given node is involved in anchor's subsystem for a given height (or smth like that)
 */
bool CMasternodesView::IsAnchorInvolved(const uint256 & nodeId, int height) const
{
    /// @todo @max to be implemented
    return false;
}

CMasternodesView::CMnBlocksUndo::mapped_type const & CMasternodesView::GetBlockUndo(CMnBlocksUndo::key_type key) const
{
    static CMnBlocksUndo::mapped_type const Empty = {};
    CMnBlocksUndo::const_iterator it = blocksUndo.find(key);
    return it != blocksUndo.end() ? it->second : Empty;
}

bool CMasternodesView::OnMasternodeCreate(uint256 const & nodeId, CMasternode const & node, int txn)
{
    // Check auth addresses and that there in no MN with such owner or operator
    if ((node.operatorType != 1 && node.operatorType != 4 && node.ownerType != 1 && node.ownerType != 4) ||
        node.ownerAuthAddress.IsNull() || node.operatorAuthAddress.IsNull() ||
        ExistMasternode(nodeId) ||
        ExistMasternode(AuthIndex::ByOwner, node.ownerAuthAddress) ||
        ExistMasternode(AuthIndex::ByOperator, node.operatorAuthAddress)
        )
    {
        return false;
    }

    allNodes[nodeId] = node;
    nodesByOwner[node.ownerAuthAddress] = nodeId;
    nodesByOperator[node.operatorAuthAddress] = nodeId;

//    blocksUndo[std::make_pair(node.height, txn)] = std::make_pair(nodeId, MasternodesTxType::CreateMasternode);
    blocksUndo[node.creationHeight][txn] = std::make_pair(nodeId, MasternodesTxType::CreateMasternode);

    return true;
}

bool CMasternodesView::OnMasternodeResign(uint256 const & nodeId, uint256 const & txid, int height, int txn)
{
    // auth already checked!
    auto const node = ExistMasternode(nodeId);
    if (!node || node->resignHeight != -1 || !node->resignTx.IsNull() || IsAnchorInvolved(nodeId, height))
    {
        return false;
    }

    allNodes[nodeId] = *node; // !! cause may be cached!
    allNodes[nodeId].resignTx = txid;
    allNodes[nodeId].resignHeight = height;

//    blocksUndo[std::make_pair(height, txn)] = std::make_pair(nodeId, MasternodesTxType::ResignMasternode);
    blocksUndo[height][txn] = std::make_pair(nodeId, MasternodesTxType::ResignMasternode);

    return true;
}


CMasternodesViewCache CMasternodesView::OnUndoBlock(int height)
{
    assert(height == lastHeight);

    CMasternodesViewCache backup(this); // dummy, not used as "true base"

    auto undoTxs = GetBlockUndo(height);

    if (!undoTxs.empty())
    {
        for (auto undoData = undoTxs.rbegin(); undoData != undoTxs.rend(); ++undoData)
        {
            auto const id = undoData->second.first;
            auto const txType = undoData->second.second;
            CMasternode const & node = *ExistMasternode(id);

            switch (txType)
            {
                case MasternodesTxType::CreateMasternode:
                {
                    backup.nodesByOwner[node.ownerAuthAddress] = id;
                    backup.nodesByOperator[node.operatorAuthAddress] = id;
                    backup.allNodes[id] = node;

                    nodesByOwner[node.ownerAuthAddress] = {};
                    nodesByOperator[node.operatorAuthAddress] = {};
                    allNodes[id] = {};
                }
                break;
                case MasternodesTxType::ResignMasternode:
                {
                    backup.allNodes[id] = node; // nodesByOwner && nodesByOperator stay untouched

                    allNodes[id] = node;    // !! cause may be cached!
                    allNodes[id].resignHeight = -1;
                    allNodes[id].resignTx = {};
                }
                break;
                default:
                    break;
            }
        }
        backup.blocksUndo[height] = undoTxs; // not `blocksUndo[height]`!! cause may be cached!
        blocksUndo[height] = {};
    }
    backup.lastHeight = lastHeight;

    /// @attention do NOT do THIS!!! it will be done separately by outer SetLastHeight()!
//    --lastHeight;

    return backup; // it is new value diff for height+1
}

//bool CMasternodesView::OnConnectBlock(int height, const CKeyID & minter)
//{
//    lastHeight = height;

//    auto nodePtr = ExistMasternode(minter);
//    assert(nodePtr);

//    if (nodePtr)
//    {
//        allNodes[minter] = *nodePtr; // !! cause may be cached!
//        ++allNodes[minter].mintedBlocks;

//        return true;
//    }
//    return false;
//}


//bool CMasternodesView::IsTeamMember(int height, CKeyID const & operatorAuth) const
//{
//    CTeam const & team = ReadDposTeam(height);
//    for (auto const & member : team)
//    {
//        if (member.second.operatorAuth == operatorAuth)
//            return true;
//    }
//    return false;
//}

//CTeam const & CMasternodesView::ReadDposTeam(int height) const
//{
//    static CTeam const Empty{};

//    auto const it = teams.find(height);
//    if (it != teams.end())
//        return it->second;

//    // Nothing to complain here, cause teams not exists before dPoS activation!
////    LogPrintf("MN ERROR: Fail to get team at height %d! May be already pruned!\n", height);
//    return Empty;
//}

//void CMasternodesView::WriteDposTeam(int height, const CTeam & team)
//{
//    assert(height >= 0);
//    teams[height] = team;
//}


/// Call it only for "clear" and "full" (not cached) view
void CMasternodesView::PruneOlder(int height)
{
    return; /// @todo @max temporary off
//    /// @todo @max add foolproof (for heights, teams and collateral)
//    if (height < 0)
//    {
//        return;
//    }

//    // erase dead nodes
//    for (auto && it = allNodes.begin(); it != allNodes.end(); )
//    {
//        CMasternode const & node = it->second;
//        /// @todo @maxb adjust heights (prune delay method?)
//        if(node.resignHeight != -1 && node.resignHeight + < height)
//        {
//            nodesByOwner.erase(node.ownerAuthAddress);
//            nodesByOperator.erase(node.operatorAuthAddress);
//            it = allNodes.erase(it);
//        }
//        else ++it;
//    }

//    // erase undo info
//    blocksUndo.erase(blocksUndo.begin(), blocksUndo.lower_bound(height));
//    // erase old teams info
////    teams.erase(teams.begin(), teams.lower_bound(height));
}

boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmI(AuthIndex where) const
{
    std::string addressBase58 = (where == AuthIndex::ByOperator) ? gArgs.GetArg("-masternode_operator", "") : gArgs.GetArg("-masternode_owner", "");
    if (addressBase58 != "")
    {
        CTxDestination dest = DecodeDestination(addressBase58);
        auto const authAddress = dest.which() == 1 ? CKeyID(*boost::get<PKHash>(&dest)) : (dest.which() == 4 ? CKeyID(*boost::get<WitnessV0KeyHash>(&dest)) : CKeyID());
        if (!authAddress.IsNull())
        {
            auto const it = ExistMasternode(where, authAddress);
            if (it)
            {
                uint256 const & id = (*it)->second;
                return { CMasternodeIDs {id, ExistMasternode(id)->operatorAuthAddress, ExistMasternode(id)->ownerAuthAddress} };
            }
        }
    }
    return {};
}

boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmIOperator() const
{
    return AmI(AuthIndex::ByOperator);
}

boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmIOwner() const
{
    return AmI(AuthIndex::ByOwner);
}

//boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmIActiveOperator() const
//{
//    auto result = AmI(AuthIndex::ByOperator);
//    if (result && ExistMasternode(result->id)->IsActive())
//    {
//        return result;
//    }
//    return {};
//}

//boost::optional<CMasternodesView::CMasternodeIDs> CMasternodesView::AmIActiveOwner() const
//{
//    auto result = AmI(AuthIndex::ByOwner);
//    if (result && ExistMasternode(result->id)->IsActive())
//    {
//        return result;
//    }
//    return {};
//}

void CMasternodesView::ApplyCache(const CMasternodesView * cache)
{
    lastHeight = cache->lastHeight;

    for (auto const & pair : cache->allNodes) {
        allNodes[pair.first] = pair.second; // possible empty (if deleted)
    }

    for (auto const & pair : cache->nodesByOwner) {
        nodesByOwner[pair.first] = pair.second; // possible empty (if deleted)
    }

    for (auto const & pair : cache->nodesByOperator) {
        nodesByOperator[pair.first] = pair.second; // possible empty (if deleted)
    }

    for (auto const & pair : cache->blocksUndo) {
        blocksUndo[pair.first] = pair.second; // possible empty (if deleted)
    }
}

void CMasternodesView::Clear()
{
    lastHeight = 0;
    allNodes.clear();
    nodesByOwner.clear();
    nodesByOperator.clear();

    blocksUndo.clear();
//    teams.clear();
}

CMasternodesViewHistory & CMasternodesViewHistory::GetState(int targetHeight)
{
    int const topHeight = base->GetLastHeight();
    assert(targetHeight >= topHeight - GetMnHistoryFrame() && targetHeight <= topHeight);

    if (lastHeight > targetHeight)
    {
        // go backward (undo)

        for (; lastHeight > targetHeight; )
        {
            auto it = historyDiff.find(lastHeight);
            if (it != historyDiff.end())
            {
                historyDiff.erase(it);
            }
            historyDiff.emplace(std::make_pair(lastHeight, OnUndoBlock(lastHeight)));

            CBlockIndex* pindex = ::ChainActive()[lastHeight];
            assert(pindex);
            DecrementMintedBy(pindex->minter);

            --lastHeight;
        }
    }
    else if (lastHeight < targetHeight)
    {
        // go forward (redo)
        for (; lastHeight < targetHeight; )
        {
            ++lastHeight;

            // redo states should be cached!
            assert(historyDiff.find(lastHeight) != historyDiff.end());
            ApplyCache(&historyDiff.at(lastHeight));

            CBlockIndex* pindex = ::ChainActive()[lastHeight];
            assert(pindex);
            IncrementMintedBy(pindex->minter);
        }

    }
    return *this;
}

/*
 * Checks if given tx is probably one of 'MasternodeTx', returns tx type and serialized metadata in 'data'
*/
MasternodesTxType GuessMasternodeTxType(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (tx.vout.size() == 0)
    {
        return MasternodesTxType::None;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN)
    {
        return MasternodesTxType::None;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4) ||
            metadata.size() < MnTxMarker.size() + 1 ||     // i don't know how much exactly, but at least MnTxSignature + type prefix
            memcmp(&metadata[0], &MnTxMarker[0], MnTxMarker.size()) != 0)
    {
        return MasternodesTxType::None;
    }
    auto const & it = MasternodesTxTypeToCode.find(metadata[MnTxMarker.size()]);
    if (it == MasternodesTxTypeToCode.end())
    {
        return MasternodesTxType::None;
    }
    metadata.erase(metadata.begin(), metadata.begin() + MnTxMarker.size() + 1);
    return it->second;
}
