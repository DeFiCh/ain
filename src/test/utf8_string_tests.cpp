// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <ffi/ffiexports.h>
#include <ffi/ffihelpers.h>
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

    // Check UTF-8 validity with Rust FFI
    CrossBoundaryResult result;
    auto test1_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test1));
    BOOST_CHECK(result.ok);
    BOOST_CHECK(test1_res == test1);
    auto test2_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test2));
    BOOST_CHECK(result.ok);
    BOOST_CHECK(test2_res == test2);
    auto test3_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test3));
    BOOST_CHECK(result.ok);
    BOOST_CHECK(test3_res == test3);
    auto test4_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test4));
    BOOST_CHECK(result.ok);
    BOOST_CHECK(test4_res == test4);
    auto test5_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test5));
    BOOST_CHECK(result.ok);
    BOOST_CHECK(test5_res == test5);
    auto test6_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test6));
    BOOST_CHECK(result.ok);
    BOOST_CHECK(test6_res == test6);
    auto test7_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test7));
    BOOST_CHECK(result.ok);
    BOOST_CHECK(test7_res == test7);

    // Check UTF
}

BOOST_AUTO_TEST_CASE(check_for_invalid_utf8_strings)
{
    std::string smiling_face = "😁";
    std::string laughing_face = "🤣";
    std::string star_struck_face = "🤩";
    std::string test1 = smiling_face.substr(0, 1) +  " Beaming Face With Smiling Eyes";
    std::string test2 = laughing_face.substr(0, 3) + laughing_face.substr(0, 2) + laughing_face.substr(0, 1) + " Rolling on the Floor Laughing";
    std::string test3 = star_struck_face.substr(0, 1) + "🤩🤩 Star-🤩Struck 🤩🤩";

    // Check UTF-8 validity with Rust FFI
    CrossBoundaryResult result;
    auto test1_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test1));
    BOOST_CHECK(!result.ok);
    BOOST_CHECK(test1_res == "");
    auto test2_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test2));
    BOOST_CHECK(!result.ok);
    BOOST_CHECK(test2_res == "");
    auto test3_res = rs_try_from_utf8(result, ffi_from_string_to_slice(test3));
    BOOST_CHECK(!result.ok);
    BOOST_CHECK(test3_res == "");
}

BOOST_AUTO_TEST_SUITE_END()
