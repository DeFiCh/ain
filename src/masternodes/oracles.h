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

using BTCTimeStamp = int64_t;

using CPricePoint = std::pair<CAmount, BTCTimeStamp>;

using CTokenPrices = std::map<DCT_ID, CPricePoint>;

using COracleId = uint256;

struct CAppointOracleMessage {
    COracleId oracleId;
    CScript oracleAddress;
    uint8_t weightage;
    std::set<DCT_ID> availableTokens;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(oracleAddress);
        READWRITE(weightage);
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
    BTCTimeStamp timestamp{};
    CBalances balances;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(timestamp);
        READWRITE(balances);
    }
};

struct COracle : public CAppointOracleMessage {

    CTokenPrices tokenPrices;

    explicit COracle(CAppointOracleMessage msg = {}) :
        CAppointOracleMessage(std::move(msg)),
        tokenPrices{} {
    }

    virtual ~COracle() = default;

    inline bool SupportsToken(const DCT_ID& tokenId) const {
        return availableTokens.find(tokenId) != availableTokens.end();
    }

    Res SetTokenPrice(DCT_ID tokenId, CAmount amount, BTCTimeStamp timestamp) {
        if (!SupportsToken(tokenId)) {
            return Res::Err("token <%s> is not allowed", tokenId.ToString());
        }

        tokenPrices[tokenId] = std::make_pair(amount, timestamp);

        return Res::Ok();
    }

    boost::optional<CPricePoint> GetTokenPrice(DCT_ID tokenId) const {
        if (SupportsToken(tokenId) && tokenPrices.find(tokenId) != tokenPrices.end()) {
            return tokenPrices.at(tokenId);
        }

        return {};
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(oracleAddress);
        READWRITE(availableTokens);
        READWRITE(weightage);
        READWRITE(tokenPrices);
    }
};

class COracleView: public virtual CStorageView {
public:
    ~COracleView() override = default;

    /// register new oracle instance
    Res AppointOracle(COracleId oracleId, const COracle& oracle);

    /// store registered oracle data
    Res SetOracleData(COracleId oracleId, BTCTimeStamp timestamp, const CBalances& tokenPrices);

    /// deserialize oracle instance from database
    ResVal<COracle> GetOracleData(COracleId oracleId) const;

    /// remove oracle instancefrom database
    bool RemoveOracle(COracleId oracleId);

    struct ByName { static const unsigned char prefix; };
};

#endif
