#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
import os

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal,
)

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.toAddress = '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110'

        self.nodes[0].generate(111)

        self.nodes[0].utxostoaccount({self.address: "200@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true',
                                             'v0/params/feature/transferdomain': 'true',
                                             'v0/transferdomain/dvm-evm/enabled': 'true',
                                             'v0/transferdomain/dvm-evm/src-formats': ['p2pkh','bech32'],
                                             'v0/transferdomain/dvm-evm/dest-formats': ['erc55'],
                                             'v0/transferdomain/evm-dvm/src-formats': ['erc55'],
                                             'v0/transferdomain/evm-dvm/auth-formats': ['bech32-erc55'],
                                             'v0/transferdomain/evm-dvm/dest-formats': ['p2pkh','bech32']}})
        self.nodes[0].generate(1)

        self.ethAddress = self.nodes[0].getnewaddress('eth', 'eth')

        self.nodes[0].transferdomain([{"src": {"address": self.address, "amount":"50@DFI", "domain": 2}, "dst":{"address":self.ethAddress, "amount":"50@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

    def test_evm_block_gen(self):
        dvm_blockn = self.nodes[0].getblockcount()
        assert_equal(dvm_blockn, 113)
        evm_blockn = self.nodes[0].eth_blockNumber()
        assert_equal(evm_blockn, '0x1')

        self.nodes[0].generate(1)

        dvm_blockn = self.nodes[0].getblockcount()
        assert_equal(dvm_blockn, 114)
        evm_blockn = self.nodes[0].eth_blockNumber()
        assert_equal(evm_blockn, '0x2')

    def failed_if_empty_params(self):
        try:
            self.nodes[0].eth_getBlockByNumber()
        except JSONRPCException as e:
            assert_equal(e.error['message'], 'missing field `block_number` at line 1 column 2')

    def none_if_future_block(self):
        n = self.nodes[0].eth_blockNumber()
        b = self.nodes[0].eth_getBlockByNumber(int(n, 16) + 999)
        assert_equal(b, None)

    def block_by_number_n(self):
        n = self.nodes[0].eth_blockNumber()
        b = self.nodes[0].eth_getBlockByNumber(n)
        assert_equal(b['number'], n)

    def block_by_number_latest(self):
        n = self.nodes[0].eth_blockNumber()
        b = self.nodes[0].eth_getBlockByNumber(n)
        b_latest = self.nodes[0].eth_getBlockByNumber('latest')
        assert_equal(b['number'], b_latest['number'])

    def block_by_number_earliest(self):
        n = self.nodes[0].eth_blockNumber()
        b = self.nodes[0].eth_getBlockByNumber('earliest')
        assert_equal(b['number'], '0x0')

    def block_by_number_pending(self):
        before = self.nodes[0].eth_getBlockByNumber('pending')
        assert_equal(before.get('hash'), None)
        assert_equal(before.get('mixHash'), None)
        assert_equal(len(before['transactions']), 0)

        tx1559 = {
            'from': self.ethAddress,
            'to': self.toAddress,
            'value': '0x64', # 100
            'gas': '0x18e70', # 102_000
            'maxPriorityFeePerGas': '0x2363e7f000', # 152_000_000_000
            'maxFeePerGas': '0x22ecb25c00', # 150_000_000_000
            'type': '0x2'
        }
        txhash = self.nodes[0].eth_sendTransaction(tx1559)

        after = self.nodes[0].eth_getBlockByNumber('pending')
        assert_equal(len(after['transactions']), 1)
        assert_equal(after['transactions'][0], txhash)

        self.nodes[0].generate(1)

        latest = self.nodes[0].eth_getBlockByNumber('latest')
        assert_equal(latest['parentHash'], before['parentHash'])

        pending = self.nodes[0].eth_getBlockByNumber('pending')
        assert_equal(pending.get('hash'), None)
        assert_equal(pending.get('mixHash'), None)
        assert_equal(pending['parentHash'], latest['hash'])
        assert_equal(len(pending['transactions']), 0)

    def run_test(self):
        self.setup()

        self.test_evm_block_gen()

        self.failed_if_empty_params()

        self.none_if_future_block()

        self.block_by_number_n()

        self.block_by_number_latest()

        self.block_by_number_earliest()

        self.block_by_number_pending()


if __name__ == "__main__":
    EVMTest().main()
