#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test account mining behaviour"""

from test_framework.authproxy import JSONRPCException
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

        # Corrent account to utxo tx - entering mempool
        node.accounttoutxos(account, {destination: "4@DFI"})

        try:
            # Not enough amount - rejected
            node.accounttoutxos(account, {destination: "2@DFI"})
        except JSONRPCException as e:
            errorString = e.error['message']

        assert('bad-txns-customtx' in errorString)

        # Store block height
        blockcount = node.getblockcount()

        # One minted block with correct tx
        node.generate(1)

        # Check the blockchain height
        assert_equal(node.getblockcount(), blockcount + 1)

        # Account should have 1@DFI
        assert_equal(node.getaccount(account)[0], "1.00000000@DFI")

if __name__ == '__main__':
    AccountMiningTest().main ()
