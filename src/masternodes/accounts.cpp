// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>

void CAccountsView::ForEachBalance(std::function<bool(const CScript &, const CTokenAmount &)> callback,
                                   const BalanceKey &start) {
    ForEach<ByBalanceKey, BalanceKey, CAmount>(
        [&callback](const BalanceKey &key, CAmount val) {
            return callback(key.owner, CTokenAmount{key.tokenID, val});
        },
        start);
}

CTokenAmount CAccountsView::GetBalance(const CScript &owner, DCT_ID tokenID) const {
    CAmount val;
    bool ok = ReadBy<ByBalanceKey>(BalanceKey{owner, tokenID}, val);
    if (ok) {
        return CTokenAmount{tokenID, val};
    }
    return CTokenAmount{tokenID, 0};
}

Res CAccountsView::SetBalance(const CScript &owner, CTokenAmount amount) {
    if (amount.nValue != 0) {
        WriteBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId}, amount.nValue);
    } else {
        EraseBy<ByBalanceKey>(BalanceKey{owner, amount.nTokenId});
    }
    return Res::Ok();
}

Res CAccountsView::AddBalance(const CScript &owner, CTokenAmount amount) {
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    if (const auto res = balance.Add(amount.nValue); !res) {
        return res;
    }
    return SetBalance(owner, balance);
}

Res CAccountsView::SubBalance(const CScript &owner, CTokenAmount amount) {
    if (amount.nValue == 0) {
        return Res::Ok();
    }
    auto balance = GetBalance(owner, amount.nTokenId);
    if (const auto res = balance.Sub(amount.nValue); !res) {
        return res;
    }
    return SetBalance(owner, balance);
}

Res CAccountsView::AddBalances(const CScript &owner, const CBalances &balances) {
    for (const auto &kv : balances.balances)
        Require(AddBalance(owner, CTokenAmount{kv.first, kv.second}));

    return Res::Ok();
}

Res CAccountsView::SubBalances(const CScript &owner, const CBalances &balances) {
    for (const auto &kv : balances.balances)
        Require(SubBalance(owner, CTokenAmount{kv.first, kv.second}));

    return Res::Ok();
}

void CAccountsView::ForEachAccount(std::function<bool(const CScript &)> callback, const CScript &start) {
    ForEach<ByHeightKey, CScript, uint32_t>(
        [&callback](const CScript &owner, CLazySerialize<uint32_t>) { return callback(owner); }, start);
}

Res CAccountsView::UpdateBalancesHeight(const CScript &owner, uint32_t height) {
    WriteBy<ByHeightKey>(owner, height);
    return Res::Ok();
}

uint32_t CAccountsView::GetBalancesHeight(const CScript &owner) {
    uint32_t height;
    bool ok = ReadBy<ByHeightKey>(owner, height);
    return ok ? height : 0;
}

Res CAccountsView::StoreFuturesUserValues(const CFuturesUserKey &key, const CFuturesUserValue &futures) {
    Require(WriteBy<ByFuturesSwapKey>(key, futures), []{ return "Failed to store futures"; });
    return Res::Ok();
}

void CAccountsView::ForEachFuturesUserValues(
    std::function<bool(const CFuturesUserKey &, const CFuturesUserValue &)> callback,
    const CFuturesUserKey &start) {
    ForEach<ByFuturesSwapKey, CFuturesUserKey, CFuturesUserValue>(callback, start);
}

Res CAccountsView::EraseFuturesUserValues(const CFuturesUserKey &key) {
    Require(EraseBy<ByFuturesSwapKey>(key), []{ return "Failed to erase futures"; });
    return Res::Ok();
}

Res CAccountsView::StoreFuturesDUSD(const CFuturesUserKey &key, const CAmount &amount) {
    Require(WriteBy<ByFuturesDUSDKey>(key, amount), []{ return "Failed to store futures"; });
    return Res::Ok();
}

void CAccountsView::ForEachFuturesDUSD(std::function<bool(const CFuturesUserKey &, const CAmount &)> callback,
                                       const CFuturesUserKey &start) {
    ForEach<ByFuturesDUSDKey, CFuturesUserKey, CAmount>(callback, start);
}

Res CAccountsView::EraseFuturesDUSD(const CFuturesUserKey &key) {
    Require(EraseBy<ByFuturesDUSDKey>(key), []{ return "Failed to erase futures"; });
    return Res::Ok();
}
