// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/accounts.h>
#include <masternodes/masternodes.h>
#include <masternodes/rewardhistoryold.h>
#include <key_io.h>

#include <limits>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char CAccountsHistoryView::ByMineAccountHistoryKey::prefix = 'm';
const unsigned char CAccountsHistoryView::ByAllAccountHistoryKey::prefix = 'h';
const unsigned char CRewardsHistoryView::ByMineRewardHistoryKey::prefix = 'Q';
const unsigned char CRewardsHistoryView::ByAllRewardHistoryKey::prefix = 'W';

void CAccountsHistoryView::ForEachMineAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start) const
{
    ForEach<ByMineAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>(callback, start);
}

Res CAccountsHistoryView::SetMineAccountHistory(const AccountHistoryKey& key, const AccountHistoryValue& value)
{
    WriteBy<ByMineAccountHistoryKey>(key, value);
    return Res::Ok();
}

void CAccountsHistoryView::ForEachAllAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start) const
{
    ForEach<ByAllAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>(callback, start);
}

Res CAccountsHistoryView::SetAllAccountHistory(const AccountHistoryKey& key, const AccountHistoryValue& value)
{
    WriteBy<ByAllAccountHistoryKey>(key, value);
    return Res::Ok();
}

void CRewardsHistoryView::ForEachMineRewardHistory(std::function<bool(RewardHistoryKey const &, CLazySerialize<RewardHistoryValue>)> callback, RewardHistoryKey const & start) const
{
    ForEach<ByMineRewardHistoryKey, RewardHistoryKey, RewardHistoryValue>(callback, start);
}

Res CRewardsHistoryView::SetMineRewardHistory(const RewardHistoryKey& key, const RewardHistoryValue& value)
{
    WriteBy<ByMineRewardHistoryKey>(key, value);
    return Res::Ok();
}

void CRewardsHistoryView::ForEachAllRewardHistory(std::function<bool(RewardHistoryKey const &, CLazySerialize<RewardHistoryValue>)> callback, RewardHistoryKey const & start) const
{
    ForEach<ByAllRewardHistoryKey, RewardHistoryKey, RewardHistoryValue>(callback, start);
}

Res CRewardsHistoryView::SetAllRewardHistory(const RewardHistoryKey& key, const RewardHistoryValue& value)
{
    WriteBy<ByAllRewardHistoryKey>(key, value);
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
        prefix = CRewardsHistoryView::ByMineRewardHistoryKey::prefix;
        auto newKey = std::make_pair(prefix, RewardHistoryKey{});
        it->Seek(DbTypeToBytes(newKey));
        if (it->Valid() && BytesToDbType(it->Key(), newKey) && newKey.first == prefix) {
            return false;
        }
        prefix = CRewardsHistoryView::ByAllRewardHistoryKey::prefix;
        newKey = std::make_pair(prefix, RewardHistoryKey{});
        it->Seek(DbTypeToBytes(newKey));
        if (it->Valid() && BytesToDbType(it->Key(), newKey) && newKey.first == prefix) {
            return false;
        }
        bool hasOldAccountHistory = false;
        view.ForEachAllAccountHistory([&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue>) {
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
