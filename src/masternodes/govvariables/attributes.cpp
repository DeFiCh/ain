// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_rpc.h>
#include <masternodes/govvariables/attributes.h>

#include <masternodes/accountshistory.h> /// CAccountsHistoryWriter
#include <masternodes/masternodes.h> /// CCustomCSView
#include <masternodes/mn_checks.h> /// GetAggregatePrice / CustomTxType
#include <validation.h> /// GetNextAccPosition

#include <amount.h> /// GetDecimaleString
#include <core_io.h> /// ValueFromAmount
#include <util/strencodings.h>

extern UniValue AmountsToJSON(TAmounts const & diffs, AmountFormat format = AmountFormat::Symbol);

static inline std::string trim_all_ws(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
    return s;
}

static std::vector<std::string> KeyBreaker(const std::string& str, const char delim = '/'){
    std::string section;
    std::istringstream stream(str);
    std::vector<std::string> strVec;

    while (std::getline(stream, section, delim)) {
        strVec.push_back(section);
    }
    return strVec;
}

const std::map<std::string, uint8_t>& ATTRIBUTES::allowedVersions() {
    static const std::map<std::string, uint8_t> versions{
        {"v0",  VersionTypes::v0},
    };
    return versions;
}

const std::map<uint8_t, std::string>& ATTRIBUTES::displayVersions() {
    static const std::map<uint8_t, std::string> versions{
        {VersionTypes::v0,  "v0"},
    };
    return versions;
}

const std::map<std::string, uint8_t>& ATTRIBUTES::allowedTypes() {
    static const std::map<std::string, uint8_t> types{
        {"locks",       AttributeTypes::Locks},
        {"oracles",     AttributeTypes::Oracles},
        {"params",      AttributeTypes::Param},
        {"poolpairs",   AttributeTypes::Poolpairs},
        {"token",       AttributeTypes::Token},
    };
    return types;
}

const std::map<uint8_t, std::string>& ATTRIBUTES::displayTypes() {
    static const std::map<uint8_t, std::string> types{
        {AttributeTypes::Live,      "live"},
        {AttributeTypes::Locks,     "locks"},
        {AttributeTypes::Oracles,   "oracles"},
        {AttributeTypes::Param,     "params"},
        {AttributeTypes::Poolpairs, "poolpairs"},
        {AttributeTypes::Token,     "token"},
    };
    return types;
}

const std::map<std::string, uint8_t>& ATTRIBUTES::allowedParamIDs() {
    static const std::map<std::string, uint8_t> params{
        {"dfip2201",    ParamIDs::DFIP2201},
        {"dfip2203",    ParamIDs::DFIP2203},
        {"dfip2206a",   ParamIDs::DFIP2206A},
        // Note: DFIP2206F is currently in beta testing
        // for testnet. May not be enabled on mainnet until testing is complete.
        {"dfip2206f",   ParamIDs::DFIP2206F},
    };
    return params;
}

const std::map<std::string, uint8_t>& ATTRIBUTES::allowedLocksIDs() {
    static const std::map<std::string, uint8_t> params{
            {"token",       ParamIDs::TokenID},
    };
    return params;
}

const std::map<uint8_t, std::string>& ATTRIBUTES::displayParamsIDs() {
    static const std::map<uint8_t, std::string> params{
        {ParamIDs::DFIP2201,    "dfip2201"},
        {ParamIDs::DFIP2203,    "dfip2203"},
        {ParamIDs::DFIP2206A,   "dfip2206a"},
        // Note: DFIP2206F is currently in beta testing
        // for testnet. May not be enabled on mainnet until testing is complete.
        {ParamIDs::DFIP2206F,   "dfip2206f"},
        {ParamIDs::TokenID,     "token"},
        {ParamIDs::Economy,     "economy"},
    };
    return params;
}

const std::map<std::string, uint8_t>& ATTRIBUTES::allowedOracleIDs() {
    static const std::map<std::string, uint8_t> params{
            {"splits",    OracleIDs::Splits}
    };
    return params;
}

const std::map<uint8_t, std::string>& ATTRIBUTES::displayOracleIDs() {
    static const std::map<uint8_t, std::string> params{
            {OracleIDs::Splits,    "splits"},
    };
    return params;
}

const std::map<uint8_t, std::map<std::string, uint8_t>>& ATTRIBUTES::allowedKeys() {
    static const std::map<uint8_t, std::map<std::string, uint8_t>> keys{
        {
            AttributeTypes::Token, {
                {"payback_dfi",             TokenKeys::PaybackDFI},
                {"payback_dfi_fee_pct",     TokenKeys::PaybackDFIFeePCT},
                {"loan_payback",            TokenKeys::LoanPayback},
                {"loan_payback_fee_pct",    TokenKeys::LoanPaybackFeePCT},
                {"dex_in_fee_pct",          TokenKeys::DexInFeePct},
                {"dex_out_fee_pct",         TokenKeys::DexOutFeePct},
                {"dfip2203",                TokenKeys::DFIP2203Enabled},
                {"fixed_interval_price_id", TokenKeys::FixedIntervalPriceId},
                {"loan_collateral_enabled", TokenKeys::LoanCollateralEnabled},
                {"loan_collateral_factor",  TokenKeys::LoanCollateralFactor},
                {"loan_minting_enabled",    TokenKeys::LoanMintingEnabled},
                {"loan_minting_interest",   TokenKeys::LoanMintingInterest},
            }
        },
        {
            AttributeTypes::Poolpairs, {
                {"token_a_fee_pct",      PoolKeys::TokenAFeePCT},
                {"token_a_fee_direction",PoolKeys::TokenAFeeDir},
                {"token_b_fee_pct",      PoolKeys::TokenBFeePCT},
                {"token_b_fee_direction",PoolKeys::TokenBFeeDir},
            }
        },
        {
            AttributeTypes::Param, {
                {"active",                      DFIPKeys::Active},
                {"minswap",                     DFIPKeys::MinSwap},
                {"premium",                     DFIPKeys::Premium},
                {"reward_pct",                  DFIPKeys::RewardPct},
                {"block_period",                DFIPKeys::BlockPeriod},
                {"dusd_interest_burn",          DFIPKeys::DUSDInterestBurn},
                {"dusd_loan_burn",              DFIPKeys::DUSDLoanBurn},
                {"start_block",                 DFIPKeys::StartBlock},
            }
        },
    };
    return keys;
}

const std::map<uint8_t, std::map<uint8_t, std::string>>& ATTRIBUTES::displayKeys() {
    static const std::map<uint8_t, std::map<uint8_t, std::string>> keys{
        {
            AttributeTypes::Token, {
                {TokenKeys::PaybackDFI,            "payback_dfi"},
                {TokenKeys::PaybackDFIFeePCT,      "payback_dfi_fee_pct"},
                {TokenKeys::LoanPayback,           "loan_payback"},
                {TokenKeys::LoanPaybackFeePCT,     "loan_payback_fee_pct"},
                {TokenKeys::DexInFeePct,           "dex_in_fee_pct"},
                {TokenKeys::DexOutFeePct,          "dex_out_fee_pct"},
                {TokenKeys::FixedIntervalPriceId,  "fixed_interval_price_id"},
                {TokenKeys::LoanCollateralEnabled, "loan_collateral_enabled"},
                {TokenKeys::LoanCollateralFactor,  "loan_collateral_factor"},
                {TokenKeys::LoanMintingEnabled,    "loan_minting_enabled"},
                {TokenKeys::LoanMintingInterest,   "loan_minting_interest"},
                {TokenKeys::DFIP2203Enabled,       "dfip2203"},
                {TokenKeys::Ascendant,             "ascendant"},
                {TokenKeys::Descendant,            "descendant"},
                {TokenKeys::Epitaph,               "epitaph"},
            }
        },
        {
            AttributeTypes::Poolpairs, {
                {PoolKeys::TokenAFeePCT,      "token_a_fee_pct"},
                {PoolKeys::TokenAFeeDir,      "token_a_fee_direction"},
                {PoolKeys::TokenBFeePCT,      "token_b_fee_pct"},
                {PoolKeys::TokenBFeeDir,      "token_b_fee_direction"},
            }
        },
        {
            AttributeTypes::Param, {
                {DFIPKeys::Active,                  "active"},
                {DFIPKeys::Premium,                 "premium"},
                {DFIPKeys::MinSwap,                 "minswap"},
                {DFIPKeys::RewardPct,               "reward_pct"},
                {DFIPKeys::BlockPeriod,             "block_period"},
                {DFIPKeys::DUSDInterestBurn,        "dusd_interest_burn"},
                {DFIPKeys::DUSDLoanBurn,            "dusd_loan_burn"},
                {DFIPKeys::StartBlock,              "start_block"},
            }
        },
        {
            AttributeTypes::Live, {
                {EconomyKeys::PaybackDFITokens,  "dfi_payback_tokens"},
                {EconomyKeys::DFIP2203Current,   "dfip2203_current"},
                {EconomyKeys::DFIP2203Burned,    "dfip2203_burned"},
                {EconomyKeys::DFIP2203Minted,    "dfip2203_minted"},
                {EconomyKeys::DexTokens,         "dex"},
                {EconomyKeys::DFIP2206FCurrent,   "dfip2206f_current"},
                {EconomyKeys::DFIP2206FBurned,    "dfip2206f_burned"},
                {EconomyKeys::DFIP2206FMinted,    "dfip2206f_minted"},
            }
        },
    };
    return keys;
}

static ResVal<int32_t> VerifyInt32(const std::string& str) {
    int32_t int32;
    Require(ParseInt32(str, &int32), "Value must be an integer");
    return {int32, Res::Ok()};
}

static ResVal<int32_t> VerifyPositiveInt32(const std::string& str) {
    int32_t int32;
    Require(ParseInt32(str, &int32) && int32 >= 0, "Value must be a positive integer");
    return {int32, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyInt64(const std::string& str) {
    CAmount int64;
    Require(ParseInt64(str, &int64) && int64 >= 0, "Value must be a positive integer");
    return {int64, Res::Ok()};
}


static ResVal<CAttributeValue> VerifyFloat(const std::string& str) {
    CAmount amount = 0;
    Require(ParseFixedPoint(str, 8, &amount),"Amount must be a valid number");
    return {amount, Res::Ok()};
}

ResVal<CAttributeValue> VerifyPositiveFloat(const std::string& str) {
    CAmount amount = 0;
    Require(ParseFixedPoint(str, 8, &amount) && amount >= 0, "Amount must be a positive value");
    return {amount, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyPct(const std::string& str) {
    auto resVal = VerifyPositiveFloat(str);
    Require(resVal);
    Require(std::get<CAmount>(*resVal) <= COIN, "Percentage exceeds 100%%");
    return resVal;
}

static ResVal<CAttributeValue> VerifyBool(const std::string& str) {
    Require(str == "true" || str == "false", R"(Boolean value must be either "true" or "false")");
    return {str == "true", Res::Ok()};
}

static ResVal<CAttributeValue> VerifySplit(const std::string& str) {
    OracleSplits splits;
    const auto pairs = KeyBreaker(str);
    Require(pairs.size() == 2, "Two int values expected for split in id/mutliplier");
    const auto resId = VerifyPositiveInt32(pairs[0]);
    Require(resId);

    const auto resMultiplier = VerifyInt32(pairs[1]);
    Require(resMultiplier);
    Require(*resMultiplier != 0, "Mutliplier cannot be zero");

    splits[*resId] = *resMultiplier;

    return {splits, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyCurrencyPair(const std::string& str) {
    const auto value = KeyBreaker(str);
    Require(value.size() == 2, "Exactly two entires expected for currency pair");

    auto token = trim_all_ws(value[0]).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    auto currency = trim_all_ws(value[1]).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    Require(!token.empty() && !currency.empty(), "Empty token / currency");
    return {CTokenCurrencyPair{token, currency}, Res::Ok()};
}

static std::set<std::string> dirSet{"both", "in", "out"};

static ResVal<CAttributeValue> VerifyFeeDirection(const std::string& str) {
    auto lowerStr = ToLower(str);
    const auto it = dirSet.find(lowerStr);
    Require(it != dirSet.end(), "Fee direction value must be both, in or out");
    return {CFeeDir{static_cast<uint8_t>(std::distance(dirSet.begin(), it))}, Res::Ok()};
}

static bool VerifyToken(const CCustomCSView& view, const uint32_t id) {
    return view.GetToken(DCT_ID{id}).has_value();
}

static inline void rtrim(std::string& s, unsigned char remove) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [&remove](unsigned char ch) {
        return ch != remove;
    }).base(), s.end());
}

const std::map<uint8_t, std::map<uint8_t,
    std::function<ResVal<CAttributeValue>(const std::string&)>>>& ATTRIBUTES::parseValue() {

    static const std::map<uint8_t, std::map<uint8_t,
        std::function<ResVal<CAttributeValue>(const std::string&)>>> parsers{
        {
            AttributeTypes::Token, {
                {TokenKeys::PaybackDFI,            VerifyBool},
                {TokenKeys::PaybackDFIFeePCT,      VerifyPct},
                {TokenKeys::LoanPayback,           VerifyBool},
                {TokenKeys::LoanPaybackFeePCT,     VerifyPct},
                {TokenKeys::DexInFeePct,           VerifyPct},
                {TokenKeys::DexOutFeePct,          VerifyPct},
                {TokenKeys::FixedIntervalPriceId,  VerifyCurrencyPair},
                {TokenKeys::LoanCollateralEnabled, VerifyBool},
                {TokenKeys::LoanCollateralFactor,  VerifyPct},
                {TokenKeys::LoanMintingEnabled,    VerifyBool},
                {TokenKeys::LoanMintingInterest,   VerifyFloat},
                {TokenKeys::DFIP2203Enabled,       VerifyBool},
            }
        },
        {
            AttributeTypes::Poolpairs, {
                {PoolKeys::TokenAFeePCT,      VerifyPct},
                {PoolKeys::TokenAFeeDir,      VerifyFeeDirection},
                {PoolKeys::TokenBFeePCT,      VerifyPct},
                {PoolKeys::TokenBFeeDir,      VerifyFeeDirection},
            }
        },
        {
            AttributeTypes::Param, {
                {DFIPKeys::Active,                  VerifyBool},
                {DFIPKeys::Premium,                 VerifyPct},
                {DFIPKeys::MinSwap,                 VerifyPositiveFloat},
                {DFIPKeys::RewardPct,               VerifyPct},
                {DFIPKeys::BlockPeriod,             VerifyInt64},
                {DFIPKeys::DUSDInterestBurn,  VerifyBool},
                {DFIPKeys::DUSDLoanBurn,      VerifyBool},
                {DFIPKeys::StartBlock,              VerifyInt64},
            }
        },
        {
            AttributeTypes::Locks, {
                {ParamIDs::TokenID,          VerifyBool},
            }
        },
        {
            AttributeTypes::Oracles, {
                {OracleIDs::Splits,          VerifySplit},
            }
        },
    };
    return parsers;
}

ResVal<CScript> GetFutureSwapContractAddress(const std::string& contract) {
    CScript contractAddress;
    try {
        contractAddress = Params().GetConsensus().smartContracts.at(contract);
    } catch (const std::out_of_range&) {
        return Res::Err("Failed to get smart contract address from chainparams");
    }
    return {contractAddress, Res::Ok()};
}

static std::string ShowError(const std::string& key, const std::map<std::string, uint8_t>& keys) {
    std::string error{"Unrecognised " + key + " argument provided, valid " + key + "s are:"};
    for (const auto& pair : keys) {
        error += ' ' + pair.first + ',';
    }
    return error;
}

Res ATTRIBUTES::ProcessVariable(const std::string& key, const std::string& value,
                                std::function<Res(const CAttributeType&, const CAttributeValue&)> applyVariable) {

    Require(key.size() <= 128, "Identifier exceeds maximum length (128)");

    const auto keys = KeyBreaker(key);
    Require(!keys.empty() && !keys[0].empty(), "Empty version");

    Require(!value.empty(), "Empty value");

    auto iver = allowedVersions().find(keys[0]);
    Require(iver != allowedVersions().end(), "Unsupported version");

    auto version = iver->second;
    Require(version == VersionTypes::v0, "Unsupported version");

    Require(keys.size() >= 4 && !keys[1].empty() && !keys[2].empty() && !keys[3].empty(),
              "Incorrect key for <type>. Object of ['<version>/<type>/ID/<key>','value'] expected");

    auto itype = allowedTypes().find(keys[1]);
    Require(itype != allowedTypes().end(), ::ShowError("type", allowedTypes()));

    const auto type = itype->second;

    uint32_t typeId{0};
    if (type == AttributeTypes::Param) {
        auto id = allowedParamIDs().find(keys[2]);
        Require(id != allowedParamIDs().end(), ::ShowError("param", allowedParamIDs()));
        typeId = id->second;
    } else if (type == AttributeTypes::Locks) {
        auto id = allowedLocksIDs().find(keys[2]);
        Require(id != allowedLocksIDs().end(), ::ShowError("locks", allowedLocksIDs()));
        typeId = id->second;
    } else if (type == AttributeTypes::Oracles) {
        auto id = allowedOracleIDs().find(keys[2]);
        Require(id != allowedOracleIDs().end(), ::ShowError("oracles", allowedOracleIDs()));
        typeId = id->second;
    } else {
        auto id = VerifyInt32(keys[2]);
        Require(id);
        typeId = *id.val;
    }

    uint32_t typeKey{0};
    CDataStructureV0 attrV0{};

    if (type == AttributeTypes::Locks) {
        typeKey = ParamIDs::TokenID;
        if (const auto keyValue = VerifyInt32(keys[3])) {
            attrV0 = CDataStructureV0{type, typeId, static_cast<uint32_t>(*keyValue)};
        }
    } else if (type == AttributeTypes::Oracles) {
        typeKey = OracleIDs::Splits;
        if (const auto keyValue = VerifyPositiveInt32(keys[3])) {
            attrV0 = CDataStructureV0{type, typeId, static_cast<uint32_t>(*keyValue)};
        }
    } else {
        auto ikey = allowedKeys().find(type);
        Require(ikey != allowedKeys().end(), "Unsupported type {%d}", type);

        // Alias of reward_pct in Export.
        if (keys[3] == "fee_pct") {
            return Res::Ok();
        }

        itype = ikey->second.find(keys[3]);
        Require(itype != ikey->second.end(), ::ShowError("key", ikey->second));

        typeKey = itype->second;

        if (type == AttributeTypes::Param) {
            if (typeId == ParamIDs::DFIP2201) {
                Require(typeKey == DFIPKeys::Active
                    || typeKey == DFIPKeys::Premium
                    || typeKey == DFIPKeys::MinSwap,
                    "Unsupported type for DFIP2201 {%d}", typeKey);
            } else if (typeId == ParamIDs::DFIP2203 ||
                       typeId == ParamIDs::DFIP2206F) {
                Require(typeKey == DFIPKeys::Active
                     || typeKey == DFIPKeys::RewardPct
                     || typeKey == DFIPKeys::BlockPeriod
                     || typeKey == DFIPKeys::StartBlock,
                     "Unsupported type for this DFIP {%d}", typeKey);

                if (typeKey == DFIPKeys::BlockPeriod ||
                    typeKey == DFIPKeys::StartBlock) {
                    if (typeId == ParamIDs::DFIP2203) {
                        futureUpdated = true;
                    } else {
                        futureDUSDUpdated = true;
                    }
                }
            } else if (typeId == ParamIDs::DFIP2206A) {
                Require(typeKey == DFIPKeys::DUSDInterestBurn
                     || typeKey == DFIPKeys::DUSDLoanBurn,
                     "Unsupported type for DFIP2206A {%d}", typeKey);
            }  else {
                return Res::Err("Unsupported Param ID");
            }
        }

        attrV0 = CDataStructureV0{type, typeId, typeKey};
    }

    if (attrV0.IsExtendedSize()) {
        Require(keys.size() == 5 && !keys[4].empty(), "Exact 5 keys are required {%d}", keys.size());
        auto id = VerifyInt32(keys[4]);
        Require(id);
        attrV0.keyId = *id.val;
    } else {
        Require(keys.size() == 4, "Exact 4 keys are required {%d}", keys.size());
    }

    try {
        if (auto parser = parseValue().at(type).at(typeKey)) {
            auto attribValue = parser(value);
            Require(attribValue);
            return applyVariable(attrV0, *attribValue.val);
        }
    } catch (const std::out_of_range&) {
    }
    return Res::Err("No parse function {%d, %d}", type, typeKey);
}

Res ATTRIBUTES::RefundFuturesContracts(CCustomCSView &mnview, const uint32_t height, const uint32_t tokenID)
{
    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::BlockPeriod};
    const auto blockPeriod = GetValue(blockKey, CAmount{});
    if (blockPeriod == 0) {
        return Res::Ok();
    }

    std::map<CFuturesUserKey, CFuturesUserValue> userFuturesValues;

    mnview.ForEachFuturesUserValues([&](const CFuturesUserKey& key, const CFuturesUserValue& futuresValues) {
        if (tokenID != std::numeric_limits<uint32_t>::max()) {
            if (futuresValues.source.nTokenId.v == tokenID || futuresValues.destination == tokenID) {
                userFuturesValues[key] = futuresValues;
            }
        } else {
            userFuturesValues[key] = futuresValues;
        }

        return true;
    }, {height, {}, std::numeric_limits<uint32_t>::max()});

    const auto contractAddressValue = GetFutureSwapContractAddress(SMART_CONTRACT_DFIP_2203);
    if (!contractAddressValue) {
        return contractAddressValue;
    }

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Current};
    auto balances = GetValue(liveKey, CBalances{});

    CAccountHistoryStorage* historyStore{mnview.GetAccountHistoryStore()};
    const auto currentHeight = mnview.GetLastHeight() + 1;

    for (const auto& [key, value] : userFuturesValues) {

        mnview.EraseFuturesUserValues(key);

        CHistoryWriters subWriters{historyStore, nullptr, nullptr};
        CAccountsHistoryWriter subView(mnview, currentHeight, GetNextAccPosition(), {}, uint8_t(CustomTxType::FutureSwapRefund), &subWriters);

        Require(subView.SubBalance(*contractAddressValue, value.source));
        subView.Flush();

        CHistoryWriters addWriters{historyStore, nullptr, nullptr};
        CAccountsHistoryWriter addView(mnview, currentHeight, GetNextAccPosition(), {}, uint8_t(CustomTxType::FutureSwapRefund), &addWriters);

        Require(addView.AddBalance(key.owner, value.source));
        addView.Flush();

        Require(balances.Sub(value.source));
    }

    SetValue(liveKey, std::move(balances));

    return Res::Ok();
}

Res ATTRIBUTES::RefundFuturesDUSD(CCustomCSView &mnview, const uint32_t height)
{
    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2206F, DFIPKeys::BlockPeriod};
    const auto blockPeriod = GetValue(blockKey, CAmount{});
    if (blockPeriod == 0) {
        return Res::Ok();
    }

    std::map<CFuturesUserKey, CAmount> userFuturesValues;

    mnview.ForEachFuturesDUSD([&](const CFuturesUserKey& key, const CAmount& amount) {
        userFuturesValues[key] = amount;
        return true;
    }, {height, {}, std::numeric_limits<uint32_t>::max()});

    const auto contractAddressValue = GetFutureSwapContractAddress(SMART_CONTRACT_DFIP2206F);
    if (!contractAddressValue) {
        return contractAddressValue;
    }

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2206FCurrent};
    auto balances = GetValue(liveKey, CBalances{});

    for (const auto& [key, amount] : userFuturesValues) {

        mnview.EraseFuturesDUSD(key);

        CHistoryWriters subWriters{paccountHistoryDB.get(), nullptr, nullptr};
        CAccountsHistoryWriter subView(mnview, height, GetNextAccPosition(), {}, uint8_t(CustomTxType::FutureSwapRefund), &subWriters);
        Require(subView.SubBalance(*contractAddressValue, {DCT_ID{}, amount}));
        subView.Flush();

        CHistoryWriters addWriters{paccountHistoryDB.get(), nullptr, nullptr};
        CAccountsHistoryWriter addView(mnview, height, GetNextAccPosition(), {}, uint8_t(CustomTxType::FutureSwapRefund), &addWriters);
        Require(addView.AddBalance(key.owner, {DCT_ID{}, amount}));
        addView.Flush();

        Require(balances.Sub({DCT_ID{}, amount}));
    }

    SetValue(liveKey, std::move(balances));

    return Res::Ok();
}

Res ATTRIBUTES::Import(const UniValue & val) {
    Require(val.isObject(), "Object of values expected");

    std::map<std::string, UniValue> objMap;
    val.getObjMap(objMap);

    for (const auto& [key, value] : objMap) {
        Require(ProcessVariable(key, value.get_str(),
            [this](const CAttributeType& attribute, const CAttributeValue& attrValue) {
                if (const auto attrV0 = std::get_if<CDataStructureV0>(&attribute)) {
                    if (attrV0->type == AttributeTypes::Live ||
                            (attrV0->type == AttributeTypes::Token &&
                             (attrV0->key == TokenKeys::Ascendant ||
                              attrV0->key == TokenKeys::Descendant ||
                              attrV0->key == TokenKeys::Epitaph))) {
                        return Res::Err("Attribute cannot be set externally");
                    } else if (attrV0->type == AttributeTypes::Oracles && attrV0->typeId == OracleIDs::Splits) {
                        const auto splitValue = std::get_if<OracleSplits>(&attrValue);
                        Require(splitValue, "Failed to get Oracle split value");
                        Require(splitValue->size() == 1,"Invalid number of token splits, allowed only one per height!");

                        const auto& [id, multiplier] = *(splitValue->begin());
                        tokenSplits.insert(id);

                        SetValue(attribute, *splitValue);
                        return Res::Ok();
                    } else if (attrV0->type == AttributeTypes::Token && attrV0->key == TokenKeys::LoanMintingInterest) {
                        interestTokens.insert(attrV0->typeId);
                    }

                    // apply DFI via old keys
                    if (attrV0->IsExtendedSize() && attrV0->keyId == 0) {
                        auto newAttr = *attrV0;
                        if (attrV0->key == TokenKeys::LoanPayback) {
                            newAttr.key = TokenKeys::PaybackDFI;
                        } else {
                            newAttr.key = TokenKeys::PaybackDFIFeePCT;
                        }
                        SetValue(newAttr, attrValue);
                        return Res::Ok();
                    }
                }
                SetValue(attribute, attrValue);
                return Res::Ok();
        }
        ));
    }
    return Res::Ok();
}

// Keys to exclude when using the legacy filter mode, to keep things the
// same as pre 2.7.x versions, to reduce noise. Eventually, the APIs that
// cause too much noise can be deprecated and this code removed.
std::set<uint32_t> attrsVersion27TokenHiddenSet = {
    TokenKeys::LoanCollateralEnabled,
    TokenKeys::LoanCollateralFactor,
    TokenKeys::LoanMintingEnabled,
    TokenKeys::LoanMintingInterest,
    TokenKeys::FixedIntervalPriceId,
    TokenKeys::Ascendant,
    TokenKeys::Descendant,
    TokenKeys::Epitaph,
};

UniValue ATTRIBUTES::ExportFiltered(GovVarsFilter filter, const std::string &prefix) const {
    UniValue ret(UniValue::VOBJ);
    for (const auto& attribute : attributes) {
        const auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            continue;
        }
        if (filter == GovVarsFilter::LiveAttributes &&
            attrV0->type != AttributeTypes::Live) {
                continue;
        } else if (filter == GovVarsFilter::Version2Dot7) {
            if (attrV0->type == AttributeTypes::Token &&
            attrsVersion27TokenHiddenSet.find(attrV0->key) != attrsVersion27TokenHiddenSet.end())
                continue;
        }
        try {
            std::string id;
            if (attrV0->type == AttributeTypes::Param || attrV0->type == AttributeTypes::Live || attrV0->type == AttributeTypes::Locks) {
                id = displayParamsIDs().at(attrV0->typeId);
            } else if (attrV0->type == AttributeTypes::Oracles) {
                id = displayOracleIDs().at(attrV0->typeId);
            } else {
                id = KeyBuilder(attrV0->typeId);
            }

            auto const v0Key = attrV0->type == AttributeTypes::Oracles || attrV0->type == AttributeTypes::Locks ? KeyBuilder(attrV0->key) : displayKeys().at(attrV0->type).at(attrV0->key);

            auto key = KeyBuilder(displayVersions().at(VersionTypes::v0),
                                  displayTypes().at(attrV0->type),
                                  id,
                                  v0Key);

            if (attrV0->IsExtendedSize()) {
                key = KeyBuilder(key, attrV0->keyId);
            }

            if (filter == GovVarsFilter::PrefixedAttributes) {
                if (key.compare(0, prefix.size(), prefix) != 0) {
                    continue;
                }
            }

            if (const auto bool_val = std::get_if<bool>(&attribute.second)) {
                ret.pushKV(key, *bool_val ? "true" : "false");
            } else if (const auto amount = std::get_if<CAmount>(&attribute.second)) {
                if (attrV0->type == AttributeTypes::Param &&
                    (attrV0->typeId == ParamIDs::DFIP2203 || attrV0->typeId == ParamIDs::DFIP2206F) &&
                    (attrV0->key == DFIPKeys::BlockPeriod || attrV0->key == DFIPKeys::StartBlock)) {
                    ret.pushKV(key, KeyBuilder(*amount));
                } else {
                    auto decimalStr = GetDecimaleString(*amount);
                    rtrim(decimalStr, '0');
                    if (decimalStr.back() == '.') {
                        decimalStr.pop_back();
                    }
                    ret.pushKV(key, decimalStr);

                    // Create fee_pct alias of reward_pct.
                    if (v0Key == "reward_pct") {
                        const auto newKey = KeyBuilder(displayVersions().at(VersionTypes::v0),
                                                 displayTypes().at(attrV0->type),
                                                 id,
                                                 "fee_pct");
                        ret.pushKV(newKey, decimalStr);
                    }
                }
            } else if (const auto balances = std::get_if<CBalances>(&attribute.second)) {
                ret.pushKV(key, AmountsToJSON(balances->balances));
            } else if (const auto paybacks = std::get_if<CTokenPayback>(&attribute.second)) {
                UniValue result(UniValue::VOBJ);
                result.pushKV("paybackfees", AmountsToJSON(paybacks->tokensFee.balances));
                result.pushKV("paybacktokens", AmountsToJSON(paybacks->tokensPayback.balances));
                ret.pushKV(key, result);
            } else if (const auto balances = std::get_if<CDexBalances>(&attribute.second)) {
                for (const auto& pool : *balances) {
                    auto& dexTokenA = pool.second.totalTokenA;
                    auto& dexTokenB = pool.second.totalTokenB;
                    auto poolkey = KeyBuilder(key, pool.first.v);
                    ret.pushKV(KeyBuilder(poolkey, "total_commission_a"), ValueFromUint(dexTokenA.commissions));
                    ret.pushKV(KeyBuilder(poolkey, "total_commission_b"), ValueFromUint(dexTokenB.commissions));
                    ret.pushKV(KeyBuilder(poolkey, "fee_burn_a"), ValueFromUint(dexTokenA.feeburn));
                    ret.pushKV(KeyBuilder(poolkey, "fee_burn_b"), ValueFromUint(dexTokenB.feeburn));
                    ret.pushKV(KeyBuilder(poolkey, "total_swap_a"), ValueFromUint(dexTokenA.swaps));
                    ret.pushKV(KeyBuilder(poolkey, "total_swap_b"), ValueFromUint(dexTokenB.swaps));
                }
            } else if (const auto splitValues = std::get_if<OracleSplits>(&attribute.second)) {
                std::string keyValue;
                for (auto it{splitValues->begin()}; it != splitValues->end(); ++it) {
                    if (it != splitValues->begin()) {
                        keyValue += ',';
                    }
                    keyValue += KeyBuilder(it->first, it->second);
                }
                ret.pushKV(key, keyValue);
            } else if (const auto& descendantPair = std::get_if<DescendantValue>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(descendantPair->first, descendantPair->second));
            } else if (const auto& ascendantPair = std::get_if<AscendantValue>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(ascendantPair->first, ascendantPair->second));
            } else if (const auto currencyPair = std::get_if<CTokenCurrencyPair>(&attribute.second)) {
                ret.pushKV(key, currencyPair->first + '/' + currencyPair->second);
            } else if (const auto result = std::get_if<CFeeDir>(&attribute.second)) {
                if (result->feeDir == FeeDirValues::Both) {
                    ret.pushKV(key, "both");
                } else if (result->feeDir == FeeDirValues::In) {
                    ret.pushKV(key, "in");
                } else if (result->feeDir == FeeDirValues::Out) {
                    ret.pushKV(key, "out");
                }
            }
        } catch (const std::out_of_range&) {
            // Should not get here, that's mean maps are mismatched
        }
    }
    return ret;
}

UniValue ATTRIBUTES::Export() const {
    return ExportFiltered(GovVarsFilter::All, "");
}

Res ATTRIBUTES::Validate(const CCustomCSView & view) const
{
    Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningHillHeight, "Cannot be set before FortCanningHill");

    for (const auto& [key, value] : attributes) {
        const auto attrV0 = std::get_if<CDataStructureV0>(&key);
        Require(attrV0, "Unsupported version");
        switch (attrV0->type) {
            case AttributeTypes::Token:
                switch (attrV0->key) {
                    case TokenKeys::PaybackDFI:
                    case TokenKeys::PaybackDFIFeePCT:
                        Require(view.GetLoanTokenByID({attrV0->typeId}), "No such loan token (%d)", attrV0->typeId);
                    break;
                    case TokenKeys::LoanPayback:
                    case TokenKeys::LoanPaybackFeePCT:
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight,"Cannot be set before FortCanningRoad");
                        Require(view.GetLoanTokenByID(DCT_ID{attrV0->typeId}), "No such loan token (%d)", attrV0->typeId);
                        Require(view.GetToken(DCT_ID{attrV0->keyId}), "No such token (%d)", attrV0->keyId);
                    break;
                    case TokenKeys::DexInFeePct:
                    case TokenKeys::DexOutFeePct:
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight, "Cannot be set before FortCanningRoad");
                        Require(view.GetToken(DCT_ID{attrV0->typeId}), "No such token (%d)", attrV0->typeId);
                    break;
                    case TokenKeys::LoanMintingInterest:
                        if (view.GetLastHeight() < Params().GetConsensus().FortCanningGreatWorldHeight) {
                            const auto amount = std::get_if<CAmount>(&value);
                            Require(amount && *amount >= 0, "Amount must be a positive value");
                        }
                        [[fallthrough]];
                    case TokenKeys::LoanCollateralEnabled:
                    case TokenKeys::LoanCollateralFactor:
                    case TokenKeys::LoanMintingEnabled: {
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningCrunchHeight, "Cannot be set before FortCanningCrunch");
                        Require(VerifyToken(view, attrV0->typeId), "No such token (%d)", attrV0->typeId);
                        CDataStructureV0 intervalPriceKey{AttributeTypes::Token, attrV0->typeId,
                                                          TokenKeys::FixedIntervalPriceId};
                        Require(!(GetValue(intervalPriceKey, CTokenCurrencyPair{}) == CTokenCurrencyPair{}), "Fixed interval price currency pair must be set first");
                        break;
                    }
                    case TokenKeys::FixedIntervalPriceId:
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningCrunchHeight, "Cannot be set before FortCanningCrunch");
                        Require(VerifyToken(view, attrV0->typeId), "No such token (%d)", attrV0->typeId);
                        break;
                    case TokenKeys::DFIP2203Enabled:
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight, "Cannot be set before FortCanningRoad");
                        Require(view.GetLoanTokenByID(DCT_ID{attrV0->typeId}), "No such loan token (%d)", attrV0->typeId);
                    break;
                    case TokenKeys::Ascendant:
                    case TokenKeys::Descendant:
                    case TokenKeys::Epitaph:
                    break;
                    default:
                        return Res::Err("Unsupported key");
                }
            break;

            case AttributeTypes::Oracles:
                Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningCrunchHeight, "Cannot be set before FortCanningCrunch");
                if (attrV0->typeId == OracleIDs::Splits) {
                    const auto splitMap = std::get_if<OracleSplits>(&value);
                    Require(splitMap, "Unsupported value");

                    for (const auto& [tokenId, multipler] : *splitMap) {
                        Require(tokenId != 0, "Tokenised DFI cannot be split");
                        Require(!view.HasPoolPair(DCT_ID{tokenId}), "Pool tokens cannot be split");
                        const auto token = view.GetToken(DCT_ID{tokenId});
                        Require(token, "Token (%d) does not exist", tokenId);
                        Require(token->IsDAT(), "Only DATs can be split");
                        Require(view.GetLoanTokenByID(DCT_ID{tokenId}).has_value(), "No loan token with id (%d)", tokenId);
                    }
                } else {
                    return Res::Err("Unsupported key");
                }
            break;

            case AttributeTypes::Poolpairs:
                switch (attrV0->key) {
                    case PoolKeys::TokenAFeePCT:
                    case PoolKeys::TokenBFeePCT:
                        Require(view.GetPoolPair({attrV0->typeId}), "No such pool (%d)", attrV0->typeId);
                    break;
                    case PoolKeys::TokenAFeeDir:
                    case PoolKeys::TokenBFeeDir:
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningSpringHeight, "Cannot be set before FortCanningSpringHeight");
                        Require(view.GetPoolPair({attrV0->typeId}), "No such pool (%d)", attrV0->typeId);
                    break;
                    default:
                        return Res::Err("Unsupported key");
                }
            break;

            case AttributeTypes::Param:
                if (attrV0->typeId == ParamIDs::DFIP2206F || attrV0->key == DFIPKeys::StartBlock || attrV0->typeId == ParamIDs::DFIP2206A) {
                    Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningSpringHeight, "Cannot be set before FortCanningSpringHeight");
                } else if (attrV0->typeId == ParamIDs::DFIP2203) {
                    Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight, "Cannot be set before FortCanningRoadHeight");
                } else if (attrV0->typeId != ParamIDs::DFIP2201) {
                    return Res::Err("Unrecognised param id");
                }
            break;

            // Live is set internally
            case AttributeTypes::Live:
            break;

            case AttributeTypes::Locks:
                Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningCrunchHeight, "Cannot be set before FortCanningCrunch");
                Require(attrV0->typeId == ParamIDs::TokenID, "Unrecognised locks id");
                Require(view.GetLoanTokenByID(DCT_ID{attrV0->key}).has_value(), "No loan token with id (%d)", attrV0->key);
            break;

            default:
                return Res::Err("Unrecognised type (%d)", attrV0->type);
        }
    }

    return Res::Ok();
}

Res ATTRIBUTES::Apply(CCustomCSView & mnview, const uint32_t height)
{
    for (const auto& attribute : attributes) {
        const auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            continue;
        }
        if (attrV0->type == AttributeTypes::Poolpairs) {
            if (attrV0->key == PoolKeys::TokenAFeePCT ||
                attrV0->key == PoolKeys::TokenBFeePCT) {
                auto poolId = DCT_ID{attrV0->typeId};
                auto pool = mnview.GetPoolPair(poolId);
                Require(pool, "No such pool (%d)", poolId.v);
                auto tokenId = attrV0->key == PoolKeys::TokenAFeePCT ?
                               pool->idTokenA : pool->idTokenB;

                const auto valuePct = std::get_if<CAmount>(&attribute.second);
                Require(valuePct, "Unexpected type");
                if (auto res = mnview.SetDexFeePct(poolId, tokenId, *valuePct); !res) {
                    return res;
                }
            }
        } else if (attrV0->type == AttributeTypes::Token) {
            if (attrV0->key == TokenKeys::DexInFeePct
            ||  attrV0->key == TokenKeys::DexOutFeePct) {
                DCT_ID tokenA{attrV0->typeId}, tokenB{~0u};
                if (attrV0->key == TokenKeys::DexOutFeePct) {
                    std::swap(tokenA, tokenB);
                }
                const auto valuePct = std::get_if<CAmount>(&attribute.second);
                Require(valuePct, "Unexpected type");
                if (auto res = mnview.SetDexFeePct(tokenA, tokenB, *valuePct); !res) {
                    return res;
                }
            } else if (attrV0->key == TokenKeys::FixedIntervalPriceId) {
                if (const auto &currencyPair = std::get_if<CTokenCurrencyPair>(&attribute.second)) {
                    // Already exists, skip.
                    if (auto it = mnview.LowerBound<COracleView::FixedIntervalPriceKey>(*currencyPair);
                        it.Valid() && it.Key() == *currencyPair) {
                        continue;
                    } else if (!OraclePriceFeed(mnview, *currencyPair)) {
                        return Res::Err("Price feed %s/%s does not belong to any oracle", currencyPair->first,
                                        currencyPair->second);
                    }
                    CFixedIntervalPrice fixedIntervalPrice;
                    fixedIntervalPrice.priceFeedId = *currencyPair;
                    fixedIntervalPrice.timestamp = time;
                    fixedIntervalPrice.priceRecord[1] = -1;
                    const auto aggregatePrice = GetAggregatePrice(mnview,
                                                                  fixedIntervalPrice.priceFeedId.first,
                                                                  fixedIntervalPrice.priceFeedId.second,
                                                                  time);
                    if (aggregatePrice) {
                        fixedIntervalPrice.priceRecord[1] = aggregatePrice;
                    }
                    Require(mnview.SetFixedIntervalPrice(fixedIntervalPrice));
                } else {
                    return Res::Err("Unrecognised value for FixedIntervalPriceId");
                }
            } else if (attrV0->key == TokenKeys::DFIP2203Enabled) {
                const auto value = std::get_if<bool>(&attribute.second);
                Require(value, "Unexpected type");

                if (*value) {
                    continue;
                }

                const auto token = mnview.GetLoanTokenByID(DCT_ID{attrV0->typeId});
                Require(token, "No such loan token (%d)", attrV0->typeId);

                // Special case: DUSD will be used as a source for swaps but will
                // be set as disabled for Future swap destination.
                if (token->symbol == "DUSD") {
                    continue;
                }

                Require(RefundFuturesContracts(mnview, height, attrV0->typeId));
            } else if (attrV0->key == TokenKeys::LoanMintingInterest) {
                if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningGreatWorldHeight) && interestTokens.count(attrV0->typeId)) {
                    const auto tokenInterest = std::get_if<CAmount>(&attribute.second);
                    Require(tokenInterest, "Unexpected type");

                    std::set<CVaultId> affectedVaults;
                    mnview.ForEachLoanTokenAmount([&](const CVaultId& vaultId,  const CBalances& balances){
                        for (const auto& [tokenId, discarded] : balances.balances) {
                            if (tokenId.v == attrV0->typeId) {
                                affectedVaults.insert(vaultId);
                            }
                        }
                        return true;
                    });

                    for (const auto& vaultId : affectedVaults) {
                        const auto vault = mnview.GetVault(vaultId);
                        assert(vault);

                        // Updated stored interest with new interest rate.
                        mnview.IncreaseInterest(height, vaultId, vault->schemeId, {attrV0->typeId}, *tokenInterest, 0);
                    }
                }
            }
        } else if (attrV0->type == AttributeTypes::Param) {
            if (attrV0->typeId == ParamIDs::DFIP2203) {
                if (attrV0->key == DFIPKeys::Active) {
                    const auto value = std::get_if<bool>(&attribute.second);
                    Require(value, "Unexpected type");

                    if (*value) {
                        continue;
                    }

                    Require(RefundFuturesContracts(mnview, height));
                } else if (attrV0->key == DFIPKeys::BlockPeriod || attrV0->key == DFIPKeys::StartBlock) {

                    // Only check this when block period has been set, otherwise
                    // it will fail when DFIP2203 active is set to true.
                    if (!futureUpdated) {
                        continue;
                    }

                    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::Active};
                    Require(GetValue(activeKey, false), "Cannot set block period while DFIP2203 is active");
                }
            } else if (attrV0->typeId == ParamIDs::DFIP2206F) {
                if (attrV0->key == DFIPKeys::Active) {
                    const auto value = std::get_if<bool>(&attribute.second);
                    Require(value, "Unexpected type");

                    if (*value) {
                        continue;
                    }

                    Require(RefundFuturesDUSD(mnview, height));
                } else if (attrV0->key == DFIPKeys::BlockPeriod) {

                    // Only check this when block period has been set, otherwise
                    // it will fail when DFIP2206F active is set to true.
                    if (!futureDUSDUpdated) {
                        continue;
                    }

                    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2206F, DFIPKeys::Active};
                    Require(GetValue(activeKey, false), "Cannot set block period while DFIP2206F is active");
                }
            }

        } else if (attrV0->type == AttributeTypes::Oracles && attrV0->typeId == OracleIDs::Splits) {
            const auto value = std::get_if<OracleSplits>(&attribute.second);
            Require(value, "Unsupported value");
            for (const auto split : tokenSplits) {
                if (auto it{value->find(split)}; it == value->end()) {
                    continue;
                }

                Require(attrV0->key > height, "Cannot be set at or below current height");

                CDataStructureV0 lockKey{AttributeTypes::Locks, ParamIDs::TokenID, split};
                if (GetValue(lockKey, false)) {
                    continue;
                }

                Require(mnview.GetLoanTokenByID(DCT_ID{split}).has_value(), "Auto lock. No loan token with id (%d)", split);

                const auto startHeight = attrV0->key - Params().GetConsensus().blocksPerDay() / 2;
                if (height < startHeight) {
                    auto var = GovVariable::Create("ATTRIBUTES");
                    Require(var, "Failed to create Gov var for lock");

                    auto govVar = std::dynamic_pointer_cast<ATTRIBUTES>(var);
                    Require(govVar, "Failed to cast Gov var to ATTRIBUTES");
                    govVar->attributes[lockKey] = true;

                    CGovernanceHeightMessage lock;
                    lock.startHeight = startHeight;
                    lock.govVar = govVar;

                    Require(storeGovVars(lock, mnview));
                } else {
                    // Less than a day's worth of blocks, apply instant lock
                    SetValue(lockKey, true);
                }
            }
        }
    }

    return Res::Ok();
}
