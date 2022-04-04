#include <chainparams.h>
#include <masternodes/masternodes.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(height_filter_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(height_interval_single_pool_pair)
{
    CCustomCSView mnview(*pcustomcsview);
    CFixedIntervalPrice fixedIntervalPrice{};
    fixedIntervalPrice.priceFeedId = {"TSLA", "USD"};
    fixedIntervalPrice.priceRecord[1] = 3*COIN;
    fixedIntervalPrice.priceRecord[0] = 3*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,1));
    auto storedFixedIntervalPrice = mnview.GetFixedIntervalPrice({{"TSLA", "USD"}, 1});
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[0], fixedIntervalPrice.priceRecord[0]);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[1], fixedIntervalPrice.priceRecord[1]);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.first, fixedIntervalPrice.priceFeedId.first);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.second, fixedIntervalPrice.priceFeedId.second);
    
    fixedIntervalPrice.priceRecord[1] = 4*COIN;
    fixedIntervalPrice.priceRecord[0] = 4*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,2));
    storedFixedIntervalPrice = mnview.GetFixedIntervalPrice({{"TSLA", "USD"}, 2});
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[0], fixedIntervalPrice.priceRecord[0]);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[1], fixedIntervalPrice.priceRecord[1]);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.first, fixedIntervalPrice.priceFeedId.first);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.second, fixedIntervalPrice.priceFeedId.second);

    fixedIntervalPrice.priceRecord[1] = 2*COIN;
    fixedIntervalPrice.priceRecord[0] = 2*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,3));
    storedFixedIntervalPrice = mnview.GetFixedIntervalPrice({{"TSLA", "USD"}, 3});
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[0], fixedIntervalPrice.priceRecord[0]);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[1], fixedIntervalPrice.priceRecord[1]);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.first, fixedIntervalPrice.priceFeedId.first);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.second, fixedIntervalPrice.priceFeedId.second);

    fixedIntervalPrice.priceRecord[1] = 5*COIN;
    fixedIntervalPrice.priceRecord[0] = 5*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,4));
    // Test recently stored price interval
    storedFixedIntervalPrice = mnview.GetFixedIntervalPrice({{"TSLA", "USD"}, 0});
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[0], fixedIntervalPrice.priceRecord[0]);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[1], fixedIntervalPrice.priceRecord[1]);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.first, fixedIntervalPrice.priceFeedId.first);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.second, fixedIntervalPrice.priceFeedId.second);

    // Test price at previous height
    storedFixedIntervalPrice = mnview.GetFixedIntervalPrice({{"TSLA", "USD"}, 2});
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[0], 4*COIN);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceRecord[1], 4*COIN);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.first, fixedIntervalPrice.priceFeedId.first);
    BOOST_CHECK_EQUAL(storedFixedIntervalPrice.val->priceFeedId.second, fixedIntervalPrice.priceFeedId.second);


}

BOOST_AUTO_TEST_CASE(height_interval_multi_pool_pair)
{

    std::vector<CFixedIntervalPrice> pricesAtHeight1;

    CCustomCSView mnview(*pcustomcsview);
    CFixedIntervalPrice fixedIntervalPrice{};
    fixedIntervalPrice.priceFeedId = {"BTC", "USD"};
    fixedIntervalPrice.priceRecord[1] = 10*COIN;
    fixedIntervalPrice.priceRecord[0] = 10*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,1));
    pricesAtHeight1.push_back(fixedIntervalPrice);

    fixedIntervalPrice.priceFeedId = {"NFT", "USD"};
    fixedIntervalPrice.priceRecord[1] = 2*COIN;
    fixedIntervalPrice.priceRecord[0] = 2*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,1));
    pricesAtHeight1.push_back(fixedIntervalPrice);

    fixedIntervalPrice.priceFeedId = {"TSLA", "USD"};
    fixedIntervalPrice.priceRecord[1] = 3*COIN;
    fixedIntervalPrice.priceRecord[0] = 3*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,1));
    pricesAtHeight1.push_back(fixedIntervalPrice);

    

    std::vector<CFixedIntervalPrice> pricesAtHeight2;

    fixedIntervalPrice.priceFeedId = {"BTC", "USD"};
    fixedIntervalPrice.priceRecord[1] = 20*COIN;
    fixedIntervalPrice.priceRecord[0] = 20*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,2));
    pricesAtHeight2.push_back(fixedIntervalPrice);

    fixedIntervalPrice.priceFeedId = {"NFT", "USD"};
    fixedIntervalPrice.priceRecord[1] = 4*COIN;
    fixedIntervalPrice.priceRecord[0] = 4*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,2));
    pricesAtHeight2.push_back(fixedIntervalPrice);

    fixedIntervalPrice.priceFeedId = {"TSLA", "USD"};
    fixedIntervalPrice.priceRecord[1] = 6*COIN;
    fixedIntervalPrice.priceRecord[0] = 6*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,2));
    pricesAtHeight2.push_back(fixedIntervalPrice);


    // Test price at block height 1
    std::vector<CFixedIntervalPrice> testPricesAtHeight1;

    size_t limit = 100;
    mnview.ForEachFixedIntervalPrice([&](const CTokenCurrencyPair&, CFixedIntervalPrice fixedIntervalPrice){
        BOOST_TEST_MESSAGE( "Pair " << fixedIntervalPrice.priceFeedId.first << "-" << fixedIntervalPrice.priceFeedId.second << " Price: " << (fixedIntervalPrice.priceRecord[0]/COIN) << "-" << (fixedIntervalPrice.priceRecord[1]/COIN) );
        testPricesAtHeight1.push_back(fixedIntervalPrice);
        limit--;
        return limit != 0;
    }, {}, 1);

    BOOST_CHECK_EQUAL(testPricesAtHeight1.size(), 3);
    for(int i = 0; i < 3; i++) {
        CFixedIntervalPrice storedFixedIntervalPrice = testPricesAtHeight1[i];
        CFixedIntervalPrice fixedIntervalPrice = pricesAtHeight1[i];
        BOOST_CHECK_EQUAL(storedFixedIntervalPrice.priceRecord[0], fixedIntervalPrice.priceRecord[0]);
        BOOST_CHECK_EQUAL(storedFixedIntervalPrice.priceRecord[1], fixedIntervalPrice.priceRecord[1]);
        BOOST_CHECK_EQUAL(storedFixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.first);
        BOOST_CHECK_EQUAL(storedFixedIntervalPrice.priceFeedId.second, fixedIntervalPrice.priceFeedId.second);

    }

    // Test price at block height 2
    limit = 100;
    std::vector<CFixedIntervalPrice> testPricesAtHeight2;
    mnview.ForEachFixedIntervalPrice([&](const CTokenCurrencyPair&, CFixedIntervalPrice fixedIntervalPrice){
        BOOST_TEST_MESSAGE( "Pair " << fixedIntervalPrice.priceFeedId.first << "-" << fixedIntervalPrice.priceFeedId.second << " Price: " << (fixedIntervalPrice.priceRecord[0]/COIN) << "-" << (fixedIntervalPrice.priceRecord[1]/COIN) );
        testPricesAtHeight2.push_back(fixedIntervalPrice);
        limit--;
        return limit != 0;
    }, {}, 2);

    BOOST_CHECK_EQUAL(testPricesAtHeight2.size(), 3);
    for(int i = 0; i < 3; i++) {
        CFixedIntervalPrice storedFixedIntervalPrice = testPricesAtHeight2[i];
        CFixedIntervalPrice fixedIntervalPrice = pricesAtHeight2[i];
        BOOST_CHECK_EQUAL(storedFixedIntervalPrice.priceRecord[0], fixedIntervalPrice.priceRecord[0]);
        BOOST_CHECK_EQUAL(storedFixedIntervalPrice.priceRecord[1], fixedIntervalPrice.priceRecord[1]);
        BOOST_CHECK_EQUAL(storedFixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.first);
        BOOST_CHECK_EQUAL(storedFixedIntervalPrice.priceFeedId.second, fixedIntervalPrice.priceFeedId.second);

    }


}

BOOST_AUTO_TEST_SUITE_END()
