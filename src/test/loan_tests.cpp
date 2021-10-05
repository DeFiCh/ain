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
        loanToken.priceFeed = std::make_pair(priceFeed.substr(0, delim), priceFeed.substr(delim + 1));
    }
    loanToken.creationTx = NextTx();
    auto id = CreateToken(mnview, symbol, name);
    mnview.LoanSetLoanToken(loanToken, id);
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
        collateralToken.priceFeed = std::make_pair(priceFeed.substr(0, delim), priceFeed.substr(delim + 1));
    }
    mnview.LoanCreateSetCollateralToken(collateralToken);
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
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, token_id));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, token_id));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, token_id));

    auto rate = mnview.GetInterestRate(id, token_id);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->count, 3);
    BOOST_CHECK_EQUAL(rate->height, 1);
    auto netInterest = (scheme->rate + tokenInterest) / 100;
    BOOST_CHECK_EQUAL(rate->interestPerBlock, netInterest * rate->count / (365 * Params().GetConsensus().blocksPerDay()));

    auto interestToHeight = rate->interestPerBlock + rate->interestToHeight;
    BOOST_REQUIRE(mnview.StoreInterest(5, vault_id, id, token_id));
    BOOST_REQUIRE(mnview.StoreInterest(5, vault_id, id, token_id));

    rate = mnview.GetInterestRate(id, token_id);
    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->count, 5);
    BOOST_CHECK_EQUAL(rate->height, 5);
    BOOST_CHECK_EQUAL(rate->interestToHeight, 4 * interestToHeight);
    BOOST_CHECK_EQUAL(rate->interestPerBlock, netInterest * rate->count / (365 * Params().GetConsensus().blocksPerDay()));

    interestToHeight = rate->interestPerBlock + rate->interestToHeight;
    BOOST_REQUIRE(mnview.EraseInterest(6, vault_id, id, token_id));
    rate = mnview.GetInterestRate(id, token_id);

    BOOST_REQUIRE(rate);
    BOOST_CHECK_EQUAL(rate->count, 4);
    BOOST_CHECK_EQUAL(rate->interestToHeight, interestToHeight);
    BOOST_CHECK_EQUAL(rate->interestPerBlock, netInterest * rate->count / (365 * Params().GetConsensus().blocksPerDay()));

    mnview.ForEachVaultInterest([&](const CVaultId& vaultId, DCT_ID id, uint32_t rate_count) {
        BOOST_REQUIRE(vaultId == vault_id);
        BOOST_REQUIRE(id == token_id);
        BOOST_REQUIRE(id == token_id);
        BOOST_CHECK_EQUAL(rate_count, rate->count);
        return true;
    }, vault_id);

    for (int i = 0; i < 4; i++) {
        BOOST_REQUIRE(mnview.EraseInterest(6, vault_id, id, token_id));
    }

    rate = mnview.GetInterestRate(id, token_id);
    BOOST_CHECK_EQUAL(rate->count, 0);
    BOOST_CHECK_EQUAL(rate->interestPerBlock, 0);
    BOOST_CHECK_EQUAL(rate->interestToHeight, interestToHeight);

    BOOST_REQUIRE(!mnview.EraseInterest(6, vault_id, id, token_id));

    bool noInterestRecords = true;
    mnview.ForEachVaultInterest([&](const CVaultId&, DCT_ID, uint32_t) {
        noInterestRecords = false;
        return true;
    }, vault_id);

    BOOST_REQUIRE(noInterestRecords);

    BOOST_REQUIRE(mnview.EraseLoanScheme(id));
    rate = mnview.GetInterestRate(id, token_id);

    BOOST_REQUIRE(!rate);
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
    auto nft_id = CreateLoanToken(mnview, "NFT", "NFT", "NFT/USD", 2 * COIN);
    auto btc_id = CreateToken(mnview, "BTC", "BITCOIN");
    CreateCollateralToken(mnview, dfi_id, "DFI/USD");
    CreateCollateralToken(mnview, btc_id, "BTC/USD");

    auto vault_id = NextTx();
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, tesla_id));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, tesla_id));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, tesla_id));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, nft_id));
    BOOST_REQUIRE(mnview.StoreInterest(1, vault_id, id, nft_id));

    CVaultData msg{};
    msg.schemeId = id;
    BOOST_REQUIRE(mnview.StoreVault(vault_id, msg));

    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {tesla_id, 10 * COIN}));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {tesla_id, 1 * COIN}));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {nft_id, 5 * COIN}));
    BOOST_REQUIRE(mnview.AddLoanToken(vault_id, {nft_id, 4 * COIN}));

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

    auto colls = mnview.CalculateCollateralizationRatio(vault_id, *collaterals, 10, 0);
    BOOST_REQUIRE(colls.ok);
    BOOST_CHECK_EQUAL(colls.val->ratio(), 78);
}

BOOST_AUTO_TEST_CASE(auction_batch_creator)
{
    {
        CCollateralLoans collLoan = {
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
