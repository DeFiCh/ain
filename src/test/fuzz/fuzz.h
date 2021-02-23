// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_TEST_FUZZ_FUZZ_H
#define DEFI_TEST_FUZZ_FUZZ_H

#include <stdint.h>
#include <vector>


void test_one_input(std::vector<uint8_t> buffer);

#endif // DEFI_TEST_FUZZ_FUZZ_H
