#include <chainparams.h>
#include <masternodes/loan.h>
#include <masternodes/masternodes.h>
#include <validation.h>

#include <test/setup_common.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>

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

extern std::vector<CAuctionBatch> CollectAuctionBatches(const CVaultAssets& vaultAssets, const TAmounts& collBalances, const TAmounts& loanBalances);

BOOST_FIXTURE_TEST_SUITE(loan_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(high_precision_interest_rate_to_string_tests)
{
    std::map<std::variant<base_uint<128>, std::string>, std::string> testMap = {
        { 0, "0.000000000000000000000000" },
        { 1, "0.000000000000000000000001" },
        { 42058, "0.000000000000000000042058" },
        { 95129375, "0.000000000000000095129375" },
        { 117009132, "0.000000000000000117009132" },
        { 11700913242, "0.000000000000011700913242" },
        // 2378234398782343987
        { "21012F95D4094B33", "0.000002378234398782343987" },
        // 80897539693407360060932882613242451388
        { "3CDC4CA64879921C03BF061156E455BC" , "80897539693407.360060932882613242451388" },
         // 87741364994776235347880977943597222
        { "10E5FBB8CA9E273D0B0353C23D90A6" , "87741364994.776235347880977943597222" },
        // 877413626032608048611111111
        { "2D5C78FF9C3FE70F9F0B0C7" , "877.413626032608048611111111" },
        { std::numeric_limits<uint64_t>::min(), "0.000000000000000000000000" },
        { std::numeric_limits<uint64_t>::max(), "0.000018446744073709551615" },
        { std::numeric_limits<int64_t>::min(), "0.000009223372036854775808" },
        { std::numeric_limits<int64_t>::max(), "0.000009223372036854775807" },

        // Full list by rotating 1s all over.. The reason for adding this to full spectrum
        // test is since we use arbitrary bit ranges to achieve COIN ^ 3 precision. One vector of
        // common mistakes would be due to improper cast and the first high 1 bit being interpreted
        // as 2s complement and as such result in a negative error. This check verifies the entire
        // range to ensure this doesn't happen.
        //
        { "80000000000000000000000000000000", "170141183460469.231731687303715884105728" },
        { "40000000000000000000000000000000", "85070591730234.615865843651857942052864" },
        { "20000000000000000000000000000000", "42535295865117.307932921825928971026432" },
        { "10000000000000000000000000000000", "21267647932558.653966460912964485513216" },
        { "08000000000000000000000000000000", "10633823966279.326983230456482242756608" },
        { "04000000000000000000000000000000", "5316911983139.663491615228241121378304" },
        { "02000000000000000000000000000000", "2658455991569.831745807614120560689152" },
        { "01000000000000000000000000000000", "1329227995784.915872903807060280344576" },
        { "00800000000000000000000000000000", "664613997892.457936451903530140172288" },
        { "00400000000000000000000000000000", "332306998946.228968225951765070086144" },
        { "00200000000000000000000000000000", "166153499473.114484112975882535043072" },
        { "00100000000000000000000000000000", "83076749736.557242056487941267521536" },
        { "00080000000000000000000000000000", "41538374868.278621028243970633760768" },
        { "00040000000000000000000000000000", "20769187434.139310514121985316880384" },
        { "00020000000000000000000000000000", "10384593717.069655257060992658440192" },
        { "00010000000000000000000000000000", "5192296858.534827628530496329220096" },
        { "00008000000000000000000000000000", "2596148429.267413814265248164610048" },
        { "00004000000000000000000000000000", "1298074214.633706907132624082305024" },
        { "00002000000000000000000000000000", "649037107.316853453566312041152512" },
        { "00001000000000000000000000000000", "324518553.658426726783156020576256" },
        { "00000800000000000000000000000000", "162259276.829213363391578010288128" },
        { "00000400000000000000000000000000", "81129638.414606681695789005144064" },
        { "00000200000000000000000000000000", "40564819.207303340847894502572032" },
        { "00000100000000000000000000000000", "20282409.603651670423947251286016" },
        { "00000080000000000000000000000000", "10141204.801825835211973625643008" },
        { "00000040000000000000000000000000", "5070602.400912917605986812821504" },
        { "00000020000000000000000000000000", "2535301.200456458802993406410752" },
        { "00000010000000000000000000000000", "1267650.600228229401496703205376" },
        { "00000008000000000000000000000000", "633825.300114114700748351602688" },
        { "00000004000000000000000000000000", "316912.650057057350374175801344" },
        { "00000002000000000000000000000000", "158456.325028528675187087900672" },
        { "00000001000000000000000000000000", "79228.162514264337593543950336" },
        { "00000000800000000000000000000000", "39614.081257132168796771975168" },
        { "00000000400000000000000000000000", "19807.040628566084398385987584" },
        { "00000000200000000000000000000000", "9903.520314283042199192993792" },
        { "00000000100000000000000000000000", "4951.760157141521099596496896" },
        { "00000000080000000000000000000000", "2475.880078570760549798248448" },
        { "00000000040000000000000000000000", "1237.940039285380274899124224" },
        { "00000000020000000000000000000000", "618.970019642690137449562112" },
        { "00000000010000000000000000000000", "309.485009821345068724781056" },
        { "00000000008000000000000000000000", "154.742504910672534362390528" },
        { "00000000004000000000000000000000", "77.371252455336267181195264" },
        { "00000000002000000000000000000000", "38.685626227668133590597632" },
        { "00000000001000000000000000000000", "19.342813113834066795298816" },
        { "00000000000800000000000000000000", "9.671406556917033397649408" },
        { "00000000000400000000000000000000", "4.835703278458516698824704" },
        { "00000000000200000000000000000000", "2.417851639229258349412352" },
        { "00000000000100000000000000000000", "1.208925819614629174706176" },
        { "00000000000080000000000000000000", "0.604462909807314587353088" },
        { "00000000000040000000000000000000", "0.302231454903657293676544" },
        { "00000000000020000000000000000000", "0.151115727451828646838272" },
        { "00000000000010000000000000000000", "0.075557863725914323419136" },
        { "00000000000008000000000000000000", "0.037778931862957161709568" },
        { "00000000000004000000000000000000", "0.018889465931478580854784" },
        { "00000000000002000000000000000000", "0.009444732965739290427392" },
        { "00000000000001000000000000000000", "0.004722366482869645213696" },
        { "00000000000000800000000000000000", "0.002361183241434822606848" },
        { "00000000000000400000000000000000", "0.001180591620717411303424" },
        { "00000000000000200000000000000000", "0.000590295810358705651712" },
        { "00000000000000100000000000000000", "0.000295147905179352825856" },
        { "00000000000000080000000000000000", "0.000147573952589676412928" },
        { "00000000000000040000000000000000", "0.000073786976294838206464" },
        { "00000000000000020000000000000000", "0.000036893488147419103232" },
        { "00000000000000010000000000000000", "0.000018446744073709551616" },
        { "00000000000000008000000000000000", "0.000009223372036854775808" },
        { "00000000000000004000000000000000", "0.000004611686018427387904" },
        { "00000000000000002000000000000000", "0.000002305843009213693952" },
        { "00000000000000001000000000000000", "0.000001152921504606846976" },
        { "00000000000000000800000000000000", "0.000000576460752303423488" },
        { "00000000000000000400000000000000", "0.000000288230376151711744" },
        { "00000000000000000200000000000000", "0.000000144115188075855872" },
        { "00000000000000000100000000000000", "0.000000072057594037927936" },
        { "00000000000000000080000000000000", "0.000000036028797018963968" },
        { "00000000000000000040000000000000", "0.000000018014398509481984" },
        { "00000000000000000020000000000000", "0.000000009007199254740992" },
        { "00000000000000000010000000000000", "0.000000004503599627370496" },
        { "00000000000000000008000000000000", "0.000000002251799813685248" },
        { "00000000000000000004000000000000", "0.000000001125899906842624" },
        { "00000000000000000002000000000000", "0.000000000562949953421312" },
        { "00000000000000000001000000000000", "0.000000000281474976710656" },
        { "00000000000000000000800000000000", "0.000000000140737488355328" },
        { "00000000000000000000400000000000", "0.000000000070368744177664" },
        { "00000000000000000000200000000000", "0.000000000035184372088832" },
        { "00000000000000000000100000000000", "0.000000000017592186044416" },
        { "00000000000000000000080000000000", "0.000000000008796093022208" },
        { "00000000000000000000040000000000", "0.000000000004398046511104" },
        { "00000000000000000000020000000000", "0.000000000002199023255552" },
        { "00000000000000000000010000000000", "0.000000000001099511627776" },
        { "00000000000000000000008000000000", "0.000000000000549755813888" },
        { "00000000000000000000004000000000", "0.000000000000274877906944" },
        { "00000000000000000000002000000000", "0.000000000000137438953472" },
        { "00000000000000000000001000000000", "0.000000000000068719476736" },
        { "00000000000000000000000800000000", "0.000000000000034359738368" },
        { "00000000000000000000000400000000", "0.000000000000017179869184" },
        { "00000000000000000000000200000000", "0.000000000000008589934592" },
        { "00000000000000000000000100000000", "0.000000000000004294967296" },
        { "00000000000000000000000080000000", "0.000000000000002147483648" },
        { "00000000000000000000000040000000", "0.000000000000001073741824" },
        { "00000000000000000000000020000000", "0.000000000000000536870912" },
        { "00000000000000000000000010000000", "0.000000000000000268435456" },
        { "00000000000000000000000008000000", "0.000000000000000134217728" },
        { "00000000000000000000000004000000", "0.000000000000000067108864" },
        { "00000000000000000000000002000000", "0.000000000000000033554432" },
        { "00000000000000000000000001000000", "0.000000000000000016777216" },
        { "00000000000000000000000000800000", "0.000000000000000008388608" },
        { "00000000000000000000000000400000", "0.000000000000000004194304" },
        { "00000000000000000000000000200000", "0.000000000000000002097152" },
        { "00000000000000000000000000100000", "0.000000000000000001048576" },
        { "00000000000000000000000000080000", "0.000000000000000000524288" },
        { "00000000000000000000000000040000", "0.000000000000000000262144" },
        { "00000000000000000000000000020000", "0.000000000000000000131072" },
        { "00000000000000000000000000010000", "0.000000000000000000065536" },
        { "00000000000000000000000000008000", "0.000000000000000000032768" },
        { "00000000000000000000000000004000", "0.000000000000000000016384" },
        { "00000000000000000000000000002000", "0.000000000000000000008192" },
        { "00000000000000000000000000001000", "0.000000000000000000004096" },
        { "00000000000000000000000000000800", "0.000000000000000000002048" },
        { "00000000000000000000000000000400", "0.000000000000000000001024" },
        { "00000000000000000000000000000200", "0.000000000000000000000512" },
        { "00000000000000000000000000000100", "0.000000000000000000000256" },
        { "00000000000000000000000000000080", "0.000000000000000000000128" },
        { "00000000000000000000000000000040", "0.000000000000000000000064" },
        { "00000000000000000000000000000020", "0.000000000000000000000032" },
        { "00000000000000000000000000000010", "0.000000000000000000000016" },
        { "00000000000000000000000000000008", "0.000000000000000000000008" },
        { "00000000000000000000000000000004", "0.000000000000000000000004" },
        { "00000000000000000000000000000002", "0.000000000000000000000002" },
        { "00000000000000000000000000000001", "0.000000000000000000000001" },
    };

    for (const auto& kv: testMap) {
        auto key = kv.first;
        auto expectedResult = kv.second;

        base_uint<128> input;
        auto typeKind = key.index();
        if (typeKind == 0) input = std::get<base_uint<128>>(key);
        else if (typeKind == 1) input = base_uint<128>(std::get<std::string>(key));
        else BOOST_TEST_FAIL("unknown type");

        auto res = GetInterestPerBlockHighPrecisionString({false, input});
        BOOST_CHECK_EQUAL(res, expectedResult);
    }

    // Lets at least test a couple of negative interest rates. TODO add more!
    auto res = GetInterestPerBlockHighPrecisionString({true, 1});
    BOOST_CHECK_EQUAL(res, "-0.000000000000000000000001");
    res = GetInterestPerBlockHighPrecisionString({true, std::numeric_limits<int64_t>::min()});
    BOOST_CHECK_EQUAL(res, "-0.000009223372036854775808");

    // Quick way to generate the nums and verify
    // std::vector<base_uint<128>> nums;
    // std::string hexStr = "8000 0000 0000 0000 0000 0000 0000 0000";
    // hexStr.erase(std::remove(hexStr.begin(), hexStr.end(), ' '), hexStr.end());
    // auto i = base_uint<128>(hexStr);
    // for (auto n = 0; n < 128; n++) {
    //     nums.push_back(i);
    //     std::cout << " { \"" << i.GetHex() << "\", \"";
    //     std::cout << TryGetInterestPerBlockHighPrecisionString(i);
    //     std::cout << "\" }," << std::endl;
    //     i = i >> 1;
    // }
}

BOOST_AUTO_TEST_CASE(loan_interest_rate)
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
    BOOST_REQUIRE(mnview.IncreaseInterest(1, vault_id, id, token_id, tokenInterest, 1 * COIN));

    auto rate = mnview.GetInterestRate(vault_id, token_id, 1);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->interestToHeight.amount.GetLow64(), 0);
    BOOST_CHECK_EQUAL(rate->height, 1);

    auto interestPerBlock = rate->interestPerBlock;
    BOOST_REQUIRE(mnview.IncreaseInterest(5, vault_id, id, token_id, tokenInterest, 1 * COIN));

    rate = mnview.GetInterestRate(vault_id, token_id, 5);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->height, 5);
    BOOST_CHECK_EQUAL(rate->interestToHeight.amount.GetLow64(), 4 * interestPerBlock.amount.GetLow64());

    auto interestToHeight = rate->interestToHeight;
    interestPerBlock = rate->interestPerBlock;
    BOOST_REQUIRE(mnview.DecreaseInterest(6, vault_id, id, token_id, 1 * COIN, (interestToHeight.amount + interestPerBlock.amount).GetLow64()));
    rate = mnview.GetInterestRate(vault_id, token_id, 6);

    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->interestToHeight.amount.GetLow64(), 0);

    BOOST_REQUIRE(mnview.DecreaseInterest(6, vault_id, id, token_id, 1 * COIN, 0));

    rate = mnview.GetInterestRate(vault_id, token_id, 6);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->interestToHeight.amount.GetLow64(), 0);
}

BOOST_AUTO_TEST_CASE(loan_total_interest_calculation)
{
    // Activate negative interest rate
    const_cast<int&>(Params().GetConsensus().FortCanningGreatWorldHeight) = 1;

    CCustomCSView mnview(*pcustomcsview);

    const std::string scheme_id("sch1");
    CreateScheme(mnview, scheme_id, 150, 0);

    CAmount tokenInterest = 5 * COIN;
    auto token_id = CreateLoanToken(mnview, "TST", "TEST", "", tokenInterest);

    auto vault_id = NextTx();
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {token_id, 1 * COIN}));

    BOOST_REQUIRE(mnview.IncreaseInterest(1, vault_id, scheme_id, token_id, tokenInterest, 0));
    auto rate = mnview.GetInterestRate(vault_id, token_id, 1);
    CInterestAmount totalInterest = TotalInterestCalculation(*rate, 1);
    BOOST_CHECK_EQUAL(rate->interestToHeight.negative, false);
    BOOST_CHECK_EQUAL(rate->interestPerBlock.negative, false);
    BOOST_CHECK_EQUAL(rate->interestToHeight.amount.GetLow64(), 0);
    BOOST_CHECK_EQUAL(totalInterest.negative, false);
    BOOST_CHECK_EQUAL(totalInterest.amount.GetLow64(), 0);

    BOOST_REQUIRE(mnview.IncreaseInterest(5, vault_id, scheme_id, token_id, tokenInterest, 0));
    rate = mnview.GetInterestRate(vault_id, token_id, 5);
    totalInterest = TotalInterestCalculation(*rate, 5);
    BOOST_CHECK_EQUAL(rate->interestToHeight.negative, false);
    BOOST_CHECK_EQUAL(rate->interestPerBlock.negative, false);
    BOOST_CHECK_EQUAL(totalInterest.negative, false);
    BOOST_CHECK_EQUAL(totalInterest.amount.GetLow64(), 4 * rate->interestPerBlock.amount.GetLow64());

    tokenInterest = -5 * COIN;

    BOOST_REQUIRE(mnview.IncreaseInterest(6, vault_id, scheme_id, token_id, tokenInterest, 0));
    rate = mnview.GetInterestRate(vault_id, token_id, 6);
    totalInterest = TotalInterestCalculation(*rate, 6);
    BOOST_CHECK_EQUAL(rate->interestPerBlock.negative, true);
    BOOST_CHECK_EQUAL(rate->interestToHeight.negative, false);
    BOOST_CHECK_EQUAL(totalInterest.negative, false);
    BOOST_CHECK_EQUAL(totalInterest.amount.GetLow64(), 5 * rate->interestPerBlock.amount.GetLow64());

    BOOST_REQUIRE(mnview.IncreaseInterest(7, vault_id, scheme_id, token_id, tokenInterest, 0));
    rate = mnview.GetInterestRate(vault_id, token_id, 7);
    totalInterest = TotalInterestCalculation(*rate, 7);
    BOOST_CHECK_EQUAL(rate->interestPerBlock.negative, true);
    BOOST_CHECK_EQUAL(rate->interestToHeight.negative, false);
    BOOST_CHECK_EQUAL(totalInterest.negative, false);
    BOOST_CHECK_EQUAL(totalInterest.amount.GetLow64(), 4 * rate->interestPerBlock.amount.GetLow64());

    BOOST_REQUIRE(mnview.IncreaseInterest(11, vault_id, scheme_id, token_id, tokenInterest, 0));
    rate = mnview.GetInterestRate(vault_id, token_id, 11);
    totalInterest = TotalInterestCalculation(*rate, 11);
    BOOST_CHECK_EQUAL(rate->interestPerBlock.negative, true);
    BOOST_CHECK_EQUAL(rate->interestToHeight.negative, false);
    BOOST_CHECK_EQUAL(rate->interestToHeight.amount.GetLow64(), 0);
    BOOST_CHECK_EQUAL(totalInterest.negative, false);
    BOOST_CHECK_EQUAL(totalInterest.amount.GetLow64(), 0);

    BOOST_REQUIRE(mnview.IncreaseInterest(15, vault_id, scheme_id, token_id, tokenInterest, 0));
    rate = mnview.GetInterestRate(vault_id, token_id, 15);
    totalInterest = TotalInterestCalculation(*rate, 15);
    BOOST_CHECK_EQUAL(rate->interestPerBlock.negative, true);
    BOOST_CHECK_EQUAL(rate->interestToHeight.negative, true);
    BOOST_CHECK_EQUAL(totalInterest.negative, true);
    BOOST_CHECK_EQUAL(totalInterest.amount.GetLow64(), 4 * rate->interestPerBlock.amount.GetLow64());
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
    BOOST_REQUIRE(mnview.IncreaseInterest(1, vault_id, id, tesla_id, 5 * COIN, 10 * COIN));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {tesla_id, 1 * COIN}));
    BOOST_REQUIRE(mnview.IncreaseInterest(1, vault_id, id, tesla_id, 5 * COIN, 1 * COIN));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {nft_id, 5 * COIN}));
    BOOST_REQUIRE(mnview.IncreaseInterest(1, vault_id, id, nft_id, 2 * COIN, 5 * COIN));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {nft_id, 4 * COIN}));
    BOOST_REQUIRE(mnview.IncreaseInterest(1, vault_id, id, nft_id, 2 * COIN, 4 * COIN));

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

    auto colls = mnview.GetVaultAssets(vault_id, *collaterals, 10, 0);
    BOOST_REQUIRE(colls.ok);
    BOOST_CHECK_EQUAL(colls.val->ratio(), 78);
}

BOOST_AUTO_TEST_CASE(auction_batch_creator)
{
    {
        CVaultAssets vaultAssets = {
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

        auto batches = CollectAuctionBatches(vaultAssets, collBalances, loanBalances);
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

        CVaultAssets vaultAssets = {
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

        auto batches = CollectAuctionBatches(vaultAssets, collBalances, loanBalances);
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
