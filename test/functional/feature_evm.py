#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)

from decimal import Decimal

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1']
        ]

    def run_test(self):

        address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        ethAddress = self.nodes[0].getnewaddress("","eth")
        to_address = self.nodes[0].getnewaddress("","eth")

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].evmtx, ethAddress, 0, 21, 21000, to_address, 0.1)

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({address: "101@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        DFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        ETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)

        assert_equal(DFIbalance, Decimal('101'))
        assert_equal(len(ETHbalance), 0)

        self.nodes[0].transferbalance("evmin",{address:["100@DFI"]}, {ethAddress:["100@DFI"]})
        self.nodes[0].generate(1)

        newDFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        newETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)

        assert_equal(newDFIbalance, DFIbalance - Decimal('100'))
        assert_equal(len(newETHbalance), 0)

        self.nodes[0].transferbalance("evmout", {ethAddress:["100@DFI"]}, {address:["100@DFI"]})
        self.nodes[0].generate(1)

        newDFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        # newETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)['0']

        assert_equal(newDFIbalance, DFIbalance)
        # assert_equal(newETHbalance, ETHbalance)

        # Fund Eth address
        self.nodes[0].transferbalance("evmin",{address:["10@DFI"]}, {ethAddress:["10@DFI"]})
        self.nodes[0].generate(1)

        # Test EVM Tx
        tx = self.nodes[0].evmtx(ethAddress, 0, 21, 21000, to_address, 1)
        assert_equal(self.nodes[0].getrawmempool(), [tx])
        self.nodes[0].generate(1)

        # Check EVM Tx is in block
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        assert_equal(block['tx'][1], tx)

if __name__ == '__main__':
    EVMTest().main()
