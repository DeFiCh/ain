// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORACLES_H
#define DEFI_MASTERNODES_ORACLES_H

#include <string>
#include <vector>

#include <flushablestorage.h>

#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/res.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

using CPricePoint = std::pair<CAmount, int64_t>;

using CTokenPricePoints = std::map<DCT_ID, std::map<CURRENCY_ID, CPricePoint>>;

using CTokenPrices = std::map<DCT_ID, std::map<CURRENCY_ID, CAmount>>;

class COracleId : public uint256
{
public:
    COracleId()
    {
        std::fill_n(begin(), size(), 0);
    }

    explicit COracleId(const uint256& rawId)
    {
        std::copy_n(rawId.begin(), size(), begin());
    }

    COracleId(const COracleId& other)
    {
        std::copy_n(other.begin(), size(), begin());
    }

    explicit COracleId(const std::vector<unsigned char>& rawData) : uint256(rawData) {}

    /**
     * @brief parse oracle id from hex string
     * @param str value to parse
     * @return true if provided argument is a valid 32 bytes hex string, false otherwise
     */
    bool parseHex(const std::string& str);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITEAS(uint256, *this);
    }
};

struct TokenCurrencyPair {
    DCT_ID tid{};
    CURRENCY_ID cid{};

    TokenCurrencyPair() = default;
    TokenCurrencyPair(DCT_ID _tid, CURRENCY_ID _cid) : tid{_tid}, cid{_cid} {}

    bool operator==(const TokenCurrencyPair& o) const
    {
        return tid == o.tid && cid == o.cid;
    }

    bool operator!=(const TokenCurrencyPair& o) const
    {
        return tid != o.tid || cid != o.cid;
    }

    bool operator<(const TokenCurrencyPair& o) const
    {
        return tid == o.tid ? cid < o.cid : tid < o.tid;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(tid);
        READWRITE(cid);
    }
};

struct CAppointOracleMessage {
    CScript oracleAddress;
    uint8_t weightage;
    std::set<TokenCurrencyPair> availablePairs;

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
    COracleId oracleId{};

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

/// Oracle states
enum class OracleState : uint8_t {
    EXPIRED,
    ALIVE,
};

/// names of oracle json fields
namespace oraclefields {
constexpr auto Currency = "currency";
constexpr auto Token = "token";
constexpr auto OracleId = "oracleid";
constexpr auto Timestamp = "timestamp";
constexpr auto Weightage = "weightage";
constexpr auto State = "state";
constexpr auto RawPrice = "rawprice";
constexpr auto AggregatedPrice = "price";
constexpr auto Alive = "live";
constexpr auto Expired = "expired";
constexpr auto TokenAmount = "tokenAmount";
constexpr auto Amount = "amount";
constexpr auto ValidityFlag = "ok";
constexpr auto FlagIsValid = true;
constexpr auto FlagIsError = false;
constexpr auto PriceFeeds = "priceFeeds";
constexpr auto OracleAddress = "address";
constexpr auto TokenPrices = "tokenPrices";
constexpr uint8_t MaxWeightage{100u};
constexpr uint8_t MinWeightage{1u};

}; // namespace oraclefields

/// Oracle representation
struct COracle : public CAppointOracleMessage {
    COracleId oracleId;
    CTokenPricePoints tokenPrices;

    explicit COracle(const COracleId& oracleId = {}, CAppointOracleMessage const& msg = {}) : CAppointOracleMessage(msg),
                                                                                              oracleId{oracleId},
                                                                                              tokenPrices{}
    {
    }

    virtual ~COracle() = default;

    bool SupportsPair(DCT_ID token, CURRENCY_ID currency) const
    {
        return availablePairs.find(TokenCurrencyPair{token, currency}) != availablePairs.end();
    }

    bool operator==(const COracle& other) const
    {
        return oracleId == other.oracleId && tokenPrices == other.tokenPrices;
    }

    bool operator!=(const COracle& other) const
    {
        return !(*this == other);
    }

    Res SetTokenPrice(DCT_ID tokenId, CURRENCY_ID currencyId, CAmount amount, int64_t timestamp);

//    boost::optional<CPricePoint> GetTokenPrice(DCT_ID tokenId, CURRENCY_ID currencyId) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(oracleId);
        READWRITE(oracleAddress);
        READWRITE(availablePairs);
        READWRITE(weightage);
        READWRITE(tokenPrices);
    }
};

/// View for managing oracles and their data
class COracleView : public virtual CStorageView
{
public:
    COracleView() : _allOraclesKey{"7cb9109f1f4b17b91e7eefad33e8d795"} {}

    ~COracleView() override = default;

    /// register new oracle instance
    Res AppointOracle(const COracleId& oracleId, const COracle& oracle);

    /// updates oracle info
    Res UpdateOracle(const COracleId& oracleId, const COracle& newOracle);

    /// remove oracle instancefrom database
    Res RemoveOracle(const COracleId& oracleId);

    /// store registered oracle data
    Res SetOracleData(const COracleId& oracleId, int64_t timestamp, const CTokenPrices& tokenPrices);

    /// deserialize oracle instance from database
    ResVal<COracle> GetOracleData(const COracleId& oracleId) const;

    /// get collection of all oracle ids
    std::vector<COracleId> GetAllOracleIds() const;

    ResVal<std::set<TokenCurrencyPair>> GetAllTokenCurrencyPairs() const;

    ResVal<CAmount> GetSingleAggregatedPrice(DCT_ID tid, CURRENCY_ID cid, int64_t lastBlockTime) const;

private:
    struct ByName {
        static const unsigned char prefix;
    };
    /// oracles list key
    const std::string _allOraclesKey;

    /// add oracle to the list
    Res AddOracleId(const COracleId& oracleId);

    /// remove oracle from the list
    Res RemoveOracleId(const COracleId& oracleId);

    /// update oracles colection
    Res UpdateOraclesList(const std::vector<COracleId>& oraclesList);
};

#endif
