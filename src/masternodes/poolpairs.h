// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_POOLPAIRS_H
#define DEFI_MASTERNODES_POOLPAIRS_H

#include <flushablestorage.h>

#include <amount.h>
#include <arith_uint256.h>
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
    Res AddLiquidity(CAmount amountA, CAmount amountB, CScript const & shareAddress, std::function<Res(CScript const & to, CAmount liqAmount)> onMint) {
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

    Res Swap(CTokenAmount in, PoolPrice const & maxPrice, std::function<Res(CTokenAmount const &)> onTransfer);

private:
    CAmount slopeSwap(CAmount unswapped, CAmount & poolFrom, CAmount & poolTo);

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


class CPoolPairView : public virtual CStorageView
{
public:
    Res SetPoolPair(const DCT_ID &poolId, CPoolPair const & pool);
    Res UpdatePoolPair(DCT_ID const & poolId, bool & status, CAmount const & commission, CScript const & ownerAddress);
    Res DeletePoolPair(DCT_ID const & poolId);

    boost::optional<CPoolPair> GetPoolPair(const DCT_ID &poolId) const;
    boost::optional<std::pair<DCT_ID, CPoolPair> > GetPoolPair(DCT_ID const & tokenA, DCT_ID const & tokenB) const;

    void ForEachPoolPair(std::function<bool(DCT_ID const & id, CPoolPair const & pool)> callback, DCT_ID const & start = DCT_ID{0});
    void ForEachPoolShare(std::function<bool(DCT_ID const & id, CScript const & provider)> callback, PoolShareKey const &startKey = PoolShareKey{0,CScript{}}) const;

    Res SetShare(DCT_ID const & poolId, CScript const & provider) {
        WriteBy<ByShare>(PoolShareKey{ poolId, provider}, '\0');
        return Res::Ok();
    }
    Res DelShare(DCT_ID const & poolId, CScript const & provider) {
        EraseBy<ByShare>(PoolShareKey{poolId, provider});
        return Res::Ok();
    }

    /// @attention it throws (at least for debug), cause errors are critical!
    CAmount DistributeRewards(CAmount yieldFarming, std::function<CTokenAmount(CScript const & owner, DCT_ID tokenID)> onGetBalance, std::function<Res(CScript const & to, CTokenAmount amount)> onTransfer) {

        uint32_t const PRECISION = 10000; // (== 100%) just searching the way to avoid arith256 inflating
        CAmount totalDistributed = 0;

        ForEachPoolPair([&] (DCT_ID const & poolId, CPoolPair const & pool) {

            // yield farming counters
            CAmount const poolReward = yieldFarming * pool.rewardPct / COIN; // 'rewardPct' should be defined by 'setgov "LP_SPLITS"', also, it is assumed that it was totally validated and normalized to 100%
            CAmount distributedFeeA = 0;
            CAmount distributedFeeB = 0;

            if (!pool.swapEvent && (poolReward == 0 || pool.totalLiquidity == 0)) {
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
                    CAmount feeA = pool.blockCommissionA * liqWeight / PRECISION;       // liquidity / pool.totalLiquidity;
                    distributedFeeA += feeA;
                    onTransfer(provider, {pool.idTokenA, feeA}); //can throw

                    CAmount feeB = pool.blockCommissionB * liqWeight / PRECISION;       // liquidity / pool.totalLiquidity;
                    distributedFeeB += feeB;
                    onTransfer(provider, {pool.idTokenB, feeB}); //can throw
                }

                // distribute yield farming
                if (poolReward) {
                    CAmount providerReward = poolReward * liqWeight / PRECISION;        //liquidity / pool.totalLiquidity;
                    if (providerReward) {
                        onTransfer(provider, {DCT_ID{0}, providerReward}); //can throw
                        totalDistributed += providerReward;
                    }
                }
                return true;
            }, PoolShareKey{poolId, CScript{}});

            // we have no "non-const foreaches", but it is safe here cause not broke indexes, so:
            const_cast<CPoolPair &>(pool).blockCommissionA -= distributedFeeA;
            const_cast<CPoolPair &>(pool).blockCommissionB -= distributedFeeB;
            const_cast<CPoolPair &>(pool).swapEvent = false;

            auto res = SetPoolPair(poolId, pool);
            if (!res.ok)
                throw std::runtime_error(strprintf("Pool rewards: can't update pool (id=%s) state: %s", poolId.ToString(), res.msg));
            return true;
        });
        return totalDistributed;
    }


    // tags
    struct ByID { static const unsigned char prefix; }; // lsTokenID -> СPoolPair
    struct ByPair { static const unsigned char prefix; }; // tokenA+tokenB -> lsTokenID
    struct ByShare { static const unsigned char prefix; }; // lsTokenID+accountID -> {}
};

struct CLiquidityMessage {
    std::map<CScript, CBalances> from; // from -> balances
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
