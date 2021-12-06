// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/anchors.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <key.h>
#include <logging.h>
#include <masternodes/masternodes.h>
#include <streams.h>
#include <script/standard.h>
#include <spv/spv_wrapper.h>
#include <timedata.h>
#include <util/system.h>
#include <util/validation.h>
#include <validation.h>

#include <algorithm>

#include <tuple>

std::unique_ptr<CAnchorAuthIndex> panchorauths;
std::unique_ptr<CAnchorIndex> panchors;
std::unique_ptr<CAnchorAwaitingConfirms> panchorAwaitingConfirms;

static const char DB_ANCHORS = 'A';
static const char DB_PENDING = 'p';
static const char DB_BITCOININDEX = 'Z';  // Bitcoin height to blockhash table

uint256 CAnchorData::GetSignHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << previousAnchor << height << blockHash << nextTeam; // << salt_;
    return Hash(ss.begin(), ss.end());
}

CAnchorAuthMessage::Signature CAnchorAuthMessage::GetSignature() const
{
    return signature;
}

uint256 CAnchorAuthMessage::GetHash() const
{
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

bool CAnchorAuthMessage::SignWithKey(const CKey& key)
{
    if (!key.SignCompact(GetSignHash(), signature)) {
        signature.clear();
    }
    return !signature.empty();
}

bool CAnchorAuthMessage::GetPubKey(CPubKey& pubKey) const
{
    return !signature.empty() && pubKey.RecoverCompact(GetSignHash(), signature);
}

CKeyID CAnchorAuthMessage::GetSigner() const
{
    CPubKey pubKey;
    return (!signature.empty() && pubKey.RecoverCompact(GetSignHash(), signature)) ? pubKey.GetID() : CKeyID{};
}

CAnchor CAnchor::Create(const std::vector<CAnchorAuthMessage> & auths, CTxDestination const & rewardDest)
{
    // assumed here that all of the auths are uniform, were checked for sigs and consensus has been reached!
    assert(rewardDest.index() == PKHashType || rewardDest.index() == WitV0KeyHashType);

    if (auths.size() > 0) {
        CAnchor anchor(static_cast<CAnchorData const &> (auths.at(0)));

        for (size_t i = 0; i < auths.size(); ++i) {
            anchor.sigs.push_back(auths[i].GetSignature());
        }
        anchor.rewardKeyID = rewardDest.index() == PKHashType ? CKeyID(std::get<PKHash>(rewardDest)) : CKeyID(std::get<WitnessV0KeyHash>(rewardDest));
        anchor.rewardKeyType = rewardDest.index();
        return anchor;
    }
    return {};
}

bool CAnchor::CheckAuthSigs(CTeam const & team) const
{
    // Sigs must meet quorum size.
    const auto quorum = GetMinAnchorQuorum(team);
    if (sigs.size() < quorum) {
        return error("%s: Anchor auth team quorum not met. Min quorum: %d sigs size %d", __func__, GetMinAnchorQuorum(team), sigs.size());
    }

    // Number of unique sigs must meet the required quorum.
    const auto uniqueKeys = CheckSigs(GetSignHash(), sigs, team);
    if (uniqueKeys < quorum) {
        return error("%s: Anchor auth team unique key quorum not met. Min quorum: %d keys size %d", __func__, quorum, uniqueKeys);
    }

    return true;
}

const CAnchorAuthIndex::Auth * CAnchorAuthIndex::GetAuth(uint256 const & msgHash) const
{
    AssertLockHeld(cs_main);

    auto const & list = auths.get<Auth::ByMsgHash>();
    auto const it = list.find(msgHash);
    return it != list.end() ? &(*it) : nullptr;
}

const CAnchorAuthIndex::Auth * CAnchorAuthIndex::GetVote(const uint256 & signHash, const CKeyID & signer) const
{
    AssertLockHeld(cs_main);

    auto const & list = auths.get<Auth::ByVote>();
    auto const it = list.find(std::make_tuple(signHash, signer));
    return it != list.end() ? &(*it) : nullptr;
}

bool CAnchorAuthIndex::ValidateAuth(const CAnchorAuthIndex::Auth & auth) const
{
    AssertLockHeld(cs_main);

    // 1. Prev and top checks

    // Skip checks if no SPV as panchors will be empty (allows non-SPV nodes to relay auth messages)
    if (spv::pspv) {
        if (!auth.previousAnchor.IsNull()) {
            auto prev = panchors->GetAnchorByTx(auth.previousAnchor);

            if (!prev) {
                LogPrint(BCLog::ANCHORING, "%s: Got anchor auth, hash %s, blockheight: %d, but can't find previousAnchor %s\n", __func__, auth.GetHash().ToString(), auth.height, auth.previousAnchor.ToString());
                return false;
            }

            if (auth.height <= prev->anchor.height) {
                LogPrint(BCLog::ANCHORING, "%s: Auth blockHeight should be higher than previousAnchor height! %d > %d\n", __func__, auth.height, prev->anchor.height);
                return false;
            }
        }

        auto const * topAnchor = panchors->GetActiveAnchor();
        if (topAnchor) {
            if (auth.height <= topAnchor->anchor.height) {
                LogPrint(BCLog::ANCHORING, "%s: Auth blockHeight should be higher than top anchor height! %d > %d\n", __func__, auth.height, topAnchor->anchor.height);
                return false;
            }

            // Top anchor should chain from previous anchor if it exists
            if (auth.previousAnchor.IsNull()) {
                LogPrint(BCLog::ANCHORING, "%s: anchor does not have previous anchor set to top anchor\n", __func__);
                return false;
            }

            // Previous anchor should link to top anchor
            if (auth.previousAnchor != topAnchor->txHash) {
                LogPrint(BCLog::ANCHORING, "%s: anchor previousAnchor does not match top anchor\n", __func__);
                return false;
            }
        }
    }

    // 2. chain context:
    CBlockIndex* block = ::ChainActive()[auth.height];
    if (block == nullptr) {
        LogPrint(BCLog::ANCHORING, "%s: Can't get block from anchor height: %d\n", __func__, auth.height);
        return false;
    }

    // 3. Full anchor validation and team context
    CTeam team;
    uint64_t anchorCreationHeight;
    CBlockIndex anchorBlock;
    if (!ContextualValidateAnchor(auth, anchorBlock, anchorCreationHeight)) {
        return false;
    }

    // Let's try and get the team that set this anchor
    auto anchorTeam = pcustomcsview->GetAuthTeam(anchorCreationHeight);
    if (anchorTeam) {
        team = *anchorTeam;
    }

    if (team.empty()) {
        LogPrint(BCLog::ANCHORING, "%s: Can't get team for previousAnchor tx %s\n", __func__, auth.previousAnchor.ToString());
        return false;
    }

    // 4. Signatures

    CPubKey pubKey;
    if (!auth.GetPubKey(pubKey)) {
        LogPrint(BCLog::ANCHORING, "%s: Can't recover pubkey from sig, auth: %s\n", __func__, auth.GetHash().ToString());
        return false;;
    }
    const CKeyID masternodeKey{pubKey.GetID()};
    if (team.find(masternodeKey) == team.end()) {
        LogPrint(BCLog::ANCHORING, "%s: Recovered keyID %s is not a current team member\n", __func__, masternodeKey.ToString());
        return false;
    }

    return true;
}

bool CAnchorAuthIndex::AddAuth(const CAnchorAuthIndex::Auth & auth)
{
    AssertLockHeld(cs_main);
    return auths.insert(auth).second;
}

uint32_t GetMinAnchorQuorum(const CAnchorData::CTeam &team)
{
    if (Params().NetworkIDString() == "regtest") {
        return gArgs.GetArg("-anchorquorum", 1);
    }
    return  static_cast<uint32_t>(1 + (team.size() * 2) / 3); // 66% + 1
}

CAmount GetAnchorSubsidy(int anchorHeight, int prevAnchorHeight, const Consensus::Params& consensusParams)
{
    if (anchorHeight < prevAnchorHeight) {
        return 0;
    }

    int period = anchorHeight - prevAnchorHeight;
    return consensusParams.spv.anchorSubsidy + (period / consensusParams.spv.subsidyIncreasePeriod) * consensusParams.spv.subsidyIncreaseValue;
}

/*
 * Choose best (high) anchor auth group with
 */
CAnchor CAnchorAuthIndex::CreateBestAnchor(CTxDestination const & rewardDest) const
{
    AssertLockHeld(cs_main);
    // KList is sorted by defi height + signHash (all except sign)
    typedef Auths::index<Auth::ByKey>::type KList;
    KList const & list = auths.get<Auth::ByKey>();

    auto const topAnchor = panchors->GetActiveAnchor();
    uint32_t quorum = 1 + (Params().GetConsensus().mn.anchoringTeamSize * 2) / 3;
    auto const topHeight = topAnchor ? topAnchor->anchor.height : 0;
    LogPrint(BCLog::ANCHORING, "auths size: %d quorum: %d\n", list.size(), quorum);

    std::vector<Auth> freshestConsensus;
    THeight curHeight = 0;
    uint256 curSignHash = {};

    // get freshest consensus:
    for (auto it = list.rbegin(); it != list.rend() && it->height > topHeight; ++it) {
        LogPrint(BCLog::ANCHORING, "%s: height %d, blockHash %s, signHash %s, msg %s\n", __func__, it->height, it->blockHash.ToString(), it->GetSignHash().ToString(), it->GetHash().ToString());
        if (topAnchor && topAnchor->txHash != it->previousAnchor)
            continue;

        if (curHeight != it->height || curSignHash != it->GetSignHash()) {
            // got next group of auths
            curHeight = it->height;
            curSignHash = it->GetSignHash();

            if (it->nextTeam.size() == 1)
            {
                // Team data reference
                const CKeyID& teamData = *it->nextTeam.begin();

                uint64_t anchorCreationHeight;
                std::shared_ptr<std::vector<unsigned char>> prefix;

                // Check this is post-fork anchor auth
                if (GetAnchorEmbeddedData(teamData, anchorCreationHeight, prefix))
                {
                    // Get anchor team at time of creating this auth
                    auto team = pcustomcsview->GetAuthTeam(anchorCreationHeight);
                    if (team) {
                        // Now we can set the appropriate quorum size
                        quorum = GetMinAnchorQuorum(*team);
                    }
                }
            }

            // we doesn't choose here between "equal" valid auths (by max quorum nor by prevTeam or anything else)
            auto count = list.count(std::make_tuple(curHeight, curSignHash));

            if (count >= quorum) {
                auto [it0, it1] = list.equal_range(std::make_tuple(curHeight, curSignHash));

                // Fix to avoid "Anchor too new" error until F hard fork
                auto anchorIndex = ::ChainActive()[it0->height];
                if (!anchorIndex || anchorIndex->nTime + Params().GetConsensus().mn.anchoringTimeDepth > GetAdjustedTime()) {
                    continue;
                }

                uint32_t validCount{0};
                auto it0Copy = it0;
                for (uint32_t i{0}; i < quorum && it0Copy != it1; ++i, ++it0Copy) {
                    // ValidateAuth called here performs extra checks with SPV enabled.
                    if (ValidateAuth(*it0Copy)) {
                        ++validCount;
                    }
                }

                if (validCount >= quorum) {
                    for (uint32_t i = 0; i < quorum && it0 != it1; ++i, ++it0) {
                        LogPrint(BCLog::ANCHORING, "auths: pick up %d, %s, %s\n", it0->height, it0->blockHash.ToString(), it0->GetHash().ToString());

                        freshestConsensus.push_back(*it0);
                    }
                    break;
                }
            }
        }
    }

    return CAnchor::Create(freshestConsensus, rewardDest);
}

void CAnchorAuthIndex::ForEachAnchorAuthByHeight(std::function<bool (const CAnchorAuthIndex::Auth &)> callback) const
{
    AssertLockHeld(cs_main);

    typedef Auths::index<Auth::ByKey>::type KList;
    KList const & list = auths.get<Auth::ByKey>();
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (!callback(*it)) break;
}

void CAnchorAuthIndex::PruneOlderThan(THeight height)
{
    AssertLockHeld(cs_main);
    // KList is sorted by defi height + signHash (all except sign)
    typedef Auths::index<Auth::ByKey>::type KList;
    KList & list = auths.get<Auth::ByKey>();

    auto it = list.upper_bound(std::make_tuple(height, uint256{}));
    list.erase(list.begin(), it);
}

CAnchorIndex::CAnchorIndex(size_t nCacheSize, bool fMemory, bool fWipe)
    : db(new CDBWrapper(GetDataDir() / "anchors", nCacheSize, fMemory, fWipe))
{
}

bool CAnchorIndex::Load()
{
    AssertLockHeld(cs_main);

    AnchorIndexImpl().swap(anchors);

    std::function<void (uint256 const &, AnchorRec &)> onLoad = [this] (uint256 const &, AnchorRec & rec) {
        // just for debug
        LogPrint(BCLog::ANCHORING, "anchor load: blockHash: %s, height %d, btc height: %d\n", rec.anchor.blockHash.ToString(), rec.anchor.height, rec.btcHeight);

        anchors.insert(std::move(rec));
    };
    bool result = IterateTable(DB_ANCHORS, onLoad);

    if (result) {
        // fix spv height to avoid datarace while choosing best anchor
        // (in the 'Load' it is safe to call spv under lock cause it is not connected yet)
        spvLastHeight = spv::pspv ? spv::pspv->GetLastBlockHeight() : 0;
        ActivateBestAnchor(true);
    }
    return result;
}

void CAnchorIndex::ForEachAnchorByBtcHeight(std::function<bool(const CAnchorIndex::AnchorRec &)> callback) const
{
    typedef AnchorIndexImpl::index<AnchorRec::ByBtcHeight>::type KList;
    KList const & list = anchors.get<AnchorRec::ByBtcHeight>();
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (!callback(*it)) break;
}

const CAnchorIndex::AnchorRec * CAnchorIndex::GetActiveAnchor() const
{
    return top;
}

const CAnchorIndex::AnchorRec * CAnchorIndex::GetAnchorByTx(const uint256 & hash) const
{
    AssertLockHeld(cs_main);

    AnchorIndexImpl const & index = anchors;
    auto & list = index.get<AnchorRec::ByBtcTxHash>();
    auto it = list.find(hash);
    return it != list.end() ? &(*it) : nullptr;
}

bool CAnchorIndex::AddAnchor(CAnchor const & anchor, uint256 const & btcTxHash, THeight btcBlockHeight, bool overwrite)
{
    AssertLockHeld(cs_main);

    AnchorRec rec{ anchor, btcTxHash, btcBlockHeight };
    if (overwrite) {
        DeleteAnchorByBtcTx(btcTxHash);
    }

    bool result = anchors.insert(rec).second;
    if (result) {
        DbWrite(rec);
        possibleReActivation = true;
    }
    return result;
}

bool CAnchorIndex::DeleteAnchorByBtcTx(const uint256 & btcTxHash)
{
    AssertLockHeld(cs_main);

    auto anchor = GetAnchorByBtcTx(btcTxHash);

    if (anchor) {
        // revert top if deleted anchor was in active chain (one of current top parents)
        for (auto it = top; it && it->btcHeight >= anchor->btcHeight; it = GetAnchorByBtcTx(it->anchor.previousAnchor)) {
            if (anchor == it) {
                top = GetAnchorByBtcTx(anchor->anchor.previousAnchor);
                possibleReActivation = true;
                break;
            }
        }
        anchors.get<AnchorRec::ByBtcTxHash>().erase(btcTxHash);
        if (DbExists(btcTxHash))
            DbErase(btcTxHash);
        return true;
    }
    return false;
}

CAnchorData::CTeam CAnchorIndex::GetNextTeam(const uint256 & btcPrevTx) const
{
    AssertLockHeld(cs_main);

    if (btcPrevTx.IsNull())
        return Params().GetGenesisTeam();

    AnchorRec const * prev = GetAnchorByTx(btcPrevTx);
    if (!prev) {
        LogPrintf("Can't get previous anchor with btc hash %s\n",  btcPrevTx.ToString());
        return CAnchorData::CTeam{};
    }
    return prev->anchor.nextTeam;
}

CAnchorIndex::AnchorRec const * CAnchorIndex::GetAnchorByBtcTx(uint256 const & txHash) const
{
    AssertLockHeld(cs_main);

    typedef AnchorIndexImpl::index<AnchorRec::ByBtcTxHash>::type KList;
    KList const & list = anchors.get<AnchorRec::ByBtcTxHash>();

    auto const it = list.find(txHash);
    return it != list.end() ? &*it : nullptr;
}

/// Get all UNREWARDED and ACTIVE anchors (resided on chain with anchor's top).
CAnchorIndex::UnrewardedResult CAnchorIndex::GetUnrewarded() const
{
    AssertLockHeld(cs_main);

    auto it = panchors->GetActiveAnchor();
    // skip unconfirmed
    for (; it && GetAnchorConfirmations(it) < 6; it = panchors->GetAnchorByBtcTx(it->anchor.previousAnchor))
        ;
    // create confirmed set
    UnrewardedResult confirmed;
    for (; it ; it = panchors->GetAnchorByBtcTx(it->anchor.previousAnchor)) {
        confirmed.insert(it->txHash);
    }

    // find unrewarded
    for (auto && it = confirmed.begin(); it != confirmed.end(); /* no advance */) {
        if (pcustomcsview->GetRewardForAnchor(*it))
            it = confirmed.erase(it);
        else
            it++;
    }

    return confirmed;
}

int CAnchorIndex::GetAnchorConfirmations(uint256 const & txHash) const
{
    AssertLockHeld(cs_main);
    return GetAnchorConfirmations(GetAnchorByBtcTx(txHash));
}

int CAnchorIndex::GetAnchorConfirmations(const CAnchorIndex::AnchorRec * rec) const
{
    AssertLockHeld(cs_main);
    if (!rec) {
        return -1;
    }
    // for cases when tx->blockHeight == TX_UNCONFIRMED _or_ GetLastBlockHeight() less than already _confirmed_ tx (rescan in progress)
    return spvLastHeight < rec->btcHeight ? 0 : spvLastHeight - rec->btcHeight + 1;
}

void CAnchorIndex::CheckPendingAnchors()
{
    AssertLockHeld(cs_main);

    spv::PendingSet anchorsPending(spv::PendingOrder);
    ForEachPending([&anchorsPending](uint256 const &, AnchorRec & rec) {
        anchorsPending.insert(rec);
    });

    std::set<uint256> deletePending;
    for (const auto& rec : anchorsPending) {

        CBlockIndex anchorBlock;
        uint64_t anchorCreationHeight;
        if (!ContextualValidateAnchor(rec.anchor, anchorBlock, anchorCreationHeight)) {

            // Height used to ignore failure if due to not being in main chain yet.
            if (anchorCreationHeight != std::numeric_limits<uint64_t>::max()) {
                deletePending.insert(rec.txHash);
            }

            continue;
        }

        const auto timestamp = spv::pspv->ReadTxTimestamp(rec.txHash);
        const auto blockHeight = spv::pspv->ReadTxBlockHeight(rec.txHash);
        const auto blockHash = panchors->ReadBlockHash(rec.btcHeight);

        // Do not delete, TX time still pending. If block height is set to max we cannot trust the timestamp.
        if (timestamp == 0 || blockHeight == std::numeric_limits<int32_t>::max() || blockHash == uint256()) {
            continue;
        }

        // Here we can check new rule that Bitcoin blocktime is three hours more than DeFi anchored block
        if (anchorBlock.nTime > timestamp - Params().GetConsensus().mn.anchoringTimeDepth) {
            LogPrint(BCLog::ANCHORING, "Anchor too new. DeFi: %d Bitcoin: %d Anchor: %s\n", anchorBlock.nTime, timestamp, rec.txHash.ToString());
            deletePending.insert(rec.txHash);
            continue;
        }

        // Let's try and get the team that set this anchor
        CTeam team;
        auto anchorTeam = pcustomcsview->GetAuthTeam(anchorCreationHeight);
        if (!anchorTeam || anchorTeam->empty()) {
            LogPrint(BCLog::ANCHORING, "No team found at height %ld. Deleting anchor txHash %s\n", anchorCreationHeight, rec.txHash.ToString());
            deletePending.insert(rec.txHash);
            continue;
        }

        // Validate the anchor sigs
        if (!rec.anchor.CheckAuthSigs(*anchorTeam)) {
            LogPrint(BCLog::ANCHORING, "Signature validation fails. Deleting anchor txHash %s\n", rec.txHash.ToString());
            deletePending.insert(rec.txHash);
            continue;
        }

        // Finally add to anchors and delete from pending
        if (AddAnchor(rec.anchor, rec.txHash, rec.btcHeight)) {
            deletePending.insert(rec.txHash);
        }

        // Recheck anchors
        possibleReActivation = true;
        CheckActiveAnchor();
    }

    for (const auto& hash : deletePending) {
        DeletePendingByBtcTx(hash);
    }
}

void CAnchorIndex::CheckActiveAnchor(bool forced)
{
    // Only continue with context of chain
    if (ShutdownRequested()) return;

    {
        // fix spv height to avoid datarace while choosing best anchor
        uint32_t const tmp = spv::pspv ? spv::pspv->GetLastBlockHeight() : 0;
        LOCK(cs_main);
        spvLastHeight = tmp;

        panchors->ActivateBestAnchor(forced);

        // prune auths older than anchor with 6 confirmations. Warning! This constant are using for start confirming reward too!
        auto it = panchors->GetActiveAnchor();
        for (; it && GetAnchorConfirmations(it) < 6; it = panchors->GetAnchorByBtcTx(it->anchor.previousAnchor))
            ;
        if (it)
            panchorauths->PruneOlderThan(it->anchor.height+1);

        /// @todo panchorAwaitingConfirms - optimize?
        /// @attention - 'it' depends on previous loop conditions (>=6).
        if (!::ChainstateActive().IsInitialBlockDownload()) {
            panchorAwaitingConfirms->ReVote();
        }
    }
}

void CAnchorIndex::UpdateLastHeight(uint32_t height)
{
    AssertLockHeld(cs_main);
    spvLastHeight = height;
}

// selects "best" of two anchors at the equal btc height (prevs must be checked before)
CAnchorIndex::AnchorRec const * BestOfTwo(CAnchorIndex::AnchorRec const * a1, CAnchorIndex::AnchorRec const * a2)
{
    if (a1 == nullptr)
        return a2;
    if (a2 == nullptr)
        return a1;

    return spv::PendingOrder(*a1, *a2) ? a1 : a2;
}

/// @returns true if top active anchor has been changed
bool CAnchorIndex::ActivateBestAnchor(bool forced)
{
    AssertLockHeld(cs_main);

    if (!possibleReActivation && !forced)
        return false;

    possibleReActivation = false;

    int const minConfirmations{Params().GetConsensus().spv.minConfirmations};
    auto oldTop = top;
    // rollback if necessary. this should not happen in prod (w/o anchor tx deletion), but possible in test when manually reduce height in btc chain
    for (; top && GetAnchorConfirmations(top) < minConfirmations; top = GetAnchorByBtcTx(top->anchor.previousAnchor))
        ;

    THeight topHeight = top ? top->btcHeight : 0;
    // special case for the first iteration to reselect top
    uint256 prev = top ? top->anchor.previousAnchor : uint256{};

    // start running from top anchor height
    // (yes, it can re-select different best anchor on the "current" top anchor level - for the cases if spv not feed txs of one block "at once")
    typedef AnchorIndexImpl::index<AnchorRec::ByBtcHeight>::type KList;
    KList const & list = anchors.get<AnchorRec::ByBtcHeight>();
    for (auto it = (topHeight == 0 ? list.begin() : list.find(topHeight)); it != list.end(); ) {

        int const confs = GetAnchorConfirmations(&*it);
        if (confs < minConfirmations)
        {
            // still pending - check it again on next event
            /// @todo may be additional check for confs > 0 (for possibleReActivation)
            possibleReActivation = true;
            break;
        }

        auto [it0, it1] = list.equal_range(it->btcHeight);
        CAnchorIndex::AnchorRec const * choosenOne = nullptr;
        // at least one iteration here
        for (; it0 != it1; ++it0) {
            if (it0->anchor.previousAnchor == prev)
            {
                // candidate
                choosenOne = BestOfTwo(choosenOne, &*it0);
            }
        }

        if (choosenOne) {
            top = choosenOne;
            prev = top->txHash;
            // after the very first iteration, top acts as prev
        }
        it = it1;
    }
    return top != oldTop;
}

bool CAnchorIndex::AddToAnchorPending(CAnchor const & anchor, uint256 const & btcTxHash, THeight btcBlockHeight, bool overwrite)
{
    AssertLockHeld(cs_main);

    AnchorRec rec{ anchor, btcTxHash, btcBlockHeight };
    if (overwrite) {
        DeletePendingByBtcTx(btcTxHash);
    }

    return db->Write(std::make_pair(DB_PENDING, rec.txHash), rec);
}

bool CAnchorIndex::GetPendingByBtcTx(uint256 const & txHash, AnchorRec & rec) const
{
    AssertLockHeld(cs_main);

    return db->Read(std::make_pair(DB_PENDING, txHash), rec);
}

bool CAnchorIndex::DeletePendingByBtcTx(uint256 const & btcTxHash)
{
    AssertLockHeld(cs_main);

    AnchorRec pending;

    if (GetPendingByBtcTx(btcTxHash, pending)) {
        if (db->Exists(std::make_pair(DB_PENDING, btcTxHash))) {
            db->Erase(std::make_pair(DB_PENDING, btcTxHash));
        }
        return true;
    }

    return false;
}

bool CAnchorIndex::WriteBlock(const uint32_t height, const uint256& blockHash)
{
    // Store Bitcoin block index
    return db->Write(std::make_pair(DB_BITCOININDEX, height), blockHash);
}

uint256 CAnchorIndex::ReadBlockHash(const uint32_t& height)
{
    uint256 blockHash;
    db->Read(std::make_pair(DB_BITCOININDEX, height), blockHash);
    return blockHash;
}

void CAnchorIndex::ForEachPending(std::function<void (uint256 const &, AnchorRec &)> callback)
{
    AssertLockHeld(cs_main);

    IterateTable(DB_PENDING, callback);
}

bool CAnchorIndex::DbExists(const uint256 & hash) const
{
    return db->Exists(std::make_pair(DB_ANCHORS, hash));
}

bool CAnchorIndex::DbRead(uint256 const & hash, AnchorRec & rec) const
{
    return db->Read(std::make_pair(DB_ANCHORS, hash), rec);
}

bool CAnchorIndex::DbWrite(AnchorRec const & rec)
{
    return db->Write(std::make_pair(DB_ANCHORS, rec.txHash), rec);
}

bool CAnchorIndex::DbErase(uint256 const & hash)
{
    return db->Erase(std::make_pair(DB_ANCHORS, hash));
}

const CAnchorIndex::AnchorRec* CAnchorIndex::GetLatestAnchorUpToDeFiHeight(THeight blockHeightDeFi) const
{
    AssertLockHeld(cs_main);

    auto & index = anchors.get<AnchorRec::ByDeFiHeight>();
    auto it = index.lower_bound(blockHeightDeFi);
    return (it != index.begin()) ? &(*(--it)) : nullptr;
}

/// Validates all except tx confirmations
bool ValidateAnchor(const CAnchor & anchor)
{
    AssertLockHeld(cs_main);

    // Check sig size to avoid storing bogus anchors with large number of sigs
    if (anchor.nextTeam.size() != 1) {
        return error("%s: Incorrect anchor team size. Found: %d",
                     __func__, anchor.nextTeam.size());
    }

    if (anchor.sigs.size() <= static_cast<size_t>(Params().GetConsensus().mn.anchoringTeamSize))
    {
        // Team entry
        const CKeyID& teamData = *anchor.nextTeam.begin();

        uint64_t anchorCreationHeight;
        std::shared_ptr<std::vector<unsigned char>> prefix;

        // Post-fork anchor, will be added to pending anchors to validate in chain context
        if (GetAnchorEmbeddedData(teamData, anchorCreationHeight, prefix))
        {
            // Make sure anchor created after fork.
            if (anchorCreationHeight >= static_cast<uint64_t>(Params().GetConsensus().DakotaHeight)) {
                return true;
            } else {
                LogPrint(BCLog::ANCHORING, "%s: Post fork anchor created before fork height. Anchor %ld fork %d\n",
                         __func__, anchorCreationHeight, Params().GetConsensus().DakotaHeight);
                return false;
            }
        }
    }

    return false;
}

bool ContextualValidateAnchor(const CAnchorData &anchor, CBlockIndex& anchorBlock, uint64_t &anchorCreationHeight)
{
    AssertLockHeld(cs_main);

    // Validate previous anchor. Set spv::pspv to allow anchor auth relay on non-SPV nodes.
    if (spv::pspv && !anchor.previousAnchor.IsNull())
    {
        auto prev = panchors->GetAnchorByTx(anchor.previousAnchor);
        CAnchorIndex::AnchorRec pending;
        if (!prev)
        {
            // Check pending anchors
            if (!panchors->GetPendingByBtcTx(anchor.previousAnchor, pending))
            {
                return error("%s: Previous anchor %s specified, but does not exist.",
                             __func__, anchor.previousAnchor.ToString());
            }
        }
        else if ((prev && anchor.height <= prev->anchor.height) || // Check height in accepted anchor
                 (!pending.txHash.IsNull() && anchor.height <= pending.anchor.height)) // Check height in pending anchor
        {
            return error("%s: Anchor blockHeight should be higher than previousAnchor height! %d > %d",
                         __func__, anchor.height, prev->anchor.height);
        }
    }

    CBlockIndex* anchorIndex = ::ChainActive()[anchor.height];
    if (!anchorIndex) {
        // Set to max to be used to avoid deleting pending anchor.
        anchorCreationHeight = std::numeric_limits<uint64_t>::max();

        return error("%s: Active chain does not yet contain block height %d!", __func__, anchor.height);
    }

    if (anchorIndex->GetBlockHash() != anchor.blockHash) {
        return error("%s: Anchor and blockchain mismatch at height %d. Expected %s found %s",
                     __func__, anchor.height, anchorIndex->GetBlockHash().ToString(), anchor.blockHash.ToString());
    }

    // Should already be checked before adding to pending, double check here.
    if (anchor.nextTeam.empty() || anchor.nextTeam.size() != 1) {
        return error("%s: nextTeam empty or incorrect size. %d elements in team.", __func__, anchor.nextTeam.size());
    }

    // Team entry
    const CKeyID& teamData = *anchor.nextTeam.begin();

    std::shared_ptr<std::vector<unsigned char>> prefix;

    // Check this is post-fork anchor auth
    if (!GetAnchorEmbeddedData(teamData, anchorCreationHeight, prefix)) {
        return error("%s: Post-fork anchor marker missing or incorrect.", __func__);
    }

    // Only anchor by specified frequency
    if (anchorCreationHeight % Params().GetConsensus().mn.anchoringFrequency != 0) {
        return error("%s: Anchor height does not meet frequency rule. Height %ld, frequency %d",
                     __func__, anchorCreationHeight, Params().GetConsensus().mn.anchoringFrequency);
    }

    // Make sure height exist
    const auto anchorCreationBlock = ::ChainActive()[anchorCreationHeight];
    if (anchorCreationBlock == nullptr) {
        LogPrint(BCLog::ANCHORING, "%s: Cannot get block from anchor team data: height %ld\n", __func__, anchorCreationHeight);

        // Set to max to be used to avoid deleting pending anchor.
        anchorCreationHeight = std::numeric_limits<uint64_t>::max();

        return false;
    }

    // Then match the hash prefix from anchor and active chain
    size_t prefixLength{CKeyID().size() - (spv::BtcAnchorMarker.size() * sizeof(uint8_t)) - sizeof(uint64_t)};
    std::vector<unsigned char> hashPrefix{anchorCreationBlock->GetBlockHash().begin(), anchorCreationBlock->GetBlockHash().begin() + prefixLength};
    if (hashPrefix != *prefix) {
        return error("%s: Anchor prefix and active chain do not match. anchor %s active %s height %ld",
                     __func__, HexStr(*prefix), HexStr(hashPrefix), anchorCreationHeight);
    }

    // Get start anchor height
    int anchorHeight = static_cast<int>(anchorCreationHeight) - Params().GetConsensus().mn.anchoringFrequency;

    // Recreate the creation height of the anchor
    int64_t timeDepth = Params().GetConsensus().mn.anchoringTimeDepth;
    while (anchorHeight > 0 && ::ChainActive()[anchorHeight]->nTime + timeDepth > anchorCreationBlock->nTime) {
        --anchorHeight;
    }

    // Recreate deeper anchor depth
    if (anchorCreationHeight >= Params().GetConsensus().FortCanningHeight) {
        timeDepth += Params().GetConsensus().mn.anchoringAdditionalTimeDepth;
        while (anchorHeight > 0 && ::ChainActive()[anchorHeight]->nTime + timeDepth > anchorCreationBlock->nTime) {
            --anchorHeight;
        }
    }

    // Wind back further by anchoring frequency
    while (anchorHeight > 0 && anchorHeight % Params().GetConsensus().mn.anchoringFrequency != 0) {
        --anchorHeight;
    }

    // Check heights match
    if (static_cast<int>(anchor.height) != anchorHeight) {
        return error("%s: Anchor height mismatch. Anchor height %d calculated height %d", __func__, anchor.height, anchorHeight);
    }

    anchorBlock = *::ChainActive()[anchorHeight];

    return true;
}

uint256 CAnchorConfirmData::GetSignHash() const
{
    CDataStream ss{SER_GETHASH, 0};
    ss << btcTxHash << anchorHeight << prevAnchorHeight << rewardKeyID << rewardKeyType;
    return Hash(ss.begin(), ss.end());
}

uint256 CAnchorConfirmDataPlus::GetSignHash() const
{
    CDataStream ss{SER_GETHASH, 0};
    ss << btcTxHash << anchorHeight << prevAnchorHeight << rewardKeyID << rewardKeyType << dfiBlockHash << btcTxHeight;
    return Hash(ss.begin(), ss.end());
}

std::optional<CAnchorConfirmMessage> CAnchorConfirmMessage::CreateSigned(const CAnchor& anchor, const THeight prevAnchorHeight,
                                                                           const uint256 &btcTxHash, CKey const & key, const THeight btcTxHeight)
{
    // Potential post-fork unrewarded anchor
    if (anchor.nextTeam.size() == 1)
    {
        // Team data reference
        const CKeyID& teamData = *anchor.nextTeam.begin();

        uint64_t anchorCreationHeight;
        std::shared_ptr<std::vector<unsigned char>> prefix;

        // Get anchor creation height
        GetAnchorEmbeddedData(teamData, anchorCreationHeight, prefix);
    }

    CAnchorConfirmMessage message(CAnchorConfirmDataPlus{btcTxHash, anchor.height, prevAnchorHeight, anchor.rewardKeyID,
                                                         anchor.rewardKeyType, anchor.blockHash, btcTxHeight});

    if (!key.SignCompact(message.GetSignHash(), message.signature)) {
        message.signature.clear();
    }

    return message;
}

uint256 CAnchorConfirmMessage::GetHash() const
{
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

CKeyID CAnchorConfirmMessage::GetSigner() const
{
    CPubKey pubKey;
    return (!signature.empty() && pubKey.RecoverCompact(GetSignHash(), signature)) ? pubKey.GetID() : CKeyID{};
}

bool CAnchorFinalizationMessage::CheckConfirmSigs()
{
    return CheckSigs(GetSignHash(), sigs, currentTeam);
}

size_t CAnchorFinalizationMessagePlus::CheckConfirmSigs(CAnchorData::CTeam const & team,  const uint32_t height)
{
    return CheckSigs(GetSignHash(), sigs, team);
}

bool CAnchorAwaitingConfirms::EraseAnchor(AnchorTxHash const &txHash)
{
    AssertLockHeld(cs_main);

    auto & list = confirms.get<Confirm::ByAnchor>();
    auto count = list.erase(txHash); // should erase ALL with that key. Check it!
    LogPrint(BCLog::ANCHORING, "AnchorConfirms::EraseAnchor: erase %d confirms for anchor %s\n", count, txHash.ToString());

    return count > 0;
}

const CAnchorConfirmMessage *CAnchorAwaitingConfirms::GetConfirm(ConfirmMessageHash const &msgHash) const
{
    AssertLockHeld(cs_main);

    auto const & list = confirms.get<Confirm::ByMsgHash>();
    auto it = list.find(msgHash);
    return it != list.end() ? &(*it) : nullptr;
}

bool CAnchorAwaitingConfirms::Validate(CAnchorConfirmMessage const &confirmMessage) const
{
    AssertLockHeld(cs_main);

    CKeyID signer = confirmMessage.GetSigner();
    if (signer.IsNull()) {
        LogPrint(BCLog::ANCHORING, "%s: Warning! Signature incorrect. btcTxHash: %s confirmMessageHash: %s Key: %s\n",
                 __func__, confirmMessage.btcTxHash.ToString(), confirmMessage.GetHash().ToString(), signer.ToString());
        return false;
    }

    const auto height = ::ChainActive().Height();
    const auto team = pcustomcsview->GetConfirmTeam(height);
    if (!team) {
        LogPrint(BCLog::ANCHORING, "%s: Unable to get team for current height %d\n", __func__, height);
        return false;
    }

    if (!team->count(signer)) {
        LogPrint(BCLog::ANCHORING, "%s: Signer not part of current anchor confirm team\n", __func__);
        return false;
    }

    auto it = pcustomcsview->GetMasternodeIdByOperator(signer);
    if (!it || !pcustomcsview->GetMasternode(*it)->IsActive(height)) {
        LogPrint(BCLog::ANCHORING, "%s: Warning! Masternode with operator key %s does not exist or not active!\n", __func__, signer.ToString());
        return false;
    }

    CBlockIndex* anchorIndex = ::ChainActive()[confirmMessage.anchorHeight];
    if (!anchorIndex) {
        LogPrint(BCLog::ANCHORING, "%s: Active chain does not contain block height %d\n", __func__, confirmMessage.anchorHeight);
        return false;
    }

    if (anchorIndex->GetBlockHash() != confirmMessage.dfiBlockHash) {
        LogPrint(BCLog::ANCHORING, "%s: Anchor and blockchain mismatch at height %d. Expected %s found %s\n",
                 __func__, confirmMessage.anchorHeight, anchorIndex->GetBlockHash().ToString(), confirmMessage.dfiBlockHash.ToString());
        return false;
    }

    if (auto reward = pcustomcsview->GetRewardForAnchor(confirmMessage.btcTxHash)) {
        LogPrint(BCLog::ANCHORING, "%s: Anchor already paid. DeFi Transaction: %s\n", __func__, reward->ToString());
        return false;
    }

    return true;
}

bool CAnchorAwaitingConfirms::Add(CAnchorConfirmMessage const &newConfirmMessage)
{
    AssertLockHeld(cs_main);
    return confirms.insert(newConfirmMessage).second;
}

void CAnchorAwaitingConfirms::Clear()
{
    AssertLockHeld(cs_main);
    Confirms().swap(confirms);
}

void CAnchorAwaitingConfirms::ReVote()
{
    AssertLockHeld(cs_main);

    const auto height = ::ChainActive().Height();

    CTeamView::CTeam currentTeam;
    auto team = pcustomcsview->GetConfirmTeam(height);
    if (team) {
        currentTeam = *team;
    } else if (!team || team->empty()) {
        return;
    }

    const auto operatorDetails = AmISignerNow(height, currentTeam);

    for (const auto& keys : operatorDetails) {
        CAnchorIndex::UnrewardedResult unrewarded = panchors->GetUnrewarded();
        for (auto const & btcTxHash : unrewarded) {
            pcustomcsview->CreateAndRelayConfirmMessageIfNeed(panchors->GetAnchorByTx(btcTxHash), btcTxHash, keys.second);
        }
    }

}

// for MINERS only!
std::vector<CAnchorConfirmMessage> CAnchorAwaitingConfirms::GetQuorumFor(const CAnchorData::CTeam & team) const
{
    AssertLockHeld(cs_main);

    typedef Confirms::index<Confirm::ByKey>::type KList;
    KList const & list = confirms.get<Confirm::ByKey>();
    uint32_t quorum = GetMinAnchorQuorum(team);

    std::vector<CAnchorConfirmMessage> result;

    for (auto it = list.begin(); it != list.end(); /* w/o advance! */) {
        // get next group of confirms
        auto [it0, it1] = list.equal_range(std::make_tuple(it->btcTxHeight, it->btcTxHash));
        if (std::distance(it0,it1) >= quorum) {
            result.clear();
            for (; result.size() < quorum && it0 != it1; ++it0) {
                if (team.find(it0->GetSigner()) != team.end()) {
                    result.push_back(*it0);
                }
            }
            if (result.size() == quorum) {
                return result;
            }
        }
        it = it1; // next group!
    }
    return {};
}

void CAnchorAwaitingConfirms::ForEachConfirm(std::function<void (const CAnchorAwaitingConfirms::Confirm &)> callback) const
{
    AssertLockHeld(cs_main);
    auto const & list = confirms.get<Confirm::ByKey>();
    for (auto it = list.begin(); it != list.end(); ++it)
        callback(*it);
}

bool GetAnchorEmbeddedData(const CKeyID& data, uint64_t& anchorCreationHeight, std::shared_ptr<std::vector<unsigned char>>& prefix)
{
    // Blockhash prefix length
    size_t prefixLength{CKeyID().size() - (spv::BtcAnchorMarker.size() * sizeof(uint8_t)) - sizeof(uint64_t)};

    std::vector<uint8_t> marker(spv::BtcAnchorMarker.size(), 0);
    prefix = std::make_shared<std::vector<unsigned char>>(prefixLength);

    // Get marker
    memcpy(marker.data(), &(*data.begin()), spv::BtcAnchorMarker.size());

    if (marker != spv::BtcAnchorMarker) {
        return false;
    }

    size_t offset{spv::BtcAnchorMarker.size()};
    memcpy(&anchorCreationHeight, &(*(data.begin() + offset)), sizeof(uint64_t));
    offset += sizeof(uint64_t);
    memcpy(prefix->data(), &(*(data.begin() + offset)), prefixLength);

    return true;
}

namespace spv
{
const PendingOrderType PendingOrder = PendingOrderType([](const CAnchorIndex::AnchorRec& a, const CAnchorIndex::AnchorRec& b)
{
    if (a.btcHeight == b.btcHeight)
    {
        if (a.anchor.height == b.anchor.height)
        {
            if (a.anchor.height >= static_cast<THeight>(Params().GetConsensus().EunosHeight))
            {
                const auto blockHash = panchors->ReadBlockHash(a.btcHeight);
                auto aHash = Hash(a.txHash.begin(), a.txHash.end(), blockHash.begin(), blockHash.end());
                auto bHash = Hash(b.txHash.begin(), b.txHash.end(), blockHash.begin(), blockHash.end());
                return aHash < bHash;
            }

            return a.txHash < b.txHash;
        }

        // Higher DeFi comes first
        return a.anchor.height > b.anchor.height;
    }

    return a.btcHeight < b.btcHeight;
});
}
