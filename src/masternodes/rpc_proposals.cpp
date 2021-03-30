
#include <masternodes/mn_rpc.h>

#include <functional>

UniValue propToJSON(CPropId const& propId, CPropObject const& prop)
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("proposalId", propId.GetHex());
    ret.pushKV("title", prop.title);
    ret.pushKV("type", CPropTypeToString(prop.type));
    ret.pushKV("status", CPropStatusToString(prop.status));
    ret.pushKV("amount", ValueFromAmount(prop.nAmount));
    ret.pushKV("cyclesPaid", int(prop.cycle));
    ret.pushKV("totalCycles", int(prop.nCycles));
    ret.pushKV("finalizeAfter", int64_t(prop.finalHeight));
    ret.pushKV("payoutAddress", ScriptToString(prop.address));
    return ret;
}

/*
 *  Issued by: any
*/
UniValue createcfp(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    auto votingPeriodStr = std::to_string(Params().GetConsensus().props.votingPeriod);

    RPCHelpMan{"createcfp",
               "\nCreates a Cummunity Fund Request" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"data", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "data in json-form, containing cfp data",
                        {
                            {"title", RPCArg::Type::STR, RPCArg::Optional::NO, "The title of community fund request"},
                            {"finalizeAfter", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Defaulted to " + votingPeriodStr + " / 2"},
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
    LockedCoinsScopedGuard lcGuard(pwallet); // no need here, but for symmetry

    RPCTypeCheck(request.params, { UniValue::VOBJ, UniValue::VARR }, true);

    CAmount amount;
    int cycles = 1;
    std::string title, addressStr;
    auto finalizeAfter = Params().GetConsensus().props.votingPeriod / 2;

    const UniValue& data = request.params[0].get_obj();

    if (!data["title"].isNull()) {
        title = data["title"].get_str();
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "<title> is required");
    }

    if (!data["cycles"].isNull()) {
        cycles = data["cycles"].get_int();
        if (cycles > 3 || cycles <= 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "<cycles> should be between 1 and 3");
        }
    }

    if (!data["finalizeAfter"].isNull()) {
        finalizeAfter = data["finalizeAfter"].get_int();
        if (finalizeAfter < 0 || finalizeAfter > 3 * Params().GetConsensus().props.votingPeriod) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "<finalizeAfter> should be higher than 0 and lower than 3 x " + votingPeriodStr);
        }
    }

    if (!data["amount"].isNull()) {
        amount = data["amount"].get_int() * COIN;
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
    pm.blocksCount = finalizeAfter;

    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateCfp)
             << pm;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    auto targetHeight = chainHeight(*pwallet->chain().lock()) + 1;
    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true /*needFoundersAuth*/, optAuthTx, request.params[1]);

    CAmount cfpFee = GetPropsCreationFee(targetHeight, pm.type);
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
    {
        LOCK(cs_main);
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, pm});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CCreatePropMessage{}, coins);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
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

    LOCK(cs_main);
    pcustomcsview->ForEachProp([&](CPropId const& propId, CPropObject const& prop) {
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
    {"proposals",   "listproposals",         &listproposals,         {"type", "status"} },
};

void RegisterProposalRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
