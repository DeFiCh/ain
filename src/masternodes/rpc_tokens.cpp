#include <masternodes/mn_rpc.h>
#include <index/txindex.h>

UniValue createtoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createtoken",
               "\nCreates (and submits to local node and network) a token creation transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO,
                             "Token's symbol (unique), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                            {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                             "Token's name (optional), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                            {"isDAT", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                             "Token's 'isDAT' property (bool, optional), default is 'False'"},
                            {"decimal", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                             "Token's decimal places (optional, fixed to 8 for now, unchecked)"},
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                             "Token's total supply limit (optional, zero for now, unchecked)"},
                            {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                             "Token's 'Mintable' property (bool, optional), default is 'True'"},
                            {"tradeable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                             "Token's 'Tradeable' property (bool, optional), default is 'True'"},
                            {"collateralAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
                             "Any valid destination for keeping collateral amount - used as token's owner auth"},
                        },
                    },
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
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
                       HelpExampleCli("createtoken", "'{\"symbol\":\"MyToken\","
                                                     "\"collateralAddress\":\"address\"}'")
                       + HelpExampleCli("createtoken", "'{\"symbol\":\"MyToken\","
                                                     "\"collateralAddress\":\"address\"}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("createtoken", "'{\"symbol\":\"MyToken\","
                                                       "\"collateralAddress\":\"address\"}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create token while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"symbol\",\"collateralAddress\"}");
    }

    const UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    std::string collateralAddress = metaObj["collateralAddress"].getValStr();
    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    if (collateralDest.which() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress (" + collateralAddress + ") does not refer to any valid address");
    }

    CToken token;
    token.symbol = trim_ws(metaObj["symbol"].getValStr()).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(metaObj["name"].getValStr()).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.flags = metaObj["isDAT"].getBool() ? token.flags | (uint8_t)CToken::TokenFlags::DAT : token.flags; // setting isDAT

    if (!metaObj["tradeable"].isNull()) {
        token.flags = metaObj["tradeable"].getBool() ?
            token.flags | uint8_t(CToken::TokenFlags::Tradeable) :
            token.flags & ~uint8_t(CToken::TokenFlags::Tradeable);
    }
    if (!metaObj["mintable"].isNull()) {
        token.flags = metaObj["mintable"].getBool() ?
            token.flags | uint8_t(CToken::TokenFlags::Mintable) :
            token.flags & ~uint8_t(CToken::TokenFlags::Mintable);
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateToken)
             << token;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, metaObj["isDAT"].getBool() /*needFoundersAuth*/, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(GetTokenCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetTokenCollateralAmount(), GetScriptForDestination(collateralDest)));

    CCoinControl coinControl;

    // Return change to auth address
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
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, token});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CCreateTokenMessage{}, coins);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue updatetoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"updatetoken",
               "\nCreates (and submits to local node and network) a transaction of token promotion to isDAT or demotion from isDAT. Collateral will be unlocked.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The tokens's symbol, id or creation tx"},
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                           {"symbol", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                            "New token's symbol, no longer than " +
                            std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                           {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                            "New token's name (optional), no longer than " +
                            std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                           {"isDAT", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'isDAT' property (bool, optional), default is 'False'"},
                           {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'Mintable' property (bool, optional)"},
                           {"tradeable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'Tradeable' property (bool, optional)"},
                           {"finalize", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Lock token properties forever (bool, optional)"},
                           // it is possible to transfer token's owner. but later
//                           {"collateralAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
//                            "Any valid destination for keeping collateral amount - used as token's owner auth"},
                           // omitted for now, need to research/discuss
//                           {"decimal", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
//                            "Token's decimal places (optional, fixed to 8 for now, unchecked)"},
//                           {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
//                            "Token's total supply limit (optional, zero for now, unchecked)"},
                        },
                    },
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
                       HelpExampleCli("updatetoken", "token '{\"isDAT\":true}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("updatetoken", "token '{\"isDAT\":true}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot update token while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValueType(), UniValue::VOBJ, UniValue::VARR}, true); // first means "any"

    /// @todo RPCTypeCheckObj or smth to help with option's names and old/new tx type

    std::string const tokenStr = trim_ws(request.params[0].getValStr());
    UniValue metaObj = request.params[1].get_obj();
    UniValue const & txInputs = request.params[2];

    CTokenImplementation tokenImpl;
    CTxDestination ownerDest;
    CScript owner;
    int targetHeight;
    {
        LOCK(cs_main);
        DCT_ID id;
        auto token = pcustomcsview->GetTokenGuessId(tokenStr, id);
        if (id == DCT_ID{0}) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't alter DFI token!"));
        }
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));
        }
        tokenImpl = static_cast<CTokenImplementation const& >(*token);
        if (tokenImpl.IsPoolShare()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is the LPS token! Can't alter pool share's tokens!", tokenStr));
        }

        const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(tokenImpl.creationTx, 1)); // always n=1 output
        if (!ExtractDestination(authCoin.out.scriptPubKey, ownerDest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Can't extract destination for token's %s collateral", tokenImpl.symbol));
        }
        owner = authCoin.out.scriptPubKey;
        targetHeight = ::ChainActive().Height() + 1;
    }

    if (!metaObj["symbol"].isNull()) {
        tokenImpl.symbol = trim_ws(metaObj["symbol"].getValStr()).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    }
    if (!metaObj["name"].isNull()) {
        tokenImpl.name = trim_ws(metaObj["name"].getValStr()).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    }
    if (!metaObj["isDAT"].isNull()) {
        tokenImpl.flags = metaObj["isDAT"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::DAT :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::DAT;
    }
    if (!metaObj["tradeable"].isNull()) {
        tokenImpl.flags = metaObj["tradeable"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Tradeable :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::Tradeable;
    }
    if (!metaObj["mintable"].isNull()) {
        tokenImpl.flags = metaObj["mintable"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Mintable :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::Mintable;
    }
    if (!metaObj["finalize"].isNull()) {
        tokenImpl.flags = metaObj["finalize"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Finalized :
                              tokenImpl.flags;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    CTransactionRef optAuthTx;
    std::set<CScript> auths;

    if (targetHeight < Params().GetConsensus().BayfrontHeight) {
        if (metaObj.size() > 1 || !metaObj.exists("isDAT")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Only 'isDAT' flag modification allowed before Bayfront fork (<" + std::to_string(Params().GetConsensus().BayfrontHeight) + ")");
        }

        // before BayfrontHeight it needs only founders auth
        rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths /*auths*/, true /*needFoundersAuth*/, optAuthTx, txInputs);
    }
    else
    { // post-bayfront auth
        bool isFoundersToken = Params().GetConsensus().foundationMembers.find(owner) != Params().GetConsensus().foundationMembers.end();
        if (isFoundersToken) { // need any founder's auth
            rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths /*auths*/, true /*needFoundersAuth*/, optAuthTx, txInputs);
        }
        else {// "common" auth
            auths.insert(owner);
            rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);

    // tx type and serialized data differ:
    if (targetHeight < Params().GetConsensus().BayfrontHeight) {
        metadata << static_cast<unsigned char>(CustomTxType::UpdateToken)
                 << tokenImpl.creationTx <<  metaObj["isDAT"].getBool();
    }
    else {
        metadata << static_cast<unsigned char>(CustomTxType::UpdateTokenAny)
                 << tokenImpl.creationTx << static_cast<CToken>(tokenImpl); // casting to base token's data
    }

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Set change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        CCustomTxMessage txMessage = CUpdateTokenMessage{};
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, tokenImpl.creationTx, static_cast<CToken>(tokenImpl)});
        if (targetHeight < Params().GetConsensus().BayfrontHeight) {
            txMessage = CUpdateTokenPreAMKMessage{};
            metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, tokenImpl.creationTx, metaObj["isDAT"].getBool()});
        }
        execTestTx(CTransaction(rawTx), targetHeight, metadata, txMessage, coins);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue tokenToJSON(DCT_ID const& id, CTokenImplementation const& token, bool verbose) {
    UniValue tokenObj(UniValue::VOBJ);
    tokenObj.pushKV("symbol", token.symbol);
    tokenObj.pushKV("symbolKey", token.CreateSymbolKey(id));

    tokenObj.pushKV("name", token.name);
    if (verbose) {
        tokenObj.pushKV("decimal", token.decimal);
        tokenObj.pushKV("limit", token.limit);
        tokenObj.pushKV("mintable", token.IsMintable());
        tokenObj.pushKV("tradeable", token.IsTradeable());
        tokenObj.pushKV("isDAT", token.IsDAT());
        tokenObj.pushKV("isLPS", token.IsPoolShare());
        tokenObj.pushKV("finalized", token.IsFinalized());
        tokenObj.pushKV("isLoanToken", token.IsLoanToken());

        tokenObj.pushKV("minted", ValueFromAmount(token.minted));
        tokenObj.pushKV("creationTx", token.creationTx.ToString());
        tokenObj.pushKV("creationHeight", token.creationHeight);
        tokenObj.pushKV("destructionTx", token.destructionTx.ToString());
        tokenObj.pushKV("destructionHeight", token.destructionHeight);
        if (!token.IsPoolShare()) {
            const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output
            tokenObj.pushKV("collateralAddress", ScriptToString(authCoin.out.scriptPubKey));
        } else {
            tokenObj.pushKV("collateralAddress", "undefined");
        }
    }
    UniValue ret(UniValue::VOBJ);
    ret.pushKV(id.ToString(), tokenObj);
    return ret;
}

UniValue listtokens(const JSONRPCRequest& request) {
    RPCHelpMan{"listtokens",
               "\nReturns information about tokens.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of tokens to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids, symbols and names are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with tokens information\n"
               },
               RPCExamples{
                       HelpExampleCli("listtokens", "'{\"start\":128}' false")
                       + HelpExampleRpc("listtokens", "'{\"start\":128}' false")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start{0};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start.v = (uint32_t) paginationObj["start"].get_int();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                ++start.v;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);
    pcustomcsview->ForEachToken([&](DCT_ID const& id, CTokenImplementation token) {
        ret.pushKVs(tokenToJSON(id, token, verbose));

        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue gettoken(const JSONRPCRequest& request) {
    RPCHelpMan{"gettoken",
               "\nReturns information about token.\n",
               {
                       {"key", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "One of the keys may be specified (id/symbol/creationTx)"},
               },
               RPCResult{
                       "{id:{...}}     (array) Json object with token information\n"
               },
               RPCExamples{
                       HelpExampleCli("gettoken", "GOLD")
                       + HelpExampleRpc("gettoken", "GOLD")
               },
    }.Check(request);

    LOCK(cs_main);

    DCT_ID id;
    auto token = pcustomcsview->GetTokenGuessId(request.params[0].getValStr(), id);
    if (token) {
        return tokenToJSON(id, *static_cast<CTokenImplementation*>(token.get()), true);
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
}

UniValue getcustomtx(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    RPCHelpMan{"getcustomtx",
        "\nGet detailed information about a DeFiChain custom transaction. Will search wallet transactions and mempool transaction,\n"
        "if a blockhash is provided and that block is available then details for that transaction can be returned. -txindex\n"
        "can be enabled to return details for any transaction.",
        {
            {"txid", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction id"},
            {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED_NAMED_ARG, "The block in which to look for the transaction"},
        },
        RPCResult{
            "{\n"
            "  \"type\":               (string) The transaction type.\n"
            "  \"valid\"               (bool) Whether the transaction was valid.\n"
            "  \"results\"             (json object) Set of results related to the transaction type\n"
            "  \"block height\"        (string) The block height containing the transaction.\n"
            "  \"blockhash\"           (string) The block hash containing the transaction.\n"
            "  \"confirmations\": n,   (numeric) The number of confirmations for the transaction."
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("getcustomtx", "\"66ea2ac081e2917f075e2cca7c1c0baa12fb85c469f34561185fa64d7d2f9305\"")
                    + HelpExampleRpc("getcustomtx", "\"66ea2ac081e2917f075e2cca7c1c0baa12fb85c469f34561185fa64d7d2f9305\"")
        },
    }.Check(request);

    const uint256 hash(ParseHashV(request.params[0], "txid"));

    CTransactionRef tx;
    uint256 hashBlock;

    // Search wallet if available
    if (pwallet) {
        LOCK(pwallet->cs_wallet);
        if (auto wtx = pwallet->GetWalletTx(hash))
        {
            tx = wtx->tx;
            hashBlock = wtx->hashBlock;
        }
    }

    CBlockIndex* blockindex{nullptr};

    // No wallet or not a wallet TX, try mempool, txindex and a block if hash provided
    if (!pwallet || !tx)
    {
        if (!request.params[1].isNull()) {
            LOCK(cs_main);

            uint256 blockhash = ParseHashV(request.params[1], "blockhash");
            blockindex = LookupBlockIndex(blockhash);
            if (!blockindex) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
            }
        }

        bool f_txindex_ready{false};
        if (g_txindex && !blockindex) {
            f_txindex_ready = g_txindex->BlockUntilSyncedToCurrentChain();
        }

        if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, blockindex)) {
            std::string errmsg;
            if (blockindex) {
                if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                    throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
                }
                errmsg = "No such transaction found in the provided block.";
            } else if (!g_txindex) {
                errmsg = "No such mempool or wallet transaction. Use -txindex or provide a block hash.";
            } else if (!f_txindex_ready) {
                errmsg = "No such mempool or wallet transaction. Transactions are still in the process of being indexed.";
            } else {
                errmsg = "No such mempool, wallet or blockchain transaction.";
            }
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg);
        }
    }

    int nHeight{0};
    bool actualHeight{false};
    CustomTxType guess;
    UniValue txResults(UniValue::VOBJ);
    Res res{};

    if (tx)
    {
        LOCK(cs_main);

        // Found a block hash but no block index yet
        if (!hashBlock.IsNull() && !blockindex) {
            blockindex = LookupBlockIndex(hashBlock);
        }

        // Default to next block height
        nHeight = ::ChainActive().Height() + 1;

        // Get actual height if blockindex avaiable
        if (blockindex) {
            nHeight = blockindex->nHeight;
            actualHeight = true;
        }

        // Skip coinbase TXs except for genesis block
        if ((tx->IsCoinBase() && nHeight > 0)) {
            return "Coinbase transaction. Not a custom transaction.";
        }

        res = RpcInfo(*tx, nHeight, guess, txResults);
        if (guess == CustomTxType::None) {
            return "Not a custom transaction";
        }

    } else {
        // Should not really get here without prior failure.
        return "Could not find matching transaction.";
    }

    UniValue result(UniValue::VOBJ);

    result.pushKV("type", ToString(guess));
    if (!actualHeight) {

        LOCK(cs_main);
        CCustomCSView mnview(*pcustomcsview);
        CCoinsViewCache view(&::ChainstateActive().CoinsTip());

        auto res = ApplyCustomTx(mnview, view, *tx, Params().GetConsensus(), nHeight);
        result.pushKV("valid", res.ok);
    } else {
        if (nHeight >= Params().GetConsensus().DakotaHeight) {
            result.pushKV("valid", actualHeight);
        } else {
            result.pushKV("valid", !IsSkippedTx(tx->GetHash()));
        }
    }

    if (!res.ok) {
        result.pushKV("error", res.msg);
    } else {
        result.pushKV("results", txResults);
    }

    if (!hashBlock.IsNull()) {
        LOCK(cs_main);

        result.pushKV("blockhash", hashBlock.GetHex());
        if (blockindex) {
            result.pushKV("blockHeight", blockindex->nHeight);
            result.pushKV("blockTime", blockindex->GetBlockTime());
            result.pushKV("confirmations", 1 + ::ChainActive().Height() - blockindex->nHeight);
        } else {
            result.pushKV("confirmations", 0);
        }
    }

    return result;
}

UniValue minttokens(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"minttokens",
               "\nCreates (and submits to local node and network) a transaction minting your token (for accounts and/or UTXOs). \n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"amounts", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Amount in amount@token format."
                    },
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
                       HelpExampleCli("minttokens", "10@symbol")
                       + HelpExampleCli("minttokens",
                                      "10@symbol '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("minttokens", "10@symbol '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot mint tokens while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    const CBalances minted = DecodeAmounts(pwallet->chain(), request.params[0], "");
    UniValue const & txInputs = request.params[1];

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    CTransactionRef optAuthTx;
    std::set<CScript> auths;

    // auth
    {
        if (!txInputs.isNull() && !txInputs.empty()) {
            rawTx.vin = GetInputs(txInputs.get_array());            // separate call here to do not process the rest in "else"
        }
        else {
            bool needFoundersAuth = false;
            for (auto const & kv : minted.balances) {

                CTokenImplementation tokenImpl;
                {
                    LOCK(cs_main);
                    auto token = pcustomcsview->GetToken(kv.first);
                    if (!token) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", kv.first.ToString()));
                    }

                    tokenImpl = static_cast<CTokenImplementation const& >(*token);
                    const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(tokenImpl.creationTx, 1)); // always n=1 output
                    if (tokenImpl.IsDAT()) {
                        needFoundersAuth = true;
                    }
                    auths.insert(authCoin.out.scriptPubKey);
                }
            }
            rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, needFoundersAuth, optAuthTx, txInputs);
        } // else
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::MintToken)
             << minted; /// @note here, that whole CBalances serialized!, not a 'minted.balances'!

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Set change to auth address if there's only one auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, minted });
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CMintTokensMessage{}, coins);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue decodecustomtx(const JSONRPCRequest& request)
{
    RPCHelpMan{"decodecustomtx",
        "\nGet detailed information about a DeFiChain custom transaction.\n",
        {
            {"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction hex string"},
            {"iswitness", RPCArg::Type::BOOL, /* default */ "depends on heuristic tests", "Whether the transaction hex is a serialized witness transaction.\n"
                "If iswitness is not present, heuristic tests will be used in decoding.\n"
                "If true, only witness deserialization will be tried.\n"
                "If false, only non-witness deserialization will be tried.\n"
                "This boolean should reflect whether the transaction has inputs\n"
                "(e.g. fully valid, or on-chain transactions), if known by the caller."
            },
        },
        RPCResult{
            "{\n"
            "  \"txid\":               (string) The transaction id.\n"
            "  \"type\":               (string) The transaction type.\n"
            "  \"valid\"               (bool) Whether the transaction was valid.\n"
            "  \"results\"             (json object) Set of results related to the transaction type\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("decodecustomtx", "\"hexstring\"")
            + HelpExampleRpc("decodecustomtx", "\"hexstring\"")
        },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    bool try_witness = request.params[1].isNull() ? true : request.params[1].get_bool();
    bool try_no_witness = request.params[1].isNull() ? true : !request.params[1].get_bool();

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str(), try_no_witness, try_witness)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    int nHeight{0};
    CustomTxType guess;
    UniValue txResults(UniValue::VOBJ);
    Res res{};
    CTransactionRef tx = MakeTransactionRef(std::move(mtx));
    std::string warnings;

    if (tx)
    {
        LOCK(cs_main);
        // Default to INT_MAX
        nHeight = std::numeric_limits<int>::max();

        // Skip coinbase TXs except for genesis block
        if ((tx->IsCoinBase() && nHeight > 0)) {
            return "Coinbase transaction. Not a custom transaction.";
        }
        //get custom tx info. We pass nHeight INT_MAX,
        //just to get over hardfork validations. txResults are based on transaction metadata.
        res = RpcInfo(*tx, nHeight, guess, txResults);
        if (guess == CustomTxType::None) {
            return "Not a custom transaction";
        }

        UniValue result(UniValue::VOBJ);
        result.pushKV("txid", tx->GetHash().GetHex());
        result.pushKV("type", ToString(guess));
        result.pushKV("valid", res.ok ? true : false); // no actual block height
        if (!res.ok) {
            result.pushKV("error", res.msg);
        } else {
            result.pushKV("results", txResults);
        }

        return result;
    } else {
        // Should not get here without prior failure.
        return "Could not decode the input transaction hexstring.";
    }
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  -------------   ---------------------    --------------------    ----------
    {"tokens",      "createtoken",           &createtoken,           {"metadata", "inputs"}},
    {"tokens",      "updatetoken",           &updatetoken,           {"token", "metadata", "inputs"}},
    {"tokens",      "listtokens",            &listtokens,            {"pagination", "verbose"}},
    {"tokens",      "gettoken",              &gettoken,              {"key" }},
    {"tokens",      "getcustomtx",           &getcustomtx,           {"txid", "blockhash"}},
    {"tokens",      "minttokens",            &minttokens,            {"amounts", "inputs"}},
    {"tokens",      "decodecustomtx",        &decodecustomtx,        {"hexstring", "iswitness"}},
};

void RegisterTokensRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
