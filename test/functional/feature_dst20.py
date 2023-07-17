#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
import math

from test_framework.evm_key_pair import KeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, int_to_eth_u256
from test_framework.evm_contract import EVMContract

from decimal import Decimal
from web3 import Web3


class DST20(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txordering=2",
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
                "-changiintermediateheight=105",
                "-changiintermediate3height=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
            [
                "-txordering=2",
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
                "-changiintermediateheight=105",
                "-changiintermediate3height=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def run_test(self):
        node = self.nodes[0]
        address = node.get_genesis_keys().ownerAuthAddress

        # Generate chain
        node.generate(105)
        self.nodes[0].utxostoaccount({address: "100@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/feature/evm": "true"}})
        self.nodes[0].generate(1)
        node.transferdomain(
            [
                {
                    "src": {"address": address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": "0xeB4B222C3dE281d40F5EBe8B273106bFcC1C1b94",
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)

        from web3 import Web3

        web3 = Web3(Web3.HTTPProvider(node.get_evm_rpc()))
        web3_n2 = Web3(Web3.HTTPProvider(self.nodes[1].get_evm_rpc()))

        tx = node.createtoken(
            {
                "symbol": "BTC",
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": address,
            }
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

        node.createtoken(
            {
                "symbol": "ETH",
                "name": "ETH token",
                "isDAT": True,
                "collateralAddress": address,
            }
        )
        node.createtoken(
            {
                "symbol": "DUSD",
                "name": "DUSD token",
                "isDAT": True,
                "collateralAddress": address,
            }
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

        contract_address_btc = "0xff00000000000000000000000000000000000001"
        contract_address_eth = "0xff00000000000000000000000000000000000002"
        contract_address_dusd = Web3.to_checksum_address(
            "0xff00000000000000000000000000000000000003"
        )

        abi = open("./lib/ain-rs-exports/dst20/output/abi.json").read()

        btc = web3.eth.contract(address=contract_address_btc, abi=abi)
        print(btc.functions.name().call())
        print(btc.functions.symbol().call())

        eth = web3.eth.contract(address=contract_address_eth, abi=abi)
        print(eth.functions.name().call())
        print(eth.functions.symbol().call())

        dusd = web3.eth.contract(address=contract_address_dusd, abi=abi)
        print(dusd.functions.name().call())
        print(dusd.functions.symbol().call())

        key_pair = KeyPair.from_node(node)

        node.minttokens("10@BTC")
        node.generate(1)

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        node.generate(1)

        assert_equal(
            btc.functions.balanceOf(key_pair.address).call()
            / math.pow(10, btc.functions.decimals().call()),
            Decimal(1),
        )

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        node.generate(1)

        assert_equal(
            btc.functions.balanceOf(key_pair.address).call()
            / math.pow(10, btc.functions.decimals().call()),
            Decimal(2),
        )

        self.nodes[0].transferdomain(
            [
                {
                    "dst": {"address": address, "amount": "1.5@BTC", "domain": 2},
                    "src": {
                        "address": key_pair.address,
                        "amount": "1.5@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        node.generate(1)

        assert_equal(
            btc.functions.balanceOf(key_pair.address).call()
            / math.pow(10, btc.functions.decimals().call()),
            Decimal(0.5),
        )


if __name__ == "__main__":
    DST20().main()
