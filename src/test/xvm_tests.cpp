// Copyright (c) 2023 DeFiChain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <test/setup_common.h>
#include <boost/test/unit_test.hpp>
#include <masternodes/mn_checks.h>

BOOST_FIXTURE_TEST_SUITE(xvm_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(xvm_test_case_1)
{
    auto zero = uint256S("0x0");
    auto one = uint256S("0x1");
    auto oneArr = uint256S("0x1").GetByteArray();
    auto oneVec = std::vector<uint8_t>(oneArr.begin(), oneArr.end());
    auto oneVecReversed = std::vector<uint8_t>(oneArr.begin(), oneArr.end());
    std::reverse(oneVecReversed.begin(), oneVecReversed.end());

    auto twoArr20 = EvmAddressRaw{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2};
    auto toString = [](EvmAddressRaw arr) {
        std::ostringstream os;
        for (int i: arr) {
            os << i;
        }
        return os.str();
    };

    XVM xvm;
    xvm.evm.blockHash = one;
    xvm.evm.beneficiary = twoArr20;

    auto s = xvm.ToScript();
    auto xvm2 = XVM::TryFrom(s);

    XVM xvm3;
    xvm3.evm.blockHash = uint256(oneVec);

    XVM xvm4;
    xvm4.evm.blockHash = uint256(oneVecReversed);

    for (const auto& [result, expected]: std::vector<std::tuple<std::string, std::string>> { 
        { zero.GetHex(), "0000000000000000000000000000000000000000000000000000000000000000" },
        { one.GetHex(), "0000000000000000000000000000000000000000000000000000000000000001" },
        { xvm.evm.blockHash.GetHex(), "0000000000000000000000000000000000000000000000000000000000000001" },
        { xvm2->evm.blockHash.GetHex(), "0000000000000000000000000000000000000000000000000000000000000001" },
        { xvm3.evm.blockHash.GetHex(), "0100000000000000000000000000000000000000000000000000000000000000" },
        { xvm4.evm.blockHash.GetHex(), "0000000000000000000000000000000000000000000000000000000000000001" },
        { toString(twoArr20), "00000000000000000002" },
        { toString(xvm.evm.beneficiary), "00000000000000000002" },
        { toString(xvm2->evm.beneficiary), "00000000000000000002" },
    }) { 
        // BOOST_TEST_MESSAGE("expected: " + expected + ", result: " + result);
        BOOST_CHECK(result == expected);
    };
}

BOOST_AUTO_TEST_SUITE_END()