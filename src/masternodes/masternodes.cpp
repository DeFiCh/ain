// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>
#include <masternodes/anchors.h>
#include <masternodes/mn_checks.h>

#include <chainparams.h>
#include <consensus/merkle.h>
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

std::unique_ptr<CCustomCSView> pcustomcsview;

int GetMnActivationDelay(int height)
{
    // Restore previous activation delay on testnet after FC
    if (height < Params().GetConsensus().EunosHeight ||
    (Params().NetworkIDString() == CBaseChainParams::TESTNET && height >= Params().GetConsensus().FortCanningHeight)) {
        return Params().GetConsensus().mn.activationDelay;
    }

    return Params().GetConsensus().mn.newActivationDelay;
}

int GetMnResignDelay(int height)
{
    // Restore previous activation delay on testnet after FC
    if (height < Params().GetConsensus().EunosHeight ||
    (Params().NetworkIDString() == CBaseChainParams::TESTNET && height >= Params().GetConsensus().FortCanningHeight)) {
        return Params().GetConsensus().mn.resignDelay;
    }

    return Params().GetConsensus().mn.newResignDelay;
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
    , rewardAddress()
    , rewardAddressType(0)
    , creationHeight(0)
    , resignHeight(-1)
    , version(-1)
    , resignTx()
    , banTx()
{
}

CMasternode::State CMasternode::GetState(int height) const
{
    int EunosPayaHeight = Params().GetConsensus().EunosPayaHeight;

    if (height < creationHeight) {
        return State::UNKNOWN;
    }

    if (resignHeight == -1 || height < resignHeight) { // enabled or pre-enabled
        // Special case for genesis block
        int activationDelay = height < EunosPayaHeight ? GetMnActivationDelay(height) : GetMnActivationDelay(creationHeight);
        if (creationHeight == 0 || height >= creationHeight + activationDelay) {
            return State::ENABLED;
        }
        return State::PRE_ENABLED;
    }

    if (resignHeight != -1) { // pre-resigned or resigned
        int resignDelay = height < EunosPayaHeight ? GetMnResignDelay(height) : GetMnResignDelay(resignHeight);
        if (height < resignHeight + resignDelay) {
            return State::PRE_RESIGNED;
        }
        return State::RESIGNED;
    }
    return State::UNKNOWN;
}

bool CMasternode::IsActive(int height) const
{
    State state = GetState(height);
    if (height >= Params().GetConsensus().EunosPayaHeight) {
        return state == ENABLED;
    }
    return state == ENABLED || state == PRE_RESIGNED;
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
        default:
            return "UNKNOWN";
    }
}

std::string CMasternode::GetTimelockToString(TimeLock timelock)
{
  switch (timelock) {
    case FIVEYEAR : return "FIVEYEARTIMELOCK";
    case TENYEAR  : return "TENYEARTIMELOCK";
    default       : return "NONE";
  }
}

bool operator==(CMasternode const & a, CMasternode const & b)
{
    return (a.mintedBlocks == b.mintedBlocks &&
            a.ownerType == b.ownerType &&
            a.ownerAuthAddress == b.ownerAuthAddress &&
            a.operatorType == b.operatorType &&
            a.operatorAuthAddress == b.operatorAuthAddress &&
            a.rewardAddress == b.rewardAddress &&
            a.rewardAddressType == b.rewardAddressType &&
            a.creationHeight == b.creationHeight &&
            a.resignHeight == b.resignHeight &&
            a.version == b.version &&
            a.resignTx == b.resignTx &&
            a.banTx == b.banTx
            );
}

bool operator!=(CMasternode const & a, CMasternode const & b)
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
std::optional<CMasternode> CMasternodesView::GetMasternode(const uint256 & id) const
{
    return ReadBy<ID, CMasternode>(id);
}

std::optional<uint256> CMasternodesView::GetMasternodeIdByOperator(const CKeyID & id) const
{
    return ReadBy<Operator, uint256>(id);
}

std::optional<uint256> CMasternodesView::GetMasternodeIdByOwner(const CKeyID & id) const
{
    return ReadBy<Owner, uint256>(id);
}

void CMasternodesView::ForEachMasternode(std::function<bool (const uint256 &, CLazySerialize<CMasternode>)> callback, uint256 const & start)
{
    ForEach<ID, uint256, CMasternode>(callback, start);
}

void CMasternodesView::IncrementMintedBy(const uint256& nodeId)
{
    auto node = GetMasternode(nodeId);
    assert(node);
    ++node->mintedBlocks;
    WriteBy<ID>(nodeId, *node);
}

void CMasternodesView::DecrementMintedBy(const uint256& nodeId)
{
    auto node = GetMasternode(nodeId);
    assert(node);
    --node->mintedBlocks;
    WriteBy<ID>(nodeId, *node);
}

std::optional<std::pair<CKeyID, uint256> > CMasternodesView::AmIOperator() const
{
    auto const operators = gArgs.GetArgs("-masternode_operator");
    for(auto const & key : operators) {
        CTxDestination const dest = DecodeDestination(key);
        CKeyID const authAddress = dest.index() == PKHashType ? CKeyID(std::get<PKHash>(dest)) :
                                   dest.index() == WitV0KeyHashType ? CKeyID(std::get<WitnessV0KeyHash>(dest)) : CKeyID();
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
        CKeyID const authAddress = dest.index() == PKHashType ? CKeyID(std::get<PKHash>(dest)) :
                                   dest.index() == WitV0KeyHashType ? CKeyID(std::get<WitnessV0KeyHash>(dest)) : CKeyID();
        if (!authAddress.IsNull()) {
            if (auto nodeId = GetMasternodeIdByOperator(authAddress)) {
                operatorPairs.insert(std::make_pair(authAddress, *nodeId));
            }
        }
    }

    return operatorPairs;
}

std::optional<std::pair<CKeyID, uint256> > CMasternodesView::AmIOwner() const
{
    CTxDestination dest = DecodeDestination(gArgs.GetArg("-masternode_owner", ""));
    CKeyID const authAddress = dest.index() == PKHashType ? CKeyID(std::get<PKHash>(dest)) : (dest.index() == WitV0KeyHashType ? CKeyID(std::get<WitnessV0KeyHash>(dest)) : CKeyID());
    if (!authAddress.IsNull()) {
        auto nodeId = GetMasternodeIdByOwner(authAddress);
        if (nodeId)
            return { std::make_pair(authAddress, *nodeId) };
    }
    return {};
}

Res CMasternodesView::CreateMasternode(const uint256 & nodeId, const CMasternode & node, uint16_t timelock)
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

    if (timelock > 0) {
        WriteBy<Timelock>(nodeId, timelock);
    }

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
    if (height >= Params().GetConsensus().EunosPayaHeight) {
        if (state != CMasternode::ENABLED) {
            return Res::Err("node %s state is not 'ENABLED'", nodeId.ToString());
        }
    } else if ((state != CMasternode::PRE_ENABLED && state != CMasternode::ENABLED)) {
        return Res::Err("node %s state is not 'PRE_ENABLED' or 'ENABLED'", nodeId.ToString());
    }

    const auto timelock = GetTimelock(nodeId, *node, height);
    if (timelock) {
        return Res::Err("Trying to resign masternode before timelock expiration.");
    }

    node->resignTx =  txid;
    node->resignHeight = height;
    WriteBy<ID>(nodeId, *node);

    return Res::Ok();
}

Res CMasternodesView::SetForcedRewardAddress(uint256 const & nodeId, const char rewardAddressType, CKeyID const & rewardAddress, int height)
{
    // Temporarily disabled for 2.2
    return Res::Err("reward address change is disabled for Fort Canning");

    auto node = GetMasternode(nodeId);
    if (!node) {
        return Res::Err("masternode %s does not exists", nodeId.ToString());
    }
    auto state = node->GetState(height);
    if ((state != CMasternode::PRE_ENABLED && state != CMasternode::ENABLED)) {
        return Res::Err("masternode %s state is not 'PRE_ENABLED' or 'ENABLED'", nodeId.ToString());
    }

    // If old masternode update foor new serialisatioono
    if (node->version < CMasternode::VERSION0) {
        node->version = CMasternode::VERSION0;
    }

    // Set new reward address
    node->rewardAddressType = rewardAddressType;
    node->rewardAddress = rewardAddress;
    WriteBy<ID>(nodeId, *node);

    return Res::Ok();
}

Res CMasternodesView::RemForcedRewardAddress(uint256 const & nodeId, int height)
{
    // Temporarily disabled for 2.2
    return Res::Err("reward address change is disabled for Fort Canning");

    auto node = GetMasternode(nodeId);
    if (!node) {
        return Res::Err("masternode %s does not exists", nodeId.ToString());
    }
    auto state = node->GetState(height);
    if ((state != CMasternode::PRE_ENABLED && state != CMasternode::ENABLED)) {
        return Res::Err("masternode %s state is not 'PRE_ENABLED' or 'ENABLED'", nodeId.ToString());
    }

    node->rewardAddressType = 0;
    node->rewardAddress.SetNull();
    WriteBy<ID>(nodeId, *node);

    return Res::Ok();
}

Res CMasternodesView::UpdateMasternode(uint256 const & nodeId, char operatorType, const CKeyID& operatorAuthAddress, int height) {
    // Temporarily disabled for 2.2
    return Res::Err("updatemasternode is disabled for Fort Canning");

    // auth already checked!
    auto node = GetMasternode(nodeId);
    if (!node) {
        return Res::Err("node %s does not exists", nodeId.ToString());
    }

    const auto state = node->GetState(height);
    if (state != CMasternode::ENABLED) {
        return Res::Err("node %s state is not 'ENABLED'", nodeId.ToString());
    }

    if (operatorType == node->operatorType && operatorAuthAddress == node->operatorAuthAddress) {
        return Res::Err("The new operator is same as existing operator");
    }

    // Remove old record
    EraseBy<Operator>(node->operatorAuthAddress);

    node->operatorType = operatorType;
    node->operatorAuthAddress = operatorAuthAddress;

    // Overwrite and create new record
    WriteBy<ID>(nodeId, *node);
    WriteBy<Operator>(node->operatorAuthAddress, nodeId);

    return Res::Ok();
}

void CMasternodesView::SetMasternodeLastBlockTime(const CKeyID & minter, const uint32_t &blockHeight, const int64_t& time)
{
    auto nodeId = GetMasternodeIdByOperator(minter);
    assert(nodeId);

    WriteBy<Staker>(MNBlockTimeKey{*nodeId, blockHeight}, time);
}

std::optional<int64_t> CMasternodesView::GetMasternodeLastBlockTime(const CKeyID & minter, const uint32_t height)
{
    auto nodeId = GetMasternodeIdByOperator(minter);
    assert(nodeId);

    int64_t time{0};

    ForEachMinterNode([&](const MNBlockTimeKey &key, int64_t blockTime)
    {
        if (key.masternodeID == nodeId)
        {
            time = blockTime;
        }

        // Get first result only and exit
        return false;
    }, MNBlockTimeKey{*nodeId, height - 1});

    if (time)
    {
        return time;
    }

    return {};
}

void CMasternodesView::EraseMasternodeLastBlockTime(const uint256& nodeId, const uint32_t& blockHeight)
{
    EraseBy<Staker>(MNBlockTimeKey{nodeId, blockHeight});
}

void CMasternodesView::ForEachMinterNode(std::function<bool(MNBlockTimeKey const &, CLazySerialize<int64_t>)> callback, MNBlockTimeKey const & start)
{
    ForEach<Staker, MNBlockTimeKey, int64_t>(callback, start);
}

void CMasternodesView::SetSubNodesBlockTime(const CKeyID & minter, const uint32_t &blockHeight, const uint8_t id, const int64_t& time)
{
    auto nodeId = GetMasternodeIdByOperator(minter);
    assert(nodeId);

    WriteBy<SubNode>(SubNodeBlockTimeKey{*nodeId, id, blockHeight}, time);
}

std::vector<int64_t> CMasternodesView::GetSubNodesBlockTime(const CKeyID & minter, const uint32_t height)
{
    auto nodeId = GetMasternodeIdByOperator(minter);
    assert(nodeId);

    std::vector<int64_t> times(SUBNODE_COUNT, 0);

    for (uint8_t i{0}; i < SUBNODE_COUNT; ++i) {
        ForEachSubNode([&](const SubNodeBlockTimeKey &key, int64_t blockTime)
        {
            if (height >= Params().GetConsensus().FortCanningHeight) {
                if (key.masternodeID == nodeId && key.subnode == i) {
                    times[i] = blockTime;
                }
            } else if (key.masternodeID == nodeId) {
                times[i] = blockTime;
            }

            // Get first result only and exit
            return false;
        }, SubNodeBlockTimeKey{*nodeId, i, height - 1});
    }

    return times;
}

void CMasternodesView::ForEachSubNode(std::function<bool(SubNodeBlockTimeKey const &, CLazySerialize<int64_t>)> callback, SubNodeBlockTimeKey const & start)
{
    ForEach<SubNode, SubNodeBlockTimeKey, int64_t>(callback, start);
}

void CMasternodesView::EraseSubNodesLastBlockTime(const uint256& nodeId, const uint32_t& blockHeight)
{
    for (uint8_t i{0}; i < SUBNODE_COUNT; ++i) {
        EraseBy<SubNode>(SubNodeBlockTimeKey{nodeId, i, blockHeight});
    }
}

Res CMasternodesView::UnCreateMasternode(const uint256 & nodeId)
{
    auto node = GetMasternode(nodeId);
    if (node) {
        EraseBy<ID>(nodeId);
        EraseBy<Operator>(node->operatorAuthAddress);
        EraseBy<Owner>(node->ownerAuthAddress);
        return Res::Ok();
    }
    return Res::Err("No such masternode %s", nodeId.GetHex());
}

Res CMasternodesView::UnResignMasternode(const uint256 & nodeId, const uint256 & resignTx)
{
    auto node = GetMasternode(nodeId);
    if (node && node->resignTx == resignTx) {
        node->resignHeight = -1;
        node->resignTx = {};
        WriteBy<ID>(nodeId, *node);
        return Res::Ok();
    }
    return Res::Err("No such masternode %s, resignTx: %s", nodeId.GetHex(), resignTx.GetHex());
}

uint16_t CMasternodesView::GetTimelock(const uint256& nodeId, const CMasternode& node, const uint64_t height) const
{
    auto timelock = ReadBy<Timelock, uint16_t>(nodeId);
    if (timelock) {
        // Get last height
        auto lastHeight = height - 1;

        // Cannot expire below block count required to calculate average time
        if (lastHeight < Params().GetConsensus().mn.newResignDelay) {
            return *timelock;
        }

        LOCK(cs_main);
        // Get timelock expiration time. Timelock set in weeks, convert to seconds.
        const auto timelockExpire = ::ChainActive()[node.creationHeight]->nTime + (*timelock * 7 * 24 * 60 * 60);

        // Get average time of the last two times the activation delay worth of blocks
        uint64_t totalTime{0};
        for (; lastHeight + Params().GetConsensus().mn.newResignDelay >= height; --lastHeight) {
            totalTime += ::ChainActive()[lastHeight]->nTime;
        }
        const uint32_t averageTime = totalTime / Params().GetConsensus().mn.newResignDelay;

        // Below expiration return timelock
        if (averageTime < timelockExpire) {
            return *timelock;
        } else { // Expired. Return null.
            return 0;
        }
    }
    return 0;
}

std::vector<int64_t> CMasternodesView::GetBlockTimes(const CKeyID& keyID, const uint32_t blockHeight, const int32_t creationHeight, const uint16_t timelock)
{
    // Get last block time for non-subnode staking
    std::optional<int64_t> stakerBlockTime = GetMasternodeLastBlockTime(keyID, blockHeight);

    // Get times for sub nodes, defaults to {0, 0, 0, 0} for MNs created before EunosPayaHeight
    std::vector<int64_t> subNodesBlockTime = GetSubNodesBlockTime(keyID, blockHeight);

    // Set first entry to previous accrued multiplier.
    if (stakerBlockTime && !subNodesBlockTime[0]) {
        subNodesBlockTime[0] = *stakerBlockTime;
    }

    auto eunosPayaBlockTime = [&]() -> int64_t {
        LOCK(cs_main);
        if (auto block = ::ChainActive()[Params().GetConsensus().EunosPayaHeight]) {
            if (creationHeight < Params().GetConsensus().DakotaCrescentHeight && !stakerBlockTime && !subNodesBlockTime[0]) {
                if (auto dakotaBlock = ::ChainActive()[Params().GetConsensus().DakotaCrescentHeight]) {
                    subNodesBlockTime[0] = dakotaBlock->GetBlockTime();
                }
            }
            return block->GetBlockTime();
        }
        return 0;
    }();

    if (eunosPayaBlockTime > 0) {
        // If no values set for pre-fork MN use the fork time
        const uint8_t loops = timelock == CMasternode::TENYEAR ? 4 : timelock == CMasternode::FIVEYEAR ? 3 : 2;
        for (uint8_t i{0}; i < loops; ++i) {
            if (!subNodesBlockTime[i]) {
                subNodesBlockTime[i] = eunosPayaBlockTime;
            }
        }
    }

    return subNodesBlockTime;
}

/*
 *  CLastHeightView
 */
int CLastHeightView::GetLastHeight() const
{
    int result;
    if (Read(Height::prefix(), result))
        return result;
    return 0;
}

void CLastHeightView::SetLastHeight(int height)
{
    Write(Height::prefix(), height);
}

/*
 *  CFoundationsDebtView
 */
CAmount CFoundationsDebtView::GetFoundationsDebt() const
{
    CAmount debt(0);
    if(Read(Debt::prefix(), debt))
        assert(debt >= 0);
    return debt;
}

void CFoundationsDebtView::SetFoundationsDebt(CAmount debt)
{
    assert(debt >= 0);
    Write(Debt::prefix(), debt);
}


/*
 *  CTeamView
 */
void CTeamView::SetTeam(const CTeamView::CTeam & newTeam)
{
    Write(CurrentTeam::prefix(), newTeam);
}

CTeamView::CTeam CTeamView::GetCurrentTeam() const
{
    CTeam team;
    if (Read(CurrentTeam::prefix(), team) && team.size() > 0)
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

std::optional<CTeamView::CTeam> CTeamView::GetAuthTeam(int height) const
{
    height -= height % Params().GetConsensus().mn.anchoringTeamChange;

    return ReadBy<AuthTeam, CTeam>(height);
}

std::optional<CTeamView::CTeam> CTeamView::GetConfirmTeam(int height) const
{
    height -= height % Params().GetConsensus().mn.anchoringTeamChange;

    return ReadBy<ConfirmTeam, CTeam>(height);
}

/*
 *  CAnchorRewardsView
 */
std::optional<CAnchorRewardsView::RewardTxHash> CAnchorRewardsView::GetRewardForAnchor(const CAnchorRewardsView::AnchorTxHash & btcTxHash) const
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
int CCustomCSView::GetDbVersion() const
{
    int version;
    if (Read(DbVersion::prefix(), version))
        return version;
    return 0;
}

void CCustomCSView::SetDbVersion(int version)
{
    Write(DbVersion::prefix(), version);
}

CTeamView::CTeam CCustomCSView::CalcNextTeam(int height, const uint256 & stakeModifier)
{
    if (stakeModifier == uint256())
        return Params().GetGenesisTeam();

    int anchoringTeamSize = Params().GetConsensus().mn.anchoringTeamSize;

    std::map<arith_uint256, CKeyID, std::less<arith_uint256>> priorityMN;
    ForEachMasternode([&] (uint256 const & id, CMasternode node) {
        if(!node.IsActive(height))
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
    const int blockSample = 7 * Params().GetConsensus().blocksPerDay(); // One week

    {
        LOCK(cs_main);
        const CBlockIndex* pindex = pindexNew;
        for (int i{0}; pindex && i < blockSample; pindex = pindex->pprev, ++i) {
            if (auto id = GetMasternodeIdByOperator(pindex->minterKey())) {
                masternodeIDs.insert(*id);
            }
        }
    }

    std::map<arith_uint256, CKeyID, std::less<arith_uint256>> authMN;
    std::map<arith_uint256, CKeyID, std::less<arith_uint256>> confirmMN;
    ForEachMasternode([&] (uint256 const & id, CMasternode node) {
        if(!node.IsActive(pindexNew->nHeight))
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
        SetAnchorTeams(authTeam, confirmTeam, pindexNew->nHeight);
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

void CCustomCSView::AddUndo(CCustomCSView & cache, uint256 const & txid, uint32_t height)
{
    auto flushable = cache.GetStorage().GetFlushableStorage();
    assert(flushable);
    SetUndo({height, txid}, CUndo::Construct(GetStorage(), flushable->GetRaw()));
}

void CCustomCSView::OnUndoTx(uint256 const & txid, uint32_t height)
{
    const auto undo = GetUndo(UndoKey{height, txid});
    if (!undo) {
        return; // not custom tx, or no changes done
    }
    CUndo::Revert(GetStorage(), *undo); // revert the changes of this tx
    DelUndo(UndoKey{height, txid}); // erase undo data, it served its purpose
}

bool CCustomCSView::CanSpend(const uint256 & txId, int height) const
{
    auto node = GetMasternode(txId);
    // check if it was mn collateral and mn was resigned or banned
    if (node) {
        auto state = node->GetState(height);
        return state == CMasternode::RESIGNED;
    }
    // check if it was token collateral and token already destroyed
    /// @todo token check for total supply/limit when implemented
    auto pair = GetTokenByCreationTx(txId);
    return !pair || pair->second.destructionTx != uint256{} || pair->second.IsPoolShare();
}

bool CCustomCSView::CalculateOwnerRewards(CScript const & owner, uint32_t targetHeight)
{
    auto balanceHeight = GetBalancesHeight(owner);
    if (balanceHeight >= targetHeight) {
        return false;
    }
    ForEachPoolId([&] (DCT_ID const & poolId) {
        auto height = GetShare(poolId, owner);
        if (!height || *height >= targetHeight) {
            return true; // no share or target height is before a pool share' one
        }
        auto onLiquidity = [&]() -> CAmount {
            return GetBalance(owner, poolId).nValue;
        };
        auto beginHeight = std::max(*height, balanceHeight);
        CalculatePoolRewards(poolId, onLiquidity, beginHeight, targetHeight,
            [&](RewardType, CTokenAmount amount, uint32_t height) {
                auto res = AddBalance(owner, amount);
                if (!res) {
                    LogPrintf("Pool rewards: can't update balance of %s: %s, height %ld\n", owner.GetHex(), res.msg, targetHeight);
                }
            }
        );
        return true;
    });

    return UpdateBalancesHeight(owner, targetHeight);
}

void CCustomCSView::SetBackend(CCustomCSView & backend)
{
    // update backend
    CStorageView::SetBackend(backend);
}

double CCollateralLoans::calcRatio(uint64_t maxRatio) const
{
    return !totalLoans ? double(maxRatio) : double(totalCollaterals) / totalLoans;
}

uint32_t CCollateralLoans::ratio() const
{
    constexpr auto maxRatio = std::numeric_limits<uint32_t>::max();
    auto ratio = calcRatio(maxRatio) * 100;
    return ratio > maxRatio ? maxRatio : uint32_t(lround(ratio));
}

CAmount CCollateralLoans::precisionRatio() const
{
    constexpr auto maxRatio = std::numeric_limits<CAmount>::max();
    auto ratio = calcRatio(maxRatio);
    const auto precision = COIN * 100;
    return ratio > maxRatio / precision ? -COIN : CAmount(ratio * precision);
}

ResVal<CAmount> CCustomCSView::GetAmountInCurrency(CAmount amount, CTokenCurrencyPair priceFeedId, bool useNextPrice, bool requireLivePrice)
{
    auto priceResult = GetValidatedIntervalPrice(priceFeedId, useNextPrice, requireLivePrice);
    if (!priceResult)
        return priceResult;

    auto price = *priceResult.val;
    auto amountInCurrency = MultiplyAmounts(price, amount);
    if (price > COIN && amountInCurrency < amount)
        return Res::Err("Value/price too high (%s/%s)", GetDecimaleString(amount), GetDecimaleString(price));

    return ResVal<CAmount>(amountInCurrency, Res::Ok());
}

ResVal<CCollateralLoans> CCustomCSView::GetLoanCollaterals(CVaultId const& vaultId, CBalances const& collaterals, uint32_t height,
                                                           int64_t blockTime, bool useNextPrice, bool requireLivePrice)
{
    auto vault = GetVault(vaultId);
    if (!vault || vault->isUnderLiquidation)
        return Res::Err("Vault is under liquidation");

    CCollateralLoans result{};
    auto res = PopulateLoansData(result, vaultId, height, blockTime, useNextPrice, requireLivePrice);
    if (!res)
        return std::move(res);

    res = PopulateCollateralData(result, vaultId, collaterals, height, blockTime, useNextPrice, requireLivePrice);
    if (!res)
        return std::move(res);

    LogPrint(BCLog::LOAN, "\t\t%s(): totalCollaterals - %lld, totalLoans - %lld, ratio - %d\n",
        __func__, result.totalCollaterals, result.totalLoans, result.ratio());

    return ResVal<CCollateralLoans>(result, Res::Ok());
}

ResVal<CAmount> CCustomCSView::GetValidatedIntervalPrice(CTokenCurrencyPair priceFeedId, bool useNextPrice, bool requireLivePrice)
{
    auto tokenSymbol = priceFeedId.first;
    auto currency = priceFeedId.second;

    LogPrint(BCLog::ORACLE,"\t\t%s()->for_loans->%s->", __func__, tokenSymbol); /* Continued */

    auto priceFeed = GetFixedIntervalPrice(priceFeedId);
    if (!priceFeed)
        return std::move(priceFeed);

    if (requireLivePrice && !priceFeed.val->isLive(GetPriceDeviation()))
        return Res::Err("No live fixed prices for %s/%s", tokenSymbol, currency);

    auto priceRecordIndex = useNextPrice ? 1 : 0;
    auto price = priceFeed.val->priceRecord[priceRecordIndex];
    if (price <= 0)
        return Res::Err("Negative price (%s/%s)", tokenSymbol, currency);

    return ResVal<CAmount>(price, Res::Ok());
}

Res CCustomCSView::PopulateLoansData(CCollateralLoans& result, CVaultId const& vaultId, uint32_t height,
                                     int64_t blockTime, bool useNextPrice, bool requireLivePrice)
{
    auto loanTokens = GetLoanTokens(vaultId);
    if (!loanTokens)
        return Res::Ok();

    for (const auto& loan : loanTokens->balances) {
        auto loanTokenId = loan.first;
        auto loanTokenAmount = loan.second;

        auto token = GetLoanTokenByID(loanTokenId);
        if (!token)
            return Res::Err("Loan token with id (%s) does not exist!", loanTokenId.ToString());

        auto rate = GetInterestRate(vaultId, loanTokenId, height);
        if (!rate)
            return Res::Err("Cannot get interest rate for token (%s)!", token->symbol);

        if (rate->height > height)
            return Res::Err("Trying to read loans in the past");
        LogPrint(BCLog::LOAN,"\t\t%s()->for_loans->%s->", __func__, token->symbol); /* Continued */

        auto totalAmount = loanTokenAmount + TotalInterest(*rate, height);
        auto amountInCurrency = GetAmountInCurrency(totalAmount, token->fixedIntervalPriceId, useNextPrice, requireLivePrice);
        if (!amountInCurrency)
            return std::move(amountInCurrency);

        auto prevLoans = result.totalLoans;
        result.totalLoans += *amountInCurrency.val;

        if (prevLoans > result.totalLoans)
            return Res::Err("Exceeded maximum loans");

        result.loans.push_back({loanTokenId, amountInCurrency});
    }
    return Res::Ok();
}

Res CCustomCSView::PopulateCollateralData(CCollateralLoans& result, CVaultId const& vaultId, CBalances const& collaterals,
                                          uint32_t height, int64_t blockTime, bool useNextPrice, bool requireLivePrice)
{
    for (const auto& col : collaterals.balances) {
        auto tokenId = col.first;
        auto tokenAmount = col.second;

        auto token = HasLoanCollateralToken({tokenId, height});
        if (!token)
            return Res::Err("Collateral token with id (%s) does not exist!", tokenId.ToString());

        auto amountInCurrency = GetAmountInCurrency(tokenAmount, token->fixedIntervalPriceId, useNextPrice, requireLivePrice);
        if (!amountInCurrency)
            return std::move(amountInCurrency);

        auto amountFactor = MultiplyAmounts(token->factor, *amountInCurrency.val);

        auto prevCollaterals = result.totalCollaterals;
        result.totalCollaterals += amountFactor;

        if (prevCollaterals > result.totalCollaterals)
            return Res::Err("Exceeded maximum collateral");

        result.collaterals.push_back({tokenId, amountInCurrency});
    }
    return Res::Ok();
}

uint256 CCustomCSView::MerkleRoot()
{
    auto flushable = GetStorage().GetFlushableStorage();
    assert(flushable);
    auto& rawMap = flushable->GetRaw();
    if (rawMap.empty()) {
        return {};
    }
    std::vector<uint256> hashes;
    for (const auto& it : rawMap) {
        auto value = it.second ? *it.second : TBytes{};
        hashes.push_back(Hash2(it.first, value));
    }
    return ComputeMerkleRoot(std::move(hashes));
}

std::map<CKeyID, CKey> AmISignerNow(int height, CAnchorData::CTeam const & team)
{
    AssertLockHeld(cs_main);

    std::map<CKeyID, CKey> operatorDetails;
    auto const mnIds = pcustomcsview->GetOperatorsMulti();
    for (const auto& mnId : mnIds)
    {
        auto node = pcustomcsview->GetMasternode(mnId.second);
        if (!node) {
            continue;
        }

        if (node->IsActive(height) && team.find(mnId.first) != team.end()) {
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
