// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <test/setup_common.h>
#include <string>
#include <boost/test/unit_test.hpp>

#include <rust/evm/sputnikvm.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptor/transformed.hpp>

BOOST_AUTO_TEST_CASE(EVM_default)
{
    sputnikvm_header_params params = sputnikvm_default_header_params();

    BOOST_CHECK_EQUAL(strcmp((const char*)params.beneficiary.data, (const char*)address_from_str("0x0000000000000000000000000000000000000000").data), 0);
    BOOST_CHECK_EQUAL(params.timestamp, 0ULL);
    BOOST_CHECK_EQUAL(strcmp((const char*)params.number.data, (const char*)u256_from_str("0x0000000000000000000000000000000000000000").data), 0);
    BOOST_CHECK_EQUAL(strcmp((const char*)params.difficulty.data, (const char*)u256_from_str("0x0000000000000000000000000000000000000000").data), 0);
    BOOST_CHECK_EQUAL(strcmp((const char*)params.gas_limit.data, (const char*)gas_from_str("0x0000000000000000000000000000000000000000").data), 0);
}
