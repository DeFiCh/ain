#include <chainparams.h>
#include <masternodes/masternodes.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(height_interval)
{
    CCustomCSView mnview(*pcustomcsview);
    CFixedIntervalPrice fixedIntervalPrice{};
    fixedIntervalPrice.priceFeedId = {"TSLA", "USD"};
    fixedIntervalPrice.priceRecord[1] = 3*COIN;
    fixedIntervalPrice.priceRecord[0] = 3*COIN;
}