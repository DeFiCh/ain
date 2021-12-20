#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Tx version and expiration."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_raises_rpc_error

class TxVersionAndExpirationTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=101', '-greatworldheight=101']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Create transaction to use in testing
        address = self.nodes[0].getnewaddress("", "legacy")
        tx = self.nodes[0].utxostoaccount({address:"1@DFI"})
        rawtx = self.nodes[0].getrawtransaction(tx)
        rawtx_verbose = self.nodes[0].getrawtransaction(tx, 1)
        self.nodes[0].clearmempool()

        # Test invalid version
        invalid_version = rawtx.replace('056600000001', '0566000000FF')
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(invalid_version)
        assert_raises_rpc_error(-26, "Invalid transaction version set", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Test invalid expiration
        invalid_expiration = rawtx.replace('056600000001', '050000000001')
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(invalid_expiration)
        assert_raises_rpc_error(-26, "Invalid transaction expiration set", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

if __name__ == '__main__':
    TxVersionAndExpirationTest().main()
