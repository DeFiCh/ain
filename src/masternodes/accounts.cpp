// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char CAccountsView::ByBalanceKey::prefix = 'a';

void CAccountsView::ForEachBalance(std::function<bool(CScript const & owner, CTokenAmount const & amount)> callback, BalanceKey start) const
{
    ForEach<ByBalanceKey, BalanceKey, CAmount>([&callback] (BalanceKey const & key, CAmount const & val) {
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
    auto res = balance.Add(amount.nValue);
    if (!res.ok) {
        return res;
    }
    return SetBalance(owner, balance);
}

Res CAccountsView::SubBalance(CScript const & owner, CTokenAmount amount)
{
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    auto res = balance.Sub(amount.nValue);
    if (!res.ok) {
        return res;
    }
    return SetBalance(owner, balance);
}

Res CAccountsView::AddBalances(CScript const & owner, CBalances const & balances)
{
    for (const auto& kv : balances.balances) {
        auto res = AddBalance(owner, CTokenAmount{kv.first, kv.second});
        if (!res.ok) {
            return res;
        }
    }
    return Res::Ok();
}

Res CAccountsView::SubBalances(CScript const & owner, CBalances const & balances)
{
    for (const auto& kv : balances.balances) {
        auto res = SubBalance(owner, CTokenAmount{kv.first, kv.second});
        if (!res.ok) {
            return res;
        }
    }
    return Res::Ok();
}
