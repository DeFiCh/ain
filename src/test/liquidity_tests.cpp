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

struct TokenSettings {
    std::string symbol;
    uint8_t decimal = 8;
    CAmount limit = MAX_MONEY;
    uint8_t flags = uint8_t(CToken::TokenFlags::Default);
    TokenSettings(const char* symbol) : symbol(symbol) {}
    TokenSettings(const std::string& symbol) : symbol(symbol) {}
    TokenSettings(const std::string& symbol, uint8_t flags) : symbol(symbol), flags(flags) {}
};

DCT_ID CreateToken(CCustomCSView &mnview, const TokenSettings& settings)
{
    CTokenImplementation token;
    token.creationTx = NextTx();
    token.symbol = settings.symbol;
    token.flags = settings.flags;
    token.decimal = settings.decimal;
    token.limit = settings.limit;

    auto res = mnview.CreateToken(token, false);
    if (!res.ok) printf("%s\n", res.msg.c_str());
    BOOST_REQUIRE(res.ok);
    return *res.val;
}

static auto FAIL_onMint = [](const CScript &, CAmount)-> Res { BOOST_REQUIRE(false); return Res::Err("it should not happen"); };
static auto FAIL_onSwap = [](const CTokenAmount &)-> Res { BOOST_REQUIRE(false); return Res::Err("it should not happen"); };
static auto OK_onMint = [](const CScript &, CAmount)-> Res { return Res::Ok(); };
static auto OK_onSwap = [](const CTokenAmount &)-> Res { return Res::Ok(); };

// tokenz A,B,LP
std::tuple<DCT_ID, DCT_ID, DCT_ID> CreatePoolNTokens(CCustomCSView &mnview, const TokenSettings& settingsA, const TokenSettings& settingsB)
{
    DCT_ID idA = CreateToken(mnview, settingsA);
    DCT_ID idB = CreateToken(mnview, settingsB);

    auto settings = settingsA;
    settings.symbol = settingsA.symbol + "-" + settingsB.symbol;
    settings.limit = std::max(settingsA.limit, settingsB.limit);
    settings.decimal = std::max(settingsA.decimal, settingsB.decimal);
    settings.flags = uint8_t(CToken::TokenFlags::Default) | uint8_t(CToken::TokenFlags::DAT) | uint8_t(CToken::TokenFlags::LPS);

    DCT_ID idPool = CreateToken(mnview, settings);
    {
        CPoolPair pool{};
        pool.idTokenA = idA;
        pool.idTokenB = idB;
        pool.commission = 1000000; // 1%
    //    poolMsg.ownerAddress = CScript();
        pool.status = true;
        BOOST_REQUIRE(mnview.SetPoolPair(idPool, pool).ok);
    }
    return std::make_tuple(idA, idB, idPool); // ! simple initialization list (as "{a,b,c}")  doesn't work here under ubuntu 16.04 - due to older gcc?
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
        res = pool.AddLiquidity(MAX_MONEY, 1, {}, [](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == 346409161); // == sqrt(limit)-MINIMUM_LIQUIDITY
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == MAX_MONEY);
        BOOST_CHECK(pool.reserveB == 1);
        BOOST_CHECK(pool.totalLiquidity == 346409161 + CPoolPair::MINIMUM_LIQUIDITY);

        // plus 1
        res = pool.AddLiquidity(1, 1, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "amounts too low, zero liquidity");

        // we should place smth significant, that give us at least 1 liq point ("X * totalLiquidity >= reserve")
        // plus 3037000500+1
        res = pool.AddLiquidity(std::numeric_limits<CAmount>::max(), 1, {}, FAIL_onMint);
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
        res = pool.AddLiquidity(MAX_MONEY, MAX_MONEY, {}, [](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == MAX_MONEY - CPoolPair::MINIMUM_LIQUIDITY);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == MAX_MONEY);
        BOOST_CHECK(pool.reserveB == MAX_MONEY);
        BOOST_CHECK(pool.totalLiquidity == MAX_MONEY);

        // thats all, we can't do anything here until removing
    }

    // trying to swap moooore than reserved on low reserves (sliding)
    // it works extremely bad on low reserves:
    {
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(1001, 1001, {}, [](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == 1001 - CPoolPair::MINIMUM_LIQUIDITY);
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, 1000000}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK(ta.nValue == 1000);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.blockCommissionA == 10000);
        BOOST_CHECK(pool.reserveA == 991001);
        BOOST_CHECK(pool.reserveB == 1);
    }

    // trying to swap moooore than reserved (sliding), but on "resonable" reserves
    {
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(COIN, COIN, {}, [](const CScript &, CAmount liq)-> Res {
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, 2*COIN}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK(ta.nValue == 66442954);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.blockCommissionA == 2000000);
        BOOST_CHECK(pool.reserveA == 298000000);
        BOOST_CHECK(pool.reserveB == 33557046);
    }

    {
//        printf("2 COIN (1:1000)\n");
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(COIN, 1000*COIN, {}, [](const CScript &, CAmount liq)-> Res {
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, 2*COIN}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK(ta.nValue == 66442953021);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.blockCommissionA == 2000000);
        BOOST_CHECK(pool.reserveA == 298000000);
        BOOST_CHECK(pool.reserveB == 33557046979);
    }
    {
//        printf("1 COIN (1:1000)\n");
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(COIN, 1000*COIN, {}, [](const CScript &, CAmount liq)-> Res {
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, COIN}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK(ta.nValue == 49748743719);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.blockCommissionA == 1000000);
        BOOST_CHECK(pool.reserveA == 199000000);
        BOOST_CHECK(pool.reserveB == 50251256281);
    }
    {
//        printf("COIN/1000 (1:1000) (no slope due to commission)\n");
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(COIN, 1000*COIN, {}, [](const CScript &, CAmount liq)-> Res {
            return Res::Ok();
        });
        res = pool.Swap(CTokenAmount{pool.idTokenA, COIN/1000}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK(ta.nValue == 98902087);
//              printf("ta = %ld\n", ta.nValue);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.blockCommissionA == 1000);
        BOOST_CHECK(pool.reserveA == 100099000);
        BOOST_CHECK(pool.reserveB == 99901097913);

//        printf("comissionA = %ld\n", pool.blockCommissionA);
//        printf("reserveA = %ld\n", pool.reserveA);
//        printf("reserveB = %ld\n", pool.reserveB);
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
        BOOST_CHECK(distributed == 9999999900); // always slightly less due to MINIMUM_LIQUIDITY & rounding

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

BOOST_AUTO_TEST_CASE(math_liquidity_token_decimal)
{
    CCustomCSView mnview(*pcustomcsview);

    DCT_ID idA, idB, idPool;
    TokenSettings sa("AAA"), sb("BBB");
    sa.decimal = 4;
    CAmount coinA = std::pow(10, sa.decimal);
    sa.limit = MAX_MONEY / COIN * coinA;
    sb.decimal = 7;
    CAmount coinB = std::pow(10, sb.decimal);
    sb.limit = MAX_MONEY / COIN * coinB;
    std::tie(idA, idB, idPool) = CreatePoolNTokens(mnview, sa, sb);
    auto optPool = mnview.GetPoolPair(idPool);
    BOOST_REQUIRE(optPool);

    Res res{};

    // calc min liquidity base on max token decimal
    const auto minimum_liq = CPoolPair::MINIMUM_LIQUIDITY * COIN / coinB;
    const CAmount coinDiff = std::pow(10, std::abs(int(sa.decimal) - int(sb.decimal)));

    { // check tokens
        CPoolPair pool = *optPool;
        BOOST_CHECK(pool.tokenA.symbol == "AAA");
        BOOST_CHECK(pool.tokenA.decimal == 4);
        BOOST_CHECK(pool.tokenA.limit == MAX_MONEY / COIN * coinA);
        BOOST_CHECK(pool.tokenB.symbol == "BBB");
        BOOST_CHECK(pool.tokenB.decimal == 7);
        BOOST_CHECK(pool.tokenB.limit == MAX_MONEY / COIN * coinB);
    }

    { // fail exactly on minimum liquidity
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(minimum_liq/coinDiff, minimum_liq, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "liquidity too low");
        res = pool.AddLiquidity(minimum_liq, minimum_liq/coinDiff, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "liquidity too low");
    }

    { // add liquidity
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(minimum_liq, minimum_liq, {}, [&](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == CAmount(minimum_liq*sqrt(coinDiff) - minimum_liq));
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == minimum_liq);
        BOOST_CHECK(pool.reserveB == minimum_liq);
        BOOST_CHECK(pool.totalLiquidity == CAmount(minimum_liq*sqrt(coinDiff)));

        CAmount a = minimum_liq*10, b = minimum_liq*100;
        CAmount liqA = (arith_uint256(a) * pool.totalLiquidity / pool.reserveA).GetLow64();
        CAmount liqB = (arith_uint256(b) * pool.totalLiquidity / pool.reserveB).GetLow64();
        auto liqMin = std::min(liqA, liqB);
        auto totalLiquidity = liqMin + pool.totalLiquidity;

        res = pool.AddLiquidity(a, b, {}, [&](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == liqMin);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == minimum_liq + a);
        BOOST_CHECK(pool.reserveB == minimum_liq + b);
        BOOST_CHECK(pool.totalLiquidity == totalLiquidity);

        // swap
        res = pool.Swap(CTokenAmount{idA, minimum_liq}, PoolPrice{0, 0}, FAIL_onSwap);
        BOOST_CHECK(!res.ok && res.msg == "Lack of liquidity.");

        // add liquidity for swap
        auto liq_minA = CPoolPair::SLOPE_SWAP_RATE * COIN / coinA;
        auto liq_minB = CPoolPair::SLOPE_SWAP_RATE * COIN / coinB;

        auto resA = pool.reserveA + liq_minA;
        auto resB = pool.reserveB + liq_minB;

        res = pool.AddLiquidity(liq_minA, liq_minB, {}, OK_onMint);
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == resA);
        BOOST_CHECK(pool.reserveB == resB);

        res = pool.Swap(CTokenAmount{idA, 10000}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [&] (CTokenAmount const &ta) -> Res{
            BOOST_CHECK(ta.nValue == 998);
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.blockCommissionA == 100);
        BOOST_CHECK(pool.reserveA == 10119900);
        BOOST_CHECK(pool.reserveB == 1019002);

        // limits
        auto limitA = sa.limit - pool.reserveA;
        auto limitB = sb.limit - pool.reserveB;

        res = pool.AddLiquidity(limitA + 1, 10000, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "money out of range");

        res = pool.AddLiquidity(10000, limitB + 1, {}, FAIL_onMint);
        BOOST_CHECK(!res.ok && res.msg == "money out of range");
    }
}

BOOST_AUTO_TEST_SUITE_END()
