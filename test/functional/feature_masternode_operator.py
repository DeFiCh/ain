#!/usr/bin/env python3
# Copyright (c) 2021 The DeFi Blockchain developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes_bi,
)

class TestMasternodeOperator(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node0_keys = self.nodes[0].get_genesis_keys()
        node1_keys = self.nodes[1].get_genesis_keys()

        self.log.info("Import private keys...")
        self.nodes[0].importprivkey(node0_keys.operatorPrivKey)
        self.nodes[1].importprivkey(node0_keys.operatorPrivKey)
        self.nodes[1].importprivkey(node1_keys.operatorPrivKey)

        operators = [node0_keys.operatorAuthAddress, node1_keys.operatorAuthAddress]

        self.log.info("Restart nodes...")
        self.restart_node(0, ['-gen', '-masternode_operator=' + operators[0]])
        self.restart_node(1, ['-gen', '-rewardaddress=' + operators[1]] +
                             ['-masternode_operator=' + x for x in operators])

        connect_nodes_bi(self.nodes, 0, 1)

        self.log.info("Mining blocks ...")
        startNode0 = self.nodes[0].getblockcount() + 1
        self.nodes[0].generate(10)
        print(self.nodes[0].getblockcount())
        self.sync_all()
        self.nodes[1].generate(10)
        self.sync_all()

        minters = set()
        for x in range(startNode0, startNode0 + 10):
            blockhash = self.nodes[0].getblockhash(x)
            minters.add(self.nodes[0].getblock(blockhash)["minter"])

        assert_equal(len(minters), 1)

        minters.clear()
        for x in range(11, 21):
            blockhash = self.nodes[0].getblockhash(x)
            minters.add(self.nodes[0].getblock(blockhash)["minter"])

        assert_equal(len(minters), 2)


if __name__ == '__main__':
    TestMasternodeOperator().main()
