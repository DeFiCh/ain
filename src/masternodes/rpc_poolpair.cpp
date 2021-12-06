#include <masternodes/mn_rpc.h>

UniValue poolToJSON(CCustomCSView& view, DCT_ID const& id, CPoolPair const& pool, CToken const& token, bool verbose) {
    UniValue poolObj(UniValue::VOBJ);
    poolObj.pushKV("symbol", token.symbol);
    poolObj.pushKV("name", token.name);
    poolObj.pushKV("status", pool.status);
    poolObj.pushKV("idTokenA", pool.idTokenA.ToString());
    poolObj.pushKV("idTokenB", pool.idTokenB.ToString());

    if (verbose) {
        if (const auto dexFee = pcustomcsview->GetDexFeePct(id, pool.idTokenA)) {
            poolObj.pushKV("dexFeePctTokenA", ValueFromAmount(dexFee));
        }
        if (const auto dexFee = pcustomcsview->GetDexFeePct(id, pool.idTokenB)) {
            poolObj.pushKV("dexFeePctTokenB", ValueFromAmount(dexFee));
        }
        poolObj.pushKV("reserveA", ValueFromAmount(pool.reserveA));
        poolObj.pushKV("reserveB", ValueFromAmount(pool.reserveB));
        poolObj.pushKV("commission", ValueFromAmount(pool.commission));
        poolObj.pushKV("totalLiquidity", ValueFromAmount(pool.totalLiquidity));

        if (pool.reserveB == 0) {
            poolObj.pushKV("reserveA/reserveB", "0");
        } else {
            poolObj.pushKV("reserveA/reserveB", ValueFromAmount(DivideAmounts(pool.reserveA, pool.reserveB)));
        }

        if (pool.reserveA == 0) {
            poolObj.pushKV("reserveB/reserveA", "0");
        } else {
            poolObj.pushKV("reserveB/reserveA", ValueFromAmount(DivideAmounts(pool.reserveB, pool.reserveA)));
        }
        poolObj.pushKV("tradeEnabled", pool.reserveA >= CPoolPair::SLOPE_SWAP_RATE && pool.reserveB >= CPoolPair::SLOPE_SWAP_RATE);

        poolObj.pushKV("ownerAddress", ScriptToString(pool.ownerAddress));

        poolObj.pushKV("blockCommissionA", ValueFromAmount(pool.blockCommissionA));
        poolObj.pushKV("blockCommissionB", ValueFromAmount(pool.blockCommissionB));

        poolObj.pushKV("rewardPct", ValueFromAmount(pool.rewardPct));
        poolObj.pushKV("rewardLoanPct", ValueFromAmount(pool.rewardLoanPct));

        auto rewards = pool.rewards;
        if (!rewards.balances.empty()) {
            for (auto it = rewards.balances.cbegin(), next_it = it; it != rewards.balances.cend(); it = next_it) {
                ++next_it;

                // Get token balance
                const auto balance = view.GetBalance(pool.ownerAddress, it->first).nValue;

                // Make there's enough to pay reward otherwise remove it
                if (balance < it->second) {
                    rewards.balances.erase(it);
                }
            }

            if (!rewards.balances.empty()) {
                UniValue rewardArr(UniValue::VARR);

                for (const auto& reward : rewards.balances) {
                    rewardArr.push_back(CTokenAmount{reward.first, reward.second}.ToString());
                }

                poolObj.pushKV("customRewards", rewardArr);
            }
        }

        poolObj.pushKV("creationTx", pool.creationTx.GetHex());
        poolObj.pushKV("creationHeight", (uint64_t) pool.creationHeight);
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(id.ToString(), poolObj);
    return ret;
}

UniValue poolShareToJSON(DCT_ID const & poolId, CScript const & provider, CAmount const& amount, CPoolPair const& poolPair, bool verbose) {
    UniValue poolObj(UniValue::VOBJ);
    poolObj.pushKV("poolID", poolId.ToString());
    poolObj.pushKV("owner", ScriptToString(provider));
    CAmount percentage = (arith_uint256(amount) * 100 * COIN / poolPair.totalLiquidity).GetLow64();
    poolObj.pushKV("%", ValueFromAmount(percentage));

    if (verbose) {
        poolObj.pushKV("amount", ValueFromAmount(amount));
        poolObj.pushKV("totalLiquidity", ValueFromAmount(poolPair.totalLiquidity));
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(poolId.ToString() + "@" + ScriptToString(provider), poolObj);
    return ret;
}


UniValue poolPathsToJSON(std::vector<std::vector<DCT_ID>> & poolPaths) {
    UniValue paths(UniValue::VARR);
    for (auto poolIds : poolPaths) {
        UniValue pathObj(UniValue::VARR);
        for (auto poolId : poolIds){
            pathObj.push_back(poolId.ToString());
        }
        paths.push_back(pathObj);
    }
    return paths;
}
void CheckAndFillPoolSwapMessage(const JSONRPCRequest& request, CPoolSwapMessage &poolSwapMsg) {
    std::string tokenFrom, tokenTo;
    UniValue metadataObj = request.params[0].get_obj();
    if (!metadataObj["from"].isNull()) {
        poolSwapMsg.from = DecodeScript(metadataObj["from"].getValStr());
    }
    if (!metadataObj["tokenFrom"].isNull()) {
        tokenFrom = metadataObj["tokenFrom"].getValStr();
    }
    if (!metadataObj["amountFrom"].isNull()) {
        poolSwapMsg.amountFrom = AmountFromValue(metadataObj["amountFrom"]);
    }
    if (!metadataObj["to"].isNull()) {
        poolSwapMsg.to = DecodeScript(metadataObj["to"].getValStr());
    }
    if (!metadataObj["tokenTo"].isNull()) {
        tokenTo = metadataObj["tokenTo"].getValStr();
    }
    {
        auto token = pcustomcsview->GetTokenGuessId(tokenFrom, poolSwapMsg.idTokenFrom);
        if (!token)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "TokenFrom was not found");

        auto token2 = pcustomcsview->GetTokenGuessId(tokenTo, poolSwapMsg.idTokenTo);
        if (!token2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "TokenTo was not found");

        if (!metadataObj["maxPrice"].isNull()) {
            CAmount maxPrice = AmountFromValue(metadataObj["maxPrice"]);
            poolSwapMsg.maxPrice.integer = maxPrice / COIN;
            poolSwapMsg.maxPrice.fraction = maxPrice % COIN;
        } else {
            // There is no maxPrice calculation anymore
            poolSwapMsg.maxPrice.integer = std::numeric_limits<CAmount>::max();
            poolSwapMsg.maxPrice.fraction = std::numeric_limits<CAmount>::max();
        }
    }
}

UniValue listpoolpairs(const JSONRPCRequest& request) {
    RPCHelpMan{"listpoolpairs",
               "\nReturns information about pools.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of pools to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids, symbols and names are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with pools information\n"
               },
               RPCExamples{
                       HelpExampleCli("listpoolpairs", "'{\"start\":128}' false")
                       + HelpExampleRpc("listpoolpairs", "'{\"start\":128}' false")
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

    UniValue ret(UniValue::VOBJ);
    CCustomCSView view(*pcustomcsview);
    view.ForEachPoolPair([&](DCT_ID const & id, CPoolPair pool) {
        const auto token = view.GetToken(id);
        if (token) {
            ret.pushKVs(poolToJSON(view, id, pool, *token, verbose));
            limit--;
        }

        return limit != 0;
    }, start);

    return ret;
}

UniValue getpoolpair(const JSONRPCRequest& request) {
    RPCHelpMan{"getpoolpair",
               "\nReturns information about pool.\n",
               {
                       {"key", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "One of the keys may be specified (id/symbol/creationTx)"},
                       {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Flag for verbose list (default = true), otherwise limited objects are listed"},
               },
               RPCResult{
                       "{id:{...}}     (array) Json object with pool information\n"
               },
               RPCExamples{
                       HelpExampleCli("getpoolpair", "GOLD")
                       + HelpExampleRpc("getpoolpair", "GOLD")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].getBool();
    }

    DCT_ID id;
    CCustomCSView view(*pcustomcsview);
    auto token = view.GetTokenGuessId(request.params[0].getValStr(), id);
    if (token) {
        auto pool = view.GetPoolPair(id);
        if (pool) {
            return poolToJSON(view, id, *pool, *token, verbose);
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pool not found");
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pool not found");
}

UniValue addpoolliquidity(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"addpoolliquidity",
               "\nCreates (and submits to local node and network) a add pool liquidity transaction.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"from", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                                {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address(es) is the key(s), the value(s) is amount in amount@token format. "
                                                                                     "You should provide exectly two types of tokens for pool's 'token A' and 'token B' in any combinations."
                                                                                     "If multiple tokens from one address are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"
                                                                                     "If \"from\" obj contain only one amount entry with address-key: \"*\" (star), it's means auto-selection accounts from wallet."},
                        },
                       },
                       {"shareAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address for crediting tokens."},
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
                       HelpExampleCli("addpoolliquidity",
                                      "'{\"address1\":\"1.0@DFI\",\"address2\":\"1.0@DFI\"}' "
                                      "share_address '[]'")
                       + HelpExampleCli("addpoolliquidity",
                                      "'{\"*\": [\"2.0@BTC\", \"3.0@ETH\"]}' "
                                      "share_address '[]'")
                       + HelpExampleRpc("addpoolliquidity",
                                      "'{\"address1\":\"1.0@DFI\",\"address2\":\"1.0@DFI\"}' "
                                      "share_address '[]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VOBJ, UniValue::VSTR, UniValue::VARR }, true);

    // decode
    CLiquidityMessage msg{};
    if (request.params[0].get_obj().getKeys().size() == 1 &&
        request.params[0].get_obj().getKeys()[0] == "*") { // auto-selection accounts from wallet

        CAccounts foundMineAccounts = GetAllMineAccounts(pwallet);

        CBalances sumTransfers = DecodeAmounts(pwallet->chain(), request.params[0].get_obj()["*"], "*");

        msg.from = SelectAccountsByTargetBalances(foundMineAccounts, sumTransfers, SelectionPie);

        if (msg.from.empty()) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Not enough balance on wallet accounts, call utxostoaccount to increase it.\n");
        }
    }
    else {
        msg.from = DecodeRecipients(pwallet->chain(), request.params[0].get_obj());
    }
    msg.shareAddress = DecodeScript(request.params[1].get_str());

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AddPoolLiquidity)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // auth
    std::set<CScript> auths;
    for (const auto& kv : msg.from) {
        auths.emplace(kv.first);
    }
    UniValue const & txInputs = request.params[2];
    CTransactionRef optAuthTx;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to from address if there's only one auth address
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
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue removepoolliquidity(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"removepoolliquidity",
               "\nCreates (and submits to local node and network) a remove pool liquidity transaction.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address which has tokens"},
                       {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Liquidity amount@Liquidity pool symbol"},
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
                       HelpExampleCli("removepoolliquidity", "from_address 1.0@LpSymbol")
                       + HelpExampleRpc("removepoolliquidity", "from_address 1.0@LpSymbol")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);

    std::string from = request.params[0].get_str();
    std::string amount = request.params[1].get_str();
    UniValue const & txInputs = request.params[2];

    // decode
    CRemoveLiquidityMessage msg{};
    msg.from = DecodeScript(from);
    msg.amount = DecodeAmount(pwallet->chain(), amount, from);

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::RemovePoolLiquidity)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CTransactionRef optAuthTx;
    std::set<CScript> auths{msg.from};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

     // Set change to from address
    CTxDestination dest;
    ExtractDestination(msg.from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue createpoolpair(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"createpoolpair",
               "\nCreates (and submits to local node and network) a poolpair transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                            {"tokenA", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "One of the keys may be specified (id/symbol)"},
                            {"tokenB", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "One of the keys may be specified (id/symbol)"},
                            {"commission", RPCArg::Type::NUM, RPCArg::Optional::NO,
                            "Pool commission, up to 10^-8"},
                            {"status", RPCArg::Type::BOOL, RPCArg::Optional::NO,
                            "Pool Status: True is Active, False is Restricted"},
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "Address of the pool owner."},
                            {"customRewards", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                            "Token reward to be paid on each block, multiple can be specified."},
                            {"pairSymbol", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                             "Pair symbol (unique), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
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
                       HelpExampleCli("createpoolpair",   "'{\"tokenA\":\"MyToken1\","
                                                          "\"tokenB\":\"MyToken2\","
                                                          "\"commission\":\"0.001\","
                                                          "\"status\":\"True\","
                                                          "\"ownerAddress\":\"Address\","
                                                          "\"customRewards\":\"[\\\"1@tokena\\\",\\\"10@tokenb\\\"]\""
                                                          "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("createpoolpair", "'{\"tokenA\":\"MyToken1\","
                                                          "\"tokenB\":\"MyToken2\","
                                                          "\"commission\":\"0.001\","
                                                          "\"status\":\"True\","
                                                          "\"ownerAddress\":\"Address\","
                                                          "\"customRewards\":\"[\\\"1@tokena\\\",\\\"10@tokenb\\\"]\""
                                                          "}' '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    std::string tokenA, tokenB, pairSymbol;
    CAmount commission = 0; // !!!
    CScript ownerAddress;
    CBalances rewards;
    bool status = true; // default Active
    UniValue metadataObj = request.params[0].get_obj();
    if (!metadataObj["tokenA"].isNull()) {
        tokenA = metadataObj["tokenA"].getValStr();
    }
    if (!metadataObj["tokenB"].isNull()) {
        tokenB = metadataObj["tokenB"].getValStr();
    }
    if (!metadataObj["commission"].isNull()) {
        commission = AmountFromValue(metadataObj["commission"]);
    }
    if (!metadataObj["status"].isNull()) {
        status = metadataObj["status"].getBool();
    }
    if (!metadataObj["ownerAddress"].isNull()) {
        ownerAddress = DecodeScript(metadataObj["ownerAddress"].getValStr());
    }
    if (!metadataObj["pairSymbol"].isNull()) {
        pairSymbol = metadataObj["pairSymbol"].getValStr();
    }
    if (!metadataObj["customRewards"].isNull()) {
        rewards = DecodeAmounts(pwallet->chain(), metadataObj["customRewards"], "");
    }

    int targetHeight;
    DCT_ID idtokenA, idtokenB;
    {
        auto token = pcustomcsview->GetTokenGuessId(tokenA, idtokenA);
        if (!token)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "TokenA was not found");

        auto token2 = pcustomcsview->GetTokenGuessId(tokenB, idtokenB);
        if (!token2)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "TokenB was not found");

        targetHeight = pcustomcsview->GetLastHeight() + 1;
    }

    CCreatePoolPairMessage poolPairMsg{};
    poolPairMsg.idTokenA = idtokenA;
    poolPairMsg.idTokenB = idtokenB;
    poolPairMsg.commission = commission;
    poolPairMsg.status = status;
    poolPairMsg.ownerAddress = ownerAddress;
    poolPairMsg.pairSymbol = pairSymbol;

    if (targetHeight >= Params().GetConsensus().ClarkeQuayHeight) {
        poolPairMsg.rewards = rewards;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreatePoolPair)
             << poolPairMsg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue const & txInputs = request.params[1];

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to selected foundation address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue updatepoolpair(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"updatepoolpair",
               "\nCreates (and submits to local node and network) a pool status update transaction.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the pool's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                           {"pool", RPCArg::Type::STR, RPCArg::Optional::NO, "The pool's symbol, id or creation tx"},
                           {"status", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Pool Status new property (bool)"},
                           {"commission", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Pool commission, up to 10^-8"},
                           {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address of the pool owner."},
                           {"customRewards", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token reward to be paid on each block, multiple can be specified."},
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
                       HelpExampleCli("updatepoolpair", "'{\"pool\":\"POOL\",\"status\":true,"
                                                     "\"commission\":0.01,\"ownerAddress\":\"Address\","
                                                     "\"customRewards\":\"[\\\"1@tokena\\\",\\\"10@tokenb\\\"]\"}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("updatepoolpair", "'{\"pool\":\"POOL\",\"status\":true,"
                                                       "\"commission\":0.01,\"ownerAddress\":\"Address\","
                                                       "\"customRewards\":\"[\\\"1@tokena\\\",\\\"10@tokenb\\\"]\"}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    bool status = true;
    CAmount commission = -1;
    CScript ownerAddress;
    CBalances rewards;
    UniValue const & metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    std::string const poolStr = trim_ws(metaObj["pool"].getValStr());
    DCT_ID poolId;
    int targetHeight;
    {
        auto token = pcustomcsview->GetTokenGuessId(poolStr, poolId);
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pool %s does not exist!", poolStr));
        }

        auto pool = pcustomcsview->GetPoolPair(poolId);
        if (!pool) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pool %s does not exist!", poolStr));
        }
        status = pool->status;
        targetHeight = pcustomcsview->GetLastHeight() + 1;
    }

    if (!metaObj["status"].isNull()) {
        status = metaObj["status"].getBool();
    }
    if (!metaObj["commission"].isNull()) {
        commission = AmountFromValue(metaObj["commission"]);
    }
    if (!metaObj["ownerAddress"].isNull()) {
        ownerAddress = DecodeScript(metaObj["ownerAddress"].getValStr());
    }
    if (!metaObj["customRewards"].isNull()) {
        rewards = DecodeAmounts(pwallet->chain(), metaObj["customRewards"], "");

        if (rewards.balances.empty()) {
            // Add special case to wipe rewards
            rewards.balances.insert(std::pair<DCT_ID, CAmount>(DCT_ID{std::numeric_limits<uint32_t>::max()}, std::numeric_limits<CAmount>::max()));
        }
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true /*needFoundersAuth*/, optAuthTx, txInputs);

    CUpdatePoolPairMessage msg{};
    msg.poolId = poolId;
    msg.status = status;
    msg.commission = commission;
    msg.ownerAddress = ownerAddress;

    if (targetHeight >= Params().GetConsensus().ClarkeQuayHeight) {
        msg.rewards = rewards;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UpdatePoolPair)
             << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Set change to selected foundation address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue poolswap(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"poolswap",
               "\nCreates (and submits to local node and network) a poolswap transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                       {
                           {"from", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenA."},
                           {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO,
                                       "tokenFrom coins amount"},
                           {"to", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenB."},
                           {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"maxPrice", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                       "Maximum acceptable price"},
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
                                 HelpExampleCli("poolswap",   "'{\"from\":\"MyAddress\","
                                                                    "\"tokenFrom\":\"MyToken1\","
                                                                    "\"amountFrom\":\"0.001\","
                                                                    "\"to\":\"Address\","
                                                                    "\"tokenTo\":\"Token2\","
                                                                    "\"maxPrice\":\"0.01\""
                                                                    "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                                         + HelpExampleRpc("poolswap", "'{\"from\":\"MyAddress\","
                                                                            "\"tokenFrom\":\"MyToken1\","
                                                                            "\"amountFrom\":\"0.001\","
                                                                            "\"to\":\"Address\","
                                                                            "\"tokenTo\":\"Token2\","
                                                                            "\"maxPrice\":\"0.01\""
                                                                            "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                             },
              }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    CPoolSwapMessage poolSwapMsg{};
    CheckAndFillPoolSwapMessage(request, poolSwapMsg);
    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::PoolSwap);
    metadata << poolSwapMsg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const & txInputs = request.params[1];
    CTransactionRef optAuthTx;
    std::set<CScript> auths{poolSwapMsg.from};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to from address
    CTxDestination dest;
    ExtractDestination(poolSwapMsg.from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue compositeswap(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"compositeswap",
               "\nCreates (and submits to local node and network) a composite swap (swap between multiple poolpairs) transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                       {
                           {"from", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenA."},
                           {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO,
                                       "tokenFrom coins amount"},
                           {"to", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenB."},
                           {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"maxPrice", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                       "Maximum acceptable price"},
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
                                 HelpExampleCli("compositeswap",   "'{\"from\":\"MyAddress\","
                                                                    "\"tokenFrom\":\"MyToken1\","
                                                                    "\"amountFrom\":\"0.001\","
                                                                    "\"to\":\"Address\","
                                                                    "\"tokenTo\":\"Token2\","
                                                                    "\"maxPrice\":\"0.01\""
                                                                    "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                                         + HelpExampleRpc("compositeswap", "'{\"from\":\"MyAddress\","
                                                                            "\"tokenFrom\":\"MyToken1\","
                                                                            "\"amountFrom\":\"0.001\","
                                                                            "\"to\":\"Address\","
                                                                            "\"tokenTo\":\"Token2\","
                                                                            "\"maxPrice\":\"0.01\""
                                                                            "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                             },
              }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    int targetHeight = pcustomcsview->GetLastHeight() + 1;
    if (targetHeight < Params().GetConsensus().FortCanningHeight) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "compositeswap is available post Fort Canning");
    }

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    CPoolSwapMessageV2 poolSwapMsgV2{};
    CPoolSwapMessage& poolSwapMsg = poolSwapMsgV2.swapInfo;
    CheckAndFillPoolSwapMessage(request, poolSwapMsg);

    {
        // If no direct swap found search for composite swap
        CCustomCSView view(*pcustomcsview);
        if (!view.GetPoolPair(poolSwapMsg.idTokenFrom, poolSwapMsg.idTokenTo)) {

            auto compositeSwap = CPoolSwap(poolSwapMsg, targetHeight);
            poolSwapMsgV2.poolIDs = compositeSwap.CalculateSwaps(view);

            // No composite or direct pools found
            if (poolSwapMsgV2.poolIDs.empty()) {
                // Base error message
                std::string errorMsg{"Cannot find usable pool pair."};
                if (!compositeSwap.errors.empty()) {
                    errorMsg += " Details: (";
                    for (size_t i{0}; i < compositeSwap.errors.size(); ++i) {
                        errorMsg += '"' + compositeSwap.errors[i].first + "\":\"" +  compositeSwap.errors[i].second + '"' + (i + 1 < compositeSwap.errors.size() ? "," : "");
                    }
                    errorMsg += ')';
                }
                throw JSONRPCError(RPC_INVALID_REQUEST, errorMsg);
            }
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::PoolSwapV2);
    metadata << poolSwapMsgV2;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const & txInputs = request.params[1];
    CTransactionRef optAuthTx;
    std::set<CScript> auths{poolSwapMsg.from};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to from address
    CTxDestination dest;
    ExtractDestination(poolSwapMsg.from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue testpoolswap(const JSONRPCRequest& request) {

    RPCHelpMan{"testpoolswap",
               "\nTests a poolswap transaction with given metadata and returns poolswap result.\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                           {"from", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenA."},
                           {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO,
                                       "tokenFrom coins amount"},
                           {"to", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenB."},
                           {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"maxPrice", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                       "Maximum acceptable price"},
                       },
                   },
                   {
                       "path", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                       "One of auto/direct (default = direct)\n"
                       "auto - automatically use composite swap or direct swap as needed.\n"
                       "direct - uses direct path only or fails.\n"
                       "Note: The default will be switched to auto in the upcoming versions."
                   },
                   {
                       "verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                       "Returns estimated composite path when true (default = false)"
                   },
               },
               RPCResult{
                          "\"amount@tokenId\"    (string) The string with amount result of poolswap in format AMOUNT@TOKENID.\n"
               },
               RPCExamples{
                    HelpExampleCli("testpoolswap",   "'{\"from\":\"MyAddress\","
                                                    "\"tokenFrom\":\"MyToken1\","
                                                    "\"amountFrom\":\"0.001\","
                                                    "\"to\":\"Address\","
                                                    "\"tokenTo\":\"Token2\","
                                                    "\"maxPrice\":\"0.01\""
                                                    "}'")
                    + HelpExampleRpc("testpoolswap", "'{\"from\":\"MyAddress\","
                                                    "\"tokenFrom\":\"MyToken1\","
                                                    "\"amountFrom\":\"0.001\","
                                                    "\"to\":\"Address\","
                                                    "\"tokenTo\":\"Token2\","
                                                    "\"maxPrice\":\"0.01\""
                                                    "}'")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValueType(), UniValue::VBOOL}, true);

    std::string path = "direct";
    if (request.params.size() > 1) {
        path = request.params[1].getValStr();
    }

    bool verbose = false;
    if (request.params.size() > 2) {
        verbose = request.params[2].get_bool();
    }

    UniValue pools{UniValue::VARR};

    CPoolSwapMessage poolSwapMsg{};
    CheckAndFillPoolSwapMessage(request, poolSwapMsg);

    // test execution and get amount
    Res res = Res::Ok();
    {
        CCustomCSView mnview_dummy(*pcustomcsview); // create dummy cache for test state writing

        int targetHeight = mnview_dummy.GetLastHeight() + 1;

        // If no direct swap found search for composite swap
        if (path == "direct") {
            auto poolPair = mnview_dummy.GetPoolPair(poolSwapMsg.idTokenFrom, poolSwapMsg.idTokenTo);
            if (!poolPair)
                throw JSONRPCError(RPC_INVALID_REQUEST, std::string{"Direct pool pair not found. Use 'auto' mode to use composite swap."});

            if (poolSwapMsg.amountFrom <= 0)
                throw JSONRPCError(RPC_INVALID_REQUEST, "Input amount should be positive");

            CPoolPair pp = poolPair->second;
            auto dexfeeInPct = mnview_dummy.GetDexFeePct(poolPair->first, poolSwapMsg.idTokenFrom);

            res = pp.Swap({poolSwapMsg.idTokenFrom, poolSwapMsg.amountFrom}, dexfeeInPct, poolSwapMsg.maxPrice, [&] (const CTokenAmount &, const CTokenAmount &tokenAmount) {
                auto resPP = mnview_dummy.SetPoolPair(poolPair->first, targetHeight, pp);
                if (!resPP) {
                    return resPP;
                }

                auto resultAmount = tokenAmount;
                if (targetHeight >= Params().GetConsensus().FortCanningHillHeight) {
                    if (auto dexfeeOutPct = mnview_dummy.GetDexFeePct(poolPair->first, tokenAmount.nTokenId)) {
                        auto dexfeeOutAmount = MultiplyAmounts(tokenAmount.nValue, dexfeeOutPct);
                        resultAmount.nValue -= dexfeeOutAmount;
                    }
                }

                return Res::Ok(resultAmount.ToString());
            }, targetHeight);

            if (!res)
                throw JSONRPCError(RPC_VERIFY_ERROR, res.msg);

            pools.push_back(poolPair->first.ToString());
        } else {
            auto compositeSwap = CPoolSwap(poolSwapMsg, targetHeight);

            std::vector<DCT_ID> poolIds;
            if (path == "auto") {
                poolIds = compositeSwap.CalculateSwaps(mnview_dummy, true);
            } else {
                path = "custom";

                UniValue poolArray(UniValue::VARR);
                if (request.params[1].isArray()) {
                    poolArray = request.params[1].get_array();
                } else {
                    poolArray.read(request.params[1].getValStr().c_str());
                }

                for (const auto& id : poolArray.getValues()) {
                    poolIds.push_back(DCT_ID::FromString(id.getValStr()));
                }

                auto availablePaths = compositeSwap.CalculatePoolPaths(mnview_dummy);
                if (std::find(availablePaths.begin(), availablePaths.end(), poolIds) == availablePaths.end()) {
                    throw JSONRPCError(RPC_INVALID_REQUEST, "Custom pool path is invalid.");
                }
            }

            res = compositeSwap.ExecuteSwap(mnview_dummy, poolIds, true);
            if (!res) {
                std::string errorMsg{"Cannot find usable pool pair."};
                if (!compositeSwap.errors.empty()) {
                    errorMsg += " Details: (";
                    for (size_t i{0}; i < compositeSwap.errors.size(); ++i) {
                        errorMsg += '"' + compositeSwap.errors[i].first + "\":\"" +  compositeSwap.errors[i].second + '"' + (i + 1 < compositeSwap.errors.size() ? "," : "");
                    }
                    errorMsg += ')';
                }
                throw JSONRPCError(RPC_INVALID_REQUEST, errorMsg);
            }
            for (const auto& id : poolIds) {
                pools.push_back(id.ToString());
            }
            res.msg = compositeSwap.GetResult().ToString();
        }
    }
    if (verbose) {
        UniValue swapObj{UniValue::VOBJ};
        swapObj.pushKV("path", path);
        swapObj.pushKV("pools", pools);
        swapObj.pushKV("amount", res.msg);
        return swapObj;
    }

    return res.msg;
}

UniValue listpoolshares(const JSONRPCRequest& request) {
    RPCHelpMan{"listpoolshares",
               "\nReturns information about pool shares.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of pools to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only % are shown."},
                        {"is_mine_only", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Get shares for all accounts belonging to the wallet (default = false)"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with pools information\n"
               },
               RPCExamples{
                       HelpExampleCli("listpoolshares", "'{\"start\":128}' false false")
                       + HelpExampleRpc("listpoolshares", "'{\"start\":128}' false false")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].getBool();
    }

    bool isMineOnly = false;
    if (request.params.size() > 2) {
        isMineOnly = request.params[2].get_bool();
    }

    auto pwallet = GetWallet(request);

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

    PoolShareKey startKey{ start, CScript{} };

    UniValue ret(UniValue::VOBJ);
    CCustomCSView view(*pcustomcsview);
    view.ForEachPoolShare([&](DCT_ID const & poolId, CScript const & provider, uint32_t) {
        const CTokenAmount tokenAmount = view.GetBalance(provider, poolId);
        if(tokenAmount.nValue) {
            const auto poolPair = view.GetPoolPair(poolId);
            if(poolPair) {
                if (isMineOnly) {
                    if (IsMineCached(*pwallet, provider) == ISMINE_SPENDABLE) {
                        ret.pushKVs(poolShareToJSON(poolId, provider, tokenAmount.nValue, *poolPair, verbose));
                        limit--;
                    }
                } else {
                    ret.pushKVs(poolShareToJSON(poolId, provider, tokenAmount.nValue, *poolPair, verbose));
                    limit--;
                }
            }
        }

        return limit != 0;
    }, startKey);

    return ret;
}

static const CRPCCommand commands[] =
{
//  category        name                        actor (function)            params
//  -------------   -----------------------     ---------------------       ----------
    {"poolpair",    "listpoolpairs",            &listpoolpairs,             {"pagination", "verbose"}},
    {"poolpair",    "getpoolpair",              &getpoolpair,               {"key", "verbose" }},
    {"poolpair",    "addpoolliquidity",         &addpoolliquidity,          {"from", "shareAddress", "inputs"}},
    {"poolpair",    "removepoolliquidity",      &removepoolliquidity,       {"from", "amount", "inputs"}},
    {"poolpair",    "createpoolpair",           &createpoolpair,            {"metadata", "inputs"}},
    {"poolpair",    "updatepoolpair",           &updatepoolpair,            {"metadata", "inputs"}},
    {"poolpair",    "poolswap",                 &poolswap,                  {"metadata", "inputs"}},
    {"poolpair",    "compositeswap",            &compositeswap,             {"metadata", "inputs"}},
    {"poolpair",    "listpoolshares",           &listpoolshares,            {"pagination", "verbose", "is_mine_only"}},
    {"poolpair",    "testpoolswap",             &testpoolswap,              {"metadata", "path", "verbose"}},
};

void RegisterPoolpairRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
