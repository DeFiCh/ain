#include <test/setup_common.h>

#include <masternodes/masternodes.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(mn_timelock_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(GetTimelockToString)
{
    BOOST_CHECK_EQUAL(CMasternode::GetTimelockToString(CMasternode::ZEROYEAR), "NONE");
    BOOST_CHECK_EQUAL(CMasternode::GetTimelockToString(CMasternode::FIVEYEAR), "FIVEYEARTIMELOCK");
    BOOST_CHECK_EQUAL(CMasternode::GetTimelockToString(CMasternode::TENYEAR), "TENYEARTIMELOCK");

    // from unit16_t
    BOOST_CHECK_EQUAL(CMasternode::GetTimelockToString(static_cast<CMasternode::TimeLock>(uint16_t {0})), "NONE");
    BOOST_CHECK_EQUAL(CMasternode::GetTimelockToString(static_cast<CMasternode::TimeLock>(uint16_t {260})), "FIVEYEARTIMELOCK");
    BOOST_CHECK_EQUAL(CMasternode::GetTimelockToString(static_cast<CMasternode::TimeLock>(uint16_t {520})), "TENYEARTIMELOCK");
}

BOOST_AUTO_TEST_SUITE_END()
