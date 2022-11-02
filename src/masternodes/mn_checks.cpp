// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/anchors.h>
#include <masternodes/balances.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/mn_checks.h>
#include <masternodes/oracles.h>
#include <masternodes/res.h>
#include <masternodes/vaulthistory.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/tx_check.h>
#include <core_io.h>
#include <index/txindex.h>
#include <logging.h>
#include <masternodes/govvariables/oracle_block_interval.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <txmempool.h>
#include <streams.h>
#include <validation.h>

#include <algorithm>

constexpr std::string_view ERR_STRING_MIN_COLLATERAL_DFI_PCT = "At least 50%% of the minimum required collateral must be in DFI";
constexpr std::string_view ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT = "At least 50%% of the minimum required collateral must be in DFI or DUSD";

std::string ToString(CustomTxType type) {
    switch (type)
    {
        case CustomTxType::CreateMasternode:    return "CreateMasternode";
        case CustomTxType::ResignMasternode:    return "ResignMasternode";
        case CustomTxType::SetForcedRewardAddress: return "SetForcedRewardAddress";
        case CustomTxType::RemForcedRewardAddress: return "RemForcedRewardAddress";
        case CustomTxType::UpdateMasternode:    return "UpdateMasternode";
        case CustomTxType::CreateToken:         return "CreateToken";
        case CustomTxType::UpdateToken:         return "UpdateToken";
        case CustomTxType::UpdateTokenAny:      return "UpdateTokenAny";
        case CustomTxType::MintToken:           return "MintToken";
        case CustomTxType::BurnToken:           return "BurnToken";
        case CustomTxType::CreatePoolPair:      return "CreatePoolPair";
        case CustomTxType::UpdatePoolPair:      return "UpdatePoolPair";
        case CustomTxType::PoolSwap:            return "PoolSwap";
        case CustomTxType::PoolSwapV2:          return "PoolSwap";
        case CustomTxType::AddPoolLiquidity:    return "AddPoolLiquidity";
        case CustomTxType::RemovePoolLiquidity: return "RemovePoolLiquidity";
        case CustomTxType::UtxosToAccount:      return "UtxosToAccount";
        case CustomTxType::AccountToUtxos:      return "AccountToUtxos";
        case CustomTxType::AccountToAccount:    return "AccountToAccount";
        case CustomTxType::AnyAccountsToAccounts:   return "AnyAccountsToAccounts";
        case CustomTxType::SmartContract:       return "SmartContract";
        case CustomTxType::FutureSwap:          return "DFIP2203";
        case CustomTxType::SetGovVariable:      return "SetGovVariable";
        case CustomTxType::SetGovVariableHeight:return "SetGovVariableHeight";
        case CustomTxType::AppointOracle:       return "AppointOracle";
        case CustomTxType::RemoveOracleAppoint: return "RemoveOracleAppoint";
        case CustomTxType::UpdateOracleAppoint: return "UpdateOracleAppoint";
        case CustomTxType::SetOracleData:       return "SetOracleData";
        case CustomTxType::AutoAuthPrep:        return "AutoAuth";
        case CustomTxType::ICXCreateOrder:      return "ICXCreateOrder";
        case CustomTxType::ICXMakeOffer:        return "ICXMakeOffer";
        case CustomTxType::ICXSubmitDFCHTLC:    return "ICXSubmitDFCHTLC";
        case CustomTxType::ICXSubmitEXTHTLC:    return "ICXSubmitEXTHTLC";
        case CustomTxType::ICXClaimDFCHTLC:     return "ICXClaimDFCHTLC";
        case CustomTxType::ICXCloseOrder:       return "ICXCloseOrder";
        case CustomTxType::ICXCloseOffer:       return "ICXCloseOffer";
        case CustomTxType::SetLoanCollateralToken: return "SetLoanCollateralToken";
        case CustomTxType::SetLoanToken:        return "SetLoanToken";
        case CustomTxType::UpdateLoanToken:     return "UpdateLoanToken";
        case CustomTxType::LoanScheme:          return "LoanScheme";
        case CustomTxType::DefaultLoanScheme:   return "DefaultLoanScheme";
        case CustomTxType::DestroyLoanScheme:   return "DestroyLoanScheme";
        case CustomTxType::Vault:               return "Vault";
        case CustomTxType::CloseVault:          return "CloseVault";
        case CustomTxType::UpdateVault:         return "UpdateVault";
        case CustomTxType::DepositToVault:      return "DepositToVault";
        case CustomTxType::WithdrawFromVault:   return "WithdrawFromVault";
        case CustomTxType::PaybackWithCollateral: return "PaybackWithCollateral";
        case CustomTxType::TakeLoan:            return "TakeLoan";
        case CustomTxType::PaybackLoan:         return "PaybackLoan";
        case CustomTxType::PaybackLoanV2:       return "PaybackLoan";
        case CustomTxType::AuctionBid:          return "AuctionBid";
        case CustomTxType::FutureSwapExecution: return "FutureSwapExecution";
        case CustomTxType::FutureSwapRefund:    return "FutureSwapRefund";
        case CustomTxType::TokenSplit:          return "TokenSplit";
        case CustomTxType::Reject:              return "Reject";
        case CustomTxType::None:                return "None";
    }
    return "None";
}

CustomTxType FromString(const std::string& str) {
    static const auto customTxTypeMap = []() {
     std::map<std::string, CustomTxType> generatedMap;
     for (auto i = 0u; i < 256; i++) {
          auto txType = static_cast<CustomTxType>(i);
          generatedMap.emplace(ToString(txType), txType);
     }
     return generatedMap;
    }();
    auto type = customTxTypeMap.find(str);
    return type == customTxTypeMap.end() ? CustomTxType::None : type->second;
}

static ResVal<CBalances> BurntTokens(CTransaction const & tx) {
    CBalances balances;
    for (const auto& out : tx.vout) {
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN) {
            auto res = balances.Add(out.TokenAmount());
            if (!res.ok) {
                return res;
            }
        }
    }
    return {balances, Res::Ok()};
}

static ResVal<CBalances> MintedTokens(CTransaction const & tx, uint32_t mintingOutputsStart) {
    CBalances balances;
    for (uint32_t i = mintingOutputsStart; i < (uint32_t) tx.vout.size(); i++) {
        auto res = balances.Add(tx.vout[i].TokenAmount());
        if (!res.ok) {
            return res;
        }
    }
    return {balances, Res::Ok()};
}

CCustomTxMessage customTypeToMessage(CustomTxType txType) {
    switch (txType)
    {
        case CustomTxType::CreateMasternode:        return CCreateMasterNodeMessage{};
        case CustomTxType::ResignMasternode:        return CResignMasterNodeMessage{};
        case CustomTxType::SetForcedRewardAddress:  return CSetForcedRewardAddressMessage{};
        case CustomTxType::RemForcedRewardAddress:  return CRemForcedRewardAddressMessage{};
        case CustomTxType::UpdateMasternode:        return CUpdateMasterNodeMessage{};
        case CustomTxType::CreateToken:             return CCreateTokenMessage{};
        case CustomTxType::UpdateToken:             return CUpdateTokenPreAMKMessage{};
        case CustomTxType::UpdateTokenAny:          return CUpdateTokenMessage{};
        case CustomTxType::MintToken:               return CMintTokensMessage{};
        case CustomTxType::BurnToken:               return CBurnTokensMessage{};
        case CustomTxType::CreatePoolPair:          return CCreatePoolPairMessage{};
        case CustomTxType::UpdatePoolPair:          return CUpdatePoolPairMessage{};
        case CustomTxType::PoolSwap:                return CPoolSwapMessage{};
        case CustomTxType::PoolSwapV2:              return CPoolSwapMessageV2{};
        case CustomTxType::AddPoolLiquidity:        return CLiquidityMessage{};
        case CustomTxType::RemovePoolLiquidity:     return CRemoveLiquidityMessage{};
        case CustomTxType::UtxosToAccount:          return CUtxosToAccountMessage{};
        case CustomTxType::AccountToUtxos:          return CAccountToUtxosMessage{};
        case CustomTxType::AccountToAccount:        return CAccountToAccountMessage{};
        case CustomTxType::AnyAccountsToAccounts:   return CAnyAccountsToAccountsMessage{};
        case CustomTxType::SmartContract:           return CSmartContractMessage{};
        case CustomTxType::FutureSwap:                return CFutureSwapMessage{};
        case CustomTxType::SetGovVariable:          return CGovernanceMessage{};
        case CustomTxType::SetGovVariableHeight:    return CGovernanceHeightMessage{};
        case CustomTxType::AppointOracle:           return CAppointOracleMessage{};
        case CustomTxType::RemoveOracleAppoint:     return CRemoveOracleAppointMessage{};
        case CustomTxType::UpdateOracleAppoint:     return CUpdateOracleAppointMessage{};
        case CustomTxType::SetOracleData:           return CSetOracleDataMessage{};
        case CustomTxType::AutoAuthPrep:            return CCustomTxMessageNone{};
        case CustomTxType::ICXCreateOrder:          return CICXCreateOrderMessage{};
        case CustomTxType::ICXMakeOffer:            return CICXMakeOfferMessage{};
        case CustomTxType::ICXSubmitDFCHTLC:        return CICXSubmitDFCHTLCMessage{};
        case CustomTxType::ICXSubmitEXTHTLC:        return CICXSubmitEXTHTLCMessage{};
        case CustomTxType::ICXClaimDFCHTLC:         return CICXClaimDFCHTLCMessage{};
        case CustomTxType::ICXCloseOrder:           return CICXCloseOrderMessage{};
        case CustomTxType::ICXCloseOffer:           return CICXCloseOfferMessage{};
        case CustomTxType::SetLoanCollateralToken:  return CLoanSetCollateralTokenMessage{};
        case CustomTxType::SetLoanToken:            return CLoanSetLoanTokenMessage{};
        case CustomTxType::UpdateLoanToken:         return CLoanUpdateLoanTokenMessage{};
        case CustomTxType::LoanScheme:              return CLoanSchemeMessage{};
        case CustomTxType::DefaultLoanScheme:       return CDefaultLoanSchemeMessage{};
        case CustomTxType::DestroyLoanScheme:       return CDestroyLoanSchemeMessage{};
        case CustomTxType::Vault:                   return CVaultMessage{};
        case CustomTxType::CloseVault:              return CCloseVaultMessage{};
        case CustomTxType::UpdateVault:             return CUpdateVaultMessage{};
        case CustomTxType::DepositToVault:          return CDepositToVaultMessage{};
        case CustomTxType::WithdrawFromVault:       return CWithdrawFromVaultMessage{};
        case CustomTxType::PaybackWithCollateral:   return CPaybackWithCollateralMessage{};
        case CustomTxType::TakeLoan:                return CLoanTakeLoanMessage{};
        case CustomTxType::PaybackLoan:             return CLoanPaybackLoanMessage{};
        case CustomTxType::PaybackLoanV2:           return CLoanPaybackLoanV2Message{};
        case CustomTxType::AuctionBid:              return CAuctionBidMessage{};
        case CustomTxType::FutureSwapExecution:     return CCustomTxMessageNone{};
        case CustomTxType::FutureSwapRefund:        return CCustomTxMessageNone{};
        case CustomTxType::TokenSplit:              return CCustomTxMessageNone{};
        case CustomTxType::Reject:                  return CCustomTxMessageNone{};
        case CustomTxType::None:                    return CCustomTxMessageNone{};
    }
    return CCustomTxMessageNone{};
}

extern std::string ScriptToString(CScript const& script);

class CCustomMetadataParseVisitor
{
    uint32_t height;
    const Consensus::Params& consensus;
    const std::vector<unsigned char>& metadata;

    Res isPostAMKFork() const {
        if(static_cast<int>(height) < consensus.AMKHeight) {
            return Res::Err("called before AMK height");
        }
        return Res::Ok();
    }

    Res isPostBayfrontFork() const {
        if(static_cast<int>(height) < consensus.BayfrontHeight) {
            return Res::Err("called before Bayfront height");
        }
        return Res::Ok();
    }

    Res isPostBayfrontGardensFork() const {
        if(static_cast<int>(height) < consensus.BayfrontGardensHeight) {
            return Res::Err("called before Bayfront Gardens height");
        }
        return Res::Ok();
    }

    Res isPostEunosFork() const {
        if(static_cast<int>(height) < consensus.EunosHeight) {
            return Res::Err("called before Eunos height");
        }
        return Res::Ok();
    }

    Res isPostEunosPayaFork() const {
        if(static_cast<int>(height) < consensus.EunosPayaHeight) {
            return Res::Err("called before EunosPaya height");
        }
        return Res::Ok();
    }

    Res isPostFortCanningFork() const {
        if(static_cast<int>(height) < consensus.FortCanningHeight) {
            return Res::Err("called before FortCanning height");
        }
        return Res::Ok();
    }

    Res isPostFortCanningHillFork() const {
        if(static_cast<int>(height) < consensus.FortCanningHillHeight) {
            return Res::Err("called before FortCanningHill height");
        }
        return Res::Ok();
    }

    Res isPostFortCanningRoadFork() const {
        if(static_cast<int>(height) < consensus.FortCanningRoadHeight) {
            return Res::Err("called before FortCanningRoad height");
        }
        return Res::Ok();
    }

    Res isPostFortCanningEpilogueFork() const {
        if(static_cast<int>(height) < consensus.FortCanningEpilogueHeight) {
            return Res::Err("called before FortCanningEpilogue height");
        }
        return Res::Ok();
    }

    Res isPostGrandCentralFork() const {
        if(static_cast<int>(height) < consensus.GrandCentralHeight) {
            return Res::Err("called before GrandCentral height");
        }
        return Res::Ok();
    }

    template<typename T>
    Res serialize(T& obj) const {
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> obj;
        if (!ss.empty()) {
            return Res::Err("deserialization failed: excess %d bytes", ss.size());
        }
        return Res::Ok();
    }

public:
    CCustomMetadataParseVisitor(uint32_t height,
                                const Consensus::Params& consensus,
                                const std::vector<unsigned char>& metadata)
        : height(height), consensus(consensus), metadata(metadata) {}

    Res operator()(CCreateMasterNodeMessage& obj) const {
        return serialize(obj);
    }

    Res operator()(CResignMasterNodeMessage& obj) const {
        if (metadata.size() != sizeof(obj)) {
            return Res::Err("metadata must contain 32 bytes");
        }
        return serialize(obj);
    }

    Res operator()(CSetForcedRewardAddressMessage& obj) const {
        // Temporarily disabled for 2.2
        return Res::Err("reward address change is disabled for Fort Canning");

        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CRemForcedRewardAddressMessage& obj) const {
        // Temporarily disabled for 2.2
        return Res::Err("reward address change is disabled for Fort Canning");

        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CUpdateMasterNodeMessage& obj) const {
        // Temporarily disabled for 2.2
        return Res::Err("updatemasternode is disabled for Fort Canning");

        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CCreateTokenMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CUpdateTokenPreAMKMessage& obj) const {
        auto res = isPostAMKFork();
        if (!res) {
            return res;
        }
        if(isPostBayfrontFork()) {
            return Res::Err("called post Bayfront height");
        }
        return serialize(obj);
    }

    Res operator()(CUpdateTokenMessage& obj) const {
        auto res = isPostBayfrontFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CMintTokensMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CPoolSwapMessage& obj) const {
        auto res = isPostBayfrontFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLiquidityMessage& obj) const {
        auto res = isPostBayfrontFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CRemoveLiquidityMessage& obj) const {
        auto res = isPostBayfrontFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CUtxosToAccountMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CAccountToUtxosMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CAccountToAccountMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CAnyAccountsToAccountsMessage& obj) const {
        auto res = isPostBayfrontGardensFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CSmartContractMessage& obj) const {
        auto res = isPostFortCanningHillFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CFutureSwapMessage& obj) const {
        auto res = isPostFortCanningRoadFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CCreatePoolPairMessage& obj) const {
        auto res = isPostBayfrontFork();
        if (!res) {
            return res;
        }

        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> obj.poolPair;
        ss >> obj.pairSymbol;

        // Read custom pool rewards
        if (static_cast<int>(height) >= consensus.ClarkeQuayHeight && !ss.empty()) {
            ss >> obj.rewards;
        }
        if (!ss.empty()) {
            return Res::Err("deserialization failed: excess %d bytes", ss.size());
        }
        return Res::Ok();
    }

    Res operator()(CUpdatePoolPairMessage& obj) const {
        auto res = isPostBayfrontFork();
        if (!res) {
            return res;
        }

        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        // serialize poolId as raw integer
        ss >> obj.poolId.v;
        ss >> obj.status;
        ss >> obj.commission;
        ss >> obj.ownerAddress;

        // Read custom pool rewards
        if (static_cast<int>(height) >= consensus.ClarkeQuayHeight && !ss.empty()) {
            ss >> obj.rewards;
        }

        if (!ss.empty()) {
            return Res::Err("deserialization failed: excess %d bytes", ss.size());
        }
        return Res::Ok();
    }

    Res operator()(CGovernanceMessage& obj) const {
        auto res = isPostBayfrontFork();
        if (!res) {
            return res;
        }
        std::string name;
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        while(!ss.empty()) {
            ss >> name;
            auto var = GovVariable::Create(name);
            if (!var) {
                return Res::Err("'%s': variable is not registered", name);
            }
            ss >> *var;
            obj.govs.insert(std::move(var));
        }
        return Res::Ok();
    }

    Res operator()(CGovernanceHeightMessage& obj) const {
        auto res = isPostFortCanningFork();
        if (!res) {
            return res;
        }
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        std::string name;
        ss >> name;
        obj.govVar = GovVariable::Create(name);
        if (!obj.govVar) {
            return Res::Err("'%s': variable is not registered", name);
        }
        ss >> *obj.govVar;
        ss >> obj.startHeight;
        return Res::Ok();
    }

    Res operator()(CAppointOracleMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CRemoveOracleAppointMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CUpdateOracleAppointMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CSetOracleDataMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CICXCreateOrderMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CICXMakeOfferMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CICXSubmitDFCHTLCMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CICXSubmitEXTHTLCMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CICXClaimDFCHTLCMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CICXCloseOrderMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CICXCloseOfferMessage& obj) const {
        auto res = isPostEunosFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CPoolSwapMessageV2& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLoanSetCollateralTokenMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLoanSetLoanTokenMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLoanUpdateLoanTokenMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLoanSchemeMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CDefaultLoanSchemeMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CDestroyLoanSchemeMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CVaultMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CCloseVaultMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CUpdateVaultMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CDepositToVaultMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CWithdrawFromVaultMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CPaybackWithCollateralMessage& obj) const {
        auto res = isPostFortCanningEpilogueFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLoanTakeLoanMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLoanPaybackLoanMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLoanPaybackLoanV2Message& obj) const {
        auto res = isPostFortCanningRoadFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CAuctionBidMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

        Res operator()(CBurnTokensMessage& obj) const {
        auto res = isPostGrandCentralFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CCustomTxMessageNone&) const {
        return Res::Ok();
    }
};

class CCustomTxVisitor
{
protected:
    uint32_t height;
    CCustomCSView& mnview;
    const CTransaction& tx;
    const CCoinsViewCache& coins;
    const Consensus::Params& consensus;

public:
    CCustomTxVisitor(const CTransaction& tx,
                     uint32_t height,
                     const CCoinsViewCache& coins,
                     CCustomCSView& mnview,
                     const Consensus::Params& consensus)

        : height(height), mnview(mnview), tx(tx), coins(coins), consensus(consensus) {}

    bool HasAuth(const CScript& auth) const {
        for (const auto& input : tx.vin) {
            const Coin& coin = coins.AccessCoin(input.prevout);
            if (!coin.IsSpent() && coin.out.scriptPubKey == auth) {
                return true;
            }
        }
        return false;
    }

    Res HasCollateralAuth(const uint256& collateralTx) const {
        const Coin& auth = coins.AccessCoin(COutPoint(collateralTx, 1)); // always n=1 output
        if (!HasAuth(auth.out.scriptPubKey)) {
            return Res::Err("tx must have at least one input from the owner");
        }
        return Res::Ok();
    }

    Res HasFoundationAuth() const {
        for (const auto& input : tx.vin) {
            const Coin& coin = coins.AccessCoin(input.prevout);
            if (!coin.IsSpent() && consensus.foundationMembers.count(coin.out.scriptPubKey) > 0) {
                return Res::Ok();
            }
        }
        return Res::Err("tx not from foundation member");
    }

    Res CheckMasternodeCreationTx() const {
        if (tx.vout.size() < 2
        || tx.vout[0].nValue < GetMnCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0}
        || tx.vout[1].nValue != GetMnCollateralAmount(height) || tx.vout[1].nTokenId != DCT_ID{0}) {
            return Res::Err("malformed tx vouts (wrong creation fee or collateral amount)");
        }
        return Res::Ok();
    }

    Res CheckTokenCreationTx() const {
        if (tx.vout.size() < 2
        || tx.vout[0].nValue < GetTokenCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0}
        || tx.vout[1].nValue != GetTokenCollateralAmount() || tx.vout[1].nTokenId != DCT_ID{0}) {
            return Res::Err("malformed tx vouts (wrong creation fee or collateral amount)");
        }
        return Res::Ok();
    }

    Res CheckCustomTx() const {
        if (static_cast<int>(height) < consensus.EunosPayaHeight && tx.vout.size() != 2) {
            return Res::Err("malformed tx vouts ((wrong number of vouts)");
        }
        if (static_cast<int>(height) >= consensus.EunosPayaHeight && tx.vout[0].nValue != 0) {
            return Res::Err("malformed tx vouts, first vout must be OP_RETURN vout with value 0");
        }
        return Res::Ok();
    }

    Res TransferTokenBalance(DCT_ID id, CAmount amount, CScript const & from, CScript const & to) const {
        assert(!from.empty() || !to.empty());

        CTokenAmount tokenAmount{id, amount};
        // if "from" not supplied it will only add balance on "to" address
        if (!from.empty()) {
            auto res = mnview.SubBalance(from, tokenAmount);
            if (!res)
                return res;
        }

        // if "to" not supplied it will only sub balance from "form" address
        if (!to.empty()) {
            auto res = mnview.AddBalance(to,tokenAmount);
            if (!res)
                return res;
        }

        return Res::Ok();
    }

    DCT_ID FindTokenByPartialSymbolName(const std::string& symbol) const {
        DCT_ID res{0};
        mnview.ForEachToken([&](DCT_ID id, CTokenImplementation token) {
            if (token.symbol.find(symbol) == 0) {
                res = id;
                return false;
            }
            return true;
        }, DCT_ID{1});
        assert(res.v != 0);
        return res;
    }

    CPoolPair GetBTCDFIPoolPair() const {
        auto BTC = FindTokenByPartialSymbolName(CICXOrder::TOKEN_BTC);
        auto pair = mnview.GetPoolPair(BTC, DCT_ID{0});
        assert(pair);
        return std::move(pair->second);
    }

    CAmount GetDFIperBTC() const {
        auto BTCDFIPoolPair = GetBTCDFIPoolPair();
        if (BTCDFIPoolPair.idTokenA == DCT_ID({0}))
            return (arith_uint256(BTCDFIPoolPair.reserveA) * arith_uint256(COIN) / BTCDFIPoolPair.reserveB).GetLow64();
        return (arith_uint256(BTCDFIPoolPair.reserveB) * arith_uint256(COIN) / BTCDFIPoolPair.reserveA).GetLow64();
    }

    CAmount CalculateTakerFee(CAmount amount) const {
        return (arith_uint256(amount) * arith_uint256(mnview.ICXGetTakerFeePerBTC()) / arith_uint256(COIN)
              * arith_uint256(GetDFIperBTC()) / arith_uint256(COIN)).GetLow64();
    }

    ResVal<CScript> MintableToken(DCT_ID id, const CTokenImplementation& token) const {
        if (token.destructionTx != uint256{}) {
            return Res::Err("token %s already destroyed at height %i by tx %s", token.symbol,
                            token.destructionHeight, token.destructionTx.GetHex());
        }
        const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output

        // pre-bayfront logic:
        if (static_cast<int>(height) < consensus.BayfrontHeight) {
            if (id < CTokensView::DCT_ID_START) {
                return Res::Err("token %s is a 'stable coin', can't mint stable coin!", id.ToString());
            }

            if (!HasAuth(auth.out.scriptPubKey)) {
                return Res::Err("tx must have at least one input from token owner");
            }
            return {auth.out.scriptPubKey, Res::Ok()};
        }

        if (id == DCT_ID{0}) {
            return Res::Err("can't mint default DFI coin!");
        }

        if (token.IsPoolShare()) {
            return Res::Err("can't mint LPS token %s!", id.ToString());
        }

        static const auto isMainNet = Params().NetworkIDString() == CBaseChainParams::MAIN;
        // may be different logic with LPS, so, dedicated check:
        if (!token.IsMintable() || (isMainNet && mnview.GetLoanTokenByID(id))) {
            return Res::Err("token %s is not mintable!", id.ToString());
        }

        if (!HasAuth(auth.out.scriptPubKey)) { // in the case of DAT, it's ok to do not check foundation auth cause exact DAT owner is foundation member himself
            if (!token.IsDAT()) {
                return Res::Err("tx must have at least one input from token owner");
            } else if (!HasFoundationAuth()) { // Is a DAT, check founders auth
                if (height < static_cast<uint32_t>(consensus.GrandCentralHeight))
                    return Res::Err("token is DAT and tx not from foundation member");
            }
        }

        return {auth.out.scriptPubKey, Res::Ok()};
    }

    Res eraseEmptyBalances(TAmounts& balances) const {
        for (auto it = balances.begin(), next_it = it; it != balances.end(); it = next_it) {
            ++next_it;

            auto token = mnview.GetToken(it->first);
            if (!token) {
                return Res::Err("reward token %d does not exist!", it->first.v);
            }

            if (it->second == 0) {
                balances.erase(it);
            }
        }
        return Res::Ok();
    }

    Res setShares(const CScript& owner, const TAmounts& balances) const {
        for (const auto& balance : balances) {
            auto token = mnview.GetToken(balance.first);
            if (token && token->IsPoolShare()) {
                const auto bal = mnview.GetBalance(owner, balance.first);
                if (bal.nValue == balance.second) {
                    auto res = mnview.SetShare(balance.first, owner, height);
                    if (!res) {
                        return res;
                    }
                }
            }
        }
        return Res::Ok();
    }

    Res delShares(const CScript& owner, const TAmounts& balances) const {
        for (const auto& kv : balances) {
            auto token = mnview.GetToken(kv.first);
            if (token && token->IsPoolShare()) {
                const auto balance = mnview.GetBalance(owner, kv.first);
                if (balance.nValue == 0) {
                    auto res = mnview.DelShare(kv.first, owner);
                    if (!res) {
                        return res;
                    }
                }
            }
        }
        return Res::Ok();
    }

    // we need proxy view to prevent add/sub balance record
    void CalculateOwnerRewards(const CScript& owner) const {
        CCustomCSView view(mnview);
        view.CalculateOwnerRewards(owner, height);
        view.Flush();
    }

    Res subBalanceDelShares(const CScript& owner, const CBalances& balance) const {
        CalculateOwnerRewards(owner);
        auto res = mnview.SubBalances(owner, balance);
        if (!res) {
            return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, res.msg);
        }
        return delShares(owner, balance.balances);
    }

    Res addBalanceSetShares(const CScript& owner, const CBalances& balance) const {
        CalculateOwnerRewards(owner);
        auto res = mnview.AddBalances(owner, balance);
        return !res ? res : setShares(owner, balance.balances);
    }

    Res addBalancesSetShares(const CAccounts& accounts) const {
        for (const auto& account : accounts) {
            auto res = addBalanceSetShares(account.first, account.second);
            if (!res) {
                return res;
            }
        }
        return Res::Ok();
    }

    Res subBalancesDelShares(const CAccounts& accounts) const {
        for (const auto& account : accounts) {
            auto res = subBalanceDelShares(account.first, account.second);
            if (!res) {
                return res;
            }
        }
        return Res::Ok();
    }

    Res normalizeTokenCurrencyPair(std::set<CTokenCurrencyPair>& tokenCurrency) const {
        std::set<CTokenCurrencyPair> trimmed;
        for (const auto& pair : tokenCurrency) {
            auto token = trim_ws(pair.first).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
            auto currency = trim_ws(pair.second).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
            if (token.empty() || currency.empty()) {
                return Res::Err("empty token / currency");
            }
            trimmed.emplace(token, currency);
        }
        tokenCurrency = std::move(trimmed);
        return Res::Ok();
    }
    bool IsTokensMigratedToGovVar() const {
        return static_cast<int>(height) > consensus.FortCanningCrunchHeight + 1;
    }
};

class CCustomTxApplyVisitor : public CCustomTxVisitor
{
    uint64_t time;
    uint32_t txn;
public:
    CCustomTxApplyVisitor(const CTransaction& tx,
                          uint32_t height,
                          const CCoinsViewCache& coins,
                          CCustomCSView& mnview,
                          const Consensus::Params& consensus,
                          uint64_t time,
                          uint32_t txn)

        : CCustomTxVisitor(tx, height, coins, mnview, consensus), time(time), txn(txn) {}

    Res operator()(const CCreateMasterNodeMessage& obj) const {
        auto res = CheckMasternodeCreationTx();
        if (!res) {
            return res;
        }

        if (height >= static_cast<uint32_t>(Params().GetConsensus().EunosHeight) && !HasAuth(tx.vout[1].scriptPubKey)) {
            return Res::Err("masternode creation needs owner auth");
        }

        if (height >= static_cast<uint32_t>(Params().GetConsensus().EunosPayaHeight)) {
            switch(obj.timelock) {
                case CMasternode::ZEROYEAR:
                case CMasternode::FIVEYEAR:
                case CMasternode::TENYEAR:
                    break;
                default:
                    return Res::Err("Timelock must be set to either 0, 5 or 10 years");
            }
        } else if (obj.timelock != 0) {
            return Res::Err("collateral timelock cannot be set below EunosPaya");
        }

        CMasternode node;
        CTxDestination dest;
        if (ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
            if (dest.index() == PKHashType) {
                node.ownerType = 1;
                node.ownerAuthAddress = CKeyID(std::get<PKHash>(dest));
            } else if (dest.index() == WitV0KeyHashType) {
                node.ownerType = 4;
                node.ownerAuthAddress = CKeyID(std::get<WitnessV0KeyHash>(dest));
            }
        }
        node.creationHeight = height;
        node.operatorType = obj.operatorType;
        node.operatorAuthAddress = obj.operatorAuthAddress;

        // Set masternode version2 after FC for new serialisation
        if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHeight)) {
            node.version = CMasternode::VERSION0;
        }

        res = mnview.CreateMasternode(tx.GetHash(), node, obj.timelock);
        // Build coinage from the point of masternode creation
        if (res) {
            if (height >= static_cast<uint32_t>(Params().GetConsensus().EunosPayaHeight)) {
                for (uint8_t i{0}; i < SUBNODE_COUNT; ++i) {
                    mnview.SetSubNodesBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), i, time);
                }
            } else if (height >= static_cast<uint32_t>(Params().GetConsensus().DakotaCrescentHeight)) {
                mnview.SetMasternodeLastBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), time);
            }
        }
        return res;
    }

    Res operator()(const CResignMasterNodeMessage& obj) const {
        auto res = HasCollateralAuth(obj);
        return !res ? res : mnview.ResignMasternode(obj, tx.GetHash(), height);
    }

    Res operator()(const CSetForcedRewardAddressMessage& obj) const {
        // Temporarily disabled for 2.2
        return Res::Err("reward address change is disabled for Fort Canning");

        auto const node = mnview.GetMasternode(obj.nodeId);
        if (!node) {
            return Res::Err("masternode %s does not exist", obj.nodeId.ToString());
        }
        if (!HasCollateralAuth(obj.nodeId)) {
            return Res::Err("%s: %s", obj.nodeId.ToString(), "tx must have at least one input from masternode owner");
        }

        return mnview.SetForcedRewardAddress(obj.nodeId, obj.rewardAddressType, obj.rewardAddress, height);
    }

    Res operator()(const CRemForcedRewardAddressMessage& obj) const {
        // Temporarily disabled for 2.2
        return Res::Err("reward address change is disabled for Fort Canning");

        auto const node = mnview.GetMasternode(obj.nodeId);
        if (!node) {
            return Res::Err("masternode %s does not exist", obj.nodeId.ToString());
        }
        if (!HasCollateralAuth(obj.nodeId)) {
            return Res::Err("%s: %s", obj.nodeId.ToString(), "tx must have at least one input from masternode owner");
        }

        return mnview.RemForcedRewardAddress(obj.nodeId, height);
    }

    Res operator()(const CUpdateMasterNodeMessage& obj) const {
        // Temporarily disabled for 2.2
        return Res::Err("updatemasternode is disabled for Fort Canning");

        auto res = HasCollateralAuth(obj.mnId);
        return !res ? res : mnview.UpdateMasternode(obj.mnId, obj.operatorType, obj.operatorAuthAddress, height);
    }

    Res operator()(const CCreateTokenMessage& obj) const {
        auto res = CheckTokenCreationTx();
        if (!res) {
            return res;
        }

        CTokenImplementation token;
        static_cast<CToken&>(token) = obj;

        token.symbol = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        token.name = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.creationTx = tx.GetHash();
        token.creationHeight = height;

        //check foundation auth
        if (token.IsDAT() && !HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }

        if (static_cast<int>(height) >= consensus.BayfrontHeight) { // formal compatibility if someone cheat and create LPS token on the pre-bayfront node
            if (token.IsPoolShare()) {
                return Res::Err("Cant't manually create 'Liquidity Pool Share' token; use poolpair creation");
            }
        }

        return mnview.CreateToken(token, static_cast<int>(height) < consensus.BayfrontHeight);
    }

    Res operator()(const CUpdateTokenPreAMKMessage& obj) const {
        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        if (!pair) {
            return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());
        }
        auto token = pair->second;

        //check foundation auth
        auto res = HasFoundationAuth();

        if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
            token.flags ^= (uint8_t)CToken::TokenFlags::DAT;
            return !res ? res : mnview.UpdateToken(token, true);
        }
        return res;
    }

    Res operator()(const CUpdateTokenMessage& obj) const {
        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        if (!pair) {
            return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());
        }
        if (pair->first == DCT_ID{0}) {
            return Res::Err("Can't alter DFI token!"); // may be redundant cause DFI is 'finalized'
        }

        if (mnview.AreTokensLocked({pair->first.v})) {
            return Res::Err("Cannot update token during lock");
        }

        const auto& token = pair->second;

        // need to check it exectly here cause lps has no collateral auth (that checked next)
        if (token.IsPoolShare()) {
            return Res::Err("token %s is the LPS token! Can't alter pool share's tokens!", obj.tokenTx.ToString());
        }

        // check auth, depends from token's "origins"
        const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output
        bool isFoundersToken = consensus.foundationMembers.count(auth.out.scriptPubKey) > 0;

        auto res = Res::Ok();
        if (isFoundersToken && !(res = HasFoundationAuth())) {
            return res;
        } else if (!(res = HasCollateralAuth(token.creationTx))) {
            return res;
        }

        // Check for isDAT change in non-foundation token after set height
        if (static_cast<int>(height) >= consensus.BayfrontMarinaHeight) {
            //check foundation auth
            if (obj.token.IsDAT() != token.IsDAT() && !HasFoundationAuth()) { //no need to check Authority if we don't create isDAT
                return Res::Err("can't set isDAT to true, tx not from foundation member");
            }
        }

        CTokenImplementation updatedToken{obj.token};
        updatedToken.creationTx = token.creationTx;
        updatedToken.destructionTx = token.destructionTx;
        updatedToken.destructionHeight = token.destructionHeight;
        if (height >= static_cast<uint32_t>(consensus.FortCanningHeight)) {
            updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        }

        return mnview.UpdateToken(updatedToken);
    }

    Res operator()(const CMintTokensMessage& obj) const {
        // check auth and increase balance of token's owner
        for (const auto& [tokenId, amount] : obj.balances) {
            if (Params().NetworkIDString() == CBaseChainParams::MAIN &&
                height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) &&
                mnview.GetLoanTokenByID(tokenId)) {
                return Res::Err("Loan tokens cannot be minted");
            }

            auto token = mnview.GetToken(tokenId);
            if (!token)
                return Res::Err("token %s does not exist!", tokenId.ToString());

            auto mintable = MintableToken(tokenId, *token);
            if (!mintable)
                return std::move(mintable);


            if (height >= static_cast<uint32_t>(consensus.GrandCentralHeight) && token->IsDAT() && !HasFoundationAuth())
            {
                auto attributes = mnview.GetAttributes();
                assert(attributes);

                CDataStructureV0 enableKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::ConsortiumEnabled};
                if (attributes->GetValue(enableKey, false))
                {
                    mintable.ok = false;

                    CDataStructureV0 membersKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::Members};
                    const auto members = attributes->GetValue(membersKey, CConsortiumMembers{});

                    CDataStructureV0 membersMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
                    auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});

                    for (auto const& [key, member] : members)
                    {
                        if (HasAuth(member.ownerAddress))
                        {
                            if (member.status != CConsortiumMember::Status::Active)
                                return Res::Err("Cannot mint token, not an active member of consortium for %s!", token->symbol);

                            auto add = SafeAdd(membersBalances[tokenId][key].minted, amount);
                            if (!add)
                                return (std::move(add));
                            membersBalances[tokenId][key].minted = add;

                            const auto dailyInterval = height / consensus.blocksPerDay() * consensus.blocksPerDay();
                            if (dailyInterval == membersBalances[tokenId][key].dailyMinted.first) {
                                add = SafeAdd(membersBalances[tokenId][key].dailyMinted.second, amount);
                                if (!add)
                                    return (std::move(add));
                                membersBalances[tokenId][key].dailyMinted.second = add;
                            } else {
                                membersBalances[tokenId][key].dailyMinted.first = dailyInterval;
                                membersBalances[tokenId][key].dailyMinted.second = amount;
                            }

                            if (membersBalances[tokenId][key].minted > member.mintLimit)
                                return Res::Err("You will exceed your maximum mint limit for %s token by minting this amount!", token->symbol);

                            if (membersBalances[tokenId][key].dailyMinted.second > member.dailyMintLimit) {
                                return Res::Err("You will exceed your daily mint limit for %s token by minting this amount", token->symbol);
                            }

                            *mintable.val = member.ownerAddress;
                            mintable.ok = true;
                            break;
                        }
                    }

                    if (!mintable)
                        return Res::Err("You are not a foundation or consortium member and cannot mint this token!");

                    CDataStructureV0 maxLimitKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::MintLimit};
                    const auto maxLimit = attributes->GetValue(maxLimitKey, CAmount{0});

                    CDataStructureV0 consortiumMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
                    auto globalBalances = attributes->GetValue(consortiumMintedKey, CConsortiumGlobalMinted{});

                    auto add = SafeAdd(globalBalances[tokenId].minted, amount);
                    if (!add)
                        return (std::move(add));

                    globalBalances[tokenId].minted = add;

                    if (globalBalances[tokenId].minted > maxLimit)
                        return Res::Err("You will exceed global maximum consortium mint limit for %s token by minting this amount!", token->symbol);

                    attributes->SetValue(consortiumMintedKey, globalBalances);
                    attributes->SetValue(membersMintedKey, membersBalances);

                    auto saved = mnview.SetVariable(*attributes);
                    if (!saved)
                        return saved;
                }
                else
                    return Res::Err("You are not a foundation member and cannot mint this token!");
            }

            auto minted = mnview.AddMintedTokens(tokenId, amount);
            if (!minted)
                return minted;

            CalculateOwnerRewards(*mintable.val);
            auto res = mnview.AddBalance(*mintable.val, CTokenAmount{tokenId, amount});
            if (!res)
                return res;
        }

        return Res::Ok();
    }

    Res operator()(const CBurnTokensMessage& obj) const {
        if (obj.amounts.balances.empty()) {
            return Res::Err("tx must have balances to burn");
        }

        for (const auto& [tokenId, amount] : obj.amounts.balances)
        {
            // check auth
            if (!HasAuth(obj.from))
                return Res::Err("tx must have at least one input from account owner");

            auto subMinted = mnview.SubMintedTokens(tokenId, amount);
            if (!subMinted)
                return subMinted;

            if (obj.burnType != CBurnTokensMessage::BurnType::TokenBurn)
                return Res::Err("Currently only burn type 0 - TokenBurn is supported!");

            CScript ownerAddress;

            if (auto address = std::get_if<CScript>(&obj.context); address && !address->empty())
                ownerAddress = *address;
            else ownerAddress = obj.from;

            auto attributes = mnview.GetAttributes();
            if (!attributes)
                return Res::Err("Cannot read from attributes gov variable!");

            CDataStructureV0 membersKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::Members};
            const auto members = attributes->GetValue(membersKey, CConsortiumMembers{});
            CDataStructureV0 membersMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
            auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});
            CDataStructureV0 consortiumMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
            auto globalBalances = attributes->GetValue(consortiumMintedKey, CConsortiumGlobalMinted{});

            bool setVariable = false;
            for (auto const& tmp : members)
                if (tmp.second.ownerAddress == ownerAddress)
                {
                    auto add = SafeAdd(membersBalances[tokenId][tmp.first].burnt, amount);
                    if (!add)
                        return (std::move(add));

                    membersBalances[tokenId][tmp.first].burnt = add;

                    add = SafeAdd(globalBalances[tokenId].burnt, amount);
                    if (!add)
                        return (std::move(add));

                    globalBalances[tokenId].burnt = add;

                    setVariable = true;
                    break;
                }

            if (setVariable)
            {
                attributes->SetValue(membersMintedKey, membersBalances);
                attributes->SetValue(consortiumMintedKey, globalBalances);

                auto saved = mnview.SetVariable(*attributes);
                if (!saved)
                    return saved;
            }

            CalculateOwnerRewards(obj.from);

            auto res = TransferTokenBalance(tokenId, amount, obj.from, consensus.burnAddress);
            if (!res)
                return res;
        }

        return Res::Ok();

    }

    Res operator()(const CCreatePoolPairMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        if (obj.poolPair.commission < 0 || obj.poolPair.commission > COIN) {
            return Res::Err("wrong commission");
        }

        if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningCrunchHeight) && obj.pairSymbol.find('/') != std::string::npos) {
            return Res::Err("token symbol should not contain '/'");
        }

        /// @todo ownerAddress validity checked only in rpc. is it enough?
        CPoolPair poolPair(obj.poolPair);
        auto pairSymbol = obj.pairSymbol;
        poolPair.creationTx = tx.GetHash();
        poolPair.creationHeight = height;
        auto& rewards = poolPair.rewards;

        auto tokenA = mnview.GetToken(poolPair.idTokenA);
        if (!tokenA) {
            return Res::Err("token %s does not exist!", poolPair.idTokenA.ToString());
        }

        auto tokenB = mnview.GetToken(poolPair.idTokenB);
        if (!tokenB) {
            return Res::Err("token %s does not exist!", poolPair.idTokenB.ToString());
        }

        const auto symbolLength = height >= static_cast<uint32_t>(consensus.FortCanningHeight) ? CToken::MAX_TOKEN_POOLPAIR_LENGTH : CToken::MAX_TOKEN_SYMBOL_LENGTH;
        if (pairSymbol.empty()) {
            pairSymbol = trim_ws(tokenA->symbol + "-" + tokenB->symbol).substr(0, symbolLength);
        } else {
            pairSymbol = trim_ws(pairSymbol).substr(0, symbolLength);
        }

        CTokenImplementation token;
        token.flags = (uint8_t)CToken::TokenFlags::DAT |
                      (uint8_t)CToken::TokenFlags::LPS |
                      (uint8_t)CToken::TokenFlags::Tradeable |
                      (uint8_t)CToken::TokenFlags::Finalized;

        token.name = trim_ws(tokenA->name + "-" + tokenB->name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.symbol = pairSymbol;
        token.creationTx = tx.GetHash();
        token.creationHeight = height;

        auto tokenId = mnview.CreateToken(token);
        if (!tokenId) {
            return std::move(tokenId);
        }

        rewards = obj.rewards;
        if (!rewards.balances.empty()) {
            // Check tokens exist and remove empty reward amounts
            auto res = eraseEmptyBalances(rewards.balances);
            if (!res) {
                return res;
            }
        }

        return mnview.SetPoolPair(tokenId, height, poolPair);
    }

    Res operator()(const CUpdatePoolPairMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }

        auto rewards = obj.rewards;
        if (!rewards.balances.empty()) {
            // Check for special case to wipe rewards
            if (!(rewards.balances.size() == 1 && rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()}
            && rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max())) {
                // Check if tokens exist and remove empty reward amounts
                auto res = eraseEmptyBalances(rewards.balances);
                if (!res) {
                    return res;
                }
            }
        }
        return mnview.UpdatePoolPair(obj.poolId, height, obj.status, obj.commission, obj.ownerAddress, rewards);
    }

    Res operator()(const CPoolSwapMessage& obj) const {
        // check auth
        if (!HasAuth(obj.from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        return CPoolSwap(obj, height).ExecuteSwap(mnview, {});
    }

    Res operator()(const CPoolSwapMessageV2& obj) const {
        // check auth
        if (!HasAuth(obj.swapInfo.from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight) && obj.poolIDs.size() > 3) {
            return Res::Err(strprintf("Too many pool IDs provided, max 3 allowed, %d provided", obj.poolIDs.size()));
        }

        return CPoolSwap(obj.swapInfo, height).ExecuteSwap(mnview, obj.poolIDs);
    }

    Res operator()(const CLiquidityMessage& obj) const {
        CBalances sumTx = SumAllTransfers(obj.from);
        if (sumTx.balances.size() != 2) {
            return Res::Err("the pool pair requires two tokens");
        }

        std::pair<DCT_ID, CAmount> amountA = *sumTx.balances.begin();
        std::pair<DCT_ID, CAmount> amountB = *(std::next(sumTx.balances.begin(), 1));

        // checked internally too. remove here?
        if (amountA.second <= 0 || amountB.second <= 0) {
            return Res::Err("amount cannot be less than or equal to zero");
        }

        auto pair = mnview.GetPoolPair(amountA.first, amountB.first);
        if (!pair) {
            return Res::Err("there is no such pool pair");
        }

        for (const auto& kv : obj.from) {
            if (!HasAuth(kv.first)) {
                return Res::Err("tx must have at least one input from account owner");
            }
        }

        for (const auto& kv : obj.from) {
            CalculateOwnerRewards(kv.first);
            auto res = mnview.SubBalances(kv.first, kv.second);
            if (!res) {
                return res;
            }
        }

        const auto& lpTokenID = pair->first;
        auto& pool = pair->second;

        // normalize A & B to correspond poolpair's tokens
        if (amountA.first != pool.idTokenA) {
            std::swap(amountA, amountB);
        }

        bool slippageProtection = static_cast<int>(height) >= consensus.BayfrontMarinaHeight;
        auto res = pool.AddLiquidity(amountA.second, amountB.second, [&] /*onMint*/(CAmount liqAmount) {

            CBalances balance{TAmounts{{lpTokenID, liqAmount}}};
            return addBalanceSetShares(obj.shareAddress, balance);
        }, slippageProtection);

        return !res ? res : mnview.SetPoolPair(lpTokenID, height, pool);
    }

    Res operator()(const CRemoveLiquidityMessage& obj) const {
        const auto& from = obj.from;
        auto amount = obj.amount;

        // checked internally too. remove here?
        if (amount.nValue <= 0) {
            return Res::Err("amount cannot be less than or equal to zero");
        }

        auto pair = mnview.GetPoolPair(amount.nTokenId);
        if (!pair) {
            return Res::Err("there is no such pool pair");
        }

        if (!HasAuth(from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        CPoolPair& pool = pair.value();

        // subtract liq.balance BEFORE RemoveLiquidity call to check balance correctness
        {
            CBalances balance{TAmounts{{amount.nTokenId, amount.nValue}}};
            auto res = subBalanceDelShares(from, balance);
            if (!res) {
                return res;
            }
        }

        auto res = pool.RemoveLiquidity(amount.nValue, [&] (CAmount amountA, CAmount amountB) {

            CalculateOwnerRewards(from);
            CBalances balances{TAmounts{{pool.idTokenA, amountA}, {pool.idTokenB, amountB}}};
            return mnview.AddBalances(from, balances);
        });

        return !res ? res : mnview.SetPoolPair(amount.nTokenId, height, pool);
    }

    Res operator()(const CUtxosToAccountMessage& obj) const {
        // check enough tokens are "burnt"
        const auto burnt = BurntTokens(tx);
        if (!burnt) {
            return burnt;
        }

        const auto mustBeBurnt = SumAllTransfers(obj.to);
        if (*burnt.val != mustBeBurnt) {
            return Res::Err("transfer tokens mismatch burnt tokens: (%s) != (%s)", mustBeBurnt.ToString(), burnt.val->ToString());
        }

        // transfer
        return addBalancesSetShares(obj.to);
    }

    Res operator()(const CAccountToUtxosMessage& obj) const {
        // check auth
        if (!HasAuth(obj.from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        // check that all tokens are minted, and no excess tokens are minted
        auto minted = MintedTokens(tx, obj.mintingOutputsStart);
        if (!minted) {
            return std::move(minted);
        }

        if (obj.balances != *minted.val) {
            return Res::Err("amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)", minted.val->ToString(), obj.balances.ToString());
        }

        // block for non-DFI transactions
        for (const auto& kv : obj.balances.balances) {
            const DCT_ID& tokenId = kv.first;
            if (tokenId != DCT_ID{0}) {
                return Res::Err("only available for DFI transactions");
            }
        }

        // transfer
        return subBalanceDelShares(obj.from, obj.balances);
    }

    Res operator()(const CAccountToAccountMessage& obj) const {
        // check auth
        if (!HasAuth(obj.from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        // transfer
        auto res = subBalanceDelShares(obj.from, SumAllTransfers(obj.to));
        return !res ? res : addBalancesSetShares(obj.to);
    }


    Res HandleDFIP2201Contract(const CSmartContractMessage& obj) const {
        const auto attributes = mnview.GetAttributes();
        if (!attributes)
            return Res::Err("Attributes unavailable");

        CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Active};

        if (!attributes->GetValue(activeKey, false))
            return Res::Err("DFIP2201 smart contract is not enabled");

        if (obj.name != SMART_CONTRACT_DFIP_2201)
            return Res::Err("DFIP2201 contract mismatch - got: " + obj.name);

        if (obj.accounts.size() != 1)
            return Res::Err("Only one address entry expected for " + obj.name);

        if (obj.accounts.begin()->second.balances.size() != 1)
            return Res::Err("Only one amount entry expected for " + obj.name);

        const auto& script = obj.accounts.begin()->first;
        if (!HasAuth(script))
            return Res::Err("Must have at least one input from supplied address");

        const auto& id = obj.accounts.begin()->second.balances.begin()->first;
        const auto& amount = obj.accounts.begin()->second.balances.begin()->second;

        if (amount <= 0)
            return Res::Err("Amount out of range");

        CDataStructureV0 minSwapKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::MinSwap};
        auto minSwap = attributes->GetValue(minSwapKey, CAmount{0});

        if (minSwap && amount < minSwap) {
            return Res::Err("Below minimum swapable amount, must be at least " + GetDecimaleString(minSwap) + " BTC");
        }

        const auto token = mnview.GetToken(id);
        if (!token)
            return Res::Err("Specified token not found");

        if (token->symbol != "BTC" || token->name != "Bitcoin" || !token->IsDAT())
            return Res::Err("Only Bitcoin can be swapped in " + obj.name);

        auto res = mnview.SubBalance(script, {id, amount});
        if (!res)
            return res;

        const CTokenCurrencyPair btcUsd{"BTC","USD"};
        const CTokenCurrencyPair dfiUsd{"DFI","USD"};


        bool useNextPrice{false}, requireLivePrice{true};
        auto resVal = mnview.GetValidatedIntervalPrice(btcUsd, useNextPrice, requireLivePrice);
        if (!resVal)
            return std::move(resVal);

        CDataStructureV0 premiumKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Premium};
        auto premium = attributes->GetValue(premiumKey, CAmount{2500000});

        const auto& btcPrice = MultiplyAmounts(*resVal.val, premium + COIN);

        resVal = mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice);
        if (!resVal)
            return std::move(resVal);

        const auto totalDFI = MultiplyAmounts(DivideAmounts(btcPrice, *resVal.val), amount);

        res = mnview.SubBalance(Params().GetConsensus().smartContracts.begin()->second, {{0}, totalDFI});
        if (!res)
            return res;

        res = mnview.AddBalance(script, {{0}, totalDFI});
        if (!res)
            return res;

        return Res::Ok();
    }

    Res operator()(const CSmartContractMessage& obj) const {
        if (obj.accounts.empty()) {
            return Res::Err("Contract account parameters missing");
        }
        auto contracts = Params().GetConsensus().smartContracts;

        auto contract = contracts.find(obj.name);
        if (contract == contracts.end())
            return Res::Err("Specified smart contract not found");

        // Convert to switch when it's long enough.
        if (obj.name == SMART_CONTRACT_DFIP_2201)
            return HandleDFIP2201Contract(obj);

        return Res::Err("Specified smart contract not found");
    }

    Res operator()(const CFutureSwapMessage& obj) const {
        if (!HasAuth(obj.owner)) {
            return Res::Err("Transaction must have at least one input from owner");
        }

        const auto attributes = mnview.GetAttributes();
        if (!attributes) {
            return Res::Err("Attributes unavailable");
        }

        bool dfiToDUSD = !obj.source.nTokenId.v;
        const auto paramID = dfiToDUSD ? ParamIDs::DFIP2206F : ParamIDs::DFIP2203;

        CDataStructureV0 activeKey{AttributeTypes::Param, paramID, DFIPKeys::Active};
        CDataStructureV0 blockKey{AttributeTypes::Param, paramID, DFIPKeys::BlockPeriod};
        CDataStructureV0 rewardKey{AttributeTypes::Param, paramID, DFIPKeys::RewardPct};
        if (!attributes->GetValue(activeKey, false) ||
            !attributes->CheckKey(blockKey) ||
            !attributes->CheckKey(rewardKey)) {
            return Res::Err("%s not currently active", dfiToDUSD ? "DFIP2206F" : "DFIP2203");
        }

        CDataStructureV0 startKey{AttributeTypes::Param, paramID, DFIPKeys::StartBlock};
        if (const auto startBlock = attributes->GetValue(startKey, CAmount{}); height < startBlock) {
            return Res::Err("%s not active until block %d", dfiToDUSD ? "DFIP2206F" : "DFIP2203", startBlock);
        }

        if (obj.source.nValue <= 0) {
            return Res::Err("Source amount must be more than zero");
        }

        const auto source = mnview.GetLoanTokenByID(obj.source.nTokenId);
        if (!dfiToDUSD && !source) {
            return Res::Err("Could not get source loan token %d", obj.source.nTokenId.v);
        }

        if (!dfiToDUSD && source->symbol == "DUSD") {
            CDataStructureV0 tokenKey{AttributeTypes::Token, obj.destination, TokenKeys::DFIP2203Enabled};
            const auto enabled = attributes->GetValue(tokenKey, true);
            if (!enabled) {
                return Res::Err("DFIP2203 currently disabled for token %d", obj.destination);
            }

            const auto loanToken = mnview.GetLoanTokenByID({obj.destination});
            if (!loanToken) {
                return Res::Err("Could not get destination loan token %d. Set valid destination.", obj.destination);
            }

            if (mnview.AreTokensLocked({obj.destination})) {
                return Res::Err("Cannot create future swap for locked token");
            }
        } else {
            if (!dfiToDUSD) {
                if (obj.destination != 0) {
                    return Res::Err("Destination should not be set when source amount is dToken or DFI");
                }

                if (mnview.AreTokensLocked({obj.source.nTokenId.v})) {
                    return Res::Err("Cannot create future swap for locked token");
                }

                CDataStructureV0 tokenKey{AttributeTypes::Token, obj.source.nTokenId.v, TokenKeys::DFIP2203Enabled};
                const auto enabled = attributes->GetValue(tokenKey, true);
                if (!enabled) {
                    return Res::Err("DFIP2203 currently disabled for token %s", obj.source.nTokenId.ToString());
                }
            } else {
                DCT_ID id{};
                const auto token = mnview.GetTokenGuessId("DUSD", id);
                if (!token) {
                    return Res::Err("No DUSD token defined");
                }

                if (!mnview.GetFixedIntervalPrice({"DFI", "USD"})) {
                    return Res::Err("DFI / DUSD fixed interval price not found");
                }

                if (obj.destination != id.v) {
                    return Res::Err("Incorrect destination defined for DFI swap, DUSD destination expected id: %d", id.v);
                }
            }
        }

        const auto contractType = dfiToDUSD ? SMART_CONTRACT_DFIP2206F : SMART_CONTRACT_DFIP_2203;
        const auto contractAddressValue = GetFutureSwapContractAddress(contractType);
        if (!contractAddressValue) {
            return contractAddressValue;
        }

        const auto economyKey = dfiToDUSD ? EconomyKeys::DFIP2206FCurrent : EconomyKeys::DFIP2203Current;
        CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, economyKey};
        auto balances = attributes->GetValue(liveKey, CBalances{});

        if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight)) {
            CalculateOwnerRewards(obj.owner);
        }

        if (obj.withdraw) {

            CTokenAmount totalFutures{};
            totalFutures.nTokenId = obj.source.nTokenId;

            if (!dfiToDUSD) {
                std::map<CFuturesUserKey, CFuturesUserValue> userFuturesValues;

                mnview.ForEachFuturesUserValues([&](const CFuturesUserKey& key, const CFuturesUserValue& futuresValues) {
                    if (key.owner == obj.owner &&
                        futuresValues.source.nTokenId == obj.source.nTokenId &&
                        futuresValues.destination == obj.destination) {
                        userFuturesValues[key] = futuresValues;
                    }
                    return true;
                }, {height, obj.owner, std::numeric_limits<uint32_t>::max()});

                for (const auto& [key, value] : userFuturesValues) {
                    totalFutures.Add(value.source.nValue);
                    mnview.EraseFuturesUserValues(key);
                }
            } else {
                std::map<CFuturesUserKey, CAmount> userFuturesValues;

                mnview.ForEachFuturesDUSD([&](const CFuturesUserKey& key, const CAmount& futuresValues) {
                    if (key.owner == obj.owner) {
                        userFuturesValues[key] = futuresValues;
                    }
                    return true;
                }, {height, obj.owner, std::numeric_limits<uint32_t>::max()});

                for (const auto& [key, amount] : userFuturesValues) {
                    totalFutures.Add(amount);
                    mnview.EraseFuturesDUSD(key);
                }
            }

            auto res = totalFutures.Sub(obj.source.nValue);
            if (!res) {
                return res;
            }

            if (totalFutures.nValue > 0) {
                Res res{};
                if (!dfiToDUSD) {
                    res = mnview.StoreFuturesUserValues({height, obj.owner, txn}, {totalFutures, obj.destination});
                } else {
                    res = mnview.StoreFuturesDUSD({height, obj.owner, txn}, totalFutures.nValue);
                }
                if (!res) {
                    return res;
                }
            }

            res = TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, *contractAddressValue, obj.owner);
            if (!res) {
                return res;
            }

            res = balances.Sub(obj.source);
            if (!res) {
                return res;
            }
        } else {
            auto res = TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, obj.owner, *contractAddressValue);
            if (!res) {
                return res;
            }

            if (!dfiToDUSD) {
                res = mnview.StoreFuturesUserValues({height, obj.owner, txn}, {obj.source, obj.destination});
            } else {
                res = mnview.StoreFuturesDUSD({height, obj.owner, txn}, obj.source.nValue);
            }
            if (!res) {
                return res;
            }

            balances.Add(obj.source);
        }

        attributes->SetValue(liveKey, balances);

        mnview.SetVariable(*attributes);

        return Res::Ok();
    }

    Res operator()(const CAnyAccountsToAccountsMessage& obj) const {
        // check auth
        for (const auto& kv : obj.from) {
            if (!HasAuth(kv.first)) {
                return Res::Err("tx must have at least one input from account owner");
            }
        }

        // compare
        const auto sumFrom = SumAllTransfers(obj.from);
        const auto sumTo = SumAllTransfers(obj.to);

        if (sumFrom != sumTo) {
            return Res::Err("sum of inputs (from) != sum of outputs (to)");
        }

        // transfer
        // substraction
        auto res = subBalancesDelShares(obj.from);
        // addition
        return !res ? res : addBalancesSetShares(obj.to);
    }

    Res operator()(const CGovernanceMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        for(const auto& gov : obj.govs) {
            Res res{};

            auto var = gov;

            if (var->GetName() == "ATTRIBUTES") {
                // Add to existing ATTRIBUTES instead of overwriting.
                auto govVar = mnview.GetAttributes();

                if (!govVar) {
                    return Res::Err("%s: %s", var->GetName(), "Failed to get existing ATTRIBUTES");
                }

                govVar->time = time;

                // Validate as complete set. Check for future conflicts between key pairs.
                if (!(res = govVar->Import(var->Export()))
                    ||  !(res = govVar->Validate(mnview))
                    ||  !(res = govVar->Apply(mnview, height)))
                    return Res::Err("%s: %s", var->GetName(), res.msg);

                var = govVar;
            } else {
                // After GW, some ATTRIBUTES changes require the context of its map to validate,
                // moving this Validate() call to else statement from before this conditional.
                res = var->Validate(mnview);
                if (!res)
                    return Res::Err("%s: %s", var->GetName(), res.msg);

                if (var->GetName() == "ORACLE_BLOCK_INTERVAL") {
                    // Make sure ORACLE_BLOCK_INTERVAL only updates at end of interval
                    const auto diff = height % mnview.GetIntervalBlock();
                    if (diff != 0) {
                        // Store as pending change
                        storeGovVars({var, height + mnview.GetIntervalBlock() - diff}, mnview);
                        continue;
                    }
                }

                res = var->Apply(mnview, height);
                if (!res) {
                    return Res::Err("%s: %s", var->GetName(), res.msg);
                }
            }

            res = mnview.SetVariable(*var);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }
        }
        return Res::Ok();
    }

    Res operator()(const CGovernanceHeightMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        if (obj.startHeight <= height) {
            return Res::Err("startHeight must be above the current block height");
        }

        if (obj.govVar->GetName() == "ORACLE_BLOCK_INTERVAL") {
            return Res::Err("%s: %s", obj.govVar->GetName(), "Cannot set via setgovheight.");
        }

        // Validate GovVariables before storing
        // TODO remove GW check after fork height. No conflict expected as attrs should not been set by height before.
        if (height >= uint32_t(consensus.FortCanningCrunchHeight) && obj.govVar->GetName() == "ATTRIBUTES") {

            auto govVar = mnview.GetAttributes();
            if (!govVar) {
                return Res::Err("%s: %s", obj.govVar->GetName(), "Failed to get existing ATTRIBUTES");
            }

            auto storedGovVars = mnview.GetStoredVariablesRange(height, obj.startHeight);
            storedGovVars.emplace_back(obj.startHeight, obj.govVar);

            Res res{};
            CCustomCSView govCache(mnview);
            for (const auto& [varHeight, var] : storedGovVars) {
                if (var->GetName() == "ATTRIBUTES") {
                    if (!(res = govVar->Import(var->Export())) ||
                        !(res = govVar->Validate(govCache)) ||
                        !(res = govVar->Apply(govCache, varHeight))) {
                        return Res::Err("%s: Cumulative application of Gov vars failed: %s", obj.govVar->GetName(), res.msg);
                    }
                }
            }
        } else {
            auto result = obj.govVar->Validate(mnview);
            if (!result)
                return Res::Err("%s: %s", obj.govVar->GetName(), result.msg);
        }

        // Store pending Gov var change
        storeGovVars(obj, mnview);

        return Res::Ok();
    }

    Res operator()(const CAppointOracleMessage& obj) const {
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        COracle oracle;
        static_cast<CAppointOracleMessage&>(oracle) = obj;
        auto res = normalizeTokenCurrencyPair(oracle.availablePairs);
        return !res ? res : mnview.AppointOracle(tx.GetHash(), oracle);
    }

    Res operator()(const CUpdateOracleAppointMessage& obj) const {
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        COracle oracle;
        static_cast<CAppointOracleMessage&>(oracle) = obj.newOracleAppoint;
        auto res = normalizeTokenCurrencyPair(oracle.availablePairs);
        return !res ? res : mnview.UpdateOracle(obj.oracleId, std::move(oracle));
    }

    Res operator()(const CRemoveOracleAppointMessage& obj) const {
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        return mnview.RemoveOracle(obj.oracleId);
    }

    Res operator()(const CSetOracleDataMessage& obj) const {

        auto oracle = mnview.GetOracleData(obj.oracleId);
        if (!oracle) {
            return Res::Err("failed to retrieve oracle <%s> from database", obj.oracleId.GetHex());
        }
        if (!HasAuth(oracle.val->oracleAddress)) {
            return Res::Err("tx must have at least one input from account owner");
        }
        if (height >= uint32_t(Params().GetConsensus().FortCanningHeight)) {
            for (const auto& tokenPrice : obj.tokenPrices) {
                for (const auto& price : tokenPrice.second) {
                    if (price.second <= 0) {
                        return Res::Err("Amount out of range");
                    }
                    auto timestamp = time;
                    extern bool diffInHour(int64_t time1, int64_t time2);
                    if (!diffInHour(obj.timestamp, timestamp)) {
                        return Res::Err("Timestamp (%d) is out of price update window (median: %d)",
                            obj.timestamp, timestamp);
                    }
                }
            }
        }
        return mnview.SetOracleData(obj.oracleId, obj.timestamp, obj.tokenPrices);
    }

    Res operator()(const CICXCreateOrderMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        CICXOrderImplemetation order;
        static_cast<CICXOrder&>(order) = obj;

        order.creationTx = tx.GetHash();
        order.creationHeight = height;

        if (!HasAuth(order.ownerAddress))
            return Res::Err("tx must have at least one input from order owner");

        if (!mnview.GetToken(order.idToken))
            return Res::Err("token %s does not exist!", order.idToken.ToString());

        if (order.orderType == CICXOrder::TYPE_INTERNAL) {
            if (!order.receivePubkey.IsFullyValid())
                return Res::Err("receivePubkey must be valid pubkey");

            // subtract the balance from tokenFrom to dedicate them for the order
            CScript txidAddr(order.creationTx.begin(), order.creationTx.end());
            CalculateOwnerRewards(order.ownerAddress);
            res = TransferTokenBalance(order.idToken, order.amountFrom, order.ownerAddress, txidAddr);
        }

        return !res ? res : mnview.ICXCreateOrder(order);
    }

    Res operator()(const CICXMakeOfferMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        CICXMakeOfferImplemetation makeoffer;
        static_cast<CICXMakeOffer&>(makeoffer) = obj;

        makeoffer.creationTx = tx.GetHash();
        makeoffer.creationHeight = height;

        if (!HasAuth(makeoffer.ownerAddress))
            return Res::Err("tx must have at least one input from order owner");

        auto order = mnview.GetICXOrderByCreationTx(makeoffer.orderTx);
        if (!order)
            return Res::Err("order with creation tx " + makeoffer.orderTx.GetHex() + " does not exists!");

        auto expiry = static_cast<int>(height) < consensus.EunosPayaHeight ? CICXMakeOffer::DEFAULT_EXPIRY : CICXMakeOffer::EUNOSPAYA_DEFAULT_EXPIRY;

        if (makeoffer.expiry < expiry)
            return Res::Err("offer expiry must be greater than %d!", expiry - 1);

        CScript txidAddr(makeoffer.creationTx.begin(), makeoffer.creationTx.end());

        if (order->orderType == CICXOrder::TYPE_INTERNAL) {
            // calculating takerFee
            makeoffer.takerFee = CalculateTakerFee(makeoffer.amount);
        } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
            if (!makeoffer.receivePubkey.IsFullyValid())
                return Res::Err("receivePubkey must be valid pubkey");

            // calculating takerFee
            CAmount BTCAmount(static_cast<CAmount>((arith_uint256(makeoffer.amount) * arith_uint256(COIN) / arith_uint256(order->orderPrice)).GetLow64()));
            makeoffer.takerFee = CalculateTakerFee(BTCAmount);
        }

        // locking takerFee in offer txidaddr
        CalculateOwnerRewards(makeoffer.ownerAddress);
        res = TransferTokenBalance(DCT_ID{0}, makeoffer.takerFee, makeoffer.ownerAddress, txidAddr);

        return !res ? res : mnview.ICXMakeOffer(makeoffer);
    }

    Res operator()(const CICXSubmitDFCHTLCMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res) {
            return res;
        }

        CICXSubmitDFCHTLCImplemetation submitdfchtlc;
        static_cast<CICXSubmitDFCHTLC&>(submitdfchtlc) = obj;

        submitdfchtlc.creationTx = tx.GetHash();
        submitdfchtlc.creationHeight = height;

        auto offer = mnview.GetICXMakeOfferByCreationTx(submitdfchtlc.offerTx);
        if (!offer)
            return Res::Err("offer with creation tx %s does not exists!", submitdfchtlc.offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

        if (order->creationHeight + order->expiry < height + submitdfchtlc.timeout)
            return Res::Err("order will expire before dfc htlc expires!");

        if (mnview.HasICXSubmitDFCHTLCOpen(submitdfchtlc.offerTx))
            return Res::Err("dfc htlc already submitted!");

        CScript srcAddr;
        if (order->orderType == CICXOrder::TYPE_INTERNAL) {

            // check auth
            if (!HasAuth(order->ownerAddress))
                return Res::Err("tx must have at least one input from order owner");

            if (!mnview.HasICXMakeOfferOpen(offer->orderTx, submitdfchtlc.offerTx))
                return Res::Err("offerTx (%s) has expired", submitdfchtlc.offerTx.GetHex());

            uint32_t timeout;
            if (static_cast<int>(height) < consensus.EunosPayaHeight)
                timeout = CICXSubmitDFCHTLC::MINIMUM_TIMEOUT;
            else
                timeout = CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;

            if (submitdfchtlc.timeout < timeout)
                return Res::Err("timeout must be greater than %d", timeout - 1);

            srcAddr = CScript(order->creationTx.begin(), order->creationTx.end());

            CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());

            CAmount calcAmount(static_cast<CAmount>((arith_uint256(submitdfchtlc.amount) * arith_uint256(order->orderPrice) / arith_uint256(COIN)).GetLow64()));
            if (calcAmount > offer->amount)
                return Res::Err("amount must be lower or equal the offer one");

            CAmount takerFee = offer->takerFee;
            //EunosPaya: calculating adjusted takerFee only if amount in htlc different than in offer
            if (static_cast<int>(height) >= consensus.EunosPayaHeight)
            {
                if (calcAmount < offer->amount)
                {
                    CAmount BTCAmount(static_cast<CAmount>((arith_uint256(submitdfchtlc.amount) * arith_uint256(order->orderPrice) / arith_uint256(COIN)).GetLow64()));
                    takerFee = static_cast<CAmount>((arith_uint256(BTCAmount) * arith_uint256(offer->takerFee) / arith_uint256(offer->amount)).GetLow64());
                }
            }
            else
            {
                CAmount BTCAmount(static_cast<CAmount>((arith_uint256(submitdfchtlc.amount) * arith_uint256(order->orderPrice) / arith_uint256(COIN)).GetLow64()));
                takerFee = CalculateTakerFee(BTCAmount);
            }

            // refund the rest of locked takerFee if there is difference
            if (offer->takerFee - takerFee) {
                CalculateOwnerRewards(offer->ownerAddress);
                res = TransferTokenBalance(DCT_ID{0}, offer->takerFee - takerFee, offerTxidAddr, offer->ownerAddress);
                if (!res)
                    return res;

                // update the offer with adjusted takerFee
                offer->takerFee = takerFee;
                mnview.ICXUpdateMakeOffer(*offer);
            }

            // burn takerFee
            res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, offerTxidAddr, consensus.burnAddress);
            if (!res)
                return res;

            // burn makerDeposit
            CalculateOwnerRewards(order->ownerAddress);
            res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, order->ownerAddress, consensus.burnAddress);
            if (!res)
                return res;

        } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
            // check auth
            if (!HasAuth(offer->ownerAddress))
                return Res::Err("tx must have at least one input from offer owner");

            srcAddr = offer->ownerAddress;
            CalculateOwnerRewards(offer->ownerAddress);

            auto exthtlc = mnview.HasICXSubmitEXTHTLCOpen(submitdfchtlc.offerTx);
            if (!exthtlc)
                return Res::Err("offer (%s) needs to have ext htlc submitted first, but no external htlc found!", submitdfchtlc.offerTx.GetHex());

            CAmount calcAmount(static_cast<CAmount>((arith_uint256(exthtlc->amount) * arith_uint256(order->orderPrice) / arith_uint256(COIN)).GetLow64()));
            if (submitdfchtlc.amount != calcAmount)
                return Res::Err("amount must be equal to calculated exthtlc amount");

            if (submitdfchtlc.hash != exthtlc->hash)
                return Res::Err("Invalid hash, dfc htlc hash is different than extarnal htlc hash - %s != %s",
                        submitdfchtlc.hash.GetHex(),exthtlc->hash.GetHex());

            uint32_t timeout, btcBlocksInDfi;
            if (static_cast<int>(height) < consensus.EunosPayaHeight)
            {
                timeout = CICXSubmitDFCHTLC::MINIMUM_2ND_TIMEOUT;
                btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
            }
            else
            {
                timeout = CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;
                btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
            }

            if (submitdfchtlc.timeout < timeout)
                return Res::Err("timeout must be greater than %d", timeout - 1);

            if (submitdfchtlc.timeout >= (exthtlc->creationHeight + (exthtlc->timeout * btcBlocksInDfi)) - height)
                return Res::Err("timeout must be less than expiration period of 1st htlc in DFI blocks");
        }

        // subtract the balance from order txidaddr or offer owner address and dedicate them for the dfc htlc
        CScript htlcTxidAddr(submitdfchtlc.creationTx.begin(), submitdfchtlc.creationTx.end());

        res = TransferTokenBalance(order->idToken, submitdfchtlc.amount, srcAddr, htlcTxidAddr);
        return !res ? res : mnview.ICXSubmitDFCHTLC(submitdfchtlc);
    }

    Res operator()(const CICXSubmitEXTHTLCMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        CICXSubmitEXTHTLCImplemetation submitexthtlc;
        static_cast<CICXSubmitEXTHTLC&>(submitexthtlc) = obj;

        submitexthtlc.creationTx = tx.GetHash();
        submitexthtlc.creationHeight = height;

        auto offer = mnview.GetICXMakeOfferByCreationTx(submitexthtlc.offerTx);
        if (!offer)
            return Res::Err("order with creation tx %s does not exists!", submitexthtlc.offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

        if (order->creationHeight + order->expiry < height + (submitexthtlc.timeout * CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS))
            return Res::Err("order will expire before ext htlc expires!");

        if (mnview.HasICXSubmitEXTHTLCOpen(submitexthtlc.offerTx))
            return Res::Err("ext htlc already submitted!");

        if (order->orderType == CICXOrder::TYPE_INTERNAL) {

            if (!HasAuth(offer->ownerAddress))
                return Res::Err("tx must have at least one input from offer owner");

            auto dfchtlc = mnview.HasICXSubmitDFCHTLCOpen(submitexthtlc.offerTx);
            if (!dfchtlc)
                return Res::Err("offer (%s) needs to have dfc htlc submitted first, but no dfc htlc found!", submitexthtlc.offerTx.GetHex());

            CAmount calcAmount(static_cast<CAmount>((arith_uint256(dfchtlc->amount) * arith_uint256(order->orderPrice) / arith_uint256(COIN)).GetLow64()));
            if (submitexthtlc.amount != calcAmount)
                return Res::Err("amount must be equal to calculated dfchtlc amount");

            if (submitexthtlc.hash != dfchtlc->hash)
                return Res::Err("Invalid hash, external htlc hash is different than dfc htlc hash");

            uint32_t timeout, btcBlocksInDfi;
            if (static_cast<int>(height) < consensus.EunosPayaHeight)
            {
                timeout = CICXSubmitEXTHTLC::MINIMUM_2ND_TIMEOUT;
                btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
            }
            else
            {
                timeout = CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;
                btcBlocksInDfi = CICXSubmitEXTHTLC::EUNOSPAYA_BTC_BLOCKS_IN_DFI_BLOCKS;
            }

            if (submitexthtlc.timeout < timeout)
                return Res::Err("timeout must be greater than %d", timeout - 1);

            if (submitexthtlc.timeout * btcBlocksInDfi >= (dfchtlc->creationHeight + dfchtlc->timeout) - height)
                return Res::Err("timeout must be less than expiration period of 1st htlc in DFC blocks");
        } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {

            if (!HasAuth(order->ownerAddress))
                return Res::Err("tx must have at least one input from order owner");

            if (!mnview.HasICXMakeOfferOpen(offer->orderTx, submitexthtlc.offerTx))
                return Res::Err("offerTx (%s) has expired", submitexthtlc.offerTx.GetHex());

            uint32_t timeout;
            if (static_cast<int>(height) < consensus.EunosPayaHeight)
                timeout = CICXSubmitEXTHTLC::MINIMUM_TIMEOUT;
            else
                timeout = CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;

            if (submitexthtlc.timeout < timeout)
                return Res::Err("timeout must be greater than %d", timeout - 1);

            CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());

            CAmount calcAmount(static_cast<CAmount>((arith_uint256(submitexthtlc.amount) * arith_uint256(order->orderPrice) / arith_uint256(COIN)).GetLow64()));
            if (calcAmount > offer->amount)
                return Res::Err("amount must be lower or equal the offer one");

            CAmount takerFee = offer->takerFee;
            //EunosPaya: calculating adjusted takerFee only if amount in htlc different than in offer
            if (static_cast<int>(height) >= consensus.EunosPayaHeight)
            {
                if (calcAmount < offer->amount)
                {
                    CAmount BTCAmount(static_cast<CAmount>((arith_uint256(offer->amount) * arith_uint256(COIN) / arith_uint256(order->orderPrice)).GetLow64()));
                    takerFee = static_cast<CAmount>((arith_uint256(submitexthtlc.amount) * arith_uint256(offer->takerFee) / arith_uint256(BTCAmount)).GetLow64());
                }
            }
            else
            {
                takerFee = CalculateTakerFee(submitexthtlc.amount);
            }

            // refund the rest of locked takerFee if there is difference
            if (offer->takerFee - takerFee) {
                CalculateOwnerRewards(offer->ownerAddress);
                res = TransferTokenBalance(DCT_ID{0}, offer->takerFee - takerFee, offerTxidAddr, offer->ownerAddress);
                if (!res)
                    return res;

                // update the offer with adjusted takerFee
                offer->takerFee = takerFee;
                mnview.ICXUpdateMakeOffer(*offer);
            }

            // burn takerFee
            res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, offerTxidAddr, consensus.burnAddress);
            if (!res)
                return res;

            // burn makerDeposit
            CalculateOwnerRewards(order->ownerAddress);
            res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, order->ownerAddress, consensus.burnAddress);
        }

        return !res ? res : mnview.ICXSubmitEXTHTLC(submitexthtlc);
    }

    Res operator()(const CICXClaimDFCHTLCMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        CICXClaimDFCHTLCImplemetation claimdfchtlc;
        static_cast<CICXClaimDFCHTLC&>(claimdfchtlc) = obj;

        claimdfchtlc.creationTx = tx.GetHash();
        claimdfchtlc.creationHeight = height;

        auto dfchtlc = mnview.GetICXSubmitDFCHTLCByCreationTx(claimdfchtlc.dfchtlcTx);
        if (!dfchtlc)
            return Res::Err("dfc htlc with creation tx %s does not exists!", claimdfchtlc.dfchtlcTx.GetHex());

        if (!mnview.HasICXSubmitDFCHTLCOpen(dfchtlc->offerTx))
            return Res::Err("dfc htlc not found or already claimed or refunded!");

        uint256 calcHash;
        uint8_t calcSeedBytes[32];
        CSHA256()
            .Write(claimdfchtlc.seed.data(), claimdfchtlc.seed.size())
            .Finalize(calcSeedBytes);
        calcHash.SetHex(HexStr(calcSeedBytes, calcSeedBytes + 32));

        if (dfchtlc->hash != calcHash)
            return Res::Err("hash generated from given seed is different than in dfc htlc: %s - %s!", calcHash.GetHex(), dfchtlc->hash.GetHex());

        auto offer = mnview.GetICXMakeOfferByCreationTx(dfchtlc->offerTx);
        if (!offer)
            return Res::Err("offer with creation tx %s does not exists!", dfchtlc->offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

        auto exthtlc = mnview.HasICXSubmitEXTHTLCOpen(dfchtlc->offerTx);
        if (static_cast<int>(height) < consensus.EunosPayaHeight && !exthtlc)
            return Res::Err("cannot claim, external htlc for this offer does not exists or expired!");

        // claim DFC HTLC to receiveAddress
        CalculateOwnerRewards(order->ownerAddress);
        CScript htlcTxidAddr(dfchtlc->creationTx.begin(), dfchtlc->creationTx.end());
        if (order->orderType == CICXOrder::TYPE_INTERNAL)
        {
            CalculateOwnerRewards(offer->ownerAddress);
            res = TransferTokenBalance(order->idToken, dfchtlc->amount, htlcTxidAddr, offer->ownerAddress);
        }
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
            res = TransferTokenBalance(order->idToken, dfchtlc->amount, htlcTxidAddr, order->ownerAddress);
        if (!res)
            return res;

        // refund makerDeposit
        res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, CScript(), order->ownerAddress);
        if (!res)
            return res;

        // makerIncentive
        res = TransferTokenBalance(DCT_ID{0}, offer->takerFee * 25 / 100, CScript(), order->ownerAddress);
        if (!res)
            return res;

        // maker bonus only on fair dBTC/BTC (1:1) trades for now
        DCT_ID BTC = FindTokenByPartialSymbolName(CICXOrder::TOKEN_BTC);
        if (order->idToken == BTC && order->orderPrice == COIN) {
            if ((Params().NetworkIDString() == CBaseChainParams::TESTNET && height >= 1250000) ||
                 Params().NetworkIDString() == CBaseChainParams::REGTEST) {
                res = TransferTokenBalance(DCT_ID{0}, offer->takerFee * 50 / 100, CScript(), order->ownerAddress);
            } else {
                res = TransferTokenBalance(BTC, offer->takerFee * 50 / 100, CScript(), order->ownerAddress);
            }
            if (!res)
                return res;
        }

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
            order->amountToFill -= dfchtlc->amount;
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
            order->amountToFill -= static_cast<CAmount>((arith_uint256(dfchtlc->amount) * arith_uint256(COIN) / arith_uint256(order->orderPrice)).GetLow64());

        // Order fulfilled, close order.
        if (order->amountToFill == 0) {
            order->closeTx = claimdfchtlc.creationTx;
            order->closeHeight = height;
            res = mnview.ICXCloseOrderTx(*order, CICXOrder::STATUS_FILLED);
            if (!res)
                return res;
        }

        res = mnview.ICXClaimDFCHTLC(claimdfchtlc,offer->creationTx,*order);
        if (!res)
            return res;
        // Close offer
        res = mnview.ICXCloseMakeOfferTx(*offer, CICXMakeOffer::STATUS_CLOSED);
        if (!res)
            return res;
        res = mnview.ICXCloseDFCHTLC(*dfchtlc, CICXSubmitDFCHTLC::STATUS_CLAIMED);
        if (!res)
            return res;

        if (static_cast<int>(height) >= consensus.EunosPayaHeight)
        {
            if (exthtlc)
                return mnview.ICXCloseEXTHTLC(*exthtlc, CICXSubmitEXTHTLC::STATUS_CLOSED);
            else
                return (Res::Ok());
        }
        else
            return mnview.ICXCloseEXTHTLC(*exthtlc, CICXSubmitEXTHTLC::STATUS_CLOSED);
    }

    Res operator()(const CICXCloseOrderMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        CICXCloseOrderImplemetation closeorder;
        static_cast<CICXCloseOrder&>(closeorder) = obj;

        closeorder.creationTx = tx.GetHash();
        closeorder.creationHeight = height;

        std::unique_ptr<CICXOrderImplemetation> order;
        if (!(order = mnview.GetICXOrderByCreationTx(closeorder.orderTx)))
            return Res::Err("order with creation tx %s does not exists!", closeorder.orderTx.GetHex());

        if (!order->closeTx.IsNull())
            return Res::Err("order with creation tx %s is already closed!", closeorder.orderTx.GetHex());

        if (!mnview.HasICXOrderOpen(order->idToken, order->creationTx))
            return Res::Err("order with creation tx %s is already closed!", closeorder.orderTx.GetHex());

        // check auth
        if (!HasAuth(order->ownerAddress))
            return Res::Err("tx must have at least one input from order owner");

        order->closeTx = closeorder.creationTx;
        order->closeHeight = closeorder.creationHeight;

        if (order->orderType == CICXOrder::TYPE_INTERNAL && order->amountToFill > 0) {
            // subtract the balance from txidAddr and return to owner
            CScript txidAddr(order->creationTx.begin(), order->creationTx.end());
            CalculateOwnerRewards(order->ownerAddress);
            res = TransferTokenBalance(order->idToken, order->amountToFill, txidAddr, order->ownerAddress);
            if (!res)
                return res;
        }

        res = mnview.ICXCloseOrder(closeorder);
        return !res ? res : mnview.ICXCloseOrderTx(*order,CICXOrder::STATUS_CLOSED);
    }

    Res operator()(const CICXCloseOfferMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        CICXCloseOfferImplemetation closeoffer;
        static_cast<CICXCloseOffer&>(closeoffer) = obj;

        closeoffer.creationTx = tx.GetHash();
        closeoffer.creationHeight = height;

        std::unique_ptr<CICXMakeOfferImplemetation> offer;
        if (!(offer = mnview.GetICXMakeOfferByCreationTx(closeoffer.offerTx)))
            return Res::Err("offer with creation tx %s does not exists!", closeoffer.offerTx.GetHex());

        if (!offer->closeTx.IsNull())
            return Res::Err("offer with creation tx %s is already closed!", closeoffer.offerTx.GetHex());

        if (!mnview.HasICXMakeOfferOpen(offer->orderTx, offer->creationTx))
            return Res::Err("offer with creation tx %s does not exists!", closeoffer.offerTx.GetHex());

        std::unique_ptr<CICXOrderImplemetation> order;
        if (!(order = mnview.GetICXOrderByCreationTx(offer->orderTx)))
            return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

        // check auth
        if (!HasAuth(offer->ownerAddress))
            return Res::Err("tx must have at least one input from offer owner");

        offer->closeTx = closeoffer.creationTx;
        offer->closeHeight = closeoffer.creationHeight;

        bool isPreEunosPaya = static_cast<int>(height) < consensus.EunosPayaHeight;

        if (order->orderType == CICXOrder::TYPE_INTERNAL && !mnview.ExistedICXSubmitDFCHTLC(offer->creationTx, isPreEunosPaya))
        {
            // subtract takerFee from txidAddr and return to owner
            CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
            CalculateOwnerRewards(offer->ownerAddress);
            res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, txidAddr, offer->ownerAddress);
            if (!res)
                return res;
        }
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
            // subtract the balance from txidAddr and return to owner
            CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
            CalculateOwnerRewards(offer->ownerAddress);
            if (isPreEunosPaya)
            {
                res = TransferTokenBalance(order->idToken, offer->amount, txidAddr, offer->ownerAddress);
                if (!res)
                    return res;
            }
            if (!mnview.ExistedICXSubmitEXTHTLC(offer->creationTx, isPreEunosPaya))
            {
                res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, txidAddr, offer->ownerAddress);
                if (!res)
                    return res;
            }
        }

        res = mnview.ICXCloseOffer(closeoffer);
        return !res ? res : mnview.ICXCloseMakeOfferTx(*offer, CICXMakeOffer::STATUS_CLOSED);
    }

    Res operator()(const CLoanSetCollateralTokenMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        if (!HasFoundationAuth())
            return Res::Err("tx not from foundation member!");

        if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) && IsTokensMigratedToGovVar())
        {
            const auto& tokenId = obj.idToken.v;

            auto attributes = mnview.GetAttributes();
            attributes->time = time;

            CDataStructureV0 collateralEnabled{AttributeTypes::Token, tokenId, TokenKeys::LoanCollateralEnabled};
            CDataStructureV0 collateralFactor{AttributeTypes::Token, tokenId, TokenKeys::LoanCollateralFactor};
            CDataStructureV0 pairKey{AttributeTypes::Token, tokenId, TokenKeys::FixedIntervalPriceId};

            auto gv = GovVariable::Create("ATTRIBUTES");
            if (!gv) {
                return Res::Err("Failed to create ATTRIBUTES Governance variable");
            }

            auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
            if (!var) {
                return Res::Err("Failed to convert ATTRIBUTES Governance variable");
            }

            var->SetValue(collateralEnabled, true);
            var->SetValue(collateralFactor, obj.factor);
            var->SetValue(pairKey, obj.fixedIntervalPriceId);

            res = attributes->Import(var->Export());
            if (!res)
                return res;

            res = attributes->Validate(mnview);
            if (!res)
                return res;

            res = attributes->Apply(mnview, height);
            if (!res)
                return res;

            return mnview.SetVariable(*attributes);
        }

        CLoanSetCollateralTokenImplementation collToken;
        static_cast<CLoanSetCollateralToken&>(collToken) = obj;

        collToken.creationTx = tx.GetHash();
        collToken.creationHeight = height;

        auto token = mnview.GetToken(collToken.idToken);
        if (!token)
            return Res::Err("token %s does not exist!", collToken.idToken.ToString());

        if (!collToken.activateAfterBlock)
            collToken.activateAfterBlock = height;

        if (collToken.activateAfterBlock < height)
            return Res::Err("activateAfterBlock cannot be less than current height!");

        if (!OraclePriceFeed(mnview, collToken.fixedIntervalPriceId))
            return Res::Err("Price feed %s/%s does not belong to any oracle", collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second);

        CFixedIntervalPrice fixedIntervalPrice;
        fixedIntervalPrice.priceFeedId = collToken.fixedIntervalPriceId;

        auto price = GetAggregatePrice(mnview, collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second, time);
        if (!price)
            return Res::Err(price.msg);

        fixedIntervalPrice.priceRecord[1] = price;
        fixedIntervalPrice.timestamp = time;

        auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
        if (!resSetFixedPrice)
            return Res::Err(resSetFixedPrice.msg);

        return mnview.CreateLoanCollateralToken(collToken);
    }

    Res operator()(const CLoanSetLoanTokenMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        if (!HasFoundationAuth())
            return Res::Err("tx not from foundation member!");

        if (obj.interest < 0 && height < static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
            return Res::Err("interest rate cannot be less than 0!");
        }

        CTokenImplementation token;
        token.symbol = trim_ws(obj.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        token.name = trim_ws(obj.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.creationTx = tx.GetHash();
        token.creationHeight = height;
        token.flags = obj.mintable ? static_cast<uint8_t>(CToken::TokenFlags::Default) : static_cast<uint8_t>(CToken::TokenFlags::Tradeable);
        token.flags |= static_cast<uint8_t>(CToken::TokenFlags::LoanToken) | static_cast<uint8_t>(CToken::TokenFlags::DAT);

        auto tokenId = mnview.CreateToken(token);
        if (!tokenId)
            return std::move(tokenId);

        if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) && IsTokensMigratedToGovVar())
        {
            const auto& id = tokenId.val->v;

            auto attributes = mnview.GetAttributes();
            attributes->time = time;

            CDataStructureV0 mintEnabled{AttributeTypes::Token, id, TokenKeys::LoanMintingEnabled};
            CDataStructureV0 mintInterest{AttributeTypes::Token, id, TokenKeys::LoanMintingInterest};
            CDataStructureV0 pairKey{AttributeTypes::Token, id, TokenKeys::FixedIntervalPriceId};

            auto gv = GovVariable::Create("ATTRIBUTES");
            if (!gv) {
                return Res::Err("Failed to create ATTRIBUTES Governance variable");
            }

            auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
            if (!var) {
                return Res::Err("Failed to convert ATTRIBUTES Governance variable");
            }

            var->SetValue(mintEnabled, obj.mintable);
            var->SetValue(mintInterest, obj.interest);
            var->SetValue(pairKey, obj.fixedIntervalPriceId);

            res = attributes->Import(var->Export());
            if (!res)
                return res;

            res = attributes->Validate(mnview);
            if (!res)
                return res;

            res = attributes->Apply(mnview, height);
            if (!res)
                return res;

            return mnview.SetVariable(*attributes);
        }

        CLoanSetLoanTokenImplementation loanToken;
        static_cast<CLoanSetLoanToken&>(loanToken) = obj;

        loanToken.creationTx = tx.GetHash();
        loanToken.creationHeight = height;

        auto nextPrice = GetAggregatePrice(mnview, obj.fixedIntervalPriceId.first, obj.fixedIntervalPriceId.second, time);
        if (!nextPrice)
            return Res::Err(nextPrice.msg);

        if (!OraclePriceFeed(mnview, obj.fixedIntervalPriceId))
            return Res::Err("Price feed %s/%s does not belong to any oracle", obj.fixedIntervalPriceId.first, obj.fixedIntervalPriceId.second);

        CFixedIntervalPrice fixedIntervalPrice;
        fixedIntervalPrice.priceFeedId = loanToken.fixedIntervalPriceId;
        fixedIntervalPrice.priceRecord[1] = nextPrice;
        fixedIntervalPrice.timestamp = time;

        auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
        if (!resSetFixedPrice)
            return Res::Err(resSetFixedPrice.msg);

        return mnview.SetLoanToken(loanToken, *(tokenId.val));
    }

    Res operator()(const CLoanUpdateLoanTokenMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        if (!HasFoundationAuth())
            return Res::Err("tx not from foundation member!");

        if (obj.interest < 0 && height < static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
            return Res::Err("interest rate cannot be less than 0!");
        }

        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        if (!pair)
            return Res::Err("Loan token (%s) does not exist!", obj.tokenTx.GetHex());

        auto loanToken = (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) && IsTokensMigratedToGovVar()) ?
                mnview.GetLoanTokenByID(pair->first) : mnview.GetLoanToken(obj.tokenTx);

        if (!loanToken)
            return Res::Err("Loan token (%s) does not exist!", obj.tokenTx.GetHex());

        if (obj.mintable != loanToken->mintable)
            loanToken->mintable = obj.mintable;

        if (obj.interest != loanToken->interest)
            loanToken->interest = obj.interest;

        if (obj.symbol != pair->second.symbol)
            pair->second.symbol = trim_ws(obj.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

        if (obj.name != pair->second.name)
            pair->second.name = trim_ws(obj.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

        if (obj.mintable != (pair->second.flags & (uint8_t)CToken::TokenFlags::Mintable))
            pair->second.flags ^= (uint8_t)CToken::TokenFlags::Mintable;

        res = mnview.UpdateToken(pair->second);
        if (!res)
            return res;

        if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) && IsTokensMigratedToGovVar())
        {
            const auto& id = pair->first.v;

            auto attributes = mnview.GetAttributes();
            attributes->time = time;

            CDataStructureV0 mintEnabled{AttributeTypes::Token, id, TokenKeys::LoanMintingEnabled};
            CDataStructureV0 mintInterest{AttributeTypes::Token, id, TokenKeys::LoanMintingInterest};
            CDataStructureV0 pairKey{AttributeTypes::Token, id, TokenKeys::FixedIntervalPriceId};

            auto gv = GovVariable::Create("ATTRIBUTES");
            if (!gv) {
                return Res::Err("Failed to create ATTRIBUTES Governance variable");
            }

            auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
            if (!var) {
                return Res::Err("Failed to convert ATTRIBUTES Governance variable");
            }

            var->SetValue(mintEnabled, obj.mintable);
            var->SetValue(mintInterest, obj.interest);
            var->SetValue(pairKey, obj.fixedIntervalPriceId);

            res = attributes->Import(var->Export());
            if (!res)
                return res;

            res = attributes->Validate(mnview);
            if (!res)
                return res;

            res = attributes->Apply(mnview, height);
            if (!res)
                return res;

            return mnview.SetVariable(*attributes);
        }

        if (obj.fixedIntervalPriceId != loanToken->fixedIntervalPriceId) {
            if (!OraclePriceFeed(mnview, obj.fixedIntervalPriceId))
                return Res::Err("Price feed %s/%s does not belong to any oracle", obj.fixedIntervalPriceId.first, obj.fixedIntervalPriceId.second);

            loanToken->fixedIntervalPriceId = obj.fixedIntervalPriceId;
        }

        return mnview.UpdateLoanToken(*loanToken, pair->first);
    }

    Res operator()(const CLoanSchemeMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res) {
            return res;
        }

        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member!");
        }

        if (obj.ratio < 100) {
            return Res::Err("minimum collateral ratio cannot be less than 100");
        }

        if (obj.rate < 1000000) {
            return Res::Err("interest rate cannot be less than 0.01");
        }

        if (obj.identifier.empty() || obj.identifier.length() > 8) {
            return Res::Err("id cannot be empty or more than 8 chars long");
        }

        // Look for loan scheme which already has matching rate and ratio
        bool duplicateLoan = false;
        std::string duplicateID;
        mnview.ForEachLoanScheme([&](const std::string& key, const CLoanSchemeData& data)
        {
            // Duplicate scheme already exists
            if (data.ratio == obj.ratio && data.rate == obj.rate) {
                duplicateLoan = true;
                duplicateID = key;
                return false;
            }
            return true;
        });

        if (duplicateLoan) {
            return Res::Err("Loan scheme %s with same interestrate and mincolratio already exists", duplicateID);
        } else {
            // Look for delayed loan scheme which already has matching rate and ratio
            std::pair<std::string, uint64_t> duplicateKey;
            mnview.ForEachDelayedLoanScheme([&](const std::pair<std::string, uint64_t>& key, const CLoanSchemeMessage& data)
            {
                // Duplicate delayed loan scheme
                if (data.ratio == obj.ratio && data.rate == obj.rate) {
                    duplicateLoan = true;
                    duplicateKey = key;
                    return false;
                }
                return true;
            });

            if (duplicateLoan) {
                return Res::Err("Loan scheme %s with same interestrate and mincolratio pending on block %d", duplicateKey.first, duplicateKey.second);
            }
        }

        // New loan scheme, no duplicate expected.
        if (mnview.GetLoanScheme(obj.identifier)) {
            if (!obj.updateHeight) {
                return Res::Err("Loan scheme already exist with id %s", obj.identifier);
            }
        } else if (obj.updateHeight) {
            return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);
        }

        // Update set, not max uint64_t which indicates immediate update and not updated on this block.
        if (obj.updateHeight && obj.updateHeight != std::numeric_limits<uint64_t>::max() && obj.updateHeight != height) {
            if (obj.updateHeight < height) {
                return Res::Err("Update height below current block height, set future height");
            }

            return mnview.StoreDelayedLoanScheme(obj);
        }

        // If no default yet exist set this one as default.
        if (!mnview.GetDefaultLoanScheme()) {
            mnview.StoreDefaultLoanScheme(obj.identifier);
        }

        return mnview.StoreLoanScheme(obj);
    }

    Res operator()(const CDefaultLoanSchemeMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res) {
            return res;
        }

        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member!");
        }

        if (obj.identifier.empty() || obj.identifier.length() > 8) {
            return Res::Err("id cannot be empty or more than 8 chars long");
        }

        if (!mnview.GetLoanScheme(obj.identifier)) {
            return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);
        }

        const auto currentID = mnview.GetDefaultLoanScheme();
        if (currentID && *currentID == obj.identifier) {
            return Res::Err("Loan scheme with id %s is already set as default", obj.identifier);
        }

        if (auto height = mnview.GetDestroyLoanScheme(obj.identifier)) {
            return Res::Err("Cannot set %s as default, set to destroyed on block %d", obj.identifier, *height);
        }

        return mnview.StoreDefaultLoanScheme(obj.identifier);;
    }

    Res operator()(const CDestroyLoanSchemeMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res) {
            return res;
        }

        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member!");
        }

        if (obj.identifier.empty() || obj.identifier.length() > 8) {
            return Res::Err("id cannot be empty or more than 8 chars long");
        }

        if (!mnview.GetLoanScheme(obj.identifier)) {
            return Res::Err("Cannot find existing loan scheme with id %s", obj.identifier);
        }

        const auto currentID = mnview.GetDefaultLoanScheme();
        if (currentID && *currentID == obj.identifier) {
            return Res::Err("Cannot destroy default loan scheme, set new default first");
        }

        // Update set and not updated on this block.
        if (obj.destroyHeight && obj.destroyHeight != height) {
            if (obj.destroyHeight < height) {
                return Res::Err("Destruction height below current block height, set future height");
            }
            return mnview.StoreDelayedDestroyScheme(obj);
        }

        mnview.ForEachVault([&](const CVaultId& vaultId, CVaultData vault) {
            if (vault.schemeId == obj.identifier) {
                vault.schemeId = *mnview.GetDefaultLoanScheme();
                mnview.StoreVault(vaultId, vault);
            }
            return true;
        });

        return mnview.EraseLoanScheme(obj.identifier);
    }

    Res operator()(const CVaultMessage& obj) const {

        auto vaultCreationFee = consensus.vaultCreationFee;
        if (tx.vout[0].nValue != vaultCreationFee || tx.vout[0].nTokenId != DCT_ID{0}) {
            return Res::Err("malformed tx vouts, creation vault fee is %s DFI", GetDecimaleString(vaultCreationFee));
        }

        CVaultData vault{};
        static_cast<CVaultMessage&>(vault) = obj;

        // set loan scheme to default if non provided
        if (obj.schemeId.empty()) {
            if (auto defaultScheme = mnview.GetDefaultLoanScheme()){
                vault.schemeId = *defaultScheme;
            } else {
                return Res::Err("There is no default loan scheme");
            }
        }

        // loan scheme exists
        if (!mnview.GetLoanScheme(vault.schemeId)) {
            return Res::Err("Cannot find existing loan scheme with id %s", vault.schemeId);
        }

        // check loan scheme is not to be destroyed
        if (auto height = mnview.GetDestroyLoanScheme(obj.schemeId)) {
            return Res::Err("Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *height);
        }

        auto vaultId = tx.GetHash();
        return mnview.StoreVault(vaultId, vault);
    }

    Res operator()(const CCloseVaultMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        if (vault->isUnderLiquidation)
            return Res::Err("Cannot close vault under liquidation");

        // owner auth
        if (!HasAuth(vault->ownerAddress))
            return Res::Err("tx must have at least one input from token owner");

        if (const auto loans = mnview.GetLoanTokens(obj.vaultId)) {
            for (const auto& [tokenId, amount] : loans->balances) {
                const auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
                if (!rate) {
                    return Res::Err("Cannot get interest rate for this token (%d)", tokenId.v);
                }

                const auto totalInterest = TotalInterest(*rate, height);
                if (amount + totalInterest > 0) {
                    return Res::Err("Vault <%s> has loans", obj.vaultId.GetHex());
                }

                if (totalInterest < 0) {
                    TrackNegativeInterest(mnview, {tokenId, amount > std::abs(totalInterest)  ? std::abs(totalInterest) : amount});
                }
            }
        }

        CalculateOwnerRewards(obj.to);
        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
            for (const auto& col : collaterals->balances) {
                auto res = mnview.AddBalance(obj.to, {col.first, col.second});
                if (!res)
                    return res;
            }
        }

        // delete all interest to vault
        res = mnview.EraseInterest(obj.vaultId, height);
        if (!res)
            return res;

        // return half fee, the rest is burned at creation
        auto feeBack = consensus.vaultCreationFee / 2;
        res = mnview.AddBalance(obj.to, {DCT_ID{0}, feeBack});
        return !res ? res : mnview.EraseVault(obj.vaultId);
    }

    Res operator()(const CUpdateVaultMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        if (vault->isUnderLiquidation)
            return Res::Err("Cannot update vault under liquidation");

        // owner auth
        if (!HasAuth(vault->ownerAddress))
            return Res::Err("tx must have at least one input from token owner");

        // loan scheme exists
        const auto scheme = mnview.GetLoanScheme(obj.schemeId);
        if (!scheme)
            return Res::Err("Cannot find existing loan scheme with id %s", obj.schemeId);

        // loan scheme is not set to be destroyed
        if (auto height = mnview.GetDestroyLoanScheme(obj.schemeId))
            return Res::Err("Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *height);

        if (!IsVaultPriceValid(mnview, obj.vaultId, height))
            return Res::Err("Cannot update vault while any of the asset's price is invalid");

        // don't allow scheme change when vault is going to be in liquidation
        if (vault->schemeId != obj.schemeId) {
            if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
                for (int i = 0; i < 2; i++) {
                    bool useNextPrice = i > 0, requireLivePrice = true;
                    auto collateralsLoans = mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
                    if (!collateralsLoans)
                        return std::move(collateralsLoans);

                    if (collateralsLoans.val->ratio() < scheme->ratio)
                        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme->ratio);
                }
            }
            if (height >= static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
                if (const auto loanTokens = mnview.GetLoanTokens(obj.vaultId)) {
                    for (const auto& [tokenId, tokenAmount] : loanTokens->balances) {
                        const auto loanToken = mnview.GetLoanTokenByID(tokenId);
                        assert(loanToken);
                        res = mnview.IncreaseInterest(height, obj.vaultId, obj.schemeId, tokenId, loanToken->interest, 0);
                        if (!res) {
                            return res;
                        }
                    }
                }
            }
        }

        vault->schemeId = obj.schemeId;
        vault->ownerAddress = obj.ownerAddress;
        return mnview.UpdateVault(obj.vaultId, *vault);
    }

    Res CollateralPctCheck(const bool hasDUSDLoans, const CCollateralLoans& collateralsLoans, const uint32_t ratio) const {

        std::optional<std::pair<DCT_ID, std::optional<CTokensView::CTokenImpl>>> tokenDUSD;
        if (static_cast<int>(height) >= consensus.FortCanningRoadHeight) {
            tokenDUSD = mnview.GetToken("DUSD");
        }

        // Calculate DFI and DUSD value separately
        uint64_t totalCollateralsDUSD = 0;
        uint64_t totalCollateralsDFI = 0;

        for (auto& col : collateralsLoans.collaterals){
            if (col.nTokenId == DCT_ID{0} )
                totalCollateralsDFI += col.nValue;

            if(tokenDUSD && col.nTokenId == tokenDUSD->first)
                totalCollateralsDUSD += col.nValue;
        }
        auto totalCollaterals = totalCollateralsDUSD + totalCollateralsDFI;

        // Height checks
        auto isPostFCH = static_cast<int>(height) >= consensus.FortCanningHillHeight;
        auto isPreFCH = static_cast<int>(height) < consensus.FortCanningHillHeight;
        auto isPostFCE = static_cast<int>(height) >= consensus.FortCanningEpilogueHeight;
        auto isPostFCR = static_cast<int>(height) >= consensus.FortCanningRoadHeight;

        // Condition checks
        auto isDFILessThanHalfOfTotalCollateral = totalCollateralsDFI < collateralsLoans.totalCollaterals / 2;
        auto isDFIAndDUSDLessThanHalfOfRequiredCollateral = arith_uint256(totalCollaterals) * 100 < (arith_uint256(collateralsLoans.totalLoans) * ratio / 2);
        auto isDFILessThanHalfOfRequiredCollateral = arith_uint256(totalCollateralsDFI) * 100 < (arith_uint256(collateralsLoans.totalLoans) * ratio / 2);

        if(isPostFCE){
            if (hasDUSDLoans){
                if(isDFILessThanHalfOfRequiredCollateral)
                    return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT));
            }else {
                if(isDFIAndDUSDLessThanHalfOfRequiredCollateral)
                    return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT));
            }
            return Res::Ok();
        }

        if (isPostFCR)
            return isDFIAndDUSDLessThanHalfOfRequiredCollateral ?
                Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT)) :
                Res::Ok();

        if (isPostFCH)
            return isDFILessThanHalfOfRequiredCollateral ?
                Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT)) :
                Res::Ok();

        if (isPreFCH && isDFILessThanHalfOfTotalCollateral)
            return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT));

        return Res::Ok();
    }


    Res operator()(const CDepositToVaultMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        // owner auth
        if (!HasAuth(obj.from))
            return Res::Err("tx must have at least one input from token owner");

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        if (vault->isUnderLiquidation)
            return Res::Err("Cannot deposit to vault under liquidation");

        // If collateral token exist make sure it is enabled.
        if (mnview.GetCollateralTokenFromAttributes(obj.amount.nTokenId)) {
            CDataStructureV0 collateralKey{AttributeTypes::Token, obj.amount.nTokenId.v, TokenKeys::LoanCollateralEnabled};
            if (const auto attributes = mnview.GetAttributes(); !attributes->GetValue(collateralKey, false)) {
                return Res::Err("Collateral token (%d) is disabled", obj.amount.nTokenId.v);
            }
        }

        //check balance
        CalculateOwnerRewards(obj.from);
        res = mnview.SubBalance(obj.from, obj.amount);
        if (!res)
            return Res::Err("Insufficient funds: can't subtract balance of %s: %s\n", ScriptToString(obj.from), res.msg);

        res = mnview.AddVaultCollateral(obj.vaultId, obj.amount);
        if (!res) return res;

        bool useNextPrice = false, requireLivePrice = false;
        auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);

        auto collateralsLoans = mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
        if (!collateralsLoans)
            return std::move(collateralsLoans);

        auto scheme = mnview.GetLoanScheme(vault->schemeId);
        if (collateralsLoans.val->ratio() < scheme->ratio)
            return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme->ratio);

        return Res::Ok();
    }

    Res operator()(const CWithdrawFromVaultMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        if (vault->isUnderLiquidation)
            return Res::Err("Cannot withdraw from vault under liquidation");

        // owner auth
        if (!HasAuth(vault->ownerAddress))
            return Res::Err("tx must have at least one input from token owner");

        if (!IsVaultPriceValid(mnview, obj.vaultId, height))
            return Res::Err("Cannot withdraw from vault while any of the asset's price is invalid");

        res = mnview.SubVaultCollateral(obj.vaultId, obj.amount);
        if (!res)
            return res;

        auto hasDUSDLoans = false;

        std::optional<std::pair<DCT_ID, std::optional<CTokensView::CTokenImpl>>> tokenDUSD;
        if (static_cast<int>(height) >= consensus.FortCanningRoadHeight) {
            tokenDUSD = mnview.GetToken("DUSD");
        }

        if (const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId))
        {
            // Update negative interest in vault
            for (const auto& [tokenId, currentLoanAmount] : loanAmounts->balances) {

                if (tokenDUSD && tokenId == tokenDUSD->first) {
                    hasDUSDLoans = true;
                }

                const auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
                assert(rate);

                const auto totalInterest = TotalInterest(*rate, height);

                // Ignore positive or nil interest
                if (totalInterest >= 0) {
                    continue;
                }

                const auto subAmount = currentLoanAmount > std::abs(totalInterest) ? std::abs(totalInterest) : currentLoanAmount;
                res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{tokenId, subAmount});
                if (!res) {
                    return res;
                }

                TrackNegativeInterest(mnview, {tokenId, subAmount});

                mnview.ResetInterest(height, obj.vaultId, vault->schemeId, tokenId);

            }

            if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId))
            {

                const auto scheme = mnview.GetLoanScheme(vault->schemeId);
                for (int i = 0; i < 2; i++) {
                    // check collaterals for active and next price
                    bool useNextPrice = i > 0, requireLivePrice = true;

                    auto collateralsLoans = mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
                    if (!collateralsLoans)
                        return std::move(collateralsLoans);

                    if (collateralsLoans.val->ratio() < scheme->ratio)
                        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme->ratio);

                    res = CollateralPctCheck(hasDUSDLoans, collateralsLoans, scheme->ratio);
                    if(!res)
                        return res;
                }
            } else {
                return Res::Err("Cannot withdraw all collaterals as there are still active loans in this vault");
            }
        }

        return mnview.AddBalance(obj.to, obj.amount);
    }

    Res operator()(const CPaybackWithCollateralMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        // vault exists
        const auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        if (vault->isUnderLiquidation)
            return Res::Err("Cannot payback vault with collateral while vault's under liquidation");

        // owner auth
        if (!HasAuth(vault->ownerAddress))
            return Res::Err("tx must have at least one input from token owner");

        return PaybackWithCollateral(mnview, *vault, obj.vaultId, height, time);
    }

    Res operator()(const CLoanTakeLoanMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        const auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        if (vault->isUnderLiquidation)
            return Res::Err("Cannot take loan on vault under liquidation");

        // vault owner auth
        if (!HasAuth(vault->ownerAddress))
            return Res::Err("tx must have at least one input from vault owner");

        if (!IsVaultPriceValid(mnview, obj.vaultId, height))
            return Res::Err("Cannot take loan while any of the asset's price in the vault is not live");

        auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
        if (!collaterals)
            return Res::Err("Vault with id %s has no collaterals", obj.vaultId.GetHex());

        const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);

        auto hasDUSDLoans = false;

        std::optional<std::pair<DCT_ID, std::optional<CTokensView::CTokenImpl>>> tokenDUSD;
        if (static_cast<int>(height) >= consensus.FortCanningRoadHeight) {
            tokenDUSD = mnview.GetToken("DUSD");
        }

        uint64_t totalLoansActivePrice = 0, totalLoansNextPrice = 0;
        for (const auto& [tokenId, tokenAmount] : obj.amounts.balances)
        {
            if (height >= static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight) && tokenAmount <= 0)
                return Res::Err("Valid loan amount required (input: %d@%d)", tokenAmount, tokenId.v);

            auto loanToken = mnview.GetLoanTokenByID(tokenId);
            if (!loanToken)
                return Res::Err("Loan token with id (%s) does not exist!", tokenId.ToString());

            if (!loanToken->mintable)
                return Res::Err("Loan cannot be taken on token with id (%s) as \"mintable\" is currently false",tokenId.ToString());

            if (tokenDUSD && tokenId == tokenDUSD->first) {
                hasDUSDLoans = true;
            }

            // Calculate interest
            CAmount currentLoanAmount{};
            bool resetInterestToHeight{};
            auto loanAmountChange = tokenAmount;

            if (loanAmounts && loanAmounts->balances.count(tokenId)) {
                currentLoanAmount = loanAmounts->balances.at(tokenId);
                const auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
                assert(rate);
                const auto totalInterest = TotalInterest(*rate, height);

                if (totalInterest < 0) {
                    loanAmountChange = currentLoanAmount > std::abs(totalInterest) ?
                        // Interest to decrease smaller than overall existing loan amount.
                        // So reduce interest from the borrowing principal. If this is negative,
                        // we'll reduce from principal.
                        tokenAmount + totalInterest :
                        // Interest to decrease is larger than old loan amount.
                        // We reduce from the borrowing principal. If this is negative,
                        // we'll reduce from principal.
                        tokenAmount - currentLoanAmount;
                    resetInterestToHeight = true;
                    TrackNegativeInterest(mnview, {tokenId, currentLoanAmount > std::abs(totalInterest) ? std::abs(totalInterest) : currentLoanAmount});
                }
            }

            if (loanAmountChange > 0) {
                res = mnview.AddLoanToken(obj.vaultId, CTokenAmount{tokenId, loanAmountChange});
                if (!res)
                    return res;
            } else {
                const auto subAmount = currentLoanAmount > std::abs(loanAmountChange) ? std::abs(loanAmountChange) : currentLoanAmount;
                res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{tokenId, subAmount});
                if (!res) {
                    return res;
                }
            }

            if (resetInterestToHeight) {
                mnview.ResetInterest(height, obj.vaultId, vault->schemeId, tokenId);
            } else {
                res = mnview.IncreaseInterest(height, obj.vaultId, vault->schemeId, tokenId, loanToken->interest, loanAmountChange);
                if (!res)
                    return res;
            }

            const auto tokenCurrency = loanToken->fixedIntervalPriceId;

            auto priceFeed = mnview.GetFixedIntervalPrice(tokenCurrency);
            if (!priceFeed)
                return Res::Err(priceFeed.msg);

            if (!priceFeed.val->isLive(mnview.GetPriceDeviation()))
                return Res::Err("No live fixed prices for %s/%s", tokenCurrency.first, tokenCurrency.second);

            for (int i = 0; i < 2; i++) {
                // check active and next price
                auto price = priceFeed.val->priceRecord[int(i > 0)];
                auto amount = MultiplyAmounts(price, tokenAmount);
                if (price > COIN && amount < tokenAmount)
                    return Res::Err("Value/price too high (%s/%s)", GetDecimaleString(tokenAmount), GetDecimaleString(price));

                auto& totalLoans = i > 0 ? totalLoansNextPrice : totalLoansActivePrice;
                auto prevLoans = totalLoans;
                totalLoans += amount;
                if (prevLoans > totalLoans)
                    return Res::Err("Exceed maximum loans");
            }

            res = mnview.AddMintedTokens(tokenId, tokenAmount);
            if (!res)
                return res;

            const auto& address = !obj.to.empty() ? obj.to
                                                  : vault->ownerAddress;
            CalculateOwnerRewards(address);
            res = mnview.AddBalance(address, CTokenAmount{tokenId, tokenAmount});
            if (!res)
                return res;
        }

        auto scheme = mnview.GetLoanScheme(vault->schemeId);
        for (int i = 0; i < 2; i++) {
            // check ratio against current and active price
            bool useNextPrice = i > 0, requireLivePrice = true;

            auto collateralsLoans = mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
            if (!collateralsLoans)
                return std::move(collateralsLoans);

            if (collateralsLoans.val->ratio() < scheme->ratio)
                return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme->ratio);

            res = CollateralPctCheck(hasDUSDLoans, collateralsLoans, scheme->ratio);
            if(!res)
                return res;
        }
        return Res::Ok();
    }

    Res operator()(const CLoanPaybackLoanMessage& obj) const {
        std::map<DCT_ID, CBalances> loans;
        for (auto& balance: obj.amounts.balances) {
            auto id = balance.first;
            auto amount = balance.second;

            CBalances* loan;
            if (id == DCT_ID{0})
            {
                auto tokenDUSD = mnview.GetToken("DUSD");
                if (!tokenDUSD)
                    return Res::Err("Loan token DUSD does not exist!");
                loan = &loans[tokenDUSD->first];
            }
            else
                loan = &loans[id];

            loan->Add({id, amount});
        }
        return (*this)(
            CLoanPaybackLoanV2Message{
                obj.vaultId,
                obj.from,
                loans
            });
    }

    Res operator()(const CLoanPaybackLoanV2Message& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        const auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Cannot find existing vault with id %s", obj.vaultId.GetHex());

        if (vault->isUnderLiquidation)
            return Res::Err("Cannot payback loan on vault under liquidation");

        if (!mnview.GetVaultCollaterals(obj.vaultId)) {
            return Res::Err("Vault with id %s has no collaterals", obj.vaultId.GetHex());
        }

        if (!HasAuth(obj.from))
            return Res::Err("tx must have at least one input from token owner");

        if (static_cast<int>(height) < consensus.FortCanningRoadHeight && !IsVaultPriceValid(mnview, obj.vaultId, height))
            return Res::Err("Cannot payback loan while any of the asset's price is invalid");

        // Handle payback with collateral special case
        if (static_cast<int>(height) >= consensus.FortCanningEpilogueHeight
        && IsPaybackWithCollateral(mnview, obj.loans)) {
            return PaybackWithCollateral(mnview, *vault, obj.vaultId, height, time);
        }

        auto shouldSetVariable = false;
        auto attributes = mnview.GetAttributes();

        for (const auto& [loanTokenId, paybackAmounts] : obj.loans)
        {
            const auto loanToken = mnview.GetLoanTokenByID(loanTokenId);
            if (!loanToken)
                return Res::Err("Loan token with id (%s) does not exist!", loanTokenId.ToString());

            for (const auto& kv : paybackAmounts.balances)
            {
                const auto& paybackTokenId = kv.first;
                auto paybackAmount = kv.second;

                if (height >= static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight) && paybackAmount <= 0) {
                    return Res::Err("Valid payback amount required (input: %d@%d)", paybackAmount, paybackTokenId.v);
                }

                CAmount paybackUsdPrice{0}, loanUsdPrice{0}, penaltyPct{COIN};

                auto paybackToken = mnview.GetToken(paybackTokenId);
                if (!paybackToken)
                    return Res::Err("Token with id (%s) does not exists", paybackTokenId.ToString());

                if (loanTokenId != paybackTokenId)
                {
                    if (!IsVaultPriceValid(mnview, obj.vaultId, height))
                        return Res::Err("Cannot payback loan while any of the asset's price is invalid");

                    if (!attributes)
                        return Res::Err("Payback is not currently active");

                    // search in token to token
                    if (paybackTokenId != DCT_ID{0})
                    {
                        CDataStructureV0 activeKey{AttributeTypes::Token, loanTokenId.v, TokenKeys::LoanPayback, paybackTokenId.v};
                        if (!attributes->GetValue(activeKey, false))
                            return Res::Err("Payback of loan via %s token is not currently active", paybackToken->symbol);

                        CDataStructureV0 penaltyKey{AttributeTypes::Token, loanTokenId.v, TokenKeys::LoanPaybackFeePCT, paybackTokenId.v};
                        penaltyPct -= attributes->GetValue(penaltyKey, CAmount{0});
                    }
                    else
                    {
                        CDataStructureV0 activeKey{AttributeTypes::Token, loanTokenId.v, TokenKeys::PaybackDFI};
                        if (!attributes->GetValue(activeKey, false))
                            return Res::Err("Payback of loan via %s token is not currently active", paybackToken->symbol);

                        CDataStructureV0 penaltyKey{AttributeTypes::Token, loanTokenId.v, TokenKeys::PaybackDFIFeePCT};
                        penaltyPct -= attributes->GetValue(penaltyKey, COIN / 100);

                    }

                    // Get token price in USD
                    const CTokenCurrencyPair tokenUsdPair{paybackToken->symbol,"USD"};
                    bool useNextPrice{false}, requireLivePrice{true};
                    const auto resVal = mnview.GetValidatedIntervalPrice(tokenUsdPair, useNextPrice, requireLivePrice);
                    if (!resVal)
                        return std::move(resVal);

                    paybackUsdPrice = MultiplyAmounts(*resVal.val, penaltyPct);

                    // Calculate the DFI amount in DUSD
                    auto usdAmount = MultiplyAmounts(paybackUsdPrice, kv.second);

                    if (loanToken->symbol == "DUSD")
                    {
                        paybackAmount = usdAmount;
                        if (paybackUsdPrice > COIN && paybackAmount < kv.second)
                            return Res::Err("Value/price too high (%s/%s)", GetDecimaleString(kv.second), GetDecimaleString(paybackUsdPrice));
                    }
                    else
                    {
                        // Get dToken price in USD
                        const CTokenCurrencyPair dTokenUsdPair{loanToken->symbol, "USD"};
                        bool useNextPrice{false}, requireLivePrice{true};
                        const auto resVal = mnview.GetValidatedIntervalPrice(dTokenUsdPair, useNextPrice, requireLivePrice);
                        if (!resVal)
                            return std::move(resVal);

                        loanUsdPrice = *resVal.val;

                        paybackAmount = DivideAmounts(usdAmount, loanUsdPrice);
                    }
                }

                const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);
                if (!loanAmounts)
                    return Res::Err("There are no loans on this vault (%s)!", obj.vaultId.GetHex());

                if (!loanAmounts->balances.count(loanTokenId))
                    return Res::Err("There is no loan on token (%s) in this vault!", loanToken->symbol);

                const auto& currentLoanAmount = loanAmounts->balances.at(loanTokenId);

                const auto rate = mnview.GetInterestRate(obj.vaultId, loanTokenId, height);
                if (!rate)
                    return Res::Err("Cannot get interest rate for this token (%s)!", loanToken->symbol);

                auto subInterest = TotalInterest(*rate, height);

                if (subInterest < 0) {
                    TrackNegativeInterest(mnview, {loanTokenId, currentLoanAmount > std::abs(subInterest) ? std::abs(subInterest) : subInterest});
                }

                // In the case of negative subInterest the amount ends up being added to paybackAmount
                auto subLoan = paybackAmount - subInterest;

                if (paybackAmount < subInterest)
                {
                    subInterest = paybackAmount;
                    subLoan = 0;
                }
                else if (currentLoanAmount - subLoan < 0)
                {
                    subLoan = currentLoanAmount;
                }

                res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{loanTokenId, subLoan});
                if (!res)
                    return res;

                // Eraseinterest. On subInterest is nil interest ITH and IPB will be updated, if
                // subInterest is negative or IPB is negative and subLoan is equal to the loan amount
                // then IPB will be updated and ITH will be wiped.
                res = mnview.DecreaseInterest(height, obj.vaultId, vault->schemeId, loanTokenId, subLoan,
                    subInterest < 0 || (rate->interestPerBlock.negative && subLoan == currentLoanAmount) ? std::numeric_limits<CAmount>::max() : subInterest);
                if (!res)
                    return res;

                if (height >= static_cast<uint32_t>(consensus.FortCanningMuseumHeight) &&
                    subLoan < currentLoanAmount &&
                    height < static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight))
                {
                    auto newRate = mnview.GetInterestRate(obj.vaultId, loanTokenId, height);
                    if (!newRate)
                        return Res::Err("Cannot get interest rate for this token (%s)!", loanToken->symbol);

                    if (newRate->interestPerBlock.amount == 0)
                        return Res::Err("Cannot payback this amount of loan for %s, either payback full amount or less than this amount!", loanToken->symbol);
                }

                CalculateOwnerRewards(obj.from);

                if (paybackTokenId == loanTokenId)
                {
                    // If interest was negative remove it from sub amount
                    if (height >= static_cast<uint32_t>(consensus.FortCanningEpilogueHeight) && subInterest < 0)
                        subLoan += subInterest;

                    res = mnview.SubMintedTokens(loanTokenId, subLoan);
                    if (!res)
                        return res;

                    // Do not sub balance if negative interest fully negates the current loan amount
                    if (!(subInterest < 0 && std::abs(subInterest) >= currentLoanAmount)) {

                        // If negative interest plus payback amount overpays then reduce payback amount by the difference
                        if (subInterest < 0 && paybackAmount - subInterest > currentLoanAmount) {
                            subLoan = currentLoanAmount + subInterest;
                        }

                        // subtract loan amount first, interest is burning below
                        LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Sub loan from balance - %lld, height - %d\n", subLoan, height);
                        res = mnview.SubBalance(obj.from, CTokenAmount{loanTokenId, subLoan});
                        if (!res)
                            return res;
                    }

                    // burn interest Token->USD->DFI->burnAddress
                    if (subInterest > 0)
                    {
                        LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Swapping %s interest to DFI - %lld, height - %d\n", loanToken->symbol, subInterest, height);
                        res = SwapToDFIorDUSD(mnview, loanTokenId, subInterest, obj.from, consensus.burnAddress, height);
                    }
                }
                else
                {
                    CAmount subInToken;
                    const auto subAmount = subLoan + subInterest;

                    // if payback overpay loan and interest amount
                    if (paybackAmount > subAmount)
                    {
                        if (loanToken->symbol == "DUSD")
                        {
                            subInToken = DivideAmounts(subAmount, paybackUsdPrice);
                            if (MultiplyAmounts(subInToken, paybackUsdPrice) != subAmount)
                                subInToken += 1;
                        }
                        else
                        {
                            auto tempAmount = MultiplyAmounts(subAmount, loanUsdPrice);

                            subInToken = DivideAmounts(tempAmount, paybackUsdPrice);
                            if (DivideAmounts(MultiplyAmounts(subInToken, paybackUsdPrice), loanUsdPrice) != subAmount)
                                subInToken += 1;
                        }
                    }
                    else
                    {
                        subInToken = kv.second;
                    }

                    shouldSetVariable = true;

                    auto penalty = MultiplyAmounts(subInToken, COIN - penaltyPct);

                    if (paybackTokenId == DCT_ID{0})
                    {
                        CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackDFITokens};
                        auto balances = attributes->GetValue(liveKey, CBalances{});

                        balances.Add(CTokenAmount{loanTokenId, subAmount});
                        balances.Add(CTokenAmount{paybackTokenId, penalty});
                        attributes->SetValue(liveKey, balances);

                        LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Burning interest and loan in %s directly - total loan %lld (%lld %s), height - %d\n", paybackToken->symbol, subLoan + subInterest, subInToken, paybackToken->symbol, height);

                        res = TransferTokenBalance(paybackTokenId, subInToken, obj.from, consensus.burnAddress);
                    }
                    else
                    {
                        CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackTokens};
                        auto balances = attributes->GetValue(liveKey, CTokenPayback{});

                        balances.tokensPayback.Add(CTokenAmount{loanTokenId, subAmount});
                        balances.tokensFee.Add(CTokenAmount{paybackTokenId, penalty});
                        attributes->SetValue(liveKey, balances);

                        LogPrint(BCLog::LOAN, "CLoanPaybackLoanMessage(): Swapping %s to DFI and burning it - total loan %lld (%lld %s), height - %d\n", paybackToken->symbol, subLoan + subInterest, subInToken, paybackToken->symbol, height);

                        CDataStructureV0 directBurnKey{AttributeTypes::Param, ParamIDs::DFIP2206A, DFIPKeys::DUSDLoanBurn};
                        auto directLoanBurn = attributes->GetValue(directBurnKey, false);

                        res = SwapToDFIorDUSD(mnview, paybackTokenId, subInToken, obj.from, consensus.burnAddress, height, !directLoanBurn);
                    }
                }

                if (!res)
                    return res;
            }
        }

        return shouldSetVariable ? mnview.SetVariable(*attributes) : Res::Ok();
    }

    Res operator()(const CAuctionBidMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        // owner auth
        if (!HasAuth(obj.from))
            return Res::Err("tx must have at least one input from token owner");

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        if (!vault->isUnderLiquidation)
            return Res::Err("Cannot bid to vault which is not under liquidation");

        auto data = mnview.GetAuction(obj.vaultId, height);
        if (!data)
            return Res::Err("No auction data to vault %s", obj.vaultId.GetHex());

        auto batch = mnview.GetAuctionBatch({obj.vaultId, obj.index});
        if (!batch)
            return Res::Err("No batch to vault/index %s/%d", obj.vaultId.GetHex(), obj.index);

        if (obj.amount.nTokenId != batch->loanAmount.nTokenId)
            return Res::Err("Bid token does not match auction one");

        auto bid = mnview.GetAuctionBid({obj.vaultId, obj.index});
        if (!bid) {
            auto amount = MultiplyAmounts(batch->loanAmount.nValue, COIN + data->liquidationPenalty);
            if (amount > obj.amount.nValue)
                return Res::Err("First bid should include liquidation penalty of %d%%", data->liquidationPenalty * 100 / COIN);

            if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight
            && data->liquidationPenalty && obj.amount.nValue == batch->loanAmount.nValue)
                return Res::Err("First bid should be higher than batch one");
        } else {
            auto amount = MultiplyAmounts(bid->second.nValue, COIN + (COIN / 100));
            if (amount > obj.amount.nValue)
                return Res::Err("Bid override should be at least 1%% higher than current one");

            if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight
            && obj.amount.nValue == bid->second.nValue)
                return Res::Err("Bid override should be higher than last one");

            // immediate refund previous bid
            CalculateOwnerRewards(bid->first);
            mnview.AddBalance(bid->first, bid->second);
        }
        //check balance
        CalculateOwnerRewards(obj.from);
        res = mnview.SubBalance(obj.from, obj.amount);
        return !res ? res : mnview.StoreAuctionBid({obj.vaultId, obj.index}, {obj.from, obj.amount});
    }

    Res operator()(const CCustomTxMessageNone&) const {
        return Res::Ok();
    }
};

Res CustomMetadataParse(uint32_t height, const Consensus::Params& consensus, const std::vector<unsigned char>& metadata, CCustomTxMessage& txMessage) {
    try {
        return std::visit(CCustomMetadataParseVisitor(height, consensus, metadata), txMessage);
    } catch (const std::exception& e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

bool IsDisabledTx(uint32_t height, CustomTxType type, const Consensus::Params& consensus) {
    // All the heights that are involved in disabled Txs
    auto fortCanningParkHeight = static_cast<uint32_t>(consensus.FortCanningParkHeight);
    auto fortCanningHillHeight = static_cast<uint32_t>(consensus.FortCanningHillHeight);

    if (height < fortCanningParkHeight)
        return false;

    // For additional safety, since some APIs do block + 1 calc
    if (height == fortCanningHillHeight || height == fortCanningHillHeight - 1) {
        switch (type) {
            case CustomTxType::TakeLoan:
            case CustomTxType::PaybackLoan:
            case CustomTxType::DepositToVault:
            case CustomTxType::WithdrawFromVault:
            case CustomTxType::UpdateVault:
                return true;
            default:
                break;
            }
    }

    // ICXCreateOrder      = '1',
    // ICXMakeOffer        = '2',
    // ICXSubmitDFCHTLC    = '3',
    // ICXSubmitEXTHTLC    = '4',
    // ICXClaimDFCHTLC     = '5',
    // ICXCloseOrder       = '6',
    // ICXCloseOffer       = '7',

    // disable ICX orders for all networks other than testnet
    if (Params().NetworkIDString() == CBaseChainParams::REGTEST ||
        (Params().NetworkIDString() == CBaseChainParams::TESTNET && static_cast<int>(height) >= 1250000)) {
        return false;
    }

    // Leaving close orders, as withdrawal of existing should be ok
    switch (type) {
        case CustomTxType::ICXCreateOrder:
        case CustomTxType::ICXMakeOffer:
        case CustomTxType::ICXSubmitDFCHTLC:
        case CustomTxType::ICXSubmitEXTHTLC:
        case CustomTxType::ICXClaimDFCHTLC:
            return true;
        default:
            return false;
    }
}

bool IsDisabledTx(uint32_t height, const CTransaction& tx, const Consensus::Params& consensus) {
    TBytes dummy;
    auto txType = GuessCustomTxType(tx, dummy);
    return IsDisabledTx(height, txType, consensus);
}

Res CustomTxVisit(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, uint32_t height, const Consensus::Params& consensus, const CCustomTxMessage& txMessage, uint64_t time, uint32_t txn) {
    if (IsDisabledTx(height, tx, consensus)) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "Disabled custom transaction");
    }
    try {
        return std::visit(CCustomTxApplyVisitor(tx, height, coins, mnview, consensus, time, txn), txMessage);
    } catch (const std::bad_variant_access& e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

bool ShouldReturnNonFatalError(const CTransaction& tx, uint32_t height) {
    static const std::map<uint32_t, uint256> skippedTx = {
        { 471222, uint256S("0ab0b76352e2d865761f4c53037041f33e1200183d55cdf6b09500d6f16b7329") },
    };
    auto it = skippedTx.find(height);
    return it != skippedTx.end() && it->second == tx.GetHash();
}

void PopulateVaultHistoryData(CHistoryWriters* writers, CAccountsHistoryWriter& view, const CCustomTxMessage& txMessage, const CustomTxType txType, const uint32_t height, const uint32_t txn, const uint256& txid) {
    if (txType == CustomTxType::Vault) {
        auto obj = std::get<CVaultMessage>(txMessage);
        writers->schemeID = obj.schemeId;
        view.vaultID = txid;
    } else if (txType == CustomTxType::CloseVault) {
        auto obj = std::get<CCloseVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::UpdateVault) {
        auto obj = std::get<CUpdateVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
        if (!obj.schemeId.empty()) {
            writers->schemeID = obj.schemeId;
        }
    } else if (txType == CustomTxType::DepositToVault) {
        auto obj = std::get<CDepositToVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::WithdrawFromVault) {
        auto obj = std::get<CWithdrawFromVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::PaybackWithCollateral) {
        auto obj = std::get<CPaybackWithCollateralMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::TakeLoan) {
        auto obj = std::get<CLoanTakeLoanMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::PaybackLoan) {
        auto obj = std::get<CLoanPaybackLoanMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::PaybackLoanV2) {
        auto obj = std::get<CLoanPaybackLoanV2Message>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::AuctionBid) {
        auto obj = std::get<CAuctionBidMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::LoanScheme) {
        auto obj = std::get<CLoanSchemeMessage>(txMessage);
        writers->globalLoanScheme.identifier = obj.identifier;
        writers->globalLoanScheme.ratio = obj.ratio;
        writers->globalLoanScheme.rate = obj.rate;
        if (!obj.updateHeight) {
            writers->globalLoanScheme.schemeCreationTxid = txid;
        } else {
            writers->vaultView->ForEachGlobalScheme([&writers](VaultGlobalSchemeKey const & key, CLazySerialize<VaultGlobalSchemeValue> value) {
                if (value.get().loanScheme.identifier != writers->globalLoanScheme.identifier) {
                    return true;
                }
                writers->globalLoanScheme.schemeCreationTxid = key.schemeCreationTxid;
                return false;
            }, {height, txn, {}});
        }
    }
}

Res ApplyCustomTx(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height, uint64_t time, uint32_t txn, CHistoryWriters* writers) {
    auto res = Res::Ok();
    if (tx.IsCoinBase() && height > 0) { // genesis contains custom coinbase txs
        return res;
    }
    std::vector<unsigned char> metadata;
    const auto metadataValidation = height >= static_cast<uint32_t>(consensus.FortCanningHeight);

    auto txType = GuessCustomTxType(tx, metadata, metadataValidation);
    if (txType == CustomTxType::None) {
        return res;
    }

    if (metadataValidation && txType == CustomTxType::Reject) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "Invalid custom transaction");
    }
    auto txMessage = customTypeToMessage(txType);
    CAccountsHistoryWriter view(mnview, height, txn, tx.GetHash(), uint8_t(txType), writers);
    if ((res = CustomMetadataParse(height, consensus, metadata, txMessage))) {
        if (pvaultHistoryDB && writers) {
           PopulateVaultHistoryData(writers, view, txMessage, txType, height, txn, tx.GetHash());
        }
        res = CustomTxVisit(view, coins, tx, height, consensus, txMessage, time, txn);

        // Track burn fee
        if (txType == CustomTxType::CreateToken || txType == CustomTxType::CreateMasternode) {
            if (writers) {
                writers->AddFeeBurn(tx.vout[0].scriptPubKey, tx.vout[0].nValue);
            }
        }
        if (txType == CustomTxType::Vault) {
            // burn the half, the rest is returned on close vault
            auto burnFee = tx.vout[0].nValue / 2;
            if (writers) {
                writers->AddFeeBurn(tx.vout[0].scriptPubKey, burnFee);
            }
        }
    }
    // list of transactions which aren't allowed to fail:
    if (!res) {
        res.msg = strprintf("%sTx: %s", ToString(txType), res.msg);

        if (NotAllowedToFail(txType, height)) {
            if (ShouldReturnNonFatalError(tx, height)) {
                return res;
            }
            res.code |= CustomTxErrCodes::Fatal;
        }
        if (height >= static_cast<uint32_t>(consensus.DakotaHeight)) {
            res.code |= CustomTxErrCodes::Fatal;
        }
        return res;
    }

    // construct undo
    auto& flushable = view.GetStorage();
    auto undo = CUndo::Construct(mnview.GetStorage(), flushable.GetRaw());
    // flush changes
    view.Flush();
    // write undo
    if (!undo.before.empty()) {
        mnview.SetUndo(UndoKey{height, tx.GetHash()}, undo);
    }
    return res;
}

ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView & mnview, CTransaction const & tx, int height, uint256 const & prevStakeModifier, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if (height >= consensusParams.DakotaHeight) {
        return Res::Err("Old anchor TX type after Dakota fork. Height %d", height);
    }

    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    CAnchorFinalizationMessage finMsg;
    ss >> finMsg;

    auto rewardTx = mnview.GetRewardForAnchor(finMsg.btcTxHash);
    if (rewardTx) {
        return Res::ErrDbg("bad-ar-exists", "reward for anchor %s already exists (tx: %s)",
                           finMsg.btcTxHash.ToString(), (*rewardTx).ToString());
    }

    if (!finMsg.CheckConfirmSigs()) {
        return Res::ErrDbg("bad-ar-sigs", "anchor signatures are incorrect");
    }

    if (finMsg.sigs.size() < GetMinAnchorQuorum(finMsg.currentTeam)) {
        return Res::ErrDbg("bad-ar-sigs-quorum", "anchor sigs (%d) < min quorum (%) ",
                           finMsg.sigs.size(), GetMinAnchorQuorum(finMsg.currentTeam));
    }

    // check reward sum
    if (height >= consensusParams.AMKHeight) {
        auto const cbValues = tx.GetValuesOut();
        if (cbValues.size() != 1 || cbValues.begin()->first != DCT_ID{0})
            return Res::ErrDbg("bad-ar-wrong-tokens", "anchor reward should be payed only in Defi coins");

        auto const anchorReward = mnview.GetCommunityBalance(CommunityAccountType::AnchorReward);
        if (cbValues.begin()->second != anchorReward) {
            return Res::ErrDbg("bad-ar-amount", "anchor pays wrong amount (actual=%d vs expected=%d)",
                               cbValues.begin()->second, anchorReward);
        }
    }
    else { // pre-AMK logic
        auto anchorReward = GetAnchorSubsidy(finMsg.anchorHeight, finMsg.prevAnchorHeight, consensusParams);
        if (tx.GetValueOut() > anchorReward) {
            return Res::ErrDbg("bad-ar-amount", "anchor pays too much (actual=%d vs limit=%d)",
                               tx.GetValueOut(), anchorReward);
        }
    }

    CTxDestination destination = finMsg.rewardKeyType == 1 ? CTxDestination(PKHash(finMsg.rewardKeyID)) : CTxDestination(WitnessV0KeyHash(finMsg.rewardKeyID));
    if (tx.vout[1].scriptPubKey != GetScriptForDestination(destination)) {
        return Res::ErrDbg("bad-ar-dest", "anchor pay destination is incorrect");
    }

    if (finMsg.currentTeam != mnview.GetCurrentTeam()) {
        return Res::ErrDbg("bad-ar-curteam", "anchor wrong current team");
    }

    if (finMsg.nextTeam != mnview.CalcNextTeam(height, prevStakeModifier)) {
        return Res::ErrDbg("bad-ar-nextteam", "anchor wrong next team");
    }
    mnview.SetTeam(finMsg.nextTeam);
    if (height >= consensusParams.AMKHeight) {
        LogPrint(BCLog::ACCOUNTCHANGE, "AccountChange: txid=%s fund=%s change=%s\n", tx.GetHash().ToString(), GetCommunityAccountName(CommunityAccountType::AnchorReward), (CBalances{{{{0}, -mnview.GetCommunityBalance(CommunityAccountType::AnchorReward)}}}.ToString()));
        mnview.SetCommunityBalance(CommunityAccountType::AnchorReward, 0); // just reset
    }
    else {
        mnview.SetFoundationsDebt(mnview.GetFoundationsDebt() + tx.GetValueOut());
    }

    return { finMsg.btcTxHash, Res::Ok() };
}

ResVal<uint256> ApplyAnchorRewardTxPlus(CCustomCSView & mnview, CTransaction const & tx, int height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if (height < consensusParams.DakotaHeight) {
        return Res::Err("New anchor TX type before Dakota fork. Height %d", height);
    }

    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    CAnchorFinalizationMessagePlus finMsg;
    ss >> finMsg;

    auto rewardTx = mnview.GetRewardForAnchor(finMsg.btcTxHash);
    if (rewardTx) {
        return Res::ErrDbg("bad-ar-exists", "reward for anchor %s already exists (tx: %s)",
                           finMsg.btcTxHash.ToString(), (*rewardTx).ToString());
    }

    // Miner used confirm team at chain height when creating this TX, this is height - 1.
    int anchorHeight = height - 1;
    auto uniqueKeys = finMsg.CheckConfirmSigs(anchorHeight);
    if (!uniqueKeys) {
        return Res::ErrDbg("bad-ar-sigs", "anchor signatures are incorrect");
    }

    auto team = mnview.GetConfirmTeam(anchorHeight);
    if (!team) {
        return Res::ErrDbg("bad-ar-team", "could not get confirm team for height: %d", anchorHeight);
    }

    auto quorum = GetMinAnchorQuorum(*team);
    if (finMsg.sigs.size() < quorum) {
        return Res::ErrDbg("bad-ar-sigs-quorum", "anchor sigs (%d) < min quorum (%) ",
                           finMsg.sigs.size(), quorum);
    }

    if (uniqueKeys < quorum) {
        return Res::ErrDbg("bad-ar-sigs-quorum", "anchor unique keys (%d) < min quorum (%) ",
                           uniqueKeys, quorum);
    }

    // Make sure anchor block height and hash exist in chain.
    CBlockIndex* anchorIndex = ::ChainActive()[finMsg.anchorHeight];
    if (!anchorIndex) {
        return Res::ErrDbg("bad-ar-height", "Active chain does not contain block height %d. Chain height %d",
                           finMsg.anchorHeight, ::ChainActive().Height());
    }

    if (anchorIndex->GetBlockHash() != finMsg.dfiBlockHash) {
        return Res::ErrDbg("bad-ar-hash", "Anchor and blockchain mismatch at height %d. Expected %s found %s",
                           finMsg.anchorHeight, anchorIndex->GetBlockHash().ToString(), finMsg.dfiBlockHash.ToString());
    }

    // check reward sum
    auto const cbValues = tx.GetValuesOut();
    if (cbValues.size() != 1 || cbValues.begin()->first != DCT_ID{0})
        return Res::ErrDbg("bad-ar-wrong-tokens", "anchor reward should be paid in DFI only");

    auto const anchorReward = mnview.GetCommunityBalance(CommunityAccountType::AnchorReward);
    if (cbValues.begin()->second != anchorReward) {
        return Res::ErrDbg("bad-ar-amount", "anchor pays wrong amount (actual=%d vs expected=%d)",
                           cbValues.begin()->second, anchorReward);
    }

    CTxDestination destination = finMsg.rewardKeyType == 1 ? CTxDestination(PKHash(finMsg.rewardKeyID)) : CTxDestination(WitnessV0KeyHash(finMsg.rewardKeyID));
    if (tx.vout[1].scriptPubKey != GetScriptForDestination(destination)) {
        return Res::ErrDbg("bad-ar-dest", "anchor pay destination is incorrect");
    }

    LogPrint(BCLog::ACCOUNTCHANGE, "AccountChange: txid=%s fund=%s change=%s\n", tx.GetHash().ToString(), GetCommunityAccountName(CommunityAccountType::AnchorReward), (CBalances{{{{0}, -mnview.GetCommunityBalance(CommunityAccountType::AnchorReward)}}}.ToString()));
    mnview.SetCommunityBalance(CommunityAccountType::AnchorReward, 0); // just reset
    mnview.AddRewardForAnchor(finMsg.btcTxHash, tx.GetHash());

    // Store reward data for RPC info
    mnview.AddAnchorConfirmData(CAnchorConfirmDataPlus{finMsg});

    return { finMsg.btcTxHash, Res::Ok() };
}

bool IsMempooledCustomTxCreate(const CTxMemPool & pool, const uint256 & txid)
{
    CTransactionRef ptx = pool.get(txid);
    if (ptx) {
        std::vector<unsigned char> dummy;
        CustomTxType txType = GuessCustomTxType(*ptx, dummy);
        return txType == CustomTxType::CreateMasternode || txType == CustomTxType::CreateToken;
    }
    return false;
}

std::vector<DCT_ID> CPoolSwap::CalculateSwaps(CCustomCSView& view, bool testOnly) {

    std::vector<std::vector<DCT_ID>> poolPaths = CalculatePoolPaths(view);

    // Record best pair
    std::pair<std::vector<DCT_ID>, CAmount> bestPair{{}, -1};

    // Loop through all common pairs
    for (const auto& path : poolPaths) {

        // Test on copy of view
        CCustomCSView dummy(view);

        // Execute pool path
        auto res = ExecuteSwap(dummy, path, testOnly);

        // Add error for RPC user feedback
        if (!res) {
            const auto token = dummy.GetToken(currentID);
            if (token) {
                errors.emplace_back(token->symbol, res.msg);
            }
        }

        // Record amount if more than previous or default value
        if (res && result > bestPair.second) {
            bestPair = {path, result};
        }
    }

    return bestPair.first;
}

std::vector<std::vector<DCT_ID>> CPoolSwap::CalculatePoolPaths(CCustomCSView& view) {

    std::vector<std::vector<DCT_ID>> poolPaths;

    // For tokens to be traded get all pairs and pool IDs
    std::multimap<uint32_t, DCT_ID> fromPoolsID, toPoolsID;
    view.ForEachPoolPair([&](DCT_ID const & id, const CPoolPair& pool) {
        if ((obj.idTokenFrom == pool.idTokenA && obj.idTokenTo == pool.idTokenB)
        || (obj.idTokenTo == pool.idTokenA && obj.idTokenFrom == pool.idTokenB)) {
            // Push poolId when direct path
            poolPaths.push_back({{id}});
        }

        if (pool.idTokenA == obj.idTokenFrom) {
            fromPoolsID.emplace(pool.idTokenB.v, id);
        } else if (pool.idTokenB == obj.idTokenFrom) {
            fromPoolsID.emplace(pool.idTokenA.v, id);
        }

        if (pool.idTokenA == obj.idTokenTo) {
            toPoolsID.emplace(pool.idTokenB.v, id);
        } else if (pool.idTokenB == obj.idTokenTo) {
            toPoolsID.emplace(pool.idTokenA.v, id);
        }
        return true;
    }, {0});

    if (fromPoolsID.empty() || toPoolsID.empty()) {
        return {};
    }

    // Find intersection on key
    std::map<uint32_t, DCT_ID> commonPairs;
    set_intersection(fromPoolsID.begin(), fromPoolsID.end(), toPoolsID.begin(), toPoolsID.end(),
                     std::inserter(commonPairs, commonPairs.begin()),
                     [](std::pair<uint32_t, DCT_ID> a, std::pair<uint32_t, DCT_ID> b) {
                         return a.first < b.first;
                     });

    // Loop through all common pairs and record direct pool to pool swaps
    for (const auto& item : commonPairs) {
        // Loop through all source/intermediate pools matching common pairs
        const auto poolFromIDs = fromPoolsID.equal_range(item.first);
        for (auto fromID = poolFromIDs.first; fromID != poolFromIDs.second; ++fromID) {

            // Loop through all destination pools matching common pairs
            const auto poolToIDs = toPoolsID.equal_range(item.first);
            for (auto toID = poolToIDs.first; toID != poolToIDs.second; ++toID) {
                // Add to pool paths
                poolPaths.push_back({fromID->second, toID->second});
            }
        }
    }

    // Look for pools that bridges token. Might be in addition to common token pairs paths.
    view.ForEachPoolPair([&](DCT_ID const & id, const CPoolPair& pool) {

        // Loop through from pool multimap on unique keys only
        for (auto fromIt = fromPoolsID.begin(); fromIt != fromPoolsID.end(); fromIt = fromPoolsID.equal_range(fromIt->first).second) {

            // Loop through to pool multimap on unique keys only
            for (auto toIt = toPoolsID.begin(); toIt != toPoolsID.end(); toIt = toPoolsID.equal_range(toIt->first).second) {

                // If a pool pairs matches from pair and to pair add it to the pool paths
                if ((fromIt->first == pool.idTokenA.v && toIt->first == pool.idTokenB.v) ||
                    (fromIt->first == pool.idTokenB.v && toIt->first == pool.idTokenA.v)) {
                    poolPaths.push_back({fromIt->second, id, toIt->second});
                }
            }
        }
        return true;
    }, {0});

    // return pool paths
    return poolPaths;
}

// Note: `testOnly` doesn't update views, and as such can result in a previous price calculations
// for a pool, if used multiple times (or duplicated pool IDs) with the same view.
// testOnly is only meant for one-off tests per well defined view.
Res CPoolSwap::ExecuteSwap(CCustomCSView& view, std::vector<DCT_ID> poolIDs, bool testOnly) {

    Res poolResult = Res::Ok();

    // No composite swap allowed before Fort Canning
    if (height < static_cast<uint32_t>(Params().GetConsensus().FortCanningHeight) && !poolIDs.empty()) {
        poolIDs.clear();
    }

    if (obj.amountFrom <= 0) {
        return Res::Err("Input amount should be positive");
    }

    // Single swap if no pool IDs provided
    auto poolPrice = POOLPRICE_MAX;
    std::optional<std::pair<DCT_ID, CPoolPair> > poolPair;
    if (poolIDs.empty()) {
        poolPair = view.GetPoolPair(obj.idTokenFrom, obj.idTokenTo);
        if (!poolPair) {
            return Res::Err("Cannot find the pool pair.");
        }

        // Add single swap pool to vector for loop
        poolIDs.push_back(poolPair->first);

        // Get legacy max price
        poolPrice = obj.maxPrice;
    }

    if (!testOnly) {
        CCustomCSView mnview(view);
        mnview.CalculateOwnerRewards(obj.from, height);
        mnview.CalculateOwnerRewards(obj.to, height);
        mnview.Flush();
    }

    auto attributes = view.GetAttributes();
    assert(attributes);

    CDataStructureV0 dexKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DexTokens};
    auto dexBalances = attributes->GetValue(dexKey, CDexBalances{});

    // Set amount to be swapped in pool
    CTokenAmount swapAmountResult{obj.idTokenFrom, obj.amountFrom};

    for (size_t i{0}; i < poolIDs.size(); ++i) {

        // Also used to generate pool specific error messages for RPC users
        currentID = poolIDs[i];

        // Use single swap pool if already found
        std::optional<CPoolPair> pool;
        if (poolPair) {
            pool = poolPair->second;
        }
        else // Or get pools from IDs provided for composite swap
        {
            pool = view.GetPoolPair(currentID);
            if (!pool) {
                return Res::Err("Cannot find the pool pair.");
            }
        }

        // Check if last pool swap
        bool lastSwap = i + 1 == poolIDs.size();

        const auto swapAmount = swapAmountResult;

        if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight) && lastSwap) {
            if (obj.idTokenTo == swapAmount.nTokenId) {
                return Res::Err("Final swap should have idTokenTo as destination, not source");
            }
            if (pool->idTokenA != obj.idTokenTo && pool->idTokenB != obj.idTokenTo) {
                return Res::Err("Final swap pool should have idTokenTo, incorrect final pool ID provided");
            }
        }

        if (view.AreTokensLocked({pool->idTokenA.v, pool->idTokenB.v})) {
            return Res::Err("Pool currently disabled due to locked token");
        }

        CDataStructureV0 dirAKey{AttributeTypes::Poolpairs, currentID.v, PoolKeys::TokenAFeeDir};
        CDataStructureV0 dirBKey{AttributeTypes::Poolpairs, currentID.v, PoolKeys::TokenBFeeDir};
        const auto dirA = attributes->GetValue(dirAKey, CFeeDir{FeeDirValues::Both});
        const auto dirB = attributes->GetValue(dirBKey, CFeeDir{FeeDirValues::Both});
        const auto asymmetricFee = std::make_pair(dirA, dirB);

        auto dexfeeInPct = view.GetDexFeeInPct(currentID, swapAmount.nTokenId);
        auto& balances = dexBalances[currentID];
        auto forward = swapAmount.nTokenId == pool->idTokenA;

        auto& totalTokenA = forward ? balances.totalTokenA : balances.totalTokenB;
        auto& totalTokenB = forward ? balances.totalTokenB : balances.totalTokenA;

        const auto& reserveAmount = forward ? pool->reserveA : pool->reserveB;
        const auto& blockCommission = forward ? pool->blockCommissionA : pool->blockCommissionB;

        const auto initReserveAmount = reserveAmount;
        const auto initBlockCommission = blockCommission;

        // Perform swap
        poolResult = pool->Swap(swapAmount, dexfeeInPct, poolPrice, asymmetricFee, [&] (const CTokenAmount& dexfeeInAmount, const CTokenAmount& tokenAmount) {
            // Save swap amount for next loop
            swapAmountResult = tokenAmount;

            CTokenAmount dexfeeOutAmount{tokenAmount.nTokenId, 0};

            auto dexfeeOutPct = view.GetDexFeeOutPct(currentID, tokenAmount.nTokenId);
            if (dexfeeOutPct > 0 && poolOutFee(swapAmount.nTokenId == pool->idTokenA, asymmetricFee)) {
                dexfeeOutAmount.nValue = MultiplyAmounts(tokenAmount.nValue, dexfeeOutPct);
                swapAmountResult.nValue -= dexfeeOutAmount.nValue;
            }

            // If we're just testing, don't do any balance transfers.
            // Just go over pools and return result. The only way this can
            // cause inaccurate result is if we go over the same path twice,
            // which shouldn't happen in the first place.
            if (testOnly)
                return Res::Ok();

            auto res = view.SetPoolPair(currentID, height, *pool);
            if (!res) {
                return res;
            }

            CCustomCSView intermediateView(view);
            // hide interemidiate swaps
            auto& subView = i == 0 ? view : intermediateView;
            res = subView.SubBalance(obj.from, swapAmount);
            if (!res) {
                return res;
            }
            intermediateView.Flush();

            auto& addView = lastSwap ? view : intermediateView;
            res = addView.AddBalance(lastSwap ? obj.to : obj.from, swapAmountResult);
            if (!res) {
                return res;
            }
            intermediateView.Flush();

            // burn the dex in amount
            if (dexfeeInAmount.nValue > 0) {
                res = view.AddBalance(Params().GetConsensus().burnAddress, dexfeeInAmount);
                if (!res) {
                    return res;
                }
                totalTokenA.feeburn += dexfeeInAmount.nValue;
            }

            // burn the dex out amount
            if (dexfeeOutAmount.nValue > 0) {
                res = view.AddBalance(Params().GetConsensus().burnAddress, dexfeeOutAmount);
                if (!res) {
                    return res;
                }
                totalTokenB.feeburn += dexfeeOutAmount.nValue;
            }

            totalTokenA.swaps += (reserveAmount - initReserveAmount);
            totalTokenA.commissions += (blockCommission - initBlockCommission);

            if (lastSwap && obj.to == Params().GetConsensus().burnAddress) {
                totalTokenB.feeburn += swapAmountResult.nValue;
            }

           return res;
        }, static_cast<int>(height));

        if (!poolResult) {
            return poolResult;
        }
    }

    // Reject if price paid post-swap above max price provided
    if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHeight) && obj.maxPrice != POOLPRICE_MAX) {
        if (swapAmountResult.nValue != 0) {
            const auto userMaxPrice = arith_uint256(obj.maxPrice.integer) * COIN + obj.maxPrice.fraction;
            if (arith_uint256(obj.amountFrom) * COIN / swapAmountResult.nValue > userMaxPrice) {
                return Res::Err("Price is higher than indicated.");
            }
        }
    }

    if (!testOnly && view.GetDexStatsEnabled().value_or(false)) {
        attributes->SetValue(dexKey, std::move(dexBalances));
        view.SetVariable(*attributes);
    }
    // Assign to result for loop testing best pool swap result
    result = swapAmountResult.nValue;

    return poolResult;
}

Res  SwapToDFIorDUSD(CCustomCSView & mnview, DCT_ID tokenId, CAmount amount, CScript const & from, CScript const & to, uint32_t height, bool forceLoanSwap)
{
    CPoolSwapMessage obj;

    obj.from = from;
    obj.to = to;
    obj.idTokenFrom = tokenId;
    obj.idTokenTo = DCT_ID{0};
    obj.amountFrom = amount;
    obj.maxPrice = POOLPRICE_MAX;

    auto poolSwap = CPoolSwap(obj, height);
    auto token = mnview.GetToken(tokenId);
    if (!token)
        return Res::Err("Cannot find token with id %s!", tokenId.ToString());

    // TODO: Optimize double look up later when first token is DUSD.
    auto dUsdToken = mnview.GetToken("DUSD");
    if (!dUsdToken)
        return Res::Err("Cannot find token DUSD");

    const auto attributes = mnview.GetAttributes();
    if (!attributes) {
        return Res::Err("Attributes unavailable");
    }
    CDataStructureV0 directBurnKey{AttributeTypes::Param, ParamIDs::DFIP2206A, DFIPKeys::DUSDInterestBurn};

    // Direct swap from DUSD to DFI as defined in the CPoolSwapMessage.
    if (tokenId == dUsdToken->first) {
        if (to == Params().GetConsensus().burnAddress && !forceLoanSwap && attributes->GetValue(directBurnKey, false))
        {
            // direct burn dUSD
            CTokenAmount dUSD{dUsdToken->first, amount};

            auto res = mnview.SubBalance(from, dUSD);
            if (!res)
                return res;

            return mnview.AddBalance(to, dUSD);
        }
        else
            // swap dUSD -> DFI and burn DFI
            return poolSwap.ExecuteSwap(mnview, {});
    }

    auto pooldUSDDFI = mnview.GetPoolPair(dUsdToken->first, DCT_ID{0});
    if (!pooldUSDDFI)
        return Res::Err("Cannot find pool pair DUSD-DFI!");

    auto poolTokendUSD = mnview.GetPoolPair(tokenId,dUsdToken->first);
    if (!poolTokendUSD)
        return Res::Err("Cannot find pool pair %s-DUSD!", token->symbol);

    if (to == Params().GetConsensus().burnAddress && !forceLoanSwap && attributes->GetValue(directBurnKey, false))
    {
        obj.idTokenTo = dUsdToken->first;

        // swap tokenID -> dUSD and burn dUSD
        return poolSwap.ExecuteSwap(mnview, {});
    }
    else
        // swap tokenID -> dUSD -> DFI and burn DFI
        return poolSwap.ExecuteSwap(mnview, {poolTokendUSD->first, pooldUSDDFI->first});
}

bool IsVaultPriceValid(CCustomCSView& mnview, const CVaultId& vaultId, uint32_t height)
{
    if (auto collaterals = mnview.GetVaultCollaterals(vaultId))
        for (const auto& collateral : collaterals->balances) {
            if (auto collateralToken = mnview.HasLoanCollateralToken({collateral.first, height})) {
                if (auto fixedIntervalPrice = mnview.GetFixedIntervalPrice(collateralToken->fixedIntervalPriceId)) {
                    if (!fixedIntervalPrice.val->isLive(mnview.GetPriceDeviation())) {
                        return false;
                    }
                } else {
                    // No fixed interval prices available. Should not have happened.
                    return false;
                }
            } else {
                // Not a collateral token. Should not have happened.
                return false;
            }
        }

    if (auto loans = mnview.GetLoanTokens(vaultId))
        for (const auto& loan : loans->balances) {
            if (auto loanToken = mnview.GetLoanTokenByID(loan.first)) {
                if (auto fixedIntervalPrice = mnview.GetFixedIntervalPrice(loanToken->fixedIntervalPriceId)) {
                    if (!fixedIntervalPrice.val->isLive(mnview.GetPriceDeviation())) {
                        return false;
                    }
                } else {
                    // No fixed interval prices available. Should not have happened.
                    return false;
                }
            } else {
                // Not a loan token. Should not have happened.
                return false;
            }
        }
    return true;
}

bool IsPaybackWithCollateral(CCustomCSView& view, const std::map<DCT_ID, CBalances>& loans) {
    auto tokenDUSD = view.GetToken("DUSD");
    if (!tokenDUSD)
        return false;

    if (loans.size() == 1
    && loans.count(tokenDUSD->first)
    && loans.at(tokenDUSD->first) == CBalances{{{tokenDUSD->first, 999999999999999999LL}}}) {
        return true;
    }
    return false;
}

Res PaybackWithCollateral(CCustomCSView& view, const CVaultData& vault, const CVaultId& vaultId, uint32_t height, uint64_t time) {
    const auto attributes = view.GetAttributes();
    if (!attributes)
        return Res::Err("Attributes unavailable");

    const auto dUsdToken = view.GetToken("DUSD");
    if (!dUsdToken)
        return Res::Err("Cannot find token DUSD");

    CDataStructureV0 activeKey{AttributeTypes::Token, dUsdToken->first.v, TokenKeys::LoanPaybackCollateral};
    if (!attributes->GetValue(activeKey, false))
        return Res::Err("Payback of DUSD loan with collateral is not currently active");

    const auto collateralAmounts = view.GetVaultCollaterals(vaultId);
    if (!collateralAmounts) {
        return Res::Err("Vault has no collaterals");
    }
    if (!collateralAmounts->balances.count(dUsdToken->first)) {
        return Res::Err("Vault does not have any DUSD collaterals");
    }
    const auto& collateralDUSD = collateralAmounts->balances.at(dUsdToken->first);

    const auto loanAmounts = view.GetLoanTokens(vaultId);
    if (!loanAmounts) {
        return Res::Err("Vault has no loans");
    }
    if (!loanAmounts->balances.count(dUsdToken->first)) {
        return Res::Err("Vault does not have any DUSD loans");
    }
    const auto& loanDUSD = loanAmounts->balances.at(dUsdToken->first);

    const auto rate = view.GetInterestRate(vaultId, dUsdToken->first, height);
    if (!rate)
        return Res::Err("Cannot get interest rate for this token (DUSD)!");
    const auto subInterest = TotalInterest(*rate, height);

    Res res{};
    CAmount subLoanAmount{0};
    CAmount subCollateralAmount{0};
    CAmount burnAmount{0};

    // Case where interest > collateral: decrease interest, wipe collateral.
    if (subInterest > collateralDUSD) {
        subCollateralAmount = collateralDUSD;

        res = view.SubVaultCollateral(vaultId, {dUsdToken->first, subCollateralAmount});
        if (!res) return res;

        res = view.DecreaseInterest(height, vaultId, vault.schemeId, dUsdToken->first, 0, subCollateralAmount);
        if (!res) return res;

        burnAmount = subCollateralAmount;
    } else {
        // Postive interest: Loan + interest > collateral.
        // Negative interest: Loan - abs(interest) > collateral.
        if (loanDUSD + subInterest > collateralDUSD) {
            subLoanAmount = collateralDUSD - subInterest;
            subCollateralAmount = collateralDUSD;
        } else {
            // Common case: Collateral > loans.
            subLoanAmount = loanDUSD;
            subCollateralAmount = loanDUSD + subInterest;
        }

        if (subLoanAmount > 0) {
            res = view.SubLoanToken(vaultId, {dUsdToken->first, subLoanAmount});
            if (!res) return res;
        }

        if (subCollateralAmount > 0) {
            res = view.SubVaultCollateral(vaultId, {dUsdToken->first,subCollateralAmount});
            if (!res) return res;
        }

        view.ResetInterest(height, vaultId, vault.schemeId, dUsdToken->first);
        burnAmount = subInterest;
    }


    if (burnAmount > 0)
    {
        res = view.AddBalance(Params().GetConsensus().burnAddress, {dUsdToken->first, burnAmount});
        if (!res) return res;
    } else {
        TrackNegativeInterest(view, {dUsdToken->first, std::abs(burnAmount)});
    }

    // Guard against liquidation
    const auto collaterals = view.GetVaultCollaterals(vaultId);
    const auto loans = view.GetLoanTokens(vaultId);
    if (!collaterals && loans)
        return Res::Err("Vault cannot have loans without collaterals");

    auto collateralsLoans = view.GetLoanCollaterals(vaultId, *collaterals, height, time);
    if (!collateralsLoans)
        return std::move(collateralsLoans);

    // The check is required to do a ratio check safe guard, or the vault of ratio is unreliable.
    // This can later be removed, if all edge cases of price deviations and max collateral factor for DUSD (1.5 currently)
    // can be tested for economical stability. Taking the safer approach for now.
    if (!IsVaultPriceValid(view, vaultId, height))
        return Res::Err("Cannot payback vault with non-DUSD assets while any of the asset's price is invalid");

    const auto scheme = view.GetLoanScheme(vault.schemeId);
    if (collateralsLoans.val->ratio() < scheme->ratio)
        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme->ratio);

    if (subCollateralAmount > 0) {
        res = view.SubMintedTokens(dUsdToken->first, subCollateralAmount);
        if (!res) return res;
    }

    return Res::Ok();
}

Res storeGovVars(const CGovernanceHeightMessage& obj, CCustomCSView& view) {

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
