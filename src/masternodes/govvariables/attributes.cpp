// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/attributes.h>

#include <masternodes/accountshistory.h> /// CAccountsHistoryWriter
#include <masternodes/masternodes.h> /// CCustomCSView
#include <masternodes/mn_checks.h> /// GetAggregatePrice
#include <masternodes/mn_rpc.h>

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
                {"consortium_members",      TokenKeys::ConsortiumMembers},
                {"consortium_mint_limit",   TokenKeys::ConsortiumMintLimit},
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
                {TokenKeys::ConsortiumMembers,     "consortium_members"},
                {TokenKeys::ConsortiumMintLimit,   "consortium_mint_limit"},
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
                {EconomyKeys::PaybackDFITokens,         "dfi_payback_tokens"},
                {EconomyKeys::DFIP2203Current,          "dfip2203_current"},
                {EconomyKeys::DFIP2203Burned,           "dfip2203_burned"},
                {EconomyKeys::DFIP2203Minted,           "dfip2203_minted"},
                {EconomyKeys::DexTokens,                "dex"},
                {EconomyKeys::ConsortiumMinted,         "consortium_minted"},
                {EconomyKeys::ConsortiumMembersMinted,  "consortium_members_minted"},
            }
        },
    };
    return keys;
}

static ResVal<int32_t> VerifyInt32(const std::string& str) {
    int32_t int32;
    if (!ParseInt32(str, &int32) || int32 < 0) {
        return Res::Err("Value must be a positive integer");
    }
    return {int32, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyInt64(const std::string& str) {
    CAmount int64;
    if (!ParseInt64(str, &int64) || int64 < 0) {
        return Res::Err("Value must be a positive integer");
    }
    return {int64, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyFloat(const std::string& str) {
    CAmount amount = 0;
    if (!ParseFixedPoint(str, 8, &amount) || amount < 0) {
        return Res::Err("Amount must be a positive value");
    }
    return {amount, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyPct(const std::string& str) {
    auto resVal = VerifyFloat(str);
    if (!resVal) {
        return resVal;
    }
    if (std::get<CAmount>(*resVal.val) > COIN) {
        return Res::Err("Percentage exceeds 100%%");
    }
    return resVal;
}

static ResVal<CAttributeValue> VerifyCurrencyPair(const std::string& str) {
    const auto value = KeyBreaker(str);
    if (value.size() != 2) {
        return Res::Err("Exactly two entires expected for currency pair");
    }
    auto token = trim_all_ws(value[0]).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    auto currency = trim_all_ws(value[1]).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    if (token.empty() || currency.empty()) {
        return Res::Err("Empty token / currency");
    }
    return {CTokenCurrencyPair{token, currency}, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyBool(const std::string& str) {
    if (str != "true" && str != "false") {
        return Res::Err(R"(Boolean value must be either "true" or "false")");
    }
    return {str == "true", Res::Ok()};
}

static bool VerifyToken(const CCustomCSView& view, const uint32_t id) {
    return view.GetToken(DCT_ID{id}).has_value();
}

static ResVal<CAttributeValue> VerifyConsortiumMember(const std::string& str) {
    UniValue values(UniValue::VARR);
    CConsortiumMembers members;

    if (!values.read(str))
        return Res::Err("Not a valid consortium member object!");

    if (!values.isArray())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Data is not array");

    for (const auto &value : values.get_array().getValues())
    {
        CConsortiumMember member;
        member.status = 0;

        member.name = trim_all_ws(value["name"].getValStr()).substr(0, CConsortiumMember::MAX_CONSORTIUM_MEMBERS_STRING_LENGHT);
        if (!value["ownerAddress"].isNull())
            member.ownerAddress = DecodeScript(value["ownerAddress"].getValStr());
        else
            return Res::Err("Empty ownerAddress in consortium member data");

        member.backingId = trim_all_ws(value["backingId"].getValStr()).substr(0, CConsortiumMember::MAX_CONSORTIUM_MEMBERS_STRING_LENGHT);
        member.mintLimit = AmountFromValue(value["mintLimit"].getValStr());
        member.mintLimitPerInterval = AmountFromValue(value["mintLimitPerInterval"].getValStr());

        if (!ParseUInt32(value["mintIntervalBlocks"].getValStr(), &member.mintIntervalBlocks)) {
            return Res::Err("Mint interval blocks not a valid integer!");
        }

        if (!value["status"].isNull())
        {
            uint32_t tmp;
            if (ParseUInt32(value["status"].getValStr(), &tmp))
                member.status = static_cast<uint8_t>(tmp);
            else
            return Res::Err("Status must be a positiv number!");

        }
        else
            return Res::Err("Status field is empty!");

        members.push_back(member);
    }

    return {members, Res::Ok()};
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
                {TokenKeys::ConsortiumMembers,     VerifyConsortiumMember},
                {TokenKeys::ConsortiumMintLimit,   VerifyInt64},
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

static Res ShowError(const std::string& key, const std::map<std::string, uint8_t>& keys) {
    std::string error{"Unrecognised " + key + " argument provided, valid " + key + "s are:"};
    for (const auto& pair : keys) {
        error += ' ' + pair.first + ',';
    }
    return Res::Err(error);
}

Res ATTRIBUTES::ProcessVariable(const std::string& key, const std::string& value,
                                const std::function<Res(const CAttributeType&, const CAttributeValue&)>& applyVariable) {

    if (key.size() > 128) {
        return Res::Err("Identifier exceeds maximum length (128)");
    }

    const auto keys = KeyBreaker(key);
    if (keys.empty() || keys[0].empty()) {
        return Res::Err("Empty version");
    }

    if (value.empty()) {
        return Res::Err("Empty value");
    }

    auto iver = allowedVersions().find(keys[0]);
    if (iver == allowedVersions().end()) {
        return Res::Err("Unsupported version");
    }

    auto version = iver->second;
    if (version != VersionTypes::v0) {
        return Res::Err("Unsupported version");
    }

    if (keys.size() < 4 || keys[1].empty() || keys[2].empty() || keys[3].empty()) {
        return Res::Err("Incorrect key for <type>. Object of ['<version>/<type>/ID/<key>','value'] expected");
    }

    auto itype = allowedTypes().find(keys[1]);
    if (itype == allowedTypes().end()) {
        return ::ShowError("type", allowedTypes());
    }

    auto type = itype->second;

    uint32_t typeId{0};
    if (type == AttributeTypes::Param) {
        auto id = allowedParamIDs().find(keys[2]);
        if (id == allowedParamIDs().end()) {
            return ::ShowError("params", allowedParamIDs());
        }
        typeId = id->second;
    } else if (type == AttributeTypes::Locks) {
        auto id = allowedLocksIDs().find(keys[2]);
        if (id == allowedLocksIDs().end()) {
            return ::ShowError("locks", allowedLocksIDs());
        }
        typeId = id->second;
    } else {
        auto id = VerifyInt32(keys[2]);
        if (!id) {
            return std::move(id);
        }
        typeId = *id.val;
    }

    uint8_t typeKey;
    uint32_t locksKey{0};
    if (type != AttributeTypes::Locks) {
        auto ikey = allowedKeys().find(type);
        if (ikey == allowedKeys().end()) {
            return Res::Err("Unsupported type {%d}", type);
        }

        itype = ikey->second.find(keys[3]);
        if (itype == ikey->second.end()) {
            return ::ShowError("key", ikey->second);
        }

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
            if (!attribValue) {
                return std::move(attribValue);
            }

            if (type == AttributeTypes::Locks) {
                return applyVariable(CDataStructureV0{type, typeId, locksKey}, *attribValue.val);
            }

            CDataStructureV0 attrV0{type, typeId, typeKey};

            if (attrV0.IsExtendedSize()) {
                if (keys.size() != 5 || keys[4].empty()) {
                    return Res::Err("Exact 5 keys are required {%d}", keys.size());
                }
                auto id = VerifyInt32(keys[4]);
                if (!id) {
                    return std::move(id);
                }
                attrV0.keyId = *id.val;
            } else {
                if (keys.size() != 4) {
                    return Res::Err("Exact 4 keys are required {%d}", keys.size());
                }
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
    if (!contractAddressValue) {
        return contractAddressValue;
    }

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Current};
    auto balances = GetValue(liveKey, CBalances{});

    auto txn = std::numeric_limits<uint32_t>::max();

    for (const auto& [key, value] : userFuturesValues) {

        mnview.EraseFuturesUserValues(key);

        CHistoryWriters subWriters{paccountHistoryDB.get(), nullptr, nullptr};
        CAccountsHistoryWriter subView(mnview, height, txn--, {}, uint8_t(CustomTxType::FutureSwapRefund), &subWriters);
        auto res = subView.SubBalance(*contractAddressValue, value.source);
        if (!res) {
            return res;
        }
        subView.Flush();

        CHistoryWriters addWriters{paccountHistoryDB.get(), nullptr, nullptr};
        CAccountsHistoryWriter addView(mnview, height, txn--, {}, uint8_t(CustomTxType::FutureSwapRefund), &addWriters);
        res = addView.AddBalance(key.owner, value.source);
        if (!res) {
            return res;
        }
        addView.Flush();

        res = balances.Sub(value.source);
        if (!res) {
            return res;
        }
    }

    SetValue(liveKey, std::move(balances));

    return Res::Ok();
}

Res ATTRIBUTES::Import(const UniValue & val) {
    if (!val.isObject() && !val.isArray()) {
        return Res::Err("Array or object of values expected");
    }

    std::map<std::string, UniValue> objMap;
    val.getObjMap(objMap);

    for (const auto& [key, value] : objMap) {
        auto res = ProcessVariable(key, value.get_str(),
            [this](const CAttributeType& attribute, const CAttributeValue& attrValue) {
                if (auto attrV0 = std::get_if<CDataStructureV0>(&attribute)) {
                    if (attrV0->type == AttributeTypes::Live) {
                        return Res::Err("Live attribute cannot be set externally");
                    }
                    // applay DFI via old keys
                    if (attrV0->IsExtendedSize() && attrV0->keyId == 0) {
                        auto newAttr = *attrV0;
                        if (attrV0->key == TokenKeys::LoanPayback) {
                            newAttr.key = TokenKeys::PaybackDFI;
                        } else {
                            newAttr.key = TokenKeys::PaybackDFIFeePCT;
                        }
                        SetValue(newAttr, attrValue);
                        return Res::Ok();
                    } else if (attrV0->key == TokenKeys::ConsortiumMembers) {
                        if (auto value = std::get_if<CConsortiumMembers>(&attrValue)) {
                            auto members = GetValue(*attrV0, CConsortiumMembers{});
                            for (auto const & member : *value)
                            {
                                // if (member.id != -1)
                                //     members.at(member.id) = member;
                                // else
                                members.push_back(member);
                            }

                            SetValue(*attrV0, members);
                            return Res::Ok();
                        } else
                            return Res::Err("Invalid member data");
                    }
                }
                SetValue(attribute, attrValue);
                return Res::Ok();
            }
        );
        if (!res) {
            return res;
        }
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
                if ((attrV0->typeId == DFIP2203 && attrV0->key == DFIPKeys::BlockPeriod) ||
                    (attrV0->type == Token && attrV0->key == TokenKeys::ConsortiumMintLimit)) {
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
            } else if (auto members = std::get_if<CConsortiumMembers>(&attribute.second)) {
                UniValue result(UniValue::VARR);
                for (auto const& member : *members)
                {
                    UniValue elem(UniValue::VOBJ);
                    elem.pushKV("name", member.name);
                    elem.pushKV("ownerAddress", ScriptToString(member.ownerAddress));
                    elem.pushKV("backingId", member.backingId);
                    elem.pushKV("mintLimit", ValueFromAmount(member.mintLimit));
                    elem.pushKV("mintLimitPerInterval", ValueFromAmount(member.mintLimitPerInterval));
                    elem.pushKV("mintIntervalBlocks", static_cast<int>(member.mintIntervalBlocks));
                    elem.pushKV("status", member.status);
                    result.push_back(elem);
                }
                ret.pushKV(key, result.write());
            } else if (auto membersMinted = std::get_if<CConsortiumMembersMinted>(&attribute.second)) {
                UniValue result(UniValue::VARR);
                for (auto const& memberMinted : *membersMinted)
                {
                    UniValue elem(UniValue::VOBJ);
                    elem.pushKV("minted", AmountsToJSON(memberMinted.minted.balances));
                    elem.pushKV("mintedPerInterval", AmountsToJSON(memberMinted.mintedPerInterval.balances));
                    result.push_back(elem);
                }
                ret.pushKV(key, result.write());
            }
        } catch (const std::out_of_range&) {
            // Should not get here, that's mean maps are mismatched
        }
    }
    return ret;
}

Res ATTRIBUTES::Validate(const CCustomCSView & view) const
{
    if (view.GetLastHeight() < Params().GetConsensus().FortCanningHillHeight)
        return Res::Err("Cannot be set before FortCanningHill");

    for (const auto& attribute : attributes) {
        auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            return Res::Err("Unsupported version");
        }
        switch (attrV0->type) {
            case AttributeTypes::Token:
                switch (attrV0->key) {
                    case TokenKeys::PaybackDFI:
                    case TokenKeys::PaybackDFIFeePCT:
                        if (!view.GetLoanTokenByID({attrV0->typeId})) {
                            return Res::Err("No such loan token (%d)", attrV0->typeId);
                        }
                        break;
                    case TokenKeys::LoanPayback:
                    case TokenKeys::LoanPaybackFeePCT:
                        if (view.GetLastHeight() < Params().GetConsensus().FortCanningRoadHeight) {
                            return Res::Err("Cannot be set before FortCanningRoad");
                        }
                        if (!view.GetLoanTokenByID(DCT_ID{attrV0->typeId})) {
                            return Res::Err("No such loan token (%d)", attrV0->typeId);
                        }
                        if (!view.GetToken(DCT_ID{attrV0->keyId})) {
                            return Res::Err("No such token (%d)", attrV0->keyId);
                        }
                        break;
                    case TokenKeys::DexInFeePct:
                    case TokenKeys::DexOutFeePct:
                        if (view.GetLastHeight() < Params().GetConsensus().FortCanningRoadHeight) {
                            return Res::Err("Cannot be set before FortCanningRoad");
                        }
                        if (!view.GetToken(DCT_ID{attrV0->typeId})) {
                            return Res::Err("No such token (%d)", attrV0->typeId);
                        }
                        break;
                    case TokenKeys::DFIP2203Enabled:
                        if (view.GetLastHeight() < Params().GetConsensus().FortCanningRoadHeight) {
                            return Res::Err("Cannot be set before FortCanningRoad");
                        }
                        if (!view.GetLoanTokenByID(DCT_ID{attrV0->typeId})) {
                            return Res::Err("No such loan token (%d)", attrV0->typeId);
                        }
                        break;
                    case TokenKeys::LoanCollateralEnabled:
                    case TokenKeys::LoanCollateralFactor:
                    case TokenKeys::LoanMintingEnabled:
                    case TokenKeys::LoanMintingInterest: {
                        if (view.GetLastHeight() < Params().GetConsensus().GreatWorldHeight) {
                            return Res::Err("Cannot be set before GreatWorld");
                        }
                        if (!VerifyToken(view, attrV0->typeId)) {
                            return Res::Err("No such token (%d)", attrV0->typeId);
                        }
                        CDataStructureV0 intervalPriceKey{AttributeTypes::Token, attrV0->typeId,
                                                          TokenKeys::FixedIntervalPriceId};
                        if (GetValue(intervalPriceKey, CTokenCurrencyPair{}) == CTokenCurrencyPair{}) {
                            return Res::Err("Fixed interval price currency pair must be set first");
                        }
                        break;
                    }
                    case TokenKeys::FixedIntervalPriceId:
                        if (view.GetLastHeight() < Params().GetConsensus().GreatWorldHeight) {
                            return Res::Err("Cannot be set before GreatWorld");
                        }
                        if (!VerifyToken(view, attrV0->typeId)) {
                            return Res::Err("No such token (%d)", attrV0->typeId);
                        }
                        break;
                    case TokenKeys::ConsortiumMembers:
                        if (view.GetLastHeight() < Params().GetConsensus().GreatWorldHeight) {
                            return Res::Err("Cannot be set before GreatWorld");
                        }
                        if (!view.GetToken(DCT_ID{attrV0->typeId})) {
                                return Res::Err("No such token (%d)", attrV0->typeId);
                            }
                        break;
                    case TokenKeys::ConsortiumMintLimit:
                        if (view.GetLastHeight() < Params().GetConsensus().GreatWorldHeight) {
                            return Res::Err("Cannot be set before GreatWorld");
                        }
                        if (!view.GetToken(DCT_ID{attrV0->typeId})) {
                                return Res::Err("No such token (%d)", attrV0->typeId);
                            }
                        break;
                    default:
                        return Res::Err("Unsupported key");
                }
                break;

            case AttributeTypes::Poolpairs:
                if (!std::get_if<CAmount>(&attribute.second)) {
                    return Res::Err("Unsupported value");
                }
                switch (attrV0->key) {
                    case PoolKeys::TokenAFeePCT:
                    case PoolKeys::TokenBFeePCT:
                        if (!view.GetPoolPair({attrV0->typeId})) {
                            return Res::Err("No such pool (%d)", attrV0->typeId);
                        }
                        break;
                    default:
                        return Res::Err("Unsupported key");
                }
                break;

            case AttributeTypes::Param:
                if (attrV0->typeId == ParamIDs::DFIP2203) {
                    if (view.GetLastHeight() < Params().GetConsensus().FortCanningRoadHeight) {
                        return Res::Err("Cannot be set before FortCanningRoad");
                    }
                } else if (attrV0->typeId != ParamIDs::DFIP2201) {
                    return Res::Err("Unrecognised param id");
                }
                break;

                // Live is set internally
            case AttributeTypes::Live:
                break;

            case AttributeTypes::Locks:
                if (view.GetLastHeight() < Params().GetConsensus().GreatWorldHeight) {
                    return Res::Err("Cannot be set before GreatWorld");
                }
                if (attrV0->typeId != ParamIDs::TokenID) {
                    return Res::Err("Unrecognised locks id");
                }
                if (!view.GetLoanTokenByID(DCT_ID{attrV0->key}).has_value()) {
                    return Res::Err("No loan token with id (%d)", attrV0->key);
                }
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
            if (!pool) {
                return Res::Err("No such pool (%d)", poolId.v);
            }
            auto tokenId = attrV0->key == PoolKeys::TokenAFeePCT ?
                           pool->idTokenA : pool->idTokenB;

            auto valuePct = std::get<CAmount>(attribute.second);
            if (auto res = mnview.SetDexFeePct(poolId, tokenId, valuePct); !res) {
                return res;
            }
        } else if (attrV0->type == AttributeTypes::Token) {
            if (attrV0->key == TokenKeys::DexInFeePct
            ||  attrV0->key == TokenKeys::DexOutFeePct) {
                DCT_ID tokenA{attrV0->typeId}, tokenB{~0u};
                if (attrV0->key == TokenKeys::DexOutFeePct) {
                    std::swap(tokenA, tokenB);
                }
                auto valuePct = std::get<CAmount>(attribute.second);
                if (auto res = mnview.SetDexFeePct(tokenA, tokenB, valuePct); !res) {
                    return res;
                }
            } else if (attrV0->key == TokenKeys::FixedIntervalPriceId) {
                if (const auto& currencyPair = std::get_if<CTokenCurrencyPair>(&attribute.second)) {
                    // Already exists, skip.
                    if (mnview.GetFixedIntervalPrice(*currencyPair)) {
                        continue;
                    } else if (!OraclePriceFeed(mnview, *currencyPair)) {
                        return Res::Err("Price feed %s/%s does not belong to any oracle", currencyPair->first, currencyPair->second);
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
                if (!token) {
                    return Res::Err("No such loan token (%d)", attrV0->typeId);
                }

                // Special case: DUSD will be used as a source for swaps but will
                // be set as disabled for Future swap destination.
                if (token->symbol == "DUSD") {
                    continue;
                }

                auto res = RefundFuturesContracts(mnview, height, attrV0->typeId);
                if (!res) {
                    return res;
                }
            }
            if (attrV0->key == TokenKeys::ConsortiumMembers) {
                auto members = GetValue(*attrV0, CConsortiumMembers{});
                CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
                auto membersBalances = GetValue(liveKey, CConsortiumMembersMinted{});

                for (size_t i=0; i < members.size(); i++)
                    membersBalances.push_back(CConsortiumMemberMinted{});

                SetValue(liveKey, membersBalances);
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

                auto res = RefundFuturesContracts(mnview, height);
                if (!res) {
                    return res;
                }

            } else if (attrV0->key == DFIPKeys::BlockPeriod) {

                // Only check this when block period has been set, otherwise
                // it will fail when DFIP2203 active is set to true.
                if (!futureBlockUpdated) {
                    continue;
                }

                CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::Active};
                if (GetValue(activeKey, false)) {
                    return Res::Err("Cannot set block period while DFIP2203 is active");
                }
            }
        }
    }

    return Res::Ok();
}
