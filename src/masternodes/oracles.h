// Copyright (c) 2021 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORACLES_H
#define DEFI_MASTERNODES_ORACLES_H

#include <string>
#include <vector>

#include <flushablestorage.h>

#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/factory.h>
#include <masternodes/res.h>
#include <serialize.h>
#include <script/script.h>
#include <uint256.h>
#include <univalue/include/univalue.h>

using CPricePoint = std::pair<CAmount, int64_t>;

using CTokenPricePoints = std::map<DCT_ID, std::map<CURRENCY_ID, CPricePoint>>;

using CTokenPrices = std::map<DCT_ID, std::map<CURRENCY_ID, CAmount>>;

class COracleId : public uint256 {
public:
    COracleId() {
        std::fill_n(begin(), size(), 0);
    }

    explicit COracleId(const uint256 &rawId) {
        std::copy_n(rawId.begin(), size(), begin());
    }

    COracleId(const COracleId &other) {
        std::copy_n(other.begin(), size(), begin());
    }

    explicit COracleId(const std::vector<unsigned char> &rawData) :
            uint256(rawData) {}

    /**
     * @brief parse oracle id from hex string
     * @param str value to parse
     * @return true if provided argument is a valid 32 bytes hex string, false otherwise
     */
    bool parseHex(const std::string &str);

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITEAS(uint256, *this);
    }
};

struct CAppointOracleMessage {
    CScript oracleAddress;
    uint8_t weightage;
    std::set<std::pair<DCT_ID, CURRENCY_ID>> availablePairs;

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(oracleAddress);
        READWRITE(weightage);
        READWRITE(availablePairs);
    }
};

struct CRemoveOracleAppointMessage {
    COracleId oracleId{};

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(oracleId);
    }
};

struct CUpdateOracleAppointMessage {
    COracleId oracleId;
    CAppointOracleMessage newOracleAppoint;

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(newOracleAppoint);
    }
};

struct CSetOracleDataMessage {
    COracleId oracleId;
    int64_t timestamp;
    CTokenPrices tokenPrices;

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
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
struct OracleFields {
    static constexpr auto Currency = "currency";
    static constexpr auto Token = "token";
    static constexpr auto OracleId = "oracleid";
    static constexpr auto Timestamp = "timestamp";
    static constexpr auto Weightage = "weightage";
    static constexpr auto State = "state";
    static constexpr auto Price = "rawprice";
    static constexpr auto Alive = "live";
    static constexpr auto Expired = "expired";
    static constexpr auto USD = "USD";
    static constexpr auto EUR = "EUR";
};

/// Oracle representation
struct COracle : public CAppointOracleMessage {
    COracleId oracleId;
    CTokenPricePoints tokenPrices;

    explicit COracle(const COracleId &oracleId = {}, CAppointOracleMessage const &msg = {}) :
            CAppointOracleMessage(msg),
            oracleId{oracleId},
            tokenPrices{} {
    }

    virtual ~COracle() = default;

    inline bool SupportsPair(DCT_ID token, CURRENCY_ID currency) const {
        return availablePairs.find(std::make_pair(token, currency)) != availablePairs.end();
    }

    bool operator==(const COracle &other) const {
        return oracleId == other.oracleId && tokenPrices == other.tokenPrices;
    }

    bool operator!=(const COracle &other) const {
        return !(*this == other);
    }

    Res SetTokenPrice(DCT_ID tokenId, CURRENCY_ID currencyId, CAmount amount, int64_t timestamp);

    boost::optional<CPricePoint> GetTokenPrice(DCT_ID tokenId, CURRENCY_ID currencyId) const;

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(oracleId);
        READWRITE(oracleAddress);
        READWRITE(availablePairs);
        READWRITE(weightage);
        READWRITE(tokenPrices);
    }
};

/// View for managing oracles and their data
class COracleView : public virtual CStorageView {
public:
    COracleView() :
            _allOraclesKey{"7cb9109f1f4b17b91e7eefad33e8d795"} {}

    ~COracleView() override = default;

    /// register new oracle instance
    Res AppointOracle(const COracleId &oracleId, const COracle &oracle);

    /// updates oracle info
    Res UpdateOracle(const COracleId &oracleId, const COracle &oracle);

    /// remove oracle instancefrom database
    Res RemoveOracle(const COracleId &oracleId);

    /// store registered oracle data
    Res SetOracleData(const COracleId &oracleId, int64_t timestamp, const CTokenPrices &tokenPrices);

    /// deserialize oracle instance from database
    ResVal<COracle> GetOracleData(const COracleId &oracleId) const;

    /// get collection of all oracle ids
    std::vector<COracleId> GetAllOracleIds();

private:
    struct ByName {
        static const unsigned char prefix;
    };
    /// oracles list key
    const std::string _allOraclesKey;

    /// add oracle to the list
    Res AddOracleId(const COracleId &oracleId);

    /// remove oracle from the list
    Res RemoveOracleId(const COracleId &oracleId);

    /// update oracles colection
    Res UpdateOraclesList(const std::vector<COracleId> &oraclesList);
};

#endif
