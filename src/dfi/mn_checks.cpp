// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/accountshistory.h>
#include <dfi/consensus/accounts.h>
#include <dfi/consensus/governance.h>
#include <dfi/consensus/icxorders.h>
#include <dfi/consensus/loans.h>
#include <dfi/consensus/masternodes.h>
#include <dfi/consensus/oracles.h>
#include <dfi/consensus/poolpairs.h>
#include <dfi/consensus/proposals.h>
#include <dfi/consensus/smartcontracts.h>
#include <dfi/consensus/tokens.h>
#include <dfi/consensus/vaults.h>
#include <dfi/consensus/xvm.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/mn_checks.h>
#include <dfi/vaulthistory.h>
#include <ffi/ffihelpers.h>

#include <ain_rs_exports.h>
#include <core_io.h>
#include <ffi/cxx.h>
#include <index/txindex.h>
#include <txmempool.h>
#include <validation.h>

#include <algorithm>

extern std::string ScriptToString(const CScript &script);

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
        case CustomTxType::TransferDomain:
            return CTransferDomainMessage{};
        case CustomTxType::EvmTx:
            return CEvmTxMessage{};
        case CustomTxType::None:
            return CCustomTxMessageNone{};
    }
    return CCustomTxMessageNone{};
}

template <typename... T>
constexpr bool FalseType = false;

template <typename T>
constexpr bool IsOneOf() {
    return false;
}

template <typename T, typename T1, typename... Args>
constexpr bool IsOneOf() {
    return std::is_same_v<T, T1> || IsOneOf<T, Args...>();
}

class CCustomMetadataParseVisitor {
    uint32_t height;
    const Consensus::Params &consensus;
    const std::vector<unsigned char> &metadata;

    Res IsHardforkEnabled(const uint32_t startHeight) const {
        const std::unordered_map<int, std::string> hardforks = {
            {consensus.DF1AMKHeight,                  "called before AMK height"                },
            {consensus.DF2BayfrontHeight,             "called before Bayfront height"           },
            {consensus.DF4BayfrontGardensHeight,      "called before Bayfront Gardens height"   },
            {consensus.DF8EunosHeight,                "called before Eunos height"              },
            {consensus.DF10EunosPayaHeight,           "called before EunosPaya height"          },
            {consensus.DF11FortCanningHeight,         "called before FortCanning height"        },
            {consensus.DF14FortCanningHillHeight,     "called before FortCanningHill height"    },
            {consensus.DF15FortCanningRoadHeight,     "called before FortCanningRoad height"    },
            {consensus.DF19FortCanningEpilogueHeight, "called before FortCanningEpilogue height"},
            {consensus.DF20GrandCentralHeight,        "called before GrandCentral height"       },
            {consensus.DF22MetachainHeight,           "called before Metachain height"          },
        };
        if (startHeight && height < startHeight) {
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

    template <typename T>
    Res EnabledAfter() const {
        if constexpr (IsOneOf<T,
                              CCreateTokenMessage,
                              CUpdateTokenPreAMKMessage,
                              CUtxosToAccountMessage,
                              CAccountToUtxosMessage,
                              CAccountToAccountMessage,
                              CMintTokensMessage>()) {
            return IsHardforkEnabled(consensus.DF1AMKHeight);
        } else if constexpr (IsOneOf<T,
                                     CUpdateTokenMessage,
                                     CPoolSwapMessage,
                                     CLiquidityMessage,
                                     CRemoveLiquidityMessage,
                                     CCreatePoolPairMessage,
                                     CUpdatePoolPairMessage,
                                     CGovernanceMessage>()) {
            return IsHardforkEnabled(consensus.DF2BayfrontHeight);
        } else if constexpr (IsOneOf<T,
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
                                     CICXCloseOfferMessage>()) {
            return IsHardforkEnabled(consensus.DF8EunosHeight);
        } else if constexpr (IsOneOf<T,
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
                                     CGovernanceHeightMessage>()) {
            return IsHardforkEnabled(consensus.DF11FortCanningHeight);
        } else if constexpr (IsOneOf<T, CAnyAccountsToAccountsMessage>()) {
            return IsHardforkEnabled(consensus.DF4BayfrontGardensHeight);
        } else if constexpr (IsOneOf<T, CSmartContractMessage>()) {
            return IsHardforkEnabled(consensus.DF14FortCanningHillHeight);
        } else if constexpr (IsOneOf<T, CLoanPaybackLoanV2Message, CFutureSwapMessage>()) {
            return IsHardforkEnabled(consensus.DF15FortCanningRoadHeight);
        } else if constexpr (IsOneOf<T, CPaybackWithCollateralMessage>()) {
            return IsHardforkEnabled(consensus.DF19FortCanningEpilogueHeight);
        } else if constexpr (IsOneOf<T,
                                     CUpdateMasterNodeMessage,
                                     CBurnTokensMessage,
                                     CCreateProposalMessage,
                                     CProposalVoteMessage,
                                     CGovernanceUnsetMessage>()) {
            return IsHardforkEnabled(consensus.DF20GrandCentralHeight);
        } else if constexpr (IsOneOf<T, CTransferDomainMessage, CEvmTxMessage>()) {
            return IsHardforkEnabled(consensus.DF22MetachainHeight);
        } else if constexpr (IsOneOf<T, CCreateMasterNodeMessage, CResignMasterNodeMessage>()) {
            return Res::Ok();
        } else {
            static_assert(FalseType<T>, "Unhandled type");
        }
    }

    template <typename T>
    Res DisabledAfter() const {
        if constexpr (IsOneOf<T, CUpdateTokenPreAMKMessage>()) {
            return IsHardforkEnabled(consensus.DF2BayfrontHeight) ? Res::Err("called after Bayfront height")
                                                                  : Res::Ok();
        }

        return Res::Ok();
    }

    template <typename T>
    Res operator()(T &obj) const {
        auto res = EnabledAfter<T>();
        if (!res) {
            return res;
        }

        res = DisabledAfter<T>();
        if (!res) {
            return res;
        }

        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> obj;
        if (!ss.empty()) {
            return Res::Err("deserialization failed: excess %d bytes", ss.size());
        }

        return Res::Ok();
    }

    Res operator()(CCustomTxMessageNone &) const { return Res::Ok(); }
};

// -- -- -- -- -- -- -- -DONE

class CCustomTxApplyVisitor {
    BlockContext &blockCtx;
    const TransactionContext &txCtx;

    template <typename T, typename T1, typename... Args>
    Res ConsensusHandler(const T &obj) const {
        static_assert(std::is_base_of_v<CCustomTxVisitor, T1>, "CCustomTxVisitor base required");

        if constexpr (std::is_invocable_v<T1, T>) {
            return T1{blockCtx, txCtx}(obj);
        } else if constexpr (sizeof...(Args) != 0) {
            return ConsensusHandler<T, Args...>(obj);
        } else {
            static_assert(FalseType<T>, "Unhandled type");
        }

        return Res::Err("(%s): Unhandled type", __func__);
    }

public:
    CCustomTxApplyVisitor(BlockContext &blockCtx, const TransactionContext &txCtx)

        : blockCtx(blockCtx),
          txCtx(txCtx) {}

    template <typename T>
    Res operator()(const T &obj) const {
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
                                CVaultsConsensus,
                                CXVMConsensus>(obj);
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
        return Res::Err("%s unexpected error", __func__);
    }
}

bool IsDisabledTx(uint32_t height, CustomTxType type, const Consensus::Params &consensus) {
    // All the heights that are involved in disabled Txs
    auto fortCanningParkHeight = static_cast<uint32_t>(consensus.DF13FortCanningParkHeight);
    auto fortCanningHillHeight = static_cast<uint32_t>(consensus.DF14FortCanningHillHeight);

    if (height < fortCanningParkHeight) {
        return false;
    }

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

    return false;
}

bool IsDisabledTx(uint32_t height, const CTransaction &tx, const Consensus::Params &consensus) {
    TBytes dummy;
    auto txType = GuessCustomTxType(tx, dummy);
    return IsDisabledTx(height, txType, consensus);
}

Res CustomTxVisit(const CCustomTxMessage &txMessage, BlockContext &blockCtx, const TransactionContext &txCtx) {
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    const auto &tx = txCtx.GetTransaction();

    if (IsDisabledTx(height, tx, consensus)) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "Disabled custom transaction");
    }

    const auto isEvmEnabledForBlock = blockCtx.GetEVMEnabledForBlock();
    const auto &evmTemplate = blockCtx.GetEVMTemplate();

    if (!evmTemplate && isEvmEnabledForBlock) {
        std::string minerAddress{};
        blockCtx.SetEVMTemplate(CScopedTemplate::Create(height, minerAddress, 0u, time, 0));
        if (!evmTemplate) {
            return Res::Err("Failed to create queue");
        }
    }

    try {
        auto res = std::visit(CCustomTxApplyVisitor(blockCtx, txCtx), txMessage);
        return res;
    } catch (const std::bad_variant_access &e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("%s unexpected error", __func__);
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
                              const TransactionContext &txCtx) {
    const auto height = txCtx.GetHeight();
    const auto &txid = txCtx.GetTransaction().GetHash();
    const auto txn = txCtx.GetTxn();

    if (txType == CustomTxType::Vault) {
        auto obj = std::get<CVaultMessage>(txMessage);
        writers.schemeID = obj.schemeId;
        view.vaultID = txid;
    } else if (txType == CustomTxType::CloseVault) {
        auto obj = std::get<CCloseVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
    } else if (txType == CustomTxType::UpdateVault) {
        auto obj = std::get<CUpdateVaultMessage>(txMessage);
        view.vaultID = obj.vaultId;
        if (!obj.schemeId.empty()) {
            writers.schemeID = obj.schemeId;
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
        writers.globalLoanScheme.identifier = obj.identifier;
        writers.globalLoanScheme.ratio = obj.ratio;
        writers.globalLoanScheme.rate = obj.rate;
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

Res ApplyCustomTx(BlockContext &blockCtx, TransactionContext &txCtx) {
    auto &mnview = blockCtx.GetView();
    const auto isEvmEnabledForBlock = blockCtx.GetEVMEnabledForBlock();
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto metadataValidation = txCtx.GetMetadataValidation();
    const auto &tx = txCtx.GetTransaction();
    const auto &txn = txCtx.GetTxn();

    auto r = Res::Ok();
    if (tx.IsCoinBase() && height > 0) {  // genesis contains custom coinbase txs
        return r;
    }

    const auto txType = txCtx.GetTxType();
    auto attributes = mnview.GetAttributes();

    if ((txType == CustomTxType::EvmTx || txType == CustomTxType::TransferDomain) && !isEvmEnabledForBlock) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "EVM is not enabled on this block");
    }

    // Check OP_RETURN sizes
    const auto opReturnLimits = OpReturnLimits::From(height, consensus, *attributes);
    if (opReturnLimits.shouldEnforce) {
        if (r = opReturnLimits.Validate(tx, txType); !r) {
            return r;
        }
    }

    if (txType == CustomTxType::None) {
        return r;
    }

    if (metadataValidation && txType == CustomTxType::Reject) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "Invalid custom transaction");
    }

    CAccountsHistoryWriter view(mnview, height, txn, tx.GetHash(), uint8_t(txType));

    auto &[res, txMessage] = txCtx.GetTxMessage();

    if (res) {
        if (mnview.GetHistoryWriters().GetVaultView()) {
            PopulateVaultHistoryData(mnview.GetHistoryWriters(), view, txMessage, txType, txCtx);
        }

        // TX changes are applied on a different view which
        // is then used to create the TX undo based on the
        // difference between the original and the copy.
        BlockContext blockCtxTxView{blockCtx, view};

        res = CustomTxVisit(txMessage, blockCtxTxView, txCtx);

        if (res) {
            // Track burn fee
            if (txType == CustomTxType::CreateToken || txType == CustomTxType::CreateMasternode) {
                mnview.GetHistoryWriters().AddFeeBurn(tx.vout[0].scriptPubKey, tx.vout[0].nValue);
            }

            if (txType == CustomTxType::CreateCfp || txType == CustomTxType::CreateVoc) {
                // burn fee_burn_pct of creation fee, the rest is distributed among voting masternodes
                CDataStructureV0 burnPctKey{
                    AttributeTypes::Governance, GovernanceIDs::Proposals, GovernanceKeys::FeeBurnPct};

                auto attributes = view.GetAttributes();

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
        if (height >= static_cast<uint32_t>(consensus.DF6DakotaHeight)) {
            res.code |= CustomTxErrCodes::Fatal;
            return res;
        }

        // Below DF6, only the following are fatal:
        // - mint
        // - account to utxo
        // - explicit skip lists
        if (IsBelowDF6MintTokenOrAccountToUtxos(txType, height)) {
            if (ShouldReturnNonFatalError(tx, height)) {
                return res;
            }
            res.code |= CustomTxErrCodes::Fatal;
        }
        return res;
    }

    // construct undo
    auto &flushable = view.GetStorage();
    auto undo = CUndo::Construct(mnview.GetStorage(), flushable.GetRaw());
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
    if (height >= consensusParams.DF6DakotaHeight) {
        return Res::Err("Old anchor TX type after Dakota fork. Height %d", height);
    }

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
    if (height >= consensusParams.DF1AMKHeight) {
        const auto cbValues = tx.GetValuesOut();
        if (cbValues.size() != 1 || cbValues.begin()->first != DCT_ID{0}) {
            return Res::ErrDbg("bad-ar-wrong-tokens", "anchor reward should be payed only in Defi coins");
        }

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

    CTxDestination destination = FromOrDefaultKeyIDToDestination(
        finMsg.rewardKeyID, TxDestTypeToKeyType(finMsg.rewardKeyType), KeyType::MNOwnerKeyType);
    if (!IsValidDestination(destination) || tx.vout[1].scriptPubKey != GetScriptForDestination(destination)) {
        return Res::ErrDbg("bad-ar-dest", "anchor pay destination is incorrect");
    }

    if (finMsg.currentTeam != mnview.GetCurrentTeam()) {
        return Res::ErrDbg("bad-ar-curteam", "anchor wrong current team");
    }

    if (finMsg.nextTeam != mnview.CalcNextTeam(height, prevStakeModifier)) {
        return Res::ErrDbg("bad-ar-nextteam", "anchor wrong next team");
    }
    mnview.SetTeam(finMsg.nextTeam);
    if (height >= consensusParams.DF1AMKHeight) {
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
    if (height < consensusParams.DF6DakotaHeight) {
        return Res::Err("New anchor TX type before Dakota fork. Height %d", height);
    }

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
        return Res::Err("anchor sigs (%d) < min quorum (%) ", finMsg.sigs.size(), quorum);
    }
    if (uniqueKeys < quorum) {
        return Res::Err("anchor unique keys (%d) < min quorum (%) ", uniqueKeys, quorum);
    }

    // Make sure anchor block height and hash exist in chain.
    auto *anchorIndex = ::ChainActive()[finMsg.anchorHeight];
    if (!anchorIndex) {
        return Res::Err("Active chain does not contain block height %d. Chain height %d",
                        finMsg.anchorHeight,
                        ::ChainActive().Height());
    }
    if (anchorIndex->GetBlockHash() != finMsg.dfiBlockHash) {
        return Res::Err("Anchor and blockchain mismatch at height %d. Expected %s found %s",
                        finMsg.anchorHeight,
                        anchorIndex->GetBlockHash().ToString(),
                        finMsg.dfiBlockHash.ToString());
    }
    // check reward sum
    const auto cbValues = tx.GetValuesOut();
    if (cbValues.size() != 1 || cbValues.begin()->first != DCT_ID{0}) {
        return Res::Err("anchor reward should be paid in DFI only");
    }

    const auto anchorReward = mnview.GetCommunityBalance(CommunityAccountType::AnchorReward);
    if (cbValues.begin()->second != anchorReward) {
        return Res::Err("anchor pays wrong amount (actual=%d vs expected=%d)", cbValues.begin()->second, anchorReward);
    }

    CTxDestination destination;
    if (height < consensusParams.DF22MetachainHeight) {
        destination = FromOrDefaultKeyIDToDestination(
            finMsg.rewardKeyID, TxDestTypeToKeyType(finMsg.rewardKeyType), KeyType::MNOwnerKeyType);
    } else {
        destination = FromOrDefaultKeyIDToDestination(
            finMsg.rewardKeyID, TxDestTypeToKeyType(finMsg.rewardKeyType), KeyType::MNRewardKeyType);
    }
    if (!IsValidDestination(destination) || tx.vout[1].scriptPubKey != GetScriptForDestination(destination)) {
        return Res::ErrDbg("bad-ar-dest", "anchor pay destination is incorrect");
    }

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

std::vector<DCT_ID> CPoolSwap::CalculateSwaps(CCustomCSView &view, const Consensus::Params &consensus, bool testOnly) {
    std::vector<std::vector<DCT_ID> > poolPaths = CalculatePoolPaths(view);

    // Record best pair
    std::pair<std::vector<DCT_ID>, CAmount> bestPair{{}, -1};

    // Loop through all common pairs
    for (const auto &path : poolPaths) {
        // Test on copy of view
        CCustomCSView dummy(view);

        // Execute pool path
        auto res = ExecuteSwap(dummy, path, consensus, testOnly);

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
                 fromIt = fromPoolsID.equal_range(fromIt->first).second) {
                // Loop through to pool multimap on unique keys only
                for (auto toIt = toPoolsID.begin(); toIt != toPoolsID.end();
                     toIt = toPoolsID.equal_range(toIt->first).second) {
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
Res CPoolSwap::ExecuteSwap(CCustomCSView &view,
                           std::vector<DCT_ID> poolIDs,
                           const Consensus::Params &consensus,
                           bool testOnly) {
    Res poolResult = Res::Ok();
    // No composite swap allowed before Fort Canning
    if (height < static_cast<uint32_t>(consensus.DF11FortCanningHeight) && !poolIDs.empty()) {
        poolIDs.clear();
    }

    if (obj.amountFrom <= 0) {
        return Res::Err("Input amount should be positive");
    }

    if (height >= static_cast<uint32_t>(consensus.DF14FortCanningHillHeight) && poolIDs.size() > MAX_POOL_SWAPS) {
        return Res::Err(
            strprintf("Too many pool IDs provided, max %d allowed, %d provided", MAX_POOL_SWAPS, poolIDs.size()));
    }

    // Single swap if no pool IDs provided
    auto poolPrice = PoolPrice::getMaxValid();
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
            if (!pool) {
                return Res::Err("Cannot find the pool pair.");
            }
        }

        // Check if last pool swap
        bool lastSwap = i + 1 == poolIDs.size();

        const auto swapAmount = swapAmountResult;

        if (height >= static_cast<uint32_t>(consensus.DF14FortCanningHillHeight) && lastSwap) {
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
        auto &balances = dexBalances[currentID];
        auto forward = swapAmount.nTokenId == pool->idTokenA;

        auto &totalTokenA = forward ? balances.totalTokenA : balances.totalTokenB;
        auto &totalTokenB = forward ? balances.totalTokenB : balances.totalTokenA;

        const auto &reserveAmount = forward ? pool->reserveA : pool->reserveB;
        const auto &blockCommission = forward ? pool->blockCommissionA : pool->blockCommissionB;

        const auto initReserveAmount = reserveAmount;
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
                if (testOnly) {
                    return Res::Ok();
                }

                auto res = view.SetPoolPair(currentID, height, *pool);
                if (!res) {
                    return res;
                }

                CCustomCSView intermediateView(view);
                // hide interemidiate swaps
                auto &subView = i == 0 ? view : intermediateView;
                res = subView.SubBalance(obj.from, swapAmount);
                if (!res) {
                    return res;
                }
                intermediateView.Flush();

                auto &addView = lastSwap ? view : intermediateView;
                if (height >= static_cast<uint32_t>(consensus.DF20GrandCentralHeight)) {
                    res = addView.AddBalance(lastSwap ? (obj.to.empty() ? obj.from : obj.to) : obj.from,
                                             swapAmountResult);
                } else {
                    res = addView.AddBalance(lastSwap ? obj.to : obj.from, swapAmountResult);
                }
                if (!res) {
                    return res;
                }

                if (LogAcceptCategory(BCLog::SWAPRESULT) && lastSwap) {
                    LogPrint(BCLog::SWAPRESULT,
                             "SwapResult: height=%d destination=%s result=%s\n",
                             height,
                             ScriptToString(obj.to),
                             swapAmountResult.ToString());
                }

                intermediateView.Flush();

                const auto token = view.GetToken("DUSD");

                // burn the dex in amount
                if (dexfeeInAmount.nValue > 0) {
                    res = view.AddBalance(consensus.burnAddress, dexfeeInAmount);
                    if (!res) {
                        return res;
                    }
                    totalTokenA.feeburn += dexfeeInAmount.nValue;
                }

                // burn the dex out amount
                if (dexfeeOutAmount.nValue > 0) {
                    res = view.AddBalance(consensus.burnAddress, dexfeeOutAmount);
                    if (!res) {
                        return res;
                    }
                    totalTokenB.feeburn += dexfeeOutAmount.nValue;
                }

                totalTokenA.swaps += (reserveAmount - initReserveAmount);
                totalTokenA.commissions += (blockCommission - initBlockCommission);

                if (lastSwap && obj.to == consensus.burnAddress) {
                    totalTokenB.feeburn += swapAmountResult.nValue;
                }

                return res;
            },
            static_cast<int>(height));

        if (!poolResult) {
            return poolResult;
        }
    }

    if (height >= static_cast<uint32_t>(consensus.DF20GrandCentralHeight)) {
        if (swapAmountResult.nTokenId != obj.idTokenTo) {
            return Res::Err("Final swap output is not same as idTokenTo");
        }
    }

    // Reject if price paid post-swap above max price provided
    if (height >= static_cast<uint32_t>(consensus.DF11FortCanningHeight) && !obj.maxPrice.isAboveValid()) {
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
                    const Consensus::Params &consensus,
                    bool forceLoanSwap) {
    CPoolSwapMessage obj;

    obj.from = from;
    obj.to = to;
    obj.idTokenFrom = tokenId;
    obj.idTokenTo = DCT_ID{0};
    obj.amountFrom = amount;
    obj.maxPrice = PoolPrice::getMaxValid();

    auto poolSwap = CPoolSwap(obj, height);
    auto token = mnview.GetToken(tokenId);
    if (!token) {
        return Res::Err("Cannot find token with id %s!", tokenId.ToString());
    }

    // TODO: Optimize double look up later when first token is DUSD.
    auto dUsdToken = mnview.GetToken("DUSD");
    if (!dUsdToken) {
        return Res::Err("Cannot find token DUSD");
    }

    const auto attributes = mnview.GetAttributes();
    CDataStructureV0 directBurnKey{AttributeTypes::Param, ParamIDs::DFIP2206A, DFIPKeys::DUSDInterestBurn};

    // Direct swap from DUSD to DFI as defined in the CPoolSwapMessage.
    if (tokenId == dUsdToken->first) {
        if (to == consensus.burnAddress && !forceLoanSwap && attributes->GetValue(directBurnKey, false)) {
            // direct burn dUSD
            CTokenAmount dUSD{dUsdToken->first, amount};

            if (auto res = mnview.SubBalance(from, dUSD); !res) {
                return res;
            }

            return mnview.AddBalance(to, dUSD);
        } else {
            // swap dUSD -> DFI and burn DFI
            return poolSwap.ExecuteSwap(mnview, {}, consensus);
        }
    }

    auto pooldUSDDFI = mnview.GetPoolPair(dUsdToken->first, DCT_ID{0});
    if (!pooldUSDDFI) {
        return Res::Err("Cannot find pool pair DUSD-DFI!");
    }

    auto poolTokendUSD = mnview.GetPoolPair(tokenId, dUsdToken->first);
    if (!poolTokendUSD) {
        return Res::Err("Cannot find pool pair %s-DUSD!", token->symbol);
    }

    if (to == consensus.burnAddress && !forceLoanSwap && attributes->GetValue(directBurnKey, false)) {
        obj.idTokenTo = dUsdToken->first;

        // swap tokenID -> dUSD and burn dUSD
        return poolSwap.ExecuteSwap(mnview, {}, consensus);
    } else {
        // swap tokenID -> dUSD -> DFI and burn DFI
        return poolSwap.ExecuteSwap(mnview, {poolTokendUSD->first, pooldUSDDFI->first}, consensus);
    }
}

bool IsVaultPriceValid(CCustomCSView &mnview, const CVaultId &vaultId, uint32_t height) {
    if (auto collaterals = mnview.GetVaultCollaterals(vaultId)) {
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
    }

    if (auto loans = mnview.GetLoanTokens(vaultId)) {
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
    }
    return true;
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

bool CheckOPReturnSize(const CScript &scriptPubKey, const uint32_t opreturnSize) {
    opcodetype opcode;
    auto pc = scriptPubKey.begin();
    if (scriptPubKey.GetOp(pc, opcode) && opcode == OP_RETURN && scriptPubKey.size() > opreturnSize) {
        return false;
    }
    return true;
}

bool IsRegtestNetwork() {
    return Params().NetworkIDString() == CBaseChainParams::REGTEST;
}
bool IsTestNetwork() {
    return Params().NetworkIDString() == CBaseChainParams::TESTNET ||
           Params().NetworkIDString() == CBaseChainParams::CHANGI ||
           Params().NetworkIDString() == CBaseChainParams::DEVNET;
}

bool IsMainNetwork() {
    return Params().NetworkIDString() == CBaseChainParams::MAIN;
}

UniValue EVM::ToUniValue() const {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", static_cast<uint64_t>(version));
    obj.pushKV("blockHash", "0x" + blockHash);
    obj.pushKV("burntFee", burntFee);
    obj.pushKV("priorityFee", priorityFee);
    obj.pushKV("beneficiary", "0x" + beneficiary);
    return obj;
}

UniValue XVM::ToUniValue() const {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", static_cast<uint64_t>(version));
    obj.pushKV("evm", evm.ToUniValue());
    return obj;
}

CScript XVM::ToScript() const {
    CDataStream metadata(SER_NETWORK, PROTOCOL_VERSION);
    metadata << *this;

    CScript script;
    script << OP_RETURN << ToByteVector(metadata);
    return script;
}

ResVal<XVM> XVM::TryFrom(const CScript &scriptPubKey) {
    opcodetype opcode;
    auto pc = scriptPubKey.begin();
    if (!scriptPubKey.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return Res::Err("Coinbase XVM: OP_RETURN expected");
    }

    std::vector<unsigned char> metadata;
    if (!scriptPubKey.GetOp(pc, opcode, metadata) ||
        (opcode > OP_PUSHDATA1 && opcode != OP_PUSHDATA2 && opcode != OP_PUSHDATA4)) {
        return Res::Err("Coinbase XVM: OP_PUSHDATA expected");
    }

    XVM obj;
    try {
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> obj;
    } catch (...) {
        return Res::Err("Coinbase XVM: Deserialization failed");
    }
    return {obj, Res::Ok()};
}

OpReturnLimits OpReturnLimits::Default() {
    return OpReturnLimits{
        false,
        MAX_OP_RETURN_CORE_ACCEPT,
        MAX_OP_RETURN_DVM_ACCEPT,
        MAX_OP_RETURN_EVM_ACCEPT,
    };
}

struct OpReturnLimitsKeys {
    CDataStructureV0 coreKey{AttributeTypes::Rules, RulesIDs::TXRules, RulesKeys::CoreOPReturn};
    CDataStructureV0 dvmKey{AttributeTypes::Rules, RulesIDs::TXRules, RulesKeys::DVMOPReturn};
    CDataStructureV0 evmKey{AttributeTypes::Rules, RulesIDs::TXRules, RulesKeys::EVMOPReturn};
};

OpReturnLimits OpReturnLimits::From(const uint64_t height,
                                    const Consensus::Params &consensus,
                                    const ATTRIBUTES &attributes) {
    OpReturnLimitsKeys k{};
    auto item = OpReturnLimits::Default();
    item.shouldEnforce = height >= static_cast<uint64_t>(consensus.DF22MetachainHeight);
    item.coreSizeBytes = attributes.GetValue(k.coreKey, item.coreSizeBytes);
    item.dvmSizeBytes = attributes.GetValue(k.dvmKey, item.dvmSizeBytes);
    item.evmSizeBytes = attributes.GetValue(k.evmKey, item.evmSizeBytes);
    return item;
}

void OpReturnLimits::SetToAttributesIfNotExists(ATTRIBUTES &attrs) const {
    OpReturnLimitsKeys k{};
    if (!attrs.CheckKey(k.coreKey)) {
        attrs.SetValue(k.coreKey, coreSizeBytes);
    }
    if (!attrs.CheckKey(k.dvmKey)) {
        attrs.SetValue(k.dvmKey, dvmSizeBytes);
    }
    if (!attrs.CheckKey(k.evmKey)) {
        attrs.SetValue(k.evmKey, evmSizeBytes);
    }
}

Res OpReturnLimits::Validate(const CTransaction &tx, const CustomTxType txType) const {
    auto err = [](const std::string area, const int voutIndex) {
        return Res::ErrCode(CustomTxErrCodes::Fatal, "OP_RETURN size check: vout[%d] %s failure", voutIndex, area);
    };

    // Check core OP_RETURN size on vout[0]
    if (txType == CustomTxType::EvmTx) {
        if (!CheckOPReturnSize(tx.vout[0].scriptPubKey, evmSizeBytes)) {
            return err("EVM", 0);
        }
    } else if (txType != CustomTxType::None) {
        if (!CheckOPReturnSize(tx.vout[0].scriptPubKey, dvmSizeBytes)) {
            return err("DVM", 0);
        }
    } else if (!CheckOPReturnSize(tx.vout[0].scriptPubKey, coreSizeBytes)) {
        return err("Core", 0);
    }
    // Check core OP_RETURN size on vout[1] and higher outputs
    for (size_t i{1}; i < tx.vout.size(); ++i) {
        if (!CheckOPReturnSize(tx.vout[i].scriptPubKey, coreSizeBytes)) {
            return err("Core", i);
        }
    }
    return Res::Ok();
}

TransferDomainConfig TransferDomainConfig::Default() {
    return TransferDomainConfig{
        true,
        true,
        {XVmAddressFormatTypes::Bech32, XVmAddressFormatTypes::PkHash},
        {XVmAddressFormatTypes::Erc55},
        {XVmAddressFormatTypes::Bech32, XVmAddressFormatTypes::PkHash},
        {XVmAddressFormatTypes::Erc55},
        {XVmAddressFormatTypes::Bech32ProxyErc55, XVmAddressFormatTypes::PkHashProxyErc55},
        true,
        true,
        false,
        false,
        {},
        {}
    };
}

struct TransferDomainConfigKeys {
    CDataStructureV0 dvm_to_evm_enabled{AttributeTypes::Transfer, TransferIDs::DVMToEVM, TransferKeys::TransferEnabled};
    CDataStructureV0 dvm_to_evm_src_formats{AttributeTypes::Transfer, TransferIDs::DVMToEVM, TransferKeys::SrcFormats};
    CDataStructureV0 dvm_to_evm_dest_formats{AttributeTypes::Transfer,
                                             TransferIDs::DVMToEVM,
                                             TransferKeys::DestFormats};
    CDataStructureV0 dvm_to_evm_dat_enabled{AttributeTypes::Transfer, TransferIDs::DVMToEVM, TransferKeys::DATEnabled};
    CDataStructureV0 dvm_to_evm_native_enabled{AttributeTypes::Transfer,
                                               TransferIDs::DVMToEVM,
                                               TransferKeys::NativeEnabled};
    CDataStructureV0 evm_to_dvm_enabled{AttributeTypes::Transfer, TransferIDs::EVMToDVM, TransferKeys::TransferEnabled};
    CDataStructureV0 evm_to_dvm_src_formats{AttributeTypes::Transfer, TransferIDs::EVMToDVM, TransferKeys::SrcFormats};
    CDataStructureV0 evm_to_dvm_dest_formats{AttributeTypes::Transfer,
                                             TransferIDs::EVMToDVM,
                                             TransferKeys::DestFormats};
    CDataStructureV0 evm_to_dvm_auth_formats{AttributeTypes::Transfer,
                                             TransferIDs::EVMToDVM,
                                             TransferKeys::AuthFormats};
    CDataStructureV0 evm_to_dvm_native_enabled{AttributeTypes::Transfer,
                                               TransferIDs::EVMToDVM,
                                               TransferKeys::NativeEnabled};
    CDataStructureV0 evm_to_dvm_dat_enabled{AttributeTypes::Transfer, TransferIDs::EVMToDVM, TransferKeys::DATEnabled};
};

TransferDomainConfig TransferDomainConfig::From(const CCustomCSView &mnview) {
    TransferDomainConfigKeys k{};
    const auto attributes = mnview.GetAttributes();
    auto r = TransferDomainConfig::Default();

    r.dvmToEvmEnabled = attributes->GetValue(k.dvm_to_evm_enabled, r.dvmToEvmEnabled);
    r.dvmToEvmSrcAddresses = attributes->GetValue(k.dvm_to_evm_src_formats, r.dvmToEvmSrcAddresses);
    r.dvmToEvmDestAddresses = attributes->GetValue(k.dvm_to_evm_dest_formats, r.dvmToEvmDestAddresses);
    r.dvmToEvmNativeTokenEnabled = attributes->GetValue(k.dvm_to_evm_native_enabled, r.dvmToEvmNativeTokenEnabled);
    r.dvmToEvmDatEnabled = attributes->GetValue(k.dvm_to_evm_dat_enabled, r.dvmToEvmDatEnabled);

    r.evmToDvmEnabled = attributes->GetValue(k.evm_to_dvm_enabled, r.evmToDvmEnabled);
    r.evmToDvmSrcAddresses = attributes->GetValue(k.evm_to_dvm_src_formats, r.evmToDvmSrcAddresses);
    r.evmToDvmDestAddresses = attributes->GetValue(k.evm_to_dvm_dest_formats, r.evmToDvmDestAddresses);
    r.evmToDvmAuthFormats = attributes->GetValue(k.evm_to_dvm_auth_formats, r.evmToDvmAuthFormats);
    r.evmToDvmNativeTokenEnabled = attributes->GetValue(k.evm_to_dvm_native_enabled, r.evmToDvmNativeTokenEnabled);
    r.evmToDvmDatEnabled = attributes->GetValue(k.evm_to_dvm_dat_enabled, r.evmToDvmDatEnabled);

    return r;
}

void TransferDomainConfig::SetToAttributesIfNotExists(ATTRIBUTES &attrs) const {
    TransferDomainConfigKeys k{};
    if (!attrs.CheckKey(k.dvm_to_evm_enabled)) {
        attrs.SetValue(k.dvm_to_evm_enabled, dvmToEvmEnabled);
    }
    if (!attrs.CheckKey(k.dvm_to_evm_src_formats)) {
        attrs.SetValue(k.dvm_to_evm_src_formats, dvmToEvmSrcAddresses);
    }
    if (!attrs.CheckKey(k.dvm_to_evm_dest_formats)) {
        attrs.SetValue(k.dvm_to_evm_dest_formats, dvmToEvmDestAddresses);
    }
    if (!attrs.CheckKey(k.dvm_to_evm_native_enabled)) {
        attrs.SetValue(k.dvm_to_evm_native_enabled, dvmToEvmNativeTokenEnabled);
    }
    if (!attrs.CheckKey(k.dvm_to_evm_dat_enabled)) {
        attrs.SetValue(k.dvm_to_evm_dat_enabled, dvmToEvmDatEnabled);
    }

    if (!attrs.CheckKey(k.evm_to_dvm_enabled)) {
        attrs.SetValue(k.evm_to_dvm_enabled, evmToDvmEnabled);
    }
    if (!attrs.CheckKey(k.evm_to_dvm_src_formats)) {
        attrs.SetValue(k.evm_to_dvm_src_formats, evmToDvmSrcAddresses);
    }
    if (!attrs.CheckKey(k.evm_to_dvm_dest_formats)) {
        attrs.SetValue(k.evm_to_dvm_dest_formats, evmToDvmDestAddresses);
    }
    if (!attrs.CheckKey(k.evm_to_dvm_auth_formats)) {
        attrs.SetValue(k.evm_to_dvm_auth_formats, evmToDvmAuthFormats);
    }
    if (!attrs.CheckKey(k.evm_to_dvm_native_enabled)) {
        attrs.SetValue(k.evm_to_dvm_native_enabled, evmToDvmNativeTokenEnabled);
    }
    if (!attrs.CheckKey(k.evm_to_dvm_dat_enabled)) {
        attrs.SetValue(k.evm_to_dvm_dat_enabled, evmToDvmDatEnabled);
    }
}

TransactionContext::TransactionContext(const CCoinsViewCache &coins,
                                       const CTransaction &tx,
                                       const BlockContext &blockCtx,
                                       const uint32_t txn)
    : coins(coins),
      tx(tx),
      consensus(blockCtx.GetConsensus()),
      height(blockCtx.GetHeight()),
      time(blockCtx.GetTime()),
      txn(txn) {
    metadataValidation = height >= static_cast<uint32_t>(consensus.DF11FortCanningHeight);
}

const CCoinsViewCache &TransactionContext::GetCoins() const {
    return coins;
};

const CTransaction &TransactionContext::GetTransaction() const {
    return tx;
};

const Consensus::Params &TransactionContext::GetConsensus() const {
    return consensus;
};

uint32_t TransactionContext::GetHeight() const {
    return height;
};

uint64_t TransactionContext::GetTime() const {
    return time;
};

uint32_t TransactionContext::GetTxn() const {
    return txn;
};

CustomTxType TransactionContext::GetTxType() {
    if (!txType) {
        txType = GuessCustomTxType(tx, metadata, metadataValidation);
    }
    return *txType;
};

std::pair<Res, CCustomTxMessage> &TransactionContext::GetTxMessage() {
    if (!txMessageResult) {
        auto txMessage = customTypeToMessage(GetTxType());
        auto res = CustomMetadataParse(height, consensus, metadata, txMessage);
        txMessageResult = std::make_pair(res, txMessage);
    }
    return *txMessageResult;
};

bool TransactionContext::GetMetadataValidation() const {
    return metadataValidation;
}
