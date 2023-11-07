// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <util/strencodings.h>
#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(utf8_string_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(check_for_valid_utf8_strings)
{
    std::string test1 = "abcdefghijklmnopqrstuvwxyz1234567890~_= ^+%]{}";
    std::string test2 = "abcdeàèéìòù";
    std::string test3 = "😁 Beaming Face With Smiling Eyes";
    std::string test4 = "Slightly Smiling Face 🙂";
    std::string test5 = "🤣🤣🤣 Rolling on the Floor Laughing";
    std::string test6 = "🤩🤩🤩 Star-🤩Struck 🤩🤩";
    std::string test7 = "Left till here away at to whom past. Feelings laughing at no wondered repeated provided finished."
        " It acceptance thoroughly my advantages everything as. Are projecting inquietude affronting preference saw who."
        " Marry of am do avoid ample as. Old disposal followed she ignorant desirous two has. Called played entire roused"
        " though for one too. He into walk roof made tall cold he. Feelings way likewise addition wandered contempt bed indulged.";

    BOOST_CHECK(check_is_valid_utf8(test1));
    BOOST_CHECK(check_is_valid_utf8(test2));
    BOOST_CHECK(check_is_valid_utf8(test3));
    BOOST_CHECK(check_is_valid_utf8(test4));
    BOOST_CHECK(check_is_valid_utf8(test5));
    BOOST_CHECK(check_is_valid_utf8(test6));
    BOOST_CHECK(check_is_valid_utf8(test7));
}

BOOST_AUTO_TEST_CASE(check_for_invalid_utf8_strings)
{
    std::string smiling_face = "😁";
    std::string laughing_face = "🤣";
    std::string star_struck_face = "🤩";
    std::string test1 = smiling_face.substr(0, 1) +  " Beaming Face With Smiling Eyes";
    std::string test2 = laughing_face.substr(0, 3) + laughing_face.substr(0, 2) + laughing_face.substr(0, 1) + " Rolling on the Floor Laughing";
    std::string test3 = star_struck_face.substr(0, 1) + "🤩🤩 Star-🤩Struck 🤩🤩";

    BOOST_CHECK(!check_is_valid_utf8(test1));
    BOOST_CHECK(!check_is_valid_utf8(test2));
    BOOST_CHECK(!check_is_valid_utf8(test3));
}

BOOST_AUTO_TEST_SUITE_END()
