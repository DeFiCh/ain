#include <masternodes/mn_rpc.h>

#include <pos_kernel.h>

// Here (but not a class method) just by similarity with other '..ToJSON'
UniValue mnToJSON(CCustomCSView& view, uint256 const & nodeId, CMasternode const& node, bool verbose, const std::set<std::pair<CKeyID, uint256>>& mnIds, const CWallet* pwallet)
{
    UniValue ret(UniValue::VOBJ);
    auto currentHeight = ChainActive().Height();
    if (!verbose) {
        ret.pushKV(nodeId.GetHex(), CMasternode::GetHumanReadableState(node.GetState(currentHeight, view)));
    }
    else {
        UniValue obj(UniValue::VOBJ);
        CTxDestination ownerDest = FromOrDefaultKeyIDToDestination(node.ownerType, node.ownerAuthAddress, KeyType::MNKeyType);
        obj.pushKV("ownerAuthAddress", EncodeDestination(ownerDest));
        CTxDestination operatorDest = FromOrDefaultKeyIDToDestination(node.operatorType, node.operatorAuthAddress, KeyType::MNKeyType);
        obj.pushKV("operatorAuthAddress", EncodeDestination(operatorDest));
        if (node.rewardAddressType != 0) {
            obj.pushKV("rewardAddress", EncodeDestination(FromOrDefaultKeyIDToDestination(node.rewardAddressType, node.rewardAddress, KeyType::MNRewardKeyType)));
        }
        else {
            obj.pushKV("rewardAddress", EncodeDestination(CTxDestination()));
        }

        obj.pushKV("creationHeight", node.creationHeight);
        obj.pushKV("resignHeight", node.resignHeight);
        obj.pushKV("resignTx", node.resignTx.GetHex());
        obj.pushKV("collateralTx", node.collateralTx.GetHex());
        obj.pushKV("state", CMasternode::GetHumanReadableState(node.GetState(currentHeight, view)));
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

        const auto timelock = pcustomcsview->GetTimelock(nodeId, node, currentHeight);

        // Only get targetMultiplier for active masternodes
        if (timelock && node.IsActive(currentHeight, view)) {
            // Get block times with next block as height
            const auto subNodesBlockTime = pcustomcsview->GetBlockTimes(node.operatorAuthAddress, currentHeight + 1, node.creationHeight, *timelock);

            if (currentHeight >= Params().GetConsensus().EunosPayaHeight) {
                const uint8_t loops = *timelock == CMasternode::TENYEAR ? 4 : *timelock == CMasternode::FIVEYEAR ? 3 : 2;
                UniValue multipliers(UniValue::VARR);
                for (uint8_t i{0}; i < loops; ++i) {
                    multipliers.push_back(pos::CalcCoinDayWeight(Params().GetConsensus(), GetTime(), subNodesBlockTime[i]).getdouble());
                }
                obj.pushKV("targetMultipliers", multipliers);
            } else {
                obj.pushKV("targetMultiplier", pos::CalcCoinDayWeight(Params().GetConsensus(), GetTime(),subNodesBlockTime[0]).getdouble());
            }
        }

        if (timelock && *timelock) {
            obj.pushKV("timelock", strprintf("%d years", *timelock / 52));
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
        eunosPaya = ::ChainActive().Tip()->nHeight >= Params().GetConsensus().EunosPayaHeight;
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
    if (operatorDest.index() != 1 && operatorDest.index() != 4) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAddress (" + operatorAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    if (!::IsMine(*pwallet, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Address (%s) is not owned by the wallet", EncodeDestination(ownerDest)));
    }

    CKeyID const operatorAuthKey = CKeyID::FromOrDefaultDestination(operatorDest, KeyType::MNKeyType);
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateMasternode)
             << static_cast<char>(operatorDest.index()) << operatorAuthKey;

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
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2], request.metadata.coinSelectOpts);

    // Return change to owner address
    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(targetHeight), scriptOwner));

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

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
    CTxDestination ownerDest, collateralDest;
    int targetHeight;
    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        if (!nodePtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", nodeIdStr));
        }

        ownerDest = FromOrDefaultKeyIDToDestination(nodePtr->ownerType, nodePtr->ownerAuthAddress, KeyType::MNKeyType);
        if (!nodePtr->collateralTx.IsNull()) {
            const auto& coin = ::ChainstateActive().CoinsTip().AccessCoin({nodePtr->collateralTx, 1});
            if (coin.IsSpent() || !ExtractDestination(coin.out.scriptPubKey, collateralDest)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode collateral not available");
            }
        }

        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{GetScriptForDestination(ownerDest)};
    if (collateralDest.index() != 0) {
        auths.insert(GetScriptForDestination(collateralDest));
    }
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[1], request.metadata.coinSelectOpts);

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

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue updatemasternode(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan{"updatemasternode",
               "\nCreates (and submits to local node and network) a masternode update transaction which update the masternode operator addresses, spending the given inputs..\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The Masternode's ID"},
                   {"values", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                       {
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The new masternode owner address, requires masternode collateral fee (P2PKH or P2WPKH)"},
                            {"operatorAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The new masternode operator address (P2PKH or P2WPKH)"},
                            {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Masternode`s new reward address, empty \"\" to remove reward address."},
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
                   HelpExampleCli("updatemasternode", "mn_id operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("updatemasternode", "mn_id operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot update Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VOBJ, UniValue::VARR }, true);

    const std::string nodeIdStr = request.params[0].getValStr();
    const uint256 nodeId = uint256S(nodeIdStr);
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

    CTxDestination newOwnerDest, operatorDest, rewardDest;

    UniValue metaObj = request.params[1].get_obj();
    if (!metaObj["ownerAddress"].isNull()) {
        newOwnerDest = DecodeDestination(metaObj["ownerAddress"].getValStr());
        if (newOwnerDest.index() != PKHashType && newOwnerDest.index() != WitV0KeyHashType) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "ownerAddress (" + metaObj["ownerAddress"].getValStr() +
                                                      ") does not refer to a P2PKH or P2WPKH address");
        }
    }

    if (!metaObj["operatorAddress"].isNull()) {
        operatorDest = DecodeDestination(metaObj["operatorAddress"].getValStr());
        if (operatorDest.index() != PKHashType && operatorDest.index() != WitV0KeyHashType) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAddress (" + metaObj["operatorAddress"].getValStr() + ") does not refer to a P2PKH or P2WPKH address");
        }
    }

    std::string rewardAddress;
    if (!metaObj["rewardAddress"].isNull()) {
        rewardAddress = metaObj["rewardAddress"].getValStr();
        if (!rewardAddress.empty()) {
            rewardDest = DecodeDestination(rewardAddress);
            if (rewardDest.index() != PKHashType && rewardDest.index() != ScriptHashType && rewardDest.index() != WitV0KeyHashType) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "rewardAddress (" + rewardAddress + ") does not refer to a P2SH, P2PKH or P2WPKH address");
            }
        }
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{GetScriptForDestination(ownerDest)};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2], request.metadata.coinSelectOpts);

    // Return change to owner address
    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    CUpdateMasterNodeMessage msg{nodeId};

    if (!metaObj["ownerAddress"].isNull()) {
        msg.updates.emplace_back(static_cast<uint8_t>(UpdateMasternodeType::OwnerAddress), std::pair<char, std::vector<unsigned char>>());
    }

    if (!metaObj["operatorAddress"].isNull()) {
        const CKeyID keyID = CKeyID::FromOrDefaultDestination(operatorDest, KeyType::MNKeyType);
        msg.updates.emplace_back(static_cast<uint8_t>(UpdateMasternodeType::OperatorAddress), std::make_pair(static_cast<char>(operatorDest.index()), std::vector<unsigned char>(keyID.begin(), keyID.end())));
    }

    if (!metaObj["rewardAddress"].isNull()) {
        if (rewardAddress.empty()) {
            msg.updates.emplace_back(static_cast<uint8_t>(UpdateMasternodeType::RemRewardAddress), std::pair<char, std::vector<unsigned char>>());
        } else {
            const CKeyID keyID = CKeyID::FromOrDefaultDestination(rewardDest, KeyType::MNRewardKeyType);
            msg.updates.emplace_back(static_cast<uint8_t>(UpdateMasternodeType::SetRewardAddress), std::make_pair(static_cast<char>(rewardDest.index()), std::vector<unsigned char>(keyID.begin(), keyID.end())));
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UpdateMasternode)
             << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.emplace_back(0, scriptMeta);

    // Add new owner collateral
    if (!metaObj["ownerAddress"].isNull()) {
        if (const auto node = pcustomcsview->GetMasternode(nodeId)) {
            rawTx.vin.emplace_back(node->collateralTx.IsNull() ? nodeId : node->collateralTx, 1);
            rawTx.vout.emplace_back(GetMnCollateralAmount(targetHeight), GetScriptForDestination(newOwnerDest));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("masternode %s does not exists", nodeIdStr));
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

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

    SyncWithValidationInterfaceQueue();

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

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
        ret.pushKVs(mnToJSON(*pcustomcsview, nodeId, node, verbose, mnIds, pwallet));
        limit--;
        return limit != 0;
    }, start);

    return GetRPCResultCache().Set(request, ret);
}

UniValue getmasternode(const JSONRPCRequest& request)
{
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

    SyncWithValidationInterfaceQueue();

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;
    auto pwallet = GetWallet(request);

    uint256 id = ParseHashV(request.params[0], "masternode id");

    LOCK(cs_main);
    const auto mnIds = pcustomcsview->GetOperatorsMulti();
    auto node = pcustomcsview->GetMasternode(id);
    if (node) {
        auto res = mnToJSON(*pcustomcsview, id, *node, true, mnIds, pwallet); // or maybe just node, w/o id?
        return GetRPCResultCache().Set(request, res);
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

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

    UniValue identifier = request.params[0].get_obj();
    int idCount{0};
    uint256 mn_id{};

    if (!identifier["id"].isNull()) {
        mn_id = ParseHashV(identifier["id"], "id");
        ++idCount;
    }

    LOCK(cs_main);

    if (!identifier["ownerAddress"].isNull()) {
        CKeyID ownerAddressID;
        auto ownerAddress = identifier["ownerAddress"].getValStr();
        auto ownerDest = DecodeDestination(ownerAddress);
        if (ownerDest.index() == 1) {
            ownerAddressID = CKeyID(std::get<PKHash>(ownerDest));
        } else if (ownerDest.index() == WitV0KeyHashType) {
            ownerAddressID = CKeyID(std::get<WitnessV0KeyHash>(ownerDest));
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
        if (operatorDest.index() == 1) {
            operatorAddressID = CKeyID(std::get<PKHash>(operatorDest));
        } else if (operatorDest.index() == WitV0KeyHashType) {
            operatorAddressID = CKeyID(std::get<WitnessV0KeyHash>(operatorDest));
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

    int depth{std::numeric_limits<int>::max()};
    if (!request.params[1].isNull()) {
        depth = request.params[1].get_int();
    }

    int lastHeight{};
    const auto creationHeight = masternode->creationHeight;
    std::map<int, uint256, std::greater<>> mintedBlocks;
    const auto currentHeight = ::ChainActive().Height();
    depth = std::min(depth, currentHeight);
    auto startBlock = currentHeight - depth;

    auto masternodeBlocks = [&](const uint256& masternodeID, int blockHeight) {
        if (masternodeID != mn_id) {
            return false;
        }

        if (blockHeight <= creationHeight) {
            return true;
        }

        auto tip = ::ChainActive()[blockHeight];
        if (tip) {
            lastHeight = tip->nHeight;
            mintedBlocks.emplace(lastHeight, tip->GetBlockHash());
        }

        return true;
    };

    pcustomcsview->ForEachSubNode([&](const SubNodeBlockTimeKey &key, CLazySerialize<int64_t>){
        return masternodeBlocks(key.masternodeID, key.blockHeight);
    }, SubNodeBlockTimeKey{mn_id, 0, std::numeric_limits<uint32_t>::max()});

    pcustomcsview->ForEachMinterNode([&](MNBlockTimeKey const & key, CLazySerialize<int64_t>) {
        return masternodeBlocks(key.masternodeID, key.blockHeight);
    }, MNBlockTimeKey{mn_id, std::numeric_limits<uint32_t>::max()});

    auto tip = ::ChainActive()[std::min(lastHeight, Params().GetConsensus().DakotaCrescentHeight) - 1];

    for (; tip && tip->nHeight > creationHeight && tip->nHeight > startBlock; tip = tip->pprev) {
        auto id = pcustomcsview->GetMasternodeIdByOperator(tip->minterKey());
        if (id && *id == mn_id) {
            mintedBlocks.emplace(tip->nHeight, tip->GetBlockHash());
        }
    }

    UniValue ret(UniValue::VOBJ);
    for (const auto& [height, hash] : mintedBlocks) {
        if (height <= currentHeight - depth) {
            break;
        }
        ret.pushKV(std::to_string(height), hash.ToString());
    }

    return GetRPCResultCache().Set(request, ret);
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

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

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

    return GetRPCResultCache().Set(request, result);
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
    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

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

    auto res = static_cast<uint64_t>(masternodes.size());
    return GetRPCResultCache().Set(request, res);
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

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

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

    return GetRPCResultCache().Set(request, result);
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  --------------- ----------------------   ---------------------   ----------
    {"masternodes", "createmasternode",      &createmasternode,      {"ownerAddress", "operatorAddress", "inputs"}},
    {"masternodes", "resignmasternode",      &resignmasternode,      {"mn_id", "inputs"}},
    {"masternodes", "updatemasternode",      &updatemasternode,      {"mn_id", "values", "inputs"}},
    {"masternodes", "listmasternodes",       &listmasternodes,       {"pagination", "verbose"}},
    {"masternodes", "getmasternode",         &getmasternode,         {"mn_id"}},
    {"masternodes", "getmasternodeblocks",   &getmasternodeblocks,   {"identifier", "depth"}},
    {"masternodes", "getanchorteams",        &getanchorteams,        {"blockHeight"}},
    {"masternodes", "getactivemasternodecount",  &getactivemasternodecount,  {"blockCount"}},
    {"masternodes", "listanchors",           &listanchors,           {}},
};

void RegisterMasternodesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
