// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_rpc.h>
#include <masternodes/tokenpriceiterator.h>

namespace {
    TokenCurrencyPair DecodeSingleTokenCurrencyPair(const UniValue& uni, interfaces::Chain& chain) {
        if (!uni.exists(oraclefields::Currency)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s is required field", oraclefields::Currency).msg);
        }
        if (!uni.exists(oraclefields::Token)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s is required field", oraclefields::Token).msg);
        }

        auto currencyString = uni[oraclefields::Currency].getValStr();
        auto cid = CURRENCY_ID::FromString(currencyString);
        if (!cid.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               Res::Err("%s <%s> is not supported", oraclefields::Currency, currencyString).msg);
        }

        auto tokenStr = uni[oraclefields::Token].getValStr();
        DCT_ID tid{};

        std::unique_ptr<CToken> tokenPtr = chain.existTokenGuessId(tokenStr, tid);
        if (!tokenPtr) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, Res::Err("Invalid Defi token: %s", tokenStr).msg);
        }

        return {tid, cid};
    }

    std::set<TokenCurrencyPair> DecodeTokenCurrencyPairs(const std::string& data, interfaces::Chain& chain) {
        UniValue array{UniValue::VARR};
        if (!array.read(data)) {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "failed to decode token-currency pairs");
        }

        if (!array.isArray()) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "data is not array");
        }

        std::set<TokenCurrencyPair> pairs{};

        for (const auto &pairUni : array.get_array().getValues()) {
            CURRENCY_ID cid{};
            DCT_ID tid{};
            auto p = DecodeSingleTokenCurrencyPair(pairUni, chain);
            pairs.insert(p);
        }

        return pairs;
    }

    ResVal<std::string> FormatToken(const CCustomCSView& tokensView, DCT_ID tid) {
        auto tokenPtr = tokensView.GetToken(tid);
        if (!tokenPtr) {
            return Res::Err("token %s doesn't exist", tid.ToString());
        }

        std::string value;
        if (tokenPtr->IsDAT()) {
            value = tokenPtr->symbol;
        } else {
            value = tokenPtr->symbol + "#" + tid.ToString();
        }

        return {value, Res::Ok()};
    }
}

UniValue appointoracle(const JSONRPCRequest &request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"appointoracle",
               "\nCreates (and submits to local node and network) a `appoint oracle transaction`, \n"
               "and saves oracle to database.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "oracle address",},
                       {"pricefeeds", RPCArg::Type::STR, RPCArg::Optional::NO, "list of allowed token-currency pairs"},
                       {"weightage", RPCArg::Type::NUM, RPCArg::Optional::NO, "oracle weightage"},
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
                       HelpExampleCli(
                               "appointoracle",
                               R"(mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF '[{"currency": "USD", "token": "BTC#1"}, {"currency": "EUR", "token":"ETH#2"]}' 20)")
                       + HelpExampleRpc(
                               "appointoracle",
                               R"(mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF '[{"currency": "USD", "token": "BTC#1"}, {"currency": "EUR", "token":"ETH#2"]}' 20)")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VSTR, UniValue::VNUM}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode
    std::string address = request.params[0].getValStr();
    CScript script{};
    try {
        script = DecodeScript(address);
    } catch(...) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse address");
    }

    std::string allowedPairsStr = request.params[1].getValStr();
    auto allowedPairs = DecodeTokenCurrencyPairs(allowedPairsStr, pwallet->chain());

    uint32_t weightage{};
    try {
        weightage = std::stoul(request.params[2].getValStr());
    } catch (...) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "failed to decode weightage");
    }

    if (weightage > oraclefields::MaxWeightage || weightage < oraclefields::MinWeightage) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "the weightage value is out of bounds");
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    CAppointOracleMessage msg{std::move(script), static_cast<uint8_t>(weightage), std::move(allowedPairs)};
    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AppointOracle)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const &txInputs = request.params[3];
    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(
            pwallet,
            rawTx.nVersion,
            auths,
            true,
            optAuthTx,
            txInputs);

    CCoinControl coinControl;

    // Set change to auth address if there's only one auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    std::string oracles;
    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyAppointOracleTx(
                mnview,
                coinview,
                CTransaction(rawTx),
                targetHeight,
                ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}),
                Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }


    UniValue result = signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();

    return result;
}

UniValue updateoracle(const JSONRPCRequest& request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"updateoracle",
               "\nCreates (and submits to local node and network) a `update oracle transaction`, \n"
               "and saves oracle updates to database.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"oracleid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "oracle id"},
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "oracle address",},
                       {"pricefeeds", RPCArg::Type::STR, RPCArg::Optional::NO, "list of allowed token-currency pairs"},
                       {"weightage", RPCArg::Type::NUM, RPCArg::Optional::NO, "oracle weightage"},
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
                       HelpExampleCli(
                               "updateoracle",
                               R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF '[{"currency": "USD", "token": "BTC#1"}, {"currency": "EUR", "token":"ETH#2"]}' 20)")
                       + HelpExampleRpc(
                               "updateoracle",
                               R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF '[{"currency": "USD", "token": "BTC#1"}, {"currency": "EUR", "token":"ETH#2"]}' 20)")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VNUM},
                 false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode oracleid
    COracleId oracleId{};
    if (!oracleId.parseHex(request.params[0].getValStr())) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse oracle id");
    }

    // decode address
    std::string address = request.params[1].getValStr();
    CScript script{};
    try {
        script = DecodeScript(address);
    } catch(...) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse address");
    }

    // decode allowed token-currency pairs
    std::string allowedPairsStr = request.params[2].getValStr();
    auto allowedPairs = DecodeTokenCurrencyPairs(allowedPairsStr, pwallet->chain());

    // decode weightage
    uint32_t weightage{};
    try {
        weightage = std::stoul(request.params[3].getValStr());
    } catch (...) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "failed to decode weightage");
    }

    if (weightage > oraclefields::MaxWeightage || weightage < oraclefields::MinWeightage) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "the weightage value is out of bounds");
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    CUpdateOracleAppointMessage msg{
            oracleId,
            CAppointOracleMessage{std::move(script), static_cast<uint8_t>(weightage), std::move(allowedPairs)}
    };

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::UpdateOracleAppoint)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const &txInputs = request.params[4];
    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(
            pwallet,
            rawTx.nVersion,
            auths,
            true,
            optAuthTx,
            txInputs);

    CCoinControl coinControl;//    std::string oracles;

    // Set change to auth address if there's only one auth address
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
        CCustomCSView mnview(*pcustomcsview); // don't write into actual DB
        // check if tx is valid
        auto oracles = mnview.GetAllOracleIds();
        if (std::find(oracles.begin(), oracles.end(), msg.oracleId) == std::end(oracles)) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                               Res::Err("Oracle <%s> doesn't exist\n", msg.oracleId.GetHex()).msg);
        }

        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyUpdateOracleAppointTx(
                mnview,
                coinview,
                CTransaction(rawTx),
                targetHeight,
                ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}),
                Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    UniValue result = signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();

    return result;
}

UniValue removeoracle(const JSONRPCRequest& request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"removeoracle",
               "\nRemoves oracle, \n"
               "The only argument is oracleid hex value." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"oracleid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "oracle id"},
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
                       HelpExampleCli("removeoracle", "0xabcd1234ac1243578697085986498694")
                       + HelpExampleRpc("removeoracle", "0xabcd1234ac1243578697085986498694")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode
    std::string oracleIdStr = request.params[0].getValStr();
    CRemoveOracleAppointMessage msg{};
    if (!msg.oracleId.parseHex(oracleIdStr)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse oracle id");
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::RemoveOracleAppoint)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const &txInputs = request.params[1];
    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, txInputs);

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
        CCustomCSView mnview(*pcustomcsview); // don't write into actual DB
        // check if tx is valid
        auto oracles = mnview.GetAllOracleIds();
        if (std::find(oracles.begin(), oracles.end(), msg.oracleId) == std::end(oracles)) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                               Res::Err("Oracle <%s> doesn't exist\n", msg.oracleId.GetHex()).msg);
        }

        CCoinsViewCache coinView(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinView, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        const auto res = ApplyRemoveOracleAppointTx(
                mnview,
                coinView,
                CTransaction(rawTx),
                targetHeight,
                metadata,
                Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    UniValue result = signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();

    return result;
}

UniValue setoracledata(const JSONRPCRequest &request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"setoracledata",
               "\nCreates (and submits to local node and network) a `set oracle data transaction`.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"oracleid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "oracle hex id",},
                       {"timestamp", RPCArg::Type::NUM, RPCArg::Optional::NO, "balances timestamp",},
                       {"prices", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "tokens raw prices:the array of price and token strings in price@token#number_id format. ",
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
                       HelpExampleCli(
                               "setoracledata",
                               "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf 1612237937 "
                               R"('[{"currency":"USD", "tokenAmount":"38293.12@BTC#1"}"
                               ", {currency:"EUR", "tokenAmount":"1328.32@ETH"}]')"
                       )
                       + HelpExampleRpc(
                               "setoracledata",
                               "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf 1612237937 "
                               R"('[{"currency":"USD", "tokenAmount":"38293.12@BTC#1"}"
                               ", {currency:"EUR", "tokenAmount":"1328.32@ETH"}]')"
                       )
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VNUM, UniValue::VSTR},
                 false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode oracle id
    COracleId oracleId{};
    if (!oracleId.parseHex(request.params[0].getValStr())) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse oracle id");
    }
    // decode timestamp
    int64_t timestamp{};
    try {
        timestamp = std::stoll(request.params[1].getValStr());
    } catch (...) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "failed to decode timestamp");
    }

    if (0 == timestamp) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "timestamp cannot be ze");
    }
    // decode prices
    UniValue prices{UniValue::VARR};
    if (!prices.read(request.params[2].getValStr())) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "failed to decode prices");
    }

    if (!prices.isArray()) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "data is not array");
    }

    CMutableTransaction rawTx{};
    CTransactionRef optAuthTx;

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());

        auto &chain = pwallet->chain();
        auto lock = chain.lock();

        auto parseDataItem = [&chain](const UniValue &uni) -> std::pair<CURRENCY_ID, CTokenAmount> {
            if (!uni.exists(oraclefields::Currency)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s is required field", oraclefields::Currency).msg);
            }
            if (!uni.exists(oraclefields::TokenAmount)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s is required field", oraclefields::TokenAmount).msg);
            }

            auto currencyString = uni[oraclefields::Currency].getValStr();
            auto currency = CURRENCY_ID::FromString(currencyString);
            if (!currency.IsValid()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   Res::Err("%s <%s> is not supported", oraclefields::Currency, currencyString).msg);
            }

            auto amountUni = uni[oraclefields::TokenAmount].getValStr();
            CTokenAmount tokenAmount{};
            try {
                // TODO: remove workaround when (if) the underlying problem is fixed
                // the problem is that thrown exception has code 0, which is not correct
                // fixing it in-place potentially can ruin some tests, so just a workaround for now
                tokenAmount = DecodeAmount(chain, amountUni, "");
            } catch (const UniValue& error) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, error["message"].get_str());
            }

            return std::make_pair(currency, tokenAmount);
        };

        CTokenPrices tokenPrices{};

        for (const auto &priceUni : prices.get_array().getValues()) {
            CURRENCY_ID cid{};
            CTokenAmount tokenAmount{};
            std::tie(cid, tokenAmount) = parseDataItem(priceUni);
            tokenPrices[tokenAmount.nTokenId][cid] = tokenAmount.nValue;
        }

        CSetOracleDataMessage msg{oracleId, timestamp, std::move(tokenPrices)};

        // check if tx parameters are valid

        auto oracleRes = mnview.GetOracleData(oracleId);
        if (!oracleRes.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, oracleRes.msg);
        }

        auto &oracle = *oracleRes.val;
        for (auto &tpair: msg.tokenPrices) {
            DCT_ID tid = tpair.first;
            for (auto &cpair: tpair.second) {
                CURRENCY_ID cid = cpair.first;
                if (!oracle.SupportsPair(tid, cid)) {
                    throw JSONRPCError(RPC_INVALID_REQUEST,
                                       Res::Err("token-currency pair <%s>:<%s> is not supported",
                                                tid.ToString(),
                                                cid.ToString()).msg);
                }
            }
        }

        // encode
        CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        markedMetadata << static_cast<unsigned char>(CustomTxType::SetOracleData)
                       << msg;

        CScript scriptMeta;
        scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

        int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;
        const auto txVersion = GetTransactionVersion(targetHeight);
        rawTx = CMutableTransaction(txVersion);
        rawTx.vout.emplace_back(0, scriptMeta);

        UniValue const &txInputs = request.params[3];

        std::set<CScript> auths;
        auths.insert({oracleRes.val->oracleAddress});

        rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

        CCoinControl coinControl;

        // Set change to auth address if there's only one auth address
        if (auths.size() == 1) {
            CTxDestination dest;
            ExtractDestination(*auths.cbegin(), dest);
            if (IsValidDestination(dest)) {
                coinControl.destChange = dest;
            }
        }

        fund(rawTx, pwallet, optAuthTx, &coinControl);

        if (nullptr != optAuthTx) {
            AddCoins(coinview, *optAuthTx, targetHeight);
        }

        const auto res = ApplySetOracleDataTx(
                mnview,
                coinview,
                CTransaction(rawTx),
                targetHeight,
                ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}),
                Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    UniValue result = signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();

    return result;
}

namespace {
    UniValue PriceFeedToJSON(CCustomCSView& view, const TokenCurrencyPair& priceFeed) {
        UniValue pair(UniValue::VOBJ);
        auto tokenRes = FormatToken(view, priceFeed.tid);
        if (!tokenRes.ok) {
            throw JSONRPCError(tokenRes.code, tokenRes.msg);
        }
        pair.pushKV(oraclefields::Token, *tokenRes.val);
        pair.pushKV(oraclefields::Currency, priceFeed.cid.ToString());
        return pair;
    }

    std::string AmountToString(CAmount amount) {
        const bool sign = amount < 0;
        const int64_t n_abs = (sign ? -amount : amount);
        const int64_t quotient = n_abs / COIN;
        const int64_t remainder = n_abs % COIN;
        return strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder);
    }

    UniValue OracleToJSON(CCustomCSView& view, const COracle& oracle) {
        UniValue result{UniValue::VOBJ};
        result.pushKV(oraclefields::Weightage, oracle.weightage);
        result.pushKV(oraclefields::OracleId, oracle.oracleId.GetHex());
        result.pushKV(oraclefields::OracleAddress, oracle.oracleAddress.GetHex());

        UniValue priceFeeds{UniValue::VARR};
        std::for_each(oracle.availablePairs.begin(), oracle.availablePairs.end(),
                      [&priceFeeds, &view](const TokenCurrencyPair& feed) {
            priceFeeds.push_back(PriceFeedToJSON(view, feed));
        });

        result.pushKV(oraclefields::PriceFeeds, priceFeeds);

        UniValue tokenPrices{UniValue::VARR};
        for (auto& tp: oracle.tokenPrices) {
            auto tid = tp.first;
            const auto& map = tp.second;
            for (const auto& cp: map) {
                auto cid = cp.first;
                const auto &pricePoint = cp.second;
                const auto& amount = pricePoint.first;
                uint64_t timestamp = pricePoint.second;

                UniValue item{UniValue::VOBJ};
                auto resval = FormatToken(view, tid);
                if (!resval.ok) {
                    throw JSONRPCError(resval.code, resval.msg);
                }
                item.pushKV(oraclefields::Token, *resval.val);
                item.pushKV(oraclefields::Currency, cid.ToString());
                item.pushKV(oraclefields::Amount, AmountToString(amount));
                item.pushKV(oraclefields::Timestamp, timestamp);
                tokenPrices.push_back(item);
            }
        }

        result.pushKV(oraclefields::TokenPrices, tokenPrices);

        return result;
    }
}

UniValue getoracledata(const JSONRPCRequest &request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"getoracledata",
               "\nReturns oracle data in json form.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"oracleid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "oracle hex id",},
               },
               RPCResult{
                       "\"json\"                  (string) oracle data in json form\n"
               },
               RPCExamples{
                       HelpExampleCli(
                               "getoracledata", "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf")
                       + HelpExampleRpc(
                               "getoracledata", "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf"
                       )
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode oracle id
    COracleId oracleId{};
    if (!oracleId.parseHex(request.params[0].getValStr())) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse oracle id");
    }

    LOCK(cs_main);
    CCustomCSView mnview(*pcustomcsview); // don't write into actual DB
    CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());

    auto &chain = pwallet->chain();
    auto lock = chain.lock();

    auto oracleRes = mnview.GetOracleData(oracleId);
    if (!oracleRes.ok) {
        throw JSONRPCError(RPC_DATABASE_ERROR, oracleRes.msg);
    }

    auto& oracle = *oracleRes.val;

    UniValue result = OracleToJSON(mnview, oracle);

    return result;
}

UniValue listoracles(const JSONRPCRequest &request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"listoracles",
               "\nReturns list of oracle ids." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
               },
               RPCResult{
                       "\"hash\"                  (string) list of known oracle ids\n"
               },
               RPCExamples{
                       HelpExampleCli("listoracles", "") + HelpExampleRpc("listoracles", "")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);
    LOCK(cs_main);

    CCustomCSView view(*pcustomcsview); // don't write into actual DB
    auto oracles = view.GetAllOracleIds();

    UniValue value(UniValue::VARR);
    for (auto &v: oracles)
        value.push_back(v.GetHex());

    return value;
}

UniValue listlatestrawprices(const JSONRPCRequest &request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"listlatestrawprices",
               "\nReturns latest raw price updates through all the oracles for specified token and currency , \n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"request", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                        "request in json-form, containing currency and token names"},
               },
               RPCResult{
                       "\"json\"                  (string) Array of json objects containing full information about token prices\n"
               },
               RPCExamples{
                       HelpExampleCli("listlatestrawprices",
                                      R"(listlatestrawprices '{"currency": "USD", "token": "BTC#1"}')")
                       + HelpExampleRpc("listlatestrawprices",
                                        R"(listlatestrawprices '{"currency": "USD", "token": "BTC#1"}')")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    bool useAllFeeds = false;
    UniValue data{UniValue::VOBJ};

    if (request.params.empty()) {
        useAllFeeds = true;
    } else if (!data.read(request.params[0].getValStr())) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to read input json");
    }

    LOCK(cs_main);

    CCustomCSView mnview(*pcustomcsview);

    const auto oracleIds = mnview.GetAllOracleIds();

    auto &chain = pwallet->chain();
    auto lock = chain.lock();

    boost::optional<TokenCurrencyPair> optParams{};
    if (!useAllFeeds) {
        const auto &currency = data[oraclefields::Currency].getValStr();
        const auto &token = data[oraclefields::Token].getValStr();

        // get and check token
        DCT_ID tokenId{};
        auto tokenPtr = chain.existTokenGuessId(token, tokenId);
        if (!tokenPtr) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, Res::Err("Invalid Defi token: <%s>", token).msg);
        }

        CURRENCY_ID currencyId = CURRENCY_ID::FromString(currency);
        if (!currencyId.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("Currency <%s> is not supported", currency).msg);
        }

        optParams = TokenCurrencyPair{tokenId, currencyId};
    }

    auto optHeight = lock->getHeight();
    int lastHeight = optHeight.is_initialized() ? *optHeight : 0;
    auto lastBlockTime = lock->getBlockTime(lastHeight);

    auto iterator = TokenPriceIterator(mnview, lastBlockTime);
    UniValue result(UniValue::VARR);
    iterator.ForEach(
            [&result, &mnview](
                    const COracleId &oracleId,
                    DCT_ID tokenId,
                    CURRENCY_ID currencyId,
                    int64_t oracleTime,
                    CAmount rawPrice,
                    uint8_t weightage,
                    OracleState oracleState) -> Res {
                UniValue value{UniValue::VOBJ};
                value.pushKV(oraclefields::Currency, currencyId.ToString());
                auto tokenRes = FormatToken(mnview, tokenId);
                if (!tokenRes.ok) {
                    return tokenRes;
                }
                value.pushKV(oraclefields::Token, *tokenRes.val);
                value.pushKV(oraclefields::OracleId, oracleId.GetHex());
                value.pushKV(oraclefields::Weightage, weightage);
                value.pushKV(oraclefields::Timestamp, oracleTime);
                value.pushKV(oraclefields::RawPrice, ValueFromAmount(rawPrice));

                auto state = oracleState == OracleState::ALIVE ? oraclefields::Alive : oraclefields::Expired;
                value.pushKV(oraclefields::State, state);
                result.push_back(value);
                return Res::Ok();
            }, optParams);

    return result;
}

UniValue getprice(const JSONRPCRequest &request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"getprice",
               "\nCalculates aggregated price, \n"
               "The only argument is a json-form request containing token and currency names." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"request", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "request in json-form, containing currency and token names, both are mandatory"},
               },
               RPCResult{
                       "\"string\"                  (string) aggregated price if\n"
                       "                        if no live oracles which meet specified request or their weights are zero, throws error\n"
               },
               RPCExamples{
                       HelpExampleCli("getprice", R"(getprice '{"currency": "USD", "token": "BTC#1"}')")
                       + HelpExampleRpc("getprice", R"(getprice '{"currency": "USD", "token": "BTC#1"}')")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);

    UniValue data{};
    if (!data.read(request.params[0].getValStr())) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to read input json");
    }

    if (!data.exists(oraclefields::Currency)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "currency name not specified");
    }
    if (!data.exists(oraclefields::Token)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "token name not specified");
    }

    const auto &currency = data[oraclefields::Currency].getValStr();
    const auto &token = data[oraclefields::Token].getValStr();

    LOCK(cs_main);

    CCustomCSView view(*pcustomcsview);

    auto &chain = pwallet->chain();
    auto lock = chain.lock();

    DCT_ID tokenId{};
    auto tokenPtr = chain.existTokenGuessId(token, tokenId);
    if (!tokenPtr) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, Res::Err("Invalid Defi token: %s", token).msg);
    }

    CURRENCY_ID currencyId = CURRENCY_ID::INVALID();
    currencyId = CURRENCY_ID::FromString(currency);
    if (!currencyId.IsValid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("Currency %s is not supported", currency).msg);
    }

    auto optHeight = lock->getHeight();
    int lastHeight = optHeight.is_initialized() ? *optHeight : 0;
    auto lastBlockTime = lock->getBlockTime(lastHeight);

    auto result = view.GetSingleAggregatedPrice(tokenId, currencyId, lastBlockTime);
    if (!result.ok) {
        throw JSONRPCError(result.code, result.msg);
    }

    UniValue price = ValueFromAmount(*result.val);

    return price;
}

namespace {
    UniValue GetAllAggregatedPrices(const CCustomCSView &view, int64_t lastBlockTime) {
        auto pairsRes = view.GetAllTokenCurrencyPairs();
        if (!pairsRes.ok) {
            throw JSONRPCError(RPC_DATABASE_ERROR, pairsRes.msg);
        }
        auto &set = *pairsRes.val;

        UniValue result(UniValue::VARR);
        for (auto &pair : set) {
            UniValue item{UniValue::VOBJ};
            auto tokenRes = FormatToken(view, pair.tid);
            if (!tokenRes.ok) {
                throw JSONRPCError(tokenRes.code, tokenRes.msg);
            }
            item.pushKV(oraclefields::Token, *tokenRes.val);
            item.pushKV(oraclefields::Currency, pair.cid.ToString());

            try {
                auto priceRes = view.GetSingleAggregatedPrice(pair.tid, pair.cid, lastBlockTime);
                if (!priceRes.ok) {
                    throw JSONRPCError(priceRes.code, priceRes.msg);
                }
                auto price = ValueFromAmount(*priceRes.val);
                item.pushKV(oraclefields::AggregatedPrice, price);
                item.pushKV(oraclefields::ValidityFlag, oraclefields::FlagIsValid);
            } catch (UniValue &err) {
                item.pushKV(oraclefields::ValidityFlag, oraclefields::FlagIsError);
            }

            result.push_back(item);
        }

        return result;
    }
} // namespace

UniValue listprices(const JSONRPCRequest& request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"listprices",
               "\nCalculates aggregated prices for all supported pairs (token, currency), \n"
               "Takes no arguments." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {},
               RPCResult{
                       "\"json\"                  (string) array containing json-objects having following fields:\n"
                       "                  `token` - token name,\n"
                       "                  `currency` - currency name,\n"
                       "                  `price` - aggregated price value,\n"
                       "                  `ok` - validity flag,\n"
                       "                  `msg` - optional - if ok is `false`, contains reason\n"
                       "                   possible reasons for a price result to be invalid:"
                       "                   1. if there are no live oracles which meet specified request.\n"
                       "                   2. Sum of the weight of live oracles is zero.\n"
               },
               RPCExamples{
                       HelpExampleCli("listprices", "listprices")
                       + HelpExampleRpc("listprices", "listprices")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {}, false);

    LOCK(cs_main);

    CCustomCSView view(*pcustomcsview);

    auto &chain = pwallet->chain();
    auto lock = chain.lock();

    auto optHeight = lock->getHeight();
    int lastHeight = optHeight.is_initialized() ? *optHeight : 0;
    auto lastBlockTime = lock->getBlockTime(lastHeight);

    UniValue result = GetAllAggregatedPrices(view, lastBlockTime);

    return result;
}

static const CRPCCommand commands[] =
        {
//  category        name                     actor (function)        params
//  -------------   ---------------------    --------------------    ----------
                {"oracles",     "appointoracle",         &appointoracle,          {"address", "pricefeeds", "weightage"}},
                {"oracles",     "removeoracle",          &removeoracle,           {"oracleid"}},
                {"oracles",     "updateoracle",          &updateoracle,           {"oracleid", "address", "allowedtokens"}},
                {"oracles",     "setoracledata",         &setoracledata,          {"oracleid", "timestamp", "prices"}},
                {"oracles",     "getoracledata",         &getoracledata,          {"oracleid"}},
                {"oracles",     "listoracles",           &listoracles,                 {}},
                {"oracles",     "listlatestrawprices",   &listlatestrawprices,    {"request"}},
                {"oracles",     "getprice",              &getprice,               {"request"}},
                {"oracles",     "listprices",            &listprices,                   {}},
        };

void RegisterOraclesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
