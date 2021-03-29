#include <masternodes/mn_rpc.h>

UniValue icxOrderToJSON(CICXOrderImplemetation const& order) {
    UniValue orderObj(UniValue::VOBJ);
    UniValue ret(UniValue::VOBJ);

    auto tokenFrom = pcustomcsview->GetToken(order.idTokenFrom);
    if (!tokenFrom)
        return (ret);
    auto tokenTo = pcustomcsview->GetToken(order.idTokenTo);
    if (!tokenTo)
        return (ret);
    if (order.orderType)
    {
        orderObj.pushKV("tokenFrom", tokenFrom->CreateSymbolKey(order.idTokenFrom));
        orderObj.pushKV("chainTo",order.chainTo);
    }
    else
    {
        orderObj.pushKV("chainFrom",order.chainFrom);
        orderObj.pushKV("tokenTo", tokenTo->CreateSymbolKey(order.idTokenTo));
    }
    orderObj.pushKV("amountFrom", ValueFromAmount(order.amountFrom));
    orderObj.pushKV("amountToFill", ValueFromAmount(order.amountToFill));
    orderObj.pushKV("orderPrice", ValueFromAmount(order.orderPrice));
    orderObj.pushKV("height", static_cast<int>(order.creationHeight));
    orderObj.pushKV("expiry", static_cast<int>(order.expiry));
    if (order.closeHeight > -1) orderObj.pushKV("closeHeight", static_cast<int>(order.closeHeight));
    if (!order.closeTx.IsNull()) orderObj.pushKV("closeTx", order.closeTx.GetHex());

    ret.pushKV(order.creationTx.GetHex(), orderObj);
    return (ret);
}

UniValue icxMakeOfferToJSON(CICXMakeOfferImplemetation const& makeoffer) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("orderTx", makeoffer.orderTx.GetHex());
    orderObj.pushKV("amount", ValueFromAmount(makeoffer.amount));
    if (makeoffer.offerType) orderObj.pushKV("ownerAddress", makeoffer.ownerAddress);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(makeoffer.creationTx.GetHex(), orderObj);
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
                            {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol or id of selling token"},
                            {"chainFrom", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol of chain for selling asset"},
                            {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol or id of buying token"},
                            {"chainTo", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol of chain for buying asset"},
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address of tokens when using tokenFrom"},
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
                        HelpExampleCli("-cx_createorder", "'{\"ownerAddress\":\"tokenAddress\","
                                                        "\"tokenFrom\":\"MyToken1\",\"chainTo\":\"BTC\","
                                                        "\"amountFrom\":\"10\",\"orderPrice\":\"0.02\"}'")
                        + HelpExampleCli("icx_createorder", "'{\"chainFrom\":\"BTC\",\"tokenTo\":\"MyToken2\","
                                                        "\"amountFrom\":\"5\",\"orderPrice\":\"10000\","
                                                        "\"expiry\":\"120\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create order while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"ownerAddress\",\"tokenFrom\",\"tokenTo\",\"amountFrom\",\"orderPrice\"}");
    }
    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXOrder order;
    std::string tokenFromSymbol, tokenToSymbol;

    if (!metaObj["tokenFrom"].isNull()) {
        tokenFromSymbol = trim_ws(metaObj["tokenFrom"].getValStr());

        if (!metaObj["chainTo"].isNull()) order.chainTo = trim_ws(metaObj["chainTo"].getValStr());
        else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"chainTo\" must be non-null if \"tokenFrom\" specified");

        if (!metaObj["ownerAddress"].isNull()) order.ownerAddress = trim_ws(metaObj["ownerAddress"].getValStr());
        else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"ownerAddress\" must be non-null if \"tokenFrom\" specified");
    
    }
    else if (!metaObj["chainFrom"].isNull()) {
        order.chainFrom = trim_ws(metaObj["chainFrom"].getValStr());

        if (!metaObj["tokenTo"].isNull()) tokenToSymbol = trim_ws(metaObj["tokenTo"].getValStr());
        else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"tokenTo\" must be non-null if \"chainFrom\" specified");
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"tokenFrom\" or \"chainFrom\" must be non-null");

    if (!metaObj["amountFrom"].isNull()) order.amountFrom = AmountFromValue(metaObj["amountFrom"]);
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amountFrom\" must be non-null");
    
    if (!metaObj["orderPrice"].isNull()) {
        order.orderPrice = AmountFromValue(metaObj["orderPrice"]);
        order.amountToFill = order.amountFrom*COIN/order.orderPrice;
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"orderPrice\" must be non-null");
    
    if (!metaObj["expiry"].isNull()) order.expiry = metaObj["expiry"].get_int();

    if (tokenFromSymbol.empty() && order.chainFrom.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, either \"tokenFrom\" or \"chainFrom\" must be non-null. [tokenFrom,chainTo] or [chainFrom,tokenTo]");
    if (!tokenFromSymbol.empty() && !order.chainFrom.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, \"tokenFrom\" and \"chainFrom\" cannot be set in the same time. [tokenFrom,chainTo] or [chainFrom,tokenTo]");
    if (!tokenToSymbol.empty() && !order.chainTo.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, \"tokenTo\" and \"chainTo\" cannot be set in the same time. [tokenFrom,chainTo] or [chainFrom,tokenTo]");
    
    if (!tokenFromSymbol.empty()) order.orderType=1;
    else order.orderType=0;

    int targetHeight;
    {
        LOCK(cs_main);
        DCT_ID idTokenFrom, idTokenTo;
        std::unique_ptr<CToken> tokenFrom, tokenTo;

        if (!order.orderType)
        {
            tokenFrom = pcustomcsview->GetTokenGuessId(tokenFromSymbol, idTokenFrom);
            if (!tokenFrom) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenFromSymbol));
            }
            order.idTokenFrom = idTokenFrom;

            CBalances totalBalances;
            CAmount total = 0;
            pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
                if (IsMineCached(*pwallet, owner) == ISMINE_SPENDABLE) {
                    totalBalances.Add(balance);
                }
                return true;
            });
            auto it = totalBalances.balances.begin();
            for (int i = 0; it != totalBalances.balances.end(); it++, i++) {
                CTokenAmount bal = CTokenAmount{(*it).first, (*it).second};
                if (bal.nTokenId == order.idTokenFrom) total += bal.nValue;
            }
            if (total < order.amountFrom)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s for order amount %s!", tokenFrom->CreateSymbolKey(order.idTokenFrom), ValueFromAmount(order.amountFrom).getValStr()));
        }
        else
        {
            tokenTo = pcustomcsview->GetTokenGuessId(tokenToSymbol, idTokenTo);
            if (!tokenTo) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenToSymbol));
            }
            order.idTokenTo = idTokenTo;
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
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyICXCreateOrderTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, order}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue icxmakeoffer(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"icx_makeoffer",
                "\nCreates (and submits to local node and network) a fill order transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"order", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of order tx for which is the offer"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "amount fulfilling the order"},
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
                        HelpExampleCli("icx_makeoffer", "'{\"ownerAddress\":\"tokenAddress\","
                                                        "\"orderTx\":\"txid\",\"amount\":\"10\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create order while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"ownerAddress\",\"orderTx\",\"amount\"}");
    }
    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXMakeOffer makeoffer;

    if (!metaObj["orderTx"].isNull()) {
        makeoffer.orderTx = uint256S(metaObj["orderTx"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"orderTx\" must be non-null");
    if (!metaObj["amount"].isNull()) {
        makeoffer.amount = AmountFromValue(metaObj["amount"]);
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amount\" must be non-null");

    int targetHeight;
    {
        LOCK(cs_main);
        auto order = pcustomcsview->GetICXOrderByCreationTx(makeoffer.orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + makeoffer.orderTx.GetHex() + ") does not exist");

        if (order->amountToFill<makeoffer.amount)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "cannot make offer with that amount, order (" + order->creationTx.GetHex() + ") has less amount to fill!");

        targetHeight = ::ChainActive().Height() + 1;
    }


    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ICXCreateOrder)
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
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyICXMakeOfferTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, makeoffer}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
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
                        HelpExampleCli("closeorder", "'{\"orderTx\":\"acb4d7eef089e74708afc6d9ca40af34f27a70506094dac39a5b9fb0347614fb\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot close order while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"orderTx\"}");
    }
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
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx,&coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyICXCloseOrderTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, closeorder}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
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
                    HelpExampleCli("icx_getorder", "'{\"orderTx\":\"acb4d7eef089e74708afc6d9ca40af34f27a70506094dac39a5b9fb0347614fb\"}'")     
                },
     }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"orderTx\"}");
    }
    uint256 orderTxid= uint256S(request.params[0].getValStr());
    auto order = pcustomcsview->GetICXOrderByCreationTx(orderTxid);
    if (order)
    {
        return icxOrderToJSON(*order);
    }
    auto fillorder = pcustomcsview->GetICXMakeOfferByCreationTx(orderTxid);
    if (fillorder)
    {
        return icxMakeOfferToJSON(*fillorder);
    }
    throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + orderTxid.GetHex() + ") does not exist");    
}

UniValue icxlistorders(const JSONRPCRequest& request) {
    RPCHelpMan{"icx_listorders",
                "\nReturn information about orders.\n",
                {
                        {"by", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                {"limit",  RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Maximum number of orders to return (default: 50)"},
                                {"from", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token or chain symbol"},
                                {"to", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token or chain symbol"},
                                {"orderTx",RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Order txid to list all offers for this order"},
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
                        + HelpExampleCli("icx_listorders", "'{\"token\":\"MyToken1\",\"tokenPair\":\"Mytoken2\"}'")
                        + HelpExampleCli("icx_listorders", "'{\"token\":\"MyToken1\",\"tokenPair\":\"Mytoken2\",\"closed\":true}'")
                        + HelpExampleCli("icx_listorders", "'{\"orderTx\":\"acb4d7eef089e74708afc6d9ca40af34f27a70506094dac39a5b9fb0347614fb\"}'")
                }
     }.Check(request);

    size_t limit = 50;
    std::string fromSymbol, toSymbol, chainFrom, chainTo;
    uint256 orderTxid;
    bool closed = false;
    if (request.params.size() > 0)
    {
        UniValue byObj = request.params[0].get_obj();
        if (!byObj["from"].isNull()) fromSymbol = trim_ws(byObj["from"].getValStr());
        if (!byObj["to"].isNull()) toSymbol = trim_ws(byObj["to"].getValStr());
        if (!byObj["limit"].isNull()) limit = (size_t) byObj["limit"].get_int64();
        if (!byObj["orderTx"].isNull()) orderTxid = uint256S(byObj["orderTx"].getValStr());
        if (!byObj["closed"].isNull()) closed = byObj["closed"].get_bool();
    }

    DCT_ID idTokenFrom = {std::numeric_limits<uint32_t>::max()}, idTokenTo = {std::numeric_limits<uint32_t>::max()};
    if (!fromSymbol.empty())
    {        
        auto token1 = pcustomcsview->GetTokenGuessId(fromSymbol, idTokenFrom);
        if (!token1) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", fromSymbol));
        chainTo=toSymbol;
    }
    else if (!toSymbol.empty())
    {
        chainFrom=fromSymbol;
        auto token2 = pcustomcsview->GetTokenGuessId(toSymbol, idTokenTo);
        if (!token2) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", toSymbol));
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("\"from\" or \"to\" field must have a Token that exists on DFI chain!"));

    
    UniValue ret(UniValue::VOBJ);
    if (idTokenFrom.v != std::numeric_limits<uint32_t>::max() || idTokenTo.v != std::numeric_limits<uint32_t>::max())
    {
        CICXOrderView::AssetPair prefix;
        if (idTokenFrom.v != std::numeric_limits<uint32_t>::max())
        {
            prefix.first=idTokenFrom;
            prefix.second=chainTo;
        }
        else
        {
            prefix.first=idTokenTo;
            prefix.second=chainFrom;
        }

        if (!closed)
            pcustomcsview->ForEachICXOrder([&](CICXOrderView::AssetPairKey const & key, CICXOrderImplemetation order) {
                if (key.first != prefix) return (false);
                ret.pushKVs(icxOrderToJSON(order));
                limit--;
                return limit != 0;
            },prefix);
        else
            pcustomcsview->ForEachICXClosedOrder([&](CICXOrderView::AssetPairKey const & key, CICXOrderImplemetation order) {
            if (key.first != prefix) return (false);
            ret.pushKVs(icxOrderToJSON(order));
            limit--;
            return limit != 0;
        },prefix);

        return ret;
    }
    else if (!orderTxid.IsNull())
    {
        pcustomcsview->ForEachICXMakeOffer([&](uint256 const & key, CICXMakeOfferImplemetation makeoffer) {
            if (key != orderTxid) return (false);
            ret.pushKVs(icxMakeOfferToJSON(makeoffer));
            limit--;
            return limit != 0;
        },orderTxid);
        return ret;
    }
    if (!closed)
        pcustomcsview->ForEachICXOrder([&](CICXOrderView::AssetPairKey const & key, CICXOrderImplemetation order) {
            ret.pushKVs(icxOrderToJSON(order));

            limit--;
            return limit != 0;
        });
    else 
        pcustomcsview->ForEachICXClosedOrder([&](CICXOrderView::AssetPairKey const & key, CICXOrderImplemetation order) {
            ret.pushKVs(icxOrderToJSON(order));

            limit--;
            return limit != 0;
        });
    return ret;
}

static const CRPCCommand commands[] =
{ 
//  category        name                     actor (function)        params
//  --------------- ----------------------   ---------------------   ----------
    {"icxorderbook",   "icx_createorder",           &icxcreateorder, {"order"}},
    {"icxorderbook",   "icx_makeoffer",             &icxmakeoffer,   {"order"}},
    {"icxorderbook",   "icx_closeorder",            &icxcloseorder,  {"orderTx"}},
    {"icxorderbook",   "icx_getorder",              &icxgetorder,    {"orderTx"}},
    {"icxorderbook",   "icx_listorders",            &icxlistorders,  {"by"}},

};

void RegisterICXOrderbookRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}