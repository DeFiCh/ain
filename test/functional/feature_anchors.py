#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify basic MN creation and resign
"""

import time

from test_framework.test_framework import BitcoinTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi, disconnect_nodes

class AnchorsTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-checkblockindex=0" ],
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-checkblockindex=0" ],
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-checkblockindex=0" ],
        ]
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_all()

    def run_test(self):
        assert_equal(len(self.nodes[0].mn_list()), 8)

        disconnect_nodes(self.nodes[0], 1)
        self.nodes[0].generate(15)

        print ("Anc at start: ", self.nodes[0].spv_listanchors())


        txinfo = self.nodes[0].spv_createanchor("mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")
        print (txinfo)
        self.nodes[0].spv_setlastheight(10)
        print ("Anc 0: ", self.nodes[0].spv_listanchors())


        self.nodes[1].generate(30)
        # self.nodes[1].spv_setlastheight(10)
        # txinfo = self.nodes[1].spv_createanchor("mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")
        # print (txinfo)
        print ("Anc 1: ", self.nodes[1].spv_listanchors())

        # self.sync_all()

        # disconnect_nodes(self.nodes[0], 1)

        print ("0: ", self.nodes[0].getblockcount())
        print ("1: ", self.nodes[1].getblockcount())


        connect_nodes_bi(self.nodes, 0, 1)

        # self.sync_all()
        time.sleep(2)
        print ("0: ", self.nodes[0].getblockcount())
        print ("1: ", self.nodes[1].getblockcount())

        txinfo = self.nodes[1].spv_sendrawtx(txinfo['txHex'])
        self.nodes[1].spv_setlastheight(10)
        print ("Anc 1: ", self.nodes[1].spv_listanchors())
        input ("attach to process..")
        self.nodes[1].generate(1)

        time.sleep(2)
        print ("0: ", self.nodes[0].getblockcount())
        print ("1: ", self.nodes[1].getblockcount())





        # time.sleep(10)


if __name__ == '__main__':
    AnchorsTest ().main ()
