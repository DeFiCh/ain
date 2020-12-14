// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/accounts.h>
#include <key_io.h>

#include <limits>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char CAccountsHistoryView::ByAccountHistoryKey::prefix = 'h'; // don't intersects with CMintedHeadersView::MintedHeaders::prefix due to different DB

void CAccountsHistoryView::ForEachAccountHistory(std::function<bool(CScript const & owner, uint32_t height, uint32_t txn, uint256 const & txid, unsigned char category, TAmounts const & diffs)> callback, AccountHistoryKey start) const
{
    ForEach<ByAccountHistoryKey, AccountHistoryKey, AccountHistoryValue>([&callback] (AccountHistoryKey const & key, AccountHistoryValue const & val) {
        return callback(key.owner,key.blockHeight, key.txn, val.txid, val.category, val.diff);
    }, start);
}

Res CAccountsHistoryView::SetAccountHistory(const CScript & owner, uint32_t height, uint32_t txn, const uint256 & txid, unsigned char category, TAmounts const & diff)
{
////  left for debug:
//    std::string ownerStr;
//    CTxDestination dest;
//    if (!ExtractDestination(owner, dest)) {
//        ownerStr = owner.GetHex();
//    } else
//        ownerStr = EncodeDestination(dest);
//    LogPrintf("DEBUG: SetAccountHistory: owner: %s, ownerStr: %s, block: %i, txn: %d, txid: %s, diffs: %ld\n", owner.GetHex().c_str(), ownerStr.c_str(), height, txn, txid.ToString().c_str(), diff.size());

    WriteBy<ByAccountHistoryKey>(AccountHistoryKey{owner, height, txn}, AccountHistoryValue{txid, category, diff});
    return Res::Ok();
}

bool CAccountsHistoryView::TrackAffectedAccounts(CStorageKV const & before, MapKV const & diff, uint32_t height, uint32_t txn, const uint256 & txid, unsigned char category) {
    // txn set to max if called from CreateNewBlock to check account balances, do not track here.
    if (!gArgs.GetBoolArg("-acindex", false))
        return false;

    std::map<CScript, TAmounts> balancesDiff;
    using TKey = std::pair<unsigned char, BalanceKey>;

    for (auto it = diff.lower_bound({CAccountsView::ByBalanceKey::prefix}); it != diff.end() && it->first.at(0) == CAccountsView::ByBalanceKey::prefix; ++it) {
        CAmount oldAmount = 0, newAmount = 0;

        if (it->second) {
            BytesToDbType(*it->second, newAmount);
        }
        TBytes beforeVal;
        if (before.Read(it->first, beforeVal)) {
            BytesToDbType(beforeVal, oldAmount);
        }
        TKey balanceKey;
        BytesToDbType(it->first, balanceKey);
        balancesDiff[balanceKey.second.owner][balanceKey.second.tokenID] = newAmount - oldAmount;
    }
    for (auto const & kv : balancesDiff) {
        SetAccountHistory(kv.first, height, txn, txid, category, kv.second);
    }
    return true;
}
