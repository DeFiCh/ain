// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <chainparams.h>
#include <coins.h>
#include <masternodes/accounts.h>
#include <masternodes/consensus/txvisitor.h>
#include <masternodes/customtx.h>
#include <masternodes/errors.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/icxorder.h>
#include <masternodes/loan.h>
#include <masternodes/masternodes.h>


constexpr std::string_view ERR_STRING_MIN_COLLATERAL_DFI_PCT =
        "At least 50%% of the minimum required collateral must be in DFI";
constexpr std::string_view ERR_STRING_MIN_COLLATERAL_DFI_DUSD_PCT =
        "At least 50%% of the minimum required collateral must be in DFI or DUSD";

Res HasAuth(const CTransaction &tx, const CCoinsViewCache &coins, const CScript &auth, AuthStrategy strategy) {
    for (const auto &input : tx.vin) {
        const Coin &coin = coins.AccessCoin(input.prevout);
        if (coin.IsSpent()) continue;
        if (strategy == AuthStrategy::DirectPubKeyMatch) {
            if (coin.out.scriptPubKey == auth) {
                return Res::Ok();
            }
        } else if (strategy == AuthStrategy::EthKeyMatch) {
            std::vector<TBytes> vRet;
            if (Solver(coin.out.scriptPubKey, vRet) == txnouttype::TX_PUBKEYHASH)
            {
                auto it = input.scriptSig.begin();
                CPubKey pubkey(input.scriptSig.begin() + *it + 2, input.scriptSig.end());
                auto script = GetScriptForDestination(WitnessV16EthHash(pubkey));
                if (script == auth)
                    return Res::Ok();
            }
        }
    }
    return DeFiErrors::InvalidAuth();
}

CCustomTxVisitor::CCustomTxVisitor(const CTransaction &tx,
                                   uint32_t height,
                                   const CCoinsViewCache &coins,
                                   CCustomCSView &mnview,
                                   const Consensus::Params &consensus,
                                   const uint64_t time,
                                   const uint32_t txn,
                                   uint64_t evmContext,
                                   uint64_t &gasUsed)
        : height(height),
          mnview(mnview),
          tx(tx),
          coins(coins),
          consensus(consensus),
          time(time),
          txn(txn),
          evmContext(evmContext),
          gasUsed(gasUsed) {}


Res CCustomTxVisitor::HasAuth(const CScript &auth) const {
    return ::HasAuth(tx, coins, auth);
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

Res CCustomTxVisitor::CheckCustomTx() const {
    if (static_cast<int>(height) < consensus.EunosPayaHeight)
        Require(tx.vout.size() == 2, "malformed tx vouts ((wrong number of vouts)");
    if (static_cast<int>(height) >= consensus.EunosPayaHeight)
        Require(tx.vout[0].nValue == 0, "malformed tx vouts, first vout must be OP_RETURN vout with value 0");
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

ResVal<CBalances> CCustomTxVisitor::MintedTokens(uint32_t mintingOutputsStart) const {
    CBalances balances;
    for (uint32_t i = mintingOutputsStart; i < (uint32_t)tx.vout.size(); i++) {
        Require(balances.Add(tx.vout[i].TokenAmount()));
    }
    return {balances, Res::Ok()};
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


Res CCustomTxVisitor::CollateralPctCheck(const bool hasDUSDLoans,
                       const CVaultAssets &vaultAssets,
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
            hasDUSDColl= true;
        } else {
            hasOtherColl = true;
        }
    }

    // Height checks
    auto isPostFCH = static_cast<int>(height) >= consensus.FortCanningHillHeight;
    auto isPreFCH  = static_cast<int>(height) < consensus.FortCanningHillHeight;
    auto isPostFCE = static_cast<int>(height) >= consensus.FortCanningEpilogueHeight;
    auto isPostFCR = static_cast<int>(height) >= consensus.FortCanningRoadHeight;
    auto isPostGC  = static_cast<int>(height) >= consensus.GrandCentralHeight;
    auto isPostNext =  static_cast<int>(height) >= consensus.ChangiIntermediateHeight2; // Change to NextNetworkUpgradeHeight on mainnet release

    if(isPostNext) {
        const CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::AllowDUSDLoops};
        auto attributes = mnview.GetAttributes();
        assert(attributes);
        auto DUSDLoopsAllowed= attributes->GetValue(enabledKey, false);
        if(DUSDLoopsAllowed && hasDUSDColl && !hasOtherColl) {
            return Res::Ok(); //every loan ok when DUSD loops allowed and 100% DUSD collateral
        }
    }


    if (isPostGC) {
        totalCollateralsDUSD = MultiplyAmounts(totalCollateralsDUSD, factorDUSD);
        totalCollateralsDFI  = MultiplyAmounts(totalCollateralsDFI, factorDFI);
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

ResVal<CVaultAssets> CCustomTxVisitor::CheckCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals, bool useNextPrice, bool requireLivePrice) const {

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

Res CCustomTxVisitor::CheckNextCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals, const bool hasDUSDLoans) const {

    for (int i{}; i < 2; ++i) {
        // check ratio against current and active price
        bool useNextPrice = i > 0, requireLivePrice = true;
        auto vaultAssets =
                mnview.GetVaultAssets(vaultId, collaterals, height, time, useNextPrice, requireLivePrice);
        if (!vaultAssets) {
            return vaultAssets;
        }

        if (vaultAssets.val->ratio() < scheme.ratio) {
            return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d",
                            vaultAssets.val->ratio(),
                            scheme.ratio);
        }

        if (auto res = CollateralPctCheck(hasDUSDLoans, vaultAssets, scheme.ratio); !res) {
            return res;
        }
    }
    return Res::Ok();
}
