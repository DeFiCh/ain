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

struct CPoolPairMessage;


class CPoolPair
{
public:
    CAmount reserveA, reserveB, totalLiquidity;

    arith_uint256 priceACumulativeLast, priceBCumulativeLast; // not sure about 'arith', at least sqrt() undefined
    arith_uint256 kLast;
    int32_t lastPoolEventHeight;

    CAmount commissionPct;   // comission %% for traders
    CAmount rewardPct;       // pool yield farming reward %%
    CScript ownerFeeAddress;

    uint256 creationTx;
    int32_t creationHeight;

    DCT_ID idTokenA, idTokenB;
    bool status = true;
    std::string pairSymbol;

    ResVal<CPoolPair> Create(CPoolPairMessage const & msg);     // or smth else
    ResVal<CTokenAmount> AddLiquidity(CTokenAmount const & amountA, CTokenAmount amountB, CScript const & shareAddress);
    // or:
//    ResVal<CTokenAmount> AddLiquidity(CLiquidityMessage const & msg);

    // ????
//    ResVal<???> RemoveLiquidity(???);
//    ResVal<???> Swap(???);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {

    }
};

class CPoolPairView : public virtual CStorageView
{
public:
    Res CreatePoolPair(CPoolPair const & pool, DCT_ID & poolId);
    Res DeletePoolPair(DCT_ID const & poolId);

    boost::optional<CPoolPair> GetPoolPair(DCT_ID const & poolId) const;
//    boost::optional<std::pair<DCT_ID, CPoolPair> > GetPoolPairGuessId(const std::string & str) const; // optional
    boost::optional<std::pair<DCT_ID, CPoolPair> > GetPoolPair(DCT_ID const & tokenA, DCT_ID const & tokenB) const;

    void ForEachPoolPair(std::function<bool(DCT_ID const & id, CPoolPair const & pool)> callback, DCT_ID const & start = DCT_ID{0});
    void ForEachShare(std::function<bool(DCT_ID const & id, CScript const & provider)> callback, DCT_ID const & start = DCT_ID{0});
//    void ForEachShare(std::function<bool(DCT_ID const & id, CScript const & provider, CAmount amount)> callback, DCT_ID const & start = DCT_ID{0}); // optional, with lookup into accounts

    // tags
    struct ByID { static const unsigned char prefix; }; // lsTokenID -> СPoolPair
    struct ByPair { static const unsigned char prefix; }; // tokenA+tokenB -> lsTokenID
    struct ByShare { static const unsigned char prefix; }; // lsTokenID+accountID -> {}
};


#endif // DEFI_MASTERNODES_POOLPAIRS_H
