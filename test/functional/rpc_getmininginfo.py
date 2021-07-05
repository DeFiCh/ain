#!/usr/bin/env python3
# Copyright (c) 2021 The DeFi Blockchain developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes_bi,
)

class GetMiningInfoRPCTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node0_keys = self.nodes[0].get_genesis_keys()
        node1_keys = self.nodes[1].get_genesis_keys()

        self.log.info("Import private keys...")
        # node[0] has only one master node
        self.nodes[0].importprivkey(node0_keys.operatorPrivKey)

        # node[1] has two master nodes
        self.nodes[1].importprivkey(node0_keys.operatorPrivKey)
        self.nodes[1].importprivkey(node1_keys.operatorPrivKey)

        operators = [node0_keys.operatorAuthAddress, node1_keys.operatorAuthAddress]

        self.log.info("Restart nodes...")
        self.restart_node(0, ['-gen', '-masternode_operator=' + operators[0]])
        self.restart_node(1, ['-gen', '-rewardaddress=' + operators[1]] +
                             ['-masternode_operator=' + x for x in operators])

        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info("Mining blocks ...")
        self.nodes[0].generate(10)
        self.sync_all()
        self.nodes[1].generate(50)
        self.sync_all()

        # getmininginfo() on node[0], should only return one master node in the response array
        resp0 = self.nodes[0].getmininginfo()
        assert_equal(len(resp0['masternodes']), 1)
        assert_equal(resp0['masternodes'][0]['state'], "ENABLED")
        assert_equal(resp0['masternodes'][0]['generate'], True)
        assert_equal(resp0['masternodes'][0]['lastblockcreationattempt'] != "0", True)

        # getmininginfo() on node[1], should return two master nodes in the response array
        resp1 = self.nodes[1].getmininginfo()
        assert_equal(len(resp1['masternodes']), 2)
        assert_equal(resp1['masternodes'][0]['state'], "ENABLED")
        assert_equal(resp1['masternodes'][0]['generate'], True)
        assert_equal(resp1['masternodes'][0]['lastblockcreationattempt'] != "0", True)

        assert_equal(resp1['masternodes'][1]['state'], "ENABLED")
        assert_equal(resp1['masternodes'][1]['generate'], True)
        assert_equal(resp1['masternodes'][1]['lastblockcreationattempt'] != "0", True)

if __name__ == '__main__':
    GetMiningInfoRPCTest().main()
