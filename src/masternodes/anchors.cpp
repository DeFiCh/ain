// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/anchors.h>

#include <chainparams.h>
#include <key.h>
#include <logging.h>
#include <streams.h>
#include <script/standard.h>
#include <spv/spv_wrapper.h>
#include <util/system.h>
#include <validation.h>

// for anchor processing
#include <wallet/wallet.h>

#include <algorithm>
#include <functional>

#include <tuple>

std::unique_ptr<CAnchorAuthIndex> panchorauths;
std::unique_ptr<CAnchorIndex> panchors;

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

uint256 CAnchorAuthMessage::GetSignHash() const
{
    CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
    ss << previousAnchor << height << blockHash << nextTeam; // << salt_;
    return Hash(ss.begin(), ss.end());
}

CAnchor CAnchor::Create(const std::vector<CAnchorAuthMessage> & auths, CTxDestination const & rewardDest)
{
    // assumed here that all of the auths are uniform, were checked for sigs and consensus has been reached!
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

const CAnchorAuthIndex::Auth * CAnchorAuthIndex::ExistAuth(uint256 const & hash) const
{
    auto & list = auths.get<Auth::ByMsgHash>();
    auto it = list.find(hash);
    return it != list.end() ? &(*it) : nullptr;
}

bool CAnchorAuthIndex::ValidateAuth(const CAnchorAuthIndex::Auth & auth) const
{
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
    /// @todo @max check that blockHash is in active chain?


    // 3. team context and signs:
    CTeam const team = panchors->GetNextTeam(auth.previousAnchor);
    if (team.empty()) {
        return error("%s: Can't get team for previousAnchor tx %s !", __func__, auth.previousAnchor.ToString());
    }
    /// @todo @max check that auth next team calculated correctly. add log
    if (team != pmasternodesview->CalcNextTeam()) {
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

    /// @todo @maxb should we match here:
    /// 3. and that previous anchor is active for THIS chain?

    return true;
}

bool CAnchorAuthIndex::AddAuth(const CAnchorAuthIndex::Auth & auth)
{
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
CAnchor CAnchorAuthIndex::CreateBestAnchor(CTxDestination const & rewardDest, uint256 const & forBlock) const
{
    /// @todo @maxb forBlock is ignored by now, possible implement
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
                    /// @todo @maxb do we need for an extra check of the auth signature here?
                    freshestConsensus.push_back(*it0);
                }
                break;
            }
        }
    }

    return CAnchor::Create(freshestConsensus, rewardDest);
}


static const char DB_ANCHORS = 'A';

CAnchorIndex::CAnchorIndex(size_t nCacheSize, bool fMemory, bool fWipe)
    : db(new CDBWrapper(GetDataDir() / "anchors", nCacheSize, fMemory, fWipe))
{
}

bool CAnchorIndex::Load()
{
    AnchorIndexImpl().swap(anchors);

    std::function<void (uint256 const &, AnchorRec &)> onLoad = [this] (uint256 const &, AnchorRec & rec) {
        // just for debug
        LogPrintf("anchor load: blockHash: %s, height %d, btc height: %d\n", rec.anchor.blockHash.ToString(), rec.anchor.height, rec.btcHeight);

        anchors.insert(std::move(rec));
    };
    bool result = IterateTable(DB_ANCHORS, onLoad);
    if (result)
        ActivateBestAnchor();

    return result;
}


const CAnchorIndex::AnchorRec * CAnchorIndex::GetActiveAnchor() const
{
    return top;
}

const CAnchorIndex::AnchorRec * CAnchorIndex::ExistAnchorByTx(const uint256 & hash) const
{
    AnchorIndexImpl const & index = anchors;
    auto & list = index.get<AnchorRec::ByBtcTxHash>();
    auto it = list.find(hash);
    return it != list.end() ? &(*it) : nullptr;
}

bool CAnchorIndex::AddAnchor(CAnchor const & anchor, uint256 const & btcTxHash, THeight btcBlockHeight)
{
    AnchorIndexImpl & index = anchors;
    AnchorRec rec{ anchor, btcTxHash, btcBlockHeight };
    bool result = index.insert(rec).second;
    if (result)
        DbWrite(rec);
    return result;
}

bool CAnchorIndex::DeleteAnchorByBtcTx(const uint256 & btcTxHash)
{
    typedef AnchorIndexImpl::index<AnchorRec::ByBtcTxHash>::type KList;
    KList & list = anchors.get<AnchorRec::ByBtcTxHash>();

    auto const it = list.find(btcTxHash);
    if (it != list.end()) {
        if (top && top == &*it)
        {
            top = GetAnchorByBtcTx(top->anchor.previousAnchor);
            /// @todo @maxb call for ActivateBestAnchor() here?
        }
        list.erase(it);
        if (DbExists(btcTxHash))
            DbErase(btcTxHash);
        return true;
    }
    return false;
}

CMasternodesView::CTeam CAnchorIndex::GetNextTeam(const uint256 & btcPrevTx) const
{
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
    if (!anchor)
        return Params().GetGenesisTeam();

    return GetNextTeam(anchor->anchor.previousAnchor);
}

CAnchorIndex::AnchorRec const * CAnchorIndex::GetAnchorByBtcTx(uint256 const & txHash) const
{
    typedef AnchorIndexImpl::index<AnchorRec::ByBtcTxHash>::type KList;
    KList const & list = anchors.get<AnchorRec::ByBtcTxHash>();

    auto const it = list.find(txHash);
    return it != list.end() ? &*it : nullptr;
}

int CAnchorIndex::GetAnchorConfirmations(uint256 const & txHash) const
{
    return GetAnchorConfirmations(GetAnchorByBtcTx(txHash));
}

int CAnchorIndex::GetAnchorConfirmations(const CAnchorIndex::AnchorRec * rec) const
{
    if (!rec) {
        return -1;
    }
    uint32_t const spvLastBlock = spv::pspv->GetLastBlockHeight();
    // for cases when tx->blockHeight == TX_UNCONFIRMED _or_ GetLastBlockHeight() less than already _confirmed_ tx (rescan in progress)
    return spvLastBlock < rec->btcHeight ? 0 : spvLastBlock - rec->btcHeight + 1;
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

void CAnchorIndex::ActivateBestAnchor()
{
    typedef AnchorIndexImpl::index<AnchorRec::ByBtcHeight>::type KList;
    KList const & list = anchors.get<AnchorRec::ByBtcHeight>();

    THeight topHeight = top ? top->btcHeight : 0;
    // special case for the first iteration to reselect top
    uint256 prev = top ? top->anchor.previousAnchor : uint256{};

    // start running from top anchor height
    // (yes, it can re-select different best anchor on the "current" top anchor level - for the cases if spv not feed txs of one block "at once")
    for (auto it = (topHeight == 0 ? list.begin() : list.find(topHeight)); it != list.end(); ) {

        if (GetAnchorConfirmations(&*it) < 6)
            break;

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

