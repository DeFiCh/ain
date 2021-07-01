#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test sendutxosfrom."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

class SendUTXOsFromTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-txindex=1'],
                           ['-txnotokens=0', '-amkheight=1', '-txindex=1']]

    def run_test(self):
        self.nodes[0].generate(110)
        self.sync_blocks()

        # Create addresses
        address = self.nodes[1].getnewaddress()
        to = self.nodes[1].getnewaddress()
        change = self.nodes[1].getnewaddress()

        # Try send without funding from address
        try:
            self.nodes[1].sendutxosfrom(address, to, 0.1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Insufficient funds" in errorString)

        # Fund from address
        for _ in range(10):
            self.nodes[0].sendtoaddress(address, 1)
            self.nodes[0].generate(1)
        self.sync_blocks()

        # Invalid from address
        try:
            self.nodes[1].sendutxosfrom("", to, 0.1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid from address" in errorString)

        # Invalid to address
        try:
            self.nodes[1].sendutxosfrom(address, "", 0.1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid to address" in errorString)


        # Invalid change address
        try:
            self.nodes[1].sendutxosfrom(address, to, 0.1, "")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid change address" in errorString)

        # Try sending too little
        try:
            self.nodes[1].sendutxosfrom(address, to, 0)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        # Fund many addresses
        for _ in range(100):
            self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
            self.nodes[0].generate(1)
        self.sync_blocks()

        # Test send with change to specified address
        txid = self.nodes[1].sendutxosfrom(address, to, 1.5, change)
        self.nodes[1].generate(1)

        raw_tx = self.nodes[1].getrawtransaction(txid, 1)

        # Check all inputs are from the from address
        for vin in raw_tx['vin']:
            num = vin['vout']
            input_tx = self.nodes[1].getrawtransaction(vin['txid'], 1)
            assert_equal(input_tx['vout'][num]['scriptPubKey']['addresses'][0], address)

        # Check change address is present
        found = False
        for vout in raw_tx['vout']:
            if change in vout['scriptPubKey']['addresses']:
                found = True

        assert(found)

        # Test send with change to default from address
        txid = self.nodes[1].sendutxosfrom(address, to, 1.5)
        self.nodes[1].generate(1)

        raw_tx = self.nodes[1].getrawtransaction(txid, 1)

        # Check all inputs are from the from address
        for vin in raw_tx['vin']:
            num = vin['vout']
            input_tx = self.nodes[1].getrawtransaction(vin['txid'], 1)
            assert_equal(input_tx['vout'][num]['scriptPubKey']['addresses'][0], address)

        # Check change address is present
        found = False
        for vout in raw_tx['vout']:
            if address in vout['scriptPubKey']['addresses']:
                found = True

        assert(found)

        # Test fee is not deducted from recipient 'to'
        amount = 2.5
        txid = self.nodes[1].sendutxosfrom(address, to, amount)
        self.nodes[1].generate(2)

        raw_tx = self.nodes[1].getrawtransaction(txid, 1)

        # Check 'to' address is present
        found = False
        for vout in raw_tx['vout']:
            if to in vout['scriptPubKey']['addresses']:
                found = True
                assert_equal(vout['value'], amount)

        assert(found)

if __name__ == '__main__':
    SendUTXOsFromTest().main()
