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
const unsigned char CAccountsHistoryView::ByAccountHistoryKey::prefix = 'h';

void CAccountsHistoryView::ForEachAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start)
{
    ForEach<ByAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>(callback, start);
}

Res CAccountsHistoryView::SetAccountHistory(const AccountHistoryKey& key, const AccountHistoryValue& value)
{
    WriteBy<ByAccountHistoryKey>(key, value);
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
