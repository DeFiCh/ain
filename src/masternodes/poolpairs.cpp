// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
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
    DCT_ID poolId;
    auto varint = WrapVarInt(poolId.v);
    ByPairKey key {tokenA, tokenB};
    if(ReadBy<ByPair, ByPairKey>(key, varint)) {
        auto poolPair = ReadBy<ByID, CPoolPair>(varint);
        if(poolPair)
            return { std::make_pair(poolId, std::move(*poolPair)) };
    }
    return {};
}

Res CPoolPair::Swap(CTokenAmount in, CAmount maxPrice, std::function<Res (const CTokenAmount &tokenAmount)> onTransfer) {
    if (in.nTokenId != idTokenA && in.nTokenId != idTokenB) {
        throw std::runtime_error("Error, input token ID (" + in.nTokenId.ToString() + ") doesn't match pool tokens (" + idTokenA.ToString() + "," + idTokenB.ToString() + ")");
    }
    if (in.nValue <= 0)
        return Res::Err("Input amount should be positive!");

    bool const forward = in.nTokenId == idTokenA;

    if (reserveA <= 0 || reserveB <= 0)
        return Res::Err("Lack of liquidity.");

    CAmount priceAB = (((double) reserveA / PRECISION) / ((double) reserveB / PRECISION)) * PRECISION;
    CAmount priceBA = (((double) reserveB / PRECISION) / ((double) reserveA / PRECISION)) * PRECISION;
    CAmount price = forward ? priceBA : priceAB;
    maxPrice = maxPrice > 0 ? maxPrice : price * 1.03;

    // claim trading fee
    if (commission) {
        CAmount const tradeFee = in.nValue * commission / PRECISION; /// @todo check overflow
        in.nValue -= tradeFee;
        if (forward) {
            blockCommissionA += tradeFee;
        }
        else {
            blockCommissionB += tradeFee;
        }
    }
    CAmount result = forward ? slopeSwap(in.nValue, reserveA, reserveB) : slopeSwap(in.nValue, reserveB, reserveA);
    CAmount realPrice = (((double) result / PRECISION) / ((double) in.nValue / PRECISION)) * PRECISION;
    if (realPrice > maxPrice)
        return Res::Err("Price higher than indicated.");

    swapEvent = true; // (!!!)

    return onTransfer({ forward ? idTokenB : idTokenA, result });
}

CAmount CPoolPair::slopeSwap(arith_uint256 unswapped, CAmount &poolFrom, CAmount &poolTo) {
    assert (unswapped >= 0 && poolFrom > 0 && poolTo > 0);
    arith_uint256 poolF = arith_uint256(poolFrom);
    arith_uint256 poolT = arith_uint256(poolTo);
    arith_uint256 swapped = 0;
    while (unswapped > 0) {
        //arith_uint256 stepFrom = std::min(poolFrom/1000, unswapped); // 0.1%
        arith_uint256 stepFrom = poolF/1000 > unswapped ? unswapped : poolF/1000;
        //CAmount stepTo = poolTo * stepFrom / poolFrom;
        arith_uint256 stepTo = poolT * stepFrom / poolF;
        poolF += stepFrom;
        poolT -= stepTo;
        poolFrom = poolF.GetLow64();
        poolTo = poolT.GetLow64();
        unswapped -= stepFrom;
        swapped += stepTo;
    }
    return swapped.GetLow64();
}

void CPoolPairView::ForEachPoolPair(std::function<bool(const DCT_ID &, const CPoolPair &)> callback, DCT_ID const & start) {
    DCT_ID poolId = start;
    auto hint = WrapVarInt(poolId.v);

    ForEach<ByID, CVarInt<VarIntMode::DEFAULT, uint32_t>, CPoolPair>([&poolId, &callback] (CVarInt<VarIntMode::DEFAULT, uint32_t> const &, CPoolPair & pool) {
        return callback(poolId, pool);
    }, hint);
}

void CPoolPairView::ForEachPoolShare(std::function<bool (DCT_ID const & id, CScript const & provider)> callback, const PoolShareKey &startKey) const
{
    ForEach<ByShare, PoolShareKey, char>([&callback] (PoolShareKey const & poolShareKey, const char &) {
        return callback(poolShareKey.poolID, poolShareKey.owner);
    }, startKey);
}

//void CPoolPairView::ForEachPoolShare(std::function<bool (const PoolShareKey &poolShareKey, const char &value)> callback, const PoolShareKey &startKey) const
//{
//    ForEach<ByShare, PoolShareKey, char>([&callback] (const PoolShareKey & poolShareKey, const char & value) {
//        return callback(poolShareKey, value);
//    }, startKey);
//}
