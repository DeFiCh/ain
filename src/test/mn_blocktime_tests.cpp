#include <test/setup_common.h>

#include <masternodes/masternodes.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(mn_blocktime_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(retrieve_last_time)
{
    CCustomCSView mnview(*pcustomcsview.get());

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
    mnview.CreateMasternode(mnId, mn);

    // Add time records
    mnview.SetMasternodeLastBlockTime(minter, 100, 1000);
    mnview.SetMasternodeLastBlockTime(minter, 200, 2000);
    mnview.SetMasternodeLastBlockTime(minter, 300, 3000);

    // Make sure result is found and returns result previous to 200
    const auto time200 = mnview.GetMasternodeLastBlockTime(minter, 200);
    BOOST_CHECK_EQUAL(*time200, 1000);

    // Make sure result is found and returns result previous to 200
    const auto time300 = mnview.GetMasternodeLastBlockTime(minter, 300);
    BOOST_CHECK_EQUAL(*time300, 2000);

    // For max value we expect the last result
    const auto timeMax = mnview.GetMasternodeLastBlockTime(minter, std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_EQUAL(*timeMax, 3000);
}

BOOST_AUTO_TEST_SUITE_END()
