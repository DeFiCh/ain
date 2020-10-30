#include <chainparams.h>
#include <masternodes/masternodes.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>


inline uint256 NextTx()
{
    static int txs_counter = 1;

    std::stringstream stream;
    stream << std::hex << txs_counter++;
    return uint256S(stream.str() );
}

DCT_ID CreateToken(CCustomCSView &mnview, std::string const & symbol, uint8_t flags = (uint8_t)CToken::TokenFlags::Default)
{
    CTokenImplementation token;
    token.creationTx = NextTx();
    token.symbol = symbol;
    token.flags = flags;

    auto res = mnview.CreateToken(token, false);
    if (!res.ok) printf("%s\n", res.msg.c_str());
    BOOST_REQUIRE(res.ok);
    return *res.val;
}


// tokenz A,B,LP
std::tuple<DCT_ID, DCT_ID, DCT_ID> CreatePoolNTokens(CCustomCSView &mnview, std::string const & symbolA, std::string const & symbolB)
{
    DCT_ID idA = CreateToken(mnview, symbolA);
    DCT_ID idB = CreateToken(mnview, symbolB);

    DCT_ID idPool = CreateToken(mnview, symbolA + "-" +  symbolB, (uint8_t)CToken::TokenFlags::Default | (uint8_t)CToken::TokenFlags::DAT | (uint8_t)CToken::TokenFlags::LPS);
    {
        CPoolPair pool{};
        pool.idTokenA = idA;
        pool.idTokenB = idB;
        pool.commission = 1000000; // 1%
    //    poolMsg.ownerAddress = CScript();
        pool.status = true;
        BOOST_REQUIRE(mnview.SetPoolPair(idPool, pool).ok);
    }
    return std::tuple<DCT_ID, DCT_ID, DCT_ID>(idA, idB, idPool); // ! simple initialization list (as "{a,b,c}")  doesn't work here under ubuntu 16.04 - due to older gcc?
}

Res AddPoolLiquidity(CCustomCSView &mnview, DCT_ID idPool, CAmount amountA, CAmount amountB, CScript const & shareAddress)
{
    auto optPool = mnview.GetPoolPair(idPool);
    BOOST_REQUIRE(optPool);

    const auto res = optPool->AddLiquidity(amountA, amountB, shareAddress, [&] /*onMint*/(CScript const & to, CAmount liqAmount) -> Res {
        BOOST_CHECK(liqAmount > 0);

        auto add = mnview.AddBalance(to, { idPool, liqAmount });
        if (!add.ok) {
            return add;
        }
        //insert update ByShare index
        const auto setShare = mnview.SetShare(idPool, to);
        if (!setShare.ok) {
            return setShare;
        }
        return Res::Ok();
    });
    BOOST_REQUIRE(res.ok);
    return mnview.SetPoolPair(idPool, *optPool);
}

BOOST_FIXTURE_TEST_SUITE(liquidity_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(math_liquidity_and_trade)
{
    auto FAIL_onMint = [](const CScript &, CAmount)-> Res { BOOST_REQUIRE(false); return Res::Err("it should not happen"); };
    auto FAIL_onSwap = [](const CTokenAmount &)-> Res { BOOST_REQUIRE(false); return Res::Err("it should not happen"); };
    auto OK_onMint = [](const CScript &, CAmount)-> Res { return Res::Ok(); };
    auto OK_onSwap = [](const CTokenAmount &)-> Res { return Res::Ok(); };

    CCustomCSView mnview(*pcustomcsview);

    DCT_ID idA, idB, idPool;
    std::tie(idA, idB, idPool) = CreatePoolNTokens(mnview, "AAA", "BBB");
    auto optPool = mnview.GetPoolPair(idPool);
    BOOST_REQUIRE(optPool);

    Res res{};
    { // basic fails
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(-1, 1000, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "amounts should be positive");
        res = pool.AddLiquidity(0, 1000, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "amounts should be positive");
        res = pool.AddLiquidity(1, 1000, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "liquidity too low");
        res = pool.AddLiquidity(10, 100000, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "liquidity too low"); // median == MINIMUM_LIQUIDITY
    }

    { // amounts a bit larger MINIMUM_LIQUIDITY
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(11, 100000, {}, [](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == 48); // sqrt (11*100000) - MINIMUM_LIQUIDITY
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == 11);
        BOOST_CHECK(pool.reserveB == 100000);
    }

    {   // one limit
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(std::numeric_limits<CAmount>::max(), 1, {}, [](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == 3036999499); // == sqrt(limit)-MINIMUM_LIQUIDITY
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == std::numeric_limits<CAmount>::max());
        BOOST_CHECK(pool.reserveB == 1);
        BOOST_CHECK(pool.totalLiquidity == 3036999499 + CPoolPair::MINIMUM_LIQUIDITY);

        // plus 1
        res = pool.AddLiquidity(1, 1, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "amounts too low, zero liquidity");

        // we should place smth significant, that give us at least 1 liq point ("X * totalLiquidity >= reserve")
        // plus 3037000500+1
        res = pool.AddLiquidity(3037000500+1, 1, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "overflow when adding to reserves"); // in fact we got min liquidity == 1, but reserves overflowed

        // thats all, we can't place anything here until removing. trading disabled due to reserveB < SLOPE_SWAP_RATE

        // we can't swap forward even 1 satoshi
        res = pool.Swap(CTokenAmount{pool.idTokenA, 1}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, FAIL_onSwap);
        BOOST_CHECK(!res.ok && res.msg == "Lack of liquidity.");

        // and backward too
        res = pool.Swap(CTokenAmount{pool.idTokenB, 2}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, FAIL_onSwap);
        BOOST_CHECK(!res.ok && res.msg == "Lack of liquidity.");
    }

    {   // two limits
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(std::numeric_limits<CAmount>::max(), std::numeric_limits<CAmount>::max(), {}, [](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == std::numeric_limits<CAmount>::max() - CPoolPair::MINIMUM_LIQUIDITY);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == std::numeric_limits<CAmount>::max());
        BOOST_CHECK(pool.reserveB == std::numeric_limits<CAmount>::max());
        BOOST_CHECK(pool.totalLiquidity == std::numeric_limits<CAmount>::max());

        // thats all, we can't do anything here until removing
    }

    // trying to swap moooore than reserved on low reserves (sliding)
    // it works extremely bad on low reserves, but it's okay, just bad trade.
    {
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(1001, 1001, {}, [](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == 1001 - CPoolPair::MINIMUM_LIQUIDITY);
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, 1000000}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK_EQUAL(ta.nValue, 1000);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK_EQUAL(pool.blockCommissionA, 10000);
        BOOST_CHECK_EQUAL(pool.reserveA, 991001);
        BOOST_CHECK_EQUAL(pool.reserveB, 1);
    }

    // trying to swap moooore than reserved (sliding), but on "resonable" reserves
    {
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(COIN, COIN, {}, [](const CScript &, CAmount liq)-> Res {
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, 2*COIN}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK_EQUAL(ta.nValue, 66442954); // pre-optimization: 66464593
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK_EQUAL(pool.blockCommissionA, 2000000);
        BOOST_CHECK_EQUAL(pool.reserveA, 298000000);
        BOOST_CHECK_EQUAL(pool.reserveB, 33557046); // pre-optimization: 33535407

    }

    {
//        printf("2 COIN (1:1000)\n");
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(COIN, 1000*COIN, {}, [](const CScript &, CAmount liq)-> Res {
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, 2*COIN}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK_EQUAL(ta.nValue, 66442953021); // pre-optimization: 66465256146
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK_EQUAL(pool.blockCommissionA, 2000000);
        BOOST_CHECK_EQUAL(pool.reserveA, 298000000);
        BOOST_CHECK_EQUAL(pool.reserveB, 33557046979); // pre-optimization: 33534743854
    }
    {
//        printf("1 COIN (1:1000)\n");
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(COIN, 1000*COIN, {}, [](const CScript &, CAmount liq)-> Res {
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, COIN}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK_EQUAL(ta.nValue, 49748743719); // pre-optimization: 49773755285
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK_EQUAL(pool.blockCommissionA, 1000000);
        BOOST_CHECK_EQUAL(pool.reserveA, 199000000);
        BOOST_CHECK_EQUAL(pool.reserveB, 50251256281); // pre-optimization: 50226244715
    }
    {
//        printf("COIN/1000 (1:1000) (no slope due to commission)\n");
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(COIN, 1000*COIN, {}, [](const CScript &, CAmount liq)-> Res {
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, COIN/1000}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK_EQUAL(ta.nValue, 98902087); // pre-optimization: 99000000
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK_EQUAL(pool.blockCommissionA, 1000);
        BOOST_CHECK_EQUAL(pool.reserveA, 100099000);
        BOOST_CHECK_EQUAL(pool.reserveB, 99901097913); // pre-optimization: 99901000000

       printf("comissionA = %ld\n", pool.blockCommissionA);
       printf("reserveA = %ld\n", pool.reserveA);
       printf("reserveB = %ld\n", pool.reserveB);
    }
}

void SetPoolRewardPct(CCustomCSView & mnview, DCT_ID idPool, CAmount pct)
{
    auto optPool = mnview.GetPoolPair(idPool);
    BOOST_REQUIRE(optPool);
    optPool->rewardPct = pct;
    mnview.SetPoolPair(idPool, *optPool);
}

void SetPoolTradeFees(CCustomCSView & mnview, DCT_ID idPool, CAmount A, CAmount B)
{
    auto optPool = mnview.GetPoolPair(idPool);
    BOOST_REQUIRE(optPool);
    optPool->blockCommissionA = A;
    optPool->blockCommissionB = B;
    optPool->swapEvent = true;
    mnview.SetPoolPair(idPool, *optPool);
}

BOOST_AUTO_TEST_CASE(math_rewards)
{
    const int PoolCount = 10; // less than DCT_ID_START!
    const int ProvidersCount = 10000;

    CCustomCSView mnview(*pcustomcsview);

    // create pools
    for (int i = 0; i < PoolCount; ++i) {
        DCT_ID idA, idB, idPool;
        std::tie(idA, idB, idPool) = CreatePoolNTokens(mnview, "A"+std::to_string(i), "B"+std::to_string(i));
        auto optPool = mnview.GetPoolPair(idPool);
        BOOST_REQUIRE(optPool);

    }
    // create shares
    mnview.ForEachPoolPair([&] (DCT_ID const & idPool, CPoolPair const & pool) {
//            printf("pool id = %s\n", idPool.ToString().c_str());
        for (int i = 0; i < ProvidersCount; ++i) {
            CScript shareAddress = CScript(idPool.v * ProvidersCount + i);
            Res res = AddPoolLiquidity(mnview, idPool, idPool.v*COIN, idPool.v*COIN, shareAddress);
            BOOST_CHECK(res.ok);
        }
        return true;
    });

    {
        CCustomCSView cache(mnview);

        // set pool rewards rates
        const DCT_ID RWD50 = DCT_ID{1};
        const DCT_ID RWD25 = DCT_ID{2};
        SetPoolRewardPct(cache, RWD50, COIN/2); // 50%
        SetPoolRewardPct(cache, RWD25, COIN/4); // 25%

        SetPoolRewardPct(cache, DCT_ID{3}, COIN/10); // 10%
        SetPoolRewardPct(cache, DCT_ID{4}, COIN/10); // 10%
        SetPoolRewardPct(cache, DCT_ID{5}, COIN/100);
        SetPoolRewardPct(cache, DCT_ID{6}, COIN/100);
        SetPoolRewardPct(cache, DCT_ID{7}, COIN/100);
        SetPoolRewardPct(cache, DCT_ID{8}, COIN/100);
        SetPoolRewardPct(cache, DCT_ID{9}, COIN/100);
        /// DCT_ID{10} - 0

        // set "traded fees" here too, just to estimate proc.load
        cache.ForEachPoolPair([&] (DCT_ID const & idPool, CPoolPair const & pool) {
            SetPoolTradeFees(cache, idPool, idPool.v * COIN, idPool.v * COIN*2);
            return true;
        });

        // distribute 100 coins
        CAmount totalRwd = 100*COIN;

        int64_t nTimeBegin = GetTimeMicros();
        CAmount distributed = cache.DistributeRewards(totalRwd,
            [&cache] (CScript const & owner, DCT_ID tokenID) {
                return cache.GetBalance(owner, tokenID);
            },
            [&cache] (CScript const & to, CTokenAmount amount) {
                return cache.AddBalance(to, amount);
            }
        );
        int64_t nTimeEnd = GetTimeMicros(); auto nTimeRwd = nTimeEnd - nTimeBegin;
        printf("Rewarded %d pools with %d shares each: %.2fms \n", PoolCount, ProvidersCount, 0.001 * (nTimeRwd));

        printf("Distributed: = %ld\n", distributed);
        BOOST_CHECK(distributed == 9999000000); // always slightly less due to MINIMUM_LIQUIDITY & rounding

        // check it
        auto rwd25 = 25 * COIN / ProvidersCount;
        auto rwd50 = 50 * COIN / ProvidersCount;
        cache.ForEachPoolShare([&] (DCT_ID const & id, CScript const & owner) {
//            if (id == RWD25/* || id == RWD50*/)
//                printf("owner = %s: %s,\n", owner.GetHex().c_str(), cache.GetBalance(owner, DCT_ID{0}).ToString().c_str());

            // check only first couple of pools and the last (zero)
            if (id == RWD25 && owner != CScript(id.v * ProvidersCount)) { // first got slightly less due to MINIMUM_LIQUIDITY
                CAmount rwd = cache.GetBalance(owner, DCT_ID{0}).nValue;
                BOOST_CHECK(rwd == rwd25);
            }
            if (id == RWD50 && owner != CScript(id.v * ProvidersCount)) { // first got slightly less due to MINIMUM_LIQUIDITY
                CAmount rwd = cache.GetBalance(owner, DCT_ID{0}).nValue;
                BOOST_CHECK(rwd == rwd50);
            }
            if (id == DCT_ID{10} ) { // first got slightly less due to MINIMUM_LIQUIDITY
                CAmount rwd = cache.GetBalance(owner, DCT_ID{0}).nValue;
                BOOST_CHECK(rwd == 0);
            }
            return true;
        });

        // check trade comissions for one of pools
        {
            DCT_ID idPool = DCT_ID{1};
            auto optPool = cache.GetPoolPair(idPool);
            cache.ForEachPoolShare([&] (DCT_ID const & id, CScript const & owner) {
                if (id != idPool)
                    return false;

                if (owner != CScript(id.v * ProvidersCount)) { // first got slightly less due to MINIMUM_LIQUIDITY
                    CAmount rwdA = cache.GetBalance(owner, DCT_ID{optPool->idTokenA}).nValue;
                    CAmount rwdB = cache.GetBalance(owner, DCT_ID{optPool->idTokenB}).nValue;
                    BOOST_CHECK(rwdA == id.v * COIN / ProvidersCount);
                    BOOST_CHECK(rwdB == id.v * COIN * 2 / ProvidersCount);
                }
                return true;
            }, PoolShareKey{idPool, {}});
        }
    }
}


BOOST_AUTO_TEST_SUITE_END()
