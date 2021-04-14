// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_rpc.h>

/// names of oracle json fields
namespace oraclefields {
    constexpr auto Alive = "live";
    constexpr auto Token = "token";
    constexpr auto State = "state";
    constexpr auto Amount = "amount";
    constexpr auto Expired = "expired";
    constexpr auto Currency = "currency";
    constexpr auto OracleId = "oracleid";
    constexpr auto RawPrice = "rawprice";
    constexpr auto Timestamp = "timestamp";
    constexpr auto Weightage = "weightage";
    constexpr auto AggregatedPrice = "price";
    constexpr auto TokenAmount = "tokenAmount";
    constexpr auto ValidityFlag = "ok";
    constexpr auto FlagIsValid = true;
    constexpr auto PriceFeeds = "priceFeeds";
    constexpr auto OracleAddress = "address";
    constexpr auto TokenPrices = "tokenPrices";
    constexpr uint8_t MaxWeightage{100u};
    constexpr uint8_t MinWeightage{1u};
}; // namespace oraclefields

namespace {
    CTokenCurrencyPair DecodeTokenCurrencyPair(const UniValue& value) {
        if (!value.exists(oraclefields::Currency)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s is required field", oraclefields::Currency).msg);
        }
        if (!value.exists(oraclefields::Token)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s is required field", oraclefields::Token).msg);
        }

        auto token = value[oraclefields::Token].getValStr();
        auto currency = value[oraclefields::Currency].getValStr();

        token = trim_ws(token).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        currency = trim_ws(currency).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

        if (token.empty() || currency.empty()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s/%s is empty", oraclefields::Token, oraclefields::Currency).msg);
        }

        return std::make_pair(token, currency);
    }

    std::set<CTokenCurrencyPair> DecodeTokenCurrencyPairs(const std::string& data) {
        UniValue values{UniValue::VARR};
        if (!values.read(data)) {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, "failed to decode token-currency pairs");
        }

        if (!values.isArray()) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "data is not array");
        }

        std::set<CTokenCurrencyPair> pairs;

        for (const auto &value : values.get_array().getValues()) {
            pairs.insert(DecodeTokenCurrencyPair(value));
        }

        return pairs;
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
                               R"(mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF '[{"currency": "USD", "token": "BTC"}, {"currency": "EUR", "token":"ETH"}]' 20)")
                       + HelpExampleRpc(
                               "appointoracle",
                               R"(mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF '[{"currency": "USD", "token": "BTC"}, {"currency": "EUR", "token":"ETH"}]' 20)")
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
    CScript script;
    try {
        script = DecodeScript(request.params[0].getValStr());
    } catch(...) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse address");
    }

    std::string allowedPairsStr = request.params[1].getValStr();
    auto allowedPairs = DecodeTokenCurrencyPairs(allowedPairsStr);

    uint32_t weightage;
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

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CAppointOracleMessage{}, coins);
    }

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
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
                               R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF '[{"currency": "USD", "token": "BTC"}, {"currency": "EUR", "token":"ETH"]}' 20)")
                       + HelpExampleRpc(
                               "updateoracle",
                               R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF '[{"currency": "USD", "token": "BTC"}, {"currency": "EUR", "token":"ETH"]}' 20)")
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
    COracleId oracleId = ParseHashV(request.params[0], "oracleid");

    // decode address
    CScript script;
    try {
        script = DecodeScript(request.params[1].getValStr());
    } catch(...) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to parse address");
    }

    // decode allowed token-currency pairs
    std::string allowedPairsStr = request.params[2].getValStr();
    auto allowedPairs = DecodeTokenCurrencyPairs(allowedPairsStr);

    // decode weightage
    uint32_t weightage;
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
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true, optAuthTx, txInputs);

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
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CUpdateOracleAppointMessage{}, coins);
    }

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
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
    CRemoveOracleAppointMessage msg{};
    msg.oracleId = ParseHashV(request.params[0], "oracleid");

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
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CRemoveOracleAppointMessage{}, coins);
    }

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
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
                        "tokens raw prices:the array of price and token strings in price@token format. ",
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
                               R"('[{"currency":"USD", "tokenAmount":"38293.12@BTC"}"
                               ", {currency:"EUR", "tokenAmount":"1328.32@ETH"}]')"
                       )
                       + HelpExampleRpc(
                               "setoracledata",
                               "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf 1612237937 "
                               R"('[{"currency":"USD", "tokenAmount":"38293.12@BTC"}"
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
    COracleId oracleId = ParseHashV(request.params[0], "oracleid");

    // decode timestamp
    int64_t timestamp;
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

        auto parseDataItem = [&](const UniValue &value) -> std::pair<std::string, std::pair<CAmount, std::string>> {
            if (!value.exists(oraclefields::Currency)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s is required field", oraclefields::Currency).msg);
            }
            if (!value.exists(oraclefields::TokenAmount)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, Res::Err("%s is required field", oraclefields::TokenAmount).msg);
            }

            auto currency = value[oraclefields::Currency].getValStr();
            auto tokenAmount = value[oraclefields::TokenAmount].getValStr();
            auto amounts = ParseTokenAmount(tokenAmount);
            if (!amounts.ok) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, amounts.msg);
            }

            return std::make_pair(currency, *amounts.val);
        };

        CTokenPrices tokenPrices;

        for (const auto &value : prices.get_array().getValues()) {
            std::string currency;
            std::pair<CAmount, std::string> tokenAmount;
            std::tie(currency, tokenAmount) = parseDataItem(value);
            tokenPrices[tokenAmount.second][currency] = tokenAmount.first;
        }

        CSetOracleDataMessage msg{oracleId, timestamp, std::move(tokenPrices)};

        // check if tx parameters are valid

        auto oracleRes = mnview.GetOracleData(oracleId);
        if (!oracleRes.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, oracleRes.msg);
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

        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CSetOracleDataMessage{}, coins);
    }

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

namespace {
    UniValue PriceFeedToJSON(const CTokenCurrencyPair& priceFeed) {
        UniValue pair(UniValue::VOBJ);
        pair.pushKV(oraclefields::Token, priceFeed.first);
        pair.pushKV(oraclefields::Currency, priceFeed.second);
        return pair;
    }

    bool diffInHour(int64_t time1, int64_t time2) {
        constexpr const uint64_t SECONDS_PER_HOUR = 3600u;
        return std::abs(time1 - time2) < SECONDS_PER_HOUR;
    }

    UniValue OracleToJSON(const COracleId& oracleId, const COracle& oracle) {
        UniValue result{UniValue::VOBJ};
        result.pushKV(oraclefields::Weightage, oracle.weightage);
        result.pushKV(oraclefields::OracleId, oracleId.GetHex());
        result.pushKV(oraclefields::OracleAddress, oracle.oracleAddress.GetHex());

        UniValue priceFeeds{UniValue::VARR};
        for (const auto& feed : oracle.availablePairs) {
            priceFeeds.push_back(PriceFeedToJSON(feed));
        }

        result.pushKV(oraclefields::PriceFeeds, priceFeeds);

        UniValue tokenPrices{UniValue::VARR};
        for (const auto& tokenPrice: oracle.tokenPrices) {
            for (const auto& price: tokenPrice.second) {
                const auto& currency = price.first;
                const auto& pricePair = price.second;
                auto amount = pricePair.first;
                auto timestamp = pricePair.second;

                UniValue item(UniValue::VOBJ);
                item.pushKV(oraclefields::Token, tokenPrice.first);
                item.pushKV(oraclefields::Currency, currency);
                item.pushKV(oraclefields::Amount, ValueFromAmount(amount));
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
    COracleId oracleId = ParseHashV(request.params[0], "oracleid");

    LOCK(cs_main);
    CCustomCSView mnview(*pcustomcsview); // don't write into actual DB

    auto oracleRes = mnview.GetOracleData(oracleId);
    if (!oracleRes.ok) {
        throw JSONRPCError(RPC_DATABASE_ERROR, oracleRes.msg);
    }

    return OracleToJSON(oracleId, *oracleRes.val);
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

    UniValue value(UniValue::VARR);
    CCustomCSView view(*pcustomcsview);
    view.ForEachOracle([&](const COracleId& id, CLazySerialize<COracle>) {
        value.push_back(id.GetHex());
        return true;
    });

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
                                      R"(listlatestrawprices '{"currency": "USD", "token": "BTC"}')")
                       + HelpExampleRpc("listlatestrawprices",
                                        R"(listlatestrawprices '{"currency": "USD", "token": "BTC"}')")
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

    auto &chain = pwallet->chain();
    auto lock = chain.lock();

    boost::optional<CTokenCurrencyPair> tokenPair;
    if (!useAllFeeds) {
        tokenPair = DecodeTokenCurrencyPair(data);
    }

    auto optHeight = lock->getHeight();
    int lastHeight = optHeight ? *optHeight : 0;
    auto lastBlockTime = lock->getBlockTime(lastHeight);

    UniValue result(UniValue::VARR);
    mnview.ForEachOracle([&](const COracleId& oracleId, COracle oracle) {
        if (tokenPair && !oracle.SupportsPair(tokenPair->first, tokenPair->second)) {
            return true;
        }
        for (const auto& tokenPrice: oracle.tokenPrices) {
            const auto& token = tokenPrice.first;
            if (tokenPair && tokenPair->first != token) {
                continue;
            }
            for (const auto& price: tokenPrice.second) {
                const auto& currency = price.first;
                if (tokenPair && tokenPair->second != currency) {
                    continue;
                }
                const auto& pricePair = price.second;
                auto amount = pricePair.first;
                auto timestamp = pricePair.second;

                UniValue value{UniValue::VOBJ};
                auto tokenCurrency = std::make_pair(token, currency);
                value.pushKV(oraclefields::PriceFeeds, PriceFeedToJSON(tokenCurrency));
                value.pushKV(oraclefields::OracleId, oracleId.GetHex());
                value.pushKV(oraclefields::Weightage, oracle.weightage);
                value.pushKV(oraclefields::Timestamp, timestamp);
                value.pushKV(oraclefields::RawPrice, ValueFromAmount(amount));
                auto state = diffInHour(timestamp, lastBlockTime) ? oraclefields::Alive : oraclefields::Expired;
                value.pushKV(oraclefields::State, state);
                result.push_back(value);
            }
        }
        return true;
    });
    return result;
}

namespace {
    CAmount GetAggregatePrice(CCustomCSView& view, const std::string& token, const std::string& currency, uint64_t lastBlockTime) {
        arith_uint256 weightedSum = 0;
        uint64_t numLiveOracles = 0, sumWeights = 0;
        view.ForEachOracle([&](const COracleId&, COracle oracle) {
            if (!oracle.SupportsPair(token, currency)) {
                return true;
            }
            for (const auto& tokenPrice : oracle.tokenPrices) {
                if (token != tokenPrice.first) {
                    continue;
                }
                for (const auto& price : tokenPrice.second) {
                    if (currency != price.first) {
                        continue;
                    }
                    const auto& pricePair = price.second;
                    auto amount = pricePair.first;
                    auto timestamp = pricePair.second;
                    if (!diffInHour(timestamp, lastBlockTime)) {
                        continue;
                    }
                    ++numLiveOracles;
                    sumWeights += oracle.weightage;
                    weightedSum += arith_uint256(amount) * arith_uint256(oracle.weightage);
                }
            }
            return true;
        });

        if (numLiveOracles == 0) {
            throw JSONRPCError(RPC_MISC_ERROR, "no live oracles for specified request");
        }

        if (sumWeights == 0) {
            throw JSONRPCError(RPC_MISC_ERROR, "all live oracles which meet specified request, have zero weight");
        }

        return (weightedSum / arith_uint256(sumWeights)).GetLow64();
    }

    UniValue GetAllAggregatePrices(CCustomCSView& view, uint64_t lastBlockTime) {
        UniValue result(UniValue::VARR);
        std::set<CTokenCurrencyPair> setTokenCurrency;
        view.ForEachOracle([&](const COracleId&, COracle oracle) {
            const auto& pair = oracle.availablePairs;
            setTokenCurrency.insert(pair.begin(), pair.end());
            return true;
        });
        for (const auto& tokenCurrency : setTokenCurrency) {
            UniValue item{UniValue::VOBJ};
            const auto& token = tokenCurrency.first;
            const auto& currency = tokenCurrency.second;
            item.pushKV(oraclefields::Token, token);
            item.pushKV(oraclefields::Currency, currency);
            try {
                auto price = GetAggregatePrice(view, token, currency, lastBlockTime);
                item.pushKV(oraclefields::AggregatedPrice, ValueFromAmount(price));
                item.pushKV(oraclefields::ValidityFlag, oraclefields::FlagIsValid);
            } catch (const UniValue& error) {
                item.pushKV(oraclefields::ValidityFlag, error["message"]);
            }
            result.push_back(item);
        }
        return result;
    }
} // namespace

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
                       HelpExampleCli("getprice", R"(getprice '{"currency": "USD", "token": "BTC"}')")
                       + HelpExampleRpc("getprice", R"(getprice '{"currency": "USD", "token": "BTC"}')")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);

    LOCK(cs_main);

    CCustomCSView view(*pcustomcsview);

    UniValue data{UniValue::VOBJ};
    if (!data.read(request.params[0].getValStr())) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "failed to read input json");
    }

    auto tokenPair = DecodeTokenCurrencyPair(data);

    auto &chain = pwallet->chain();
    auto lock = chain.lock();

    auto optHeight = lock->getHeight();
    int lastHeight = optHeight ? *optHeight : 0;
    auto lastBlockTime = lock->getBlockTime(lastHeight);

    auto result = GetAggregatePrice(view, tokenPair.first, tokenPair.second, lastBlockTime);
    return ValueFromAmount(result);
}

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
    int lastHeight = optHeight ? *optHeight : 0;
    auto lastBlockTime = lock->getBlockTime(lastHeight);

    return GetAllAggregatePrices(view, lastBlockTime);
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  -------------   ---------------------    --------------------    ----------
    {"oracles",     "appointoracle",         &appointoracle,          {"address", "pricefeeds", "weightage"}},
    {"oracles",     "removeoracle",          &removeoracle,           {"oracleid"}},
    {"oracles",     "updateoracle",          &updateoracle,           {"oracleid", "address", "pricefeeds", "weightage"}},
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
