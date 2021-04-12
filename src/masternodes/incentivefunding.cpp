// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/incentivefunding.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char CCommunityBalancesView::ById::prefix = 'F';


CAmount CCommunityBalancesView::GetCommunityBalance(CommunityAccountType account) const
{
    CAmount val;
    bool ok = ReadBy<ById>(static_cast<unsigned char>(account), val);
    if (ok) {
        return val;
    }
    return 0;
}

Res CCommunityBalancesView::SetCommunityBalance(CommunityAccountType account, CAmount amount)
{
    // deny negative values on db level!
    if (amount < 0) {
        return Res::Err("negative amount");
    }
    WriteBy<ById>(static_cast<unsigned char>(account), amount);
    return Res::Ok();
}

void CCommunityBalancesView::ForEachCommunityBalance(std::function<bool (CommunityAccountType, CLazySerialize<CAmount>)> callback)
{
    ForEach<ById, unsigned char, CAmount>([&callback] (unsigned char const & key, CLazySerialize<CAmount> val) {
        return callback(CommunityAccountCodeToType(key), val);
    }, '\0');

}

Res CCommunityBalancesView::AddCommunityBalance(CommunityAccountType account, CAmount amount)
{
    if (amount == 0) {
        return Res::Ok();
    }
    auto res = SafeAdd(amount, GetCommunityBalance(account));
    return !res.ok ? res : SetCommunityBalance(account, *res.val);
}

Res CCommunityBalancesView::SubCommunityBalance(CommunityAccountType account, CAmount amount)
{
    if (amount == 0) {
        return Res::Ok();
    }
    if (amount < 0) {
        return Res::Err("negative amount");
    }
    CAmount oldBalance = GetCommunityBalance(account);
    if (oldBalance < amount) {
        return Res::Err("Amount %d is less than %d", oldBalance, amount);
    }
    return SetCommunityBalance(account, oldBalance - amount);
}
