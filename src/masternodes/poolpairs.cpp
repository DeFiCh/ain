// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <masternodes/poolpairs.h>
#include <core_io.h>
#include <primitives/transaction.h>

struct PoolSwapValue {
    bool swapEvent;
    CAmount blockCommissionA;
    CAmount blockCommissionB;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(swapEvent);
        READWRITE(blockCommissionA);
        READWRITE(blockCommissionB);
    }
};

struct PoolReservesValue {
    CAmount reserveA;
    CAmount reserveB;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(reserveA);
        READWRITE(reserveB);
    }
};

std::string RewardToString(RewardType type)
{
    if (type & RewardType::Rewards) {
        return "Rewards";
    } else if (type == RewardType::Commission) {
        return "Commission";
    }
    return "Unknown";
}

std::string RewardTypeToString(RewardType type)
{
    switch(type) {
        case RewardType::Coinbase: return "Coinbase";
        case RewardType::Pool: return "Pool";
        case RewardType::LoanTokenDEXReward: return "LoanTokenDEXReward";
        default: return "Unknown";
    }
}

template <typename By, typename ReturnType>
ReturnType ReadValueAt(CPoolPairView * poolView, PoolHeightKey const & poolKey) {
    auto it = poolView->LowerBound<By>(poolKey);
    if (it.Valid() && it.Key().poolID == poolKey.poolID) {
        return it.Value();
    }
    return {};
}

Res CPoolPairView::SetPoolPair(DCT_ID const & poolId, uint32_t height, CPoolPair const & pool)
{
    if (pool.idTokenA == pool.idTokenB) {
        return Res::Err("Error: tokens IDs are the same.");
    }

    auto poolPairByID = GetPoolPair(poolId);
    auto poolIdByTokens = ReadBy<ByPair, DCT_ID>(ByPairKey{pool.idTokenA, pool.idTokenB});

    if ((!poolPairByID && poolIdByTokens)
    || (poolPairByID && !poolIdByTokens)) {
        return Res::Err("Error, there is already a poolpair with same tokens, but different poolId");
    }

    // create new
    if (!poolPairByID && !poolIdByTokens) {
        WriteBy<ByID>(poolId, pool);
        WriteBy<ByPair>(ByPairKey{pool.idTokenA, pool.idTokenB}, poolId);
        WriteBy<ByPair>(ByPairKey{pool.idTokenB, pool.idTokenA}, poolId);
        WriteBy<ByIDPair>(poolId, ByPairKey{pool.idTokenA, pool.idTokenB});
        return Res::Ok();
    }

    if (poolId != *poolIdByTokens) {
        return Res::Err("Error, PoolID is incorrect");
    }

    auto poolPairByTokens = ReadBy<ByIDPair, ByPairKey>(poolId);
    assert(poolPairByTokens);

    // update
    if(poolPairByID->idTokenA == pool.idTokenA
    && poolPairByID->idTokenB == pool.idTokenB
    && poolPairByTokens->idTokenA == pool.idTokenA
    && poolPairByTokens->idTokenB == pool.idTokenB) {
        if (poolPairByID->reserveA != pool.reserveA
        || poolPairByID->reserveB != pool.reserveB) {
            WriteBy<ByReserves>(poolId, PoolReservesValue{pool.reserveA, pool.reserveB});
        }
        PoolHeightKey poolKey = {poolId, height};
        if (pool.swapEvent) {
            WriteBy<ByPoolSwap>(poolKey, PoolSwapValue{true, pool.blockCommissionA, pool.blockCommissionB});
        }
        if (poolPairByID->totalLiquidity != pool.totalLiquidity) {
            WriteBy<ByTotalLiquidity>(poolKey, pool.totalLiquidity);
        }
        return Res::Ok();
    }

    return Res::Err("Error, idTokenA or idTokenB is incorrect.");
}

Res CPoolPairView::UpdatePoolPair(DCT_ID const & poolId, uint32_t height, bool status, CAmount const & commission, CScript const & ownerAddress, CBalances const & rewards)
{
    auto poolPair = GetPoolPair(poolId);
    if (!poolPair) {
        return Res::Err("Pool with poolId %s does not exist", poolId.ToString());
    }

    CPoolPair & pool = poolPair.value();

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

    if (!rewards.balances.empty()) {
        auto customRewards = rewards;
        // Check for special case to wipe rewards
        if (rewards.balances.size() == 1 && rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()}
        && rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max()) {
            customRewards.balances.clear();
        }
        if (pool.rewards != customRewards) {
            pool.rewards = customRewards;
            WriteBy<ByCustomReward>(PoolHeightKey{poolId, height}, customRewards);
        }
    }

    WriteBy<ByID>(poolId, pool);
    return Res::Ok();
}

std::optional<CPoolPair> CPoolPairView::GetPoolPair(const DCT_ID &poolId) const
{
    auto pool = ReadBy<ByID, CPoolPair>(poolId);
    if (!pool) {
        return {};
    }
    if (auto reserves = ReadBy<ByReserves, PoolReservesValue>(poolId)) {
        pool->reserveA = reserves->reserveA;
        pool->reserveB = reserves->reserveB;
    }
    if (auto rewardPct = ReadBy<ByRewardPct, CAmount>(poolId)) {
        pool->rewardPct = *rewardPct;
    }
    if (auto rewardLoanPct = ReadBy<ByRewardLoanPct, CAmount>(poolId)) {
        pool->rewardLoanPct = *rewardLoanPct;
    }
    PoolHeightKey poolKey = {poolId, UINT_MAX};
    // it's safe needed by iterator creation
    auto view = const_cast<CPoolPairView *>(this);
    auto swapValue = ReadValueAt<ByPoolSwap, PoolSwapValue>(view, poolKey);
    /// @Note swapEvent isn't restored
    pool->blockCommissionA = swapValue.blockCommissionA;
    pool->blockCommissionB = swapValue.blockCommissionB;
    pool->totalLiquidity = ReadValueAt<ByTotalLiquidity, CAmount>(view, poolKey);
    return pool;
}

std::optional<std::pair<DCT_ID, CPoolPair> > CPoolPairView::GetPoolPair(const DCT_ID &tokenA, const DCT_ID &tokenB) const
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

template<typename TIterator, typename ValueType>
void ReadValueMoveToNext(TIterator & it, DCT_ID poolId, ValueType & value, uint32_t & height) {

    if (it.Valid() && it.Key().poolID == poolId) {
        value = it.Value();
        /// @Note we store keys in desc order so Prev is actually go in forward
        it.Prev();
        if (it.Valid() && it.Key().poolID == poolId) {
            height = it.Key().height;
        } else {
            height = UINT_MAX;
        }
    } else {
        value = {};
        height = UINT_MAX;
    }
}

void CPoolPairView::CalculatePoolRewards(DCT_ID const & poolId, std::function<CAmount()> onLiquidity, uint32_t begin, uint32_t end, std::function<void(RewardType, CTokenAmount, uint32_t)> onReward) {
    if (begin >= end) {
        return;
    }
    constexpr const uint32_t PRECISION = 10000;
    const auto newCalcHeight = uint32_t(Params().GetConsensus().BayfrontGardensHeight);

    auto tokenIds = ReadBy<ByIDPair, ByPairKey>(poolId);
    assert(tokenIds); // contract to verify pool data

    PoolHeightKey poolKey = {poolId, begin};

    CAmount poolReward = 0;
    CAmount poolLoanReward = 0;
    auto nextPoolReward = begin;
    auto nextPoolLoanReward = begin;
    auto itPoolReward = LowerBound<ByPoolReward>(poolKey);
    auto itPoolLoanReward = LowerBound<ByPoolLoanReward>(poolKey);

    CAmount totalLiquidity = 0;
    auto nextTotalLiquidity = begin;
    auto itTotalLiquidity = LowerBound<ByTotalLiquidity>(poolKey);

    CBalances customRewards;
    auto nextCustomRewards = begin;
    auto itCustomRewards = LowerBound<ByCustomReward>(poolKey);

    PoolSwapValue poolSwap;
    auto nextPoolSwap = UINT_MAX;
    auto poolSwapHeight = UINT_MAX;
    auto itPoolSwap = LowerBound<ByPoolSwap>(poolKey);
    if (itPoolSwap.Valid() && itPoolSwap.Key().poolID == poolId) {
        nextPoolSwap = itPoolSwap.Key().height;
    }

    for (auto height = begin; height < end;) {
        // find suitable pool liquidity
        if (height == nextTotalLiquidity || totalLiquidity == 0) {
            height = nextTotalLiquidity;
            ReadValueMoveToNext(itTotalLiquidity, poolId, totalLiquidity, nextTotalLiquidity);
            continue;
        }
        // adjust iterators to working height
        while (height >= nextPoolReward) {
            ReadValueMoveToNext(itPoolReward, poolId, poolReward, nextPoolReward);
        }
        while (height >= nextPoolLoanReward) {
            ReadValueMoveToNext(itPoolLoanReward, poolId, poolLoanReward, nextPoolLoanReward);
        }
        while (height >= nextPoolSwap) {
            poolSwapHeight = nextPoolSwap;
            ReadValueMoveToNext(itPoolSwap, poolId, poolSwap, nextPoolSwap);
        }
        while (height >= nextCustomRewards) {
            ReadValueMoveToNext(itCustomRewards, poolId, customRewards, nextCustomRewards);
        }
        const auto liquidity = onLiquidity();
        // daily rewards
        if (poolReward != 0) {
            CAmount providerReward = 0;
            if (height < newCalcHeight) { // old calculation
                uint32_t liqWeight = liquidity * PRECISION / totalLiquidity;
                providerReward = poolReward * liqWeight / PRECISION;
            } else { // new calculation
                providerReward = liquidityReward(poolReward, liquidity, totalLiquidity);
            }
            onReward(RewardType::Coinbase, {DCT_ID{0}, providerReward}, height);
        }
        if (poolLoanReward != 0) {
            CAmount providerReward = liquidityReward(poolLoanReward, liquidity, totalLiquidity);
            onReward(RewardType::LoanTokenDEXReward, {DCT_ID{0}, providerReward}, height);
        }
        // commissions
        if (poolSwapHeight == height && poolSwap.swapEvent) {
            CAmount feeA, feeB;
            if (height < newCalcHeight) {
                uint32_t liqWeight = liquidity * PRECISION / totalLiquidity;
                feeA = poolSwap.blockCommissionA * liqWeight / PRECISION;
                feeB = poolSwap.blockCommissionB * liqWeight / PRECISION;
            } else {
                feeA = liquidityReward(poolSwap.blockCommissionA, liquidity, totalLiquidity);
                feeB = liquidityReward(poolSwap.blockCommissionB, liquidity, totalLiquidity);
            }
            if (feeA) {
                onReward(RewardType::Commission, {tokenIds->idTokenA, feeA}, height);
            }
            if (feeB) {
                onReward(RewardType::Commission, {tokenIds->idTokenB, feeB}, height);
            }
        }
        // custom rewards
        for (const auto& reward : customRewards.balances) {
            if (auto providerReward = liquidityReward(reward.second, liquidity, totalLiquidity)) {
                onReward(RewardType::Pool, {reward.first, providerReward}, height);
            }
        }
        ++height;
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

Res CPoolPair::Swap(CTokenAmount in, CAmount dexfeeInPct, PoolPrice const & maxPrice, std::function<Res (const CTokenAmount &, const CTokenAmount &)> onTransfer, int height) {
    if (in.nTokenId != idTokenA && in.nTokenId != idTokenB)
        return Res::Err("Error, input token ID (" + in.nTokenId.ToString() + ") doesn't match pool tokens (" + idTokenA.ToString() + "," + idTokenB.ToString() + ")");

    // TODO: The whole block of the fork condition can be removed safely after FCH.
    if (height < Params().GetConsensus().FortCanningHillHeight) {
        if (in.nValue <= 0)
            return Res::Err("Input amount should be positive!");
    }

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
        CAmount const tradeFee = MultiplyAmounts(in.nValue, commission);
        in.nValue -= tradeFee;
        if (forward) {
            blockCommissionA += tradeFee;
        } else {
            blockCommissionB += tradeFee;
        }
    }

    CTokenAmount dexfeeInAmount{in.nTokenId, 0};
    if (dexfeeInPct > 0 && height >= Params().GetConsensus().FortCanningHillHeight) {
        if (dexfeeInPct > COIN) {
            return Res::Err("Dex fee input percentage over 100%%");
        }
        dexfeeInAmount.nValue = MultiplyAmounts(in.nValue, dexfeeInPct);
        in.nValue -= dexfeeInAmount.nValue;
    }

    auto checkRes = SafeAdd(reserveF, in.nValue);
    if (!checkRes) {
        return Res::Err("Swapping will lead to pool's reserve overflow");
    }

    CAmount result = slopeSwap(in.nValue, reserveF, reserveT, height);

    swapEvent = true; // (!!!)

    return onTransfer(dexfeeInAmount, { forward ? idTokenB : idTokenA, result });
}

CAmount CPoolPair::slopeSwap(CAmount unswapped, CAmount &poolFrom, CAmount &poolTo, int height) {
    assert (unswapped >= 0);
    assert (SafeAdd(unswapped, poolFrom).ok);

    arith_uint256 poolF = arith_uint256(poolFrom);
    arith_uint256 poolT = arith_uint256(poolTo);

    arith_uint256 swapped = 0;
    if (height < Params().GetConsensus().BayfrontGardensHeight) {
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
        if (height >= Params().GetConsensus().FortCanningHillHeight && swapped != 0) {
            // floor the result
            --swapped;
        }
        poolF += unswappedA;
        poolT -= swapped;
    }

    poolFrom = poolF.GetLow64();
    poolTo = poolT.GetLow64();
    return swapped.GetLow64();
}

std::pair<CAmount, CAmount> CPoolPairView::UpdatePoolRewards(std::function<CTokenAmount(CScript const &, DCT_ID)> onGetBalance, std::function<Res(CScript const &, CScript const &, CTokenAmount)> onTransfer, int nHeight) {

    bool newRewardCalc = nHeight >= Params().GetConsensus().BayfrontGardensHeight;
    bool newRewardLogic = nHeight >= Params().GetConsensus().EunosHeight;
    bool newCustomRewards = nHeight >= Params().GetConsensus().ClarkeQuayHeight;

    constexpr uint32_t const PRECISION = 10000; // (== 100%) just searching the way to avoid arith256 inflating
    CAmount totalDistributed = 0;
    CAmount totalLoanDistributed = 0;

    ForEachPoolId([&] (DCT_ID const & poolId) {

        CAmount distributedFeeA = 0;
        CAmount distributedFeeB = 0;
        std::optional<CScript> ownerAddress;

        PoolHeightKey poolKey = {poolId, uint32_t(nHeight)};

        CBalances rewards;
        if (newCustomRewards) {
            if (auto pool = ReadBy<ByID, CPoolPair>(poolId)) {
                rewards = std::move(pool->rewards);
                ownerAddress = std::move(pool->ownerAddress);
            }

            for (auto it = rewards.balances.begin(), next_it = it; it != rewards.balances.end(); it = next_it) {
                ++next_it;

                // Get token balance
                const auto balance = onGetBalance(*ownerAddress, it->first).nValue;

                // Make there's enough to pay reward otherwise remove it
                if (balance < it->second) {
                    rewards.balances.erase(it);
                }
            }

            if (rewards != ReadValueAt<ByCustomReward, CBalances>(this, poolKey)) {
                WriteBy<ByCustomReward>(poolKey, rewards);
            }
        }

        auto totalLiquidity = ReadValueAt<ByTotalLiquidity, CAmount>(this, poolKey);
        if (!totalLiquidity) {
            return true;
        }

        auto swapValue = ReadBy<ByPoolSwap, PoolSwapValue>(poolKey);
        const auto swapEvent = swapValue && swapValue->swapEvent;
        auto poolReward = ReadValueAt<ByPoolReward, CAmount>(this, poolKey);

        if (newRewardLogic) {

            if (swapEvent) {
                // it clears block commission
                distributedFeeA = swapValue->blockCommissionA;
                distributedFeeB = swapValue->blockCommissionB;
            }

            // Get LP loan rewards
            auto poolLoanReward = ReadValueAt<ByPoolLoanReward, CAmount>(this, poolKey);

            // increase by pool block reward
            totalDistributed += poolReward;
            totalLoanDistributed += poolLoanReward;

            for (const auto& reward : rewards.balances) {
                // subtract pool's owner account by custom block reward
                onTransfer(*ownerAddress, {}, {reward.first, reward.second});
            }

        } else {
            if (!swapEvent && poolReward == 0 && rewards.balances.empty()) {
                return true; // no events, skip to the next pool
            }

            ForEachPoolShare([&] (DCT_ID const & currentId, CScript const & provider, uint32_t) {
                if (currentId != poolId) {
                    return false; // stop
                }
                CAmount const liquidity = onGetBalance(provider, poolId).nValue;

                uint32_t const liqWeight = liquidity * PRECISION / totalLiquidity;
                assert (liqWeight < PRECISION);

                // distribute trading fees
                if (swapEvent) {
                    CAmount feeA, feeB;
                    if (newRewardCalc) {
                        feeA = liquidityReward(swapValue->blockCommissionA, liquidity, totalLiquidity);
                        feeB = liquidityReward(swapValue->blockCommissionB, liquidity, totalLiquidity);
                    } else {
                        feeA = swapValue->blockCommissionA * liqWeight / PRECISION;
                        feeB = swapValue->blockCommissionB * liqWeight / PRECISION;
                    }
                    auto tokenIds = ReadBy<ByIDPair, ByPairKey>(poolId);
                    assert(tokenIds);
                    if (onTransfer({}, provider, {tokenIds->idTokenA, feeA})) {
                        distributedFeeA += feeA;
                    }
                    if (onTransfer({}, provider, {tokenIds->idTokenB, feeB})) {
                        distributedFeeB += feeB;
                    }
                }

                // distribute yield farming
                if (poolReward) {
                    CAmount providerReward;
                    if (newRewardCalc) {
                        providerReward = liquidityReward(poolReward, liquidity, totalLiquidity);
                    } else {
                        providerReward = poolReward * liqWeight / PRECISION;
                    }
                    if (onTransfer({}, provider, {DCT_ID{0}, providerReward})) {
                        totalDistributed += providerReward;
                    }
                }

                for (const auto& reward : rewards.balances) {
                    if (auto providerReward = liquidityReward(reward.second, liquidity, totalLiquidity)) {
                        onTransfer(*ownerAddress, provider, {reward.first, providerReward});
                    }
                }

                return true;
            }, PoolShareKey{poolId, CScript{}});
        }

        if (swapEvent) {
            swapValue->blockCommissionA -= distributedFeeA;
            swapValue->blockCommissionB -= distributedFeeB;
            poolKey.height++; // block commissions to next block
            WriteBy<ByPoolSwap>(poolKey, PoolSwapValue{false, swapValue->blockCommissionA, swapValue->blockCommissionB});
        }
        return true;
    });
    return {totalDistributed, totalLoanDistributed};
}

Res CPoolPairView::SetShare(DCT_ID const & poolId, CScript const & provider, uint32_t height) {
    WriteBy<ByShare>(PoolShareKey{poolId, provider}, height);
    return Res::Ok();
}

Res CPoolPairView::DelShare(DCT_ID const & poolId, CScript const & provider) {
    EraseBy<ByShare>(PoolShareKey{poolId, provider});
    return Res::Ok();
}

std::optional<uint32_t> CPoolPairView::GetShare(DCT_ID const & poolId, CScript const & provider) {
    return ReadBy<ByShare, uint32_t>(PoolShareKey{poolId, provider});
}

inline CAmount PoolRewardPerBlock(CAmount dailyReward, CAmount rewardPct) {
    return dailyReward / Params().GetConsensus().blocksPerDay() * rewardPct / COIN;
}

Res CPoolPairView::SetRewardPct(DCT_ID const & poolId, uint32_t height, CAmount rewardPct) {
    if (!HasPoolPair(poolId)) {
        return Res::Err("No such pool pair");
    }
    WriteBy<ByRewardPct>(poolId, rewardPct);
    if (auto dailyReward = ReadBy<ByDailyReward, CAmount>(DCT_ID{})) {
        WriteBy<ByPoolReward>(PoolHeightKey{poolId, height}, PoolRewardPerBlock(*dailyReward, rewardPct));
    }
    return Res::Ok();
}

Res CPoolPairView::SetRewardLoanPct(DCT_ID const & poolId, uint32_t height, CAmount rewardLoanPct) {
    if (!HasPoolPair(poolId)) {
        return Res::Err("No such pool pair");
    }
    WriteBy<ByRewardLoanPct>(poolId, rewardLoanPct);
    if (auto dailyReward = ReadBy<ByDailyLoanReward, CAmount>(DCT_ID{})) {
        WriteBy<ByPoolLoanReward>(PoolHeightKey{poolId, height}, PoolRewardPerBlock(*dailyReward, rewardLoanPct));
    }
    return Res::Ok();
}

Res CPoolPairView::SetDailyReward(uint32_t height, CAmount reward) {
    ForEachPoolId([&](DCT_ID const & poolId) {
        if (auto rewardPct = ReadBy<ByRewardPct, CAmount>(poolId)) {
            WriteBy<ByPoolReward>(PoolHeightKey{poolId, height}, PoolRewardPerBlock(reward, *rewardPct));
        }
        return true;
    });
    WriteBy<ByDailyReward>(DCT_ID{}, reward);
    return Res::Ok();
}

Res CPoolPairView::SetLoanDailyReward(const uint32_t height, const CAmount reward)
{
    ForEachPoolId([&](DCT_ID const & poolId) {
        if (auto rewardLoanPct = ReadBy<ByRewardLoanPct, CAmount>(poolId)) {
            WriteBy<ByPoolLoanReward>(PoolHeightKey{poolId, height}, PoolRewardPerBlock(reward, *rewardLoanPct));
        }
        return true;
    });
    WriteBy<ByDailyLoanReward>(DCT_ID{}, reward);
    return Res::Ok();
}

bool CPoolPairView::HasPoolPair(DCT_ID const & poolId) const {
    return ExistsBy<ByID>(poolId);
}

void CPoolPairView::ForEachPoolId(std::function<bool(const DCT_ID &)> callback, DCT_ID const & start) {
    ForEach<ByID, DCT_ID, CPoolPair>([&callback](const DCT_ID & poolId, CLazySerialize<CPoolPair>) {
        return callback(poolId);
    }, start);
}

void CPoolPairView::ForEachPoolPair(std::function<bool(const DCT_ID &, CPoolPair)> callback, DCT_ID const & start) {
    ForEach<ByID, DCT_ID, CPoolPair>([&](const DCT_ID & poolId, CLazySerialize<CPoolPair>) {
        return callback(poolId, *GetPoolPair(poolId));
    }, start);
}

void CPoolPairView::ForEachPoolShare(std::function<bool (DCT_ID const &, CScript const &, uint32_t)> callback, const PoolShareKey &startKey) {
    ForEach<ByShare, PoolShareKey, uint32_t>([&callback] (PoolShareKey const & poolShareKey, uint32_t height) {
        return callback(poolShareKey.poolID, poolShareKey.owner, height);
    }, startKey);
}

Res CPoolPairView::SetDexFeePct(DCT_ID poolId, DCT_ID tokenId, CAmount feePct) {
    if (!HasPoolPair(poolId)) {
        return Res::Err("No such pool pair");
    }
    if (feePct < 0 || feePct > COIN) {
        return Res::Err("Token dex fee should be in percentage");
    }
    WriteBy<ByTokenDexFeePct>(std::make_pair(poolId, tokenId), uint32_t(feePct));
    return Res::Ok();
}

CAmount CPoolPairView::GetDexFeePct(DCT_ID poolId, DCT_ID tokenId) const {
    uint32_t feePct;
    if (ReadBy<ByTokenDexFeePct>(std::make_pair(poolId, tokenId), feePct)) {
        return feePct;
    }
    return 0;
}
