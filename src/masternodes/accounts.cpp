// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>

#include <masternodes/futureswap.h>

void CAccountsView::ForEachBalance(std::function<bool(CScript const &, CTokenAmount const &)> callback, BalanceKey const & start)
{
    ForEach<ByBalanceKey, BalanceKey, CAmount>([&callback] (BalanceKey const & key, CAmount val) {
        return callback(key.owner, CTokenAmount{key.tokenID, val});
    }, start);
}

CTokenAmount CAccountsView::GetBalance(CScript const & owner, DCT_ID tokenID) const
{
    CAmount val;
    bool ok = ReadBy<ByBalanceKey>(BalanceKey{owner, tokenID}, val);
    if (ok) {
        return CTokenAmount{tokenID, val};
    }
    return CTokenAmount{tokenID, 0};
}

Res CAccountsView::SetBalance(CScript const & owner, CTokenAmount amount)
{
    if (amount.nValue != 0) {
        WriteBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId}, amount.nValue);
    } else {
        EraseBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId});
    }
    return Res::Ok();
}

Res CAccountsView::AddBalance(CScript const & owner, CTokenAmount amount)
{
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    Require(balance.Add(amount.nValue));
    return SetBalance(owner, balance);
}

Res CAccountsView::SubBalance(CScript const & owner, CTokenAmount amount)
{
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    Require(balance.Sub(amount.nValue));
    return SetBalance(owner, balance);
}

Res CAccountsView::AddBalances(CScript const & owner, CBalances const & balances)
{
    for (const auto& kv : balances.balances)
        Require(AddBalance(owner, CTokenAmount{kv.first, kv.second}));

    return Res::Ok();
}

Res CAccountsView::SubBalances(CScript const & owner, CBalances const & balances)
{
    for (const auto& kv : balances.balances)
        Require(SubBalance(owner, CTokenAmount{kv.first, kv.second}));

    return Res::Ok();
}

void CAccountsView::ForEachAccount(std::function<bool(CScript const &)> callback, CScript const & start)
{
    ForEach<ByHeightKey, CScript, uint32_t>([&callback] (CScript const & owner, CLazySerialize<uint32_t>) {
        return callback(owner);
    }, start);
}

Res CAccountsView::UpdateBalancesHeight(CScript const & owner, uint32_t height)
{
    WriteBy<ByHeightKey>(owner, height);
    return Res::Ok();
}

uint32_t CAccountsView::GetBalancesHeight(CScript const & owner)
{
    uint32_t height;
    bool ok = ReadBy<ByHeightKey>(owner, height);
    return ok ? height : 0;
}
