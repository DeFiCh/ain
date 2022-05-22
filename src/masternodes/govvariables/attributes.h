// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
#define DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H

#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/gv.h>
#include <masternodes/oracles.h>

#include <variant>

class CFutureSwapView;

enum VersionTypes : uint8_t {
    v0 = 0,
};

enum AttributeTypes : uint8_t {
    Live       = 'l',
    Oracles    = 'o',
    Param      = 'a',
    Token      = 't',
    Poolpairs  = 'p',
    Locks      = 'L',
    Consortium = 'c',
};

enum ParamIDs : uint8_t  {
    DFIP2201  = 'a',
    DFIP2203  = 'b',
    TokenID   = 'c',
    Economy   = 'e',
};

enum OracleIDs : uint8_t  {
    Splits    = 'a',
};

enum EconomyKeys : uint8_t {
    PaybackDFITokens        = 'a',
    PaybackTokens           = 'b',
    DFIP2203Current         = 'c',
    DFIP2203Burned          = 'd',
    DFIP2203Minted          = 'e',
    DexTokens               = 'f',
    ConsortiumMinted        = 'g',
    ConsortiumMembersMinted = 'h',
};

enum DFIPKeys : uint8_t  {
    Active       = 'a',
    Premium      = 'b',
    MinSwap      = 'c',
    RewardPct    = 'd',
    BlockPeriod  = 'e',
    StartBlock   = 'f',
};

enum TokenKeys : uint8_t  {
    PaybackDFI            = 'a',
    PaybackDFIFeePCT      = 'b',
    LoanPayback           = 'c',
    LoanPaybackFeePCT     = 'd',
    DexInFeePct           = 'e',
    DexOutFeePct          = 'f',
    DFIP2203Enabled       = 'g',
    FixedIntervalPriceId  = 'h',
    LoanCollateralEnabled = 'i',
    LoanCollateralFactor  = 'j',
    LoanMintingEnabled    = 'k',
    LoanMintingInterest   = 'l',
    Ascendant             = 'm',
    Descendant            = 'n',
    Epitaph               = 'o',
};

enum ConsortiumKeys : uint8_t  {
    Members               = 'a',
    MintLimit             = 'b',
};

enum PoolKeys : uint8_t {
    TokenAFeePCT = 'a',
    TokenBFeePCT = 'b',
};

struct CDataStructureV0 {
    uint8_t type;
    uint32_t typeId;
    uint32_t key;
    uint32_t keyId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(type);
        READWRITE(typeId);
        READWRITE(VARINT(key));
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

struct CDexTokenInfo {

    struct CTokenInfo {
        uint64_t swaps;
        uint64_t feeburn;
        uint64_t commissions;

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            READWRITE(swaps);
            READWRITE(feeburn);
            READWRITE(commissions);
        }
    };

    CTokenInfo totalTokenA;
    CTokenInfo totalTokenB;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(totalTokenA);
        READWRITE(totalTokenB);
    }
};

struct CConsortiumMember
{
    static const uint16_t MAX_CONSORTIUM_MEMBERS_STRING_LENGTH = 512;
    enum Status : uint8_t
    {
        Active = 0,
        Disabled = 0x01,
    };

    std::string name;
    CScript ownerAddress;
    std::string backingId;
    CAmount mintLimit;
    uint8_t status;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(name);
        READWRITE(ownerAddress);
        READWRITE(backingId);
        READWRITE(mintLimit);
        READWRITE(status);
    }
};

struct CConsortiumMinted
{
    CAmount minted;
    CAmount burnt;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(minted);
        READWRITE(burnt);
    }
};

using CDexBalances = std::map<DCT_ID, CDexTokenInfo>;
using OracleSplits = std::map<uint32_t, int32_t>;
using DescendantValue = std::pair<uint32_t, int32_t>;
using AscendantValue = std::pair<uint32_t, std::string>;
using CConsortiumMembers = std::map<std::string, CConsortiumMember>;
using CConsortiumMembersMinted = std::map<DCT_ID, std::map<std::string, CConsortiumMinted>>;
using CConsortiumGlobalMinted = std::map<DCT_ID, CConsortiumMinted>;

using CAttributeType = std::variant<CDataStructureV0>;
using CAttributeValue = std::variant<bool, CAmount, CBalances, CTokenPayback, CDexBalances, CTokenCurrencyPair, OracleSplits, DescendantValue, AscendantValue,
                                        CConsortiumMembers, CConsortiumMembersMinted, CConsortiumGlobalMinted>;

class ATTRIBUTES : public GovVariable, public AutoRegistrator<GovVariable, ATTRIBUTES>
{
public:
    bool IsEmpty() const override;
    Res Import(UniValue const &val) override;
    UniValue Export() const override;
    Res Validate(CCustomCSView const &mnview) const override;
    Res Apply(CCustomCSView& mnview, uint32_t height) override { return Res::Err("Calling the wrong Apply"); };
    Res Apply(CCustomCSView& mnview, CFutureSwapView& futureSwapView, uint32_t height);
    Res Erase(CCustomCSView& mnview, uint32_t height, std::vector<std::string> const &) override;

    std::string GetName() const override { return TypeName(); }
    static constexpr char const * TypeName() { return "ATTRIBUTES"; }
    static GovVariable * Create() { return new ATTRIBUTES(); }

    template<typename T>
    static void GetIf(std::optional<T>& opt, const CAttributeValue& var) {
        if (auto value = std::get_if<T>(&var)) {
            opt = *value;
        }
    }

    template<typename T>
    static void GetIf(T& val, const CAttributeValue& var) {
        if (auto value = std::get_if<T>(&var)) {
            val = *value;
        }
    }

    template<typename K, typename T>
    [[nodiscard]] T GetValue(const K& key, T value) const {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        auto it = attributes.find(key);
        if (it != attributes.end()) {
            GetIf(value, it->second);
        }
        return value;
    }

    template<typename K, typename T>
    void SetValue(const K& key, T&& value) {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        static_assert(std::is_convertible_v<T, CAttributeValue>);
        changed.insert(key);
        attributes[key] = std::forward<T>(value);
    }

    template<typename K>
    bool EraseKey(const K& key) {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        if (attributes.erase(key)) {
            changed.insert(key);
            return true;
        }
        return false;
    }

    template<typename K>
    [[nodiscard]] bool CheckKey(const K& key) const {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        return attributes.count(key) > 0;
    }

    template<typename C, typename K>
    void ForEach(const C& callback, const K& key) const {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        static_assert(std::is_invocable_r_v<bool, C, K, CAttributeValue>);
        for (auto it = attributes.lower_bound(key); it != attributes.end(); ++it) {
            if (auto attrV0 = std::get_if<K>(&it->first)) {
                if (!std::invoke(callback, *attrV0, it->second)) {
                    break;
                }
            }
        }
    }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(attributes);
    }

    uint32_t time{0};
    std::map<CAttributeType, CAttributeValue> attributes;

    // For formatting in export
    static const std::map<uint8_t, std::string>& displayVersions();
    static const std::map<uint8_t, std::string>& displayTypes();
    static const std::map<uint8_t, std::string>& displayParamsIDs();
    static const std::map<uint8_t, std::string>& displayOracleIDs();
    static const std::map<uint8_t, std::map<uint8_t, std::string>>& displayKeys();
private:
    friend class CGovView;
    bool futureBlockUpdated{};
    std::set<uint32_t> tokenSplits{};
    std::set<CAttributeType> changed;

    // Defined allowed arguments
    static const std::map<std::string, uint8_t>& allowedVersions();
    static const std::map<std::string, uint8_t>& allowedTypes();
    static const std::map<std::string, uint8_t>& allowedParamIDs();
    static const std::map<std::string, uint8_t>& allowedLocksIDs();
    static const std::map<std::string, uint8_t>& allowedOracleIDs();
    static const std::map<uint8_t, std::map<std::string, uint8_t>>& allowedKeys();
    static const std::map<uint8_t, std::map<uint8_t,
            std::function<ResVal<CAttributeValue>(const std::string&)>>>& parseValue();

    Res ProcessVariable(const std::string& key, std::optional<std::string> value,
                        const std::function<Res(const CAttributeType&, const CAttributeValue&)>& applyVariable);
    Res RefundFuturesContracts(CCustomCSView &mnview, CFutureSwapView& futureSwapView, const uint32_t height, const uint32_t tokenID = std::numeric_limits<uint32_t>::max());
};

ResVal<CScript> GetFutureSwapContractAddress();

#endif // DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
