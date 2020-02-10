#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify basic MN creation and resign
"""

import time

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, \
    connect_nodes_bi, disconnect_nodes

class AnchorAuthsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [
            [ "-dummypos=1", "-spv=1", "-fakespv=1"],
            [ "-dummypos=1", "-spv=1", "-fakespv=1"],
            [ "-dummypos=1", "-spv=1", "-fakespv=1"],
        ]
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_all()

    def dumphashes(self, nodes=None, block = None):
        if nodes is None:
            nodes = range(self.num_nodes)
        for i in nodes:
            bl = self.nodes[i].getblockcount() if block is None else block
            print ("Node%d: [%d] %s" % (i, bl, self.nodes[i].getblockhash(bl)))

    def dumpheights(self):
        print ("Heights:", self.nodes[0].getblockcount(), "\t", self.nodes[1].getblockcount(), "\t", self.nodes[2].getblockcount())
        # pass

    def dumpauths(self, nodes=None, block = None):
        if nodes is None:
            nodes = range(self.num_nodes)
        for i in nodes:
            print ("Node%d: %s" % (i, self.nodes[i].spv_listanchorauths()))

    def run_test(self):
        assert_equal(len(self.nodes[0].listmasternodes()), 8)
        disconnect_nodes(self.nodes[0], 1)

        # no auths yet
        print ("Node0:")
        self.nodes[0].generate(29)
        auths = self.nodes[0].spv_listanchorauths()
        assert_equal(len(auths), 0)

        # auth should appear
        self.nodes[0].generate(1)
        auths = self.nodes[0].spv_listanchorauths()
        assert_equal(len(auths), 1)
        assert_equal(auths[0]['blockHeight'], 15)
        assert_equal(auths[0]['blockHash'], self.nodes[0].getblockhash(15))
        assert_equal(auths[0]['signers'], 1)
        self.nodes[0].generate(1) # to be longer than chain of Node1

        self.dumpheights()
        self.dumphashes()
        self.dumpauths()

        print ("Node1:")
        assert_equal(len(self.nodes[1].spv_listanchorauths()), 0)
        self.nodes[1].generate(30)
        time.sleep(1)
        self.dumphashes()
        self.dumpauths()

        assert_equal(len(self.nodes[1].spv_listanchorauths()), 1)

        self.dumpheights()
        self.dumpauths()

        connect_nodes_bi(self.nodes, 0, 1)

        print ("After connect:")
        time.sleep(1)

        self.dumpheights()
        self.dumphashes()
        self.dumpauths()

        # TODO: still has issue on auth old blocks when sidechain switching
        # auths = self.nodes[1].spv_listanchorauths()
        # assert_equal(len(auths), 1)
        # assert_equal(auths[0]['blockHeight'], auths[1]['blockHeight'])
        # assert(auths[0]['blockHeight'] != auths[1]['blockHeight'])

if __name__ == '__main__':
    AnchorAuthsTest ().main ()
