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
    if (!val.isArray()) {
        return Res::Err("Array of values expected");
    }

    const auto& array = val.get_array();
    if (array.empty()) {
        return Res::Err("Empty array, no vaules provided");
    }

    AttributesKey mapKey;
    try {
        mapKey.type = allowedTypes.at(array[0].getValStr());
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
            if (array.size() != 4) {
                return Res::Err("Incorrect number of items for token type. Array of ['token','loan token ID','key','value'] expected");
            }

            int32_t tokenId;
            if (!ParseInt32(array[1].getValStr(), &tokenId) || tokenId < 1) {
                return Res::Err("Identifier for token must be a positive integer");
            }

            mapKey.identifier = array[1].get_str();

            try {
                mapKey.key = allowedTokenKeys.at(array[2].get_str());
            } catch(const std::out_of_range&) {
                std::string error{"Unrecognised key argument provided, valid keys are:"};
                for (const auto& pair : allowedTokenKeys) {
                    error += " " + pair.first + ",";
                }
                return Res::Err(error);
            }

            switch(mapKey.key) {
                case TokenKeys::PaybackDFI:
                    int32_t paybackDFI;
                    if (!ParseInt32(array[3].get_str(), &paybackDFI) || paybackDFI < 0 || paybackDFI > 1) {
                        return Res::Err("Payback DFI value must be either 0 or 1");
                    }
                    attributes[KeyBuilder(mapKey.type, mapKey.identifier, mapKey.key)] = array[3].getValStr();
                    break;
                case TokenKeys::PaybackDFIFeePCT:
                    int32_t paybackDFIFeePCT;
                    if (!ParseInt32(array[3].getValStr(), &paybackDFIFeePCT) || paybackDFIFeePCT < 0) {
                        return Res::Err("Payback DFI fee percentage value must be a positive integer");
                    }
                    attributes[KeyBuilder(mapKey.type, mapKey.identifier, mapKey.key)] = array[3].getValStr();
                    break;
            }
            break;
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
                int32_t paybackDFI;
                if (!ParseInt32(item.second, &paybackDFI) || paybackDFI < 0 || paybackDFI > 1) {
                    return Res::Err("Payback DFI value must be either 0 or 1");
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
    // On implemenation of features that rely on ATTRIBUTES store values in mnview where they are needed.
    return Res::Ok();
}
