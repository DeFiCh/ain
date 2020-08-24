// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_POOLPAIRS_H
#define DEFI_MASTERNODES_POOLPAIRS_H

#include <flushablestorage.h>

#include <amount.h>
#include <masternodes/res.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>


class CPoolPair
{
public:
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {

    }
};

class CPoolPairView : public virtual CStorageView
{
public:

};


#endif // DEFI_MASTERNODES_POOLPAIRS_H
