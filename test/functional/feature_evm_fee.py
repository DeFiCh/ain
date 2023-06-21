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

class EVMFeeTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-subsidytest=1', '-txindex=1'],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.ethAddress = '0x9b8a4af42140d8a4c153a822f02571a1dd037e89'
        self.toAddress = '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110'
        self.nodes[0].importprivkey('af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23') # ethAddress
        self.nodes[0].importprivkey('17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35') # toAddress

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].evmtx, self.ethAddress, 0, 21, 21000, self.toAddress, 0.1)

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

    def test_fee_deduction(self):
        height = self.nodes[0].getblockcount()

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(int(balance[2:], 16), 100000000000000000000)

        self.nodes[0].eth_sendTransaction({
            'from': self.ethAddress,
            'to': self.toAddress,
            'value': '0x7148', # 29_000
            'gas': '0x7a120',
            'gasPrice': '0x1',
        })
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        # Deduct 50000. 29000 value + min 21000 call fee
        assert_equal(int(balance[2:], 16), 99999999999999950000)

        self.rollback_to(height)

    def test_high_gas_price(self):
        height = self.nodes[0].getblockcount()

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(int(balance[2:], 16), 100000000000000000000)

        self.nodes[0].eth_sendTransaction({
            'from': self.ethAddress,
            'to': self.toAddress,
            'value': '0x7148', # 29_000
            'gas': '0x7a120',
            'gasPrice': '0x3B9ACA00', # 1_000_000_000
        })
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        # Deduct 21_000_000_029_000. 29_000 value + 21_000 * 1_000_000_000
        assert_equal(int(balance[2:], 16), 99999978999999971000)

        self.rollback_to(height)

    def test_max_gas_price(self):
        height = self.nodes[0].getblockcount()

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(int(balance[2:], 16), 100000000000000000000)

        assert_raises_rpc_error(-32001, "evm tx failed to validate insufficient balance to pay fees", self.nodes[0].eth_sendTransaction, {
            'from': self.ethAddress,
            'to': self.toAddress,
            'value': '0x7148', # 29_000
            'gas': '0x7a120',
            'gasPrice': '0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff',
        })

        self.rollback_to(height)

    def test_gas_limit_higher_than_block_limit(self):
        height = self.nodes[0].getblockcount()

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(int(balance[2:], 16), 100000000000000000000)

        assert_raises_rpc_error(-32001, "evm tx failed to validate Gas limit higher than MAX_GAS_PER_BLOCK", self.nodes[0].eth_sendTransaction, {
            'from': self.ethAddress,
            'to': self.toAddress,
            'value': '0x7148', # 29_000
            'gas': '0x1C9C381', # 30_000_001
            'gasPrice': '0x1',
        })

        self.rollback_to(height)

    def test_fee_deduction_empty_balance(self):
        height = self.nodes[0].getblockcount()

        emptyAddress = self.nodes[0].getnewaddress("", "eth")
        balance = self.nodes[0].eth_getBalance(emptyAddress, "latest")
        assert_equal(int(balance[2:], 16), 000000000000000000000)

        assert_raises_rpc_error(-32001, "evm tx failed to validate insufficient balance to pay fees", self.nodes[0].eth_sendTransaction, {
            'from': emptyAddress,
            'to': self.toAddress,
            'value': '0x7148', # 29_000
            'gas': '0x7a120',
            'gasPrice': '0x1',
        })

        self.rollback_to(height)

    def test_fee_deduction_send_full_balance(self):
        height = self.nodes[0].getblockcount()

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(int(balance[2:], 16), 100000000000000000000)

        self.nodes[0].eth_sendTransaction({
            'from': self.ethAddress,
            'to': self.toAddress,
            'value': balance,
            'gas': '0x7a120',
            'gasPrice': '0x1',
        })
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")

        # Don't consume balance as not enough to cover send value + fee.
        # Deduct only 21000 call fee
        assert_equal(int(balance[2:], 16), 99999999999999979000)

        self.rollback_to(height)

    def run_test(self):
        self.setup()

        self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.ethAddress, "amount":"100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        self.test_fee_deduction()

        self.test_high_gas_price()

        self.test_max_gas_price()

        self.test_gas_limit_higher_than_block_limit()

        self.test_fee_deduction_empty_balance()

        self.test_fee_deduction_send_full_balance()

if __name__ == '__main__':
    EVMFeeTest().main()
