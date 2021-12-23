// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ATTRIBUTES_H
#define DEFI_MASTERNODES_ATTRIBUTES_H

#include <masternodes/gv.h>

enum AttributeTypes : uint8_t {
    Token = 0x00,
};

enum TokenKeys : uint8_t  {
    PaybackDFI = 0x00,
    PaybackDFIFeePCT = 0x01,
};

struct AttributesKey {
    uint8_t type;
    std::string identifier;
    uint8_t key;

    bool operator<(const AttributesKey& comp) const {
        return std::tie(type, identifier, key) < std::tie(comp.type, comp.identifier, comp.key);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(type);
        READWRITE(identifier);
        READWRITE(key);
    }
};

class ATTRIBUTES : public GovVariable, public AutoRegistrator<GovVariable, ATTRIBUTES>
{
public:
    virtual ~ATTRIBUTES() override {}

    std::string GetName() const override {
        return TypeName();
    }

    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView &mnview, const uint32_t height) override;

    static constexpr char const * TypeName() { return "ATTRIBUTES"; }
    static GovVariable * Create() { return new ATTRIBUTES(); }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(attributes);
    }

    std::map<AttributesKey, std::string> attributes;

private:
    // Defined allowed arguments
    const std::map<std::string, uint8_t> allowedTypes{{"token", AttributeTypes::Token}};
    const std::map<std::string, uint8_t> allowedTokenKeys{{"payback_dfi", TokenKeys::PaybackDFI},
                                                    {"payback_dfi_fee_pct", TokenKeys::PaybackDFIFeePCT}};

    // For formatting in export
    const std::map<uint8_t, std::string> displayTypes{{ AttributeTypes::Token, "token"}};
    const std::map<uint8_t, std::string> displayTokenKeys{{TokenKeys::PaybackDFI, "payback_dfi"},
                                                    {TokenKeys::PaybackDFIFeePCT, "payback_dfi_fee_pct"}};
};

#endif // DEFI_MASTERNODES_ATTRIBUTES_H
