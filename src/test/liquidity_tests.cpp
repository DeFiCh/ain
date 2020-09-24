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

    auto res = mnview.CreateToken(token);
    if (!res.ok) printf("%s\n", res.msg.c_str());
    BOOST_REQUIRE(res.ok);
    return *res.val;
}


// tokenz A,B,LP
std::tuple<DCT_ID, DCT_ID, DCT_ID> CreatePoolNTokens(CCustomCSView &mnview, std::string const & symbolA, std::string const & symbolB)
{
    DCT_ID idA = CreateToken(mnview, symbolA);
    DCT_ID idB = CreateToken(mnview, symbolB);

    DCT_ID idPool = CreateToken(mnview, symbolA + "-" +  symbolB, (uint8_t)CToken::TokenFlags::Default | (uint8_t)CToken::TokenFlags::LPS);
    {
        CPoolPair pool{};
        pool.idTokenA = idA;
        pool.idTokenB = idB;
        pool.commission = 01000000;
    //    poolMsg.ownerAddress = CScript();
        pool.status = true;
        BOOST_REQUIRE(mnview.SetPoolPair(idPool, pool).ok);
    }
    return { idA, idB, idPool};
}

Res AddPoolLiquidity(CCustomCSView &mnview, DCT_ID idPool, CPoolPair & pool, CAmount amountA, CAmount amountB, CScript const & shareAddress)
{
    const auto res = pool.AddLiquidity(amountA, amountB, shareAddress, [&] /*onMint*/(CScript const & to, CAmount liqAmount) -> Res {
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
    return res;
}

BOOST_FIXTURE_TEST_SUITE(liquidity_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(math)
{
    auto FAILFUN = [](const CScript &, CAmount)-> Res { BOOST_REQUIRE(false); return Res::Err("it shold not happen"); };

    CCustomCSView mnview(*pcustomcsview);

    DCT_ID idA, idB, idPool;
    std::tie(idA, idB, idPool) = CreatePoolNTokens(mnview, "AAA", "BBB");
    auto optPool = mnview.GetPoolPair(idPool);
    BOOST_REQUIRE(optPool);

    CScript lpOwner = CScript(3);
    Res res{};
    { // basic fails
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(-1, 1000, {}, FAILFUN);
        BOOST_CHECK(!res.ok && res.msg == "amounts should be positive");
        res = pool.AddLiquidity(0, 1000, {}, FAILFUN);
        BOOST_CHECK(!res.ok && res.msg == "amounts should be positive");
        res = pool.AddLiquidity(1, 1000, {}, FAILFUN);
        BOOST_CHECK(!res.ok && res.msg == "liquidity too low");
        res = pool.AddLiquidity(10, 100000, {}, FAILFUN);
        BOOST_CHECK(!res.ok && res.msg == "liquidity too low"); // median == MINIMUM_LIQUIDITY
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
        res = pool.AddLiquidity(1, 1, {}, FAILFUN);
        BOOST_CHECK(!res.ok && res.msg == "amounts too low, zero liquidity");

        // we should place smth significant, that give us at least 1 liq point ("X * totalLiquidity >= reserve")
        // plus 3037000500+1
        res = pool.AddLiquidity(3037000500+1, 1, {}, FAILFUN);
        BOOST_CHECK(!res.ok && res.msg == "overflow when adding to reserves"); // in fact we got min liquidity == 1, but reserves overflowed

        // thats all, we can't place anything here until removing or trading

//        res = pool.Swap(CTokenAmount{pool.idTokenB, 1}, PoolPrice{std::numeric_limits<CAmount>::max(), 0}, [] (CTokenAmount const &) -> Res{ return {}; } );
//        if (!res.ok) printf("Err: %s\n", res.msg.c_str());
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

        // thats all, we can't place anything here until removing;  even trading can't help
    }

    {
        CPoolPair pool = *optPool;
        res = pool.AddLiquidity(11, 100000, {}, [](const CScript &, CAmount liq)-> Res {
            BOOST_CHECK(liq == 48); // sqrt (11*100000) - MINIMUM_LIQUIDITY
            return Res::Ok();
        });
        BOOST_CHECK(res.ok);
        BOOST_CHECK(pool.reserveA == 11);
        BOOST_CHECK(pool.reserveB == 100000);
    }


    //            printf("liq = %ld\n", liq);
    if (!res.ok) printf("Err: %s\n", res.msg.c_str());
    //        BOOST_CHECK((mnview.GetBalance(lpOwner, idPool).nValue == 48)); // 1048 - MINIMUM_LIQUIDITY


//    const auto res = pool.AddLiquidity(amountA, amountB, shareAddress, [&] /*onMint*/(CScript to, CAmount liqAmount) {
//        BOOST_CHECK(liqAmount > 0);
//        BOOST_CHECK((mnview.GetBalance(lpOwner, idPool).nValue == 48)); // 1048 - MINIMUM_LIQUIDITY

//        auto resTotal = SafeAdd(pool.totalLiquidity, liqAmount);
//        if (!resTotal.ok) {
//            return resTotal;
//        }
//        pool.totalLiquidity = *resTotal.val;

//        auto add = mnview.AddBalance(to, { lpTokenID, liqAmount });
//        if (!add.ok) {
//            return Res::Err("%s: %s", base, add.msg);
//        }

        //insert update ByShare index
//        const auto setShare = mnview.SetShare(lpTokenID, to);
//        if (!setShare.ok) {
//            return Res::Err("%s: %s", base, setShare.msg);
//        }

//        return Res::Ok();
//    });




//    CScript owner1 = CScript(1);
//    CScript owner2 = CScript(2);
//    pcustomcsview->SetBalance(owner1, CTokenAmount{idA, MAX_MONEY});
//    pcustomcsview->SetBalance(owner1, CTokenAmount{idB, MAX_MONEY});

}


BOOST_AUTO_TEST_SUITE_END()
