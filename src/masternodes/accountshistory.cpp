// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/accounts.h>
#include <masternodes/masternodes.h>
#include <masternodes/rewardhistoryold.h>
#include <key_io.h>

#include <limits>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char CAccountsHistoryView::ByAccountHistoryKey::prefix = 'h'; // don't intersects with CMintedHeadersView::MintedHeaders::prefix due to different DB
const unsigned char CRewardsHistoryView::ByRewardHistoryKey::prefix = 'H'; // don't intersects with CMintedHeadersView::MintedHeaders::prefix due to different DB

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

bool shouldMigrateOldRewardHistory(CCustomCSView & view)
{
    auto it = view.GetRaw().NewIterator();
    try {
        auto prefix = oldRewardHistoryPrefix;
        auto oldKey = std::make_pair(prefix, oldRewardHistoryKey{});
        it->Seek(DbTypeToBytes(oldKey));
        if (it->Valid() && BytesToDbType(it->Key(), oldKey) && oldKey.first == prefix) {
            return true;
        }
        prefix = CRewardsHistoryView::ByRewardHistoryKey::prefix;
        auto newKey = std::make_pair(prefix, RewardHistoryKey{});
        it->Seek(DbTypeToBytes(newKey));
        if (it->Valid() && BytesToDbType(it->Key(), newKey) && newKey.first == prefix) {
            return false;
        }
        bool hasOldAccountHistory = false;
        view.ForEachAccountHistory([&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue>) {
            if (key.txn == std::numeric_limits<uint32_t>::max()) {
                hasOldAccountHistory = true;
                return false;
            }
            return true;
        }, { {}, 0, std::numeric_limits<uint32_t>::max() });
        return hasOldAccountHistory;
    } catch(...) {
        return true;
    }
}
