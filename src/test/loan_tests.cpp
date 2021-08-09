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

DCT_ID CreateLoanToken(CCustomCSView &mnview, CAmount interest)
{
    CTokenImplementation token;
    CLoanSetLoanTokenImplementation loanToken;
    loanToken.interest = interest;
    loanToken.creationTx = token.creationTx = NextTx();
    token.flags = (uint8_t)CToken::TokenFlags::Default;
    token.flags |= (uint8_t)CToken::TokenFlags::LoanToken | (uint8_t)CToken::TokenFlags::DAT;

    token.symbol = "TST";
    token.name = "Test";

    auto res = mnview.CreateToken(token, false);
    if (!res.ok) printf("%s\n", res.msg.c_str());
    BOOST_REQUIRE(res.ok);
    auto id = *res.val;
    mnview.LoanSetLoanToken(loanToken, id);
    return id;
}

BOOST_FIXTURE_TEST_SUITE(loan_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(loan_iterest_rate)
{
    CCustomCSView mnview(*pcustomcsview);

    const std::string id("sch1");
    CLoanSchemeMessage msg;
    msg.ratio = 150;
    msg.rate = 2 * COIN;
    msg.identifier = id;
    mnview.StoreLoanScheme(msg);

    const CAmount tokenInterest = 5 * COIN;
    auto token_id = CreateLoanToken(mnview, tokenInterest);
    auto scheme = mnview.GetLoanScheme(id);
    BOOST_REQUIRE(scheme);
    BOOST_CHECK_EQUAL(scheme->ratio, 150);
    BOOST_CHECK_EQUAL(scheme->rate, 2 * COIN);

    mnview.StoreInterest(1, id, token_id);
    mnview.StoreInterest(1, id, token_id);
    mnview.StoreInterest(1, id, token_id);

    auto rate = mnview.GetInterestRate(id, token_id);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->count, 3);
    BOOST_CHECK_EQUAL(rate->height, 1);
    auto netInterest = scheme->rate + tokenInterest;
    BOOST_CHECK_EQUAL(rate->interestPerBlock, netInterest * rate->count / (365 * Params().GetConsensus().blocksPerDay()));

    auto interestPerBlock = rate->interestPerBlock + rate->interestToHeight;
    mnview.StoreInterest(5, id, token_id);
    mnview.StoreInterest(5, id, token_id);

    rate = mnview.GetInterestRate(id, token_id);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->count, 5);
    BOOST_CHECK_EQUAL(rate->height, 5);
    BOOST_CHECK_EQUAL(rate->interestToHeight, 4 * interestPerBlock);
    BOOST_CHECK_EQUAL(rate->interestPerBlock, netInterest * rate->count / (365 * Params().GetConsensus().blocksPerDay()));

    interestPerBlock = rate->interestPerBlock + rate->interestToHeight;
    mnview.EraseInterest(6, id, token_id);
    rate = mnview.GetInterestRate(id, token_id);

    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->count, 4);
    BOOST_CHECK_EQUAL(rate->interestToHeight, interestPerBlock);
    BOOST_CHECK_EQUAL(rate->interestPerBlock, netInterest * rate->count / (365 * Params().GetConsensus().blocksPerDay()));

    for (int i = 0; i < 4; i++) {
        mnview.EraseInterest(6, id, token_id);
    }
    rate = mnview.GetInterestRate(id, token_id);

    BOOST_REQUIRE(!rate);
}

BOOST_AUTO_TEST_SUITE_END()
