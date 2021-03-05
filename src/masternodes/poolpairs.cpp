// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <masternodes/poolpairs.h>
#include <core_io.h>
#include <primitives/transaction.h>

const unsigned char CPoolPairView::ByID             ::prefix = 'i';
const unsigned char CPoolPairView::ByPair           ::prefix = 'j';
const unsigned char CPoolPairView::ByShare          ::prefix = 'k';
const unsigned char CPoolPairView::Reward           ::prefix = 'I';
const unsigned char CPoolPairView::ByDailyReward    ::prefix = 'C';
const unsigned char CPoolPairView::ByPoolHeight     ::prefix = 'P';

Res CPoolPairView::SetPoolPair(DCT_ID const & poolId, uint32_t height, CPoolPair const & pool)
{
    if (pool.idTokenA == pool.idTokenB) {
        return Res::Err("Error: tokens IDs are the same.");
    }

    auto poolPairByID = GetPoolPair(poolId);
    auto poolPairByTokens = GetPoolPair(pool.idTokenA, pool.idTokenB);

    if (!poolPairByID && poolPairByTokens) {
        return Res::Err("Error, there is already a poolpairwith same tokens, but different poolId");
    }

    // create new
    if (!poolPairByID && !poolPairByTokens) {
        WriteBy<ByID>(poolId, pool);
        WriteBy<ByPair>(ByPairKey{pool.idTokenA, pool.idTokenB}, poolId);
        WriteBy<ByPair>(ByPairKey{pool.idTokenB, pool.idTokenA}, poolId);
        if (height < UINT_MAX) {
            WriteBy<ByPoolHeight>(PoolRewardKey{poolId, height}, pool);
        }
        return Res::Ok();
    }

    // update
    if(poolPairByTokens && poolId == poolPairByTokens->first
    && poolPairByTokens->second.idTokenA == pool.idTokenA
    && poolPairByTokens->second.idTokenB == pool.idTokenB) {
        WriteBy<ByID>(poolId, pool);
        if (height < UINT_MAX) {
            WriteBy<ByPoolHeight>(PoolRewardKey{poolId, height}, pool);
        }
        return Res::Ok();
    }

    // errors
    if (poolPairByTokens && poolId != poolPairByTokens->first) {
        return Res::Err("Error, PoolID is incorrect");
    }
    if (poolPairByTokens && (poolPairByTokens->second.idTokenA != pool.idTokenA
    || poolPairByTokens->second.idTokenB == pool.idTokenB)) {
        return Res::Err("Error, idTokenA or idTokenB is incorrect.");
    }
    return Res::Err("Error: Couldn't create/update pool pair.");
}

Res CPoolPairView::UpdatePoolPair(DCT_ID const & poolId, uint32_t height, bool status, CAmount const & commission, CScript const & ownerAddress)
{
    auto poolPair = GetPoolPair(poolId);
    if (!poolPair) {
        return Res::Err("Pool with poolId %s does not exist", poolId.ToString());
    }

    uint32_t usedHeight = UINT_MAX;
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
        if (pool.ownerAddress != ownerAddress) {
            usedHeight = height;
        }
        pool.ownerAddress = ownerAddress;
    }

    auto res = SetPoolPair(poolId, usedHeight, pool);
    if (!res.ok) {
        return Res::Err("Update poolpair: %s" , res.msg);
    }
    return Res::Ok();
}

boost::optional<CPoolPair> CPoolPairView::GetPoolPair(const DCT_ID &poolId) const
{
    return ReadBy<ByID, CPoolPair>(poolId);
}

boost::optional<std::pair<DCT_ID, CPoolPair> > CPoolPairView::GetPoolPair(const DCT_ID &tokenA, const DCT_ID &tokenB) const
{
    DCT_ID poolId;
    ByPairKey key {tokenA, tokenB};
    if(ReadBy<ByPair, ByPairKey>(key, poolId)) {
        if (auto poolPair = GetPoolPair(poolId)) {
            return std::make_pair(poolId, std::move(*poolPair));
        }
    }
    return {};
}

Res CPoolPairView::SetPoolCustomReward(const DCT_ID &poolId, uint32_t height, const CBalances& rewards)
{
    if (!GetPoolPair(poolId)) {
        return Res::Err("Error %s: poolID %s does not exist", __func__, poolId.ToString());
    }

    WriteBy<Reward>(PoolRewardKey{poolId, height}, rewards);
    return Res::Ok();
}

boost::optional<CBalances> CPoolPairView::GetPoolCustomReward(const DCT_ID &poolId)
{
    auto it = LowerBound<Reward>(PoolRewardKey{poolId, UINT_MAX});
    if (!it.Valid() || it.Key().poolID != poolId) {
        return {};
    }
    return it.Value().as<CBalances>();
}

void CPoolPairView::CalculatePoolRewards(DCT_ID const & poolId, CAmount liquidity, uint32_t begin, uint32_t end, std::function<void(CScript const &, uint8_t, CTokenAmount, uint32_t, uint32_t)> onReward) {
    if (begin >= end || liquidity == 0) {
        return;
    }
    constexpr const uint32_t PRECISION = 10000;
    const auto newCalcHeight = uint32_t(Params().GetConsensus().BayfrontGardensHeight);
    const auto blocksPerDay = (60 * 60 * 24 / Params().GetConsensus().pos.nTargetSpacing);
    const auto beginCustomRewards = std::max(begin, uint32_t(Params().GetConsensus().ClarkeQuayHeight));

    auto itPct = LowerBound<ByDailyReward>(PoolRewardKey{{}, end - 1});
    auto itPool = LowerBound<ByPoolHeight>(PoolRewardKey{poolId, end - 1});
    auto itReward = LowerBound<Reward>(PoolRewardKey{poolId, end - 1});

    while (itPool.Valid() && itPool.Key().poolID == poolId) {
        // rewards starting in same block as pool
        const auto poolHeight = itPool.Key().height;
        const auto pool = itPool.Value().as<CPoolPair>();
        // daily rewards
        for (auto endHeight = end; itPct.Valid(); itPct.Next()) {
            // we have desc order so we select higher height
            auto beginHeight = std::max(begin, std::max(poolHeight, itPct.Key().height));
            auto poolReward = itPct.Value().as<CAmount>() / blocksPerDay * pool.rewardPct / COIN;
            if (poolReward != 0 && pool.totalLiquidity != 0) {
                auto beginCalcHeight = beginHeight;
                auto endCalcHeight = std::min(endHeight, newCalcHeight);
                if (endCalcHeight > beginHeight) { // old calculation
                    uint32_t liqWeight = liquidity * PRECISION / pool.totalLiquidity;
                    auto providerReward = poolReward * liqWeight / PRECISION;
                    onReward({}, uint8_t(RewardType::Rewards), {DCT_ID{0}, providerReward}, beginHeight, endCalcHeight);
                    beginCalcHeight = endCalcHeight;
                }
                if (endHeight > beginCalcHeight) { // new calculation
                    auto providerReward = static_cast<CAmount>((arith_uint256(poolReward) * arith_uint256(liquidity) / arith_uint256(pool.totalLiquidity)).GetLow64());
                    onReward({}, uint8_t(RewardType::Rewards), {DCT_ID{0}, providerReward}, beginCalcHeight, endHeight);
                }
            }
            if (beginHeight == begin || beginHeight == poolHeight) {
                break;
            }
            endHeight = beginHeight;
        }
        // commissions
        if (pool.swapEvent && pool.totalLiquidity != 0) {
            CAmount feeA, feeB;
            if (poolHeight < newCalcHeight) {
                uint32_t liqWeight = liquidity * PRECISION / pool.totalLiquidity;
                feeA = pool.blockCommissionA * liqWeight / PRECISION;
                feeB = pool.blockCommissionB * liqWeight / PRECISION;
            } else {
                feeA = static_cast<CAmount>((arith_uint256(pool.blockCommissionA) * arith_uint256(liquidity) / arith_uint256(pool.totalLiquidity)).GetLow64());
                feeB = static_cast<CAmount>((arith_uint256(pool.blockCommissionB) * arith_uint256(liquidity) / arith_uint256(pool.totalLiquidity)).GetLow64());
            }
            onReward({}, uint8_t(RewardType::Commission), {pool.idTokenA, feeA}, poolHeight, poolHeight + 1);
            onReward({}, uint8_t(RewardType::Commission), {pool.idTokenB, feeB}, poolHeight, poolHeight + 1);
        }
        // custom rewards
        if (end > beginCustomRewards) {
            for (auto endHeight = end; itReward.Valid() && itReward.Key().poolID == poolId; itReward.Next()) {
                auto beginHeight = std::max(beginCustomRewards, std::max(poolHeight, itReward.Key().height));
                if (endHeight > beginHeight && pool.totalLiquidity != 0) {
                    for (const auto& reward : itReward.Value().as<CBalances>().balances) {
                        if (auto providerReward = static_cast<CAmount>((arith_uint256(reward.second) * arith_uint256(liquidity) / arith_uint256(pool.totalLiquidity)).GetLow64())) {
                            onReward(pool.ownerAddress, uint8_t(RewardType::Rewards), {reward.first, providerReward}, beginHeight, endHeight);
                        }
                    }
                }
                if (beginHeight == beginCustomRewards || beginHeight == poolHeight) {
                    break;
                }
                endHeight = beginHeight;
            }
        }
        if (begin >= poolHeight) {
            break;
        }
        itPool.Next();
        end = poolHeight;
    }
}

Res CPoolPair::Swap(CTokenAmount in, PoolPrice const & maxPrice, std::function<Res (const CTokenAmount &tokenAmount)> onTransfer, int height) {
    if (in.nTokenId != idTokenA && in.nTokenId != idTokenB)
        return Res::Err("Error, input token ID (" + in.nTokenId.ToString() + ") doesn't match pool tokens (" + idTokenA.ToString() + "," + idTokenB.ToString() + ")");

    if (in.nValue <= 0)
        return Res::Err("Input amount should be positive!");

    if (!status)
        return Res::Err("Pool trading is turned off!");

    bool const forward = in.nTokenId == idTokenA;
    auto& reserveF = forward ? reserveA : reserveB;
    auto& reserveT = forward ? reserveB : reserveA;

    // it is important that reserves are at least SLOPE_SWAP_RATE (1000) to be able to slide, otherwise it can lead to underflow
    if (reserveA < SLOPE_SWAP_RATE || reserveB < SLOPE_SWAP_RATE)
        return Res::Err("Lack of liquidity.");

    auto const maxPrice256 = arith_uint256(maxPrice.integer) * PRECISION + maxPrice.fraction;
    // NOTE it has a bug prior Dakota hardfork
    auto const price = height < Params().GetConsensus().DakotaHeight
                              ? arith_uint256(reserveT) * PRECISION / reserveF
                              : arith_uint256(reserveF) * PRECISION / reserveT;
    if (price > maxPrice256)
        return Res::Err("Price is higher than indicated.");

    // claim trading fee
    if (commission) {
        CAmount const tradeFee = (arith_uint256(in.nValue) * arith_uint256(commission) / arith_uint256(COIN)).GetLow64(); /// @todo check overflow (COIN vs PRECISION cause commission was normalized to COIN)
        in.nValue -= tradeFee;
        if (forward) {
            blockCommissionA += tradeFee;
        } else {
            blockCommissionB += tradeFee;
        }
    }
    auto checkRes = SafeAdd(reserveF, in.nValue);
    if (!checkRes.ok) {
        return Res::Err("Swapping will lead to pool's reserve overflow");
    }

    CAmount result = slopeSwap(in.nValue, reserveF, reserveT, height >= Params().GetConsensus().BayfrontGardensHeight);

    swapEvent = true; // (!!!)

    return onTransfer({ forward ? idTokenB : idTokenA, result });
}

CAmount CPoolPair::slopeSwap(CAmount unswapped, CAmount &poolFrom, CAmount &poolTo, bool postBayfrontGardens) {
    assert (unswapped >= 0);
    assert (SafeAdd(unswapped, poolFrom).ok);

    arith_uint256 poolF = arith_uint256(poolFrom);
    arith_uint256 poolT = arith_uint256(poolTo);

    arith_uint256 swapped = 0;
    if (!postBayfrontGardens) {
        CAmount chunk = poolFrom/SLOPE_SWAP_RATE < unswapped ? poolFrom/SLOPE_SWAP_RATE : unswapped;
        while (unswapped > 0) {
            //arith_uint256 stepFrom = std::min(poolFrom/1000, unswapped); // 0.1%
            CAmount stepFrom = std::min(chunk, unswapped);
            arith_uint256 stepFrom256(stepFrom);
            arith_uint256 stepTo = poolT * stepFrom256 / poolF;
            poolF += stepFrom256;
            poolT -= stepTo;
            unswapped -= stepFrom;
            swapped += stepTo;
        }
    } else {
        arith_uint256 unswappedA = arith_uint256(unswapped);

        swapped = poolT - (poolT * poolF / (poolF + unswappedA));
        poolF += unswappedA;
        poolT -= swapped;
    }

    poolFrom = poolF.GetLow64();
    poolTo = poolT.GetLow64();
    return swapped.GetLow64();
}

void CPoolPairView::ForEachPoolPair(std::function<bool(const DCT_ID &, CLazySerialize<CPoolPair>)> callback, DCT_ID const & start)
{
    ForEach<ByID, DCT_ID, CPoolPair>(callback, start);
}

void CPoolPairView::ForEachPoolShare(std::function<bool (DCT_ID const &, CScript const &, uint32_t)> callback, const PoolShareKey &startKey)
{
    ForEach<ByShare, PoolShareKey, uint32_t>([&callback] (PoolShareKey const & poolShareKey, uint32_t height) {
        return callback(poolShareKey.poolID, poolShareKey.owner, height);
    }, startKey);
}
