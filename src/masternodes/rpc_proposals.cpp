#include <masternodes/govvariables/attributes.h>
#include <masternodes/mn_rpc.h>

#include <functional>

const bool DEFAULT_RPC_GOV_NEUTRAL = false;
const int SLEEP_TIME_MILLIS = 500;

struct VotingInfo {
    int32_t votesPossible = 0;
    int32_t votesPresent = 0;
    int32_t votesYes = 0;
    int32_t votesNo = 0;
    int32_t votesNeutral = 0;
    int32_t votesInvalid = 0;
};

UniValue proposalToJSON(const CProposalId &propId,
                        const CProposalObject &prop,
                        const CCustomCSView &view,
                        const std::optional<VotingInfo> votingInfo) {
    auto proposalId        = propId.GetHex();
    auto creationHeight    = static_cast<int32_t>(prop.creationHeight);
    auto title             = prop.title;
    auto context           = prop.context;
    auto contextHash       = prop.contextHash;
    auto type              = static_cast<CProposalType>(prop.type);
    auto typeString        = CProposalTypeToString(type);
    auto amountValue       = ValueFromAmount(prop.nAmount);
    auto payoutAddress     = ScriptToString(prop.address);
    auto currentCycle      = static_cast<int32_t>(prop.cycle);
    auto totalCycles       = static_cast<int32_t>(prop.nCycles);
    auto cycleEndHeight    = static_cast<int32_t>(prop.cycleEndHeight);
    auto proposalEndHeight = static_cast<int32_t>(prop.proposalEndHeight);
    auto votingPeriod      = static_cast<int32_t>(prop.votingPeriod);
    bool isEmergency       = prop.options & CProposalOption::Emergency;
    auto quorum            = prop.quorum;
    auto approvalThreshold = prop.approvalThreshold;
    auto status            = static_cast<CProposalStatusType>(prop.status);
    auto statusString      = CProposalStatusToString(status);
    auto feeTotalValue     = ValueFromAmount(prop.fee);
    auto feeBurnValue      = ValueFromAmount(prop.feeBurnAmount);

    auto quorumString            = strprintf("%d.%02d%%", quorum / 100, quorum % 100);
    auto approvalThresholdString = strprintf("%d.%02d%%", approvalThreshold / 100, approvalThreshold % 100);

    auto votesPossible                = -1;
    auto votesPresent                 = -1;
    auto votesPresentPct              = -1;
    auto votesYes                     = -1;
    auto votesYesPct                  = -1;
    auto votesNo                      = -1;
    auto votesNeutral                 = -1;
    auto votesInvalid                 = -1;
    std::string votesPresentPctString = "-1";
    std::string votesYesPctString     = "-1";

    auto isVotingInfoAvailable = votingInfo.has_value();
    if (isVotingInfoAvailable) {
        votesPresent  = votingInfo->votesPresent;
        votesYes      = votingInfo->votesYes;
        votesNo       = votingInfo->votesNo;
        votesNeutral  = votingInfo->votesNeutral;
        votesInvalid  = votingInfo->votesInvalid;
        votesPossible = votingInfo->votesPossible;

        votesPresentPct = lround(votesPresent * 10000.f / votesPossible);
        votesYesPct     = lround(votesYes * 10000.f / votesPresent);

        votesPresentPctString = strprintf("%d.%02d%%", votesPresentPct / 100, votesPresentPct % 100);
        votesYesPctString     = strprintf("%d.%02d%%", votesYesPct / 100, votesYesPct % 100);
    }

    UniValue ret(UniValue::VOBJ);

    ret.pushKV("proposalId", proposalId);
    ret.pushKV("creationHeight", creationHeight);
    ret.pushKV("title", title);
    ret.pushKV("context", context);
    ret.pushKV("contextHash", contextHash);
    ret.pushKV("status", statusString);
    ret.pushKV("type", typeString);
    if (type != CProposalType::VoteOfConfidence) {
        ret.pushKV("amount", amountValue);
        ret.pushKV("payoutAddress", payoutAddress);
    }
    ret.pushKV("currentCycle", currentCycle);
    ret.pushKV("totalCycles", totalCycles);
    ret.pushKV("cycleEndHeight", cycleEndHeight);
    ret.pushKV("proposalEndHeight", proposalEndHeight);
    ret.pushKV("votingPeriod", votingPeriod);
    ret.pushKV("quorum", quorumString);
    ret.pushKV("approvalThreshold", approvalThresholdString);
    if (isVotingInfoAvailable) {
        ret.pushKV("votesPossible", votesPossible);
        ret.pushKV("votesPresent", votesPresent);
        ret.pushKV("votesPresentPct", votesPresentPctString);
        ret.pushKV("votesYes", votesYes);
        ret.pushKV("votesYesPct", votesYesPctString);
        ret.pushKV("votesNo", votesNo);
        ret.pushKV("votesNeutral", votesNeutral);
        ret.pushKV("votesInvalid", votesInvalid);
        ret.pushKV("feeRedistributionPerVote", ValueFromAmount(DivideAmounts(prop.fee - prop.feeBurnAmount, votesPresent * COIN)));
        ret.pushKV("feeRedistributionTotal", ValueFromAmount(MultiplyAmounts(DivideAmounts(prop.fee - prop.feeBurnAmount, votesPresent * COIN), votesPresent * COIN)));
    }
    ret.pushKV("fee", feeTotalValue);
    // ret.pushKV("feeBurn", feeBurnValue);
    if (prop.options) {
        UniValue opt = UniValue(UniValue::VARR);
        if (isEmergency)
            opt.push_back("emergency");

        ret.pushKV("options", opt);
    }
    return ret;
}

UniValue proposalVoteToJSON(const CProposalId &propId, uint8_t cycle, const uint256 &mnId, CProposalVoteType vote, bool valid) {
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("proposalId", propId.GetHex());
    ret.pushKV("masternodeId", mnId.GetHex());
    ret.pushKV("cycle", int(cycle));
    ret.pushKV("vote", CProposalVoteToString(vote));
    ret.pushKV("valid", valid);
    return ret;
}

void proposalVoteAccounting(const CProposalVoteType &vote, uint256 propId, std::map<std::string, VotingInfo> &map) {
    const auto voteString = CProposalVoteToString(vote);

    if (map.find(propId.GetHex()) == map.end())
        map.emplace(propId.GetHex(), VotingInfo{0, 0, 0, 0, 0, 0});

    auto entry = &map.find(propId.GetHex())->second;

    if (voteString == "YES")
        ++entry->votesYes;
    else if (voteString == "NEUTRAL")
        ++entry->votesNeutral;
    else if (voteString == "NO")
        ++entry->votesNo;

    ++entry->votesPresent;
}

/*
 *  Issued by: any
 */
UniValue creategovcfp(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "creategovcfp",
        "\nCreates a Community Fund Proposal" + HelpRequiringPassphrase(pwallet) + "\n",
        {
                                                    {
                "data",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED_NAMED_ARG,
                "data in json-form, containing cfp data",
                {
                    {"title", RPCArg::Type::STR, RPCArg::Optional::NO, "The title of community fund request"},
                    {"context", RPCArg::Type::STR, RPCArg::Optional::NO, "The context field of community fund request"},
                    {"contextHash",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "The hash of the content which context field point to of community fund request"},
                    {"cycles", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Defaulted to one cycle"},
                    {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount in DFI to request"},
                    {"payoutAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address for receiving"},
                },
            }, {
                "inputs",
                RPCArg::Type::ARR,
                RPCArg::Optional::OMITTED_NAMED_ARG,
                "A json array of json objects",
                {
                    {
                        "",
                        RPCArg::Type::OBJ,
                        RPCArg::Optional::OMITTED,
                        "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            }, },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                                                    HelpExampleCli("creategovcfp",
                                                    "'{\"title\":\"The cfp title\",\"context\":\"The cfp "
                           "context\",\"amount\":10,\"payoutAddress\":\"address\"}' '[{\"txid\":\"id\",\"vout\":0}]'") +
            HelpExampleRpc("creategovcfp",
                                                    "'{\"title\":\"The cfp title\",\"context\":\"The cfp "
                           "context\",\"amount\":10,\"payoutAddress\":\"address\"} '[{\"txid\":\"id\",\"vout\":0}]'")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create a cfp while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    CAmount amount;
    int cycles = 1;
    std::string title, context, contextHash, addressStr;

    const UniValue &data = request.params[0].get_obj();

    RPCTypeCheckObj(data,
                    {
                        {"title",         UniValue::VSTR},
                        {"context",       UniValue::VSTR},
                        {"contextHash",   UniValue::VSTR},
                        {"cycles",        UniValue::VNUM},
                        {"amount",        UniValueType()},
                        {"payoutAddress", UniValue::VSTR}
    },
                    true,
                    true);

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

    CCreateProposalMessage pm;
    pm.type        = CProposalType::CommunityFundProposal;
    pm.address     = GetScriptForDestination(address);
    pm.nAmount     = amount;
    pm.nCycles     = cycles;
    pm.title       = title;
    pm.context     = context;
    pm.contextHash = contextHash;
    pm.options     = 0;

    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateCfp) << pm;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    auto targetHeight    = pcustomcsview->GetLastHeight() + 1;
    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{pm.address};
    rawTx.vin =
        GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[1], request.metadata.coinSelectOpts);

    auto cfpFee = GetProposalCreationFee(targetHeight, *pcustomcsview, pm);
    rawTx.vout.emplace_back(CTxOut(cfpFee, scriptMeta));

    CCoinControl coinControl;

    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue creategovvoc(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "creategovvoc",
        "\nCreates a Vote of Confidence" + HelpRequiringPassphrase(pwallet) + "\n",
        {
                                                    {
                "data",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED_NAMED_ARG,
                "data in json-form, containing voc data",
                {
                    {"title", RPCArg::Type::STR, RPCArg::Optional::NO, "The title of vote of confidence"},
                    {"context", RPCArg::Type::STR, RPCArg::Optional::NO, "The context field for vote of confidence"},
                    {"contextHash",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "The hash of the content which context field point to of vote of confidence request"},
                    {"emergency", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Is this emergency VOC"},
                    {"special", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Preferred alias for emergency VOC"},
                },
            }, {
                "inputs",
                RPCArg::Type::ARR,
                RPCArg::Optional::OMITTED_NAMED_ARG,
                "A json array of json objects",
                {
                    {
                        "",
                        RPCArg::Type::OBJ,
                        RPCArg::Optional::OMITTED,
                        "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            }, },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                                                    HelpExampleCli("creategovvoc", "'The voc title' 'The voc context' '[{\"txid\":\"id\",\"vout\":0}]'") +
            HelpExampleRpc("creategovvoc", "'The voc title' 'The voc context' '[{\"txid\":\"id\",\"vout\":0}]'")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create a voc while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    std::string title, context, contextHash;
    bool emergency = false;

    const UniValue &data = request.params[0].get_obj();

    RPCTypeCheckObj(data,
                    {
                        {"title",       UniValue::VSTR },
                        {"context",     UniValue::VSTR },
                        {"contextHash", UniValue::VSTR },
                        {"emergency",   UniValue::VBOOL},
                        {"special",     UniValue::VBOOL}
    },
                    true,
                    true);

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

    if (!data["emergency"].isNull()) {
        emergency = data["emergency"].get_bool();
    } else if (!data["special"].isNull()) {
        emergency = data["special"].get_bool();
    }

    CCreateProposalMessage pm;
    pm.type        = CProposalType::VoteOfConfidence;
    pm.nAmount     = 0;
    pm.nCycles     = (emergency ? 1 : VOC_CYCLES);
    pm.title       = title;
    pm.context     = context;
    pm.contextHash = contextHash;
    pm.options     = (emergency ? CProposalOption::Emergency : 0);

    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateVoc) << pm;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    auto targetHeight    = pcustomcsview->GetLastHeight() + 1;
    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin =
        GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[1], request.metadata.coinSelectOpts);

    auto vocFee = GetProposalCreationFee(targetHeight, *pcustomcsview, pm);
    rawTx.vout.emplace_back(CTxOut(vocFee, scriptMeta));

    CCoinControl coinControl;

    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue votegov(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "votegov",
        "\nVote for community proposal" + HelpRequiringPassphrase(pwallet) + "\n",
        {
                                                    {"proposalId", RPCArg::Type::STR, RPCArg::Optional::NO, "The proposal txid"},
                                                    {"masternodeId", RPCArg::Type::STR, RPCArg::Optional::NO, "The masternode id / owner address / operator address which made the vote"},
                                                    {"decision", RPCArg::Type::STR, RPCArg::Optional::NO, "The vote decision (yes/no/neutral)"},
                                                    {
                "inputs",
                RPCArg::Type::ARR,
                RPCArg::Optional::OMITTED_NAMED_ARG,
                "A json array of json objects",
                {
                    {
                        "",
                        RPCArg::Type::OBJ,
                        RPCArg::Optional::OMITTED,
                        "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            }, },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{HelpExampleCli("votegov", "txid masternodeId yes") +
                    HelpExampleRpc("votegov", "txid masternodeId yes")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot vote while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VARR}, true);

    auto propId = ParseHashV(request.params[0].get_str(), "proposalId");
    std::string id = request.params[1].get_str();
    uint256 mnId;
    auto vote   = CProposalVoteType::VoteNeutral;
    auto voteStr = ToLower(request.params[2].get_str());
    auto neutralVotesAllowed = gArgs.GetBoolArg("-rpc-governance-accept-neutral", DEFAULT_RPC_GOV_NEUTRAL);

    if (voteStr == "no") {
        vote = CProposalVoteType::VoteNo;
    } else if (voteStr == "yes") {
        vote = CProposalVoteType::VoteYes;
    } else if (neutralVotesAllowed && voteStr != "neutral") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "decision supports yes/no/neutral");
    } else if (!neutralVotesAllowed) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Decision supports yes or no. Neutral is currently disabled because of issue https://github.com/DeFiCh/ain/issues/1704");
    }

    int targetHeight;
    CTxDestination ownerDest;
    {
        CCustomCSView view(*pcustomcsview);

        auto prop = view.GetProposal(propId);
        if (!prop) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Proposal <%s> does not exist", propId.GetHex()));
        }
        if (prop->status != CProposalStatusType::Voting) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Proposal <%s> is not in voting period", propId.GetHex()));
        }
        if (id.length() == 64) {
            mnId = ParseHashV(id, "masternodeId");
        } else {
            CTxDestination dest = DecodeDestination(id);
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                    strprintf("The masternode id or address is not valid: %s", id));
            }
            CKeyID ckeyId;
            if (dest.index() == PKHashType) { 
                ckeyId = CKeyID(std::get<PKHash>(dest));
            } else if (dest.index() == WitV0KeyHashType) {
                ckeyId =  CKeyID(std::get<WitnessV0KeyHash>(dest));
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s does not refer to a P2PKH or P2WPKH address", id));
            }
            if (auto masterNodeIdByOwner = view.GetMasternodeIdByOwner(ckeyId)) {
                mnId = masterNodeIdByOwner.value();
            } else if (auto masterNodeIdByOperator = view.GetMasternodeIdByOperator(ckeyId)) {
                mnId = masterNodeIdByOperator.value();
            }
        }
        auto node = view.GetMasternode(mnId);
        if (!node) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                                    strprintf("The masternode does not exist or the address doesn't own a masternode: %s", id));
        }
        ownerDest = node->ownerType == 1 ? CTxDestination(PKHash(node->ownerAuthAddress))
                                         : CTxDestination(WitnessV0KeyHash(node->ownerAuthAddress));

        targetHeight = view.GetLastHeight() + 1;
    }

    CProposalVoteMessage msg;
    msg.propId       = propId;
    msg.masternodeId = mnId;
    msg.vote         = vote;

    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::Vote) << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths = {GetScriptForDestination(ownerDest)};
    rawTx.vin =
        GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[3], request.metadata.coinSelectOpts);

    rawTx.vout.emplace_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}


UniValue votegovbatch(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
            "votegovbatch",
            "\nVote for community proposal with multiple masternodes" + HelpRequiringPassphrase(pwallet) + "\n",
            {
                    {"votes", RPCArg::Type::ARR, RPCArg::Optional::NO, "A json array of proposal ID, masternode IDs, operator or owner addresses and vote decision (yes/no/neutral).",
                     {
                             {"proposalId", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The proposal txid"},
                             {"masternodeId", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "The masternode ID, operator or owner address"},
                             {"decision", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The vote decision (yes/no/neutral)"},
                            }
                    },
                    {"sleepTime", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "No. of milliseconds to wait between each vote. A small wait time is required for large number of votes to prevent the node from being too busy to broadcast TXs (default: 500)"}
            },
            RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
            RPCExamples{HelpExampleCli("votegovbatch", "{{proposalId, masternodeId, yes}...}") +
                        HelpExampleRpc("votegovbatch", "{{proposalId, masternodeId, yes}...}")},
    }
            .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot vote while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VARR}, false);

    const auto &keys = request.params[0].get_array();
    auto sleepTime = (request.params.size() > 1 && request.params[1].isNull()) ? SLEEP_TIME_MILLIS : request.params[1].get_int();
    auto neutralVotesAllowed = gArgs.GetBoolArg("-rpc-governance-accept-neutral", DEFAULT_RPC_GOV_NEUTRAL);

    int targetHeight;

    struct VotingState {
        uint256 propId;
        uint256 mnId;
        CTxDestination dest;
        CProposalVoteType type;
    };

    std::vector<VotingState> voteList;
    {
        CCustomCSView view(*pcustomcsview);

        for (size_t i{}; i < keys.size(); ++i) {

            const auto &votes{keys[i].get_array()};
            if (votes.size() != 3) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("Incorrect number of items, three expected, proposal ID, masternode ID and vote expected. %d entries provided.", 
                    votes.size()));
            }

            const auto propId = ParseHashV(votes[0].get_str(), "proposalId");
            const auto prop = view.GetProposal(propId);
            if (!prop) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                    strprintf("Proposal <%s> does not exist", 
                    propId.GetHex()));
            }

            if (prop->status != CProposalStatusType::Voting) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    strprintf("Proposal <%s> is not in voting period", 
                    propId.GetHex()));
            }

            uint256 mnId;
            const auto &id = votes[1].get_str();

            if (id.length() == 64) {
                mnId = ParseHashV(id, "masternodeId");
            } else {
                const CTxDestination dest = DecodeDestination(id);
                if (!IsValidDestination(dest)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                        strprintf("The masternode id or address is not valid: %s", id));
                }
                CKeyID ckeyId;
                if (dest.index() == PKHashType) {
                    ckeyId = CKeyID(std::get<PKHash>(dest));
                } else if (dest.index() == WitV0KeyHashType) {
                    ckeyId =  CKeyID(std::get<WitnessV0KeyHash>(dest));
                } else {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, 
                        strprintf("%s does not refer to a P2PKH or P2WPKH address", id));
                }
                if (auto masterNodeIdByOwner = view.GetMasternodeIdByOwner(ckeyId)) {
                    mnId = masterNodeIdByOwner.value();
                } else if (auto masterNodeIdByOperator = view.GetMasternodeIdByOperator(ckeyId)) {
                    mnId = masterNodeIdByOperator.value();
                }
            }

            const auto node = view.GetMasternode(mnId);
            if (!node) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    strprintf("The masternode does not exist or the address doesn't own a masternode: %s", id));
            }

            auto vote   = CProposalVoteType::VoteNeutral;
            auto voteStr = ToLower(votes[2].get_str());

            if (voteStr == "no") {
                vote = CProposalVoteType::VoteNo;
            } else if (voteStr == "yes") {
                vote = CProposalVoteType::VoteYes;
            } else if (neutralVotesAllowed && voteStr != "neutral") {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "decision supports yes/no/neutral");
            } else if (!neutralVotesAllowed) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Decision supports yes or no. Neutral is currently disabled because of issue https://github.com/DeFiCh/ain/issues/1704");
            }

            voteList.push_back({
                propId,
                mnId,
                node->ownerType == 1 ? CTxDestination(PKHash(node->ownerAuthAddress)) : 
                    CTxDestination(WitnessV0KeyHash(node->ownerAuthAddress)),
                vote
            });
        }

        targetHeight = view.GetLastHeight() + 1;
    }

    UniValue ret(UniValue::VARR);

    for (const auto& [propId, mnId, ownerDest, vote] : voteList) {

        CProposalVoteMessage msg;
        msg.propId       = propId;
        msg.masternodeId = mnId;
        msg.vote         = vote;

        // encode
        CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        metadata << static_cast<unsigned char>(CustomTxType::Vote) << msg;

        CScript scriptMeta;
        scriptMeta << OP_RETURN << ToByteVector(metadata);

        const auto txVersion = GetTransactionVersion(targetHeight);
        CMutableTransaction rawTx(txVersion);

        CTransactionRef optAuthTx;
        std::set<CScript> auths = {GetScriptForDestination(ownerDest)};
        rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, {}, request.metadata.coinSelectOpts);
        rawTx.vout.emplace_back(0, scriptMeta);

        CCoinControl coinControl;
        if (IsValidDestination(ownerDest)) {
            coinControl.destChange = ownerDest;
        }

        fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

        // check execution
        execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

        ret.push_back(signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex());
        // Sleep the RPC worker thread a bit, so that the node can 
        // relay the TXs as it works through. Otherwise, the main 
        // chain can be locked for too long that prevent broadcasting of
        // TXs
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
    }

    return ret;
}

UniValue listgovproposalvotes(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);
    RPCHelpMan{
        "listgovproposalvotes",
        "\nReturns information about proposal votes.\n",
        {
          {"proposalId", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The proposal id)"},
          {"masternode", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "mine/all/id (default = mine)"},
          {"cycle",
             RPCArg::Type::NUM,
             RPCArg::Optional::OMITTED,
             "cycle: 0 (show current), cycle: N (show cycle N), cycle: -1 (show all) (default = 0)"},
          {
                "pagination",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"start",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Vote index to iterate from."
                     "Typically it's set to last ID from previous request."},
                    {"including_start",
                     RPCArg::Type::BOOL,
                     RPCArg::Optional::OMITTED,
                     "If true, then iterate including starting position. False by default"},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of votes to return, 100 by default"},
                },
            },
            {
                "aggregate",
                RPCArg::Type::BOOL,
                RPCArg::Optional::OMITTED,
                "0: return raw vote data, 1: return total votes by type"
            },
            {
                "valid",
                RPCArg::Type::BOOL,
                RPCArg::Optional::OMITTED,
                "0: show only invalid votes at current height, 1: show only valid votes at current height (default: 1)"
            }
          },
        RPCResult{"{id:{...},...}     (array) Json object with proposal vote information\n"},
        RPCExamples{HelpExampleCli("listgovproposalvotes", "txid") + HelpExampleRpc("listgovproposalvotes", "txid")},
    }
        .Check(request);

    UniValue optionsObj(UniValue::VOBJ);

    if (!request.params[0].empty()) {
        if (!request.params[0].isObject() && !optionsObj.read(request.params[0].getValStr()))
            RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VNUM, UniValue::VOBJ, UniValue::VBOOL, UniValue::VBOOL}, true);
        else if (request.params[0].isObject())
            optionsObj = request.params[0].get_obj();
    }

    CCustomCSView view(*pcustomcsview);

    uint256 mnId;
    uint256 propId;
    bool isMine = false;
    uint8_t cycle{1};
    int8_t inputCycle{0};
    bool aggregate = true;
    bool latestOnly = true;
    bool validOnly = true;

    size_t limit         = 100;
    size_t start         = 0;
    bool including_start = true;

    if (!optionsObj.empty()) {
        if (!optionsObj["proposalId"].isNull()) {
            propId = ParseHashV(optionsObj["proposalId"].get_str(), "proposalId");
            aggregate = false;
            isMine = true;
            latestOnly = false;
        }

        if (!optionsObj["masternode"].isNull()) {
            if (optionsObj["masternode"].get_str() == "all") {
                isMine = false;
            } else if (optionsObj["masternode"].get_str() != "mine") {
                isMine = false;
                mnId   = ParseHashV(optionsObj["masternode"].get_str(), "masternode");
            }
        }

        if (!optionsObj["cycle"].isNull()) {
            inputCycle = optionsObj["cycle"].get_int();
            latestOnly = false;
        }

        if (!optionsObj["pagination"].isNull()) {
            UniValue paginationObj = optionsObj["pagination"].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start           = (size_t)paginationObj["start"].get_int();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                ++start;
            }
        }

        if (!optionsObj["aggregate"].isNull()) {
            aggregate = optionsObj["aggregate"].getBool();
        }

        if (!optionsObj["valid"].isNull()) {
            validOnly = optionsObj["valid"].getBool();
        }

        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    } else {
        if (!request.params.empty() && request.params[0].isStr()) {
            propId = ParseHashV(request.params[0].get_str(), "proposalId");
            aggregate = false;
            isMine = true;
            latestOnly = false;
        }

        if (request.params.size() > 1) {
            auto str = request.params[1].get_str();
            if (str == "all") {
                isMine = false;
            } else if (str != "mine") {
                isMine = false;
                mnId   = ParseHashV(str, "masternode");
            }
        }

        if (request.params.size() > 2) {
            inputCycle = request.params[2].get_int();
            latestOnly = false;
        }

        if (request.params.size() > 3) {
            UniValue paginationObj = request.params[3].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start           = (size_t)paginationObj["start"].get_int();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                ++start;
            }
        }

        if (request.params.size() > 4) {
            aggregate = request.params[4].getBool();
        }

        if (request.params.size() > 5) {
            validOnly = request.params[5].getBool();
        }

        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    if (inputCycle == 0) {
        if (!propId.IsNull()) {
            auto prop = view.GetProposal(propId);
            if (!prop) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Proposal <%s> does not exist", propId.GetHex()));
            }
            cycle = prop->cycle;
        } else {
            inputCycle = -1;
        }
    } else if (inputCycle > 0) {
        cycle = inputCycle;
    } else if (inputCycle == -1) {
        cycle = 1;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Incorrect cycle value");
    }

    UniValue ret(UniValue::VARR);

    std::map<std::string, VotingInfo> map;

    view.ForEachProposalVote(
        [&](const CProposalId &pId, uint8_t propCycle, const uint256 &id, CProposalVoteType vote) {
            if (!propId.IsNull() && pId != propId) {
                return false;
            }

            if (inputCycle != -1 && cycle != propCycle) {
                return false;
            }

            if (aggregate && latestOnly && propCycle != view.GetProposal(pId)->cycle)
                return true;

            int targetHeight;
            auto prop = view.GetProposal(propId);
            if (prop->status == CProposalStatusType::Voting) {
                targetHeight = view.GetLastHeight() + 1;
            } else {
                targetHeight = prop->cycleEndHeight;
            }

            if (isMine) {
                auto node = view.GetMasternode(id);
                if (!node) {
                    return true;
                }

                bool valid = true;
                if (!node->IsActive(targetHeight, view) || !node->mintedBlocks) {
                    valid = false;
                }

                if (validOnly && !valid)
                    return true;
                if (!validOnly && valid)
                    return true;

                // skip entries until we reach start index
                if (!aggregate && start != 0) {
                    --start;
                    return true;
                }

                auto ownerDest = node->ownerType == 1 ? CTxDestination(PKHash(node->ownerAuthAddress))
                                                      : CTxDestination(WitnessV0KeyHash(node->ownerAuthAddress));
                if (::IsMineCached(*pwallet, GetScriptForDestination(ownerDest))) {
                    if (!aggregate) {
                        ret.push_back(proposalVoteToJSON(propId, propCycle, id, vote, valid));
                        limit--;
                    } else {
                        proposalVoteAccounting(vote, pId, map);
                    }
                }
            } else if (mnId.IsNull() || mnId == id) {
                // skip entries until we reach start index
                if (!aggregate && start != 0) {
                    --start;
                    return true;
                }

                bool valid = true;
                auto node = view.GetMasternode(id);
                if (!node->IsActive(targetHeight, view) || !node->mintedBlocks) {
                    valid = false;
                }

                if (validOnly && !valid)
                    return true;
                if (!validOnly && valid)
                    return true;

                if (!aggregate) {
                    ret.push_back(proposalVoteToJSON(propId, propCycle, id, vote, valid));
                    limit--;
                } else {
                    proposalVoteAccounting(vote, pId, map);
                }
            }

            return limit != 0;
        },
        CMnVotePerCycle{propId, cycle, mnId});

    if(aggregate) {
        for (const auto& entry : map) {
            UniValue stats(UniValue::VOBJ);

            stats.pushKV("proposalId", entry.first);
            stats.pushKV("total", entry.second.votesPresent);
            stats.pushKV("yes", entry.second.votesYes);
            stats.pushKV("neutral", entry.second.votesNeutral);
            stats.pushKV("no", entry.second.votesNo);

            ret.push_back(stats);
        }
    }

    return ret;
}

UniValue getgovproposal(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getgovproposal",
        "\nReturns real time information about proposal state.\n",
        {
          {"proposalId", RPCArg::Type::STR, RPCArg::Optional::NO, "The proposal id)"},
          },
        RPCResult{"{id:{...},...}     (obj) Json object with proposal vote information\n"},
        RPCExamples{HelpExampleCli("getgovproposal", "txid") + HelpExampleRpc("getgovproposal", "txid")},
    }
        .Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, true);

    auto propId = ParseHashV(request.params[0].get_str(), "proposalId");
    CCustomCSView view(*pcustomcsview);
    auto prop = view.GetProposal(propId);
    if (!prop) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Proposal <%s> does not exist", propId.GetHex()));
    }

    int targetHeight;
    if (prop->status == CProposalStatusType::Voting) {
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

    VotingInfo info;
    info.votesPossible = activeMasternodes.size();

    view.ForEachProposalVote(
        [&](const CProposalId &pId, uint8_t cycle, const uint256 &mnId, CProposalVoteType vote) {
            if (pId != propId || cycle != prop->cycle) {
                return false;
            }
            if (activeMasternodes.count(mnId)) {
                ++info.votesPresent;
                if (vote == CProposalVoteType::VoteYes) {
                    ++info.votesYes;
                } else if (vote == CProposalVoteType::VoteNo) {
                    ++info.votesNo;
                } else if (vote == CProposalVoteType::VoteNeutral) {
                    ++info.votesNeutral;
                }
            }
            else
                ++info.votesInvalid;

            return true;
        },
        CMnVotePerCycle{propId, prop->cycle});

    if (!info.votesPresent) {
        return proposalToJSON(propId, *prop, view, std::nullopt);
    }

    return proposalToJSON(propId, *prop, view, info);
}

template <typename T>
void iterateProposals(const T &list,
                  UniValue &ret,
                  const CProposalId &start,
                  bool including_start,
                  size_t limit,
                  const uint8_t type,
                  const uint8_t status) {
    bool pastStart = false;
    for (const auto &prop : list) {
        if (status && status != prop.second.status) {
            continue;
        }
        if (type && type != prop.second.type) {
            continue;
        }
        if (prop.first == start)
            pastStart = true;
        if (start != CProposalId{} && prop.first != start && !pastStart)
            continue;
        if (!including_start) {
            including_start = true;
            continue;
        }

        limit--;
        ret.push_back(proposalToJSON(prop.first, prop.second, *pcustomcsview, std::nullopt));
        if (!limit)
            break;
    }
}

UniValue listgovproposals(const JSONRPCRequest &request) {
    RPCHelpMan{
        "listgovproposals",
        "\nReturns information about proposals.\n",
        {
          {"type", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "cfp/voc/all (default = all)"},
          {"status", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "voting/rejected/completed/all (default = all)"},
          {"cycle",
             RPCArg::Type::NUM,
             RPCArg::Optional::OMITTED,
             "cycle: 0 (all), cycle: N (show cycle N), cycle: -1 (show previous cycle) (default = 0)"},
          {
                "pagination",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"start",
                     RPCArg::Type::STR_HEX,
                     RPCArg::Optional::OMITTED,
                     "Proposal id to iterate from."
                     "Typically it's set to last ID from previous request."},
                    {"including_start",
                     RPCArg::Type::BOOL,
                     RPCArg::Optional::OMITTED,
                     "If true, then iterate including starting position. False by default"},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of proposals to return, 100 by default"},
                },
            }, },
        RPCResult{"{id:{...},...}     (array) Json object with proposals information\n"},
        RPCExamples{HelpExampleCli("listgovproposals", "") + HelpExampleRpc("listgovproposals", "")},
    }
        .Check(request);

    UniValue optionsObj(UniValue::VOBJ);

    if (!request.params[0].empty()) {
        if (!request.params[0].isObject() && !optionsObj.read(request.params[0].getValStr()))
            RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VNUM, UniValue::VOBJ}, true);
        else if (request.params[0].isObject())
            optionsObj = request.params[0].get_obj();
    }

    uint8_t type{0}, status{0};
    int cycle{0};
    size_t limit         = 100;
    CProposalId start    = {};
    bool including_start = true;

    if (!optionsObj.empty()) {
        if (optionsObj.exists("type")) {
            auto str = optionsObj["type"].get_str();
            if (str == "cfp") {
                type = CProposalType::CommunityFundProposal;
            } else if (str == "voc") {
                type = CProposalType::VoteOfConfidence;
            } else if (str != "all") {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "type supports cfp/voc/all");
            }
        }

        if (optionsObj.exists("status")) {
            auto str = optionsObj["status"].get_str();
            if (str == "voting") {
                status = CProposalStatusType::Voting;
            } else if (str == "rejected") {
                status = CProposalStatusType::Rejected;
            } else if (str == "completed") {
                status = CProposalStatusType::Completed;
            } else if (str != "all") {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "status supports voting/rejected/completed/all");
            }
        }

        if (optionsObj.exists("cycle")) {
            cycle = optionsObj["cycle"].get_int();
            if (cycle < -1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "Incorrect cycle value (0 -> all cycles, -1 -> previous cycle, N -> nth cycle");
            }
        }

        if (!optionsObj["pagination"].isNull()) {
            auto paginationObj = optionsObj["pagination"].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start           = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
        }
    } else {
        if (request.params.size() > 0 && request.params[0].isStr()) {
            auto str = request.params[0].get_str();
            if (str == "cfp") {
                type = uint8_t(CProposalType::CommunityFundProposal);
            } else if (str == "voc") {
                type = uint8_t(CProposalType::VoteOfConfidence);
            } else if (str != "all") {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "type supports cfp/voc/all");
            }
        }

        if (request.params.size() > 1) {
            auto str = request.params[1].get_str();
            if (str == "voting") {
                status = uint8_t(CProposalStatusType::Voting);
            } else if (str == "rejected") {
                status = uint8_t(CProposalStatusType::Rejected);
            } else if (str == "completed") {
                status = uint8_t(CProposalStatusType::Completed);
            } else if (str != "all") {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "status supports voting/rejected/completed/all");
            }
        }

        if (request.params.size() > 2) {
            cycle = request.params[2].get_int();
            if (cycle < -1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "Incorrect cycle value (0 -> all cycles, -1 -> previous cycle, N -> nth cycle");
            }
        }

        if (request.params.size() > 3) {
            auto paginationObj = request.params[3].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start           = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
        }
    }

    if (limit == 0) {
        limit = std::numeric_limits<decltype(limit)>::max();
    }

    UniValue ret{UniValue::VARR};
    CCustomCSView view(*pcustomcsview);

    using IdPropPair        = std::pair<CProposalId, CProposalObject>;
    using CycleEndHeightInt = int;
    using CyclePropsMap     = std::map<CycleEndHeightInt, std::vector<IdPropPair>>;

    std::map<CProposalId, CProposalObject> props;
    CyclePropsMap cycleProps;

    view.ForEachProposal(
        [&](const CProposalId &propId, const CProposalObject &prop) {
            props.insert({propId, prop});
            return true;
        },
        static_cast<CProposalStatusType>(0));

    if (cycle != 0) {
        // populate map
        for (const auto &[propId, prop] : props) {
            auto batch    = cycleProps.find(prop.cycleEndHeight);
            auto propPair = std::make_pair(propId, prop);
            // if batch is not found create it
            if (batch == cycleProps.end()) {
                cycleProps.insert({prop.cycleEndHeight, std::vector<IdPropPair>{propPair}});
            } else {  // else insert to prop vector
                batch->second.push_back(propPair);
            }
        }

        auto batch = cycleProps.rbegin();
        if (cycle != -1) {
            if (static_cast<CyclePropsMap::size_type>(cycle) > cycleProps.size())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not find cycle");
            for (unsigned int i = 1; i <= (cycleProps.size() - cycle); i++) {
                batch++;
            }
        } else {
            batch++;
        }

        iterateProposals(batch->second, ret, start, including_start, limit, type, status);

        return ret;
    }

    iterateProposals(props, ret, start, including_start, limit, type, status);

    return ret;
}

static const CRPCCommand commands[] = {
  //  category        name                     actor (function)        params
  //  --------------- ----------------------   ---------------------   ----------
    {"proposals", "creategovcfp",         &creategovcfp,         {"data", "inputs"}                                  },
    {"proposals", "creategovvoc",         &creategovvoc,         {"data", "inputs"}                                  },
    {"proposals", "votegov",              &votegov,              {"proposalId", "masternodeId", "decision", "inputs"}},
    {"proposals", "votegovbatch",         &votegovbatch,         {"proposalId", "masternodeIds", "decision"}},
    {"proposals", "listgovproposalvotes", &listgovproposalvotes, {"proposalId", "masternode", "cycle", "pagination"} },
    {"proposals", "getgovproposal",       &getgovproposal,       {"proposalId"}                                      },
    {"proposals", "listgovproposals",     &listgovproposals,     {"type", "status", "cycle", "pagination"}           },
};

void RegisterProposalRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
