// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/setup_common.h>
#include <string>
#include <boost/test/unit_test.hpp>

#include <masternodes/oracles.h>
#include <rpc/rawtransaction_util.h>
#include <masternodes/masternodes.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptor/transformed.hpp>

struct OraclesTestingSetup: public TestingSetup {
    const std::string data1 =
            "\x9c\x52\x4a\xdb\xcf\x56\x11\x12\x2b\x29\x12\x5e\x5d\x35\xd2\xd2"
            "\x22\x81\xaa\xb5\x33\xf0\x08\x32\xd5\x56\xb1\xf9\xea\xe5\x1d\x7d";

    const std::string data2 =
            "\x9c\x52\x4a\xac\xcf\x56\x11\x12\x2b\x29\x12\x5e\x5d\x35\xd2\xd2"
            "\x22\x81\xaa\xb5\x24\xf0\x08\x32\xd5\x56\xb1\xf9\xea\xe5\x1d\x7d";

    const std::string hex1 = "7d1de5eaf9b156d53208f033b5aa8122d2d2355d5e12292b121156cfdb4a529c";
    const std::vector<unsigned char> rawVector1{data1.begin(), data1.end()};
    const std::vector<unsigned char> rawVector2{data2.begin(), data2.end()};
    const std::string address1 = "mhWzxsS5aDfmNY2EpPuM2xQZx7Ju3yjkQ4";
    std::string JoinOracles(const std::vector<COracleId>& oracles) {
        auto list =  boost::algorithm::join(
                oracles | boost::adaptors::transformed(
                        [](const COracleId& x) -> std::string {
                            return x.GetHex();
                        }),
                ", ");
        return "[" + list + "]";
    }
};

BOOST_FIXTURE_TEST_SUITE(oracle_tests, OraclesTestingSetup)

    BOOST_AUTO_TEST_CASE(check_oracleid_compare_operator) {
        COracleId id1{rawVector1}, id2{}, id3{rawVector1};
        BOOST_ASSERT_MSG(id1 != id2, "compare unequal failed");
        BOOST_ASSERT_MSG(id1 == id3, "compare equal failed");
    }

    BOOST_AUTO_TEST_CASE(oracleid_serialization_test) {

        CDataStream stream(std::vector<unsigned char>{}, SER_NETWORK, PROTOCOL_VERSION);
        COracleId oracleId{rawVector1};
        stream << oracleId;
        COracleId oracleId1{};
        stream >> oracleId1;
        BOOST_ASSERT_MSG(oracleId == oracleId1, "failed to serialize/deserialize COracleId");
    }

    BOOST_AUTO_TEST_CASE(oracleid_parse_hex_test) {
        COracleId oracleId{rawVector1};
        COracleId oracleId2{};
        BOOST_ASSERT_MSG(oracleId2.parseHex(hex1), "failed to parse hex value");
        BOOST_ASSERT_MSG(oracleId2 == oracleId, "hex value parsed incorrectly");
    }

    BOOST_AUTO_TEST_CASE(oracle_serialization_test) {
        COracleId oracleId1{rawVector1};
        COracleId oracleId2{rawVector2};

        BOOST_ASSERT_MSG(oracleId1 != oracleId2, "bad test data");

        std::cout << "press enter" << std::endl;
        std::cin.get();
        std::vector<unsigned char> tmp{'a', 'b', 'c'};
        CScript oracleAddress1{tmp.begin(), tmp.end()};
        uint8_t weightage = 15;
        std::set<DCT_ID> availableTokens = {{1}, {2}};
        CAppointOracleMessage msg{oracleAddress1, weightage, availableTokens};
        COracle oracle{oracleId1, msg};
        std::cerr << "testpoint\n";
        CCustomCSView mnview(*pcustomcsview);
        auto res = mnview.AppointOracle(oracleId1, oracle);
        BOOST_ASSERT_MSG(res.ok, res.msg.c_str());

        res = mnview.AppointOracle(oracleId2, oracle);
        BOOST_ASSERT_MSG(res.ok, res.msg.c_str());

        auto allOracleIds = mnview.GetAllOracleIds();
        BOOST_ASSERT_MSG((allOracleIds == std::vector<COracleId>{oracleId1, oracleId2}),
                "wrong list of oracles");

        std::cout << "all oracles = " << JoinOracles(allOracleIds) << std::endl;
    }

BOOST_AUTO_TEST_SUITE_END()
