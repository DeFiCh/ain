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

int GetMnActivationDelay()
{
    return Params().GetConsensus().mn.activationDelay;
}

int GetMnResignDelay()
{
    return Params().GetConsensus().mn.resignDelay;
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
}

CMasternode::CMasternode()
    : mintedBlocks(0)
    , ownerAuthAddress()
    , ownerType(0)
    , operatorAuthAddress()
    , operatorType(0)
    , creationHeight(0)
    , resignHeight(-1)
    , banHeight(-1)
    , resignTx()
    , banTx()
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
    banHeight = -1;

    resignTx = {};
    banTx = {};
    mintedBlocks = 0;
}

CMasternode::State CMasternode::GetState() const
{
    return GetState(::ChainActive().Height());
}

CMasternode::State CMasternode::GetState(int h) const
{
    assert (banHeight == -1 || resignHeight == -1); // mutually exclusive!: ban XOR resign

    if (resignHeight == -1 && banHeight == -1) { // enabled or pre-enabled
        // Special case for genesis block
        if (creationHeight == 0 || h >= creationHeight + GetMnActivationDelay()) {
            return State::ENABLED;
        }
        return State::PRE_ENABLED;
    }
    if (resignHeight != -1) { // pre-resigned or resigned
        if (h < resignHeight + GetMnResignDelay()) {
            return State::PRE_RESIGNED;
        }
        return State::RESIGNED;
    }
    if (banHeight != -1) { // pre-banned or banned
        if (h < banHeight + GetMnResignDelay()) {
            return State::PRE_BANNED;
        }
        return State::BANNED;
    }
}

bool CMasternode::IsActive() const
{
    return IsActive(::ChainActive().Height());
}

bool CMasternode::IsActive(int h) const
{
    State state = GetState(h);
    return state == ENABLED || state == PRE_RESIGNED || state == PRE_BANNED;
}

std::string CMasternode::GetHumanReadableState(State state)
{
    switch (state) {
        case PRE_ENABLED:
            return "PRE_ENABLED";
        case ENABLED:
            return "ENABLED";
        case PRE_RESIGNED:
            return "PRE_RESIGNED";
        case RESIGNED:
            return "RESIGNED";
        case PRE_BANNED:
            return "PRE_BANNED";
        case BANNED:
            return "BANNED";
        default:
            return "UNKNOWN";
    }
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
            a.banHeight == b.banHeight &&
            a.resignTx == b.resignTx &&
            a.banTx == b.banTx
            );
}

bool operator!=(CMasternode const & a, CMasternode const & b)
{
    return !(a == b);
}

bool operator==(CDoubleSignFact const & a, CDoubleSignFact const & b)
{
    return (a.blockHeader.GetHash() == b.blockHeader.GetHash() &&
            a.conflictBlockHeader.GetHash() == b.conflictBlockHeader.GetHash() &&
            a.wastedTxId == b.wastedTxId
    );
}

bool operator!=(CDoubleSignFact const & a, CDoubleSignFact const & b)
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
    return !nodePtr || (nodePtr->GetState(height) == CMasternode::RESIGNED) || (nodePtr->GetState(height) == CMasternode::BANNED);
}

/*
 * Check that given node is involved in anchor's subsystem for a given height (or smth like that)
 */
bool CMasternodesView::IsAnchorInvolved(const uint256 & nodeId, int height) const
{
    /// @todo to be implemented
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

    blocksUndo[node.creationHeight][txn] = std::make_pair(nodeId, MasternodesTxType::CreateMasternode);

    return true;
}

bool CMasternodesView::OnMasternodeResign(uint256 const & nodeId, uint256 const & txid, int height, int txn)
{
    // auth already checked!
    auto const node = ExistMasternode(nodeId);
    if (!node)
    {
        return false;
    }
    auto state = node->GetState(height);
    if ((state != CMasternode::PRE_ENABLED && state != CMasternode::ENABLED) || IsAnchorInvolved(nodeId, height)) // if already spoiled by resign or ban, or need for anchor
    {
        return false;
    }

    allNodes[nodeId] = *node; // !! cause may be cached!
    allNodes[nodeId].resignTx = txid;
    allNodes[nodeId].resignHeight = height;

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

/// Call it only for "clear" and "full" (not cached) view
void CMasternodesView::PruneOlder(int height)
{
    return; /// @todo temporary off
//    /// @todo add foolproof (for heights, teams and collateral)
//    if (height < 0)
//    {
//        return;
//    }

//    // erase dead nodes
//    for (auto && it = allNodes.begin(); it != allNodes.end(); )
//    {
//        CMasternode const & node = it->second;
//        /// @todo adjust heights (prune delay method?)
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
}

void CMasternodesView::SetTeam(CTeam newTeam)
{
    currentTeam = std::move(newTeam);
}

const std::set<CKeyID> &CMasternodesView::GetCurrentTeam()
{
    if (!currentTeam.size())
        return Params().GetGenesisTeam();
    return currentTeam;
}

CMasternodesView::CTeam CMasternodesView::CalcNextTeam(uint256 stakeModifier)
{
    int anchoringTeamSize = Params().GetConsensus().mn.anchoringTeamSize;

    std::map<arith_uint256, CKeyID, std::less<arith_uint256>> priorityMN;
    for (auto && it = allNodes.begin(); it != allNodes.end(); ++it) {
        CMasternode const & node = it->second;

        if(!node.IsActive())
            continue;

        CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
        ss << it->first << stakeModifier;
        priorityMN.insert(std::make_pair(UintToArith256(Hash(ss.begin(), ss.end())), node.operatorAuthAddress));
    }

    CMasternodesView::CTeam newTeam;
    auto && it = priorityMN.begin();
    for (int i = 0; i < anchoringTeamSize && it != priorityMN.end(); ++i, ++it) {
        newTeam.insert(it->second);
    }

    return newTeam;
}

bool CMasternodesView::CheckDoubleSign(CBlockHeader const & oneHeader, CBlockHeader const & twoHeader)
{
    CKeyID firstKey, secondKey;
    if (!oneHeader.ExtractMinterKey(firstKey)) {
        // TODO: SS may be throw exception
        return false;
    }
    auto itFirstMN = ExistMasternode(AuthIndex::ByOperator, firstKey);
    if (!itFirstMN) {
        // TODO: SS may be throw exception
        return false;
    }
    if (!twoHeader.ExtractMinterKey(secondKey)) {
        // TODO: SS may be throw exception
        return false;
    }
    auto itSecondMN = ExistMasternode(AuthIndex::ByOperator, firstKey);
    if (!itSecondMN) {
        // TODO: SS may be throw exception
        return false;
    }

    uint64_t maxHeight = oneHeader.height > twoHeader.height? oneHeader.height : twoHeader.height;
    uint64_t minHeight = oneHeader.height > twoHeader.height? twoHeader.height : oneHeader.height;

    if ((maxHeight - minHeight) <= DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL &&
        itFirstMN == itSecondMN &&
        oneHeader.mintedBlocks == twoHeader.mintedBlocks &&
        oneHeader.GetHash() != twoHeader.GetHash()
        ) {
        return false;
    }
    return true;
}

void CMasternodesView::MarkMasternodeAsCriminals(uint256 const & id, CBlockHeader const & blockHeader, CBlockHeader const & conflictBlockHeader)
{
    criminals.emplace(std::make_pair(id, CDoubleSignFact{blockHeader, conflictBlockHeader, uint256{}}));
}

boost::optional<CDoubleSignFact> CMasternodesView::FindCriminalProofForMasternode(uint256 const & id)
{
    auto it = criminals.find(id);
    if (it != criminals.end()) {
        return {it->second};
    }
    return {};
}

void CMasternodesView::MarkMasternodeAsWastedCriminal(uint256 const &mnId, uint256 const &txId)
{
    if (criminals[mnId].wastedTxId != uint256{} && txId != uint256{}) { // skip repeated transactions
        return ;
    }
    auto it = criminals.find(mnId);
    if (it != criminals.end()) {
        criminals[mnId].wastedTxId = txId;
    }
}

void CMasternodesView::RemoveMasternodeFromCriminals(uint256 const &criminalID)
{
    auto it = criminals.find(criminalID);
    if (it != criminals.end()) {
        criminals.erase(it);
    }
}

void CMasternodesView::BanCriminal(const uint256 txid, std::vector<unsigned char> & metadata, int height)
{
    std::pair<CBlockHeader, CBlockHeader> criminal;
    uint256 mnid;
    uint32_t index;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> criminal.first >> criminal.second >> mnid;

    if (!CheckDoubleSign(criminal.first, criminal.second)) {
        if (!FindCriminalProofForMasternode(mnid)) {
            MarkMasternodeAsCriminals(mnid, criminal.first, criminal.second);
        }

        auto const node = ExistMasternode(mnid);
        if (node) {
            auto state = node->GetState(height);
            if (state == CMasternode::PRE_ENABLED || state == CMasternode::ENABLED) {
                allNodes[mnid] = *node; // !! cause may be cached!
                allNodes[mnid].banHeight = height;
                allNodes[mnid].banTx = txid;
            }
        }

        MarkMasternodeAsWastedCriminal(mnid, txid);
    }
}

void CMasternodesView::UnbanCriminal(const uint256 txid, std::vector<unsigned char> & metadata)
{
    std::pair<CBlockHeader, CBlockHeader> criminal;
    uint256 mnid;
    uint32_t index;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> criminal.first >> criminal.second >> mnid;

    auto it = criminals.find(mnid);

    assert(it != criminals.end());

    if (criminals[mnid].wastedTxId != uint256{} && criminals[mnid].wastedTxId != txid) {
        return ; // skip reverting repeated transactions
    }

    if (!CheckDoubleSign(criminal.first, criminal.second)) {
        auto const node = ExistMasternode(mnid);
        if (node) {
            allNodes[mnid] = *node; // !! cause may be cached!
            allNodes[mnid].banHeight = -1;
            allNodes[mnid].banTx = {};
        }

        MarkMasternodeAsWastedCriminal(mnid, uint256{});
    }
}

bool CMasternodesView::ExtractCriminalProofFromTx(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (!tx.IsCoinBase() || tx.vout.size() == 0) {
        return false;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
        (opcode > OP_PUSHDATA1 &&
         opcode != OP_PUSHDATA2 &&
         opcode != OP_PUSHDATA4) ||
        metadata.size() < DfCriminalTxMarker.size() + 1 ||
        memcmp(&metadata[0], &DfCriminalTxMarker[0], DfCriminalTxMarker.size()) != 0) {
        return false;
    }
    metadata.erase(metadata.begin(), metadata.begin() + DfCriminalTxMarker.size());
    return true;
}

bool CMasternodesView::ExtractAnchorRewardFromTx(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (tx.vout.size() != 2) {
        return false;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
        (opcode > OP_PUSHDATA1 &&
         opcode != OP_PUSHDATA2 &&
         opcode != OP_PUSHDATA4) ||
        metadata.size() < DfAnchorFinalizeTxMarker.size() + 1 ||
        memcmp(&metadata[0], &DfAnchorFinalizeTxMarker[0], DfAnchorFinalizeTxMarker.size()) != 0) {
        return false;
    }
    metadata.erase(metadata.begin(), metadata.begin() + DfAnchorFinalizeTxMarker.size());
    return true;
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

    for (auto const & pair : cache->criminals) {
        criminals[pair.first] = pair.second; // possible empty (if deleted)
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
    criminals.clear();

    blocksUndo.clear();
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
            metadata.size() < DfTxMarker.size() + 1 ||     // i don't know how much exactly, but at least MnTxSignature + type prefix
            memcmp(&metadata[0], &DfTxMarker[0], DfTxMarker.size()) != 0)
    {
        return MasternodesTxType::None;
    }
    auto const & it = MasternodesTxTypeToCode.find(metadata[DfTxMarker.size()]);
    if (it == MasternodesTxTypeToCode.end())
    {
        return MasternodesTxType::None;
    }
    metadata.erase(metadata.begin(), metadata.begin() + DfTxMarker.size() + 1);
    return it->second;
}
