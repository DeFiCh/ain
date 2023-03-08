// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/historywriter.h>
#include <masternodes/mn_checks.h>
#include <masternodes/vaulthistory.h>
#include <masternodes/errors.h>

#include <core_io.h>
#include <index/txindex.h>
#include <txmempool.h>
#include <validation.h>

#include <algorithm>

constexpr std::string_view ERR_STRING_MIN_COLLATERAL_DFI_PCT =
    "At least 50%% of the minimum required collateral must be in DFI";
constexpr std::string_view ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT =
    "At least 50%% of the minimum required collateral must be in DFI or DUSD";

std::string ToString(CustomTxType type) {
    switch (type) {
        case CustomTxType::CreateMasternode:
            return "CreateMasternode";
        case CustomTxType::ResignMasternode:
            return "ResignMasternode";
        case CustomTxType::UpdateMasternode:
            return "UpdateMasternode";
        case CustomTxType::CreateToken:
            return "CreateToken";
        case CustomTxType::UpdateToken:
            return "UpdateToken";
        case CustomTxType::UpdateTokenAny:
            return "UpdateTokenAny";
        case CustomTxType::MintToken:
            return "MintToken";
        case CustomTxType::BurnToken:
            return "BurnToken";
        case CustomTxType::CreatePoolPair:
            return "CreatePoolPair";
        case CustomTxType::UpdatePoolPair:
            return "UpdatePoolPair";
        case CustomTxType::PoolSwap:
            return "PoolSwap";
        case CustomTxType::PoolSwapV2:
            return "PoolSwap";
        case CustomTxType::AddPoolLiquidity:
            return "AddPoolLiquidity";
        case CustomTxType::RemovePoolLiquidity:
            return "RemovePoolLiquidity";
        case CustomTxType::UtxosToAccount:
            return "UtxosToAccount";
        case CustomTxType::AccountToUtxos:
            return "AccountToUtxos";
        case CustomTxType::AccountToAccount:
            return "AccountToAccount";
        case CustomTxType::AnyAccountsToAccounts:
            return "AnyAccountsToAccounts";
        case CustomTxType::SmartContract:
            return "SmartContract";
        case CustomTxType::FutureSwap:
            return "DFIP2203";
        case CustomTxType::SetGovVariable:
            return "SetGovVariable";
        case CustomTxType::SetGovVariableHeight:
            return "SetGovVariableHeight";
        case CustomTxType::AppointOracle:
            return "AppointOracle";
        case CustomTxType::RemoveOracleAppoint:
            return "RemoveOracleAppoint";
        case CustomTxType::UpdateOracleAppoint:
            return "UpdateOracleAppoint";
        case CustomTxType::SetOracleData:
            return "SetOracleData";
        case CustomTxType::AutoAuthPrep:
            return "AutoAuth";
        case CustomTxType::ICXCreateOrder:
            return "ICXCreateOrder";
        case CustomTxType::ICXMakeOffer:
            return "ICXMakeOffer";
        case CustomTxType::ICXSubmitDFCHTLC:
            return "ICXSubmitDFCHTLC";
        case CustomTxType::ICXSubmitEXTHTLC:
            return "ICXSubmitEXTHTLC";
        case CustomTxType::ICXClaimDFCHTLC:
            return "ICXClaimDFCHTLC";
        case CustomTxType::ICXCloseOrder:
            return "ICXCloseOrder";
        case CustomTxType::ICXCloseOffer:
            return "ICXCloseOffer";
        case CustomTxType::SetLoanCollateralToken:
            return "SetLoanCollateralToken";
        case CustomTxType::SetLoanToken:
            return "SetLoanToken";
        case CustomTxType::UpdateLoanToken:
            return "UpdateLoanToken";
        case CustomTxType::LoanScheme:
            return "LoanScheme";
        case CustomTxType::DefaultLoanScheme:
            return "DefaultLoanScheme";
        case CustomTxType::DestroyLoanScheme:
            return "DestroyLoanScheme";
        case CustomTxType::Vault:
            return "Vault";
        case CustomTxType::CloseVault:
            return "CloseVault";
        case CustomTxType::UpdateVault:
            return "UpdateVault";
        case CustomTxType::DepositToVault:
            return "DepositToVault";
        case CustomTxType::WithdrawFromVault:
            return "WithdrawFromVault";
        case CustomTxType::PaybackWithCollateral:
            return "PaybackWithCollateral";
        case CustomTxType::TakeLoan:
            return "TakeLoan";
        case CustomTxType::PaybackLoan:
            return "PaybackLoan";
        case CustomTxType::PaybackLoanV2:
            return "PaybackLoan";
        case CustomTxType::AuctionBid:
            return "AuctionBid";
        case CustomTxType::FutureSwapExecution:
            return "FutureSwapExecution";
        case CustomTxType::FutureSwapRefund:
            return "FutureSwapRefund";
        case CustomTxType::TokenSplit:
            return "TokenSplit";
        case CustomTxType::Reject:
            return "Reject";
        case CustomTxType::CreateCfp:
            return "CreateCfp";
        case CustomTxType::ProposalFeeRedistribution:
            return "ProposalFeeRedistribution";
        case CustomTxType::CreateVoc:
            return "CreateVoc";
        case CustomTxType::Vote:
            return "Vote";
        case CustomTxType::UnsetGovVariable:
            return "UnsetGovVariable";
        case CustomTxType::None:
            return "None";
    }
    return "None";
}

CustomTxType FromString(const std::string &str) {
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

static ResVal<CBalances> BurntTokens(const CTransaction &tx) {
    CBalances balances;
    for (const auto &out : tx.vout) {
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN) {
            Require(balances.Add(out.TokenAmount()));
        }
    }
    return {balances, Res::Ok()};
}

static ResVal<CBalances> MintedTokens(const CTransaction &tx, uint32_t mintingOutputsStart) {
    CBalances balances;
    for (uint32_t i = mintingOutputsStart; i < (uint32_t)tx.vout.size(); i++) {
        Require(balances.Add(tx.vout[i].TokenAmount()));
    }
    return {balances, Res::Ok()};
}

CCustomTxMessage customTypeToMessage(CustomTxType txType) {
    switch (txType) {
        case CustomTxType::CreateMasternode:
            return CCreateMasterNodeMessage{};
        case CustomTxType::ResignMasternode:
            return CResignMasterNodeMessage{};
        case CustomTxType::UpdateMasternode:
            return CUpdateMasterNodeMessage{};
        case CustomTxType::CreateToken:
            return CCreateTokenMessage{};
        case CustomTxType::UpdateToken:
            return CUpdateTokenPreAMKMessage{};
        case CustomTxType::UpdateTokenAny:
            return CUpdateTokenMessage{};
        case CustomTxType::MintToken:
            return CMintTokensMessage{};
        case CustomTxType::BurnToken:
            return CBurnTokensMessage{};
        case CustomTxType::CreatePoolPair:
            return CCreatePoolPairMessage{};
        case CustomTxType::UpdatePoolPair:
            return CUpdatePoolPairMessage{};
        case CustomTxType::PoolSwap:
            return CPoolSwapMessage{};
        case CustomTxType::PoolSwapV2:
            return CPoolSwapMessageV2{};
        case CustomTxType::AddPoolLiquidity:
            return CLiquidityMessage{};
        case CustomTxType::RemovePoolLiquidity:
            return CRemoveLiquidityMessage{};
        case CustomTxType::UtxosToAccount:
            return CUtxosToAccountMessage{};
        case CustomTxType::AccountToUtxos:
            return CAccountToUtxosMessage{};
        case CustomTxType::AccountToAccount:
            return CAccountToAccountMessage{};
        case CustomTxType::AnyAccountsToAccounts:
            return CAnyAccountsToAccountsMessage{};
        case CustomTxType::SmartContract:
            return CSmartContractMessage{};
        case CustomTxType::FutureSwap:
            return CFutureSwapMessage{};
        case CustomTxType::SetGovVariable:
            return CGovernanceMessage{};
        case CustomTxType::SetGovVariableHeight:
            return CGovernanceHeightMessage{};
        case CustomTxType::AppointOracle:
            return CAppointOracleMessage{};
        case CustomTxType::RemoveOracleAppoint:
            return CRemoveOracleAppointMessage{};
        case CustomTxType::UpdateOracleAppoint:
            return CUpdateOracleAppointMessage{};
        case CustomTxType::SetOracleData:
            return CSetOracleDataMessage{};
        case CustomTxType::AutoAuthPrep:
            return CCustomTxMessageNone{};
        case CustomTxType::ICXCreateOrder:
            return CICXCreateOrderMessage{};
        case CustomTxType::ICXMakeOffer:
            return CICXMakeOfferMessage{};
        case CustomTxType::ICXSubmitDFCHTLC:
            return CICXSubmitDFCHTLCMessage{};
        case CustomTxType::ICXSubmitEXTHTLC:
            return CICXSubmitEXTHTLCMessage{};
        case CustomTxType::ICXClaimDFCHTLC:
            return CICXClaimDFCHTLCMessage{};
        case CustomTxType::ICXCloseOrder:
            return CICXCloseOrderMessage{};
        case CustomTxType::ICXCloseOffer:
            return CICXCloseOfferMessage{};
        case CustomTxType::SetLoanCollateralToken:
            return CLoanSetCollateralTokenMessage{};
        case CustomTxType::SetLoanToken:
            return CLoanSetLoanTokenMessage{};
        case CustomTxType::UpdateLoanToken:
            return CLoanUpdateLoanTokenMessage{};
        case CustomTxType::LoanScheme:
            return CLoanSchemeMessage{};
        case CustomTxType::DefaultLoanScheme:
            return CDefaultLoanSchemeMessage{};
        case CustomTxType::DestroyLoanScheme:
            return CDestroyLoanSchemeMessage{};
        case CustomTxType::Vault:
            return CVaultMessage{};
        case CustomTxType::CloseVault:
            return CCloseVaultMessage{};
        case CustomTxType::UpdateVault:
            return CUpdateVaultMessage{};
        case CustomTxType::DepositToVault:
            return CDepositToVaultMessage{};
        case CustomTxType::WithdrawFromVault:
            return CWithdrawFromVaultMessage{};
        case CustomTxType::PaybackWithCollateral:
            return CPaybackWithCollateralMessage{};
        case CustomTxType::TakeLoan:
            return CLoanTakeLoanMessage{};
        case CustomTxType::PaybackLoan:
            return CLoanPaybackLoanMessage{};
        case CustomTxType::PaybackLoanV2:
            return CLoanPaybackLoanV2Message{};
        case CustomTxType::AuctionBid:
            return CAuctionBidMessage{};
        case CustomTxType::FutureSwapExecution:
            return CCustomTxMessageNone{};
        case CustomTxType::FutureSwapRefund:
            return CCustomTxMessageNone{};
        case CustomTxType::TokenSplit:
            return CCustomTxMessageNone{};
        case CustomTxType::Reject:
            return CCustomTxMessageNone{};
        case CustomTxType::CreateCfp:
            return CCreateProposalMessage{};
        case CustomTxType::CreateVoc:
            return CCreateProposalMessage{};
        case CustomTxType::Vote:
            return CProposalVoteMessage{};
        case CustomTxType::ProposalFeeRedistribution:
            return CCustomTxMessageNone{};
        case CustomTxType::UnsetGovVariable:
            return CGovernanceUnsetMessage{};
        case CustomTxType::None:
            return CCustomTxMessageNone{};
    }
    return CCustomTxMessageNone{};
}

extern std::string ScriptToString(const CScript &script);

template <typename ...T>
constexpr bool FalseType = false;

template<typename T>
constexpr bool IsOneOf() {
    return false;
}

template<typename T, typename T1, typename ...Args>
constexpr bool IsOneOf() {
    return std::is_same_v<T, T1> || IsOneOf<T, Args...>();
}

class CCustomMetadataParseVisitor {
    uint32_t height;
    const Consensus::Params &consensus;
    const std::vector<unsigned char> &metadata;

    Res IsHardforkEnabled(int startHeight) const {
        const std::unordered_map<int, std::string> hardforks = {
                { consensus.AMKHeight,                    "called before AMK height" },
                { consensus.BayfrontHeight,               "called before Bayfront height" },
                { consensus.BayfrontGardensHeight,        "called before Bayfront Gardens height" },
                { consensus.EunosHeight,                  "called before Eunos height" },
                { consensus.EunosPayaHeight,              "called before EunosPaya height" },
                { consensus.FortCanningHeight,            "called before FortCanning height" },
                { consensus.FortCanningHillHeight,        "called before FortCanningHill height" },
                { consensus.FortCanningRoadHeight,        "called before FortCanningRoad height" },
                { consensus.FortCanningEpilogueHeight,    "called before FortCanningEpilogue height" },
                { consensus.GrandCentralHeight,           "called before GrandCentral height" },
        };
        if (startHeight && int(height) < startHeight) {
            auto it = hardforks.find(startHeight);
            assert(it != hardforks.end());
            return Res::Err(it->second);
        }

        return Res::Ok();
    }

public:
    CCustomMetadataParseVisitor(uint32_t height,
                                const Consensus::Params &consensus,
                                const std::vector<unsigned char> &metadata)
        : height(height),
          consensus(consensus),
          metadata(metadata) {}

    template<typename T>
    Res EnabledAfter() const {
        if constexpr (IsOneOf<T,
                CCreateTokenMessage,
                CUpdateTokenPreAMKMessage,
                CUtxosToAccountMessage,
                CAccountToUtxosMessage,
                CAccountToAccountMessage,
                CMintTokensMessage>())
            return IsHardforkEnabled(consensus.AMKHeight);
        else if constexpr (IsOneOf<T,
                CUpdateTokenMessage,
                CPoolSwapMessage,
                CLiquidityMessage,
                CRemoveLiquidityMessage,
                CCreatePoolPairMessage,
                CUpdatePoolPairMessage,
                CGovernanceMessage>())
            return IsHardforkEnabled(consensus.BayfrontHeight);
        else if constexpr (IsOneOf<T,
                CAppointOracleMessage,
                CRemoveOracleAppointMessage,
                CUpdateOracleAppointMessage,
                CSetOracleDataMessage,
                CICXCreateOrderMessage,
                CICXMakeOfferMessage,
                CICXSubmitDFCHTLCMessage,
                CICXSubmitEXTHTLCMessage,
                CICXClaimDFCHTLCMessage,
                CICXCloseOrderMessage,
                CICXCloseOfferMessage>())
            return IsHardforkEnabled(consensus.EunosHeight);
        else if constexpr (IsOneOf<T,
                CPoolSwapMessageV2,
                CLoanSetCollateralTokenMessage,
                CLoanSetLoanTokenMessage,
                CLoanUpdateLoanTokenMessage,
                CLoanSchemeMessage,
                CDefaultLoanSchemeMessage,
                CDestroyLoanSchemeMessage,
                CVaultMessage,
                CCloseVaultMessage,
                CUpdateVaultMessage,
                CDepositToVaultMessage,
                CWithdrawFromVaultMessage,
                CLoanTakeLoanMessage,
                CLoanPaybackLoanMessage,
                CAuctionBidMessage,
                CGovernanceHeightMessage>())
            return IsHardforkEnabled(consensus.FortCanningHeight);
        else if constexpr (IsOneOf<T,
                CAnyAccountsToAccountsMessage>())
            return IsHardforkEnabled(consensus.BayfrontGardensHeight);
        else if constexpr (IsOneOf<T,
                CSmartContractMessage>())
            return IsHardforkEnabled(consensus.FortCanningHillHeight);
        else if constexpr (IsOneOf<T,
                CLoanPaybackLoanV2Message,
                CFutureSwapMessage>())
            return IsHardforkEnabled(consensus.FortCanningRoadHeight);
        else if constexpr (IsOneOf<T,
                CPaybackWithCollateralMessage>())
            return IsHardforkEnabled(consensus.FortCanningEpilogueHeight);
        else if constexpr (IsOneOf<T,
                CUpdateMasterNodeMessage,
                CBurnTokensMessage,
                CCreateProposalMessage,
                CProposalVoteMessage,
                CGovernanceUnsetMessage>())
            return IsHardforkEnabled(consensus.GrandCentralHeight);
        else if constexpr (IsOneOf<T,
                CCreateMasterNodeMessage,
                CResignMasterNodeMessage>())
            return Res::Ok();
        else
            static_assert(FalseType<T>, "Unhandled type");
    }

    template<typename T>
    Res DisabledAfter() const {
        if constexpr (IsOneOf<T, CUpdateTokenPreAMKMessage>())
            return IsHardforkEnabled(consensus.BayfrontHeight) ? Res::Err("called after Bayfront height") : Res::Ok();

        return Res::Ok();
    }

    template<typename T>
    Res operator()(T& obj) const {
        auto res = EnabledAfter<T>();
        if (!res)
            return res;

        res = DisabledAfter<T>();
        if (!res)
            return res;

        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> obj;
        if (!ss.empty())
            return Res::Err("deserialization failed: excess %d bytes", ss.size());

        return Res::Ok();
    }

    Res operator()(CCustomTxMessageNone &) const { return Res::Ok(); }
};

CCustomTxVisitor::CCustomTxVisitor(const CTransaction &tx,
                                   uint32_t height,
                                   const CCoinsViewCache &coins,
                                   CCustomCSView &mnview,
                                   const Consensus::Params &consensus)
    : height(height),
      mnview(mnview),
      tx(tx),
      coins(coins),
      consensus(consensus) {}

Res CCustomTxVisitor::HasAuth(const CScript &auth) const {
    for (const auto &input : tx.vin) {
        const Coin &coin = coins.AccessCoin(input.prevout);
        if (!coin.IsSpent() && coin.out.scriptPubKey == auth)
            return Res::Ok();
    }
    return Res::Err("tx must have at least one input from account owner");
}

Res CCustomTxVisitor::HasCollateralAuth(const uint256 &collateralTx) const {
    const Coin &auth = coins.AccessCoin(COutPoint(collateralTx, 1));  // always n=1 output
    Require(HasAuth(auth.out.scriptPubKey), "tx must have at least one input from the owner");
    return Res::Ok();
}

Res CCustomTxVisitor::HasFoundationAuth() const {
    auto members          = consensus.foundationMembers;
    const auto attributes = mnview.GetAttributes();
    assert(attributes);
    if (attributes->GetValue(CDataStructureV0{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovFoundation},
                             false)) {
        if (const auto databaseMembers = attributes->GetValue(
                CDataStructureV0{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members}, std::set<CScript>{});
            !databaseMembers.empty()) {
            members = databaseMembers;
        }
    }

    for (const auto &input : tx.vin) {
        const Coin &coin = coins.AccessCoin(input.prevout);
        if (!coin.IsSpent() && members.count(coin.out.scriptPubKey) > 0) {
            return Res::Ok();
        }
    }
    return Res::Err("tx not from foundation member");
}

Res CCustomTxVisitor::CheckMasternodeCreationTx() const {
    Require(tx.vout.size() >= 2 && tx.vout[0].nValue >= GetMnCreationFee(height) && tx.vout[0].nTokenId == DCT_ID{0} &&
                tx.vout[1].nValue == GetMnCollateralAmount(height) && tx.vout[1].nTokenId == DCT_ID{0},
            "malformed tx vouts (wrong creation fee or collateral amount)");

    return Res::Ok();
}

Res CCustomTxVisitor::CheckTokenCreationTx() const {
    Require(tx.vout.size() >= 2 && tx.vout[0].nValue >= GetTokenCreationFee(height) &&
                tx.vout[0].nTokenId == DCT_ID{0} && tx.vout[1].nValue == GetTokenCollateralAmount() &&
                tx.vout[1].nTokenId == DCT_ID{0},
            "malformed tx vouts (wrong creation fee or collateral amount)");

    return Res::Ok();
}

Res CCustomTxVisitor::CheckCustomTx() const {
    if (static_cast<int>(height) < consensus.EunosPayaHeight)
        Require(tx.vout.size() == 2, "malformed tx vouts ((wrong number of vouts)");
    if (static_cast<int>(height) >= consensus.EunosPayaHeight)
        Require(tx.vout[0].nValue == 0, "malformed tx vouts, first vout must be OP_RETURN vout with value 0");
    return Res::Ok();
}

Res CCustomTxVisitor::CheckProposalTx(const CCreateProposalMessage &msg) const {
    if (tx.vout[0].nValue != GetProposalCreationFee(height, mnview, msg) || tx.vout[0].nTokenId != DCT_ID{0})
        return Res::Err("malformed tx vouts (wrong creation fee)");

    return Res::Ok();
}

Res CCustomTxVisitor::TransferTokenBalance(DCT_ID id, CAmount amount, const CScript &from, const CScript &to) const {
    assert(!from.empty() || !to.empty());

    CTokenAmount tokenAmount{id, amount};
    // if "from" not supplied it will only add balance on "to" address
    if (!from.empty()) {
        Require(mnview.SubBalance(from, tokenAmount));
    }

    // if "to" not supplied it will only sub balance from "form" address
    if (!to.empty()) {
        Require(mnview.AddBalance(to, tokenAmount));
    }
    return Res::Ok();
}

DCT_ID CCustomTxVisitor::FindTokenByPartialSymbolName(const std::string &symbol) const {
    DCT_ID res{0};
    mnview.ForEachToken(
        [&](DCT_ID id, CTokenImplementation token) {
            if (token.symbol.find(symbol) == 0) {
                res = id;
                return false;
            }
            return true;
        },
        DCT_ID{1});
    assert(res.v != 0);
    return res;
}

CPoolPair CCustomTxVisitor::GetBTCDFIPoolPair() const {
    auto BTC  = FindTokenByPartialSymbolName(CICXOrder::TOKEN_BTC);
    auto pair = mnview.GetPoolPair(BTC, DCT_ID{0});
    assert(pair);
    return std::move(pair->second);
}

static CAmount GetDFIperBTC(const CPoolPair &BTCDFIPoolPair) {
    if (BTCDFIPoolPair.idTokenA == DCT_ID({0}))
        return DivideAmounts(BTCDFIPoolPair.reserveA, BTCDFIPoolPair.reserveB);
    return DivideAmounts(BTCDFIPoolPair.reserveB, BTCDFIPoolPair.reserveA);
}

CAmount CCustomTxVisitor::CalculateTakerFee(CAmount amount) const {
    auto tokenBTC = mnview.GetToken(CICXOrder::TOKEN_BTC);
    assert(tokenBTC);
    auto pair = mnview.GetPoolPair(tokenBTC->first, DCT_ID{0});
    assert(pair);
    return (arith_uint256(amount) * mnview.ICXGetTakerFeePerBTC() / COIN * GetDFIperBTC(pair->second) / COIN)
        .GetLow64();
}

ResVal<CScript> CCustomTxVisitor::MintableToken(DCT_ID id,
                                                const CTokenImplementation &token,
                                                bool anybodyCanMint) const {
    if (token.destructionTx != uint256{}) {
        return Res::Err("token %s already destroyed at height %i by tx %s",
                        token.symbol,
                        token.destructionHeight,
                        token.destructionTx.GetHex());
    }
    const Coin &auth = coins.AccessCoin(COutPoint(token.creationTx, 1));  // always n=1 output

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

    ResVal<CScript> result = {auth.out.scriptPubKey, Res::Ok()};
    if (anybodyCanMint || HasAuth(auth.out.scriptPubKey))
        return result;

    // Historic: in the case of DAT, it's ok to do not check foundation auth cause exact DAT owner is foundation
    // member himself The above is no longer true.

    if (token.IsDAT()) {
        // Is a DAT, check founders auth
        if (height < static_cast<uint32_t>(consensus.GrandCentralHeight) && !HasFoundationAuth()) {
            return Res::Err("token is DAT and tx not from foundation member");
        }
    } else {
        return Res::Err("tx must have at least one input from token owner");
    }

    return result;
}

Res CCustomTxVisitor::EraseEmptyBalances(TAmounts &balances) const {
    for (auto it = balances.begin(), next_it = it; it != balances.end(); it = next_it) {
        ++next_it;

        Require(mnview.GetToken(it->first), "reward token %d does not exist!", it->first.v);

        if (it->second == 0) {
            balances.erase(it);
        }
    }
    return Res::Ok();
}

Res CCustomTxVisitor::SetShares(const CScript &owner, const TAmounts &balances) const {
    for (const auto &balance : balances) {
        auto token = mnview.GetToken(balance.first);
        if (token && token->IsPoolShare()) {
            const auto bal = mnview.GetBalance(owner, balance.first);
            if (bal.nValue == balance.second) {
                Require(mnview.SetShare(balance.first, owner, height));
            }
        }
    }
    return Res::Ok();
}

Res CCustomTxVisitor::DelShares(const CScript &owner, const TAmounts &balances) const {
    for (const auto &kv : balances) {
        auto token = mnview.GetToken(kv.first);
        if (token && token->IsPoolShare()) {
            const auto balance = mnview.GetBalance(owner, kv.first);
            if (balance.nValue == 0) {
                Require(mnview.DelShare(kv.first, owner));
            }
        }
    }
    return Res::Ok();
}

// we need proxy view to prevent add/sub balance record
void CCustomTxVisitor::CalculateOwnerRewards(const CScript &owner) const {
    CCustomCSView view(mnview);
    view.CalculateOwnerRewards(owner, height);
    view.Flush();
}

Res CCustomTxVisitor::SubBalanceDelShares(const CScript &owner, const CBalances &balance) const {
    CalculateOwnerRewards(owner);
    auto res = mnview.SubBalances(owner, balance);
    if (!res) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, res.msg);
    }
    return DelShares(owner, balance.balances);
}

Res CCustomTxVisitor::AddBalanceSetShares(const CScript &owner, const CBalances &balance) const {
    CalculateOwnerRewards(owner);
    Require(mnview.AddBalances(owner, balance));
    return SetShares(owner, balance.balances);
}

Res CCustomTxVisitor::AddBalancesSetShares(const CAccounts &accounts) const {
    for (const auto &account : accounts) {
        Require(AddBalanceSetShares(account.first, account.second));
    }
    return Res::Ok();
}

Res CCustomTxVisitor::SubBalancesDelShares(const CAccounts &accounts) const {
    for (const auto &account : accounts) {
        Require(SubBalanceDelShares(account.first, account.second));
    }
    return Res::Ok();
}

Res CCustomTxVisitor::NormalizeTokenCurrencyPair(std::set<CTokenCurrencyPair> &tokenCurrency) const {
    std::set<CTokenCurrencyPair> trimmed;
    for (const auto &pair : tokenCurrency) {
        auto token    = trim_ws(pair.first).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        auto currency = trim_ws(pair.second).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        Require(!token.empty() && !currency.empty(), "empty token / currency");
        trimmed.emplace(token, currency);
    }
    tokenCurrency = std::move(trimmed);
    return Res::Ok();
}

bool CCustomTxVisitor::IsTokensMigratedToGovVar() const {
    return static_cast<int>(height) > consensus.FortCanningCrunchHeight + 1;
}

Res CCustomTxVisitor::IsOnChainGovernanceEnabled() const {
    CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovernanceEnabled};

    auto attributes = mnview.GetAttributes();
    Require(attributes, "Attributes unavailable");

    Require(attributes->GetValue(enabledKey, false), "Cannot create tx, on-chain governance is not enabled");

    return Res::Ok();
}

// -- -- -- -- -- -- -- -DONE

class CCustomTxApplyVisitor : public CCustomTxVisitor {
    uint64_t time;
    uint32_t txn;

public:
    CCustomTxApplyVisitor(const CTransaction &tx,
                          uint32_t height,
                          const CCoinsViewCache &coins,
                          CCustomCSView &mnview,
                          const Consensus::Params &consensus,
                          uint64_t time,
                          uint32_t txn)

        : CCustomTxVisitor(tx, height, coins, mnview, consensus),
          time(time),
          txn(txn) {}

    Res operator()(const CCreateMasterNodeMessage &obj) const {
        Require(CheckMasternodeCreationTx());

        if (height >= static_cast<uint32_t>(consensus.EunosHeight))
            Require(HasAuth(tx.vout[1].scriptPubKey), "masternode creation needs owner auth");

        if (height >= static_cast<uint32_t>(consensus.EunosPayaHeight)) {
            switch (obj.timelock) {
                case CMasternode::ZEROYEAR:
                case CMasternode::FIVEYEAR:
                case CMasternode::TENYEAR:
                    break;
                default:
                    return Res::Err("Timelock must be set to either 0, 5 or 10 years");
            }
        } else
            Require(obj.timelock == 0, "collateral timelock cannot be set below EunosPaya");

        CMasternode node;
        CTxDestination dest;
        if (ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
            if (dest.index() == PKHashType) {
                node.ownerType        = 1;
                node.ownerAuthAddress = CKeyID(std::get<PKHash>(dest));
            } else if (dest.index() == WitV0KeyHashType) {
                node.ownerType        = 4;
                node.ownerAuthAddress = CKeyID(std::get<WitnessV0KeyHash>(dest));
            }
        }
        node.creationHeight      = height;
        node.operatorType        = obj.operatorType;
        node.operatorAuthAddress = obj.operatorAuthAddress;

        // Set masternode version2 after FC for new serialisation
        if (height >= static_cast<uint32_t>(consensus.FortCanningHeight))
            node.version = CMasternode::VERSION0;

        bool duplicate{};
        mnview.ForEachNewCollateral([&](const uint256 &key, CLazySerialize<MNNewOwnerHeightValue> valueKey) {
            const auto &value = valueKey.get();
            if (height > value.blockHeight) {
                return true;
            }
            const auto &coin = coins.AccessCoin({key, 1});
            assert(!coin.IsSpent());
            CTxDestination pendingDest;
            assert(ExtractDestination(coin.out.scriptPubKey, pendingDest));
            const CKeyID storedID = pendingDest.index() == PKHashType ? CKeyID(std::get<PKHash>(pendingDest))
                                                                      : CKeyID(std::get<WitnessV0KeyHash>(pendingDest));
            if (storedID == node.ownerAuthAddress || storedID == node.operatorAuthAddress) {
                duplicate = true;
                return false;
            }
            return true;
        });

        if (duplicate) {
            return Res::ErrCode(CustomTxErrCodes::Fatal, "Masternode exist with that owner address pending");
        }

        Require(mnview.CreateMasternode(tx.GetHash(), node, obj.timelock));
        // Build coinage from the point of masternode creation

        if (height >= static_cast<uint32_t>(consensus.EunosPayaHeight))
            for (uint8_t i{0}; i < SUBNODE_COUNT; ++i)
                mnview.SetSubNodesBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), i, time);

        else if (height >= static_cast<uint32_t>(consensus.DakotaCrescentHeight))
            mnview.SetMasternodeLastBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), time);

        return Res::Ok();
    }

    Res operator()(const CResignMasterNodeMessage &obj) const {
        auto node = mnview.GetMasternode(obj);
        if (!node) return DeFiErrors::MNInvalid(obj.ToString());

        Require(HasCollateralAuth(node->collateralTx.IsNull() ? static_cast<uint256>(obj) : node->collateralTx));
        return mnview.ResignMasternode(*node, obj, tx.GetHash(), height);
    }

    Res operator()(const CUpdateMasterNodeMessage &obj) const {
        if (obj.updates.empty()) {
            return Res::Err("No update arguments provided");
        }

        if (obj.updates.size() > 3) {
            return Res::Err("Too many updates provided");
        }

        auto node = mnview.GetMasternode(obj.mnId);
        if (!node)
            return DeFiErrors::MNInvalidAltMsg(obj.mnId.ToString());

        const auto collateralTx = node->collateralTx.IsNull() ? obj.mnId : node->collateralTx;
        Require(HasCollateralAuth(collateralTx));

        auto state = node->GetState(height, mnview);
        if (state != CMasternode::ENABLED) {
            return DeFiErrors::MNStateNotEnabled(obj.mnId.ToString());
        }

        const auto attributes = mnview.GetAttributes();
        assert(attributes);

        bool ownerType{}, operatorType{}, rewardType{};
        for (const auto &[type, addressPair] : obj.updates) {
            const auto &[addressType, rawAddress] = addressPair;
            if (type == static_cast<uint8_t>(UpdateMasternodeType::OwnerAddress)) {
                CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MNSetOwnerAddress};
                if (!attributes->GetValue(key, false)) {
                    return Res::Err("Updating masternode owner address not currently enabled in attributes.");
                }
                if (ownerType) {
                    return Res::Err("Multiple owner updates provided");
                }
                ownerType = true;
                bool collateralFound{};
                for (const auto &vin : tx.vin) {
                    if (vin.prevout.hash == collateralTx && vin.prevout.n == 1) {
                        collateralFound = true;
                    }
                }
                if (!collateralFound) {
                    return Res::Err("Missing previous collateral from transaction inputs");
                }
                if (tx.vout.size() == 1) {
                    return Res::Err("Missing new collateral output");
                }

                CTxDestination dest;
                if (!ExtractDestination(tx.vout[1].scriptPubKey, dest) ||
                    (dest.index() != PKHashType && dest.index() != WitV0KeyHashType)) {
                    return Res::Err("Owner address must be P2PKH or P2WPKH type");
                }

                if (tx.vout[1].nValue != GetMnCollateralAmount(height)) {
                    return Res::Err("Incorrect collateral amount");
                }

                const auto keyID = dest.index() == PKHashType ? CKeyID(std::get<PKHash>(dest))
                                                              : CKeyID(std::get<WitnessV0KeyHash>(dest));
                if (mnview.GetMasternodeIdByOwner(keyID) || mnview.GetMasternodeIdByOperator(keyID)) {
                    return Res::Err("Masternode with collateral address as operator or owner already exists");
                }

                bool duplicate{};
                mnview.ForEachNewCollateral([&](const uint256 &key, CLazySerialize<MNNewOwnerHeightValue> valueKey) {
                    const auto &value = valueKey.get();
                    if (height > value.blockHeight) {
                        return true;
                    }
                    const auto &coin = coins.AccessCoin({key, 1});
                    assert(!coin.IsSpent());
                    CTxDestination pendingDest;
                    assert(ExtractDestination(coin.out.scriptPubKey, pendingDest));
                    const CKeyID storedID = pendingDest.index() == PKHashType
                                                ? CKeyID(std::get<PKHash>(pendingDest))
                                                : CKeyID(std::get<WitnessV0KeyHash>(pendingDest));
                    if (storedID == keyID) {
                        duplicate = true;
                        return false;
                    }
                    return true;
                });
                if (duplicate) {
                    return Res::ErrCode(CustomTxErrCodes::Fatal,
                                        "Masternode exist with that owner address pending already");
                }

                mnview.UpdateMasternodeCollateral(obj.mnId, *node, tx.GetHash(), height);
            } else if (type == static_cast<uint8_t>(UpdateMasternodeType::OperatorAddress)) {
                CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MNSetOperatorAddress};
                if (!attributes->GetValue(key, false)) {
                    return Res::Err("Updating masternode operator address not currently enabled in attributes.");
                }
                if (operatorType) {
                    return Res::Err("Multiple operator updates provided");
                }
                operatorType = true;

                if (addressType != 1 && addressType != 4) {
                    return Res::Err("Operator address must be P2PKH or P2WPKH type");
                }

                const auto keyID = CKeyID(uint160(rawAddress));
                if (mnview.GetMasternodeIdByOwner(keyID) || mnview.GetMasternodeIdByOperator(keyID)) {
                    return Res::Err("Masternode with that operator address already exists");
                }
                mnview.UpdateMasternodeOperator(obj.mnId, *node, addressType, keyID, height);
            } else if (type == static_cast<uint8_t>(UpdateMasternodeType::SetRewardAddress)) {
                CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MNSetRewardAddress};
                if (!attributes->GetValue(key, false)) {
                    return Res::Err("Updating masternode reward address not currently enabled in attributes.");
                }
                if (rewardType) {
                    return Res::Err("Multiple reward address updates provided");
                }
                rewardType = true;

                if (addressType != 1 && addressType != 4) {
                    return Res::Err("Reward address must be P2PKH or P2WPKH type");
                }

                const auto keyID = CKeyID(uint160(rawAddress));
                mnview.SetForcedRewardAddress(obj.mnId, *node, addressType, keyID, height);

                // Store history of all reward address changes. This allows us to call CalculateOwnerReward
                // on reward addresses owned by the local wallet. This can be removed some time after the
                // next hard fork as this is a workaround for the issue fixed in the following PR:
                // https://github.com/DeFiCh/ain/pull/1766
                if (auto addresses = mnview.SettingsGetRewardAddresses()) {
                    const CScript rewardAddress = GetScriptForDestination(addressType == PKHashType ?
                                                                          CTxDestination(PKHash(keyID)) :
                                                                          CTxDestination(WitnessV0KeyHash(keyID)));
                    addresses->insert(rewardAddress);
                    mnview.SettingsSetRewardAddresses(*addresses);
                }
            } else if (type == static_cast<uint8_t>(UpdateMasternodeType::RemRewardAddress)) {
                CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MNSetRewardAddress};
                if (!attributes->GetValue(key, false)) {
                    return Res::Err("Updating masternode reward address not currently enabled in attributes.");
                }
                if (rewardType) {
                    return Res::Err("Multiple reward address updates provided");
                }
                rewardType = true;

                mnview.RemForcedRewardAddress(obj.mnId, *node, height);
            } else {
                return Res::Err("Unknown update type provided");
            }
        }

        return Res::Ok();
    }

    Res operator()(const CCreateTokenMessage &obj) const {
        auto res = CheckTokenCreationTx();
        if (!res) {
            return res;
        }

        CTokenImplementation token;
        static_cast<CToken &>(token) = obj;

        token.symbol         = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        token.name           = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.creationTx     = tx.GetHash();
        token.creationHeight = height;

        // check foundation auth
        if (token.IsDAT() && !HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }

        if (static_cast<int>(height) >= consensus.BayfrontHeight) {  // formal compatibility if someone cheat and create
                                                                     // LPS token on the pre-bayfront node
            if (token.IsPoolShare()) {
                return Res::Err("Cant't manually create 'Liquidity Pool Share' token; use poolpair creation");
            }
        }

        return mnview.CreateToken(token, static_cast<int>(height) < consensus.BayfrontHeight);
    }

    Res operator()(const CUpdateTokenPreAMKMessage &obj) const {
        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        if (!pair) {
            return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());
        }
        auto token = pair->second;

        // check foundation auth
        auto res = HasFoundationAuth();

        if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
            token.flags ^= (uint8_t)CToken::TokenFlags::DAT;
            return !res ? res : mnview.UpdateToken(token, true);
        }
        return res;
    }

    Res operator()(const CUpdateTokenMessage &obj) const {
        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        Require(pair, "token with creationTx %s does not exist", obj.tokenTx.ToString());
        Require(pair->first != DCT_ID{0}, "Can't alter DFI token!");

        Require(!mnview.AreTokensLocked({pair->first.v}), "Cannot update token during lock");

        const auto &token = pair->second;

        // need to check it exectly here cause lps has no collateral auth (that checked next)
        Require(!token.IsPoolShare(),
                "token %s is the LPS token! Can't alter pool share's tokens!",
                obj.tokenTx.ToString());

        // check auth, depends from token's "origins"
        const Coin &auth = coins.AccessCoin(COutPoint(token.creationTx, 1));  // always n=1 output

        const auto attributes = mnview.GetAttributes();
        assert(attributes);
        std::set<CScript> databaseMembers;
        if (attributes->GetValue(CDataStructureV0{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovFoundation},
                                 false)) {
            databaseMembers = attributes->GetValue(
                CDataStructureV0{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members}, std::set<CScript>{});
        }
        bool isFoundersToken = !databaseMembers.empty() ? databaseMembers.count(auth.out.scriptPubKey) > 0
                                                        : consensus.foundationMembers.count(auth.out.scriptPubKey) > 0;

        if (isFoundersToken)
            Require(HasFoundationAuth());
        else
            Require(HasCollateralAuth(token.creationTx));

        // Check for isDAT change in non-foundation token after set height
        if (static_cast<int>(height) >= consensus.BayfrontMarinaHeight)
            // check foundation auth
            Require(obj.token.IsDAT() == token.IsDAT() || HasFoundationAuth(),
                    "can't set isDAT to true, tx not from foundation member");

        CTokenImplementation updatedToken{obj.token};
        updatedToken.creationTx        = token.creationTx;
        updatedToken.destructionTx     = token.destructionTx;
        updatedToken.destructionHeight = token.destructionHeight;
        if (static_cast<int>(height) >= consensus.FortCanningHeight)
            updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

        return mnview.UpdateToken(updatedToken);
    }

    Res operator()(const CMintTokensMessage &obj) const {
        const auto isRegTest                = Params().NetworkIDString() == CBaseChainParams::REGTEST;
        const auto isRegTestSimulateMainnet = gArgs.GetArg("-regtest-minttoken-simulate-mainnet", false);
        const auto fortCanningCrunchHeight  = static_cast<uint32_t>(consensus.FortCanningCrunchHeight);
        const auto grandCentralHeight       = static_cast<uint32_t>(consensus.GrandCentralHeight);

        CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MintTokens};
        const auto attributes = mnview.GetAttributes();
        assert(attributes);
        const auto toAddressEnabled = attributes->GetValue(enabledKey, false);

        if (!toAddressEnabled && !obj.to.empty())
            return Res::Err("Mint tokens to address is not enabled");

        // check auth and increase balance of token's owner
        for (const auto &[tokenId, amount] : obj.balances) {
            if (Params().NetworkIDString() == CBaseChainParams::MAIN && height >= fortCanningCrunchHeight &&
                mnview.GetLoanTokenByID(tokenId)) {
                return Res::Err("Loan tokens cannot be minted");
            }

            auto token = mnview.GetToken(tokenId);
            if (!token)
                return Res::Err("token %s does not exist!", tokenId.ToString());

            bool anybodyCanMint = isRegTest && !isRegTestSimulateMainnet;
            auto mintable       = MintableToken(tokenId, *token, anybodyCanMint);

            auto mintTokensInternal = [&](DCT_ID tokenId, CAmount amount) {
                auto minted = mnview.AddMintedTokens(tokenId, amount);
                if (!minted)
                    return minted;

                CScript mintTo{*mintable.val};
                if (!obj.to.empty()) {
                    CTxDestination destination;
                    if (ExtractDestination(obj.to, destination) && IsValidDestination(destination))
                        mintTo = obj.to;
                    else
                        return Res::Err("Invalid \'to\' address provided");
                }

                CalculateOwnerRewards(mintTo);
                auto res = mnview.AddBalance(mintTo, CTokenAmount{tokenId, amount});
                if (!res)
                    return res;

                return Res::Ok();
            };

            if (!mintable)
                return std::move(mintable);

            if (anybodyCanMint || height < grandCentralHeight || !token->IsDAT() || HasFoundationAuth()) {
                auto res = mintTokensInternal(tokenId, amount);
                if (!res)
                    return res;
                continue;
            }

            auto attributes = mnview.GetAttributes();
            assert(attributes);

            CDataStructureV0 enableKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::ConsortiumEnabled};
            CDataStructureV0 membersKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::MemberValues};
            const auto members = attributes->GetValue(membersKey, CConsortiumMembers{});

            if (!attributes->GetValue(enableKey, false) || members.empty()) {
                const Coin &auth = coins.AccessCoin(COutPoint(token->creationTx, 1));  // always n=1 output
                if (!HasAuth(auth.out.scriptPubKey))
                    return Res::Err("You are not a foundation member or token owner and cannot mint this token!");

                auto res = mintTokensInternal(tokenId, amount);
                if (!res)
                    return res;
                continue;
            }

            mintable.ok = false;

            CDataStructureV0 membersMintedKey{
                AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
            auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});

            const auto dailyInterval = height / consensus.blocksPerDay() * consensus.blocksPerDay();

            for (const auto &[key, member] : members) {
                if (HasAuth(member.ownerAddress)) {
                    if (member.status != CConsortiumMember::Status::Active)
                        return Res::Err("Cannot mint token, not an active member of consortium for %s!", token->symbol);

                    auto add = SafeAdd(membersBalances[tokenId][key].minted, amount);
                    if (!add)
                        return (std::move(add));
                    membersBalances[tokenId][key].minted = add;

                    if (dailyInterval == membersBalances[tokenId][key].dailyMinted.first) {
                        add = SafeAdd(membersBalances[tokenId][key].dailyMinted.second, amount);
                        if (!add)
                            return (std::move(add));
                        membersBalances[tokenId][key].dailyMinted.second = add;
                    } else {
                        membersBalances[tokenId][key].dailyMinted.first  = dailyInterval;
                        membersBalances[tokenId][key].dailyMinted.second = amount;
                    }

                    if (membersBalances[tokenId][key].minted > member.mintLimit)
                        return Res::Err("You will exceed your maximum mint limit for %s token by minting this amount!",
                                        token->symbol);

                    if (membersBalances[tokenId][key].dailyMinted.second > member.dailyMintLimit) {
                        return Res::Err("You will exceed your daily mint limit for %s token by minting this amount",
                                        token->symbol);
                    }

                    *mintable.val = member.ownerAddress;
                    mintable.ok   = true;
                    break;
                }
            }

            if (!mintable)
                return Res::Err("You are not a foundation or consortium member and cannot mint this token!");

            CDataStructureV0 maxLimitKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::MintLimit};
            const auto maxLimit = attributes->GetValue(maxLimitKey, CAmount{0});

            CDataStructureV0 dailyLimitKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::DailyMintLimit};
            const auto dailyLimit = attributes->GetValue(dailyLimitKey, CAmount{0});

            CDataStructureV0 consortiumMintedKey{
                AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
            auto globalBalances = attributes->GetValue(consortiumMintedKey, CConsortiumGlobalMinted{});

            auto add = SafeAdd(globalBalances[tokenId].minted, amount);
            if (!add)
                return (std::move(add));

            globalBalances[tokenId].minted = add;

            if (maxLimit != -1 * COIN && globalBalances[tokenId].minted > maxLimit)
                return Res::Err(
                    "You will exceed global maximum consortium mint limit for %s token by minting this amount!",
                    token->symbol);

            CAmount totalDaily{};
            for (const auto &[key, value] : membersBalances[tokenId]) {
                if (value.dailyMinted.first == dailyInterval) {
                    totalDaily += value.dailyMinted.second;
                }
            }

            if (dailyLimit != -1 * COIN && totalDaily > dailyLimit)
                return Res::Err(
                    "You will exceed global daily maximum consortium mint limit for %s token by minting this "
                    "amount.",
                    token->symbol);

            attributes->SetValue(consortiumMintedKey, globalBalances);
            attributes->SetValue(membersMintedKey, membersBalances);

            auto saved = mnview.SetVariable(*attributes);
            if (!saved)
                return saved;

            auto minted = mintTokensInternal(tokenId, amount);
            if (!minted)
                return minted;
        }

        return Res::Ok();
    }

    Res operator()(const CBurnTokensMessage &obj) const {
        if (obj.amounts.balances.empty()) {
            return Res::Err("tx must have balances to burn");
        }

        for (const auto &[tokenId, amount] : obj.amounts.balances) {
            // check auth
            if (!HasAuth(obj.from))
                return Res::Err("tx must have at least one input from account owner");

            if (obj.burnType != CBurnTokensMessage::BurnType::TokenBurn)
                return Res::Err("Currently only burn type 0 - TokenBurn is supported!");

            CScript ownerAddress;

            if (auto address = std::get_if<CScript>(&obj.context); address && !address->empty())
                ownerAddress = *address;
            else
                ownerAddress = obj.from;

            auto attributes = mnview.GetAttributes();
            Require(attributes, "Cannot read from attributes gov variable!");

            CDataStructureV0 membersKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::MemberValues};
            const auto members = attributes->GetValue(membersKey, CConsortiumMembers{});
            CDataStructureV0 membersMintedKey{
                AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
            auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});
            CDataStructureV0 consortiumMintedKey{
                AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
            auto globalBalances = attributes->GetValue(consortiumMintedKey, CConsortiumGlobalMinted{});

            bool setVariable = false;
            for (const auto &tmp : members)
                if (tmp.second.ownerAddress == ownerAddress) {
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

            if (setVariable) {
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

    Res operator()(const CCreatePoolPairMessage &obj) const {
        // check foundation auth
        Require(HasFoundationAuth());
        Require(obj.commission >= 0 && obj.commission <= COIN, "wrong commission");

        if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningCrunchHeight)) {
            Require(obj.pairSymbol.find('/') == std::string::npos, "token symbol should not contain '/'");
        }

        /// @todo ownerAddress validity checked only in rpc. is it enough?
        CPoolPair poolPair{};
        static_cast<CPoolPairMessageBase&>(poolPair) = obj;
        auto pairSymbol         = obj.pairSymbol;
        poolPair.creationTx     = tx.GetHash();
        poolPair.creationHeight = height;
        auto &rewards           = poolPair.rewards;

        auto tokenA = mnview.GetToken(poolPair.idTokenA);
        Require(tokenA, "token %s does not exist!", poolPair.idTokenA.ToString());

        auto tokenB = mnview.GetToken(poolPair.idTokenB);
        Require(tokenB, "token %s does not exist!", poolPair.idTokenB.ToString());

        const auto symbolLength = height >= static_cast<uint32_t>(consensus.FortCanningHeight)
                                      ? CToken::MAX_TOKEN_POOLPAIR_LENGTH
                                      : CToken::MAX_TOKEN_SYMBOL_LENGTH;
        if (pairSymbol.empty()) {
            pairSymbol = trim_ws(tokenA->symbol + "-" + tokenB->symbol).substr(0, symbolLength);
        } else {
            pairSymbol = trim_ws(pairSymbol).substr(0, symbolLength);
        }

        CTokenImplementation token;
        token.flags = (uint8_t)CToken::TokenFlags::DAT | (uint8_t)CToken::TokenFlags::LPS |
                      (uint8_t)CToken::TokenFlags::Tradeable | (uint8_t)CToken::TokenFlags::Finalized;

        token.name           = trim_ws(tokenA->name + "-" + tokenB->name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.symbol         = pairSymbol;
        token.creationTx     = tx.GetHash();
        token.creationHeight = height;

        auto tokenId = mnview.CreateToken(token);
        Require(tokenId);

        rewards = obj.rewards;
        if (!rewards.balances.empty()) {
            // Check tokens exist and remove empty reward amounts
            Require(EraseEmptyBalances(rewards.balances));
        }

        return mnview.SetPoolPair(tokenId, height, poolPair);
    }

    Res operator()(const CUpdatePoolPairMessage &obj) const {
        // check foundation auth
        Require(HasFoundationAuth());

        auto rewards = obj.rewards;
        if (!rewards.balances.empty()) {
            // Check for special case to wipe rewards
            if (!(rewards.balances.size() == 1 &&
                  rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()} &&
                  rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max())) {
                // Check if tokens exist and remove empty reward amounts
                Require(EraseEmptyBalances(rewards.balances));
            }
        }
        return mnview.UpdatePoolPair(obj.poolId, height, obj.status, obj.commission, obj.ownerAddress, rewards);
    }

    Res operator()(const CPoolSwapMessage &obj) const {
        // check auth
        Require(HasAuth(obj.from));

        return CPoolSwap(obj, height).ExecuteSwap(mnview, {});
    }

    Res operator()(const CPoolSwapMessageV2 &obj) const {
        // check auth
        Require(HasAuth(obj.swapInfo.from));

        return CPoolSwap(obj.swapInfo, height).ExecuteSwap(mnview, obj.poolIDs);
    }

    Res operator()(const CLiquidityMessage &obj) const {
        CBalances sumTx = SumAllTransfers(obj.from);
        Require(sumTx.balances.size() == 2, "the pool pair requires two tokens");

        std::pair<DCT_ID, CAmount> amountA = *sumTx.balances.begin();
        std::pair<DCT_ID, CAmount> amountB = *(std::next(sumTx.balances.begin(), 1));

        // checked internally too. remove here?
        Require(amountA.second > 0 && amountB.second > 0, "amount cannot be less than or equal to zero");

        auto pair = mnview.GetPoolPair(amountA.first, amountB.first);
        Require(pair, "there is no such pool pair");

        for (const auto &kv : obj.from) {
            Require(HasAuth(kv.first));
        }

        for (const auto &kv : obj.from) {
            CalculateOwnerRewards(kv.first);
            Require(mnview.SubBalances(kv.first, kv.second));
        }

        const auto &lpTokenID = pair->first;
        auto &pool            = pair->second;

        // normalize A & B to correspond poolpair's tokens
        if (amountA.first != pool.idTokenA)
            std::swap(amountA, amountB);

        bool slippageProtection = static_cast<int>(height) >= consensus.BayfrontMarinaHeight;
        Require(pool.AddLiquidity(
            amountA.second,
            amountB.second,
            [&] /*onMint*/ (CAmount liqAmount) {
                CBalances balance{TAmounts{{lpTokenID, liqAmount}}};
                return AddBalanceSetShares(obj.shareAddress, balance);
            },
            slippageProtection));
        return mnview.SetPoolPair(lpTokenID, height, pool);
    }

    Res operator()(const CRemoveLiquidityMessage &obj) const {
        const auto &from = obj.from;
        auto amount      = obj.amount;

        // checked internally too. remove here?
        Require(amount.nValue > 0, "amount cannot be less than or equal to zero");

        auto pair = mnview.GetPoolPair(amount.nTokenId);
        Require(pair, "there is no such pool pair");

        Require(HasAuth(from));

        CPoolPair &pool = pair.value();

        // subtract liq.balance BEFORE RemoveLiquidity call to check balance correctness
        CBalances balance{TAmounts{{amount.nTokenId, amount.nValue}}};
        Require(SubBalanceDelShares(from, balance));

        Require(pool.RemoveLiquidity(amount.nValue, [&](CAmount amountA, CAmount amountB) {
            CalculateOwnerRewards(from);
            CBalances balances{
                TAmounts{{pool.idTokenA, amountA}, {pool.idTokenB, amountB}}
            };
            return mnview.AddBalances(from, balances);
        }));

        return mnview.SetPoolPair(amount.nTokenId, height, pool);
    }

    Res operator()(const CUtxosToAccountMessage &obj) const {
        // check enough tokens are "burnt"
        auto burnt = BurntTokens(tx);
        Require(burnt);

        const auto mustBeBurnt = SumAllTransfers(obj.to);
        Require(*burnt.val == mustBeBurnt,
                "transfer tokens mismatch burnt tokens: (%s) != (%s)",
                mustBeBurnt.ToString(),
                burnt.val->ToString());

        // transfer
        return AddBalancesSetShares(obj.to);
    }

    Res operator()(const CAccountToUtxosMessage &obj) const {
        // check auth
        Require(HasAuth(obj.from));

        // check that all tokens are minted, and no excess tokens are minted
        auto minted = MintedTokens(tx, obj.mintingOutputsStart);
        Require(minted);

        Require(obj.balances == *minted.val,
                "amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)",
                minted.val->ToString(),
                obj.balances.ToString());

        // block for non-DFI transactions
        for (const auto &kv : obj.balances.balances) {
            const DCT_ID &tokenId = kv.first;
            Require(tokenId == DCT_ID{0}, "only available for DFI transactions");
        }

        // transfer
        return SubBalanceDelShares(obj.from, obj.balances);
    }

    Res operator()(const CAccountToAccountMessage &obj) const {
        // check auth
        Require(HasAuth(obj.from));

        // transfer
        Require(SubBalanceDelShares(obj.from, SumAllTransfers(obj.to)));
        return AddBalancesSetShares(obj.to);
    }

    Res HandleDFIP2201Contract(const CSmartContractMessage &obj) const {
        const auto attributes = mnview.GetAttributes();
        Require(attributes, "Attributes unavailable");

        CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Active};

        Require(attributes->GetValue(activeKey, false), "DFIP2201 smart contract is not enabled");

        Require(obj.name == SMART_CONTRACT_DFIP_2201, "DFIP2201 contract mismatch - got: " + obj.name);

        Require(obj.accounts.size() == 1, "Only one address entry expected for " + obj.name);

        Require(obj.accounts.begin()->second.balances.size() == 1, "Only one amount entry expected for " + obj.name);

        const auto &script = obj.accounts.begin()->first;
        Require(HasAuth(script), "Must have at least one input from supplied address");

        const auto &id     = obj.accounts.begin()->second.balances.begin()->first;
        const auto &amount = obj.accounts.begin()->second.balances.begin()->second;

        Require(amount > 0, "Amount out of range");

        CDataStructureV0 minSwapKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::MinSwap};
        auto minSwap = attributes->GetValue(minSwapKey, CAmount{0});

        if (amount < minSwap) {
            return DeFiErrors::ICXBTCBelowMinSwap(amount, minSwap);
        }

        const auto token = mnview.GetToken(id);
        Require(token, "Specified token not found");

        Require(token->symbol == "BTC" && token->name == "Bitcoin" && token->IsDAT(),
                "Only Bitcoin can be swapped in " + obj.name);

        if (height >= static_cast<uint32_t>(consensus.NextNetworkUpgradeHeight)) {
            mnview.CalculateOwnerRewards(script, height);
        }

        Require(mnview.SubBalance(script, {id, amount}));

        const CTokenCurrencyPair btcUsd{"BTC", "USD"};
        const CTokenCurrencyPair dfiUsd{"DFI", "USD"};

        bool useNextPrice{false}, requireLivePrice{true};
        auto resVal = mnview.GetValidatedIntervalPrice(btcUsd, useNextPrice, requireLivePrice);
        Require(resVal);

        CDataStructureV0 premiumKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Premium};
        auto premium = attributes->GetValue(premiumKey, CAmount{2500000});

        const auto &btcPrice = MultiplyAmounts(*resVal.val, premium + COIN);

        resVal = mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice);
        Require(resVal);

        const auto totalDFI = MultiplyAmounts(DivideAmounts(btcPrice, *resVal.val), amount);

        Require(mnview.SubBalance(Params().GetConsensus().smartContracts.begin()->second, {{0}, totalDFI}));

        Require(mnview.AddBalance(script, {{0}, totalDFI}));

        return Res::Ok();
    }

    Res operator()(const CSmartContractMessage &obj) const {
        Require(!obj.accounts.empty(), "Contract account parameters missing");
        auto contracts = Params().GetConsensus().smartContracts;

        auto contract = contracts.find(obj.name);
        Require(contract != contracts.end(), "Specified smart contract not found");

        // Convert to switch when it's long enough.
        if (obj.name == SMART_CONTRACT_DFIP_2201)
            return HandleDFIP2201Contract(obj);

        return Res::Err("Specified smart contract not found");
    }

    Res operator()(const CFutureSwapMessage &obj) const {
        Require(HasAuth(obj.owner), "Transaction must have at least one input from owner");

        const auto attributes = mnview.GetAttributes();
        Require(attributes, "Attributes unavailable");

        bool dfiToDUSD     = !obj.source.nTokenId.v;
        const auto paramID = dfiToDUSD ? ParamIDs::DFIP2206F : ParamIDs::DFIP2203;

        CDataStructureV0 activeKey{AttributeTypes::Param, paramID, DFIPKeys::Active};
        CDataStructureV0 blockKey{AttributeTypes::Param, paramID, DFIPKeys::BlockPeriod};
        CDataStructureV0 rewardKey{AttributeTypes::Param, paramID, DFIPKeys::RewardPct};

        Require(
            attributes->GetValue(activeKey, false) && attributes->CheckKey(blockKey) && attributes->CheckKey(rewardKey),
            "%s not currently active",
            dfiToDUSD ? "DFIP2206F" : "DFIP2203");

        CDataStructureV0 startKey{AttributeTypes::Param, paramID, DFIPKeys::StartBlock};
        if (const auto startBlock = attributes->GetValue(startKey, CAmount{})) {
            Require(
                height >= startBlock, "%s not active until block %d", dfiToDUSD ? "DFIP2206F" : "DFIP2203", startBlock);
        }

        Require(obj.source.nValue > 0, "Source amount must be more than zero");

        const auto source = mnview.GetLoanTokenByID(obj.source.nTokenId);
        Require(dfiToDUSD || source, "Could not get source loan token %d", obj.source.nTokenId.v);

        if (!dfiToDUSD && source->symbol == "DUSD") {
            CDataStructureV0 tokenKey{AttributeTypes::Token, obj.destination, TokenKeys::DFIP2203Enabled};
            const auto enabled = attributes->GetValue(tokenKey, true);
            Require(enabled, "DFIP2203 currently disabled for token %d", obj.destination);

            const auto loanToken = mnview.GetLoanTokenByID({obj.destination});
            Require(loanToken, "Could not get destination loan token %d. Set valid destination.", obj.destination);

            Require(!mnview.AreTokensLocked({obj.destination}), "Cannot create future swap for locked token");
        } else {
            if (!dfiToDUSD) {
                Require(obj.destination == 0, "Destination should not be set when source amount is dToken or DFI");

                Require(!mnview.AreTokensLocked({obj.source.nTokenId.v}), "Cannot create future swap for locked token");

                CDataStructureV0 tokenKey{AttributeTypes::Token, obj.source.nTokenId.v, TokenKeys::DFIP2203Enabled};
                const auto enabled = attributes->GetValue(tokenKey, true);
                Require(enabled, "DFIP2203 currently disabled for token %s", obj.source.nTokenId.ToString());
            } else {
                DCT_ID id{};
                const auto token = mnview.GetTokenGuessId("DUSD", id);
                Require(token, "No DUSD token defined");

                Require(mnview.GetFixedIntervalPrice({"DFI", "USD"}), "DFI / DUSD fixed interval price not found");

                Require(obj.destination == id.v,
                        "Incorrect destination defined for DFI swap, DUSD destination expected id: %d",
                        id.v);
            }
        }

        const auto contractType         = dfiToDUSD ? SMART_CONTRACT_DFIP2206F : SMART_CONTRACT_DFIP_2203;
        const auto contractAddressValue = GetFutureSwapContractAddress(contractType);
        Require(contractAddressValue);

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

                mnview.ForEachFuturesUserValues(
                    [&](const CFuturesUserKey &key, const CFuturesUserValue &futuresValues) {
                        if (key.owner == obj.owner && futuresValues.source.nTokenId == obj.source.nTokenId &&
                            futuresValues.destination == obj.destination) {
                            userFuturesValues[key] = futuresValues;
                        }
                        return true;
                    },
                    {height, obj.owner, std::numeric_limits<uint32_t>::max()});

                for (const auto &[key, value] : userFuturesValues) {
                    totalFutures.Add(value.source.nValue);
                    mnview.EraseFuturesUserValues(key);
                }
            } else {
                std::map<CFuturesUserKey, CAmount> userFuturesValues;

                mnview.ForEachFuturesDUSD(
                    [&](const CFuturesUserKey &key, const CAmount &futuresValues) {
                        if (key.owner == obj.owner) {
                            userFuturesValues[key] = futuresValues;
                        }
                        return true;
                    },
                    {height, obj.owner, std::numeric_limits<uint32_t>::max()});

                for (const auto &[key, amount] : userFuturesValues) {
                    totalFutures.Add(amount);
                    mnview.EraseFuturesDUSD(key);
                }
            }

            Require(totalFutures.Sub(obj.source.nValue));

            if (totalFutures.nValue > 0) {
                Res res{};
                if (!dfiToDUSD) {
                    Require(mnview.StoreFuturesUserValues({height, obj.owner, txn}, {totalFutures, obj.destination}));
                } else {
                    Require(mnview.StoreFuturesDUSD({height, obj.owner, txn}, totalFutures.nValue));
                }
            }

            Require(TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, *contractAddressValue, obj.owner));

            Require(balances.Sub(obj.source));
        } else {
            Require(TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, obj.owner, *contractAddressValue));

            if (!dfiToDUSD) {
                Require(mnview.StoreFuturesUserValues({height, obj.owner, txn}, {obj.source, obj.destination}));
            } else {
                Require(mnview.StoreFuturesDUSD({height, obj.owner, txn}, obj.source.nValue));
            }
            balances.Add(obj.source);
        }

        attributes->SetValue(liveKey, balances);

        mnview.SetVariable(*attributes);

        return Res::Ok();
    }

    Res operator()(const CAnyAccountsToAccountsMessage &obj) const {
        // check auth
        for (const auto &kv : obj.from) {
            Require(HasAuth(kv.first));
        }

        // compare
        const auto sumFrom = SumAllTransfers(obj.from);
        const auto sumTo   = SumAllTransfers(obj.to);

        Require(sumFrom == sumTo, "sum of inputs (from) != sum of outputs (to)");

        // transfer
        // substraction
        Require(SubBalancesDelShares(obj.from));
        // addition
        return AddBalancesSetShares(obj.to);
    }

    Res operator()(const CGovernanceMessage &obj) const {
        // check foundation auth
        Require(HasFoundationAuth());
        for (const auto &gov : obj.govs) {
            if (!gov.second) {
                return Res::Err("'%s': variable does not registered", gov.first);
            }

            auto var = gov.second;
            Res res{};

            if (var->GetName() == "ATTRIBUTES") {
                // Add to existing ATTRIBUTES instead of overwriting.
                auto govVar = mnview.GetAttributes();

                if (!govVar) {
                    return Res::Err("%s: %s", var->GetName(), "Failed to get existing ATTRIBUTES");
                }

                govVar->time = time;

                auto newVar = std::dynamic_pointer_cast<ATTRIBUTES>(var);
                assert(newVar);

                CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members};
                auto memberRemoval = newVar->GetValue(key, std::set<std::string>{});

                if (!memberRemoval.empty()) {
                    auto existingMembers = govVar->GetValue(key, std::set<CScript>{});

                    for (auto &member : memberRemoval) {
                        if (member.empty()) {
                            return Res::Err("Invalid address provided");
                        }

                        if (member[0] == '-') {
                            auto memberCopy{member};
                            const auto dest = DecodeDestination(memberCopy.erase(0, 1));
                            if (!IsValidDestination(dest)) {
                                return Res::Err("Invalid address provided");
                            }
                            CScript removeMember = GetScriptForDestination(dest);
                            if (!existingMembers.count(removeMember)) {
                                return Res::Err("Member to remove not present");
                            }
                            existingMembers.erase(removeMember);
                        } else {
                            const auto dest = DecodeDestination(member);
                            if (!IsValidDestination(dest)) {
                                return Res::Err("Invalid address provided");
                            }
                            CScript addMember = GetScriptForDestination(dest);
                            if (existingMembers.count(addMember)) {
                                return Res::Err("Member to add already present");
                            }
                            existingMembers.insert(addMember);
                        }
                    }

                    govVar->SetValue(key, existingMembers);

                    // Remove this key and apply any other changes
                    newVar->EraseKey(key);
                    if (!(res = govVar->Import(newVar->Export())) || !(res = govVar->Validate(mnview)) ||
                        !(res = govVar->Apply(mnview, height)))
                        return Res::Err("%s: %s", var->GetName(), res.msg);
                } else {
                    // Validate as complete set. Check for future conflicts between key pairs.
                    if (!(res = govVar->Import(var->Export())) || !(res = govVar->Validate(mnview)) ||
                        !(res = govVar->Apply(mnview, height)))
                        return Res::Err("%s: %s", var->GetName(), res.msg);
                }

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
                        storeGovVars({gov.first, var, height + mnview.GetIntervalBlock() - diff}, mnview);
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

    Res operator()(const CGovernanceUnsetMessage &obj) const {
        // check foundation auth
        if (!HasFoundationAuth())
            return Res::Err("tx not from foundation member");

        const auto attributes = mnview.GetAttributes();
        assert(attributes);
        CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovUnset};
        if (!attributes->GetValue(key, false)) {
            return Res::Err("Unset Gov variables not currently enabled in attributes.");
        }

        for (const auto &gov : obj.govs) {
            auto var = mnview.GetVariable(gov.first);
            if (!var)
                return Res::Err("'%s': variable does not registered", gov.first);

            auto res = var->Erase(mnview, height, gov.second);
            if (!res)
                return Res::Err("%s: %s", var->GetName(), res.msg);

            if (!(res = mnview.SetVariable(*var)))
                return Res::Err("%s: %s", var->GetName(), res.msg);
        }
        return Res::Ok();
    }

    Res operator()(const CGovernanceHeightMessage &obj) const {
        // check foundation auth
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
        if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) &&
            obj.govVar->GetName() == "ATTRIBUTES") {
            auto govVar = mnview.GetAttributes();
            if (!govVar) {
                return Res::Err("%s: %s", obj.govVar->GetName(), "Failed to get existing ATTRIBUTES");
            }

            auto storedGovVars = mnview.GetStoredVariablesRange(height, obj.startHeight);

            Res res{};
            CCustomCSView govCache(mnview);
            for (const auto &[varHeight, var] : storedGovVars) {
                if (var->GetName() == "ATTRIBUTES") {
                    if (res = govVar->Import(var->Export()); !res) {
                        return Res::Err("%s: Failed to import stored vars: %s", obj.govVar->GetName(), res.msg);
                    }
                }
            }

            // After GW exclude TokenSplit if split will have already been performed by startHeight
            if (height >= static_cast<uint32_t>(Params().GetConsensus().GrandCentralHeight)) {
                if (const auto attrVar = std::dynamic_pointer_cast<ATTRIBUTES>(govVar); attrVar) {
                    const auto attrMap = attrVar->GetAttributesMap();
                    std::vector<CDataStructureV0> keysToErase;
                    for (const auto &[key, value] : attrMap) {
                        if (const auto attrV0 = std::get_if<CDataStructureV0>(&key); attrV0) {
                            if (attrV0->type == AttributeTypes::Oracles && attrV0->typeId == OracleIDs::Splits &&
                                attrV0->key < obj.startHeight) {
                                keysToErase.push_back(*attrV0);
                            }
                        }
                    }
                    for (const auto &key : keysToErase) {
                        attrVar->EraseKey(key);
                    }
                }
            }

            if (!(res = govVar->Import(obj.govVar->Export())) || !(res = govVar->Validate(govCache)) ||
                !(res = govVar->Apply(govCache, obj.startHeight))) {
                return Res::Err("%s: Cumulative application of Gov vars failed: %s", obj.govVar->GetName(), res.msg);
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

    Res operator()(const CAppointOracleMessage &obj) const {
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        COracle oracle;
        static_cast<CAppointOracleMessage &>(oracle) = obj;
        auto res                                     = NormalizeTokenCurrencyPair(oracle.availablePairs);
        return !res ? res : mnview.AppointOracle(tx.GetHash(), oracle);
    }

    Res operator()(const CUpdateOracleAppointMessage &obj) const {
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        COracle oracle;
        static_cast<CAppointOracleMessage &>(oracle) = obj.newOracleAppoint;
        Require(NormalizeTokenCurrencyPair(oracle.availablePairs));
        return mnview.UpdateOracle(obj.oracleId, std::move(oracle));
    }

    Res operator()(const CRemoveOracleAppointMessage &obj) const {
        Require(HasFoundationAuth());

        return mnview.RemoveOracle(obj.oracleId);
    }

    Res operator()(const CSetOracleDataMessage &obj) const {
        auto oracle = mnview.GetOracleData(obj.oracleId);
        if (!oracle) {
            return Res::Err("failed to retrieve oracle <%s> from database", obj.oracleId.GetHex());
        }
        if (!HasAuth(oracle.val->oracleAddress)) {
            return Res::Err("tx must have at least one input from account owner");
        }
        if (height >= uint32_t(Params().GetConsensus().FortCanningHeight)) {
            for (const auto &tokenPrice : obj.tokenPrices) {
                for (const auto &price : tokenPrice.second) {
                    if (price.second <= 0) {
                        return Res::Err("Amount out of range");
                    }
                    auto timestamp = time;
                    extern bool diffInHour(int64_t time1, int64_t time2);
                    if (!diffInHour(obj.timestamp, timestamp)) {
                        return Res::Err(
                            "Timestamp (%d) is out of price update window (median: %d)", obj.timestamp, timestamp);
                    }
                }
            }
        }
        return mnview.SetOracleData(obj.oracleId, obj.timestamp, obj.tokenPrices);
    }

    Res operator()(const CICXCreateOrderMessage &obj) const {
        Require(CheckCustomTx());

        CICXOrderImplemetation order;
        static_cast<CICXOrder &>(order) = obj;

        order.creationTx     = tx.GetHash();
        order.creationHeight = height;

        Require(HasAuth(order.ownerAddress), "tx must have at least one input from order owner");

        Require(mnview.GetToken(order.idToken), "token %s does not exist!", order.idToken.ToString());

        if (order.orderType == CICXOrder::TYPE_INTERNAL) {
            Require(order.receivePubkey.IsFullyValid(), "receivePubkey must be valid pubkey");

            // subtract the balance from tokenFrom to dedicate them for the order
            CScript txidAddr(order.creationTx.begin(), order.creationTx.end());
            CalculateOwnerRewards(order.ownerAddress);
            Require(TransferTokenBalance(order.idToken, order.amountFrom, order.ownerAddress, txidAddr));
        }

        return mnview.ICXCreateOrder(order);
    }

    Res operator()(const CICXMakeOfferMessage &obj) const {
        Require(CheckCustomTx());

        CICXMakeOfferImplemetation makeoffer;
        static_cast<CICXMakeOffer &>(makeoffer) = obj;

        makeoffer.creationTx     = tx.GetHash();
        makeoffer.creationHeight = height;

        Require(HasAuth(makeoffer.ownerAddress), "tx must have at least one input from order owner");

        auto order = mnview.GetICXOrderByCreationTx(makeoffer.orderTx);
        Require(order, "order with creation tx " + makeoffer.orderTx.GetHex() + " does not exists!");

        auto expiry = static_cast<int>(height) < consensus.EunosPayaHeight ? CICXMakeOffer::DEFAULT_EXPIRY
                                                                           : CICXMakeOffer::EUNOSPAYA_DEFAULT_EXPIRY;

        Require(makeoffer.expiry >= expiry, "offer expiry must be greater than %d!", expiry - 1);

        CScript txidAddr(makeoffer.creationTx.begin(), makeoffer.creationTx.end());

        if (order->orderType == CICXOrder::TYPE_INTERNAL) {
            // calculating takerFee
            makeoffer.takerFee = CalculateTakerFee(makeoffer.amount);
        } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
            Require(makeoffer.receivePubkey.IsFullyValid(), "receivePubkey must be valid pubkey");

            // calculating takerFee
            CAmount BTCAmount(static_cast<CAmount>(
                (arith_uint256(makeoffer.amount) * arith_uint256(COIN) / arith_uint256(order->orderPrice)).GetLow64()));
            makeoffer.takerFee = CalculateTakerFee(BTCAmount);
        }

        // locking takerFee in offer txidaddr
        CalculateOwnerRewards(makeoffer.ownerAddress);
        Require(TransferTokenBalance(DCT_ID{0}, makeoffer.takerFee, makeoffer.ownerAddress, txidAddr));

        return mnview.ICXMakeOffer(makeoffer);
    }

    Res operator()(const CICXSubmitDFCHTLCMessage &obj) const {
        Require(CheckCustomTx());

        CICXSubmitDFCHTLCImplemetation submitdfchtlc;
        static_cast<CICXSubmitDFCHTLC &>(submitdfchtlc) = obj;

        submitdfchtlc.creationTx     = tx.GetHash();
        submitdfchtlc.creationHeight = height;

        auto offer = mnview.GetICXMakeOfferByCreationTx(submitdfchtlc.offerTx);
        Require(offer, "offer with creation tx %s does not exists!", submitdfchtlc.offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        Require(order, "order with creation tx %s does not exists!", offer->orderTx.GetHex());

        Require(order->creationHeight + order->expiry >= height + submitdfchtlc.timeout,
                "order will expire before dfc htlc expires!");
        Require(!mnview.HasICXSubmitDFCHTLCOpen(submitdfchtlc.offerTx), "dfc htlc already submitted!");

        CScript srcAddr;
        if (order->orderType == CICXOrder::TYPE_INTERNAL) {
            // check auth
            Require(HasAuth(order->ownerAddress), "tx must have at least one input from order owner");
            Require(mnview.HasICXMakeOfferOpen(offer->orderTx, submitdfchtlc.offerTx),
                    "offerTx (%s) has expired",
                    submitdfchtlc.offerTx.GetHex());

            uint32_t timeout;
            if (static_cast<int>(height) < consensus.EunosPayaHeight)
                timeout = CICXSubmitDFCHTLC::MINIMUM_TIMEOUT;
            else
                timeout = CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;

            Require(submitdfchtlc.timeout >= timeout, "timeout must be greater than %d", timeout - 1);

            srcAddr = CScript(order->creationTx.begin(), order->creationTx.end());

            CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());

            auto calcAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
            Require(calcAmount <= offer->amount, "amount must be lower or equal the offer one");

            CAmount takerFee = offer->takerFee;
            // EunosPaya: calculating adjusted takerFee only if amount in htlc different than in offer
            if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
                if (calcAmount < offer->amount) {
                    auto BTCAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
                    takerFee       = (arith_uint256(BTCAmount) * offer->takerFee / offer->amount).GetLow64();
                }
            } else {
                auto BTCAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
                takerFee       = CalculateTakerFee(BTCAmount);
            }

            // refund the rest of locked takerFee if there is difference
            if (offer->takerFee - takerFee) {
                CalculateOwnerRewards(offer->ownerAddress);
                Require(
                    TransferTokenBalance(DCT_ID{0}, offer->takerFee - takerFee, offerTxidAddr, offer->ownerAddress));

                // update the offer with adjusted takerFee
                offer->takerFee = takerFee;
                mnview.ICXUpdateMakeOffer(*offer);
            }

            // burn takerFee
            Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, offerTxidAddr, consensus.burnAddress));

            // burn makerDeposit
            CalculateOwnerRewards(order->ownerAddress);
            Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, order->ownerAddress, consensus.burnAddress));

        } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
            // check auth
            Require(HasAuth(offer->ownerAddress), "tx must have at least one input from offer owner");

            srcAddr = offer->ownerAddress;
            CalculateOwnerRewards(offer->ownerAddress);

            auto exthtlc = mnview.HasICXSubmitEXTHTLCOpen(submitdfchtlc.offerTx);
            Require(exthtlc,
                    "offer (%s) needs to have ext htlc submitted first, but no external htlc found!",
                    submitdfchtlc.offerTx.GetHex());

            auto calcAmount = MultiplyAmounts(exthtlc->amount, order->orderPrice);
            Require(submitdfchtlc.amount == calcAmount, "amount must be equal to calculated exthtlc amount");

            Require(submitdfchtlc.hash == exthtlc->hash,
                    "Invalid hash, dfc htlc hash is different than extarnal htlc hash - %s != %s",
                    submitdfchtlc.hash.GetHex(),
                    exthtlc->hash.GetHex());

            uint32_t timeout, btcBlocksInDfi;
            if (static_cast<int>(height) < consensus.EunosPayaHeight) {
                timeout        = CICXSubmitDFCHTLC::MINIMUM_2ND_TIMEOUT;
                btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
            } else {
                timeout        = CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;
                btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
            }

            Require(submitdfchtlc.timeout >= timeout, "timeout must be greater than %d", timeout - 1);
            Require(submitdfchtlc.timeout < (exthtlc->creationHeight + (exthtlc->timeout * btcBlocksInDfi)) - height,
                    "timeout must be less than expiration period of 1st htlc in DFI blocks");
        }

        // subtract the balance from order txidaddr or offer owner address and dedicate them for the dfc htlc
        CScript htlcTxidAddr(submitdfchtlc.creationTx.begin(), submitdfchtlc.creationTx.end());

        Require(TransferTokenBalance(order->idToken, submitdfchtlc.amount, srcAddr, htlcTxidAddr));
        return mnview.ICXSubmitDFCHTLC(submitdfchtlc);
    }

    Res operator()(const CICXSubmitEXTHTLCMessage &obj) const {
        Require(CheckCustomTx());

        CICXSubmitEXTHTLCImplemetation submitexthtlc;
        static_cast<CICXSubmitEXTHTLC &>(submitexthtlc) = obj;

        submitexthtlc.creationTx     = tx.GetHash();
        submitexthtlc.creationHeight = height;

        auto offer = mnview.GetICXMakeOfferByCreationTx(submitexthtlc.offerTx);
        Require(offer, "order with creation tx %s does not exists!", submitexthtlc.offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        Require(order, "order with creation tx %s does not exists!", offer->orderTx.GetHex());

        Require(order->creationHeight + order->expiry >=
                    height + (submitexthtlc.timeout * CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS),
                "order will expire before ext htlc expires!");

        Require(!mnview.HasICXSubmitEXTHTLCOpen(submitexthtlc.offerTx), "ext htlc already submitted!");

        if (order->orderType == CICXOrder::TYPE_INTERNAL) {
            Require(HasAuth(offer->ownerAddress), "tx must have at least one input from offer owner");

            auto dfchtlc = mnview.HasICXSubmitDFCHTLCOpen(submitexthtlc.offerTx);
            Require(dfchtlc,
                    "offer (%s) needs to have dfc htlc submitted first, but no dfc htlc found!",
                    submitexthtlc.offerTx.GetHex());

            auto calcAmount = MultiplyAmounts(dfchtlc->amount, order->orderPrice);
            Require(submitexthtlc.amount == calcAmount, "amount must be equal to calculated dfchtlc amount");
            Require(submitexthtlc.hash == dfchtlc->hash,
                    "Invalid hash, external htlc hash is different than dfc htlc hash");

            uint32_t timeout, btcBlocksInDfi;
            if (static_cast<int>(height) < consensus.EunosPayaHeight) {
                timeout        = CICXSubmitEXTHTLC::MINIMUM_2ND_TIMEOUT;
                btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
            } else {
                timeout        = CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;
                btcBlocksInDfi = CICXSubmitEXTHTLC::EUNOSPAYA_BTC_BLOCKS_IN_DFI_BLOCKS;
            }

            Require(submitexthtlc.timeout >= timeout, "timeout must be greater than %d", timeout - 1);
            Require(submitexthtlc.timeout * btcBlocksInDfi < (dfchtlc->creationHeight + dfchtlc->timeout) - height,
                    "timeout must be less than expiration period of 1st htlc in DFC blocks");

        } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
            Require(HasAuth(order->ownerAddress), "tx must have at least one input from order owner");
            Require(mnview.HasICXMakeOfferOpen(offer->orderTx, submitexthtlc.offerTx),
                    "offerTx (%s) has expired",
                    submitexthtlc.offerTx.GetHex());

            uint32_t timeout;
            if (static_cast<int>(height) < consensus.EunosPayaHeight)
                timeout = CICXSubmitEXTHTLC::MINIMUM_TIMEOUT;
            else
                timeout = CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;

            Require(submitexthtlc.timeout >= timeout, "timeout must be greater than %d", timeout - 1);

            CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());

            auto calcAmount = MultiplyAmounts(submitexthtlc.amount, order->orderPrice);
            Require(calcAmount <= offer->amount, "amount must be lower or equal the offer one");

            CAmount takerFee = offer->takerFee;
            // EunosPaya: calculating adjusted takerFee only if amount in htlc different than in offer
            if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
                if (calcAmount < offer->amount) {
                    auto BTCAmount = DivideAmounts(offer->amount, order->orderPrice);
                    takerFee       = (arith_uint256(submitexthtlc.amount) * offer->takerFee / BTCAmount).GetLow64();
                }
            } else
                takerFee = CalculateTakerFee(submitexthtlc.amount);

            // refund the rest of locked takerFee if there is difference
            if (offer->takerFee - takerFee) {
                CalculateOwnerRewards(offer->ownerAddress);
                Require(
                    TransferTokenBalance(DCT_ID{0}, offer->takerFee - takerFee, offerTxidAddr, offer->ownerAddress));

                // update the offer with adjusted takerFee
                offer->takerFee = takerFee;
                mnview.ICXUpdateMakeOffer(*offer);
            }

            // burn takerFee
            Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, offerTxidAddr, consensus.burnAddress));

            // burn makerDeposit
            CalculateOwnerRewards(order->ownerAddress);
            Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, order->ownerAddress, consensus.burnAddress));
        }

        return mnview.ICXSubmitEXTHTLC(submitexthtlc);
    }

    Res operator()(const CICXClaimDFCHTLCMessage &obj) const {
        Require(CheckCustomTx());

        CICXClaimDFCHTLCImplemetation claimdfchtlc;
        static_cast<CICXClaimDFCHTLC &>(claimdfchtlc) = obj;

        claimdfchtlc.creationTx     = tx.GetHash();
        claimdfchtlc.creationHeight = height;

        auto dfchtlc = mnview.GetICXSubmitDFCHTLCByCreationTx(claimdfchtlc.dfchtlcTx);
        Require(dfchtlc, "dfc htlc with creation tx %s does not exists!", claimdfchtlc.dfchtlcTx.GetHex());

        Require(mnview.HasICXSubmitDFCHTLCOpen(dfchtlc->offerTx), "dfc htlc not found or already claimed or refunded!");

        uint256 calcHash;
        uint8_t calcSeedBytes[32];
        CSHA256().Write(claimdfchtlc.seed.data(), claimdfchtlc.seed.size()).Finalize(calcSeedBytes);
        calcHash.SetHex(HexStr(calcSeedBytes, calcSeedBytes + 32));

        Require(dfchtlc->hash == calcHash,
                "hash generated from given seed is different than in dfc htlc: %s - %s!",
                calcHash.GetHex(),
                dfchtlc->hash.GetHex());

        auto offer = mnview.GetICXMakeOfferByCreationTx(dfchtlc->offerTx);
        Require(offer, "offer with creation tx %s does not exists!", dfchtlc->offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        Require(order, "order with creation tx %s does not exists!", offer->orderTx.GetHex());

        auto exthtlc = mnview.HasICXSubmitEXTHTLCOpen(dfchtlc->offerTx);
        if (static_cast<int>(height) < consensus.EunosPayaHeight)
            Require(exthtlc, "cannot claim, external htlc for this offer does not exists or expired!");

        // claim DFC HTLC to receiveAddress
        CalculateOwnerRewards(order->ownerAddress);
        CScript htlcTxidAddr(dfchtlc->creationTx.begin(), dfchtlc->creationTx.end());

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
            Require(TransferTokenBalance(order->idToken, dfchtlc->amount, htlcTxidAddr, offer->ownerAddress));
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
            Require(TransferTokenBalance(order->idToken, dfchtlc->amount, htlcTxidAddr, order->ownerAddress));

        // refund makerDeposit
        Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, CScript(), order->ownerAddress));

        // makerIncentive
        Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee * 25 / 100, CScript(), order->ownerAddress));

        // maker bonus only on fair dBTC/BTC (1:1) trades for now
        DCT_ID BTC = FindTokenByPartialSymbolName(CICXOrder::TOKEN_BTC);
        if (order->idToken == BTC && order->orderPrice == COIN) {
            if ((IsTestNetwork() && height >= 1250000) ||
                Params().NetworkIDString() == CBaseChainParams::REGTEST) {
                Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee * 50 / 100, CScript(), order->ownerAddress));
            } else {
                Require(TransferTokenBalance(BTC, offer->takerFee * 50 / 100, CScript(), order->ownerAddress));
            }
        }

        if (order->orderType == CICXOrder::TYPE_INTERNAL)
            order->amountToFill -= dfchtlc->amount;
        else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
            order->amountToFill -= DivideAmounts(dfchtlc->amount, order->orderPrice);

        // Order fulfilled, close order.
        if (order->amountToFill == 0) {
            order->closeTx     = claimdfchtlc.creationTx;
            order->closeHeight = height;
            Require(mnview.ICXCloseOrderTx(*order, CICXOrder::STATUS_FILLED));
        }

        Require(mnview.ICXClaimDFCHTLC(claimdfchtlc, offer->creationTx, *order));
        // Close offer
        Require(mnview.ICXCloseMakeOfferTx(*offer, CICXMakeOffer::STATUS_CLOSED));

        Require(mnview.ICXCloseDFCHTLC(*dfchtlc, CICXSubmitDFCHTLC::STATUS_CLAIMED));

        if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
            if (exthtlc)
                return mnview.ICXCloseEXTHTLC(*exthtlc, CICXSubmitEXTHTLC::STATUS_CLOSED);
            else
                return (Res::Ok());
        } else
            return mnview.ICXCloseEXTHTLC(*exthtlc, CICXSubmitEXTHTLC::STATUS_CLOSED);
    }

    Res operator()(const CICXCloseOrderMessage &obj) const {
        Require(CheckCustomTx());

        CICXCloseOrderImplemetation closeorder;
        static_cast<CICXCloseOrder &>(closeorder) = obj;

        closeorder.creationTx     = tx.GetHash();
        closeorder.creationHeight = height;

        auto order = mnview.GetICXOrderByCreationTx(closeorder.orderTx);
        Require(order, "order with creation tx %s does not exists!", closeorder.orderTx.GetHex());

        Require(order->closeTx.IsNull(), "order with creation tx %s is already closed!", closeorder.orderTx.GetHex());
        Require(mnview.HasICXOrderOpen(order->idToken, order->creationTx),
                "order with creation tx %s is already closed!",
                closeorder.orderTx.GetHex());

        // check auth
        Require(HasAuth(order->ownerAddress), "tx must have at least one input from order owner");

        order->closeTx     = closeorder.creationTx;
        order->closeHeight = closeorder.creationHeight;

        if (order->orderType == CICXOrder::TYPE_INTERNAL && order->amountToFill > 0) {
            // subtract the balance from txidAddr and return to owner
            CScript txidAddr(order->creationTx.begin(), order->creationTx.end());
            CalculateOwnerRewards(order->ownerAddress);
            Require(TransferTokenBalance(order->idToken, order->amountToFill, txidAddr, order->ownerAddress));
        }

        Require(mnview.ICXCloseOrder(closeorder));
        return mnview.ICXCloseOrderTx(*order, CICXOrder::STATUS_CLOSED);
    }

    Res operator()(const CICXCloseOfferMessage &obj) const {
        Require(CheckCustomTx());

        CICXCloseOfferImplemetation closeoffer;
        static_cast<CICXCloseOffer &>(closeoffer) = obj;

        closeoffer.creationTx     = tx.GetHash();
        closeoffer.creationHeight = height;

        auto offer = mnview.GetICXMakeOfferByCreationTx(closeoffer.offerTx);
        Require(offer, "offer with creation tx %s does not exists!", closeoffer.offerTx.GetHex());

        Require(offer->closeTx.IsNull(), "offer with creation tx %s is already closed!", closeoffer.offerTx.GetHex());
        Require(mnview.HasICXMakeOfferOpen(offer->orderTx, offer->creationTx),
                "offer with creation tx %s does not exists!",
                closeoffer.offerTx.GetHex());

        auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
        Require(order, "order with creation tx %s does not exists!", offer->orderTx.GetHex());

        // check auth
        Require(HasAuth(offer->ownerAddress), "tx must have at least one input from offer owner");

        offer->closeTx     = closeoffer.creationTx;
        offer->closeHeight = closeoffer.creationHeight;

        bool isPreEunosPaya = static_cast<int>(height) < consensus.EunosPayaHeight;

        if (order->orderType == CICXOrder::TYPE_INTERNAL &&
            !mnview.ExistedICXSubmitDFCHTLC(offer->creationTx, isPreEunosPaya)) {
            // subtract takerFee from txidAddr and return to owner
            CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
            CalculateOwnerRewards(offer->ownerAddress);
            Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, txidAddr, offer->ownerAddress));
        } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
            // subtract the balance from txidAddr and return to owner
            CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
            CalculateOwnerRewards(offer->ownerAddress);
            if (isPreEunosPaya)
                Require(TransferTokenBalance(order->idToken, offer->amount, txidAddr, offer->ownerAddress));

            if (!mnview.ExistedICXSubmitEXTHTLC(offer->creationTx, isPreEunosPaya))
                Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, txidAddr, offer->ownerAddress));
        }

        Require(mnview.ICXCloseOffer(closeoffer));
        return mnview.ICXCloseMakeOfferTx(*offer, CICXMakeOffer::STATUS_CLOSED);
    }

    Res operator()(const CLoanSetCollateralTokenMessage &obj) const {
        Require(CheckCustomTx());

        Require(HasFoundationAuth(), "tx not from foundation member!");

        if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) && IsTokensMigratedToGovVar()) {
            const auto &tokenId = obj.idToken.v;

            auto attributes  = mnview.GetAttributes();
            attributes->time = time;

            CDataStructureV0 collateralEnabled{AttributeTypes::Token, tokenId, TokenKeys::LoanCollateralEnabled};
            CDataStructureV0 collateralFactor{AttributeTypes::Token, tokenId, TokenKeys::LoanCollateralFactor};
            CDataStructureV0 pairKey{AttributeTypes::Token, tokenId, TokenKeys::FixedIntervalPriceId};

            auto gv = GovVariable::Create("ATTRIBUTES");
            Require(gv, "Failed to create ATTRIBUTES Governance variable");

            auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
            Require(var, "Failed to convert ATTRIBUTES Governance variable");

            var->SetValue(collateralEnabled, true);
            var->SetValue(collateralFactor, obj.factor);
            var->SetValue(pairKey, obj.fixedIntervalPriceId);

            Require(attributes->Import(var->Export()));
            Require(attributes->Validate(mnview));
            Require(attributes->Apply(mnview, height));

            return mnview.SetVariable(*attributes);
        }

        CLoanSetCollateralTokenImplementation collToken;
        static_cast<CLoanSetCollateralToken &>(collToken) = obj;

        collToken.creationTx     = tx.GetHash();
        collToken.creationHeight = height;

        auto token = mnview.GetToken(collToken.idToken);
        Require(token, "token %s does not exist!", collToken.idToken.ToString());

        if (!collToken.activateAfterBlock)
            collToken.activateAfterBlock = height;

        Require(collToken.activateAfterBlock >= height, "activateAfterBlock cannot be less than current height!");

        Require(OraclePriceFeed(mnview, collToken.fixedIntervalPriceId),
                "Price feed %s/%s does not belong to any oracle",
                collToken.fixedIntervalPriceId.first,
                collToken.fixedIntervalPriceId.second);

        CFixedIntervalPrice fixedIntervalPrice;
        fixedIntervalPrice.priceFeedId = collToken.fixedIntervalPriceId;

        auto price = GetAggregatePrice(
            mnview, collToken.fixedIntervalPriceId.first, collToken.fixedIntervalPriceId.second, time);
        Require(price, price.msg);

        fixedIntervalPrice.priceRecord[1] = price;
        fixedIntervalPrice.timestamp      = time;

        auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
        Require(resSetFixedPrice, resSetFixedPrice.msg);

        return mnview.CreateLoanCollateralToken(collToken);
    }

    Res operator()(const CLoanSetLoanTokenMessage &obj) const {
        Require(CheckCustomTx());

        Require(HasFoundationAuth(), "tx not from foundation member!");

        if (height < static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
            Require(obj.interest >= 0, "interest rate cannot be less than 0!");
        }

        CTokenImplementation token;
        token.symbol         = trim_ws(obj.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        token.name           = trim_ws(obj.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.creationTx     = tx.GetHash();
        token.creationHeight = height;
        token.flags          = obj.mintable ? static_cast<uint8_t>(CToken::TokenFlags::Default)
                                            : static_cast<uint8_t>(CToken::TokenFlags::Tradeable);
        token.flags |=
            static_cast<uint8_t>(CToken::TokenFlags::LoanToken) | static_cast<uint8_t>(CToken::TokenFlags::DAT);

        auto tokenId = mnview.CreateToken(token);
        Require(tokenId);

        if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) && IsTokensMigratedToGovVar()) {
            const auto &id = tokenId.val->v;

            auto attributes  = mnview.GetAttributes();
            attributes->time = time;

            CDataStructureV0 mintEnabled{AttributeTypes::Token, id, TokenKeys::LoanMintingEnabled};
            CDataStructureV0 mintInterest{AttributeTypes::Token, id, TokenKeys::LoanMintingInterest};
            CDataStructureV0 pairKey{AttributeTypes::Token, id, TokenKeys::FixedIntervalPriceId};

            auto gv = GovVariable::Create("ATTRIBUTES");
            Require(gv, "Failed to create ATTRIBUTES Governance variable");

            auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
            Require(var, "Failed to convert ATTRIBUTES Governance variable");

            var->SetValue(mintEnabled, obj.mintable);
            var->SetValue(mintInterest, obj.interest);
            var->SetValue(pairKey, obj.fixedIntervalPriceId);

            Require(attributes->Import(var->Export()));
            Require(attributes->Validate(mnview));
            Require(attributes->Apply(mnview, height));
            return mnview.SetVariable(*attributes);
        }

        CLoanSetLoanTokenImplementation loanToken;
        static_cast<CLoanSetLoanToken &>(loanToken) = obj;

        loanToken.creationTx     = tx.GetHash();
        loanToken.creationHeight = height;

        auto nextPrice =
            GetAggregatePrice(mnview, obj.fixedIntervalPriceId.first, obj.fixedIntervalPriceId.second, time);
        Require(nextPrice, nextPrice.msg);

        Require(OraclePriceFeed(mnview, obj.fixedIntervalPriceId),
                "Price feed %s/%s does not belong to any oracle",
                obj.fixedIntervalPriceId.first,
                obj.fixedIntervalPriceId.second);

        CFixedIntervalPrice fixedIntervalPrice;
        fixedIntervalPrice.priceFeedId    = loanToken.fixedIntervalPriceId;
        fixedIntervalPrice.priceRecord[1] = nextPrice;
        fixedIntervalPrice.timestamp      = time;

        auto resSetFixedPrice = mnview.SetFixedIntervalPrice(fixedIntervalPrice);
        Require(resSetFixedPrice, resSetFixedPrice.msg);

        return mnview.SetLoanToken(loanToken, *(tokenId.val));
    }

    Res operator()(const CLoanUpdateLoanTokenMessage &obj) const {
        Require(CheckCustomTx());

        Require(HasFoundationAuth(), "tx not from foundation member!");

        if (height < static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
            Require(obj.interest >= 0, "interest rate cannot be less than 0!");
        }

        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        Require(pair, "Loan token (%s) does not exist!", obj.tokenTx.GetHex());

        auto loanToken =
            (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) && IsTokensMigratedToGovVar())
                ? mnview.GetLoanTokenByID(pair->first)
                : mnview.GetLoanToken(obj.tokenTx);

        Require(loanToken, "Loan token (%s) does not exist!", obj.tokenTx.GetHex());

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

        Require(mnview.UpdateToken(pair->second));

        if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight) && IsTokensMigratedToGovVar()) {
            const auto &id = pair->first.v;

            auto attributes  = mnview.GetAttributes();
            attributes->time = time;

            CDataStructureV0 mintEnabled{AttributeTypes::Token, id, TokenKeys::LoanMintingEnabled};
            CDataStructureV0 mintInterest{AttributeTypes::Token, id, TokenKeys::LoanMintingInterest};
            CDataStructureV0 pairKey{AttributeTypes::Token, id, TokenKeys::FixedIntervalPriceId};

            auto gv = GovVariable::Create("ATTRIBUTES");
            Require(gv, "Failed to create ATTRIBUTES Governance variable");

            auto var = std::dynamic_pointer_cast<ATTRIBUTES>(gv);
            Require(var, "Failed to convert ATTRIBUTES Governance variable");

            var->SetValue(mintEnabled, obj.mintable);
            var->SetValue(mintInterest, obj.interest);
            var->SetValue(pairKey, obj.fixedIntervalPriceId);

            Require(attributes->Import(var->Export()));
            Require(attributes->Validate(mnview));
            Require(attributes->Apply(mnview, height));
            return mnview.SetVariable(*attributes);
        }

        if (obj.fixedIntervalPriceId != loanToken->fixedIntervalPriceId) {
            Require(OraclePriceFeed(mnview, obj.fixedIntervalPriceId),
                    "Price feed %s/%s does not belong to any oracle",
                    obj.fixedIntervalPriceId.first,
                    obj.fixedIntervalPriceId.second);

            loanToken->fixedIntervalPriceId = obj.fixedIntervalPriceId;
        }

        return mnview.UpdateLoanToken(*loanToken, pair->first);
    }

    Res operator()(const CLoanSchemeMessage &obj) const {
        Require(CheckCustomTx());

        Require(HasFoundationAuth(), "tx not from foundation member!");

        Require(obj.ratio >= 100, "minimum collateral ratio cannot be less than 100");

        Require(obj.rate >= 1000000, "interest rate cannot be less than 0.01");

        Require(!obj.identifier.empty() && obj.identifier.length() <= 8,
                "id cannot be empty or more than 8 chars long");

        // Look for loan scheme which already has matching rate and ratio
        bool duplicateLoan = false;
        std::string duplicateID;
        mnview.ForEachLoanScheme([&](const std::string &key, const CLoanSchemeData &data) {
            // Duplicate scheme already exists
            if (data.ratio == obj.ratio && data.rate == obj.rate) {
                duplicateLoan = true;
                duplicateID   = key;
                return false;
            }
            return true;
        });

        Require(!duplicateLoan, "Loan scheme %s with same interestrate and mincolratio already exists", duplicateID);

        // Look for delayed loan scheme which already has matching rate and ratio
        std::pair<std::string, uint64_t> duplicateKey;
        mnview.ForEachDelayedLoanScheme(
            [&](const std::pair<std::string, uint64_t> &key, const CLoanSchemeMessage &data) {
                // Duplicate delayed loan scheme
                if (data.ratio == obj.ratio && data.rate == obj.rate) {
                    duplicateLoan = true;
                    duplicateKey  = key;
                    return false;
                }
                return true;
            });

        Require(!duplicateLoan,
                "Loan scheme %s with same interestrate and mincolratio pending on block %d",
                duplicateKey.first,
                duplicateKey.second);

        // New loan scheme, no duplicate expected.
        if (mnview.GetLoanScheme(obj.identifier))
            Require(obj.updateHeight, "Loan scheme already exist with id %s", obj.identifier);
        else
            Require(!obj.updateHeight, "Cannot find existing loan scheme with id %s", obj.identifier);

        // Update set, not max uint64_t which indicates immediate update and not updated on this block.
        if (obj.updateHeight && obj.updateHeight != std::numeric_limits<uint64_t>::max() &&
            obj.updateHeight != height) {
            Require(obj.updateHeight >= height, "Update height below current block height, set future height");
            return mnview.StoreDelayedLoanScheme(obj);
        }

        // If no default yet exist set this one as default.
        if (!mnview.GetDefaultLoanScheme()) {
            mnview.StoreDefaultLoanScheme(obj.identifier);
        }

        return mnview.StoreLoanScheme(obj);
    }

    Res operator()(const CDefaultLoanSchemeMessage &obj) const {
        Require(CheckCustomTx());
        Require(HasFoundationAuth(), "tx not from foundation member!");

        Require(!obj.identifier.empty() && obj.identifier.length() <= 8,
                "id cannot be empty or more than 8 chars long");
        Require(mnview.GetLoanScheme(obj.identifier), "Cannot find existing loan scheme with id %s", obj.identifier);

        if (auto currentID = mnview.GetDefaultLoanScheme())
            Require(*currentID != obj.identifier, "Loan scheme with id %s is already set as default", obj.identifier);

        const auto height = mnview.GetDestroyLoanScheme(obj.identifier);
        Require(!height, "Cannot set %s as default, set to destroyed on block %d", obj.identifier, *height);
        return mnview.StoreDefaultLoanScheme(obj.identifier);
    }

    Res operator()(const CDestroyLoanSchemeMessage &obj) const {
        Require(CheckCustomTx());

        Require(HasFoundationAuth(), "tx not from foundation member!");

        Require(!obj.identifier.empty() && obj.identifier.length() <= 8,
                "id cannot be empty or more than 8 chars long");
        Require(mnview.GetLoanScheme(obj.identifier), "Cannot find existing loan scheme with id %s", obj.identifier);

        const auto currentID = mnview.GetDefaultLoanScheme();
        Require(currentID && *currentID != obj.identifier, "Cannot destroy default loan scheme, set new default first");

        // Update set and not updated on this block.
        if (obj.destroyHeight && obj.destroyHeight != height) {
            Require(obj.destroyHeight >= height, "Destruction height below current block height, set future height");
            return mnview.StoreDelayedDestroyScheme(obj);
        }

        mnview.ForEachVault([&](const CVaultId &vaultId, CVaultData vault) {
            if (vault.schemeId == obj.identifier) {
                vault.schemeId = *mnview.GetDefaultLoanScheme();
                mnview.StoreVault(vaultId, vault);
            }
            return true;
        });

        return mnview.EraseLoanScheme(obj.identifier);
    }

    Res operator()(const CVaultMessage &obj) const {
        auto vaultCreationFee = consensus.vaultCreationFee;
        Require(tx.vout[0].nValue == vaultCreationFee && tx.vout[0].nTokenId == DCT_ID{0},
                "Malformed tx vouts, creation vault fee is %s DFI",
                GetDecimaleString(vaultCreationFee));

        CVaultData vault{};
        static_cast<CVaultMessage &>(vault) = obj;

        // set loan scheme to default if non provided
        if (obj.schemeId.empty()) {
            auto defaultScheme = mnview.GetDefaultLoanScheme();
            Require(defaultScheme, "There is no default loan scheme");
            vault.schemeId = *defaultScheme;
        }

        // loan scheme exists
        Require(mnview.GetLoanScheme(vault.schemeId), "Cannot find existing loan scheme with id %s", vault.schemeId);

        // check loan scheme is not to be destroyed
        auto height = mnview.GetDestroyLoanScheme(obj.schemeId);
        Require(!height, "Cannot set %s as loan scheme, set to be destroyed on block %d", obj.schemeId, *height);

        auto vaultId = tx.GetHash();
        return mnview.StoreVault(vaultId, vault);
    }

    Res operator()(const CCloseVaultMessage &obj) const {
        Require(CheckCustomTx());

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        Require(!vault->isUnderLiquidation, "Cannot close vault under liquidation");

        // owner auth
        Require(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

        if (const auto loans = mnview.GetLoanTokens(obj.vaultId)) {
            for (const auto &[tokenId, amount] : loans->balances) {
                const auto rate = mnview.GetInterestRate(obj.vaultId, tokenId, height);
                Require(rate, "Cannot get interest rate for this token (%d)", tokenId.v);

                const auto totalInterest = TotalInterest(*rate, height);

                Require(amount + totalInterest <= 0, "Vault <%s> has loans", obj.vaultId.GetHex());

                // If there is an amount negated by interested remove it from loan tokens.
                if (amount > 0) {
                    mnview.SubLoanToken(obj.vaultId, {tokenId, amount});
                }

                if (totalInterest < 0) {
                    TrackNegativeInterest(
                        mnview, {tokenId, amount > std::abs(totalInterest) ? std::abs(totalInterest) : amount});
                }
            }
        }

        CalculateOwnerRewards(obj.to);
        if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId))
            for (const auto &col : collaterals->balances)
                Require(mnview.AddBalance(obj.to, {col.first, col.second}));

        // delete all interest to vault
        Require(mnview.EraseInterest(obj.vaultId, height));

        // return half fee, the rest is burned at creation
        auto feeBack = consensus.vaultCreationFee / 2;
        Require(mnview.AddBalance(obj.to, {DCT_ID{0}, feeBack}));
        return mnview.EraseVault(obj.vaultId);
    }

    Res operator()(const CUpdateVaultMessage &obj) const {
        Require(CheckCustomTx());

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        Require(!vault->isUnderLiquidation, "Cannot update vault under liquidation");

        // owner auth
        Require(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

        // loan scheme exists
        const auto scheme = mnview.GetLoanScheme(obj.schemeId);
        Require(scheme, "Cannot find existing loan scheme with id %s", obj.schemeId);

        // loan scheme is not set to be destroyed
        auto destroyHeight = mnview.GetDestroyLoanScheme(obj.schemeId);
        Require(!destroyHeight,
                "Cannot set %s as loan scheme, set to be destroyed on block %d",
                obj.schemeId,
                *destroyHeight);

        Require(IsVaultPriceValid(mnview, obj.vaultId, height),
                "Cannot update vault while any of the asset's price is invalid");

        // don't allow scheme change when vault is going to be in liquidation
        if (vault->schemeId != obj.schemeId) {
            if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
                for (int i = 0; i < 2; i++) {
                    bool useNextPrice = i > 0, requireLivePrice = true;
                    auto collateralsLoans = mnview.GetLoanCollaterals(
                        obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
                    Require(collateralsLoans);

                    Require(collateralsLoans.val->ratio() >= scheme->ratio,
                            "Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                            collateralsLoans.val->ratio(),
                            scheme->ratio);
                }
            }
            if (height >= static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
                if (const auto loanTokens = mnview.GetLoanTokens(obj.vaultId)) {
                    for (const auto &[tokenId, tokenAmount] : loanTokens->balances) {
                        const auto loanToken = mnview.GetLoanTokenByID(tokenId);
                        assert(loanToken);
                        Require(mnview.IncreaseInterest(
                            height, obj.vaultId, obj.schemeId, tokenId, loanToken->interest, 0));
                    }
                }
            }
        }

        vault->schemeId     = obj.schemeId;
        vault->ownerAddress = obj.ownerAddress;
        return mnview.UpdateVault(obj.vaultId, *vault);
    }

    Res CollateralPctCheck(const bool hasDUSDLoans,
                           const CCollateralLoans &collateralsLoans,
                           const uint32_t ratio) const {
        std::optional<std::pair<DCT_ID, std::optional<CTokensView::CTokenImpl> > > tokenDUSD;
        if (static_cast<int>(height) >= consensus.FortCanningRoadHeight) {
            tokenDUSD = mnview.GetToken("DUSD");
        }

        // Calculate DFI and DUSD value separately
        CAmount totalCollateralsDUSD = 0;
        CAmount totalCollateralsDFI  = 0;
        CAmount factorDUSD           = 0;
        CAmount factorDFI            = 0;

        for (auto &col : collateralsLoans.collaterals) {
            auto token = mnview.GetCollateralTokenFromAttributes(col.nTokenId);

            if (col.nTokenId == DCT_ID{0}) {
                totalCollateralsDFI += col.nValue;
                factorDFI = token->factor;
            }

            if (tokenDUSD && col.nTokenId == tokenDUSD->first) {
                totalCollateralsDUSD += col.nValue;
                factorDUSD = token->factor;
            }
        }

        // Height checks
        auto isPostFCH = static_cast<int>(height) >= consensus.FortCanningHillHeight;
        auto isPreFCH  = static_cast<int>(height) < consensus.FortCanningHillHeight;
        auto isPostFCE = static_cast<int>(height) >= consensus.FortCanningEpilogueHeight;
        auto isPostFCR = static_cast<int>(height) >= consensus.FortCanningRoadHeight;
        auto isPostGC  = static_cast<int>(height) >= consensus.GrandCentralHeight;

        if (isPostGC) {
            totalCollateralsDUSD = MultiplyAmounts(totalCollateralsDUSD, factorDUSD);
            totalCollateralsDFI  = MultiplyAmounts(totalCollateralsDFI, factorDFI);
        }
        auto totalCollaterals = totalCollateralsDUSD + totalCollateralsDFI;

        // Condition checks
        auto isDFILessThanHalfOfTotalCollateral =
            arith_uint256(totalCollateralsDFI) < arith_uint256(collateralsLoans.totalCollaterals) / 2;
        auto isDFIAndDUSDLessThanHalfOfRequiredCollateral =
            arith_uint256(totalCollaterals) * 100 < (arith_uint256(collateralsLoans.totalLoans) * ratio / 2);
        auto isDFILessThanHalfOfRequiredCollateral =
            arith_uint256(totalCollateralsDFI) * 100 < (arith_uint256(collateralsLoans.totalLoans) * ratio / 2);

        if (isPostFCE) {
            if (hasDUSDLoans) {
                if (isDFILessThanHalfOfRequiredCollateral)
                    return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT));
            } else {
                if (isDFIAndDUSDLessThanHalfOfRequiredCollateral)
                    return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT));
            }
            return Res::Ok();
        }

        if (isPostFCR)
            return isDFIAndDUSDLessThanHalfOfRequiredCollateral
                       ? Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT))
                       : Res::Ok();

        if (isPostFCH)
            return isDFILessThanHalfOfRequiredCollateral ? Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT))
                                                         : Res::Ok();

        if (isPreFCH && isDFILessThanHalfOfTotalCollateral)
            return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT));

        return Res::Ok();
    }

    Res operator()(const CDepositToVaultMessage &obj) const {
        Require(CheckCustomTx());

        // owner auth
        Require(HasAuth(obj.from), "tx must have at least one input from token owner");

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        Require(!vault->isUnderLiquidation, "Cannot deposit to vault under liquidation");

        // If collateral token exist make sure it is enabled.
        if (mnview.GetCollateralTokenFromAttributes(obj.amount.nTokenId)) {
            CDataStructureV0 collateralKey{
                AttributeTypes::Token, obj.amount.nTokenId.v, TokenKeys::LoanCollateralEnabled};
            if (const auto attributes = mnview.GetAttributes()) {
                Require(attributes->GetValue(collateralKey, false),
                        "Collateral token (%d) is disabled",
                        obj.amount.nTokenId.v);
            }
        }

        // check balance
        CalculateOwnerRewards(obj.from);
        Require(mnview.SubBalance(obj.from, obj.amount), [&](const std::string &msg) {
            return strprintf("Insufficient funds: can't subtract balance of %s: %s\n", ScriptToString(obj.from), msg);
        });

        Require(mnview.AddVaultCollateral(obj.vaultId, obj.amount));

        bool useNextPrice = false, requireLivePrice = false;
        auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);

        auto collateralsLoans =
            mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
        Require(collateralsLoans);

        auto scheme = mnview.GetLoanScheme(vault->schemeId);
        Require(collateralsLoans.val->ratio() >= scheme->ratio,
                "Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                collateralsLoans.val->ratio(),
                scheme->ratio);

        return Res::Ok();
    }

    Res operator()(const CWithdrawFromVaultMessage &obj) const {
        Require(CheckCustomTx());

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        Require(!vault->isUnderLiquidation, "Cannot withdraw from vault under liquidation");

        // owner auth
        Require(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

        Require(IsVaultPriceValid(mnview, obj.vaultId, height),
                "Cannot withdraw from vault while any of the asset's price is invalid");

        Require(mnview.SubVaultCollateral(obj.vaultId, obj.amount));

        auto hasDUSDLoans = false;

        std::optional<std::pair<DCT_ID, std::optional<CTokensView::CTokenImpl> > > tokenDUSD;
        if (static_cast<int>(height) >= consensus.FortCanningRoadHeight) {
            tokenDUSD = mnview.GetToken("DUSD");
        }

        if (const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId)) {
            // Update negative interest in vault
            for (const auto &[tokenId, currentLoanAmount] : loanAmounts->balances) {
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

                const auto subAmount =
                    currentLoanAmount > std::abs(totalInterest) ? std::abs(totalInterest) : currentLoanAmount;

                if (const auto token = mnview.GetToken("DUSD"); token && tokenId == token->first) {
                    TrackDUSDSub(mnview, {tokenId, subAmount});
                }

                Require(mnview.SubLoanToken(obj.vaultId, CTokenAmount{tokenId, subAmount}));

                TrackNegativeInterest(mnview, {tokenId, subAmount});

                mnview.ResetInterest(height, obj.vaultId, vault->schemeId, tokenId);
            }

            if (auto collaterals = mnview.GetVaultCollaterals(obj.vaultId)) {
                const auto scheme = mnview.GetLoanScheme(vault->schemeId);
                for (int i = 0; i < 2; i++) {
                    // check collaterals for active and next price
                    bool useNextPrice = i > 0, requireLivePrice = true;
                    auto collateralsLoans = mnview.GetLoanCollaterals(
                        obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
                    Require(collateralsLoans);

                    Require(collateralsLoans.val->ratio() >= scheme->ratio,
                            "Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                            collateralsLoans.val->ratio(),
                            scheme->ratio);

                    Require(CollateralPctCheck(hasDUSDLoans, collateralsLoans, scheme->ratio));
                }
            } else {
                return Res::Err("Cannot withdraw all collaterals as there are still active loans in this vault");
            }
        }

        if (height >= static_cast<uint32_t>(consensus.NextNetworkUpgradeHeight)) {
            mnview.CalculateOwnerRewards(obj.to, height);
        }

        return mnview.AddBalance(obj.to, obj.amount);
    }

    Res operator()(const CPaybackWithCollateralMessage &obj) const {
        Require(CheckCustomTx());

        // vault exists
        const auto vault = mnview.GetVault(obj.vaultId);
        Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        Require(!vault->isUnderLiquidation, "Cannot payback vault with collateral while vault's under liquidation");

        // owner auth
        Require(HasAuth(vault->ownerAddress), "tx must have at least one input from token owner");

        return PaybackWithCollateral(mnview, *vault, obj.vaultId, height, time);
    }

    Res operator()(const CLoanTakeLoanMessage &obj) const {
        Require(CheckCustomTx());

        const auto vault = mnview.GetVault(obj.vaultId);
        Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

        Require(!vault->isUnderLiquidation, "Cannot take loan on vault under liquidation");

        // vault owner auth
        Require(HasAuth(vault->ownerAddress), "tx must have at least one input from vault owner");

        Require(IsVaultPriceValid(mnview, obj.vaultId, height),
                "Cannot take loan while any of the asset's price in the vault is not live");

        auto collaterals = mnview.GetVaultCollaterals(obj.vaultId);
        Require(collaterals, "Vault with id %s has no collaterals", obj.vaultId.GetHex());

        const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);

        auto hasDUSDLoans = false;

        std::optional<std::pair<DCT_ID, std::optional<CTokensView::CTokenImpl> > > tokenDUSD;
        if (static_cast<int>(height) >= consensus.FortCanningRoadHeight) {
            tokenDUSD = mnview.GetToken("DUSD");
        }

        uint64_t totalLoansActivePrice = 0, totalLoansNextPrice = 0;
        for (const auto &[tokenId, tokenAmount] : obj.amounts.balances) {
            if (height >= static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
                Require(tokenAmount > 0, "Valid loan amount required (input: %d@%d)", tokenAmount, tokenId.v);
            }

            auto loanToken = mnview.GetLoanTokenByID(tokenId);
            Require(loanToken, "Loan token with id (%s) does not exist!", tokenId.ToString());

            Require(loanToken->mintable,
                    "Loan cannot be taken on token with id (%s) as \"mintable\" is currently false",
                    tokenId.ToString());
            if (tokenDUSD && tokenId == tokenDUSD->first) {
                hasDUSDLoans = true;
            }

            // Calculate interest
            CAmount currentLoanAmount{};
            bool resetInterestToHeight{};
            auto loanAmountChange = tokenAmount;

            if (loanAmounts && loanAmounts->balances.count(tokenId)) {
                currentLoanAmount = loanAmounts->balances.at(tokenId);
                const auto rate   = mnview.GetInterestRate(obj.vaultId, tokenId, height);
                assert(rate);
                const auto totalInterest = TotalInterest(*rate, height);

                if (totalInterest < 0) {
                    loanAmountChange      = currentLoanAmount > std::abs(totalInterest)
                                                ?
                                                // Interest to decrease smaller than overall existing loan amount.
                                           // So reduce interest from the borrowing principal. If this is negative,
                                           // we'll reduce from principal.
                                           tokenAmount + totalInterest
                                                :
                                                // Interest to decrease is larger than old loan amount.
                                           // We reduce from the borrowing principal. If this is negative,
                                           // we'll reduce from principal.
                                           tokenAmount - currentLoanAmount;
                    resetInterestToHeight = true;
                    TrackNegativeInterest(
                        mnview,
                        {tokenId,
                         currentLoanAmount > std::abs(totalInterest) ? std::abs(totalInterest) : currentLoanAmount});
                }
            }

            if (loanAmountChange > 0) {
                if (const auto token = mnview.GetToken("DUSD"); token && token->first == tokenId) {
                    TrackDUSDAdd(mnview, {tokenId, loanAmountChange});
                }

                Require(mnview.AddLoanToken(obj.vaultId, CTokenAmount{tokenId, loanAmountChange}));
            } else {
                const auto subAmount =
                    currentLoanAmount > std::abs(loanAmountChange) ? std::abs(loanAmountChange) : currentLoanAmount;

                if (const auto token = mnview.GetToken("DUSD"); token && token->first == tokenId) {
                    TrackDUSDSub(mnview, {tokenId, subAmount});
                }

                Require(mnview.SubLoanToken(obj.vaultId, CTokenAmount{tokenId, subAmount}));
            }

            if (resetInterestToHeight) {
                mnview.ResetInterest(height, obj.vaultId, vault->schemeId, tokenId);
            } else {
                Require(mnview.IncreaseInterest(
                    height, obj.vaultId, vault->schemeId, tokenId, loanToken->interest, loanAmountChange));
            }

            const auto tokenCurrency = loanToken->fixedIntervalPriceId;

            auto priceFeed = mnview.GetFixedIntervalPrice(tokenCurrency);
            Require(priceFeed, priceFeed.msg);

            Require(priceFeed.val->isLive(mnview.GetPriceDeviation()),
                    "No live fixed prices for %s/%s",
                    tokenCurrency.first,
                    tokenCurrency.second);

            for (int i = 0; i < 2; i++) {
                // check active and next price
                auto price  = priceFeed.val->priceRecord[int(i > 0)];
                auto amount = MultiplyAmounts(price, tokenAmount);
                if (price > COIN) {
                    Require(amount >= tokenAmount,
                            "Value/price too high (%s/%s)",
                            GetDecimaleString(tokenAmount),
                            GetDecimaleString(price));
                }
                auto &totalLoans = i > 0 ? totalLoansNextPrice : totalLoansActivePrice;
                auto prevLoans   = totalLoans;
                totalLoans += amount;
                Require(prevLoans <= totalLoans, "Exceed maximum loans");
            }

            Require(mnview.AddMintedTokens(tokenId, tokenAmount));

            const auto &address = !obj.to.empty() ? obj.to : vault->ownerAddress;
            CalculateOwnerRewards(address);
            Require(mnview.AddBalance(address, CTokenAmount{tokenId, tokenAmount}));
        }

        auto scheme = mnview.GetLoanScheme(vault->schemeId);
        for (int i = 0; i < 2; i++) {
            // check ratio against current and active price
            bool useNextPrice = i > 0, requireLivePrice = true;
            auto collateralsLoans =
                mnview.GetLoanCollaterals(obj.vaultId, *collaterals, height, time, useNextPrice, requireLivePrice);
            Require(collateralsLoans);

            Require(collateralsLoans.val->ratio() >= scheme->ratio,
                    "Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                    collateralsLoans.val->ratio(),
                    scheme->ratio);

            Require(CollateralPctCheck(hasDUSDLoans, collateralsLoans, scheme->ratio));
        }
        return Res::Ok();
    }

    Res operator()(const CLoanPaybackLoanMessage &obj) const {
        std::map<DCT_ID, CBalances> loans;
        for (auto &balance : obj.amounts.balances) {
            auto id     = balance.first;
            auto amount = balance.second;

            CBalances *loan;
            if (id == DCT_ID{0}) {
                auto tokenDUSD = mnview.GetToken("DUSD");
                if (!tokenDUSD) return DeFiErrors::LoanTokenNotFoundForName("DUSD");
                loan = &loans[tokenDUSD->first];
            } else
                loan = &loans[id];

            loan->Add({id, amount});
        }
        return (*this)(CLoanPaybackLoanV2Message{obj.vaultId, obj.from, loans});
    }

    Res operator()(const CLoanPaybackLoanV2Message &obj) const {
        auto res = CheckCustomTx();
        if (!res)
            return res;

        const auto vault = mnview.GetVault(obj.vaultId);
        if (!vault) return DeFiErrors::VaultInvalid(obj.vaultId);

        if (vault->isUnderLiquidation) return DeFiErrors::LoanNoPaybackOnLiquidation();

        if (!mnview.GetVaultCollaterals(obj.vaultId)) return DeFiErrors::VaultNoCollateral(obj.vaultId.GetHex());

        if (!HasAuth(obj.from)) return DeFiErrors::TXMissingInput();

        if (static_cast<int>(height) < consensus.FortCanningRoadHeight) {
            if (!IsVaultPriceValid(mnview, obj.vaultId, height)) return DeFiErrors::LoanAssetPriceInvalid();
        }

        // Handle payback with collateral special case
        if (static_cast<int>(height) >= consensus.FortCanningEpilogueHeight &&
            IsPaybackWithCollateral(mnview, obj.loans)) {
            return PaybackWithCollateral(mnview, *vault, obj.vaultId, height, time);
        }

        auto shouldSetVariable = false;
        auto attributes        = mnview.GetAttributes();
        assert(attributes);

        for (const auto &[loanTokenId, paybackAmounts] : obj.loans) {
            const auto loanToken = mnview.GetLoanTokenByID(loanTokenId);
            if (!loanToken) return DeFiErrors::LoanTokenIdInvalid(loanTokenId);

            for (const auto &kv : paybackAmounts.balances) {
                const auto &paybackTokenId = kv.first;
                auto paybackAmount         = kv.second;

                if (height >= static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
                    if (paybackAmount <= 0) return DeFiErrors::LoanPaymentAmountInvalid(paybackAmount, paybackTokenId.v);
                }

                CAmount paybackUsdPrice{0}, loanUsdPrice{0}, penaltyPct{COIN};

                auto paybackToken = mnview.GetToken(paybackTokenId);
                if (!paybackToken) return DeFiErrors::TokenIdInvalid(paybackTokenId);

                if (loanTokenId != paybackTokenId) {
                    if (!IsVaultPriceValid(mnview, obj.vaultId, height)) return DeFiErrors::LoanAssetPriceInvalid();

                    // search in token to token
                    if (paybackTokenId != DCT_ID{0}) {
                        CDataStructureV0 activeKey{
                            AttributeTypes::Token, loanTokenId.v, TokenKeys::LoanPayback, paybackTokenId.v};
                        if (!attributes->GetValue(activeKey, false)) return DeFiErrors::LoanPaybackDisabled(
                                    paybackToken->symbol);

                        CDataStructureV0 penaltyKey{
                            AttributeTypes::Token, loanTokenId.v, TokenKeys::LoanPaybackFeePCT, paybackTokenId.v};
                        penaltyPct -= attributes->GetValue(penaltyKey, CAmount{0});
                    } else {
                        CDataStructureV0 activeKey{AttributeTypes::Token, loanTokenId.v, TokenKeys::PaybackDFI};
                        if (!attributes->GetValue(activeKey, false)) return DeFiErrors::LoanPaybackDisabled(
                                    paybackToken->symbol);

                        CDataStructureV0 penaltyKey{AttributeTypes::Token, loanTokenId.v, TokenKeys::PaybackDFIFeePCT};
                        penaltyPct -= attributes->GetValue(penaltyKey, COIN / 100);
                    }

                    // Get token price in USD
                    const CTokenCurrencyPair tokenUsdPair{paybackToken->symbol, "USD"};
                    bool useNextPrice{false}, requireLivePrice{true};
                    const auto resVal = mnview.GetValidatedIntervalPrice(tokenUsdPair, useNextPrice, requireLivePrice);
                    if (!resVal)
                        return std::move(resVal);

                    paybackUsdPrice = MultiplyAmounts(*resVal.val, penaltyPct);

                    // Calculate the DFI amount in DUSD
                    auto usdAmount = MultiplyAmounts(paybackUsdPrice, kv.second);

                    if (loanToken->symbol == "DUSD") {
                        paybackAmount = usdAmount;
                        if (paybackUsdPrice > COIN) {
                            if (paybackAmount < kv.second) {
                                return DeFiErrors::AmountOverflowAsValuePrice(kv.second, paybackUsdPrice);
                            }
                        }
                    } else {
                        // Get dToken price in USD
                        const CTokenCurrencyPair dTokenUsdPair{loanToken->symbol, "USD"};
                        bool useNextPrice{false}, requireLivePrice{true};
                        const auto resVal =
                            mnview.GetValidatedIntervalPrice(dTokenUsdPair, useNextPrice, requireLivePrice);
                        if (!resVal)
                            return std::move(resVal);

                        loanUsdPrice = *resVal.val;

                        paybackAmount = DivideAmounts(usdAmount, loanUsdPrice);
                    }
                }

                const auto loanAmounts = mnview.GetLoanTokens(obj.vaultId);
                if (!loanAmounts) return DeFiErrors::LoanInvalidVault(obj.vaultId);

                if (!loanAmounts->balances.count(loanTokenId)) 
                    return DeFiErrors::LoanInvalidTokenForSymbol(loanToken->symbol);

                const auto &currentLoanAmount = loanAmounts->balances.at(loanTokenId);

                const auto rate = mnview.GetInterestRate(obj.vaultId, loanTokenId, height);
                if (!rate) return DeFiErrors::TokenInterestRateInvalid(loanToken->symbol);

                auto subInterest = TotalInterest(*rate, height);

                if (subInterest < 0) {
                    TrackNegativeInterest(
                        mnview,
                        {loanTokenId, currentLoanAmount > std::abs(subInterest) ? std::abs(subInterest) : subInterest});
                }

                // In the case of negative subInterest the amount ends up being added to paybackAmount
                auto subLoan = paybackAmount - subInterest;

                if (paybackAmount < subInterest) {
                    subInterest = paybackAmount;
                    subLoan     = 0;
                } else if (currentLoanAmount - subLoan < 0) {
                    subLoan = currentLoanAmount;
                }

                if (loanToken->symbol == "DUSD") {
                    TrackDUSDSub(mnview, {loanTokenId, subLoan});
                }

                res = mnview.SubLoanToken(obj.vaultId, CTokenAmount{loanTokenId, subLoan});
                if (!res)
                    return res;

                // Eraseinterest. On subInterest is nil interest ITH and IPB will be updated, if
                // subInterest is negative or IPB is negative and subLoan is equal to the loan amount
                // then IPB will be updated and ITH will be wiped.
                res = mnview.DecreaseInterest(
                    height,
                    obj.vaultId,
                    vault->schemeId,
                    loanTokenId,
                    subLoan,
                    subInterest < 0 || (rate->interestPerBlock.negative && subLoan == currentLoanAmount)
                        ? std::numeric_limits<CAmount>::max()
                        : subInterest);
                if (!res)
                    return res;

                if (height >= static_cast<uint32_t>(consensus.FortCanningMuseumHeight) && subLoan < currentLoanAmount &&
                    height < static_cast<uint32_t>(consensus.FortCanningGreatWorldHeight)) {
                    auto newRate = mnview.GetInterestRate(obj.vaultId, loanTokenId, height);
                    if (!newRate) return DeFiErrors::TokenInterestRateInvalid(loanToken->symbol);

                    Require(newRate->interestPerBlock.amount != 0,
                            "Cannot payback this amount of loan for %s, either payback full amount or less than this "
                            "amount!",
                            loanToken->symbol);
                }

                CalculateOwnerRewards(obj.from);

                if (paybackTokenId == loanTokenId) {
                    res = mnview.SubMintedTokens(loanTokenId, subInterest > 0 ? subLoan : subLoan + subInterest);
                    if (!res)
                        return res;

                    // If interest was negative remove it from sub amount
                    if (height >= static_cast<uint32_t>(consensus.FortCanningEpilogueHeight) && subInterest < 0)
                        subLoan += subInterest;

                    // Do not sub balance if negative interest fully negates the current loan amount
                    if (!(subInterest < 0 && std::abs(subInterest) >= currentLoanAmount)) {
                        // If negative interest plus payback amount overpays then reduce payback amount by the
                        // difference
                        if (subInterest < 0 && paybackAmount - subInterest > currentLoanAmount) {
                            subLoan = currentLoanAmount + subInterest;
                        }

                        // subtract loan amount first, interest is burning below
                        LogPrint(BCLog::LOAN,
                                 "CLoanPaybackLoanMessage(): Sub loan from balance - %lld, height - %d\n",
                                 subLoan,
                                 height);
                        res = mnview.SubBalance(obj.from, CTokenAmount{loanTokenId, subLoan});
                        if (!res)
                            return res;
                    }

                    // burn interest Token->USD->DFI->burnAddress
                    if (subInterest > 0) {
                        LogPrint(BCLog::LOAN,
                                 "CLoanPaybackLoanMessage(): Swapping %s interest to DFI - %lld, height - %d\n",
                                 loanToken->symbol,
                                 subInterest,
                                 height);
                        res = SwapToDFIorDUSD(mnview, loanTokenId, subInterest, obj.from, consensus.burnAddress, height);
                        if (!res)
                            return res;
                    }
                } else {
                    CAmount subInToken;
                    const auto subAmount = subLoan + subInterest;

                    // if payback overpay loan and interest amount
                    if (paybackAmount > subAmount) {
                        if (loanToken->symbol == "DUSD") {
                            subInToken = DivideAmounts(subAmount, paybackUsdPrice);
                            if (MultiplyAmounts(subInToken, paybackUsdPrice) != subAmount)
                                subInToken += 1;
                        } else {
                            auto tempAmount = MultiplyAmounts(subAmount, loanUsdPrice);

                            subInToken = DivideAmounts(tempAmount, paybackUsdPrice);
                            if (DivideAmounts(MultiplyAmounts(subInToken, paybackUsdPrice), loanUsdPrice) != subAmount)
                                subInToken += 1;
                        }
                    } else {
                        subInToken = kv.second;
                    }

                    shouldSetVariable = true;

                    auto penalty = MultiplyAmounts(subInToken, COIN - penaltyPct);

                    if (paybackTokenId == DCT_ID{0}) {
                        CDataStructureV0 liveKey{
                            AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackDFITokens};
                        auto balances = attributes->GetValue(liveKey, CBalances{});
                        balances.Add({loanTokenId, subAmount});
                        balances.Add({paybackTokenId, penalty});
                        attributes->SetValue(liveKey, balances);

                        liveKey.key = EconomyKeys::PaybackDFITokensPrincipal;
                        balances    = attributes->GetValue(liveKey, CBalances{});
                        balances.Add({loanTokenId, subLoan});
                        attributes->SetValue(liveKey, balances);

                        LogPrint(BCLog::LOAN,
                                 "CLoanPaybackLoanMessage(): Burning interest and loan in %s directly - total loan "
                                 "%lld (%lld %s), height - %d\n",
                                 paybackToken->symbol,
                                 subLoan + subInterest,
                                 subInToken,
                                 paybackToken->symbol,
                                 height);

                        res = TransferTokenBalance(paybackTokenId, subInToken, obj.from, consensus.burnAddress);
                        if (!res)
                            return res;
                    } else {
                        CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackTokens};
                        auto balances = attributes->GetValue(liveKey, CTokenPayback{});

                        balances.tokensPayback.Add(CTokenAmount{loanTokenId, subAmount});
                        balances.tokensFee.Add(CTokenAmount{paybackTokenId, penalty});
                        attributes->SetValue(liveKey, balances);

                        LogPrint(BCLog::LOAN,
                                 "CLoanPaybackLoanMessage(): Swapping %s to DFI and burning it - total loan %lld (%lld "
                                 "%s), height - %d\n",
                                 paybackToken->symbol,
                                 subLoan + subInterest,
                                 subInToken,
                                 paybackToken->symbol,
                                 height);

                        CDataStructureV0 directBurnKey{
                            AttributeTypes::Param, ParamIDs::DFIP2206A, DFIPKeys::DUSDLoanBurn};
                        auto directLoanBurn = attributes->GetValue(directBurnKey, false);

                        res = SwapToDFIorDUSD(mnview,
                                                paybackTokenId,
                                                subInToken,
                                                obj.from,
                                                consensus.burnAddress,
                                                height,
                                                !directLoanBurn);
                        if (!res)
                            return res;
                    }
                }
            }
        }

        return shouldSetVariable ? mnview.SetVariable(*attributes) : Res::Ok();
    }

    Res operator()(const CAuctionBidMessage &obj) const {
        Require(CheckCustomTx());

        // owner auth
        Require(HasAuth(obj.from), "tx must have at least one input from token owner");

        // vault exists
        auto vault = mnview.GetVault(obj.vaultId);
        Require(vault, "Vault <%s> not found", obj.vaultId.GetHex());

        // vault under liquidation
        Require(vault->isUnderLiquidation, "Cannot bid to vault which is not under liquidation");

        auto data = mnview.GetAuction(obj.vaultId, height);
        Require(data, "No auction data to vault %s", obj.vaultId.GetHex());

        auto batch = mnview.GetAuctionBatch({obj.vaultId, obj.index});
        Require(batch, "No batch to vault/index %s/%d", obj.vaultId.GetHex(), obj.index);

        Require(obj.amount.nTokenId == batch->loanAmount.nTokenId, "Bid token does not match auction one");

        auto bid = mnview.GetAuctionBid({obj.vaultId, obj.index});
        if (!bid) {
            auto amount = MultiplyAmounts(batch->loanAmount.nValue, COIN + data->liquidationPenalty);
            Require(amount <= obj.amount.nValue,
                    "First bid should include liquidation penalty of %d%%",
                    data->liquidationPenalty * 100 / COIN);

            if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight && data->liquidationPenalty &&
                obj.amount.nValue == batch->loanAmount.nValue)
                return Res::Err("First bid should be higher than batch one");
        } else {
            auto amount = MultiplyAmounts(bid->second.nValue, COIN + (COIN / 100));
            Require(amount <= obj.amount.nValue, "Bid override should be at least 1%% higher than current one");

            if (static_cast<int>(height) >= consensus.FortCanningMuseumHeight &&
                obj.amount.nValue == bid->second.nValue)
                return Res::Err("Bid override should be higher than last one");

            // immediate refund previous bid
            CalculateOwnerRewards(bid->first);
            mnview.AddBalance(bid->first, bid->second);
        }
        // check balance
        CalculateOwnerRewards(obj.from);
        Require(mnview.SubBalance(obj.from, obj.amount));
        return mnview.StoreAuctionBid({obj.vaultId, obj.index}, {obj.from, obj.amount});
    }

    Res operator()(const CCreateProposalMessage &obj) const {
        auto res = IsOnChainGovernanceEnabled();
        if (!res) {
            return res;
        }

        switch (obj.type) {
            case CProposalType::CommunityFundProposal:
                if (!HasAuth(obj.address))
                    return Res::Err("tx must have at least one input from proposal account");
                break;

            case CProposalType::VoteOfConfidence:
                if (obj.nAmount != 0)
                    return Res::Err("proposal amount in vote of confidence");

                if (!obj.address.empty())
                    return Res::Err("vote of confidence address should be empty");

                if (!(obj.options & CProposalOption::Emergency) && obj.nCycles != VOC_CYCLES)
                    return Res::Err("proposal cycles should be %d", int(VOC_CYCLES));
                break;

            default:
                return Res::Err("unsupported proposal type");
        }

        res = CheckProposalTx(obj);
        if (!res)
            return res;

        if (obj.nAmount >= MAX_MONEY)
            return Res::Err("proposal wants to gain all money");

        if (obj.title.empty())
            return Res::Err("proposal title must not be empty");

        if (obj.title.size() > MAX_PROPOSAL_TITLE_SIZE)
            return Res::Err("proposal title cannot be more than %d bytes", MAX_PROPOSAL_TITLE_SIZE);

        if (obj.context.empty())
            return Res::Err("proposal context must not be empty");

        if (obj.context.size() > MAX_PROPOSAL_CONTEXT_SIZE)
            return Res::Err("proposal context cannot be more than %d bytes", MAX_PROPOSAL_CONTEXT_SIZE);

        if (obj.contextHash.size() > MAX_PROPOSAL_CONTEXT_SIZE)
            return Res::Err("proposal context hash cannot be more than %d bytes", MAX_PROPOSAL_CONTEXT_SIZE);

        auto attributes = mnview.GetAttributes();
        assert(attributes);
        CDataStructureV0 cfpMaxCycles{AttributeTypes::Governance, GovernanceIDs::Proposals, GovernanceKeys::CFPMaxCycles};
        auto maxCycles = attributes->GetValue(cfpMaxCycles, static_cast<uint32_t>(MAX_CYCLES));

        if (obj.nCycles < 1 || obj.nCycles > maxCycles )
            return Res::Err("proposal cycles can be between 1 and %d", maxCycles);

        if ((obj.options & CProposalOption::Emergency)) {
            if (obj.nCycles != 1) {
                return Res::Err("emergency proposal cycles must be 1");
            }

            if (static_cast<CProposalType>(obj.type) != CProposalType::VoteOfConfidence) {
                return Res::Err("only vote of confidence allowed with emergency option");
            }
        }

        return mnview.CreateProposal(tx.GetHash(), height, obj, tx.vout[0].nValue);
    }

    Res operator()(const CProposalVoteMessage &obj) const {
        auto res = IsOnChainGovernanceEnabled();
        if (!res) {
            return res;
        }

        auto prop = mnview.GetProposal(obj.propId);
        if (!prop)
            return Res::Err("proposal <%s> does not exist", obj.propId.GetHex());

        if (prop->status != CProposalStatusType::Voting)
            return Res::Err("proposal <%s> is not in voting period", obj.propId.GetHex());

        auto node = mnview.GetMasternode(obj.masternodeId);
        if (!node)
            return Res::Err("masternode <%s> does not exist", obj.masternodeId.GetHex());

        auto ownerDest = node->ownerType == 1 ? CTxDestination(PKHash(node->ownerAuthAddress))
                                              : CTxDestination(WitnessV0KeyHash(node->ownerAuthAddress));

        if (!HasAuth(GetScriptForDestination(ownerDest)))
            return Res::Err("tx must have at least one input from the owner");

        if (!node->IsActive(height, mnview))
            return Res::Err("masternode <%s> is not active", obj.masternodeId.GetHex());

        if (node->mintedBlocks < 1)
            return Res::Err("masternode <%s> does not mine at least one block", obj.masternodeId.GetHex());

        switch (obj.vote) {
            case CProposalVoteType::VoteNo:
            case CProposalVoteType::VoteYes:
            case CProposalVoteType::VoteNeutral:
                break;
            default:
                return Res::Err("unsupported vote type");
        }
        auto vote = static_cast<CProposalVoteType>(obj.vote);
        return mnview.AddProposalVote(obj.propId, obj.masternodeId, vote);
    }

    Res operator()(const CCustomTxMessageNone &) const { return Res::Ok(); }
};

Res CustomMetadataParse(uint32_t height,
                        const Consensus::Params &consensus,
                        const std::vector<unsigned char> &metadata,
                        CCustomTxMessage &txMessage) {
    try {
        return std::visit(CCustomMetadataParseVisitor(height, consensus, metadata), txMessage);
    } catch (const std::exception &e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

bool IsDisabledTx(uint32_t height, CustomTxType type, const Consensus::Params &consensus) {
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
        (IsTestNetwork() && static_cast<int>(height) >= 1250000)) {
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

bool IsDisabledTx(uint32_t height, const CTransaction &tx, const Consensus::Params &consensus) {
    TBytes dummy;
    auto txType = GuessCustomTxType(tx, dummy);
    return IsDisabledTx(height, txType, consensus);
}

Res CustomTxVisit(CCustomCSView &mnview,
                  const CCoinsViewCache &coins,
                  const CTransaction &tx,
                  uint32_t height,
                  const Consensus::Params &consensus,
                  const CCustomTxMessage &txMessage,
                  uint64_t time,
                  uint32_t txn) {
    if (IsDisabledTx(height, tx, consensus)) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "Disabled custom transaction");
    }
    try {
        return std::visit(CCustomTxApplyVisitor(tx, height, coins, mnview, consensus, time, txn), txMessage);
    } catch (const std::bad_variant_access &e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

bool ShouldReturnNonFatalError(const CTransaction &tx, uint32_t height) {
    static const std::map<uint32_t, uint256> skippedTx = {
        {471222, uint256S("0ab0b76352e2d865761f4c53037041f33e1200183d55cdf6b09500d6f16b7329")},
    };
    auto it = skippedTx.find(height);
    return it != skippedTx.end() && it->second == tx.GetHash();
}

void PopulateVaultHistoryData(CHistoryWriters &writers,
                              CAccountsHistoryWriter &view,
                              const CCustomTxMessage &txMessage,
                              const CustomTxType txType,
                              const uint32_t height,
                              const uint32_t txn,
                              const uint256 &txid) {
    if (txType == CustomTxType::Vault) {
        auto obj          = std::get<CVaultMessage>(txMessage);
        writers.schemeID = obj.schemeId;
        view.vaultID      = txid;
    } else if (txType == CustomTxType::CloseVault) {
        auto obj     = std::get<CCloseVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::UpdateVault) {
        auto obj     = std::get<CUpdateVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
        if (!obj.schemeId.empty()) {
            writers.schemeID = obj.schemeId;
        }
    } else if (txType == CustomTxType::DepositToVault) {
        auto obj     = std::get<CDepositToVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::WithdrawFromVault) {
        auto obj     = std::get<CWithdrawFromVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::PaybackWithCollateral) {
        auto obj     = std::get<CPaybackWithCollateralMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::TakeLoan) {
        auto obj     = std::get<CLoanTakeLoanMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::PaybackLoan) {
        auto obj     = std::get<CLoanPaybackLoanMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::PaybackLoanV2) {
        auto obj     = std::get<CLoanPaybackLoanV2Message>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::AuctionBid) {
        auto obj     = std::get<CAuctionBidMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::LoanScheme) {
        auto obj                             = std::get<CLoanSchemeMessage>(txMessage);
        writers.globalLoanScheme.identifier = obj.identifier;
        writers.globalLoanScheme.ratio      = obj.ratio;
        writers.globalLoanScheme.rate       = obj.rate;
        if (!obj.updateHeight) {
            writers.globalLoanScheme.schemeCreationTxid = txid;
        } else {
            writers.GetVaultView()->ForEachGlobalScheme(
                [&writers](const VaultGlobalSchemeKey &key, CLazySerialize<VaultGlobalSchemeValue> value) {
                    if (value.get().loanScheme.identifier != writers.globalLoanScheme.identifier) {
                        return true;
                    }
                    writers.globalLoanScheme.schemeCreationTxid = key.schemeCreationTxid;
                    return false;
                },
                {height, txn, {}});
        }
    }
}

Res ApplyCustomTx(CCustomCSView &mnview,
                  const CCoinsViewCache &coins,
                  const CTransaction &tx,
                  const Consensus::Params &consensus,
                  uint32_t height,
                  uint64_t time,
                  uint256 *canSpend,
                  uint32_t txn) {
    auto res = Res::Ok();
    if (tx.IsCoinBase() && height > 0) {  // genesis contains custom coinbase txs
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
    CAccountsHistoryWriter view(mnview, height, txn, tx.GetHash(), uint8_t(txType));
    if ((res = CustomMetadataParse(height, consensus, metadata, txMessage))) {
        if (mnview.GetHistoryWriters().GetVaultView()) {
            PopulateVaultHistoryData(mnview.GetHistoryWriters(), view, txMessage, txType, height, txn, tx.GetHash());
        }

        res = CustomTxVisit(view, coins, tx, height, consensus, txMessage, time, txn);

        if (res) {
            if (canSpend && txType == CustomTxType::UpdateMasternode) {
                auto obj = std::get<CUpdateMasterNodeMessage>(txMessage);
                for (const auto &item : obj.updates) {
                    if (item.first == static_cast<uint8_t>(UpdateMasternodeType::OwnerAddress)) {
                        if (const auto node = mnview.GetMasternode(obj.mnId)) {
                            *canSpend = node->collateralTx.IsNull() ? obj.mnId : node->collateralTx;
                        }
                        break;
                    }
                }
            }

            // Track burn fee
            if (txType == CustomTxType::CreateToken || txType == CustomTxType::CreateMasternode) {
                mnview.GetHistoryWriters().AddFeeBurn(tx.vout[0].scriptPubKey, tx.vout[0].nValue);
            }

            if (txType == CustomTxType::CreateCfp || txType == CustomTxType::CreateVoc) {
                // burn fee_burn_pct of creation fee, the rest is distributed among voting masternodes
                CDataStructureV0 burnPctKey{
                        AttributeTypes::Governance, GovernanceIDs::Proposals, GovernanceKeys::FeeBurnPct};

                auto attributes = view.GetAttributes();
                assert(attributes);

                auto burnFee = MultiplyAmounts(tx.vout[0].nValue, attributes->GetValue(burnPctKey, COIN / 2));
                mnview.GetHistoryWriters().AddFeeBurn(tx.vout[0].scriptPubKey, burnFee);
            }

            if (txType == CustomTxType::Vault) {
                // burn the half, the rest is returned on close vault
                auto burnFee = tx.vout[0].nValue / 2;
                mnview.GetHistoryWriters().AddFeeBurn(tx.vout[0].scriptPubKey, burnFee);
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
    auto &flushable = view.GetStorage();
    auto undo       = CUndo::Construct(mnview.GetStorage(), flushable.GetRaw());
    // flush changes
    view.Flush();
    // write undo
    if (!undo.before.empty()) {
        mnview.SetUndo(UndoKey{height, tx.GetHash()}, undo);
    }
    return res;
}

ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView &mnview,
                                    const CTransaction &tx,
                                    int height,
                                    const uint256 &prevStakeModifier,
                                    const std::vector<unsigned char> &metadata,
                                    const Consensus::Params &consensusParams) {
    Require(height < consensusParams.DakotaHeight, "Old anchor TX type after Dakota fork. Height %d", height);

    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    CAnchorFinalizationMessage finMsg;
    ss >> finMsg;

    auto rewardTx = mnview.GetRewardForAnchor(finMsg.btcTxHash);
    if (rewardTx) {
        return Res::ErrDbg("bad-ar-exists",
                           "reward for anchor %s already exists (tx: %s)",
                           finMsg.btcTxHash.ToString(),
                           (*rewardTx).ToString());
    }

    if (!finMsg.CheckConfirmSigs()) {
        return Res::ErrDbg("bad-ar-sigs", "anchor signatures are incorrect");
    }

    if (finMsg.sigs.size() < GetMinAnchorQuorum(finMsg.currentTeam)) {
        return Res::ErrDbg("bad-ar-sigs-quorum",
                           "anchor sigs (%d) < min quorum (%) ",
                           finMsg.sigs.size(),
                           GetMinAnchorQuorum(finMsg.currentTeam));
    }

    // check reward sum
    if (height >= consensusParams.AMKHeight) {
        const auto cbValues = tx.GetValuesOut();
        if (cbValues.size() != 1 || cbValues.begin()->first != DCT_ID{0})
            return Res::ErrDbg("bad-ar-wrong-tokens", "anchor reward should be payed only in Defi coins");

        const auto anchorReward = mnview.GetCommunityBalance(CommunityAccountType::AnchorReward);
        if (cbValues.begin()->second != anchorReward) {
            return Res::ErrDbg("bad-ar-amount",
                               "anchor pays wrong amount (actual=%d vs expected=%d)",
                               cbValues.begin()->second,
                               anchorReward);
        }
    } else {  // pre-AMK logic
        auto anchorReward = GetAnchorSubsidy(finMsg.anchorHeight, finMsg.prevAnchorHeight, consensusParams);
        if (tx.GetValueOut() > anchorReward) {
            return Res::ErrDbg(
                "bad-ar-amount", "anchor pays too much (actual=%d vs limit=%d)", tx.GetValueOut(), anchorReward);
        }
    }

    CTxDestination destination = finMsg.rewardKeyType == 1 ? CTxDestination(PKHash(finMsg.rewardKeyID))
                                                           : CTxDestination(WitnessV0KeyHash(finMsg.rewardKeyID));
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
        LogPrint(BCLog::ACCOUNTCHANGE,
                 "AccountChange: hash=%s fund=%s change=%s\n",
                 tx.GetHash().ToString(),
                 GetCommunityAccountName(CommunityAccountType::AnchorReward),
                 (CBalances{{{{0}, -mnview.GetCommunityBalance(CommunityAccountType::AnchorReward)}}}.ToString()));
        mnview.SetCommunityBalance(CommunityAccountType::AnchorReward, 0);  // just reset
    } else {
        mnview.SetFoundationsDebt(mnview.GetFoundationsDebt() + tx.GetValueOut());
    }

    return {finMsg.btcTxHash, Res::Ok()};
}

ResVal<uint256> ApplyAnchorRewardTxPlus(CCustomCSView &mnview,
                                        const CTransaction &tx,
                                        int height,
                                        const std::vector<unsigned char> &metadata,
                                        const Consensus::Params &consensusParams) {
    Require(height >= consensusParams.DakotaHeight, "New anchor TX type before Dakota fork. Height %d", height);

    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    CAnchorFinalizationMessagePlus finMsg;
    ss >> finMsg;

    auto rewardTx = mnview.GetRewardForAnchor(finMsg.btcTxHash);
    if (rewardTx) {
        return Res::ErrDbg("bad-ar-exists",
                           "reward for anchor %s already exists (tx: %s)",
                           finMsg.btcTxHash.ToString(),
                           (*rewardTx).ToString());
    }

    // Miner used confirm team at chain height when creating this TX, this is height - 1.
    int anchorHeight = height - 1;
    auto uniqueKeys  = finMsg.CheckConfirmSigs(anchorHeight);
    if (!uniqueKeys) {
        return Res::ErrDbg("bad-ar-sigs", "anchor signatures are incorrect");
    }

    auto team = mnview.GetConfirmTeam(anchorHeight);
    if (!team) {
        return Res::ErrDbg("bad-ar-team", "could not get confirm team for height: %d", anchorHeight);
    }

    auto quorum = GetMinAnchorQuorum(*team);
    Require(finMsg.sigs.size() >= quorum, "anchor sigs (%d) < min quorum (%) ", finMsg.sigs.size(), quorum);
    Require(uniqueKeys >= quorum, "anchor unique keys (%d) < min quorum (%) ", uniqueKeys, quorum);

    // Make sure anchor block height and hash exist in chain.
    auto *anchorIndex = ::ChainActive()[finMsg.anchorHeight];
    Require(anchorIndex,
            "Active chain does not contain block height %d. Chain height %d",
            finMsg.anchorHeight,
            ::ChainActive().Height());
    Require(anchorIndex->GetBlockHash() == finMsg.dfiBlockHash,
            "Anchor and blockchain mismatch at height %d. Expected %s found %s",
            finMsg.anchorHeight,
            anchorIndex->GetBlockHash().ToString(),
            finMsg.dfiBlockHash.ToString());
    // check reward sum
    const auto cbValues = tx.GetValuesOut();
    Require(cbValues.size() == 1 && cbValues.begin()->first == DCT_ID{0}, "anchor reward should be paid in DFI only");

    const auto anchorReward = mnview.GetCommunityBalance(CommunityAccountType::AnchorReward);
    Require(cbValues.begin()->second == anchorReward,
            "anchor pays wrong amount (actual=%d vs expected=%d)",
            cbValues.begin()->second,
            anchorReward);

    CTxDestination destination = finMsg.rewardKeyType == 1 ? CTxDestination(PKHash(finMsg.rewardKeyID))
                                                           : CTxDestination(WitnessV0KeyHash(finMsg.rewardKeyID));
    Require(tx.vout[1].scriptPubKey == GetScriptForDestination(destination), "anchor pay destination is incorrect");

    LogPrint(BCLog::ACCOUNTCHANGE,
             "AccountChange: hash=%s fund=%s change=%s\n",
             tx.GetHash().ToString(),
             GetCommunityAccountName(CommunityAccountType::AnchorReward),
             (CBalances{{{{0}, -mnview.GetCommunityBalance(CommunityAccountType::AnchorReward)}}}.ToString()));
    mnview.SetCommunityBalance(CommunityAccountType::AnchorReward, 0);  // just reset
    mnview.AddRewardForAnchor(finMsg.btcTxHash, tx.GetHash());

    // Store reward data for RPC info
    mnview.AddAnchorConfirmData(CAnchorConfirmDataPlus{finMsg});

    return {finMsg.btcTxHash, Res::Ok()};
}

bool IsMempooledCustomTxCreate(const CTxMemPool &pool, const uint256 &txid) {
    CTransactionRef ptx = pool.get(txid);
    if (ptx) {
        std::vector<unsigned char> dummy;
        CustomTxType txType = GuessCustomTxType(*ptx, dummy);
        return txType == CustomTxType::CreateMasternode || txType == CustomTxType::CreateToken;
    }
    return false;
}

std::vector<DCT_ID> CPoolSwap::CalculateSwaps(CCustomCSView &view, bool testOnly) {
    std::vector<std::vector<DCT_ID> > poolPaths = CalculatePoolPaths(view);

    // Record best pair
    std::pair<std::vector<DCT_ID>, CAmount> bestPair{{}, -1};

    // Loop through all common pairs
    for (const auto &path : poolPaths) {
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

std::vector<std::vector<DCT_ID> > CPoolSwap::CalculatePoolPaths(CCustomCSView &view) {
    std::vector<std::vector<DCT_ID> > poolPaths;

    // For tokens to be traded get all pairs and pool IDs
    std::multimap<uint32_t, DCT_ID> fromPoolsID, toPoolsID;
    view.ForEachPoolPair(
        [&](DCT_ID const &id, const CPoolPair &pool) {
            if ((obj.idTokenFrom == pool.idTokenA && obj.idTokenTo == pool.idTokenB) ||
                (obj.idTokenTo == pool.idTokenA && obj.idTokenFrom == pool.idTokenB)) {
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
        },
        {0});

    if (fromPoolsID.empty() || toPoolsID.empty()) {
        return {};
    }

    // Find intersection on key
    std::map<uint32_t, DCT_ID> commonPairs;
    set_intersection(fromPoolsID.begin(),
                     fromPoolsID.end(),
                     toPoolsID.begin(),
                     toPoolsID.end(),
                     std::inserter(commonPairs, commonPairs.begin()),
                     [](std::pair<uint32_t, DCT_ID> a, std::pair<uint32_t, DCT_ID> b) { return a.first < b.first; });

    // Loop through all common pairs and record direct pool to pool swaps
    for (const auto &item : commonPairs) {
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
    view.ForEachPoolPair(
        [&](DCT_ID const &id, const CPoolPair &pool) {
            // Loop through from pool multimap on unique keys only
            for (auto fromIt = fromPoolsID.begin(); fromIt != fromPoolsID.end();
                 fromIt      = fromPoolsID.equal_range(fromIt->first).second) {
                // Loop through to pool multimap on unique keys only
                for (auto toIt = toPoolsID.begin(); toIt != toPoolsID.end();
                     toIt      = toPoolsID.equal_range(toIt->first).second) {
                    // If a pool pairs matches from pair and to pair add it to the pool paths
                    if ((fromIt->first == pool.idTokenA.v && toIt->first == pool.idTokenB.v) ||
                        (fromIt->first == pool.idTokenB.v && toIt->first == pool.idTokenA.v)) {
                        poolPaths.push_back({fromIt->second, id, toIt->second});
                    }
                }
            }

            return true;
        },
        {0});

    // return pool paths
    return poolPaths;
}

// Note: `testOnly` doesn't update views, and as such can result in a previous price calculations
// for a pool, if used multiple times (or duplicated pool IDs) with the same view.
// testOnly is only meant for one-off tests per well defined view.
Res CPoolSwap::ExecuteSwap(CCustomCSView &view, std::vector<DCT_ID> poolIDs, bool testOnly) {
    Res poolResult = Res::Ok();
    // No composite swap allowed before Fort Canning
    if (height < static_cast<uint32_t>(Params().GetConsensus().FortCanningHeight) && !poolIDs.empty()) {
        poolIDs.clear();
    }

    Require(obj.amountFrom > 0, "Input amount should be positive");

    if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight) &&
        poolIDs.size() > MAX_POOL_SWAPS) {
        return Res::Err(
            strprintf("Too many pool IDs provided, max %d allowed, %d provided", MAX_POOL_SWAPS, poolIDs.size()));
    }

    // Single swap if no pool IDs provided
    auto poolPrice = POOLPRICE_MAX;
    std::optional<std::pair<DCT_ID, CPoolPair> > poolPair;
    if (poolIDs.empty()) {
        poolPair = view.GetPoolPair(obj.idTokenFrom, obj.idTokenTo);
        Require(poolPair, "Cannot find the pool pair.");

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
        } else  // Or get pools from IDs provided for composite swap
        {
            pool = view.GetPoolPair(currentID);
            Require(pool, "Cannot find the pool pair.");
        }

        // Check if last pool swap
        bool lastSwap = i + 1 == poolIDs.size();

        const auto swapAmount = swapAmountResult;

        if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHillHeight) && lastSwap) {
            Require(obj.idTokenTo != swapAmount.nTokenId,
                    "Final swap should have idTokenTo as destination, not source");

            Require(pool->idTokenA == obj.idTokenTo || pool->idTokenB == obj.idTokenTo,
                    "Final swap pool should have idTokenTo, incorrect final pool ID provided");
        }

        if (view.AreTokensLocked({pool->idTokenA.v, pool->idTokenB.v})) {
            return Res::Err("Pool currently disabled due to locked token");
        }

        CDataStructureV0 dirAKey{AttributeTypes::Poolpairs, currentID.v, PoolKeys::TokenAFeeDir};
        CDataStructureV0 dirBKey{AttributeTypes::Poolpairs, currentID.v, PoolKeys::TokenBFeeDir};
        const auto dirA          = attributes->GetValue(dirAKey, CFeeDir{FeeDirValues::Both});
        const auto dirB          = attributes->GetValue(dirBKey, CFeeDir{FeeDirValues::Both});
        const auto asymmetricFee = std::make_pair(dirA, dirB);

        auto dexfeeInPct = view.GetDexFeeInPct(currentID, swapAmount.nTokenId);
        auto &balances   = dexBalances[currentID];
        auto forward     = swapAmount.nTokenId == pool->idTokenA;

        auto &totalTokenA = forward ? balances.totalTokenA : balances.totalTokenB;
        auto &totalTokenB = forward ? balances.totalTokenB : balances.totalTokenA;

        const auto &reserveAmount   = forward ? pool->reserveA : pool->reserveB;
        const auto &blockCommission = forward ? pool->blockCommissionA : pool->blockCommissionB;

        const auto initReserveAmount   = reserveAmount;
        const auto initBlockCommission = blockCommission;

        // Perform swap
        poolResult = pool->Swap(
            swapAmount,
            dexfeeInPct,
            poolPrice,
            asymmetricFee,
            [&](const CTokenAmount &dexfeeInAmount, const CTokenAmount &tokenAmount) {
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
                auto &subView = i == 0 ? view : intermediateView;
                res           = subView.SubBalance(obj.from, swapAmount);
                if (!res) {
                    return res;
                }
                intermediateView.Flush();

                auto &addView = lastSwap ? view : intermediateView;
                if (height >= static_cast<uint32_t>(Params().GetConsensus().GrandCentralHeight)) {
                    res = addView.AddBalance(lastSwap ? (obj.to.empty() ? obj.from : obj.to) : obj.from,
                                             swapAmountResult);
                } else {
                    res = addView.AddBalance(lastSwap ? obj.to : obj.from, swapAmountResult);
                }
                if (!res) {
                    return res;
                }
                intermediateView.Flush();

                const auto token = view.GetToken("DUSD");

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
            },
            static_cast<int>(height));

        if (!poolResult) {
            return poolResult;
        }
    }

    if (height >= static_cast<uint32_t>(Params().GetConsensus().GrandCentralHeight)) {
        if (swapAmountResult.nTokenId != obj.idTokenTo) {
            return Res::Err("Final swap output is not same as idTokenTo");
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

    return Res::Ok();
}

Res SwapToDFIorDUSD(CCustomCSView &mnview,
                    DCT_ID tokenId,
                    CAmount amount,
                    const CScript &from,
                    const CScript &to,
                    uint32_t height,
                    bool forceLoanSwap) {
    CPoolSwapMessage obj;

    obj.from        = from;
    obj.to          = to;
    obj.idTokenFrom = tokenId;
    obj.idTokenTo   = DCT_ID{0};
    obj.amountFrom  = amount;
    obj.maxPrice    = POOLPRICE_MAX;

    auto poolSwap = CPoolSwap(obj, height);
    auto token    = mnview.GetToken(tokenId);
    Require(token, "Cannot find token with id %s!", tokenId.ToString());

    // TODO: Optimize double look up later when first token is DUSD.
    auto dUsdToken = mnview.GetToken("DUSD");
    Require(dUsdToken, "Cannot find token DUSD");

    const auto attributes = mnview.GetAttributes();
    Require(attributes, "Attributes unavailable");
    CDataStructureV0 directBurnKey{AttributeTypes::Param, ParamIDs::DFIP2206A, DFIPKeys::DUSDInterestBurn};

    // Direct swap from DUSD to DFI as defined in the CPoolSwapMessage.
    if (tokenId == dUsdToken->first) {
        if (to == Params().GetConsensus().burnAddress && !forceLoanSwap && attributes->GetValue(directBurnKey, false)) {
            // direct burn dUSD
            CTokenAmount dUSD{dUsdToken->first, amount};

            Require(mnview.SubBalance(from, dUSD));

            return mnview.AddBalance(to, dUSD);
        } else
            // swap dUSD -> DFI and burn DFI
            return poolSwap.ExecuteSwap(mnview, {});
    }

    auto pooldUSDDFI = mnview.GetPoolPair(dUsdToken->first, DCT_ID{0});
    Require(pooldUSDDFI, "Cannot find pool pair DUSD-DFI!");

    auto poolTokendUSD = mnview.GetPoolPair(tokenId, dUsdToken->first);
    Require(poolTokendUSD, "Cannot find pool pair %s-DUSD!", token->symbol);

    if (to == Params().GetConsensus().burnAddress && !forceLoanSwap && attributes->GetValue(directBurnKey, false)) {
        obj.idTokenTo = dUsdToken->first;

        // swap tokenID -> dUSD and burn dUSD
        return poolSwap.ExecuteSwap(mnview, {});
    } else
        // swap tokenID -> dUSD -> DFI and burn DFI
        return poolSwap.ExecuteSwap(mnview, {poolTokendUSD->first, pooldUSDDFI->first});
}

bool IsVaultPriceValid(CCustomCSView &mnview, const CVaultId &vaultId, uint32_t height) {
    if (auto collaterals = mnview.GetVaultCollaterals(vaultId))
        for (const auto &collateral : collaterals->balances) {
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
        for (const auto &loan : loans->balances) {
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

bool IsPaybackWithCollateral(CCustomCSView &view, const std::map<DCT_ID, CBalances> &loans) {
    auto tokenDUSD = view.GetToken("DUSD");
    if (!tokenDUSD)
        return false;

    if (loans.size() == 1 && loans.count(tokenDUSD->first) &&
        loans.at(tokenDUSD->first) == CBalances{{{tokenDUSD->first, 999999999999999999LL}}}) {
        return true;
    }
    return false;
}

Res PaybackWithCollateral(CCustomCSView &view,
                          const CVaultData &vault,
                          const CVaultId &vaultId,
                          uint32_t height,
                          uint64_t time) {
    const auto attributes = view.GetAttributes();
    if (!attributes) return DeFiErrors::MNInvalidAttribute();

    const auto dUsdToken = view.GetToken("DUSD");
    if (!dUsdToken) return DeFiErrors::TokenInvalidForName("DUSD");

    CDataStructureV0 activeKey{AttributeTypes::Token, dUsdToken->first.v, TokenKeys::LoanPaybackCollateral};
    if (!attributes->GetValue(activeKey, false)) return DeFiErrors::LoanPaybackWithCollateralDisable();

    const auto collateralAmounts = view.GetVaultCollaterals(vaultId);
    if (!collateralAmounts) return DeFiErrors::VaultNoCollateral();

    if (!collateralAmounts->balances.count(dUsdToken->first)) return DeFiErrors::VaultNoDUSDCollateral();

    const auto &collateralDUSD = collateralAmounts->balances.at(dUsdToken->first);

    const auto loanAmounts = view.GetLoanTokens(vaultId);
    if (!loanAmounts) return DeFiErrors::VaultNoLoans();

    if (!loanAmounts->balances.count(dUsdToken->first)) return DeFiErrors::VaultNoLoans("DUSD");

    const auto &loanDUSD = loanAmounts->balances.at(dUsdToken->first);

    const auto rate = view.GetInterestRate(vaultId, dUsdToken->first, height);
    if (!rate) return DeFiErrors::TokenInterestRateInvalid("DUSD");
    const auto subInterest = TotalInterest(*rate, height);

    Res res{};
    CAmount subLoanAmount{0};
    CAmount subCollateralAmount{0};
    CAmount burnAmount{0};

    // Case where interest > collateral: decrease interest, wipe collateral.
    if (subInterest > collateralDUSD) {
        subCollateralAmount = collateralDUSD;

        res = view.SubVaultCollateral(vaultId, {dUsdToken->first, subCollateralAmount});
        if (!res)
            return res;

        res = view.DecreaseInterest(height, vaultId, vault.schemeId, dUsdToken->first, 0, subCollateralAmount);
        if (!res)
            return res;

        burnAmount = subCollateralAmount;
    } else {
        // Postive interest: Loan + interest > collateral.
        // Negative interest: Loan - abs(interest) > collateral.
        if (loanDUSD + subInterest > collateralDUSD) {
            subLoanAmount       = collateralDUSD - subInterest;
            subCollateralAmount = collateralDUSD;
        } else {
            // Common case: Collateral > loans.
            subLoanAmount       = loanDUSD;
            subCollateralAmount = loanDUSD + subInterest;
        }

        if (subLoanAmount > 0) {
            TrackDUSDSub(view, {dUsdToken->first, subLoanAmount});
            res = view.SubLoanToken(vaultId, {dUsdToken->first, subLoanAmount});
            if (!res)
                return res;
        }

        if (subCollateralAmount > 0) {
            res = view.SubVaultCollateral(vaultId, {dUsdToken->first, subCollateralAmount});
            if (!res)
                return res;
        }

        view.ResetInterest(height, vaultId, vault.schemeId, dUsdToken->first);
        burnAmount = subInterest;
    }

    if (burnAmount > 0) {
        res = view.AddBalance(Params().GetConsensus().burnAddress, {dUsdToken->first, burnAmount});
        if (!res)
            return res;
    } else {
        TrackNegativeInterest(view, {dUsdToken->first, std::abs(burnAmount)});
    }

    // Guard against liquidation
    const auto collaterals = view.GetVaultCollaterals(vaultId);
    const auto loans       = view.GetLoanTokens(vaultId);
    if (loans)
        if (!collaterals) return DeFiErrors::VaultNeedCollateral();

    auto collateralsLoans = view.GetLoanCollaterals(vaultId, *collaterals, height, time);
    if (!collateralsLoans)
        return std::move(collateralsLoans);

    // The check is required to do a ratio check safe guard, or the vault of ratio is unreliable.
    // This can later be removed, if all edge cases of price deviations and max collateral factor for DUSD (1.5
    // currently) can be tested for economical stability. Taking the safer approach for now.
    if (!IsVaultPriceValid(view, vaultId, height)) return DeFiErrors::VaultInvalidPrice();

    const auto scheme = view.GetLoanScheme(vault.schemeId);
    if (collateralsLoans.val->ratio() < scheme->ratio) return DeFiErrors::VaultInsufficientCollateralization(
                collateralsLoans.val->ratio(), scheme->ratio);

    if (subCollateralAmount > 0) {
        res = view.SubMintedTokens(dUsdToken->first, subCollateralAmount);
        if (!res)
            return res;
    }

    return Res::Ok();
}

Res storeGovVars(const CGovernanceHeightMessage &obj, CCustomCSView &view) {
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

bool IsTestNetwork() {
    return Params().NetworkIDString() == CBaseChainParams::TESTNET || Params().NetworkIDString() == CBaseChainParams::DEVNET;
}
