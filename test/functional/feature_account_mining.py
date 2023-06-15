#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test account mining behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_array_result,
    assert_equal
)
from decimal import Decimal


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
        txid = node.utxostoaccount({account: "10@0"})
        node.generate(1)

        # exclude_custom_tx is False by default, so should can find the utxostoaccount tx
        assert_array_result(node.listtransactions(), {"txid": txid}, {"amount": Decimal("-10")})
        # exclude_custom_tx is True then should not find the utxostoaccount tx
        assert_array_result(node.listtransactions("*", 10, 0, True, True), {"txid": txid}, {}, True)

        # Check we have expected balance
        assert_equal(node.getaccount(account)[0], "10.00000000@DFI")

        # Send double the amount we have in account
        thrown = False
        for _ in range(100):
            try:
                node.accounttoutxos(account, {destination: "2@DFI"})
            except JSONRPCException:
                thrown = True
        assert_equal(thrown, True)

        # Store block height
        blockcount = node.getblockcount()

        # Generate a block
        node.generate(1)

        # Check the blockchain has extended as expected
        assert_equal(node.getblockcount(), blockcount + 1)

        # Generate 10 more blocks
        node.generate(10)

        # Check the blockchain has extended as expected
        assert_equal(node.getblockcount(), blockcount + 11)

        # Account should now be empty
        assert_equal(node.getaccount(account), [])

        # Update block height
        blockcount = node.getblockcount()

        # Send more UTXOs to account
        node.utxostoaccount({account: "1@0"})
        node.generate(1)

        # Update block height
        blockcount = node.getblockcount()

        # Test mixture of account TXs
        thrown = False
        for _ in range(10):
            try:
                node.accounttoaccount(account, {destination: "1@DFI"})
                node.accounttoutxos(account, {destination: "1@DFI"})
            except JSONRPCException:
                thrown = True
        assert_equal(thrown, True)

        # Generate 1 more blocks
        node.generate(1)

        # Check the blockchain has extended as expected
        assert_equal(node.getblockcount(), blockcount + 1)


if __name__ == '__main__':
    AccountMiningTest().main()
