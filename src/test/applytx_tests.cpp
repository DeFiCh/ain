// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <masternodes/futureswap.h>
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

    LOCK(cs_main);
    CCustomCSView mnview(*pcustomcsview);
    CFutureSwapView futureSwapView(*pfutureSwapView);
    CUndosView undosView(*pundosView);
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

        res = ApplyCustomTx(mnview, futureSwapView, coinview, CTransaction(rawTx), amkCheated, 1);
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

        res = ApplyCustomTx(mnview, futureSwapView, coinview, CTransaction(rawTx), amkCheated, 1);
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

        res = ApplyCustomTx(mnview, futureSwapView, coinview, CTransaction(rawTx), amkCheated, 1);
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

        res = ApplyCustomTx(mnview, futureSwapView, coinview, CTransaction(rawTx), amkCheated, 1);
        BOOST_CHECK(res.ok);
        // check result balances:
        auto const dfi90 = CTokenAmount{DFI, 90};
        auto const dfi10 = CTokenAmount{DFI, 10};
        BOOST_CHECK_EQUAL(mnview.GetBalance(owner, DFI), dfi90);
        BOOST_CHECK_EQUAL(mnview.GetBalance(CScript(0xA), DFI), dfi10);
    }
}

BOOST_AUTO_TEST_CASE(hardfork_guard)
{
    auto& consensus = Params().GetConsensus();

    const std::map<int, std::string> forks = {
        { consensus.AMKHeight,              "called before AMK height" },
        { consensus.BayfrontHeight,         "called before Bayfront height" },
        { consensus.BayfrontGardensHeight,  "called before Bayfront Gardens height" },
        { consensus.EunosHeight,            "called before Eunos height" },
        { consensus.FortCanningHeight,      "called before FortCanning height" },
        { consensus.FortCanningHillHeight,  "called before FortCanningHill height" },
        { consensus.FortCanningRoadHeight,  "called before FortCanningRoad height" },
        { consensus.GreatWorldHeight,       "called before GreatWorld height" },
    };

    auto parseValidator = [&](int height, auto msg, std::string error = {}) -> bool {
        if (height != 0) {
            auto it = forks.find(height);
            if (it == forks.end())
                return false;
            error = it->second;
            height--;
        }
        CCustomTxMessage message = msg;
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << msg;
        return CustomMetadataParse(height, consensus, {}, message).msg == error
            && (height == 0
            || CustomMetadataParse(height + 1, consensus, ToByteVector(stream), message).ok);
    };

    BOOST_REQUIRE(parseValidator(0, CCreateMasterNodeMessage{},
                                 "CDataStream::read(): end of data: iostream error"));
    BOOST_REQUIRE(parseValidator(0, CResignMasterNodeMessage{},
                                 "CDataStream::read(): end of data: iostream error"));

    BOOST_REQUIRE(parseValidator(consensus.AMKHeight, CCreateTokenMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.AMKHeight, CUpdateTokenPreAMKMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.AMKHeight, CMintTokensMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.AMKHeight, CUtxosToAccountMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.AMKHeight, CAccountToUtxosMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.AMKHeight, CAccountToAccountMessage{}));

    BOOST_REQUIRE(parseValidator(consensus.BayfrontHeight, CUpdateTokenMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.BayfrontHeight, CPoolSwapMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.BayfrontHeight, CLiquidityMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.BayfrontHeight, CRemoveLiquidityMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.BayfrontHeight, CCreatePoolPairMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.BayfrontHeight, CUpdatePoolPairMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.BayfrontHeight, CGovernanceMessage{}));

    BOOST_REQUIRE(parseValidator(consensus.BayfrontGardensHeight, CAnyAccountsToAccountsMessage{}));

    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CAppointOracleMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CRemoveOracleAppointMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CUpdateOracleAppointMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CSetOracleDataMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CICXCreateOrderMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CICXMakeOfferMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CICXSubmitDFCHTLCMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CICXSubmitEXTHTLCMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CICXClaimDFCHTLCMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CICXCloseOrderMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.EunosHeight, CICXCloseOfferMessage{}));

    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CGovernanceHeightMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CPoolSwapMessageV2{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CLoanSetCollateralTokenMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CLoanSetLoanTokenMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CLoanUpdateLoanTokenMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CLoanSchemeMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CDefaultLoanSchemeMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CDestroyLoanSchemeMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CVaultMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CCloseVaultMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CUpdateVaultMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CDepositToVaultMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CWithdrawFromVaultMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CLoanTakeLoanMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CLoanPaybackLoanMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningHeight, CAuctionBidMessage{}));

    BOOST_REQUIRE(parseValidator(consensus.FortCanningHillHeight, CSmartContractMessage{}));

    BOOST_REQUIRE(parseValidator(consensus.FortCanningRoadHeight, CFutureSwapMessage{}));
    BOOST_REQUIRE(parseValidator(consensus.FortCanningRoadHeight, CLoanPaybackLoanV2Message{}));

    BOOST_REQUIRE(parseValidator(consensus.GreatWorldHeight, CBurnTokensMessage{}));
}

BOOST_AUTO_TEST_SUITE_END()

