#!/usr/bin/env python3
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test sending funds to metachain"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal


class MetachainSendFundsTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101', '-dmcgenesisheight=151'],
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101', '-dmcgenesisheight=151'],
        ]

    def run_test(self):
        node = self.nodes[0]
        node1 = self.nodes[1]
        node.generate(101)
        self.sync_blocks()
        assert_equal(node.getblockcount(), 101)

        # Get addresses and set up account
        account = node.getnewaddress()
        node.utxostoaccount({account: "10@0"})
        node.generate(1)
        self.sync_blocks()
        assert_equal(node1.getaccount(account)[0], "10.00000000@DFI")

        node.generate(49)  # DMC genesis
        self.sync_blocks()
        assert_equal(node.getblockcount(), 151)

        node.accounttometachain(account, "000000000000000000000000000000000000dead", "3.14")
        blockcount = node.getblockcount()
        node.generate(1)
        self.sync_blocks()
        assert_equal(node1.getblockcount(), blockcount + 1)  # Check that both nodes have sync'd

        lock_address = "2N9HiV9k2smL1953raxC4PxqN6kmN28c53D"
        assert_equal(node1.getaccount(lock_address)[0], "3.14000000@DFI")

        # FIXME: This is a dummy value and must be updated once metachain has the implementation
        expected_dmc_payload = '0102030405'
        assert_equal(node.getblock(node.getblockhash(blockcount + 1), 2)['dmcblock'], expected_dmc_payload)

        # FIXME: dmcpayload does not exist on node1
        # assert_equal(node1.getblock(node1.getblockhash(blockcount + 1), 2)['dmcblock'], expected_dmc_payload)


# FIXME: In order to test this using RPC, we need to setup a simple JSON RPC server
# using 'jsonrpcserver' package:
#
# ------------------
# from jsonrpcserver import Success, method, serve

# @method
# def metaConsensusRpc_mintBlock(block_input):
#     return Success({"payload": [1, 2, 3, 4, 5]})

# if __name__ == "__main__":
#     serve('localhost', 5001)
# ------------------
#
# Now, we can set `self.metachain_rpc = 'http://localhost:5001'` in the above test class
# and defid will use the RPC server instead.


if __name__ == '__main__':
    MetachainSendFundsTest().main()
