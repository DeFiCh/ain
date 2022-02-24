#include <runtime.h>
#include <boost/test/unit_test.hpp>

#include <util/time.h>
#include <amount.h>

BOOST_AUTO_TEST_CASE(libain_dex)
{
    auto path = std::string(std::getenv("WASM_PATH")) + "/dex.wasm";
    auto registered = ainrt_register_dex_module(path.c_str());
    BOOST_CHECK_NE(registered, 0);

    DctId gold = 1;
    DctId silver = 2;

    PoolPair pool_pair = { gold, silver, static_cast<DctId>(0.1 * COIN), 200 * COIN, 1000 * COIN, 100000 * COIN, 0, 0 };
    TokenAmount token_in = { silver, 100 * COIN };
    PoolPrice max_price = { 100 * COIN, 0 };
    auto ret = ainrt_call_dex_swap(&pool_pair, &token_in, &max_price, true);
    BOOST_CHECK_NE(ret, 0);
}
