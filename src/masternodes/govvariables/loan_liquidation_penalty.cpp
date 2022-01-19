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
    if (view.GetLastHeight() < Params().GetConsensus().FortCanningHeight)
        return Res::Err("Cannot be set before FortCanning");

    if (penalty < COIN / 100)
        return Res::Err("Penalty cannot be less than 0.01 DFI");

    return Res::Ok();
}

Res LOAN_LIQUIDATION_PENALTY::Apply(CCustomCSView & mnview, uint32_t height)
{
    return mnview.SetLoanLiquidationPenalty(penalty);
}
