#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test RPC stats."""

import time

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
)
from test_framework.authproxy import JSONRPCException

class RPCstats(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50'],
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-rpcstats=0'],
        ]

    @DefiTestFramework.rollback
    def run_test(self):
        self.nodes[0].generate(101)

        self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].listunspent()
        time.sleep(1) # sleep to get different timestamp
        self.nodes[0].listunspent()

        listrpcstats = self.nodes[0].listrpcstats()
        assert(any(elem for elem in listrpcstats if elem["name"] == "getnewaddress"))
        assert(any(elem for elem in listrpcstats if elem["name"] == "listunspent"))

        getrpcstats = self.nodes[0].getrpcstats("listunspent")
        assert_equal(getrpcstats["name"], "listunspent")
        assert_equal(getrpcstats["count"], 2)

        # test history's circular buffer of 5 elements
        [historyEntry1, historyEntry2] = getrpcstats["history"]
        for _ in range(0, 4):
            time.sleep(1)
            self.nodes[0].listunspent()

        getrpcstats = self.nodes[0].getrpcstats("listunspent")
        assert_equal(getrpcstats["count"], 6)
        assert(historyEntry1 not in getrpcstats["history"])
        assert_equal(getrpcstats["history"][0], historyEntry2)

        try:
            self.nodes[0].getrpcstats("WRONGCMD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No stats for this command." in errorString)

        try:
            self.nodes[1].getrpcstats("listunspent")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Rpcstats is desactivated." in errorString)

        try:
            self.nodes[1].listrpcstats()
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Rpcstats is desactivated." in errorString)

if __name__ == '__main__':
    RPCstats().main ()
