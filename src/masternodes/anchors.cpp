// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/anchors.h>

#include <chainparams.h>
#include <key.h>
#include <logging.h>
#include <streams.h>
#include <script/standard.h>
#include <validation.h>

#include <algorithm>
#include <functional>

#include <tuple>

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

const CAnchorAuthIndex::Auth * CAnchorAuthIndex::ExistAuth(uint256 const & hash) const
{
    auto & list = auths.get<Auth::ByMsgHash>();
    auto it = list.find(hash);
    return it != list.end() ? &(*it) : nullptr;
}

bool CAnchorAuthIndex::ValidateAuth(const CAnchorAuthIndex::Auth & auth) const
{
    /// @todo @max implement
    CPubKey pubKey;

    if (!auth.GetPubKey(pubKey)) {
        return error("%s: Can't recover pubkey from sig, auth: ", __func__, auth.GetHash().ToString());
    }
    const CKeyID masternodeKey{pubKey.GetID()};
    /// @todo @maxb or maybe more valid 'GetNextTeamFromAnchor(auth.previousAnchor)'
    CTeam const team = pmasternodesview->GetCurrentTeam();

    if (team.find(masternodeKey) == team.end()) {
        return error("%s: Recovered keyID %s is not a current team member!", __func__, masternodeKey.ToString());
    }
    return true;
}

bool CAnchorAuthIndex::AddAuth(const CAnchorAuthIndex::Auth & auth)
{
    return auths.insert(auth).second;
}

uint32_t CAnchorAuthIndex::GetMinAnchorQuorum(CMasternodesView::CTeam const & team) const
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
            if (count >= GetMinAnchorQuorum(pmasternodesview->GetCurrentTeam())) {
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

std::unique_ptr<CAnchorAuthIndex> panchorauths;
std::unique_ptr<CAnchorIndex> panchors;
std::unique_ptr<CAnchorConfirms> panchorconfirms;
