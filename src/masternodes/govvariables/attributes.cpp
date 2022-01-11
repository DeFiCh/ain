// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/attributes.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView

template<typename T>
static std::string KeyBuilder(const T& value){
    std::ostringstream oss;
    oss << value;
    return oss.str();
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

struct AttributesKey {
    uint8_t key;
    uint8_t type;
    std::string identifier;
};

static ResVal<int32_t> VerifyInt32(const std::string& str) {
    int32_t int32;
    if (!ParseInt32(str, &int32) || int32 < 0) {
        return Res::Err("Identifier for must be a positive integer");
    }
    return {int32, Res::Ok()};
}

static bool VerifyPct(const std::string& str) {
    auto res = VerifyInt32(str);
    return res && *res.val <= COIN;
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
        if (pair.first.size() > 128) {
            return Res::Err("Identifier exceed maximum length (128)");
        }

        const auto keys = KeyBreaker(pair.first);
        if (keys.empty() || keys[0].empty()) {
            return Res::Err("Empty key");
        }

        const auto& value = pair.second.get_str();
        if (value.empty()) {
            return Res::Err("Empty value");
        }

        auto res = GetType(keys[0], allowedTypes);
        if (!res) {
            return std::move(res);
        }

        auto type = *res.val;

        if (type == AttributeTypes::Token) {

            if (keys.size() != 3 || keys[1].empty() || keys[2].empty()) {
                return Res::Err("Incorrect key for token type. Object of ['token/ID/key','value'] expected");
            }

            if (!VerifyInt32(keys[1])) {
                return Res::Err("Identifier for token must be a positive integer");
            }

            auto res = GetType(keys[2], allowedTokenKeys);
            if (!res) {
                return std::move(res);
            }

            auto key = *res.val;

            if (key == TokenKeys::PaybackDFI) {
                if (value != "true" && value != "false") {
                    return Res::Err("Payback DFI value must be either \"true\" or \"false\"");
                }
            } else if (key == TokenKeys::PaybackDFIFeePCT) {
                if (!VerifyPct(value)) {
                    return Res::Err("Payback DFI fee percentage value must be a positive integer");
                }
            } else {
                return Res::Err("Unrecognised key");
            }

            attributes[KeyBuilder(type, keys[1], key)] = value;
        }
    }
    return Res::Ok();
}

UniValue ATTRIBUTES::Export() const {
    UniValue res(UniValue::VOBJ);
    for (const auto& item : attributes) {
        const auto strVec = KeyBreaker(item.first);
        // Should never be empty
        if (!strVec.empty()) {
            // Token should always have three items
            if (strVec[0].size() == 1 && strVec[0].at(0) == AttributeTypes::Token && strVec.size() == 3 && strVec[2].size() == 1) {
                try {
                    res.pushKV(displayTypes.at(AttributeTypes::Token) + '/' + strVec[1] + '/' + displayTokenKeys.at(strVec[2].at(0)), item.second);
                } catch (const std::out_of_range&) {
                    // Should not get here, if we do perhaps update displayTypes and displayTokenKeys for newly added types and keys.
                }
            }
        }
    }
    return res;
}

Res ATTRIBUTES::Validate(const CCustomCSView & view) const
{
    if (view.GetLastHeight() < Params().GetConsensus().FortCanningHillHeight)
        return Res::Err("Cannot be set before FortCanningHill");

    for (const auto& item : attributes) {
        if (item.first.size() > 128) {
            return Res::Err("Identifier exceed maximum length (128)");
        }

        const auto strVec = KeyBreaker(item.first);

        if (strVec.empty()) {
            return Res::Err("Empty map key found");
        }

        if (strVec[0].size() != 1) {
            return Res::Err("Incorrect attribute length");
        }

        auto type = strVec[0][0];

        if (type == AttributeTypes::Token) {

            if (strVec.size() != 3 || strVec[1].empty() || strVec[2].empty()) {
                return Res::Err("Incorrect key for token type. Object of ['token/ID/key','value'] expected");
            }

            auto res = VerifyInt32(strVec[1]);
            if (!res) {
                return std::move(res);
            }

            auto tokenId = uint32_t(*res.val);

            auto key = strVec[2][0];

            if (key == TokenKeys::PaybackDFI) {
                if (!view.GetLoanTokenByID({tokenId})) {
                    return Res::Err("Invalid loan token specified");
                }
                if (item.second != "true" && item.second != "false") {
                    return Res::Err("Payback DFI value must be either \"true\" or \"false\"");
                }
            } else if (key == TokenKeys::PaybackDFIFeePCT) {
                if (!view.GetLoanTokenByID({tokenId})) {
                    return Res::Err("Invalid loan token specified");
                }
                if (!VerifyPct(item.second)) {
                    return Res::Err("Payback DFI fee percentage value must be a positive integer");
                }
            } else {
                return Res::Err("Unrecognised key");
            }
        } else {
            return Res::Err("Unrecognised type");
        }
    }

    return Res::Ok();
}

Res ATTRIBUTES::Apply(CCustomCSView & mnview, const uint32_t height)
{
    return Res::Ok();
}
