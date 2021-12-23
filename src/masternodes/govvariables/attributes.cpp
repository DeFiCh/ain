// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/attributes.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView

Res ATTRIBUTES::Import(const UniValue & val) {
    if (!val.isArray()) {
        return Res::Err("Object of ['type','id','key','value'] expected");
    }

    const auto& array = val.get_array();
    if (array.size() != 4) {
        return Res::Err("Incorrect number of items. Object of ['type','id','key','value'] expected");
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
                    attributes[mapKey] = array[3].getValStr();
                    break;
                case TokenKeys::PaybackDFIFeePCT:
                    int32_t paybackDFIFeePCT;
                    if (!ParseInt32(array[3].getValStr(), &paybackDFIFeePCT) || paybackDFIFeePCT < 0) {
                        return Res::Err("Payback DFI fee percentage value must be a positive integer");
                    }
                    attributes[mapKey] = array[3].getValStr();
                    break;
            }
            break;
    }

    return Res::Ok();
}

UniValue ATTRIBUTES::Export() const {
    UniValue res(UniValue::VARR);
    for (const auto& item : attributes) {
        // switch for the many more types coming later!
        switch(item.first.type) {
            case AttributeTypes::Token:
                try {
                    res.pushKV(displayTypes.at(item.first.type) + "/" + item.first.identifier + "/" + displayTokenKeys.at(item.first.key), item.second);
                } catch (const std::out_of_range&) {
                    // Should not get here, if we do perhaps update displayTypes and displayTokenKeys for newly added types and keys.
                }
                break;
        }
    }
    return {};
}

Res ATTRIBUTES::Validate(const CCustomCSView & view) const
{
    if (view.GetLastHeight() < Params().GetConsensus().FortCanningHillHeight) {
        return Res::Err("Cannot be set before FortCanningHill");
    }

    for (const auto& item : attributes) {
        // switch for the many more types coming later!
        switch(item.first.type) {
            case AttributeTypes::Token:
                int32_t tokenId;
                if (!ParseInt32(item.first.identifier, &tokenId) || tokenId < 1) {
                    return Res::Err("Identifier for token must be a positive integer");
                }
                if (!view.GetLoanTokenByID({static_cast<uint32_t>(tokenId)})) {
                    return Res::Err("Invalid loan token specified");
                }

                switch(item.first.key) {
                    case TokenKeys::PaybackDFI:
                        int32_t paybackDFI;
                        if (!ParseInt32(item.second, &paybackDFI) || paybackDFI < 0 || paybackDFI > 1) {
                            return Res::Err("Payback DFI value must be either 0 or 1");
                        }
                        break;
                    case TokenKeys::PaybackDFIFeePCT:
                        int32_t paybackDFIFeePCT;
                        if (!ParseInt32(item.second, &paybackDFIFeePCT) || paybackDFIFeePCT < 0) {
                            return Res::Err("Payback DFI fee percentage value must be a positive integer");
                        }
                        break;
                    default:
                        return Res::Err("Unrecognised key");
                }

                break;
            default:
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
