// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_rpc.h>
#include <masternodes/operators.h>

CAmount EstimateOperatorCreationFee(int targetHeight) {
    // Current height + (1 hour blocks) to avoid rejection;
    targetHeight += (60 * 60 / Params().GetConsensus().pos.nTargetSpacing);
    return GetOperatorCreationFee(targetHeight);
}

OperatorState GetOperatorState(std::string state) {
    if (state == "DRAFT")
        return OperatorState::DRAFT;
    else if (state == "ACTIVE")
        return OperatorState::ACTIVE;
    else 
        return OperatorState::INVALID;
}

std::string GetOperatorStateString(uint8_t state) {
    switch (state) {
        case 2      : return "ACTIVE"; break;
        case 1      : return "DRAFT"; break;
        default     : return "INVALID"; break;

    }
}

// creates an operator
UniValue createoperator(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createoperator",
               "\nCreates (and submits to local node and network) an operator creation transaction with given name, url, state and owner address by spending the given inputs..\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address used as owner key"},
                   {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "Name of the operator"},
                   {"url", RPCArg::Type::STR, RPCArg::Optional::NO, "Url of the operator"},
                   {"state", RPCArg::Type::STR, RPCArg::Optional::NO, "State of the operator. DRAFT or ACTIVE"},
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
                   HelpExampleCli("createoperator", "ownerAddress name url state '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("createoperator", "ownerAddress name url state '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
               {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VSTR}, false);

    //length check input params
    RPCStringArgumentLengthCheck(request.params[1], Params().GetConsensus().oprtr.operatorNameMaxLen); // name
    auto operatorName = request.params[1].get_str();
    RPCStringArgumentLengthCheck(request.params[2], Params().GetConsensus().oprtr.operatorURLMaxLen); // url
    auto operatorURL = request.params[2].get_str();
    OperatorState state = OperatorState::INVALID;

    if ((state = GetOperatorState(request.params[3].get_str())) == OperatorState::INVALID) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid operator state");
    }

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode ownerAddress
    CScript ownerScript;
    try {
        ownerScript = DecodeScript(request.params[0].getValStr());
    } catch(...) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse address");
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    CCreateOperatorMessage msg{ownerScript, operatorName, operatorURL, state};

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::CreateOperator)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    if (request.params.size() > 4) {
        rawTx.vin = GetInputs(request.params[4].get_array());
    }
    
    rawTx.vout.push_back(CTxOut(EstimateOperatorCreationFee(targetHeight), scriptMeta)); // NOTE(sp): send to new burn address?

    // Return change to owner address
    CCoinControl coinControl;
    CTxDestination dest;
    ExtractDestination(ownerScript, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }
    
    fund(rawTx, pwallet, {}, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CCreateOperatorMessage{});
    }

    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

//updates an operator
UniValue updateoperator(const JSONRPCRequest& request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"updateoperator",
               "\nCreates (and submits to local node and network) an update operator transaction, \n"
               "and saves operator updates to database." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"operatorid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "operator id"},
                       {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "name of the operator"},
                       {"url", RPCArg::Type::NUM, RPCArg::Optional::NO, "operator url"},
                       {"state", RPCArg::Type::STR, RPCArg::Optional::NO, "State of the operator. DRAFT or ACTIVE"},
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("updateoperator", "operatorid name url state")
                   + HelpExampleRpc("updateoperator", "operatorid name url state")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VSTR},
                 false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode operatorid
    COperatorId operatorId = ParseHashV(request.params[0], "operatorid");

    CScript ownerScript;
    //load operator from db to get owner script
    int targetHeight;
    {
        LOCK(cs_main);
        auto operatorPtr = pcustomcsview->GetOperatorData(operatorId);
        if (!operatorPtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The operator %s does not exist", request.params[0].getValStr()));
        }
        
        //get owner script
        ownerScript = operatorPtr.val->operatorAddress;
        targetHeight = ::ChainActive().Height() + 1;
    }

    //check inputs for max length
    RPCStringArgumentLengthCheck(request.params[1], Params().GetConsensus().oprtr.operatorNameMaxLen); // name
    auto operatorName = request.params[1].get_str();
    RPCStringArgumentLengthCheck(request.params[2], Params().GetConsensus().oprtr.operatorURLMaxLen); // url
    auto operatorURL = request.params[2].get_str();
    OperatorState operatorState = OperatorState::INVALID;

    if ((operatorState = GetOperatorState(request.params[3].get_str())) == OperatorState::INVALID) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid operator state");
    }

    CUpdateOperatorMessage msg{
            operatorId,
            CCreateOperatorMessage{ownerScript, operatorName, operatorURL, operatorState}
    };

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::UpdateOperator)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{ownerScript};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, {});

    CCoinControl coinControl;
    // Set change to auth address if there's only one auth address
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
        CCustomCSView mnview(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CUpdateOperatorMessage{}, coins);
    }

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  -------------   ---------------------    --------------------    ----------
    {"operator",    "createoperator",         &createoperator,         {"name", "url", "owner", "state"}},
    {"operator",    "updateoperator",         &updateoperator,         {"operatorid", "name", "url", "state"}},
};

void RegisterOperatorsRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
