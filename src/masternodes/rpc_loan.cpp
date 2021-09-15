#include <masternodes/mn_rpc.h>

extern UniValue tokenToJSON(DCT_ID const& id, CTokenImplementation const& token, bool verbose);

UniValue setCollateralTokenToJSON(CLoanSetCollateralTokenImplementation const& collToken)
{
    UniValue collTokenObj(UniValue::VOBJ);

    auto token = pcustomcsview->GetToken(collToken.idToken);
    if (!token)
        return (UniValue::VNULL);
    collTokenObj.pushKV("token", token->CreateSymbolKey(collToken.idToken));
    collTokenObj.pushKV("factor", ValueFromAmount(collToken.factor));
    collTokenObj.pushKV("priceFeedId", collToken.priceFeedTxid.GetHex());
    if (collToken.activateAfterBlock)
        collTokenObj.pushKV("activateAfterBlock", static_cast<int>(collToken.activateAfterBlock));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(collToken.creationTx.GetHex(), collTokenObj);
    return (ret);
}

UniValue setLoanTokenToJSON(CLoanSetLoanTokenImplementation const& loanToken, DCT_ID tokenId)
{
    UniValue loanTokenObj(UniValue::VOBJ);

    auto token = pcustomcsview->GetToken(tokenId);
    if (!token)
        return (UniValue::VNULL);

    loanTokenObj.pushKV("token",tokenToJSON(tokenId, *static_cast<CTokenImplementation*>(token.get()), true));
    loanTokenObj.pushKV("priceFeedId", loanToken.priceFeedTxid.GetHex());
    loanTokenObj.pushKV("interest", ValueFromAmount(loanToken.interest));

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(loanToken.creationTx.GetHex(), loanTokenObj);
    return (ret);
}

UniValue setcollateraltoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"setcollateraltoken",
                "Creates (and submits to local node and network) a set colleteral token transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Symbol or id of collateral token"},
                            {"factor", RPCArg::Type::NUM, RPCArg::Optional::NO, "Collateralization factor"},
                            {"priceFeedId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "txid of oracle feeding the price"},
                            {"activateAfterBlock", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "changes will be active after the block height (Optional)"},
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
                        HelpExampleCli("setcollateraltoken", R"('{"token":"XXX","factor":"150","priceFeedId":"txid"}')")
                        },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot setcollateraltoken while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"token\",\"factor\",\"priceFeedId\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    std::string tokenSymbol;
    CLoanSetCollateralToken collToken;

    if (!metaObj["token"].isNull())
        tokenSymbol = trim_ws(metaObj["token"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"token\" must not be null");
    if (!metaObj["factor"].isNull())
        collToken.factor = AmountFromValue(metaObj["factor"]);
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"factor\" must not be null");
    if (!metaObj["priceFeedId"].isNull())
        collToken.priceFeedTxid = uint256S(metaObj["priceFeedId"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"priceFeedId\" must be non-null");
    if (!metaObj["activateAfterBlock"].isNull())
        collToken.activateAfterBlock = metaObj["activateAfterBlock"].get_int();

    int targetHeight;
    {
        LOCK(cs_main);

        DCT_ID idToken;
        std::unique_ptr<CToken> token;

        token = pcustomcsview->GetTokenGuessId(tokenSymbol, idToken);
        if (!token)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));
        collToken.idToken = idToken;

        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::LoanSetCollateralToken)
             << collToken;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, txInputs);

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

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue getcollateraltoken(const JSONRPCRequest& request) {
    RPCHelpMan{"getcollateraltoken",
                "Return collateral token information.\n",
                {
                    {"by", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Symbol or id of collateral token"},
                            {"height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Valid at specified height"},
                        },
                    },
                },
                RPCResult
                {
                    "{...}     (object) Json object with collateral token information\n"
                },
                RPCExamples{
                    HelpExampleCli("getcollateraltoken", "")
                },
     }.Check(request);

    UniValue ret(UniValue::VOBJ);
    std::string tokenSymbol;
    DCT_ID idToken = {std::numeric_limits<uint32_t>::max()}, currentToken = {std::numeric_limits<uint32_t>::max()};

    LOCK(cs_main);
    uint32_t height = ::ChainActive().Height();

    if (request.params.size() > 0)
    {
        UniValue byObj = request.params[0].get_obj();

        if (!byObj["token"].isNull())
        {
            auto token = pcustomcsview->GetTokenGuessId(trim_ws(byObj["token"].getValStr()), idToken);
            if (!token)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));
        }
        if (!byObj["height"].isNull())
            height = (size_t) byObj["height"].get_int64();
    }

    CollateralTokenKey start{DCT_ID{0}, height};
    if (idToken.v != std::numeric_limits<uint32_t>::max())
    {
        start.id = idToken;
        auto collToken = pcustomcsview->HasLoanSetCollateralToken(start);
        if (collToken && collToken->factor)
        {
            ret.pushKVs(setCollateralTokenToJSON(*collToken));
        }

        return (ret);
    }

    pcustomcsview->ForEachLoanSetCollateralToken([&](CollateralTokenKey const & key, uint256 const & collTokenTx) {

        if (idToken.v != std::numeric_limits<uint32_t>::max() && key.id != idToken)
            return false;

        if (key.height > height || currentToken == key.id) return true;

        currentToken = key.id;
        auto collToken = pcustomcsview->GetLoanSetCollateralToken(collTokenTx);
        if (collToken && collToken->factor)
        {
            ret.pushKVs(setCollateralTokenToJSON(*collToken));
        }
        return true;
    }, start);

    return (ret);
}


UniValue listcollateraltokens(const JSONRPCRequest& request) {
    RPCHelpMan{"listcollateraltokens",
                "Return list of all created collateral tokens.\n",
                {},
                RPCResult
                {
                    "{...}     (object) Json object with collateral token information\n"
                },
                RPCExamples{
                    HelpExampleCli("listcollateraltokens", "")
                },
     }.Check(request);

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);
    pcustomcsview->ForEachLoanSetCollateralToken([&](CollateralTokenKey const & key, uint256 const & collTokenTx) {
        auto collToken = pcustomcsview->GetLoanSetCollateralToken(collTokenTx);
        if (collToken)
        {
            ret.pushKVs(setCollateralTokenToJSON(*collToken));
        }
        return true;
    });

    return (ret);
}

UniValue setloantoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"setloantoken",
                "Creates (and submits to local node and network) a token for a price feed set in collateral token.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO, "Token's symbol (unique), not longer than " + std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                            {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token's name (optional), not longer than " + std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                            {"priceFeedId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "txid of oracle feeding the price"},
                            {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Token's 'Mintable' property (bool, optional), default is 'False'"},
                            {"interest", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Interest rate (default: 0)"},
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
                        HelpExampleCli("setloantoken", R"('{"symbol":"USDC","name":"USD Cake coin","priceFeedId":"txid","mintable":false,"interest":"0.03"}')")
                        },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot setloantoken while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"token\",\"factor\",\"priceFeedId\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CLoanSetLoanToken loanToken;

    if (!metaObj["symbol"].isNull())
        loanToken.symbol = trim_ws(metaObj["symbol"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"symbol\" must not be null");
    if (!metaObj["name"].isNull())
        loanToken.name = trim_ws(metaObj["name"].getValStr());
    if (!metaObj["priceFeedId"].isNull())
        loanToken.priceFeedTxid = uint256S(metaObj["priceFeedId"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"priceFeedId\" must be non-null");
    if (!metaObj["mintable"].isNull())
        loanToken.mintable = metaObj["mintable"].getBool();
    if (!metaObj["interest"].isNull())
        loanToken.interest = AmountFromValue(metaObj["interest"]);
    else
        loanToken.interest = 0;

    int targetHeight;

    {
        LOCK(cs_main);

        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::LoanSetLoanToken)
             << loanToken;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, txInputs);

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

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue updateloantoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"updateloantoken",
                "Creates (and submits to local node and network) a token for a price feed set in collateral token.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The tokens's symbol, id or creation tx"},
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"symbol", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token's symbol (unique), not longer than " + std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                            {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token's name (optional), not longer than " + std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                            {"priceFeedId", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "txid of oracle feeding the price"},
                            {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Token's 'Mintable' property (bool, optional), default is 'True'"},
                            {"interest", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Interest rate (optional)."},
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
                        HelpExampleCli("updateloantoken", R"('"token":"XXX", {"priceFeedId":"txid", "mintable": true, "interest": 0.03}')")
                        },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot updateloantoken while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValueType(), UniValue::VOBJ, UniValue::VARR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 0 must be non-null and expected as string with token symbol, id or creation txid");

    std::string const tokenStr = trim_ws(request.params[0].getValStr());
    UniValue metaObj = request.params[1].get_obj();
    UniValue const & txInputs = request.params[2];

    std::unique_ptr<CLoanSetLoanTokenImplementation> loanToken;
    CTokenImplementation tokenImpl;

    int targetHeight;
    {
        LOCK(cs_main);

        DCT_ID id;
        auto token = pcustomcsview->GetTokenGuessId(tokenStr, id);
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));
        }
        if (!token->IsLoanToken())
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is not a loan token! Can't alter other tokens with this tx!", tokenStr));
        if (id == DCT_ID{0}) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't alter DFI token!"));
        }
        loanToken = pcustomcsview->GetLoanSetLoanTokenByID(id);
        if (!loanToken) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't find %s loan token!", tokenStr));
        }

        targetHeight = ::ChainActive().Height() + 1;
    }

    if (!metaObj["symbol"].isNull())
        loanToken->symbol = trim_ws(metaObj["symbol"].getValStr());
    if (!metaObj["name"].isNull())
        loanToken->name = trim_ws(metaObj["name"].getValStr());
    if (!metaObj["priceFeedId"].isNull())
        loanToken->priceFeedTxid = uint256S(metaObj["priceFeedId"].getValStr());
    if (!metaObj["mintable"].isNull())
        loanToken->mintable = metaObj["mintable"].getBool();
    if (!metaObj["interest"].isNull())
        loanToken->interest = AmountFromValue(metaObj["interest"]);

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::LoanUpdateLoanToken)
             << static_cast<CLoanSetLoanToken>(*loanToken) << loanToken->creationTx;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, txInputs);

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

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listloantokens(const JSONRPCRequest& request) {
    RPCHelpMan{"listloantokens",
                "Return list of all created loan tokens.\n",
                {},
                RPCResult
                {
                    "{...}     (object) Json object with loan token information\n"
                },
                RPCExamples{
                    HelpExampleCli("listloantokens", "")
                },
     }.Check(request);

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);

    pcustomcsview->ForEachLoanSetLoanToken([&](DCT_ID const & key, CLoanView::CLoanSetLoanTokenImpl loanToken) {
        ret.pushKVs(setLoanTokenToJSON(loanToken,key));

        return true;
    });

    return (ret);
}

UniValue createloanscheme(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createloanscheme",
                "Creates a loan scheme transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"mincolratio", RPCArg::Type::NUM, RPCArg::Optional::NO, "Minimum collateralization ratio (integer)."},
                    {"interestrate", RPCArg::Type::NUM, RPCArg::Optional::NO, "Interest rate (integer or float)."},
                    {"id", RPCArg::Type::STR, RPCArg::Optional::NO, "Unique identifier of the loan scheme (8 chars max)."},
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
                   HelpExampleCli("createloanscheme", "150 5 LOAN0001") +
                   HelpExampleRpc("createloanscheme", "150, 5, LOAN0001")
                },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot createloanscheme while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    CLoanSchemeMessage loanScheme;
    loanScheme.ratio = request.params[0].get_int();
    loanScheme.rate = AmountFromValue(request.params[1]);
    loanScheme.identifier = request.params[2].get_str();

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::LoanScheme)
             << loanScheme;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, request.params[3]);

    rawTx.vout.emplace_back(0, scriptMeta);

    CCoinControl coinControl;

    // Set change to foundation address
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

UniValue updateloanscheme(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"updateloanscheme",
               "Updates an existing loan scheme.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"mincolratio", RPCArg::Type::NUM, RPCArg::Optional::NO, "Minimum collateralization ratio (integer)."},
                       {"interestrate", RPCArg::Type::NUM, RPCArg::Optional::NO, "Interest rate (integer or float)."},
                       {"id", RPCArg::Type::STR, RPCArg::Optional::NO, "Unique identifier of the loan scheme (8 chars max)."},
                       {"ACTIVATE_AFTER_BLOCK", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "Block height at which new changes take effect."},
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
                       HelpExampleCli("updateloanscheme", "150 5 LOAN0001") +
                       HelpExampleRpc("updateloanscheme", "150, 5, LOAN0001")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot updateloanscheme while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    CLoanSchemeMessage loanScheme;
    loanScheme.ratio = request.params[0].get_int();
    loanScheme.rate = AmountFromValue(request.params[1]);
    loanScheme.identifier = request.params[2].get_str();

    // Max value is ignored as block height
    loanScheme.updateHeight = std::numeric_limits<uint64_t>::max();
    if (!request.params[3].isNull()) {
        loanScheme.updateHeight = request.params[3].get_int();
    }

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::LoanScheme)
             << loanScheme;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, request.params[4]);

    rawTx.vout.emplace_back(0, scriptMeta);

    CCoinControl coinControl;

    // Set change to foundation address
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

UniValue setdefaultloanscheme(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"setdefaultloanscheme",
               "Sets the default loan scheme.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"id", RPCArg::Type::STR, RPCArg::Optional::NO, "Unique identifier of the loan scheme (8 chars max)."},
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
                       HelpExampleCli("setdefaultloanscheme", "LOAN0001") +
                       HelpExampleRpc("setdefaultloanscheme", "LOAN0001")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot setdefaultloanschem while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    CDefaultLoanSchemeMessage defaultScheme;
    defaultScheme.identifier = request.params[0].get_str();

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::DefaultLoanScheme)
             << defaultScheme;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, request.params[1]);

    rawTx.vout.emplace_back(0, scriptMeta);

    // Set change to foundation address
    CCoinControl coinControl;
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

UniValue destroyloanscheme(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"destroyloanscheme",
               "Destroys a loan scheme.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"id", RPCArg::Type::STR, RPCArg::Optional::NO, "Unique identifier of the loan scheme (8 chars max)."},
                       {"ACTIVATE_AFTER_BLOCK", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "Block height at which new changes take effect."},
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
                       HelpExampleCli("destroyloanscheme", "LOAN0001") +
                       HelpExampleRpc("destroyloanscheme", "LOAN0001")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot destroyloanscheme while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    CDestroyLoanSchemeMessage destroyScheme;
    destroyScheme.identifier = request.params[0].get_str();
    if (!request.params[1].isNull()) {
        destroyScheme.height = request.params[1].get_int();
    }

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::DestroyLoanScheme)
             << destroyScheme;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, request.params[2]);

    rawTx.vout.emplace_back(0, scriptMeta);

    // Set change to foundation address
    CCoinControl coinControl;
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

UniValue listloanschemes(const JSONRPCRequest& request) {

    RPCHelpMan{"listloanschemes",
               "List all available loan schemes\n",
               {},
               RPCResult{
                       "[                         (json array of objects)\n"
                       "  {\n"
                       "    \"id\" : n                   (string)\n"
                       "    \"mincolratio\" : n          (numeric)\n"
                       "    \"interestrate\" : n         (numeric)\n"
                       "  },\n"
                       "  ...\n"
                       "]\n"
               },
               RPCExamples{
                       HelpExampleCli("listloanschemes", "") +
                       HelpExampleRpc("listloanschemes", "")
               },
    }.Check(request);

    auto cmp = [](const CLoanScheme& a, const CLoanScheme& b) {
        return a.ratio == b.ratio ? a.rate < b.rate : a.ratio < b.ratio;
    };
    std::set<CLoanScheme, decltype(cmp)> loans(cmp);

    LOCK(cs_main);

    pcustomcsview->ForEachLoanScheme([&loans](const std::string& identifier, const CLoanSchemeData& data){
        CLoanScheme loanScheme;
        loanScheme.rate = data.rate;
        loanScheme.ratio = data.ratio;
        loanScheme.identifier = identifier;
        loans.insert(loanScheme);
        return true;
    });

    auto defaultLoan = pcustomcsview->GetDefaultLoanScheme();

    UniValue ret(UniValue::VARR);
    for (const auto& item : loans) {
        UniValue arr(UniValue::VOBJ);
        arr.pushKV("id", item.identifier);
        arr.pushKV("mincolratio", static_cast<uint64_t>(item.ratio));
        arr.pushKV("interestrate", ValueFromAmount(item.rate));
        if (defaultLoan && *defaultLoan == item.identifier) {
            arr.pushKV("default", true);
        } else {
            arr.pushKV("default", false);
        }
        ret.push_back(arr);
    }

    return ret;
}

UniValue getloanscheme(const JSONRPCRequest& request) {

    RPCHelpMan{"getloanscheme",
               "Sets the default loan scheme.\n",
               {
                    {"id", RPCArg::Type::STR, RPCArg::Optional::NO, "Unique identifier of the loan scheme (8 chars max)."},
               },
               RPCResult{
                       "  {\n"
                       "    \"id\" : n                   (string)\n"
                       "    \"mincolratio\" : n          (numeric)\n"
                       "    \"interestrate\" : n         (numeric)\n"
                       "  },\n"
               },
               RPCExamples{
                       HelpExampleCli("getloanscheme", "LOAN0001") +
                       HelpExampleRpc("getloanscheme", "LOAN0001")
               },
    }.Check(request);

    if(request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter id, argument must be non-null");

    auto loanSchemeId = request.params[0].getValStr();

    if (loanSchemeId.empty() || loanSchemeId.length() > 8)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "id cannot be empty or more than 8 chars long");

    LOCK(cs_main);
    auto loanScheme = pcustomcsview->GetLoanScheme(loanSchemeId);
    if (!loanScheme)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot find existing loan scheme with id " + loanSchemeId);

    UniValue result{UniValue::VOBJ};
    result.pushKV("id", loanSchemeId);
    result.pushKV("mincolratio", static_cast<uint64_t>(loanScheme->ratio));
    result.pushKV("interestrate", ValueFromAmount(loanScheme->rate));

    return result;
}

UniValue takeloan(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"takeloan",
                "Creates (and submits to local node and network) a tx to mint loan token in desired amount based on defined loan.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Id of vault used for loan"},
                            {"amounts", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount in amount@token format."},
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
                        HelpExampleCli("takeloan", R"('{"vaultId":84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2,"amount":"10@TSLA"}')")
                        },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot takeloan while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"vaultId\",\"amounts\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CLoanTakeLoan takeLoan;

    if (!metaObj["vaultId"].isNull())
        takeLoan.vaultId = uint256S(metaObj["vaultId"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"vaultId\" must be non-null");

    if (!metaObj["amounts"].isNull())
        takeLoan.amounts = DecodeAmounts(pwallet->chain(), metaObj["amounts"], "");
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" must not be null");

    int targetHeight;
    CScript ownerAddress;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
        auto vault = pcustomcsview->GetVault(takeLoan.vaultId);
        if (!vault)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Vault <%s> not found", takeLoan.vaultId.GetHex()));
        ownerAddress = vault->ownerAddress;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::LoanTakeLoan)
             << takeLoan;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{ownerAddress};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.emplace_back(0, scriptMeta);

    CCoinControl coinControl;

    // Return change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest))
        coinControl.destChange = dest;

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue loanpayback(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"loanpayback",
                "Creates (and submits to local node and network) a tx to return the loan in desired amount.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Id of vault used for loan"},
                            {"amounts", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount in amount@token format."},
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
                        HelpExampleCli("loanpayback", R"('{"vaultId":84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2,"amount":"10@TSLA"}')")
                        },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot loanpayback while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, argument 1 must be non-null and expected as object at least with "
                           "{\"vaultId\",\"amounts\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CLoanPaybackLoan loanPayback;

    if (!metaObj["vaultId"].isNull())
        loanPayback.vaultId = uint256S(metaObj["vaultId"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"vaultId\" must be non-null");

    // Get vault if exists, vault owner used as auth.
    auto vault = pcustomcsview->GetVault(loanPayback.vaultId);
    if (!vault) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Cannot find existing vault with id " + loanPayback.vaultId.GetHex());
    }

    if (!metaObj["amounts"].isNull())
        loanPayback.amounts = DecodeAmounts(pwallet->chain(), metaObj["amounts"], "");
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" must not be null");

    int targetHeight;
    {
        LOCK(cs_main);

        targetHeight = ::ChainActive().Height() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::LoanPaybackLoan)
             << loanPayback;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{vault.val->ownerAddress};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    rawTx.vout.emplace_back(0, scriptMeta);

    CCoinControl coinControl;

    // Return change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest))
        coinControl.destChange = dest;

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue getloaninfo(const JSONRPCRequest& request) {
    RPCHelpMan{"getloaninfo",
                "Returns the attributes of loan offered.\n",
                {},
                RPCResult
                {
                    "{...}     (object) Json object with loan information\n"
                },
                RPCExamples{
                    HelpExampleCli("getloaninfo", "")
                },
     }.Check(request);

    UniValue ret(UniValue::VOBJ);

    ret.pushKV("Collateral tokens",getcollateraltoken(request));
    ret.pushKV("Loan tokens",listloantokens(request));
    ret.pushKV("Loan schemes",listloanschemes(request));

    LOCK(cs_main);

    uint32_t height = ::ChainActive().Height();
    CAmount totalCollateral = 0, totalLoan = 0;

    pcustomcsview->ForEachVaultCollateral([&](const CVaultId& vaultId, const CBalances& collaterals) {
        auto rate = pcustomcsview->CalculateCollateralizationRatio(vaultId, collaterals, height);

        if (rate)
        {
            totalCollateral += rate->totalCollaterals();
            totalLoan += rate->totalLoans();
        }

        return true;
    });

    ret.pushKV("collateralValueUSD",ValueFromAmount(totalCollateral));
    ret.pushKV("loanValueUSD",ValueFromAmount(totalLoan));

    return (ret);
}

UniValue getinterest(const JSONRPCRequest& request) {
    RPCHelpMan{"getinterest",
                "Returns the global and per block interest by loan scheme.\n",
                {
                    {"id", RPCArg::Type::STR, RPCArg::Optional::NO, "Unique identifier of the loan scheme (8 chars max)."},
                    {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The tokens's symbol, id or creation tx"},
                },
                RPCResult
                {
                    "{...}     (object) Json object with interest information\n"
                },
                RPCExamples{
                    HelpExampleCli("getinterest", "LOAN0001 TSLA")
                },
     }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValueType()}, false);

    auto loanSchemeId = request.params[0].get_str();
    auto tokenStr = trim_ws(request.params[1].getValStr());

    if (loanSchemeId.empty() || loanSchemeId.length() > 8)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "id cannot be empty or more than 8 chars long");

    LOCK(cs_main);

    if (!pcustomcsview->GetLoanScheme(loanSchemeId))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot find existing loan scheme with id " + loanSchemeId);

    DCT_ID id{~0u};

    if (!tokenStr.empty() && !pcustomcsview->GetTokenGuessId(tokenStr, id))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));

    const auto mask = id.v;
    if (id.v == ~0u)
        id.v = 0;

    UniValue ret(UniValue::VARR);
    uint32_t height = ::ChainActive().Height();

    pcustomcsview->ForEachInterest([&](const std::string& schemeId, DCT_ID tokenId, CInterestRate rate) {
        if (schemeId != loanSchemeId)
            return false;

        if ((tokenId.v & mask) != tokenId.v)
            return false;

        auto token = pcustomcsview->GetToken(tokenId);
        if (!token)
            return true;

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("token", token->CreateSymbolKey(tokenId));
        obj.pushKV("totalInterest", ValueFromAmount(TotalInterest(rate, height)));
        obj.pushKV("interestPerBlock", ValueFromAmount(rate.interestPerBlock));
        ret.push_back(obj);

        return true;
    }, loanSchemeId, id);

    return ret;
}


static const CRPCCommand commands[] =
{
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"loan",        "setcollateraltoken",        &setcollateraltoken,    {"metadata", "inputs"}},
    {"loan",        "getcollateraltoken",        &getcollateraltoken,    {"by"}},
    {"loan",        "listcollateraltokens",      &listcollateraltokens,  {}},
    {"loan",        "setloantoken",              &setloantoken,          {"metadata", "inputs"}},
    {"loan",        "updateloantoken",           &updateloantoken,       {"metadata", "inputs"}},
    {"loan",        "listloantokens",            &listloantokens,        {}},
    {"loan",        "createloanscheme",          &createloanscheme,      {"mincolratio", "interestrate", "id", "inputs"}},
    {"loan",        "updateloanscheme",          &updateloanscheme,      {"mincolratio", "interestrate", "id", "ACTIVATE_AFTER_BLOCK", "inputs"}},
    {"loan",        "setdefaultloanscheme",      &setdefaultloanscheme,  {"id", "inputs"}},
    {"loan",        "destroyloanscheme",         &destroyloanscheme,     {"id", "ACTIVATE_AFTER_BLOCK", "inputs"}},
    {"loan",        "listloanschemes",           &listloanschemes,       {}},
    {"loan",        "getloanscheme",             &getloanscheme,         {"id"}},
    {"loan",        "takeloan",                  &takeloan,              {"metadata", "inputs"}},
    {"loan",        "loanpayback",               &loanpayback,           {"metadata", "inputs"}},
    {"loan",        "getloaninfo",               &getloaninfo,           {}},
    {"loan",        "getinterest",               &getinterest,           {"id", "token"}},
};

void RegisterLoanRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
