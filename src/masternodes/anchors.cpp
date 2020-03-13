// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/anchors.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <key.h>
#include <logging.h>
#include <streams.h>
#include <script/standard.h>
#include <spv/spv_wrapper.h>
#include <util/system.h>
#include <util/validation.h>
#include <validation.h>

#include <algorithm>

#include <tuple>

std::unique_ptr<CAnchorAuthIndex> panchorauths;
std::unique_ptr<CAnchorIndex> panchors;
std::unique_ptr<CAnchorAwaitingConfirms> panchorAwaitingConfirms;

template <typename TContainer>
bool CheckSigs(uint256 const & sigHash, TContainer const & sigs, std::set<CKeyID> const & keys)
{
    for (auto const & sig : sigs) {
        CPubKey pubkey;
        if (!pubkey.RecoverCompact(sigHash, sig) || keys.find(pubkey.GetID()) == keys.end())
            return false;
    }
    return true;
}

CAnchorAuthMessage::CAnchorAuthMessage(uint256 const & previousAnchor, int height, uint256 const & hash, CTeam const & nextTeam)
    : previousAnchor(previousAnchor)
    , height(height)
    , blockHash(hash)
    , nextTeam(nextTeam)
{
    signature.clear();
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

uint256 CAnchorAuthMessage::GetSignHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << previousAnchor << height << blockHash << nextTeam; // << salt_;
    return Hash(ss.begin(), ss.end());
}

CAnchor CAnchor::Create(const std::vector<CAnchorAuthMessage> & auths, CTxDestination const & rewardDest)
{
    // assumed here that all of the auths are uniform, were checked for sigs and consensus has been reached!
    assert(rewardDest.which() == 1 || rewardDest.which() == 4);
    CAnchor anchor;
    if (auths.size() > 0) {
        anchor.previousAnchor = auths[0].previousAnchor;
        anchor.height = auths[0].height;
        anchor.blockHash = auths[0].blockHash;
        anchor.nextTeam = auths[0].nextTeam;

        for (size_t i = 0; i < auths.size(); ++i) {
            anchor.sigs.push_back(auths[i].GetSignature());
        }
        anchor.rewardKeyID = rewardDest.which() == 1 ? CKeyID(*boost::get<PKHash>(&rewardDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&rewardDest));
        anchor.rewardKeyType = rewardDest.which();
    }
    return anchor;
}

bool CAnchor::CheckAuthSigs(CTeam const & team) const
{
    // create tmp auth
    CAnchorAuthMessage auth(previousAnchor, height, blockHash, nextTeam);
    return CheckSigs(auth.GetSignHash(), sigs, team);
}

const CAnchorAuthIndex::Auth * CAnchorAuthIndex::ExistAuth(uint256 const & msgHash) const
{
    AssertLockHeld(cs_main);

    auto const & list = auths.get<Auth::ByMsgHash>();
    auto const it = list.find(msgHash);
    return it != list.end() ? &(*it) : nullptr;
}

const CAnchorAuthIndex::Auth * CAnchorAuthIndex::ExistVote(const uint256 & signHash, const CKeyID & signer) const
{
    AssertLockHeld(cs_main);

    auto const & list = auths.get<Auth::ByVote>();
    auto const it = list.find(std::make_tuple(signHash, signer));
    return it != list.end() ? &(*it) : nullptr;
}

bool CAnchorAuthIndex::ValidateAuth(const CAnchorAuthIndex::Auth & auth) const
{
    AssertLockHeld(cs_main);

    // 1. common (prev and top checks)
    if (!auth.previousAnchor.IsNull()) {
        auto prev = panchors->ExistAnchorByTx(auth.previousAnchor);
        if (!prev) {
            return error("%s: Got anchor auth, hash %s, blockheight: %d, but can't find previousAnchor %s", __func__, auth.GetHash().ToString(), auth.height, auth.previousAnchor.ToString());
        }
        if (auth.height <= prev->anchor.height) {
            return error("%s: Auth blockHeight should be higher than previousAnchor height! %d > %d !", __func__, auth.height, prev->anchor.height);
        }
    }
    auto const * topAnchor = panchors->GetActiveAnchor();
    if (topAnchor && auth.height <= topAnchor->anchor.height) {
        return error("%s: Auth blockHeight should be higher than top anchor height! %d > %d !", __func__, auth.height, topAnchor->anchor.height);
    }

    // 2. chain context:
    // we not check that blockHash is in active chain due to they wouldn't be signed with current team

    // 3. team context and signs:
    CTeam const team = panchors->GetNextTeam(auth.previousAnchor);
    if (team.empty()) {
        return error("%s: Can't get team for previousAnchor tx %s !", __func__, auth.previousAnchor.ToString());
    }

    CBlockIndex* block = ::ChainActive()[auth.height];
    if (block == nullptr) {
        return error("%s: Can't get block from height: %d !", __func__, auth.height);
    }

    if (auth.nextTeam != pmasternodesview->CalcNextTeam(block->stakeModifier)) {
        return error("%s: Wrong nextTeam for auth %s!!!", __func__, auth.GetHash().ToString());
    }

    CPubKey pubKey;
    if (!auth.GetPubKey(pubKey)) {
        return error("%s: Can't recover pubkey from sig, auth: ", __func__, auth.GetHash().ToString());
    }
    const CKeyID masternodeKey{pubKey.GetID()};
    if (team.find(masternodeKey) == team.end()) {
        return error("%s: Recovered keyID %s is not a current team member!", __func__, masternodeKey.ToString());
    }

    return true;
}

bool CAnchorAuthIndex::AddAuth(const CAnchorAuthIndex::Auth & auth)
{
    AssertLockHeld(cs_main);
    return auths.insert(auth).second;
}

uint32_t GetMinAnchorQuorum(CMasternodesView::CTeam const & team)
{
    if (Params().NetworkIDString() == "regtest") {
        return 1;
    }
    return  static_cast<uint32_t>(1 + (team.size() * 2) / 3); // 66% + 1
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
    LogPrintf("auths total size: %d\n", list.size());

    auto const topAnchor = panchors->GetActiveAnchor();
    auto const topTeam = panchors->GetCurrentTeam(topAnchor);
    uint32_t quorum = GetMinAnchorQuorum(topTeam);
    auto const topHeight = topAnchor ? topAnchor->anchor.height : 0;

    std::vector<Auth> freshestConsensus;
    THeight curHeight = 0;
    uint256 curSignHash = {};

    // get freshest consensus:
    for (auto it = list.rbegin(); it != list.rend() && it->height > topHeight; ++it) {
        LogPrintf("auths: debug %d, %s, %s\n", it->height, it->blockHash.ToString(), it->GetHash().ToString());
        if (topAnchor && topAnchor->txHash != it->previousAnchor)
            continue;

        if (curHeight != it->height || curSignHash != it->GetSignHash()) {
            // got next group of auths
            curHeight = it->height;
            curSignHash = it->GetSignHash();

            // we doesn't choose here between "equal" valid auths (by max quorum nor by prevTeam or anything else)
            auto count = list.count(std::make_tuple(curHeight, curSignHash));
            if (count >= quorum) {
                KList::iterator it0, it1;
                std::tie(it0,it1) = list.equal_range(std::make_tuple(curHeight, curSignHash));
                for (uint32_t i = 0; i < quorum && it0 != it1; ++i, ++it0) {
                    LogPrintf("auths: pick up %d, %s, %s\n", it0->height, it0->blockHash.ToString(), it0->GetHash().ToString());
                    /// @todo do we need for an extra check of the auth signature here?
                    freshestConsensus.push_back(*it0);
                }
                break;
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

static const char DB_ANCHORS = 'A';

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
        LogPrintf("anchor load: blockHash: %s, height %d, btc height: %d\n", rec.anchor.blockHash.ToString(), rec.anchor.height, rec.btcHeight);

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

void CAnchorIndex::ForEachAnchorByBtcHeight(std::function<void(const CAnchorIndex::AnchorRec &)> callback) const
{
    typedef AnchorIndexImpl::index<AnchorRec::ByBtcHeight>::type KList;
    KList const & list = anchors.get<AnchorRec::ByBtcHeight>();
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        callback(*it);

}

const CAnchorIndex::AnchorRec * CAnchorIndex::GetActiveAnchor() const
{
    return top;
}

const CAnchorIndex::AnchorRec * CAnchorIndex::ExistAnchorByTx(const uint256 & hash) const
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

CMasternodesView::CTeam CAnchorIndex::GetNextTeam(const uint256 & btcPrevTx) const
{
    AssertLockHeld(cs_main);

    if (btcPrevTx.IsNull())
        return Params().GetGenesisTeam();

    AnchorRec const * prev = ExistAnchorByTx(btcPrevTx);
    if (!prev) {
        LogPrintf("Can't get previous anchor with btc hash %s\n",  btcPrevTx.ToString());
        return CMasternodesView::CTeam{};
    }
    return prev->anchor.nextTeam;
}

CMasternodesView::CTeam CAnchorIndex::GetCurrentTeam(const CAnchorIndex::AnchorRec * anchor) const
{
    AssertLockHeld(cs_main);

    if (!anchor)
        return Params().GetGenesisTeam();

    return GetNextTeam(anchor->anchor.previousAnchor);
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

    // custom comparator for set_difference
    struct cmp {
        bool operator()(uint256 const & a, std::pair<uint256 const, uint256> const & b) const {
            return a < b.first;
        }
        bool operator()(std::pair<uint256 const, uint256> const & a, uint256 const & b) const {
            return a.first < b;
        }
    };

    // find unrewarded
    UnrewardedResult result;
    auto const rewards = pmasternodesview->ListAnchorRewards();
    std::set_difference(confirmed.begin(), confirmed.end(), rewards.begin(), rewards.end(), std::inserter(result, result.end()), cmp());

    return result;
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

void CAnchorIndex::CheckActiveAnchor(bool forced)
{
    // we should avoid slow operations on exit, especially ActivateBestChain
    if (ShutdownRequested()) return;

    bool topChanged{false};
    {
        // fix spv height to avoid datarace while choosing best anchor
        uint32_t const tmp = spv::pspv ? spv::pspv->GetLastBlockHeight() : 0;
        LOCK(cs_main);
        spvLastHeight = tmp;
        topChanged = panchors->ActivateBestAnchor(forced);

        // prune auths older than anchor with 6 confirmations. Warning! This constant are using for start confirming reward too!
        auto it = panchors->GetActiveAnchor();
        for (; it && GetAnchorConfirmations(it) < 6; it = panchors->GetAnchorByBtcTx(it->anchor.previousAnchor))
            ;
        if (it)
            panchorauths->PruneOlderThan(it->anchor.height+1);

        /// @todo panchorAwaitingConfirms - optimize?
        /// @attention - 'it' depends on previous loop conditions (>=6).
        if (!::ChainstateActive().IsInitialBlockDownload()) {
//            for (; it; it = panchors->GetAnchorByBtcTx(it->anchor.previousAnchor)) {
//                if (pmasternodesview->GetRewardForAnchor(it->txHash) == uint256{}) {
//                    pmasternodesview->CreateAndRelayConfirmMessageIfNeed(it->anchor, it->txHash);
//                }
//            }

            panchorAwaitingConfirms->ReVote();

        }
    }
    CValidationState state;
    if (topChanged && !ActivateBestChain(state, Params())) {
        throw std::runtime_error(strprintf("CheckActiveAnchor: ActivateBestChain failed. (%s)", FormatStateMessage(state)));
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

    if (a1->anchor.height > a2->anchor.height)
        return a1;
    else if (a1->anchor.height < a2->anchor.height)
        return a2;
    // if heights are equal, return anchor with less btc tx hash
    else if (a1->txHash < a2->txHash)
        return a1;
    return a2;
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

        KList::iterator it0, it1;
        std::tie(it0,it1) = list.equal_range(it->btcHeight);
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

/// Validates all except tx confirmations
bool ValidateAnchor(const CAnchor & anchor, bool noThrow)
{
    AssertLockHeld(cs_main);
    try {
        // common: check heights and prevs
        if (!anchor.previousAnchor.IsNull()) {
            auto prev = panchors->ExistAnchorByTx(anchor.previousAnchor);

            if (!prev) {
                // I think this should not happen cause auth sigs
                throw std::runtime_error("Previous anchor " + anchor.previousAnchor.ToString() + " specified, but does not exist!");
            }

            if (anchor.height <= prev->anchor.height) {
                // I think this should not happen cause auth sigs
                throw std::runtime_error("Anchor blockHeight should be higher than previousAnchor height! " + std::to_string(anchor.height) + " > " + std::to_string(prev->anchor.height) + " !");
            }
        }

        // team context:
        // current team for THIS message extracted from PREV anchor message, overwise "genesis" team
        CMasternodesView::CTeam curTeam = panchors->GetNextTeam(anchor.previousAnchor);
        assert(!curTeam.empty()); // we should not get empty team with valid prev!

        if (!anchor.CheckAuthSigs(curTeam)) {
            throw std::runtime_error("Message auth sigs doesn't match current team (extracted from previousAnchor)");
        }

    } catch (std::runtime_error const & e) {
        if (noThrow) {
            LogPrintf("%s\n", e.what());
            return false;
        }
        throw e;
    }
    return true;
}

CAnchorConfirmMessage CAnchorConfirmMessage::Create(THeight anchorHeight, CKeyID const & rewardKeyID, char rewardKeyType, THeight prevAnchorHeight, uint256 btcTxHash)
{
    CAnchorConfirmMessage message;

    message.btcTxHash = btcTxHash;
    message.anchorHeight = anchorHeight;
    message.prevAnchorHeight = prevAnchorHeight;
    message.rewardKeyID = rewardKeyID;
    message.rewardKeyType = rewardKeyType;

    return message;
}

CAnchorConfirmMessage CAnchorConfirmMessage::Create(CAnchor const & anchor, THeight prevAnchorHeight, uint256 btcTxHash, CKey const & key)
{
    CAnchorConfirmMessage message = CAnchorConfirmMessage::Create(anchor.height, anchor.rewardKeyID, anchor.rewardKeyType, prevAnchorHeight, btcTxHash);
    if (!key.SignCompact(message.GetSignHash(), message.signature)) {
        message.signature.clear();
    }
    return message;
}

uint256 CAnchorConfirmMessage::GetSignHash() const
{
    CDataStream ss{SER_GETHASH, 0};
    ss << btcTxHash << anchorHeight << prevAnchorHeight << rewardKeyID << rewardKeyType;
    return Hash(ss.begin(), ss.end());
}

bool CAnchorConfirmMessage::CheckConfirmSigs(std::vector<Signature> const & sigs, CMasternodesView::CTeam team)
{
    return CheckSigs(GetSignHash(), sigs, team);
}

bool CAnchorConfirmMessage::isEqualDataWith(const CAnchorConfirmMessage &message) const
{
    return  btcTxHash == message.btcTxHash &&
            anchorHeight == message.anchorHeight &&
            prevAnchorHeight == message.prevAnchorHeight &&
            rewardKeyID == message.rewardKeyID &&
            rewardKeyType == message.rewardKeyType;
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

bool CAnchorAwaitingConfirms::EraseAnchor(AnchorTxHash const &txHash)
{
    AssertLockHeld(cs_main);

    auto & list = confirms.get<Confirm::ByMsgHash>();
    auto count = list.erase(txHash); // should erase ALL with that key. Check it!
    LogPrintf("AnchorConfirms::EraseAnchor: erase %d confirms for anchor %s\n", count, txHash.ToString());

    return count > 0;
}

const CAnchorConfirmMessage *CAnchorAwaitingConfirms::Exist(ConfirmMessageHash const &msgHash) const
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
        LogPrintf("AnchorConfirms::Validate: Warning! Signature incorrect. btcTxHash: %s confirmMessageHash: %s Key: %s\n", confirmMessage.btcTxHash.ToString(), confirmMessage.GetHash().ToString(), signer.ToString());
        return false;
    }
    auto it = pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, signer);
    if (!it || !pmasternodesview->ExistMasternode((*it)->second)->IsActive()) {
        LogPrintf("AnchorConfirms::Validate: Warning! Masternode with operator key %s does not exist or not active!\n", signer.ToString());
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

    auto myIDs = pmasternodesview->AmIOperator();
    if (myIDs && pmasternodesview->ExistMasternode(myIDs->id)->IsActive()) {
        auto const & currentTeam = pmasternodesview->GetCurrentTeam();
        if (currentTeam.find(myIDs->operatorAuthAddress) != currentTeam.end()) {

            CAnchorIndex::UnrewardedResult unrewarded = panchors->GetUnrewarded();
            for (auto const & btcTxHash : unrewarded) {
                /// @todo non-optimal! (secondary checks of amI, keys etc...)
                pmasternodesview->CreateAndRelayConfirmMessageIfNeed(panchors->ExistAnchorByTx(btcTxHash)->anchor, btcTxHash);
            }
        }
    }
}

// for MINERS only!
std::vector<CAnchorConfirmMessage> CAnchorAwaitingConfirms::GetQuorumFor(const CMasternodesView::CTeam & team) const
{
    AssertLockHeld(cs_main);

    typedef Confirms::index<Confirm::ByKey>::type KList;
    KList const & list = confirms.get<Confirm::ByKey>();
    uint32_t quorum = GetMinAnchorQuorum(team);

    std::vector<CAnchorConfirmMessage> result;

    for (auto it = list.begin(); it != list.end(); /* w/o advance! */) {
        // get next group of confirms
        KList::iterator it0, it1;
        std::tie(it0,it1) = list.equal_range(std::make_tuple(it->btcTxHash, it->GetSignHash()));
        if (std::distance(it0,it1) >= quorum) {
            result.clear();
            for (; result.size() < quorum && it0 != it1; ++it0) {
                if (team.find(it0->GetSigner()) != team.end()) {
                    LogPrintf("GetQuorumFor: pick up confirm vote by %s for %s, defiHeight %d\n", it0->GetSigner().ToString(), it0->btcTxHash.ToString(), it0->anchorHeight);
                    result.push_back(*it0);
                }
            }
            if (result.size() == quorum) {
                LogPrintf("GetQuorumFor: get valid group of confirmations for %s, defiHeight %d\n", result[0].btcTxHash.ToString(), result[0].anchorHeight);
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
