#include <masternodes/mn_rpc.h>
#include <masternodes/govvariables/attributes.h>
#include <boost/asio.hpp>

extern UniValue tokenToJSON(CCustomCSView& view, DCT_ID const& id, CTokenImplementation const& token, bool verbose);
extern std::pair<int, int> GetFixedIntervalPriceBlocks(int currentHeight, const CCustomCSView &mnview);

UniValue setCollateralTokenToJSON(CCustomCSView& view, CLoanSetCollateralTokenImplementation const& collToken)
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

UniValue setLoanTokenToJSON(CCustomCSView& view, CLoanSetLoanTokenImplementation const& loanToken, DCT_ID tokenId)
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

CTokenCurrencyPair DecodePriceFeedString(const std::string& value){
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

    int targetHeight;
    {
        LOCK(cs_main);

        DCT_ID idToken;

        auto token = pcustomcsview->GetTokenGuessId(tokenSymbol, idToken);
        if (!token)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));
        collToken.idToken = idToken;

        targetHeight = ::ChainActive().Height() + 1;
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

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

    UniValue ret(UniValue::VOBJ);
    std::string tokenSymbol = request.params[0].get_str();
    DCT_ID idToken;

    LOCK(cs_main);

    uint32_t height = ::ChainActive().Height();

    auto token = pcustomcsview->GetTokenGuessId(trim_ws(tokenSymbol), idToken);
    if (!token)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));

    CollateralTokenKey start{idToken, height};

    auto collToken = pcustomcsview->HasLoanCollateralToken(start);
    if (collToken && collToken->factor)
    {
        ret.pushKVs(setCollateralTokenToJSON(*pcustomcsview, *collToken));
    }

    return GetRPCResultCache().Set(request, ret);
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
    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

    UniValue ret(UniValue::VARR);
    CCustomCSView view(*pcustomcsview);

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

    return GetRPCResultCache().Set(request, ret);
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
        loanToken.interest = AmountFromValue(metaObj["interest"], true);
    else
        loanToken.interest = 0;

    int targetHeight;

    {
        LOCK(cs_main);

        targetHeight = ::ChainActive().Height() + 1;
    }

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
    std::optional<CTokenImplementation> token;

    int targetHeight;
    {
        LOCK(cs_main);

        DCT_ID id;
        token = pcustomcsview->GetTokenGuessId(tokenStr, id);
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));
        }
        if (!token->IsLoanToken())
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is not a loan token! Can't alter other tokens with this tx!", tokenStr));
        if (id == DCT_ID{0}) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't alter DFI token!"));
        }
        loanToken = pcustomcsview->GetLoanTokenByID(id);
        if (!loanToken) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't find %s loan token!", tokenStr));
        }

        targetHeight = ::ChainActive().Height() + 1;
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
        loanToken->interest = AmountFromValue(metaObj["interest"], true);

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UpdateLoanToken)
             << static_cast<CLoanSetLoanToken>(*loanToken) << token->creationTx;

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
    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

    UniValue ret(UniValue::VARR);

    CCustomCSView view(*pcustomcsview);

    view.ForEachLoanToken([&](DCT_ID const & key, CLoanView::CLoanSetLoanTokenImpl loanToken) {
        ret.push_back(setLoanTokenToJSON(view, loanToken, key));
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

    return GetRPCResultCache().Set(request, ret);
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
    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "Invalid parameters, arguments 1 must be non-null and expected as string for token symbol or id");

    std::string tokenSymbol = request.params[0].get_str();
    DCT_ID idToken;

    LOCK(cs_main);

    auto token = pcustomcsview->GetTokenGuessId(trim_ws(tokenSymbol), idToken);
    if (!token)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenSymbol));

    auto loanToken = pcustomcsview->GetLoanTokenByID(idToken);
    if (!loanToken) {
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("<%s> is not a valid loan token!", tokenSymbol.c_str()));
    }

    auto res = setLoanTokenToJSON(*pcustomcsview, *loanToken, idToken);
    return GetRPCResultCache().Set(request, res);

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
    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

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

    return GetRPCResultCache().Set(request, ret);
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

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

    if(request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter id, argument must be non-null");

    auto loanSchemeId = request.params[0].getValStr();

    if (loanSchemeId.empty() || loanSchemeId.length() > 8)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "id cannot be empty or more than 8 chars long");

    LOCK(cs_main);
    auto loanScheme = pcustomcsview->GetLoanScheme(loanSchemeId);
    if (!loanScheme)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot find existing loan scheme with id " + loanSchemeId);

    auto defaultLoan = pcustomcsview->GetDefaultLoanScheme();

    UniValue result{UniValue::VOBJ};
    result.pushKV("id", loanSchemeId);
    result.pushKV("mincolratio", static_cast<uint64_t>(loanScheme->ratio));
    result.pushKV("interestrate", ValueFromAmount(loanScheme->rate));
    if (defaultLoan && *defaultLoan == loanSchemeId) {
        result.pushKV("default", true);
    } else {
        result.pushKV("default", false);
    }

    return GetRPCResultCache().Set(request, result);
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
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
        auto vault = pcustomcsview->GetVault(takeLoan.vaultId);
        if (!vault)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Vault <%s> not found", takeLoan.vaultId.GetHex()));
        ownerAddress = vault->ownerAddress;
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
    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }
    bool isFCR = targetHeight >= Params().GetConsensus().FortCanningRoadHeight;
    CBalances amounts;
    if (hasAmounts){
        if(hasLoans)
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" and \"loans\" cannot be set at the same time");
        else
            amounts = DecodeAmounts(pwallet->chain(), metaObj["amounts"], "");
    }
    else if(!isFCR)
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" must not be null");
    else if(!hasLoans)
        throw JSONRPCError(RPC_INVALID_PARAMETER,"Invalid parameters, argument \"amounts\" and \"loans\" cannot be empty at the same time");

    std::map<DCT_ID, CBalances> loans;
    UniValue array {UniValue::VARR};
    if(hasLoans) {
        try {
            array = metaObj["loans"].get_array();
            for (unsigned int i=0; i<array.size(); i++){
                auto obj = array[i].get_obj();
                auto tokenStr = trim_ws(obj["dToken"].getValStr());

                DCT_ID id;
                auto token = pcustomcsview->GetTokenGuessId(tokenStr, id);
                if (!token)
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));

                if (!token->IsLoanToken())
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is not a loan token!", tokenStr));

                auto loanToken = pcustomcsview->GetLoanTokenByID(id);
                if (!loanToken)
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't find %s loan token!", tokenStr));
                loans[id] = DecodeAmounts(pwallet->chain(), obj["amounts"], "");
            }
        }catch(std::runtime_error& e) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, e.what());
        }
    }

    CScript from;
    if (fromStr == "*") {
        CBalances balances;
        for (const auto& amounts : loans)
            balances.AddBalances(amounts.second.balances);

        if (loans.empty())
            balances = amounts;

        auto selectedAccounts = SelectAccountsByTargetBalances(GetAllMineAccounts(pwallet), balances, SelectionPie);

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

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;
    UniValue ret{UniValue::VOBJ};

    LOCK(cs_main);
    auto view = *pcustomcsview;

    auto height = ::ChainActive().Height() + 1;

    bool useNextPrice = false, requireLivePrice = true;
    auto lastBlockTime = ::ChainActive().Tip()->GetBlockTime();

    uint64_t totalCollateralValue = 0, totalLoanValue = 0,
             totalVaults = 0, totalAuctions = 0, totalLoanSchemes = 0,
             totalCollateralTokens = 0, totalLoanTokens = 0;

    auto fixedIntervalBlock = view.GetIntervalBlock();
    auto priceDeviation = view.GetPriceDeviation();
    auto defaultScheme = view.GetDefaultLoanScheme();
    auto priceBlocks = GetFixedIntervalPriceBlocks(::ChainActive().Height(), view);

    // TODO: Later optimize this into a general dynamic worker pool, so we don't
    // need to recreate these threads on each call.
    boost::asio::thread_pool workerPool{[]() {
        const size_t workersMax = GetNumCores() - 1;
        // More than 8 is likely not very fruitful for ~10k vaults.
        return std::min(workersMax > 2 ? workersMax : 3, static_cast<size_t>(8));
    }()};

    boost::asio::post(workerPool, [&] {
        view.ForEachLoanScheme([&](const std::string& identifier, const CLoanSchemeData& data) {
            totalLoanSchemes++;
            return true;
        });

        // First assume it's on the DB. For later, might be worth thinking if it's better to incorporate
        // attributes right into the for each loop, so the interface remains consistent.
        view.ForEachLoanCollateralToken([&](CollateralTokenKey const& key, uint256 const& collTokenTx) {
            totalCollateralTokens++;
            return true;
        });

        view.ForEachLoanToken([&](DCT_ID const& key, CLoanView::CLoanSetLoanTokenImpl loanToken) {
            totalLoanTokens++;
            return true;
        });

        // Now, let's go over attributes. If it's on attributes, the above calls would have done nothing.
        auto attributes = view.GetAttributes();
        if (!attributes) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "attributes access failure");
        }

        attributes->ForEach([&](const CDataStructureV0& attr, const CAttributeValue&) {
            if (attr.type != AttributeTypes::Token)
                return false;
            if (attr.key == TokenKeys::LoanCollateralEnabled)
                totalCollateralTokens++;
            else if (attr.key == TokenKeys::LoanMintingEnabled)
                totalLoanTokens++;
            return true;
        }, CDataStructureV0{AttributeTypes::Token});

        view.ForEachVaultAuction([&](const CVaultId& vaultId, const CAuctionData& data) {
            totalAuctions += data.batchCount;
            return true;
        }, height);
    });

    std::atomic<uint64_t> vaultsTotal{0};
    std::atomic<uint64_t> colsValTotal{0};
    std::atomic<uint64_t> loansValTotal{0};

    view.ForEachVault([&](const CVaultId& vaultId, const CVaultData& data) {
        boost::asio::post(workerPool, [&, &colsValTotal=colsValTotal,
            &loansValTotal=loansValTotal, &vaultsTotal=vaultsTotal,
            vaultId=vaultId, height=height, useNextPrice=useNextPrice,
            requireLivePrice=requireLivePrice] {
            auto collaterals = view.GetVaultCollaterals(vaultId);
            if (!collaterals)
                collaterals = CBalances{};
            auto rate = view.GetLoanCollaterals(vaultId, *collaterals, height, lastBlockTime, useNextPrice, requireLivePrice);
            if (rate)
            {
                colsValTotal.fetch_add(rate.val->totalCollaterals, std::memory_order_relaxed);
                loansValTotal.fetch_add(rate.val->totalLoans, std::memory_order_relaxed);
            }
            vaultsTotal.fetch_add(1, std::memory_order_relaxed);
        });
        return true;
    });

    workerPool.join();
    // We use relaxed ordering to increment. Thread joins should in theory,
    // resolve have resulted in full barriers, but we ensure
    // to throw in a full barrier anyway. x86 arch might appear to work without
    // but let's be extra cautious about RISC optimizers.
    totalVaults = vaultsTotal.load();
    totalLoanValue = loansValTotal.load();
    totalCollateralValue = colsValTotal.load();

    UniValue totalsObj{UniValue::VOBJ};

    totalsObj.pushKV("schemes", totalLoanSchemes);
    totalsObj.pushKV("collateralTokens", totalCollateralTokens);
    totalsObj.pushKV("collateralValue", ValueFromUint(totalCollateralValue));
    totalsObj.pushKV("loanTokens", totalLoanTokens);
    totalsObj.pushKV("loanValue", ValueFromUint(totalLoanValue));
    totalsObj.pushKV("openVaults", totalVaults);
    totalsObj.pushKV("openAuctions", totalAuctions);
    UniValue defaultsObj{UniValue::VOBJ};
    if(!defaultScheme)
        defaultsObj.pushKV("scheme", "");
    else
        defaultsObj.pushKV("scheme", *defaultScheme);
    defaultsObj.pushKV("maxPriceDeviationPct", ValueFromUint(priceDeviation * 100));
    auto minLiveOracles = Params().NetworkIDString() == CBaseChainParams::REGTEST ? 1 : 2;
    defaultsObj.pushKV("minOraclesPerPrice", minLiveOracles);
    defaultsObj.pushKV("fixedIntervalBlocks", int(fixedIntervalBlock));
    ret.pushKV("currentPriceBlock", (int)priceBlocks.first);
    ret.pushKV("nextPriceBlock", (int)priceBlocks.second);
    ret.pushKV("defaults", defaultsObj);
    ret.pushKV("totals", totalsObj);

    return GetRPCResultCache().Set(request, ret);
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

    if (auto res = GetRPCResultCache().TryGet(request)) return *res;

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValueType()}, false);

    auto loanSchemeId = request.params[0].get_str();
    auto tokenStr = trim_ws(request.params[1].getValStr());

    if (loanSchemeId.empty() || loanSchemeId.length() > 8)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "id cannot be empty or more than 8 chars long");

    LOCK(cs_main);

    const auto scheme = pcustomcsview->GetLoanScheme(loanSchemeId);
    if (!scheme)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot find existing loan scheme with id " + loanSchemeId);

    DCT_ID id{~0u};

    if (!tokenStr.empty() && !pcustomcsview->GetTokenGuessId(tokenStr, id))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));

    UniValue ret(UniValue::VARR);
    const auto height = ::ChainActive().Height() + 1;

    std::map<DCT_ID, std::pair<CInterestAmount, CInterestAmount>> interest;

    auto vaultInterest = [&](const CVaultId& vaultId, const DCT_ID tokenId, const CInterestRateV3 &rate)
    {
        auto vault = pcustomcsview->GetVault(vaultId);
        if (!vault || vault->schemeId != loanSchemeId)
            return true;
        if ((id != DCT_ID{~0U}) && tokenId != id)
            return true;

        auto& [cumulativeInterest, interestPerBlock] = interest[tokenId];

        auto token = pcustomcsview->GetToken(tokenId);
        if (!token)
            return true;

        const auto totalInterest = TotalInterestCalculation(rate, height);
        cumulativeInterest = InterestAddition(cumulativeInterest, totalInterest);
        interestPerBlock = InterestAddition(interestPerBlock, rate.interestPerBlock);

        return true;
    };

    if (height >= Params().GetConsensus().FortCanningGreatWorldHeight) {
        pcustomcsview->ForEachVaultInterestV3(vaultInterest);
    } else if (height >= Params().GetConsensus().FortCanningHillHeight) {
        pcustomcsview->ForEachVaultInterestV2([&](const CVaultId& vaultId, DCT_ID tokenId, const CInterestRateV2 &rate) {
            return vaultInterest(vaultId, tokenId, ConvertInterestRateToV3(rate));
        });
    } else {
        pcustomcsview->ForEachVaultInterest([&](const CVaultId& vaultId, DCT_ID tokenId, const CInterestRate &rate) {
            return vaultInterest(vaultId, tokenId, ConvertInterestRateToV3(rate));
        });
    }

    UniValue obj(UniValue::VOBJ);
    for (auto it = interest.begin(); it != interest.end(); ++it)
    {
        const auto& tokenId = it->first;
        const auto& [cumulativeInterest, totalInterestPerBlock] = it->second;

        const auto totalInterest = cumulativeInterest.negative ? -CeilInterest(cumulativeInterest.amount, height) : CeilInterest(cumulativeInterest.amount, height);
        const auto interestPerBlock = totalInterestPerBlock.negative ? -CeilInterest(totalInterestPerBlock.amount, height) : CeilInterest(totalInterestPerBlock.amount, height);

        const auto token = pcustomcsview->GetToken(tokenId);
        obj.pushKV("token", token->CreateSymbolKey(tokenId));
        obj.pushKV("totalInterest", ValueFromAmount(totalInterest));
        obj.pushKV("interestPerBlock", ValueFromAmount(interestPerBlock));
        if (height >= Params().GetConsensus().FortCanningHillHeight)
        {
            obj.pushKV("realizedInterestPerBlock", UniValue(UniValue::VNUM, GetInterestPerBlockHighPrecisionString(totalInterestPerBlock)));
        }
        ret.push_back(obj);
    }
    return GetRPCResultCache().Set(request, ret);
}

UniValue paybackwithcollateral(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"paybackwithcollateral",
               "Payback vault's loans with vault's collaterals.\n",
               {
                       {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault hex id"},
               },
                RPCResult{
                        "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
               RPCExamples{
                       HelpExampleCli("paybackwithcollateral", R"(5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf)") +
                       HelpExampleRpc("paybackwithcollateral", R"(5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf)")
               },
    }.Check(request);

    const auto vaultId = ParseHashV(request.params[0], "vaultId");
    CPaybackWithCollateralMessage msg{vaultId};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::PaybackWithCollateral)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight;
    CScript ownerAddress;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
        // decode vaultId
        auto vault = pcustomcsview->GetVault(vaultId);
        if (!vault)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> not found", vaultId.GetHex()));

        ownerAddress = vault->ownerAddress;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue const & txInputs = request.params[3];

    CTransactionRef optAuthTx;
    std::set<CScript> auths{ownerAddress};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

     // Set change to from address
    CTxDestination dest;
    ExtractDestination(ownerAddress, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
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
    {"vault",       "paybackwithcollateral",     &paybackwithcollateral, {"vaultId"}},
    {"loan",        "getloaninfo",               &getloaninfo,           {}},
    {"loan",        "getinterest",               &getinterest,           {"id", "token"}},
};

void RegisterLoanRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
