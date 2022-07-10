// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_AUCTIONHISTORY_H
#define DEFI_MASTERNODES_AUCTIONHISTORY_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/masternodes.h>
#include <masternodes/vault.h>
#include <script/script.h>

struct AuctionHistoryKey {
    uint32_t blockHeight;
    CScript owner;
    CVaultId vaultId;
    uint32_t index;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        } else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }

        READWRITE(owner);
        READWRITE(vaultId);
        READWRITE(index);
    }
};

struct AuctionHistoryValue {
    CTokenAmount bidAmount;
    TAmounts collaterals;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(bidAmount);
        READWRITE(collaterals);
    }
};

class CAuctionHistoryView : public virtual CStorageView {
   public:
    Res WriteAuctionHistory(AuctionHistoryKey const &key, AuctionHistoryValue const &value);
    Res EraseAuctionHistory(AuctionHistoryKey const &key);
    Res EraseAuctionHistoryHeight(uint32_t height);
    void ForEachAuctionHistory(
        std::function<bool(AuctionHistoryKey const &, CLazySerialize<AuctionHistoryValue>)> callback,
        AuctionHistoryKey const &start = {});

    // tags
    struct ByAuctionHistoryKey {
        static constexpr uint8_t prefix() { return 'a'; }
    };
};

#endif  // DEFI_MASTERNODES_AUCTIONHISTORY_H
