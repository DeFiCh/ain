#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
from test_framework.evm_key_pair import KeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256
)
from test_framework.evm_contract import EVMContract

from decimal import Decimal
from web3 import Web3


class DST20(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate3height=105', '-subsidytest=1', '-txindex=1'],
        ]

    def run_test(self):
        node = self.nodes[0]
        address = node.get_genesis_keys().ownerAuthAddress

        # Generate chain
        node.generate(105)
        self.nodes[0].utxostoaccount({address: "50@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)
        node.transferdomain([{"src": {"address": address, "amount": "50@DFI", "domain": 2},
                              "dst": {"address": "0xeB4B222C3dE281d40F5EBe8B273106bFcC1C1b94", "amount": "50@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        node.createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": address
        })
        self.nodes[0].generate(1)

        from web3 import Web3
        web3 = Web3(Web3.HTTPProvider(node.get_evm_rpc()))

        print(web3.eth.get_block("latest"), True)
        print(web3.eth.get_code(Web3.to_checksum_address("0x5a433c6717e21e9e1c3d376ca859cd81901529a9")))
        print(web3.eth.get_transaction_receipt("0x6c70f61286bcaef4f2a08d041789ec8b5e0f67055668680eb91d5478a577cf02"))

        raise Exception()



if __name__ == '__main__':
    DST20().main()
