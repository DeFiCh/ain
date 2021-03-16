#include <masternodes/mn_rpc.h>

UniValue orderToJSON(COrderImplemetation const& order) {
    UniValue orderObj(UniValue::VOBJ);
    auto tokenFrom = pcustomcsview->GetToken(order.idTokenFrom);
    auto tokenTo = pcustomcsview->GetToken(order.idTokenTo);
    orderObj.pushKV("ownerAddress", order.ownerAddress);
    orderObj.pushKV("tokenFrom", tokenFrom->CreateSymbolKey(order.idTokenFrom));
    orderObj.pushKV("tokenTo", tokenTo->CreateSymbolKey(order.idTokenTo));
    orderObj.pushKV("amountFrom", order.amountFrom);
    orderObj.pushKV("orderPrice", order.orderPrice);
    orderObj.pushKV("height", static_cast<int>(order.creationHeight));
    orderObj.pushKV("expiry", static_cast<int>(order.expiry));
    if (!order.closeTx.IsNull())
    {
        orderObj.pushKV("closeTx", order.closeTx.GetHex());
        orderObj.pushKV("closeHeight", static_cast<int>(order.closeHeight));
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(order.creationTx.GetHex(), orderObj);
    return ret;
}

UniValue fulfillOrderToJSON(CFulfillOrderImplemetation const& fulfillorder) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("ownerAddress", fulfillorder.ownerAddress);
    orderObj.pushKV("orderTx", fulfillorder.orderTx.GetHex());
    orderObj.pushKV("amount", fulfillorder.amount);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(fulfillorder.creationTx.GetHex(), orderObj);
    return ret;
}

UniValue createorder(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createorder",
                "\nCreates (and submits to local node and network) a order creation transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"order", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address of the owner of token"},
                            {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::NO, "Symbol or id of selling token"},
                            {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::NO, "Symbol or id of buying token"},
                            {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO, "tokenFrom coins amount"},
                            {"orderPrice", RPCArg::Type::NUM, RPCArg::Optional::NO, "Price per unit"},
                            {"expiry", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Number of blocks until the order expires (Default: 2880 blocks)"},
                            {"optionDFI", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Amount in DFI per unit the taker has to pay if they do not complete the order (Default: 8 DFI)"}
                        },
                    },
                },
                RPCResult{
                        "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                        HelpExampleCli("createorder", "'{\"ownerAddress\":\"tokenAddress\","
                                                        "\"tokenFrom\":\"MyToken1\",\"tokenTo\":\"MyToken2\","
                                                        "\"amountFrom\":\"10\",\"orderPrice\":\"0.02\"}'")
                        + HelpExampleCli("createorder", "'{\"ownerAddress\":\"tokenAddress\","
                                                        "\"tokenFrom\":\"MyToken1\",\"tokenTo\":\"MyToken2\","
                                                        "\"amountFrom\":\"5\",\"orderPrice\":\"0.1\","
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
    COrder order;
    std::string tokenFromSymbol, tokenToSymbol;

    if (!metaObj["ownerAddress"].isNull()) {
        order.ownerAddress = trim_ws(metaObj["ownerAddress"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"ownerAddress\" must be non-null");
    if (!metaObj["tokenFrom"].isNull()) {
        tokenFromSymbol = trim_ws(metaObj["tokenFrom"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"tokenFrom\" must be non-null");
    if (!metaObj["tokenTo"].isNull()) {
        tokenToSymbol = trim_ws(metaObj["tokenTo"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"tokenTo\" must be non-null");
    if (!metaObj["amountFrom"].isNull()) {
        order.amountFrom = AmountFromValue(metaObj["amountFrom"]);
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amountFrom\" must be non-null");
    if (!metaObj["orderPrice"].isNull()) {
        order.orderPrice = AmountFromValue(metaObj["orderPrice"]);
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"orderPrice\" must be non-null");
    if (!metaObj["expiry"].isNull()) {
        order.expiry = metaObj["expiry"].get_int();
    }
    if (!metaObj["optionDFI"].isNull()) {
        order.optionDFI = AmountFromValue(metaObj["optionDFI"]);
    }
    CTxDestination ownerDest = DecodeDestination(order.ownerAddress);
    if (ownerDest.which() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ownerAdress (" + order.ownerAddress + ") does not refer to any valid address");
    }

    int targetHeight;
    {
        LOCK(cs_main);
        DCT_ID idTokenFrom, idTokenTo;
        auto tokenFrom = pcustomcsview->GetTokenGuessId(tokenFromSymbol, idTokenFrom);
        if (!tokenFrom) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenFromSymbol));
        }
        auto tokenTo = pcustomcsview->GetTokenGuessId(tokenToSymbol, idTokenTo);
        if (!tokenTo) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenToSymbol));
        }

        order.idTokenFrom = idTokenFrom;
        order.idTokenTo = idTokenTo;

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
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s for order amount %f!", tokenFrom->CreateSymbolKey(order.idTokenFrom), (double)order.amountFrom/COIN));
    
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateOrder)
             << order;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    fund(rawTx, pwallet, {});

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        const auto res = ApplyCreateOrderTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, order}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue fulfillorder(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"fulfillorder",
                "\nCreates (and submits to local node and network) a fill order transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"order", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address of the owner of token"},
                            {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of maker order"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "coins amount to fulfill the order"},
                        },
                    },
                },
                RPCResult{
                        "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                        HelpExampleCli("fulfillorder", "'{\"ownerAddress\":\"tokenAddress\","
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
    CFulfillOrder fillorder;

    if (!metaObj["ownerAddress"].isNull()) {
        fillorder.ownerAddress = trim_ws(metaObj["ownerAddress"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"ownerAddress\" must be non-null");
    if (!metaObj["orderTx"].isNull()) {
        fillorder.orderTx = uint256S(metaObj["orderTx"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"orderTx\" must be non-null");
    if (!metaObj["amount"].isNull()) {
        fillorder.amount = AmountFromValue(metaObj["amount"]);
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amount\" must be non-null");

    CTxDestination ownerDest = DecodeDestination(fillorder.ownerAddress);
    if (ownerDest.which() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "ownerAdress (" + fillorder.ownerAddress + ") does not refer to any valid address");
    }

    int targetHeight;
    {
        LOCK(cs_main);
        auto order = pcustomcsview->GetOrderByCreationTx(fillorder.orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + fillorder.orderTx.GetHex() + ") does not exist");

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
            if (bal.nTokenId == order->idTokenTo) total += bal.nValue;
        }
        if (total < fillorder.amount)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s for order amount %f!", pcustomcsview->GetToken(order->idTokenTo)->CreateSymbolKey(order->idTokenTo), (double)fillorder.amount/COIN));
    
        targetHeight = ::ChainActive().Height() + 1;
    }


    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::FulfillOrder)
             << fillorder;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    fund(rawTx, pwallet, {});

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        const auto res = ApplyFulfillOrderTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, fillorder}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue closeorder(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"closeorder",
                "\nCloses (and submits to local node and network) order transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of maker order"},
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
    CCloseOrder closeorder;
    closeorder.orderTx = uint256S(request.params[0].getValStr());

    int targetHeight;
    {
        LOCK(cs_main);
        auto order = pcustomcsview->GetOrderByCreationTx(closeorder.orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + closeorder.orderTx.GetHex() + ") does not exist");
        if (!order->closeTx.IsNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,"orderTx (" + closeorder.orderTx.GetHex() + " is already closed!");
        }
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CloseOrder)
             << closeorder;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    fund(rawTx, pwallet, {});

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        const auto res = ApplyCloseOrderTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, closeorder}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue getorder(const JSONRPCRequest& request) {
    RPCHelpMan{"getorder",
                "\nReturn information about order or fillorder.\n",
                {
                    {"orderTx", RPCArg::Type::STR, RPCArg::Optional::NO, "txid of createorder or fulfillorder tx"},
                },
                RPCResult
                {
                    "{...}     (object) Json object with order information\n"
                },
                RPCExamples{
                    HelpExampleCli("getorder", "'{\"orderTx\":\"acb4d7eef089e74708afc6d9ca40af34f27a70506094dac39a5b9fb0347614fb\"}'")     
                },
     }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as \"orderTx\"}");
    }
    uint256 orderTxid= uint256S(request.params[0].getValStr());
    auto order = pcustomcsview->GetOrderByCreationTx(orderTxid);
    if (order)
    {
        return orderToJSON(*order);
    }
    auto fillorder = pcustomcsview->GetFulfillOrderByCreationTx(orderTxid);
    if (fillorder)
    {
        return fulfillOrderToJSON(*fillorder);
    }
    throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + orderTxid.GetHex() + ") does not exist");    
}

UniValue listorders(const JSONRPCRequest& request) {
    RPCHelpMan{"listorders",
                "\nReturn information about orders.\n",
                {
                        {"by", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                {"limit",  RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Maximum number of orders to return (default: 50)"},
                                {"token1", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token symbol"},
                                {"token2", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token symbol"},
                                {"orderTx",RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Order txid to list all fulfill orders for this order"},
                                {"closed", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Display closed orders (default: false)"},
                            },
                        },
                },
                RPCResult
                {
                        "{{...},...}     (array) Json object with orders information\n"
                },
                RPCExamples{
                        HelpExampleCli("listorders", "'{\"limit\":\"10\"}'")
                        + HelpExampleCli("listorders", "'{\"token\":\"MyToken1\",\"tokenPair\":\"Mytoken2\"}'")
                        + HelpExampleCli("listorders", "'{\"token\":\"MyToken1\",\"tokenPair\":\"Mytoken2\",\"closed\":true}'")
                        + HelpExampleCli("listorders", "'{\"orderTx\":\"acb4d7eef089e74708afc6d9ca40af34f27a70506094dac39a5b9fb0347614fb\"}'")
                }
     }.Check(request);

    size_t limit = 50;
    std::string token1Symbol, token2Symbol;
    uint256 orderTxid;
    bool closed = false;
    if (request.params.size() > 0)
    {
        UniValue byObj = request.params[0].get_obj();
        if (!byObj["token1"].isNull()) token1Symbol = trim_ws(byObj["token1"].getValStr());
        if (!byObj["token2"].isNull()) token2Symbol = trim_ws(byObj["token2"].getValStr());
        if (!byObj["limit"].isNull()) limit = (size_t) byObj["limit"].get_int64();
        if (!byObj["orderTx"].isNull()) orderTxid = uint256S(byObj["orderTx"].getValStr());
        if (!byObj["closed"].isNull()) closed = byObj["closed"].get_bool();
    }

    DCT_ID idToken1 = {std::numeric_limits<uint32_t>::max()}, idToken2 = {std::numeric_limits<uint32_t>::max()};
    if (!token1Symbol.empty())
    {
        if (token2Symbol.empty()) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("If token1 is specified you must specify token2!"));
        auto token1 = pcustomcsview->GetTokenGuessId(token1Symbol, idToken1);
            if (!token1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", token1Symbol));
            }
        auto token2 = pcustomcsview->GetTokenGuessId(token2Symbol, idToken2);
            if (!token2) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", token2Symbol));
            }
    }
    
    UniValue ret(UniValue::VOBJ);
    if (idToken1.v != std::numeric_limits<uint32_t>::max() && idToken2.v != std::numeric_limits<uint32_t>::max())
    {
        COrderView::TokenPair prefix(idToken1,idToken2);
        if (!closed)
            pcustomcsview->ForEachOrder([&](COrderView::TokenPairKey const & key, COrderImplemetation order) {
                if (key.first != prefix) return (false);
                ret.pushKVs(orderToJSON(order));
                limit--;
                return limit != 0;
            },prefix);
        else
            pcustomcsview->ForEachClosedOrder([&](COrderView::TokenPairKey const & key, COrderImplemetation order) {
            if (key.first != prefix) return (false);
            ret.pushKVs(orderToJSON(order));
            limit--;
            return limit != 0;
        },prefix);
        return ret;
    }
    else if (!orderTxid.IsNull())
    {
        pcustomcsview->ForEachFulfillOrder([&](COrderView::FulfillOrderId const & key, CFulfillOrderImplemetation fillorder) {
            if (key.first != orderTxid) return (false);
            ret.pushKVs(fulfillOrderToJSON(fillorder));
            limit--;
            return limit != 0;
        },orderTxid);
        return ret;
    }
    if (!closed)
        pcustomcsview->ForEachOrder([&](COrderView::TokenPairKey const & key, COrderImplemetation order) {
            ret.pushKVs(orderToJSON(order));

            limit--;
            return limit != 0;
        });
    else 
        pcustomcsview->ForEachClosedOrder([&](COrderView::TokenPairKey const & key, COrderImplemetation order) {
            ret.pushKVs(orderToJSON(order));

            limit--;
            return limit != 0;
        });
    return ret;
}

static const CRPCCommand commands[] =
{ 
//  category        name                     actor (function)        params
//  --------------- ----------------------   ---------------------   ----------
    {"orderbook",   "createorder",           &createorder,           {"order"}},
    {"orderbook",   "fulfillorder",          &fulfillorder,          {"order"}},
    {"orderbook",   "closeorder",            &closeorder,            {"orderTx"}},
    {"orderbook",   "getorder",              &getorder,              {"orderTx"}},
    {"orderbook",   "listorders",            &listorders,            {"by"}},

};

void RegisterOrderbookRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
