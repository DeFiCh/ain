#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from feature_poolswap import PoolPairTest

if __name__ == '__main__':
    # testing any src addresses RPC
    poolPairTest = PoolPairTest()
    poolPairTest.testMode = "any"
    poolPairTest.main()

