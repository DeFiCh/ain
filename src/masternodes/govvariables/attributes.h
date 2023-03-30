// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
#define DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H

#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/gv.h>
#include <masternodes/oracles.h>

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
    Governance = 'g',
    Consortium = 'c',
};

enum ParamIDs : uint8_t {
    DFIP2201   = 'a',
    DFIP2203   = 'b',
    TokenID    = 'c',
    Economy    = 'e',
    DFIP2206A  = 'f',
    DFIP2206F  = 'g',
    Feature    = 'h',
    Auction    = 'i',
    Foundation = 'j',
};

enum OracleIDs : uint8_t {
    Splits = 'a',
};

enum GovernanceIDs : uint8_t {
    Global    = 'a',
    Proposals = 'b',
};

enum EconomyKeys : uint8_t {
    PaybackDFITokens          = 'a',
    PaybackTokens             = 'b',
    DFIP2203Current           = 'c',
    DFIP2203Burned            = 'd',
    DFIP2203Minted            = 'e',
    DFIP2206FCurrent          = 'f',
    DFIP2206FBurned           = 'g',
    DFIP2206FMinted           = 'h',
    DexTokens                 = 'i',
    NegativeInt               = 'j',
    NegativeIntCurrent        = 'k',
    ConsortiumMinted          = 'l',
    ConsortiumMembersMinted   = 'm',
    BatchRoundingExcess       = 'n',  // Extra added to loan amounts on auction creation due to round errors.
    ConsolidatedInterest      = 'o',  // Amount added to loan amounts after auction with no bids.
    PaybackDFITokensPrincipal = 'p',  // Same as PaybackDFITokens but without interest.
    Loans                     = 'q',
};

enum DFIPKeys : uint8_t {
    Active               = 'a',
    Premium              = 'b',
    MinSwap              = 'c',
    RewardPct            = 'd',
    BlockPeriod          = 'e',
    DUSDInterestBurn     = 'g',
    DUSDLoanBurn         = 'h',
    StartBlock           = 'i',
    GovUnset             = 'j',
    GovFoundation        = 'k',
    MNSetRewardAddress   = 'l',
    MNSetOperatorAddress = 'm',
    MNSetOwnerAddress    = 'n',
    ConsortiumEnabled    = 'o',
    Members              = 'p',
    GovernanceEnabled    = 'q',
    CFPPayout            = 'r',
    EmissionUnusedFund   = 's',
    MintTokens           = 't',
    EVMEnabled           = 'u',
};

enum GovernanceKeys : uint8_t {
    FeeRedistribution       = 'a',
    FeeBurnPct              = 'b',
    CFPFee                  = 'd',
    CFPApprovalThreshold    = 'e',
    VOCFee                  = 'f',
    VOCEmergencyFee         = 'g',
    VOCEmergencyPeriod      = 'h',
    VOCApprovalThreshold    = 'i',
    Quorum                  = 'j',
    VotingPeriod            = 'k',
    VOCEmergencyQuorum      = 'l',
    CFPMaxCycles            = 'm',
};

enum TokenKeys : uint8_t {
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
    LoanPaybackCollateral = 'p',
};

enum ConsortiumKeys : uint8_t {
    MemberValues   = 'a',
    MintLimit      = 'b',
    DailyMintLimit = 'c',
};

enum PoolKeys : uint8_t {
    TokenAFeePCT = 'a',
    TokenBFeePCT = 'b',
    TokenAFeeDir = 'c',
    TokenBFeeDir = 'd',
};

struct CDataStructureV0 {
    uint8_t type;
    uint32_t typeId;
    uint32_t key;
    uint32_t keyId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
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
        return type == AttributeTypes::Token && (key == TokenKeys::LoanPayback || key == TokenKeys::LoanPaybackFeePCT);
    }

    bool operator<(const CDataStructureV0 &o) const {
        return std::tie(type, typeId, key, keyId) < std::tie(o.type, o.typeId, o.key, o.keyId);
    }
};

// for future use
struct CDataStructureV1 {
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {}

    bool operator<(const CDataStructureV1 &o) const { return false; }
};

struct CTokenPayback {
    CBalances tokensFee;
    CBalances tokensPayback;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(tokensFee);
        READWRITE(tokensPayback);
    }
};

struct CFeeDir {
    uint8_t feeDir;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(feeDir);
    }
};

ResVal<CScript> GetFutureSwapContractAddress(const std::string &contract);

struct CDexTokenInfo {
    struct CTokenInfo {
        uint64_t swaps;
        uint64_t feeburn;
        uint64_t commissions;

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream &s, Operation ser_action) {
            READWRITE(swaps);
            READWRITE(feeburn);
            READWRITE(commissions);
        }
    };

    CTokenInfo totalTokenA;
    CTokenInfo totalTokenB;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(totalTokenA);
        READWRITE(totalTokenB);
    }
};

enum FeeDirValues : uint8_t { Both, In, Out };

struct CConsortiumMember {
    static const uint16_t MAX_CONSORTIUM_MEMBERS_STRING_LENGTH = 512;
    static const uint16_t MIN_CONSORTIUM_MEMBERS_STRING_LENGTH = 3;
    enum Status : uint8_t {
        Active   = 0,
        Disabled = 0x01,
    };

    std::string name;
    CScript ownerAddress;
    std::string backingId;
    CAmount mintLimit;
    CAmount dailyMintLimit;
    uint8_t status;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(name);
        READWRITE(ownerAddress);
        READWRITE(backingId);
        READWRITE(mintLimit);
        READWRITE(dailyMintLimit);
        READWRITE(status);
    }
};

struct CConsortiumMinted {
    CAmount minted;
    CAmount burnt;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(minted);
        READWRITE(burnt);
    }
};

struct CConsortiumDailyMinted : public CConsortiumMinted {
    std::pair<uint32_t, CAmount> dailyMinted;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(CConsortiumMinted, *this);
        READWRITE(dailyMinted);
    }
};

using CDexBalances             = std::map<DCT_ID, CDexTokenInfo>;
using OracleSplits             = std::map<uint32_t, int32_t>;
using DescendantValue          = std::pair<uint32_t, int32_t>;
using AscendantValue           = std::pair<uint32_t, std::string>;
using CConsortiumMembers       = std::map<std::string, CConsortiumMember>;
using CConsortiumMembersMinted = std::map<DCT_ID, std::map<std::string, CConsortiumDailyMinted>>;
using CConsortiumGlobalMinted  = std::map<DCT_ID, CConsortiumMinted>;
using CAttributeType           = std::variant<CDataStructureV0, CDataStructureV1>;
using CAttributeValue          = std::variant<bool,
                                     CAmount,
                                     CBalances,
                                     CTokenPayback,
                                     CTokenCurrencyPair,
                                     OracleSplits,
                                     DescendantValue,
                                     AscendantValue,
                                     CFeeDir,
                                     CDexBalances,
                                     std::set<CScript>,
                                     std::set<std::string>,
                                     CConsortiumMembers,
                                     CConsortiumMembersMinted,
                                     CConsortiumGlobalMinted,
                                     int32_t,
                                     uint32_t>;

void TrackNegativeInterest(CCustomCSView &mnview, const CTokenAmount &amount);
void TrackLiveBalances(CCustomCSView &mnview, const CBalances &balances, const uint8_t key);
void TrackDUSDAdd(CCustomCSView &mnview, const CTokenAmount &amount);
void TrackDUSDSub(CCustomCSView &mnview, const CTokenAmount &amount);

enum GovVarsFilter {
    All,
    NoAttributes,
    AttributesOnly,
    PrefixedAttributes,
    LiveAttributes,
    Version2Dot7,
};

class ATTRIBUTES : public GovVariable, public AutoRegistrator<GovVariable, ATTRIBUTES> {
public:
    virtual ~ATTRIBUTES() override {}

    std::string GetName() const override { return TypeName(); }

    bool IsEmpty() const override;
    Res Import(const UniValue &val) override;
    UniValue Export() const override;
    UniValue ExportFiltered(GovVarsFilter filter, const std::string &prefix) const;

    Res Validate(const CCustomCSView &mnview) const override;
    Res Apply(CCustomCSView &mnview, const uint32_t height) override;
    Res Erase(CCustomCSView &mnview, uint32_t height, const std::vector<std::string> &) override;

    static constexpr const char *TypeName() { return "ATTRIBUTES"; }
    static GovVariable *Create() { return new ATTRIBUTES(); }

    template <typename T>
    static void GetIf(std::optional<T> &opt, const CAttributeValue &var) {
        if (auto value = std::get_if<T>(&var)) {
            opt = *value;
        }
    }

    template <typename T>
    static void GetIf(T &val, const CAttributeValue &var) {
        if (auto value = std::get_if<T>(&var)) {
            val = *value;
        }
    }

    template <typename K, typename T>
    [[nodiscard]] T GetValue(const K &key, T value) const {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        auto it = attributes.find(key);
        if (it != attributes.end()) {
            GetIf(value, it->second);
        }
        return value;
    }

    template <typename K, typename T>
    void SetValue(const K &key, T &&value) {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        static_assert(std::is_convertible_v<T, CAttributeValue>);
        changed.insert(key);
        attributes[key] = std::forward<T>(value);
    }

    template <typename K>
    bool EraseKey(const K &key) {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        if (attributes.erase(key)) {
            changed.insert(key);
            return true;
        }
        return false;
    }

    template <typename K>
    [[nodiscard]] bool CheckKey(const K &key) const {
        static_assert(std::is_convertible_v<K, CAttributeType>);
        return attributes.count(key) > 0;
    }

    template <typename C, typename K>
    void ForEach(const C &callback, const K &key) const {
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

    [[nodiscard]] const std::map<CAttributeType, CAttributeValue> &GetAttributesMap() const { return attributes; }

    ADD_OVERRIDE_VECTOR_SERIALIZE_METHODS
    ADD_OVERRIDE_SERIALIZE_METHODS(CDataStream)

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(attributes);
    }

    uint32_t time{0};

    // For formatting in export
    static const std::map<uint8_t, std::string> &displayVersions();
    static const std::map<uint8_t, std::string> &displayTypes();
    static const std::map<uint8_t, std::string> &displayParamsIDs();
    static const std::map<uint8_t, std::string> &displayOracleIDs();
    static const std::map<uint8_t, std::string> &displayConsortiumIDs();
    static const std::map<uint8_t, std::string> &displayGovernanceIDs();
    static const std::map<uint8_t, std::map<uint8_t, std::string>> &displayKeys();

    Res RefundFuturesContracts(CCustomCSView &mnview,
                               const uint32_t height,
                               const uint32_t tokenID = std::numeric_limits<uint32_t>::max());

private:
    friend class CGovView;
    bool futureUpdated{};
    bool futureDUSDUpdated{};
    std::set<uint32_t> tokenSplits{};
    std::set<uint32_t> interestTokens{};
    std::set<CAttributeType> changed;
    std::map<CAttributeType, CAttributeValue> attributes;

    // Defined allowed arguments
    static const std::map<std::string, uint8_t> &allowedVersions();
    static const std::map<std::string, uint8_t> &allowedTypes();
    static const std::map<std::string, uint8_t> &allowedParamIDs();
    static const std::map<std::string, uint8_t> &allowedLocksIDs();
    static const std::map<std::string, uint8_t> &allowedOracleIDs();
    static const std::map<std::string, uint8_t> &allowedConsortiumIDs();
    static const std::map<std::string, uint8_t> &allowedGovernanceIDs();
    static const std::map<uint8_t, std::map<std::string, uint8_t>> &allowedKeys();
    static const std::map<uint8_t, std::map<uint8_t, std::function<ResVal<CAttributeValue>(const std::string &)>>>
        &parseValue();

    Res ProcessVariable(const std::string &key,
                        const std::optional<UniValue> &value,
                        std::function<Res(const CAttributeType &, const CAttributeValue &)> applyVariable);
    Res RefundFuturesDUSD(CCustomCSView &mnview, const uint32_t height);
};

#endif  // DEFI_MASTERNODES_GOVVARIABLES_ATTRIBUTES_H
