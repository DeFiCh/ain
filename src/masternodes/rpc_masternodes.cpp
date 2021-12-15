#include <masternodes/mn_rpc.h>

#include <pos_kernel.h>

// Here (but not a class method) just by similarity with other '..ToJSON'
UniValue mnToJSON(uint256 const & nodeId, CMasternode const& node, bool verbose, const std::set<std::pair<CKeyID, uint256>>& mnIds, const CWallet* pwallet)
{
    UniValue ret(UniValue::VOBJ);
    auto currentHeight = ChainActive().Height();
    if (!verbose) {
        ret.pushKV(nodeId.GetHex(), CMasternode::GetHumanReadableState(node.GetState(currentHeight)));
    }
    else {
        UniValue obj(UniValue::VOBJ);
        CTxDestination ownerDest = node.ownerType == 1 ? CTxDestination(PKHash(node.ownerAuthAddress)) :
                CTxDestination(WitnessV0KeyHash(node.ownerAuthAddress));
        obj.pushKV("ownerAuthAddress", EncodeDestination(ownerDest));
        CTxDestination operatorDest = node.operatorType == 1 ? CTxDestination(PKHash(node.operatorAuthAddress)) :
                                      CTxDestination(WitnessV0KeyHash(node.operatorAuthAddress));
        obj.pushKV("operatorAuthAddress", EncodeDestination(operatorDest));
        if (node.rewardAddressType != 0) {
            obj.pushKV("rewardAddress", EncodeDestination(
                node.rewardAddressType == 1 ? CTxDestination(PKHash(node.rewardAddress)) : CTxDestination(
                        WitnessV0KeyHash(node.rewardAddress))));
        }
        else {
            obj.pushKV("rewardAddress", EncodeDestination(CTxDestination()));
        }

        obj.pushKV("creationHeight", node.creationHeight);
        obj.pushKV("resignHeight", node.resignHeight);
        obj.pushKV("resignTx", node.resignTx.GetHex());
        obj.pushKV("banTx", node.banTx.GetHex());
        obj.pushKV("state", CMasternode::GetHumanReadableState(node.GetState(currentHeight)));
        obj.pushKV("mintedBlocks", (uint64_t) node.mintedBlocks);
        isminetype ownerMine = IsMineCached(*pwallet, ownerDest);
        obj.pushKV("ownerIsMine", bool(ownerMine & ISMINE_SPENDABLE));
        isminetype operatorMine = IsMineCached(*pwallet, operatorDest);
        obj.pushKV("operatorIsMine", bool(operatorMine & ISMINE_SPENDABLE));
        bool localMasternode{false};
        for (const auto& entry : mnIds) {
            if (entry.first == node.operatorAuthAddress) {
                localMasternode = true;
            }
        }
        obj.pushKV("localMasternode", localMasternode);

        uint16_t timelock = pcustomcsview->GetTimelock(nodeId, node, currentHeight);

        // Only get targetMultiplier for active masternodes
        if (node.IsActive(currentHeight)) {
            // Get block times with next block as height
            const auto subNodesBlockTime = pcustomcsview->GetBlockTimes(node.operatorAuthAddress, currentHeight + 1, node.creationHeight, timelock);

            if (currentHeight >= Params().GetConsensus().EunosPayaHeight) {
                const uint8_t loops = timelock == CMasternode::TENYEAR ? 4 : timelock == CMasternode::FIVEYEAR ? 3 : 2;
                UniValue multipliers(UniValue::VARR);
                for (uint8_t i{0}; i < loops; ++i) {
                    multipliers.push_back(pos::CalcCoinDayWeight(Params().GetConsensus(), GetTime(), subNodesBlockTime[i]).getdouble());
                }
                obj.pushKV("targetMultipliers", multipliers);
            } else {
                obj.pushKV("targetMultiplier", pos::CalcCoinDayWeight(Params().GetConsensus(), GetTime(),subNodesBlockTime[0]).getdouble());
            }
        }

        if (timelock) {
            obj.pushKV("timelock", strprintf("%d years", timelock / 52));
        }

        /// @todo add unlock height and|or real resign height
        ret.pushKV(nodeId.GetHex(), obj);
    }
    return ret;
}

CAmount EstimateMnCreationFee(int targetHeight) {
    // Current height + (1 day blocks) to avoid rejection;
    targetHeight += (60 * 60 / Params().GetConsensus().pos.nTargetSpacing);
    return GetMnCreationFee(targetHeight);
}

/*
 *
 *  Issued by: any
*/
UniValue createmasternode(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"createmasternode",
               "\nCreates (and submits to local node and network) a masternode creation transaction with given owner and operator addresses, spending the given inputs..\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address for keeping collateral amount (any P2PKH or P2WKH address) - used as owner key"},
                   {"operatorAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Optional (== ownerAddress) masternode operator auth address (P2PKH only, unique)"},
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
                   {"timelock", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Defaults to no timelock period so masternode can be resigned once active. To set a timelock period\n"
                                                                              "specify either FIVEYEARTIMELOCK or TENYEARTIMELOCK to create a masternode that cannot be resigned for\n"
                                                                              "five or ten years and will have 1.5x or 2.0 the staking power respectively. Be aware that this means\n"
                                                                              "that you cannot spend the collateral used to create a masternode for whatever period is specified."},
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                   HelpExampleCli("createmasternode", "ownerAddress operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("createmasternode", "ownerAddress operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, at least argument 1 must be non-null");
    }

    std::string ownerAddress = request.params[0].getValStr();
    std::string operatorAddress = request.params.size() > 1 && !request.params[1].getValStr().empty() ? request.params[1].getValStr() : ownerAddress;
    CTxDestination ownerDest = DecodeDestination(ownerAddress); // type will be checked on apply/create
    CTxDestination operatorDest = DecodeDestination(operatorAddress);

    bool eunosPaya;
    {
        LOCK(cs_main);
        eunosPaya = ::ChainActive().Tip()->height >= Params().GetConsensus().EunosPayaHeight;
    }

    // Get timelock if any
    uint16_t timelock{0};
    if (!request.params[3].isNull()) {
        if (!eunosPaya) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Timelock cannot be specified before EunosPaya hard fork");
        }
        std::string timelockStr = request.params[3].getValStr();
        if (timelockStr == "FIVEYEARTIMELOCK") {
            timelock = CMasternode::FIVEYEAR;
        } else if (timelockStr == "TENYEARTIMELOCK") {
            timelock = CMasternode::TENYEAR;
        }
    }

    // check type here cause need operatorAuthKey. all other validation (for owner for ex.) in further apply/create
    if (operatorDest.which() != 1 && operatorDest.which() != 4) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAddress (" + operatorAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    if (!::IsMine(*pwallet, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Address (%s) is not owned by the wallet", EncodeDestination(ownerDest)));
    }

    CKeyID const operatorAuthKey = operatorDest.which() == 1 ? CKeyID(*boost::get<PKHash>(&operatorDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest));

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateMasternode)
             << static_cast<char>(operatorDest.which()) << operatorAuthKey;

    if (eunosPaya) {
        metadata << timelock;
    }

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    auto scriptOwner = GetScriptForDestination(ownerDest);
    std::set<CScript> auths{scriptOwner};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2]);

    // Return change to owner address
    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(targetHeight), scriptOwner));

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue setforcedrewardaddress(const JSONRPCRequest& request)
{
    // Temporarily disabled for 2.2
    throw JSONRPCError(RPC_INVALID_REQUEST,
                           "reward address change is disabled for Fort Canning");

    auto pwallet = GetWallet(request);

    RPCHelpMan{"setforcedrewardaddress",
               "\nCreates (and submits to local node and network) a set forced reward address transaction with given masternode id and reward address\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The Masternode's ID"},
                   {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Masternode`s new reward address (any P2PKH or P2WKH address)"},
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
                   HelpExampleCli("setforcedrewardaddress", "mn_id rewardAddress '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("setforcedrewardaddress", "mn_id rewardAddress '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot update Masternode while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);

    // decode and verify
    std::string const nodeIdStr = request.params[0].getValStr();
    uint256 const nodeId = uint256S(nodeIdStr);
    CTxDestination ownerDest;
    int targetHeight;
    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        if (!nodePtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", nodeIdStr));
        }
        ownerDest = nodePtr->ownerType == PKHashType ?
            CTxDestination(PKHash(nodePtr->ownerAuthAddress)) :
            CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress));

        targetHeight = ::ChainActive().Height() + 1;
    }

    if (!::IsMine(*pwallet, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Masternode ownerAddress (%s) is not owned by the wallet", EncodeDestination(ownerDest)));
    }

    std::string rewardAddress = request.params[1].getValStr();
    CTxDestination rewardDest = DecodeDestination(rewardAddress);
    if (rewardDest.which() != PKHashType && rewardDest.which() != WitV0KeyHashType) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "rewardAddress (" + rewardAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    CKeyID const rewardAuthKey = rewardDest.which() == PKHashType ?
        CKeyID(*boost::get<PKHash>(&rewardDest)) :
        CKeyID(*boost::get<WitnessV0KeyHash>(&rewardDest)
    );

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{GetScriptForDestination(ownerDest)};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2]);

    // Return change to owner address
    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    CSetForcedRewardAddressMessage msg{nodeId, static_cast<char>(rewardDest.which()), rewardAuthKey};

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::SetForcedRewardAddress)
             << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue remforcedrewardaddress(const JSONRPCRequest& request)
{
    // Temporarily disabled for 2.2
    throw JSONRPCError(RPC_INVALID_REQUEST,
                           "reward address change is disabled for Fort Canning");
    
    auto pwallet = GetWallet(request);

    RPCHelpMan{"remforcedrewardaddress",
               "\nCreates (and submits to local node and network) a remove forced reward address transaction with given masternode id\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The Masternode's ID"},
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
                   HelpExampleCli("remforcedrewardaddress", "mn_id '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("remforcedrewardaddress", "mn_id '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot update Masternode while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VARR }, true);

    // decode and verify
    std::string const nodeIdStr = request.params[0].getValStr();
    uint256 const nodeId = uint256S(nodeIdStr);
    CTxDestination ownerDest;
    int targetHeight;
    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        if (!nodePtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", nodeIdStr));
        }
        ownerDest = nodePtr->ownerType == PKHashType ?
            CTxDestination(PKHash(nodePtr->ownerAuthAddress)) :
            CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress));

        targetHeight = ::ChainActive().Height() + 1;
    }

    if (!::IsMine(*pwallet, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Masternode ownerAddress (%s) is not owned by the wallet", EncodeDestination(ownerDest)));
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{GetScriptForDestination(ownerDest)};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[1]);

    // Return change to owner address
    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    CRemForcedRewardAddressMessage msg{nodeId};

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::RemForcedRewardAddress)
             << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue resignmasternode(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"resignmasternode",
               "\nCreates (and submits to local node and network) a transaction resigning your masternode. Collateral will be unlocked after " +
               std::to_string(GetMnResignDelay(::ChainActive().Height())) + " blocks.\n"
                                                    "The last optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the MN's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The Masternode's ID"},
                   {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects. Provide it if you want to spent specific UTXOs",
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
                   HelpExampleCli("resignmasternode", "mn_id '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("resignmasternode", "mn_id '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot resign Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VARR }, true);

    std::string const nodeIdStr = request.params[0].getValStr();
    uint256 const nodeId = uint256S(nodeIdStr);
    CTxDestination ownerDest;
    int targetHeight;
    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        if (!nodePtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", nodeIdStr));
        }
        ownerDest = nodePtr->ownerType == PKHashType ?
            CTxDestination(PKHash(nodePtr->ownerAuthAddress)) :
            CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress));

        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{GetScriptForDestination(ownerDest)};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[1]);

    // Return change to owner address
    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ResignMasternode)
             << nodeId;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue updatemasternode(const JSONRPCRequest& request)
{
    // Temporarily disabled for 2.2
    throw JSONRPCError(RPC_INVALID_REQUEST,
                           "updatemasternode is disabled for Fort Canning");

    auto pwallet = GetWallet(request);

    RPCHelpMan{"updatemasternode",
               "\nCreates (and submits to local node and network) a masternode update transaction which update the masternode operator addresses, spending the given inputs..\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The Masternode's ID"},
                   {"operatorAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "The new masternode operator auth address (P2PKH only, unique)"},
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
                   HelpExampleCli("updatemasternode", "mn_id operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("updatemasternode", "mn_id operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot update Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    bool forkCanning;
    {
        LOCK(cs_main);
        forkCanning = ::ChainActive().Tip()->height >= Params().GetConsensus().FortCanningHeight;
    }

    if (!forkCanning) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "updatemasternode cannot be called before Fortcanning hard fork");
    }

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);
    if (request.params[0].isNull() || request.params[1].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, at least argument 2 must be non-null");
    }

    std::string const nodeIdStr = request.params[0].getValStr();
    uint256 const nodeId = uint256S(nodeIdStr);
    CTxDestination ownerDest;
    int targetHeight;
    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        if (!nodePtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", nodeIdStr));
        }
        ownerDest = nodePtr->ownerType == 1 ? CTxDestination(PKHash(nodePtr->ownerAuthAddress)) : CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress));

        targetHeight = ::ChainActive().Height() + 1;
    }

    std::string operatorAddress = request.params[1].getValStr();
    CTxDestination operatorDest = DecodeDestination(operatorAddress);

    // check type here cause need operatorAuthKey. all other validation (for owner for ex.) in further apply/create
    if (operatorDest.which() != PKHashType && operatorDest.which() != WitV0KeyHashType) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAddress (" + operatorAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{GetScriptForDestination(ownerDest)};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2]);

    // Return change to owner address
    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    CKeyID const operatorAuthKey = operatorDest.which() == PKHashType ? CKeyID(*boost::get<PKHash>(&operatorDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest));

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UpdateMasternode)
             << nodeId
             << static_cast<char>(operatorDest.which()) << operatorAuthKey;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listmasternodes(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"listmasternodes",
               "\nReturns information about specified masternodes (or all, if list of ids is empty).\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                         {
                                 {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of orders to return, 1000000 by default"},
                         },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with masternodes information\n"
               },
               RPCExamples{
                       HelpExampleCli("listmasternodes", "'[mn_id]' false")
                       + HelpExampleRpc("listmasternodes", "'[mn_id]' false")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }
    // parse pagination
    size_t limit = 1000000;
    uint256 start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);
    const auto mnIds = pcustomcsview->GetOperatorsMulti();
    pcustomcsview->ForEachMasternode([&](uint256 const& nodeId, CMasternode node) {
        if (!including_start)
        {
            including_start = true;
            return (true);
        }
        ret.pushKVs(mnToJSON(nodeId, node, verbose, mnIds, pwallet));
        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue getmasternode(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"getmasternode",
               "\nReturns information about specified masternode.\n",
               {
                       {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Masternode's id"},
               },
               RPCResult{
                       "{id:{...}}     (object) Json object with masternode information\n"
               },
               RPCExamples{
                       HelpExampleCli("getmasternode", "mn_id")
                       + HelpExampleRpc("getmasternode", "mn_id")
               },
    }.Check(request);

    uint256 id = ParseHashV(request.params[0], "masternode id");

    LOCK(cs_main);
    const auto mnIds = pcustomcsview->GetOperatorsMulti();
    auto node = pcustomcsview->GetMasternode(id);
    if (node) {
        return mnToJSON(id, *node, true, mnIds, pwallet); // or maybe just node, w/o id?
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Masternode not found");
}

UniValue getmasternodeblocks(const JSONRPCRequest& request) {
    RPCHelpMan{"getmasternodeblocks",
               "\nReturns blocks generated by the specified masternode.\n",
               {
                     {"identifier", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG, "A json object containing one masternode identifying information",
                             {
                                     {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Masternode's id"},
                                     {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Masternode owner address"},
                                     {"operatorAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Masternode operator address"},
                             },
                      },
                      {"depth", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Maximum depth, from the genesis block is the default"},
               },
               RPCResult{
                       "{...}     (object) Json object with block hash and height information\n"
               },
               RPCExamples{
                       HelpExampleCli("getmasternodeblocks", R"('{"ownerAddress":"dPyup5C9hfRd2SUC1p3a7VcjcNuGSXa9bT"}')")
                       + HelpExampleRpc("getmasternodeblocks", R"({"ownerAddress":"dPyup5C9hfRd2SUC1p3a7VcjcNuGSXa9bT"})")
               },
    }.Check(request);

    UniValue identifier = request.params[0].get_obj();
    int idCount{0};
    uint256 mn_id;

    if (!identifier["id"].isNull()) {
        mn_id = ParseHashV(identifier["id"], "id");
        ++idCount;
    }

    LOCK(cs_main);

    if (!identifier["ownerAddress"].isNull()) {
        CKeyID ownerAddressID;
        auto ownerAddress = identifier["ownerAddress"].getValStr();
        auto ownerDest = DecodeDestination(ownerAddress);
        if (ownerDest.which() == 1) {
            ownerAddressID = CKeyID(*boost::get<PKHash>(&ownerDest));
        } else if (ownerDest.which() == WitV0KeyHashType) {
            ownerAddressID = CKeyID(*boost::get<WitnessV0KeyHash>(&ownerDest));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid P2PKH address");
        }
        auto node = pcustomcsview->GetMasternodeIdByOwner(ownerAddressID);
        if (!node) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Masternode not found");
        }
        mn_id = *node;
        ++idCount;
    }

    if (!identifier["operatorAddress"].isNull()) {
        CKeyID operatorAddressID;
        auto operatorAddress = identifier["operatorAddress"].getValStr();
        auto operatorDest = DecodeDestination(operatorAddress);
        if (operatorDest.which() == 1) {
            operatorAddressID = CKeyID(*boost::get<PKHash>(&operatorDest));
        } else if (operatorDest.which() == WitV0KeyHashType) {
            operatorAddressID = CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid P2PKH address");
        }
        auto node = pcustomcsview->GetMasternodeIdByOperator(operatorAddressID);
        if (!node) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Masternode not found");
        }
        mn_id = *node;
        ++idCount;
    }

    if (idCount == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Provide masternode identifier information");
    }

    if (idCount > 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Only provide one identifier information");
    }

    auto masternode = pcustomcsview->GetMasternode(mn_id);
    if (!masternode) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Masternode not found");
    }

    auto lastHeight = ::ChainActive().Tip()->height + 1;
    const auto creationHeight = masternode->creationHeight;

    int depth{std::numeric_limits<int>::max()};
    if (!request.params[1].isNull()) {
        depth = request.params[1].get_int();
    }

    UniValue ret(UniValue::VOBJ);

    auto masternodeBlocks = [&](const uint256& masternodeID, uint32_t blockHeight) {
        if (masternodeID != mn_id) {
            return false;
        }

        if (blockHeight <= creationHeight) {
            return true;
        }

        if (auto tip = ::ChainActive()[blockHeight]) {
            lastHeight = tip->nHeight;
            ret.pushKV(std::to_string(lastHeight), tip->GetBlockHash().ToString());
        }

        return true;
    };

    pcustomcsview->ForEachSubNode([&](const SubNodeBlockTimeKey &key, CLazySerialize<int64_t>){
        return masternodeBlocks(key.masternodeID, key.blockHeight);
    }, SubNodeBlockTimeKey{mn_id, 0, std::numeric_limits<uint32_t>::max()});

    pcustomcsview->ForEachMinterNode([&](MNBlockTimeKey const & key, CLazySerialize<int64_t>) {
        return masternodeBlocks(key.masternodeID, key.blockHeight);
    }, MNBlockTimeKey{mn_id, std::numeric_limits<uint32_t>::max()});

    auto tip = ::ChainActive()[std::min(lastHeight, uint64_t(Params().GetConsensus().DakotaCrescentHeight)) - 1];

    for (; tip && tip->nHeight > creationHeight && depth > 0; tip = tip->pprev, --depth) {
        auto id = pcustomcsview->GetMasternodeIdByOperator(tip->minterKey());
        if (id && *id == mn_id) {
            ret.pushKV(std::to_string(tip->nHeight), tip->GetBlockHash().ToString());
        }
    }

    return ret;
}

UniValue getanchorteams(const JSONRPCRequest& request)
{
    RPCHelpMan{"getanchorteams",
               "\nReturns the auth and confirm anchor masternode teams at current or specified height\n",
               {
                    {"blockHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The height of block which contain tx"}
               },
               RPCResult{
                       "{\"auth\":[Address,...],\"confirm\":[Address,...]} Two sets of masternode operator addresses\n"
               },
               RPCExamples{
                       HelpExampleCli("getanchorteams", "1005")
                       + HelpExampleRpc("getanchorteams", "1005")
               },
    }.Check(request);

    int blockHeight;

    LOCK(cs_main);
    if (!request.params[0].isNull()) {
        blockHeight = request.params[0].get_int();
    } else {
        blockHeight = ::ChainActive().Height();
    }

    const auto authTeam = pcustomcsview->GetAuthTeam(blockHeight);
    const auto confirmTeam = pcustomcsview->GetConfirmTeam(blockHeight);

    UniValue result(UniValue::VOBJ);
    UniValue authRes(UniValue::VARR);
    UniValue confirmRes(UniValue::VARR);

    if (authTeam) {
        for (const auto& hash160 : *authTeam) {
            const auto id = pcustomcsview->GetMasternodeIdByOperator(hash160);
            if (id) {
                const auto mn = pcustomcsview->GetMasternode(*id);
                if (mn) {
                    auto dest = mn->operatorType == 1 ? CTxDestination(PKHash(hash160)) : CTxDestination(WitnessV0KeyHash(hash160));
                    authRes.push_back(EncodeDestination(dest));
                }
            }
        }
    }

    if (confirmTeam) {
        for (const auto& hash160 : *confirmTeam) {
            const auto id = pcustomcsview->GetMasternodeIdByOperator(hash160);
            if (id) {
                const auto mn = pcustomcsview->GetMasternode(*id);
                if (mn) {
                    auto dest = mn->operatorType == 1 ? CTxDestination(PKHash(hash160)) : CTxDestination(WitnessV0KeyHash(hash160));
                    confirmRes.push_back(EncodeDestination(dest));
                }
            }
        }
    }

    result.pushKV("auth", authRes);
    result.pushKV("confirm", confirmRes);

    return result;
}


UniValue getactivemasternodecount(const JSONRPCRequest& request)
{
    RPCHelpMan{"getactivemasternodecount",
               "\nReturn number of unique masternodes in the last specified number of blocks\n",
               {
                    {"blockCount", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The number of blocks to check for unique masternodes. (Default: 20160)"}
               },
               RPCResult{
                       "n    (numeric) Number of unique masternodes seen\n"
               },
               RPCExamples{
                       HelpExampleCli("getactivemasternodecount", "20160")
                       + HelpExampleRpc("getactivemasternodecount", "20160")
               },
    }.Check(request);

    int blockSample{7 * 2880}; // One week
    if (!request.params[0].isNull()) {
        blockSample = request.params[0].get_int();
    }

    std::set<uint256> masternodes;

    LOCK(cs_main);
    auto pindex = ::ChainActive().Tip();
    // Get active MNs from last week's worth of blocks
    for (int i{0}; pindex && i < blockSample; pindex = pindex->pprev, ++i) {
        if (auto id = pcustomcsview->GetMasternodeIdByOperator(pindex->minterKey())) {
            masternodes.insert(*id);
        }
    }

    return static_cast<uint64_t>(masternodes.size());
}

UniValue listanchors(const JSONRPCRequest& request)
{
    RPCHelpMan{"listanchors",
               "\nList anchors (if any)\n",
               {
               },
               RPCResult{
                       "\"array\"                  Returns array of anchors\n"
               },
               RPCExamples{
                       HelpExampleCli("listanchors", "")
                       + HelpExampleRpc("listanchors", "")
               },
    }.Check(request);

    LOCK(cs_main);
    auto confirms = pcustomcsview->CAnchorConfirmsView::GetAnchorConfirmData();

    std::sort(confirms.begin(), confirms.end(), [](CAnchorConfirmDataPlus a, CAnchorConfirmDataPlus b) {
        return a.anchorHeight < b.anchorHeight;
    });

    UniValue result(UniValue::VARR);
    for (const auto& item : confirms) {
        auto defiHash = pcustomcsview->GetRewardForAnchor(item.btcTxHash);

        CTxDestination rewardDest = item.rewardKeyType == 1 ? CTxDestination(PKHash(item.rewardKeyID)) : CTxDestination(WitnessV0KeyHash(item.rewardKeyID));
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("anchorHeight", static_cast<int>(item.anchorHeight));
        entry.pushKV("anchorHash", item.dfiBlockHash.ToString());
        entry.pushKV("rewardAddress", EncodeDestination(rewardDest));
        entry.pushKV("dfiRewardHash", defiHash->ToString());
        entry.pushKV("btcAnchorHeight", static_cast<int>(item.btcTxHeight));
        entry.pushKV("btcAnchorHash", item.btcTxHash.ToString());
        entry.pushKV("confirmSignHash", item.GetSignHash().ToString());

        result.push_back(entry);
    }

    return result;
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  --------------- ----------------------   ---------------------   ----------
    {"masternodes", "createmasternode",      &createmasternode,      {"ownerAddress", "operatorAddress", "inputs"}},
    {"masternodes", "resignmasternode",      &resignmasternode,      {"mn_id", "inputs"}},
    //{"masternodes", "updatemasternode",      &updatemasternode,      {"mn_id", "operatorAddress", "inputs"}},
    {"masternodes", "listmasternodes",       &listmasternodes,       {"pagination", "verbose"}},
    {"masternodes", "getmasternode",         &getmasternode,         {"mn_id"}},
    {"masternodes", "getmasternodeblocks",   &getmasternodeblocks,   {"identifier", "depth"}},
    {"masternodes", "getanchorteams",        &getanchorteams,        {"blockHeight"}},
    {"masternodes", "getactivemasternodecount",  &getactivemasternodecount,  {"blockCount"}},
    {"masternodes", "listanchors",           &listanchors,           {}},
    //{"masternodes", "setforcedrewardaddress", &setforcedrewardaddress, {"mn_id", "rewardAddress", "inputs"}},
    //{"masternodes", "remforcedrewardaddress", &remforcedrewardaddress, {"mn_id", "inputs"}},
};

void RegisterMasternodesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
