#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

import math
import time
from decimal import Decimal
import os

from test_framework.evm_key_pair import EvmKeyPair
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
                "-subsidytest=1",
                "-txindex=1",
            ]
        ]

    def test_deploy_token(self):
        # should have no code on contract address
        assert_equal(
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_btc)
            ),
            "0x",
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
        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_btc)
            )
            != "0x"
        )

        # check contract variables
        self.btc = self.nodes[0].w3.eth.contract(
            address=self.contract_address_btc, abi=self.abi
        )
        assert_equal(self.btc.functions.name().call(), "BTC token")
        assert_equal(self.btc.functions.symbol().call(), "BTC")

    def test_deploy_multiple_tokens(self):
        # should have no code on contract addresses
        assert_equal(
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_eth)
            ),
            "0x",
        )
        assert_equal(
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_dusd)
            ),
            "0x",
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
        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_eth)
            )
            != "0x"
        )
        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_dusd)
            )
            != "0x"
        )

        # check contract variables
        self.eth = self.nodes[0].w3.eth.contract(
            address=self.contract_address_eth, abi=self.abi
        )
        assert_equal(self.eth.functions.name().call(), "ETH token")
        assert_equal(self.eth.functions.symbol().call(), "ETH")

        self.dusd = self.nodes[0].w3.eth.contract(
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

    def test_negative_transfer(self):
        assert_raises_rpc_error(
            -3,
            "Amount out of range",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "-1@BTC", "domain": 2},
                    "dst": {
                        "address": self.contract_address_btc,
                        "amount": "-1@BTC",
                        "domain": 3,
                    },
                }
            ],
        )

    def test_different_tokens(self):
        assert_raises_rpc_error(
            -32600,
            "Source token and destination token must be the same",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.contract_address_btc,
                        "amount": "1@ETH",
                        "domain": 3,
                    },
                }
            ],
        )

    def test_loan_token(self):
        # setup oracle
        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "TSLA"},
        ]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "5@TSLA"},
        ]
        mock_time = int(time.time())
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(oracle_id1, mock_time, oracle1_prices)
        self.nodes[0].generate(8)  # activate prices

        # set price again
        timestamp = int(time.time())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        # create loan token
        self.nodes[0].setloantoken(
            {
                "symbol": "TSLA",
                "name": "Tesla Token",
                "fixedIntervalPriceId": "TSLA/USD",
                "mintable": True,
                "interest": 0.01,
            }
        )
        self.nodes[0].createloanscheme(200, 1, "LOAN0001")
        self.nodes[0].setcollateraltoken(
            {"token": "DFI", "factor": 1, "fixedIntervalPriceId": "DFI/USD"}
        )
        self.nodes[0].generate(1)
        self.nodes[0].generate(1)

        # mint loan token
        vaultId1 = self.nodes[0].createvault(self.address, "")
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId1, self.address, "1000@DFI")
        self.nodes[0].generate(1)

        # take loan
        self.nodes[0].takeloan({"vaultId": vaultId1, "amounts": "100@TSLA"})
        self.nodes[0].generate(1)

        # check DST token
        self.tsla = self.nodes[0].w3.eth.contract(
            address=self.contract_address_tsla, abi=self.abi
        )

        assert_equal(self.tsla.functions.name().call(), "Tesla Token")
        assert_equal(self.tsla.functions.symbol().call(), "TSLA")

        # check DVM-EVM transferdomain
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "2@TSLA", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "2@TSLA",
                        "domain": 3,
                    },
                }
            ]
        )
        self.node.generate(1)
        [TSLAAmount] = [x for x in self.node.getaccount(self.address) if "TSLA" in x]
        assert_equal(TSLAAmount, "98.00000000@TSLA")
        assert_equal(
            self.tsla.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
        )

        # check EVM-DVM transferdomain
        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.key_pair.address,
                        "amount": "1@TSLA",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "1@TSLA", "domain": 2},
                }
            ]
        )
        self.node.generate(1)
        [TSLAAmount] = [x for x in self.node.getaccount(self.address) if "TSLA" in x]
        assert_equal(TSLAAmount, "99.00000000@TSLA")
        assert_equal(
            self.tsla.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )

    def run_test(self):
        self.node = self.nodes[0]
        self.address = self.node.get_genesis_keys().ownerAuthAddress

        # Contract addresses
        self.contract_address_btc = "0xff00000000000000000000000000000000000001"
        self.contract_address_eth = "0xff00000000000000000000000000000000000002"
        self.contract_address_dusd = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000003"
        )
        self.contract_address_tsla = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000004"
        )

        # Contract ABI
        # Temp. workaround
        self.abi = open(
            f"{os.path.dirname(__file__)}/../../lib/ain-contracts/dst20/output/abi.json",
            "r",
            encoding="utf8",
        ).read()

        # Generate chain
        self.node.generate(150)
        self.nodes[0].utxostoaccount({self.address: "1000@DFI"})

        # enable EVM, transferdomain, DVM to EVM transfers and EVM to DVM transfers
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                }
            }
        )

        self.nodes[0].generate(1)

        self.test_deploy_token()
        self.test_deploy_multiple_tokens()

        self.key_pair = EvmKeyPair.from_node(self.node)
        self.key_pair2 = EvmKeyPair.from_node(self.node)
        self.node.minttokens("10@BTC")
        self.node.generate(1)

        self.test_dst20_dvm_to_evm_bridge()
        self.test_dst20_evm_to_dvm_bridge()
        self.test_multiple_dvm_evm_bridge()
        self.test_conflicting_bridge()
        self.test_invalid_token()
        self.test_transfer_to_token_address()
        self.test_bridge_when_no_balance()
        self.test_negative_transfer()
        self.test_different_tokens()
        self.test_loan_token()


if __name__ == "__main__":
    DST20().main()
