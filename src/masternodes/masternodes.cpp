// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>
#include <masternodes/anchors.h>
#include <masternodes/criminals.h>
#include <masternodes/mn_checks.h>

#include <chainparams.h>
#include <net_processing.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <functional>

/// @attention make sure that it does not overlap with those in tokens.cpp !!!
// Prefixes for the 'custom chainstate database' (customsc/)
const unsigned char DB_MASTERNODES = 'M';     // main masternodes table
const unsigned char DB_MN_OPERATORS = 'o';    // masternodes' operators index
const unsigned char DB_MN_OWNERS = 'w';       // masternodes' owners index
const unsigned char DB_MN_HEIGHT = 'H';       // single record with last processed chain height
const unsigned char DB_MN_ANCHOR_REWARD = 'r';
const unsigned char DB_MN_CURRENT_TEAM = 't';
const unsigned char DB_MN_FOUNDERS_DEBT = 'd';

const unsigned char CMasternodesView::ID      ::prefix = DB_MASTERNODES;
const unsigned char CMasternodesView::Operator::prefix = DB_MN_OPERATORS;
const unsigned char CMasternodesView::Owner   ::prefix = DB_MN_OWNERS;
const unsigned char CAnchorRewardsView::BtcTx ::prefix = DB_MN_ANCHOR_REWARD;

std::unique_ptr<CCustomCSView> pcustomcsview;
std::unique_ptr<CStorageLevelDB> pcustomcsDB;

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

CAmount GetMnCreationFee(int)
{
    return Params().GetConsensus().mn.creationFee;
}

CAmount GetTokenCollateralAmount()
{
    return Params().GetConsensus().token.collateralAmount;
}

CAmount GetTokenCreationFee(int)
{
    return Params().GetConsensus().token.creationFee;
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
    return State::UNKNOWN;
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
            a.conflictBlockHeader.GetHash() == b.conflictBlockHeader.GetHash()
    );
}

bool operator!=(CDoubleSignFact const & a, CDoubleSignFact const & b)
{
    return !(a == b);
}

/*
 * Check that given node is involved in anchor's subsystem for a given height (or smth like that)
 */
//bool IsAnchorInvolved(const uint256 & nodeId, int height) const
//{
//    /// @todo to be implemented
//    return false;
//}




/*
 *  CMasternodesView
 */
boost::optional<CMasternode> CMasternodesView::GetMasternode(const uint256 & id) const
{
    return ReadBy<ID, CMasternode>(id);
}

boost::optional<uint256> CMasternodesView::GetMasternodeIdByOperator(const CKeyID & id) const
{
    return ReadBy<Operator, uint256>(id);
}

boost::optional<uint256> CMasternodesView::GetMasternodeIdByOwner(const CKeyID & id) const
{
    return ReadBy<Owner, uint256>(id);
}

void CMasternodesView::ForEachMasternode(std::function<bool (const uint256 &, CMasternode &)> callback, uint256 const & start)
{
    ForEach<ID, uint256, CMasternode>([&callback] (uint256 const & txid, CMasternode & node) {
        return callback(txid, node);
    }, start);
}

void CMasternodesView::IncrementMintedBy(const CKeyID & minter)
{
    auto nodeId = GetMasternodeIdByOperator(minter);
    assert(nodeId);
    auto node = GetMasternode(*nodeId);
    assert(node);
    ++node->mintedBlocks;
    WriteBy<ID>(*nodeId, *node);
}

void CMasternodesView::DecrementMintedBy(const CKeyID & minter)
{
    auto nodeId = GetMasternodeIdByOperator(minter);
    assert(nodeId);
    auto node = GetMasternode(*nodeId);
    assert(node);
    --node->mintedBlocks;
    WriteBy<ID>(*nodeId, *node);
}

bool CMasternodesView::BanCriminal(const uint256 txid, std::vector<unsigned char> & metadata, int height)
{
    std::pair<CBlockHeader, CBlockHeader> criminal;
    uint256 nodeId;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> criminal.first >> criminal.second >> nodeId; // mnid is totally unnecessary!

    CKeyID minter;
    if (IsDoubleSigned(criminal.first, criminal.second, minter)) {
        auto node = GetMasternode(nodeId);
        if (node && node->operatorAuthAddress == minter && node->banTx.IsNull()) {
            node->banTx = txid;
            node->banHeight = height;
            WriteBy<ID>(nodeId, *node);

            return true;
        }
    }
    return false;
}

bool CMasternodesView::UnbanCriminal(const uint256 txid, std::vector<unsigned char> & metadata)
{
    std::pair<CBlockHeader, CBlockHeader> criminal;
    uint256 nodeId;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> criminal.first >> criminal.second >> nodeId; // mnid is totally unnecessary!

    // there is no need to check doublesigning or smth, we just rolling back previously approved (or ignored) banTx!
    auto node = GetMasternode(nodeId);
    if (node && node->banTx == txid) {
        node->banTx = {};
        node->banHeight = -1;
        WriteBy<ID>(nodeId, *node);
        return true;
    }
    return false;
}

boost::optional<std::pair<CKeyID, uint256> > CMasternodesView::AmIOperator() const
{
    CTxDestination dest = DecodeDestination(gArgs.GetArg("-masternode_operator", ""));
    CKeyID const authAddress = dest.which() == 1 ? CKeyID(*boost::get<PKHash>(&dest)) : (dest.which() == 4 ? CKeyID(*boost::get<WitnessV0KeyHash>(&dest)) : CKeyID());
    if (!authAddress.IsNull()) {
        auto nodeId = GetMasternodeIdByOperator(authAddress);
        if (nodeId)
            return { std::make_pair(authAddress, *nodeId) };
    }
    return {};
}

boost::optional<std::pair<CKeyID, uint256> > CMasternodesView::AmIOwner() const
{
    CTxDestination dest = DecodeDestination(gArgs.GetArg("-masternode_owner", ""));
    CKeyID const authAddress = dest.which() == 1 ? CKeyID(*boost::get<PKHash>(&dest)) : (dest.which() == 4 ? CKeyID(*boost::get<WitnessV0KeyHash>(&dest)) : CKeyID());
    if (!authAddress.IsNull()) {
        auto nodeId = GetMasternodeIdByOwner(authAddress);
        if (nodeId)
            return { std::make_pair(authAddress, *nodeId) };
    }
    return {};
}

Res CMasternodesView::CreateMasternode(const uint256 & nodeId, const CMasternode & node)
{
    // Check auth addresses and that there in no MN with such owner or operator
    if ((node.operatorType != 1 && node.operatorType != 4) || (node.ownerType != 1 && node.ownerType != 4) ||
        node.ownerAuthAddress.IsNull() || node.operatorAuthAddress.IsNull() ||
        GetMasternode(nodeId) ||
        GetMasternodeIdByOwner(node.ownerAuthAddress)    || GetMasternodeIdByOperator(node.ownerAuthAddress) ||
        GetMasternodeIdByOwner(node.operatorAuthAddress) || GetMasternodeIdByOperator(node.operatorAuthAddress)
        ) {
        return Res::Err("bad owner and|or operator address (should be P2PKH or P2WPKH only) or node with those addresses exists");
    }

    WriteBy<ID>(nodeId, node);
    WriteBy<Owner>(node.ownerAuthAddress, nodeId);
    WriteBy<Operator>(node.operatorAuthAddress, nodeId);

    return Res::Ok();
}

Res CMasternodesView::ResignMasternode(const uint256 & nodeId, const uint256 & txid, int height)
{
    // auth already checked!
    auto node = GetMasternode(nodeId);
    if (!node) {
        return Res::Err("node %s does not exists", nodeId.ToString());
    }
    auto state = node->GetState(height);
    if ((state != CMasternode::PRE_ENABLED && state != CMasternode::ENABLED) /*|| IsAnchorInvolved(nodeId, height)*/) { // if already spoiled by resign or ban, or need for anchor
        return Res::Err("node %s state is not 'PRE_ENABLED' or 'ENABLED'", nodeId.ToString());
    }

    node->resignTx =  txid;
    node->resignHeight = height;
    WriteBy<ID>(nodeId, *node);

    return Res::Ok();
}

//void CMasternodesView::UnCreateMasternode(const uint256 & nodeId)
//{
//    auto node = GetMasternode(nodeId);
//    if (node) {
//        EraseBy<ID>(nodeId);
//        EraseBy<Operator>(node->operatorAuthAddress);
//        EraseBy<Owner>(node->ownerAuthAddress);
//    }
//}

//void CMasternodesView::UnResignMasternode(const uint256 & nodeId, const uint256 & resignTx)
//{
//    auto node = GetMasternode(nodeId);
//    if (node && node->resignTx == resignTx) {
//        node->resignHeight = -1;
//        node->resignTx = {};
//        WriteBy<ID>(nodeId, *node);
//    }
//}

/*
 *  CLastHeightView
 */
int CLastHeightView::GetLastHeight() const
{
    int result;
    if (Read(DB_MN_HEIGHT, result))
        return result;
    return 0;
}

void CLastHeightView::SetLastHeight(int height)
{
    Write(DB_MN_HEIGHT, height);
}

/*
 *  CFoundationsDebtView
 */
CAmount CFoundationsDebtView::GetFoundationsDebt() const
{
    CAmount debt(0);
    if(Read(DB_MN_FOUNDERS_DEBT, debt))
        assert(debt >= 0);
    return debt;
}

void CFoundationsDebtView::SetFoundationsDebt(CAmount debt)
{
    assert(debt >= 0);
    Write(DB_MN_FOUNDERS_DEBT, debt);
}


/*
 *  CTeamView
 */
void CTeamView::SetTeam(const CTeamView::CTeam & newTeam)
{
    Write(DB_MN_CURRENT_TEAM, newTeam);
}

CTeamView::CTeam CTeamView::GetCurrentTeam() const
{
    CTeam team;
    if (Read(DB_MN_CURRENT_TEAM, team) && team.size() > 0)
        return team;

    return Params().GetGenesisTeam();
}

/*
 *  CAnchorRewardsView
 */
boost::optional<CAnchorRewardsView::RewardTxHash> CAnchorRewardsView::GetRewardForAnchor(const CAnchorRewardsView::AnchorTxHash & btcTxHash) const
{
    return ReadBy<BtcTx, RewardTxHash>(btcTxHash);
}

void CAnchorRewardsView::AddRewardForAnchor(const CAnchorRewardsView::AnchorTxHash & btcTxHash, const CAnchorRewardsView::RewardTxHash & rewardTxHash)
{
    WriteBy<BtcTx>(btcTxHash, rewardTxHash);
}

void CAnchorRewardsView::RemoveRewardForAnchor(const CAnchorRewardsView::AnchorTxHash & btcTxHash)
{
    EraseBy<BtcTx>(btcTxHash);
}

void CAnchorRewardsView::ForEachAnchorReward(std::function<bool (const CAnchorRewardsView::AnchorTxHash &, CAnchorRewardsView::RewardTxHash &)> callback)
{
    ForEach<BtcTx, AnchorTxHash, RewardTxHash>(callback);
}

/*
 *  CCustomCSView
 */
CTeamView::CTeam CCustomCSView::CalcNextTeam(const uint256 & stakeModifier)
{
    if (stakeModifier == uint256())
        return Params().GetGenesisTeam();

    int anchoringTeamSize = Params().GetConsensus().mn.anchoringTeamSize;

    std::map<arith_uint256, CKeyID, std::less<arith_uint256>> priorityMN;
    ForEachMasternode([&stakeModifier, &priorityMN] (uint256 const & id, CMasternode & node) {
        if(!node.IsActive())
            return true;

        CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
        ss << id << stakeModifier;
        priorityMN.insert(std::make_pair(UintToArith256(Hash(ss.begin(), ss.end())), node.operatorAuthAddress));
        return true;
    });

    CTeam newTeam;
    auto && it = priorityMN.begin();
    for (int i = 0; i < anchoringTeamSize && it != priorityMN.end(); ++i, ++it) {
        newTeam.insert(it->second);
    }
    return newTeam;
}

/// @todo newbase move to networking?
void CCustomCSView::CreateAndRelayConfirmMessageIfNeed(const CAnchor & anchor, const uint256 & btcTxHash)
{
    /// @todo refactor to use AmISignerNow()
    auto myIDs = AmIOperator();
    if (!myIDs || !GetMasternode(myIDs->second)->IsActive())
        return ;
    CKeyID const & operatorAuthAddress = myIDs->first;
    CTeam const currentTeam = GetCurrentTeam();
    if (currentTeam.find(operatorAuthAddress) == currentTeam.end()) {
        LogPrintf("AnchorConfirms::CreateAndRelayConfirmMessageIfNeed: Warning! I am not in a team %s\n", operatorAuthAddress.ToString());
        return ;
    }

    std::vector<std::shared_ptr<CWallet>> wallets = GetWallets();
    CKey masternodeKey{};
    for (auto const wallet : wallets) {
        if (wallet->GetKey(operatorAuthAddress, masternodeKey)) {
            break;
        }
        masternodeKey = CKey{};
    }

    if (!masternodeKey.IsValid()) {
        LogPrintf("AnchorConfirms::CreateAndRelayConfirmMessageIfNeed: Warning! Masternodes is't valid %s\n", operatorAuthAddress.ToString());
        // return error("%s: Can't read masternode operator private key", __func__);
        return ;
    }

    auto prev = panchors->GetAnchorByTx(anchor.previousAnchor);
    auto confirmMessage = CAnchorConfirmMessage::CreateSigned(anchor, prev? prev->anchor.height : 0, btcTxHash, masternodeKey);
    if (panchorAwaitingConfirms->Add(confirmMessage)) {
        LogPrintf("AnchorConfirms::CreateAndRelayConfirmMessageIfNeed: Create message %s\n", confirmMessage.GetHash().GetHex());
        RelayAnchorConfirm(confirmMessage.GetHash(), *g_connman);
    }
    else {
        LogPrintf("AnchorConfirms::CreateAndRelayConfirmMessageIfNeed: Warning! not need relay %s because message (or vote!) already exist\n", confirmMessage.GetHash().GetHex());
    }

}

void CCustomCSView::OnUndoTx(uint256 const & txid, uint32_t height)
{
    const auto undo = this->GetUndo(UndoKey{height, txid});
    if (!undo) {
        return; // not custom tx, or no changes done
    }
    CUndo::Revert(this->GetRaw(), *undo); // revert the changes of this tx
    this->DelUndo(UndoKey{height, txid}); // erase undo data, it served its purpose
}

bool CCustomCSView::CanSpend(const uint256 & txId, int height) const
{
    auto node = GetMasternode(txId);
    // check if it was mn collateral and mn was resigned or banned
    if (node) {
        auto state = node->GetState(height);
        return state == CMasternode::RESIGNED || state == CMasternode::BANNED;
    }
    // check if it was token collateral and token already destroyed
    /// @todo token check for total supply/limit when implemented
    auto pair = GetTokenByCreationTx(txId);
    return !pair || pair->second.destructionTx != uint256{} || pair->second.IsPoolShare();
}

