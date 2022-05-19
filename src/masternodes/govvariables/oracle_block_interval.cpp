// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/oracle_block_interval.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue

bool ORACLE_BLOCK_INTERVAL::IsEmpty() const
{
    return !blockInterval.has_value();
}

Res ORACLE_BLOCK_INTERVAL::Import(const UniValue & val)
{
    if (!val.isNum())
        throw JSONRPCError(RPC_TYPE_ERROR, "Block interval amount is not a number");

    blockInterval = val.get_int();
    return Res::Ok();
}

UniValue ORACLE_BLOCK_INTERVAL::Export() const
{
    return static_cast<uint64_t>(blockInterval.value_or(0));
}

Res ORACLE_BLOCK_INTERVAL::Validate(const CCustomCSView & view) const
{
    if (view.GetLastHeight() < Params().GetConsensus().FortCanningHeight)
        return Res::Err("Cannot be set before FortCanning");

    if (blockInterval <= 0)
        return Res::Err("Block interval cannot be less than 1");

    return Res::Ok();
}

Res ORACLE_BLOCK_INTERVAL::Apply(CCustomCSView & mnview, uint32_t height)
{
    return mnview.SetIntervalBlock(blockInterval.value_or(0));
}

Res ORACLE_BLOCK_INTERVAL::Erase(CCustomCSView & mnview, uint32_t, std::vector<std::string> const &)
{
    blockInterval.reset();
    return mnview.EraseIntervalBlock();
}
