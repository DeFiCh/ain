#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the Bitcoin SPV wallet"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from test_framework.authproxy import JSONRPCException
from decimal import Decimal

class BitcoinSPVTests(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [
            [ "-dummypos=1", "-spv=1"],
        ]
        self.setup_clean_chain = True

    def run_test(self):
        # Set up wallet
        self.nodes[0].spv_setlastheight(1)
        address = self.nodes[0].spv_getnewaddress()
        txid = self.nodes[0].spv_fundaddress(address)

        # Should now have a balance of 1 Bitcoin
        result = self.nodes[0].spv_getbalance()
        assert_equal(result, Decimal("1.00000000"))

        # Make sure tx is present in wallet
        txs = self.nodes[0].spv_listtransactions()
        assert_equal(txs[0], txid)

        # Get private key for address and import into wallet.
        # Should not throw any error.
        priv_key = self.nodes[0].spv_dumpprivkey(address)
        result = self.nodes[0].importprivkey(priv_key)

        # Try and send to null address
        try:
            dummy_address = "000000000000000000000000000000000000000000"
            self.nodes[0].spv_sendtoaddress(dummy_address, 0.1)
        except JSONRPCException as e:
            errorString = e.error['message']
            assert("Invalid address" in errorString)

        # Send to external address
        dummy_address = "bcrt1qfpnmx6jrn30yvscrw9spudj5aphyrc8es6epva"
        result = self.nodes[0].spv_sendtoaddress(dummy_address, 0.1)
        assert_equal(result['sendmessage'], "")

        # Make sure tx is present in wallet
        txs = self.nodes[0].spv_listtransactions()
        assert(result['txid'] in txs)

        # Make sure balance reduced
        balance = self.nodes[0].spv_getbalance()
        assert_equal(balance, Decimal("0.89998800"))

        # Send to self
        result = self.nodes[0].spv_sendtoaddress(address, 0.1)
        assert_equal(result['sendmessage'], "")

        # Make sure tx is present in wallet
        txs = self.nodes[0].spv_listtransactions()
        assert(result['txid'] in txs)

        # Make sure balance reduced
        balance = self.nodes[0].spv_getbalance()
        assert_equal(balance, Decimal("0.89997600"))

        # Let's find the output that matches our address
        # One is the send-to-self and the other change.
        # This also tests spv_getrawtransaction
        raw_tx = self.nodes[0].spv_getrawtransaction(result['txid'])
        decoded_tx = self.nodes[0].decoderawtransaction(raw_tx)
        output_one = decoded_tx['vout'][0]['scriptPubKey']['addresses'][0]
        output_two = decoded_tx['vout'][1]['scriptPubKey']['addresses'][0]

        # Let's get the private key from the DeFi wallet
        if output_one == address:
            wallet_priv_key = self.nodes[0].dumpprivkey(output_one)
            change_address = output_two
        else:
            wallet_priv_key = self.nodes[0].dumpprivkey(output_two)
            change_address = output_one

        # Private keys should match
        change_priv_key = self.nodes[0].spv_dumpprivkey(change_address)
        wallet_change_priv_key = self.nodes[0].dumpprivkey(change_address)
        assert_equal(wallet_priv_key, priv_key)
        assert_equal(wallet_change_priv_key, change_priv_key)

if __name__ == '__main__':
    BitcoinSPVTests().main()
