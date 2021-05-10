// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <test/setup_common.h>
#include <string>
#include <boost/test/unit_test.hpp>

#include <masternodes/operators.h>
#include <masternodes/rpc_customtx.cpp>
#include <rpc/rawtransaction_util.h>
#include <masternodes/masternodes.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptor/transformed.hpp>

struct OperatorTestingSetup : public TestingSetup {
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
};

void PrintOperator(COperator& oprtr) {
    std::cout << oprtr.operatorAddress.GetHex() << " " << oprtr.operatorName << " " << oprtr.operatorURL
        << " " << GetOperatorStateString(oprtr.operatorState)  << std::endl;
}

BOOST_FIXTURE_TEST_SUITE(operator_tests, OperatorTestingSetup)

    BOOST_AUTO_TEST_CASE(check_operatorid_compare_operator) {
        COperatorId id1{rawVector1}, id2{}, id3{rawVector1};
        BOOST_ASSERT_MSG(id1 != id2, "compare unequal failed");
        BOOST_ASSERT_MSG(id1 == id3, "compare equal failed");
    }

    BOOST_AUTO_TEST_CASE(operatorid_serialization_test) {

        CDataStream stream(std::vector<unsigned char>{}, SER_NETWORK, PROTOCOL_VERSION);
        COperatorId operatorId{rawVector1};
        stream << operatorId;
        COperatorId operatorId1{};
        stream >> operatorId1;
        BOOST_ASSERT_MSG(operatorId == operatorId1, "failed to serialize/deserialize COperatorId");
        std::cout << operatorId.GetHex() << std::endl;
        std::cout << operatorId.ToString() << std::endl;

    }

    BOOST_AUTO_TEST_CASE(operator_serialization_test) {
        COperatorId operatorId1{rawVector1};
        COperatorId operatorId2{rawVector2};

        BOOST_ASSERT_MSG(operatorId1 != operatorId2, "Bad data");

        std::vector<unsigned char> tmp{'a', 'b', 'c'};
        CScript operatorAddress1{tmp.begin(), tmp.end()};
        std::string operatorName = "testoperator1";
        std::string operatorURL = "testoperator1url";
        OperatorState operatorState = OperatorState::DRAFT;

        CCreateOperatorMessage msg{operatorAddress1, operatorName, operatorURL, operatorState};
        COperator oprtr{msg};

        CCustomCSView mnview(*pcustomcsview);
        auto res = mnview.CreateOperator(operatorId1, oprtr);
        BOOST_ASSERT_MSG(res.ok, res.msg.c_str());

        res = mnview.CreateOperator(operatorId2, oprtr);
        BOOST_ASSERT_MSG(res.ok, res.msg.c_str());
    }

    BOOST_AUTO_TEST_CASE(update_operator_test) {

        COperatorId operatorId1{rawVector1};

        std::vector<unsigned char> tmp{'a', 'b', 'c'};
        CScript operatorAddress1{tmp.begin(), tmp.end()};
        std::string operatorName = "testoperator1";
        std::string operatorURL = "testoperator1url";
        OperatorState operatorState = OperatorState::DRAFT;

        CCreateOperatorMessage msg1{operatorAddress1, operatorName, operatorURL, operatorState};
        COperator oprtr1{msg1};

        //new operator
        std::string operatorName2 = "testoperator11";
        std::string operatorURL2 = "testoperator1url";
        OperatorState operatorState2 = OperatorState::ACTIVE;

        CCreateOperatorMessage msg2{operatorAddress1, operatorName2, operatorURL2, operatorState2};
        COperator oprtr2{msg2};

        CCustomCSView mnview(*pcustomcsview);
        auto res = mnview.CreateOperator(operatorId1, oprtr1);
        BOOST_ASSERT_MSG(res.ok, res.msg.c_str());

        //update
        res = mnview.UpdateOperator(operatorId1, oprtr2);
        BOOST_ASSERT_MSG(res.ok, res.msg.c_str());

        //retrive operator from db
        auto operatorData = mnview.GetOperatorData(operatorId1);
        BOOST_ASSERT_MSG(operatorData.ok, operatorData.msg.c_str());

        //PrintOperator(operatorData.val.get());

        //compare
        BOOST_REQUIRE_EQUAL(operatorData.val->operatorAddress.GetHex(), oprtr2.operatorAddress.GetHex());
        BOOST_REQUIRE_EQUAL(operatorData.val->operatorName, oprtr2.operatorName);
        BOOST_REQUIRE_EQUAL(operatorData.val->operatorState, oprtr2.operatorState);
        BOOST_REQUIRE_EQUAL(operatorData.val->operatorURL, oprtr2.operatorURL);

    }

BOOST_AUTO_TEST_SUITE_END()
