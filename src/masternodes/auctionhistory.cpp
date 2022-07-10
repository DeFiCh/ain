// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <masternodes/auctionhistory.h>
#include <masternodes/masternodes.h>

void CAuctionHistoryView::ForEachAuctionHistory(
    std::function<bool(AuctionHistoryKey const &, CLazySerialize<AuctionHistoryValue>)> callback,
    AuctionHistoryKey const &start) {
    ForEach<ByAuctionHistoryKey, AuctionHistoryKey, AuctionHistoryValue>(callback, start);
}

Res CAuctionHistoryView::WriteAuctionHistory(const AuctionHistoryKey &key, const AuctionHistoryValue &value) {
    WriteBy<ByAuctionHistoryKey>(key, value);
    return Res::Ok();
}

Res CAuctionHistoryView::EraseAuctionHistoryHeight(uint32_t height) {
    std::vector<AuctionHistoryKey> keysToDelete;

    auto it = LowerBound<ByAuctionHistoryKey>(AuctionHistoryKey{height});
    for (; it.Valid() && it.Key().blockHeight == height; it.Next()) {
        keysToDelete.push_back(it.Key());
    }

    for (const auto &key : keysToDelete) {
        EraseAuctionHistory(key);
    }
    return Res::Ok();
}

Res CAuctionHistoryView::EraseAuctionHistory(const AuctionHistoryKey &key) {
    EraseBy<ByAuctionHistoryKey>(key);
    return Res::Ok();
}
