// Copyright (c) 2021 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORACLES_H
#define DEFI_MASTERNODES_ORACLES_H

#include <flushablestorage.h>

#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/res.h>
#include <serialize.h>
#include <script/script.h>
#include <uint256.h>

struct CAppointOracleMessage {
    CScript oracleAddress;
    std::vector<DCT_ID> availableTokens; // available tokens to price control

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleAddress);
        READWRITE(availableTokens);
    }
};

struct CRemoveOracleAppointMessage {
    uint256 oracleId;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
    }
};

struct CUpdateOracleAppointMessage {
    uint256 oracleId;
    CAppointOracleMessage newOracleAppoint;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(newOracleAppoint);
    }
};

struct CSetOracleDataMessage {
    uint256 oracleId;
    CBalances tokenPrices;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(tokenPrices);
    }
};

class COracle : public CAppointOracleMessage {
    CBalances tokenPrices;

    COracle(CAppointOracleMessage const & msg = {}) :
        CAppointOracleMessage(msg),
        tokenPrices(CBalances{}) {}

    virtual ~COracle() = default;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(tokenPrices);
    }
};

class COracleView : public virtual CStorageView {

};

#endif