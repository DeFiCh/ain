// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <masternodes/orders_matching.h>

static arith_uint256 To256(uint64_t a) {
    return arith_uint256{a};
}

static arith_uint256 To256(int64_t a) {
    assert(a >= 0);
    return To256((uint64_t) a);
}

/*
// nearlyEqual returns true if difference between A and B is smaller than ratio of 1/maxDiffFactor
static bool nearlyEqual(arith_uint256 a, arith_uint256 b, uint64_t maxDiffFactor) {
    const auto& big = std::max(a, b);
    const auto& small = std::min(a, b);
    const auto diff = big - small;
    return (diff * To256(maxDiffFactor)) <= big;
}
*/

static CAmount ToAmount(arith_uint256 const & a) {
    return (CAmount) a.GetLow64();
}

constexpr auto errTokensMismatch = "give/take token IDs do not match";
constexpr auto errPriceMismatch = "give/take prices do not overlap";
constexpr auto errSanity = "invariants check failed";

// CalcGiven returns amount Alice should pay to Carol during matching. May be called for Carol by mirroring variables
static arith_uint256 CalcGiven(arith_uint256 const & at, arith_uint256 const & ct, arith_uint256 const & AG, arith_uint256 const & AT) {
    if (at == AT) { // short circuit if order is fulfilled
        return AG;
    }
    arith_uint256 _ag = (at * AG) / AT; // alice pays
    return std::max(_ag, ct); // alice cannot pay less than carol receives (1) (correct rounding error)
}

static bool CheckInvatiants(arith_uint256 const & at, arith_uint256 const & ag, arith_uint256 const & ct, arith_uint256 const & AT, arith_uint256 const & AG) {
    /// check final price is not smaller than requested due to rounding
    if (AT * ag > at * AG) {
        return false;
    }
    // alice gives not less than carol takes
    if (!(ag >= ct)) { // ag >= ct due to (1)
        return false;
    }
    /*// check rounding error is not greater than 0.01%
    if (!neatlyEqual(AT * ag, at * AG, 10000)) {
        return OrdersMatching::Err("rounding error is greater than 0.01% [alice]");
    }*/
    // limit checks
    return at <= AT && ag <= AG;
}

ResVal<OrdersMatching> OrdersMatching::Calculate(COrder const & aliceOrder, COrder const & carolOrder)
{
    /// check token IDs do match
    const bool matchOk = aliceOrder.give.nTokenId == carolOrder.take.nTokenId && aliceOrder.take.nTokenId == carolOrder.give.nTokenId;
    if (!matchOk) {
        return Res::Err(errTokensMismatch);
    }

    /// a/c means alice/carol, t/g means take/give
    const auto AT = To256(aliceOrder.take.nValue);
    const auto AG = To256(aliceOrder.give.nValue);
    const auto CT = To256(carolOrder.take.nValue);
    const auto CG = To256(carolOrder.give.nValue);

    /// check prices do overlap
    if (AT * CT > AG * CG) {
        return Res::Err(errPriceMismatch);
    }

    /// calculate actual coin movements in this tx, according to limits in orders
    const auto& at = std::min(AT, CG); // alice receives not more than she's willing to take, not more than carol is willing to give
    const auto& ct = std::min(CT, AG); // carol receives

    /// calculate amount of paid tokens according to price ratio (rounding error is possible)
    const auto ag = CalcGiven(at, ct, AG, AT); // alice gives
    const auto cg = CalcGiven(ct, at, CG, CT); // carol gives

    if (!CheckInvatiants(at, ag, ct, AT, AG)) {
        return Res::Err(errSanity);
    }
    if (!CheckInvatiants(ct, cg, at, CT, CG)) {
        return Res::Err(errSanity);
    }

    /// calculate matcher income
    OrdersMatching m{};
    /// premium from Alice
    m.alice.premiumGive.nTokenId = aliceOrder.premium.nTokenId;
    m.alice.premiumGive.nValue = ToAmount((at * To256(aliceOrder.premium.nValue)) / AT);
    auto res = m.matcherTake.Add(m.alice.premiumGive);
    if (!res.ok) {
        return res;
    }
    /// premium from Carol
    m.carol.premiumGive.nTokenId = carolOrder.premium.nTokenId;
    m.carol.premiumGive.nValue = ToAmount((ct *  To256(carolOrder.premium.nValue)) / CT);
    res = m.matcherTake.Add(m.carol.premiumGive);
    if (!res.ok) {
        return res;
    }

    /// difference between prices goes to matcher
    res = m.matcherTake.Add(CTokenAmount{aliceOrder.give.nTokenId, ToAmount(ag - ct)}); // ag >= ct due to (1)
    if (!res.ok) {
        return res;
    }
    res = m.matcherTake.Add(CTokenAmount{carolOrder.give.nTokenId, ToAmount(cg - at)});
    if (!res.ok) {
        return res;
    }

    /// glue code
    m.alice.take.nTokenId = aliceOrder.take.nTokenId;
    m.alice.take.nValue = ToAmount(at);
    m.alice.give.nTokenId = aliceOrder.give.nTokenId;
    m.alice.give.nValue = ToAmount(ag);

    m.carol.take.nTokenId = carolOrder.take.nTokenId;
    m.carol.take.nValue = ToAmount(ct);
    m.carol.give.nTokenId = carolOrder.give.nTokenId;
    m.carol.give.nValue = ToAmount(cg);

    return {m, Res::Ok()};
}
