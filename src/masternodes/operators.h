// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_OPERATORS_H
#define DEFI_OPERATORS_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/balances.h>
#include <masternodes/res.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

#include <string>
#include <vector>

using COperatorId = uint256;

enum OperatorState {
    INVALID,
    DRAFT,
    ACTIVE,
};

CAmount GetOperatorCreationFee(int height);

struct CCreateOperatorMessage {
    CScript operatorAddress;
    std::string operatorName;
    std::string operatorURL;
    // OperatorState operatorState;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(operatorAddress);
        READWRITE(operatorName);
        READWRITE(operatorURL);
        // READWRITE(operatorState);
    }
};

struct CUpdateOperatorMessage {
    COperatorId operatorId;
    CCreateOperatorMessage newOperator;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(operatorId);
        READWRITE(newOperator);
    }
};

//Operator
struct COperator : public CCreateOperatorMessage {
    COperator(const CCreateOperatorMessage& msg = {});

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITEAS(CCreateOperatorMessage, *this);
    }
};

// View to managing operators and their data
class COperatorView : public virtual CStorageView
{
public:
    ~COperatorView() override = default;

    // create new operator
    Res CreateOperator(const COperatorId& operatorId, const COperator& opertr);

    // update operaror info
    Res UpdateOperator(const COperatorId& operatorId, const COperator& newOperator);

    // remove operator from database
    Res RemoveOperator(const COperatorId& operatorId);

    /// deserialize operator instance from database
    ResVal<COperator> GetOperatorData(const COperatorId& operatorId) const;

    void ForEachOperator(std::function<bool(const COperatorId&, CLazySerialize<COperator>)> callback, const COperatorId& start = {});

private:
    struct ById { static const unsigned char prefix; };
};

#endif // DEFI_OPERATORS_H