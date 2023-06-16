#include <chainparams.h>
#include <masternodes/masternodes.h>
#include <masternodes/params.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(dip1fork_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(blockreward_dfip1)
{
    const CScript SCRIPT_PUB{CScript(OP_TRUE)};
    const Consensus::Params & consensus = Params().GetConsensus();
    auto height = consensus.AMKHeight;

    CMutableTransaction coinbaseTx{};

    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = SCRIPT_PUB;
    coinbaseTx.vout[0].nValue = GetBlockSubsidy(height, consensus);
    coinbaseTx.vin[0].scriptSig = CScript() << height << OP_0;

    {   // check on pre-AMK height:
        CCustomCSView mnview(*pcustomcsview.get());
        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(coinbaseTx), height-1, 0, consensus);
        BOOST_CHECK(res.ok);
    }
    {   // try to generate wrong tokens
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout[0].nTokenId.v = 1;
        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-wrong-tokens");
    }
    {   // do not pay foundation reward at all
        CCustomCSView mnview(*pcustomcsview.get());
        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(coinbaseTx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-foundation-reward");
    }
    {   // try to pay foundation reward slightly less than expected
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[1].scriptPubKey = DeFiParams().GetConsensus().foundationShareScript;
        tx.vout[1].nValue = GetBlockSubsidy(height, consensus) * DeFiParams().GetConsensus().foundationShareDFIP1 / COIN -1;
        tx.vout[0].nValue -= tx.vout[1].nValue;

        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-foundation-reward");
    }
    {   // pay proper foundation reward, but not subtract consensus share
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[1].scriptPubKey = DeFiParams().GetConsensus().foundationShareScript;
        tx.vout[1].nValue = GetBlockSubsidy(height, consensus) * DeFiParams().GetConsensus().foundationShareDFIP1 / COIN;
        tx.vout[0].nValue -= tx.vout[1].nValue;

        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-amount");
    }
    {   // at least, all is ok
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[1].scriptPubKey = DeFiParams().GetConsensus().foundationShareScript;
        CAmount const baseSubsidy = GetBlockSubsidy(height, consensus);
        tx.vout[1].nValue = baseSubsidy * DeFiParams().GetConsensus().foundationShareDFIP1 / COIN;
        tx.vout[0].nValue -= tx.vout[1].nValue;
        tx.vout[0].nValue -= baseSubsidy * DeFiParams().GetConsensus().nonUtxoBlockSubsidies.at(CommunityAccountType::IncentiveFunding) / COIN;
        tx.vout[0].nValue -= baseSubsidy * DeFiParams().GetConsensus().nonUtxoBlockSubsidies.at(CommunityAccountType::AnchorReward) / COIN;

        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(res.ok);
    }
}


BOOST_AUTO_TEST_CASE(blockreward_dfip8)
{
    const CScript SCRIPT_PUB{CScript(OP_TRUE)};
    Consensus::Params consensus = Params().GetConsensus();
    consensus.EunosHeight = 10000000;
    consensus.GrandCentralHeight = 10000001;
    auto height = consensus.EunosHeight;
    CAmount blockReward = GetBlockSubsidy(height, consensus);

    CMutableTransaction coinbaseTx{};

    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = SCRIPT_PUB;
    coinbaseTx.vout[0].nValue = GetBlockSubsidy(height, consensus);
    coinbaseTx.vin[0].scriptSig = CScript() << height << OP_0;

    {   // do not pay foundation reward at all
        CCustomCSView mnview(*pcustomcsview.get());
        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(coinbaseTx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-foundation-reward");
    }
    {   // try to pay foundation reward slightly less than expected
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[0].nValue = CalculateCoinbaseReward(blockReward, DeFiParams().GetConsensus().dist.masternode);
        tx.vout[1].scriptPubKey = DeFiParams().GetConsensus().foundationShareScript;
        tx.vout[1].nValue = CalculateCoinbaseReward(blockReward, DeFiParams().GetConsensus().dist.community) - 1;
        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-foundation-reward");
    }
    {   // try to pay foundation reward slightly more than expected
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[0].nValue = CalculateCoinbaseReward(blockReward, DeFiParams().GetConsensus().dist.masternode);
        tx.vout[1].scriptPubKey = DeFiParams().GetConsensus().foundationShareScript;
        tx.vout[1].nValue = CalculateCoinbaseReward(blockReward, DeFiParams().GetConsensus().dist.community) + 1;
        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-amount");
    }
    {   // Try and pay staker too much
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[0].nValue = CalculateCoinbaseReward(blockReward, DeFiParams().GetConsensus().dist.masternode) + 1;
        tx.vout[1].scriptPubKey = DeFiParams().GetConsensus().foundationShareScript;
        tx.vout[1].nValue = CalculateCoinbaseReward(blockReward, DeFiParams().GetConsensus().dist.community);
        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-amount");
    }
    {   // Test everything is okay
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[0].nValue = CalculateCoinbaseReward(blockReward, DeFiParams().GetConsensus().dist.masternode);
        tx.vout[1].scriptPubKey = DeFiParams().GetConsensus().foundationShareScript;
        tx.vout[1].nValue = CalculateCoinbaseReward(blockReward, DeFiParams().GetConsensus().dist.community);
        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(res.ok);
    }
}

BOOST_AUTO_TEST_CASE(blockreward_dfip8_reductions)
{
    Consensus::Params consensus = Params().GetConsensus();
    consensus.EunosHeight = 10000000;

    auto GetReductionsHeight = [consensus](const uint32_t reductions)
    {
        return consensus.EunosHeight + (reductions * DeFiParams().GetConsensus().emissionReductionPeriod);
    };

    // Test coinbase rewards reduction 0
    {
        auto blockSubsidy = GetBlockSubsidy(GetReductionsHeight(0), consensus);
        BOOST_CHECK_EQUAL(blockSubsidy, 40504000000);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.masternode), 13499983200);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.community), 1988746400);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.anchor), 8100800);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.liquidity), 10308268000);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.loan), 9996387200);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.options), 4001795200);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.unallocated), 700719200);
    }

    // Test coinbase rewards reduction 1
    {
        auto blockSubsidy = GetBlockSubsidy(GetReductionsHeight(1), consensus);
        BOOST_CHECK_EQUAL(blockSubsidy, 39832443680);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.masternode), 13276153478);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.community), 1955772984);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.anchor), 7966488);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.liquidity), 10137356916);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.loan), 9830647100);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.options), 3935445435);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.unallocated), 689101275);
    }

    // Test coinbase rewards reduction 100
    {
        auto blockSubsidy = GetBlockSubsidy(GetReductionsHeight(100), consensus);
        BOOST_CHECK_EQUAL(blockSubsidy, 7610296073);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.masternode), 2536511681);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.community), 373665537);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.anchor), 1522059);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.liquidity), 1936820350);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.loan), 1878221070);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.options), 751897252);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.unallocated), 131658122);
    }

    // Test coinbase rewards reduction 1000
    {
        auto blockSubsidy = GetBlockSubsidy(GetReductionsHeight(1000), consensus);
        BOOST_CHECK_EQUAL(blockSubsidy, 2250);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.masternode), 749);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.community), 110);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.anchor), 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.liquidity), 572);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.loan), 555);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.options), 222);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.unallocated), 38);
    }

    // Test coinbase rewards reduction 1251
    {
        auto blockSubsidy = GetBlockSubsidy(GetReductionsHeight(1251), consensus);
        BOOST_CHECK_EQUAL(blockSubsidy, 60);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.masternode), 19);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.community), 2);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.anchor), 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.liquidity), 15);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.loan), 14);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.options), 5);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.unallocated), 1);
    }

    // Test coinbase rewards reduction 1252
    {
        auto blockSubsidy = GetBlockSubsidy(GetReductionsHeight(1252), consensus);
        BOOST_CHECK_EQUAL(blockSubsidy, 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.masternode), 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.community), 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.anchor), 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.liquidity), 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.loan), 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.options), 0);
        BOOST_CHECK_EQUAL(CalculateCoinbaseReward(blockSubsidy, DeFiParams().GetConsensus().dist.unallocated), 0);
    }
}

BOOST_AUTO_TEST_SUITE_END()
