#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

import math
from decimal import Decimal

from test_framework.evm_key_pair import KeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (assert_equal, assert_raises_rpc_error)



class DST20(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
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
            ]
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

        node.createtoken(
            {
                "symbol": "BTC",
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": address,
            }
        )
        self.nodes[0].generate(1)

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

        contract_address_btc = "0xff00000000000000000000000000000000000001"
        contract_address_eth = "0xff00000000000000000000000000000000000002"
        contract_address_dusd = Web3.to_checksum_address(
            "0xff00000000000000000000000000000000000003"
        )

        abi = open("./lib/ain-contracts/dst20/output/abi.json", encoding="utf-8").read()

        btc = web3.eth.contract(address=contract_address_btc, abi=abi)
        assert_equal(btc.functions.name().call(), "BTC token")
        assert_equal(btc.functions.symbol().call(), "BTC")

        eth = web3.eth.contract(address=contract_address_eth, abi=abi)
        assert_equal(eth.functions.name().call(), "ETH token")
        assert_equal(eth.functions.symbol().call(), "ETH")

        dusd = web3.eth.contract(address=contract_address_dusd, abi=abi)
        assert_equal(dusd.functions.name().call(), "DUSD token")
        assert_equal(dusd.functions.symbol().call(), "DUSD")

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

        [amountBTC] = [x for x in node.getaccount(address) if "BTC" in x]
        assert_equal(amountBTC, "9.00000000@BTC")

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
        [amountBTC] = [x for x in node.getaccount(address) if "BTC" in x]
        assert_equal(amountBTC, "8.00000000@BTC")

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
        [amountBTC] = [x for x in node.getaccount(address) if "BTC" in x]
        assert_equal(amountBTC, "9.50000000@BTC")

        # test multiple transferdomains in the same block
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
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": address, "amount": "2@BTC", "domain": 2},
                    "dst": {
                        "address": key_pair.address,
                        "amount": "2@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        node.generate(1)

        assert_equal(
            btc.functions.balanceOf(key_pair.address).call()
            / math.pow(10, btc.functions.decimals().call()),
            Decimal(3.5),
            )
        [amountBTC] = [x for x in node.getaccount(address) if "BTC" in x]
        assert_equal(amountBTC, "6.50000000@BTC")

        # test transferdomain for contract that does not exist
        assert_raises_rpc_error(0, "Invalid Defi token: XYZ", self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": address, "amount": "1@XYZ", "domain": 2},
                    "dst": {
                        "address": key_pair.address,
                        "amount": "1@XYZ",
                        "domain": 3,
                    },
                }
            ]
        )

        # transferdomain to DST20 token address
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": address, "amount": "2@BTC", "domain": 2},
                    "dst": {
                        "address": contract_address_btc,
                        "amount": "2@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        node.generate(1)

        assert_equal(
            btc.functions.balanceOf(contract_address_btc).call()
            / math.pow(10, btc.functions.decimals().call()),
            Decimal(2),
        )


if __name__ == "__main__":
    DST20().main()