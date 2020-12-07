// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/accounts.h>
#include <key_io.h>

#include <limits>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char CAccountsHistoryView::ByAccountHistoryKey::prefix = 'h'; // don't intersects with CMintedHeadersView::MintedHeaders::prefix due to different DB
const unsigned char CRewardsHistoryView::ByRewardHistoryKey::prefix = 'R'; // don't intersects with CMintedHeadersView::MintedHeaders::prefix due to different DB

void CAccountsHistoryView::ForEachAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start) const
{
    ForEach<ByAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>(callback, start);
}

Res CAccountsHistoryView::SetAccountHistory(const AccountHistoryKey& key, const AccountHistoryValue& value)
{
    WriteBy<ByAccountHistoryKey>(key, value);
    return Res::Ok();
}

void CRewardsHistoryView::ForEachRewardHistory(std::function<bool(RewardHistoryKey const &, CLazySerialize<RewardHistoryValue>)> callback, RewardHistoryKey const & start) const
{
    ForEach<ByRewardHistoryKey, RewardHistoryKey, RewardHistoryValue>(callback, start);
}

Res CRewardsHistoryView::SetRewardHistory(const RewardHistoryKey& key, const RewardHistoryValue& value)
{
    WriteBy<ByRewardHistoryKey>(key, value);
    return Res::Ok();
}
