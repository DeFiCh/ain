// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/poolpairs.h>
#include <core_io.h>
#include <primitives/transaction.h>

const unsigned char CPoolPairView::ByID          ::prefix = 'i';
const unsigned char CPoolPairView::ByPair        ::prefix = 'j';
const unsigned char CPoolPairView::ByShare       ::prefix = 'k';

Res CPoolPairView::SetPoolPair(DCT_ID const & poolId, CPoolPair const & pool)
{
    DCT_ID poolID = poolId;
    if(pool.idTokenA == pool.idTokenB)
        return Res::Err("Error: tokens IDs are the same.");

    if(GetPoolPair(pool.idTokenA, pool.idTokenB) || !GetPoolPair(poolId))
    {//if pool exists and data is correct || pool doesn't exist
        WriteBy<ByID>(WrapVarInt(poolID.v), pool);

        if(pool.idTokenA < pool.idTokenB)
            WriteBy<ByPair>(ByPairKey{pool.idTokenA, pool.idTokenB}, WrapVarInt(poolID.v));
        else
            WriteBy<ByPair>(ByPairKey{pool.idTokenB, pool.idTokenA}, WrapVarInt(poolID.v));

        return Res::Ok();
    }
    return Res::Err("Error: Couldn't create/change pool pair.");
}

boost::optional<CPoolPair> CPoolPairView::GetPoolPair(const DCT_ID &poolId) const
{
    DCT_ID poolID = poolId;
    return ReadBy<ByID, CPoolPair>(WrapVarInt(poolID.v));
}

boost::optional<std::pair<DCT_ID, CPoolPair> > CPoolPairView::GetPoolPair(const DCT_ID &tokenA, const DCT_ID &tokenB) const
{
    ByPairKey key;
    if (tokenA < tokenB)
        key = {tokenA, tokenB};
    else
        key = {tokenB, tokenA};

    auto poolId = ReadBy<ByPair, DCT_ID>(key);
    if(poolId) {
        auto poolPair = ReadBy<ByID, CPoolPair>(WrapVarInt(poolId->v));
        if(poolPair)
            return { std::make_pair(*poolId, *poolPair) };
    }
    return {};
}

//Res CPoolPairView::AddLiquidity(CTokenAmount const & amountA, CTokenAmount amountB, CScript const & shareAddress)
//{
//    return Res::Ok();
//}
