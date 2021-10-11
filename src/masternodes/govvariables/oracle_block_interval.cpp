// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/govvariables/oracle_block_interval.h>

#include <core_io.h> /// ValueFromAmount
#include <masternodes/masternodes.h> /// CCustomCSView
#include <rpc/util.h> /// AmountFromValue


Res ORACLE_BLOCK_INTERVAL::Import(const UniValue & val) {
    blockInterval = AmountFromValue(val);
    return Res::Ok();
}

UniValue ORACLE_BLOCK_INTERVAL::Export() const {
    return ValueFromAmount(blockInterval);
}

Res ORACLE_BLOCK_INTERVAL::Validate(const CCustomCSView & view) const
{
    return Res::Err("Cannot be set manually.");
}

Res ORACLE_BLOCK_INTERVAL::Apply(CCustomCSView & mnview, const uint32_t blockInterval)
{
    return mnview.SetIntervalBlock(blockInterval);
}
