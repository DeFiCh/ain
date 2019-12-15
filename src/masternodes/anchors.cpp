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
#include <net_processing.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <functional>

#include <tuple>

std::unique_ptr<CAnchorAuthIndex> panchorauths;
std::unique_ptr<CAnchorIndex> panchors;
std::unique_ptr<CAnchorConfirms> panchorconfirms;

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
    ss << previousAnchor << height << blockHash; // << salt_;
    return Hash(ss.begin(), ss.end());
}

CAnchorMessage CAnchorMessage::Create(const std::vector<CAnchorAuthMessage> & auths, CScript const & rewardScript)
{
    // assumed here that all of the auths are uniform, were checked for sigs and consensus has been reached!
    CAnchorMessage anchor;
    if (auths.size() > 0) {
        anchor.previousAnchor = auths[0].previousAnchor;
        anchor.height = auths[0].height;
        anchor.blockHash = auths[0].blockHash;
        anchor.nextTeam = auths[0].nextTeam;

        for (size_t i = 0; i < auths.size(); ++i) {
            anchor.sigs.push_back(auths[i].GetSignature());
        }
        anchor.rewardScript = rewardScript;
    }
    return anchor;
}

uint256 CAnchorMessage::GetHash() const
{
    CDataStream ss{SER_NETWORK, PROTOCOL_VERSION};
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

bool CAnchorMessage::CheckAuthSigs(CTeam const & team) const
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
    /// @todo @max check that blockHash is in active chain!!!


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

CAnchorMessage CAnchorAuthIndex::CreateBestAnchor(uint256 const & forBlock, CScript const & rewardScript) const
{
    /// @todo @maxb forBlock is ignored by now, possible implement
    // get freshest consensus:
    typedef Auths::index<Auth::ByKey>::type KList;
    KList const & list = auths.get<Auth::ByKey>();

    std::vector<Auth> freshestConsensus;
    THeight curHeight = 0;
    uint256 curHash = {};
    LogPrintf("auths total size: %d\n", list.size());

    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        LogPrintf("auths: debug %d, %s, %s\n", it->height, it->blockHash.ToString(), it->GetHash().ToString());
        if (curHeight != it->height || curHash != it->blockHash) {
            curHeight = it->height;
            curHash = it->blockHash;
            auto count = list.count(std::make_tuple(curHeight, curHash));
            /// @todo @maxb replace pmasternodesview->GetCurrentTeam() with actual team
            if (count >= GetMinAnchorQuorum(panchors->GetCurrentTeam(panchors->GetActiveAnchor()))) {
                KList::iterator it0, it1;
                std::tie(it0,it1) = list.equal_range(std::make_tuple(curHeight, curHash));
                while(it0 != it1) {
                    LogPrintf("auths: pick up %d, %s, %s\n", it0->height, it0->blockHash.ToString(), it0->GetHash().ToString());
                    freshestConsensus.push_back(*it0);
                    ++it0;
                }
                break;
            }
        }

    }
    return CAnchorMessage::Create(freshestConsensus, rewardScript);
}

CAnchorConfirmMessage CAnchorConfirmMessage::Create(CAnchorMessage const & anchorMessage, CKey const & key)
{
    CAnchorConfirmMessage message;
    message.hashAnchorMessage = anchorMessage.GetHash();

    if (!key.SignCompact(message.hashAnchorMessage, message.signature)) {
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

const CAnchorConfirmMessage *CAnchorConfirms::Exist(uint256 const &hash)
{
    auto it = confirms.find(hash);
    return it != confirms.end() ? &(it->second) : nullptr;
}

bool CAnchorConfirms::Validate(CAnchorConfirmMessage const &message)
{
    return true;
}

void CAnchorConfirms::Add(CAnchorConfirmMessage const &newMessage)
{
    confirms.insert(std::make_pair(newMessage.GetHash(), newMessage));
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
        LogPrintf("anchor load: msg: %s, btc height: %d\n", rec.msgHash.ToString(), rec.btcHeight);

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

const CAnchorIndex::AnchorRec * CAnchorIndex::ExistAnchorByMsg(const uint256 & hash, bool isMainIndex) const
{
    AnchorIndexImpl const & index = isMainIndex ? anchors : orphans;
    auto & list = index.get<AnchorRec::ByMsgHash>();
    auto it = list.find(hash);
    return it != list.end() ? &(*it) : nullptr;
}

const CAnchorIndex::AnchorRec * CAnchorIndex::ExistAnchorByTx(const uint256 & hash, bool isMainIndex) const
{
    AnchorIndexImpl const & index = isMainIndex ? anchors : orphans;
    auto & list = index.get<AnchorRec::ByBtcTxHash>();
    auto it = list.find(hash);
    return it != list.end() ? &(*it) : nullptr;
}

bool CAnchorIndex::AddAnchor(CAnchorMessage const & msg, spv::BtcAnchorTx const * btcTx, ConfirmationSigs const & confirm_sigs, bool isMainIndex)
{
    AnchorIndexImpl & index = isMainIndex ? anchors : orphans;
    AnchorRec rec{ msg, msg.GetHash(), btcTx->txHash, btcTx->blockHeight, btcTx->txIndex, confirm_sigs };
    bool result = index.insert(rec).second;
    if (isMainIndex)
        DbWrite(rec);
    return result;
}

bool CAnchorIndex::UpdateAnchorConfirmations(AnchorRec const * rec, ConfirmationSigs const & confirm_sigs)
{

    if (CheckSigs(rec->msgHash, confirm_sigs, GetCurrentTeam(rec))) {
        AnchorRec new_rec(*rec);
        new_rec.confirm_sigs.insert(confirm_sigs.begin(), confirm_sigs.end());

        auto & list = anchors.get<AnchorRec::ByMsgHash>();
        list.erase(rec->msgHash);
        anchors.insert(new_rec);
        DbWrite(new_rec);
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

void CAnchorIndex::ActivateBestAnchor()
{

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
    return db->Write(std::make_pair(DB_ANCHORS, rec.msgHash), rec);
}

bool CAnchorIndex::DbErase(uint256 const & hash)
{
    return db->Erase(std::make_pair(DB_ANCHORS, hash));
}



/// Validate NOT ORPHANED anchor (anchor that has valid previousAnchor or null(first) )
bool ValidateAnchor(const CAnchorMessage & anchor, CAnchorIndex::ConfirmationSigs const & conf_sigs, bool noThrow)
{
    uint256 const msgHash = anchor.GetHash();
    try {
        // common: check heights
        if (!anchor.previousAnchor.IsNull()) {
            auto prev = panchors->ExistAnchorByTx(anchor.previousAnchor);

            assert (prev); // we should not got here with empty prev -> should be added to orphans before

            if (anchor.height <= prev->anchor.height) {
                throw std::runtime_error("Anchor blockHeight should be higher than previousAnchor height! " + std::to_string(anchor.height) + " > " + std::to_string(prev->anchor.height) + " !");
            }
        }

        // team context:
        // current team for THIS message extracted from PREV anchor message, overwise "genesis" team
        CMasternodesView::CTeam curTeam = panchors->GetNextTeam(anchor.previousAnchor);
        assert(!curTeam.empty()); // we should not got empty team with valid prev!

        if (!anchor.CheckAuthSigs(curTeam)) {
            throw std::runtime_error("Message auth sigs doesn't match current team (extracted from previousAnchor)");
        }
        // check confirmation sigs
        if (!CheckSigs(msgHash, conf_sigs, curTeam)) {
            throw std::runtime_error("Message confirmation sigs doesn't match current team (extracted from previousAnchor)");
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

bool TryToVoteAnchor(CAnchorIndex::AnchorRec const * anchor_rec, CConnman& connman)
{
    if (!anchor_rec)
        return false;

    // if this anchor not older than top->prev
    uint64_t prevBtcHeight{0};
    auto top = panchors->GetActiveAnchor();
    if (top) {
        auto prevTxHash = top->anchor.previousAnchor;
        if (!prevTxHash.IsNull()) {
            prevBtcHeight = panchors->ExistAnchorByTx(prevTxHash)->GetBtcHeight();
        }
    }

    if (anchor_rec->GetBtcHeight() > prevBtcHeight) {
        // VOTE?
        auto team = panchors->GetNextTeam(anchor_rec->anchor.previousAnchor);
        auto const mnId = pmasternodesview->AmIOperator();
        /// @todo @maxb should we check for IsActive() here? i think it is redundant
        if (mnId && pmasternodesview->ExistMasternode(mnId->id)->IsActive() && team.find(mnId->operatorAuthAddress) != team.end()) { // this is safe due to prev call `AmIOperator`

            CKey masternodeKey = GetWalletsKey(mnId->operatorAuthAddress);
            if (!masternodeKey.IsValid()) {
                return error("%s: Can't read masternode operator private key", __func__);
            }
            CAnchorConfirmMessage confirmMessage = CAnchorConfirmMessage::Create(anchor_rec->anchor, masternodeKey);
            if (!confirmMessage.signature.empty()) {
                RelayAnchorConfirm(confirmMessage.GetHash(), connman, nullptr);

                panchors->UpdateAnchorConfirmations(anchor_rec, {confirmMessage.signature});
            }
        }
    }
    return true;
}

bool ProcessNewAnchor(CAnchorMessage const & anchor, CAnchorIndex::ConfirmationSigs const & conf_sigs, bool noThrow, CConnman& connman, CNode* skipNode)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(spv::pspv->GetCS());

    uint256 const msgHash{anchor.GetHash()};

    // checking spv txs:
    spv::BtcAnchorTx const * spv_anc = spv::pspv->CheckConfirmations(anchor.previousAnchor, msgHash, noThrow);
    if (!spv_anc)
        return false;

    /// @todo @maxb there should be NO orphans due to sequental request/response!
    if (!anchor.previousAnchor.IsNull() && !panchors->ExistAnchorByTx(anchor.previousAnchor)) {
//        // add to ORPHANS, unchecked!
//        panchors->AddAnchor(anchor, spv_anc, conf_sigs, false);

//        /// @todo @maxb relay orphaned or does NOT???
//        // return true;
        return false;
    }
    else
    {
        if (!ValidateAnchor(anchor, conf_sigs, noThrow))
            return false;

        if (panchors->AddAnchor(anchor, spv_anc, conf_sigs)) {
            RelayAnchor(msgHash, connman, skipNode);
        }

        /// @todo @maxb check and move orphans where orphan.previousAnchor == this spv_anc.txHash
//        panchors->ConnectOrphans(anchor.previousAnchor);

        /// @todo @maxb WHERE VOTING SHOULD BE PLACED?????
        /// ? after addition and relaying cause anchor does not exist on the other nodes
        /// @todo @maxb how to deny voting on old anchors??
        if (!spv::pspv->IsInitialSync()) {
            TryToVoteAnchor(panchors->ExistAnchorByMsg(msgHash), connman);
        }
    }

    // new request to index here cause confirm_sigs.size() could be changed by voting
    if (panchors->ExistAnchorByMsg(msgHash)->confirm_sigs.size() >= GetMinAnchorQuorum(panchors->GetNextTeam(anchor.previousAnchor))) {
        /// @todo @maxb implement
        // TRIGGER "Confirmed" - replay from this point
        panchors->ActivateBestAnchor();
    }


    return true;
}

