// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/attributes.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <util/strencodings.h>

template<typename T>
static std::string KeyBuilder(const T& value){
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

static std::string KeyBuilder(const UniValue& value){
    return value.getValStr();
}

template<typename T, typename ... Args >
static std::string KeyBuilder(const T& value, const Args& ... args){
    return KeyBuilder(value) + '/' + KeyBuilder(args...);
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

static ResVal<int32_t> VerifyInt32(const std::string& str) {
    int32_t int32;
    if (!ParseInt32(str, &int32) || int32 < 0) {
        return Res::Err("Identifier must be a positive integer");
    }
    return {int32, Res::Ok()};
}

static ResVal<CAmount> VerifyPct(const std::string& str) {
    CAmount amount = 0;
    if (!ParseFixedPoint(str, 8, &amount) || amount < 0) {
        return Res::Err("Percentage must be a positive integer or float");
    }
    if (amount > COIN) {
        return Res::Err("Percentage exceeds 100%%");
    }
    return ResVal<CAmount>(amount, Res::Ok());
}

static Res ProcessVariable(const std::string& key, const std::string& value,
                           std::function<ResVal<uint8_t>(const std::string&)> parseType,
                           std::function<ResVal<uint8_t>(uint8_t, const std::string&)> parseKey,
                           std::function<Res(const UniValue&)> applyVariable = {}) {

    if (key.size() > 128) {
        return Res::Err("Identifier exceeds maximum length (128)");
    }

    const auto keys = KeyBreaker(key);
    if (keys.empty() || keys[0].empty()) {
        return Res::Err("Empty key");
    }

    if (value.empty()) {
        return Res::Err("Empty value");
    }

    auto resType = parseType(keys[0]);
    if (!resType) {
        return std::move(resType);
    }

    auto type = *resType.val;

    if (keys.size() != 3 || keys[1].empty() || keys[2].empty()) {
        return Res::Err("Incorrect key for <type>. Object of ['<type>/ID/<key>','value'] expected");
    }

    auto typeId = VerifyInt32(keys[1]);
    if (!typeId) {
        return std::move(typeId);
    }

    auto resKey = parseKey(type, keys[2]);
    if (!resKey) {
        return std::move(resKey);
    }

    UniValue univalue;
    auto typeKey = *resKey.val;

    if (type == AttributeTypes::Token) {
        if (typeKey == TokenKeys::PaybackDFI) {
            if (value != "true" && value != "false") {
                return Res::Err("Payback DFI value must be either \"true\" or \"false\"");
            }
            univalue.setBool(value == "true");
        } else if (typeKey == TokenKeys::PaybackDFIFeePCT) {
            auto res = VerifyPct(value);
            if (!res) {
                return std::move(res);
            }
            univalue = ValueFromAmount(*res.val);
        } else {
            return Res::Err("Unrecognised key");
        }
    } else if (type == AttributeTypes::Poolpairs) {
        if (typeKey == PoolKeys::TokenAFeePCT
        ||  typeKey == PoolKeys::TokenBFeePCT) {
            auto res = VerifyPct(value);
            if (!res) {
                return std::move(res);
            }
            univalue = ValueFromAmount(*res.val);
        } else {
            return Res::Err("Unrecognised key");
        }
    } else {
        return Res::Err("Unrecognised type");
    }

    if (applyVariable) {
        UniValue values(UniValue::VARR);
        values.push_back(type);
        values.push_back(*typeId.val);
        values.push_back(typeKey);
        values.push_back(univalue);
        return applyVariable(values);
    }
    return Res::Ok();
}

static ResVal<uint8_t> GetType(const std::string& key, const std::map<std::string, uint8_t>& types) {
    try {
        return {types.at(key), Res::Ok()};
    } catch (const std::out_of_range&) {
        std::string error{"Unrecognised type argument provided, valid types are:"};
        for (const auto& pair : types) {
            error += ' ' + pair.first + ',';
        }
        return Res::Err(error);
    }
}

static ResVal<uint8_t> GetKey(uint8_t type, const std::string& key, const std::map<uint8_t, std::map<std::string, uint8_t>>& types) {
    try {
        return GetType(key, types.at(type));
    } catch (const std::out_of_range&) {
        return Res::Err("Unrecognised type");
    }
}

Res ATTRIBUTES::Import(const UniValue & val) {
    if (!val.isObject()) {
        return Res::Err("Object of values expected");
    }

    std::map<std::string, UniValue> objMap;
    val.getObjMap(objMap);

    for (const auto& pair : objMap) {
        auto res = ProcessVariable(
            pair.first, pair.second.get_str(),
            std::bind(GetType, std::placeholders::_1, allowedTypes),
            std::bind(GetKey, std::placeholders::_1, std::placeholders::_2, allowedKeys),
            [this](const UniValue& values) {
                if (values[0].get_int() == AttributeTypes::Token
                ||  values[0].get_int() == AttributeTypes::Poolpairs) {
                    auto value = values[3].getValStr();
                    if (values[3].isBool()) {
                        value = value == "1" ? "true" : "false";
                    }
                    if (values[3].isNum()) {
                        value = KeyBuilder(values[3].get_real());
                    }
                    attributes[KeyBuilder(values[0], values[1], values[2])] = value;
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

static ResVal<uint8_t> GetTypeByKey(const std::string& key) {
    auto res = VerifyInt32(key);
    if (!res) {
        return res;
    }
    return ResVal<uint8_t>(*res.val, Res::Ok());
}

UniValue ATTRIBUTES::Export() const {
    UniValue ret(UniValue::VOBJ);
    for (const auto& item : attributes) {
        const auto keys = KeyBreaker(item.first);
        // Should never be empty
        if (keys.empty()) {
            continue;
        }
        auto res = GetTypeByKey(keys[0]);
        if (!res) {
            continue;
        }
        auto type = *res.val;
        if (!displayTypes.count(type)) {
            continue;
        }
        // Token should always have three items
        if (keys.size() == 3) {
            res = GetTypeByKey(keys[2]);
            if (!res) {
                continue;
            }
            auto tokenKey = *res.val;
            try {
                ret.pushKV(displayTypes.at(type) + '/' + keys[1] + '/' + displayKeys.at(type).at(tokenKey), item.second);
            } catch (const std::out_of_range&) {
                // Should not get here, if we do perhaps update displayTypes and displayKeys for newly added types and keys.
            }
        }
    }
    return ret;
}

Res ATTRIBUTES::Validate(const CCustomCSView & view) const
{
    if (view.GetLastHeight() < Params().GetConsensus().FortCanningHillHeight)
        return Res::Err("Cannot be set before FortCanningHill");

    for (const auto& item : attributes) {
        auto res = ProcessVariable(
            item.first, item.second, GetTypeByKey,
            [](uint8_t, const std::string& key) {
                return GetTypeByKey(key);
            },
            [&](const UniValue& values) {
                if (values[0].get_int() == AttributeTypes::Token) {
                    uint32_t tokenId = uint32_t(values[1].get_int());
                    if (values[2].get_int() == TokenKeys::PaybackDFI
                    ||  values[2].get_int() == TokenKeys::PaybackDFIFeePCT) {
                        if (!view.GetLoanTokenByID(DCT_ID{tokenId})) {
                            return Res::Err("No such loan token (%d)", tokenId);
                        }
                        return Res::Ok();
                    }
                }
                if (values[0].get_int() == AttributeTypes::Poolpairs) {
                    uint32_t poolId = uint32_t(values[1].get_int());
                    if (values[2].get_int() == PoolKeys::TokenAFeePCT
                    ||  values[2].get_int() == PoolKeys::TokenBFeePCT) {
                        if (!view.GetPoolPair(DCT_ID{poolId})) {
                            return Res::Err("No such pool (%d)", poolId);
                        }
                        return Res::Ok();
                    }
                }
                return Res::Err("Unrecognised type");
            }
        );
        if (!res) {
            return res;
        }
    }

    return Res::Ok();
}

Res ATTRIBUTES::Apply(CCustomCSView & mnview, const uint32_t height)
{
    for (const auto& item : attributes) {
        auto res = ProcessVariable(
            item.first, item.second, GetTypeByKey,
            [](uint8_t, const std::string& key) {
                return GetTypeByKey(key);
            },
            [&](const UniValue& values) -> Res {
                if (values[0].get_int() == AttributeTypes::Poolpairs) {
                    auto poolId = uint32_t(values[1].get_int());
                    auto pool = mnview.GetPoolPair(DCT_ID{poolId});
                    if (!pool) {
                        return Res::Err("No such pool (%d)", poolId);
                    }
                    auto tokenId = values[2].get_int() == PoolKeys::TokenAFeePCT ?
                                    pool->idTokenA : pool->idTokenB;
                    auto res = VerifyPct(values[3].getValStr());
                    if (!res) {
                        return std::move(res);
                    }
                    return mnview.SetDexfeePct(DCT_ID{poolId}, tokenId, *res.val);
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
