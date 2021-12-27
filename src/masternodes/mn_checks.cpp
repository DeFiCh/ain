// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/anchors.h>
#include <masternodes/balances.h>
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
        case CustomTxType::TakeLoan:            return "TakeLoan";
        case CustomTxType::PaybackLoan:         return "PaybackLoan";
        case CustomTxType::AuctionBid:          return "AuctionBid";
        case CustomTxType::Reject:              return "Reject";
        case CustomTxType::None:                return "None";
    }
    return "None";
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
        case CustomTxType::TakeLoan:                return CLoanTakeLoanMessage{};
        case CustomTxType::PaybackLoan:             return CLoanPaybackLoanMessage{};
        case CustomTxType::AuctionBid:              return CAuctionBidMessage{};
        case CustomTxType::Reject:                  return CCustomTxMessageNone{};
        case CustomTxType::None:                    return CCustomTxMessageNone{};
    }
    return CCustomTxMessageNone{};
}

extern std::string ScriptToString(CScript const& script);

class CCustomMetadataParseVisitor : public boost::static_visitor<Res>
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
                return Res::Err("'%s': variable does not registered", name);
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
            return Res::Err("'%s': variable does not registered", name);
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

    Res operator()(CLoanTakeLoanMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLoanPaybackLoanMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CAuctionBidMessage& obj) const {
        auto res = isPostFortCanningFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CCustomTxMessageNone&) const {
        return Res::Ok();
    }
};

class CCustomTxVisitor : public boost::static_visitor<Res>
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

    bool oraclePriceFeed(const CTokenCurrencyPair& priceFeed) const {
        // Allow hard coded DUSD/USD
        if (priceFeed.first == "DUSD" && priceFeed.second == "USD") {
            return true;
        }
        bool found = false;
        mnview.ForEachOracle([&](const COracleId&, COracle oracle) {
            return !(found = oracle.SupportsPair(priceFeed.first, priceFeed.second));
        });
        return found;
    }

    void storeGovVars(const CGovernanceHeightMessage& obj) const {

        // Retrieve any stored GovVariables at startHeight
        auto storedGovVars = mnview.GetStoredVariables(obj.startHeight);

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
        mnview.SetStoredVariables(storedGovVars, obj.startHeight);
    }
};

class CCustomTxApplyVisitor : public CCustomTxVisitor
{
    uint64_t time;
public:
    CCustomTxApplyVisitor(const CTransaction& tx,
                          uint32_t height,
                          const CCoinsViewCache& coins,
                          CCustomCSView& mnview,
                          const Consensus::Params& consensus,
                          uint64_t time)

        : CCustomTxVisitor(tx, height, coins, mnview, consensus), time(time) {}

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
            if (dest.which() == PKHashType) {
                node.ownerType = 1;
                node.ownerAuthAddress = CKeyID(*boost::get<PKHash>(&dest));
            } else if (dest.which() == WitV0KeyHashType) {
                node.ownerType = 4;
                node.ownerAuthAddress = CKeyID(*boost::get<WitnessV0KeyHash>(&dest));
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
        const auto& token = pair->second;

        //check foundation auth
        auto res = HasFoundationAuth();

        if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
            CToken newToken = static_cast<CToken>(token); // keeps old and triggers only DAT!
            newToken.flags ^= (uint8_t)CToken::TokenFlags::DAT;

            return !res ? res : mnview.UpdateToken(token.creationTx, newToken, true);
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

        auto updatedToken = obj.token;
        if (height >= consensus.FortCanningHeight) {
            updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        }

        return mnview.UpdateToken(token.creationTx, updatedToken, false);
    }

    Res operator()(const CMintTokensMessage& obj) const {
        // check auth and increase balance of token's owner
        for (const auto& kv : obj.balances) {
            DCT_ID tokenId = kv.first;

            auto token = mnview.GetToken(kv.first);
            if (!token) {
                return Res::Err("token %s does not exist!", tokenId.ToString());
            }
            auto tokenImpl = static_cast<const CTokenImplementation&>(*token);

            auto mintable = MintableToken(tokenId, tokenImpl);
            if (!mintable) {
                return std::move(mintable);
            }
            auto minted = mnview.AddMintedTokens(tokenImpl.creationTx, kv.second);
            if (!minted) {
                return minted;
            }
            CalculateOwnerRewards(*mintable.val);
            auto res = mnview.AddBalance(*mintable.val, CTokenAmount{kv.first, kv.second});
            if (!res) {
                return res;
            }
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

        const auto symbolLength = height >= consensus.FortCanningHeight ? CToken::MAX_TOKEN_POOLPAIR_LENGTH : CToken::MAX_TOKEN_SYMBOL_LENGTH;
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

        auto tokenId = mnview.CreateToken(token, false);
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

        CPoolPair& pool = pair.get();

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
        for(const auto& var : obj.govs) {
            auto result = var->Validate(mnview);
            if (!result) {
                return Res::Err("%s: %s", var->GetName(), result.msg);
            }
            // Make sure ORACLE_BLOCK_INTERVAL only updates at end of interval
            if (var->GetName() == "ORACLE_BLOCK_INTERVAL") {
                const auto diff = height % mnview.GetIntervalBlock();
                if (diff != 0) {
                    // Store as pending change
                    storeGovVars({var, height + mnview.GetIntervalBlock() - diff});
                    continue;
                }
            }
            auto res = var->Apply(mnview, height);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }
            auto add = mnview.SetVariable(*var);
            if (!add) {
                return Res::Err("%s: %s", var->GetName(), add.msg);
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
        auto result = obj.govVar->Validate(mnview);
        if (!result) {
            return Res::Err("%s: %s", obj.govVar->GetName(), result.msg);
        }

        // Store pending Gov var change
        storeGovVars(obj);

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
            res = TransferTokenBalance(BTC, offer->takerFee * 50 / 100, CScript(), order->ownerAddress);
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

        CLoanSetCollateralTokenImplementation collToken;
        static_cast<CLoanSetCollateralToken&>(collToken) = obj;

        collToken.creationTx = tx.GetHash();
        collToken.creationHeight = height;

        if (!HasFoundationAuth())
            return Res::Err("tx not from foundation member!");

        auto token = mnview.GetToken(collToken.idToken);
        if (!token)
            return Res::Err("token %s does not exist!", collToken.idToken.ToString());

        if (!collToken.activateAfterBlock)
            collToken.activateAfterBlock = height;

        if (collToken.activateAfterBlock < height)
            return Res::Err("activateAfterBlock cannot be less than current height!");

        if (!oraclePriceFeed(collToken.fixedIntervalPriceId))
            return Res::Err("Price feed %s/%s does not belong to any oracle", collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second);

        CFixedIntervalPrice fixedIntervalPrice;
        fixedIntervalPrice.priceFeedId = collToken.fixedIntervalPriceId;

        LogPrint(BCLog::LOAN, "CLoanSetCollateralTokenMessage()->"); /* Continued */
        auto price = GetAggregatePrice(mnview, collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second, time);
        if (!price)
            return Res::Err(price.msg);

        fixedIntervalPrice.priceRecord[1] = price;
        fixedIntervalPrice.timestamp = time;

        LogPrint(BCLog::ORACLE,"CLoanSetCollateralTokenMessage()->"); /* Continued */
        auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
        if (!resSetFixedPrice)
            return Res::Err(resSetFixedPrice.msg);

        return mnview.CreateLoanCollateralToken(collToken);
    }

    Res operator()(const CLoanSetLoanTokenMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        CLoanSetLoanTokenImplementation loanToken;
        static_cast<CLoanSetLoanToken&>(loanToken) = obj;

        loanToken.creationTx = tx.GetHash();
        loanToken.creationHeight = height;

        CFixedIntervalPrice fixedIntervalPrice;
        fixedIntervalPrice.priceFeedId = loanToken.fixedIntervalPriceId;

        auto nextPrice = GetAggregatePrice(mnview, loanToken.fixedIntervalPriceId.first, loanToken.fixedIntervalPriceId.second, time);
        if (!nextPrice)
            return Res::Err(nextPrice.msg);

        fixedIntervalPrice.priceRecord[1] = nextPrice;
        fixedIntervalPrice.timestamp = time;

        LogPrint(BCLog::ORACLE,"CLoanSetLoanTokenMessage()->"); /* Continued */
        auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
        if (!resSetFixedPrice)
            return Res::Err(resSetFixedPrice.msg);

        if (!HasFoundationAuth())
            return Res::Err("tx not from foundation member!");

        if (!oraclePriceFeed(loanToken.fixedIntervalPriceId))
            return Res::Err("Price feed %s/%s does not belong to any oracle", loanToken.fixedIntervalPriceId.first, loanToken.fixedIntervalPriceId.second);

        CTokenImplementation token;
        token.flags = loanToken.mintable ? (uint8_t)CToken::TokenFlags::Default : (uint8_t)CToken::TokenFlags::Tradeable;
        token.flags |= (uint8_t)CToken::TokenFlags::LoanToken | (uint8_t)CToken::TokenFlags::DAT;

        token.symbol = trim_ws(loanToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        token.name = trim_ws(loanToken.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.creationTx = tx.GetHash();
        token.creationHeight = height;

        auto tokenId = mnview.CreateToken(token, false);
        if (!tokenId)
            return std::move(tokenId);

        return mnview.SetLoanToken(loanToken, *(tokenId.val));
    }

    Res operator()(const CLoanUpdateLoanTokenMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        if (!HasFoundationAuth())
            return Res::Err("tx not from foundation member!");

        auto loanToken = mnview.GetLoanToken(obj.tokenTx);
        if (!loanToken)
            return Res::Err("Loan token (%s) does not exist!", obj.tokenTx.GetHex());

        if (obj.mintable != loanToken->mintable)
            loanToken->mintable = obj.mintable;

        if (obj.interest != loanToken->interest)
            loanToken->interest = obj.interest;

        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        if (!pair)
            return Res::Err("Loan token (%s) does not exist!", obj.tokenTx.GetHex());

        if (obj.symbol != pair->second.symbol)
            pair->second.symbol = trim_ws(obj.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

        if (obj.name != pair->second.name)
            pair->second.name = trim_ws(obj.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

        if (obj.fixedIntervalPriceId != loanToken->fixedIntervalPriceId) {
            if (!oraclePriceFeed(obj.fixedIntervalPriceId))
                return Res::Err("Price feed %s/%s does not belong to any oracle", obj.fixedIntervalPriceId.first, obj.fixedIntervalPriceId.second);

            loanToken->fixedIntervalPriceId = obj.fixedIntervalPriceId;
        }

        if (obj.mintable != (pair->second.flags & (uint8_t)CToken::TokenFlags::Mintable))
            pair->second.flags ^= (uint8_t)CToken::TokenFlags::Mintable;

        res = mnview.UpdateToken(pair->second.creationTx, static_cast<CToken>(pair->second), false);
        if (!res)
            return res;

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
                return Res::Err("There is not default loan scheme");
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

        if (mnview.GetLoanTokens(obj.vaultId))
            return Res::Err("Vault <%s> has loans", obj.vaultId.GetHex());

        CalculateOwnerRewards(obj.to);
        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
            for (const auto& col : collaterals->balances) {
                auto res = mnview.AddBalance(obj.to, {col.first, col.second});
                if (!res)
                    return res;
            }
        }

        // delete all interest to vault
        res = mnview.DeleteInterest(obj.vaultId);
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
        if (!mnview.GetLoanScheme(obj.schemeId))
            return Res::Err("Cannot find existing loan scheme with id %s", obj.schemeId);

        // loan scheme is not set to be destroyed
        if (auto height = mnview.GetDestroyLoanScheme(obj.schemeId))
            return Res::Err("Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *height);

        if (!IsVaultPriceValid(mnview, obj.vaultId, height))
            return Res::Err("Cannot update vault while any of the asset's price is invalid");

        // don't allow scheme change when vault is going to be in liquidation
        if (vault->schemeId != obj.schemeId)
            if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId))
                for (int i = 0; i < 2; i++) {
                    LogPrint(BCLog::LOAN,"CUpdateVaultMessage():\n");
                    bool useNextPrice = i > 0, requireLivePrice = true;
                    auto collateralsLoans = mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
                    if (!collateralsLoans)
                        return std::move(collateralsLoans);

                    auto scheme = mnview.GetLoanScheme(obj.schemeId);
                    if (collateralsLoans.val->ratio() < scheme->ratio)
                        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme->ratio);
                }

        vault->schemeId = obj.schemeId;
        vault->ownerAddress = obj.ownerAddress;
        return mnview.UpdateVault(obj.vaultId, *vault);
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

        //check balance
        CalculateOwnerRewards(obj.from);
        res = mnview.SubBalance(obj.from, obj.amount);
        if (!res)
            return Res::Err("Insufficient funds: can't subtract balance of %s: %s\n", ScriptToString(obj.from), res.msg);

        res = mnview.AddVaultCollateral(obj.vaultId, obj.amount);
        if (!res)
            return res;

        bool useNextPrice = false, requireLivePrice = false;
        auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);

        LogPrint(BCLog::LOAN,"CDepositToVaultMessage():\n");
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

        if (mnview.GetLoanTokens(obj.vaultId))
        {
            if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId))
            {
                const auto scheme = mnview.GetLoanScheme(vault->schemeId);
                for (int i = 0; i < 2; i++) {
                    // check collaterals for active and next price
                    bool useNextPrice = i > 0, requireLivePrice = true;
                    LogPrint(BCLog::LOAN,"CWithdrawFromVaultMessage():\n");
                    auto collateralsLoans = mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
                    if (!collateralsLoans)
                        return std::move(collateralsLoans);

                    uint64_t totalDFI = 0;
                    for (auto& col : collateralsLoans.val->collaterals)
                        if (col.nTokenId == DCT_ID{0})
                            totalDFI += col.nValue;

                    if (totalDFI < collateralsLoans.val->totalCollaterals / 2)
                        return Res::Err("At least 50%% of the vault must be in DFI");

                    if (collateralsLoans.val->ratio() < scheme->ratio)
                        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme->ratio);
                }
            }
            else
                return Res::Err("Cannot withdraw all collaterals as there are still active loans in this vault");
        }

        return mnview.AddBalance(obj.to, obj.amount);
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

        uint64_t totalLoansActivePrice = 0, totalLoansNextPrice = 0;
        for (const auto& kv : obj.amounts.balances)
        {
            DCT_ID tokenId = kv.first;
            auto loanToken = mnview.GetLoanTokenByID(tokenId);
            if (!loanToken)
                return Res::Err("Loan token with id (%s) does not exist!", tokenId.ToString());

            if (!loanToken->mintable)
                return Res::Err("Loan cannot be taken on token with id (%s) as \"mintable\" is currently false",tokenId.ToString());

            res = mnview.AddLoanToken(obj.vaultId, CTokenAmount{kv.first, kv.second});
            if (!res)
                return res;

            res = mnview.StoreInterest(height, obj.vaultId, vault->schemeId, tokenId, kv.second);
            if (!res)
                return res;

            auto tokenCurrency = loanToken->fixedIntervalPriceId;

            LogPrint(BCLog::ORACLE,"CLoanTakeLoanMessage()->%s->", loanToken->symbol); /* Continued */
            auto priceFeed = mnview.GetFixedIntervalPrice(tokenCurrency);
            if (!priceFeed)
                return Res::Err(priceFeed.msg);

            if (!priceFeed.val->isLive(mnview.GetPriceDeviation()))
                return Res::Err("No live fixed prices for %s/%s", tokenCurrency.first, tokenCurrency.second);

            for (int i = 0; i < 2; i++) {
                // check active and next price
                auto price = priceFeed.val->priceRecord[int(i > 0)];
                auto amount = MultiplyAmounts(price, kv.second);
                if (price > COIN && amount < kv.second)
                    return Res::Err("Value/price too high (%s/%s)", GetDecimaleString(kv.second), GetDecimaleString(price));

                auto& totalLoans = i > 0 ? totalLoansNextPrice : totalLoansActivePrice;
                auto prevLoans = totalLoans;
                totalLoans += amount;
                if (prevLoans > totalLoans)
                    return Res::Err("Exceed maximum loans");
            }

            res = mnview.AddMintedTokens(loanToken->creationTx, kv.second);
            if (!res)
                return res;

            const auto& address = !obj.to.empty() ? obj.to
                                                  : vault->ownerAddress;
            CalculateOwnerRewards(address);
            res = mnview.AddBalance(address, CTokenAmount{kv.first, kv.second});
            if (!res)
                return res;
        }

        auto scheme = mnview.GetLoanScheme(vault->schemeId);
        for (int i = 0; i < 2; i++) {
            // check ratio against current and active price
            bool useNextPrice = i > 0, requireLivePrice = true;
            LogPrint(BCLog::LOAN,"CLoanTakeLoanMessage():\n");
            auto collateralsLoans = mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
            if (!collateralsLoans)
                return std::move(collateralsLoans);

            uint64_t totalDFI = 0;
            for (auto& col : collateralsLoans.val->collaterals)
                if (col.nTokenId == DCT_ID{0})
                    totalDFI += col.nValue;

            if (totalDFI < collateralsLoans.val->totalCollaterals / 2)
                return Res::Err("At least 50%% of the vault must be in DFI when taking a loan");

            if (collateralsLoans.val->ratio() < scheme->ratio)
                return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme->ratio);
        }

        return Res::Ok();
    }

    Res operator()(const CLoanPaybackLoanMessage& obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        const auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Cannot find existing vault with id %s", obj.vaultId.GetHex());

        if (vault->isUnderLiquidation)
            return Res::Err("Cannot payback loan on vault under liquidation");

        if (!mnview.GetVaultCollaterals(obj.vaultId))
            return Res::Err("Vault with id %s has no collaterals", obj.vaultId.GetHex());

        if (!HasAuth(obj.from))
            return Res::Err("tx must have at least one input from token owner");

        if (!IsVaultPriceValid(mnview, obj.vaultId, height))
            return Res::Err("Cannot payback loan while any of the asset's price is invalid");

        for (const auto& kv : obj.amounts.balances)
        {
            DCT_ID tokenId = kv.first;
            auto loanToken = mnview.GetLoanTokenByID(tokenId);
            if (!loanToken)
                return Res::Err("Loan token with id (%s) does not exist!", tokenId.ToString());

            auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);
            if (!loanAmounts)
                return Res::Err("There are no loans on this vault (%s)!", obj.vaultId.GetHex());

            auto it = loanAmounts->balances.find(tokenId);
            if (it == loanAmounts->balances.end())
                return Res::Err("There is no loan on token (%s) in this vault!", loanToken->symbol);

            auto rate = mnview.GetInterestRate(obj.vaultId, tokenId);
            if (!rate)
                return Res::Err("Cannot get interest rate for this token (%s)!", loanToken->symbol);

            LogPrint(BCLog::LOAN,"CLoanPaybackMessage()->%s->", loanToken->symbol); /* Continued */
            auto subInterest = TotalInterest(*rate, height);
            auto subLoan = kv.second - subInterest;

            if (kv.second < subInterest)
            {
                subInterest = kv.second;
                subLoan = 0;
            }
            else if (it->second - subLoan < 0)
                subLoan = it->second;

            res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{kv.first, subLoan});
            if (!res)
                return res;

            LogPrint(BCLog::LOAN,"CLoanPaybackMessage()->%s->", loanToken->symbol); /* Continued */
            res = mnview.EraseInterest(height, obj.vaultId, vault->schemeId, tokenId, subLoan, subInterest);
            if (!res)
                return res;

            if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight && subLoan < it->second)
            {
                auto newRate = mnview.GetInterestRate(obj.vaultId, tokenId);
                if (!newRate)
                    return Res::Err("Cannot get interest rate for this token (%s)!", loanToken->symbol);

                if (newRate->interestPerBlock == 0)
                        return Res::Err("Cannot payback this amount of loan for %s, either payback full amount or less than this amount!", loanToken->symbol);
            }

            res = mnview.SubMintedTokens(loanToken->creationTx, subLoan);
            if (!res)
                return res;

            CalculateOwnerRewards(obj.from);
            // subtract loan amount first, interest is burning below
            res = mnview.SubBalance(obj.from, CTokenAmount{kv.first, subLoan});
            if (!res)
                return res;

            // burn interest Token->USD->DFI->burnAddress
            if (subInterest)
            {
                LogPrint(BCLog::LOAN, "CLoanTakeLoanMessage(): Swapping %s interest to DFI - %lld, height - %d\n", loanToken->symbol, subInterest, height);
                res = SwapToDFIOverUSD(mnview, kv.first, subInterest, obj.from, consensus.burnAddress, height);
                if (!res)
                    return res;
            }
        }

        return Res::Ok();
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

        auto batch = mnview.GetAuctionBatch(obj.vaultId, obj.index);
        if (!batch)
            return Res::Err("No batch to vault/index %s/%d", obj.vaultId.GetHex(), obj.index);

        if (obj.amount.nTokenId != batch->loanAmount.nTokenId)
            return Res::Err("Bid token does not match auction one");

        auto bid = mnview.GetAuctionBid(obj.vaultId, obj.index);
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
        return !res ? res : mnview.StoreAuctionBid(obj.vaultId, obj.index, {obj.from, obj.amount});
    }

    Res operator()(const CCustomTxMessageNone&) const {
        return Res::Ok();
    }
};

class CCustomTxRevertVisitor : public CCustomTxVisitor
{
    Res EraseHistory(const CScript& owner) const {
        // notify account changes, no matter Sub or Add
       return mnview.AddBalance(owner, {});
    }

public:
    using CCustomTxVisitor::CCustomTxVisitor;

    template<typename T>
    Res operator()(const T&) const {
        return Res::Ok();
    }

    Res operator()(const CCreateMasterNodeMessage& obj) const {
        auto res = CheckMasternodeCreationTx();
        return !res ? res : mnview.UnCreateMasternode(tx.GetHash());
    }

    Res operator()(const CResignMasterNodeMessage& obj) const {
        auto res = HasCollateralAuth(obj);
        return !res ? res : mnview.UnResignMasternode(obj, tx.GetHash());
    }

    Res operator()(const CCreateTokenMessage& obj) const {
        auto res = CheckTokenCreationTx();
        return !res ? res : mnview.RevertCreateToken(tx.GetHash());
    }

    Res operator()(const CCreatePoolPairMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        auto pool = mnview.GetPoolPair(obj.poolPair.idTokenA, obj.poolPair.idTokenB);
        if (!pool) {
            return Res::Err("no such poolPair tokenA %s, tokenB %s",
                            obj.poolPair.idTokenA.ToString(),
                            obj.poolPair.idTokenB.ToString());
        }
        return mnview.RevertCreateToken(tx.GetHash());
    }

    Res operator()(const CMintTokensMessage& obj) const {
        for (const auto& kv : obj.balances) {
            DCT_ID tokenId = kv.first;
            auto token = mnview.GetToken(tokenId);
            if (!token) {
                return Res::Err("token %s does not exist!", tokenId.ToString());
            }
            auto tokenImpl = static_cast<const CTokenImplementation&>(*token);
            const Coin& coin = coins.AccessCoin(COutPoint(tokenImpl.creationTx, 1));
            EraseHistory(coin.out.scriptPubKey);
        }
        return Res::Ok();
    }

    Res operator()(const CPoolSwapMessage& obj) const {
        EraseHistory(obj.to);
        return EraseHistory(obj.from);
    }

    Res operator()(const CPoolSwapMessageV2& obj) const {
        return (*this)(obj.swapInfo);
    }

    Res operator()(const CLiquidityMessage& obj) const {
        for (const auto& kv : obj.from) {
            EraseHistory(kv.first);
        }
        return EraseHistory(obj.shareAddress);
    }

    Res operator()(const CRemoveLiquidityMessage& obj) const {
        return EraseHistory(obj.from);
    }

    Res operator()(const CUtxosToAccountMessage& obj) const {
        for (const auto& account : obj.to) {
            EraseHistory(account.first);
        }
        return Res::Ok();
    }

    Res operator()(const CAccountToUtxosMessage& obj) const {
        return EraseHistory(obj.from);
    }

    Res operator()(const CAccountToAccountMessage& obj) const {
        for (const auto& account : obj.to) {
            EraseHistory(account.first);
        }
        return EraseHistory(obj.from);
    }

    Res operator()(const CAnyAccountsToAccountsMessage& obj) const {
        for (const auto& account : obj.to) {
            EraseHistory(account.first);
        }
        for (const auto& account : obj.from) {
            EraseHistory(account.first);
        }
        return Res::Ok();
    }

    Res operator()(const CICXCreateOrderMessage& obj) const {
        if (obj.orderType == CICXOrder::TYPE_INTERNAL) {
            auto hash = tx.GetHash();
            EraseHistory({hash.begin(), hash.end()});
            EraseHistory(obj.ownerAddress);
        }
        return Res::Ok();
    }

    Res operator()(const CICXMakeOfferMessage& obj) const {
        auto hash = tx.GetHash();
        EraseHistory({hash.begin(), hash.end()});
        return EraseHistory(obj.ownerAddress);
    }

    Res operator()(const CICXSubmitDFCHTLCMessage& obj) const {
        auto offer = mnview.GetICXMakeOfferByCreationTx(obj.offerTx);
        if (!offer)
            return Res::Err("offer with creation tx %s does not exists!", obj.offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

        EraseHistory(offer->ownerAddress);
        if (order->orderType == CICXOrder::TYPE_INTERNAL) {
            CScript orderTxidAddr(order->creationTx.begin(), order->creationTx.end());
            CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());
            EraseHistory(orderTxidAddr);
            EraseHistory(offerTxidAddr);
            EraseHistory(consensus.burnAddress);
        }
        auto hash = tx.GetHash();
        return EraseHistory({hash.begin(), hash.end()});
    }

    Res operator()(const CICXSubmitEXTHTLCMessage& obj) const {
        auto offer = mnview.GetICXMakeOfferByCreationTx(obj.offerTx);
        if (!offer)
            return Res::Err("order with creation tx %s does not exists!", obj.offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

        if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
            CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());
            EraseHistory(offerTxidAddr);
            EraseHistory(offer->ownerAddress);
            EraseHistory(consensus.burnAddress);
        }
        return Res::Ok();
    }

    Res operator()(const CICXClaimDFCHTLCMessage& obj) const {
        auto dfchtlc = mnview.GetICXSubmitDFCHTLCByCreationTx(obj.dfchtlcTx);
        if (!dfchtlc)
            return Res::Err("dfc htlc with creation tx %s does not exists!", obj.dfchtlcTx.GetHex());

        auto offer = mnview.GetICXMakeOfferByCreationTx(dfchtlc->offerTx);
        if (!offer)
            return Res::Err("offer with creation tx %s does not exists!", dfchtlc->offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        if (!order)
            return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

        CScript htlcTxidAddr(dfchtlc->creationTx.begin(), dfchtlc->creationTx.end());
        EraseHistory(htlcTxidAddr);
        EraseHistory(order->ownerAddress);
        if (order->orderType == CICXOrder::TYPE_INTERNAL)
            EraseHistory(offer->ownerAddress);
        return Res::Ok();
    }

    Res operator()(const CICXCloseOrderMessage& obj) const {
        std::unique_ptr<CICXOrderImplemetation> order;
        if (!(order = mnview.GetICXOrderByCreationTx(obj.orderTx)))
            return Res::Err("order with creation tx %s does not exists!", obj.orderTx.GetHex());

        if (order->orderType == CICXOrder::TYPE_INTERNAL) {
            CScript txidAddr(order->creationTx.begin(), order->creationTx.end());
            EraseHistory(txidAddr);
            EraseHistory(order->ownerAddress);
        }
        return Res::Ok();
    }

    Res operator()(const CICXCloseOfferMessage& obj) const {
        std::unique_ptr<CICXMakeOfferImplemetation> offer;
        if (!(offer = mnview.GetICXMakeOfferByCreationTx(obj.offerTx)))
            return Res::Err("offer with creation tx %s does not exists!", obj.offerTx.GetHex());

        CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
        EraseHistory(txidAddr);
        return EraseHistory(offer->ownerAddress);
    }

    Res operator()(const CDepositToVaultMessage& obj) const {
        return EraseHistory(obj.from);
    }

    Res operator()(const CCloseVaultMessage& obj) const {
        return EraseHistory(obj.to);
    }

    Res operator()(const CLoanTakeLoanMessage& obj) const {
        const auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        return EraseHistory(!obj.to.empty() ? obj.to : vault->ownerAddress);
    }

    Res operator()(const CWithdrawFromVaultMessage& obj) const {
        return EraseHistory(obj.to);
    }

    Res operator()(const CLoanPaybackLoanMessage& obj) const {
        const auto vault = mnview.GetVault(obj.vaultId);
        if (!vault)
            return Res::Err("Vault <%s> not found", obj.vaultId.GetHex());

        EraseHistory(obj.from);
        EraseHistory(consensus.burnAddress);
        return EraseHistory(vault->ownerAddress);
    }

    Res operator()(const CAuctionBidMessage& obj) const {
        if (auto bid = mnview.GetAuctionBid(obj.vaultId, obj.index))
            EraseHistory(bid->first);

        return EraseHistory(obj.from);
    }
};

Res CustomMetadataParse(uint32_t height, const Consensus::Params& consensus, const std::vector<unsigned char>& metadata, CCustomTxMessage& txMessage) {
    try {
        return boost::apply_visitor(CCustomMetadataParseVisitor(height, consensus, metadata), txMessage);
    } catch (const std::exception& e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

Res CustomTxVisit(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, uint32_t height, const Consensus::Params& consensus, const CCustomTxMessage& txMessage, uint64_t time) {
    try {
        return boost::apply_visitor(CCustomTxApplyVisitor(tx, height, coins, mnview, consensus, time), txMessage);
    } catch (const std::exception& e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

Res CustomTxRevert(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, uint32_t height, const Consensus::Params& consensus, const CCustomTxMessage& txMessage) {
    try {
        return boost::apply_visitor(CCustomTxRevertVisitor(tx, height, coins, mnview, consensus), txMessage);
    } catch (const std::exception& e) {
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

Res RevertCustomTx(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height, uint32_t txn, CHistoryErasers& erasers) {
    if (tx.IsCoinBase() && height > 0) { // genesis contains custom coinbase txs
        return Res::Ok();
    }
    auto res = Res::Ok();
    std::vector<unsigned char> metadata;
    auto txType = GuessCustomTxType(tx, metadata);
    switch(txType)
    {
        case CustomTxType::CreateMasternode:
        case CustomTxType::ResignMasternode:
        case CustomTxType::CreateToken:
        case CustomTxType::CreatePoolPair:
            // Enable these in the future
        case CustomTxType::None:
            return res;
        default:
            break;
    }
    auto txMessage = customTypeToMessage(txType);
    CAccountsHistoryEraser view(mnview, height, txn, erasers);
    uint256 vaultID;
    std::string schemeID;
    CLoanSchemeCreation globalScheme;
    if ((res = CustomMetadataParse(height, consensus, metadata, txMessage))) {
        res = CustomTxRevert(view, coins, tx, height, consensus, txMessage);

        // Track burn fee
        if (txType == CustomTxType::CreateToken
        || txType == CustomTxType::CreateMasternode
        || txType == CustomTxType::Vault) {
            erasers.SubFeeBurn(tx.vout[0].scriptPubKey);
        }
    }
    if (!res) {
        res.msg = strprintf("%sRevertTx: %s", ToString(txType), res.msg);
        return res;
    }
    return (view.Flush(), res);
}

void PopulateVaultHistoryData(CHistoryWriters* writers, CAccountsHistoryWriter& view, const CCustomTxMessage& txMessage, const CustomTxType txType, const uint32_t height, const uint32_t txn, const uint256& txid) {
    if (txType == CustomTxType::Vault) {
        auto obj = boost::get<CVaultMessage>(txMessage);
        writers->schemeID = obj.schemeId;
        view.vaultID = txid;
    } else if (txType == CustomTxType::CloseVault) {
        auto obj = boost::get<CCloseVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::UpdateVault) {
        auto obj = boost::get<CUpdateVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
        if (!obj.schemeId.empty()) {
            writers->schemeID = obj.schemeId;
        }
    } else if (txType == CustomTxType::DepositToVault) {
        auto obj = boost::get<CDepositToVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::WithdrawFromVault) {
        auto obj = boost::get<CWithdrawFromVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::TakeLoan) {
        auto obj = boost::get<CLoanTakeLoanMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::PaybackLoan) {
        auto obj = boost::get<CLoanPaybackLoanMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::AuctionBid) {
        auto obj = boost::get<CAuctionBidMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::LoanScheme) {
        auto obj = boost::get<CLoanSchemeMessage>(txMessage);
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
    const auto metadataValidation = height >= consensus.FortCanningHeight;
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
        res = CustomTxVisit(view, coins, tx, height, consensus, txMessage, time);

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
        if (height >= consensus.DakotaHeight) {
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

std::vector<DCT_ID> CPoolSwap::CalculateSwaps(CCustomCSView& view) {

    std::vector<std::vector<DCT_ID>> poolPaths = CalculatePoolPaths(view);

    // Record best pair
    std::pair<std::vector<DCT_ID>, CAmount> bestPair{{}, 0};

    // Loop through all common pairs
    for (const auto& path : poolPaths) {

        // Test on copy of view
        CCustomCSView dummy(view);

        // Execute pool path
        auto res = ExecuteSwap(dummy, path);

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

    // For tokens to be traded get all pairs and pool IDs
    std::multimap<uint32_t, DCT_ID> fromPoolsID, toPoolsID;
    view.ForEachPoolPair([&](DCT_ID const & id, const CPoolPair& pool) {
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
    std::vector<std::vector<DCT_ID>> poolPaths;
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

    CTokenAmount swapAmountResult{{},0};
    Res poolResult = Res::Ok();

    // No composite swap allowed before Fort Canning
    if (height < Params().GetConsensus().FortCanningHeight && !poolIDs.empty()) {
        poolIDs.clear();
    }

    // Single swap if no pool IDs provided
    auto poolPrice = POOLPRICE_MAX;
    boost::optional<std::pair<DCT_ID, CPoolPair> > poolPair;
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

    for (size_t i{0}; i < poolIDs.size(); ++i) {

        // Also used to generate pool specific error messages for RPC users
        currentID = poolIDs[i];

        // Use single swap pool if already found
        boost::optional<CPoolPair> pool;
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

        // Set amount to be swapped in pool
        CTokenAmount swapAmount{obj.idTokenFrom, obj.amountFrom};

        // If set use amount from previous loop
        if (swapAmountResult.nValue != 0) {
            swapAmount = swapAmountResult;
        }

        // Check if last pool swap
        bool lastSwap = i + 1 == poolIDs.size();

        // Perform swap
        poolResult = pool->Swap(swapAmount, poolPrice, [&] (const CTokenAmount &tokenAmount) {
            // Save swap amount for next loop
            swapAmountResult = tokenAmount;

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
            res = addView.AddBalance(lastSwap ? obj.to : obj.from, tokenAmount);
            if (!res) {
                return res;
            }
            intermediateView.Flush();

           return res;
        }, static_cast<int>(height));

        if (!poolResult) {
            return poolResult;
        }
    }

    // Reject if price paid post-swap above max price provided
    if (height >= Params().GetConsensus().FortCanningHeight && obj.maxPrice != POOLPRICE_MAX) {
        const CAmount userMaxPrice = obj.maxPrice.integer * COIN + obj.maxPrice.fraction;
        if (arith_uint256(obj.amountFrom) * COIN / swapAmountResult.nValue > userMaxPrice) {
            return Res::Err("Price is higher than indicated.");
        }
    }

    // Assign to result for loop testing best pool swap result
    result = swapAmountResult.nValue;

    return poolResult;
}

Res  SwapToDFIOverUSD(CCustomCSView & mnview, DCT_ID tokenId, CAmount amount, CScript const & from, CScript const & to, uint32_t height)
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

    // Direct swap from DUSD to DFI as defined in the CPoolSwapMessage.
    if (tokenId == dUsdToken->first) {
        return poolSwap.ExecuteSwap(mnview, {});
    }

    auto pooldUSDDFI = mnview.GetPoolPair(dUsdToken->first, DCT_ID{0});
    if (!pooldUSDDFI)
        return Res::Err("Cannot find pool pair DUSD-DFI!");

    auto poolTokendUSD = mnview.GetPoolPair(tokenId,dUsdToken->first);
    if (!poolTokendUSD)
        return Res::Err("Cannot find pool pair %s-DUSD!", token->symbol);

    // swap tokenID -> USD -> DFI
    auto res = poolSwap.ExecuteSwap(mnview, {poolTokendUSD->first, pooldUSDDFI->first});

    return res;
}

bool IsVaultPriceValid(CCustomCSView& mnview, const CVaultId& vaultId, uint32_t height)
{
    if (auto collaterals = mnview.GetVaultCollaterals(vaultId))
        for (const auto collateral : collaterals->balances)
            if (auto collateralToken = mnview.HasLoanCollateralToken({collateral.first, height}))
                if (auto fixedIntervalPrice = mnview.GetFixedIntervalPrice(collateralToken->fixedIntervalPriceId))
                    if (!fixedIntervalPrice.val->isLive(mnview.GetPriceDeviation()))
                        return false;

    if (auto loans = mnview.GetLoanTokens(vaultId))
        for (const auto loan : loans->balances)
            if (auto loanToken = mnview.GetLoanTokenByID(loan.first))
                if (auto fixedIntervalPrice = mnview.GetFixedIntervalPrice(loanToken->fixedIntervalPriceId))
                    if (!fixedIntervalPrice.val->isLive(mnview.GetPriceDeviation()))
                        return false;
    return true;
}
