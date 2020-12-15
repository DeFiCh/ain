// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ACCOUNTSHISTORY_H
#define DEFI_MASTERNODES_ACCOUNTSHISTORY_H

#include <flushablestorage.h>
#include <masternodes/res.h>
#include <amount.h>
#include <script/script.h>
#include <uint256.h>


struct AccountHistoryKey {
    CScript owner;
    uint32_t blockHeight;
    uint32_t txn; // for order in block

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(owner);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
            READWRITE(WrapBigEndian(txn));
            txn = ~txn;
        }
        else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
            uint32_t txn_ = ~txn;
            READWRITE(WrapBigEndian(txn_));
        }
    }
};

struct AccountHistoryValue {
    uint256 txid;
    unsigned char category;
    TAmounts diff;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(category);
        READWRITE(diff);
    }
};

struct RewardHistoryKey {
    CScript owner;
    uint32_t blockHeight;
    DCT_ID poolID; // for order in block

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(owner);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        }
        else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }

        READWRITE(VARINT(poolID.v));
    }
};

struct RewardHistoryValue {
    unsigned char category;
    TAmounts diff;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(category);
        READWRITE(diff);
    }
};

class CAccountsHistoryView : public virtual CStorageView
{
public:
    Res SetAccountHistory(AccountHistoryKey const & key, AccountHistoryValue const & value);
    void ForEachAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start = {}) const;

    // tags
    struct ByAccountHistoryKey { static const unsigned char prefix; };
};

class CRewardsHistoryView : public virtual CStorageView
{
public:
    Res SetRewardHistory(RewardHistoryKey const & key, RewardHistoryValue const & value);
    void ForEachRewardHistory(std::function<bool(RewardHistoryKey const &, CLazySerialize<RewardHistoryValue>)> callback, RewardHistoryKey const & start = {}) const;

    // tags
    struct ByRewardHistoryKey { static const unsigned char prefix; };
};

#endif //DEFI_MASTERNODES_ACCOUNTSHISTORY_H
