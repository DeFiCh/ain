// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_GOVVARIABLES_ATTRIBUTETYPES_H
#define DEFI_DFI_GOVVARIABLES_ATTRIBUTETYPES_H

#include <amount.h>
#include <dfi/balances.h>
#include <dfi/evm.h>
#include <dfi/loan.h>
#include <dfi/oracles.h>

enum VersionTypes : uint8_t {
    v0 = 0,
};

enum AttributeTypes : uint8_t {
    Live = 'l',
    Oracles = 'o',
    Param = 'a',
    Token = 't',
    Poolpairs = 'p',
    Locks = 'L',
    Governance = 'g',
    Transfer = 'b',
    EVMType = 'e',
    Vaults = 'v',
    Rules = 'r',
};

enum ParamIDs : uint8_t {
    DFIP2201 = 'a',
    DFIP2203 = 'b',
    TokenID = 'c',
    Economy = 'e',
    DFIP2206A = 'f',
    DFIP2206F = 'g',
    Feature = 'h',
    Auction = 'i',
    Foundation = 'j',
};

enum OracleIDs : uint8_t {
    Splits = 'a',
};

enum EVMIDs : uint8_t {
    Block = 'a',
};

enum EVMKeys : uint8_t {
    Finalized = 'a',
    GasLimit = 'b',
    GasTargetFactor = 'c',
    RbfIncrementMinPct = 'd',
};

enum GovernanceIDs : uint8_t {
    Global = 'a',
    Proposals = 'b',
};

enum TransferIDs : uint8_t {
    DVMToEVM = 'a',
    EVMToDVM = 'b',
};

enum VaultIDs : uint8_t {
    DUSDVault = 'a',
};

enum RulesIDs : uint8_t {
    TXRules = 'a',
};

enum EconomyKeys : uint8_t {
    PaybackDFITokens = 'a',
    PaybackTokens = 'b',
    DFIP2203Current = 'c',
    DFIP2203Burned = 'd',
    DFIP2203Minted = 'e',
    DFIP2206FCurrent = 'f',
    DFIP2206FBurned = 'g',
    DFIP2206FMinted = 'h',
    DexTokens = 'i',
    NegativeInt = 'j',
    NegativeIntCurrent = 'k',
    BatchRoundingExcess = 'n',        // Extra added to loan amounts on auction creation due to round errors.
    ConsolidatedInterest = 'o',       // Amount added to loan amounts after auction with no bids.
    PaybackDFITokensPrincipal = 'p',  // Same as PaybackDFITokens but without interest.
    Loans = 'q',
    TransferDomainStatsLive = 'r',
    EVMBlockStatsLive = 's',
};

enum DFIPKeys : uint8_t {
    Active = 'a',
    Premium = 'b',
    MinSwap = 'c',
    RewardPct = 'd',
    BlockPeriod = 'e',
    DUSDInterestBurn = 'g',
    DUSDLoanBurn = 'h',
    StartBlock = 'i',
    GovUnset = 'j',
    GovFoundation = 'k',
    MNSetRewardAddress = 'l',
    MNSetOperatorAddress = 'm',
    MNSetOwnerAddress = 'n',
    Members = 'p',
    GovernanceEnabled = 'q',
    CFPPayout = 'r',
    EmissionUnusedFund = 's',
    MintTokens = 't',
    EVMEnabled = 'u',
    ICXEnabled = 'v',
    TransferDomain = 'w',
};

enum GovernanceKeys : uint8_t {
    FeeRedistribution = 'a',
    FeeBurnPct = 'b',
    CFPFee = 'd',
    CFPApprovalThreshold = 'e',
    VOCFee = 'f',
    VOCEmergencyFee = 'g',
    VOCEmergencyPeriod = 'h',
    VOCApprovalThreshold = 'i',
    Quorum = 'j',
    VotingPeriod = 'k',
    VOCEmergencyQuorum = 'l',
    CFPMaxCycles = 'm',
};

enum TokenKeys : uint8_t {
    PaybackDFI = 'a',
    PaybackDFIFeePCT = 'b',
    LoanPayback = 'c',
    LoanPaybackFeePCT = 'd',
    DexInFeePct = 'e',
    DexOutFeePct = 'f',
    DFIP2203Enabled = 'g',
    FixedIntervalPriceId = 'h',
    LoanCollateralEnabled = 'i',
    LoanCollateralFactor = 'j',
    LoanMintingEnabled = 'k',
    LoanMintingInterest = 'l',
    Ascendant = 'm',
    Descendant = 'n',
    Epitaph = 'o',
    LoanPaybackCollateral = 'p',
};

enum PoolKeys : uint8_t {
    TokenAFeePCT = 'a',
    TokenBFeePCT = 'b',
    TokenAFeeDir = 'c',
    TokenBFeeDir = 'd',
};

enum TransferKeys : uint8_t {
    TransferEnabled = 'a',
    SrcFormats = 'b',
    DestFormats = 'c',
    AuthFormats = 'd',
    NativeEnabled = 'e',
    DATEnabled = 'f',
    Disallowed = 'g',
};

enum VaultKeys : uint8_t {
    DUSDVaultEnabled = 'w',
};

enum RulesKeys : uint8_t {
    CoreOPReturn = 'a',
    DVMOPReturn = 'b',
    EVMOPReturn = 'c',
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

struct CTransferDomainStatsLive {
    CStatsTokenBalances dvmEvmTotal;
    CStatsTokenBalances evmDvmTotal;
    CStatsTokenBalances dvmIn;
    CStatsTokenBalances evmIn;
    CStatsTokenBalances dvmOut;
    CStatsTokenBalances evmOut;
    CStatsTokenBalances dvmCurrent;
    CStatsTokenBalances evmCurrent;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(dvmEvmTotal);
        READWRITE(evmDvmTotal);
        READWRITE(dvmIn);
        READWRITE(evmIn);
        READWRITE(dvmOut);
        READWRITE(evmOut);
        READWRITE(dvmCurrent);
        READWRITE(evmCurrent);
    }

    static constexpr CDataStructureV0 Key = {AttributeTypes::Live,
                                             ParamIDs::Economy,
                                             EconomyKeys::TransferDomainStatsLive};
};

enum XVmAddressFormatTypes : uint8_t {
    None,
    Bech32,
    Bech32ProxyErc55,
    PkHash,
    PkHashProxyErc55,
    Erc55,
};

struct CEvmBlockStatsLive {
    CAmount feeBurnt;
    CAmount feeBurntMin = std::numeric_limits<CAmount>::max();
    uint256 feeBurntMinHash;
    CAmount feeBurntMax = std::numeric_limits<CAmount>::min();
    uint256 feeBurntMaxHash;
    CAmount feePriority;
    CAmount feePriorityMin = std::numeric_limits<CAmount>::max();
    uint256 feePriorityMinHash;
    CAmount feePriorityMax = std::numeric_limits<CAmount>::min();
    uint256 feePriorityMaxHash;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(feeBurnt);
        READWRITE(feeBurntMin);
        READWRITE(feeBurntMinHash);
        READWRITE(feeBurntMax);
        READWRITE(feeBurntMaxHash);
        READWRITE(feePriority);
        READWRITE(feePriorityMin);
        READWRITE(feePriorityMinHash);
        READWRITE(feePriorityMax);
        READWRITE(feePriorityMaxHash);
    }

    static constexpr CDataStructureV0 Key = {AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::EVMBlockStatsLive};
};

using CDexBalances = std::map<DCT_ID, CDexTokenInfo>;
using OracleSplits = std::map<uint32_t, int32_t>;
using DescendantValue = std::pair<uint32_t, int32_t>;
using AscendantValue = std::pair<uint32_t, std::string>;
using CAttributeType = std::variant<CDataStructureV0, CDataStructureV1>;
using XVmAddressFormatItems = std::set<uint8_t>;

// Unused legacy types but can be changed and update for future use.
// Required for sync to maintain consistent variant indexing.
using LegacyEntry1 = std::map<std::string, std::string>;
using LegacyEntry2 = std::map<std::string, uint64_t>;
using LegacyEntry3 = std::map<std::string, int64_t>;

using CAttributeValue = std::variant<bool,
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
                                     LegacyEntry1,
                                     LegacyEntry2,
                                     LegacyEntry3,
                                     int32_t,
                                     uint32_t,
                                     uint64_t,
                                     XVmAddressFormatItems,
                                     CTransferDomainStatsLive,
                                     CEvmBlockStatsLive>;

#endif  // DEFI_DFI_GOVVARIABLES_ATTRIBUTETYPES_H