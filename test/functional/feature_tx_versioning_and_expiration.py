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

        # Fund account for tests
        self.nodes[0].utxostoaccount({address:"1@DFI"})
        self.nodes[0].generate(1)
        self.sync_blocks()
        disconnect_nodes(self.nodes[0], 1)

        # Test auto auth expiration value set by startup flag
        self.stop_node(0)
        self.start_node(0, ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=101', '-greatworldheight=101', '-customtxexpiration=1'])
        destination = self.nodes[0].getnewaddress("", "legacy")
        tx = self.nodes[0].accounttoaccount(address, {destination: "1@DFI"})
        autoauth_tx = self.nodes[0].getrawtransaction(tx, 1)['vin'][0]['txid']
        expiration = self.nodes[0].getblockcount() + 1
        assert_equal(self.nodes[0].getrawtransaction(autoauth_tx, 1)['vout'][0]['scriptPubKey']['hex'][14:], '05' + hex(expiration)[2:] + '00000001')
        autoauth_rawtx = self.nodes[0].getrawtransaction(autoauth_tx)
        self.nodes[0].clearmempool()

        # Test auto auth expiration
        self.nodes[0].generate(1)
        assert_raises_rpc_error(-26, "Transaction has expired", self.nodes[0].sendrawtransaction, autoauth_rawtx)
        self.nodes[1].sendrawtransaction(autoauth_rawtx)
        assert_equal(self.nodes[1].getmempoolinfo()['size'], 1)
        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()
        assert_equal(self.nodes[1].getmempoolinfo()['size'], 0)
        disconnect_nodes(self.nodes[0], 1)

        # Test auto auth expiration value set by RPC
        self.nodes[0].setcustomtxexpiration(10)
        tx = self.nodes[0].accounttoaccount(address, {destination: "1@DFI"})
        autoauth_tx = self.nodes[0].getrawtransaction(tx, 1)['vin'][0]['txid']
        expiration = self.nodes[0].getblockcount() + 10
        assert_equal(self.nodes[0].getrawtransaction(autoauth_tx, 1)['vout'][0]['scriptPubKey']['hex'][14:], '05' + hex(expiration)[2:] + '00000001')
        autoauth_rawtx = self.nodes[0].getrawtransaction(autoauth_tx)
        self.nodes[0].clearmempool()

        # Test auto auth expiration
        self.nodes[0].generate(10)
        assert_raises_rpc_error(-26, "Transaction has expired", self.nodes[0].sendrawtransaction, autoauth_rawtx)
        self.nodes[1].sendrawtransaction(autoauth_rawtx)
        assert_equal(self.nodes[1].getmempoolinfo()['size'], 1)
        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()
        assert_equal(self.nodes[1].getmempoolinfo()['size'], 0)
        disconnect_nodes(self.nodes[0], 1)

if __name__ == '__main__':
    TxVersionAndExpirationTest().main()
