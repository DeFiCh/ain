#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test sending dust to token recipients."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

from decimal import Decimal

class DustToRecipientsTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontgardensheight=1', '-txindex=1']]

    def run_test(self):
        self.nodes[0].generate(110)

        # Create addresses
        address_p2sh = self.nodes[0].getnewaddress() # P2SH-P2WPKH
        address_legacy = self.nodes[0].getnewaddress("", "legacy") # P2PKH
        address_bech32 = self.nodes[0].getnewaddress("", "bech32") # P2WPKH

        # utxostoaccount
        txid = self.nodes[0].utxostoaccount({address_p2sh: "100@DFI",address_legacy: "100@DFI", address_bech32: "100@DFI"})
        raw_tx = self.nodes[0].getrawtransaction(txid, 1)
        self.nodes[0].generate(1)

        # Check dust outputs
        assert_equal(raw_tx['vout'][1]['scriptPubKey']['addresses'][0], address_bech32)
        assert_equal(raw_tx['vout'][1]['value'], Decimal('0.00000297'))
        assert_equal(raw_tx['vout'][2]['scriptPubKey']['addresses'][0], address_p2sh)
        assert_equal(raw_tx['vout'][2]['value'], Decimal('0.00000543'))
        assert_equal(raw_tx['vout'][3]['scriptPubKey']['addresses'][0], address_legacy)
        assert_equal(raw_tx['vout'][3]['value'], Decimal('0.00000549'))

        # accounttoaccount
        txid = self.nodes[0].accounttoaccount(address_p2sh, {address_p2sh: "1@DFI",address_legacy: "1@DFI", address_bech32: "1@DFI"})
        raw_tx = self.nodes[0].getrawtransaction(txid, 1)
        self.nodes[0].generate(1)

        # Check dust outputs
        assert_equal(raw_tx['vout'][1]['scriptPubKey']['addresses'][0], address_bech32)
        assert_equal(raw_tx['vout'][1]['value'], Decimal('0.00000297'))
        assert_equal(raw_tx['vout'][2]['scriptPubKey']['addresses'][0], address_p2sh)
        assert_equal(raw_tx['vout'][2]['value'], Decimal('0.00000543'))
        assert_equal(raw_tx['vout'][3]['scriptPubKey']['addresses'][0], address_legacy)
        assert_equal(raw_tx['vout'][3]['value'], Decimal('0.00000549'))

        # sendtokenstoaddress
        txid = self.nodes[0].sendtokenstoaddress({}, {address_p2sh: "1@DFI",address_legacy: "1@DFI", address_bech32: "1@DFI"})
        raw_tx = self.nodes[0].getrawtransaction(txid, 1)
        self.nodes[0].generate(1)

        # Check dust outputs
        assert_equal(raw_tx['vout'][1]['scriptPubKey']['addresses'][0], address_bech32)
        assert_equal(raw_tx['vout'][1]['value'], Decimal('0.00000297'))
        assert_equal(raw_tx['vout'][2]['scriptPubKey']['addresses'][0], address_p2sh)
        assert_equal(raw_tx['vout'][2]['value'], Decimal('0.00000543'))
        assert_equal(raw_tx['vout'][3]['scriptPubKey']['addresses'][0], address_legacy)
        assert_equal(raw_tx['vout'][3]['value'], Decimal('0.00000549'))


if __name__ == '__main__':
    DustToRecipientsTest().main()
