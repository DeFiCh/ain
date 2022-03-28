// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/attributes.h>

#include <masternodes/accountshistory.h> /// CAccountsHistoryWriter
#include <masternodes/masternodes.h> /// CCustomCSView
#include <masternodes/mn_checks.h> /// GetAggregatePrice

#include <core_io.h> /// ValueFromAmount
#include <util/strencodings.h>

extern UniValue AmountsToJSON(TAmounts const & diffs);

static inline std::string trim_all_ws(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
    return s;
}

static std::vector<std::string> KeyBreaker(const std::string& str){
    std::string section;
    std::istringstream stream(str);
    std::vector<std::string> strVec;

    while (std::getline(stream, section, '/')) {
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
        {ParamIDs::TokenID,     "token"},
        {ParamIDs::Economy,     "economy"},
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
                {"token_a_fee_pct",     PoolKeys::TokenAFeePCT},
                {"token_b_fee_pct",     PoolKeys::TokenBFeePCT},
            }
        },
        {
            AttributeTypes::Param, {
                {"active",              DFIPKeys::Active},
                {"minswap",             DFIPKeys::MinSwap},
                {"premium",             DFIPKeys::Premium},
                {"reward_pct",          DFIPKeys::RewardPct},
                {"block_period",        DFIPKeys::BlockPeriod},
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
                {TokenKeys::DFIP2203Enabled,       "dfip2203"},
                {TokenKeys::FixedIntervalPriceId,  "fixed_interval_price_id"},
                {TokenKeys::LoanCollateralEnabled, "loan_collateral_enabled"},
                {TokenKeys::LoanCollateralFactor,  "loan_collateral_factor"},
                {TokenKeys::LoanMintingEnabled,    "loan_minting_enabled"},
                {TokenKeys::LoanMintingInterest,   "loan_minting_interest"},
            }
        },
        {
            AttributeTypes::Poolpairs, {
                {PoolKeys::TokenAFeePCT,      "token_a_fee_pct"},
                {PoolKeys::TokenBFeePCT,      "token_b_fee_pct"},
            }
        },
        {
            AttributeTypes::Param, {
                {DFIPKeys::Active,       "active"},
                {DFIPKeys::Premium,      "premium"},
                {DFIPKeys::MinSwap,      "minswap"},
                {DFIPKeys::RewardPct,    "reward_pct"},
                {DFIPKeys::BlockPeriod,  "block_period"},
            }
        },
        {
            AttributeTypes::Live, {
                {EconomyKeys::PaybackDFITokens,  "dfi_payback_tokens"},
                {EconomyKeys::DFIP2203Current,   "dfip2203_current"},
                {EconomyKeys::DFIP2203Burned,    "dfip2203_burned"},
                {EconomyKeys::DFIP2203Minted,    "dfip2203_minted"},
                {EconomyKeys::DexTokens,         "dex"},
            }
        },
    };
    return keys;
}

static ResVal<int32_t> VerifyInt32(const std::string& str) {
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
    Require(ParseFixedPoint(str, 8, &amount) && amount >= 0, "Amount must be a positive value");
    return {amount, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyPct(const std::string& str) {
    auto resVal = VerifyFloat(str);
    Require(resVal);
    Require(std::get<CAmount>(*resVal) <= COIN, "Percentage exceeds 100%%");
    return resVal;
}

static ResVal<CAttributeValue> VerifyCurrencyPair(const std::string& str) {
    const auto value = KeyBreaker(str);
    Require(value.size() == 2, "Exactly two entires expected for currency pair");

    auto token = trim_all_ws(value[0]).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    auto currency = trim_all_ws(value[1]).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

    Require(!token.empty() && !currency.empty(), "Empty token / currency");
    return {CTokenCurrencyPair{token, currency}, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyBool(const std::string& str) {
    Require(str == "true" || str == "false", R"(Boolean value must be either "true" or "false")");
    return {str == "true", Res::Ok()};
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
                {TokenKeys::DFIP2203Enabled,       VerifyBool},
                {TokenKeys::FixedIntervalPriceId,  VerifyCurrencyPair},
                {TokenKeys::LoanCollateralEnabled, VerifyBool},
                {TokenKeys::LoanCollateralFactor,  VerifyPct},
                {TokenKeys::LoanMintingEnabled,    VerifyBool},
                {TokenKeys::LoanMintingInterest,   VerifyFloat},
            }
        },
        {
            AttributeTypes::Poolpairs, {
                {PoolKeys::TokenAFeePCT,      VerifyPct},
                {PoolKeys::TokenBFeePCT,      VerifyPct},
            }
        },
        {
            AttributeTypes::Param, {
                {DFIPKeys::Active,       VerifyBool},
                {DFIPKeys::Premium,      VerifyPct},
                {DFIPKeys::MinSwap,      VerifyFloat},
                {DFIPKeys::RewardPct,    VerifyPct},
                {DFIPKeys::BlockPeriod,  VerifyInt64},
            }
        },
        {
            AttributeTypes::Locks, {
                {ParamIDs::TokenID,          VerifyBool},
            }
        },
    };
    return parsers;
}

static std::string ShowError(const std::string& key, const std::map<std::string, uint8_t>& keys) {
    std::string error{"Unrecognised " + key + " argument provided, valid " + key + "s are:"};
    for (const auto& pair : keys) {
        error += ' ' + pair.first + ',';
    }
    return error;
}

Res ATTRIBUTES::ProcessVariable(const std::string& key, const std::string& value,
                                const std::function<Res(const CAttributeType&, const CAttributeValue&)>& applyVariable) {

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

    auto type = itype->second;

    uint32_t typeId{0};
    if (type == AttributeTypes::Param) {
        auto id = allowedParamIDs().find(keys[2]);
        Require(id != allowedParamIDs().end(), ::ShowError("params", allowedParamIDs()));
        typeId = id->second;
    } else if (type == AttributeTypes::Locks) {
        auto id = allowedLocksIDs().find(keys[2]);
        Require(id != allowedLocksIDs().end(), ::ShowError("locks", allowedLocksIDs()));
        typeId = id->second;
    } else {
        auto id = VerifyInt32(keys[2]);
        Require(id);
        typeId = *id.val;
    }

    uint8_t typeKey;
    uint32_t locksKey{0};
    if (type != AttributeTypes::Locks) {
        auto ikey = allowedKeys().find(type);
        Require(ikey != allowedKeys().end(), "Unsupported type {%d}", type);

        itype = ikey->second.find(keys[3]);
        Require(itype != ikey->second.end(), ::ShowError("key", ikey->second));

        typeKey = itype->second;

        if (type == AttributeTypes::Param) {
            if (typeId == ParamIDs::DFIP2201) {
                if (typeKey == DFIPKeys::RewardPct ||
                    typeKey == DFIPKeys::BlockPeriod) {
                    return Res::Err("Unsupported type for DFIP2201 {%d}", typeKey);
                }
            } else if (typeId == ParamIDs::DFIP2203) {
                if (typeKey == DFIPKeys::Premium ||
                    typeKey == DFIPKeys::MinSwap) {
                    return Res::Err("Unsupported type for DFIP2203 {%d}", typeKey);
                }

                if (typeKey == DFIPKeys::BlockPeriod) {
                    futureBlockUpdated = true;
                }
            } else {
                return Res::Err("Unsupported Param ID");
            }
        }
    } else {
        typeKey = ParamIDs::TokenID;
        if (const auto keyValue = VerifyInt32(keys[3])) {
            locksKey = *keyValue;
        }
    }

    try {
        if (auto parser = parseValue().at(type).at(typeKey)) {
            auto attribValue = parser(value);
            Require(attribValue);

            if (type == AttributeTypes::Locks) {
                return applyVariable(CDataStructureV0{type, typeId, locksKey}, *attribValue.val);
            }

            CDataStructureV0 attrV0{type, typeId, typeKey};

            if (attrV0.IsExtendedSize()) {
                Require(keys.size() == 5 && !keys[4].empty(), "Exact 5 keys are required {%d}", keys.size());
                auto id = VerifyInt32(keys[4]);
                Require(id);
                attrV0.keyId = *id.val;
            } else {
                Require(keys.size() == 4, "Exact 4 keys are required {%d}", keys.size());
            }

            return applyVariable(attrV0, *attribValue.val);
        }
    } catch (const std::out_of_range&) {
    }
    return Res::Err("No parse function {%d, %d}", type, typeKey);
}

ResVal<CScript> GetFutureSwapContractAddress()
{
    CScript contractAddress;
    try {
        contractAddress = Params().GetConsensus().smartContracts.at(SMART_CONTRACT_DFIP_2203);
    } catch (const std::out_of_range&) {
        return Res::Err("Failed to get smart contract address from chainparams");
    }
    return {contractAddress, Res::Ok()};
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

    const auto contractAddressValue = GetFutureSwapContractAddress();
    Require(contractAddressValue);

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Current};
    auto balances = GetValue(liveKey, CBalances{});

    auto txn = std::numeric_limits<uint32_t>::max();

    for (const auto& [key, value] : userFuturesValues) {

        mnview.EraseFuturesUserValues(key);

        CHistoryWriters subWriters{paccountHistoryDB.get(), nullptr, nullptr};
        CAccountsHistoryWriter subView(mnview, height, txn--, {}, uint8_t(CustomTxType::FutureSwapRefund), &subWriters);
        Require(subView.SubBalance(*contractAddressValue, value.source));
        subView.Flush();

        CHistoryWriters addWriters{paccountHistoryDB.get(), nullptr, nullptr};
        CAccountsHistoryWriter addView(mnview, height, txn--, {}, uint8_t(CustomTxType::FutureSwapRefund), &addWriters);
        Require(addView.AddBalance(key.owner, value.source));
        addView.Flush();

        Require(balances.Sub(value.source));
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
                if (auto attrV0 = std::get_if<CDataStructureV0>(&attribute)) {
                    Require(attrV0->type != AttributeTypes::Live, "Live attribute cannot be set externally");

                    // applay DFI via old keys
                    if (attrV0->IsExtendedSize() && attrV0->keyId == 0) {
                        auto newAttr = *attrV0;
                        newAttr.key = attrV0->key == TokenKeys::LoanPayback ?
                                TokenKeys::PaybackDFI: TokenKeys::PaybackDFIFeePCT;

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

UniValue ATTRIBUTES::Export() const {
    UniValue ret(UniValue::VOBJ);
    for (const auto& attribute : attributes) {
        auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            continue;
        }
        try {
            const auto id = attrV0->type == AttributeTypes::Param
                            || attrV0->type == AttributeTypes::Live
                            || attrV0->type == AttributeTypes::Locks
                            ? displayParamsIDs().at(attrV0->typeId)
                            : KeyBuilder(attrV0->typeId);

            const auto keyId = attrV0->type == AttributeTypes::Locks
                               ? KeyBuilder(attrV0->key)
                               : displayKeys().at(attrV0->type).at(attrV0->key);

            auto key = KeyBuilder(displayVersions().at(VersionTypes::v0),
                                  displayTypes().at(attrV0->type),
                                  id,
                                  keyId);

            if (attrV0->IsExtendedSize()) {
                key = KeyBuilder(key, attrV0->keyId);
            }

            if (auto bool_val = std::get_if<bool>(&attribute.second)) {
                ret.pushKV(key, *bool_val ? "true" : "false");
            } else if (auto amount = std::get_if<CAmount>(&attribute.second)) {
                if (attrV0->typeId == DFIP2203 && attrV0->key == DFIPKeys::BlockPeriod) {
                    ret.pushKV(key, KeyBuilder(*amount));
                } else {
                    auto uvalue = ValueFromAmount(*amount);
                    ret.pushKV(key, KeyBuilder(uvalue.get_real()));
                }
            } else if (auto balances = std::get_if<CBalances>(&attribute.second)) {
                ret.pushKV(key, AmountsToJSON(balances->balances));
            } else if (auto currencyPair = std::get_if<CTokenCurrencyPair>(&attribute.second)) {
                ret.pushKV(key, currencyPair->first + '/' + currencyPair->second);
            } else if (auto paybacks = std::get_if<CTokenPayback>(&attribute.second)) {
                UniValue result(UniValue::VOBJ);
                result.pushKV("paybackfees", AmountsToJSON(paybacks->tokensFee.balances));
                result.pushKV("paybacktokens", AmountsToJSON(paybacks->tokensPayback.balances));
                ret.pushKV(key, result);
            } else if (auto balances = std::get_if<CDexBalances>(&attribute.second)) {
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
            }
        } catch (const std::out_of_range&) {
            // Should not get here, that's mean maps are mismatched
        }
    }
    return ret;
}

Res ATTRIBUTES::Validate(const CCustomCSView & view) const
{
    Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningHillHeight, "Cannot be set before FortCanningHill");

    for (const auto& attribute : attributes) {
        auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
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
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight, "Cannot be set before FortCanningRoad");
                        Require(view.GetLoanTokenByID(DCT_ID{attrV0->typeId}), "No such loan token (%d)", attrV0->typeId);
                        Require(view.GetToken(DCT_ID{attrV0->keyId}), "No such token (%d)", attrV0->keyId);
                        break;
                    case TokenKeys::DexInFeePct:
                    case TokenKeys::DexOutFeePct:
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight, "Cannot be set before FortCanningRoad");
                        Require(view.GetToken(DCT_ID{attrV0->typeId}), "No such token (%d)", attrV0->typeId);
                        break;
                    case TokenKeys::DFIP2203Enabled:
                        Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight, "Cannot be set before FortCanningRoad");
                        Require(view.GetLoanTokenByID(DCT_ID{attrV0->typeId}), "No such loan token (%d)", attrV0->typeId);
                        break;
                    case TokenKeys::LoanCollateralEnabled:
                    case TokenKeys::LoanCollateralFactor:
                    case TokenKeys::LoanMintingEnabled:
                    case TokenKeys::LoanMintingInterest: {
                        Require(view.GetLastHeight() >= Params().GetConsensus().GreatWorldHeight, "Cannot be set before GreatWorld");
                        Require(view.GetToken(DCT_ID{attrV0->typeId}), "No such token (%d)", attrV0->typeId);

                        CDataStructureV0 intervalPriceKey{AttributeTypes::Token, attrV0->typeId,
                                                          TokenKeys::FixedIntervalPriceId};
                        Require(CheckKey(intervalPriceKey), "Fixed interval price currency pair must be set first");
                        break;
                    }
                    case TokenKeys::FixedIntervalPriceId:
                        Require(view.GetLastHeight() >= Params().GetConsensus().GreatWorldHeight, "Cannot be set before GreatWorld");
                        Require(view.GetToken(DCT_ID{attrV0->typeId}), "No such token (%d)", attrV0->typeId);
                        break;
                    default:
                        return Res::Err("Unsupported key");
                }
                break;

            case AttributeTypes::Poolpairs:
                Require(std::get_if<CAmount>(&attribute.second), "Unsupported value");
                switch (attrV0->key) {
                    case PoolKeys::TokenAFeePCT:
                    case PoolKeys::TokenBFeePCT:
                        Require(view.GetPoolPair({attrV0->typeId}), "No such pool (%d)", attrV0->typeId);
                        break;
                    default:
                        return Res::Err("Unsupported key");
                }
                break;

            case AttributeTypes::Param:
                if (attrV0->typeId == ParamIDs::DFIP2203) {
                    Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight, "Cannot be set before FortCanningRoad");
                } else {
                    Require(attrV0->typeId == ParamIDs::DFIP2201, "Unrecognised param id");
                }
                break;

                // Live is set internally
            case AttributeTypes::Live:
                break;

            case AttributeTypes::Locks:
                Require(view.GetLastHeight() >= Params().GetConsensus().GreatWorldHeight, "Cannot be set before GreatWorld");
                Require(attrV0->typeId == ParamIDs::TokenID, "Unrecognised locks id");
                Require(view.GetLoanTokenByID(DCT_ID{attrV0->key}), "No loan token with id (%d)", attrV0->key);
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
        auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            continue;
        }
        if (attrV0->type == AttributeTypes::Poolpairs) {
            auto poolId = DCT_ID{attrV0->typeId};
            auto pool = mnview.GetPoolPair(poolId);
            Require(pool, "No such pool (%d)", poolId.v);

            auto tokenId = attrV0->key == PoolKeys::TokenAFeePCT ?
                           pool->idTokenA : pool->idTokenB;

            auto valuePct = std::get<CAmount>(attribute.second);
            Require(mnview.SetDexFeePct(poolId, tokenId, valuePct));

        } else if (attrV0->type == AttributeTypes::Token) {
            if (attrV0->key == TokenKeys::DexInFeePct
            ||  attrV0->key == TokenKeys::DexOutFeePct) {
                DCT_ID tokenA{attrV0->typeId}, tokenB{~0u};
                if (attrV0->key == TokenKeys::DexOutFeePct)
                    std::swap(tokenA, tokenB);

                auto valuePct = std::get<CAmount>(attribute.second);
                Require(mnview.SetDexFeePct(tokenA, tokenB, valuePct));

            } else if (attrV0->key == TokenKeys::FixedIntervalPriceId) {
                if (const auto& currencyPair = std::get_if<CTokenCurrencyPair>(&attribute.second)) {
                    // Already exists, skip.
                    if (mnview.GetFixedIntervalPrice(*currencyPair))
                        continue;

                    Require(OraclePriceFeed(mnview, *currencyPair),
                              "Price feed %s/%s does not belong to any oracle", currencyPair->first, currencyPair->second);

                    CFixedIntervalPrice fixedIntervalPrice;
                    fixedIntervalPrice.priceFeedId = *currencyPair;
                    fixedIntervalPrice.timestamp = time;
                    fixedIntervalPrice.priceRecord[1] = -1;
                    const auto aggregatePrice = GetAggregatePrice(mnview,
                                                                  fixedIntervalPrice.priceFeedId.first,
                                                                  fixedIntervalPrice.priceFeedId.second,
                                                                  time);
                    if (aggregatePrice)
                        fixedIntervalPrice.priceRecord[1] = aggregatePrice;

                    mnview.SetFixedIntervalPrice(fixedIntervalPrice);
                } else {
                    return Res::Err("Unrecognised value for FixedIntervalPriceId");
                }
            }
            if (attrV0->key == TokenKeys::DFIP2203Enabled) {

                // Skip on block period change to avoid refunding and erasing entries.
                // Block period change will check for conflicting entries, deleting them
                // via RefundFuturesContracts will fail that check.
                if (futureBlockUpdated) {
                    continue;
                }

                auto value = std::get<bool>(attribute.second);
                if (value) {
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
            }
        } else if (attrV0->type == AttributeTypes::Param && attrV0->typeId == ParamIDs::DFIP2203) {
            if (attrV0->key == DFIPKeys::Active) {

                // Skip on block period change to avoid refunding and erasing entries.
                // Block period change will check for conflicting entries, deleting them
                // via RefundFuturesContracts will fail that check.
                if (futureBlockUpdated) {
                    continue;
                }

                auto value = std::get<bool>(attribute.second);
                if (value) {
                    continue;
                }

                Require(RefundFuturesContracts(mnview, height));

            } else if (attrV0->key == DFIPKeys::BlockPeriod) {

                // Only check this when block period has been set, otherwise
                // it will fail when DFIP2203 active is set to true.
                if (!futureBlockUpdated) {
                    continue;
                }

                CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::Active};
                Require(!GetValue(activeKey, false), "Cannot set block period while DFIP2203 is active");
            }
        }
    }

    return Res::Ok();
}
