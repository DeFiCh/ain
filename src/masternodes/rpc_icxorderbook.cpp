#include <masternodes/mn_rpc.h>

UniValue icxOrderToJSON(CICXOrderImplemetation const& order, uint8_t status) {
    UniValue orderObj(UniValue::VOBJ);
    UniValue ret(UniValue::VOBJ);

    auto token = pcustomcsview->GetToken(order.idToken);
        if (!token)
            return (ret);
    switch (status)
    {
        case 0: orderObj.pushKV("status", "OPEN");
                break;
        case 1: orderObj.pushKV("status", "CLOSED");
                break;
        case 2: orderObj.pushKV("status", "FILLED");
                break;
        case 3: orderObj.pushKV("status", "EXPIRED");
                break;
    }
    if (order.orderType == CICXOrder::TYPE_INTERNAL)
    {
        orderObj.pushKV("type", "INTERNAL");
        orderObj.pushKV("tokenFrom", token->CreateSymbolKey(order.idToken));
        orderObj.pushKV("chainTo", order.chain);
        orderObj.pushKV("ownerAddress", ScriptToString(order.ownerAddress));
    }
    else if (order.orderType == CICXOrder::TYPE_EXTERNAL)
    {
        orderObj.pushKV("type", "EXTERNAL");
        orderObj.pushKV("chainFrom",order.chain);
        orderObj.pushKV("tokenTo", token->CreateSymbolKey(order.idToken));
    }
    orderObj.pushKV("amountFrom", ValueFromAmount(order.amountFrom));
    orderObj.pushKV("amountToFill", ValueFromAmount(order.amountToFill));
    orderObj.pushKV("orderPrice", ValueFromAmount(order.orderPrice));
    CAmount calcedAmount(static_cast<CAmount>((arith_uint256(order.amountToFill) * arith_uint256(order.orderPrice) / arith_uint256(COIN)).GetLow64()));
    orderObj.pushKV("amountToFillInToAsset", ValueFromAmount(calcedAmount));
    orderObj.pushKV("height", static_cast<int>(order.creationHeight));
    orderObj.pushKV("expireHeight", static_cast<int>(order.creationHeight + order.expiry));
    if (order.closeHeight > -1)
    {
        orderObj.pushKV("closeHeight", static_cast<int>(order.closeHeight));
        if (!order.closeTx.IsNull()) orderObj.pushKV("closeTx", order.closeTx.GetHex());
    }
    else if (order.creationHeight + order.expiry <= pcustomcsview->GetLastHeight())
    {
        orderObj.pushKV("expired", true);
    }
    
    ret.pushKV(order.creationTx.GetHex(), orderObj);
    return (ret);
}

UniValue icxMakeOfferToJSON(CICXMakeOfferImplemetation const& makeoffer, uint8_t status) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("orderTx", makeoffer.orderTx.GetHex());
    orderObj.pushKV("status", status == CICXMakeOffer::STATUS_OPEN ? "OPEN" : status == CICXMakeOffer::STATUS_CLOSED ? "CLOSED" : "EXPIRED");
    orderObj.pushKV("amount", ValueFromAmount(makeoffer.amount));
    if (!makeoffer.ownerAddress.empty())
        orderObj.pushKV("ownerAddress",ScriptToString(makeoffer.ownerAddress));
    if (CPubKey(makeoffer.receiveDestination).IsFullyValid())
        orderObj.pushKV("receivePubkey", HexStr(makeoffer.receiveDestination));
    else if (!ScriptToString(CScript(makeoffer.receiveDestination.begin(),makeoffer.receiveDestination.end())).empty())
        orderObj.pushKV("receiveAddress", ScriptToString(CScript(makeoffer.receiveDestination.begin(),makeoffer.receiveDestination.end())));
    orderObj.pushKV("takerFee", ValueFromAmount(makeoffer.takerFee));
    orderObj.pushKV("expireHeight", static_cast<int>(makeoffer.creationHeight + makeoffer.expiry));
    
    UniValue ret(UniValue::VOBJ);
    ret.pushKV(makeoffer.creationTx.GetHex(), orderObj);
    return ret;
}

UniValue icxSubmitDFCHTLCToJSON(CICXSubmitDFCHTLCImplemetation const& dfchtlc, uint8_t status) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("type", "DFC");
    orderObj.pushKV("status", status == CICXSubmitDFCHTLC::STATUS_OPEN ? "OPEN" : status == CICXSubmitDFCHTLC::STATUS_CLAIMED ? "CLAIMED" : "REFUNDED");
    orderObj.pushKV("offerTx", dfchtlc.offerTx.GetHex());
    orderObj.pushKV("amount", ValueFromAmount(dfchtlc.amount));
    orderObj.pushKV("receiveAddress",ScriptToString(dfchtlc.receiveAddress));
    if (dfchtlc.receivePubkey.IsFullyValid())
        orderObj.pushKV("receivePubkey", HexStr(dfchtlc.receivePubkey));
    orderObj.pushKV("hash", dfchtlc.hash.GetHex());
    orderObj.pushKV("timeout", static_cast<int>(dfchtlc.timeout));
    orderObj.pushKV("height", static_cast<int>(dfchtlc.creationHeight));
    orderObj.pushKV("refundHeight", static_cast<int>(dfchtlc.creationHeight + dfchtlc.timeout));
    
    UniValue ret(UniValue::VOBJ);
    ret.pushKV(dfchtlc.creationTx.GetHex(), orderObj);
    return ret;
}

UniValue icxSubmitEXTHTLCToJSON(CICXSubmitEXTHTLCImplemetation const& exthtlc) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("type", "EXTERNAL");
    orderObj.pushKV("offerTx", exthtlc.offerTx.GetHex());
    orderObj.pushKV("amount", ValueFromAmount(exthtlc.amount));
    if (!exthtlc.receiveAddress.empty())
        orderObj.pushKV("receiveAddress",ScriptToString(exthtlc.receiveAddress));
    orderObj.pushKV("hash", exthtlc.hash.GetHex());
    orderObj.pushKV("htlcscriptAddress", exthtlc.htlcscriptAddress);
    orderObj.pushKV("ownerPubkey", HexStr(exthtlc.ownerPubkey));
    orderObj.pushKV("externalTimeout", static_cast<int>(exthtlc.timeout));
    orderObj.pushKV("height", static_cast<int>(exthtlc.creationHeight));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(exthtlc.creationTx.GetHex(), orderObj);
    return ret;
}

UniValue icxClaimDFCHTLCToJSON(CICXClaimDFCHTLCImplemetation const& claimdfchtlc) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("type", "CLAIM DFC");
    orderObj.pushKV("dfchtlcTx", claimdfchtlc.dfchtlcTx.GetHex());
    orderObj.pushKV("seed", HexStr(claimdfchtlc.seed));
    orderObj.pushKV("height", static_cast<int>(claimdfchtlc.creationHeight));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(claimdfchtlc.creationTx.GetHex(), orderObj);
    return ret;
}

UniValue icxcreateorder(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"icx_createorder",
                "\nCreates (and submits to local node and network) a order creation transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"order", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"tokenFrom|chainFrom", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol or id of selling token/chain"},
                            {"chainTo|tokenTo", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol or id of buying chain/token"},
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address of tokens in case of DFC/EXT order"},
                            {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO, "tokenFrom coins amount"},
                            {"orderPrice", RPCArg::Type::NUM, RPCArg::Optional::NO, "Price per unit"},
                            {"expiry", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Number of blocks until the order expires (Default: 2880 blocks)"},
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
                        HelpExampleCli("icx_createorder", "'{\"ownerAddress\":\"<tokenAddress>\","
                                                        "\"tokenFrom\":\"GOLD#128\",\"chainTo\":\"BTC\","
                                                        "\"amountFrom\":\"10\",\"orderPrice\":\"10\"}'")
                        + HelpExampleCli("icx_createorder", "'{\"chainFrom\":\"BTC\",\"tokenTo\":\"SILVER#129\","
                                                        "\"amountFrom\":\"5\",\"orderPrice\":\"0.01\","
                                                        "\"expiry\":\"120\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create order while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"tokenFrom|chainFrom\",\"chainTo|tokenTo\",\"ownerAddress\",\"amountFrom\",\"orderPrice\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXOrder order;
    std::string tokenFromSymbol, tokenToSymbol;

    if (!metaObj["tokenFrom"].isNull()) {
        tokenFromSymbol = trim_ws(metaObj["tokenFrom"].getValStr());

        if (!metaObj["chainTo"].isNull())
            order.chain = trim_ws(metaObj["chainTo"].getValStr());
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"chainTo\" must be non-null if \"tokenFrom\" specified");

        if (!metaObj["ownerAddress"].isNull())
            order.ownerAddress = DecodeScript(metaObj["ownerAddress"].getValStr());
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"ownerAddress\" must be non-null if \"tokenFrom\" specified");
       
        if (!::IsMine(*pwallet, DecodeDestination(metaObj["ownerAddress"].getValStr())))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Address (%s) is not owned by the wallet", metaObj["ownerAddress"].getValStr()));
    }
    else if (!metaObj["chainFrom"].isNull()) {
        order.chain = trim_ws(metaObj["chainFrom"].getValStr());

        if (!metaObj["tokenTo"].isNull()) 
            tokenToSymbol = trim_ws(metaObj["tokenTo"].getValStr());
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"tokenTo\" must be non-null if \"chainFrom\" specified");
    }
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"tokenFrom\" or \"chainFrom\" must be non-null");
    if (!metaObj["amountFrom"].isNull())
        order.amountToFill = order.amountFrom = AmountFromValue(metaObj["amountFrom"]);
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amountFrom\" must be non-null");
    if (!metaObj["orderPrice"].isNull())
        order.orderPrice = AmountFromValue(metaObj["orderPrice"]);
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"orderPrice\" must be non-null");
    
    if (!metaObj["expiry"].isNull()) order.expiry = metaObj["expiry"].get_int();

    if (tokenFromSymbol.empty() && order.chain.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, either \"tokenFrom\" or \"chainFrom\" must be non-null. [tokenFrom,chainTo] or [chainFrom,tokenTo]");
    if (!tokenFromSymbol.empty() && !tokenToSymbol.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, \"tokenFrom\" and \"tokenTo\" cannot be set in the same time. [tokenFrom,chainTo] or [chainFrom,tokenTo]");
    
    if (!tokenFromSymbol.empty())
        order.orderType = CICXOrder::TYPE_INTERNAL;
    else
        order.orderType = CICXOrder::TYPE_EXTERNAL;

    int targetHeight;
    {
        LOCK(cs_main);
        DCT_ID idToken;
        std::unique_ptr<CToken> token;

        if (order.orderType == CICXOrder::TYPE_INTERNAL)
        {
            token = pcustomcsview->GetTokenGuessId(tokenFromSymbol, idToken);
            if (!token)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenFromSymbol));
            order.idToken = idToken;

            CTokenAmount balance = pcustomcsview->GetBalance(order.ownerAddress,idToken);
            if (balance.nValue < order.amountFrom)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s on address %s!", token->CreateSymbolKey(order.idToken), ScriptToString(order.ownerAddress)));
        }
        else
        {
            token = pcustomcsview->GetTokenGuessId(tokenToSymbol, idToken);
            if (!token)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenToSymbol));
            order.idToken = idToken;
        }

        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXCreateOrder)
             << order;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest))
            coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, order});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXCreateOrderMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue icxmakeoffer(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"icx_makeoffer",
                "\nCreates (and submits to local node and network) a makeoffer transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"offer", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of order tx for which is the offer"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "amount fulfilling the order"},
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address of tokens in case of EXT/DFC order"},
                            {"receiveAddress", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "address for receiving DFC tokens in case of DFC/EXT order type"},
                            {"receivePubkey", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "pubkey which can claim external HTLC in case of EXT/DFC order type"},
                            {"expiry", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Number of blocks until the offer expires (Default: 10 blocks)"},
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
                        HelpExampleCli("icx_makeoffer", "'{\"orderTx\":\"<txid>\",\"amount\":\"10\","
                                                        "\receiveAddress\":\"<address>\",}'")
                        + HelpExampleCli("icx_makeoffer", "'{\"orderTx\":\"txid\",\"amount\":\"10\","
                                                        "\"ownerAddress\":\"<address>\",\"receivePubkey\":\"<pubkey>\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot make offer while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"orderTx\",\"amount\", \"receivePubkey|receiveAddress\"}");
    }
    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXMakeOffer makeoffer;

    if (!metaObj["orderTx"].isNull())
        makeoffer.orderTx = uint256S(metaObj["orderTx"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"orderTx\" must be non-null");
    if (!metaObj["amount"].isNull())
        makeoffer.amount = AmountFromValue(metaObj["amount"]);
    else 
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amount\" must be non-null");
    
    if (!metaObj["expiry"].isNull()) makeoffer.expiry = metaObj["expiry"].get_int();

    if (makeoffer.expiry < CICXMakeOffer::DEFAULT_EXPIRY)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameters, argument \"expiry\" must be greater than %d", CICXMakeOffer::DEFAULT_EXPIRY - 1));

    int targetHeight;
    {
        LOCK(cs_main);
        auto order = pcustomcsview->GetICXOrderByCreationTx(makeoffer.orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("orderTx (%s) does not exist",makeoffer.orderTx.GetHex()));

        // if (order->amountToFill * order->orderPrice / COIN < makeoffer.amount)
        //     throw JSONRPCError(RPC_INVALID_PARAMETER, "cannot make offer with that amount, order (" + order->creationTx.GetHex() + ") has less amount to fill!");

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
        {
            if (!metaObj["receiveAddress"].isNull()) {
                CScript dest=DecodeScript(metaObj["receiveAddress"].getValStr());
                makeoffer.receiveDestination = std::vector<uint8_t>(dest.begin(),dest.end());
            }
            else
                throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receiveAddress\" must be non-null");
            if (!::IsMine(*pwallet, DecodeDestination(metaObj["receiveAddress"].getValStr())))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Address (%s) is not owned by the wallet", metaObj["receiveAddress"].getValStr()));
        }
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        {
            if (!metaObj["ownerAddress"].isNull())
                makeoffer.ownerAddress = DecodeScript(metaObj["ownerAddress"].getValStr());
            else
                throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"ownerAddress\" must be non-null");
            if (!::IsMine(*pwallet, DecodeDestination(metaObj["ownerAddress"].getValStr())))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Address (%s) is not owned by the wallet", metaObj["ownerAddress"].getValStr()));
            
            CTokenAmount balance = pcustomcsview->GetBalance(makeoffer.ownerAddress,order->idToken);
            if (balance.nValue < makeoffer.amount)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s on address %s!", 
                        pcustomcsview->GetToken(order->idToken)->CreateSymbolKey(order->idToken), ScriptToString(makeoffer.ownerAddress)));
            
            if (!metaObj["receivePubkey"].isNull())
                makeoffer.receiveDestination = ParseHex(trim_ws(metaObj["receivePubkey"].getValStr()));
            else
                throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receivePubKey\" must be non-null");
        }
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXMakeOffer)
             << makeoffer;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest))
            coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, makeoffer});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXMakeOfferMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue icxsubmitdfchtlc(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"icx_submitdfchtlc",
                "\nCreates (and submits to local node and network) a dfc htlc transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"htlc", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"offerTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of offer tx for which the htlc is"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "amount in htlc"},
                            {"receiveAddress", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "address that receives DFC tokens when HTLC is claimed"},
                            {"receivePubkey", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "pubkey which can claim external HTLC in case of DFC/EXT order type"},
                            {"hash", RPCArg::Type::STR, RPCArg::Optional::NO, "hash of seed used for the hash lock part"},
                            {"timeout", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "timeout (absolute in blocks) for expiration of htlc (Default: 10)"},
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
                        HelpExampleCli("icx_submitdfchtlc", "'{\"offerTx\":\"<txid>\",\"amount\":\"10\","
                                                        "\"receiveAddress\":\"<address>\",\"hash\":\"<hash>\",\"timeout\":\"50\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot submit dfc htlc while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"offerTx\",\"amount\",\"receiverAddress\",\"hash\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXSubmitDFCHTLC submitdfchtlc;

    if (!metaObj["offerTx"].isNull())
        submitdfchtlc.offerTx = uint256S(metaObj["offerTx"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"offerTx\" must be non-null");
    if (!metaObj["amount"].isNull())
        submitdfchtlc.amount = AmountFromValue(metaObj["amount"]);
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amount\" must be non-null");
    if (!metaObj["receiveAddress"].isNull())
        submitdfchtlc.receiveAddress = DecodeScript(metaObj["receiveAddress"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receiveAddress\" must be non-null");
    if (!metaObj["hash"].isNull())
        submitdfchtlc.hash = uint256S(metaObj["hash"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"hash\" must be non-null");
    if (!metaObj["timeout"].isNull())
        submitdfchtlc.timeout = metaObj["timeout"].get_int();
    
    int targetHeight;
    {
        LOCK(cs_main);
        auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(submitdfchtlc.offerTx);
        if (!offer)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("offerTx (%s) does not exist",submitdfchtlc.offerTx.GetHex()));
        auto order = pcustomcsview->GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("orderTx (%s) does not exist",offer->orderTx.GetHex()));

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
        {
            // if (submitdfchtlc.amount * order->orderPrice / COIN != offer->amount)
            //     throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("cannot make dfc htlc with that amount, different amount necessary for offer (%s) - %s != %s!",
            //                     offer->creationTx.GetHex(), ValueFromAmount(submitdfchtlc.amount * order->orderPrice / COIN).getValStr(), ValueFromAmount(offer->amount).getValStr()));

            if (!metaObj["receivePubkey"].isNull()) {
                submitdfchtlc.receivePubkey = PublickeyFromString(trim_ws(metaObj["receivePubkey"].getValStr()));
                if (!submitdfchtlc.receivePubkey.IsFullyValid())
                    throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receivePubKey\" is not a valid pubkey");
            }
            else
                throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receivePubKey\" must be non-null");

            bool found = false;
            pcustomcsview->ForEachICXSubmitDFCHTLCOpen([&found, &submitdfchtlc](CICXOrderView::TxidPairKey const & key, uint8_t i) {
                if (key.first != submitdfchtlc.offerTx) 
                    return false;
                found = true;
                return false;
            }, submitdfchtlc.offerTx);
            if (found)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "dfc htlc already submitted!");

            found = false;
            pcustomcsview->ForEachICXSubmitEXTHTLC([&found, &submitdfchtlc](CICXOrderView::TxidPairKey const & key, uint8_t i) {
                if (key.first == submitdfchtlc.offerTx)
                {
                    found = true;
                    return (false);
                }
                return false;
            }, submitdfchtlc.offerTx);
            if (found)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("offer (%s) needs to have dfc htlc submitted first, but external htlc already submitted!",
                        submitdfchtlc.offerTx.GetHex()));
            
            if (submitdfchtlc.timeout < CICXSubmitDFCHTLC::DEFAULT_TIMEOUT)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameters, argument \"timeout\" must be greater than %d", CICXSubmitDFCHTLC::DEFAULT_TIMEOUT - 1));
        }
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        {
            CTokenAmount balance = pcustomcsview->GetBalance(offer->ownerAddress,order->idToken);
            if (balance.nValue < offer->amount)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s on address %s!", 
                        pcustomcsview->GetToken(order->idToken)->CreateSymbolKey(order->idToken), ScriptToString(offer->ownerAddress)));

            if (submitdfchtlc.amount != offer->amount)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("cannot make dfc htlc with that amount, different amount necessary for offer (%s) - %s != %s!",
                                offer->creationTx.GetHex(), ValueFromAmount(submitdfchtlc.amount).getValStr(), ValueFromAmount(offer->amount).getValStr()));

            bool found = false;
            pcustomcsview->ForEachICXSubmitDFCHTLCOpen([&found, &submitdfchtlc](CICXOrderView::TxidPairKey const & key, uint8_t i) {
                if (key.first != submitdfchtlc.offerTx)
                    return false;
                found = true;
                return false;
            }, submitdfchtlc.offerTx);
            if (found)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "dfc htlc already submitted!");

            std::unique_ptr<CICXSubmitEXTHTLCImplemetation> exthtlc;
            pcustomcsview->ForEachICXSubmitEXTHTLC([&](CICXOrderView::TxidPairKey const & key, uint8_t i) {
                if (key.first == submitdfchtlc.offerTx)
                {
                    exthtlc = pcustomcsview->GetICXSubmitEXTHTLCByCreationTx(key.second);
                    return (false);
                }
                return false;
            }, submitdfchtlc.offerTx);
            if (!exthtlc)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("offer (%s) needs to have ext htlc submitted first, but no external htlc found!",
                            submitdfchtlc.offerTx.GetHex()));
            if (submitdfchtlc.hash != exthtlc->hash)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid hash, dfc htlc hash is different than extarnal htlc hash!");
        }

        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXSubmitDFCHTLC)
             << submitdfchtlc;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest))
            coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, submitdfchtlc});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXSubmitDFCHTLCMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue icxsubmitexthtlc(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"icx_submitexthtlc",
                "\nCreates (and submits to local node and network) ext htlc transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"htlc", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"offerTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of offer tx for which the htlc is"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "amount in htlc"},
                            {"receiveAddress", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "address for receiving DFC tokens in case of EXT/BTC order type"},
                            {"htlcScriptAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "script address of external htlc"},
                            {"hash", RPCArg::Type::STR, RPCArg::Optional::NO, "hash of seed used for the hash lock part"},
                            {"ownerPubkey", RPCArg::Type::STR, RPCArg::Optional::NO, "pubkey of the owner to which the funds are refunded if HTLC timeouts"},
                            {"timeout", RPCArg::Type::NUM, RPCArg::Optional::NO, "timeout (absolute in block) for expiration of external htlc"},
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
                        HelpExampleCli("icx_submitexthtlc", "'{\"offerTx\":\"<txid>\",\"amount\":\"1\""
                                                        "\"htlcScriptAddress\":\"<script_address>\",\"hash\":\"<hash>\""
                                                        "\"ownerPubkey\":\"<pubkey>\",\"timeout\":\"20\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot submit ext htlc while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"offerTx\",\"amount\",\"htlcScriptAddress\",\"hash\",\"refundPubkey\",\"timeout\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXSubmitEXTHTLC submitexthtlc;

    if (!metaObj["offerTx"].isNull())
        submitexthtlc.offerTx = uint256S(metaObj["offerTx"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"offerTx\" must be non-null");
    if (!metaObj["amount"].isNull())
        submitexthtlc.amount = AmountFromValue(metaObj["amount"]);
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amount\" must be non-null");
    if (!metaObj["hash"].isNull())
        submitexthtlc.hash = uint256S(metaObj["hash"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"hash\" must be non-null");
    if (!metaObj["htlcScriptAddress"].isNull())
        submitexthtlc.htlcscriptAddress = trim_ws(metaObj["htlcScriptAddress"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"htlcScriptAddress\" must be non-null");
    if (!metaObj["ownerPubkey"].isNull())
        submitexthtlc.ownerPubkey = PublickeyFromString(metaObj["ownerPubkey"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"ownerPubkey\" must be non-null");
    if (!metaObj["timeout"].isNull())
        submitexthtlc.timeout = metaObj["timeout"].get_int();
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"timeout\" must be non-null");

    int targetHeight;
    {
        LOCK(cs_main);
        auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(submitexthtlc.offerTx);
        if (!offer)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("offerTx (%s) does not exist",submitexthtlc.offerTx.GetHex()));
        auto order = pcustomcsview->GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("orderTx (%s) does not exist",offer->orderTx.GetHex()));
        

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
        {
            if (submitexthtlc.amount != offer->amount)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("cannot make ext htlc with that amount, different amount necessary for offer (%s) - %s != %s!",
                                offer->creationTx.GetHex(), ValueFromAmount(submitexthtlc.amount).getValStr(), ValueFromAmount(offer->amount).getValStr()));

            bool found = false;
            pcustomcsview->ForEachICXSubmitEXTHTLC([&found, &submitexthtlc](CICXOrderView::TxidPairKey const & key, uint8_t i) {
                if (key.first != submitexthtlc.offerTx)
                    return false;
                found = true;
                return false;
            }, submitexthtlc.offerTx);
            if (found)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "ext htlc already submitted!");

            std::unique_ptr<CICXSubmitDFCHTLCImplemetation> dfchtlc;
            pcustomcsview->ForEachICXSubmitDFCHTLCOpen([&](CICXOrderView::TxidPairKey const & key, uint8_t) {
                if (key.first == submitexthtlc.offerTx)
                {
                    dfchtlc = pcustomcsview->GetICXSubmitDFCHTLCByCreationTx(key.second);
                    return (false);
                }
                return false;
            }, submitexthtlc.offerTx);
            if (!dfchtlc)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("offer (%s) needs to have dfc htlc submitted first, but no dfc htlc found!",
                        submitexthtlc.offerTx.GetHex()));
            if (submitexthtlc.hash != dfchtlc->hash)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid hash, external htlc hash is different than dfc htlc hash! - %s != %s!",
                        submitexthtlc.hash.GetHex(),dfchtlc->hash.GetHex()));
        }
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        {
            // if (submitexthtlc.amount * order->orderPrice / COIN != offer->amount )
            //     throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("cannot make ext htlc with that amount, different amount necessary for offer (%s) - %s != %s!",
            //                     offer->creationTx.GetHex(), ValueFromAmount(submitexthtlc.amount * order->orderPrice / COIN).getValStr(), ValueFromAmount(offer->amount).getValStr()));

            if (!metaObj["receiveAddress"].isNull())
                submitexthtlc.receiveAddress=DecodeScript(metaObj["receiveAddress"].getValStr());
            else
                throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receiveAddress\" must be non-null");
            if (!::IsMine(*pwallet, DecodeDestination(metaObj["receiveAddress"].getValStr())))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Address (%s) is not owned by the wallet", metaObj["receiveAddress"].getValStr()));

            bool found = false;
            pcustomcsview->ForEachICXSubmitEXTHTLC([&found, &submitexthtlc](CICXOrderView::TxidPairKey const & key, uint8_t i) {
                if (key.first != submitexthtlc.offerTx)
                    return false;
                found = true;
                return false;
            }, submitexthtlc.offerTx);
            if (found)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "ext htlc already submitted!");

            found = false;
            pcustomcsview->ForEachICXSubmitDFCHTLCOpen([&found, &submitexthtlc](CICXOrderView::TxidPairKey const & key, uint8_t) {
                if (key.first == submitexthtlc.offerTx)
                {
                    found = true;
                    return (false);
                }
                return true;
            }, submitexthtlc.offerTx);
            if (found)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("offer (%s) needs to have dfc htlc submitted first, but external htlc already submitted!",
                        submitexthtlc.offerTx.GetHex()));
        }        
        
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXSubmitEXTHTLC)
             << submitexthtlc;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) 
            coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, submitexthtlc});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXSubmitEXTHTLCMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue icxclaimdfchtlc(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"icx_claimdfchtlc",
                "\nCreates (and submits to local node and network) a dfc htlc transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"htlc", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"dfchtlcTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of dfc htlc tx for which the claim is"},
                            {"seed", RPCArg::Type::STR, RPCArg::Optional::NO, "secret seed for claiming htlc"},
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
                        HelpExampleCli("icx_claimdfchtlc", "'{\"dfchtlcTx\":\"<txid>>\",\"seed\":\"<seed>\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot claim dfc htlc while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"dfchtlcTx\",\"receiverAddress\",\"seed\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXClaimDFCHTLC claimdfchtlc;

    if (!metaObj["dfchtlcTx"].isNull())
        claimdfchtlc.dfchtlcTx = uint256S(metaObj["dfchtlcTx"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"dfchtlcTx\" must be non-null");
    if (!metaObj["seed"].isNull())
        claimdfchtlc.seed = ParseHex(metaObj["seed"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"seed\" must be non-null");

    int targetHeight;
    {
        LOCK(cs_main);
        auto dfchtlc = pcustomcsview->GetICXSubmitDFCHTLCByCreationTx(claimdfchtlc.dfchtlcTx);
        if (!dfchtlc)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "offerTx (" + claimdfchtlc.dfchtlcTx.GetHex() + ") does not exist");

        std::vector<unsigned char> calcSeedBytes(32);
        uint256 calcHash;
        CSHA256()
            .Write(claimdfchtlc.seed.data(),claimdfchtlc.seed.size())
            .Finalize(calcSeedBytes.data());
        calcHash.SetHex(HexStr(calcSeedBytes));

        if (dfchtlc->hash != calcHash)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "hash generated from given seed is different than in dfc htlc: " + calcHash.GetHex() + " - " + dfchtlc->hash.GetHex());
        
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXClaimDFCHTLC)
             << claimdfchtlc;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest))
            coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, claimdfchtlc});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXClaimDFCHTLCMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue icxcloseorder(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"icx_closeorder",
                "\nCloses (and submits to local node and network) order transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of maker order"},
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
                        HelpExampleCli("closeorder", "'{\"orderTx\":\"<txid>>\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot close order while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"orderTx\"}");

    UniValue const & txInputs = request.params[1];

    CICXCloseOrder closeorder;
    closeorder.orderTx = uint256S(request.params[0].getValStr());

    int targetHeight;
    {
        LOCK(cs_main);
        auto order = pcustomcsview->GetICXOrderByCreationTx(closeorder.orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + closeorder.orderTx.GetHex() + ") does not exist");
        if (!order->closeTx.IsNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,"orderTx (" + closeorder.orderTx.GetHex() + " is already closed!");
        }

        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXCloseOrder)
             << closeorder;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest))
            coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, closeorder});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXCloseOrderMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue icxcloseoffer(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"icx_closeoffer",
                "\nCloses (and submits to local node and network) offer transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"offerTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of maker offer"},
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
                        HelpExampleCli("closeOffer", "'{\"offerTx\":\"<txid>>\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot close offer while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"offerTx\"}");

    UniValue const & txInputs = request.params[1];

    CICXCloseOffer closeoffer;
    closeoffer.offerTx = uint256S(request.params[0].getValStr());

    int targetHeight;
    {
        LOCK(cs_main);

        auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(closeoffer.offerTx);
        if (!offer)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "OfferTx (" + closeoffer.offerTx.GetHex() + ") does not exist");
        if (!offer->closeTx.IsNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,"OfferTx (" + closeoffer.offerTx.GetHex() + " is already closed!");
        }

        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXCloseOffer)
             << closeoffer;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest))
            coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, closeoffer});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXCloseOfferMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue icxgetorder(const JSONRPCRequest& request) {
    RPCHelpMan{"icx_getorder",
                "\nReturn information about order or fillorder.\n",
                {
                    {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of createorder or fulfillorder tx"},
                },
                RPCResult
                {
                    "{...}     (object) Json object with order information\n"
                },
                RPCExamples{
                    HelpExampleCli("icx_getorder", "'{\"orderTx\":\"<txid>>\"}'")     
                },
     }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"orderTx\"}");

    uint256 orderTxid= uint256S(request.params[0].getValStr());
    auto order = pcustomcsview->GetICXOrderByCreationTx(orderTxid);
    if (order)
        return icxOrderToJSON(*order,-1);
    
    auto fillorder = pcustomcsview->GetICXMakeOfferByCreationTx(orderTxid);
    if (fillorder)
        return icxMakeOfferToJSON(*fillorder,-1);

    throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + orderTxid.GetHex() + ") does not exist");    
}

UniValue icxlistorders(const JSONRPCRequest& request) {
    RPCHelpMan{"icx_listorders",
                "\nReturn information about orders.\n",
                {
                        {"by", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                {"token",  RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Token asset"},
                                {"chain",  RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Chain asset"},
                                {"orderTx",RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Order txid to list all offers for this order"},
                                {"limit",  RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Maximum number of orders to return (default: 50)"},
                                {"closed", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Display closed orders (default: false)"},
                            },
                        },
                },
                RPCResult
                {
                        "{{...},...}     (array) Json object with orders information\n"
                },
                RPCExamples{
                        HelpExampleCli("icx_listorders", "'{\"limit\":\"10\"}'")
                        + HelpExampleCli("icx_listorders", "'{\"token\":\"GOLD#128\",\"chain\":\"BTC\"}'")
                        + HelpExampleCli("icx_listorders", "'{\"chain\":\"BTC\",\"token\":\"SILVER#129\",\"closed\":true}'")
                        + HelpExampleCli("icx_listorders", "'{\"orderTx\":\"<txid>>\"}'")
                }
     }.Check(request);

    size_t limit = 50;
    std::string tokenSymbol, chain;
    uint256 orderTxid;
    bool closed = false;

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params.size() > 0)
    {
        UniValue byObj = request.params[0].get_obj();
        if (!byObj["token"].isNull()) tokenSymbol = trim_ws(byObj["token"].getValStr());
        if (!byObj["chain"].isNull()) chain = trim_ws(byObj["chain"].getValStr());
        if (!byObj["orderTx"].isNull()) orderTxid = uint256S(byObj["orderTx"].getValStr());
        if (!byObj["limit"].isNull()) limit = (size_t) byObj["limit"].get_int64();
        if (!byObj["closed"].isNull()) closed = byObj["closed"].get_bool();
    }

    DCT_ID idToken = {std::numeric_limits<uint32_t>::max()};
    if (!tokenSymbol.empty() && !chain.empty())
    {        
        auto token1 = pcustomcsview->GetTokenGuessId(tokenSymbol, idToken);
        if (!token1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));
    }

    UniValue ret(UniValue::VOBJ);
    if (idToken.v != std::numeric_limits<uint32_t>::max())
    {
        CICXOrderView::AssetPair prefix;
        prefix.first = idToken;
        prefix.second = chain;

        if (closed)
        {
            pcustomcsview->ForEachICXOrderClose([&](CICXOrderView::OrderKey const & key, uint8_t status) {
                if (key.first != prefix)
                    return (false);
                auto order = pcustomcsview->GetICXOrderByCreationTx(key.second);
                if (order)
                    ret.pushKVs(icxOrderToJSON(*order, status));
                limit--;
                return limit != 0;
            }, prefix);
        }
        else 
        {
            pcustomcsview->ForEachICXOrderOpen([&](CICXOrderView::OrderKey const & key, uint8_t status) {
                if (key.first != prefix)
                    return (false);
                auto order = pcustomcsview->GetICXOrderByCreationTx(key.second);
                if (order)
                    ret.pushKVs(icxOrderToJSON(*order, status));
                limit--;
                return limit != 0;
            }, prefix);
        }
        return ret;
    }
    else if (!orderTxid.IsNull())
    {
        if (closed)
        {
            pcustomcsview->ForEachICXMakeOfferClose([&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
                if (key.first != orderTxid)
                    return (false);
                auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(key.second);
                if (offer)
                    ret.pushKVs(icxMakeOfferToJSON(*offer, status));
                limit--;
                return limit != 0;
            }, orderTxid);
        }
        else
        {
            pcustomcsview->ForEachICXMakeOfferOpen([&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
                if (key.first != orderTxid)
                    return (false);
                auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(key.second);
                if (offer)
                    ret.pushKVs(icxMakeOfferToJSON(*offer, status));
                limit--;
                return limit != 0;
            }, orderTxid);
        }
        return ret;
    }
    if (closed)
        pcustomcsview->ForEachICXOrderClose([&](CICXOrderView::OrderKey const & key, uint8_t status) {
            auto order = pcustomcsview->GetICXOrderByCreationTx(key.second);
            if (order) ret.pushKVs(icxOrderToJSON(*order, status));
            limit--;
            return limit != 0;
        });
    else
        pcustomcsview->ForEachICXOrderOpen([&](CICXOrderView::OrderKey const & key, uint8_t status) {
            auto order = pcustomcsview->GetICXOrderByCreationTx(key.second);
            if (order) ret.pushKVs(icxOrderToJSON(*order, status));
            limit--;
            return limit != 0;
        });
    return ret;
}

UniValue icxlisthtlcs(const JSONRPCRequest& request) {
    RPCHelpMan{"icx_listhtlcs",
                "\nReturn information about orders.\n",
                {
                        {"by", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                            {
                                {"offerTx",RPCArg::Type::STR, RPCArg::Optional::NO, "Offer txid  for which to list all HTLCS"},
                                {"limit",  RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Maximum number of orders to return (default: 20)"},
                                {"refunded", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Display refunded HTLC (default: false)"},
                                {"claimed",  RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Display claimed HTLCs (default: false)"},
                                
                            },
                        },
                },
                RPCResult
                {
                        "{{...},...}     (array) Json object with orders information\n"
                },
                RPCExamples{
                        HelpExampleCli("icx_listorders", "'{\"offerTx\":\"<txid>\"}'")
                    
                }
     }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"offerTx\"}");
   
    size_t limit = 20;
    uint256 offerTxid;
    bool closed = false;
    
    UniValue byObj = request.params[0].get_obj();
    if (!byObj["offerTx"].isNull())
        offerTxid = uint256S(byObj["offerTx"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"offerTx\" must be non-null");
    
    if (!byObj["closed"].isNull())
        closed = byObj["closed"].get_bool();
    if (!byObj["limit"].isNull())
        limit = (size_t) byObj["limit"].get_int64();
    
    UniValue ret(UniValue::VOBJ);
    pcustomcsview->ForEachICXClaimDFCHTLC([&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
        if (key.first != offerTxid)
            return false;
        auto claimdfchtlc = pcustomcsview->GetICXClaimDFCHTLCByCreationTx(key.second);
        if (claimdfchtlc)
            ret.pushKVs(icxClaimDFCHTLCToJSON(*claimdfchtlc));
        limit--;
        return limit != 0;
    }, offerTxid);
    if (closed)
    {
        pcustomcsview->ForEachICXSubmitDFCHTLCClose([&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
            if (key.first != offerTxid)
                return false;
            auto dfchtlc = pcustomcsview->GetICXSubmitDFCHTLCByCreationTx(key.second);
            if (dfchtlc)
                ret.pushKVs(icxSubmitDFCHTLCToJSON(*dfchtlc,status));
            limit--;
            return limit != 0;
        }, offerTxid);
    }
    pcustomcsview->ForEachICXSubmitDFCHTLCOpen([&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
        if (key.first != offerTxid)
            return false;
        auto dfchtlc = pcustomcsview->GetICXSubmitDFCHTLCByCreationTx(key.second);
        if (dfchtlc)
            ret.pushKVs(icxSubmitDFCHTLCToJSON(*dfchtlc,status));
        limit--;
        return limit != 0;
    }, offerTxid);
    pcustomcsview->ForEachICXSubmitEXTHTLC([&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
        if (key.first != offerTxid)
            return false;
        auto exthtlc = pcustomcsview->GetICXSubmitEXTHTLCByCreationTx(key.second);
        if (exthtlc)
            ret.pushKVs(icxSubmitEXTHTLCToJSON(*exthtlc));
        limit--;
        return limit != 0;
    }, offerTxid);
    return ret;
}

static const CRPCCommand commands[] =
{ 
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"icxorderbook",   "icx_createorder",        &icxcreateorder,        {"order"}},
    {"icxorderbook",   "icx_makeoffer",          &icxmakeoffer,          {"offer"}},
    {"icxorderbook",   "icx_submitdfchtlc",      &icxsubmitdfchtlc,      {"dfchtlc"}},
    {"icxorderbook",   "icx_submitexthtlc",      &icxsubmitexthtlc,      {"exthtlc"}},
    {"icxorderbook",   "icx_claimdfchtlc",       &icxclaimdfchtlc,       {"claim"}},
    {"icxorderbook",   "icx_closeorder",         &icxcloseorder,         {"orderTx"}},
    {"icxorderbook",   "icx_getorder",           &icxgetorder,           {"orderTx"}},
    {"icxorderbook",   "icx_listorders",         &icxlistorders,         {"by"}},
    {"icxorderbook",   "icx_listhtlcs",          &icxlisthtlcs,          {"by"}},

};

void RegisterICXOrderbookRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
