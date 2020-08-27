// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/poolpairs.h>
#include <core_io.h>
#include <primitives/transaction.h>

const unsigned char CPoolPairView::ByID          ::prefix = 'i';
const unsigned char CPoolPairView::ByPair        ::prefix = 'j';
const unsigned char CPoolPairView::ByShare       ::prefix = 'k';

Res CPoolPairView::SetPoolPair(DCT_ID &poolId, const CPoolPair &pool)
{
    WriteBy<ByID>(WrapVarInt(poolId.v), pool);
    WriteBy<ByPair>(pool.poolPairMsg.pairSymbol, WrapVarInt(poolId.v));
    return Res::Ok();
}

//Res CPoolPairView::AddLiquidity(CTokenAmount const & amountA, CTokenAmount amountB, CScript const & shareAddress)
//{
//    return Res::Ok();
//}
