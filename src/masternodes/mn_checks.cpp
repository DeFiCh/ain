// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/consensus/accounts.h>
#include <masternodes/consensus/governance.h>
#include <masternodes/consensus/icxorders.h>
#include <masternodes/consensus/loans.h>
#include <masternodes/consensus/masternodes.h>
#include <masternodes/consensus/oracles.h>
#include <masternodes/consensus/poolpairs.h>
#include <masternodes/consensus/proposals.h>
#include <masternodes/consensus/smartcontracts.h>
#include <masternodes/consensus/tokens.h>
#include <masternodes/consensus/vaults.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/mn_checks.h>
#include <masternodes/vaulthistory.h>

#include <core_io.h>
#include <index/txindex.h>
#include <txmempool.h>
#include <validation.h>

#include <algorithm>

CCustomTxMessage customTypeToMessage(CustomTxType txType) {
    switch (txType) {
        case CustomTxType::CreateMasternode:            return CCreateMasterNodeMessage{};
        case CustomTxType::ResignMasternode:            return CResignMasterNodeMessage{};
        case CustomTxType::UpdateMasternode:            return CUpdateMasterNodeMessage{};
        case CustomTxType::CreateToken:                 return CCreateTokenMessage{};
        case CustomTxType::UpdateToken:                 return CUpdateTokenPreAMKMessage{};
        case CustomTxType::UpdateTokenAny:              return CUpdateTokenMessage{};
        case CustomTxType::MintToken:                   return CMintTokensMessage{};
        case CustomTxType::BurnToken:                   return CBurnTokensMessage{};
        case CustomTxType::CreatePoolPair:              return CCreatePoolPairMessage{};
        case CustomTxType::UpdatePoolPair:              return CUpdatePoolPairMessage{};
        case CustomTxType::PoolSwap:                    return CPoolSwapMessage{};
        case CustomTxType::PoolSwapV2:                  return CPoolSwapMessageV2{};
        case CustomTxType::AddPoolLiquidity:            return CLiquidityMessage{};
        case CustomTxType::RemovePoolLiquidity:         return CRemoveLiquidityMessage{};
        case CustomTxType::UtxosToAccount:              return CUtxosToAccountMessage{};
        case CustomTxType::AccountToUtxos:              return CAccountToUtxosMessage{};
        case CustomTxType::AccountToAccount:            return CAccountToAccountMessage{};
        case CustomTxType::AnyAccountsToAccounts:       return CAnyAccountsToAccountsMessage{};
        case CustomTxType::SmartContract:               return CSmartContractMessage{};
        case CustomTxType::FutureSwap:                  return CFutureSwapMessage{};
        case CustomTxType::SetGovVariable:              return CGovernanceMessage{};
        case CustomTxType::SetGovVariableHeight:        return CGovernanceHeightMessage{};
        case CustomTxType::AppointOracle:               return CAppointOracleMessage{};
        case CustomTxType::RemoveOracleAppoint:         return CRemoveOracleAppointMessage{};
        case CustomTxType::UpdateOracleAppoint:         return CUpdateOracleAppointMessage{};
        case CustomTxType::SetOracleData:               return CSetOracleDataMessage{};
        case CustomTxType::AutoAuthPrep:                return CCustomTxMessageNone{};
        case CustomTxType::ICXCreateOrder:              return CICXCreateOrderMessage{};
        case CustomTxType::ICXMakeOffer:                return CICXMakeOfferMessage{};
        case CustomTxType::ICXSubmitDFCHTLC:            return CICXSubmitDFCHTLCMessage{};
        case CustomTxType::ICXSubmitEXTHTLC:            return CICXSubmitEXTHTLCMessage{};
        case CustomTxType::ICXClaimDFCHTLC:             return CICXClaimDFCHTLCMessage{};
        case CustomTxType::ICXCloseOrder:               return CICXCloseOrderMessage{};
        case CustomTxType::ICXCloseOffer:               return CICXCloseOfferMessage{};
        case CustomTxType::SetLoanCollateralToken:      return CLoanSetCollateralTokenMessage{};
        case CustomTxType::SetLoanToken:                return CLoanSetLoanTokenMessage{};
        case CustomTxType::UpdateLoanToken:             return CLoanUpdateLoanTokenMessage{};
        case CustomTxType::LoanScheme:                  return CLoanSchemeMessage{};
        case CustomTxType::DefaultLoanScheme:           return CDefaultLoanSchemeMessage{};
        case CustomTxType::DestroyLoanScheme:           return CDestroyLoanSchemeMessage{};
        case CustomTxType::Vault:                       return CVaultMessage{};
        case CustomTxType::CloseVault:                  return CCloseVaultMessage{};
        case CustomTxType::UpdateVault:                 return CUpdateVaultMessage{};
        case CustomTxType::DepositToVault:              return CDepositToVaultMessage{};
        case CustomTxType::WithdrawFromVault:           return CWithdrawFromVaultMessage{};
        case CustomTxType::PaybackWithCollateral:       return CPaybackWithCollateralMessage{};
        case CustomTxType::TakeLoan:                    return CLoanTakeLoanMessage{};
        case CustomTxType::PaybackLoan:                 return CLoanPaybackLoanMessage{};
        case CustomTxType::PaybackLoanV2:               return CLoanPaybackLoanV2Message{};
        case CustomTxType::AuctionBid:                  return CAuctionBidMessage{};
        case CustomTxType::FutureSwapExecution:         return CCustomTxMessageNone{};
        case CustomTxType::FutureSwapRefund:            return CCustomTxMessageNone{};
        case CustomTxType::TokenSplit:                  return CCustomTxMessageNone{};
        case CustomTxType::Reject:                      return CCustomTxMessageNone{};
        case CustomTxType::CreateCfp:                   return CCreatePropMessage{};
        case CustomTxType::CreateVoc:                   return CCreatePropMessage{};
        case CustomTxType::Vote:                        return CPropVoteMessage{};
        case CustomTxType::ProposalFeeRedistribution:   return CCustomTxMessageNone{};
        case CustomTxType::UnsetGovVariable:            return CGovernanceUnsetMessage{};
        case CustomTxType::None:                        return CCustomTxMessageNone{};
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
                CCreatePropMessage,
                CPropVoteMessage,
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

    Res operator()(CCustomTxMessageNone &) const {
        return Res::Ok();
    }
};

class CCustomTxApplyVisitor {
    const CTransaction& tx;
    const uint32_t height;
    const CCoinsViewCache& coins;
    CCustomCSView& mnview;
    const Consensus::Params& consensus;
    const uint64_t time;
    const uint32_t txn;

    template<typename T, typename T1, typename ...Args>
    Res ConsensusHandler(const T& obj) const {

        static_assert(std::is_base_of_v<CCustomTxVisitor, T1>, "CCustomTxVisitor base required");

        if constexpr (std::is_invocable_v<T1, T>)
            return T1{tx, height, coins, mnview, consensus, time, txn}(obj);
        else if constexpr (sizeof...(Args) != 0)
            return ConsensusHandler<T, Args...>(obj);
        else
            static_assert(FalseType<T>, "Unhandled type");

        return Res::Err("(%s): Unhandled type", __func__);
    }

public:
    CCustomTxApplyVisitor(const CTransaction &tx,
                          const uint32_t height,
                          const CCoinsViewCache &coins,
                          CCustomCSView &mnview,
                          const Consensus::Params &consensus,
                          const uint64_t time,
                          const uint32_t txn)
        : tx(tx), height(height), coins(coins), mnview(mnview),  consensus(consensus), time(time), txn(txn) {}

    template<typename T>
    Res operator()(const T& obj) const {

        return ConsensusHandler<T,
                CAccountsConsensus,
                CGovernanceConsensus,
                CICXOrdersConsensus,
                CLoansConsensus,
                CMasternodesConsensus,
                COraclesConsensus,
                CPoolPairsConsensus,
                CProposalsConsensus,
                CSmartContractsConsensus,
                CTokensConsensus,
                CVaultsConsensus
        >(obj);
    }

    Res operator()(const CCustomTxMessageNone &) const {
        return Res::Ok();
    }
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

void PopulateVaultHistoryData(CHistoryWriters *writers,
                              CAccountsHistoryWriter &view,
                              const CCustomTxMessage &txMessage,
                              const CustomTxType txType,
                              const uint32_t height,
                              const uint32_t txn,
                              const uint256 &txid) {
    if (txType == CustomTxType::Vault) {
        auto obj          = std::get<CVaultMessage>(txMessage);
        writers->schemeID = obj.schemeId;
        view.vaultID      = txid;
    } else if (txType == CustomTxType::CloseVault) {
        auto obj     = std::get<CCloseVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::UpdateVault) {
        auto obj     = std::get<CUpdateVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
        if (!obj.schemeId.empty()) {
            writers->schemeID = obj.schemeId;
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
        writers->globalLoanScheme.identifier = obj.identifier;
        writers->globalLoanScheme.ratio      = obj.ratio;
        writers->globalLoanScheme.rate       = obj.rate;
        if (!obj.updateHeight) {
            writers->globalLoanScheme.schemeCreationTxid = txid;
        } else {
            writers->vaultView->ForEachGlobalScheme(
                [&writers](const VaultGlobalSchemeKey &key, CLazySerialize<VaultGlobalSchemeValue> value) {
                    if (value.get().loanScheme.identifier != writers->globalLoanScheme.identifier) {
                        return true;
                    }
                    writers->globalLoanScheme.schemeCreationTxid = key.schemeCreationTxid;
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
                  uint32_t txn,
                  CHistoryWriters *writers) {
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
    CAccountsHistoryWriter view(mnview, height, txn, tx.GetHash(), uint8_t(txType), writers);
    if ((res = CustomMetadataParse(height, consensus, metadata, txMessage))) {
        if (pvaultHistoryDB && writers) {
            PopulateVaultHistoryData(writers, view, txMessage, txType, height, txn, tx.GetHash());
        }
        res = CustomTxVisit(view, coins, tx, height, consensus, txMessage, time, txn);

        if (res && canSpend && txType == CustomTxType::UpdateMasternode) {
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
            if (writers) {
                writers->AddFeeBurn(tx.vout[0].scriptPubKey, tx.vout[0].nValue);
            }
        }

        if (txType == CustomTxType::CreateCfp || txType == CustomTxType::CreateVoc) {
            // burn fee_burn_pct of creation fee, the rest is distributed among voting masternodes
            CDataStructureV0 burnPctKey{
                AttributeTypes::Governance, GovernanceIDs::Proposals, GovernanceKeys::FeeBurnPct};

            auto attributes = view.GetAttributes();
            assert(attributes);

            auto burnFee = MultiplyAmounts(tx.vout[0].nValue, attributes->GetValue(burnPctKey, COIN / 2));
            if (writers) {
                writers->AddFeeBurn(tx.vout[0].scriptPubKey, burnFee);
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
                 "AccountChange: txid=%s fund=%s change=%s\n",
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
             "AccountChange: txid=%s fund=%s change=%s\n",
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

bool OraclePriceFeed(CCustomCSView &view, const CTokenCurrencyPair &priceFeed) {
    // Allow hard coded DUSD/USD
    if (priceFeed.first == "DUSD" && priceFeed.second == "USD") {
        return true;
    }
    bool found = false;
    view.ForEachOracle([&](const COracleId &, COracle oracle) {
        return !(found = oracle.SupportsPair(priceFeed.first, priceFeed.second));
    });
    return found;
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
