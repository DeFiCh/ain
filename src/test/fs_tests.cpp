// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.
//
#include <fs.h>
#include <test/setup_common.h>
#include <util/system.h>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <ios>
#include <string>

BOOST_FIXTURE_TEST_SUITE(fs_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(fsbridge_pathtostring)
{
    std::string u8_str = "fs_tests_₿_🏃";
    BOOST_CHECK_EQUAL(fs::PathToString(fs::PathFromString(u8_str)), u8_str);
    BOOST_CHECK_EQUAL(fs::u8path(u8_str).u8string(), u8_str);
    BOOST_CHECK_EQUAL(fs::PathFromString(u8_str).u8string(), u8_str);
    BOOST_CHECK_EQUAL(fs::PathToString(fs::u8path(u8_str)), u8_str);
#ifndef WIN32
    // On non-windows systems, verify that arbitrary byte strings containing
    // invalid UTF-8 can be round tripped successfully with PathToString and
    // PathFromString. On non-windows systems, paths are just byte strings so
    // these functions do not do any encoding. On windows, paths are Unicode,
    // and these functions do encoding and decoding, so the behavior of this
    // test would be undefined.
    std::string invalid_u8_str = "\xf0";
    BOOST_CHECK_EQUAL(invalid_u8_str.size(), 1);
    BOOST_CHECK_EQUAL(fs::PathToString(fs::PathFromString(invalid_u8_str)), invalid_u8_str);
#endif
}

BOOST_AUTO_TEST_CASE(fsbridge_stem)
{
    std::string test_filename = "fs_tests_₿_🏃.dat";
    std::string expected_stem = "fs_tests_₿_🏃";
    BOOST_CHECK_EQUAL(fs::PathToString(fs::PathFromString(test_filename).stem()), expected_stem);
}

BOOST_AUTO_TEST_CASE(fsbridge_fstream)
{
    fs::path tmpfolder = GetDataDir();
    // tmpfile1 should be the same as tmpfile2
    fs::path tmpfile1 = tmpfolder / fs::u8path("fs_tests_₿_🏃");
    fs::path tmpfile2 = tmpfolder / fs::u8path("fs_tests_₿_🏃");
    {
        std::ofstream file(tmpfile1);
        file << "defi";
    }
    {
        std::ifstream file(tmpfile2);
        std::string input_buffer;
        file >> input_buffer;
        BOOST_CHECK_EQUAL(input_buffer, "defi");
    }
    {
        std::ifstream file(tmpfile1, std::ios_base::in | std::ios_base::ate);
        std::string input_buffer;
        file >> input_buffer;
        BOOST_CHECK_EQUAL(input_buffer, "");
    }
    {
        std::ofstream file(tmpfile2, std::ios_base::out | std::ios_base::app);
        file << "tests";
    }
    {
        std::ifstream file(tmpfile1);
        std::string input_buffer;
        file >> input_buffer;
        BOOST_CHECK_EQUAL(input_buffer, "defitests");
    }
    {
        std::ofstream file(tmpfile2, std::ios_base::out | std::ios_base::trunc);
        file << "defi";
    }
    {
        std::ifstream file(tmpfile1);
        std::string input_buffer;
        file >> input_buffer;
        BOOST_CHECK_EQUAL(input_buffer, "defi");
    }
    {
        // Join an absolute path and a relative path.
        fs::path p = fsbridge::AbsPathJoin(tmpfolder, fs::u8path("fs_tests_₿_🏃"));
        BOOST_CHECK(p.is_absolute());
        BOOST_CHECK_EQUAL(tmpfile1, p);
    }
    {
        // Join two absolute paths.
        fs::path p = fsbridge::AbsPathJoin(tmpfile1, tmpfile2);
        BOOST_CHECK(p.is_absolute());
        BOOST_CHECK_EQUAL(tmpfile2, p);
    }
    {
        // Ensure joining with empty paths does not add trailing path components.
        BOOST_CHECK_EQUAL(tmpfile1, fsbridge::AbsPathJoin(tmpfile1, ""));
        BOOST_CHECK_EQUAL(tmpfile1, fsbridge::AbsPathJoin(tmpfile1, {}));
    }
}

BOOST_AUTO_TEST_SUITE_END()
