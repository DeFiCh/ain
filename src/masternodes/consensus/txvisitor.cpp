
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
                                   CFutureSwapView& futureSwapView,
                                   const CCoinsViewCache& coins,
                                   const CTransaction& tx,
                                   const Consensus::Params& consensus,
                                   uint32_t height,
                                   uint64_t time,
                                   uint32_t txn)

        : txn(txn), time(time), height(height), mnview(mnview), futureSwapView(futureSwapView), tx(tx), coins(coins), consensus(consensus) {}

Res CCustomTxVisitor::HasAuth(const CScript& auth) const {
    for (const auto& input : tx.vin) {
        const Coin& coin = coins.AccessCoin(input.prevout);
        if (!coin.IsSpent() && coin.out.scriptPubKey == auth)
            return Res::Ok();
    }
    return Res::Err("tx must have at least one input from account owner");
}

ResVal<CBalances> CCustomTxVisitor::BurntTokens() const {
    CBalances balances;
    for (const auto& out : tx.vout)
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN)
            Require(balances.Add(out.TokenAmount()));

    return {balances, Res::Ok()};
}

ResVal<CBalances> CCustomTxVisitor::MintedTokens(uint32_t mintingOutputsStart) const {
    CBalances balances;
    for (uint32_t i = mintingOutputsStart; i < tx.vout.size(); i++)
        Require(balances.Add(tx.vout[i].TokenAmount()));

    return {balances, Res::Ok()};
}

Res CCustomTxVisitor::HasCollateralAuth(const uint256& collateralTx) const {
    const Coin& auth = coins.AccessCoin(COutPoint(collateralTx, 1)); // always n=1 output
    Require(HasAuth(auth.out.scriptPubKey));
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
    Require(tx.vout.size() >= 2
        && tx.vout[0].nValue >= GetMnCreationFee(height) && tx.vout[0].nTokenId == DCT_ID{0}
        && tx.vout[1].nValue == GetMnCollateralAmount(height) && tx.vout[1].nTokenId == DCT_ID{0},
        "malformed tx vouts (wrong creation fee or collateral amount)");

    return Res::Ok();
}

Res CCustomTxVisitor::CheckTokenCreationTx() const {
    Require(tx.vout.size() >= 2
        && tx.vout[0].nValue >= GetTokenCreationFee(height) && tx.vout[0].nTokenId == DCT_ID{0}
        && tx.vout[1].nValue == GetTokenCollateralAmount() && tx.vout[1].nTokenId == DCT_ID{0},
        "malformed tx vouts (wrong creation fee or collateral amount)");

    return Res::Ok();
}

Res CCustomTxVisitor::CheckCustomTx() const {
    if (static_cast<int>(height) < consensus.EunosPayaHeight)
        Require(tx.vout.size() == 2, "malformed tx vouts (wrong number of vouts)");

    if (static_cast<int>(height) >= consensus.EunosPayaHeight)
        Require(tx.vout[0].nValue == 0, "malformed tx vouts, first vout must be OP_RETURN vout with value 0");

    return Res::Ok();
}

Res CCustomTxVisitor::CheckProposalTx(uint8_t type) const {
    auto propType = static_cast<CPropType>(type);
    if (tx.vout[0].nValue != GetPropsCreationFee(height, propType) || tx.vout[0].nTokenId != DCT_ID{0})
        return Res::Err("malformed tx vouts (wrong creation fee)");

    return Res::Ok();
}

Res CCustomTxVisitor::TransferTokenBalance(DCT_ID id, CAmount amount, CScript const & from, CScript const & to) const {
    assert(!from.empty() || !to.empty());

    CTokenAmount tokenAmount{id, amount};
    // if "from" not supplied it will only add balance on "to" address
    if (!from.empty())
        Require(mnview.SubBalance(from, tokenAmount));

    // if "to" not supplied it will only sub balance from "form" address
    if (!to.empty())
        Require(mnview.AddBalance(to,tokenAmount));

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
    Require(token.destructionTx == uint256{}, "token %s already destroyed at height %i by tx %s",
              token.symbol, token.destructionHeight, token.destructionTx.GetHex());

    const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output

    // pre-bayfront logic:
    if (static_cast<int>(height) < consensus.BayfrontHeight) {
        Require(id >= CTokensView::DCT_ID_START, "token %s is a 'stable coin', can't mint stable coin!", id.ToString());
        Require(HasAuth(auth.out.scriptPubKey));
        return {auth.out.scriptPubKey, Res::Ok()};
    }

    Require(id != DCT_ID{0}, "can't mint default DFI coin!");
    Require(!token.IsPoolShare(), "can't mint LPS token %s!", id.ToString());

    static const auto isMainNet = Params().NetworkIDString() == CBaseChainParams::MAIN;
    auto mintable = token.IsMintable() && (!isMainNet || !mnview.GetLoanTokenByID(id));
    Require(mintable, "token %s is not mintable!", id.ToString());

    // may be different logic with LPS, so, dedicated check:
    if (!token.IsMintable() || (isMainNet && mnview.GetLoanTokenByID(id)))
        return Res::Err("token %s is not mintable!", id.ToString());

    if (!HasAuth(auth.out.scriptPubKey)) { // in the case of DAT, it's ok to do not check foundation auth cause exact DAT owner is foundation member himself
        Require(token.IsDAT(), "tx must have at least one input from token owner");
        if (!HasFoundationAuth()) // Is a DAT, check founders auth
            if (height < static_cast<uint32_t>(consensus.GreatWorldHeight))
                return Res::Err("token is DAT and tx not from foundation member");
    }

    return {auth.out.scriptPubKey, Res::Ok()};
}

Res CCustomTxVisitor::EraseEmptyBalances(TAmounts& balances) const {
    for (auto it = balances.begin(), next_it = it; it != balances.end(); it = next_it) {
        ++next_it;

        Require(mnview.GetToken(it->first), "reward token %d does not exist!", it->first.v);

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
            if (bal.nValue == balance.second)
                Require(mnview.SetShare(balance.first, owner, height));
        }
    }
    return Res::Ok();
}

Res CCustomTxVisitor::DelShares(const CScript& owner, const TAmounts& balances) const {
    for (const auto& kv : balances) {
        auto token = mnview.GetToken(kv.first);
        if (token && token->IsPoolShare()) {
            const auto balance = mnview.GetBalance(owner, kv.first);
            if (balance.nValue == 0)
                Require(mnview.DelShare(kv.first, owner));
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
    Require(res, CustomTxErrCodes::NotEnoughBalance, res.msg);
    return DelShares(owner, balance.balances);
}

Res CCustomTxVisitor::AddBalanceSetShares(const CScript& owner, const CBalances& balance) const {
    CalculateOwnerRewards(owner);
    Require(mnview.AddBalances(owner, balance));
    return SetShares(owner, balance.balances);
}

Res CCustomTxVisitor::AddBalancesSetShares(const CAccounts& accounts) const {
    for (const auto& account : accounts)
        Require(AddBalanceSetShares(account.first, account.second));
    return Res::Ok();
}

Res CCustomTxVisitor::SubBalancesDelShares(const CAccounts& accounts) const {
    for (const auto& account : accounts)
        Require(SubBalanceDelShares(account.first, account.second));
    return Res::Ok();
}

Res CCustomTxVisitor::NormalizeTokenCurrencyPair(std::set<CTokenCurrencyPair>& tokenCurrency) const {
    std::set<CTokenCurrencyPair> trimmed;
    for (const auto& pair : tokenCurrency) {
        auto token = trim_ws(pair.first).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        auto currency = trim_ws(pair.second).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        Require(!token.empty() && !currency.empty(), "empty token / currency");
        trimmed.emplace(token, currency);
    }
    tokenCurrency = std::move(trimmed);
    return Res::Ok();
}

ResVal<CCollateralLoans> CCustomTxVisitor::CheckCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals, bool useNextPrice, bool requireLivePrice) const {
    auto collateralsLoans = mnview.GetLoanCollaterals(vaultId, collaterals, height, time, useNextPrice, requireLivePrice);
    Require(collateralsLoans);
    Require(collateralsLoans->ratio() >= scheme.ratio, "Vault does not have enough collateralization ratio defined by loan scheme - %d < %d", collateralsLoans->ratio(), scheme.ratio);
    return collateralsLoans;
}

Res CCustomTxVisitor::CheckNextCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals) const {
    auto tokenDUSD = mnview.GetToken("DUSD");
    bool allowDUSD = tokenDUSD && static_cast<int>(height) >= consensus.FortCanningRoadHeight;

    for (int i = 0; i < 2; i++) {
        // check ratio against current and active price
        bool useNextPrice = i > 0, requireLivePrice = true;
        auto collateralsLoans = CheckCollateralRatio(vaultId, scheme, collaterals, useNextPrice, requireLivePrice);
        Require(collateralsLoans);

        uint64_t totalCollaterals = 0;
        for (auto& col : collateralsLoans->collaterals)
            if (col.nTokenId == DCT_ID{0}
            || (allowDUSD && col.nTokenId == tokenDUSD->first))
                totalCollaterals += col.nValue;

        if (static_cast<int>(height) < consensus.FortCanningHillHeight)
            Require(totalCollaterals >= collateralsLoans->totalCollaterals / 2, "At least 50%% of the collateral must be in DFI.");
        else
            Require(arith_uint256(totalCollaterals) * 100 >= arith_uint256(collateralsLoans->totalLoans) * scheme.ratio / 2,
                      "At least 50%% of the minimum required collateral must be in DFI or DUSD.");
    }
    return Res::Ok();
}
