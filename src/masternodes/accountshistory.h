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

class CAccountsHistoryView : public virtual CStorageView
{
public:
    Res SetAccountHistory(CScript const & owner, uint32_t height, uint32_t txn, uint256 const & txid, unsigned char category, TAmounts const & diff);
    void ForEachAccountHistory(std::function<bool(CScript const & owner, uint32_t height, uint32_t txn, uint256 const & txid, unsigned char category, TAmounts const & diff)> callback, AccountHistoryKey start) const;
    bool TrackAffectedAccounts(CStorageKV const & before, MapKV const & diff, uint32_t height, uint32_t txn, const uint256 & txid, unsigned char category);

    // tags
    struct ByAccountHistoryKey { static const unsigned char prefix; };
};

#endif //DEFI_MASTERNODES_ACCOUNTSHISTORY_H
