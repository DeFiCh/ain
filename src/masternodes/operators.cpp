// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/operators.h>
#include <rpc/protocol.h>
#include <chainparams.h>

const unsigned char COperatorView::ById::prefix = '0'; // the 0 (zero) for Operator

CAmount GetOperatorCreationFee(int)
{
    return Params().GetConsensus().oprtr.creationFee;
}

COperator::COperator(const CCreateOperatorMessage& msg) : CCreateOperatorMessage(msg)
{}

Res COperatorView::CreateOperator(const COperatorId& operatorId, const COperator& opertr)
{
    if (!WriteBy<ById>(operatorId, opertr)) {
        return Res::Err("failed to create new operator <%s>", operatorId.GetHex());
    }

    return Res::Ok();
}

Res COperatorView::UpdateOperator(const COperatorId& operatorId, const COperator& newOperator)
{
    COperator opertr(CCreateOperatorMessage{});
    //check operator in the database
    if (!ReadBy<ById>(operatorId, opertr)) {
        return Res::Err("operator <%s> not found", operatorId.GetHex());
    }

    opertr.operatorAddress = std::move(newOperator.operatorAddress);
    opertr.operatorName = newOperator.operatorName;
    opertr.operatorURL = newOperator.operatorURL;
    opertr.operatorState = newOperator.operatorState;

    // write
    if (!WriteBy<ById>(operatorId, opertr)) {
        return Res::Err("failed to save operator <%s>", operatorId.GetHex());
    }

    return Res::Ok();
}

Res COperatorView::RemoveOperator(const COperatorId& operatorId)
{
    //check operator in the database
    if (!ExistsBy<ById>(operatorId)) {
        return Res::Err("operator <%s> not found", operatorId.GetHex());
    }

    // remove operator
    if (!EraseBy<ById>(operatorId)) {
        return Res::Err("failed to remove operator <%s>", operatorId.GetHex());
    }

    return Res::Ok();
}

ResVal<COperator> COperatorView::GetOperatorData(const COperatorId& operatorId) const
{
    COperator opertr(CCreateOperatorMessage{});
    if (!ReadBy<ById>(operatorId, opertr)) {
        return Res::Err("operator <%s> not found", operatorId.GetHex());
    }

    return ResVal<COperator>(opertr, Res::Ok());
}

void COperatorView::ForEachOperator(std::function<bool(const COperatorId&, CLazySerialize<COperator>)> callback, const COperatorId& start)
{
    ForEach<ById, COperatorId, COperator>(callback, start);
}
