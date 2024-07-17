// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/govvariables/attributes.h>
#include <dfi/mn_rpc.h>

#include <dfi/accountshistory.h>  /// CAccountsHistoryWriter
#include <dfi/errors.h>           /// DeFiErrors
#include <dfi/historywriter.h>    /// CHiistoryWriter
#include <dfi/masternodes.h>      /// CCustomCSView
#include <dfi/mn_checks.h>        /// GetAggregatePrice / CustomTxType
#include <dfi/validation.h>       /// DEFAULT_LIQUIDITY_CALC_SAMPLING_PERIOD
#include <validation.h>           /// GetNextAccPosition

#include <amount.h>   /// GetDecimaleString
#include <core_io.h>  /// ValueFromAmount
#include <util/strencodings.h>

#include <ain_rs_exports.h>
#include <ffi/ffihelpers.h>

enum class EVMAttributesTypes : uint32_t {
    Finalized = 1,
    GasLimit = 2,
    GasTargetFactor = 3,
    RbfIncrementMinPct = 4,
};

extern UniValue AmountsToJSON(const CCustomCSView &view,
                              const TAmounts &diffs,
                              AmountFormat format = AmountFormat::Symbol);

static inline std::string trim_all_ws(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

static std::vector<std::string> KeyBreaker(const std::string &str, const char delim = '/') {
    std::string section;
    std::istringstream stream(str);
    std::vector<std::string> strVec;

    while (std::getline(stream, section, delim)) {
        strVec.push_back(section);
    }
    return strVec;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedVersions() {
    static const std::map<std::string, uint8_t> versions{
        {"v0", VersionTypes::v0},
    };
    return versions;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayVersions() {
    static const std::map<uint8_t, std::string> versions{
        {VersionTypes::v0, "v0"},
    };
    return versions;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedTypes() {
    static const std::map<std::string, uint8_t> types{
        {"locks",          AttributeTypes::Locks     },
        {"oracles",        AttributeTypes::Oracles   },
        {"params",         AttributeTypes::Param     },
        {"poolpairs",      AttributeTypes::Poolpairs },
        {"token",          AttributeTypes::Token     },
        {"gov",            AttributeTypes::Governance},
        {"transferdomain", AttributeTypes::Transfer  },
        {"evm",            AttributeTypes::EVMType   },
        {"vaults",         AttributeTypes::Vaults    },
        {"rules",          AttributeTypes::Rules     },
    };
    return types;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayTypes() {
    static const std::map<uint8_t, std::string> types{
        {AttributeTypes::Live,       "live"          },
        {AttributeTypes::Locks,      "locks"         },
        {AttributeTypes::Oracles,    "oracles"       },
        {AttributeTypes::Param,      "params"        },
        {AttributeTypes::Poolpairs,  "poolpairs"     },
        {AttributeTypes::Token,      "token"         },
        {AttributeTypes::Governance, "gov"           },
        {AttributeTypes::Transfer,   "transferdomain"},
        {AttributeTypes::EVMType,    "evm"           },
        {AttributeTypes::Vaults,     "vaults"        },
        {AttributeTypes::Rules,      "rules"         },
    };
    return types;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedParamIDs() {
    static const std::map<std::string, uint8_t> params{
        {"dfip2201",   ParamIDs::DFIP2201  },
        {"dfip2203",   ParamIDs::DFIP2203  },
        {"dfip2206a",  ParamIDs::DFIP2206A },
 // Note: DFIP2206F is currently in beta testing
  // for testnet. May not be enabled on mainnet until testing is complete.
        {"dfip2206f",  ParamIDs::DFIP2206F },
        {"dfip2211f",  ParamIDs::DFIP2211F },
        {"feature",    ParamIDs::Feature   },
        {"foundation", ParamIDs::Foundation},
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::allowedExportParamsIDs() {
    static const std::map<uint8_t, std::string> params{
        {ParamIDs::DFIP2201,   "dfip2201"  },
        {ParamIDs::DFIP2203,   "dfip2203"  },
        {ParamIDs::DFIP2206A,  "dfip2206a" },
        {ParamIDs::DFIP2206F,  "dfip2206f" },
        {ParamIDs::DFIP2211F,  "dfip2211f" },
        {ParamIDs::Feature,    "feature"   },
        {ParamIDs::Foundation, "foundation"},
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayParamsIDs() {
    static auto params = allowedExportParamsIDs();
    params[ParamIDs::Auction] = "auction";
    params[ParamIDs::Economy] = "economy";
    params[ParamIDs::TokenID] = "token";
    return params;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedLocksIDs() {
    static const std::map<std::string, uint8_t> params{
        {"token", ParamIDs::TokenID},
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayLocksIDs() {
    static const std::map<uint8_t, std::string> params{
        {ParamIDs::TokenID, "token"},
    };
    return params;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedOracleIDs() {
    static const std::map<std::string, uint8_t> params{
        {"splits", OracleIDs::Splits}
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayOracleIDs() {
    static const std::map<uint8_t, std::string> params{
        {OracleIDs::Splits, "splits"},
    };
    return params;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedEVMIDs() {
    static const std::map<std::string, uint8_t> params{
        {"block", EVMIDs::Block},
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayEVMIDs() {
    static const std::map<uint8_t, std::string> params{
        {EVMIDs::Block, "block"},
    };
    return params;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedGovernanceIDs() {
    static const std::map<std::string, uint8_t> params{
        {"proposals", GovernanceIDs::Proposals},
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayGovernanceIDs() {
    static const std::map<uint8_t, std::string> params{
        {GovernanceIDs::Proposals, "proposals"},
    };
    return params;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedTransferIDs() {
    static const std::map<std::string, uint8_t> params{
        {"evm-dvm", TransferIDs::EVMToDVM},
        {"dvm-evm", TransferIDs::DVMToEVM},
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayTransferIDs() {
    static const std::map<uint8_t, std::string> params{
        {TransferIDs::EVMToDVM, "evm-dvm"},
        {TransferIDs::DVMToEVM, "dvm-evm"},
    };
    return params;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedVaultIDs() {
    static const std::map<std::string, uint8_t> params{
        {"dusd-vault", VaultIDs::DUSDVault },
        {"params",     VaultIDs::Parameters},
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayVaultIDs() {
    static const std::map<uint8_t, std::string> params{
        {VaultIDs::DUSDVault,  "dusd-vault"},
        {VaultIDs::Parameters, "params"    },
    };
    return params;
}

const std::map<std::string, uint8_t> &ATTRIBUTES::allowedRulesIDs() {
    static const std::map<std::string, uint8_t> params{
        {"tx", RulesIDs::TXRules},
    };
    return params;
}

const std::map<uint8_t, std::string> &ATTRIBUTES::displayRulesIDs() {
    static const std::map<uint8_t, std::string> params{
        {RulesIDs::TXRules, "tx"},
    };
    return params;
}

const std::map<uint8_t, std::map<std::string, uint8_t>> &ATTRIBUTES::allowedKeys() {
    static const std::map<uint8_t, std::map<std::string, uint8_t>> keys{
        {AttributeTypes::Token,
         {
             {"payback_dfi", TokenKeys::PaybackDFI},
             {"payback_dfi_fee_pct", TokenKeys::PaybackDFIFeePCT},
             {"loan_payback", TokenKeys::LoanPayback},
             {"loan_payback_fee_pct", TokenKeys::LoanPaybackFeePCT},
             {"loan_payback_collateral", TokenKeys::LoanPaybackCollateral},
             {"dex_in_fee_pct", TokenKeys::DexInFeePct},
             {"dex_out_fee_pct", TokenKeys::DexOutFeePct},
             {"dfip2203", TokenKeys::DFIP2203Enabled},
             {"fixed_interval_price_id", TokenKeys::FixedIntervalPriceId},
             {"loan_collateral_enabled", TokenKeys::LoanCollateralEnabled},
             {"loan_collateral_factor", TokenKeys::LoanCollateralFactor},
             {"loan_minting_enabled", TokenKeys::LoanMintingEnabled},
             {"loan_minting_interest", TokenKeys::LoanMintingInterest},
         }},
        {AttributeTypes::Poolpairs,
         {
             {"token_a_fee_pct", PoolKeys::TokenAFeePCT},
             {"token_a_fee_direction", PoolKeys::TokenAFeeDir},
             {"token_b_fee_pct", PoolKeys::TokenBFeePCT},
             {"token_b_fee_direction", PoolKeys::TokenBFeeDir},
         }},
        {AttributeTypes::Param,
         {
             {"active", DFIPKeys::Active},
             {"minswap", DFIPKeys::MinSwap},
             {"premium", DFIPKeys::Premium},
             {"reward_pct", DFIPKeys::RewardPct},
             {"block_period", DFIPKeys::BlockPeriod},
             {"dusd_interest_burn", DFIPKeys::DUSDInterestBurn},
             {"dusd_loan_burn", DFIPKeys::DUSDLoanBurn},
             {"start_block", DFIPKeys::StartBlock},
             {"gov-unset", DFIPKeys::GovUnset},
             {"gov-foundation", DFIPKeys::GovFoundation},
             {"mn-setrewardaddress", DFIPKeys::MNSetRewardAddress},
             {"mn-setoperatoraddress", DFIPKeys::MNSetOperatorAddress},
             {"mn-setowneraddress", DFIPKeys::MNSetOwnerAddress},
             {"gov", DFIPKeys::GovernanceEnabled},
             {"evm", DFIPKeys::EVMEnabled},
             {"icx", DFIPKeys::ICXEnabled},
             {"members", DFIPKeys::Members},
             {"gov-payout", DFIPKeys::CFPPayout},
             {"emission-unused-fund", DFIPKeys::EmissionUnusedFund},
             {"mint-tokens-to-address", DFIPKeys::MintTokens},
             {"transferdomain", DFIPKeys::TransferDomain},
             {"liquidity_calc_sampling_period", DFIPKeys::LiquidityCalcSamplingPeriod},
             {"average_liquidity_percentage", DFIPKeys::AverageLiquidityPercentage},
         }},
        {AttributeTypes::EVMType,
         {
             {"finality_count", EVMKeys::Finalized},
             {"gas_limit", EVMKeys::GasLimit},
             {"gas_target_factor", EVMKeys::GasTargetFactor},
             {"rbf_increment_fee_pct", EVMKeys::RbfIncrementMinPct},
         }},
        {AttributeTypes::Governance,
         {
             {"fee_redistribution", GovernanceKeys::FeeRedistribution},
             {"fee_burn_pct", GovernanceKeys::FeeBurnPct},
             {"cfp_fee", GovernanceKeys::CFPFee},
             {"cfp_approval_threshold", GovernanceKeys::CFPApprovalThreshold},
             {"voc_fee", GovernanceKeys::VOCFee},
             {"voc_emergency_fee", GovernanceKeys::VOCEmergencyFee},
             {"voc_emergency_period", GovernanceKeys::VOCEmergencyPeriod},
             {"voc_emergency_quorum", GovernanceKeys::VOCEmergencyQuorum},
             {"voc_approval_threshold", GovernanceKeys::VOCApprovalThreshold},
             {"quorum", GovernanceKeys::Quorum},
             {"voting_period", GovernanceKeys::VotingPeriod},
             {"cfp_max_cycles", GovernanceKeys::CFPMaxCycles},
         }},
        {AttributeTypes::Transfer,
         {
             {"enabled", TransferKeys::TransferEnabled},
             {"src-formats", TransferKeys::SrcFormats},
             {"dest-formats", TransferKeys::DestFormats},
             {"auth-formats", TransferKeys::AuthFormats},
             {"native-enabled", TransferKeys::NativeEnabled},
             {"dat-enabled", TransferKeys::DATEnabled},
             {"disallowed", TransferKeys::Disallowed},
         }},
        {AttributeTypes::Vaults,
         {
             {"creation_fee", VaultKeys::CreationFee},
             {"enabled", VaultKeys::DUSDVaultEnabled},
         }},
        {AttributeTypes::Rules,
         {
             {"core_op_return_max_size_bytes", RulesKeys::CoreOPReturn},
             {"dvm_op_return_max_size_bytes", RulesKeys::DVMOPReturn},
             {"evm_op_return_max_size_bytes", RulesKeys::EVMOPReturn},
         }},
        {AttributeTypes::Oracles,
         {
             {"fractional_enabled", OracleKeys::FractionalSplits},
         }},
    };
    return keys;
}

const std::map<uint8_t, std::map<uint8_t, std::string>> &ATTRIBUTES::displayKeys() {
    static const std::map<uint8_t, std::map<uint8_t, std::string>> keys{
        {AttributeTypes::Token,
         {
             {TokenKeys::PaybackDFI, "payback_dfi"},
             {TokenKeys::PaybackDFIFeePCT, "payback_dfi_fee_pct"},
             {TokenKeys::LoanPayback, "loan_payback"},
             {TokenKeys::LoanPaybackFeePCT, "loan_payback_fee_pct"},
             {TokenKeys::LoanPaybackCollateral, "loan_payback_collateral"},
             {TokenKeys::DexInFeePct, "dex_in_fee_pct"},
             {TokenKeys::DexOutFeePct, "dex_out_fee_pct"},
             {TokenKeys::FixedIntervalPriceId, "fixed_interval_price_id"},
             {TokenKeys::LoanCollateralEnabled, "loan_collateral_enabled"},
             {TokenKeys::LoanCollateralFactor, "loan_collateral_factor"},
             {TokenKeys::LoanMintingEnabled, "loan_minting_enabled"},
             {TokenKeys::LoanMintingInterest, "loan_minting_interest"},
             {TokenKeys::DFIP2203Enabled, "dfip2203"},
             {TokenKeys::Ascendant, "ascendant"},
             {TokenKeys::Descendant, "descendant"},
             {TokenKeys::Epitaph, "epitaph"},
         }},
        {AttributeTypes::Poolpairs,
         {
             {PoolKeys::TokenAFeePCT, "token_a_fee_pct"},
             {PoolKeys::TokenAFeeDir, "token_a_fee_direction"},
             {PoolKeys::TokenBFeePCT, "token_b_fee_pct"},
             {PoolKeys::TokenBFeeDir, "token_b_fee_direction"},
         }},
        {AttributeTypes::Param,
         {
             {DFIPKeys::Active, "active"},
             {DFIPKeys::Premium, "premium"},
             {DFIPKeys::MinSwap, "minswap"},
             {DFIPKeys::RewardPct, "reward_pct"},
             {DFIPKeys::BlockPeriod, "block_period"},
             {DFIPKeys::DUSDInterestBurn, "dusd_interest_burn"},
             {DFIPKeys::DUSDLoanBurn, "dusd_loan_burn"},
             {DFIPKeys::StartBlock, "start_block"},
             {DFIPKeys::GovUnset, "gov-unset"},
             {DFIPKeys::GovFoundation, "gov-foundation"},
             {DFIPKeys::MNSetRewardAddress, "mn-setrewardaddress"},
             {DFIPKeys::MNSetOperatorAddress, "mn-setoperatoraddress"},
             {DFIPKeys::MNSetOwnerAddress, "mn-setowneraddress"},
             {DFIPKeys::GovernanceEnabled, "gov"},
             {DFIPKeys::EVMEnabled, "evm"},
             {DFIPKeys::ICXEnabled, "icx"},
             {DFIPKeys::Members, "members"},
             {DFIPKeys::CFPPayout, "gov-payout"},
             {DFIPKeys::EmissionUnusedFund, "emission-unused-fund"},
             {DFIPKeys::MintTokens, "mint-tokens-to-address"},
             {DFIPKeys::TransferDomain, "transferdomain"},
             {DFIPKeys::LiquidityCalcSamplingPeriod, "liquidity_calc_sampling_period"},
             {DFIPKeys::AverageLiquidityPercentage, "average_liquidity_percentage"},
         }},
        {AttributeTypes::EVMType,
         {
             {EVMKeys::Finalized, "finality_count"},
             {EVMKeys::GasLimit, "gas_limit"},
             {EVMKeys::GasTargetFactor, "gas_target_factor"},
             {EVMKeys::RbfIncrementMinPct, "rbf_increment_fee_pct"},
         }},
        {AttributeTypes::Live,
         {
             {EconomyKeys::PaybackDFITokens, "dfi_payback_tokens"},
             {EconomyKeys::PaybackDFITokensPrincipal, "dfi_payback_tokens_principal"},
             {EconomyKeys::DFIP2203Current, "dfip2203_current"},
             {EconomyKeys::DFIP2203Burned, "dfip2203_burned"},
             {EconomyKeys::DFIP2203Minted, "dfip2203_minted"},
             {EconomyKeys::DexTokens, "dex"},
             {EconomyKeys::DFIP2206FCurrent, "dfip2206f_current"},
             {EconomyKeys::DFIP2206FBurned, "dfip2206f_burned"},
             {EconomyKeys::DFIP2206FMinted, "dfip2206f_minted"},
             {EconomyKeys::NegativeInt, "negative_interest"},
             {EconomyKeys::NegativeIntCurrent, "negative_interest_current"},
             {EconomyKeys::BatchRoundingExcess, "batch_rounding_excess"},
             {EconomyKeys::ConsolidatedInterest, "consolidated_interest"},
             {EconomyKeys::Loans, "loans"},
             {EconomyKeys::TransferDomainStatsLive, "transferdomain"},
             {EconomyKeys::EVMBlockStatsLive, "evm"},
         }},
        {AttributeTypes::Governance,
         {
             {GovernanceKeys::FeeRedistribution, "fee_redistribution"},
             {GovernanceKeys::FeeBurnPct, "fee_burn_pct"},
             {GovernanceKeys::CFPFee, "cfp_fee"},
             {GovernanceKeys::CFPApprovalThreshold, "cfp_approval_threshold"},
             {GovernanceKeys::VOCFee, "voc_fee"},
             {GovernanceKeys::VOCEmergencyFee, "voc_emergency_fee"},
             {GovernanceKeys::VOCEmergencyPeriod, "voc_emergency_period"},
             {GovernanceKeys::VOCEmergencyQuorum, "voc_emergency_quorum"},
             {GovernanceKeys::VOCApprovalThreshold, "voc_approval_threshold"},
             {GovernanceKeys::Quorum, "quorum"},
             {GovernanceKeys::VotingPeriod, "voting_period"},
             {GovernanceKeys::CFPMaxCycles, "cfp_max_cycles"},
         }},
        {AttributeTypes::Transfer,
         {
             {TransferKeys::TransferEnabled, "enabled"},
             {TransferKeys::SrcFormats, "src-formats"},
             {TransferKeys::DestFormats, "dest-formats"},
             {TransferKeys::AuthFormats, "auth-formats"},
             {TransferKeys::NativeEnabled, "native-enabled"},
             {TransferKeys::DATEnabled, "dat-enabled"},
             {TransferKeys::Disallowed, "disallowed"},
         }},
        {AttributeTypes::Vaults,
         {
             {VaultKeys::CreationFee, "creation_fee"},
             {VaultKeys::DUSDVaultEnabled, "enabled"},
         }},
        {AttributeTypes::Rules,
         {
             {RulesKeys::CoreOPReturn, "core_op_return_max_size_bytes"},
             {RulesKeys::DVMOPReturn, "dvm_op_return_max_size_bytes"},
             {RulesKeys::EVMOPReturn, "evm_op_return_max_size_bytes"},
         }},
        {AttributeTypes::Oracles,
         {
             {OracleKeys::FractionalSplits, "fractional_enabled"},
         }},
    };
    return keys;
}

static ResVal<int32_t> VerifyInt32(const std::string &str) {
    int32_t int32;
    if (!ParseInt32(str, &int32)) {
        return DeFiErrors::GovVarVerifyInt();
    }
    return {int32, Res::Ok()};
}

static ResVal<int32_t> VerifyPositiveInt32(const std::string &str) {
    int32_t int32;
    if (!ParseInt32(str, &int32) || int32 < 0) {
        return DeFiErrors::GovVarVerifyPositiveNumber();
    }
    return {int32, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyUInt32(const std::string &str) {
    uint32_t uint32;
    if (!ParseUInt32(str, &uint32)) {
        return DeFiErrors::GovVarVerifyInt();
    }
    return {uint32, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyUInt64(const std::string &str) {
    uint64_t x;
    if (!ParseUInt64(str, &x)) {
        return DeFiErrors::GovVarVerifyInt();
    }
    return {x, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyMoreThenZeroUInt64(const std::string &str) {
    auto resVal = VerifyUInt64(str);
    if (!resVal) {
        return resVal;
    }
    const auto value = std::get<uint64_t>(*resVal.val);
    if (value == 0) {
        return DeFiErrors::GovVarVerifyFactor();
    }
    return resVal;
}

static ResVal<CAttributeValue> VerifyInt64(const std::string &str) {
    CAmount int64;
    if (!ParseInt64(str, &int64) || int64 < 0) {
        return DeFiErrors::GovVarVerifyPositiveNumber();
    }
    return {int64, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyMoreThenZeroInt64(const std::string &str) {
    CAmount int64;
    if (!ParseInt64(str, &int64) || int64 < 1) {
        return DeFiErrors::GovVarVerifyMoreThanZero();
    }
    return {int64, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyFloat(const std::string &str) {
    CAmount amount = 0;
    if (!ParseFixedPoint(str, 8, &amount)) {
        return DeFiErrors::GovVarInvalidNumber();
    }
    return {amount, Res::Ok()};
}

ResVal<CAttributeValue> VerifyPositiveFloat(const std::string &str) {
    CAmount amount = 0;
    if (!ParseFixedPoint(str, 8, &amount) || amount < 0) {
        return DeFiErrors::GovVarValidateNegativeAmount();
    }
    return {amount, Res::Ok()};
}

ResVal<CAttributeValue> VerifyPositiveOrMinusOneFloat(const std::string &str) {
    CAmount amount = 0;
    if (!ParseFixedPoint(str, 8, &amount) || !(amount >= 0 || amount == -1 * COIN)) {
        return Res::Err("Amount must be positive or -1");
    }

    return {amount, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyPctInt64(const std::string &str) {
    std::string val = str;
    bool isPct = (val.size() > 0 && val.back() == '%');
    if (isPct) {
        val.pop_back();
    }
    auto resVal = VerifyPositiveFloat(val);
    if (!resVal) {
        return resVal;
    }
    auto value = std::get<CAmount>(*resVal.val);
    if (isPct && value > 0) {
        (*resVal.val).emplace<CAmount>(value / 100);
    }
    if (std::get<CAmount>(*resVal.val) > COIN) {
        return Res::Err("Percentage exceeds 100%%");
    }
    return resVal;
}

static ResVal<CAttributeValue> VerifyBool(const std::string &str) {
    if (str != "true" && str != "false") {
        return Res::Err(R"(Boolean value must be either "true" or "false")");
    }
    return {str == "true", Res::Ok()};
}

static auto isFloat(const std::string &str) {
    std::stringstream ss(str);
    float floatValue;
    ss >> floatValue;

    if (!ss.fail() && ss.eof()) {
        std::stringstream ss2(str);
        int intValue;
        ss2 >> intValue;

        if (!ss2.fail() && ss2.eof()) {
            return floatValue != static_cast<float>(intValue);
        } else {
            return true;
        }
    }
    return false;
}

static ResVal<CAttributeValue> VerifySplit(const std::string &str) {
    const auto pairs = KeyBreaker(str);
    if (pairs.size() != 2) {
        return DeFiErrors::GovVarVerifySplitValues();
    }
    const auto resId = VerifyPositiveInt32(pairs[0]);
    if (!resId) {
        return resId;
    }

    if (isFloat(pairs[1])) {
        OracleSplits64 splits;
        auto resMultiplier = VerifyFloat(pairs[1]);
        if (!resMultiplier) {
            return resMultiplier;
        }
        auto value = std::get<CAmount>(*resMultiplier.val);
        if (value == 0) {
            return DeFiErrors::GovVarVerifyMultiplier();
        }
        splits[*resId] = value;
        return {splits, Res::Ok()};
    }

    OracleSplits splits;
    const auto resMultiplier = VerifyInt32(pairs[1]);
    if (!resMultiplier) {
        return resMultiplier;
    }
    if (*resMultiplier.val == 0) {
        return DeFiErrors::GovVarVerifyMultiplier();
    }
    splits[*resId] = *resMultiplier.val;
    return {splits, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyMember(const UniValue &array) {
    std::set<std::string> addresses;
    std::set<CScript> members;
    bool removal{};

    for (size_t i = 0; i < array.size(); ++i) {
        auto member = array[i].getValStr();
        if (member.empty()) {
            return Res::Err("Invalid address provided");
        }

        CTxDestination dest;
        if (member[0] == '-') {
            removal = true;
            dest = DecodeDestination(member.erase(0, 1));
            addresses.insert(array[i].getValStr());
        } else if (member[0] == '+') {
            dest = DecodeDestination(member.erase(0, 1));
            addresses.insert(member);
        } else {
            dest = DecodeDestination(member);
            addresses.insert(member);
        }

        if (!IsValidDestination(dest)) {
            return Res::Err("Invalid address provided");
        }

        members.insert(GetScriptForDestination(dest));
    }

    if (removal) {
        return {addresses, Res::Ok()};
    }

    return {members, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyCurrencyPair(const std::string &str) {
    const auto value = KeyBreaker(str);
    if (value.size() != 2) {
        return DeFiErrors::GovVarVerifyPair();
    }

    auto token = trim_all_ws(value[0]).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    auto currency = trim_all_ws(value[1]).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    if (token.empty() || currency.empty()) {
        return DeFiErrors::GovVarVerifyValues();
    }
    return {
        CTokenCurrencyPair{token, currency},
        Res::Ok()
    };
}

static std::set<std::string> dirSet{"both", "in", "out"};

static ResVal<CAttributeValue> VerifyFeeDirection(const std::string &str) {
    auto lowerStr = ToLower(str);
    const auto it = dirSet.find(lowerStr);
    if (it == dirSet.end()) {
        return DeFiErrors::GovVarVerifyFeeDirection();
    }
    return {CFeeDir{static_cast<uint8_t>(std::distance(dirSet.begin(), it))}, Res::Ok()};
}

static bool VerifyToken(const CCustomCSView &view, const uint32_t id) {
    return view.GetToken(DCT_ID{id}).has_value();
}

static bool VerifyDATToken(const CCustomCSView &view, const std::string &str) {
    const auto tokenPair = view.GetToken(str);
    if (tokenPair && tokenPair->second && tokenPair->second->IsDAT()) {
        return true;
    }
    return false;
}

static bool VerifyDATToken(const CCustomCSView &view, const uint32_t id) {
    const auto token = view.GetToken(DCT_ID{id});
    if (token && token->IsDAT()) {
        return true;
    }
    return false;
}

static Res VerifyTokenSet(const CCustomCSView &view, const std::set<std::string> &strSet) {
    for (const auto &str : strSet) {
        if (!VerifyDATToken(view, str)) {
            return Res::Err("Token not found or not a DAT token");
        }
    }
    return Res::Ok();
}

static ResVal<CAttributeValue> VerifyXVMAddressTypes(const UniValue &array) {
    XVmAddressFormatItems addressSet;
    for (const auto &value : array.getValues()) {
        if (value.getValStr() == "bech32") {
            addressSet.insert(XVmAddressFormatTypes::Bech32);
        } else if (value.getValStr() == "p2pkh") {
            addressSet.insert(XVmAddressFormatTypes::PkHash);
        } else if (value.getValStr() == "erc55") {
            addressSet.insert(XVmAddressFormatTypes::Erc55);
        } else {
            return Res::Err("Unrecognised address format, expected types are: bech32, erc55, p2pkh");
        }
    }
    if (addressSet.empty()) {
        return Res::Err("No values set");
    }
    return {addressSet, Res::Ok()};
}

static ResVal<CAttributeValue> VerifyEVMAuthTypes(const UniValue &array) {
    XVmAddressFormatItems addressSet;
    for (const auto &value : array.getValues()) {
        if (value.getValStr() == "bech32-erc55") {
            addressSet.insert(XVmAddressFormatTypes::Bech32ProxyErc55);
        } else if (value.getValStr() == "p2pkh-erc55") {
            addressSet.insert(XVmAddressFormatTypes::PkHashProxyErc55);
        } else {
            return Res::Err("Unrecognised address format, expected types are: bech32-erc55, p2pkh-erc55");
        }
    }
    if (addressSet.empty()) {
        return Res::Err("No values set");
    }
    return {addressSet, Res::Ok()};
}

static inline std::string GetDecimalStringNormalized(const CAmount amount) {
    auto decimalStr = GetDecimalString(amount);
    rtrim(decimalStr, '0');
    if (decimalStr.back() == '.') {
        decimalStr.pop_back();
    }
    return decimalStr;
}

const std::map<uint8_t, std::map<uint8_t, std::function<ResVal<CAttributeValue>(const std::string &)>>>
    &ATTRIBUTES::parseValue() {
    static const std::map<uint8_t, std::map<uint8_t, std::function<ResVal<CAttributeValue>(const std::string &)>>>
        parsers{
            {AttributeTypes::Token,
             {
                 {TokenKeys::PaybackDFI, VerifyBool},
                 {TokenKeys::PaybackDFIFeePCT, VerifyPctInt64},
                 {TokenKeys::LoanPayback, VerifyBool},
                 {TokenKeys::LoanPaybackFeePCT, VerifyPctInt64},
                 {TokenKeys::LoanPaybackCollateral, VerifyBool},
                 {TokenKeys::DexInFeePct, VerifyPctInt64},
                 {TokenKeys::DexOutFeePct, VerifyPctInt64},
                 {TokenKeys::FixedIntervalPriceId, VerifyCurrencyPair},
                 {TokenKeys::LoanCollateralEnabled, VerifyBool},
                 {TokenKeys::LoanCollateralFactor, VerifyPositiveFloat},
                 {TokenKeys::LoanMintingEnabled, VerifyBool},
                 {TokenKeys::LoanMintingInterest, VerifyFloat},
                 {TokenKeys::DFIP2203Enabled, VerifyBool},
             }},
            {AttributeTypes::Poolpairs,
             {
                 {PoolKeys::TokenAFeePCT, VerifyPctInt64},
                 {PoolKeys::TokenAFeeDir, VerifyFeeDirection},
                 {PoolKeys::TokenBFeePCT, VerifyPctInt64},
                 {PoolKeys::TokenBFeeDir, VerifyFeeDirection},
             }},
            {AttributeTypes::Param,
             {
                 {DFIPKeys::Active, VerifyBool},
                 {DFIPKeys::Premium, VerifyPctInt64},
                 {DFIPKeys::MinSwap, VerifyPositiveFloat},
                 {DFIPKeys::RewardPct, VerifyPctInt64},
                 {DFIPKeys::BlockPeriod, VerifyMoreThenZeroInt64},
                 {DFIPKeys::DUSDInterestBurn, VerifyBool},
                 {DFIPKeys::DUSDLoanBurn, VerifyBool},
                 {DFIPKeys::StartBlock, VerifyInt64},
                 {DFIPKeys::GovUnset, VerifyBool},
                 {DFIPKeys::GovFoundation, VerifyBool},
                 {DFIPKeys::MNSetRewardAddress, VerifyBool},
                 {DFIPKeys::MNSetOperatorAddress, VerifyBool},
                 {DFIPKeys::MNSetOwnerAddress, VerifyBool},
                 {DFIPKeys::GovernanceEnabled, VerifyBool},
                 {DFIPKeys::EVMEnabled, VerifyBool},
                 {DFIPKeys::ICXEnabled, VerifyBool},
                 {DFIPKeys::CFPPayout, VerifyBool},
                 {DFIPKeys::EmissionUnusedFund, VerifyBool},
                 {DFIPKeys::MintTokens, VerifyBool},
                 {DFIPKeys::TransferDomain, VerifyBool},
                 {DFIPKeys::LiquidityCalcSamplingPeriod, VerifyMoreThenZeroInt64},
                 {DFIPKeys::AverageLiquidityPercentage, VerifyPctInt64},
             }},
            {AttributeTypes::Locks,
             {
                 {ParamIDs::TokenID, VerifyBool},
             }},
            {AttributeTypes::Oracles,
             {
                 {OracleKeys::FractionalSplits, VerifyBool},
             }},
            {AttributeTypes::EVMType,
             {
                 {EVMKeys::Finalized, VerifyUInt64},
                 {EVMKeys::GasLimit, VerifyUInt64},
                 {EVMKeys::GasTargetFactor, VerifyMoreThenZeroUInt64},
                 {EVMKeys::RbfIncrementMinPct, VerifyPctInt64},
             }},
            {AttributeTypes::Governance,
             {
                 {GovernanceKeys::FeeRedistribution, VerifyBool},
                 {GovernanceKeys::FeeBurnPct, VerifyPctInt64},
                 {GovernanceKeys::CFPFee, VerifyPctInt64},
                 {GovernanceKeys::CFPApprovalThreshold, VerifyPctInt64},
                 {GovernanceKeys::VOCFee, VerifyPositiveFloat},
                 {GovernanceKeys::VOCEmergencyFee, VerifyPositiveFloat},
                 {GovernanceKeys::VOCEmergencyPeriod, VerifyUInt32},
                 {GovernanceKeys::VOCEmergencyQuorum, VerifyPctInt64},
                 {GovernanceKeys::VOCApprovalThreshold, VerifyPctInt64},
                 {GovernanceKeys::Quorum, VerifyPctInt64},
                 {GovernanceKeys::VotingPeriod, VerifyUInt32},
                 {GovernanceKeys::CFPMaxCycles, VerifyUInt32},
             }},
            {AttributeTypes::Transfer,
             {
                 {TransferKeys::TransferEnabled, VerifyBool},
                 {TransferKeys::NativeEnabled, VerifyBool},
                 {TransferKeys::DATEnabled, VerifyBool},
             }},
            {AttributeTypes::Vaults,
             {
                 {VaultKeys::CreationFee, VerifyPositiveFloat},
                 {VaultKeys::DUSDVaultEnabled, VerifyBool},
             }},
            {AttributeTypes::Rules,
             {
                 {RulesKeys::CoreOPReturn, VerifyUInt64},
                 {RulesKeys::DVMOPReturn, VerifyUInt64},
                 {RulesKeys::EVMOPReturn, VerifyUInt64},
             }},
    };
    return parsers;
}

ResVal<CScript> GetFutureSwapContractAddress(const std::string &contract) {
    CScript contractAddress;
    try {
        contractAddress = Params().GetConsensus().smartContracts.at(contract);
    } catch (const std::out_of_range &) {
        return Res::Err("Failed to get smart contract address from chainparams");
    }
    return {contractAddress, Res::Ok()};
}

static void TrackLiveBalance(CCustomCSView &mnview,
                             const CTokenAmount &amount,
                             const EconomyKeys dataKey,
                             const bool add) {
    auto attributes = mnview.GetAttributes();

    CDataStructureV0 key{AttributeTypes::Live, ParamIDs::Economy, dataKey};
    auto balances = attributes->GetValue(key, CBalances{});
    Res res{};
    if (add) {
        res = balances.Add(amount);
    } else {
        res = balances.Sub(amount);
    }
    if (res) {
        attributes->SetValue(key, balances);
        mnview.SetVariable(*attributes);
    }
}

void TrackNegativeInterest(CCustomCSView &mnview, const CTokenAmount &amount) {
    if (!gArgs.GetBoolArg("-negativeinterest", DEFAULT_NEGATIVE_INTEREST)) {
        return;
    }
    TrackLiveBalance(mnview, amount, EconomyKeys::NegativeInt, true);
}

void TrackDUSDAdd(CCustomCSView &mnview, const CTokenAmount &amount) {
    TrackLiveBalance(mnview, amount, EconomyKeys::Loans, true);
}

void TrackDUSDSub(CCustomCSView &mnview, const CTokenAmount &amount) {
    TrackLiveBalance(mnview, amount, EconomyKeys::Loans, false);
}

void TrackLiveBalances(CCustomCSView &mnview, const CBalances &balances, const uint8_t key) {
    auto attributes = mnview.GetAttributes();

    const CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Auction, key};
    auto storedBalances = attributes->GetValue(liveKey, CBalances{});
    for (const auto &[tokenID, amount] : balances.balances) {
        storedBalances.balances[tokenID] += amount;
    }
    attributes->SetValue(liveKey, storedBalances);
    mnview.SetVariable(*attributes);
}

bool IsEVMEnabled(const std::shared_ptr<ATTRIBUTES> attributes) {
    if (!attributes) {
        return false;
    }

    const CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::EVMEnabled};
    return attributes->GetValue(enabledKey, false);
}

bool IsEVMEnabled(const CCustomCSView &view) {
    auto attributes = view.GetAttributes();

    return IsEVMEnabled(attributes);
}

Res StoreGovVars(const CGovernanceHeightMessage &obj, CCustomCSView &view) {
    // Retrieve any stored GovVariables at startHeight
    auto storedGovVars = view.GetStoredVariables(obj.startHeight);

    // Remove any pre-existing entry
    for (auto it = storedGovVars.begin(); it != storedGovVars.end();) {
        if ((*it)->GetName() == obj.govVar->GetName()) {
            it = storedGovVars.erase(it);
        } else {
            ++it;
        }
    }

    // Add GovVariable to set for storage
    storedGovVars.insert(obj.govVar);

    // Store GovVariable set by height
    return view.SetStoredVariables(storedGovVars, obj.startHeight);
}

static Res CheckValidAttrV0Key(const uint8_t type, const uint32_t typeId, const uint32_t typeKey) {
    if (type == AttributeTypes::Param) {
        if (typeId == ParamIDs::DFIP2201) {
            if (typeKey != DFIPKeys::Active && typeKey != DFIPKeys::Premium && typeKey != DFIPKeys::MinSwap) {
                return Res::Err("Unsupported type for DFIP2201 {%d}", typeKey);
            }
        } else if (typeId == ParamIDs::DFIP2203 || typeId == ParamIDs::DFIP2206F) {
            if (typeKey != DFIPKeys::Active && typeKey != DFIPKeys::RewardPct && typeKey != DFIPKeys::BlockPeriod &&
                typeKey != DFIPKeys::StartBlock) {
                return Res::Err("Unsupported type for this DFIP {%d}", typeKey);
            }
        } else if (typeId == ParamIDs::DFIP2206A) {
            if (typeKey != DFIPKeys::DUSDInterestBurn && typeKey != DFIPKeys::DUSDLoanBurn) {
                return DeFiErrors::GovVarVariableUnsupportedDFIPType(typeKey);
            }
        } else if (typeId == ParamIDs::DFIP2211F) {
            if (typeKey != DFIPKeys::Active && typeKey != DFIPKeys::BlockPeriod &&
                typeKey != DFIPKeys::LiquidityCalcSamplingPeriod && typeKey != DFIPKeys::AverageLiquidityPercentage) {
                return DeFiErrors::GovVarVariableUnsupportedDFIPType(typeKey);
            }
        } else if (typeId == ParamIDs::Feature) {
            if (typeKey != DFIPKeys::GovUnset && typeKey != DFIPKeys::GovFoundation &&
                typeKey != DFIPKeys::MNSetRewardAddress && typeKey != DFIPKeys::MNSetOperatorAddress &&
                typeKey != DFIPKeys::MNSetOwnerAddress && typeKey != DFIPKeys::GovernanceEnabled &&
                typeKey != DFIPKeys::CFPPayout && typeKey != DFIPKeys::EmissionUnusedFund &&
                typeKey != DFIPKeys::MintTokens && typeKey != DFIPKeys::EVMEnabled && typeKey != DFIPKeys::ICXEnabled &&
                typeKey != DFIPKeys::TransferDomain) {
                return DeFiErrors::GovVarVariableUnsupportedFeatureType(typeKey);
            }
        } else if (typeId == ParamIDs::Foundation) {
            if (typeKey != DFIPKeys::Members) {
                return DeFiErrors::GovVarVariableUnsupportedFoundationType(typeKey);
            }
        } else {
            return DeFiErrors::GovVarVariableUnsupportedParamType();
        }
    } else if (type == AttributeTypes::EVMType) {
        if (typeId == EVMIDs::Block) {
            if (typeKey != EVMKeys::Finalized && typeKey != EVMKeys::GasLimit && typeKey != EVMKeys::GasTargetFactor &&
                typeKey != EVMKeys::RbfIncrementMinPct) {
                return DeFiErrors::GovVarVariableUnsupportedEVMType(typeKey);
            }
        } else {
            return DeFiErrors::GovVarVariableUnsupportedGovType();
        }
    } else if (type == AttributeTypes::Governance) {
        if (typeId == GovernanceIDs::Proposals) {
            if (typeKey != GovernanceKeys::FeeRedistribution && typeKey != GovernanceKeys::FeeBurnPct &&
                typeKey != GovernanceKeys::CFPFee && typeKey != GovernanceKeys::CFPApprovalThreshold &&
                typeKey != GovernanceKeys::VOCFee && typeKey != GovernanceKeys::VOCApprovalThreshold &&
                typeKey != GovernanceKeys::VOCEmergencyPeriod && typeKey != GovernanceKeys::VOCEmergencyFee &&
                typeKey != GovernanceKeys::VOCEmergencyQuorum && typeKey != GovernanceKeys::Quorum &&
                typeKey != GovernanceKeys::VotingPeriod && typeKey != GovernanceKeys::CFPMaxCycles) {
                return DeFiErrors::GovVarVariableUnsupportedProposalType(typeKey);
            }
        } else {
            return DeFiErrors::GovVarVariableUnsupportedGovType();
        }
    } else if (type == AttributeTypes::Transfer) {
        if (typeId == TransferIDs::DVMToEVM) {
            if (typeKey != TransferKeys::TransferEnabled && typeKey != TransferKeys::SrcFormats &&
                typeKey != TransferKeys::DestFormats && typeKey != TransferKeys::NativeEnabled &&
                typeKey != TransferKeys::DATEnabled && typeKey != TransferKeys::Disallowed) {
                return DeFiErrors::GovVarVariableUnsupportedTransferType(typeKey);
            }
        } else if (typeId == TransferIDs::EVMToDVM) {
            if (typeKey != TransferKeys::TransferEnabled && typeKey != TransferKeys::SrcFormats &&
                typeKey != TransferKeys::DestFormats && typeKey != TransferKeys::AuthFormats &&
                typeKey != TransferKeys::NativeEnabled && typeKey != TransferKeys::DATEnabled &&
                typeKey != TransferKeys::Disallowed) {
                return DeFiErrors::GovVarVariableUnsupportedTransferType(typeKey);
            }
        } else {
            return DeFiErrors::GovVarVariableUnsupportedGovType();
        }
    } else if (type == AttributeTypes::Vaults) {
        if (typeId == VaultIDs::DUSDVault) {
            if (typeKey != VaultKeys::DUSDVaultEnabled) {
                return DeFiErrors::GovVarVariableUnsupportedVaultsType(typeKey);
            }
        } else if (typeId == VaultIDs::Parameters) {
            if (typeKey != VaultKeys::CreationFee) {
                return DeFiErrors::GovVarVariableUnsupportedVaultsType(typeKey);
            }
        } else {
            return DeFiErrors::GovVarVariableUnsupportedGovType();
        }
    } else if (type == AttributeTypes::Rules) {
        if (typeId == RulesIDs::TXRules) {
            if (typeKey != RulesKeys::CoreOPReturn && typeKey != RulesKeys::DVMOPReturn &&
                typeKey != RulesKeys::EVMOPReturn) {
                return DeFiErrors::GovVarVariableUnsupportedRulesType(typeKey);
            }
        } else {
            return DeFiErrors::GovVarVariableUnsupportedGovType();
        }
    }

    return Res::Ok();
}

Res ATTRIBUTES::ProcessVariable(const std::string &key,
                                const std::optional<UniValue> &value,
                                std::function<Res(const CAttributeType &, const CAttributeValue &)> applyVariable) {
    if (key.size() > 128) {
        return DeFiErrors::GovVarVariableLength();
    }

    const auto keys = KeyBreaker(key);
    if (keys.empty() || keys[0].empty()) {
        return DeFiErrors::GovVarVariableNoVersion();
    }

    auto iver = allowedVersions().find(keys[0]);
    if (iver == allowedVersions().end()) {
        return DeFiErrors::GovVarUnsupportedVersion();
    }

    auto version = iver->second;
    if (version != VersionTypes::v0) {
        return DeFiErrors::GovVarUnsupportedVersion();
    }

    if (keys.size() < 4 || keys[1].empty() || keys[2].empty() || keys[3].empty()) {
        return DeFiErrors::GovVarVariableNumberOfKey();
    }

    auto itype = allowedTypes().find(keys[1]);
    if (itype == allowedTypes().end()) {
        return DeFiErrors::GovVarVariableInvalidKey("type", allowedTypes());
    }

    const auto type = itype->second;

    uint32_t typeId{0};
    if (type == AttributeTypes::Param) {
        auto id = allowedParamIDs().find(keys[2]);
        if (id == allowedParamIDs().end()) {
            return DeFiErrors::GovVarVariableInvalidKey("param", allowedParamIDs());
        }
        typeId = id->second;
    } else if (type == AttributeTypes::Locks) {
        auto id = allowedLocksIDs().find(keys[2]);
        if (id == allowedLocksIDs().end()) {
            return DeFiErrors::GovVarVariableInvalidKey("locks", allowedLocksIDs());
        }
        typeId = id->second;
    } else if (type == AttributeTypes::EVMType) {
        auto id = allowedEVMIDs().find(keys[2]);
        if (id == allowedEVMIDs().end()) {
            return DeFiErrors::GovVarVariableInvalidKey("evm", allowedEVMIDs());
        }
        typeId = id->second;
    } else if (type == AttributeTypes::Oracles) {
        auto id = allowedOracleIDs().find(keys[2]);
        if (id == allowedOracleIDs().end()) {
            return DeFiErrors::GovVarVariableInvalidKey("oracles", allowedOracleIDs());
        }
        typeId = id->second;
    } else if (type == AttributeTypes::Governance) {
        auto id = allowedGovernanceIDs().find(keys[2]);
        if (id == allowedGovernanceIDs().end()) {
            return DeFiErrors::GovVarVariableInvalidKey("governance", allowedGovernanceIDs());
        }
        typeId = id->second;
    } else if (type == AttributeTypes::Transfer) {
        auto id = allowedTransferIDs().find(keys[2]);
        if (id == allowedTransferIDs().end()) {
            return DeFiErrors::GovVarVariableInvalidKey("transferdomain", allowedTransferIDs());
        }
        typeId = id->second;
    } else if (type == AttributeTypes::Vaults) {
        auto id = allowedVaultIDs().find(keys[2]);
        if (id == allowedVaultIDs().end()) {
            return DeFiErrors::GovVarVariableInvalidKey("vaults", allowedVaultIDs());
        }
        typeId = id->second;
    } else if (type == AttributeTypes::Rules) {
        auto id = allowedRulesIDs().find(keys[2]);
        if (id == allowedRulesIDs().end()) {
            return DeFiErrors::GovVarVariableInvalidKey("rules", allowedRulesIDs());
        }
        typeId = id->second;
    } else {
        auto id = VerifyInt32(keys[2]);
        if (!id) {
            return id;
        }
        typeId = *id.val;
    }

    uint32_t typeKey{0};
    CDataStructureV0 attrV0{};

    if (type == AttributeTypes::Locks) {
        typeKey = ParamIDs::TokenID;
        if (const auto keyValue = VerifyInt32(keys[3])) {
            attrV0 = CDataStructureV0{type, typeId, static_cast<uint32_t>(*keyValue)};
        } else {
            return DeFiErrors::GovVarTokenAsString();
        }
    } else if (type == AttributeTypes::Oracles) {
        if (const auto keyValue = VerifyPositiveInt32(keys[3])) {
            attrV0 = CDataStructureV0{type, typeId, static_cast<uint32_t>(*keyValue)};
        } else {
            auto ikey = allowedKeys().find(type);
            if (ikey == allowedKeys().end()) {
                return DeFiErrors::GovVarVariableUnsupportedType(type);
            }

            itype = ikey->second.find(keys[3]);
            if (itype == ikey->second.end()) {
                return DeFiErrors::GovVarOracleInvalidKey(ikey->second);
            }

            typeKey = itype->second;

            attrV0 = CDataStructureV0{type, typeId, typeKey};
        }
    } else {
        auto ikey = allowedKeys().find(type);
        if (ikey == allowedKeys().end()) {
            return DeFiErrors::GovVarVariableUnsupportedType(type);
        }

        // Alias of reward_pct in Export.
        if (keys[3] == "fee_pct") {
            return Res::Ok();
        }

        itype = ikey->second.find(keys[3]);
        if (itype == ikey->second.end()) {
            return DeFiErrors::GovVarVariableInvalidKey("key", ikey->second);
        }

        typeKey = itype->second;

        auto res = CheckValidAttrV0Key(type, typeId, typeKey);
        if (!res) {
            return res;
        }

        if (type == AttributeTypes::Param && (typeId == ParamIDs::DFIP2203 || typeId == ParamIDs::DFIP2206F)) {
            if (typeKey == DFIPKeys::BlockPeriod || typeKey == DFIPKeys::StartBlock) {
                if (typeId == ParamIDs::DFIP2203) {
                    futureUpdated = true;
                } else {
                    futureDUSDUpdated = true;
                }
            }
        }

        attrV0 = CDataStructureV0{type, typeId, typeKey};
    }

    if (attrV0.IsExtendedSize()) {
        if (keys.size() != 5 || keys[4].empty()) {
            return DeFiErrors::GovVarVariableKeyCount(5, keys);
        }
        auto id = VerifyInt32(keys[4]);
        if (!id) {
            return id;
        }
        attrV0.keyId = *id.val;
    } else {
        if (keys.size() != 4) {
            return DeFiErrors::GovVarVariableKeyCount(4, keys);
        }
    }

    if (!value) {
        return applyVariable(attrV0, {});
    }

    // Tidy into new parseValue map for UniValue
    if (attrV0.type == AttributeTypes::Param && attrV0.typeId == ParamIDs::Foundation &&
        attrV0.key == DFIPKeys::Members) {
        if (value && !value->isArray() && value->get_array().empty()) {
            return Res::Err("Empty value");
        }

        auto attribValue = VerifyMember(*value);
        if (!attribValue) {
            return std::move(attribValue);
        }
        return applyVariable(attrV0, *attribValue.val);
    } else if (attrV0.type == AttributeTypes::Transfer && attrV0.typeId == TransferIDs::EVMToDVM &&
               (attrV0.key == TransferKeys::DestFormats || attrV0.key == TransferKeys::SrcFormats ||
                attrV0.key == TransferKeys::AuthFormats)) {
        if (value && !value->isArray() && value->get_array().empty()) {
            return Res::Err("Empty value");
        }
        ResVal<CAttributeValue> attribValue(XVmAddressFormatItems{}, Res::Ok());
        if (attrV0.key == TransferKeys::DestFormats || attrV0.key == TransferKeys::SrcFormats) {
            attribValue = VerifyXVMAddressTypes(*value);
        } else if (attrV0.key == TransferKeys::AuthFormats) {
            attribValue = VerifyEVMAuthTypes(*value);
        }
        if (!attribValue) {
            return std::move(attribValue);
        }
        return applyVariable(attrV0, *attribValue.val);
    } else if (attrV0.type == AttributeTypes::Transfer && attrV0.typeId == TransferIDs::DVMToEVM &&
               (attrV0.key == TransferKeys::DestFormats || attrV0.key == TransferKeys::SrcFormats)) {
        if (value && !value->isArray() && value->get_array().empty()) {
            return Res::Err("Empty value");
        }
        auto attribValue = VerifyXVMAddressTypes(*value);
        if (!attribValue) {
            return std::move(attribValue);
        }
        return applyVariable(attrV0, *attribValue.val);
    } else if (attrV0.type == AttributeTypes::Transfer && attrV0.typeId == TransferIDs::DVMToEVM &&
               (attrV0.typeId == TransferIDs::DVMToEVM || attrV0.typeId == TransferIDs::EVMToDVM) &&
               attrV0.key == TransferKeys::Disallowed) {
        if (value && !value->isArray() && value->get_array().empty()) {
            return Res::Err("Empty value");
        }
        std::set<std::string> attribValue;
        for (const auto &arrayValue : value->get_array().getValues()) {
            if (!arrayValue.isStr()) {
                return Res::Err("Values in array must be strings");
            }
            attribValue.insert(arrayValue.getValStr());
        }
        return applyVariable(attrV0, attribValue);
    } else if (attrV0.type == AttributeTypes::Oracles && attrV0.typeId == OracleIDs::Splits &&
               attrV0.key != OracleKeys::FractionalSplits) {
        auto attribValue = VerifySplit(value->getValStr());
        if (!attribValue) {
            return std::move(attribValue);
        }
        return applyVariable(attrV0, *attribValue);
    } else {
        if (value && !value->isStr() && value->getValStr().empty()) {
            return Res::Err("Empty value");
        }

        try {
            if (auto parser = parseValue().at(type).at(typeKey)) {
                auto attribValue = parser(value->getValStr());
                if (!attribValue) {
                    return std::move(attribValue);
                }
                return applyVariable(attrV0, *attribValue.val);
            }
        } catch (const std::out_of_range &) {
        }
    }

    return Res::Err("No parse function {%d, %d}", type, typeKey);
}

bool ATTRIBUTES::IsEmpty() const {
    return attributes.empty();
}

Res ATTRIBUTES::RefundFuturesContracts(CCustomCSView &mnview, const uint32_t height, const uint32_t tokenID) {
    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::BlockPeriod};
    const auto blockPeriod = GetValue(blockKey, CAmount{});
    if (blockPeriod == 0) {
        return Res::Ok();
    }

    std::map<CFuturesUserKey, CFuturesUserValue> userFuturesValues;

    mnview.ForEachFuturesUserValues(
        [&](const CFuturesUserKey &key, const CFuturesUserValue &futuresValues) {
            if (tokenID != std::numeric_limits<uint32_t>::max()) {
                if (futuresValues.source.nTokenId.v == tokenID || futuresValues.destination == tokenID) {
                    userFuturesValues[key] = futuresValues;
                }
            } else {
                userFuturesValues[key] = futuresValues;
            }

            return true;
        },
        {height, {}, std::numeric_limits<uint32_t>::max()});

    const auto contractAddressValue = GetFutureSwapContractAddress(SMART_CONTRACT_DFIP_2203);
    if (!contractAddressValue) {
        return contractAddressValue;
    }

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Current};
    auto balances = GetValue(liveKey, CBalances{});

    const auto currentHeight = mnview.GetLastHeight() + 1;

    for (const auto &[key, value] : userFuturesValues) {
        mnview.EraseFuturesUserValues(key);
        CAccountsHistoryWriter subView(
            mnview, currentHeight, GetNextAccPosition(), {}, uint8_t(CustomTxType::FutureSwapRefund));

        if (auto res = subView.SubBalance(*contractAddressValue, value.source); !res) {
            return res;
        }
        subView.Flush();

        CAccountsHistoryWriter addView(
            mnview, currentHeight, GetNextAccPosition(), {}, uint8_t(CustomTxType::FutureSwapRefund));

        if (auto res = addView.AddBalance(key.owner, value.source); !res) {
            return res;
        }
        addView.Flush();

        if (auto res = balances.Sub(value.source); !res) {
            return res;
        }
    }

    SetValue(liveKey, std::move(balances));

    return Res::Ok();
}

Res ATTRIBUTES::RefundFuturesDUSD(CCustomCSView &mnview, const uint32_t height) {
    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2206F, DFIPKeys::BlockPeriod};
    const auto blockPeriod = GetValue(blockKey, CAmount{});
    if (blockPeriod == 0) {
        return Res::Ok();
    }

    std::map<CFuturesUserKey, CAmount> userFuturesValues;

    mnview.ForEachFuturesDUSD(
        [&](const CFuturesUserKey &key, const CAmount &amount) {
            userFuturesValues[key] = amount;
            return true;
        },
        {height, {}, std::numeric_limits<uint32_t>::max()});

    const auto contractAddressValue = GetFutureSwapContractAddress(SMART_CONTRACT_DFIP2206F);
    if (!contractAddressValue) {
        return contractAddressValue;
    }

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2206FCurrent};
    auto balances = GetValue(liveKey, CBalances{});

    for (const auto &[key, amount] : userFuturesValues) {
        mnview.EraseFuturesDUSD(key);

        CAccountsHistoryWriter subView(
            mnview, height, GetNextAccPosition(), {}, uint8_t(CustomTxType::FutureSwapRefund));
        auto res = subView.SubBalance(*contractAddressValue, {DCT_ID{}, amount});
        if (!res) {
            return res;
        }
        subView.Flush();

        CAccountsHistoryWriter addView(
            mnview, height, GetNextAccPosition(), {}, uint8_t(CustomTxType::FutureSwapRefund));
        res = addView.AddBalance(key.owner, {DCT_ID{}, amount});
        if (!res) {
            return res;
        }
        addView.Flush();

        res = balances.Sub({DCT_ID{}, amount});
        if (!res) {
            return res;
        }
    }

    SetValue(liveKey, std::move(balances));

    return Res::Ok();
}

template <typename T>
static Res SetOracleSplit(ATTRIBUTES &attributes, const CAttributeType &attribute, const T &splitValue) {
    if (splitValue->size() != 1) {
        return Res::Err("Invalid number of token splits, allowed only one per height!");
    }

    const auto &[id, multiplier] = *(splitValue->begin());
    attributes.AddTokenSplit(id);
    attributes.SetValue(attribute, *splitValue);

    return Res::Ok();
}

Res ATTRIBUTES::Import(const UniValue &val) {
    if (!val.isObject()) {
        return DeFiErrors::GovVarImportObjectExpected();
    }

    std::map<std::string, UniValue> objMap;
    val.getObjMap(objMap);

    for (const auto &[key, value] : objMap) {
        auto res = ProcessVariable(key, value, [this](const CAttributeType &attribute, const CAttributeValue &value) {
            if (const auto attrV0 = std::get_if<CDataStructureV0>(&attribute)) {
                if (attrV0->type == AttributeTypes::Live ||
                    (attrV0->type == AttributeTypes::Token &&
                     (attrV0->key == TokenKeys::Ascendant || attrV0->key == TokenKeys::Descendant ||
                      attrV0->key == TokenKeys::Epitaph))) {
                    return Res::Err("Attribute cannot be set externally");
                } else if (attrV0->type == AttributeTypes::Oracles && attrV0->typeId == OracleIDs::Splits &&
                           attrV0->key != OracleKeys::FractionalSplits) {
                    const auto splitValue = std::get_if<OracleSplits>(&value);
                    if (!splitValue) {
                        const auto splitValue64 = std::get_if<OracleSplits64>(&value);
                        if (!splitValue64) {
                            return Res::Err("Failed to get Oracle split value");
                        }
                        return SetOracleSplit(*this, attribute, splitValue64);
                    }
                    return SetOracleSplit(*this, attribute, splitValue);
                } else if (attrV0->type == AttributeTypes::Param && attrV0->typeId == ParamIDs::Foundation &&
                           attrV0->key == DFIPKeys::Members) {
                    const auto members = std::get_if<std::set<CScript>>(&value);
                    if (members) {
                        auto existingMembers = GetValue(attribute, std::set<CScript>{});

                        for (const auto &member : *members) {
                            if (existingMembers.count(member)) {
                                return Res::Err("Member to add already present");
                            }
                            existingMembers.insert(member);
                        }

                        SetValue(attribute, existingMembers);
                    } else {
                        SetValue(attribute, value);
                    }

                    return Res::Ok();
                } else if (attrV0->type == AttributeTypes::Token && attrV0->key == TokenKeys::LoanMintingInterest) {
                    interestTokens.insert(attrV0->typeId);
                }

                // apply DFI via old keys
                if (attrV0->IsExtendedSize() && attrV0->keyId == 0) {
                    auto newAttr = *attrV0;
                    if (attrV0->key == TokenKeys::LoanPayback) {
                        newAttr.key = TokenKeys::PaybackDFI;
                    } else {
                        newAttr.key = TokenKeys::PaybackDFIFeePCT;
                    }
                    SetValue(newAttr, value);
                    return Res::Ok();
                }
            }
            SetValue(attribute, value);
            return Res::Ok();
        });
        if (!res) {
            return res;
        };
    }
    return Res::Ok();
}

// Keys to exclude when using the legacy filter mode, to keep things the
// same as pre 2.7.x versions, to reduce noise. Eventually, the APIs that
// cause too much noise can be deprecated and this code removed.
std::set<uint32_t> attrsVersion27TokenHiddenSet = {
    TokenKeys::LoanCollateralEnabled,
    TokenKeys::LoanCollateralFactor,
    TokenKeys::LoanMintingEnabled,
    TokenKeys::LoanMintingInterest,
    TokenKeys::FixedIntervalPriceId,
    TokenKeys::Ascendant,
    TokenKeys::Descendant,
    TokenKeys::Epitaph,
};

Res ATTRIBUTES::CheckKeys() const {
    for (const auto &attribute : attributes) {
        const auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            return DeFiErrors::GovVarUnsupportedVersion();
        }

        // Check type
        if (!displayTypes().count(attrV0->type)) {
            return DeFiErrors::GovVarVariableInvalidKey("type", displayTypes());
        }

        // Check typeId
        if (attrV0->type == AttributeTypes::Param) {
            if (!allowedExportParamsIDs().count(attrV0->typeId)) {
                return DeFiErrors::GovVarVariableInvalidKey("param", allowedExportParamsIDs());
            }
        } else if (attrV0->type == AttributeTypes::Locks) {
            if (!displayLocksIDs().count(attrV0->typeId)) {
                return DeFiErrors::GovVarVariableInvalidKey("locks", displayLocksIDs());
            }
        } else if (attrV0->type == AttributeTypes::EVMType) {
            if (!displayEVMIDs().count(attrV0->typeId)) {
                return DeFiErrors::GovVarVariableInvalidKey("evm", displayEVMIDs());
            }
        } else if (attrV0->type == AttributeTypes::Oracles) {
            if (!displayOracleIDs().count(attrV0->typeId)) {
                return DeFiErrors::GovVarVariableInvalidKey("oracles", displayOracleIDs());
            }
        } else if (attrV0->type == AttributeTypes::Governance) {
            if (!displayGovernanceIDs().count(attrV0->typeId)) {
                return DeFiErrors::GovVarVariableInvalidKey("governance", displayGovernanceIDs());
            }
        } else if (attrV0->type == AttributeTypes::Transfer) {
            if (!displayTransferIDs().count(attrV0->typeId)) {
                return DeFiErrors::GovVarVariableInvalidKey("transferdomain", displayTransferIDs());
            }
        } else if (attrV0->type == AttributeTypes::Vaults) {
            if (!displayVaultIDs().count(attrV0->typeId)) {
                return DeFiErrors::GovVarVariableInvalidKey("vaults", displayVaultIDs());
            }
        } else if (attrV0->type == AttributeTypes::Rules) {
            if (!displayRulesIDs().count(attrV0->typeId)) {
                return DeFiErrors::GovVarVariableInvalidKey("rules", displayRulesIDs());
            }
        }

        // Check key - Locks and Oracles have height int keys so skip.
        if (attrV0->type != AttributeTypes::Locks && attrV0->type != AttributeTypes::Oracles) {
            auto res = CheckValidAttrV0Key(attrV0->type, attrV0->typeId, attrV0->key);
            if (!res) {
                return res;
            }
        }
    }

    return Res::Ok();
}

template <typename T>
static void ExportOracleSplit(UniValue &ret, const std::string &key, const bool isFloat, const T &splitValues) {
    std::string keyValue;
    for (auto it{splitValues->begin()}; it != splitValues->end(); ++it) {
        if (it != splitValues->begin()) {
            keyValue += ',';
        }
        if (isFloat) {
            keyValue += KeyBuilder(it->first, GetDecimalStringNormalized(it->second));
        } else {
            keyValue += KeyBuilder(it->first, it->second);
        }
    }
    ret.pushKV(key, keyValue);
}

UniValue ATTRIBUTES::ExportFiltered(GovVarsFilter filter, const std::string &prefix, CCustomCSView *view) const {
    UniValue ret(UniValue::VOBJ);
    for (const auto &attribute : attributes) {
        const auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            continue;
        }
        if (filter == GovVarsFilter::LiveAttributes && attrV0->type != AttributeTypes::Live) {
            continue;
        } else if (filter == GovVarsFilter::Version2Dot7) {
            if (attrV0->type == AttributeTypes::Token &&
                attrsVersion27TokenHiddenSet.find(attrV0->key) != attrsVersion27TokenHiddenSet.end()) {
                continue;
            }
        }
        try {
            std::string id;
            if (attrV0->type == AttributeTypes::Param || attrV0->type == AttributeTypes::Live ||
                attrV0->type == AttributeTypes::Locks) {
                id = displayParamsIDs().at(attrV0->typeId);
            } else if (attrV0->type == AttributeTypes::EVMType) {
                id = displayEVMIDs().at(attrV0->typeId);
            } else if (attrV0->type == AttributeTypes::Oracles) {
                id = displayOracleIDs().at(attrV0->typeId);
            } else if (attrV0->type == AttributeTypes::Governance) {
                id = displayGovernanceIDs().at(attrV0->typeId);
            } else if (attrV0->type == AttributeTypes::Transfer) {
                id = displayTransferIDs().at(attrV0->typeId);
            } else if (attrV0->type == AttributeTypes::Vaults) {
                id = displayVaultIDs().at(attrV0->typeId);
            } else if (attrV0->type == AttributeTypes::Rules) {
                id = displayRulesIDs().at(attrV0->typeId);
            } else {
                id = KeyBuilder(attrV0->typeId);
            }

            const auto v0Key =
                (attrV0->type == AttributeTypes::Oracles && attrV0->key != OracleKeys::FractionalSplits) ||
                        attrV0->type == AttributeTypes::Locks
                    ? KeyBuilder(attrV0->key)
                    : displayKeys().at(attrV0->type).at(attrV0->key);

            auto key = KeyBuilder(displayVersions().at(VersionTypes::v0), displayTypes().at(attrV0->type), id, v0Key);

            if (attrV0->IsExtendedSize()) {
                key = KeyBuilder(key, attrV0->keyId);
            }

            if (filter == GovVarsFilter::PrefixedAttributes) {
                if (key.compare(0, prefix.size(), prefix) != 0) {
                    continue;
                }
            }

            if (const auto bool_val = std::get_if<bool>(&attribute.second)) {
                ret.pushKV(key, *bool_val ? "true" : "false");
            } else if (const auto number = std::get_if<int32_t>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(*number));
            } else if (const auto number = std::get_if<uint32_t>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(*number));
            } else if (const auto number = std::get_if<uint64_t>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(*number));
            } else if (const auto amount = std::get_if<CAmount>(&attribute.second)) {
                if (attrV0->type == AttributeTypes::Param &&
                    (attrV0->typeId == DFIP2203 || attrV0->typeId == DFIP2206F || attrV0->typeId == DFIP2211F) &&
                    (attrV0->key == DFIPKeys::BlockPeriod || attrV0->key == DFIPKeys::StartBlock ||
                     attrV0->key == DFIPKeys::LiquidityCalcSamplingPeriod)) {
                    ret.pushKV(key, KeyBuilder(*amount));
                } else {
                    const auto decimalStr = GetDecimalStringNormalized(*amount);
                    ret.pushKV(key, decimalStr);

                    // Create fee_pct alias of reward_pct.
                    if (v0Key == "reward_pct") {
                        const auto newKey = KeyBuilder(
                            displayVersions().at(VersionTypes::v0), displayTypes().at(attrV0->type), id, "fee_pct");
                        ret.pushKV(newKey, decimalStr);
                    }
                }
            } else if (const auto balances = std::get_if<CBalances>(&attribute.second)) {
                // Related to the economy keys shown in listgovs/getgov
                if (view) {
                    ret.pushKV(key, AmountsToJSON(*view, balances->balances));
                }
            } else if (const auto paybacks = std::get_if<CTokenPayback>(&attribute.second)) {
                // Related to the economy keys shown in listgovs/getgov
                UniValue result(UniValue::VOBJ);
                if (view) {
                    result.pushKV("paybackfees", AmountsToJSON(*view, paybacks->tokensFee.balances));
                    result.pushKV("paybacktokens", AmountsToJSON(*view, paybacks->tokensPayback.balances));
                }
                ret.pushKV(key, result);
            } else if (const auto balances = std::get_if<CDexBalances>(&attribute.second)) {
                for (const auto &pool : *balances) {
                    auto &dexTokenA = pool.second.totalTokenA;
                    auto &dexTokenB = pool.second.totalTokenB;
                    auto poolkey = KeyBuilder(key, pool.first.v);
                    ret.pushKV(KeyBuilder(poolkey, "total_commission_a"), ValueFromUint(dexTokenA.commissions));
                    ret.pushKV(KeyBuilder(poolkey, "total_commission_b"), ValueFromUint(dexTokenB.commissions));
                    ret.pushKV(KeyBuilder(poolkey, "fee_burn_a"), ValueFromUint(dexTokenA.feeburn));
                    ret.pushKV(KeyBuilder(poolkey, "fee_burn_b"), ValueFromUint(dexTokenB.feeburn));
                    ret.pushKV(KeyBuilder(poolkey, "total_swap_a"), ValueFromUint(dexTokenA.swaps));
                    ret.pushKV(KeyBuilder(poolkey, "total_swap_b"), ValueFromUint(dexTokenB.swaps));
                }
            } else if (const auto stats = std::get_if<CTransferDomainStatsLive>(&attribute.second)) {
                auto dvmEvmEdge = KeyBuilder(key, "dvm-evm");
                auto evmDvmEdge = KeyBuilder(key, "evm-dvm");
                auto dvmDomain = KeyBuilder(key, "dvm");
                auto evmDomain = KeyBuilder(key, "evm");
                auto v = std::vector<std::tuple<std::string, std::string, TAmounts>>{
                    {dvmEvmEdge, "total",   stats->dvmEvmTotal.balances},
                    {evmDvmEdge, "total",   stats->evmDvmTotal.balances},
                    {dvmDomain,  "current", stats->dvmCurrent.balances },
                    {dvmDomain,  "in",      stats->dvmIn.balances      },
                    {dvmDomain,  "out",     stats->dvmOut.balances     },
                    {evmDomain,  "current", stats->evmCurrent.balances },
                    {evmDomain,  "in",      stats->evmIn.balances      },
                    {evmDomain,  "out",     stats->evmOut.balances     },
                };

                for (const auto &[key, subkey, balances] : v) {
                    for (const auto &[id, value] : balances) {
                        ret.pushKV(KeyBuilder(key, id.v, subkey), ValueFromAmount(value));
                    }
                }
            } else if (const auto stats = std::get_if<CEvmBlockStatsLive>(&attribute.second)) {
                auto blockStatsKey = KeyBuilder(key, "block");
                auto v = std::vector<std::tuple<std::string, UniValue>>{
                    {"fee_burnt",             ValueFromAmount(stats->feeBurnt)      },
                    {"fee_burnt_min",         ValueFromAmount(stats->feeBurntMin)   },
                    {"fee_burnt_min_hash",    stats->feeBurntMinHash.GetHex()       },
                    {"fee_burnt_max",         ValueFromAmount(stats->feeBurntMax)   },
                    {"fee_burnt_max_hash",    stats->feeBurntMaxHash.GetHex()       },
                    {"fee_priority",          ValueFromAmount(stats->feePriority)   },
                    {"fee_priority_min",      ValueFromAmount(stats->feePriorityMin)},
                    {"fee_priority_min_hash", stats->feePriorityMinHash.GetHex()    },
                    {"fee_priority_max",      ValueFromAmount(stats->feePriorityMax)},
                    {"fee_priority_max_hash", stats->feePriorityMaxHash.GetHex()    },
                };
                for (const auto &[key, value] : v) {
                    ret.pushKV(KeyBuilder(blockStatsKey, key), value);
                }
            } else if (const auto splitValues = std::get_if<OracleSplits>(&attribute.second)) {
                ExportOracleSplit(ret, key, false, splitValues);
            } else if (const auto splitValues = std::get_if<OracleSplits64>(&attribute.second)) {
                ExportOracleSplit(ret, key, true, splitValues);
            } else if (const auto &descendantPair = std::get_if<DescendantValue>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(descendantPair->first, descendantPair->second));
            } else if (const auto &ascendantPair = std::get_if<AscendantValue>(&attribute.second)) {
                ret.pushKV(key, KeyBuilder(ascendantPair->first, ascendantPair->second));
            } else if (const auto currencyPair = std::get_if<CTokenCurrencyPair>(&attribute.second)) {
                ret.pushKV(key, currencyPair->first + '/' + currencyPair->second);
            } else if (const auto result = std::get_if<CFeeDir>(&attribute.second)) {
                if (result->feeDir == FeeDirValues::Both) {
                    ret.pushKV(key, "both");
                } else if (result->feeDir == FeeDirValues::In) {
                    ret.pushKV(key, "in");
                } else if (result->feeDir == FeeDirValues::Out) {
                    ret.pushKV(key, "out");
                }
            } else if (const auto members = std::get_if<std::set<CScript>>(&attribute.second)) {
                UniValue array(UniValue::VARR);
                for (const auto &member : *members) {
                    CTxDestination dest;
                    if (ExtractDestination(member, dest)) {
                        array.push_back(EncodeDestination(dest));
                    }
                }
                ret.pushKV(key, array);
            } else if (const auto strMembers = std::get_if<std::set<std::string>>(&attribute.second)) {
                UniValue array(UniValue::VARR);
                for (const auto &member : *strMembers) {
                    array.push_back(member);
                }
                ret.pushKV(key, array);
            } else if (const auto values = std::get_if<XVmAddressFormatItems>(&attribute.second)) {
                UniValue array(UniValue::VARR);
                for (const auto &value : *values) {
                    if (value == XVmAddressFormatTypes::Bech32) {
                        array.push_back("bech32");
                    } else if (value == XVmAddressFormatTypes::Bech32ProxyErc55) {
                        array.push_back("bech32-erc55");
                    } else if (value == XVmAddressFormatTypes::PkHash) {
                        array.push_back("p2pkh");
                    } else if (value == XVmAddressFormatTypes::PkHashProxyErc55) {
                        array.push_back("p2pkh-erc55");
                    } else if (value == XVmAddressFormatTypes::Erc55) {
                        array.push_back("erc55");
                    }
                }
                ret.pushKV(key, array);
            }
        } catch (const std::out_of_range &) {
            // Should not get here, that's mean maps are mismatched
        }
    }
    return ret;
}

UniValue ATTRIBUTES::Export() const {
    return ExportFiltered(GovVarsFilter::All, "");
}

template <typename T>
static Res ValidateOracleSplits(const ATTRIBUTES &attributes,
                                const CCustomCSView &view,
                                const bool checkFractional,
                                const T &splitMap) {
    CDataStructureV0 fractionalKey{AttributeTypes::Oracles, OracleIDs::Splits, OracleKeys::FractionalSplits};
    const auto fractionalEnabled = attributes.GetValue(fractionalKey, bool{});

    for (const auto &[tokenId, multiplier] : *splitMap) {
        if (tokenId == 0) {
            return DeFiErrors::GovVarValidateSplitDFI();
        }
        if (view.HasPoolPair({tokenId})) {
            return DeFiErrors::GovVarValidateSplitPool();
        }
        const auto token = view.GetToken(DCT_ID{tokenId});
        if (!token) {
            return DeFiErrors::GovVarValidateTokenExist(tokenId);
        }
        if (!token->IsDAT()) {
            return DeFiErrors::GovVarValidateSplitDAT();
        }
        if (!view.GetLoanTokenByID({tokenId})) {
            return DeFiErrors::GovVarValidateLoanTokenID(tokenId);
        }
        if (checkFractional) {
            if (!fractionalEnabled && multiplier % COIN != 0) {
                return DeFiErrors::GovVarVerifySplitFractional();
            }
            if (fractionalEnabled && multiplier < COIN && multiplier > -COIN) {
                return DeFiErrors::GovVarVerifySplitFractionalTooSmall();
            }
        }
    }

    return Res::Ok();
}

Res ATTRIBUTES::Validate(const CCustomCSView &view) const {
    if (view.GetLastHeight() < Params().GetConsensus().DF14FortCanningHillHeight) {
        return DeFiErrors::GovVarValidateFortCanningHill();
    }

    for (const auto &[key, value] : attributes) {
        const auto attrV0 = std::get_if<CDataStructureV0>(&key);
        if (!attrV0) {
            return DeFiErrors::GovVarUnsupportedVersion();
        }
        switch (attrV0->type) {
            case AttributeTypes::Token:
                switch (attrV0->key) {
                    case TokenKeys::LoanPaybackCollateral:
                        if (view.GetLastHeight() < Params().GetConsensus().DF19FortCanningEpilogueHeight) {
                            return DeFiErrors::GovVarValidateFortCanningEpilogue();
                        }

                        [[fallthrough]];
                    case TokenKeys::PaybackDFI:
                    case TokenKeys::PaybackDFIFeePCT:
                        if (!view.GetLoanTokenByID({attrV0->typeId})) {
                            return DeFiErrors::GovVarValidateLoanToken(attrV0->typeId);
                        }
                        break;
                    case TokenKeys::LoanPayback:
                    case TokenKeys::LoanPaybackFeePCT:
                        if (view.GetLastHeight() < Params().GetConsensus().DF15FortCanningRoadHeight) {
                            return DeFiErrors::GovVarValidateFortCanningRoad();
                        }
                        if (!view.GetLoanTokenByID({attrV0->typeId})) {
                            return DeFiErrors::GovVarValidateLoanToken(attrV0->typeId);
                        }
                        if (!view.GetToken(DCT_ID{attrV0->keyId})) {
                            return DeFiErrors::GovVarValidateToken(attrV0->keyId);
                        }
                        break;
                    case TokenKeys::DexInFeePct:
                    case TokenKeys::DexOutFeePct:
                        if (view.GetLastHeight() < Params().GetConsensus().DF15FortCanningRoadHeight) {
                            return DeFiErrors::GovVarValidateFortCanningRoad();
                        }
                        if (!view.GetToken(DCT_ID{attrV0->typeId})) {
                            return DeFiErrors::GovVarValidateToken(attrV0->typeId);
                        }
                        break;
                    case TokenKeys::LoanCollateralFactor:
                        if (view.GetLastHeight() < Params().GetConsensus().DF19FortCanningEpilogueHeight) {
                            const auto amount = std::get_if<CAmount>(&value);
                            if (amount) {
                                if (*amount > COIN) {
                                    return DeFiErrors::GovVarValidateExcessAmount();
                                }
                            }
                        }
                        [[fallthrough]];
                    case TokenKeys::LoanMintingInterest:
                        if (view.GetLastHeight() < Params().GetConsensus().DF18FortCanningGreatWorldHeight) {
                            const auto amount = std::get_if<CAmount>(&value);
                            if (amount) {
                                if (*amount < 0) {
                                    return DeFiErrors::GovVarValidateNegativeAmount();
                                }
                            }
                        }
                        [[fallthrough]];
                    case TokenKeys::LoanCollateralEnabled: {
                        if (view.GetLastHeight() < Params().GetConsensus().DF16FortCanningCrunchHeight) {
                            return DeFiErrors::GovVarValidateFortCanningCrunch();
                        }
                        // Post fork remove this guard as long as there were no non-DAT loan tokens before
                        // the fork. A full sync test on the removal of this guard will tell.
                        if (view.GetLastHeight() >= Params().GetConsensus().DF22MetachainHeight) {
                            if (!VerifyDATToken(view, attrV0->typeId)) {
                                return DeFiErrors::GovVarValidateToken(attrV0->typeId);
                            }
                        } else {
                            if (!VerifyToken(view, attrV0->typeId)) {
                                return DeFiErrors::GovVarValidateToken(attrV0->typeId);
                            }
                        }
                        CDataStructureV0 intervalPriceKey{
                            AttributeTypes::Token, attrV0->typeId, TokenKeys::FixedIntervalPriceId};
                        if (GetValue(intervalPriceKey, CTokenCurrencyPair{}) == CTokenCurrencyPair{}) {
                            return DeFiErrors::GovVarValidateCurrencyPair();
                        }
                        break;
                    }
                    case TokenKeys::LoanMintingEnabled: {
                        if (view.GetLastHeight() < Params().GetConsensus().DF16FortCanningCrunchHeight) {
                            return DeFiErrors::GovVarValidateFortCanningCrunch();
                        }
                        const auto tokenID = DCT_ID{attrV0->typeId};
                        const auto token = view.GetToken(tokenID);
                        if (!token) {
                            return DeFiErrors::GovVarValidateToken(attrV0->typeId);
                        }
                        // Post fork remove this guard as long as there were no non-DAT loan tokens before
                        // the fork. A full sync test on the removal of this guard will tell.
                        if (view.GetLastHeight() >= Params().GetConsensus().DF22MetachainHeight) {
                            if (!token->IsDAT()) {
                                return DeFiErrors::GovVarValidateToken(attrV0->typeId);
                            }
                        }
                        CDataStructureV0 intervalPriceKey{
                            AttributeTypes::Token, attrV0->typeId, TokenKeys::FixedIntervalPriceId};
                        if (GetValue(intervalPriceKey, CTokenCurrencyPair{}) == CTokenCurrencyPair{}) {
                            return DeFiErrors::GovVarValidateCurrencyPair();
                        }
                        break;
                    }
                    case TokenKeys::FixedIntervalPriceId:
                        if (view.GetLastHeight() < Params().GetConsensus().DF16FortCanningCrunchHeight) {
                            return DeFiErrors::GovVarValidateFortCanningCrunch();
                        }
                        if (!VerifyToken(view, attrV0->typeId)) {
                            return DeFiErrors::GovVarValidateToken(attrV0->typeId);
                        }
                        break;
                    case TokenKeys::DFIP2203Enabled:
                        if (view.GetLastHeight() < Params().GetConsensus().DF15FortCanningRoadHeight) {
                            return DeFiErrors::GovVarValidateFortCanningRoad();
                        }
                        if (!view.GetLoanTokenByID({attrV0->typeId})) {
                            return DeFiErrors::GovVarValidateLoanToken(attrV0->typeId);
                        }
                        break;
                    case TokenKeys::Ascendant:
                    case TokenKeys::Descendant:
                    case TokenKeys::Epitaph:
                        break;
                    default:
                        return Res::Err("Unsupported key");
                }
                break;

            case AttributeTypes::Oracles:
                if (view.GetLastHeight() < Params().GetConsensus().DF16FortCanningCrunchHeight) {
                    return DeFiErrors::GovVarValidateFortCanningCrunch();
                }
                if (attrV0->typeId == OracleIDs::Splits) {
                    if (attrV0->key == OracleKeys::FractionalSplits) {
                        if (view.GetLastHeight() < Params().GetConsensus().DF23Height) {
                            return Res::Err("Cannot be set before DF23Height");
                        }
                    } else {
                        const auto splitMap = std::get_if<OracleSplits>(&value);
                        if (splitMap) {
                            if (auto res = ValidateOracleSplits(*this, view, false, splitMap); !res) {
                                return res;
                            }
                        } else {
                            const auto splitMap64 = std::get_if<OracleSplits64>(&value);
                            if (!splitMap64) {
                                return DeFiErrors::GovVarUnsupportedValue();
                            }
                            if (auto res = ValidateOracleSplits(*this, view, true, splitMap64); !res) {
                                return res;
                            }
                        }
                    }
                } else {
                    return DeFiErrors::GovVarValidateUnsupportedKey();
                }
                break;

            case AttributeTypes::Poolpairs:
                switch (attrV0->key) {
                    case PoolKeys::TokenAFeePCT:
                    case PoolKeys::TokenBFeePCT:
                        if (!view.GetPoolPair({attrV0->typeId})) {
                            return DeFiErrors::GovVarApplyInvalidPool(attrV0->typeId);
                        }
                        break;
                    case PoolKeys::TokenAFeeDir:
                    case PoolKeys::TokenBFeeDir:
                        if (view.GetLastHeight() < Params().GetConsensus().DF17FortCanningSpringHeight) {
                            return DeFiErrors::GovVarValidateFortCanningSpring();
                        }
                        if (!view.GetPoolPair({attrV0->typeId})) {
                            return DeFiErrors::GovVarApplyInvalidPool(attrV0->typeId);
                        }
                        break;
                    default:
                        return Res::Err("Unsupported key");
                }
                break;

            case AttributeTypes::Param:
                if (attrV0->typeId == ParamIDs::Feature) {
                    if (view.GetLastHeight() < Params().GetConsensus().DF20GrandCentralHeight) {
                        return Res::Err("Cannot be set before GrandCentralHeight");
                    }
                    if (attrV0->key == DFIPKeys::MintTokens) {
                        if (view.GetLastHeight() < Params().GetConsensus().DF21GrandCentralEpilogueHeight) {
                            return Res::Err("Cannot be set before GrandCentralEpilogueHeight");
                        }
                    } else if (attrV0->key == DFIPKeys::EVMEnabled || attrV0->key == DFIPKeys::TransferDomain) {
                        if (view.GetLastHeight() < Params().GetConsensus().DF22MetachainHeight) {
                            return Res::Err("Cannot be set before MetachainHeight");
                        }
                    }
                } else if (attrV0->typeId == ParamIDs::Foundation) {
                    if (view.GetLastHeight() < Params().GetConsensus().DF20GrandCentralHeight) {
                        return Res::Err("Cannot be set before GrandCentralHeight");
                    }
                } else if (attrV0->typeId == ParamIDs::DFIP2206F || attrV0->typeId == ParamIDs::DFIP2206A) {
                    if (view.GetLastHeight() < Params().GetConsensus().DF17FortCanningSpringHeight) {
                        return Res::Err("Cannot be set before FortCanningSpringHeight");
                    }
                } else if (attrV0->typeId == ParamIDs::DFIP2203) {
                    if (view.GetLastHeight() < Params().GetConsensus().DF15FortCanningRoadHeight) {
                        return DeFiErrors::GovVarValidateFortCanningRoad();
                    }
                    if (attrV0->key == DFIPKeys::StartBlock) {
                        if (view.GetLastHeight() < Params().GetConsensus().DF17FortCanningSpringHeight) {
                            return Res::Err("Cannot be set before FortCanningSpringHeight");
                        }
                    }
                } else if (attrV0->typeId == ParamIDs::DFIP2211F) {
                    if (view.GetLastHeight() < Params().GetConsensus().DF23Height) {
                        return DeFiErrors::GovVarValidateDF23Height();
                    }
                    if (attrV0->key == DFIPKeys::BlockPeriod) {
                        CDataStructureV0 samplingKey{
                            AttributeTypes::Param, ParamIDs::DFIP2211F, DFIPKeys::LiquidityCalcSamplingPeriod};
                        const auto samplingPeriod = GetValue(samplingKey, DEFAULT_LIQUIDITY_CALC_SAMPLING_PERIOD);
                        const auto blockPeriod = std::get_if<CAmount>(&value);
                        if (!blockPeriod) {
                            return DeFiErrors::GovVarUnsupportedValue();
                        }
                        if (*blockPeriod < samplingPeriod) {
                            return DeFiErrors::GovVarValidateBlockPeriod();
                        }
                    }
                } else if (attrV0->typeId != ParamIDs::DFIP2201) {
                    return Res::Err("Unrecognised param id");
                }
                break;

            // Live is set internally
            case AttributeTypes::Live:
                break;

            case AttributeTypes::Locks:
                if (view.GetLastHeight() < Params().GetConsensus().DF16FortCanningCrunchHeight) {
                    return Res::Err("Cannot be set before FortCanningCrunch");
                }
                if (attrV0->typeId != ParamIDs::TokenID) {
                    return Res::Err("Unrecognised locks id");
                }
                if (!view.GetLoanTokenByID(DCT_ID{attrV0->key}).has_value()) {
                    return Res::Err("No loan token with id (%d)", attrV0->key);
                }
                break;

            case AttributeTypes::Governance:
                if (view.GetLastHeight() < Params().GetConsensus().DF20GrandCentralHeight) {
                    return Res::Err("Cannot be set before GrandCentral");
                }
                break;

            case AttributeTypes::EVMType:
                if (view.GetLastHeight() < Params().GetConsensus().DF22MetachainHeight) {
                    return Res::Err("Cannot be set before Metachain");
                }
                break;

            case AttributeTypes::Transfer:
                if (view.GetLastHeight() < Params().GetConsensus().DF22MetachainHeight) {
                    return Res::Err("Cannot be set before Metachain");
                }
                if ((attrV0->typeId == TransferIDs::DVMToEVM || attrV0->typeId == TransferIDs::EVMToDVM) &&
                    attrV0->key == TransferKeys::Disallowed) {
                    const auto strSet = std::get_if<std::set<std::string>>(&value);
                    if (!strSet) {
                        return DeFiErrors::GovVarUnsupportedValue();
                    }
                    if (auto res = VerifyTokenSet(view, *strSet); !res) {
                        return res;
                    }
                }
                break;

            case AttributeTypes::Vaults:
                if (attrV0->typeId == VaultIDs::DUSDVault) {
                    if (attrV0->key == VaultKeys::DUSDVaultEnabled) {
                        if (view.GetLastHeight() < Params().GetConsensus().DF22MetachainHeight) {
                            return Res::Err("Cannot be set before Metachain");
                        }
                    }
                } else if (attrV0->typeId == VaultIDs::Parameters) {
                    if (attrV0->key == VaultKeys::CreationFee) {
                        if (view.GetLastHeight() < Params().GetConsensus().DF23Height) {
                            return Res::Err("Cannot be set before DF23Height");
                        }
                    }
                }
                break;

            case AttributeTypes::Rules:
                if (view.GetLastHeight() < Params().GetConsensus().DF22MetachainHeight) {
                    return Res::Err("Cannot be set before Metachain");
                }
                break;

            default:
                return Res::Err("Unrecognised type (%d)", attrV0->type);
        }
    }

    return Res::Ok();
}

Res ATTRIBUTES::Apply(CCustomCSView &mnview, const uint32_t height) {
    for (const auto &attribute : attributes) {
        const auto attrV0 = std::get_if<CDataStructureV0>(&attribute.first);
        if (!attrV0) {
            continue;
        }
        if (attrV0->type == AttributeTypes::Poolpairs) {
            if (attrV0->key == PoolKeys::TokenAFeePCT || attrV0->key == PoolKeys::TokenBFeePCT) {
                auto poolId = DCT_ID{attrV0->typeId};
                auto pool = mnview.GetPoolPair(poolId);
                if (!pool) {
                    return DeFiErrors::GovVarApplyInvalidPool(poolId.v);
                }
                auto tokenId = attrV0->key == PoolKeys::TokenAFeePCT ? pool->idTokenA : pool->idTokenB;

                const auto valuePct = std::get_if<CAmount>(&attribute.second);
                if (!valuePct) {
                    return DeFiErrors::GovVarApplyUnexpectedType();
                }
                if (auto res = mnview.SetDexFeePct(poolId, tokenId, *valuePct); !res) {
                    return res;
                }
            }
        } else if (attrV0->type == AttributeTypes::Token) {
            if (attrV0->key == TokenKeys::DexInFeePct || attrV0->key == TokenKeys::DexOutFeePct) {
                DCT_ID tokenA{attrV0->typeId}, tokenB{~0u};
                if (attrV0->key == TokenKeys::DexOutFeePct) {
                    std::swap(tokenA, tokenB);
                }
                const auto valuePct = std::get_if<CAmount>(&attribute.second);
                if (!valuePct) {
                    return DeFiErrors::GovVarApplyUnexpectedType();
                }
                if (auto res = mnview.SetDexFeePct(tokenA, tokenB, *valuePct); !res) {
                    return res;
                }
            } else if (attrV0->key == TokenKeys::FixedIntervalPriceId) {
                if (const auto &currencyPair = std::get_if<CTokenCurrencyPair>(&attribute.second)) {
                    // Already exists, skip.
                    if (auto it = mnview.LowerBound<COracleView::FixedIntervalPriceKey>(*currencyPair);
                        it.Valid() && it.Key() == *currencyPair) {
                        continue;
                    } else if (!OraclePriceFeed(mnview, *currencyPair)) {
                        return Res::Err("Price feed %s/%s does not belong to any oracle",
                                        currencyPair->first,
                                        currencyPair->second);
                    }
                    CFixedIntervalPrice fixedIntervalPrice;
                    fixedIntervalPrice.priceFeedId = *currencyPair;
                    fixedIntervalPrice.timestamp = time;
                    fixedIntervalPrice.priceRecord[1] = -1;
                    const auto aggregatePrice = GetAggregatePrice(
                        mnview, fixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.second, time);
                    if (aggregatePrice) {
                        fixedIntervalPrice.priceRecord[1] = aggregatePrice;
                    }
                    if (auto res = mnview.SetFixedIntervalPrice(fixedIntervalPrice); !res) {
                        return res;
                    }
                } else {
                    return Res::Err("Unrecognised value for FixedIntervalPriceId");
                }
            } else if (attrV0->key == TokenKeys::DFIP2203Enabled) {
                const auto value = std::get_if<bool>(&attribute.second);
                if (!value) {
                    return DeFiErrors::GovVarApplyUnexpectedType();
                }

                if (*value) {
                    continue;
                }

                const auto token = mnview.GetLoanTokenByID(DCT_ID{attrV0->typeId});
                if (!token) {
                    return DeFiErrors::GovVarValidateLoanTokenID(attrV0->typeId);
                }

                // Special case: DUSD will be used as a source for swaps but will
                // be set as disabled for Future swap destination.
                if (token->symbol == "DUSD") {
                    continue;
                }

                if (auto res = RefundFuturesContracts(mnview, height, attrV0->typeId); !res) {
                    return res;
                }
            } else if (attrV0->key == TokenKeys::LoanMintingInterest) {
                if (height >= static_cast<uint32_t>(Params().GetConsensus().DF18FortCanningGreatWorldHeight) &&
                    interestTokens.count(attrV0->typeId)) {
                    const auto tokenInterest = std::get_if<CAmount>(&attribute.second);
                    if (!tokenInterest) {
                        return DeFiErrors::GovVarApplyUnexpectedType();
                    }

                    std::set<CVaultId> affectedVaults;
                    mnview.ForEachLoanTokenAmount([&](const CVaultId &vaultId, const CBalances &balances) {
                        for (const auto &[tokenId, discarded] : balances.balances) {
                            if (tokenId.v == attrV0->typeId) {
                                affectedVaults.insert(vaultId);
                            }
                        }
                        return true;
                    });

                    for (const auto &vaultId : affectedVaults) {
                        const auto vault = mnview.GetVault(vaultId);
                        assert(vault);

                        // Updated stored interest with new interest rate.
                        mnview.IncreaseInterest(height, vaultId, vault->schemeId, {attrV0->typeId}, *tokenInterest, 0);
                    }
                }
            } else if (attrV0->key == TokenKeys::LoanCollateralFactor) {
                if (height >= static_cast<uint32_t>(Params().GetConsensus().DF19FortCanningEpilogueHeight)) {
                    // Skip on if skip collateral check is passed
                    if (Params().NetworkIDString() == CBaseChainParams::REGTEST &&
                        gArgs.GetBoolArg("-regtest-skip-loan-collateral-validation", false)) {
                        continue;
                    }

                    std::set<CAmount> ratio;
                    mnview.ForEachLoanScheme([&ratio](const std::string &identifier, const CLoanSchemeData &data) {
                        ratio.insert(data.ratio);
                        return true;
                    });

                    // No loan schemes, fall back to 100% limit
                    if (ratio.empty()) {
                        if (const auto amount = std::get_if<CAmount>(&attribute.second); amount && *amount > COIN) {
                            return Res::Err("Percentage exceeds 100%%");
                        }
                    } else {
                        const auto factor = std::get_if<CAmount>(&attribute.second);
                        if (!factor) {
                            return DeFiErrors::GovVarApplyUnexpectedType();
                        }
                        if (*factor >= *ratio.begin() * CENT) {
                            return DeFiErrors::GovVarApplyInvalidFactor(*ratio.begin());
                        }
                    }
                }
            }
        } else if (attrV0->type == AttributeTypes::Param) {
            if (attrV0->typeId == ParamIDs::DFIP2203) {
                if (attrV0->key == DFIPKeys::Active) {
                    const auto value = std::get_if<bool>(&attribute.second);
                    if (!value) {
                        return DeFiErrors::GovVarApplyUnexpectedType();
                    }

                    if (*value) {
                        continue;
                    }

                    if (auto res = RefundFuturesContracts(mnview, height); !res) {
                        return res;
                    }
                } else if (attrV0->key == DFIPKeys::BlockPeriod || attrV0->key == DFIPKeys::StartBlock) {
                    // Only check this when block period has been set, otherwise
                    // it will fail when DFIP2203 active is set to true.
                    if (!futureUpdated) {
                        continue;
                    }

                    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::Active};
                    if (GetValue(activeKey, false)) {
                        return DeFiErrors::GovVarApplyDFIPActive("DFIP2203");
                    }
                }
            } else if (attrV0->typeId == ParamIDs::DFIP2206F) {
                if (attrV0->key == DFIPKeys::Active) {
                    const auto value = std::get_if<bool>(&attribute.second);
                    if (!value) {
                        return DeFiErrors::GovVarApplyUnexpectedType();
                    }

                    if (*value) {
                        continue;
                    }

                    if (auto res = RefundFuturesDUSD(mnview, height); !res) {
                        return res;
                    }
                } else if (attrV0->key == DFIPKeys::BlockPeriod) {
                    // Only check this when block period has been set, otherwise
                    // it will fail when DFIP2206F active is set to true.
                    if (!futureDUSDUpdated) {
                        continue;
                    }

                    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2206F, DFIPKeys::Active};
                    if (GetValue(activeKey, false)) {
                        return DeFiErrors::GovVarApplyDFIPActive("DFIP2206F");
                    }
                }
            } else if (attrV0->typeId == ParamIDs::DFIP2211F) {
                if (attrV0->key == DFIPKeys::Active) {
                    const auto value = std::get_if<bool>(&attribute.second);
                    if (!value) {
                        return DeFiErrors::GovVarApplyUnexpectedType();
                    }

                    if (*value) {
                        continue;
                    }

                    // Disabled so delete all data to prevent stale data
                    // when re-enabled. Gov var TX could enable it again
                    // and a subsequent TX in the block could be impacted.
                    std::vector<LoanTokenLiquidityPerBlockKey> perBlockKeys;
                    mnview.ForEachTokenLiquidityPerBlock(
                        [&](const LoanTokenLiquidityPerBlockKey &key, const CAmount &liquidityPerBlock) {
                            perBlockKeys.push_back(key);
                            return true;
                        });

                    for (const auto &key : perBlockKeys) {
                        mnview.EraseTokenLiquidityPerBlock(key);
                    }

                    std::vector<LoanTokenAverageLiquidityKey> averageKeys;
                    mnview.ForEachTokenAverageLiquidity([&](const LoanTokenAverageLiquidityKey &key, const uint64_t) {
                        averageKeys.push_back(key);
                        return true;
                    });

                    for (const auto &key : averageKeys) {
                        mnview.EraseTokenAverageLiquidity(key);
                    }
                }
            }

        } else if (attrV0->type == AttributeTypes::Oracles && attrV0->typeId == OracleIDs::Splits &&
                   attrV0->key != OracleKeys::FractionalSplits) {
            const auto value = std::get_if<OracleSplits>(&attribute.second);
            const auto value64 = std::get_if<OracleSplits64>(&attribute.second);
            if (!value && !value64) {
                return DeFiErrors::GovVarUnsupportedValue();
            }

            for (const auto split : tokenSplits) {
                if ((value && value->find(split) == value->end()) ||
                    (value64 && value64->find(split) == value64->end())) {
                    continue;
                }

                if (attrV0->key <= height) {
                    return DeFiErrors::GovVarApplyBelowHeight();
                }

                CDataStructureV0 lockKey{AttributeTypes::Locks, ParamIDs::TokenID, split};
                if (GetValue(lockKey, false)) {
                    continue;
                }

                if (!mnview.GetLoanTokenByID(DCT_ID{split})) {
                    return DeFiErrors::GovVarApplyAutoNoToken(split);
                }

                const auto startHeight = attrV0->key - Params().GetConsensus().blocksPerDay() / 2;
                if (height < startHeight) {
                    auto var = GovVariable::Create("ATTRIBUTES");
                    if (!var) {
                        return DeFiErrors::GovVarApplyLockFail();
                    }

                    auto govVar = std::dynamic_pointer_cast<ATTRIBUTES>(var);
                    if (!govVar) {
                        return DeFiErrors::GovVarApplyCastFail();
                    }
                    govVar->attributes[lockKey] = true;

                    CGovernanceHeightMessage lock;
                    lock.startHeight = startHeight;
                    lock.govVar = govVar;

                    if (auto res = StoreGovVars(lock, mnview); !res) {
                        return res;
                    }
                } else {
                    // Less than a day's worth of blocks, apply instant lock
                    SetValue(lockKey, true);
                }
            }
        }

        const auto govVarPtr = static_cast<const uint8_t *>(static_cast<const void *>(&attribute.second));
        const auto govVarVec = std::vector<uint8_t>(govVarPtr, govVarPtr + sizeof(attribute.second));

        rust::Vec<uint8_t> govVarValue{};
        govVarValue.reserve(govVarVec.size());
        std::copy(govVarVec.begin(), govVarVec.end(), govVarValue.begin());

        if (evmTemplate) {
            CrossBoundaryResult result;
            const auto rustKey = GovVarKeyDataStructure{attrV0->type, attrV0->typeId, attrV0->key, attrV0->keyId};
            if (!evm_try_unsafe_handle_attribute_apply(result, evmTemplate->GetTemplate(), rustKey, govVarValue)) {
                return DeFiErrors::SettingEVMAttributeFailure();
            }
            if (!result.ok) {
                return DeFiErrors::SettingEVMAttributeFailure(result.reason.c_str());
            }
        }
    }

    return Res::Ok();
}

Res ATTRIBUTES::Erase(CCustomCSView &mnview, uint32_t, const std::vector<std::string> &keys) {
    for (const auto &key : keys) {
        auto res = ProcessVariable(key, {}, [&](const CAttributeType &attribute, const CAttributeValue &) {
            auto attrV0 = std::get_if<CDataStructureV0>(&attribute);
            if (!attrV0) {
                return Res::Ok();
            }
            if (attrV0->type == AttributeTypes::Live) {
                return DeFiErrors::GovVarEraseLive();
            }
            if (!EraseKey(attribute)) {
                return DeFiErrors::GovVarEraseNonExist(attrV0->type);
            }
            if (attrV0->type == AttributeTypes::Poolpairs) {
                auto poolId = DCT_ID{attrV0->typeId};
                auto pool = mnview.GetPoolPair(poolId);
                if (!pool) {
                    return DeFiErrors::GovVarApplyInvalidPool(poolId.v);
                }
                auto tokenId = attrV0->key == PoolKeys::TokenAFeePCT ? pool->idTokenA : pool->idTokenB;

                return mnview.EraseDexFeePct(poolId, tokenId);
            } else if (attrV0->type == AttributeTypes::Token) {
                if (attrV0->key == TokenKeys::DexInFeePct || attrV0->key == TokenKeys::DexOutFeePct) {
                    DCT_ID tokenA{attrV0->typeId}, tokenB{~0u};
                    if (attrV0->key == TokenKeys::DexOutFeePct) {
                        std::swap(tokenA, tokenB);
                    }
                    return mnview.EraseDexFeePct(tokenA, tokenB);
                }
            }
            return Res::Ok();
        });
        if (!res) {
            return res;
        }
    }

    return Res::Ok();
}
