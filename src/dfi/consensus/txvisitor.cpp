// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <chainparams.h>
#include <coins.h>
#include <dfi/accounts.h>
#include <dfi/consensus/txvisitor.h>
#include <dfi/customtx.h>
#include <dfi/errors.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/icxorder.h>
#include <dfi/loan.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>
#include <validation.h>

constexpr std::string_view ERR_STRING_MIN_COLLATERAL_DFI_PCT =
    "At least 50%% of the minimum required collateral must be in DFI";
constexpr std::string_view ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT =
    "At least 50%% of the minimum required collateral must be in DFI or DUSD";

Res HasAuth(const CTransaction &tx,
            const CCoinsViewCache &coins,
            const CScript &auth,
            AuthStrategy strategy,
            AuthFlags::Type flags) {
    for (const auto &input : tx.vin) {
        const Coin &coin = coins.AccessCoin(input.prevout);
        if (coin.IsSpent()) {
            continue;
        }
        if (strategy == AuthStrategy::DirectPubKeyMatch) {
            if (coin.out.scriptPubKey == auth) {
                return Res::Ok();
            }
        } else if (strategy == AuthStrategy::Mapped) {
            std::vector<TBytes> vRet;
            const auto solution = Solver(coin.out.scriptPubKey, vRet);
            if (flags & AuthFlags::PKHashInSource && solution == txnouttype::TX_PUBKEYHASH) {
                auto it = input.scriptSig.begin();
                CPubKey pubkey(input.scriptSig.begin() + *it + 2, input.scriptSig.end());
                if (pubkey.Decompress()) {
                    const auto script = GetScriptForDestination(WitnessV16EthHash(pubkey));
                    const auto scriptOut = GetScriptForDestination(PKHash(pubkey));
                    if (script == auth && coin.out.scriptPubKey == scriptOut) {
                        return Res::Ok();
                    }
                }
            } else if (flags & AuthFlags::Bech32InSource && solution == txnouttype::TX_WITNESS_V0_KEYHASH) {
                CPubKey pubkey(input.scriptWitness.stack[1]);
                const auto scriptOut = GetScriptForDestination(WitnessV0KeyHash(pubkey));
                if (pubkey.Decompress()) {
                    auto script = GetScriptForDestination(WitnessV16EthHash(pubkey));
                    if (script == auth && coin.out.scriptPubKey == scriptOut) {
                        return Res::Ok();
                    }
                }
            }
        }
    }
    return DeFiErrors::InvalidAuth();
}

Res GetERC55AddressFromAuth(const CTransaction &tx, const CCoinsViewCache &coins, CScript &script) {
    for (const auto &input : tx.vin) {
        const Coin &coin = coins.AccessCoin(input.prevout);
        if (coin.IsSpent()) {
            continue;
        }

        std::vector<TBytes> vRet;
        const auto solution = Solver(coin.out.scriptPubKey, vRet);
        if (solution == txnouttype::TX_PUBKEYHASH) {
            auto it = input.scriptSig.begin();
            CPubKey pubkey(input.scriptSig.begin() + *it + 2, input.scriptSig.end());
            if (pubkey.Decompress()) {
                script = GetScriptForDestination(WitnessV16EthHash(pubkey));
                return Res::Ok();
            }
        } else if (solution == txnouttype::TX_WITNESS_V0_KEYHASH) {
            CPubKey pubkey(input.scriptWitness.stack[1]);
            const auto scriptOut = GetScriptForDestination(WitnessV0KeyHash(pubkey));
            if (pubkey.Decompress()) {
                script = GetScriptForDestination(WitnessV16EthHash(pubkey));
                return Res::Ok();
            }
        }
    }
    return DeFiErrors::InvalidAuth();
}

CCustomTxVisitor::CCustomTxVisitor(BlockContext &blockCtx, const TransactionContext &txCtx)
    : blockCtx(blockCtx),
      txCtx(txCtx) {}

Res CCustomTxVisitor::HasAuth(const CScript &auth) const {
    const auto &coins = txCtx.GetCoins();
    const auto &tx = txCtx.GetTransaction();
    return ::HasAuth(tx, coins, auth);
}

Res CCustomTxVisitor::HasCollateralAuth(const uint256 &collateralTx) const {
    const auto &coins = txCtx.GetCoins();
    const Coin &auth = coins.AccessCoin(COutPoint(collateralTx, 1));  // always n=1 output
    if (!HasAuth(auth.out.scriptPubKey)) {
        return Res::Err("tx must have at least one input from the owner");
    }
    return Res::Ok();
}

Res CCustomTxVisitor::HasFoundationAuth() const {
    auto &mnview = blockCtx.GetView();
    const auto &coins = txCtx.GetCoins();
    const auto &consensus = txCtx.GetConsensus();
    const auto &tx = txCtx.GetTransaction();

    auto members = consensus.foundationMembers;
    const auto attributes = mnview.GetAttributes();

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

Res CCustomTxVisitor::CheckCustomTx() const {
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto &tx = txCtx.GetTransaction();
    if (!IsRegtestNetwork() && static_cast<int>(height) < consensus.DF10EunosPayaHeight) {
        if (tx.vout.size() != 2) {
            return Res::Err("malformed tx vouts ((wrong number of vouts)");
        }
    }
    if (static_cast<int>(height) >= consensus.DF10EunosPayaHeight) {
        if (tx.vout[0].nValue != 0) {
            return Res::Err("malformed tx vouts, first vout must be OP_RETURN vout with value 0");
        }
    }
    return Res::Ok();
}

Res CCustomTxVisitor::TransferTokenBalance(DCT_ID id, CAmount amount, const CScript &from, const CScript &to) const {
    assert(!from.empty() || !to.empty());

    auto &mnview = blockCtx.GetView();

    CTokenAmount tokenAmount{id, amount};
    // if "from" not supplied it will only add balance on "to" address
    if (!from.empty()) {
        if (auto res = mnview.SubBalance(from, tokenAmount); !res) {
            return res;
        }
    }

    // if "to" not supplied it will only sub balance from "form" address
    if (!to.empty()) {
        if (auto res = mnview.AddBalance(to, tokenAmount); !res) {
            return res;
        }
    }
    return Res::Ok();
}

ResVal<CBalances> CCustomTxVisitor::MintedTokens(uint32_t mintingOutputsStart) const {
    const auto &tx = txCtx.GetTransaction();

    CBalances balances;
    for (uint32_t i = mintingOutputsStart; i < (uint32_t)tx.vout.size(); i++) {
        if (auto res = balances.Add(tx.vout[i].TokenAmount()); !res) {
            return res;
        }
    }
    return {balances, Res::Ok()};
}

Res CCustomTxVisitor::SetShares(const CScript &owner, const TAmounts &balances) const {
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();
    for (const auto &balance : balances) {
        auto token = mnview.GetToken(balance.first);
        if (token && token->IsPoolShare()) {
            const auto bal = mnview.GetBalance(owner, balance.first);
            if (bal.nValue == balance.second) {
                if (auto res = mnview.SetShare(balance.first, owner, height); !res) {
                    return res;
                }
            }
        }
    }
    return Res::Ok();
}

Res CCustomTxVisitor::DelShares(const CScript &owner, const TAmounts &balances) const {
    auto &mnview = blockCtx.GetView();
    for (const auto &kv : balances) {
        auto token = mnview.GetToken(kv.first);
        if (token && token->IsPoolShare()) {
            const auto balance = mnview.GetBalance(owner, kv.first);
            if (balance.nValue == 0) {
                if (auto res = mnview.DelShare(kv.first, owner); !res) {
                    return res;
                }
            }
        }
    }
    return Res::Ok();
}

// we need proxy view to prevent add/sub balance record
void CCustomTxVisitor::CalculateOwnerRewards(const CScript &owner) const {
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    CCustomCSView view(mnview);
    view.CalculateOwnerRewards(owner, height);
    view.Flush();
}

Res CCustomTxVisitor::SubBalanceDelShares(const CScript &owner, const CBalances &balance) const {
    auto &mnview = blockCtx.GetView();
    CalculateOwnerRewards(owner);
    auto res = mnview.SubBalances(owner, balance);
    if (!res) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, res.msg);
    }
    return DelShares(owner, balance.balances);
}

Res CCustomTxVisitor::AddBalanceSetShares(const CScript &owner, const CBalances &balance) const {
    auto &mnview = blockCtx.GetView();
    CalculateOwnerRewards(owner);
    if (auto res = mnview.AddBalances(owner, balance); !res) {
        return res;
    }
    return SetShares(owner, balance.balances);
}

Res CCustomTxVisitor::AddBalancesSetShares(const CAccounts &accounts) const {
    for (const auto &account : accounts) {
        if (auto res = AddBalanceSetShares(account.first, account.second); !res) {
            return res;
        }
    }
    return Res::Ok();
}

Res CCustomTxVisitor::SubBalancesDelShares(const CAccounts &accounts) const {
    for (const auto &account : accounts) {
        if (auto res = SubBalanceDelShares(account.first, account.second); !res) {
            return res;
        }
    }
    return Res::Ok();
}

Res CCustomTxVisitor::CollateralPctCheck(const bool hasDUSDLoans,
                                         const CVaultAssets &vaultAssets,
                                         const uint32_t ratio) const {
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    std::optional<CTokensView::TokenIDPair> tokenDUSD;
    if (static_cast<int>(height) >= consensus.DF15FortCanningRoadHeight) {
        tokenDUSD = mnview.GetToken("DUSD");
    }

    // Calculate DFI and DUSD value separately
    CAmount totalCollateralsDUSD = 0;
    CAmount totalCollateralsDFI = 0;
    CAmount factorDUSD = 0;
    CAmount factorDFI = 0;

    auto hasDUSDColl = false;
    auto hasOtherColl = false;

    for (auto &col : vaultAssets.collaterals) {
        auto token = mnview.GetCollateralTokenFromAttributes(col.nTokenId);

        if (col.nTokenId == DCT_ID{0}) {
            totalCollateralsDFI += col.nValue;
            factorDFI = token->factor;
        }

        if (tokenDUSD && col.nTokenId == tokenDUSD->first) {
            totalCollateralsDUSD += col.nValue;
            factorDUSD = token->factor;
            hasDUSDColl = true;
        } else {
            hasOtherColl = true;
        }
    }

    // Height checks
    auto isPostFCH = static_cast<int>(height) >= consensus.DF14FortCanningHillHeight;
    auto isPreFCH = static_cast<int>(height) < consensus.DF14FortCanningHillHeight;
    auto isPostFCE = static_cast<int>(height) >= consensus.DF19FortCanningEpilogueHeight;
    auto isPostFCR = static_cast<int>(height) >= consensus.DF15FortCanningRoadHeight;
    auto isPostGC = static_cast<int>(height) >= consensus.DF20GrandCentralHeight;
    auto isPostNext = static_cast<int>(height) >= consensus.DF22MetachainHeight;

    if (isPostNext) {
        const CDataStructureV0 enabledKey{AttributeTypes::Vaults, VaultIDs::DUSDVault, VaultKeys::DUSDVaultEnabled};
        auto attributes = mnview.GetAttributes();

        auto DUSDVaultsAllowed = attributes->GetValue(enabledKey, false);
        if (DUSDVaultsAllowed && hasDUSDColl && !hasOtherColl) {
            return Res::Ok();  // every loan ok when DUSD loops allowed and 100% DUSD collateral
        }
    }

    if (isPostGC) {
        totalCollateralsDUSD = MultiplyAmounts(totalCollateralsDUSD, factorDUSD);
        totalCollateralsDFI = MultiplyAmounts(totalCollateralsDFI, factorDFI);
    }
    auto totalCollaterals = totalCollateralsDUSD + totalCollateralsDFI;

    // Condition checks
    auto isDFILessThanHalfOfTotalCollateral =
        arith_uint256(totalCollateralsDFI) < arith_uint256(vaultAssets.totalCollaterals) / 2;
    auto isDFIAndDUSDLessThanHalfOfRequiredCollateral =
        arith_uint256(totalCollaterals) * 100 < (arith_uint256(vaultAssets.totalLoans) * ratio / 2);
    auto isDFILessThanHalfOfRequiredCollateral =
        arith_uint256(totalCollateralsDFI) * 100 < (arith_uint256(vaultAssets.totalLoans) * ratio / 2);

    if (isPostFCE) {
        if (hasDUSDLoans) {
            if (isDFILessThanHalfOfRequiredCollateral) {
                return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT));
            }
        } else {
            if (isDFIAndDUSDLessThanHalfOfRequiredCollateral) {
                return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT));
            }
        }
        return Res::Ok();
    }

    if (isPostFCR) {
        return isDFIAndDUSDLessThanHalfOfRequiredCollateral
                   ? Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT))
                   : Res::Ok();
    }

    if (isPostFCH) {
        return isDFILessThanHalfOfRequiredCollateral ? Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT))
                                                     : Res::Ok();
    }

    if (isPreFCH && isDFILessThanHalfOfTotalCollateral) {
        return Res::Err(std::string(ERR_STRING_MIN_COLLATERAL_DFI_PCT));
    }

    return Res::Ok();
}

ResVal<CVaultAssets> CCustomTxVisitor::CheckCollateralRatio(const CVaultId &vaultId,
                                                            const CLoanSchemeData &scheme,
                                                            const CBalances &collaterals,
                                                            bool useNextPrice,
                                                            bool requireLivePrice) const {
    const auto height = txCtx.GetHeight();
    const auto time = txCtx.GetTime();
    auto &mnview = blockCtx.GetView();

    auto vaultAssets = mnview.GetVaultAssets(vaultId, collaterals, height, time, useNextPrice, requireLivePrice);
    if (!vaultAssets) {
        return vaultAssets;
    }

    if (vaultAssets.val->ratio() < scheme.ratio) {
        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                        vaultAssets.val->ratio(),
                        scheme.ratio);
    }

    return vaultAssets;
}
