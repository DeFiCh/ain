#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test that EVM state root changes on every block"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal
)

class StateRootChangeTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate2height=105', '-changiintermediate3height=105', '-changiintermediate4height=110', '-subsidytest=1', '-txindex=1'],
        ]

    def run_test(self):
        node = self.nodes[0]
        node.generate(105)

        # Activate EVM
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        # check counter contract
        from web3 import Web3
        w3 = Web3(Web3.HTTPProvider(self.nodes[0].get_evm_rpc()))

        NUM_BLOCKS = 5
        state_roots = set()
        for i in range(NUM_BLOCKS):
            node.generate(1)
            block = w3.eth.get_block('latest')
            state_roots.add(Web3.to_hex(block["stateRoot"]))

        assert_equal(len(state_roots), NUM_BLOCKS)


if __name__ == '__main__':
    StateRootChangeTest().main()
