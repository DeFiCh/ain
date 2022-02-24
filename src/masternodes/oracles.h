// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORACLES_H
#define DEFI_MASTERNODES_ORACLES_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/balances.h>
#include <masternodes/res.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

#include <string>
#include <vector>

using COracleId = uint256;
using CPriceTimePair = std::pair<CAmount, int64_t>;
using CTokenCurrencyPair = std::pair<std::string, std::string>;
using CTokenPrices = std::map<std::string, std::map<std::string, CAmount>>;
using CTokenPricePoints = std::map<std::string, std::map<std::string, CPriceTimePair>>;

struct CAppointOracleMessage {
    CScript oracleAddress;
    uint8_t weightage;
    std::set<CTokenCurrencyPair> availablePairs;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(oracleAddress);
        READWRITE(weightage);
        READWRITE(availablePairs);
    }
};

struct CRemoveOracleAppointMessage {
    COracleId oracleId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(oracleId);
    }
};

struct CUpdateOracleAppointMessage {
    COracleId oracleId;
    CAppointOracleMessage newOracleAppoint;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(oracleId);
        READWRITE(newOracleAppoint);
    }
};

struct CSetOracleDataMessage {
    COracleId oracleId;
    int64_t timestamp;
    CTokenPrices tokenPrices;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(oracleId);
        READWRITE(timestamp);
        READWRITE(tokenPrices);
    }
};

/// Oracle representation
struct COracle : public CAppointOracleMessage {
    CTokenPricePoints tokenPrices;

    bool SupportsPair(const std::string& token, const std::string& currency) const;
    Res SetTokenPrice(const std::string& token, const std::string& currency, CAmount amount, int64_t timestamp);
    ResVal<CAmount> GetTokenPrice(const std::string& token, const std::string& currency);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITEAS(CAppointOracleMessage, *this);
        READWRITE(tokenPrices);
    }
};

struct CFixedIntervalPrice
{
    CTokenCurrencyPair priceFeedId;
    int64_t timestamp;
    std::vector<CAmount> priceRecord{0, 0}; // priceHistory[0] = active price, priceHistory[1] = next price
    bool isLive(const CAmount deviationThreshold) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(priceFeedId);
        READWRITE(timestamp);
        READWRITE(priceRecord);
    }
};

/// View for managing oracles and their data
class COracleView : public virtual CStorageView
{
public:
    /// register new oracle instance
    Res AppointOracle(const COracleId& oracleId, const COracle& oracle);

    /// updates oracle info
    Res UpdateOracle(const COracleId& oracleId, COracle&& newOracle);

    /// remove oracle instancefrom database
    Res RemoveOracle(const COracleId& oracleId);

    /// store registered oracle data
    Res SetOracleData(const COracleId& oracleId, int64_t timestamp, const CTokenPrices& tokenPrices);

    /// deserialize oracle instance from database
    ResVal<COracle> GetOracleData(const COracleId& oracleId) const;

    void ForEachOracle(std::function<bool(const COracleId&, CLazySerialize<COracle>)> callback, const COracleId& start = {});

    Res SetFixedIntervalPrice(const CFixedIntervalPrice& PriceFeed);

    ResVal<CFixedIntervalPrice> GetFixedIntervalPrice(const CTokenCurrencyPair& priceFeedId);

    void ForEachFixedIntervalPrice(std::function<bool(const CTokenCurrencyPair&, CLazySerialize<CFixedIntervalPrice>)> callback, const CTokenCurrencyPair& start = {});

    Res SetPriceDeviation(const uint32_t deviation);
    CAmount GetPriceDeviation() const;

    Res SetIntervalBlock(const uint32_t blockInterval);
    uint32_t GetIntervalBlock() const;

    struct ByName { static constexpr uint8_t prefix() { return 'O'; } };
    struct PriceDeviation { static constexpr uint8_t prefix() { return 'Y'; } };
    struct FixedIntervalBlockKey { static constexpr uint8_t prefix() { return 'z'; } };
    struct FixedIntervalPriceKey { static constexpr uint8_t prefix() { return 'y'; } };
};

#endif // DEFI_MASTERNODES_ORACLES_H
