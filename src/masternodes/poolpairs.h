// Copyright (c) 2019 The DeFi Foundation
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

struct CPoolSwapMessage {
    CScript from, to;
    DCT_ID idTokenFrom, idTokenTo;
    CAmount amountFrom, maxPrice;

    std::string ToString() const {
        std::string result = "(" + from.GetHex() + " " + idTokenFrom.ToString() + " " + std::to_string(amountFrom) + "->" + from.GetHex() + " " + idTokenFrom.ToString() +")";
        return result;
    }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(VARINT(idTokenFrom.v));
        READWRITE(amountFrom);
        READWRITE(maxPrice);
        READWRITE(to);
        READWRITE(VARINT(idTokenTo.v));
    }
};

struct CPoolPairMessage {
    DCT_ID idTokenA, idTokenB;
    CAmount commission;   // comission %% for traders
    CScript ownerFeeAddress;
    bool status = true;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(idTokenA.v));
        READWRITE(VARINT(idTokenB.v));
        READWRITE(commission);
        READWRITE(ownerFeeAddress);
        READWRITE(status);
    }
};

class CPoolPair : public CPoolPairMessage
{
public:
    static const CAmount MINIMUM_LIQUIDITY = 1000;
    static const CAmount PRECISION = COIN; // or just PRECISION_BITS for "<<" and ">>"
    CPoolPair(CPoolPairMessage const & msg = {})
        : CPoolPairMessage(msg)
        , reserveA(0)
        , reserveB(0)
        , totalLiquidity(0)
        , blockCommissionA(0)
        , blockCommissionB(0)
        , kLast(0)
        , rewardPct(0)
        , swapEvent(false)
        , creationTx()
        , creationHeight(-1)
    {}
    virtual ~CPoolPair() = default;

    CAmount reserveA, reserveB, totalLiquidity;
    CAmount blockCommissionA, blockCommissionB;

    arith_uint256 kLast;

    CAmount rewardPct;       // pool yield farming reward %%
    bool swapEvent = false;

    uint256 creationTx;
    uint32_t creationHeight;

    ResVal<CPoolPair> Create(CPoolPairMessage const & msg);     // or smth else

    // 'amountA' && 'amountB' should be normalized (correspond) to actual 'tokenA' and 'tokenB' ids in the pair!!
    // otherwise, 'AddLiquidity' should be () external to 'CPairPool' (i.e. CPoolPairView::AddLiquidity(TAmount a,b etc) with internal lookup of pool by TAmount a,b)
    Res AddLiquidity(CAmount const & amountA, CAmount amountB, CScript const & shareAddress, std::function<Res(CScript to, CAmount liqAmount)> onMint, uint32_t height) {

        mintFee(onMint); // if fact, this is delayed calc (will be 0 on the first pass) // deps: reserveA(R), reserveB(R), kLast, totalLiquidity(RW)

        CAmount liquidity;

        if (totalLiquidity == 0) {
            liquidity = ((arith_uint256(amountA) * arith_uint256(amountB)).sqrt() - MINIMUM_LIQUIDITY).GetLow64();
            // MINIMUM_LIQUIDITY is a hack for non-zero division
            totalLiquidity = MINIMUM_LIQUIDITY;
        } else {
            CAmount liqA = (arith_uint256(amountA) * arith_uint256(totalLiquidity) / reserveA).GetLow64();
            CAmount liqB = (arith_uint256(amountB) * arith_uint256(totalLiquidity) / reserveB).GetLow64();
            liquidity = std::min(liqA, liqB);
        }
        onMint(shareAddress, liquidity); // deps: totalLiquidity(RW)

        {
            auto resA = SafeAdd(reserveA, amountA);
            auto resB = SafeAdd(reserveB, amountB);
            if (resA.ok && resB.ok) {
                reserveA = *resA.val;
                reserveB = *resB.val;
            } else {
                return Res::Err("Overflow when adding to reserves");
            }
        }
        if (!ownerFeeAddress.empty()) {
            kLast = arith_uint256(reserveA) * arith_uint256(reserveB);
        }
        return Res::Ok();
    }

    Res RemoveLiquidity(CScript const & address, CAmount const & liqAmount, std::function<Res(CScript to, CAmount amountA, CAmount amountB)> onBurn, uint32_t height) {

        CAmount resAmountA, resAmountB;

        resAmountA = (arith_uint256(liqAmount) * arith_uint256(reserveA) / totalLiquidity).GetLow64();
        resAmountB = (arith_uint256(liqAmount) * arith_uint256(reserveB) / totalLiquidity).GetLow64();

        auto res = onBurn(address, resAmountA, resAmountB);
        if (!res.ok) {
            return Res::Err("Removing liquidity: %s", res.msg);
        }

        reserveA -= resAmountA;
        reserveB -= resAmountB;
        totalLiquidity -= liqAmount;

        return Res::Ok();
    }

    Res Swap(CTokenAmount in, CAmount maxPrice, std::function<Res(CTokenAmount const &)> onTransfer);

private:
    /// @todo review all 'Res' uses
    void mintFee(std::function<Res(CScript to, CAmount liqAmount)> onMint) {
        if (!ownerFeeAddress.empty()) {
            if (kLast != 0) {
                auto rootK = (arith_uint256(reserveA) * arith_uint256(reserveB)).sqrt();
                auto rootKLast = kLast.sqrt();
                if (rootK > rootKLast) {
                    auto numerator = arith_uint256(totalLiquidity) * (rootK - rootKLast);
                    auto denominator = rootK * 5 + rootKLast;
                    auto liquidity = (numerator / denominator).GetLow64(); // fee ~= 1/6 of delta K (square median) - all math got from uniswap smart contract
                    if (liquidity > 0) onMint(ownerFeeAddress, liquidity);
                }
            }
        } else {
            kLast = 0;
        }
    }

    CAmount slopeSwap(arith_uint256 unswapped, CAmount & poolFrom, CAmount & poolTo);

public:

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CPoolPairMessage, *this);
        READWRITE(reserveA);
        READWRITE(reserveB);
        READWRITE(totalLiquidity);
        READWRITE(blockCommissionA);
        READWRITE(blockCommissionB);
        if (ser_action.ForRead()) {
            uint256 k;
            READWRITE(k);
            kLast = UintToArith256(k);
        }
        else {
            uint256 k = ArithToUint256(kLast);
            READWRITE(k);
        }
        READWRITE(rewardPct);
        READWRITE(swapEvent);
        READWRITE(creationTx);
        READWRITE(creationHeight);
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

            if (!pool.swapEvent && poolReward == 0) {
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
