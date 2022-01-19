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

static ResVal<CAmount> VerifyFloat(const std::string& str) {
    CAmount amount = 0;
    if (!ParseFixedPoint(str, 8, &amount) || amount < 0) {
        return Res::Err("Amount must be a float");
    }
    return {amount, Res::Ok()};
}

static ResVal<CAmount> VerifyPct(const std::string& str) {
    auto resVal = VerifyFloat(str);
    if (!resVal) {
        return resVal;
    }
    if (*resVal.val > COIN) {
        return Res::Err("Percentage exceeds 100%%");
    }
    return resVal;
}

Res ATTRIBUTES::ProcessVariable(const std::string& key, const std::string& value,
                                std::function<Res(const CAttributeType&, const CAttributeValue&)> applyVariable) const {

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

    auto iver = allowedVersions.find(keys[0]);
    if (iver == allowedVersions.end()) {
        return Res::Err("Unsupported version");
    }

    auto version = iver->second;
    if (version != VersionTypes::v0) {
        return Res::Err("Unsupported version");
    }

    if (keys.size() != 4 || keys[1].empty() || keys[2].empty() || keys[3].empty()) {
        return Res::Err("Incorrect key for <type>. Object of ['<version>/<type>/ID/<key>','value'] expected");
    }

    auto itype = allowedTypes.find(keys[1]);
    if (itype == allowedTypes.end()) {
        std::string error{"Unrecognised type argument provided, valid types are:"};
        for (const auto& pair : allowedTypes) {
            error += ' ' + pair.first + ',';
        }
        return Res::Err(error);
    }

    auto type = itype->second;

    uint32_t typeId{0};
    if (type != AttributeTypes::Param) {
        auto id = VerifyInt32(keys[2]);
        if (!id) {
            return std::move(id);
        }

        typeId = *id.val;
    } else {
        auto id = allowedParamIDs.find(keys[2]);
        if (id == allowedParamIDs.end()) {
            return Res::Err("Unsupported ID");
        }

        typeId = id->second;
    }

    auto ikey = allowedKeys.find(type);
    if (ikey == allowedKeys.end()) {
        return Res::Err("Unsupported type");
    }

    itype = ikey->second.find(keys[3]);
    if (itype == ikey->second.end()) {
        std::string error{"Unrecognised key argument provided, valid keys are:"};
        for (const auto& pair : ikey->second) {
            error += ' ' + pair.first + ',';
        }
        return Res::Err(error);
    }

    UniValue univalue;
    auto typeKey = itype->second;

    CValueV0 valueV0;

    if (type == AttributeTypes::Token) {
        if (typeKey == TokenKeys::PaybackDFI) {
            if (value != "true" && value != "false") {
                return Res::Err("Payback DFI value must be either \"true\" or \"false\"");
            }
            valueV0 = value == "true";
        } else if (typeKey == TokenKeys::PaybackDFIFeePCT) {
            auto res = VerifyPct(value);
            if (!res) {
                return std::move(res);
            }
            valueV0 = *res.val;
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
            valueV0 = *res.val;
        } else {
            return Res::Err("Unrecognised key");
        }
    } else if (type == AttributeTypes::Param) {
        if (typeId == ParamIDs::DFIP2201) {
            if (typeKey == DFIP2201Keys::Active) {
                if (value != "true" && value != "false") {
                    return Res::Err("DFIP2201 actve value must be either \"true\" or \"false\"");
                }
                valueV0 = value == "true";
            } else if (typeKey == DFIP2201Keys::Premium) {
                auto res = VerifyPct(value);
                if (!res) {
                    return std::move(res);
                }
                valueV0 = *res.val;
            } else if (typeKey == DFIP2201Keys::MinSwap) {
                auto res = VerifyFloat(value);
                if (!res) {
                    return std::move(res);
                }
                valueV0 = *res.val;
            } else {
                return Res::Err("Unrecognised key");
            }
        }
    } else {
        return Res::Err("Unrecognised type");
    }

    if (applyVariable) {
        return applyVariable(CDataStructureV0{type, typeId, typeKey}, valueV0);
    }
    return Res::Ok();
}

Res ATTRIBUTES::Import(const UniValue & val) {
    if (!val.isObject()) {
        return Res::Err("Object of values expected");
    }

    std::map<std::string, UniValue> objMap;
    val.getObjMap(objMap);

    for (const auto& pair : objMap) {
        auto res = ProcessVariable(
            pair.first, pair.second.get_str(), [this](const CAttributeType& attribute, const CAttributeValue& value) {
                attributes[attribute] = value;
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
        auto attrV0 = boost::get<const CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            continue;
        }
        auto valV0 = boost::get<const CValueV0>(&attribute.second);
        if (!valV0) {
            continue;
        }
        try {
            const std::string id = attrV0->type == AttributeTypes::Param ? displayParamsIDs.at(attrV0->typeId) : KeyBuilder(attrV0->typeId);
            auto key = KeyBuilder(displayVersions.at(VersionTypes::v0),
                                  displayTypes.at(attrV0->type),
                                  id,
                                  displayKeys.at(attrV0->type).at(attrV0->key));

            if (auto bool_val = boost::get<const bool>(valV0)) {
                ret.pushKV(key, *bool_val ? "true" : "false");
            } else if (auto amount = boost::get<const CAmount>(valV0)) {
                auto uvalue = ValueFromAmount(*amount);
                ret.pushKV(key, KeyBuilder(uvalue.get_real()));
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
        auto attrV0 = boost::get<const CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            return Res::Err("Unsupported version");
        }
        auto valV0 = boost::get<const CValueV0>(&attribute.second);
        if (!valV0) {
            return Res::Err("Unsupported value");
        }
        if (attrV0->type == AttributeTypes::Token) {
            uint32_t tokenId = attrV0->typeId;
            if (!view.GetLoanTokenByID(DCT_ID{tokenId})) {
                return Res::Err("No such loan token (%d)", tokenId);
            }
            if (attrV0->key == TokenKeys::PaybackDFI) {
                if (!boost::get<const bool>(valV0)) {
                    return Res::Err("Unsupported value");
                }
                continue;
            }
            if (attrV0->key == TokenKeys::PaybackDFIFeePCT) {
                if (!boost::get<const CAmount>(valV0)) {
                    return Res::Err("Unsupported value");
                }
                continue;
            }
        }
        if (attrV0->type == AttributeTypes::Poolpairs) {
            if (!boost::get<const CAmount>(valV0)) {
                return Res::Err("Unsupported value");
            }
            uint32_t poolId = attrV0->typeId;
            if (attrV0->key == PoolKeys::TokenAFeePCT
            ||  attrV0->key == PoolKeys::TokenBFeePCT) {
                if (!view.GetPoolPair(DCT_ID{poolId})) {
                    return Res::Err("No such pool (%d)", poolId);
                }
                continue;
            }
        }
        if (attrV0->type == AttributeTypes::Param) {
            if (attrV0->typeId == ParamIDs::DFIP2201) {
                return Res::Ok();
            }
            return Res::Err("Unrecognised param id");
        }
        return Res::Err("Unrecognised type");
    }

    return Res::Ok();
}

Res ATTRIBUTES::Apply(CCustomCSView & mnview, const uint32_t height)
{
    for (const auto& attribute : attributes) {
        auto attrV0 = boost::get<const CDataStructureV0>(&attribute.first);
        if (attrV0 && attrV0->type == AttributeTypes::Poolpairs) {
            uint32_t poolId = attrV0->typeId;
            auto pool = mnview.GetPoolPair(DCT_ID{poolId});
            if (!pool) {
                return Res::Err("No such pool (%d)", poolId);
            }
            auto tokenId = attrV0->key == PoolKeys::TokenAFeePCT ?
                                        pool->idTokenA : pool->idTokenB;

            if (auto valV0 = boost::get<const CValueV0>(&attribute.second)) {
                auto valuePct = boost::get<const CAmount>(*valV0);
                auto res = mnview.SetDexFeePct(DCT_ID{poolId}, tokenId, valuePct);
                if (!res) {
                    return res;
                }
            }
        }
    }
    return Res::Ok();
}
