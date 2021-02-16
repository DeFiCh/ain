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
#include <wallet/walletutil.h>

#include <algorithm>
#include <functional>
#include <unordered_map>

#include <boost/signals2/signal.hpp>

/// @attention make sure that it does not overlap with those in tokens.cpp !!!
// Prefixes for the 'custom chainstate database' (customsc/)
const unsigned char DB_MASTERNODES = 'M';     // main masternodes table
const unsigned char DB_MN_OPERATORS = 'o';    // masternodes' operators index
const unsigned char DB_MN_OWNERS = 'w';       // masternodes' owners index
const unsigned char DB_MN_HEIGHT = 'H';       // single record with last processed chain height
const unsigned char DB_MN_ANCHOR_REWARD = 'r';
const unsigned char DB_MN_ANCHOR_CONFIRM = 'x';
const unsigned char DB_MN_CURRENT_TEAM = 't';
const unsigned char DB_MN_FOUNDERS_DEBT = 'd';
const unsigned char DB_MN_AUTH_TEAM = 'v';
const unsigned char DB_MN_CONFIRM_TEAM = 'V';

const unsigned char CMasternodesView::ID      ::prefix = DB_MASTERNODES;
const unsigned char CMasternodesView::Operator::prefix = DB_MN_OPERATORS;
const unsigned char CMasternodesView::Owner   ::prefix = DB_MN_OWNERS;
const unsigned char CAnchorRewardsView::BtcTx ::prefix = DB_MN_ANCHOR_REWARD;
const unsigned char CAnchorConfirmsView::BtcTx::prefix = DB_MN_ANCHOR_CONFIRM;
const unsigned char CTeamView::AuthTeam       ::prefix = DB_MN_AUTH_TEAM;
const unsigned char CTeamView::ConfirmTeam    ::prefix = DB_MN_CONFIRM_TEAM;

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

CAmount GetMnCollateralAmount(int height)
{
    auto& consensus = Params().GetConsensus();
    if (height < consensus.DakotaHeight) {
        return consensus.mn.collateralAmount;
    } else {
        return consensus.mn.collateralAmountDakota;
    }
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

void CMasternodesView::ForEachMasternode(std::function<bool (const uint256 &, CLazySerialize<CMasternode>)> callback, uint256 const & start)
{
    ForEach<ID, uint256, CMasternode>(callback, start);
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
    auto const operators = gArgs.GetArgs("-masternode_operator");
    for(auto const & key : operators) {
        CTxDestination const dest = DecodeDestination(key);
        CKeyID const authAddress = dest.which() == 1 ? CKeyID(*boost::get<PKHash>(&dest)) :
                                   dest.which() == 4 ? CKeyID(*boost::get<WitnessV0KeyHash>(&dest)) : CKeyID();
        if (!authAddress.IsNull()) {
            if (auto nodeId = GetMasternodeIdByOperator(authAddress)) {
                return std::make_pair(authAddress, *nodeId);
            }
        }
    }
    return {};
}

std::set<std::pair<CKeyID, uint256>> CMasternodesView::GetOperatorsMulti() const
{
    auto const operators = gArgs.GetArgs("-masternode_operator");
    std::set<std::pair<CKeyID, uint256>> operatorPairs;
    for(auto const & key : operators) {
        CTxDestination const dest = DecodeDestination(key);
        CKeyID const authAddress = dest.which() == 1 ? CKeyID(*boost::get<PKHash>(&dest)) :
                                   dest.which() == 4 ? CKeyID(*boost::get<WitnessV0KeyHash>(&dest)) : CKeyID();
        if (!authAddress.IsNull()) {
            if (auto nodeId = GetMasternodeIdByOperator(authAddress)) {
                operatorPairs.insert(std::make_pair(authAddress, *nodeId));
            }
        }
    }

    return operatorPairs;
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

void CTeamView::SetAnchorTeams(const CTeam& authTeam, const CTeam& confirmTeam, const int height)
{
    // Called after fork height
    if (height < Params().GetConsensus().DakotaHeight) {
        LogPrint(BCLog::ANCHORING, "%s: Called below fork. Fork: %d Arg height: %d\n",
                 __func__, Params().GetConsensus().DakotaHeight, height);
        return;
    }

    // Called every on team change intercal from fork height
    if (height % Params().GetConsensus().mn.anchoringTeamChange != 0) {
        LogPrint(BCLog::ANCHORING, "%s: Not called on interval of %d, arg height %d\n",
                 __func__, Params().GetConsensus().mn.anchoringTeamChange, height);
        return;
    }

    if (!authTeam.empty()) {
        WriteBy<AuthTeam>(height, authTeam);
    }

    if (!confirmTeam.empty()) {
        WriteBy<ConfirmTeam>(height, confirmTeam);
    }
}

boost::optional<CTeamView::CTeam> CTeamView::GetAuthTeam(int height) const
{
    height -= height % Params().GetConsensus().mn.anchoringTeamChange;

    return ReadBy<AuthTeam, CTeam>(height);
}

boost::optional<CTeamView::CTeam> CTeamView::GetConfirmTeam(int height) const
{
    height -= height % Params().GetConsensus().mn.anchoringTeamChange;

    return ReadBy<ConfirmTeam, CTeam>(height);
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

void CAnchorRewardsView::ForEachAnchorReward(std::function<bool (const CAnchorRewardsView::AnchorTxHash &, CLazySerialize<CAnchorRewardsView::RewardTxHash>)> callback)
{
    ForEach<BtcTx, AnchorTxHash, RewardTxHash>(callback);
}

/*
 *  CAnchorConfirmsView
 */

void CAnchorConfirmsView::AddAnchorConfirmData(const CAnchorConfirmDataPlus& data)
{
    WriteBy<BtcTx>(data.btcTxHash, data);
}

void CAnchorConfirmsView::EraseAnchorConfirmData(uint256 btcTxHash)
{
    EraseBy<BtcTx>(btcTxHash);
}

void CAnchorConfirmsView::ForEachAnchorConfirmData(std::function<bool(const AnchorTxHash &, CLazySerialize<CAnchorConfirmDataPlus>)> callback)
{
    ForEach<BtcTx, AnchorTxHash, CAnchorConfirmDataPlus>(callback);
}

std::vector<CAnchorConfirmDataPlus> CAnchorConfirmsView::GetAnchorConfirmData()
{
    std::vector<CAnchorConfirmDataPlus> confirms;

    ForEachAnchorConfirmData([&confirms](const CAnchorConfirmsView::AnchorTxHash &, CLazySerialize<CAnchorConfirmDataPlus> data) {
        confirms.push_back(data);
        return true;
    });

    return confirms;
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
    ForEachMasternode([&stakeModifier, &priorityMN] (uint256 const & id, CMasternode node) {
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

enum AnchorTeams {
    AuthTeam,
    ConfirmTeam
};

void CCustomCSView::CalcAnchoringTeams(const uint256 & stakeModifier, const CBlockIndex *pindexNew)
{
    std::set<uint256> masternodeIDs;
    int blockSample{7 * 2880}; // One week
    const CBlockIndex* pindex = pindexNew;

    // Get active MNs from last week's worth of blocks
    for (int i{0}; pindex && i < blockSample; pindex = pindex->pprev, ++i) {
        CKeyID minter;
        if (pindex->GetBlockHeader().ExtractMinterKey(minter)) {
            LOCK(cs_main);
            auto id = pcustomcsview->GetMasternodeIdByOperator(minter);
            if (id) {
                masternodeIDs.insert(*id);
            }
        }
    }

    std::map<arith_uint256, CKeyID, std::less<arith_uint256>> authMN;
    std::map<arith_uint256, CKeyID, std::less<arith_uint256>> confirmMN;
    ForEachMasternode([&masternodeIDs, &stakeModifier, &authMN, &confirmMN] (uint256 const & id, CMasternode node) {
        if(!node.IsActive())
            return true;

        // Not in our list of MNs from last week, skip.
        if (masternodeIDs.find(id) == masternodeIDs.end()) {
            return true;
        }

        CDataStream authStream{SER_GETHASH, PROTOCOL_VERSION};
        authStream << id << stakeModifier << static_cast<int>(AnchorTeams::AuthTeam);
        authMN.insert(std::make_pair(UintToArith256(Hash(authStream.begin(), authStream.end())), node.operatorAuthAddress));

        CDataStream confirmStream{SER_GETHASH, PROTOCOL_VERSION};
        confirmStream << id << stakeModifier << static_cast<int>(AnchorTeams::ConfirmTeam);
        confirmMN.insert(std::make_pair(UintToArith256(Hash(confirmStream.begin(), confirmStream.end())), node.operatorAuthAddress));

        return true;
    });

    int anchoringTeamSize = Params().GetConsensus().mn.anchoringTeamSize;

    CTeam authTeam;
    auto && it = authMN.begin();
    for (int i = 0; i < anchoringTeamSize && it != authMN.end(); ++i, ++it) {
        authTeam.insert(it->second);
    }

    CTeam confirmTeam;
    it = confirmMN.begin();
    for (int i = 0; i < anchoringTeamSize && it != confirmMN.end(); ++i, ++it) {
        confirmTeam.insert(it->second);
    }

    {
        LOCK(cs_main);
        pcustomcsview->SetAnchorTeams(authTeam, confirmTeam, pindexNew->height);
    }

    // Debug logging
    LogPrint(BCLog::ANCHORING, "MNs found: %d Team sizes: %d\n", masternodeIDs.size(), authTeam.size());

    for (auto& item : authTeam) {
        LogPrint(BCLog::ANCHORING, "Auth team operator addresses: %s\n", item.ToString());
    }

    for (auto& item : confirmTeam) {
        LogPrint(BCLog::ANCHORING, "Confirm team operator addresses: %s\n", item.ToString());
    }
}

/// @todo newbase move to networking?
void CCustomCSView::CreateAndRelayConfirmMessageIfNeed(const CAnchorIndex::AnchorRec *anchor, const uint256 & btcTxHash, const CKey& masternodeKey)
{
    auto prev = panchors->GetAnchorByTx(anchor->anchor.previousAnchor);
    auto confirmMessage = CAnchorConfirmMessage::CreateSigned(anchor->anchor, prev ? prev->anchor.height : 0, btcTxHash, masternodeKey, anchor->btcHeight);

    if (panchorAwaitingConfirms->Add(*confirmMessage)) {
        LogPrint(BCLog::ANCHORING, "%s: Create message %s\n", __func__, confirmMessage->GetHash().GetHex());
        RelayAnchorConfirm(confirmMessage->GetHash(), *g_connman);
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

enum accountHistoryType {
    None = 0,
    MineOnly,
    Full,
};

CAccountsHistoryStorage::CAccountsHistoryStorage(CCustomCSView & storage, uint32_t height, uint32_t txn, const uint256& txid, uint8_t type)
    : CStorageView(new CFlushableStorageKV(storage.GetRaw())), height(height), txn(txn), txid(txid), type(type)
{
    acindex = gArgs.GetBoolArg("-acindex-mineonly", DEFAULT_ACINDEX_MINEONLY) ? accountHistoryType::MineOnly :
              gArgs.GetBoolArg("-acindex", DEFAULT_ACINDEX) ? accountHistoryType::Full : accountHistoryType::None;
}

Res CAccountsHistoryStorage::AddBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::AddBalance(owner, amount);
    if (acindex && res.ok && amount.nValue != 0) {
        diffs[owner][amount.nTokenId] += amount.nValue;
    }
    return res;
}

Res CAccountsHistoryStorage::SubBalance(CScript const & owner, CTokenAmount amount)
{
    auto res = CCustomCSView::SubBalance(owner, amount);
    if (acindex && res.ok && amount.nValue != 0) {
        diffs[owner][amount.nTokenId] -= amount.nValue;
    }
    return res;
}

bool CAccountsHistoryStorage::Flush()
{
    if (acindex) {
        auto wallets = GetWallets();
        for (const auto& diff : diffs) {
            bool isMine = false;
            for (auto wallet : wallets) {
                if ((isMine = IsMineCached(*wallet, diff.first) & ISMINE_ALL)) {
                    SetMineAccountHistory({diff.first, height, txn}, {txid, type, diff.second});
                    break;
                }
            }
            if (!isMine && acindex == accountHistoryType::Full) {
                SetAllAccountHistory({diff.first, height, txn}, {txid, type, diff.second});
            }
        }
    }
    return CCustomCSView::Flush();
}

CRewardsHistoryStorage::CRewardsHistoryStorage(CCustomCSView & storage, uint32_t height)
    : CStorageView(new CFlushableStorageKV(storage.GetRaw())), height(height)
{
    acindex = gArgs.GetBoolArg("-acindex-mineonly", DEFAULT_ACINDEX_MINEONLY) ? accountHistoryType::MineOnly :
              gArgs.GetBoolArg("-acindex", DEFAULT_ACINDEX) ? accountHistoryType::Full : accountHistoryType::None;
}

Res CRewardsHistoryStorage::AddBalance(CScript const & owner, DCT_ID poolID, uint8_t type, CTokenAmount amount)
{
    auto res = CCustomCSView::AddBalance(owner, amount);
    if (acindex && res.ok && amount.nValue > 0) {
        auto& map = diffs[std::make_pair(owner, type)];
        map[poolID][amount.nTokenId] += amount.nValue;
    }
    return res;
}

bool CRewardsHistoryStorage::Flush()
{
    if (acindex) {
        auto wallets = GetWallets();
        for (const auto& diff : diffs) {
            bool isMine = false;
            const auto& pair = diff.first;
            for (auto wallet : wallets) {
                if ((isMine = IsMineCached(*wallet, pair.first) & ISMINE_ALL)) {
                    SetMineRewardHistory({pair.first, height, pair.second}, diff.second);
                    break;
                }
            }
            if (!isMine && acindex == accountHistoryType::Full) {
                SetAllRewardHistory({pair.first, height, pair.second}, diff.second);
            }
        }
    }
    return CCustomCSView::Flush();
}

struct CCacheInfo
{
    std::map<CScript, isminetype> mineData;
    std::array<boost::signals2::scoped_connection, 2> connects;
};

isminetype IsMineCached(CWallet & wallet, CScript const & script)
{
    static std::unordered_map<std::string, CCacheInfo> cache;
    const auto& walletFilePath = wallet.GetLocation().GetFilePath();
    auto it = cache.find(walletFilePath);
    if (it == cache.end()) {
        it = cache.emplace(walletFilePath, CCacheInfo{{}, {
            wallet.NotifyOwnerChanged.connect([walletFilePath](CScript const & owner) {
                WITH_LOCK(cs_main, cache[walletFilePath].mineData.erase(owner));
            }),
            wallet.NotifyUnload.connect([walletFilePath]() {
                WITH_LOCK(cs_main, cache.erase(walletFilePath));
            }),
        }}).first;
    }
    auto mIt = it->second.mineData.find(script);
    if (mIt == it->second.mineData.end()) {
        mIt = it->second.mineData.emplace(script, ::IsMine(wallet, script)).first;
    }
    return mIt->second;
}

std::map<CKeyID, CKey> AmISignerNow(CAnchorData::CTeam const & team)
{
    AssertLockHeld(cs_main);

    std::map<CKeyID, CKey> operatorDetails;
    auto const mnIds = pcustomcsview->GetOperatorsMulti();
    for (const auto& mnId : mnIds)
    {
        if (pcustomcsview->GetMasternode(mnId.second)->IsActive() && team.find(mnId.first) != team.end())
        {
            CKey masternodeKey;
            std::vector<std::shared_ptr<CWallet>> wallets = GetWallets();
            for (auto const & wallet : wallets) {
                if (wallet->GetKey(mnId.first, masternodeKey)) {
                    break;
                }
                masternodeKey = CKey{};
            }
            if (masternodeKey.IsValid()) {
                operatorDetails[mnId.first] = masternodeKey;
            }
        }
    }

    return operatorDetails;
}
