#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test account mining behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

class AccountMiningTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50']]

    def run_test(self):
        node = self.nodes[0]
        node.generate(120)

        # Get addresses and set up account
        account = node.getnewaddress()
        destination = node.getnewaddress()
        node.utxostoaccount({account: "5@0"})
        node.generate(1)

        # Check we have expected balance
        assert_equal(node.getaccount(account)[0], "5.00000000@DFI")

        node.accounttoutxos(account, {destination: "4@DFI"})
        node.accounttoutxos(account, {destination: "2@DFI"})

        # Store block height
        blockcount = node.getblockcount()

        # None block is minted, 10 tries only
        assert_equal(node.generate(1, maxtries=10), [])

        # Check the blockchain height
        assert_equal(node.getblockcount(), blockcount)

        # Account should have 5@DFI
        assert_equal(node.getaccount(account)[0], "5.00000000@DFI")

if __name__ == '__main__':
    AccountMiningTest().main ()
