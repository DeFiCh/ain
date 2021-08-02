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
        READWRITE(idTokenA);
        READWRITE(idTokenB);
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
        READWRITE(idTokenFrom);
        READWRITE(amountFrom);
        READWRITE(to);
        READWRITE(idTokenTo);
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
        READWRITE(idTokenA);
        READWRITE(idTokenB);
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
    CPoolPair(CPoolPairMessage const & msg = {}) : CPoolPairMessage(msg) {}
    virtual ~CPoolPair() = default;

    // temporary values, not serialized
    CAmount reserveA = 0;
    CAmount reserveB = 0;
    CAmount totalLiquidity = 0;
    CAmount blockCommissionA = 0;
    CAmount blockCommissionB = 0;

    CAmount rewardPct = 0;       // pool yield farming reward %%
    bool swapEvent = false;

    // serialized
    CBalances rewards;
    uint256 creationTx;
    uint32_t creationHeight = -1;

    // 'amountA' && 'amountB' should be normalized (correspond) to actual 'tokenA' and 'tokenB' ids in the pair!!
    // otherwise, 'AddLiquidity' should be () external to 'CPairPool' (i.e. CPoolPairView::AddLiquidity(TAmount a,b etc) with internal lookup of pool by TAmount a,b)
    Res AddLiquidity(CAmount amountA, CAmount amountB, std::function<Res(CAmount)> onMint, bool slippageProtection = false);
    Res RemoveLiquidity(CAmount liqAmount, std::function<Res(CAmount, CAmount)> onReclaim);

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
        READWRITE(rewards);
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

struct PoolHeightKey {
    DCT_ID poolID;
    uint32_t height;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(poolID);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(height));
            height = ~height;
        } else {
            uint32_t height_ = ~height;
            READWRITE(WrapBigEndian(height_));
        }
    }
};

enum RewardType
{
    Commission = 127,
    Rewards = 128,
    Coinbase = Rewards | 1,
    Pool = Rewards | 2,
};

std::string RewardToString(RewardType type);
std::string RewardTypeToString(RewardType type);

class CPoolPairView : public virtual CStorageView
{
public:
    Res SetPoolPair(const DCT_ID &poolId, uint32_t height, CPoolPair const & pool);
    Res UpdatePoolPair(DCT_ID const & poolId, uint32_t height, bool status, CAmount const & commission, CScript const & ownerAddress, CBalances const & rewards);

    boost::optional<CPoolPair> GetPoolPair(const DCT_ID &poolId) const;
    boost::optional<std::pair<DCT_ID, CPoolPair> > GetPoolPair(DCT_ID const & tokenA, DCT_ID const & tokenB) const;

    void ForEachPoolId(std::function<bool(DCT_ID const &)> callback, DCT_ID const & start = DCT_ID{0});
    void ForEachPoolPair(std::function<bool(DCT_ID const &, CPoolPair)> callback, DCT_ID const & start = DCT_ID{0});
    void ForEachPoolShare(std::function<bool(DCT_ID const &, CScript const &, uint32_t)> callback, PoolShareKey const &startKey = {});

    Res SetShare(DCT_ID const & poolId, CScript const & provider, uint32_t height);
    Res DelShare(DCT_ID const & poolId, CScript const & provider);

    boost::optional<uint32_t> GetShare(DCT_ID const & poolId, CScript const & provider);

    void CalculatePoolRewards(DCT_ID const & poolId, std::function<CAmount()> onLiquidity, uint32_t begin, uint32_t end, std::function<void(RewardType, CTokenAmount, uint32_t)> onReward);

    Res SetDailyReward(uint32_t height, CAmount reward);
    Res SetRewardPct(DCT_ID const & poolId, uint32_t height, CAmount rewardPct);
    bool HasPoolPair(DCT_ID const & poolId) const;

    CAmount UpdatePoolRewards(std::function<CTokenAmount(CScript const &, DCT_ID)> onGetBalance, std::function<Res(CScript const &, CScript const &, CTokenAmount)> onTransfer, int nHeight = 0);

    // tags
    struct ByID { static const unsigned char prefix; }; // lsTokenID -> СPoolPair
    struct ByPair { static const unsigned char prefix; }; // tokenA+tokenB -> lsTokenID
    struct ByShare { static const unsigned char prefix; }; // lsTokenID+accountID -> {}
    struct ByIDPair { static const unsigned char prefix; }; // lsTokenID -> tokenA+tokenB
    struct ByPoolSwap { static const unsigned char prefix; };
    struct ByReserves { static const unsigned char prefix; };
    struct ByRewardPct { static const unsigned char prefix; };
    struct ByPoolReward { static const unsigned char prefix; };
    struct ByDailyReward { static const unsigned char prefix; };
    struct ByCustomReward { static const unsigned char prefix; };
    struct ByTotalLiquidity { static const unsigned char prefix; };
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
