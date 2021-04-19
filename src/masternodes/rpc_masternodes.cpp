#include <masternodes/mn_rpc.h>

#include <pos_kernel.h>

// Here (but not a class method) just by similarity with other '..ToJSON'
UniValue mnToJSON(uint256 const & nodeId, CMasternode const& node, bool verbose)
{
    UniValue ret(UniValue::VOBJ);
    if (!verbose) {
        ret.pushKV(nodeId.GetHex(), CMasternode::GetHumanReadableState(node.GetState()));
    }
    else {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("ownerAuthAddress", EncodeDestination(
                node.ownerType == 1 ? CTxDestination(PKHash(node.ownerAuthAddress)) : CTxDestination(
                        WitnessV0KeyHash(node.ownerAuthAddress))));
        obj.pushKV("operatorAuthAddress", EncodeDestination(
                node.operatorType == 1 ? CTxDestination(PKHash(node.operatorAuthAddress)) : CTxDestination(
                        WitnessV0KeyHash(node.operatorAuthAddress))));

        obj.pushKV("creationHeight", node.creationHeight);
        obj.pushKV("resignHeight", node.resignHeight);
        obj.pushKV("resignTx", node.resignTx.GetHex());
        obj.pushKV("banHeight", node.banHeight);
        obj.pushKV("banTx", node.banTx.GetHex());
        obj.pushKV("state", CMasternode::GetHumanReadableState(node.GetState()));
        obj.pushKV("mintedBlocks", (uint64_t) node.mintedBlocks);
        obj.pushKV("targetMultiplier", pos::CalcCoinDayWeight(Params().GetConsensus(), node, GetTime()).getdouble());

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
    CWallet* const pwallet = GetWallet(request);

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
    LockedCoinsScopedGuard lcGuard(pwallet); // no need here, but for symmetry

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, at least argument 1 must be non-null");
    }

    std::string ownerAddress = request.params[0].getValStr();
    std::string operatorAddress = request.params.size() > 1 ? request.params[1].getValStr() : ownerAddress;
    CTxDestination ownerDest = DecodeDestination(ownerAddress); // type will be checked on apply/create
    CTxDestination operatorDest = DecodeDestination(operatorAddress);

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

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    if (request.params.size() > 2) {
        rawTx.vin = GetInputs(request.params[2].get_array());
    }

    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(targetHeight), GetScriptForDestination(ownerDest)));

    fund(rawTx, pwallet, {});

    // check execution
    {
        LOCK(cs_main);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, static_cast<char>(operatorDest.which()), operatorAuthKey});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CCreateMasterNodeMessage{});
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue resignmasternode(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"resignmasternode",
               "\nCreates (and submits to local node and network) a transaction resigning your masternode. Collateral will be unlocked after " +
               std::to_string(GetMnResignDelay()) + " blocks.\n"
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
    LockedCoinsScopedGuard lcGuard(pwallet);

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
        ownerDest = nodePtr->ownerType == 1 ? CTxDestination(PKHash(nodePtr->ownerAuthAddress)) : CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress));

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
    {
        LOCK(cs_main);
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, nodeId});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CResignMasterNodeMessage{}, coins);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listmasternodes(const JSONRPCRequest& request)
{
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
            if (!including_start) {
                start = ArithToUint256(UintToArith256(start) + arith_uint256{1});
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);
    pcustomcsview->ForEachMasternode([&](uint256 const& nodeId, CMasternode node) {
        ret.pushKVs(mnToJSON(nodeId, node, verbose));
        limit--;
        return limit != 0;
    }, start);

    return ret;
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

    uint256 id = ParseHashV(request.params[0], "masternode id");

    LOCK(cs_main);
    auto node = pcustomcsview->GetMasternode(id);
    if (node) {
        return mnToJSON(id, *node, true); // or maybe just node, w/o id?
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
                       HelpExampleCli("getmasternodeblocks", "{\"ownerAddress\":\"dPyup5C9hfRd2SUC1p3a7VcjcNuGSXa9bT\"}")
                       + HelpExampleRpc("getmasternodeblocks", "{\"ownerAddress\":\"dPyup5C9hfRd2SUC1p3a7VcjcNuGSXa9bT\"}")
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

    pcustomcsview->ForEachMinterNode([&](MNBlockTimeKey const & key, CLazySerialize<int64_t>) {
        if (key.masternodeID != mn_id) {
            return false;
        }

        if (auto tip = ::ChainActive()[key.blockHeight]) {
            lastHeight = tip->height;
            ret.pushKV(std::to_string(tip->height), tip->GetBlockHash().ToString());
        }

        return true;
    }, MNBlockTimeKey{mn_id, std::numeric_limits<uint32_t>::max()});

    auto tip = ::ChainActive()[std::min(lastHeight, uint64_t(Params().GetConsensus().DakotaCrescentHeight)) - 1];

    for (; tip && tip->height > creationHeight && depth > 0; tip = tip->pprev, --depth) {
        CKeyID minter;
        if (tip->GetBlockHeader().ExtractMinterKey(minter)) {
            auto id = pcustomcsview->GetMasternodeIdByOperator(minter);
            if (id && *id == mn_id) {
                ret.pushKV(std::to_string(tip->height), tip->GetBlockHash().ToString());
            }
        }
    }

    return ret;
}

UniValue listcriminalproofs(const JSONRPCRequest& request)
{
    RPCHelpMan{"listcriminalproofs",
               "\nReturns information about criminal proofs (pairs of signed blocks by one MN from different forks).\n",
               {
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                         {
                             {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                              "Optional first key to iterate from, in lexicographical order."
                              "Typically it's set to last ID from previous request."},
                             {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                              "If true, then iterate including starting position. False by default"},
                             {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                              "Maximum number of orders to return, 100 by default"},
                         },
                    },
               },
               RPCResult{
                       "{id:{block1, block2},...}     (array) Json objects with block pairs\n"
               },
               RPCExamples{
                       HelpExampleCli("listcriminalproofs", "")
                       + HelpExampleRpc("listcriminalproofs", "")
               },
    }.Check(request);

    // parse pagination
    size_t limit = 100;
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
            if (!including_start) {
                start = ArithToUint256(UintToArith256(start) + arith_uint256{1});
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);
    auto const proofs = pcriminals->GetUnpunishedCriminals();
    for (auto it = proofs.lower_bound(start); it != proofs.end() && limit != 0; ++it, --limit) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("hash1", it->second.blockHeader.GetHash().ToString());
        obj.pushKV("height1", it->second.blockHeader.height);
        obj.pushKV("hash2", it->second.conflictBlockHeader.GetHash().ToString());
        obj.pushKV("height2", it->second.conflictBlockHeader.height);
        obj.pushKV("mintedBlocks", it->second.blockHeader.mintedBlocks);
        ret.pushKV(it->first.ToString(), obj);
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

    const CBlockIndex* pindex{nullptr};
    {
        LOCK(cs_main);
        pindex = ::ChainActive().Tip();
    }

    int blockSample{7 * 2880}; // One week
    if (!request.params[0].isNull()) {
        blockSample = request.params[0].get_int();
    }

    std::set<uint256> masternodes;

    // Get active MNs from last week's worth of blocks
    for (int i{0}; pindex && i < blockSample; pindex = pindex->pprev, ++i) {
        CKeyID minter;
        if (pindex->GetBlockHeader().ExtractMinterKey(minter)) {
            LOCK(cs_main);
            auto id = pcustomcsview->GetMasternodeIdByOperator(minter);
            if (id) {
                masternodes.insert(*id);
            }
        }
    }

    return static_cast<uint64_t>(masternodes.size());
}

UniValue listanchors(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

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

    auto locked_chain = pwallet->chain().lock();

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
        if (item.dfiBlockHash != uint256()) {
            entry.pushKV("anchorHash", item.dfiBlockHash.ToString());
        }
        entry.pushKV("rewardAddress", EncodeDestination(rewardDest));
        if (defiHash) {
            entry.pushKV("dfiRewardHash", defiHash->ToString());
        }
        if (item.btcTxHeight != 0) {
            entry.pushKV("btcAnchorHeight", static_cast<int>(item.btcTxHeight));
        }
        entry.pushKV("btcAnchorHash", item.btcTxHash.ToString());

        if (item.dfiBlockHash != uint256() && item.btcTxHeight != 0) {
            entry.pushKV("confirmSignHash", item.GetSignHash().ToString());
        } else {
            entry.pushKV("confirmSignHash", static_cast<const CAnchorConfirmData &>(item).GetSignHash().ToString());
        }

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
    {"masternodes", "listmasternodes",       &listmasternodes,       {"pagination", "verbose"}},
    {"masternodes", "getmasternode",         &getmasternode,         {"mn_id"}},
    {"masternodes", "getmasternodeblocks",   &getmasternodeblocks,   {"identifier", "depth"}},
    {"masternodes", "listcriminalproofs",    &listcriminalproofs,    {}},
    {"masternodes", "getanchorteams",        &getanchorteams,        {"blockHeight"}},
    {"masternodes", "getactivemasternodecount",  &getactivemasternodecount,  {"blockCount"}},
    {"masternodes", "listanchors",           &listanchors,           {}},
};

void RegisterMasternodesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
