#include <test/setup_common.h>

#include <masternodes/masternodes.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(mn_blocktime_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(retrieve_last_time)
{
    // Create masternode
    CMasternode mn;
    std::vector<unsigned char> vec(20, '1');
    uint160 bytes{vec};
    CKeyID minter(bytes);
    mn.operatorType = 1;
    mn.ownerType = 1;
    mn.operatorAuthAddress = minter;
    mn.ownerAuthAddress = minter;
    uint256 mnId = uint256S("1111111111111111111111111111111111111111111111111111111111111111");

    CCustomCSView mnview(*pcustomcsview.get());
    mnview.CreateMasternode(mnId, mn, 0);

    // Add time records
    mnview.SetMasternodeLastBlockTime(minter, 100, 1000);
    mnview.SetMasternodeLastBlockTime(minter, 200, 2000);
    mnview.SetMasternodeLastBlockTime(minter, 300, 3000);
    mnview.Flush();

    // Make sure result is found and returns result previous to 200
    const auto time200 = pcustomcsview->GetMasternodeLastBlockTime(minter, 200);
    BOOST_CHECK_EQUAL(*time200, 1000);

    // Make sure result is found and returns result previous to 200
    const auto time300 = pcustomcsview->GetMasternodeLastBlockTime(minter, 300);
    BOOST_CHECK_EQUAL(*time300, 2000);

    // For max value we expect the last result
    const auto timeMax = pcustomcsview->GetMasternodeLastBlockTime(minter, std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_EQUAL(*timeMax, 3000);

    // Delete entry
    CCustomCSView mnviewTwo(*pcustomcsview.get());
    mnviewTwo.EraseMasternodeLastBlockTime(mnId, 300);
    mnviewTwo.Flush();

    // Should now return result before deleted entry
    const auto time2001 = pcustomcsview->GetMasternodeLastBlockTime(minter, std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_EQUAL(*time2001, 2000);
}

BOOST_AUTO_TEST_CASE(retrieve_last_time_multi)
{
    // Create masternode
    CMasternode mn;
    std::vector<unsigned char> vec(20, '1');
    uint160 bytes{vec};
    CKeyID minter(bytes);
    mn.operatorType = 1;
    mn.ownerType = 1;
    mn.operatorAuthAddress = minter;
    mn.ownerAuthAddress = minter;
    uint256 mnId = uint256S("1111111111111111111111111111111111111111111111111111111111111111");

    CCustomCSView mnview(*pcustomcsview.get());
    mnview.CreateMasternode(mnId, mn, 0);

    // Add time records
    mnview.SetSubNodesBlockTime(minter, 100, 0, 1000);
    mnview.SetSubNodesBlockTime(minter, 100, 1, 1000);
    mnview.SetSubNodesBlockTime(minter, 100, 2, 1000);
    mnview.SetSubNodesBlockTime(minter, 100, 3, 1000);
    mnview.SetSubNodesBlockTime(minter, 200, 0, 2000);
    mnview.SetSubNodesBlockTime(minter, 200, 1, 2000);
    mnview.SetSubNodesBlockTime(minter, 200, 2, 2000);
    mnview.SetSubNodesBlockTime(minter, 200, 3, 2000);
    mnview.SetSubNodesBlockTime(minter, 300, 0, 3000);
    mnview.SetSubNodesBlockTime(minter, 300, 1, 3000);
    mnview.SetSubNodesBlockTime(minter, 300, 2, 3000);
    mnview.SetSubNodesBlockTime(minter, 300, 3, 3000);
    mnview.Flush();

    // Make sure result is found and returns result previous to 200
    const auto time200 = pcustomcsview->GetSubNodesBlockTime(minter, 200);
    BOOST_CHECK_EQUAL(time200[0], 1000);
    BOOST_CHECK_EQUAL(time200[1], 1000);
    BOOST_CHECK_EQUAL(time200[2], 1000);
    BOOST_CHECK_EQUAL(time200[3], 1000);

    // Make sure result is found and returns result previous to 200
    const auto time300 = pcustomcsview->GetSubNodesBlockTime(minter, 300);
    BOOST_CHECK_EQUAL(time300[0], 2000);
    BOOST_CHECK_EQUAL(time300[1], 2000);
    BOOST_CHECK_EQUAL(time300[2], 2000);
    BOOST_CHECK_EQUAL(time300[3], 2000);

    // For max value we expect the last result
    const auto timeMax = pcustomcsview->GetSubNodesBlockTime(minter, std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_EQUAL(timeMax[0], 3000);
    BOOST_CHECK_EQUAL(timeMax[1], 3000);
    BOOST_CHECK_EQUAL(timeMax[2], 3000);
    BOOST_CHECK_EQUAL(timeMax[3], 3000);

    // Delete entry
    CCustomCSView mnviewTwo(*pcustomcsview.get());
    mnviewTwo.EraseSubNodesLastBlockTime(mnId, 300);
    mnviewTwo.Flush();

    // Should now return result before deleted entry
    const auto time2001 = pcustomcsview->GetSubNodesBlockTime(minter, std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_EQUAL(time2001[0], 2000);
    BOOST_CHECK_EQUAL(time2001[1], 2000);
    BOOST_CHECK_EQUAL(time2001[2], 2000);
    BOOST_CHECK_EQUAL(time2001[3], 2000);
}

BOOST_AUTO_TEST_SUITE_END()
