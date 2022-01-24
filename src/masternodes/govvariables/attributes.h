// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
#define DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H

#include <amount.h>
#include <masternodes/gv.h>

enum VersionTypes : uint8_t {
    v0 = 0,
};

enum AttributeTypes : uint8_t {
    Param     = 'a',
    Token     = 't',
    Poolpairs = 'p',
};

enum ParamIDs : uint8_t  {
    DFIP2201  = 'a',
};

enum DFIP2201Keys : uint8_t  {
    Active    = 'a',
    Premium   = 'b',
    MinSwap   = 'c',
};

enum TokenKeys : uint8_t  {
    PaybackDFI       = 'a',
    PaybackDFIFeePCT = 'b',
};

enum PoolKeys : uint8_t {
    TokenAFeePCT = 'a',
    TokenBFeePCT = 'b',
};

using CValueV0 = boost::variant<bool, CAmount>;

struct CDataStructureV0 {
    uint8_t type;
    uint32_t typeId;
    uint8_t key;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(type);
        READWRITE(typeId);
        READWRITE(key);
    }

    bool operator<(const CDataStructureV0& o) const {
        return type < o.type
            || (type == o.type
            && key < o.key);
    }
};

// for future use
struct CDataStructureV1 {
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {}

     bool operator<(const CDataStructureV1& o) const { return false; }
};

struct CValueV1 {
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {}
};

using CAttributeType = boost::variant<CDataStructureV0, CDataStructureV1>;
using CAttributeValue = boost::variant<CValueV0, CValueV1>;

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

    std::map<CAttributeType, CAttributeValue> attributes;

private:
    // Defined allowed arguments
    const std::map<std::string, uint8_t> allowedVersions{
        {"v0",          VersionTypes::v0},
    };

    const std::map<std::string, uint8_t> allowedTypes{
        {"token",       AttributeTypes::Token},
        {"poolpairs",   AttributeTypes::Poolpairs},
        {"params",      AttributeTypes::Param},
    };

    const std::map<std::string, uint8_t> allowedParamIDs{
        {"dfip2201",         ParamIDs::DFIP2201}
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
        {
            AttributeTypes::Param, {
                {"active",              DFIP2201Keys::Active},
                {"minswap",             DFIP2201Keys::MinSwap},
                {"premium",             DFIP2201Keys::Premium},
            }
        },
    };

    // For formatting in export
    const std::map<uint8_t, std::string> displayVersions{
        {VersionTypes::v0,          "v0"},
    };

    const std::map<uint8_t, std::string> displayTypes{
        {AttributeTypes::Token,     "token"},
        {AttributeTypes::Poolpairs, "poolpairs"},
        {AttributeTypes::Param,     "params"},
    };

    const std::map<uint8_t, std::string> displayParamsIDs{
        {ParamIDs::DFIP2201,       "dfip2201"}
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
        {
            AttributeTypes::Param, {
                {DFIP2201Keys::Active,       "active"},
                {DFIP2201Keys::Premium,      "premium"},
                {DFIP2201Keys::MinSwap,      "minswap"},
            }
        },
    };

    Res ProcessVariable(const std::string& key, const std::string& value,
                        std::function<Res(const CAttributeType&, const CAttributeValue&)> applyVariable = {}) const;
};

#endif // DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
