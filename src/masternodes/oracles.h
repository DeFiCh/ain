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


using CTokenPrices = std::map<DCT_ID, std::pair<CAmount, uint64_t>>;


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


class CPriceFeedNameValidator {
public:
    virtual ~CPriceFeedNameValidator() = default;

    virtual bool IsValidPriceFeedName(const std::string& priceFeed) const = 0;
};


using CTimeStamp = uint32_t;


struct CPriceFeed {
    CTimeStamp timestamp{};
    CAmount value{};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(timestamp);
        READWRITE(value);
    }
};


class COracleView: public virtual CStorageView {
public:
    explicit COracleView(std::shared_ptr<CPriceFeedNameValidator> priceFeedValidator);

    ~COracleView() override = default;

    bool SetPriceFeedValue(const std::string& feedName, CTimeStamp timestamp, double rawPrice);

    ResVal<CPriceFeed> GetPriceFeedValue(const std::string& feedName);

    /// check if price feed name exists
    bool ExistPriceFeed(const std::string& feedName) const;

    struct ByName { static const unsigned char prefix; };

private:
    std::shared_ptr<CPriceFeedNameValidator> _validator;
};

#endif
