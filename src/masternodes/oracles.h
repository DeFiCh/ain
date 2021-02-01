// Copyright (c) 2021 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORACLES_H
#define DEFI_MASTERNODES_ORACLES_H

#include <string>

#include <flushablestorage.h>

#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/factory.h>
#include <masternodes/res.h>
#include <serialize.h>
#include <script/script.h>
#include <uint256.h>
#include <univalue/include/univalue.h>

using CTimeStamp = uint32_t;

using CRawPrice = std::pair<CAmount, CTimeStamp>;

using CTokenPrices = std::map<DCT_ID, CRawPrice>;

using COracleId = uint256;

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
    COracleId oracleId;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
    }
};

struct CUpdateOracleAppointMessage {
    COracleId oracleId;
    CAppointOracleMessage newOracleAppoint;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(newOracleAppoint);
    }
};

struct CSetOracleDataMessage {
    COracleId oracleId;
    CTokenPrices tokenPrices;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(tokenPrices);
    }
};

struct COracle : public CAppointOracleMessage {
    CTokenPrices tokenPrices;

    explicit COracle(CAppointOracleMessage const & msg = {}) :
        CAppointOracleMessage(msg),
        tokenPrices{} {}

    virtual ~COracle() = default;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleAddress);
        READWRITE(availableTokens);
        READWRITE(tokenPrices);
    }
};

//struct CPriceFeed {
//    CTimeStamp timestamp{};
//    CAmount value{};
//
//    ADD_SERIALIZE_METHODS;
//
//    template <typename Stream, typename Operation>
//    inline void SerializationOp(Stream& s, Operation ser_action) {
//        READWRITE(timestamp);
//        READWRITE(value);
//    }
//};

class COracleView: public virtual CStorageView {
public:
    ~COracleView() override = default;

    Res RegisterOracle(const COracle& oracle);

    Res SetOracleData(COracleId oracleId, const CTokenPrices& tokenPrices);

    ResVal<CRawPrice> GetTokenRawPrice(DCT_ID tokenId, COracleId oracleId);

    Res SetTokenRawPrice(COracleId oracleId, DCT_ID tokenId, CRawPrice rawPrice);

    struct ByName { static const unsigned char prefix; };

private:
    bool isAllowedToken(DCT_ID tokenId, COracleId oracleId);
};

#endif
