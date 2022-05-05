
#include <amount.h>
#include <chainparams.h>
#include <coins.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <masternodes/accounts.h>
#include <masternodes/consensus/txvisitor.h>
#include <masternodes/customtx.h>
#include <masternodes/icxorder.h>
#include <masternodes/loan.h>
#include <masternodes/masternodes.h>
#include <masternodes/oracles.h>
#include <masternodes/poolpairs.h>
#include <masternodes/tokens.h>

CCustomTxVisitor::CCustomTxVisitor(CCustomCSView& mnview,
                                   const CCoinsViewCache& coins,
                                   const CTransaction& tx,
                                   const Consensus::Params& consensus,
                                   uint32_t height,
                                   uint64_t time,
                                   uint32_t txn)

        : txn(txn), time(time), height(height), mnview(mnview), tx(tx), coins(coins), consensus(consensus) {}

bool CCustomTxVisitor::HasAuth(const CScript& auth) const {
    for (const auto& input : tx.vin) {
        const Coin& coin = coins.AccessCoin(input.prevout);
        if (!coin.IsSpent() && coin.out.scriptPubKey == auth)
            return true;
    }
    return false;
}

ResVal<CBalances> CCustomTxVisitor::BurntTokens() const {
    CBalances balances;
    for (const auto& out : tx.vout) {
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN) {
            auto res = balances.Add(out.TokenAmount());
            if (!res)
                return res;
        }
    }
    return {balances, Res::Ok()};
}

ResVal<CBalances> CCustomTxVisitor::MintedTokens(uint32_t mintingOutputsStart) const {
    CBalances balances;
    for (uint32_t i = mintingOutputsStart; i < tx.vout.size(); i++) {
        auto res = balances.Add(tx.vout[i].TokenAmount());
        if (!res)
            return res;
    }
    return {balances, Res::Ok()};
}

Res CCustomTxVisitor::HasCollateralAuth(const uint256& collateralTx) const {
    const Coin& auth = coins.AccessCoin(COutPoint(collateralTx, 1)); // always n=1 output
    if (!HasAuth(auth.out.scriptPubKey))
        return Res::Err("tx must have at least one input from the owner");

    return Res::Ok();
}

Res CCustomTxVisitor::HasFoundationAuth() const {
    for (const auto& input : tx.vin) {
        const Coin& coin = coins.AccessCoin(input.prevout);
        if (!coin.IsSpent() && consensus.foundationMembers.count(coin.out.scriptPubKey) > 0)
            return Res::Ok();
    }
    return Res::Err("tx not from foundation member");
}

Res CCustomTxVisitor::CheckMasternodeCreationTx() const {
    if (tx.vout.size() < 2
    || tx.vout[0].nValue < GetMnCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0}
    || tx.vout[1].nValue != GetMnCollateralAmount(height) || tx.vout[1].nTokenId != DCT_ID{0})
        return Res::Err("malformed tx vouts (wrong creation fee or collateral amount)");

    return Res::Ok();
}

Res CCustomTxVisitor::CheckTokenCreationTx() const {
    if (tx.vout.size() < 2
    || tx.vout[0].nValue < GetTokenCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0}
    || tx.vout[1].nValue != GetTokenCollateralAmount() || tx.vout[1].nTokenId != DCT_ID{0})
        return Res::Err("malformed tx vouts (wrong creation fee or collateral amount)");

    return Res::Ok();
}

Res CCustomTxVisitor::CheckCustomTx() const {
    if (static_cast<int>(height) < consensus.EunosPayaHeight && tx.vout.size() != 2)
        return Res::Err("malformed tx vouts ((wrong number of vouts)");

    if (static_cast<int>(height) >= consensus.EunosPayaHeight && tx.vout[0].nValue != 0)
        return Res::Err("malformed tx vouts, first vout must be OP_RETURN vout with value 0");

    return Res::Ok();
}

Res CCustomTxVisitor::TransferTokenBalance(DCT_ID id, CAmount amount, CScript const & from, CScript const & to) const {
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

static CAmount GetDFIperBTC(const CPoolPair& BTCDFIPoolPair) {
    if (BTCDFIPoolPair.idTokenA == DCT_ID({0}))
        return DivideAmounts(BTCDFIPoolPair.reserveA, BTCDFIPoolPair.reserveB);
    return DivideAmounts(BTCDFIPoolPair.reserveB, BTCDFIPoolPair.reserveA);
}

CAmount CCustomTxVisitor::CalculateTakerFee(CAmount amount) const {
    auto tokenBTC = mnview.GetToken(CICXOrder::TOKEN_BTC);
    assert(tokenBTC);
    auto pair = mnview.GetPoolPair(tokenBTC->first, DCT_ID{0});
    assert(pair);
    return (arith_uint256(amount) * mnview.ICXGetTakerFeePerBTC() / COIN
          * GetDFIperBTC(pair->second) / COIN).GetLow64();
}

ResVal<CScript> CCustomTxVisitor::MintableToken(DCT_ID id, const CTokenImplementation& token) const {
    if (token.destructionTx != uint256{})
        return Res::Err("token %s already destroyed at height %i by tx %s", token.symbol,
                        token.destructionHeight, token.destructionTx.GetHex());

    const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output

    // pre-bayfront logic:
    if (static_cast<int>(height) < consensus.BayfrontHeight) {
        if (id < CTokensView::DCT_ID_START)
            return Res::Err("token %s is a 'stable coin', can't mint stable coin!", id.ToString());

        if (!HasAuth(auth.out.scriptPubKey))
            return Res::Err("tx must have at least one input from token owner");

        return {auth.out.scriptPubKey, Res::Ok()};
    }

    if (id == DCT_ID{0})
        return Res::Err("can't mint default DFI coin!");

    if (token.IsPoolShare())
        return Res::Err("can't mint LPS token %s!", id.ToString());

    static const auto isMainNet = Params().NetworkIDString() == CBaseChainParams::MAIN;

    // may be different logic with LPS, so, dedicated check:
    if (!token.IsMintable() || (isMainNet && mnview.GetLoanTokenByID(id)))
        return Res::Err("token %s is not mintable!", id.ToString());

    if (!HasAuth(auth.out.scriptPubKey)) { // in the case of DAT, it's ok to do not check foundation auth cause exact DAT owner is foundation member himself
        if (!token.IsDAT())
            return Res::Err("tx must have at least one input from token owner");
        else if (static_cast<int>(height) < consensus.GreatWorldHeight && !HasFoundationAuth()) // Is a DAT, check founders auth
            return Res::Err("token is DAT and tx not from foundation member");
    }

    return {auth.out.scriptPubKey, Res::Ok()};
}

Res CCustomTxVisitor::EraseEmptyBalances(TAmounts& balances) const {
    for (auto it = balances.begin(), next_it = it; it != balances.end(); it = next_it) {
        ++next_it;

        auto token = mnview.GetToken(it->first);
        if (!token)
            return Res::Err("reward token %d does not exist!", it->first.v);

        if (it->second == 0)
            balances.erase(it);
    }
    return Res::Ok();
}

Res CCustomTxVisitor::SetShares(const CScript& owner, const TAmounts& balances) const {
    for (const auto& balance : balances) {
        auto token = mnview.GetToken(balance.first);
        if (token && token->IsPoolShare()) {
            const auto bal = mnview.GetBalance(owner, balance.first);
            if (bal.nValue == balance.second) {
                auto res = mnview.SetShare(balance.first, owner, height);
                if (!res)
                    return res;
            }
        }
    }
    return Res::Ok();
}

Res CCustomTxVisitor::DelShares(const CScript& owner, const TAmounts& balances) const {
    for (const auto& kv : balances) {
        auto token = mnview.GetToken(kv.first);
        if (token && token->IsPoolShare()) {
            const auto balance = mnview.GetBalance(owner, kv.first);
            if (balance.nValue == 0) {
                auto res = mnview.DelShare(kv.first, owner);
                if (!res)
                    return res;
            }
        }
    }
    return Res::Ok();
}

// we need proxy view to prevent add/sub balance record
void CCustomTxVisitor::CalculateOwnerRewards(const CScript& owner) const {
    CCustomCSView view(mnview);
    view.CalculateOwnerRewards(owner, height);
    view.Flush();
}

Res CCustomTxVisitor::SubBalanceDelShares(const CScript& owner, const CBalances& balance) const {
    CalculateOwnerRewards(owner);
    auto res = mnview.SubBalances(owner, balance);
    if (!res)
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, res.msg);

    return DelShares(owner, balance.balances);
}

Res CCustomTxVisitor::AddBalanceSetShares(const CScript& owner, const CBalances& balance) const {
    CalculateOwnerRewards(owner);
    auto res = mnview.AddBalances(owner, balance);
    return !res ? res : SetShares(owner, balance.balances);
}

Res CCustomTxVisitor::AddBalancesSetShares(const CAccounts& accounts) const {
    for (const auto& account : accounts) {
        auto res = AddBalanceSetShares(account.first, account.second);
        if (!res)
            return res;
    }
    return Res::Ok();
}

Res CCustomTxVisitor::SubBalancesDelShares(const CAccounts& accounts) const {
    for (const auto& account : accounts) {
        auto res = SubBalanceDelShares(account.first, account.second);
        if (!res)
            return res;
    }
    return Res::Ok();
}

Res CCustomTxVisitor::NormalizeTokenCurrencyPair(std::set<CTokenCurrencyPair>& tokenCurrency) const {
    std::set<CTokenCurrencyPair> trimmed;
    for (const auto& pair : tokenCurrency) {
        auto token = trim_ws(pair.first).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        auto currency = trim_ws(pair.second).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        if (token.empty() || currency.empty())
            return Res::Err("empty token / currency");
        trimmed.emplace(token, currency);
    }
    tokenCurrency = std::move(trimmed);
    return Res::Ok();
}

ResVal<CCollateralLoans> CCustomTxVisitor::CheckCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals, bool useNextPrice, bool requireLivePrice) const {

    auto collateralsLoans = mnview.GetLoanCollaterals(vaultId, collaterals, height, time, useNextPrice, requireLivePrice);
    if (!collateralsLoans)
        return collateralsLoans;

    if (collateralsLoans.val->ratio() < scheme.ratio)
        return Res::Err("Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans.val->ratio(), scheme.ratio);

    return collateralsLoans;
}

Res CCustomTxVisitor::CheckNextCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals) const {

    auto tokenDUSD = mnview.GetToken("DUSD");
    bool allowDUSD = tokenDUSD && static_cast<int>(height) >= consensus.FortCanningRoadHeight;

    for (int i = 0; i < 2; i++) {
        // check ratio against current and active price
        bool useNextPrice = i > 0, requireLivePrice = true;
        auto collateralsLoans = CheckCollateralRatio(vaultId, scheme, collaterals, useNextPrice, requireLivePrice);
        if (!collateralsLoans)
            return std::move(collateralsLoans);

        uint64_t totalCollaterals = 0;
        for (auto& col : collateralsLoans.val->collaterals)
            if (col.nTokenId == DCT_ID{0}
            || (allowDUSD && col.nTokenId == tokenDUSD->first))
                totalCollaterals += col.nValue;

        if (static_cast<int>(height) < consensus.FortCanningHillHeight) {
            if (totalCollaterals < collateralsLoans.val->totalCollaterals / 2)
                return Res::Err("At least 50%% of the collateral must be in DFI.");
        } else {
            if (arith_uint256(totalCollaterals) * 100 < arith_uint256(collateralsLoans.val->totalLoans) * scheme.ratio / 2)
                return Res::Err("At least 50%% of the minimum required collateral must be in DFI or DUSD.");
        }
    }
    return Res::Ok();
}
