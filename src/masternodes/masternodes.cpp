#include "masternodes.h"

#include "chainparams.h"
#include "key_io.h"
//#include "mn_txdb.h"
#include "primitives/block.h"
#include "primitives/transaction.h"

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
    static const int MN_ACTIVATION_DELAY = 1500;
    if (Params().NetworkIDString() == "regtest")
    {
        return 10;
    }
    return MN_ACTIVATION_DELAY ;
}

int GetMnCollateralUnlockDelay()
{
    /// @todo @maxb adjust the delay
    static const int MN_COLLATERAL_DELAY = 100;
    if (Params().NetworkIDString() == "regtest")
    {
        return 10;
    }
    return MN_COLLATERAL_DELAY ;
}


CAmount GetMnCollateralAmount()
{
    static const CAmount MN_COLLATERAL_AMOUNT = 1000000 * COIN;

    if (Params().NetworkIDString() == "regtest")
    {
        return 10 * COIN;
    }
    return MN_COLLATERAL_AMOUNT;
}

CAmount GetMnCreationFee(int height)
{
    return 42 * COIN; // dummy

//    CAmount blockSubsidy = GetBlockSubsidy(height, Params().GetConsensus());

}

CMasternode::CMasternode()
    : ownerAuthAddress()
    , ownerType(0)
    , operatorAuthAddress()
    , operatorType(0)
    , height(0)
    , resignHeight(-1)
    , resignTx()
{
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

    height = heightIn;
    resignHeight = -1;

    resignTx = {};
}

std::string CMasternode::GetHumanReadableStatus() const
{
    std::string status;
    if (IsActive())
    {
        return "active";
    }
    status += (height + GetMnActivationDelay() <= ::ChainActive().Height()) ? "activated" : "created";
    if (resignHeight != -1 || resignTx != uint256())
    {
        status += ", resigned";
    }
    return status;
}

bool operator==(CMasternode const & a, CMasternode const & b)
{
    return (a.ownerType == b.ownerType &&
            a.ownerAuthAddress == b.ownerAuthAddress &&
            a.operatorType == b.operatorType &&
            a.operatorAuthAddress == b.operatorAuthAddress &&
            a.height == b.height &&
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
    if (it == index.end())
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
    return it != allNodes.end() ? &it->second : nullptr;
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

std::pair<uint256, MasternodesTxType> CMasternodesView::GetUndo(CTxUndo::key_type key) const
{
    static std::pair<uint256, MasternodesTxType> const Empty = {};
    CTxUndo::const_iterator it = txsUndo.find(key);
    return it != txsUndo.end() ? it->second : Empty;
}

bool CMasternodesView::OnMasternodeCreate(uint256 const & nodeId, CMasternode const & node)
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

    txsUndo[std::make_pair(node.height, nodeId)] = std::make_pair(nodeId, MasternodesTxType::CreateMasternode);

    return true;
}

bool CMasternodesView::OnMasternodeResign(uint256 const & nodeId, uint256 const & txid, int height)
{
    // auth already checked!
    auto const node = ExistMasternode(nodeId);
    if (!node || node->resignHeight != -1 || node->resignTx != uint256() || IsAnchorInvolved(nodeId, height))
    {
        return false;
    }

    allNodes[nodeId] = *node; // !! cause may be cached!
    allNodes[nodeId].resignTx = txid;
    allNodes[nodeId].resignHeight = height;

    txsUndo[std::make_pair(height, txid)] = std::make_pair(nodeId, MasternodesTxType::ResignMasternode);

    return true;
}


void CMasternodesView::OnUndo(int height, uint256 const & txid)
{
    auto const key = std::make_pair(height, txid);
    auto undoData = GetUndo(key);

    //  Undo rec = (key = [height, txid]; value = std::pair<uint256 affected_object_id, MasternodesTxType> )

    if (undoData.first != uint256()) {
        auto const id = undoData.first;
        auto const txType = undoData.second;
        CMasternode const & node = *ExistMasternode(id);

        switch (txType)
        {
            case MasternodesTxType::CreateMasternode:
            {
                nodesByOwner[node.ownerAuthAddress] = {};
                nodesByOperator[node.operatorAuthAddress] = {};
                allNodes[id] = {};
            }
            break;
            case MasternodesTxType::ResignMasternode:
            {
                allNodes[id] = node;    // !! cause may be cached!
                allNodes[id].resignHeight = -1;
                allNodes[id].resignTx = {};
            }
            break;
            default:
                break;
        }
        txsUndo[key] = {};
    }
}

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

    /// @todo @max add foolproof (for heights, teams and collateral)
    if (height < 0)
    {
        return;
    }

    // erase dead nodes
    for (auto && it = allNodes.begin(); it != allNodes.end(); )
    {
        CMasternode const & node = it->second;
        if(node.resignHeight != -1 && node.resignHeight < height)
        {
            nodesByOwner.erase(node.ownerAuthAddress);
            nodesByOperator.erase(node.operatorAuthAddress);
            it = allNodes.erase(it);
        }
        else ++it;
    }

    // erase undo info
    txsUndo.erase(txsUndo.begin(), txsUndo.lower_bound(std::make_pair(height, uint256())));
//    for (auto && it = txsUndo.begin(); it != txsUndo.end(); )
//    {
//        if(it->first.first < height)
//        {
//            it = txsUndo.erase(it);
//        }
//        else ++it;
//    }
    // erase old teams info
//    teams.erase(teams.begin(), teams.lower_bound(height));
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

void CMasternodesView::Clear()
{
    lastHeight = 0;
    allNodes.clear();
    nodesByOwner.clear();
    nodesByOperator.clear();

    txsUndo.clear();
//    teams.clear();
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
