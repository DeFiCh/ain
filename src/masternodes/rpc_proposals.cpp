#include <masternodes/mn_rpc.h>
#include <masternodes/govvariables/attributes.h>

#include <functional>

struct VotingInfo {
    int32_t votesPossible;
    int32_t votesPresent;
    int32_t votesYes;
};

UniValue
proposalToJSON(const CPropId &propId,
    const CPropObject &prop,
    const CCustomCSView &view,
    const std::optional<VotingInfo> votingInfo) {

    auto proposalId = propId.GetHex();
    auto creationHeight = static_cast<int32_t>(prop.creationHeight);
    auto title = prop.title;
    auto context = prop.context;
    auto contextHash = prop.contextHash;
    auto type = static_cast<CPropType>(prop.type);
    auto typeString = CPropTypeToString(type);
    auto amountValue = ValueFromAmount(prop.nAmount);
    auto payoutAddress = ScriptToString(prop.address);
    auto currentCycle   = static_cast<int32_t>(prop.cycle);
    auto totalCycles = static_cast<int32_t>(prop.nCycles);
    auto cycleEndHeight = static_cast<int32_t>(prop.cycleEndHeight);
    auto proposalEndHeight = static_cast<int32_t>(prop.proposalEndHeight);
    auto votingPeriod = static_cast<int32_t>(prop.votingPeriod);
    bool isEmergency = prop.options & CPropOption::Emergency;
    auto quorum = prop.quorum;
    auto approvalThreshold = prop.approvalThreshold;
    auto status = static_cast<CPropStatusType>(prop.status);
    auto statusString = CPropStatusToString(status);
    auto feeTotalValue =  ValueFromAmount(prop.fee);
    auto feeBurnValue = ValueFromAmount(prop.feeBurnAmount);

    auto quorumString = strprintf("%d.%02d%%", quorum / 100, quorum % 100);
    auto approvalThresholdString = strprintf("%d.%02d%%", approvalThreshold / 100, approvalThreshold % 100);

    auto votesPossible = -1;
    auto votesPresent = -1;
    auto votesPresentPct = -1;
    auto votesYes = -1;
    auto votesYesPct = -1;
    std::string votesPresentPctString = "-1";
    std::string votesYesPctString = "-1";

    auto isVotingInfoAvailable = votingInfo.has_value();
    if (isVotingInfoAvailable) {
        votesPresent = votingInfo->votesPresent;
        votesYes = votingInfo->votesYes;
        votesPossible = votingInfo->votesPossible;

        votesPresentPct = lround(votesPresent * 10000.f / votesPossible);
        auto valid = votesPresentPct > quorum;
        if (valid) {
            votesYesPct = lround(votesYes * 10000.f / votesPresent);
        }
        if (valid && votesYesPct > approvalThreshold) {
            statusString = "Completed";
        } else {
            statusString = "Rejected";
        }

        votesPresentPctString = strprintf("%d.%02d%%", votesPresentPct / 100, votesPresentPct % 100);
        votesYesPctString = strprintf("%d.%02d%%", votesYesPct / 100, votesYesPct % 100);
    }

    UniValue ret(UniValue::VOBJ);

    ret.pushKV("proposalId", proposalId);
    ret.pushKV("creationHeight", creationHeight);
    ret.pushKV("title", title);
    ret.pushKV("context", context);
    ret.pushKV("contextHash", contextHash);
    ret.pushKV("status", statusString);
    ret.pushKV("type", typeString);
    if (type != CPropType::VoteOfConfidence) {
        ret.pushKV("amount", amountValue);
        ret.pushKV("payoutAddress", payoutAddress);
    }
    ret.pushKV("currentCycle", currentCycle);
    ret.pushKV("totalCycles", totalCycles);
    ret.pushKV("cycleEndHeight", cycleEndHeight);
    ret.pushKV("proposalEndHeight", proposalEndHeight);
    ret.pushKV("votingPeriod", votingPeriod);
    ret.pushKV("quorum", quorumString);
    if (isVotingInfoAvailable) {
        ret.pushKV("votesPossible", votesPossible);
        ret.pushKV("votesPresent", votesPresent);
        ret.pushKV("votesPresentPct", votesPresentPctString);
        ret.pushKV("votesYes", votesYes);
        ret.pushKV("votesYesPct", votesYesPctString);
    }
    ret.pushKV("approvalThreshold", approvalThresholdString);
    ret.pushKV("fee", feeTotalValue);
    // ret.pushKV("feeBurn", feeBurnValue);
    if (prop.options)
    {
        UniValue opt = UniValue(UniValue::VARR);
        if (isEmergency)
            opt.push_back("emergency");

        ret.pushKV("options", opt);
    }
    return ret;
}

UniValue proposalVoteToJSON(const CPropId &propId, uint8_t cycle, const uint256 &mnId, CPropVoteType vote) {
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("proposalId", propId.GetHex());
    ret.pushKV("masternodeId", mnId.GetHex());
    ret.pushKV("cycle", int(cycle));
    ret.pushKV("vote", CPropVoteToString(vote));
    return ret;
}

/*
 *  Issued by: any
*/
UniValue creategovcfp(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"creategovcfp",
               "\nCreates a Community Fund Proposal" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"data", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG, "data in json-form, containing cfp data",
                        {
                            {"title", RPCArg::Type::STR, RPCArg::Optional::NO, "The title of community fund request"},
                            {"context", RPCArg::Type::STR, RPCArg::Optional::NO, "The context field of community fund request"},
                            {"contextHash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The hash of the content which context field point to of community fund request"},
                            {"cycles", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Defaulted to one cycle"},
                            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount in DFI to request"},
                            {"payoutAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address for receiving"},
                        },
                    },
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                            },
                        },
                    },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("creategovcfp", "'{\"title\":\"The cfp title\",\"context\":\"The cfp context\",\"amount\":10,\"payoutAddress\":\"address\"}' '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("creategovcfp", "'{\"title\":\"The cfp title\",\"context\":\"The cfp context\",\"amount\":10,\"payoutAddress\":\"address\"} '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create a cfp while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VOBJ, UniValue::VARR }, true);

    CAmount amount;
    int cycles = 1;
    std::string title, context, contextHash, addressStr;

    const UniValue& data = request.params[0].get_obj();

    if (!data["title"].isNull()) {
        title = data["title"].get_str();
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "<title> is required");
    }

    if (!data["context"].isNull()) {
        context = data["context"].get_str();
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "<context> is required");
    }

    if (!data["contextHash"].isNull())
        contextHash = data["contextHash"].get_str();

    if (!data["cycles"].isNull())
        cycles = data["cycles"].get_int();

    if (!data["amount"].isNull()) {
        amount = AmountFromValue(data["amount"]);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "<amount> is required");
    }

    if (!data["payoutAddress"].isNull()) {
        addressStr = data["payoutAddress"].get_str();
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "<payoutAddress> is required");
    }

    const auto address = DecodeDestination(addressStr);
    // check type if a supported script
    if (!IsValidDestination(address)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address (" + addressStr + ") is of an unknown type");
    }

    CCreatePropMessage pm;
    pm.type = CPropType::CommunityFundProposal;
    pm.address = GetScriptForDestination(address);
    pm.nAmount = amount;
    pm.nCycles = cycles;
    pm.title = title;
    pm.context = context;
    pm.contextHash = contextHash;
    pm.options = 0;

    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateCfp)
             << pm;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    auto targetHeight = pcustomcsview->GetLastHeight() + 1;
    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{pm.address};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, request.params[1]);

    auto cfpFee = GetPropsCreationFee(targetHeight, *pcustomcsview, pm);
    rawTx.vout.emplace_back(CTxOut(cfpFee, scriptMeta));

    CCoinControl coinControl;

    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue creategovvoc(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"creategovvoc",
               "\nCreates a Vote of Confidence" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"data", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG, "data in json-form, containing voc data",
                        {
                            {"title", RPCArg::Type::STR, RPCArg::Optional::NO, "The title of vote of confidence"},
                            {"context", RPCArg::Type::STR, RPCArg::Optional::NO, "The context field for vote of confidence"},
                            {"contextHash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The hash of the content which context field point to of vote of confidence request"},
                            {"emergency", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Is this emergency VOC"},
                        },
                    },
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                            },
                        },
                    },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("creategovvoc", "'The voc title' 'The voc context' '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("creategovvoc", "'The voc title' 'The voc context' '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create a voc while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VOBJ, UniValue::VARR }, true);

    std::string title, context, contextHash;
    bool emergency = false;

    const UniValue& data = request.params[0].get_obj();

    if (!data["title"].isNull()) {
        title = data["title"].get_str();
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "<title> is required");
    }

    if (!data["context"].isNull()) {
        context = data["context"].get_str();
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "<context> is required");
    }

    if (!data["contextHash"].isNull())
        contextHash = data["contextHash"].get_str();

    if (!data["emergency"].isNull())
    {
        emergency = data["emergency"].get_bool();
    }

    CCreatePropMessage pm;
    pm.type = CPropType::VoteOfConfidence;
    pm.nAmount = 0;
    pm.nCycles = (emergency ? 1 : VOC_CYCLES);
    pm.title = title;
    pm.context = context;
    pm.contextHash = contextHash;
    pm.options = (emergency ? CPropOption::Emergency : 0);

    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateVoc)
             << pm;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    auto targetHeight = pcustomcsview->GetLastHeight() + 1;
    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, request.params[1]);

    auto vocFee = GetPropsCreationFee(targetHeight, *pcustomcsview, pm);
    rawTx.vout.emplace_back(CTxOut(vocFee, scriptMeta));

    CCoinControl coinControl;

    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue votegov(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"votegov",
               "\nVote for community proposal" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"proposalId", RPCArg::Type::STR, RPCArg::Optional::NO, "The proposal txid"},
                       {"masternodeId", RPCArg::Type::STR, RPCArg::Optional::NO, "The masternode id which made the vote"},
                       {"decision", RPCArg::Type::STR, RPCArg::Optional::NO, "The vote decision (yes/no/neutral)"},
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("votegov", "txid masternodeId yes")
                       + HelpExampleRpc("votegov", "txid masternodeId yes")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot vote while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);

    auto propId = ParseHashV(request.params[0].get_str(), "proposalId");
    auto mnId = ParseHashV(request.params[1].get_str(), "masternodeId");
    auto vote = CPropVoteType::VoteNeutral;

    auto voteStr = ToLower(request.params[2].get_str());
    if (voteStr == "no") {
        vote = CPropVoteType::VoteNo;
    } else if (voteStr == "yes") {
        vote = CPropVoteType::VoteYes;
    } else if (voteStr != "neutral") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "decision supports yes/no/neutral");
    }

    int targetHeight;
    CTxDestination ownerDest;
    {
        CCustomCSView view(*pcustomcsview);

        auto prop = view.GetProp(propId);
        if (!prop) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Proposal <%s> does not exist", propId.GetHex()));
        }
        if (prop->status != CPropStatusType::Voting) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Proposal <%s> is not in voting period", propId.GetHex()));
        }
        auto node = view.GetMasternode(mnId);
        if (!node) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", mnId.ToString()));
        }
        ownerDest = node->ownerType == 1 ? CTxDestination(PKHash(node->ownerAuthAddress)) : CTxDestination(WitnessV0KeyHash(node->ownerAuthAddress));

        targetHeight = view.GetLastHeight() + 1;
    }

    CPropVoteMessage msg;
    msg.propId = propId;
    msg.masternodeId = mnId;
    msg.vote = vote;

    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::Vote)
             << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths = { GetScriptForDestination(ownerDest) };
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, request.params[3]);

    rawTx.vout.emplace_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listgovproposalvotes(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);
    RPCHelpMan{"listgovproposalvotes",
               "\nReturns information about proposal votes.\n",
               {
                        {"proposalId", RPCArg::Type::STR, RPCArg::Optional::NO, "The proposal id)"},
                        {"masternode", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "mine/all/id (default = mine)"},
                        {"cycle", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "cycle: 0 (show current), cycle: N (show cycle N), cycle: -1 (show all) (default = 0)"}
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with proposal vote information\n"
               },
               RPCExamples{
                       HelpExampleCli("listgovproposalvotes", "txid")
                       + HelpExampleRpc("listgovproposalvotes", "txid")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VNUM}, true);

    auto propId = ParseHashV(request.params[0].get_str(), "proposalId");

    uint256 mnId;
    bool isMine = true;
    if (request.params.size() > 1) {
        auto str = request.params[1].get_str();
        if (str == "all") {
            isMine = false;
        } else if (str != "mine") {
            isMine = false;
            mnId = ParseHashV(str, "masternode");
        }
    }
    CCustomCSView view(*pcustomcsview);

    uint8_t cycle{1};
    int8_t inputCycle{0};
    if (request.params.size() > 2) {
        inputCycle = request.params[2].get_int();
    }
    if (inputCycle==0){
        auto prop = view.GetProp(propId);
        if (!prop){
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Proposal <%s> does not exist", propId.GetHex()));
        }
        cycle = prop->cycle;
    } else if (inputCycle > 0){
        cycle = inputCycle;
    } else if (inputCycle == -1){
        cycle = 1;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Incorrect cycle value");
    }

    UniValue ret(UniValue::VARR);

    view.ForEachPropVote(
        [&](const CPropId &pId, uint8_t propCycle, const uint256 &id, CPropVoteType vote) {
            if (pId != propId) {
                return false;
            }

            if (inputCycle != -1 && cycle != propCycle) {
                return false;
            }

            if (isMine) {
                auto node = view.GetMasternode(id);
                if (!node) {
                    return true;
                }
                auto ownerDest = node->ownerType == 1 ? CTxDestination(PKHash(node->ownerAuthAddress))
                                                      : CTxDestination(WitnessV0KeyHash(node->ownerAuthAddress));
                if (::IsMineCached(*pwallet, GetScriptForDestination(ownerDest))) {
                    ret.push_back(proposalVoteToJSON(propId, propCycle, id, vote));
                }
            } else if (mnId.IsNull() || mnId == id) {
                ret.push_back(proposalVoteToJSON(propId, propCycle, id, vote));
            }
            return true;
        },
        CMnVotePerCycle{propId, cycle, mnId});

    return ret;
}

UniValue getgovproposal(const JSONRPCRequest& request)
{
    RPCHelpMan{"getgovproposal",
               "\nReturns real time information about proposal state.\n",
               {
                        {"proposalId", RPCArg::Type::STR, RPCArg::Optional::NO, "The proposal id)"},
               },
               RPCResult{
                       "{id:{...},...}     (obj) Json object with proposal vote information\n"
               },
               RPCExamples{
                       HelpExampleCli("getgovproposal", "txid")
                       + HelpExampleRpc("getgovproposal", "txid")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, true);

    auto propId = ParseHashV(request.params[0].get_str(), "proposalId");
    CCustomCSView view(*pcustomcsview);
    auto prop = view.GetProp(propId);
    if (!prop) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Proposal <%s> does not exist", propId.GetHex()));
    }

    int targetHeight;
    if (prop->status == CPropStatusType::Voting) {
        targetHeight = view.GetLastHeight() + 1;
    } else {
        targetHeight = prop->cycleEndHeight;
    }

    std::set<uint256> activeMasternodes;
    view.ForEachMasternode([&](const uint256 &mnId, CMasternode node) {
        if (node.IsActive(targetHeight, view) && node.mintedBlocks) {
            activeMasternodes.insert(mnId);
        }
        return true;
    });

    if (activeMasternodes.empty()) {
        return proposalToJSON(propId, *prop, view, std::nullopt);
    }

    uint32_t voteYes = 0, voters = 0;
    view.ForEachPropVote(
        [&](const CPropId &pId, uint8_t cycle, const uint256 &mnId, CPropVoteType vote) {
            if (pId != propId || cycle != prop->cycle) {
                return false;
            }
            if (activeMasternodes.count(mnId)) {
                ++voters;
                if (vote == CPropVoteType::VoteYes) {
                    ++voteYes;
                }
            }
            return true;
        },
        CMnVotePerCycle{propId, prop->cycle});

    if (!voters) {
        return proposalToJSON(propId, *prop, view, std::nullopt);
    }

    VotingInfo info;
    info.votesPossible = activeMasternodes.size();
    info.votesPresent = voters;
    info.votesYes = voteYes;

    return proposalToJSON(propId, *prop, view, info);
}

UniValue listgovproposals(const JSONRPCRequest& request)
{
    RPCHelpMan{"listgovproposals",
               "\nReturns information about proposals.\n",
               {
                        {"type", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                    "cfp/voc/all (default = all)"},
                        {"status", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                    "voting/rejected/completed/all (default = all)"},
                        {"cycle", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                    "cycle: 0 (all), cycle: N (show cycle N), cycle: -1 (show previous cycle) (default = 0)"}
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with proposals information\n"
               },
               RPCExamples{
                       HelpExampleCli("listgovproposals", "")
                       + HelpExampleRpc("listgovproposals", "")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR}, true);

    uint8_t type = 0;
    if (request.params.size() > 0) {
        auto str = request.params[0].get_str();
        if (str == "cfp") {
            type = uint8_t(CPropType::CommunityFundProposal);
        } else if (str == "voc") {
            type = uint8_t(CPropType::VoteOfConfidence);
        } else if (str != "all") {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "type supports cfp/voc/all");
        }
    }

    uint8_t status{0};
    if (request.params.size() > 1) {
        auto str = request.params[1].get_str();
        if (str == "voting") {
            status = uint8_t(CPropStatusType::Voting);
        } else if (str == "rejected") {
            status = uint8_t(CPropStatusType::Rejected);
        } else if (str == "completed") {
            status = uint8_t(CPropStatusType::Completed);
        } else if (str != "all") {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "status supports voting/rejected/completed/all");
        }
    }

    int cycle{0};
    if (request.params.size() > 2) {
        cycle = request.params[2].get_int();
        if (cycle < -1) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                    "Incorrect cycle value (0 -> all cycles, -1 -> previous cycle, N -> nth cycle");
        }    
    }

    UniValue ret{UniValue::VARR};
    CCustomCSView view(*pcustomcsview);

    using IdPropPair = std::pair<CPropId, CPropObject>;
    using CycleEndHeightInt = int; 
    using PropBatchesMap = std::map<CycleEndHeightInt, std::vector<IdPropPair>>;

    PropBatchesMap propBatches;

    if(cycle != 0){
        // populate map
        view.ForEachProp(
            [&](const CPropId &propId, const CPropObject &prop) {
                auto batch = propBatches.find(prop.cycleEndHeight);
                auto propPair = std::make_pair(propId, prop);
                // if batch is not found create it
                if (batch == propBatches.end()) {
                    propBatches.insert({prop.cycleEndHeight, std::vector<IdPropPair>{propPair}});
                } else { // else insert to prop vector
                    batch->second.push_back(propPair);
                }
                return true;
            }, 0);

        auto batch = propBatches.rbegin();
        if(cycle != -1){
            if(cycle > propBatches.size()) 
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not find cycle");
            for(uint i=1; i <= (propBatches.size() - cycle); i++){
                batch++;
            }
        } else {
            batch++;
        }
        // Filter batch
        for (auto const &prop: batch->second){
            if(status && status != uint8_t(prop.second.status)){
                continue;
            }
            if(type && type != uint8_t(prop.second.type)){
                continue;
            }
            ret.push_back(proposalToJSON(prop.first, prop.second, view, std::nullopt));
        }
        return ret;
    }

    view.ForEachProp(
        [&](const CPropId &propId, const CPropObject &prop) {
            if (status && status != uint8_t(prop.status)) {
                return false;
            }
            if (type && type != uint8_t(prop.type)) {
                return true;
            }
            ret.push_back(proposalToJSON(propId, prop, view, std::nullopt));
            return true;
        },
        status);

    return ret;
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  --------------- ----------------------   ---------------------   ----------
    {"proposals",   "creategovcfp",          &creategovcfp,          {"data", "inputs"} },
    {"proposals",   "creategovvoc",          &creategovvoc,          {"data", "inputs"} },
    {"proposals",   "votegov",               &votegov,               {"proposalId", "masternodeId", "decision", "inputs"} },
    {"proposals",   "listgovproposalvotes",  &listgovproposalvotes,  {"proposalId", "masternode", "cycle"} },
    {"proposals",   "getgovproposal",        &getgovproposal,        {"proposalId"} },
    {"proposals",   "listgovproposals",      &listgovproposals,      {"type", "status", "cycle"} },
};

void RegisterProposalRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
