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
    std::vector<std::string> strVec;
    size_t last = 0, next = 0;
    while ((next = str.find("/", last)) != std::string::npos) {
        strVec.push_back(str.substr(last, next - last));
        last = next + 1;
    }
    strVec.push_back(str.substr(last, str.size()));

    return strVec;
}

struct AttributesKey {
    uint8_t type;
    std::string identifier;
    uint8_t key;
};

Res ATTRIBUTES::Import(const UniValue & val) {
    if (!val.isObject()) {
        return Res::Err("Object of values expected");
    }

    std::map<std::string,UniValue> objMap;
    val.getObjMap(objMap);
    for (const auto pair : objMap) {
        const auto key = KeyBreaker(pair.first);
        if (key.empty() || key[0].empty()) {
            return Res::Err("Empty key");
        }

        const auto& value = pair.second.get_str();
        if (value.empty()) {
            return Res::Err("Empty value");
        }

        AttributesKey mapKey;
        try {
            mapKey.type = allowedTypes.at(key[0]);
        } catch(const std::out_of_range&) {
            std::string error{"Unrecognised type argument provided, valid types are:"};
            for (const auto& pair : allowedTypes) {
                error += " " + pair.first + ",";
            }
            return Res::Err(error);
        }

        switch(mapKey.type) {
            // switch for the many more types coming later!
            case AttributeTypes::Token:
                if (key.size() != 3 || key[1].empty() || key[2].empty()) {
                    return Res::Err("Incorrect key for token type. Object of ['token/ID/key','value'] expected");
                }
                int32_t tokenId;
                if (!ParseInt32(key[1], &tokenId) || tokenId < 1) {
                    return Res::Err("Identifier for token must be a positive integer");
                }

                mapKey.identifier = key[1];

                try {
                    mapKey.key = allowedTokenKeys.at(key[2]);
                } catch(const std::out_of_range&) {
                    std::string error{"Unrecognised key argument provided, valid keys are:"};
                    for (const auto& pair : allowedTokenKeys) {
                        error += " " + pair.first + ",";
                    }
                    return Res::Err(error);
                }


                switch(mapKey.key) {
                    case TokenKeys::PaybackDFI:
                        if (value != "true" && value != "false") {
                            return Res::Err("Payback DFI value must be either \"true\" or \"false\"");
                        }
                        attributes[KeyBuilder(mapKey.type, mapKey.identifier, mapKey.key)] = value;
                        break;
                    case TokenKeys::PaybackDFIFeePCT:
                        int32_t paybackDFIFeePCT;
                        if (!ParseInt32(value, &paybackDFIFeePCT) || paybackDFIFeePCT < 0) {
                            return Res::Err("Payback DFI fee percentage value must be a positive integer");
                        }
                        attributes[KeyBuilder(mapKey.type, mapKey.identifier, mapKey.key)] = value;
                        break;
                }
                break;
        }
    }


    return Res::Ok();
}

UniValue ATTRIBUTES::Export() const {
    UniValue res(UniValue::VOBJ);
    for (const auto& item : attributes) {
        const auto strVec= KeyBreaker(item.first);
        // Should never be empty
        if (!strVec.empty()) {
            // Token should always have three items
            if (strVec[0].size() == 1 && strVec[0].at(0) == AttributeTypes::Token && strVec.size() == 3 && strVec[2].size() == 1) {
                try {
                    res.pushKV(displayTypes.at(AttributeTypes::Token) + "/" + strVec[1] + "/" + displayTokenKeys.at(strVec[2].at(0)), item.second);
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
    if (view.GetLastHeight() < Params().GetConsensus().FortCanningHillHeight) {
        return Res::Err("Cannot be set before FortCanningHill");
    }

    for (const auto& item : attributes) {
        const auto strVec= KeyBreaker(item.first);

        if (strVec.empty()) {
            return Res::Err("Empty map key found");
        }

        // switch for the many more types coming later!
        if (strVec[0].size() == 1 && strVec[0].at(0) == AttributeTypes::Token) {
            if (strVec.size() != 3) {
                return Res::Err("Three items expected in token key");
            }

            int32_t tokenId;
            if (!ParseInt32(strVec[1], &tokenId) || tokenId < 1) {
                return Res::Err("Identifier for token must be a positive integer");
            }

            if (!view.GetLoanTokenByID({static_cast<uint32_t>(tokenId)})) {
                return Res::Err("Invalid loan token specified");
            }

            if (strVec[2].size() == 1 && strVec[2].at(0) == TokenKeys::PaybackDFI) {
                if (item.second != "true" && item.second != "false") {
                    return Res::Err("Payback DFI value must be either \"true\" or \"false\"");
                }
            } else if (strVec[2].size() == 1 && strVec[2].at(0) == TokenKeys::PaybackDFIFeePCT) {
                int32_t paybackDFIFeePCT;
                if (!ParseInt32(item.second, &paybackDFIFeePCT) || paybackDFIFeePCT < 0) {
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
