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

using CTokenPrices = std::map<DCT_ID, CPricePoint>;

using COracleId = uint256;

struct CAppointOracleMessage {
    CScript oracleAddress;
    uint8_t weightage;
    std::set<DCT_ID> availableTokens;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracleAddress);
        READWRITE(weightage);
        READWRITE(availableTokens);
    }
};

struct CRemoveOracleAppointMessage {
    COracleId oracleId{};

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
    int64_t timestamp;
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
    COracleId oracleId;
    CTokenPrices tokenPrices;

    explicit COracle(COracleId oracleId = {}, CAppointOracleMessage const & msg = {}) :
        CAppointOracleMessage(msg),
        oracleId{oracleId},
        tokenPrices{} {
    }

    virtual ~COracle() = default;

    inline bool SupportsToken(const DCT_ID& tokenId) const {
        return availableTokens.find(tokenId) != availableTokens.end();
    }

    Res SetTokenPrice(DCT_ID tokenId, CAmount amount, int64_t timestamp);

    boost::optional<CPricePoint> GetTokenPrice(DCT_ID tokenId) const;

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
    COracleView();

    ~COracleView() override = default;

    /// register new oracle instance
    Res AppointOracle(COracleId oracleId, const COracle& oracle);

    /// updates oracle info
    Res UpdateOracle(COracleId oracleId, const COracle& oracle);

    /// remove oracle instancefrom database
    Res RemoveOracle(COracleId oracleId);

    /// store registered oracle data
    Res SetOracleData(COracleId oracleId, int64_t timestamp, const CBalances& tokenPrices);

    /// deserialize oracle instance from database
    ResVal<COracle> GetOracleData(COracleId oracleId) const;

    /// get collection of all oracle ids
    std::vector<COracleId> GetAllOracleIds();

private:
    struct ByName { static const unsigned char prefix; };
    /// oracles list key
    const std::string _allOraclesKey;

    /// add oracle to the list
    Res AddOracleId(COracleId oracleId);

    /// remove oracle from the list
    Res RemoveOracleId(COracleId oracleId);

    /// update oracles colection
    Res UpdateOraclesList(const std::vector<COracleId>& oraclesList);
};

#endif
