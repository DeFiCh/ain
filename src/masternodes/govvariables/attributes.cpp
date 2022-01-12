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
    if (amount > COIN * 100) {
        return Res::Err("Percentage exceeds 100%%");
    }
    return ResVal<CAmount>(amount, Res::Ok());
}

static Res ProcessVariable(const std::string& key, const std::string& value,
                           std::function<ResVal<uint8_t>(const std::string&)> parseType,
                           std::function<ResVal<uint8_t>(const std::string&)> parseKey,
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

    auto res = parseType(keys[0]);
    if (!res) {
        return std::move(res);
    }

    auto type = *res.val;

    if (type == AttributeTypes::Token) {

        if (keys.size() != 3 || keys[1].empty() || keys[2].empty()) {
            return Res::Err("Incorrect key for token type. Object of ['token/ID/key','value'] expected");
        }

        auto resInt = VerifyInt32(keys[1]);
        if (!resInt) {
            return std::move(resInt);
        }

        auto res = parseKey(keys[2]);
        if (!res) {
            return std::move(res);
        }

        UniValue univalue;
        auto tokenKey = *res.val;

        if (tokenKey == TokenKeys::PaybackDFI) {
            if (value != "true" && value != "false") {
                return Res::Err("Payback DFI value must be either \"true\" or \"false\"");
            }
            univalue.setBool(value == "true");
        } else if (tokenKey == TokenKeys::PaybackDFIFeePCT
                || tokenKey == TokenKeys::DexFeePCT) {
            auto res = VerifyPct(value);
            if (!res) {
                return std::move(res);
            }
            univalue = ValueFromAmount(*res.val);
        } else {
            return Res::Err("Unrecognised key");
        }

        if (applyVariable) {
            UniValue values(UniValue::VARR);
            values.push_back(type);
            values.push_back(*resInt.val);
            values.push_back(tokenKey);
            values.push_back(univalue);
            return applyVariable(values);
        }
    } else {
        return Res::Err("Unrecognised type");
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

Res ATTRIBUTES::Import(const UniValue & val) {
    if (!val.isObject()) {
        return Res::Err("Object of values expected");
    }

    std::map<std::string, UniValue> objMap;
    val.getObjMap(objMap);

    for (const auto& pair : objMap) {
        auto res = ProcessVariable(pair.first, pair.second.get_str(),
                                   std::bind(GetType, std::placeholders::_1, allowedTypes),
                                   std::bind(GetType, std::placeholders::_1, allowedTokenKeys),
                                   [this](const UniValue& values) {
            if (values[0].get_int() == AttributeTypes::Token) {
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
        });
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
        // Token should always have three items
        if (type == AttributeTypes::Token && keys.size() == 3) {
            res = GetTypeByKey(keys[2]);
            if (!res) {
                continue;
            }
            auto tokenKey = *res.val;
            try {
                ret.pushKV(displayTypes.at(type) + '/' + keys[1] + '/' + displayTokenKeys.at(tokenKey), item.second);
            } catch (const std::out_of_range&) {
                // Should not get here, if we do perhaps update displayTypes and displayTokenKeys for newly added types and keys.
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
        auto res = ProcessVariable(item.first, item.second, GetTypeByKey, GetTypeByKey,
                                   [&](const UniValue& values) {
            if (values[0].get_int() == AttributeTypes::Token) {
                uint32_t tokenId = uint32_t(values[1].get_int());
                if (values[2].get_int() == TokenKeys::DexFeePCT) {
                    auto token = view.GetToken(DCT_ID{tokenId});
                    if (!token) {
                        return Res::Err("No such token (%d)", tokenId);
                    }
                    if (token->IsPoolShare()) {
                        return Res::Err("Token (%s) is pool share one", token->symbol);
                    }
                    return Res::Ok();
                }
                if (values[2].get_int() == TokenKeys::PaybackDFI
                ||  values[2].get_int() == TokenKeys::PaybackDFIFeePCT) {
                    if (!view.GetLoanTokenByID(DCT_ID{tokenId})) {
                        return Res::Err("No such loan token (%d)", tokenId);
                    }
                    return Res::Ok();
                }
            }
            return Res::Err("Unrecognised type");
        });
        if (!res) {
            return res;
        }
    }

    return Res::Ok();
}

Res ATTRIBUTES::Apply(CCustomCSView & mnview, const uint32_t height)
{
    for (const auto& item : attributes) {
        auto res = ProcessVariable(item.first, item.second, GetTypeByKey, GetTypeByKey,
                                   [&](const UniValue& values) -> Res {
            if (values[0].get_int() == AttributeTypes::Token) {
                if (values[2].get_int() == TokenKeys::DexFeePCT) {
                    auto res = VerifyPct(values[3].getValStr());
                    if (!res) {
                        return std::move(res);
                    }
                    auto tokenId = uint32_t(values[1].get_int());
                    return mnview.SetDexfeePct({tokenId}, *res.val / 100);
                }
            }
            return Res::Ok();
        });
        if (!res) {
            return res;
        }
    }
    return Res::Ok();
}
