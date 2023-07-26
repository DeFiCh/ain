#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

import math
from decimal import Decimal
from web3 import Web3

from test_framework.evm_key_pair import KeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


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

    def test_deploy_token(self):
        # should have no code on contract address
        assert_equal(
            Web3.to_hex(self.web3.eth.get_code(self.contract_address_btc)), "0x"
        )

        self.node.createtoken(
            {
                "symbol": "BTC",
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)

        # should have code on contract address
        assert Web3.to_hex(self.web3.eth.get_code(self.contract_address_btc)) != "0x"

        # check contract variables
        self.btc = self.web3.eth.contract(
            address=self.contract_address_btc, abi=self.abi
        )
        assert_equal(self.btc.functions.name().call(), "BTC token")
        assert_equal(self.btc.functions.symbol().call(), "BTC")

    def test_deploy_multiple_tokens(self):
        # should have no code on contract addresses
        assert_equal(
            Web3.to_hex(self.web3.eth.get_code(self.contract_address_eth)), "0x"
        )
        assert_equal(
            Web3.to_hex(self.web3.eth.get_code(self.contract_address_dusd)), "0x"
        )

        self.node.createtoken(
            {
                "symbol": "ETH",
                "name": "ETH token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.node.createtoken(
            {
                "symbol": "DUSD",
                "name": "DUSD token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.node.generate(1)

        # should have code on contract address
        assert Web3.to_hex(self.web3.eth.get_code(self.contract_address_eth)) != "0x"
        assert Web3.to_hex(self.web3.eth.get_code(self.contract_address_dusd)) != "0x"

        # check contract variables
        self.eth = self.web3.eth.contract(
            address=self.contract_address_eth, abi=self.abi
        )
        assert_equal(self.eth.functions.name().call(), "ETH token")
        assert_equal(self.eth.functions.symbol().call(), "ETH")

        self.dusd = self.web3.eth.contract(
            address=self.contract_address_dusd, abi=self.abi
        )
        assert_equal(self.dusd.functions.name().call(), "DUSD token")
        assert_equal(self.dusd.functions.symbol().call(), "DUSD")

    def test_dst20_dvm_to_evm_bridge(self):
        self.node.transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )

        [amountBTC] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(amountBTC, "9.00000000@BTC")

        # transfer again to check if state is updated instead of being overwritten by new value
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
        )
        [amountBTC] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(amountBTC, "8.00000000@BTC")

    def test_dst20_evm_to_dvm_bridge(self):
        self.node.transferdomain(
            [
                {
                    "dst": {"address": self.address, "amount": "1.5@BTC", "domain": 2},
                    "src": {
                        "address": self.key_pair.address,
                        "amount": "1.5@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0.5),
        )
        [amountBTC] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(amountBTC, "9.50000000@BTC")

    def test_multiple_dvm_evm_bridge(self):
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "2@BTC", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "2@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(3.5),
        )
        [amountBTC] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(amountBTC, "6.50000000@BTC")

    def test_conflicting_bridge(self):
        [beforeAmount] = [x for x in self.node.getaccount(self.address) if "BTC" in x]

        # should be processed in order so balance is bridged to and back in the same block
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.key_pair2.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.key_pair2.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "1@BTC", "domain": 2},
                }
            ]
        )
        self.node.generate(1)

        [afterAmount] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(
            self.btc.functions.balanceOf(self.key_pair2.address).call(), Decimal(0)
        )
        assert_equal(beforeAmount, afterAmount)

    def test_bridge_when_no_balance(self):
        assert_equal(
            self.btc.functions.balanceOf(self.key_pair2.address).call(), Decimal(0)
        )
        [beforeAmount] = [x for x in self.node.getaccount(self.address) if "BTC" in x]

        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.key_pair2.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "1@BTC", "domain": 2},
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair2.address).call(), Decimal(0)
        )
        [afterAmount] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(beforeAmount, afterAmount)

    def test_invalid_token(self):
        # DVM to EVM
        assert_raises_rpc_error(
            0,
            "Invalid Defi token: XYZ",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "1@XYZ", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@XYZ",
                        "domain": 3,
                    },
                }
            ],
        )

        # EVM to DVM
        assert_raises_rpc_error(
            0,
            "Invalid Defi token: XYZ",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.key_pair.address,
                        "amount": "1@XYZ",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "1@XYZ", "domain": 2},
                }
            ],
        )

    def test_transfer_to_token_address(self):
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "2@BTC", "domain": 2},
                    "dst": {
                        "address": self.contract_address_btc,
                        "amount": "2@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.contract_address_btc).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
            )


    def run_test(self):
        self.node = self.nodes[0]
        self.address = self.node.get_genesis_keys().ownerAuthAddress
        self.web3 = Web3(Web3.HTTPProvider(self.node.get_evm_rpc()))

        # Contract addresses
        self.contract_address_btc = "0xff00000000000000000000000000000000000001"
        self.contract_address_eth = "0xff00000000000000000000000000000000000002"
        self.contract_address_dusd = Web3.to_checksum_address(
            "0xff00000000000000000000000000000000000003"
        )

        # Contract ABI
        self.abi = open(
            "./lib/ain-contracts/dst20/output/abi.json", encoding="utf-8"
        ).read()

        # Generate chain
        self.node.generate(105)
        self.nodes[0].utxostoaccount({self.address: "100@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/feature/evm": "true"}})
        self.nodes[0].generate(1)

        self.test_deploy_token()
        self.test_deploy_multiple_tokens()

        self.key_pair = KeyPair.from_node(self.node)
        self.key_pair2 = KeyPair.from_node(self.node)
        self.node.minttokens("10@BTC")
        self.node.generate(1)

        self.test_dst20_dvm_to_evm_bridge()
        self.test_dst20_evm_to_dvm_bridge()
        self.test_multiple_dvm_evm_bridge()
        self.test_conflicting_bridge()
        self.test_invalid_token()
        self.test_transfer_to_token_address()

        # node crashes due to miner issue, crash is fixed by https://github.com/DeFiCh/ain/pull/2221/.
        # self.test_bridge_when_no_balance()

if __name__ == "__main__":
    DST20().main()
