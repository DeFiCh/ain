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
const unsigned char CPoolPairView::ByDailyReward    ::prefix = 'I';
const unsigned char CPoolPairView::ByPoolHeight     ::prefix = 'P';

struct PoolHeightValue {
    DCT_ID idTokenA{};
    DCT_ID idTokenB{};
    CBalances rewards;
    bool swapEvent = false;
    CAmount blockReward = 0;
    CAmount totalLiquidity = 0;
    CAmount blockCommissionA = 0;
    CAmount blockCommissionB = 0;

    PoolHeightValue() = default;

    CAmount poolRewardPerBlock(CAmount dailyReward, CAmount rewardPct) {
        return dailyReward / Params().GetConsensus().blocksPerDay() * rewardPct / COIN;
    }

    PoolHeightValue(CPoolPair const & pool, CAmount dailyReward) {
        rewards = pool.rewards;
        idTokenA = pool.idTokenA;
        idTokenB = pool.idTokenB;
        swapEvent = pool.swapEvent;
        blockReward = poolRewardPerBlock(dailyReward, pool.rewardPct);
        totalLiquidity = pool.totalLiquidity;
        blockCommissionA = pool.blockCommissionA;
        blockCommissionB = pool.blockCommissionB;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(rewards);
        READWRITE(idTokenA);
        READWRITE(idTokenB);
        READWRITE(swapEvent);
        READWRITE(blockReward);
        READWRITE(totalLiquidity);
        READWRITE(blockCommissionA);
        READWRITE(blockCommissionB);
    }
};

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

    auto dailyReward = [&]() -> CAmount {
        auto itPct = LowerBound<ByDailyReward>(PoolHeightKey{{}, height});
        if (!itPct.Valid() || itPct.Key().poolID != DCT_ID{}) {
            return 0;
        }
        return itPct.Value().as<CAmount>();
    };

    // create new
    if (!poolPairByID && !poolPairByTokens) {
        WriteBy<ByID>(poolId, pool);
        WriteBy<ByPair>(ByPairKey{pool.idTokenA, pool.idTokenB}, poolId);
        WriteBy<ByPair>(ByPairKey{pool.idTokenB, pool.idTokenA}, poolId);
        if (height < UINT_MAX) {
            WriteBy<ByPoolHeight>(PoolHeightKey{poolId, height}, PoolHeightValue(pool, dailyReward()));
        }
        return Res::Ok();
    }

    // update
    if(poolPairByTokens && poolId == poolPairByTokens->first
    && poolPairByTokens->second.idTokenA == pool.idTokenA
    && poolPairByTokens->second.idTokenB == pool.idTokenB) {
        WriteBy<ByID>(poolId, pool);
        if (height < UINT_MAX) {
            WriteBy<ByPoolHeight>(PoolHeightKey{poolId, height}, PoolHeightValue(pool, dailyReward()));
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

Res CPoolPairView::UpdatePoolPair(DCT_ID const & poolId, uint32_t height, bool status, CAmount const & commission, CScript const & ownerAddress, CBalances const & rewards)
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

    if (!rewards.balances.empty()) {
        auto customRewards = rewards;
        // Check for special case to wipe rewards
        if (rewards.balances.size() == 1 && rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()}
        && rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max()) {
            customRewards.balances.clear();
        }
        if (pool.rewards != customRewards) {
            usedHeight = height;
            pool.rewards = customRewards;
        }
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

inline CAmount liquidityReward(CAmount reward, CAmount liquidity, CAmount totalLiquidity) {
    return static_cast<CAmount>((arith_uint256(reward) * arith_uint256(liquidity) / arith_uint256(totalLiquidity)).GetLow64());
}

void CPoolPairView::CalculatePoolRewards(DCT_ID const & poolId, std::function<CAmount()> onLiquidity, uint32_t begin, uint32_t end, std::function<void(uint8_t, CTokenAmount, uint32_t)> onReward) {
    if (begin >= end) {
        return;
    }
    constexpr const uint32_t PRECISION = 10000;
    const auto newCalcHeight = uint32_t(Params().GetConsensus().BayfrontGardensHeight);

    auto itPool = LowerBound<ByPoolHeight>(PoolHeightKey{poolId, begin});
    while (itPool.Valid() && itPool.Key().poolID == poolId) {
        // rewards starting in same block as pool
        const auto poolHeight = itPool.Key().height;
        const auto pool = itPool.Value().as<PoolHeightValue>();
        const auto poolReward = pool.blockReward;
        /// @Note we store keys in desc oreder so Prev is actually go in forward
        itPool.Prev();
        auto endHeight = itPool.Valid() && itPool.Key().poolID == poolId ? itPool.Key().height : UINT_MAX;
        endHeight = std::min(end, endHeight);
        for (auto height = begin; height < endHeight && pool.totalLiquidity != 0; height++) {
            const auto liquidity = onLiquidity();
            // daily rewards
            if (poolReward != 0) {
                CAmount providerReward = 0;
                if (height < newCalcHeight) { // old calculation
                    uint32_t liqWeight = liquidity * PRECISION / pool.totalLiquidity;
                    providerReward = poolReward * liqWeight / PRECISION;
                } else { // new calculation
                    providerReward = liquidityReward(poolReward, liquidity, pool.totalLiquidity);
                }
                onReward(uint8_t(RewardType::Rewards), {DCT_ID{0}, providerReward}, height);
            }
            // commissions
            if (height == poolHeight && pool.swapEvent) {
                CAmount feeA, feeB;
                if (height < newCalcHeight) {
                    uint32_t liqWeight = liquidity * PRECISION / pool.totalLiquidity;
                    feeA = pool.blockCommissionA * liqWeight / PRECISION;
                    feeB = pool.blockCommissionB * liqWeight / PRECISION;
                } else {
                    feeA = liquidityReward(pool.blockCommissionA, liquidity, pool.totalLiquidity);
                    feeB = liquidityReward(pool.blockCommissionB, liquidity, pool.totalLiquidity);
                }
                onReward(uint8_t(RewardType::Commission), {pool.idTokenA, feeA}, height);
                onReward(uint8_t(RewardType::Commission), {pool.idTokenB, feeB}, height);
            }
            // custom rewards
            for (const auto& reward : pool.rewards.balances) {
                if (auto providerReward = liquidityReward(reward.second, liquidity, pool.totalLiquidity)) {
                    onReward(uint8_t(RewardType::Rewards), {reward.first, providerReward}, height);
                }
            }
        }
        if (endHeight == end) {
            break;
        }
        begin = endHeight;
    }
}

Res CPoolPair::AddLiquidity(CAmount amountA, CAmount amountB, std::function<Res(CAmount)> onMint, bool slippageProtection) {
    // instead of assertion due to tests
    if (amountA <= 0 || amountB <= 0) {
        return Res::Err("amounts should be positive");
    }

    CAmount liquidity{0};
    if (totalLiquidity == 0) {
        liquidity = (CAmount) (arith_uint256(amountA) * arith_uint256(amountB)).sqrt().GetLow64(); // sure this is below std::numeric_limits<CAmount>::max() due to sqrt natue
        if (liquidity <= MINIMUM_LIQUIDITY) { // ensure that it'll be non-zero
            return Res::Err("liquidity too low");
        }
        liquidity -= MINIMUM_LIQUIDITY;
        // MINIMUM_LIQUIDITY is a hack for non-zero division
        totalLiquidity = MINIMUM_LIQUIDITY;
    } else {
        CAmount liqA = (arith_uint256(amountA) * arith_uint256(totalLiquidity) / reserveA).GetLow64();
        CAmount liqB = (arith_uint256(amountB) * arith_uint256(totalLiquidity) / reserveB).GetLow64();
        liquidity = std::min(liqA, liqB);

        if (liquidity == 0) {
            return Res::Err("amounts too low, zero liquidity");
        }

        if(slippageProtection) {
            if ((std::max(liqA, liqB) - liquidity) * 100 / liquidity >= 3) {
                return Res::Err("Exceeds max ratio slippage protection of 3%%");
            }
        }
    }

    // increasing totalLiquidity
    auto resTotal = SafeAdd(totalLiquidity, liquidity);
    if (!resTotal.ok) {
        return Res::Err("can't add %d to totalLiquidity: %s", liquidity, resTotal.msg);
    }
    totalLiquidity = *resTotal.val;

    // increasing reserves
    auto resA = SafeAdd(reserveA, amountA);
    auto resB = SafeAdd(reserveB, amountB);
    if (resA.ok && resB.ok) {
        reserveA = *resA.val;
        reserveB = *resB.val;
    } else {
        return Res::Err("overflow when adding to reserves");
    }

    return onMint(liquidity);
}

Res CPoolPair::RemoveLiquidity(CAmount liqAmount, std::function<Res(CAmount, CAmount)> onReclaim) {
    // instead of assertion due to tests
    // IRL it can't be more than "total-1000", and was checked indirectly by balances before. but for tests and incapsulation:
    if (liqAmount <= 0 || liqAmount >= totalLiquidity) {
        return Res::Err("incorrect liquidity");
    }

    CAmount resAmountA, resAmountB;
    resAmountA = (arith_uint256(liqAmount) * arith_uint256(reserveA) / totalLiquidity).GetLow64();
    resAmountB = (arith_uint256(liqAmount) * arith_uint256(reserveB) / totalLiquidity).GetLow64();

    reserveA -= resAmountA; // safe due to previous math
    reserveB -= resAmountB;
    totalLiquidity -= liqAmount;

    return onReclaim(resAmountA, resAmountB);
}

Res CPoolPair::Swap(CTokenAmount in, PoolPrice const & maxPrice, std::function<Res (const CTokenAmount &)> onTransfer, int height) {
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

CAmount CPoolPairView::UpdatePoolRewards(std::function<CTokenAmount(CScript const &, DCT_ID)> onGetBalance, std::function<Res(CScript const &, CTokenAmount)> onTransfer, int nHeight) {

    bool newRewardCalc = nHeight >= Params().GetConsensus().BayfrontGardensHeight;

    constexpr uint32_t const PRECISION = 10000; // (== 100%) just searching the way to avoid arith256 inflating
    CAmount totalDistributed = 0;

    ForEachPoolPair([&] (DCT_ID const & poolId, CPoolPair pool) {

        CAmount distributedFeeA = 0;
        CAmount distributedFeeB = 0;

        uint32_t height = nHeight;
        PoolHeightValue poolHeight;
        auto itPool = LowerBound<ByPoolHeight>(PoolHeightKey{poolId, height});
        if (itPool.Valid() && itPool.Key().poolID == poolId) {
            poolHeight = itPool.Value();
        }

        const auto poolReward = poolHeight.blockReward;

        auto rewards = pool.rewards;
        for (auto it = rewards.balances.begin(), next_it = it; it != rewards.balances.end(); it = next_it) {
            ++next_it;

            // Get token balance
            const auto balance = onGetBalance(pool.ownerAddress, it->first).nValue;

            // Make there's enough to pay reward otherwise remove it
            if (balance < it->second) {
                rewards.balances.erase(it);
            }
        }

        if (rewards != poolHeight.rewards) {
            poolHeight.rewards = rewards;
            // contract to ensure data validity, commissions should not be affected
            assert(poolHeight.swapEvent == pool.swapEvent || !pool.swapEvent);
            poolHeight.swapEvent = pool.swapEvent;
            WriteBy<ByPoolHeight>(PoolHeightKey{poolId, height}, poolHeight);
        }

        if (pool.totalLiquidity == 0 || (!pool.swapEvent && poolReward == 0 && rewards.balances.empty())) {
            return true; // no events, skip to the next pool
        }

        ForEachPoolShare([&] (DCT_ID const & currentId, CScript const & provider, uint32_t) {
            if (currentId != poolId) {
                return false; // stop
            }
            CAmount const liquidity = onGetBalance(provider, poolId).nValue;

            uint32_t const liqWeight = liquidity * PRECISION / pool.totalLiquidity;
            assert (liqWeight < PRECISION);

            // distribute trading fees
            if (pool.swapEvent) {
                if (newRewardCalc) {
                    distributedFeeA += liquidityReward(pool.blockCommissionA, liquidity, pool.totalLiquidity);
                    distributedFeeB += liquidityReward(pool.blockCommissionB, liquidity, pool.totalLiquidity);
                } else {
                    distributedFeeA += pool.blockCommissionA * liqWeight / PRECISION;
                    distributedFeeB += pool.blockCommissionB * liqWeight / PRECISION;
                }
            }

            // distribute yield farming
            if (poolReward) {
                if (newRewardCalc) {
                    totalDistributed += liquidityReward(poolReward, liquidity, pool.totalLiquidity);
                } else {
                    totalDistributed += poolReward * liqWeight / PRECISION;
                }
            }

            for (const auto& reward : rewards.balances) {
                if (auto providerReward = liquidityReward(reward.second, liquidity, pool.totalLiquidity)) {
                    onTransfer(pool.ownerAddress, {reward.first, providerReward});
                }
            }

            return true;
        }, PoolShareKey{poolId, CScript{}});

        if (pool.swapEvent) {
            pool.blockCommissionA -= distributedFeeA;
            pool.blockCommissionB -= distributedFeeB;
            pool.swapEvent = false;

            auto res = SetPoolPair(poolId, UINT_MAX, pool);
            if (!res.ok) {
                LogPrintf("Pool rewards: can't update pool (id=%s) state: %s\n", poolId.ToString(), res.msg);
            }
        }
        return true;
    });
    return totalDistributed;
}

Res CPoolPairView::SetShare(DCT_ID const & poolId, CScript const & provider, uint32_t height) {
    WriteBy<ByShare>(PoolShareKey{poolId, provider}, height);
    return Res::Ok();
}

Res CPoolPairView::DelShare(DCT_ID const & poolId, CScript const & provider) {
    EraseBy<ByShare>(PoolShareKey{poolId, provider});
    return Res::Ok();
}

boost::optional<uint32_t> CPoolPairView::GetShare(DCT_ID const & poolId, CScript const & provider) {
    return ReadBy<ByShare, uint32_t>(PoolShareKey{poolId, provider});
}

Res CPoolPairView::SetDailyReward(uint32_t height, CAmount reward) {
    ForEachPoolPair([&](DCT_ID const & id, CPoolPair pool) {
        if (pool.totalLiquidity != 0) {
            WriteBy<ByPoolHeight>(PoolHeightKey{id, height}, PoolHeightValue(pool, reward));
        }
        return true;
    });
    WriteBy<ByDailyReward>(PoolHeightKey{{}, height}, reward);
    return Res::Ok();
}

void CPoolPairView::ForEachPoolPair(std::function<bool(const DCT_ID &, CLazySerialize<CPoolPair>)> callback, DCT_ID const & start) {
    ForEach<ByID, DCT_ID, CPoolPair>(callback, start);
}

void CPoolPairView::ForEachPoolShare(std::function<bool (DCT_ID const &, CScript const &, uint32_t)> callback, const PoolShareKey &startKey) {
    ForEach<ByShare, PoolShareKey, uint32_t>([&callback] (PoolShareKey const & poolShareKey, uint32_t height) {
        return callback(poolShareKey.poolID, poolShareKey.owner, height);
    }, startKey);
}
