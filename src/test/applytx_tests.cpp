// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <test/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(applytx_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(neg_token_amounts)
{
    {
        CTokenAmount val{};
        auto res = val.Add(-100);
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_EQUAL(res.msg, "negative amount: -0.00000100");

        res = val.Sub(-100);
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_EQUAL(res.msg, "negative amount: -0.00000100");
    }

    { // it is possible to create neg TokenAmount, but can't manipulate with it
        CTokenAmount val{DCT_ID{0}, -100};
        auto res = val.Add(100);
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_EQUAL(res.msg, "negative amount");

        res = val.Sub(100);
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_EQUAL(res.msg, "amount -0.00000100 is less than 0.00000100");

        res = val.Sub(-200);
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_EQUAL(res.msg, "negative amount: -0.00000200");
    }
}

// redundant due to 'neg_token_amounts'
BOOST_AUTO_TEST_CASE(neg_token_balances)
{
    CCustomCSView mnview(*pcustomcsview);

    CScript const owner = CScript(1);
    DCT_ID const DFI{0};
    {
        // Initial value
        auto dfi100 = CTokenAmount{DCT_ID{0}, 100};
        auto res = mnview.AddBalance(owner, dfi100);
        BOOST_CHECK(res.ok);
        BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi100);

        // Fail to add negative
        res = mnview.AddBalance(owner, CTokenAmount{DCT_ID{0}, -100});
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_EQUAL(res.msg, "negative amount: -0.00000100");
        BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi100);

        // Fail to sub negative
        res = mnview.SubBalance(owner, CTokenAmount{DCT_ID{0}, -100});
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_EQUAL(res.msg, "negative amount: -0.00000100");
        BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi100);
    }
}

CScript CreateMetaA2A(CAccountToAccountMessage const & msg) {
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AccountToAccount) << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);
    return scriptMeta;
}

// redundant due to 'neg_token_amounts'
BOOST_AUTO_TEST_CASE(apply_a2a_neg)
{
    Consensus::Params amkCheated = Params().GetConsensus();
    amkCheated.AMKHeight = 0;


    CCustomCSView mnview(*pcustomcsview);
    CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());

    CScript owner = CScript(424242);
    DCT_ID DFI{0};

    // add auth coin to coinview
    auto auth_out = COutPoint(uint256S("0xafaf"),42);
    coinview.AddCoin(auth_out, Coin(CTxOut(1, owner, DFI), 1, false), false);

    // Initial value
    auto dfi100 = CTokenAmount{DCT_ID{0}, 100};
    auto res = mnview.AddBalance(owner, dfi100);
    BOOST_CHECK(res.ok);
    BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi100);

    // create templates for msg and tx:
    CAccountToAccountMessage msg{};
    msg.from = owner;
    CMutableTransaction rawTx;
    rawTx.vout = { CTxOut(0, CScript()) };
    rawTx.vin = { CTxIn(auth_out) };

    // try to send "A:-1@DFI"
    {
        msg.to = {
            { CScript(0xA), CBalances{{ {DFI, -1} }} }
        };

        rawTx.vout[0].scriptPubKey = CreateMetaA2A(msg);

        uint64_t gasUsed{};
        res = ApplyCustomTx(mnview, coinview, CTransaction(rawTx), amkCheated, 1, gasUsed);
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_NE(res.msg.find("negative amount"), std::string::npos);
        // check that nothing changes:
        BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi100);
        BOOST_CHECK_EQUAL(mnview.GetBalance(CScript(0xA), DFI), CTokenAmount{});
    }

    // try to send "A:101@DFI"
    {
        msg.to = {
            { CScript(0xA), CBalances{{ {DFI, 101} }} }
        };

        rawTx.vout[0].scriptPubKey = CreateMetaA2A(msg);

        uint64_t gasUsed{};
        res = ApplyCustomTx(mnview, coinview, CTransaction(rawTx), amkCheated, 1, gasUsed);
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_EQUAL(res.code, (uint32_t) CustomTxErrCodes::NotEnoughBalance);
        // check that nothing changes:
        BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi100);
        BOOST_CHECK_EQUAL(mnview.GetBalance(CScript(0xA), DFI), CTokenAmount{});
    }

    // try to send "A:10@DFI, B:-1@DFI"
    {
        msg.to = {
            { CScript(0xA), CBalances{{ {DFI, 10} }} },
            { CScript(0xB), CBalances{{ {DFI, -1} }} }
        };

        rawTx.vout[0].scriptPubKey = CreateMetaA2A(msg);

        uint64_t gasUsed{};
        res = ApplyCustomTx(mnview, coinview, CTransaction(rawTx), amkCheated, 1, gasUsed);
        BOOST_CHECK(!res.ok);
        BOOST_CHECK_NE(res.msg.find("negative amount"), std::string::npos);
        // check that nothing changes:
        BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi100);
        BOOST_CHECK_EQUAL(mnview.GetBalance(CScript(0xA), DFI), CTokenAmount{});
        BOOST_CHECK_EQUAL(mnview.GetBalance(CScript(0xB), DFI), CTokenAmount{});
    }

    // send "A:10@DFI" (success)
    {
        msg.to = {
            { CScript(0xA), CBalances{{ {DFI, 10} }} },
        };

        rawTx.vout[0].scriptPubKey = CreateMetaA2A(msg);

        uint64_t gasUsed{};
        res = ApplyCustomTx(mnview, coinview, CTransaction(rawTx), amkCheated, 1, gasUsed);
        BOOST_CHECK(res.ok);
        // check result balances:
        auto const dfi90 = CTokenAmount{DFI, 90};
        auto const dfi10 = CTokenAmount{DFI, 10};
        BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi90);
        BOOST_CHECK_EQUAL(mnview.GetBalance(CScript(0xA), DFI), dfi10);
    }
}

BOOST_AUTO_TEST_SUITE_END()

