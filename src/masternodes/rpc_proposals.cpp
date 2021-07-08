
#include <masternodes/mn_rpc.h>

#include <functional>

UniValue propToJSON(CPropId const& propId, CPropObject const& prop)
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("proposalId", propId.GetHex());
    ret.pushKV("title", prop.title);
    auto type = static_cast<CPropType>(prop.type);
    ret.pushKV("type", CPropTypeToString(type));
    auto status = static_cast<CPropStatusType>(prop.status);
    ret.pushKV("status", CPropStatusToString(status));
    ret.pushKV("amount", ValueFromAmount(prop.nAmount));
    ret.pushKV("cyclesPaid", int(prop.cycle));
    ret.pushKV("totalCycles", int(prop.nCycles));
    ret.pushKV("finalizeAfter", int64_t(prop.finalHeight));
    ret.pushKV("payoutAddress", ScriptToString(prop.address));
    return ret;
}

UniValue propVoteToJSON(CPropId const& propId, uint8_t cycle, uint256 const & mnId, CPropVoteType vote)
{
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
UniValue createcfp(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"createcfp",
               "\nCreates a Cummunity Fund Request" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"data", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "data in json-form, containing cfp data",
                        {
                            {"title", RPCArg::Type::STR, RPCArg::Optional::NO, "The title of community fund request"},
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
                       HelpExampleCli("createcfp", "'{\"title\":\"The cfp title\",\"amount\":10,\"payoutAddress\":\"address\"}' '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("createcfp", "'{\"title\":\"The cfp title\",\"amount\":10,\"payoutAddress\":\"address\"} '[{\"txid\":\"id\",\"vout\":0}]'")
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
    std::string title, addressStr;

    const UniValue& data = request.params[0].get_obj();

    if (!data["title"].isNull()) {
        title = data["title"].get_str();
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "<title> is required");
    }

    if (!data["cycles"].isNull()) {
        cycles = data["cycles"].get_int();
        if (cycles > int(MAX_CYCLES) || cycles < 1) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("<cycles> should be between 1 and %d", int(MAX_CYCLES)));
        }
    }

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

    auto const address = DecodeDestination(addressStr);
    // check type if a supported script
    if (!IsValidDestination(address)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address (" + addressStr + ") is of an unknown type");
    }

    CCreatePropMessage pm;
    pm.type = CPropType::CommunityFundRequest;
    pm.address = GetScriptForDestination(address);
    pm.nAmount = amount;
    pm.nCycles = cycles;
    pm.title = title.substr(0, 128);

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
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, request.params[1]);

    CAmount cfpFee = GetPropsCreationFee(targetHeight, static_cast<CPropType>(pm.type));
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

UniValue vote(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"vote",
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
                       HelpExampleCli("vote", "txid masternodeId yes")
                       + HelpExampleRpc("vote", "txid masternodeId yes")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create a cfp while still in Initial Block Download");
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
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Proposal <%s> does not exists", propId.GetHex()));
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

UniValue listvotes(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"listvotes",
               "\nReturns information about proposal votes.\n",
               {
                        {"proposalId", RPCArg::Type::STR, RPCArg::Optional::NO, "The proposal id)"},
                        {"masternode", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "mine/all/id (default = mine)"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with proposal vote information\n"
               },
               RPCExamples{
                       HelpExampleCli("listvotes", "txid")
                       + HelpExampleRpc("listvotes", "txid")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR}, true);

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

    UniValue ret(UniValue::VARR);
    CCustomCSView view(*pcustomcsview);

    view.ForEachPropVote([&](CPropId const & pId, uint8_t cycle, uint256 const & id, CPropVoteType vote) {
        if (pId != propId) {
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
                ret.push_back(propVoteToJSON(propId, cycle, id, vote));
            }
        } else if (mnId.IsNull() || mnId == id) {
            ret.push_back(propVoteToJSON(propId, cycle, id, vote));
        }
        return true;
    }, CMnVotePerCycle{propId, 1, mnId});

    return ret;
}

UniValue listproposals(const JSONRPCRequest& request)
{
    RPCHelpMan{"listproposals",
               "\nReturns information about proposals.\n",
               {
                        {"type", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                    "cfp/brp/voc/all (default = all)"},
                        {"status", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                    "voting/rejected/completed/all (default = all)"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with proposals information\n"
               },
               RPCExamples{
                       HelpExampleCli("listproposals", "")
                       + HelpExampleRpc("listproposals", "")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR}, true);

    uint8_t type = 0;
    if (request.params.size() > 0) {
        auto str = request.params[0].get_str();
        if (str == "cfp") {
            type = uint8_t(CPropType::CommunityFundRequest);
        } else if (str == "brp") {
            type = uint8_t(CPropType::BlockRewardRellocation);
        } else if (str == "voc") {
            type = uint8_t(CPropType::VoteOfConfidence);
        } else if (str != "all") {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "type supports cfp/brp/voc/all");
        }
    }

    uint8_t status = 0;
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

    UniValue ret(UniValue::VARR);
    CCustomCSView view(*pcustomcsview);

    view.ForEachProp([&](CPropId const& propId, CPropObject const& prop) {
        if (status && status != uint8_t(prop.status)) {
            return false;
        }
        if (type && type != uint8_t(prop.type)) {
            return true;
        }
        ret.push_back(propToJSON(propId, prop));
        return true;
    }, status);

    return ret;
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  --------------- ----------------------   ---------------------   ----------
    {"proposals",   "createcfp",             &createcfp,             {"data", "inputs"} },
    {"proposals",   "vote",                  &vote,                  {"proposalId", "masternodeId", "decision", "inputs"} },
    {"proposals",   "listvotes",             &listvotes,             {"proposalId", "masternode"} },
    {"proposals",   "listproposals",         &listproposals,         {"type", "status"} },
};

void RegisterProposalRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
