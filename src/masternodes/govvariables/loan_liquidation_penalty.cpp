// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/loan_liquidation_penalty.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res LOAN_LIQUIDATION_PENALTY::Import(const UniValue & val)
{
    penalty = AmountFromValue(val);
    return Res::Ok();
}

UniValue LOAN_LIQUIDATION_PENALTY::Export() const
{
    return ValueFromAmount(penalty);
}

Res LOAN_LIQUIDATION_PENALTY::Validate(const CCustomCSView & view) const
{
    Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningHeight, "Cannot be set before FortCanning");

    Require(penalty >= COIN / 100, "Penalty cannot be less than 0.01 DFI");

    return Res::Ok();
}

Res LOAN_LIQUIDATION_PENALTY::Apply(CCustomCSView & mnview, uint32_t height)
{
    return mnview.SetLoanLiquidationPenalty(penalty);
}
