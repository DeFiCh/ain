// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/oracle_deviation.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue

bool ORACLE_DEVIATION::IsEmpty() const
{
    return !deviation.has_value();
}

Res ORACLE_DEVIATION::Import(const UniValue & val)
{
    CAmount amount;
    if (!AmountFromValue(val, amount)) {
        return Res::Err("Invalid amount");
    }
    deviation = amount;
    return Res::Ok();
}

UniValue ORACLE_DEVIATION::Export() const
{
    return ValueFromAmount(deviation.value_or(0));
}

Res ORACLE_DEVIATION::Validate(const CCustomCSView & view) const
{
    Require(view.GetLastHeight() >= Params().GetConsensus().FortCanningHeight, "Cannot be set before FortCanning");

    Require(deviation >= COIN / 100, "Deviation cannot be less than 1 percent");

    return Res::Ok();
}

Res ORACLE_DEVIATION::Apply(CCustomCSView & mnview, uint32_t height)
{
    return mnview.SetPriceDeviation(deviation.value_or(0));
}

Res ORACLE_DEVIATION::Erase(CCustomCSView & mnview, uint32_t, std::vector<std::string> const &)
{
    deviation.reset();
    return mnview.ErasePriceDeviation();
}
