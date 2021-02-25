// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_REWARDHISTORYOLD_H
#define DEFI_MASTERNODES_REWARDHISTORYOLD_H

#include <amount.h>
#include <flushablestorage.h>
#include <script/script.h>

// key before migration
const uint8_t oldRewardHistoryPrefix = 'R';

struct oldRewardHistoryKey {
    CScript owner;
    uint32_t blockHeight;
    DCT_ID poolID;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(owner);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        } else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }

        READWRITE(VARINT(poolID.v));
    }
};

#endif //DEFI_MASTERNODES_REWARDHISTORYOLD_H
