// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORDER_H
#define DEFI_MASTERNODES_ORDER_H

#include <amount.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

class CTransaction;

struct CCreateOrderMessage
{
    //! basic properties
    CTokenAmount give; // a.k.a. sell, pay
    CTokenAmount take; // a.k.a. buy, receive
    CTokenAmount premium;

    CScript owner;

    uint32_t timeInForce; // expiry time in blocks

    virtual ~CCreateOrderMessage() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(give);
        READWRITE(take);
        READWRITE(premium);
        READWRITE(owner);
        READWRITE(timeInForce);
    }
};

struct COrder : public CCreateOrderMessage
{
    COrder() = default;
    COrder(CCreateOrderMessage const & other, uint32_t _creationHeight) : CCreateOrderMessage(other), creationHeight(_creationHeight) {};
    uint32_t creationHeight;

    virtual ~COrder() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(static_cast<CCreateOrderMessage&>(*this));
        READWRITE(creationHeight);
    }
};

struct CMatchOrdersMessage
{
    uint256 aliceOrderTx;
    uint256 carolOrderTx;
    CScript matcher;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(aliceOrderTx);
        READWRITE(carolOrderTx);
        READWRITE(matcher);
    }
};

#endif //DEFI_MASTERNODES_ORDER_H
