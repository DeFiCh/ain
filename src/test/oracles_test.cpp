// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/setup_common.h>
#include <string>
#include <boost/test/unit_test.hpp>

#include <masternodes/oracles.h>

struct OraclesTestingSetup: public BasicTestingSetup {
    const std::string data =
            "\x9c\x52\x4a\xdb\xcf\x56\x11\x12\x2b\x29\x12\x5e\x5d\x35\xd2\xd2"
            "\x22\x81\xaa\xb5\x33\xf0\x08\x32\xd5\x56\xb1\xf9\xea\xe5\x1d\x7d";

    const std::string hex = "7d1de5eaf9b156d53208f033b5aa8122d2d2355d5e12292b121156cfdb4a529c";
    const std::vector<unsigned char> rawVector{data.begin(), data.end()};
};

BOOST_FIXTURE_TEST_SUITE(oracle_tests, OraclesTestingSetup)

    BOOST_AUTO_TEST_CASE(check_compare_operator) {
        COracleId id1{rawVector}, id2{}, id3{rawVector};
        BOOST_ASSERT_MSG(id1 != id2, "compare unequal failed");
        BOOST_ASSERT_MSG(id1 == id3, "compare equal failed");
    }

    BOOST_AUTO_TEST_CASE(serialization_test) {

        CDataStream stream(std::vector<unsigned char>{}, SER_NETWORK, PROTOCOL_VERSION);
        COracleId oracleId{rawVector};
        stream << oracleId;
        COracleId oracleId1{};
        stream >> oracleId1;
        BOOST_ASSERT_MSG(oracleId == oracleId1, "failed to serialize/deserialize COracleId");
    }

    BOOST_AUTO_TEST_CASE(parse_hex_test) {
        COracleId oracleId{rawVector};
        COracleId oracleId2{};
        BOOST_ASSERT_MSG(oracleId2.parseHex(hex), "failed to parse hex value");
        BOOST_ASSERT_MSG(oracleId2 == oracleId, "hex value parsed incorrectly");
    }

BOOST_AUTO_TEST_SUITE_END()
