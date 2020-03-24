#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- test for criminality of masternodes
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, \
    connect_nodes_bi

import os
import shutil


class CriminalsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [
            [ "-dummypos=0", "-criminals=0"],
            [ "-dummypos=0", "-criminals=1"],
            [ "-dummypos=0", "-criminals=0"],
            # [ "-dummypos=1", "-spv=1", "-fakespv=1", "-txindex=1", "-anchorquorum=2"],
        ]
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

        # for i in range(self.num_nodes - 1):
        #     connect_nodes_bi(self.nodes, i, i + 1)
        # self.sync_all()

    def dumphashes(self, nodes=None, block = None):
        if nodes is None:
            nodes = range(self.num_nodes)
        for i in nodes:
            bl = self.nodes[i].getblockcount() if block is None else block
            print ("Node%d: [%d] %s" % (i, bl, self.nodes[i].getblockhash(bl)))

    def dumpheights(self):
        print ("Heights:", self.nodes[0].getblockcount(), "\t", self.nodes[1].getblockcount(), "\t", self.nodes[2].getblockcount())
        # pass

    def erase_node(self, n):
        os.remove(os.path.join(self.nodes[n].datadir, 'regtest', 'wallets', 'wallet.dat'))
        shutil.rmtree(os.path.join(self.nodes[n].datadir, 'regtest', 'blocks'))
        shutil.rmtree(os.path.join(self.nodes[n].datadir, 'regtest', 'chainstate'))
        shutil.rmtree(os.path.join(self.nodes[n].datadir, 'regtest', 'masternodes'))
        shutil.rmtree(os.path.join(self.nodes[n].datadir, 'regtest', 'anchors'))

    def run_test(self):
        DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL = 100

        print ("Stage 1: basic tests")
        print ("Test criminal doublesigning")
        node0id = self.nodes[0].get_node_id()
        block1 = self.nodes[0].generate(1)[0]
        self.nodes[1].generate(2)
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks(self.nodes[0:2])

        assert_equal(len(self.nodes[1].listcriminalproofs()), 0)
        # node 0 generates block in a fork:
        block2 = self.nodes[0].generate(1)[0]
        self.sync_blocks(self.nodes[0:2])

        # check criminal proof:
        proof = self.nodes[1].listcriminalproofs()
        assert_equal(len(proof), 1)
        assert_equal(proof[node0id]['hash1'], block2)
        assert_equal(proof[node0id]['height1'], 3)
        assert_equal(proof[node0id]['hash2'], block1)
        assert_equal(proof[node0id]['height2'], 1)

        print ("Test ban tx")
        self.nodes[1].generate(1)
        assert_equal(self.nodes[1].listmasternodes()[node0id]['state'], "PRE_BANNED")
        # proofs cleared:
        assert_equal(len(self.nodes[1].listcriminalproofs()), 0)
        # self.dumpheights()

        print ("Test revert of ban tx")
        self.nodes[2].generate(5)
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks(self.nodes[0:3])
        # self.dumphashes()
        assert_equal(self.nodes[1].listmasternodes()[node0id]['state'], "ENABLED")
        assert_equal(self.nodes[1].listcriminalproofs(), proof)

        print ("Test persistence of proofs (start/stop)")
        self.stop_node(1)
        self.start_node(1)
        assert_equal(self.nodes[1].listcriminalproofs(), proof)
        # print(self.nodes[1].listcriminalproofs())
        # print (self.nodes[1].listmasternodes())

        print ("Stage2: Check for criminality borders")
        print ("Test criminal doublesigning under ", DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL)
        self.stop_nodes()
        self.erase_node(0)
        self.erase_node(1)
        self.erase_node(2)
        self.start_nodes()
        self.import_deterministic_coinbase_privkeys()

        node0id = self.nodes[0].get_node_id()
        block1 = self.nodes[0].generate(1)[0]
        self.nodes[1].generate(DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL)
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks(self.nodes[0:2])

        assert_equal(len(self.nodes[1].listcriminalproofs()), 0)
        # node 0 generates block in a fork:
        block2 = self.nodes[0].generate(1)[0]
        self.sync_blocks(self.nodes[0:2])
        assert_equal(len(self.nodes[1].listcriminalproofs()), 1)

        print ("Test criminal doublesigning over ", DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL)
        self.stop_nodes()
        self.erase_node(0)
        self.erase_node(1)
        self.erase_node(2)
        self.start_nodes()
        self.import_deterministic_coinbase_privkeys()

        node0id = self.nodes[0].get_node_id()
        block1 = self.nodes[0].generate(1)[0]
        self.nodes[1].generate(DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL + 1)
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks(self.nodes[0:2])

        assert_equal(len(self.nodes[1].listcriminalproofs()), 0)
        # node 0 generates block in a fork:
        block2 = self.nodes[0].generate(1)[0]
        self.sync_blocks(self.nodes[0:2])
        assert_equal(len(self.nodes[1].listcriminalproofs()), 0)


if __name__ == '__main__':
    CriminalsTest ().main ()
