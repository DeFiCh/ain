#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test rejection of invalid custom TXs."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_raises_rpc_error

class RejectCustomTx(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-dakotaheight=1', '-fortcanningheight=120', '-greatworldheight=140', '-customtxexpiration=6']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Get and fund collateral address
        collateral = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].sendtoaddress(collateral, 1)
        self.nodes[0].generate(2)

        block_count = self.nodes[0].getblockcount()

        # Record the number of MNs
        num_mns = len(self.nodes[0].listmasternodes())

        # Make create MN TX, get details and wipe from mempool
        txid = self.nodes[0].createmasternode(collateral)
        rawtx = self.nodes[0].getrawtransaction(txid, 1)
        rawtx_hex = self.nodes[0].getrawtransaction(txid)
        self.nodes[0].clearmempool()

        # Get push data and use to double push data in op_return
        pushdata = rawtx['vout'][0]['scriptPubKey']['hex'][2:]
        result = rawtx_hex.find(pushdata)
        rawtx_hex_multiop = rawtx_hex[:result - 4] + hex(len(pushdata) + 1)[2:] + '6a' + pushdata + rawtx_hex[result:]
        signed_multiop = self.nodes[0].signrawtransactionwithwallet(rawtx_hex_multiop)

        # Send TX
        self.nodes[0].sendrawtransaction(signed_multiop['hex'])
        self.nodes[0].generate(1)

        # Check that MNs have increased by 1
        assert_equal(num_mns + 1, len(self.nodes[0].listmasternodes()))

        # Rollback and wipe TXs.
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(block_count))
        self.nodes[0].clearmempool()

        # Test sending DfTx in a vout other than 0
        rawtx_opreturn_vout1 = self.nodes[0].createrawtransaction([{"txid":rawtx['vin'][0]['txid'], "vout":rawtx['vin'][0]['vout']}], [{collateral:0.99999000},{"data":pushdata[2:]}])
        signed_opreturn_vout1 = self.nodes[0].signrawtransactionwithwallet(rawtx_opreturn_vout1)
        self.nodes[0].sendrawtransaction(signed_opreturn_vout1['hex'])

        # Wipe TX
        self.nodes[0].clearmempool()

        # Test sending DfTx with None type
        rawtx_none = self.nodes[0].createrawtransaction([{"txid":rawtx['vin'][0]['txid'], "vout":rawtx['vin'][0]['vout']}], [{collateral:0.99999000},{"data":"4466547800"}])
        signed_none = self.nodes[0].signrawtransactionwithwallet(rawtx_none)
        self.nodes[0].sendrawtransaction(signed_none['hex'])

        # Wipe TX
        self.nodes[0].clearmempool()

        # Test sending DfTx with unknown type z
        rawtx_unknown = self.nodes[0].createrawtransaction([{"txid":rawtx['vin'][0]['txid'], "vout":rawtx['vin'][0]['vout']}], [{collateral:0.99999000},{"data":"4466547879"}])
        signed_unknown = self.nodes[0].signrawtransactionwithwallet(rawtx_unknown)
        self.nodes[0].sendrawtransaction(signed_unknown['hex'])

        # Wipe TX
        self.nodes[0].clearmempool()

        # Move to FortCanning height
        self.nodes[0].generate(20)

        # Make sure MNs still at original count
        assert_equal(num_mns, len(self.nodes[0].listmasternodes()))

        # Try and send multi opcode TX after hard fork
        try:
            self.nodes[0].sendrawtransaction(signed_multiop['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid custom transaction" in errorString)

        # Try and send DfTx in a vout other than 0 after hard fork
        try:
            self.nodes[0].sendrawtransaction(signed_opreturn_vout1['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid custom transaction" in errorString)

        # Try and send DfTx with None type after hard fork
        try:
            self.nodes[0].sendrawtransaction(signed_none['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid custom transaction" in errorString)

        # Try and send DfTx with unknown type after hard fork
        try:
            self.nodes[0].sendrawtransaction(signed_unknown['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid custom transaction" in errorString)

        # Set up for GW TX expiration tests
        address = self.nodes[0].getnewaddress("", "legacy")

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
        self.nodes[0].sendtoaddress(address, 0.1)
        self.nodes[0].utxostoaccount({address: "10@DFI"})
        self.nodes[0].generate(1)

        # Move to GreatWorld height
        self.nodes[0].generate(140 - self.nodes[0].getblockcount())

        # Create transaction with new expiration and version fields
        tx = self.nodes[0].addpoolliquidity({address: ["0.1@LTC", "10@DFI"]}, address)
        rawtx = self.nodes[0].getrawtransaction(tx)
        self.nodes[0].clearmempool()

        # Append extra data and test failure
        rawtx = rawtx.replace('ffffffff0200000000000000005c', 'ffffffff0200000000000000005d')
        expiration = self.nodes[0].getblockcount() + 6
        rawtx = rawtx.replace('05' + hex(expiration)[2:] + '00000001', '05' + hex(expiration)[2:] + '0000000100')
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(rawtx)
        assert_raises_rpc_error(-26, "Invalid custom transaction", self.nodes[0].sendrawtransaction, signed_rawtx['hex'])

if __name__ == '__main__':
    RejectCustomTx().main()
