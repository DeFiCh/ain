#include <masternodes/mn_rpc.h>

UniValue icxOrderToJSON(CICXOrderImplemetation const& order) {
    UniValue orderObj(UniValue::VOBJ);
    UniValue ret(UniValue::VOBJ);

    if (order.orderType)
    {
        auto tokenFrom = pcustomcsview->GetToken(order.idToken);
        if (!tokenFrom)
            return (ret);
        orderObj.pushKV("tokenFrom", tokenFrom->CreateSymbolKey(order.idToken));
        orderObj.pushKV("chainTo",order.chain);
    }
    else
    {
        auto tokenTo = pcustomcsview->GetToken(order.idToken);
        if (!tokenTo)
            return (ret);
        orderObj.pushKV("chainFrom",order.chain);
        orderObj.pushKV("tokenTo", tokenTo->CreateSymbolKey(order.idToken));
    }
    orderObj.pushKV("amountFrom", ValueFromAmount(order.amountFrom));
    orderObj.pushKV("amountToFill", ValueFromAmount(order.amountToFill));
    orderObj.pushKV("orderPrice", ValueFromAmount(order.orderPrice));
    orderObj.pushKV("height", static_cast<int>(order.creationHeight));
    orderObj.pushKV("expireHeight", static_cast<int>(order.expireHeight));
    if (order.closeHeight > -1)
    {
        orderObj.pushKV("closeHeight", static_cast<int>(order.closeHeight));
        if (!order.closeTx.IsNull()) orderObj.pushKV("closeTx", order.closeTx.GetHex());
    }
    else if (order.expireHeight <= pcustomcsview->GetLastHeight())
    {
        orderObj.pushKV("expired", true);
    }
    
    ret.pushKV(order.creationTx.GetHex(), orderObj);
    return (ret);
}

UniValue icxMakeOfferToJSON(CICXMakeOfferImplemetation const& makeoffer) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("orderTx", makeoffer.orderTx.GetHex());
    orderObj.pushKV("amount", ValueFromAmount(makeoffer.amount));
    if (!CPubKey(makeoffer.receiveDestination).IsFullyValid())
        orderObj.pushKV("receivePubkey", HexStr(makeoffer.receiveDestination));
    else if (!ScriptToString(CScript(makeoffer.receiveDestination.begin(),makeoffer.receiveDestination.end())).empty())
        orderObj.pushKV("receiveAddress", ScriptToString(CScript(makeoffer.receiveDestination.begin(),makeoffer.receiveDestination.end())));
    UniValue ret(UniValue::VOBJ);
    ret.pushKV(makeoffer.creationTx.GetHex(), orderObj);
    return ret;
}

UniValue icxSubmitDFCHTLCToJSON(CICXSubmitDFCHTLCImplemetation const& dfchtlc) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("type", "DFC");
    orderObj.pushKV("offerTx", dfchtlc.offerTx.GetHex());
    orderObj.pushKV("amount", ValueFromAmount(dfchtlc.amount));
    orderObj.pushKV("hash", dfchtlc.hash.GetHex());
    orderObj.pushKV("timeout", static_cast<int>(dfchtlc.timeout));
    orderObj.pushKV("height", static_cast<int>(dfchtlc.creationHeight));
    orderObj.pushKV("expireHeight", static_cast<int>(dfchtlc.expireHeight));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(dfchtlc.creationTx.GetHex(), orderObj);
    return ret;
}

UniValue icxSubmitEXTHTLCToJSON(CICXSubmitEXTHTLCImplemetation const& exthtlc) {
    UniValue orderObj(UniValue::VOBJ);
    orderObj.pushKV("type", "EXTERNAL");
    orderObj.pushKV("offerTx", exthtlc.offerTx.GetHex());
    orderObj.pushKV("amount", ValueFromAmount(exthtlc.amount));
    orderObj.pushKV("htlcscriptAddress", exthtlc.htlcscriptAddress);
    orderObj.pushKV("hash", exthtlc.hash.GetHex());
    orderObj.pushKV("ownerPubkey", HexStr(exthtlc.ownerPubkey));
    orderObj.pushKV("externalTimeout", static_cast<int>(exthtlc.timeout));
    orderObj.pushKV("height", static_cast<int>(exthtlc.creationHeight));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(exthtlc.creationTx.GetHex(), orderObj);
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
                        HelpExampleCli("icx_createorder", "'{\"ownerAddress\":\"tokenAddress\","
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
                           "{\"tokenFrom|chainFrom\",\"chainTo|tokenTo\",\"ownerAddress\",\"amountFrom\",\"orderPrice\"}");
    }
    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXOrder order;
    std::string tokenFromSymbol, tokenToSymbol;

    if (!metaObj["tokenFrom"].isNull()) {
        tokenFromSymbol = trim_ws(metaObj["tokenFrom"].getValStr());

        if (!metaObj["chainTo"].isNull()) order.chain = trim_ws(metaObj["chainTo"].getValStr());
        else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"chainTo\" must be non-null if \"tokenFrom\" specified");

        if (!metaObj["ownerAddress"].isNull()) order.ownerAddress = DecodeScript(metaObj["ownerAddress"].getValStr());
        else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"ownerAddress\" must be non-null if \"tokenFrom\" specified");
       
        if (!::IsMine(*pwallet, DecodeDestination(metaObj["ownerAddress"].getValStr()))) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Address (%s) is not owned by the wallet", metaObj["ownerAddress"].getValStr()));
        }

    }
    else if (!metaObj["chainFrom"].isNull()) {
        order.chain = trim_ws(metaObj["chainFrom"].getValStr());

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

    if (tokenFromSymbol.empty() && order.chain.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, either \"tokenFrom\" or \"chainFrom\" must be non-null. [tokenFrom,chainTo] or [chainFrom,tokenTo]");
    if (!tokenFromSymbol.empty() && !tokenToSymbol.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, \"tokenFrom\" and \"tokenTo\" cannot be set in the same time. [tokenFrom,chainTo] or [chainFrom,tokenTo]");
    
    if (!tokenFromSymbol.empty()) order.orderType = CICXOrder::TYPE_INTERNAL;
    else order.orderType = CICXOrder::TYPE_EXTERNAL;

    int targetHeight;
    {
        LOCK(cs_main);
        DCT_ID idToken;
        std::unique_ptr<CToken> token;

        if (order.orderType == CICXOrder::TYPE_INTERNAL)
        {
            token = pcustomcsview->GetTokenGuessId(tokenFromSymbol, idToken);
            if (!token) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenFromSymbol));
            }
            order.idToken = idToken;

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
                if (bal.nTokenId == order.idToken) total += bal.nValue;
            }
            if (total < order.amountFrom)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Not enough balance for Token %s for order amount %s!", token->CreateSymbolKey(order.idToken), ValueFromAmount(order.amountFrom).getValStr()));
        }
        else
        {
            token = pcustomcsview->GetTokenGuessId(tokenToSymbol, idToken);
            if (!token) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenToSymbol));
            }
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
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
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
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
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
                            {"receiveAddress", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "address for receiving DFC tokens in case of EXT/DFC order type"},
                            {"receivePubkey", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "pubkey which can claim external HTLC in case of DFC/EXT order type"},
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
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot make offer while still in Initial Block Download");
    }
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

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
        {
            if (!metaObj["receiveAddress"].isNull()) {
                CScript dest=DecodeScript(metaObj["receiveAddress"].getValStr());
                makeoffer.receiveDestination = std::vector<uint8_t>(dest.begin(),dest.end());
            }
            else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receiveAddress\" must be non-null");
            
            if (!::IsMine(*pwallet, DecodeDestination(metaObj["receiveAddress"].getValStr()))) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Address (%s) is not owned by the wallet", metaObj["receiveAddress"].getValStr()));
            }
        }
        else
        {
            if (!metaObj["receivePubkey"].isNull()) {
                makeoffer.receiveDestination = ParseHex(trim_ws(metaObj["receivePubkey"].getValStr()));
            }
            else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receivePubKey\" must be non-null");
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
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, makeoffer});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXMakeOfferMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
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
                            {"receiveAddress", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "address for receiving DFC tokens in case of EXT/DFC order type"},
                            {"receivePubkey", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "pubkey which can claim external HTLC in case of DFC/EXT order type"},
                            {"hash", RPCArg::Type::STR, RPCArg::Optional::NO, "hash of seed used for the hash lock part"},
                            {"timeout", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "timeout (absolute in block) for expiration of htlc"},
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
                        HelpExampleCli("icx_submitdfchtlc", "'{\"offerTx\":\"tokenAddress\","
                                                        "\"amount\":\"10\",\"hash\":\"\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot submit dfc htlc while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"offerTx\",\"amount\",\"receiverAddress\",\"hash\"}");
    }
    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXSubmitDFCHTLC submitdfchtlc;

    if (!metaObj["offerTx"].isNull()) {
        submitdfchtlc.offerTx = uint256S(metaObj["offerTx"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"offerTx\" must be non-null");
    if (!metaObj["amount"].isNull()) {
        submitdfchtlc.amount = AmountFromValue(metaObj["amount"]);
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amount\" must be non-null");
    if (!metaObj["hash"].isNull()) {
        submitdfchtlc.hash = uint256S(metaObj["hash"].getValStr());;
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"hash\" must be non-null");
    if (!metaObj["timeout"].isNull()) {
        submitdfchtlc.timeout = metaObj["timeout"].get_int();
    }

    int targetHeight;
    {
        LOCK(cs_main);
        auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(submitdfchtlc.offerTx);
        if (!offer)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "offerTx (" + submitdfchtlc.offerTx.GetHex() + ") does not exist");
        auto order = pcustomcsview->GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + offer->orderTx.GetHex() + ") does not exist");

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
        {
            if (!metaObj["receivePubkey"].isNull()) {
                submitdfchtlc.receiveDestination = ParseHex(trim_ws(metaObj["receivePubkey"].getValStr()));
            }
            else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receivePubKey\" must be non-null or invalid pubkey");
        }
        else
        {
            if (!metaObj["receiveAddress"].isNull()) {
                CScript dest=DecodeScript(metaObj["receiveAddress"].getValStr());
                submitdfchtlc.receiveDestination = std::vector<uint8_t>(dest.begin(),dest.end());
            }
            else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"receiveAddress\" must be non-null");
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
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, submitdfchtlc});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXSubmitDFCHTLCMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
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
                            {"htlcScriptAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "script address of external htlc"},
                            {"hash", RPCArg::Type::STR, RPCArg::Optional::NO, "hash of seed used for the hash lock part"},
                            {"ownerPubkey", RPCArg::Type::STR, RPCArg::Optional::NO, "pubkey of the owner to which the funds are refunded if HTLC timeouts"},
                            {"timeout", RPCArg::Type::NUM, RPCArg::Optional::NO, "timeout (absolute in block) for expiration of htlc"},
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
                        HelpExampleCli("icx_submitexthtlc", "'{\"offerTx\":\"tokenAddress\","
                                                        "\"amount\":\"10\",\"hash\":\"\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot submit ext htlc while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"offerTx\",\"amount\",\"htlcScriptAddress\",\"hash\",\"refundPubkey\",\"timeout\"}");
    }
    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXSubmitEXTHTLC submitexthtlc;

    if (!metaObj["offerTx"].isNull()) {
        submitexthtlc.offerTx = uint256S(metaObj["offerTx"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"offerTx\" must be non-null");
    if (!metaObj["amount"].isNull()) {
        submitexthtlc.amount = AmountFromValue(metaObj["amount"]);
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amount\" must be non-null");
    if (!metaObj["hash"].isNull()) {
        submitexthtlc.hash = uint256S(metaObj["hash"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"hash\" must be non-null");
    if (!metaObj["htlcScriptAddress"].isNull()) {
        submitexthtlc.htlcscriptAddress = trim_ws(metaObj["htlcScriptAddress"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"htlcScriptAddress\" must be non-null");
    if (!metaObj["ownerPubkey"].isNull()) {
        submitexthtlc.ownerPubkey = PublickeyFromString(metaObj["ownerPubkey"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"ownerPubkey\" must be non-null");
    if (!metaObj["timeout"].isNull()) {
        submitexthtlc.timeout = metaObj["timeout"].get_int();
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"timeout\" must be non-null");

    int targetHeight;
    {
        LOCK(cs_main);
        auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(submitexthtlc.offerTx);
        if (!offer)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "offerTx (" + submitexthtlc.offerTx.GetHex() + ") does not exist");
        auto order = pcustomcsview->GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "orderTx (" + offer->orderTx.GetHex() + ") does not exist");

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
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, submitexthtlc});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXSubmitEXTHTLCMessage{}, coinview);
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
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
                        HelpExampleCli("icx_claimdfchtlc", "'{\"dfchtlcTx\":\"tokenAddress\","
                                                        "\"hash\":\"\"}'")
                },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot claim dfc htlc while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"dfchtlcTx\",\"receiverAddress\",\"seed\"}");
    }
    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CICXClaimDFCHTLC claimdfchtlc;

    if (!metaObj["dfchtlcTx"].isNull()) {
        claimdfchtlc.dfchtlcTx = uint256S(metaObj["dfchtlcTx"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"dfchtlcTx\" must be non-null");
    if (!metaObj["seed"].isNull()) {
        claimdfchtlc.seed = ParseHex(metaObj["seed"].getValStr());
    }
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"seed\" must be non-null");

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
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, closeorder});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CICXCloseOrderMessage{}, coinview);
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
        auto token2 = pcustomcsview->GetTokenGuessId(toSymbol, idTokenTo);
        if (!token2) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", toSymbol));
        chainFrom=fromSymbol;
    }
    //else throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("\"from\" or \"to\" field must have a Token that exists on DFI chain!"));

    uint8_t status = closed?CICXOrder::STATUS_CLOSED:CICXOrder::STATUS_OPEN;
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
        pcustomcsview->ForEachICXOrder([&](CICXOrderView::OrderKey const & key, uint8_t i) {
            if (key.first.first != status || key.first.second != prefix)
                return (false);
            auto order = pcustomcsview->GetICXOrderByCreationTx(key.second);
            if (order)
                ret.pushKVs(icxOrderToJSON(*order));
            limit--;
            return limit != 0;
        },{status, prefix});
        return ret;
    }
    else if (!orderTxid.IsNull())
    {
        pcustomcsview->ForEachICXMakeOffer([&](CICXOrderView::TxidPairKey const & key, uint8_t i) {
            if (key.first != orderTxid)
                return (false);
            auto offer = pcustomcsview->GetICXMakeOfferByCreationTx(key.second);
            if (offer)
                ret.pushKVs(icxMakeOfferToJSON(*offer));
            limit--;
            return limit != 0;
        },orderTxid);
        return ret;
    }
    pcustomcsview->ForEachICXOrder([&](CICXOrderView::OrderKey const & key, uint8_t i) {
        if (key.first.first != status)
            return (false);
        auto order = pcustomcsview->GetICXOrderByCreationTx(key.second);
        if (order) ret.pushKVs(icxOrderToJSON(*order));
        limit--;
        return limit != 0;
    },{status,{}});
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
                        HelpExampleCli("icx_listorders", "'{\"offerTx\":\"acb4d7eef089e74708afc6d9ca40af34f27a70506094dac39a5b9fb0347614fb\"}'")
                    
                }
     }.Check(request);

    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"offerTx\"}");
    }

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"dfchtlcTx\",\"amount\",\"receiverAddress\",\"seed\"}");
    }
    size_t limit = 20;
    uint256 offerTxid;
    bool refunded = false, claimed = false;
    
    UniValue byObj = request.params[0].get_obj();
    if (!byObj["offerTx"].isNull()) offerTxid = uint256S(byObj["offerTx"].getValStr());
    else throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"offerTx\" must be non-null");
    if (!byObj["refunded"].isNull()) refunded = byObj["refunded"].get_bool();
    if (!byObj["claimed"].isNull()) claimed = byObj["claimed"].get_bool();
    if (!byObj["limit"].isNull()) limit = (size_t) byObj["limit"].get_int64();
    
    uint8_t status = CICXSubmitDFCHTLC::STATUS_OPEN;
    if (refunded) status = CICXSubmitDFCHTLC::STATUS_REFUNDED;
    else if (claimed) status = CICXSubmitDFCHTLC::STATUS_CLAIMED;
    
    UniValue ret(UniValue::VOBJ);
    pcustomcsview->ForEachICXSubmitDFCHTLC([&](CICXOrderView::StatusTxidKey const & key, uint8_t i) {
        if (key.first.first != status || key.first.second != offerTxid)
            return false;
        auto dfchtlc = pcustomcsview->GetICXSubmitDFCHTLCByCreationTx(key.second);
        if (dfchtlc)
            ret.pushKVs(icxSubmitDFCHTLCToJSON(*dfchtlc));
        limit--;
        return limit != 0;
    },{status,offerTxid});
    pcustomcsview->ForEachICXSubmitEXTHTLC([&](CICXOrderView::StatusTxidKey const & key, uint8_t i) {
        if (key.first.first != status || key.first.second != offerTxid)
            return false;
        auto exthtlc = pcustomcsview->GetICXSubmitEXTHTLCByCreationTx(key.second);
        if (exthtlc)
            ret.pushKVs(icxSubmitEXTHTLCToJSON(*exthtlc));
        limit--;
        return limit != 0;
    },{status,offerTxid});
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