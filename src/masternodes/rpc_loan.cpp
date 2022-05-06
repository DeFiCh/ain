#include <masternodes/mn_rpc.h>

#include <masternodes/govvariables/attributes.h>

extern UniValue tokenToJSON(CImmutableCSView& view, DCT_ID const& id, CTokenImplementation const& token, bool verbose);
extern UniValue listauctions(const JSONRPCRequest& request);
extern std::pair<int, int> GetFixedIntervalPriceBlocks(int currentHeight, const CImmutableCSView &mnview);

UniValue setCollateralTokenToJSON(CImmutableCSView& view, CLoanSetCollateralTokenImplementation const& collToken)
{
    UniValue collTokenObj(UniValue::VOBJ);

    auto token = view.GetToken(collToken.idToken);
    if (!token)
        return (UniValue::VNULL);

    collTokenObj.pushKV("token", token->CreateSymbolKey(collToken.idToken));
    collTokenObj.pushKV("tokenId", collToken.creationTx.GetHex());
    collTokenObj.pushKV("factor", ValueFromAmount(collToken.factor));
    collTokenObj.pushKV("fixedIntervalPriceId", collToken.fixedIntervalPriceId.first + "/" + collToken.fixedIntervalPriceId.second);
    if (collToken.activateAfterBlock)
        collTokenObj.pushKV("activateAfterBlock", static_cast<int>(collToken.activateAfterBlock));

    return (collTokenObj);
}

UniValue setLoanTokenToJSON(CImmutableCSView& view, CLoanSetLoanTokenImplementation const& loanToken, DCT_ID tokenId)
{
    UniValue loanTokenObj(UniValue::VOBJ);

    auto token = view.GetToken(tokenId);
    if (!token)
        return (UniValue::VNULL);

    loanTokenObj.pushKV("token", tokenToJSON(view, tokenId, *token, true));
    loanTokenObj.pushKV("fixedIntervalPriceId", loanToken.fixedIntervalPriceId.first + "/" + loanToken.fixedIntervalPriceId.second);
    loanTokenObj.pushKV("interest", ValueFromAmount(loanToken.interest));
    loanTokenObj.pushKV("mintable", loanToken.mintable);

    return (loanTokenObj);
}

CTokenCurrencyPair DecodePriceFeedString(const std::string& value)
{
    auto delim = value.find('/');
    if (delim == value.npos || value.find('/', delim + 1) != value.npos)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "price feed not in valid format - token/currency!");

    auto token = trim_ws(value.substr(0, std::min(delim, size_t(CToken::MAX_TOKEN_SYMBOL_LENGTH))));
    auto currency = trim_ws(value.substr(delim + 1, CToken::MAX_TOKEN_SYMBOL_LENGTH));

    if (token.empty() || currency.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token/currency contains empty string");

    return std::make_pair(token, currency);
}

CTokenCurrencyPair DecodePriceFeedUni(const UniValue& value)
{
    auto tokenCurrency = value["fixedIntervalPriceId"].getValStr();

    if (tokenCurrency.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, argument \"fixedIntervalPriceId\" must be non-null");

    return DecodePriceFeedString(tokenCurrency);
}

UniValue setcollateraltoken(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"setcollateraltoken",
                "Creates (and submits to local node and network) a set colleteral token transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Symbol or id of collateral token"},
                            {"factor", RPCArg::Type::NUM, RPCArg::Optional::NO, "Collateralization factor"},
                            {"fixedIntervalPriceId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "token/currency pair to use for price of token"},
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
                        HelpExampleCli("setcollateraltoken", R"('{"token":"TSLA","factor":"150","fixedIntervalPriceId":"TSLA/USD"}')")
                        },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot setcollateraltoken while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"token\",\"factor\",\"fixedIntervalPriceId\"}");

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

    collToken.fixedIntervalPriceId = DecodePriceFeedUni(metaObj);

    if (!metaObj["activateAfterBlock"].isNull())
        collToken.activateAfterBlock = metaObj["activateAfterBlock"].get_int();

    CImmutableCSView view(*pcustomcsview);

    int targetHeight;
    {
        DCT_ID idToken;

        const auto token = view.GetTokenGuessId(tokenSymbol, idToken);
        if (!token)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));

        collToken.idToken = idToken;

        targetHeight = view.GetLastHeight() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::SetLoanCollateralToken)
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
                    {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Symbol or id of collateral token"},
                },
                RPCResult
                {
                    "{...}     (object) Json object with collateral token information\n"
                },
                RPCExamples{
                    HelpExampleCli("getcollateraltoken", "DFI")
                },
     }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as string for token symbol or id");

    UniValue ret(UniValue::VOBJ);
    std::string tokenSymbol = request.params[0].get_str();
    DCT_ID idToken;

    CImmutableCSView view(*pcustomcsview);

    auto token = view.GetTokenGuessId(trim_ws(tokenSymbol), idToken);
    if (!token)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));

    uint32_t height = view.GetLastHeight();
    CollateralTokenKey start{idToken, height};

    auto collToken = view.HasLoanCollateralToken(start);
    if (collToken && collToken->factor)
    {
        ret.pushKVs(setCollateralTokenToJSON(view, *collToken));
    }

    return (ret);
}


UniValue listcollateraltokens(const JSONRPCRequest& request) {
    RPCHelpMan{"listcollateraltokens",
                "Return list of all created collateral tokens. If no parameters passed it will return all current valid setcollateraltoken transactions.\n",
                {},
                RPCResult
                {
                    "{...}     (object) Json object with collateral token information\n"
                },
                RPCExamples{
                    HelpExampleCli("listcollateraltokens", "")
                },
     }.Check(request);

    UniValue ret(UniValue::VARR);
    CImmutableCSView view(*pcustomcsview);

    view.ForEachLoanCollateralToken([&](CollateralTokenKey const & key, uint256 const & collTokenTx) {
        auto collToken = view.GetLoanCollateralToken(collTokenTx);
        if (collToken)
            ret.push_back(setCollateralTokenToJSON(view, *collToken));

        return true;
    });

    if (!ret.empty()) {
        return ret;
    }

    auto attributes = view.GetAttributes();
    if (!attributes) {
        return ret;
    }

    attributes->ForEach([&](const CDataStructureV0& attr, const CAttributeValue&) {
        if (attr.type != AttributeTypes::Token) {
            return false;
        }
        if (attr.key == TokenKeys::LoanCollateralEnabled) {
            if (auto collToken = view.GetCollateralTokenFromAttributes({attr.typeId})) {
                ret.push_back(setCollateralTokenToJSON(view, *collToken));
            }
        }
        return true;
    }, CDataStructureV0{AttributeTypes::Token});

    return ret;
}

UniValue setloantoken(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"setloantoken",
                "Creates (and submits to local node and network) a token for a price feed set in collateral token.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO, "Token's symbol (unique), not longer than " + std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                            {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token's name (optional), not longer than " + std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                            {"fixedIntervalPriceId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "token/currency pair to use for price of token"},
                            {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Token's 'Mintable' property (bool, optional), default is 'True'"},
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
                        HelpExampleCli("setloantoken", R"('{"symbol":"TSLA","name":"TSLA stock token","fixedIntervalPriceId":"TSLA/USD","interest":"3"}')")
                        },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot setloantoken while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"token\",\"factor\",\"fixedIntervalPriceId\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CLoanSetLoanToken loanToken;

    if (!metaObj["symbol"].isNull())
        loanToken.symbol = trim_ws(metaObj["symbol"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"symbol\" must not be null");

    if (!metaObj["name"].isNull())
        loanToken.name = trim_ws(metaObj["name"].getValStr());

    loanToken.fixedIntervalPriceId = DecodePriceFeedUni(metaObj);

    if (!metaObj["mintable"].isNull())
        loanToken.mintable = metaObj["mintable"].getBool();

    if (!metaObj["interest"].isNull())
        loanToken.interest = AmountFromValue(metaObj["interest"]);
    else
        loanToken.interest = 0;

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::SetLoanToken)
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
    auto pwallet = GetWallet(request);

    RPCHelpMan{"updateloantoken",
                "Creates (and submits to local node and network) a transaction to update loan token metadata.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The tokens's symbol, id or creation tx"},
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"symbol", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "New token's symbol (unique), not longer than " + std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                            {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Newoken's name (optional), not longer than " + std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                            {"fixedIntervalPriceId", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "token/currency pair to use for price of token"},
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
                        HelpExampleCli("updateloantoken", R"("TSLAAA", {"symbol":"TSLA","fixedIntervalPriceId":"TSLA/USD", "mintable": true, "interest": 0.03}')") +
                        HelpExampleRpc("updateloantoken", R"("TSLAAA", {"symbol":"TSLA","fixedIntervalPriceId":"TSLA/USD", "mintable": true, "interest": 0.03})")
                        },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot updateloantoken while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValueType(), UniValue::VOBJ, UniValue::VARR}, true);

    std::string const tokenStr = trim_ws(request.params[0].getValStr());
    UniValue metaObj = request.params[1].get_obj();
    UniValue const & txInputs = request.params[2];

    std::optional<CLoanSetLoanTokenImplementation> loanToken;
    CTokenImplementation tokenImpl;

    int targetHeight;
    {
        DCT_ID id;
        CImmutableCSView view(*pcustomcsview);

        auto token = view.GetTokenGuessId(tokenStr, id);
        if (!token)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));

        if (!token->IsLoanToken())
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is not a loan token! Can't alter other tokens with this tx!", tokenStr));

        if (id == DCT_ID{0})
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't alter DFI token!"));

        loanToken = view.GetLoanTokenByID(id);
        if (!loanToken)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't find %s loan token!", tokenStr));

        targetHeight = view.GetLastHeight() + 1;
    }

    if (!metaObj["symbol"].isNull())
        loanToken->symbol = trim_ws(metaObj["symbol"].getValStr());

    if (!metaObj["name"].isNull())
        loanToken->name = trim_ws(metaObj["name"].getValStr());

    if (!metaObj["fixedIntervalPriceId"].isNull())
        loanToken->fixedIntervalPriceId = DecodePriceFeedUni(metaObj);

    if (!metaObj["mintable"].isNull())
        loanToken->mintable = metaObj["mintable"].getBool();

    if (!metaObj["interest"].isNull())
        loanToken->interest = AmountFromValue(metaObj["interest"]);

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UpdateLoanToken)
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

    UniValue ret(UniValue::VARR);

    CImmutableCSView view(*pcustomcsview);

    view.ForEachLoanToken([&](DCT_ID const & key, CLoanView::CLoanSetLoanTokenImpl loanToken) {
        ret.push_back(setLoanTokenToJSON(view, loanToken,key));
        return true;
    });

    if (!ret.empty()) {
        return ret;
    }

    auto attributes = view.GetAttributes();
    if (!attributes) {
        return ret;
    }

    attributes->ForEach([&](const CDataStructureV0& attr, const CAttributeValue&) {
        if (attr.type != AttributeTypes::Token) {
            return false;
        }
        if (attr.key == TokenKeys::LoanMintingEnabled) {
            auto tokenId = DCT_ID{attr.typeId};
            if (auto loanToken = view.GetLoanTokenFromAttributes(tokenId)) {
                ret.push_back(setLoanTokenToJSON(view, *loanToken, tokenId));
            }
        }
        return true;
    }, CDataStructureV0{AttributeTypes::Token});

    return ret;
}

UniValue getloantoken(const JSONRPCRequest& request)
{
    RPCHelpMan{
        "getloantoken",
        "Return loan token information.\n",
        {
            {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Symbol or id of loan token"},
        },
        RPCResult{
            "{...}     (object) Json object with loan token information\n"},
        RPCExamples{
            HelpExampleCli("getloantoken", "DFI")},
    }
        .Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "Invalid parameters, arguments 1 must be non-null and expected as string for token symbol or id");

    UniValue ret(UniValue::VOBJ);
    std::string tokenSymbol = request.params[0].get_str();
    DCT_ID idToken;

    CImmutableCSView view(*pcustomcsview);

    auto token = view.GetTokenGuessId(trim_ws(tokenSymbol), idToken);
    if (!token)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));

    auto loanToken = view.GetLoanTokenByID(idToken);
    if (!loanToken) {
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("<%s> is not a valid loan token!", tokenSymbol.c_str()));
    }

    return setLoanTokenToJSON(view, *loanToken, idToken);
}

UniValue createloanscheme(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

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

    CLoanSchemeMessage loanScheme;
    loanScheme.ratio = request.params[0].get_int();
    loanScheme.rate = AmountFromValue(request.params[1]);
    loanScheme.identifier = request.params[2].get_str();

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

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
    auto pwallet = GetWallet(request);

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

    CLoanSchemeMessage loanScheme;
    loanScheme.ratio = request.params[0].get_int();
    loanScheme.rate = AmountFromValue(request.params[1]);
    loanScheme.identifier = request.params[2].get_str();

    // Max value is ignored as block height
    loanScheme.updateHeight = std::numeric_limits<uint64_t>::max();
    if (!request.params[3].isNull()) {
        loanScheme.updateHeight = request.params[3].get_int();
    }

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

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
    auto pwallet = GetWallet(request);

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

    CDefaultLoanSchemeMessage defaultScheme;
    defaultScheme.identifier = request.params[0].get_str();

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

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
    auto pwallet = GetWallet(request);

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

    CDestroyLoanSchemeMessage destroyScheme;
    destroyScheme.identifier = request.params[0].get_str();
    if (!request.params[1].isNull()) {
        destroyScheme.destroyHeight = request.params[1].get_int();
    }

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

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
               "List all available loan schemes.\n",
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

    CImmutableCSView view(*pcustomcsview);

    view.ForEachLoanScheme([&loans](const std::string& identifier, const CLoanSchemeData& data){
        CLoanScheme loanScheme;
        loanScheme.rate = data.rate;
        loanScheme.ratio = data.ratio;
        loanScheme.identifier = identifier;
        loans.insert(loanScheme);
        return true;
    });

    auto defaultLoan = view.GetDefaultLoanScheme();

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
               "Returns information about loan scheme.\n",
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

    CImmutableCSView view(*pcustomcsview);

    auto loanScheme = view.GetLoanScheme(loanSchemeId);
    if (!loanScheme)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot find existing loan scheme with id " + loanSchemeId);

    auto defaultLoan = view.GetDefaultLoanScheme();

    UniValue result{UniValue::VOBJ};
    result.pushKV("id", loanSchemeId);
    result.pushKV("mincolratio", static_cast<uint64_t>(loanScheme->ratio));
    result.pushKV("interestrate", ValueFromAmount(loanScheme->rate));
    if (defaultLoan && *defaultLoan == loanSchemeId) {
        result.pushKV("default", true);
    } else {
        result.pushKV("default", false);
    }

    return result;
}

UniValue takeloan(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"takeloan",
                "Creates (and submits to local node and network) a tx to mint loan token in desired amount based on defined loan.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Id of vault used for loan"},
                            {"to", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address to transfer tokens (optional)"},
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
                        HelpExampleCli("takeloan", R"('{"vaultId":84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2,"amounts":"10@TSLA"}')")
                        },
     }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot takeloan while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"vaultId\",\"amounts\"}");

    UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    CLoanTakeLoanMessage takeLoan;

    if (!metaObj["vaultId"].isNull())
        takeLoan.vaultId = uint256S(metaObj["vaultId"].getValStr());
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"vaultId\" must be non-null");

    if (!metaObj["to"].isNull())
        takeLoan.to = DecodeScript(metaObj["to"].getValStr());

    if (!metaObj["amounts"].isNull())
        takeLoan.amounts = DecodeAmounts(pwallet->chain(), metaObj["amounts"], "");
    else
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" must not be null");

    int targetHeight;
    CScript ownerAddress;
    {
        CImmutableCSView view(*pcustomcsview);

        auto vault = view.GetVault(takeLoan.vaultId);
        if (!vault)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Vault <%s> not found", takeLoan.vaultId.GetHex()));

        ownerAddress = vault->ownerAddress;
        targetHeight = view.GetLastHeight() + 1;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::TakeLoan)
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

UniValue paybackloan(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"paybackloan",
                "Creates (and submits to local node and network) a tx to return the loan in desired amount.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Id of vault used for loan"},
                            {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "Address containing repayment tokens. If \"from\" value is: \"*\" (star), it's means auto-selection accounts from wallet."},
                            {"amounts", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Amount in amount@token format."},
                            {"loans", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                                {
                                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                        {
                                            {"dToken", RPCArg::Type::STR, RPCArg::Optional::NO, "The dTokens's symbol, id or creation tx"},
                                            {"amounts", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount in amount@token format."},
                                        },
                                    },
                                },
                            },
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
                        HelpExampleCli("paybackloan", R"('{"vaultId":84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2,"from":"<address>", "amounts":"10@TSLA"}')")
                        },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot paybackloan while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);

    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, argument 1 must be non-null and expected as object at least with "
                           "{\"vaultId\",\"amounts\"}");

    UniValue metaObj = request.params[0].get_obj();

    if (metaObj["vaultId"].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"vaultId\" must be non-null");

    auto vaultId = uint256S(metaObj["vaultId"].getValStr());

    if (metaObj["from"].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"from\" must not be null");

    auto fromStr = metaObj["from"].getValStr();

    // Check amounts or/and loans
    bool hasAmounts = !metaObj["amounts"].isNull();
    bool hasLoans = !metaObj["loans"].isNull();

    CImmutableCSView view(*pcustomcsview);
    int targetHeight = view.GetLastHeight() + 1;

    CBalances amounts;
    std::map<DCT_ID, CBalances> loans;

    if (hasAmounts && hasLoans)
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" and \"loans\" cannot be set at the same time");

    if (hasAmounts)
        amounts = DecodeAmounts(pwallet->chain(), metaObj["amounts"], "");
    else if (targetHeight < Params().GetConsensus().FortCanningRoadHeight)
         throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" must not be null");
    else if (!hasLoans)
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" and \"loans\" cannot be empty at the same time");
    else {
        auto& array = metaObj["loans"].get_array();
        for (size_t i = 0; i < array.size(); i++) {
            auto obj = array[i].get_obj();
            auto tokenStr = trim_ws(obj["dToken"].getValStr());

            DCT_ID id;
            auto token = view.GetTokenGuessId(tokenStr, id);
            if (!token)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));

            if (!token->IsLoanToken())
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is not a loan token!", tokenStr));

            auto loanToken = view.GetLoanTokenByID(id);
            if (!loanToken)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't find %s loan token!", tokenStr));

            loans[id] = DecodeAmounts(pwallet->chain(), obj["amounts"], "");
        }
    }

    CScript from;
    if (fromStr == "*") {
        CBalances balances;
        for (const auto& amounts : loans)
            balances.AddBalances(amounts.second.balances);

        if (loans.empty())
            balances = amounts;

        auto selectedAccounts = SelectAccountsByTargetBalances(GetAllMineAccounts(view, pwallet), balances, SelectionPie);

        for (auto& account : selectedAccounts) {
            auto it = amounts.balances.begin();
            while (it != amounts.balances.end()) {
                if (account.second.balances[it->first] < it->second)
                    break;
                it++;
            }
            if (it == amounts.balances.end()) {
                from = account.first;
                break;
            }
        }

        if (from.empty())
            throw JSONRPCError(RPC_INVALID_REQUEST,
                    "Not enough tokens on account, call sendtokenstoaddress to increase it.\n");
    } else
        from = DecodeScript(metaObj["from"].getValStr());

    if (!::IsMine(*pwallet, from))
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                strprintf("Address (%s) is not owned by the wallet", metaObj["from"].getValStr()));

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);

    if (!hasAmounts)
        metadata << static_cast<unsigned char>(CustomTxType::PaybackLoanV2)
                 << CLoanPaybackLoanV2Message{vaultId, from, loans};
    else
        metadata << static_cast<unsigned char>(CustomTxType::PaybackLoan)
                 << CLoanPaybackLoanMessage{vaultId, from, amounts};

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{from};
    UniValue const & txInputs = request.params[1];
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
                "Returns the loan stats.\n",
                {},
                RPCResult
                {
                    "{...}     (object) Json object with loan information\n"
                },
                RPCExamples{
                    HelpExampleCli("getloaninfo", "")
                },
    }.Check(request);

    UniValue ret{UniValue::VOBJ};

    CImmutableCSView view(*pcustomcsview);
    auto height = view.GetLastHeight();
    auto blockTime = WITH_LOCK(cs_main, return ::ChainActive()[height]->GetBlockTime());

    bool useNextPrice = false, requireLivePrice = true;
    uint64_t totalCollateralValue = 0, totalLoanValue = 0, totalVaults = 0, totalAuctions = 0;

    view.ForEachVault([&](const CVaultId& vaultId, CLazySerialize<CVaultData>) {
        LogPrint(BCLog::LOAN,"getloaninfo()->Vault(%s):\n", vaultId.GetHex());
        auto collaterals = view.GetVaultCollaterals(vaultId);
        if (!collaterals)
            collaterals = CBalances{};
        auto rate = view.GetLoanCollaterals(vaultId, *collaterals, height, blockTime, useNextPrice, requireLivePrice);
        if (rate)
        {
            totalCollateralValue += rate.val->totalCollaterals;
            totalLoanValue += rate.val->totalLoans;
        }
        totalVaults++;
        return true;
    });

    view.ForEachVaultAuction([&](const CVaultId&, const CAuctionData& data) {
        totalAuctions += data.batchCount;
        return true;
    }, height);

    UniValue totalsObj{UniValue::VOBJ};
    auto totalLoanSchemes = static_cast<int>(listloanschemes(request).size());
    auto totalCollateralTokens = static_cast<int>(listcollateraltokens(request).size());

    totalsObj.pushKV("schemes", totalLoanSchemes);
    totalsObj.pushKV("collateralTokens", totalCollateralTokens);
    totalsObj.pushKV("collateralValue", ValueFromUint(totalCollateralValue));
    auto totalLoanTokens = static_cast<int>(listloantokens(request).size());
    totalsObj.pushKV("loanTokens", totalLoanTokens);
    totalsObj.pushKV("loanValue", ValueFromUint(totalLoanValue));
    totalsObj.pushKV("openVaults", totalVaults);
    totalsObj.pushKV("openAuctions", totalAuctions);

    UniValue defaultsObj{UniValue::VOBJ};
    auto defaultScheme = view.GetDefaultLoanScheme();
    if(!defaultScheme)
        defaultsObj.pushKV("scheme", "");
    else
        defaultsObj.pushKV("scheme", *defaultScheme);
    defaultsObj.pushKV("maxPriceDeviationPct", ValueFromUint(view.GetPriceDeviation() * 100));
    auto minLiveOracles = Params().NetworkIDString() == CBaseChainParams::REGTEST ? 1 : 2;
    defaultsObj.pushKV("minOraclesPerPrice", minLiveOracles);
    defaultsObj.pushKV("fixedIntervalBlocks", int(view.GetIntervalBlock()));

    auto priceBlocks = GetFixedIntervalPriceBlocks(height, view);
    ret.pushKV("currentPriceBlock", (int)priceBlocks.first);
    ret.pushKV("nextPriceBlock", (int)priceBlocks.second);
    ret.pushKV("defaults", defaultsObj);
    ret.pushKV("totals", totalsObj);

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
                    "            - `interestPerBlock`: Interest per block is always ceiled\n"
                    "               to the min. unit of fi (8 decimals), however interest\n"
                    "               less than this will continue to accrue until actual utilization\n"
                    "               (eg. - payback of the loan), or until sub-fi maturity."
                    "             - `realizedInterestPerBlock`: The actual realized interest\n"
                    "               per block. This is continues to accumulate until\n"
                    "               the min. unit of the blockchain (fi) can be realized. \n"
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

    CImmutableCSView view(*pcustomcsview);

    if (!view.GetLoanScheme(loanSchemeId))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot find existing loan scheme with id " + loanSchemeId);

    DCT_ID id{~0u};

    if (!tokenStr.empty() && !view.GetTokenGuessId(tokenStr, id))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));

    UniValue ret(UniValue::VARR);
    uint32_t height = view.GetLastHeight() + 1;

    struct CInterestStat {
        std::string token;
        base_uint<128> totalInterest;
        base_uint<128> interestPerBlock;
    };

    std::map<DCT_ID, CInterestStat> interestStats;

    LogPrint(BCLog::LOAN,"%s():\n", __func__);
    auto vaultInterest = [&](const CVaultId& vaultId, DCT_ID tokenId, CInterestRateV2 rate)
    {
        auto vault = view.GetVault(vaultId);
        if (!vault || vault->schemeId != loanSchemeId)
            return true;

        if (id.v != ~0u && id != tokenId)
            return true;

        auto token = view.GetToken(tokenId);
        if (!token)
            return true;

        auto& stat = interestStats[tokenId];
        stat.token = token->CreateSymbolKey(tokenId);
        stat.totalInterest += TotalInterestCalculation(rate, height);
        stat.interestPerBlock += rate.interestPerBlock;

        return true;
    };

    if (static_cast<int>(height) >= Params().GetConsensus().FortCanningHillHeight) {
        view.ForEachVaultInterestV2(vaultInterest);
    } else {
        view.ForEachVaultInterest([&](const CVaultId& vaultId, DCT_ID tokenId, CInterestRate rate) {
            return vaultInterest(vaultId, tokenId, ConvertInterestRateToV2(rate));
        });
    }

    UniValue obj(UniValue::VOBJ);
    for (const auto& [tokenId, stat] : interestStats)
    {
        obj.pushKV("token", stat.token);
        obj.pushKV("totalInterest", ValueFromAmount(CeilInterest(stat.totalInterest, height)));
        obj.pushKV("interestPerBlock", ValueFromAmount(CeilInterest(stat.interestPerBlock, height)));

        if (static_cast<int>(height) >= Params().GetConsensus().FortCanningHillHeight)
        {
            auto realizedInterestStr = GetInterestPerBlockHighPrecisionString(stat.interestPerBlock);
            obj.pushKV("realizedInterestPerBlock", UniValue(UniValue::VNUM, realizedInterestStr));
        }
        ret.push_back(obj);
    }
    return ret;
}


static const CRPCCommand commands[] =
{
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"loan",        "setcollateraltoken",        &setcollateraltoken,    {"metadata", "inputs"}},
    {"loan",        "getcollateraltoken",        &getcollateraltoken,    {"by"}},
    {"loan",        "listcollateraltokens",      &listcollateraltokens,  {"by"}},
    {"loan",        "setloantoken",              &setloantoken,          {"metadata", "inputs"}},
    {"loan",        "updateloantoken",           &updateloantoken,       {"token", "metadata", "inputs"}},
    {"loan",        "listloantokens",            &listloantokens,        {}},
    {"loan",        "getloantoken",              &getloantoken,          {"by"}},
    {"loan",        "createloanscheme",          &createloanscheme,      {"mincolratio", "interestrate", "id", "inputs"}},
    {"loan",        "updateloanscheme",          &updateloanscheme,      {"mincolratio", "interestrate", "id", "ACTIVATE_AFTER_BLOCK", "inputs"}},
    {"loan",        "setdefaultloanscheme",      &setdefaultloanscheme,  {"id", "inputs"}},
    {"loan",        "destroyloanscheme",         &destroyloanscheme,     {"id", "ACTIVATE_AFTER_BLOCK", "inputs"}},
    {"loan",        "listloanschemes",           &listloanschemes,       {}},
    {"loan",        "getloanscheme",             &getloanscheme,         {"id"}},
    {"loan",        "takeloan",                  &takeloan,              {"metadata", "inputs"}},
    {"loan",        "paybackloan",               &paybackloan,           {"metadata", "inputs"}},
    {"loan",        "getloaninfo",               &getloaninfo,           {}},
    {"loan",        "getinterest",               &getinterest,           {"id", "token"}},
};

void RegisterLoanRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
