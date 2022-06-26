// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/attributes.h>

#include <masternodes/accountshistory.h> /// CAccountsHistoryWriter
#include <masternodes/consensus/governance.h> /// storeGovVars
#include <masternodes/futureswap.h>
#include <masternodes/masternodes.h> /// CCustomCSView
#include <masternodes/mn_checks.h> /// GetAggregatePrice
#include <masternodes/mn_rpc.h> /// ScriptToString

#include <core_io.h> /// ValueFromAmount
#include <rpc/util.h> /// AmountFromValue

#include <util/strencodings.h>

extern UniValue AmountsToJSON(TAmounts const & diffs);
extern CScript DecodeScript(std::string const& str);

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
        {"consortium",  AttributeTypes::Consortium},
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
        {AttributeTypes::Consortium,"consortium"},
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
            AttributeTypes::Consortium, {
                {"members",             ConsortiumKeys::Members},
                {"mint_limit",          ConsortiumKeys::MintLimit},
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
                {"start_block",         DFIPKeys::StartBlock},
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
                {TokenKeys::Ascendant,             "ascendant"},
                {TokenKeys::Descendant,            "descendant"},
                {TokenKeys::Epitaph,               "epitaph"},
            }
        },
        {
            AttributeTypes::Consortium, {
                {ConsortiumKeys::Members,     "members"},
                {ConsortiumKeys::MintLimit,   "mint_limit"},
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
                {DFIPKeys::StartBlock,   "start_block"},
            }
        },
        {
            AttributeTypes::Live, {
                {EconomyKeys::PaybackDFITokens,         "dfi_payback_tokens"},
                {EconomyKeys::DFIP2203Current,          "dfip2203_current"},
                {EconomyKeys::DFIP2203Burned,           "dfip2203_burned"},
                {EconomyKeys::DFIP2203Minted,           "dfip2203_minted"},
                {EconomyKeys::DexTokens,                "dex"},
                {EconomyKeys::ConsortiumMinted,         "consortium"},
                {EconomyKeys::ConsortiumMembersMinted,  "consortium_members"},
            }
        },
    };
    return keys;
}

static ResVal<int32_t> VerifyInt32(const std::string& str) {
    int32_t int32;
    if (!ParseInt32(str, &int32)) {
        return Res::Err("Value must be an integer");
    }
    return {int32, Res::Ok()};
}

static ResVal<int32_t> VerifyPositiveInt32(const std::string& str) {
    int32_t int32;
    if (!ParseInt32(str, &int32) || int32 < 0) {
        return Res::Err("Value must be a positive integer");
    }
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

static bool VerifyToken(const CCustomCSView& view, const uint32_t id) {
    return view.GetToken(DCT_ID{id}).has_value();
}

static ResVal<CAttributeValue> VerifyConsortiumMember(const std::string& str) {
    UniValue values(UniValue::VOBJ);
    CConsortiumMembers members;

    if (!values.read(str))
        return Res::Err("Not a valid consortium member object!");

    for (const auto &key : values.getKeys())
    {
        UniValue value(values[key].get_obj());
        CConsortiumMember member;

        member.status = 0;

        member.name = trim_all_ws(value["name"].getValStr()).substr(0, CConsortiumMember::MAX_CONSORTIUM_MEMBERS_STRING_LENGTH);
        if (!value["ownerAddress"].isNull())
            member.ownerAddress = DecodeScript(value["ownerAddress"].getValStr());
        else
            return Res::Err("Empty ownerAddress in consortium member data!");

        member.backingId = trim_all_ws(value["backingId"].getValStr()).substr(0, CConsortiumMember::MAX_CONSORTIUM_MEMBERS_STRING_LENGTH);
        if (!AmountFromValue(value["mintLimit"], member.mintLimit)) {
            return Res::Err("mint limit is an invalid amount");
        }

        if (!value["status"].isNull())
        {
            uint32_t tmp;

            if (ParseUInt32(value["status"].getValStr(), &tmp)) {
                if (tmp > 1) {
                    return Res::Err("Status can be either 0 or 1");
                }
                member.status = static_cast<uint8_t>(tmp);
            } else {
                return Res::Err("Status must be a positive number!");
            }
        }

        members[key] = member;
    }

    return {members, Res::Ok()};
}

static ResVal<CAttributeValue> VerifySplit(const std::string& str) {
    const auto values = KeyBreaker(str, ',');
    if (values.empty()) {
        return Res::Err(R"(No valid values supplied, "id/multiplier, ...")");
    }

    OracleSplits splits;
    for (const auto& item : values) {
        const auto pairs = KeyBreaker(item);
        if (pairs.size() != 2) {
            return Res::Err("Two int values expected for split in id/mutliplier");
        }
        const auto resId = VerifyPositiveInt32(pairs[0]);
        if (!resId) {
            return resId;
        }
        const auto resMultiplier = VerifyInt32(pairs[1]);
        if (!resMultiplier) {
            return resMultiplier;
        }
        if (*resMultiplier == 0) {
            return Res::Err("Mutliplier cannot be zero");
        }
        splits[*resId] = *resMultiplier;
    }

    return {splits, Res::Ok()};
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
            AttributeTypes::Consortium, {
                {ConsortiumKeys::Members,          VerifyConsortiumMember},
                {ConsortiumKeys::MintLimit,        VerifyInt64},
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
                {DFIPKeys::StartBlock,   VerifyInt64},
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

static std::string ShowError(const std::string& key, const std::map<std::string, uint8_t>& keys) {
    std::string error{"Unrecognised " + key + " argument provided, valid " + key + "s are:"};
    for (const auto& pair : keys) {
        error += ' ' + pair.first + ',';
    }
    return error;
}

Res ATTRIBUTES::ProcessVariable(const std::string& key, std::optional<std::string> value,
                                const std::function<Res(const CAttributeType&, const CAttributeValue&)>& applyVariable) {

    Require(key.size() <= 128, "Identifier exceeds maximum length (128)");

    const auto keys = KeyBreaker(key);
    Require(!keys.empty() && !keys[0].empty(), "Empty version");

    Require(!value->empty(), "Empty value");

    auto iver = allowedVersions().find(keys[0]);
    Require(iver != allowedVersions().end(), "Unsupported version");

    auto version = iver->second;
    Require(version == VersionTypes::v0, "Unsupported version");

    Require(keys.size() >= 4 && !keys[1].empty() && !keys[2].empty() && !keys[3].empty(),
              "Incorrect key for <type>. Object of ['<version>/<type>/ID/<key>','value'] expected");

    auto itype = allowedTypes().find(keys[1]);
    Require(itype != allowedTypes().end(), ::ShowError("type", allowedTypes()));

    const auto& type = itype->second;

    uint32_t typeId{0};
    if (type == AttributeTypes::Param) {
        auto id = allowedParamIDs().find(keys[2]);
        Require(id != allowedParamIDs().end(), ::ShowError("params", allowedParamIDs()));
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

        itype = ikey->second.find(keys[3]);
        Require(itype != ikey->second.end(), ::ShowError("key", ikey->second));

        typeKey = itype->second;

        if (type == AttributeTypes::Param) {
            if (typeId == ParamIDs::DFIP2201) {
                if (typeKey == DFIPKeys::RewardPct ||
                    typeKey == DFIPKeys::BlockPeriod ||
                    typeKey == DFIPKeys::StartBlock) {
                    return Res::Err("Unsupported type for DFIP2201 {%d}", typeKey);
                }
            } else if (typeId == ParamIDs::DFIP2203) {
                if (typeKey == DFIPKeys::Premium ||
                    typeKey == DFIPKeys::MinSwap) {
                    return Res::Err("Unsupported type for DFIP2203 {%d}", typeKey);
                }

                if (typeKey == DFIPKeys::BlockPeriod || typeKey == DFIPKeys::StartBlock) {
                    futureBlockUpdated = true;
                }
            } else {
                return Res::Err("Unsupported Param ID");
            }
        }

        attrV0 = CDataStructureV0{type, typeId, typeKey};
    }

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

    if (!value) {
        return applyVariable(attrV0, {});
    }

    try {
        if (auto parser = parseValue().at(type).at(typeKey)) {
            auto attribValue = parser(*value);
            Require(attribValue);
            return applyVariable(attrV0, *attribValue.val);
        }
    } catch (const std::out_of_range&) {
    }
    return Res::Err("No parse function {%d, %d}", type, typeKey);
}

bool ATTRIBUTES::IsEmpty() const
{
    return attributes.empty();
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

Res ATTRIBUTES::RefundFuturesContracts(CCustomCSView &mnview, CFutureSwapView& futureSwapView, const uint32_t height, const uint32_t tokenID)
{
    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::BlockPeriod};
    const auto blockPeriod = GetValue(blockKey, CAmount{});
    if (blockPeriod == 0) {
        return Res::Ok();
    }

    std::map<CFuturesUserKey, CFuturesUserValue> userFuturesValues;

    futureSwapView.ForEachFuturesUserValues([&](const CFuturesUserKey& key, const CFuturesUserValue& futuresValues) {
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

        futureSwapView.EraseFuturesUserValues(key);

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
                    if (attrV0->type == AttributeTypes::Live ||
                       (attrV0->type == AttributeTypes::Token &&
                       (attrV0->key == TokenKeys::Ascendant ||
                        attrV0->key == TokenKeys::Descendant ||
                        attrV0->key == TokenKeys::Epitaph))) {
                        return Res::Err("Attribute cannot be set externally");
                    } else if (attrV0->type == AttributeTypes::Oracles && attrV0->typeId == OracleIDs::Splits) {
                        auto splitValue = std::get_if<OracleSplits>(&attrValue);
                        if (!splitValue) {
                            return Res::Err("Failed to get Oracle split value");
                        }
                        for (const auto& [id, multiplier] : *splitValue) {
                            tokenSplits.insert(id);
                        }
                        try {
                            auto attrMap = std::get_if<OracleSplits>(&attributes.at(attribute));
                            OracleSplits combined{*splitValue};
                            combined.merge(*attrMap);
                            SetValue(attribute, combined);
                            return Res::Ok();
                        } catch (std::out_of_range &) {}
                    }

                    // apply DFI via old keys
                    if (attrV0->IsExtendedSize() && attrV0->keyId == 0) {
                        auto newAttr = *attrV0;
                        newAttr.key = attrV0->key == TokenKeys::LoanPayback ?
                                TokenKeys::PaybackDFI: TokenKeys::PaybackDFIFeePCT;

                        SetValue(newAttr, attrValue);
                        return Res::Ok();
                    } else if (attrV0->type == AttributeTypes::Consortium && attrV0->key == ConsortiumKeys::Members) {
                        if (auto value = std::get_if<CConsortiumMembers>(&attrValue)) {
                            auto members = GetValue(*attrV0, CConsortiumMembers{});

                            for (auto const & member : *value)
                            {
                                for (auto const & tmp : members)
                                    if (tmp.first != member.first && tmp.second.ownerAddress == member.second.ownerAddress)
                                        return Res::Err("Cannot add a member with an owner address of a existing consortium member!");

                                members[member.first] = member.second;
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

            if (auto bool_val = std::get_if<bool>(&attribute.second)) {
                ret.pushKV(key, *bool_val ? "true" : "false");
            } else if (auto amount = std::get_if<CAmount>(&attribute.second)) {
                if ((attrV0->typeId == DFIP2203 && (attrV0->key == DFIPKeys::BlockPeriod || attrV0->key == DFIPKeys::StartBlock))
                    || (attrV0->type == Consortium && attrV0->key == ConsortiumKeys::MintLimit)) {
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
                UniValue result(UniValue::VOBJ);
                for (const auto& [id, member] : *members)
                {
                    UniValue elem(UniValue::VOBJ);
                    elem.pushKV("name", member.name);
                    elem.pushKV("ownerAddress", ScriptToString(member.ownerAddress));
                    elem.pushKV("backingId", member.backingId);
                    elem.pushKV("mintLimit", ValueFromAmount(member.mintLimit));
                    elem.pushKV("status", member.status);
                    result.pushKV(id, elem);
                }
                ret.pushKV(key, result.write());
            } else if (auto consortiumMinted = std::get_if<CConsortiumGlobalMinted>(&attribute.second)) {
                for (const auto& token : *consortiumMinted)
                {
                    auto& minted = token.second.minted;
                    auto& burnt = token.second.burnt;

                    auto tokenKey = KeyBuilder(key, token.first.v);
                    ret.pushKV(KeyBuilder(tokenKey, "minted"), ValueFromAmount(minted));
                    ret.pushKV(KeyBuilder(tokenKey, "burnt"), ValueFromAmount(burnt));
                    ret.pushKV(KeyBuilder(tokenKey, "supply"), ValueFromAmount(minted - burnt));
                }
            } else if (auto membersMinted = std::get_if<CConsortiumMembersMinted>(&attribute.second)) {
                for (const auto& token : *membersMinted)
                {
                    for (const auto& member : token.second)
                    {
                        auto& minted = member.second.minted;
                        auto& burnt = member.second.burnt;

                        auto tokenKey = KeyBuilder(key, token.first.v);
                        auto memberKey = KeyBuilder(tokenKey, member.first);
                        ret.pushKV(KeyBuilder(memberKey, "minted"), ValueFromAmount(minted));
                        ret.pushKV(KeyBuilder(memberKey, "burnt"), ValueFromAmount(burnt));
                        ret.pushKV(KeyBuilder(memberKey, "supply"), ValueFromAmount(minted - burnt));
                    }
                }
            } else if (const auto splitValues = std::get_if<OracleSplits>(&attribute.second)) {
                std::string keyValue;
                for (const auto& [tokenId, multiplier] : *splitValues) {
                    keyValue += KeyBuilder(tokenId, multiplier) + ',';
                }
                ret.pushKV(key, keyValue);
            } else if (const auto& descendantPair = std::get_if<DescendantValue>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(descendantPair->first, descendantPair->second));
            } else if (const auto& ascendantPair = std::get_if<AscendantValue>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(ascendantPair->first, ascendantPair->second));
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

    for (const auto& [key, value] : attributes) {
        auto attrV0 = std::get_if<CDataStructureV0>(&key);
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
                    case TokenKeys::Ascendant:
                    case TokenKeys::Descendant:
                    case TokenKeys::Epitaph:
                        break;
                    default:
                        return Res::Err("Unsupported key");
                }
                break;

            case AttributeTypes::Consortium:
                switch (attrV0->key) {
                    case ConsortiumKeys::Members:
                        if (view.GetLastHeight() < Params().GetConsensus().GreatWorldHeight) {
                            return Res::Err("Cannot be set before GreatWorld");
                        }
                        if (!view.GetToken(DCT_ID{attrV0->typeId})) {
                            return Res::Err("No such token (%d)", attrV0->typeId);
                        }
                        break;
                    case ConsortiumKeys::MintLimit:
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

            case AttributeTypes::Oracles:
                if (view.GetLastHeight() < Params().GetConsensus().GreatWorldHeight) {
                    return Res::Err("Cannot be set before GreatWorld");
                }
                if (attrV0->typeId == OracleIDs::Splits) {
                    const auto splitMap = std::get_if<OracleSplits>(&value);
                    Require(splitMap, "Unsupported value");
                    for (const auto& [tokenId, multipler] : *splitMap) {
                        Require(tokenId != 0, "Tokenised DFI cannot be split");
                        Require(!view.HasPoolPair(DCT_ID{tokenId}), "Pool tokens cannot be split");

                        const auto token = view.GetToken(DCT_ID{tokenId});
                        Require(token, "Token (%d) does not exist", tokenId);

                        Require(token->IsDAT(), "Only DATs can be split");
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
                    default:
                        return Res::Err("Unsupported key");
                }
                break;

            case AttributeTypes::Param:
                if (attrV0->typeId == ParamIDs::DFIP2203) {
                    Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningRoadHeight, "Cannot be set before FortCanningRoad");
                    if (attrV0->key == DFIPKeys::StartBlock) {
                        Require(view.GetLastHeight() >= Params().GetConsensus().GreatWorldHeight, "Cannot be set before GreatWorld");
                    }
                } else  {
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

Res ATTRIBUTES::Apply(CCustomCSView& mnview, CFutureSwapView& futureSwapView, uint32_t height)
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

                Require(RefundFuturesContracts(mnview, futureSwapView, height, attrV0->typeId));
            }
        } else if (attrV0->type == AttributeTypes::Param && attrV0->typeId == ParamIDs::DFIP2203) {
            if (attrV0->key == DFIPKeys::Active) {

                auto value = std::get<bool>(attribute.second);
                if (value) {
                    continue;
                }

                Require(RefundFuturesContracts(mnview, futureSwapView, height));
            } else if (attrV0->key == DFIPKeys::BlockPeriod || attrV0->key == DFIPKeys::StartBlock) {

                // Only check this when block period has been set, otherwise
                // it will fail when DFIP2203 active is set to true.
                if (!futureBlockUpdated) {
                    continue;
                }

                CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::Active};
                Require(!GetValue(activeKey, false), "Cannot set block period while DFIP2203 is active");
            }
        } else if (attrV0->type == AttributeTypes::Oracles && attrV0->typeId == OracleIDs::Splits) {
            const auto value = std::get_if<OracleSplits>(&attribute.second);
            if (!value) {
                return Res::Err("Unsupported value");
            }
            for (const auto split : tokenSplits) {
                if (auto it{value->find(split)}; it == value->end()) {
                    continue;
                }
                CDataStructureV0 lockKey{AttributeTypes::Locks, ParamIDs::TokenID, split};
                if (GetValue(lockKey, false)) {
                    continue;
                }

                // Loan token check imposed on lock
                if (!mnview.GetLoanTokenByID(DCT_ID{split}).has_value()) {
                    return Res::Err("Auto lock. No loan token with id (%d)", split);
                }

                if (attrV0->key < height) {
                    return Res::Err("Cannot be set at or below current height");
                }

                CGovernanceHeightMessage lock{"ATTRIBUTES"};
                auto var = GovVariable::Create(lock.govName);
                if (!var) {
                    return Res::Err("Failed to create Gov var for lock");
                }
                auto govVar = std::dynamic_pointer_cast<ATTRIBUTES>(var);
                if (!govVar) {
                    return Res::Err("Failed to cast Gov var to ATTRIBUTES");
                }
                govVar->SetValue(lockKey, true);
                lock.govVar = govVar;

                const auto startHeight = attrV0->key - Params().GetConsensus().blocksPerDay() / 2;
                if (height > startHeight) {
                    lock.startHeight = height;
                } else {
                    lock.startHeight = startHeight;
                }

                const auto res = CGovernanceConsensus::storeGovVars(lock, mnview);
                if (!res) {
                    return Res::Err("Cannot be set at or below current height");
                }
            }
        }
    }
    return Res::Ok();
}

Res ATTRIBUTES::Erase(CCustomCSView & mnview, uint32_t, std::vector<std::string> const & keys)
{
    for (const auto& key : keys) {
        auto res = ProcessVariable(key, {},
            [&](const CAttributeType& attribute, const CAttributeValue&) {
                auto attrV0 = std::get_if<CDataStructureV0>(&attribute);
                if (!attrV0) {
                    return Res::Ok();
                }
                if (attrV0->type == AttributeTypes::Live) {
                    return Res::Err("Live attribute cannot be deleted");
                }
                if (!EraseKey(attribute)) {
                    return Res::Err("Attribute {%d} not exists", attrV0->type);
                }
                if (attrV0->type == AttributeTypes::Poolpairs) {
                    auto poolId = DCT_ID{attrV0->typeId};
                    auto pool = mnview.GetPoolPair(poolId);
                    if (!pool) {
                        return Res::Err("No such pool (%d)", poolId.v);
                    }
                    auto tokenId = attrV0->key == PoolKeys::TokenAFeePCT ?
                                                pool->idTokenA : pool->idTokenB;

                    return mnview.EraseDexFeePct(poolId, tokenId);
                } else if (attrV0->type == AttributeTypes::Token) {
                    if (attrV0->key == TokenKeys::DexInFeePct
                    ||  attrV0->key == TokenKeys::DexOutFeePct) {
                        DCT_ID tokenA{attrV0->typeId}, tokenB{~0u};
                        if (attrV0->key == TokenKeys::DexOutFeePct) {
                            std::swap(tokenA, tokenB);
                        }
                        return mnview.EraseDexFeePct(tokenA, tokenB);
                    }
                }
                return Res::Ok();
            }
        );
        if (!res) {
            return res;
        }
    }

    return Res::Ok();
}
