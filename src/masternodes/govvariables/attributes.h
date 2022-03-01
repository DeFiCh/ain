// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
#define DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H

#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/gv.h>

enum VersionTypes : uint8_t {
    v0 = 0,
};

enum AttributeTypes : uint8_t {
    Live      = 'l',
    Param     = 'a',
    Token     = 't',
    Poolpairs = 'p',
};

enum ParamIDs : uint8_t  {
    DFIP2201  = 'a',
    Economy   = 'e',
};

enum EconomyKeys : uint8_t {
    PaybackDFITokens = 'a',
    PaybackTokens    = 'b',
};

enum DFIP2201Keys : uint8_t  {
    Active    = 'a',
    Premium   = 'b',
    MinSwap   = 'c',
};

enum TokenKeys : uint8_t  {
    PaybackDFI          = 'a',
    PaybackDFIFeePCT    = 'b',
    LoanPayback         = 'c',
    LoanPaybackFeePCT   = 'd',
};

enum PoolKeys : uint8_t {
    TokenAFeePCT = 'a',
    TokenBFeePCT = 'b',
};

struct CDataStructureV0 {
    uint8_t type;
    uint32_t typeId;
    uint8_t key;
    uint32_t keyId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(type);
        READWRITE(typeId);
        READWRITE(key);
        if (IsExtendedSize()) {
            READWRITE(keyId);
        } else {
            keyId = 0;
        }
    }

    bool IsExtendedSize() const {
        return type == AttributeTypes::Token
            && (key == TokenKeys::LoanPayback
            ||  key == TokenKeys::LoanPaybackFeePCT);
    }

    bool operator<(const CDataStructureV0& o) const {
        return std::tie(type, typeId, key, keyId) < std::tie(o.type, o.typeId, o.key, o.keyId);
    }
};

// for future use
struct CDataStructureV1 {
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {}

     bool operator<(const CDataStructureV1& o) const { return false; }
};

struct CTokenPayback {
    CBalances tokensFee;
    CBalances tokensPayback;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(tokensFee);
        READWRITE(tokensPayback);
    }
};

using CAttributeType = boost::variant<CDataStructureV0, CDataStructureV1>;
using CAttributeValue = boost::variant<bool, CAmount, CBalances, CTokenPayback>;

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

    template<typename T>
    T GetValue(const CAttributeType& key, T value) {
        auto it = attributes.find(key);
        if (it != attributes.end()) {
            if (auto val = boost::get<const T>(&it->second)) {
                value = std::move(*val);
            }
        }
        return std::move(value);
    }

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
        {"params",      AttributeTypes::Param},
        {"poolpairs",   AttributeTypes::Poolpairs},
        {"token",       AttributeTypes::Token},
    };

    const std::map<std::string, uint8_t> allowedParamIDs{
        {"dfip2201",         ParamIDs::DFIP2201}
    };

    const std::map<uint8_t, std::map<std::string, uint8_t>> allowedKeys{
        {
            AttributeTypes::Token, {
                {"payback_dfi",         TokenKeys::PaybackDFI},
                {"payback_dfi_fee_pct", TokenKeys::PaybackDFIFeePCT},
                {"loan_payback",        TokenKeys::LoanPayback},
                {"loan_payback_fee_pct",TokenKeys::LoanPaybackFeePCT},
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
        {AttributeTypes::Live,      "live"},
        {AttributeTypes::Param,     "params"},
        {AttributeTypes::Poolpairs, "poolpairs"},
        {AttributeTypes::Token,     "token"},
    };

    const std::map<uint8_t, std::string> displayParamsIDs{
        {ParamIDs::DFIP2201,       "dfip2201"},
        {ParamIDs::Economy,        "economy"},
    };

    const std::map<uint8_t, std::map<uint8_t, std::string>> displayKeys{
        {
            AttributeTypes::Token, {
                {TokenKeys::PaybackDFI,       "payback_dfi"},
                {TokenKeys::PaybackDFIFeePCT, "payback_dfi_fee_pct"},
                {TokenKeys::LoanPayback,      "loan_payback"},
                {TokenKeys::LoanPaybackFeePCT,"loan_payback_fee_pct"},
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
        {
            AttributeTypes::Live, {
                {EconomyKeys::PaybackDFITokens,  "dfi_payback_tokens"},
                {EconomyKeys::PaybackTokens,     "payback_tokens"},
            }
        },
    };

    Res ProcessVariable(const std::string& key, const std::string& value,
                        std::function<Res(const CAttributeType&, const CAttributeValue&)> applyVariable = {}) const;
};

#endif // DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
