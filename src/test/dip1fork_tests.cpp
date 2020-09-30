#include <chainparams.h>
#include <masternodes/masternodes.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>


//struct SpvTestingSetup : public TestingSetup {
//    SpvTestingSetup()
//        : TestingSetup(CBaseChainParams::REGTEST)
//    {
//        spv::pspv = MakeUnique<spv::CFakeSpvWrapper>();
//    }
//    ~SpvTestingSetup()
//    {
//        spv::pspv->Disconnect();
//        spv::pspv.reset();
//    }
//};

BOOST_FIXTURE_TEST_SUITE(dip1fork_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(blockreward)
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
        tx.vout[1].scriptPubKey = consensus.foundationShareScript;
        tx.vout[1].nValue = GetBlockSubsidy(height, consensus) * consensus.foundationShareDFIP1 / COIN -1;
        tx.vout[0].nValue -= tx.vout[1].nValue;

        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-foundation-reward");
    }
    {   // pay proper foundation reward, but not subtract consensus share
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[1].scriptPubKey = consensus.foundationShareScript;
        tx.vout[1].nValue = GetBlockSubsidy(height, consensus) * consensus.foundationShareDFIP1 / COIN;
        tx.vout[0].nValue -= tx.vout[1].nValue;

        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(!res.ok && res.dbgMsg == "bad-cb-amount");
    }
    {   // at least, all is ok
        CCustomCSView mnview(*pcustomcsview.get());
        CMutableTransaction tx(coinbaseTx);
        tx.vout.resize(2);
        tx.vout[1].scriptPubKey = consensus.foundationShareScript;
        CAmount const baseSubsidy = GetBlockSubsidy(height, consensus);
        tx.vout[1].nValue = baseSubsidy * consensus.foundationShareDFIP1 / COIN;
        tx.vout[0].nValue -= tx.vout[1].nValue;
        tx.vout[0].nValue -= baseSubsidy * consensus.nonUtxoBlockSubsidies.at(CommunityAccountType::IncentiveFunding) / COIN;
        tx.vout[0].nValue -= baseSubsidy * consensus.nonUtxoBlockSubsidies.at(CommunityAccountType::AnchorReward) / COIN;

        Res res = ApplyGeneralCoinbaseTx(mnview, CTransaction(tx), height, 0, consensus);
        BOOST_CHECK(res.ok);
    }
}


BOOST_AUTO_TEST_SUITE_END()
