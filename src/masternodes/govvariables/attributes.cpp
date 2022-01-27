// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/attributes.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <util/strencodings.h>

extern UniValue AmountsToJSON(TAmounts const & diffs);

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
        return Res::Err("Amount must be a positive value");
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

static Res ShowError(const std::string& key, const std::map<std::string, uint8_t>& keys) {
    std::string error{"Unrecognised " + key + " argument provided, valid " + key + "s are:"};
    for (const auto& pair : keys) {
        error += ' ' + pair.first + ',';
    }
    return Res::Err(error);
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
        return ::ShowError("type", allowedTypes);
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
            return ::ShowError("param", allowedParamIDs);
        }

        typeId = id->second;
    }

    auto ikey = allowedKeys.find(type);
    if (ikey == allowedKeys.end()) {
        return Res::Err("Unsupported type {%d}", type);
    }

    itype = ikey->second.find(keys[3]);
    if (itype == ikey->second.end()) {
        return ::ShowError("key", ikey->second);
    }

    UniValue univalue;
    auto typeKey = itype->second;

    CAttributeValue attribValue;

    if (type == AttributeTypes::Token) {
        if (typeKey == TokenKeys::PaybackDFI) {
            if (value != "true" && value != "false") {
                return Res::Err("Payback DFI value must be either \"true\" or \"false\"");
            }
            attribValue = value == "true";
        } else if (typeKey == TokenKeys::PaybackDFIFeePCT) {
            auto res = VerifyPct(value);
            if (!res) {
                return std::move(res);
            }
            attribValue = *res.val;
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
            attribValue = *res.val;
        } else {
            return Res::Err("Unrecognised key");
        }
    } else if (type == AttributeTypes::Param) {
        if (typeId == ParamIDs::DFIP2201) {
            if (typeKey == DFIP2201Keys::Active) {
                if (value != "true" && value != "false") {
                    return Res::Err("DFIP2201 actve value must be either \"true\" or \"false\"");
                }
                attribValue = value == "true";
            } else if (typeKey == DFIP2201Keys::Premium) {
                auto res = VerifyPct(value);
                if (!res) {
                    return std::move(res);
                }
                attribValue = *res.val;
            } else if (typeKey == DFIP2201Keys::MinSwap) {
                auto res = VerifyFloat(value);
                if (!res) {
                    return std::move(res);
                }
                attribValue = *res.val;
            } else {
                return Res::Err("Unrecognised key");
            }
        }
    } else {
        return Res::Err("Unrecognised type");
    }

    if (applyVariable) {
        return applyVariable(CDataStructureV0{type, typeId, typeKey}, attribValue);
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
                if (auto attrV0 = std::get_if<CDataStructureV0>(&attribute)) {
                    if (attrV0->type == AttributeTypes::Live) {
                        return Res::Err("Live attribute cannot be set externally");
                    }
                }
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
        auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            continue;
        }
        try {
            const auto id = attrV0->type == AttributeTypes::Param
                         || attrV0->type == AttributeTypes::Live
                            ? displayParamsIDs.at(attrV0->typeId)
                            : KeyBuilder(attrV0->typeId);

            auto key = KeyBuilder(displayVersions.at(VersionTypes::v0),
                                  displayTypes.at(attrV0->type),
                                  id,
                                  displayKeys.at(attrV0->type).at(attrV0->key));

            if (auto bool_val = std::get_if<bool>(&attribute.second)) {
                ret.pushKV(key, *bool_val ? "true" : "false");
            } else if (auto amount = std::get_if<CAmount>(&attribute.second)) {
                auto uvalue = ValueFromAmount(*amount);
                ret.pushKV(key, KeyBuilder(uvalue.get_real()));
            } else if (auto balances = std::get_if<CBalances>(&attribute.second)) {
                ret.pushKV(key, AmountsToJSON(balances->balances));
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
            case AttributeTypes::Token: {
                if (attrV0->key == TokenKeys::PaybackDFI
                ||  attrV0->key == TokenKeys::PaybackDFIFeePCT) {
                    uint32_t tokenId = attrV0->typeId;
                    if (!view.GetLoanTokenByID(DCT_ID{tokenId})) {
                        return Res::Err("No such loan token (%d)", tokenId);
                    }
                } else {
                    return Res::Err("Unsupported key");
                }
            }
            break;

            case AttributeTypes::Poolpairs: {
                if (!std::get_if<CAmount>(&attribute.second)) {
                    return Res::Err("Unsupported value");
                }
                if (attrV0->key == PoolKeys::TokenAFeePCT
                ||  attrV0->key == PoolKeys::TokenBFeePCT) {
                    uint32_t poolId = attrV0->typeId;
                    if (!view.GetPoolPair(DCT_ID{poolId})) {
                        return Res::Err("No such pool (%d)", poolId);
                    }
                } else {
                    return Res::Err("Unsupported key");
                }
            }
            break;

            case AttributeTypes::Param: {
                if (attrV0->typeId != ParamIDs::DFIP2201) {
                    return Res::Err("Unrecognised param id");
                }
            }
            break;

            // Live is set internally
            case AttributeTypes::Live:
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
        if (attrV0 && attrV0->type == AttributeTypes::Poolpairs) {
            uint32_t poolId = attrV0->typeId;
            auto pool = mnview.GetPoolPair(DCT_ID{poolId});
            if (!pool) {
                return Res::Err("No such pool (%d)", poolId);
            }
            auto tokenId = attrV0->key == PoolKeys::TokenAFeePCT ?
                                        pool->idTokenA : pool->idTokenB;

            auto valuePct = std::get<CAmount>(attribute.second);
            auto res = mnview.SetDexFeePct(DCT_ID{poolId}, tokenId, valuePct);
            if (!res) {
                return res;
            }
        }
    }
    return Res::Ok();
}
