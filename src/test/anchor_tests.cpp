#include <chainparams.h>
#include <masternodes/anchors.h>
#include <masternodes/masternodes.h>
#include <spv/spv_wrapper.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>


struct SpvTestingSetup : public TestingSetup {
    SpvTestingSetup()
        : TestingSetup(CBaseChainParams::REGTEST)
    {
        spv::pspv = MakeUnique<spv::CFakeSpvWrapper>();
    }
};

BOOST_FIXTURE_TEST_SUITE(anchors, SpvTestingSetup)

BOOST_AUTO_TEST_CASE(check_a)
{
    CAnchor anc;
//    { uint256(), 0, uint256(), {}, {}, CKeyID(), 1 };
//    uint256 previousAnchor;
//    THeight height;
//    uint256 blockHash;
//    CTeam nextTeam;
//    std::vector<Signature> sigs;
//    CKeyID rewardKeyID;
//    char rewardKeyType;

    LOCK(cs_main);
    panchors->AddAnchor(anc, uint256(), 15);
//    uint256 masternodeID = testMasternodeKeys.begin()->first;
//    std::map<uint256, TestMasternodeKeys>::const_iterator pos = testMasternodeKeys.find(masternodeID);
//    BOOST_CHECK(pos != testMasternodeKeys.end());
//    CKey minterKey = pos->second.operatorKey;
//    uint64_t mintedBlocks = 0;
//    std::vector<CBlockHeader> criminalsBlockHeaders = GenerateTwoCriminalsHeaders(minterKey, mintedBlocks, masternodeID);

//    pmasternodesview->WriteMintedBlockHeader(masternodeID, mintedBlocks, criminalsBlockHeaders[0].GetHash(), criminalsBlockHeaders[0], false);
//    pmasternodesview->WriteMintedBlockHeader(masternodeID, mintedBlocks, criminalsBlockHeaders[1].GetHash(), criminalsBlockHeaders[1], false);
//    BOOST_CHECK(!pmasternodesview->CheckDoubleSign(criminalsBlockHeaders[0], criminalsBlockHeaders[1]));

//    std::map<uint256, CBlockHeader> blockHeaders;
//    BOOST_CHECK(pmasternodesview->FindMintedBlockHeader(masternodeID, mintedBlocks, blockHeaders, false));
//    BOOST_CHECK(blockHeaders.size() == 2);
}


BOOST_AUTO_TEST_SUITE_END()
