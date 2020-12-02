// Copyright (c) 2020 The DeFi Foundation
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

Res CPoolPairView::UpdatePoolPair(DCT_ID const & poolId, bool & status, CAmount const & commission, CScript const & ownerAddress)
{
    auto poolPair = GetPoolPair(poolId);
    if (!poolPair) {
        return Res::Err("Pool with poolId %s does not exist", poolId.ToString());
    }

    CPoolPair & pool = poolPair.get();

    if (pool.status != status) {
        pool.status = status;
    }
    if (commission >= 0) { // default/not set is -1
        if (commission > COIN) {
            return Res::Err("commission > 100%%");
        }
        pool.commission = commission;
    }
    if (!ownerAddress.empty()) {
        pool.ownerAddress = ownerAddress;
    }

    auto res = SetPoolPair(poolId, pool);
    if (!res.ok) {
        return Res::Err("Update poolpair: %s" , res.msg);
    }
    return Res::Ok();
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

Res CPoolPair::Swap(CTokenAmount in, PoolPrice const & maxPrice, std::function<Res (const CTokenAmount &tokenAmount)> onTransfer) {
    if (in.nTokenId != idTokenA && in.nTokenId != idTokenB) {
        throw std::runtime_error("Error, input token ID (" + in.nTokenId.ToString() + ") doesn't match pool tokens (" + idTokenA.ToString() + "," + idTokenB.ToString() + ")");
    }
    if (in.nValue <= 0)
        return Res::Err("Input amount should be positive!");

    if (!status)
        return Res::Err("Pool trading is turned off!");

    bool const forward = in.nTokenId == idTokenA;

    // it is important that reserves are at least SLOPE_SWAP_RATE (1000) to be able to slide, otherwise it can lead to underflow
    if (reserveA < SLOPE_SWAP_RATE || reserveB < SLOPE_SWAP_RATE)
        return Res::Err("Lack of liquidity.");

    auto const aReserveA = arith_uint256(reserveA);
    auto const aReserveB = arith_uint256(reserveB);

    arith_uint256 maxPrice256 = arith_uint256(maxPrice.integer) * PRECISION + maxPrice.fraction;
    arith_uint256 priceAB = (aReserveA * PRECISION / aReserveB);
    arith_uint256 priceBA = (aReserveB * PRECISION / aReserveA);
    arith_uint256 curPrice = forward ? priceBA : priceAB;
    if (curPrice > maxPrice256)
        return Res::Err("Price is higher than indicated.");

    // claim trading fee
    if (commission) {
        CAmount const tradeFee = (arith_uint256(in.nValue) * arith_uint256(commission) / arith_uint256(COIN)).GetLow64(); /// @todo check overflow (COIN vs PRECISION cause commission was normalized to COIN)
        in.nValue -= tradeFee;
        if (forward) {
            blockCommissionA += tradeFee;
        }
        else {
            blockCommissionB += tradeFee;
        }
    }
    auto checkRes = forward ? SafeAdd(reserveA, in.nValue) : SafeAdd(reserveB, in.nValue);
    if (!checkRes.ok) {
        return Res::Err("Swapping will lead to pool's reserve overflow");
    }

    CAmount result = forward ? slopeSwap(in.nValue, reserveA, reserveB) : slopeSwap(in.nValue, reserveB, reserveA);
//    CAmount realPrice = (arith_uint256(result) * aPRECISION / arith_uint256(in.nValue)).GetLow64(); /// @todo check overflow
//    if (realPrice > maxPrice)
//        return Res::Err("Price higher than indicated.");

    swapEvent = true; // (!!!)

    return onTransfer({ forward ? idTokenB : idTokenA, result });
}

CAmount CPoolPair::slopeSwap(CAmount unswapped, CAmount &poolFrom, CAmount &poolTo) {
    assert (unswapped >= 0);
    assert (SafeAdd(unswapped, poolFrom).ok);

    arith_uint256 poolF = arith_uint256(poolFrom);
    arith_uint256 poolT = arith_uint256(poolTo);
    arith_uint256 unswappedA = arith_uint256(unswapped);

    arith_uint256 swapped = poolT - (poolT * poolF / (poolF + unswappedA));
    poolF += unswappedA;
    poolT -= swapped;

    poolFrom = poolF.GetLow64();
    poolTo = poolT.GetLow64();
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
