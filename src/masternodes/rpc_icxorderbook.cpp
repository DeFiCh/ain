#include <masternodes/mn_rpc.h>

UniValue icxOrderToJSON(CICXOrderImplemetation const& order, uint8_t const status, int currentHeight) {
    UniValue orderObj(UniValue::VOBJ);

    auto token = pcustomcsview->GetToken(order.idToken);
    if (!token)
        return (UniValue::VNULL);

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
        orderObj.pushKV("chainTo", CICXOrder::CHAIN_BTC);
        orderObj.pushKV("receivePubkey", HexStr(order.receivePubkey));
    }
    else if (order.orderType == CICXOrder::TYPE_EXTERNAL)
    {
        orderObj.pushKV("type", "EXTERNAL");
        orderObj.pushKV("chainFrom",CICXOrder::CHAIN_BTC);
        orderObj.pushKV("tokenTo", token->CreateSymbolKey(order.idToken));
    }
    orderObj.pushKV("ownerAddress", ScriptToString(order.ownerAddress));
    orderObj.pushKV("amountFrom", ValueFromAmount(order.amountFrom));
    orderObj.pushKV("amountToFill", ValueFromAmount(order.amountToFill));
    orderObj.pushKV("orderPrice", ValueFromAmount(order.orderPrice));
    auto calcedAmount = MultiplyAmounts(order.amountToFill, order.orderPrice);
    orderObj.pushKV("amountToFillInToAsset", ValueFromAmount(calcedAmount));
    orderObj.pushKV("height", static_cast<int>(order.creationHeight));
    orderObj.pushKV("expireHeight", static_cast<int>(order.creationHeight + order.expiry));
    if (order.closeHeight > -1)
    {
        orderObj.pushKV("closeHeight", static_cast<int>(order.closeHeight));
        if (!order.closeTx.IsNull()) orderObj.pushKV("closeTx", order.closeTx.GetHex());
    }
    else if (order.creationHeight + order.expiry <= currentHeight)
    {
        orderObj.pushKV("expired", true);
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(order.creationTx.GetHex(), orderObj);
    return (ret);
}

UniValue icxMakeOfferToJSON(CICXMakeOfferImplemetation const& makeoffer, uint8_t const status) {
    UniValue orderObj(UniValue::VOBJ);

    auto order = pcustomcsview->GetICXOrderByCreationTx(makeoffer.orderTx);
    if (!order)
        return (UniValue::VNULL);
    orderObj.pushKV("orderTx", makeoffer.orderTx.GetHex());
    orderObj.pushKV("status", status == CICXMakeOffer::STATUS_OPEN ? "OPEN" : status == CICXMakeOffer::STATUS_CLOSED ? "CLOSED" : "EXPIRED");
    orderObj.pushKV("amount", ValueFromAmount(makeoffer.amount));
    auto calcedAmount = DivideAmounts(makeoffer.amount, order->orderPrice);
    orderObj.pushKV("amountInFromAsset", ValueFromAmount(calcedAmount));
    orderObj.pushKV("ownerAddress",ScriptToString(makeoffer.ownerAddress));
    if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        orderObj.pushKV("receivePubkey", HexStr(makeoffer.receivePubkey));
    orderObj.pushKV("takerFee", ValueFromAmount(makeoffer.takerFee));
    orderObj.pushKV("expireHeight", static_cast<int>(makeoffer.creationHeight + makeoffer.expiry));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(makeoffer.creationTx.GetHex(), orderObj);
    return (ret);
}

UniValue icxSubmitDFCHTLCToJSON(CICXSubmitDFCHTLCImplemetation const& dfchtlc, uint8_t const status) {
    auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(dfchtlc.offerTx);
    if (!offer)
        return (UniValue::VNULL);
    auto order = pcustomcsview->GetICXOrderByCreationTx(offer->orderTx);
    if (!order)
        return (UniValue::VNULL);

    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("type", "DFC");
    switch (status)
    {
        case 0: orderObj.pushKV("status", "OPEN");
                break;
        case 1: orderObj.pushKV("status", "CLAIMED");
                break;
        case 2: orderObj.pushKV("status", "REFUNDED");
                break;
        case 3: orderObj.pushKV("status", "EXPIRED");
                break;
    }
    orderObj.pushKV("offerTx", dfchtlc.offerTx.GetHex());
    orderObj.pushKV("amount", ValueFromAmount(dfchtlc.amount));
    if (order->orderType == CICXOrder::TYPE_INTERNAL)
    {
        auto calcedAmount = MultiplyAmounts(dfchtlc.amount, order->orderPrice);
        orderObj.pushKV("amountInEXTAsset", ValueFromAmount(calcedAmount));
    }
    else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
    {
        auto calcedAmount = DivideAmounts(dfchtlc.amount, order->orderPrice);
        orderObj.pushKV("amountInEXTAsset", ValueFromAmount(calcedAmount));
    }
    orderObj.pushKV("hash", dfchtlc.hash.GetHex());
    orderObj.pushKV("timeout", static_cast<int>(dfchtlc.timeout));
    orderObj.pushKV("height", static_cast<int>(dfchtlc.creationHeight));
    orderObj.pushKV("refundHeight", static_cast<int>(dfchtlc.creationHeight + dfchtlc.timeout));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(dfchtlc.creationTx.GetHex(), orderObj);
    return (ret);
}

UniValue icxSubmitEXTHTLCToJSON(CICXSubmitEXTHTLCImplemetation const& exthtlc, uint8_t const status) {
    auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(exthtlc.offerTx);
    if (!offer)
        return (UniValue::VNULL);
    auto order = pcustomcsview->GetICXOrderByCreationTx(offer->orderTx);
    if (!order)
        return (UniValue::VNULL);

    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("type", "EXTERNAL");
    orderObj.pushKV("status", status == CICXSubmitEXTHTLC::STATUS_OPEN ? "OPEN" : status == CICXSubmitEXTHTLC::STATUS_CLOSED ? "CLOSED" : "EXPIRED");
    orderObj.pushKV("offerTx", exthtlc.offerTx.GetHex());
    orderObj.pushKV("amount", ValueFromAmount(exthtlc.amount));
    if (order->orderType == CICXOrder::TYPE_INTERNAL)
    {
        auto calcedAmount = DivideAmounts(exthtlc.amount, order->orderPrice);
        orderObj.pushKV("amountInDFCAsset", ValueFromAmount(calcedAmount));
    }
    else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
    {
        auto calcedAmount = MultiplyAmounts(exthtlc.amount, order->orderPrice);
        orderObj.pushKV("amountInDFCAsset", ValueFromAmount(calcedAmount));
    }
    orderObj.pushKV("hash", exthtlc.hash.GetHex());
    orderObj.pushKV("htlcScriptAddress", exthtlc.htlcscriptAddress);
    orderObj.pushKV("ownerPubkey", HexStr(exthtlc.ownerPubkey));
    orderObj.pushKV("timeout", static_cast<int>(exthtlc.timeout));
    orderObj.pushKV("height", static_cast<int>(exthtlc.creationHeight));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(exthtlc.creationTx.GetHex(), orderObj);
    return (ret);
}

UniValue icxClaimDFCHTLCToJSON(CICXClaimDFCHTLCImplemetation const& claimdfchtlc) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("type", "CLAIM DFC");
    orderObj.pushKV("dfchtlcTx", claimdfchtlc.dfchtlcTx.GetHex());
    orderObj.pushKV("seed", HexStr(claimdfchtlc.seed));
    orderObj.pushKV("height", static_cast<int>(claimdfchtlc.creationHeight));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(claimdfchtlc.creationTx.GetHex(), orderObj);
    return (ret);
}

UniValue icxcreateorder(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"icx_createorder",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nCreates (and submits to local node and network) a order creation transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"order", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"tokenFrom|chainFrom", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol or id of selling token/chain"},
                            {"chainTo|tokenTo", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol or id of buying chain/token"},
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address of DFI token for fees and selling tokens in case of EXT/DFC order type"},
                            {"receivePubkey", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Pubkey which can claim external HTLC in case of DFC/EXT order type"},
                            {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO, "\"tokenFrom\" coins amount"},
                            {"orderPrice", RPCArg::Type::NUM, RPCArg::Optional::NO, "Price per unit"},
                            {"expiry", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Number of blocks until the order expires (Default: "
                                + std::to_string(CICXOrder::DEFAULT_EXPIRY) + " DFI blocks)"},
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
                                                        "\"expiry\":\"1000\"}'")
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create order while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"tokenFrom|chainFrom\",\"chainTo|tokenTo\",\"ownerAddress\",\"amountFrom\",\"orderPrice\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXOrder order;
    std::string tokenFromSymbol, tokenToSymbol;

    if (!metaObj["ownerAddress"].isNull())
        order.ownerAddress = DecodeScript(metaObj["ownerAddress"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           R"(Invalid parameters, argument "ownerAddress" must be specified)");

    if (!::IsMine(*pwallet, order.ownerAddress))
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           strprintf("Address (%s) is not owned by the wallet", metaObj["ownerAddress"].getValStr()));

    if (metaObj["tokenFrom"].isNull() && metaObj["chainFrom"].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, either \"tokenFrom\" or \"chainFrom\" must not be both null - [tokenFrom,chainTo] or [chainFrom,tokenTo].");

    if (!metaObj["tokenFrom"].isNull()) {
        tokenFromSymbol = trim_ws(metaObj["tokenFrom"].getValStr());

        if (metaObj["chainTo"].isNull() || trim_ws(metaObj["chainTo"].getValStr()) != "BTC")
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"chainTo\" must be \"BTC\" if \"tokenFrom\" specified");
    }
    else if (!metaObj["chainFrom"].isNull()) {
        if (trim_ws(metaObj["chainFrom"].getValStr()) != "BTC")
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"chainFrom\" must be \"BTC\" if \"tokenTo\" specified");

        if (!metaObj["tokenTo"].isNull())
            tokenToSymbol = trim_ws(metaObj["tokenTo"].getValStr());
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"tokenTo\" must not be null if \"chainFrom\" specified");
    }
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"tokenFrom\" or \"chainFrom\" must be non-null");

    if (!metaObj["amountFrom"].isNull())
        order.amountToFill = order.amountFrom = AmountFromValue(metaObj["amountFrom"]);
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amountFrom\" must not be null");

    if (!metaObj["orderPrice"].isNull())
        order.orderPrice = AmountFromValue(metaObj["orderPrice"]);
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"orderPrice\" must not be null");

    if (!metaObj["expiry"].isNull())
        order.expiry = metaObj["expiry"].get_int();

    if (!tokenFromSymbol.empty() && !tokenToSymbol.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, \"tokenFrom\" and \"tokenTo\" cannot be set in the same time. [tokenFrom,chainTo] or [chainFrom,tokenTo]");

    if (!tokenFromSymbol.empty())
        order.orderType = CICXOrder::TYPE_INTERNAL;
    else
        order.orderType = CICXOrder::TYPE_EXTERNAL;

    int targetHeight;
    {
        DCT_ID idToken;
        CCustomCSView view(*pcustomcsview);

        if (order.orderType == CICXOrder::TYPE_INTERNAL)
        {
            auto token = view.GetTokenGuessId(tokenFromSymbol, idToken);
            if (!token)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenFromSymbol));
            order.idToken = idToken;

            if (!metaObj["receivePubkey"].isNull())
                order.receivePubkey = PublickeyFromString(trim_ws(metaObj["receivePubkey"].getValStr()));
            else
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, argument \"receivePubkey\" must not be null");

            CTokenAmount balance = view.GetBalance(order.ownerAddress, idToken);
            if (balance.nValue < order.amountFrom)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s on address %s!", token->CreateSymbolKey(order.idToken), ScriptToString(order.ownerAddress)));
        }
        else
        {
            auto token = view.GetTokenGuessId(tokenToSymbol, idToken);
            if (!token)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenToSymbol));
            order.idToken = idToken;
        }

        targetHeight = view.GetLastHeight() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXCreateOrder)
             << order;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{order.ownerAddress};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest))
        coinControl.destChange = dest;

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");
    ret.pushKV("txid", signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex());
    return ret;
}

UniValue icxmakeoffer(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"icx_makeoffer",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nCreates (and submits to local node and network) a makeoffer transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"offer", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "Txid of order tx for which is the offer"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount fulfilling the order"},
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address of DFI token and for receiving tokens in case of DFC/EXT order"},
                            {"receivePubkey", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Pubkey which can claim external HTLC in case of EXT/DFC order type"},
                            {"expiry", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Number of blocks until the offer expires (Default: "
                                + std::to_string(CICXMakeOffer::DEFAULT_EXPIRY) + " DFI blocks)"},
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
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot make offer while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

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
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, argument \"orderTx\" must be non-null");

    if (!metaObj["amount"].isNull())
        makeoffer.amount = AmountFromValue(metaObj["amount"]);
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, argument \"amount\" must be non-null");

    if (!metaObj["ownerAddress"].isNull())
        makeoffer.ownerAddress = DecodeScript(metaObj["ownerAddress"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, argument \"ownerAddress\" must be specified");

    if (!::IsMine(*pwallet, makeoffer.ownerAddress))
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           strprintf("Address (%s) is not owned by the wallet", metaObj["ownerAddress"].getValStr()));

    if (!metaObj["expiry"].isNull())
        makeoffer.expiry = metaObj["expiry"].get_int();

    int targetHeight;
    {
        CCustomCSView view(*pcustomcsview);

        auto order = view.GetICXOrderByCreationTx(makeoffer.orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("orderTx (%s) does not exist",makeoffer.orderTx.GetHex()));

        if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        {
            if (!metaObj["receivePubkey"].isNull())
                makeoffer.receivePubkey = PublickeyFromString(trim_ws(metaObj["receivePubkey"].getValStr()));
            else
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, argument \"receivePubkey\" must be non-null");

            CTokenAmount balance = view.GetBalance(makeoffer.ownerAddress,order->idToken);
            if (balance.nValue < makeoffer.amount)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s on address %s!",
                        view.GetToken(order->idToken)->CreateSymbolKey(order->idToken), ScriptToString(makeoffer.ownerAddress)));
        }

        targetHeight = view.GetLastHeight() + 1;

        if (targetHeight < Params().GetConsensus().EunosPayaHeight)
            makeoffer.expiry = CICXMakeOffer::DEFAULT_EXPIRY;
        else
            makeoffer.expiry = CICXMakeOffer::EUNOSPAYA_DEFAULT_EXPIRY;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXMakeOffer)
             << makeoffer;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{makeoffer.ownerAddress};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest))
        coinControl.destChange = dest;

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");
    ret.pushKV("txid", signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex());
    return ret;
}

UniValue icxsubmitdfchtlc(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"icx_submitdfchtlc",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nCreates (and submits to local node and network) a dfc htlc transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"htlc", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"offerTx", RPCArg::Type::STR, RPCArg::Optional::NO, "Txid of offer tx for which the htlc is"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount in htlc"},
                            {"hash", RPCArg::Type::STR, RPCArg::Optional::NO, "Hash of seed used for the hash lock part"},
                            {"timeout", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Timeout (absolute in blocks) for expiration of htlc in DFI blocks"},
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
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot submit dfc htlc while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

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

    if (!metaObj["hash"].isNull())
        submitdfchtlc.hash = uint256S(metaObj["hash"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"hash\" must be non-null");

    if (!metaObj["timeout"].isNull())
        submitdfchtlc.timeout = metaObj["timeout"].get_int();

    int targetHeight;
    CScript authScript;
    {
        CCustomCSView view(*pcustomcsview);

        auto offer = view.GetICXMakeOfferByCreationTx(submitdfchtlc.offerTx);
        if (!offer)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("offerTx (%s) does not exist",submitdfchtlc.offerTx.GetHex()));

        auto order = view.GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("orderTx (%s) does not exist",offer->orderTx.GetHex()));

        targetHeight = view.GetLastHeight() + 1;

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
        {
            authScript = order->ownerAddress;

            if (!submitdfchtlc.timeout)
                submitdfchtlc.timeout = (targetHeight < Params().GetConsensus().EunosPayaHeight) ? CICXSubmitDFCHTLC::MINIMUM_TIMEOUT : CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;
        }
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        {
            authScript = offer->ownerAddress;

            if (!submitdfchtlc.timeout)
            submitdfchtlc.timeout = (targetHeight < Params().GetConsensus().EunosPayaHeight) ? CICXSubmitDFCHTLC::MINIMUM_2ND_TIMEOUT : CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;

            CTokenAmount balance = view.GetBalance(offer->ownerAddress,order->idToken);
            if (balance.nValue < offer->amount)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s on address %s!",
                        view.GetToken(order->idToken)->CreateSymbolKey(order->idToken), ScriptToString(offer->ownerAddress)));
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXSubmitDFCHTLC)
             << submitdfchtlc;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{authScript};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest))
        coinControl.destChange = dest;

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");
    ret.pushKV("txid", signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex());
    return ret;
}

UniValue icxsubmitexthtlc(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"icx_submitexthtlc",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nCreates (and submits to local node and network) ext htlc transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"htlc", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"offerTx", RPCArg::Type::STR, RPCArg::Optional::NO, "Txid of offer tx for which the htlc is"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount in htlc"},
                            {"htlcScriptAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Script address of external htlc"},
                            {"hash", RPCArg::Type::STR, RPCArg::Optional::NO, "Hash of seed used for the hash lock part"},
                            {"ownerPubkey", RPCArg::Type::STR, RPCArg::Optional::NO, "Pubkey of the owner to which the funds are refunded if HTLC timeouts"},
                            {"timeout", RPCArg::Type::NUM, RPCArg::Optional::NO, "Timeout (absolute in block) for expiration of external htlc in external chain blocks"},
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
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot submit ext htlc while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

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
    CScript authScript;
    {
        CCustomCSView view(*pcustomcsview);

        auto offer = view.GetICXMakeOfferByCreationTx(submitexthtlc.offerTx);
        if (!offer)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("offerTx (%s) does not exist",submitexthtlc.offerTx.GetHex()));\

        auto order = view.GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("orderTx (%s) does not exist",offer->orderTx.GetHex()));

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
        {
            authScript = offer->ownerAddress;
        }
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        {
            authScript = order->ownerAddress;
        }

        targetHeight = view.GetLastHeight() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXSubmitEXTHTLC)
             << submitexthtlc;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{authScript};
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
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");
    ret.pushKV("txid", signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex());
    return ret;
}

UniValue icxclaimdfchtlc(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"icx_claimdfchtlc",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nCreates (and submits to local node and network) a dfc htlc transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"htlc", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"dfchtlcTx", RPCArg::Type::STR, RPCArg::Optional::NO, "Txid of dfc htlc tx for which the claim is"},
                            {"seed", RPCArg::Type::STR, RPCArg::Optional::NO, "Secret seed for claiming htlc"},
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
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot claim dfc htlc while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

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

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

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
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest))
        coinControl.destChange = dest;

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");
    ret.pushKV("txid", signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex());
    return ret;
}

UniValue icxcloseorder(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"icx_closeorder",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nCloses (and submits to local node and network) order transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "Txid of order"},
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
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot close order while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"orderTx\"}");

    UniValue const & txInputs = request.params[1];

    CICXCloseOrder closeorder;
    closeorder.orderTx = uint256S(request.params[0].getValStr());

    int targetHeight;
    CScript authScript;
    {
        CCustomCSView view(*pcustomcsview);

        auto order = view.GetICXOrderByCreationTx(closeorder.orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + closeorder.orderTx.GetHex() + ") does not exist");

        authScript = order->ownerAddress;

        if (!order->closeTx.IsNull())
            throw JSONRPCError(RPC_INVALID_PARAMETER,"orderTx (" + closeorder.orderTx.GetHex() + " is already closed!");

        targetHeight = view.GetLastHeight() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXCloseOrder)
             << closeorder;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{authScript};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest))
        coinControl.destChange = dest;

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");
    ret.pushKV("txid", signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex());
    return ret;
}

UniValue icxcloseoffer(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"icx_closeoffer",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nCloses (and submits to local node and network) offer transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"offerTx", RPCArg::Type::STR, RPCArg::Optional::NO, "Txid of makeoffer"},
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
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot close offer while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"offerTx\"}");

    UniValue const & txInputs = request.params[1];

    CICXCloseOffer closeoffer;
    closeoffer.offerTx = uint256S(request.params[0].getValStr());

    int targetHeight;
    CScript authScript;
    {
        CCustomCSView view(*pcustomcsview);

        auto offer = view.GetICXMakeOfferByCreationTx(closeoffer.offerTx);
        if (!offer)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "OfferTx (" + closeoffer.offerTx.GetHex() + ") does not exist");

        authScript = offer->ownerAddress;

        if (!offer->closeTx.IsNull())
            throw JSONRPCError(RPC_INVALID_PARAMETER,"OfferTx (" + closeoffer.offerTx.GetHex() + " is already closed!");

        targetHeight = view.GetLastHeight() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXCloseOffer)
             << closeoffer;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{authScript};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Return change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest))
        coinControl.destChange = dest;

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");
    ret.pushKV("txid", signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex());
    return ret;
}

UniValue icxgetorder(const JSONRPCRequest& request) {
    RPCHelpMan{"icx_getorder",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nReturn information about order or fillorder.\n",
                {
                    {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "Txid of createorder or fulfillorder tx"},
                },
                RPCResult
                {
                    "{...}     (object) Json object with order information\n"
                },
                RPCExamples{
                    HelpExampleCli("icx_getorder", "'{\"orderTx\":\"<txid>>\"}'")
                    + "\EXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                },
     }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"orderTx\"}");

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("EXPERIMENTAL warning:", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");

    CCustomCSView view(*pcustomcsview);

    auto currentHeight = view.GetLastHeight();
    uint256 orderTxid = uint256S(request.params[0].getValStr());
    auto order = view.GetICXOrderByCreationTx(orderTxid);
    if (order)
    {
        auto status = view.GetICXOrderStatus({order->idToken,order->creationTx});
        ret.pushKVs(icxOrderToJSON(*order, status, currentHeight));
        return ret;
    }

    auto fillorder = view.GetICXMakeOfferByCreationTx(orderTxid);
    if (fillorder)
    {
        auto status = view.GetICXMakeOfferStatus({fillorder->orderTx,fillorder->creationTx});
        ret.pushKVs(icxMakeOfferToJSON(*fillorder, status));
        return ret;
    }

    throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + orderTxid.GetHex() + ") does not exist");
}

UniValue icxlistorders(const JSONRPCRequest& request) {
    RPCHelpMan{"icx_listorders",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nReturn information about orders.\n",
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
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"
                }
     }.Check(request);

    size_t limit = 50;
    std::string tokenSymbol, chain;
    uint256 orderTxid;
    bool closed = false, offers = false;

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params.size() > 0)
    {
        UniValue byObj = request.params[0].get_obj();
        if (!byObj["token"].isNull()) tokenSymbol = trim_ws(byObj["token"].getValStr());
        if (!byObj["chain"].isNull()) chain = trim_ws(byObj["chain"].getValStr());
        if (!byObj["orderTx"].isNull())
        {
            orderTxid = uint256S(byObj["orderTx"].getValStr());
            offers = true;
        }
        if (!byObj["limit"].isNull()) limit = (size_t) byObj["limit"].get_int64();
        if (!byObj["closed"].isNull()) closed = byObj["closed"].get_bool();
    }

    CCustomCSView view(*pcustomcsview);

    auto currentHeight = view.GetLastHeight();
    DCT_ID idToken = {std::numeric_limits<uint32_t>::max()};
    if (!tokenSymbol.empty() && !chain.empty())
    {
        auto token1 = view.GetTokenGuessId(tokenSymbol, idToken);
        if (!token1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");

    if (idToken.v != std::numeric_limits<uint32_t>::max())
    {
        DCT_ID prefix;
        prefix = idToken;

        auto orderkeylambda = [&](CICXOrderView::OrderKey const & key, uint8_t status) {
            if (key.first != prefix || !limit)
                return (false);
            auto order = view.GetICXOrderByCreationTx(key.second);
            if (order)
            {
                ret.pushKVs(icxOrderToJSON(*order, status, currentHeight));
                limit--;
            }
            return true;
        };

        if (closed)
            view.ForEachICXOrderClose(orderkeylambda, prefix);
        else
            view.ForEachICXOrderOpen(orderkeylambda, prefix);

        return ret;
    }
    else if (offers)
    {
        auto offerkeylambda = [&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
            if (key.first != orderTxid || !limit)
                return (false);
            auto offer = view.GetICXMakeOfferByCreationTx(key.second);
            if (offer)
            {
                ret.pushKVs(icxMakeOfferToJSON(*offer, status));
                limit--;
            }
            return true;
        };
        if (closed)
            view.ForEachICXMakeOfferClose(offerkeylambda, orderTxid);
        else
            view.ForEachICXMakeOfferOpen(offerkeylambda, orderTxid);

        return ret;
    }

    auto orderlambda = [&](CICXOrderView::OrderKey const & key, uint8_t status) {
        if (!limit)
            return false;
        auto order = view.GetICXOrderByCreationTx(key.second);
        if (order)
        {
            ret.pushKVs(icxOrderToJSON(*order, status, currentHeight));
            limit--;
        }
        return true;
    };

    if (closed)
        view.ForEachICXOrderClose(orderlambda);
    else
        view.ForEachICXOrderOpen(orderlambda);

    return ret;
}

UniValue icxlisthtlcs(const JSONRPCRequest& request) {
    RPCHelpMan{"icx_listhtlcs",
                "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n\nReturn information about orders.\n",
                {
                        {"by", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                            {
                                {"offerTx",RPCArg::Type::STR, RPCArg::Optional::NO, "Offer txid  for which to list all HTLCS"},
                                {"limit",  RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Maximum number of orders to return (default: 20)"},
                                {"closed",  RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Display also claimed, expired and refunded HTLCs (default: false)"},

                            },
                        },
                },
                RPCResult
                {
                        "{{...},...}     (array) Json object with orders information\n"
                },
                RPCExamples{
                        HelpExampleCli("icx_listorders", "'{\"offerTx\":\"<txid>\"}'")
                        + "\nEXPERIMENTAL warning: ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.\n"

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
    ret.pushKV("WARNING", "ICX and Atomic Swap are experimental features. You might end up losing your funds. USE IT AT YOUR OWN RISK.");

    CCustomCSView view(*pcustomcsview);

    auto dfchtlclambda = [&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
        if (key.first != offerTxid || !limit)
            return false;
        auto dfchtlc = view.GetICXSubmitDFCHTLCByCreationTx(key.second);
        if (dfchtlc)
        {
            ret.pushKVs(icxSubmitDFCHTLCToJSON(*dfchtlc,status));
            limit--;
        }
        return true;
    };
    auto exthtlclambda = [&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
        if (key.first != offerTxid || !limit)
            return false;
        auto exthtlc = view.GetICXSubmitEXTHTLCByCreationTx(key.second);
        if (exthtlc)
        {
            ret.pushKVs(icxSubmitEXTHTLCToJSON(*exthtlc, status));
            limit--;
        }
        return true;
    };

    view.ForEachICXClaimDFCHTLC([&](CICXOrderView::TxidPairKey const & key, uint8_t status) {
        if (key.first != offerTxid || !limit)
            return false;
        auto claimdfchtlc = view.GetICXClaimDFCHTLCByCreationTx(key.second);
        if (claimdfchtlc)
        {
            ret.pushKVs(icxClaimDFCHTLCToJSON(*claimdfchtlc));
            limit--;
        }
        return true;
    }, offerTxid);

    if (closed)
        view.ForEachICXSubmitDFCHTLCClose(dfchtlclambda, offerTxid);
    view.ForEachICXSubmitDFCHTLCOpen(dfchtlclambda, offerTxid);

    if (closed)
        view.ForEachICXSubmitEXTHTLCClose(exthtlclambda, offerTxid);
    view.ForEachICXSubmitEXTHTLCOpen(exthtlclambda, offerTxid);

    return ret;
}

static const CRPCCommand commands[] =
{
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"icxorderbook",   "icx_createorder",        &icxcreateorder,        {"order", "inputs"}},
    {"icxorderbook",   "icx_makeoffer",          &icxmakeoffer,          {"offer", "inputs"}},
    {"icxorderbook",   "icx_closeoffer",         &icxcloseoffer,         {"offerTx", "inputs"}},
    {"icxorderbook",   "icx_submitdfchtlc",      &icxsubmitdfchtlc,      {"dfchtlc", "inputs"}},
    {"icxorderbook",   "icx_submitexthtlc",      &icxsubmitexthtlc,      {"exthtlc", "inputs"}},
    {"icxorderbook",   "icx_claimdfchtlc",       &icxclaimdfchtlc,       {"claim", "inputs"}},
    {"icxorderbook",   "icx_closeorder",         &icxcloseorder,         {"orderTx", "inputs"}},
    {"icxorderbook",   "icx_getorder",           &icxgetorder,           {"orderTx"}},
    {"icxorderbook",   "icx_listorders",         &icxlistorders,         {"by"}},
    {"icxorderbook",   "icx_listhtlcs",          &icxlisthtlcs,          {"by"}},

};

void RegisterICXOrderbookRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
