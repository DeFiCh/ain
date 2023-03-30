#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)

from decimal import Decimal

class EVMTest(DefiTestFramework):
    mns = None
    proposalId = ""

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1']
        ]

    def run_test(self):
        address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        ethAddress = self.nodes[0].getnewaddress("","eth")

        # Generate chain
        self.nodes[0].generate(105)
        self.sync_blocks()

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({address: "101@DFI"})
        self.nodes[0].generate(1)
        self.sync_blocks()

        DFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        ETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)

        assert_equal(DFIbalance, Decimal('101'))
        assert_equal(len(ETHbalance), 0)

        self.nodes[0].transferbalance("evmin",{address:["100@DFI"]}, {ethAddress:["100@DFI"]})
        self.nodes[0].evmtx("AABBCCDDEEFF00112233445566778899")

        self.nodes[0].generate(1)
        self.sync_blocks()

        newDFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        newETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)

        assert_equal(newDFIbalance, DFIbalance - Decimal('100'))
        assert_equal(len(newETHbalance), 0)

        self.nodes[0].transferbalance("evmout", {ethAddress:["100@DFI"]}, {address:["100@DFI"]})

        self.nodes[0].generate(1)
        self.sync_blocks()

        newDFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        # newETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)['0']

        assert_equal(newDFIbalance, DFIbalance)
        # assert_equal(newETHbalance, ETHbalance)

if __name__ == '__main__':
    EVMTest().main()
