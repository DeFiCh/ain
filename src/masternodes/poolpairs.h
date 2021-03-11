// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_POOLPAIRS_H
#define DEFI_MASTERNODES_POOLPAIRS_H

#include <flushablestorage.h>

#include <amount.h>
#include <arith_uint256.h>
#include <chainparams.h>
#include <masternodes/res.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>
#include <masternodes/balances.h>

struct ByPairKey {
    DCT_ID idTokenA;
    DCT_ID idTokenB;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(idTokenA.v));
        READWRITE(VARINT(idTokenB.v));
    }
};

struct PoolPrice {
    int64_t integer;
    int64_t fraction;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(integer);
        READWRITE(fraction);
    }
};

struct CPoolSwapMessage {
    CScript from, to;
    DCT_ID idTokenFrom, idTokenTo;
    CAmount amountFrom;
    PoolPrice maxPrice;

    std::string ToString() const {
        return "(" + from.GetHex() + ":" + std::to_string(amountFrom) +"@"+ idTokenFrom.ToString() + "->" + to.GetHex() + ":?@" + idTokenTo.ToString() +")";
    }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(VARINT(idTokenFrom.v));
        READWRITE(amountFrom);
        READWRITE(to);
        READWRITE(VARINT(idTokenTo.v));
        READWRITE(maxPrice);
    }
};

struct CPoolPairMessage {
    DCT_ID idTokenA, idTokenB;
    CAmount commission;   // comission %% for traders
    CScript ownerAddress;
    bool status = true;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(idTokenA.v));
        READWRITE(VARINT(idTokenB.v));
        READWRITE(commission);
        READWRITE(ownerAddress);
        READWRITE(status);
    }
};

class CPoolPair : public CPoolPairMessage
{
public:
    static const CAmount MINIMUM_LIQUIDITY = 1000;
    static const CAmount SLOPE_SWAP_RATE = 1000;
    static const uint32_t PRECISION = (uint32_t) COIN; // or just PRECISION_BITS for "<<" and ">>"
    CPoolPair(CPoolPairMessage const & msg = {})
        : CPoolPairMessage(msg)
        , reserveA(0)
        , reserveB(0)
        , totalLiquidity(0)
        , blockCommissionA(0)
        , blockCommissionB(0)
        , rewardPct(0)
        , swapEvent(false)
        , creationTx()
        , creationHeight(-1)
    {}
    virtual ~CPoolPair() = default;

    CAmount reserveA, reserveB, totalLiquidity;
    CAmount blockCommissionA, blockCommissionB;

    CAmount rewardPct;       // pool yield farming reward %%
    bool swapEvent = false;

    uint256 creationTx;
    uint32_t creationHeight;

    // 'amountA' && 'amountB' should be normalized (correspond) to actual 'tokenA' and 'tokenB' ids in the pair!!
    // otherwise, 'AddLiquidity' should be () external to 'CPairPool' (i.e. CPoolPairView::AddLiquidity(TAmount a,b etc) with internal lookup of pool by TAmount a,b)
    Res AddLiquidity(CAmount amountA, CAmount amountB, CScript const & shareAddress, std::function<Res(CScript const & to, CAmount liqAmount)> onMint, bool slippageProtection = false) {
        // instead of assertion due to tests
        if (amountA <= 0 || amountB <= 0) {
            return Res::Err("amounts should be positive");
        }

        CAmount liquidity{0};
        if (totalLiquidity == 0) {
            liquidity = (CAmount) (arith_uint256(amountA) * arith_uint256(amountB)).sqrt().GetLow64(); // sure this is below std::numeric_limits<CAmount>::max() due to sqrt natue
            if (liquidity <= MINIMUM_LIQUIDITY) // ensure that it'll be non-zero
                return Res::Err("liquidity too low");
            liquidity -= MINIMUM_LIQUIDITY;
            // MINIMUM_LIQUIDITY is a hack for non-zero division
            totalLiquidity = MINIMUM_LIQUIDITY;
        } else {
            CAmount liqA = (arith_uint256(amountA) * arith_uint256(totalLiquidity) / reserveA).GetLow64();
            CAmount liqB = (arith_uint256(amountB) * arith_uint256(totalLiquidity) / reserveB).GetLow64();
            liquidity = std::min(liqA, liqB);

            if (liquidity == 0)
                return Res::Err("amounts too low, zero liquidity");

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

        return onMint(shareAddress, liquidity);
    }

    Res RemoveLiquidity(CScript const & address, CAmount const & liqAmount, std::function<Res(CScript to, CAmount amountA, CAmount amountB)> onReclaim) {
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

        return onReclaim(address, resAmountA, resAmountB);
    }

    Res Swap(CTokenAmount in, PoolPrice const & maxPrice, std::function<Res(CTokenAmount const &)> onTransfer, int height = INT_MAX);

private:
    CAmount slopeSwap(CAmount unswapped, CAmount & poolFrom, CAmount & poolTo, bool postBayfrontGardens = false);

    inline void ioProofer() const { // may be it's more reasonable to use unsigned everywhere, but for basic CAmount compatibility
        if (reserveA < 0 || reserveB < 0 ||
            totalLiquidity < 0 ||
            blockCommissionA < 0 || blockCommissionB < 0 ||
            rewardPct < 0 || commission < 0
            ) {
            throw std::ios_base::failure("negative pool's 'CAmounts'");
        }
    }

public:

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (!ser_action.ForRead()) ioProofer();

        READWRITEAS(CPoolPairMessage, *this);
        READWRITE(reserveA);
        READWRITE(reserveB);
        READWRITE(totalLiquidity);
        READWRITE(blockCommissionA);
        READWRITE(blockCommissionB);
        READWRITE(rewardPct);
        READWRITE(swapEvent);
        READWRITE(creationTx);
        READWRITE(creationHeight);

        if (ser_action.ForRead()) ioProofer();
    }
};

struct PoolShareKey {
    DCT_ID poolID;
    CScript owner;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(WrapBigEndian(poolID.v));
        READWRITE(owner);
    }
};

enum class RewardType : uint8_t
{
    Commission = 128,
    Rewards = 129,
};

inline std::string RewardToString(RewardType type)
{
    switch(type) {
        case RewardType::Commission: return "Commission";
        case RewardType::Rewards: return "Rewards";
    }
    return "Unknown";
}

class CPoolPairView : public virtual CStorageView
{
public:
    Res SetPoolPair(const DCT_ID &poolId, CPoolPair const & pool);
    Res UpdatePoolPair(DCT_ID const & poolId, bool & status, CAmount const & commission, CScript const & ownerAddress);

    Res SetPoolCustomReward(const DCT_ID &poolId, CBalances &rewards);
    boost::optional<CBalances> GetPoolCustomReward(const DCT_ID &poolId);

    boost::optional<CPoolPair> GetPoolPair(const DCT_ID &poolId) const;
    boost::optional<std::pair<DCT_ID, CPoolPair> > GetPoolPair(DCT_ID const & tokenA, DCT_ID const & tokenB) const;

    void ForEachPoolPair(std::function<bool(DCT_ID const &, CLazySerialize<CPoolPair>)> callback, DCT_ID const & start = DCT_ID{0});
    void ForEachPoolShare(std::function<bool(DCT_ID const &, CScript const &)> callback, PoolShareKey const &startKey = {}) const;

    Res SetShare(DCT_ID const & poolId, CScript const & provider) {
        WriteBy<ByShare>(PoolShareKey{poolId, provider}, '\0');
        return Res::Ok();
    }
    Res DelShare(DCT_ID const & poolId, CScript const & provider) {
        EraseBy<ByShare>(PoolShareKey{poolId, provider});
        return Res::Ok();
    }

    CAmount DistributeRewards(CAmount yieldFarming, std::function<CTokenAmount(CScript const & owner, DCT_ID tokenID)> onGetBalance, std::function<Res(CScript const & to, CScript const & from, DCT_ID poolID, uint8_t type, CTokenAmount amount)> onTransfer, int nHeight = 0) {

        bool newRewardCalc = nHeight >= Params().GetConsensus().BayfrontGardensHeight;

        uint32_t const PRECISION = 10000; // (== 100%) just searching the way to avoid arith256 inflating
        CAmount totalDistributed = 0;

        ForEachPoolPair([&] (DCT_ID const & poolId, CPoolPair pool) {

            // yield farming counters
            CAmount const poolReward = yieldFarming * pool.rewardPct / COIN; // 'rewardPct' should be defined by 'setgov "LP_SPLITS"', also, it is assumed that it was totally validated and normalized to 100%
            CAmount distributedFeeA = 0;
            CAmount distributedFeeB = 0;

            auto rewards = GetPoolCustomReward(poolId);
            bool payCustomRewards{false};

            if (nHeight >= Params().GetConsensus().ClarkeQuayHeight && rewards && !rewards->balances.empty()) {
                for (auto it = rewards->balances.cbegin(), next_it = it; it != rewards->balances.cend(); it = next_it) {
                    ++next_it;

                    // Get token balance
                    const auto balance = onGetBalance(pool.ownerAddress, it->first).nValue;

                    // Make there's enough to pay reward otherwise remove it
                    if (balance < it->second) {
                        rewards->balances.erase(it);
                    }
                }

                if (!rewards->balances.empty()) {
                    payCustomRewards = true;
                }
            }

            if (pool.totalLiquidity == 0 || (!pool.swapEvent && poolReward == 0 && !payCustomRewards)) {
                return true; // no events, skip to the next pool
            }

            ForEachPoolShare([&] (DCT_ID const & currentId, CScript const & provider) {
                if (currentId != poolId) {
                    return false; // stop
                }
                CAmount const liquidity = onGetBalance(provider, poolId).nValue;

                uint32_t const liqWeight = liquidity * PRECISION / pool.totalLiquidity;
                assert (liqWeight < PRECISION);

                // distribute trading fees
                if (pool.swapEvent) {
                    CAmount feeA, feeB;
                    if (newRewardCalc) {
                        feeA = static_cast<CAmount>((arith_uint256(pool.blockCommissionA) * arith_uint256(liquidity) / arith_uint256(pool.totalLiquidity)).GetLow64());
                        feeB = static_cast<CAmount>((arith_uint256(pool.blockCommissionB) * arith_uint256(liquidity) / arith_uint256(pool.totalLiquidity)).GetLow64());
                    } else {
                        feeA = pool.blockCommissionA * liqWeight / PRECISION;
                        feeB = pool.blockCommissionB * liqWeight / PRECISION;
                    }
                    if (onTransfer(provider, CScript(), poolId, uint8_t(RewardType::Commission), {pool.idTokenA, feeA}).ok) {
                        distributedFeeA += feeA;
                    }
                    if (onTransfer(provider, CScript(), poolId, uint8_t(RewardType::Commission), {pool.idTokenB, feeB}).ok) {
                        distributedFeeB += feeB;
                    }
                }

                // distribute yield farming
                if (poolReward) {
                    CAmount providerReward = static_cast<CAmount>((arith_uint256(poolReward) * arith_uint256(liquidity) / arith_uint256(pool.totalLiquidity)).GetLow64());
                    if (!newRewardCalc) {
                        providerReward = poolReward * liqWeight / PRECISION;
                    }
                    if (providerReward) {
                        if (onTransfer(provider, CScript(), poolId, uint8_t(RewardType::Rewards), {DCT_ID{0}, providerReward}).ok) {
                            totalDistributed += providerReward;
                        }
                    }
                }

                // Distribute custom rewards
                if (payCustomRewards) {
                    for (const auto& reward : rewards->balances) {
                        CAmount providerReward = static_cast<CAmount>((arith_uint256(reward.second) * arith_uint256(liquidity) / arith_uint256(pool.totalLiquidity)).GetLow64());

                        if (providerReward) {
                            onTransfer(provider, pool.ownerAddress, poolId, uint8_t(RewardType::Rewards), {reward.first, providerReward});
                        }
                    }
                }

                return true;
            }, PoolShareKey{poolId, CScript{}});

            pool.blockCommissionA -= distributedFeeA;
            pool.blockCommissionB -= distributedFeeB;
            pool.swapEvent = false;

            auto res = SetPoolPair(poolId, pool);
            if (!res.ok) {
                LogPrintf("Pool rewards: can't update pool (id=%s) state: %s\n", poolId.ToString(), res.msg);
            }
            return true;
        });
        return totalDistributed;
    }


    // tags
    struct ByID { static const unsigned char prefix; }; // lsTokenID -> СPoolPair
    struct ByPair { static const unsigned char prefix; }; // tokenA+tokenB -> lsTokenID
    struct ByShare { static const unsigned char prefix; }; // lsTokenID+accountID -> {}
    struct Reward { static const unsigned char prefix; }; // lsTokenID -> CBalances
};

struct CLiquidityMessage {
    CAccounts from; // from -> balances
    CScript shareAddress;

    std::string ToString() const {
        if (from.empty()) {
            return "empty transfer";
        }
        std::string result;
        for (const auto& kv : from) {
            result += "(" + kv.first.GetHex() + "->" + kv.second.ToString() + ")";
        }
        result += " to " + shareAddress.GetHex();
        return result;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(shareAddress);
    }
};

struct CRemoveLiquidityMessage {
    CScript from;
    CTokenAmount amount;

    std::string ToString() const {
        std::string result = "(" + from.GetHex() + "->" + amount.ToString() + ")";
        return result;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(amount);
    }
};

#endif // DEFI_MASTERNODES_POOLPAIRS_H
