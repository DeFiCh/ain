// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ACCOUNTSHISTORY_H
#define DEFI_MASTERNODES_ACCOUNTSHISTORY_H

#include <amount.h>
#include <flushablestorage.h>
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
    uint8_t category;

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

        READWRITE(category);
    }
};

using RewardHistoryValue = std::map<DCT_ID, TAmounts>;

class CAccountsHistoryView : public virtual CStorageView
{
public:
    Res SetMineAccountHistory(AccountHistoryKey const & key, AccountHistoryValue const & value);
    Res SetAllAccountHistory(AccountHistoryKey const & key, AccountHistoryValue const & value);
    void ForEachMineAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start = {}) const;
    void ForEachAllAccountHistory(std::function<bool(AccountHistoryKey const &, CLazySerialize<AccountHistoryValue>)> callback, AccountHistoryKey const & start = {}) const;

    // tags
    struct ByMineAccountHistoryKey { static const unsigned char prefix; };
    struct ByAllAccountHistoryKey { static const unsigned char prefix; };
};

class CRewardsHistoryView : public virtual CStorageView
{
public:
    Res SetMineRewardHistory(RewardHistoryKey const & key, RewardHistoryValue const & value);
    Res SetAllRewardHistory(RewardHistoryKey const & key, RewardHistoryValue const & value);
    void ForEachMineRewardHistory(std::function<bool(RewardHistoryKey const &, CLazySerialize<RewardHistoryValue>)> callback, RewardHistoryKey const & start = {}) const;
    void ForEachAllRewardHistory(std::function<bool(RewardHistoryKey const &, CLazySerialize<RewardHistoryValue>)> callback, RewardHistoryKey const & start = {}) const;

    // tags
    struct ByMineRewardHistoryKey { static const unsigned char prefix; };
    struct ByAllRewardHistoryKey { static const unsigned char prefix; };
};

static constexpr bool DEFAULT_ACINDEX = false;
static constexpr bool DEFAULT_ACINDEX_MINEONLY = true;

class CCustomCSView;
bool shouldMigrateOldRewardHistory(CCustomCSView & view);

#endif //DEFI_MASTERNODES_ACCOUNTSHISTORY_H
