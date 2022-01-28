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

DCT_ID CreateLoanToken(CCustomCSView &mnview, const std::string& symbol, const std::string& name, const std::string& priceFeed, CAmount interest)
{
    CLoanSetLoanTokenImplementation loanToken;
    loanToken.interest = interest;
    loanToken.symbol = symbol;
    loanToken.name = name;
    if (!priceFeed.empty()) {
        auto delim = priceFeed.find('/');
        loanToken.fixedIntervalPriceId = std::make_pair(priceFeed.substr(0, delim), priceFeed.substr(delim + 1));
    }
    loanToken.creationTx = NextTx();
    auto id = CreateToken(mnview, symbol, name);
    mnview.SetLoanToken(loanToken, id);
    return id;
}

void CreateCollateralToken(CCustomCSView &mnview, DCT_ID id, const std::string& priceFeed)
{
    CLoanSetCollateralTokenImplementation collateralToken;
    collateralToken.idToken = id;
    collateralToken.factor = COIN;
    collateralToken.creationHeight = 0;
    collateralToken.creationTx = NextTx();
    if (!priceFeed.empty()) {
        auto delim = priceFeed.find('/');
        collateralToken.fixedIntervalPriceId = std::make_pair(priceFeed.substr(0, delim), priceFeed.substr(delim + 1));
    }
    mnview.CreateLoanCollateralToken(collateralToken);
}

void CreateScheme(CCustomCSView &mnview, const std::string& name, uint32_t ratio, CAmount rate)
{
    CLoanSchemeMessage msg;
    msg.ratio = ratio;
    msg.rate = rate;
    msg.identifier = name;
    mnview.StoreLoanScheme(msg);
}

extern std::vector<CAuctionBatch> CollectAuctionBatches(const CCollateralLoans& collLoan, const TAmounts& collBalances, const TAmounts& loanBalances);

BOOST_FIXTURE_TEST_SUITE(loan_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(high_precision_interest_rate_tests)
{
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(0)), "0.000000000000000000000000");
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(1)), "0.000000000000000000000001");
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(1)), "0.000000000000000000000001");
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(42058)), "0.000000000000000000042058");
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(95129375)), "0.000000000000000095129375");
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(117009132)), "0.000000000000000117009132");
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(11700913242)), "0.000000000000011700913242");

    base_uint<128> num;
    num.SetHex("21012F95D4094B33"); // 2378234398782343987
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(num)), "0.000002378234398782343987");
    num.SetHex("3CDC4CA64879921C03BF061156E455BC"); // 80897539693407360060932882613242451388
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(num)), "80897539693407.360060932882613242451388");
    num.SetHex("10E5FBB8CA9E273D0B0353C23D90A6"); // 87741364994776235347880977943597222
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(num)), "87741364994.776235347880977943597222");
    num.SetHex("2D5C78FF9C3FE70F9F0B0C7"); // 877413626032608048611111111
    BOOST_CHECK_EQUAL(GetInterestPerBlockHighPrecisionString(base_uint<128>(num)), "877.413626032608048611111111");
}

BOOST_AUTO_TEST_CASE(loan_iterest_rate)
{
    CCustomCSView mnview(*pcustomcsview);

    const std::string id("sch1");
    CreateScheme(mnview, id, 150, 2 * COIN);

    const CAmount tokenInterest = 5 * COIN;
    auto token_id = CreateLoanToken(mnview, "TST", "TEST", "", tokenInterest);
    auto scheme = mnview.GetLoanScheme(id);
    BOOST_REQUIRE(scheme);
    BOOST_CHECK_EQUAL(scheme->ratio, 150);
    BOOST_CHECK_EQUAL(scheme->rate, 2 * COIN);

    auto vault_id = NextTx();
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, token_id, 1 * COIN));

    auto rate = mnview.GetInterestRate(vault_id, token_id, 1);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->interestToHeight.GetLow64(), 0);
    BOOST_CHECK_EQUAL(rate->height, 1);

    auto interestPerBlock = rate->interestPerBlock;
    BOOST_REQUIRE(mnview.StoreInterest(5, vault_id, id, token_id, 1 * COIN));

    rate = mnview.GetInterestRate(vault_id, token_id, 5);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->height, 5);
    BOOST_CHECK_EQUAL(rate->interestToHeight.GetLow64(), 4 * interestPerBlock.GetLow64());

    auto interestToHeight = rate->interestToHeight;
    interestPerBlock = rate->interestPerBlock;
    BOOST_REQUIRE(mnview.EraseInterest(6, vault_id, id, token_id, 1 * COIN, (interestToHeight + interestPerBlock).GetLow64()));
    rate = mnview.GetInterestRate(vault_id, token_id, 6);

    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->interestToHeight.GetLow64(), 0);

    BOOST_REQUIRE(mnview.EraseInterest(6, vault_id, id, token_id, 1 * COIN, 0));

    rate = mnview.GetInterestRate(vault_id, token_id, 6);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->interestToHeight.GetLow64(), 0);
}

BOOST_AUTO_TEST_CASE(collateralization_ratio)
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
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice));
    auto nft_id = CreateLoanToken(mnview, "NFT", "NFT", "NFT/USD", 2 * COIN);
    fixedIntervalPrice.priceFeedId = {"NFT", "USD"};
    fixedIntervalPrice.priceRecord[1] = 2*COIN;
    fixedIntervalPrice.priceRecord[0] = 2*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice));
    auto btc_id = CreateToken(mnview, "BTC", "BITCOIN");
    CreateCollateralToken(mnview, dfi_id, "DFI/USD");
    fixedIntervalPrice.priceFeedId = {"DFI", "USD"};
    fixedIntervalPrice.priceRecord[1] = 5*COIN;
    fixedIntervalPrice.priceRecord[0] = 5*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice));
    CreateCollateralToken(mnview, btc_id, "BTC/USD");
    fixedIntervalPrice.priceFeedId = {"BTC", "USD"};
    fixedIntervalPrice.priceRecord[1] = 10*COIN;
    fixedIntervalPrice.priceRecord[0] = 10*COIN;
    BOOST_REQUIRE(mnview.SetFixedIntervalPrice(fixedIntervalPrice));

    auto vault_id = NextTx();

    CVaultData msg{};
    msg.schemeId = id;
    BOOST_REQUIRE(mnview.StoreVault(vault_id, msg));

    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {tesla_id, 10 * COIN}));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, tesla_id, 10 * COIN));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {tesla_id, 1 * COIN}));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, tesla_id, 1 * COIN));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {nft_id, 5 * COIN}));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, nft_id, 5 * COIN));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {nft_id, 4 * COIN}));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, nft_id, 4 * COIN));

    auto loan_tokens = mnview.GetLoanTokens(vault_id);
    BOOST_REQUIRE(loan_tokens);
    BOOST_CHECK_EQUAL(loan_tokens->balances.size(), 2);
    BOOST_CHECK_EQUAL(loan_tokens->balances[tesla_id], 11 * COIN);
    BOOST_CHECK_EQUAL(loan_tokens->balances[nft_id], 9 * COIN);

    BOOST_REQUIRE(mnview.AddVaultCollateral(vault_id, {dfi_id, 2 * COIN}));
    BOOST_REQUIRE(mnview.AddVaultCollateral(vault_id, {btc_id, 1 * COIN}));
    BOOST_REQUIRE(mnview.AddVaultCollateral(vault_id, {btc_id, 2 * COIN}));

    auto collaterals = mnview.GetVaultCollaterals(vault_id);
    BOOST_REQUIRE(collaterals);
    BOOST_CHECK_EQUAL(collaterals->balances.size(), 2);
    BOOST_CHECK_EQUAL(collaterals->balances[dfi_id], 2 * COIN);
    BOOST_CHECK_EQUAL(collaterals->balances[btc_id], 3 * COIN);

    auto colls = mnview.GetLoanCollaterals(vault_id, *collaterals, 10, 0);
    BOOST_REQUIRE(colls.ok);
    BOOST_CHECK_EQUAL(colls.val->ratio(), 78);
}

BOOST_AUTO_TEST_CASE(auction_batch_creator)
{
    {
        CCollateralLoans collLoan = {
            7000 * COIN, 1000 * COIN,
            {{DCT_ID{0}, 2000 * COIN}, {DCT_ID{1}, 5000 * COIN}},
            {{DCT_ID{1}, 1000 * COIN}},
        };
        TAmounts collBalances = {
            {DCT_ID{0}, 1000 * COIN},
            {DCT_ID{1}, 333 * COIN},
        };
        TAmounts loanBalances = {
            {DCT_ID{1}, 150 * COIN},
        };

        auto batches = CollectAuctionBatches(collLoan, collBalances, loanBalances);
        BOOST_CHECK_EQUAL(batches.size(), 1);
        auto& collaterals = batches[0].collaterals.balances;
        auto& loan = batches[0].loanAmount;
        BOOST_CHECK_EQUAL(collaterals.size(), 2);
        BOOST_CHECK_EQUAL(collaterals[DCT_ID{0}], 1000 * COIN);
        BOOST_CHECK_EQUAL(collaterals[DCT_ID{1}], 333 * COIN);
        BOOST_CHECK_EQUAL(loan.nTokenId.v, DCT_ID{1}.v);
        BOOST_CHECK_EQUAL(loan.nValue, 150 * COIN);
    }
    {
        CAmount value1 = 7539.54534537 * COIN;
        CAmount value2 = 3457.36134739 * COIN;
        CAmount value3 = 873.54534533 * COIN;
        CAmount value4 = 999.74743249 * COIN;
        CAmount value5 = 333.13573427 * COIN;
        CAmount value6 = 271.46557479 * COIN;

        CCollateralLoans collLoan = {
            uint64_t(value1) + value2 + value4, uint64_t(value3),
            {{DCT_ID{0}, value1}, {DCT_ID{1}, value2}, {DCT_ID{2}, value4}},
            {{DCT_ID{1}, value3}},
        };
        TAmounts collBalances = {
            {DCT_ID{0}, value4},
            {DCT_ID{1}, value5},
            {DCT_ID{2}, value6},
        };
        TAmounts loanBalances = {
            {DCT_ID{1}, value6},
        };

        auto batches = CollectAuctionBatches(collLoan, collBalances, loanBalances);
        BOOST_CHECK_EQUAL(batches.size(), 2);
        CBalances cbalances, lbalances;
        for (auto& batch : batches) {
            cbalances.AddBalances(batch.collaterals.balances);
            lbalances.Add(batch.loanAmount);
        }
        BOOST_CHECK_EQUAL(lbalances.balances.size(), 1);
        BOOST_CHECK_EQUAL(lbalances.balances[DCT_ID{1}], value6);
        BOOST_CHECK_EQUAL(cbalances.balances.size(), 3);
        BOOST_CHECK_EQUAL(cbalances.balances[DCT_ID{0}], value4);
        BOOST_CHECK_EQUAL(cbalances.balances[DCT_ID{1}], value5);
        BOOST_CHECK_EQUAL(cbalances.balances[DCT_ID{2}], value6);
    }
}

BOOST_AUTO_TEST_SUITE_END()
