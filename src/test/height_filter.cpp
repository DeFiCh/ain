#include <chainparams.h>
#include <masternodes/loan.h>
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

DCT_ID CreateToken(CCustomCSView &mnview, const std::string& symbol, const std::string& name)
{
    CTokenImplementation token;
    token.flags = (uint8_t)CToken::TokenFlags::Default;
    token.flags |= (uint8_t)CToken::TokenFlags::LoanToken | (uint8_t)CToken::TokenFlags::DAT;

    token.creationTx = NextTx();
    token.symbol = symbol;
    token.name = name;

    auto res = mnview.CreateToken(token, false);
    BOOST_REQUIRE(res.ok);
    return *res.val;
}

BOOST_AUTO_TEST_CASE(height_interval)
{
    CCustomCSView mnview(*pcustomcsview);

    const std::string id("sch1");
    CreateScheme(mnview, id, 150, 2 * COIN);

    COracle oracle;
    oracle.weightage = 1;
    oracle.availablePairs = {
        {"DFI", "USD"},
        {"BTC", "USD"},
        {"TSLA", "USD"},
        {"NFT", "USD"},
    };
    oracle.tokenPrices = {
        {"DFI", {{"USD", {5 * COIN, 0}}}},
        {"BTC", {{"USD", {10 * COIN, 0}}}},
        {"TSLA", {{"USD", {3 * COIN, 0}}}},
        {"NFT", {{"USD", {2 * COIN, 0}}}},
    };
    auto oracle_id = NextTx();
    mnview.AppointOracle(oracle_id, oracle);

    auto dfi_id = DCT_ID{0};
    auto tesla_id = CreateLoanToken(mnview, "TSLA", "TESLA", "TSLA/USD", 5 * COIN);
    CFixedIntervalPrice fixedIntervalPrice{};
    fixedIntervalPrice.priceFeedId = {"TSLA", "USD"};
    fixedIntervalPrice.priceRecord[1] = 3*COIN;
    fixedIntervalPrice.priceRecord[0] = 3*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice,1));
    auto nft_id = CreateLoanToken(mnview, "NFT", "NFT", "NFT/USD", 2 * COIN);
    fixedIntervalPrice.priceFeedId = {"NFT", "USD"};
    fixedIntervalPrice.priceRecord[1] = 2*COIN;
    fixedIntervalPrice.priceRecord[0] = 2*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice, 1));
    auto btc_id = CreateToken(mnview, "BTC", "BITCOIN");
    CreateCollateralToken(mnview, dfi_id, "DFI/USD");
    fixedIntervalPrice.priceFeedId = {"DFI", "USD"};
    fixedIntervalPrice.priceRecord[1] = 5*COIN;
    fixedIntervalPrice.priceRecord[0] = 5*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice, 1));
    CreateCollateralToken(mnview, btc_id, "BTC/USD");
    fixedIntervalPrice.priceFeedId = {"BTC", "USD"};
    fixedIntervalPrice.priceRecord[1] = 10*COIN;
    fixedIntervalPrice.priceRecord[0] = 10*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice, 1));
}