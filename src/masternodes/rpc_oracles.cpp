// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_rpc.h>

extern CTokenCurrencyPair DecodePriceFeedUni(const UniValue& value);
extern CTokenCurrencyPair DecodePriceFeedString(const std::string& value);
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
    constexpr auto MaxWeightage = 255;
    constexpr auto MinWeightage = 0;
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

    std::set<CTokenCurrencyPair> DecodeTokenCurrencyPairs(const UniValue& values) {

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
    auto pwallet = GetWallet(request);

    RPCHelpMan{"appointoracle",
               "\nCreates (and submits to local node and network) a `appoint oracle transaction`, \n"
               "and saves oracle to database.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "oracle address",},
                       {"pricefeeds", RPCArg::Type::ARR, RPCArg::Optional::NO, "list of allowed token-currency pairs",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"currency", RPCArg::Type::STR, RPCArg::Optional::NO, "Currency name"},
                                         {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Token name"},
                                 },
                                },
                        },
                       },
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
                 {UniValue::VSTR, UniValue::VARR, UniValue::VNUM, UniValue::VARR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    // decode
    CScript script = DecodeScript(request.params[0].get_str());

    auto allowedPairs = DecodeTokenCurrencyPairs(request.params[1]);

    auto weightage = request.params[2].get_int();

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
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue updateoracle(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"updateoracle",
               "\nCreates (and submits to local node and network) a `update oracle transaction`, \n"
               "and saves oracle updates to database.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"oracleid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "oracle id"},
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "oracle address",},
                       {"pricefeeds", RPCArg::Type::ARR, RPCArg::Optional::NO, "list of allowed token-currency pairs",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"currency", RPCArg::Type::STR, RPCArg::Optional::NO, "Currency name"},
                                         {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Token name"},
                                 },
                                },
                        },
                       },
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
                 {UniValue::VSTR, UniValue::VSTR, UniValue::VARR, UniValue::VNUM, UniValue::VARR},
                 false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    // decode oracleid
    COracleId oracleId = ParseHashV(request.params[0], "oracleid");

    // decode address
    CScript script = DecodeScript(request.params[1].get_str());

    // decode allowed token-currency pairs
    auto allowedPairs = DecodeTokenCurrencyPairs(request.params[2]);

    // decode weightage
    auto weightage = request.params[3].get_int();

    if (weightage > oraclefields::MaxWeightage || weightage < oraclefields::MinWeightage) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "the weightage value is out of bounds");
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    CUpdateOracleAppointMessage msg{
        oracleId,
        CAppointOracleMessage{std::move(script), uint8_t(weightage), std::move(allowedPairs)}
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
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue removeoracle(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

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

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

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
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue setoracledata(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"setoracledata",
               "\nCreates (and submits to local node and network) a `set oracle data transaction`.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"oracleid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "oracle hex id",},
                       {"timestamp", RPCArg::Type::NUM, RPCArg::Optional::NO, "balances timestamp",},
                       {"prices", RPCArg::Type::ARR, RPCArg::Optional::NO,
                        "tokens raw prices:the array of price and token strings in price@token format. ",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"currency", RPCArg::Type::STR, RPCArg::Optional::NO, "Currency name"},
                                    {"tokenAmount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount@token"},
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
                 {UniValue::VSTR, UniValue::VNUM, UniValue::VARR, UniValue::VARR},
                 false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    // decode oracle id
    COracleId oracleId = ParseHashV(request.params[0], "oracleid");

    // decode timestamp
    int64_t timestamp = request.params[1].get_int64();

    // decode prices
    auto const & prices = request.params[2];

    CMutableTransaction rawTx{};
    CTransactionRef optAuthTx;

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
        if (!amounts) {
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

    int targetHeight;
    CScript oracleAddress;
    {
        LOCK(cs_main);
        // check if tx parameters are valid
        auto oracleRes = pcustomcsview->GetOracleData(oracleId);
        if (!oracleRes) {
            throw JSONRPCError(RPC_INVALID_REQUEST, oracleRes.msg);
        }
        oracleAddress = oracleRes.val->oracleAddress;

        targetHeight = ::ChainActive().Height() + 1;
    }

    // timestamp is checked at consensus level
    if (targetHeight < Params().GetConsensus().FortCanningHeight) {
        if (timestamp <= 0 || timestamp > GetSystemTimeInSeconds() + 300) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "timestamp cannot be negative, zero or over 5 minutes in the future");
        }
    }

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::SetOracleData)
                    << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    rawTx = CMutableTransaction(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const &txInputs = request.params[3];

    std::set<CScript> auths{oracleAddress};

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

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

bool diffInHour(int64_t time1, int64_t time2) {
    constexpr const uint64_t SECONDS_PER_HOUR = 3600u;
    return std::abs(time1 - time2) < SECONDS_PER_HOUR;
}

std::pair<int, int> GetFixedIntervalPriceBlocks(int currentHeight, const CCustomCSView &mnview){
    auto fixedBlocks = mnview.GetIntervalBlock();
    auto nextPriceBlock = currentHeight + (fixedBlocks - ((currentHeight) % fixedBlocks));
    auto activePriceBlock = nextPriceBlock - fixedBlocks;
    return {activePriceBlock, nextPriceBlock};
}

namespace {
    UniValue PriceFeedToJSON(const CTokenCurrencyPair& priceFeed) {
        UniValue pair(UniValue::VOBJ);
        pair.pushKV(oraclefields::Token, priceFeed.first);
        pair.pushKV(oraclefields::Currency, priceFeed.second);
        return pair;
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

    RPCHelpMan{"getoracledata",
               "\nReturns oracle data in json form.\n",
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

    // decode oracle id
    COracleId oracleId = ParseHashV(request.params[0], "oracleid");

    LOCK(cs_main);
    CCustomCSView mnview(*pcustomcsview); // don't write into actual DB

    auto oracleRes = mnview.GetOracleData(oracleId);
    if (!oracleRes) {
        throw JSONRPCError(RPC_DATABASE_ERROR, oracleRes.msg);
    }

    return OracleToJSON(oracleId, *oracleRes.val);
}

UniValue listoracles(const JSONRPCRequest &request) {

    RPCHelpMan{"listoracles",
               "\nReturns list of oracle ids.\n",
               {
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                "Optional first key to iterate from, in lexicographical order. "
                                "Typically it's set to last ID from previous request."
                            },
                            {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                "If true, then iterate including starting position. False by default"
                            },
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Maximum number of orders to return, 100 by default"
                            },
                        },
                    },
               },
               RPCResult{
                       "\"hash\"                  (string) list of known oracle ids\n"
               },
               RPCExamples{
                       HelpExampleCli("listoracles", "")
                       + HelpExampleCli("listoracles",
                                        "'{\"start\":\"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013\", "
                                        "\"including_start\": true, "
                                        "\"limit\":100}'")
                       + HelpExampleRpc("listoracles", "'{}'")
                       + HelpExampleRpc("listoracles",
                                        "'{\"start\":\"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013\", "
                                        "\"including_start\": true, "
                                        "\"limit\":100}'")
               },
    }.Check(request);

    // parse pagination
    COracleId start = {};
    bool including_start = true;
    size_t limit = 100;
    {
        if (request.params.size() > 0){
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["start"].isNull()){
                start = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!paginationObj["limit"].isNull()){
                limit = (size_t) paginationObj["limit"].get_int64();
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue value(UniValue::VARR);
    CCustomCSView view(*pcustomcsview);
    view.ForEachOracle([&](const COracleId& id, CLazySerialize<COracle>) {
        if (!including_start)
        {
            including_start = true;
            return (true);
        }
        value.push_back(id.GetHex());
        return --limit != 0;
    }, start);

    return value;
}

UniValue listlatestrawprices(const JSONRPCRequest &request) {

    RPCHelpMan{"listlatestrawprices",
               "\nReturns latest raw price updates through all the oracles for specified token and currency , \n",
                {
                   {"request", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED,
                        "request in json-form, containing currency and token names",
                        {
                            {"currency", RPCArg::Type::STR, RPCArg::Optional::NO, "Currency name"},
                            {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Token name"},
                        },
                   },
                   {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                "Optional first key to iterate from, in lexicographical order. "
                                "Typically it's set to last ID from previous request."
                            },
                            {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                "If true, then iterate including starting position. False by default"
                            },
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Maximum number of orders to return, 100 by default"
                            },
                        },
                    },
                },
               RPCResult{
                       "\"json\"                  (string) Array of json objects containing full information about token prices\n"
               },
               RPCExamples{
                       HelpExampleCli("listlatestrawprices",
                                      R"(listlatestrawprices '{"currency": "USD", "token": "BTC"}')")
                       + HelpExampleCli("listlatestrawprices",
                                      R"(listlatestrawprices '{"currency": "USD", "token": "BTC"}' '{"start": "b7ffdcef37be39018e8a6f846db1220b3558fd649393e9a12f935007ef3bfb8e", "including_start": true, "limit": 100}')")
                       + HelpExampleRpc("listlatestrawprices",
                                        R"(listlatestrawprices '{"currency": "USD", "token": "BTC"}')")
                       + HelpExampleRpc("listlatestrawprices",
                                        R"(listlatestrawprices '{"currency": "USD", "token": "BTC"}' '{"start": "b7ffdcef37be39018e8a6f846db1220b3558fd649393e9a12f935007ef3bfb8e", "including_start": true, "limit": 100}')")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);

    std::optional<CTokenCurrencyPair> tokenPair;

    // parse pagination
    COracleId start = {};
    bool including_start = true;
    size_t limit = 100;
    {
        if (request.params.size() > 1){
            UniValue paginationObj = request.params[1].get_obj();
            if (!paginationObj["start"].isNull()){
                start = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!paginationObj["limit"].isNull()){
                limit = (size_t) paginationObj["limit"].get_int64();
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    if (!request.params.empty()) {
        tokenPair = DecodeTokenCurrencyPair(request.params[0]);
    }

    LOCK(cs_main);
    CCustomCSView mnview(*pcustomcsview);
    auto lastBlockTime = ::ChainActive().Tip()->GetBlockTime();

    UniValue result(UniValue::VARR);
    mnview.ForEachOracle([&](const COracleId& oracleId, COracle oracle) {
        if (!including_start)
        {
            including_start = true;
            return (true);
        }
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
                if (--limit == 0) {
                    return false;
                }
            }
        }
        return limit != 0;
    }, start);
    return result;
}

ResVal<CAmount> GetAggregatePrice(CCustomCSView& view, const std::string& token, const std::string& currency, uint64_t lastBlockTime) {
    // DUSD-USD always returns 1.00000000
    if (token == "DUSD" && currency == "USD") {
        return ResVal<CAmount>(COIN, Res::Ok());
    }
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

    static const auto minimumLiveOracles = Params().NetworkIDString() == CBaseChainParams::REGTEST ? 1 : 2;

    if (numLiveOracles < minimumLiveOracles) {
        return Res::Err("no live oracles for specified request");
    }

    if (sumWeights == 0) {
        return Res::Err("all live oracles which meet specified request, have zero weight");
    }

    ResVal<CAmount> res((weightedSum / arith_uint256(sumWeights)).GetLow64(), Res::Ok());
    LogPrint(BCLog::LOAN, "%s(): %s/%s=%lld\n", __func__, token, currency, *res.val);
    return res;
}

namespace {

    UniValue GetAllAggregatePrices(CCustomCSView& view, uint64_t lastBlockTime, const UniValue& paginationObj) {

        size_t limit = 100;
        int start = 0;
        bool including_start = true;
        if (!paginationObj.empty()){
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start = paginationObj["start"].get_int();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                ++start;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }

        UniValue result(UniValue::VARR);
        std::set<CTokenCurrencyPair> setTokenCurrency;
        view.ForEachOracle([&](const COracleId&, COracle oracle) {
            const auto& pairs = oracle.availablePairs;
            if (start < pairs.size()) {
                setTokenCurrency.insert(std::next(pairs.begin(), start), pairs.end());
            }
            return true;
        });
        for (const auto& tokenCurrency : setTokenCurrency) {
            UniValue item{UniValue::VOBJ};
            const auto& token = tokenCurrency.first;
            const auto& currency = tokenCurrency.second;
            item.pushKV(oraclefields::Token, token);
            item.pushKV(oraclefields::Currency, currency);
            auto aggregatePrice = GetAggregatePrice(view, token, currency, lastBlockTime);
            if (aggregatePrice) {
                item.pushKV(oraclefields::AggregatedPrice, ValueFromAmount(*aggregatePrice.val));
                item.pushKV(oraclefields::ValidityFlag, oraclefields::FlagIsValid);
            } else {
                item.pushKV(oraclefields::ValidityFlag, aggregatePrice.msg);
            }
            result.push_back(item);
            if (--limit == 0) {
                break;
            }
        }
        return result;
    }
} // namespace

UniValue getprice(const JSONRPCRequest &request) {

    RPCHelpMan{"getprice",
               "\nCalculates aggregated price, \n"
               "The only argument is a json-form request containing token and currency names.\n",
               {
                       {"request", RPCArg::Type::OBJ, RPCArg::Optional::NO,
                        "request in json-form, containing currency and token names, both are mandatory",
                        {
                            {"currency", RPCArg::Type::STR, RPCArg::Optional::NO, "Currency name"},
                            {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "Token name"},
                        },
                       },
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

    RPCTypeCheck(request.params, {UniValue::VOBJ}, false);

    auto tokenPair = DecodeTokenCurrencyPair(request.params[0]);

    LOCK(cs_main);
    CCustomCSView view(*pcustomcsview);
    auto lastBlockTime = ::ChainActive().Tip()->GetBlockTime();
    auto result = GetAggregatePrice(view, tokenPair.first, tokenPair.second, lastBlockTime);
    if (!result)
        throw JSONRPCError(RPC_MISC_ERROR, result.msg);
    return ValueFromAmount(*result.val);
}

UniValue listprices(const JSONRPCRequest& request) {

    RPCHelpMan{"listprices",
               "\nCalculates aggregated prices for all supported pairs (token, currency), \n",
               {
                   {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Optional first key to iterate from, in lexicographical order."
                                "Typically it's set to last ID from previous request."},
                            {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                "If true, then iterate including starting position. False by default"
                            },
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Maximum number of orders to return, 100 by default"
                            },
                        },
                    },
               },
               RPCResult{
                       "\"json\"                  (string) array containing json-objects having following fields:\n"
                       "                  `token` - token name,\n"
                       "                  `currency` - currency name,\n"
                       "                  `price` - aggregated price value,\n"
                       "                  `ok` - `true` if price is valid, otherwise it is populated with the reason description.\n"
                       "                   Possible reasons for a price result to be invalid:"
                       "                   1. if there are no live oracles which meet specified request.\n"
                       "                   2. Sum of the weight of live oracles is zero.\n"
               },
               RPCExamples{
                       HelpExampleCli("listprices", "")
                       + HelpExampleCli("listprices",
                                        "'{\"start\": 2, "
                                        "\"including_start\": true, "
                                        "\"limit\":100}'")
                       + HelpExampleRpc("listprices", "'{}'")
                       + HelpExampleRpc("listprices",
                                        "'{\"start\": 2, "
                                        "\"including_start\": true, "
                                        "\"limit\":100}'")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {}, false);

    // parse pagination
    UniValue paginationObj(UniValue::VOBJ);
    if (request.params.size() > 0) {
        paginationObj = request.params[0].get_obj();
    }

    LOCK(cs_main);
    CCustomCSView view(*pcustomcsview);
    auto lastBlockTime = ::ChainActive().Tip()->GetBlockTime();
    return GetAllAggregatePrices(view, lastBlockTime, paginationObj);
}

UniValue getfixedintervalprice(const JSONRPCRequest& request) {
    RPCHelpMan{"getfixedintervalprice",
                "Get fixed interval price for a given pair.\n",
                {
                    {"fixedIntervalPriceId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "token/currency pair to use for price of token"},
                },
                RPCResult{
                       "\"json\"          (string) json-object having following fields:\n"
                       "                  `activePrice` - current price used for loan calculations\n"
                       "                  `nextPrice` - next price to be assigned to pair.\n"
                       "                  `nextPriceBlock` - height of nextPrice.\n"
                       "                  `activePriceBlock` - height of activePrice.\n"
                       "                  `timestamp` - timestamp of active price.\n"
                       "                  `isLive` - price liveness, within parameters"
                       "                   Possible reasons for a price result to not be live:"
                       "                   1. Not sufficient live oracles.\n"
                       "                   2. Deviation is over the limit to be considered stable.\n"
                },
                RPCExamples{
                        HelpExampleCli("getfixedintervalprice", "TSLA/USD")
                },
    }.Check(request);

    auto fixedIntervalStr = request.params[0].getValStr();

    UniValue objPrice{UniValue::VOBJ};
    objPrice.pushKV("fixedIntervalPriceId", fixedIntervalStr);
    auto pairId = DecodePriceFeedUni(objPrice);

    LOCK(cs_main);

    LogPrint(BCLog::ORACLE,"%s()->", __func__);  /* Continued */

    auto fixedPrice = pcustomcsview->GetFixedIntervalPrice(pairId);
    if (!fixedPrice)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, fixedPrice.msg);

    auto priceBlocks = GetFixedIntervalPriceBlocks(::ChainActive().Height(), *pcustomcsview);

    objPrice.pushKV("activePrice", ValueFromAmount(fixedPrice.val->priceRecord[0]));
    objPrice.pushKV("nextPrice", ValueFromAmount(fixedPrice.val->priceRecord[1]));
    objPrice.pushKV("activePriceBlock", (int)priceBlocks.first);
    objPrice.pushKV("nextPriceBlock", (int)priceBlocks.second);
    objPrice.pushKV("timestamp", fixedPrice.val->timestamp);
    objPrice.pushKV("isLive", fixedPrice.val->isLive(pcustomcsview->GetPriceDeviation()));
    return objPrice;
}

UniValue listfixedintervalprices(const JSONRPCRequest& request) {
    RPCHelpMan{"listfixedintervalprices",
                "Get all fixed interval prices.\n",
                {
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                              "Optional first key to iterate from, in lexicographical order."
                              "Typically it's set to last ID from previous request."},
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                              "Maximum number of fixed interval prices to return, 100 by default"},
                        },
                    },
                },
                RPCResult{
                       "\"json\"          (string) array containing json-objects having following fields:\n"
                       "                  `activePrice` - current price used for loan calculations\n"
                       "                  `nextPrice` - next price to be assigned to pair.\n"
                       "                  `timestamp` - timestamp of active price.\n"
                       "                  `isLive` - price liveness, within parameters"
                       "                   Possible reasons for a price result to not be live:"
                       "                   1. Not sufficient live oracles.\n"
                       "                   2. Deviation is over the limit to be considered stable.\n"
                },
                RPCExamples{
                        HelpExampleCli("listfixedintervalprices", R"('{""}')")
                },
    }.Check(request);

    size_t limit = 100;
    CTokenCurrencyPair start{};
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                auto priceFeedId = paginationObj["start"].getValStr();
                start = DecodePriceFeedString(priceFeedId);
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue listPrice{UniValue::VARR};
    pcustomcsview->ForEachFixedIntervalPrice([&](const CTokenCurrencyPair&, CFixedIntervalPrice fixedIntervalPrice){
        UniValue obj{UniValue::VOBJ};
        obj.pushKV("priceFeedId", (fixedIntervalPrice.priceFeedId.first + "/" + fixedIntervalPrice.priceFeedId.second));
        obj.pushKV("activePrice", ValueFromAmount(fixedIntervalPrice.priceRecord[0]));
        obj.pushKV("nextPrice", ValueFromAmount(fixedIntervalPrice.priceRecord[1]));
        obj.pushKV("timestamp", fixedIntervalPrice.timestamp);
        obj.pushKV("isLive", fixedIntervalPrice.isLive(pcustomcsview->GetPriceDeviation()));
        listPrice.push_back(obj);
        return --limit != 0;
    }, start);
    return listPrice;
}

static const CRPCCommand commands[] =
{
//  category        name                       actor (function)           params
//  -------------   ---------------------      --------------------       ----------
    {"oracles",     "appointoracle",           &appointoracle,            {"address", "pricefeeds", "weightage", "inputs"}},
    {"oracles",     "removeoracle",            &removeoracle,             {"oracleid", "inputs"}},
    {"oracles",     "updateoracle",            &updateoracle,             {"oracleid", "address", "pricefeeds", "weightage", "inputs"}},
    {"oracles",     "setoracledata",           &setoracledata,            {"oracleid", "timestamp", "prices", "inputs"}},
    {"oracles",     "getoracledata",           &getoracledata,            {"oracleid"}},
    {"oracles",     "listoracles",             &listoracles,              {"pagination"}},
    {"oracles",     "listlatestrawprices",     &listlatestrawprices,      {"request", "pagination"}},
    {"oracles",     "getprice",                &getprice,                 {"request"}},
    {"oracles",     "listprices",              &listprices,               {"pagination"}},
    {"oracles",     "getfixedintervalprice",   &getfixedintervalprice,    {"fixedIntervalPriceId"}},
    {"oracles",     "listfixedintervalprices", &listfixedintervalprices,  {"pagination"}},
};

void RegisterOraclesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
