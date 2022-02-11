// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/oracle_block_interval.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res ORACLE_BLOCK_INTERVAL::Import(const UniValue & val)
{
    if (!val.isNum())
        throw JSONRPCError(RPC_TYPE_ERROR, "Block interval amount is not a number");

    blockInterval = val.get_int();
    return Res::Ok();
}

UniValue ORACLE_BLOCK_INTERVAL::Export() const
{
    return static_cast<uint64_t>(blockInterval);
}

Res ORACLE_BLOCK_INTERVAL::Validate(const CCustomCSView & view) const
{
    if (view.GetLastHeight() < Params().GetConsensus().FortCanningHeight)
        return Res::Err("Cannot be set before FortCanning");

    if (blockInterval <= 0)
        return Res::Err("Block interval cannot be less than 1");

    return Res::Ok();
}

Res ORACLE_BLOCK_INTERVAL::Apply(CCustomCSView & mnview, const uint32_t height)
{
    return mnview.SetIntervalBlock(blockInterval);
}
