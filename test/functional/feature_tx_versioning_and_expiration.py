#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Tx version and expiration."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes, disconnect_nodes

class TxVersionAndExpirationTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=101', '-greatworldheight=101', '-customtxexpiration=6'],
                           ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=101', '-greatworldheight=101', '-customtxexpiration=6']]

    def run_test(self):
        self.nodes[0].generate(101)
        self.sync_blocks()
        disconnect_nodes(self.nodes[0], 1)

        # Create transaction to use in testing
        address = self.nodes[0].getnewaddress("", "legacy")
        tx = self.nodes[0].utxostoaccount({address:"1@DFI"})
        rawtx = self.nodes[0].getrawtransaction(tx)
        self.nodes[0].clearmempool()

        # Test invalid version
        invalid_version = rawtx.replace('056b00000001', '056b000000FF')
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(invalid_version)
        assert_raises_rpc_error(-26, "Invalid transaction version set", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Test invalid expiration
        invalid_expiration = rawtx.replace('056b00000001', '050000000001')
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(invalid_expiration)
        assert_raises_rpc_error(-26, "Invalid transaction expiration set", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Check block acceptance just below expiration height
        self.nodes[0].generate(5)
        self.nodes[0].sendrawtransaction(rawtx)
        self.nodes[0].clearmempool()

        # Test mempool expiration just below expiration height
        self.nodes[1].sendrawtransaction(rawtx)
        assert_equal(self.nodes[1].getmempoolinfo()['size'], 1)
        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()
        assert_equal(self.nodes[1].getmempoolinfo()['size'], 1)
        disconnect_nodes(self.nodes[0], 1)

        # Test mempool rejection at expiration height
        self.nodes[0].generate(1)
        assert_raises_rpc_error(-26, "Transaction has expired", self.nodes[0].sendrawtransaction, rawtx)

        # Test mempool expiration at expiration height
        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()
        assert_equal(self.nodes[1].getmempoolinfo()['size'], 0)

if __name__ == '__main__':
    TxVersionAndExpirationTest().main()
