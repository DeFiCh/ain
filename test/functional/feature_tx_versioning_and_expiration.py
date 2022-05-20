#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Tx version and expiration."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error

class TxVersionAndExpirationTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=101', '-greatworldheight=101']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Create token and pool address
        address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Create token
        self.nodes[0].createtoken({
            "symbol": "LTC",
            "name": "Litecoin",
            "isDAT": True,
            "collateralAddress": address
        })
        self.nodes[0].generate(1)

        # Create pool
        self.nodes[0].createpoolpair({
            "tokenA": 'LTC',
            "tokenB": 'DFI',
            "commission": 0.01,
            "status": True,
            "ownerAddress": address
        }, [])
        self.nodes[0].generate(1)

        # Fund address with DFI and LTC
        self.nodes[0].minttokens(["0.1@LTC"])
        self.nodes[0].utxostoaccount({address: "10@DFI"})
        self.nodes[0].generate(1)

        # Create transaction to use in testing
        tx = self.nodes[0].addpoolliquidity({address: ["0.1@LTC", "10@DFI"]}, address)
        rawtx = self.nodes[0].getrawtransaction(tx)
        self.nodes[0].clearmempool()

        # Test invalid version
        print(rawtx)
        invalid_version = rawtx.replace('05e000000001', '05e0000000ff')
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(invalid_version)
        assert_raises_rpc_error(-26, "Invalid transaction version set", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Test invalid expiration
        invalid_expiration = rawtx.replace('05e000000001', '050000000001')
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(invalid_expiration)
        assert_raises_rpc_error(-26, "Invalid transaction expiration set", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

        # Check block acceptance just below expiration height
        self.nodes[0].generate(119)
        self.nodes[0].sendrawtransaction(rawtx)
        self.nodes[0].clearmempool()

        # Test mempool rejection at expiration height
        self.nodes[0].generate(1)
        assert_raises_rpc_error(-26, "Transaction has expired", self.nodes[0].sendrawtransaction, rawtx)

        # Test expiration value set by startup flag
        self.stop_node(0)
        self.start_node(0, ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=101', '-greatworldheight=101', '-customtxexpiration=1'])

        # Create expiration TX
        tx = self.nodes[0].addpoolliquidity({address: ["0.1@LTC", "10@DFI"]}, address)
        rawtx = self.nodes[0].getrawtransaction(tx, 1)
        self.nodes[0].clearmempool()

        # Check expiration is now 1 block
        expiration = self.nodes[0].getblockcount() + 1
        assert_equal(rawtx['vout'][0]['scriptPubKey']['hex'][172:], '05' + hex(expiration)[2:] + '00000001')
        self.nodes[0].clearmempool()

        # Create expiration TX
        self.nodes[0].setcustomtxexpiration(10)
        tx = self.nodes[0].addpoolliquidity({address: ["0.1@LTC", "10@DFI"]}, address)
        rawtx = self.nodes[0].getrawtransaction(tx, 1)
        self.nodes[0].clearmempool()

        # Check expiration is now 10 blocks
        expiration = self.nodes[0].getblockcount() + 10
        assert_equal(rawtx['vout'][0]['scriptPubKey']['hex'][172:], '05' + hex(expiration)[2:] + '00000001')
        self.nodes[0].clearmempool()

if __name__ == '__main__':
    TxVersionAndExpirationTest().main()
