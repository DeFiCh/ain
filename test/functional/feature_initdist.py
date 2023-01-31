#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify that initdist coins is spendable
"""


from test_framework.test_framework import DefiTestFramework

from test_framework.util import connect_nodes_bi

class InitDistTest (DefiTestFramework):
    def set_test_params(self):
        # 3 nodes used just for "clean" balanses for 0&1. node#2 used for generation
        self.num_nodes = 3
        self.extra_args = [
            [ "-dummypos=1", "-spv=0" ],
            [ "-dummypos=1", "-spv=0" ],
            [ "-dummypos=1", "-spv=0" ],
        ]
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_blocks()

    def run_test(self):

        self.nodes[0].importprivkey(privkey=self.nodes[0].PRIV_KEYS[5].ownerPrivKey, label='initdist')

        assert(self.nodes[0].getbalance() == 0)
        assert(self.nodes[1].getbalance() == 0)
        self.nodes[2].generate(100)
        self.sync_blocks()

        assert(self.nodes[0].getbalance() == 50)
        utxo0 = self.nodes[0].listunspent()
        assert(utxo0[0]['address'] == 'mud4VMfbBqXNpbt8ur33KHKx8pk3npSq8c')
        assert(utxo0[0]['amount'] == 50.0)
        assert(utxo0[0]['spendable'] == True)

        addr = self.nodes[1].getnewaddress("", "legacy")
        self.nodes[0].sendtoaddress(addr, 42)
        self.sync_mempools()
        self.nodes[2].generate(1)
        self.sync_blocks()

        assert(self.nodes[0].getbalance() >= 7.99)
        assert(self.nodes[1].getbalance() == 42)


if __name__ == '__main__':
    InitDistTest ().main ()
