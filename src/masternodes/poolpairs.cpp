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

    auto poolPairByID = GetPoolPair(poolID);
    auto poolPairByTokens = GetPoolPair(pool.idTokenA, pool.idTokenB);

    if(!poolPairByID && poolPairByTokens)
    {
        return Res::Err("Error, there is already a poolpairwith same tokens, but different poolId");
    }
    //create new
    if(!poolPairByID && !poolPairByTokens)
    {//no ByID and no ByTokens
        WriteBy<ByID>(WrapVarInt(poolID.v), pool);
        WriteBy<ByPair>(ByPairKey{pool.idTokenA, pool.idTokenB}, WrapVarInt(poolID.v));
        WriteBy<ByPair>(ByPairKey{pool.idTokenB, pool.idTokenA}, WrapVarInt(poolID.v));
        return Res::Ok();
    }
    //update
    if(poolPairByTokens && poolID == poolPairByTokens->first && poolPairByTokens->second.idTokenA == pool.idTokenA && poolPairByTokens->second.idTokenB == pool.idTokenB)
    {//if pool exists and parameters are the same -> update
        WriteBy<ByID>(WrapVarInt(poolID.v), pool);
        return Res::Ok();
    }
    else if (poolPairByTokens && poolID != poolPairByTokens->first)
    {
        return Res::Err("Error, PoolID is incorrect");
    }
    else if (poolPairByTokens && (poolPairByTokens->second.idTokenA != pool.idTokenA || poolPairByTokens->second.idTokenB == pool.idTokenB))
    {
        throw std::runtime_error("Error, idTokenA or idTokenB is incorrect.");
    }

    return Res::Err("Error: Couldn't create/update pool pair.");
}

boost::optional<CPoolPair> CPoolPairView::GetPoolPair(const DCT_ID &poolId) const
{
    DCT_ID poolID = poolId;
    return ReadBy<ByID, CPoolPair>(WrapVarInt(poolID.v));
}

boost::optional<std::pair<DCT_ID, CPoolPair> > CPoolPairView::GetPoolPair(const DCT_ID &tokenA, const DCT_ID &tokenB) const
{
    ByPairKey key {tokenA, tokenB};
    auto poolId = ReadBy<ByPair, DCT_ID>(key);
    if(poolId) {
        auto poolPair = ReadBy<ByID, CPoolPair>(WrapVarInt(poolId->v));
        if(poolPair)
            return { std::make_pair(*poolId, *poolPair) };
    }
    return {};
}

void CPoolPairView::ForEachPoolPair(std::function<bool(const DCT_ID &, const CPoolPair &)> callback, DCT_ID const & start) {
    DCT_ID poolId = start;
    auto hint = WrapVarInt(poolId.v);

    ForEach<ByID, CVarInt<VarIntMode::DEFAULT, uint32_t>, CPoolPair>([&poolId, &callback] (CVarInt<VarIntMode::DEFAULT, uint32_t> const &, CPoolPair & pool) {
        return callback(poolId, pool);
    }, hint);
}

//Res CPoolPairView::AddLiquidity(CTokenAmount const & amountA, CTokenAmount amountB, CScript const & shareAddress)
//{
//    return Res::Ok();
//}
