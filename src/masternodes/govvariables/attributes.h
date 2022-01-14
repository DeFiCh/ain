// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
#define DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H

#include <masternodes/gv.h>

enum AttributeTypes : uint8_t {
    Token     = 't',
    Poolpairs = 'p',
};

enum TokenKeys : uint8_t  {
    PaybackDFI       = 'a',
    PaybackDFIFeePCT = 'b',
};

enum PoolKeys : uint8_t {
    TokenAFeePCT = 'a',
    TokenBFeePCT = 'b',
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

    std::map<std::string, std::string> attributes;

private:
    // Defined allowed arguments
    const std::map<std::string, uint8_t> allowedTypes{
        {"token",       AttributeTypes::Token},
        {"poolpairs",   AttributeTypes::Poolpairs},
    };

    const std::map<uint8_t, std::map<std::string, uint8_t>> allowedKeys{
        {
            AttributeTypes::Token, {
                {"payback_dfi",         TokenKeys::PaybackDFI},
                {"payback_dfi_fee_pct", TokenKeys::PaybackDFIFeePCT},
            }
        },
        {
            AttributeTypes::Poolpairs, {
                {"token_a_fee_pct",     PoolKeys::TokenAFeePCT},
                {"token_b_fee_pct",     PoolKeys::TokenBFeePCT},
            }
        },
    };

    // For formatting in export
    const std::map<uint8_t, std::string> displayTypes{
        {AttributeTypes::Token,     "token"},
        {AttributeTypes::Poolpairs, "poolpairs"},
    };

    const std::map<uint8_t, std::map<uint8_t, std::string>> displayKeys{
        {
            AttributeTypes::Token, {
                {TokenKeys::PaybackDFI,       "payback_dfi"},
                {TokenKeys::PaybackDFIFeePCT, "payback_dfi_fee_pct"},
            }
        },
        {
            AttributeTypes::Poolpairs, {
                {PoolKeys::TokenAFeePCT,      "token_a_fee_pct"},
                {PoolKeys::TokenBFeePCT,      "token_b_fee_pct"},
            }
        },
    };
};

#endif // DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
