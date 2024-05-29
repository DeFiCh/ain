#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.evm_key_pair import EvmKeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    get_solc_artifact_path,
)

import math
import json
import time
from decimal import Decimal
from web3 import Web3


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
                "-dakotaheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-metachainheight=153",
                "-df23height=200",
                "-subsidytest=1",
            ]
        ]

    def test_invalid_too_long_token_name_dst20_migration_tx(self):
        block_height = self.nodes[0].getblockcount()

        self.node.createtoken(
            {
                "symbol": "TooLongTokenName",
                "name": "TheTokenWithNameMore30ByteLimit",  # 31 bytes
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(2)

        # enable EVM, transferdomain, DVM to EVM transfers and EVM to DVM transfers
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                }
            }
        )
        self.nodes[0].generate(1)

        # Trigger EVM genesis DST20 migration
        assert_equal(self.nodes[0].generatetoaddress(1, self.address, 1), 0)
        self.rollback_to(block_height)

    def test_invalid_utf8_encoding_token_name_dst20_migration_tx(self):
        block_height = self.nodes[0].getblockcount()

        self.node.createtoken(
            {
                "symbol": "InvalidUTF8TokenName",
                "name": "InvalidUTF8TokenNameIsThisOne🤩",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(2)

        # enable EVM, transferdomain, DVM to EVM transfers and EVM to DVM transfers
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                }
            }
        )
        self.nodes[0].generate(1)

        # Trigger EVM genesis DST20 migration
        assert_equal(self.nodes[0].generatetoaddress(1, self.address, 1), 0)
        self.rollback_to(block_height)

    def test_dst20_migration_txs(self):
        block_height = self.nodes[0].getblockcount()

        self.node.createtoken(
            {
                "symbol": "USDT",
                "name": "USDT token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.node.createtoken(
            {
                "symbol": "BTC",
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.node.createtoken(
            {
                "symbol": "ETH",
                "name": "ETH token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        # create DST20 token with maximum byte size limit
        self.node.createtoken(
            {
                "symbol": "Test",
                "name": "TheTokenWithNameMax30ByteLimit",  # 30 bytes
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(2)

        # enable EVM, transferdomain, DVM to EVM transfers and EVM to DVM transfers
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                }
            }
        )
        self.nodes[0].generate(1)

        # Trigger EVM genesis DST20 migration
        self.nodes[0].generate(1)

        # should have code on contract address
        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_btc)
            )
            != "0x"
        )
        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_eth)
            )
            != "0x"
        )
        self.btc = self.nodes[0].w3.eth.contract(
            address=self.contract_address_btc, abi=self.abi
        )
        assert_equal(self.btc.functions.name().call(), "BTC token")
        assert_equal(self.btc.functions.symbol().call(), "BTC")

        self.eth = self.nodes[0].w3.eth.contract(
            address=self.contract_address_eth, abi=self.abi
        )
        assert_equal(self.eth.functions.name().call(), "ETH token")
        assert_equal(self.eth.functions.symbol().call(), "ETH")

        # Check that migration has associated EVM TX and receipt
        block = self.nodes[0].eth_getBlockByNumber("latest")
        all_tokens = self.nodes[0].listtokens()
        # Keep only DAT non-DFI tokens
        loanTokens = [
            token
            for token in all_tokens.values()
            if token["isDAT"] == True and token["symbol"] != "DFI"
        ]
        # 1 extra deployment TX (for transfer domain deploy contract)
        assert_equal(len(block["transactions"]), len(loanTokens) + 5)

        # check USDT migration
        usdt_tx = block["transactions"][5]
        receipt = self.nodes[0].eth_getTransactionReceipt(usdt_tx)
        tx1 = self.nodes[0].eth_getTransactionByHash(usdt_tx)
        assert_equal(
            self.w0.to_checksum_address(receipt["contractAddress"]),
            self.contract_address_usdt,
        )
        assert_equal(receipt["from"], tx1["from"])
        assert_equal(receipt["gasUsed"], "0x0")
        assert_equal(receipt["logs"], [])
        assert_equal(receipt["status"], "0x1")
        assert_equal(receipt["to"], None)

        assert_equal(
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_usdt)
            ),
            self.bytecode,
        )

        # check BTC migration
        btc_tx = block["transactions"][6]
        receipt = self.nodes[0].eth_getTransactionReceipt(btc_tx)
        tx2 = self.nodes[0].eth_getTransactionByHash(btc_tx)
        assert_equal(
            self.w0.to_checksum_address(receipt["contractAddress"]),
            self.contract_address_btc,
        )
        assert_equal(receipt["from"], tx2["from"])
        assert_equal(receipt["gasUsed"], "0x0")
        assert_equal(receipt["logs"], [])
        assert_equal(receipt["status"], "0x1")
        assert_equal(receipt["to"], None)

        assert_equal(
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_btc)
            ),
            self.bytecode,
        )

        # check ETH migration
        eth_tx = block["transactions"][7]
        receipt = self.nodes[0].eth_getTransactionReceipt(eth_tx)
        tx3 = self.nodes[0].eth_getTransactionByHash(eth_tx)
        assert_equal(
            self.w0.to_checksum_address(receipt["contractAddress"]),
            self.contract_address_eth,
        )
        assert_equal(receipt["from"], tx3["from"])
        assert_equal(receipt["gasUsed"], "0x0")
        assert_equal(receipt["logs"], [])
        assert_equal(receipt["status"], "0x1")
        assert_equal(receipt["to"], None)

        assert_equal(
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_eth)
            ),
            self.bytecode,
        )

        assert_equal(tx1["input"], tx2["input"])
        assert_equal(tx2["input"], tx3["input"])

        self.rollback_to(block_height)

    def test_unused_dst20(self):
        # should have system reserved bytecode
        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_unused)
            )
            != self.reserved_bytecode
        )

    def test_pre_evm_token(self):
        # should have code on contract address
        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_usdt)
            )
            != self.reserved_bytecode
        )

        # check contract variables
        self.usdt = self.nodes[0].w3.eth.contract(
            address=self.contract_address_usdt, abi=self.abi
        )
        assert_equal(self.usdt.functions.name().call(), "USDT token")
        assert_equal(self.usdt.functions.symbol().call(), "USDT")

        # check transferdomain
        [beforeUSDT] = [x for x in self.node.getaccount(self.address) if "USDT" in x]
        assert_equal(beforeUSDT, "10.00000000@USDT")
        self.node.transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@USDT", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@USDT",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        [afterUSDT] = [x for x in self.node.getaccount(self.address) if "USDT" in x]

        assert_equal(
            self.usdt.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.usdt.functions.decimals().call()),
            Decimal(1),
        )
        assert_equal(afterUSDT, "9.00000000@USDT")

        self.node.transferdomain(
            [
                {
                    "src": {
                        "address": self.key_pair.address,
                        "amount": "1@USDT",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "1@USDT", "domain": 2},
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        [afterUSDT] = [x for x in self.node.getaccount(self.address) if "USDT" in x]
        assert_equal(afterUSDT, "10.00000000@USDT")
        assert_equal(
            self.usdt.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.usdt.functions.decimals().call()),
            Decimal(0),
        )

    def test_deploy_token(self):
        # should have no code on contract address
        assert_equal(
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_btc)
            ),
            self.reserved_bytecode,
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
            != self.reserved_bytecode
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
            self.reserved_bytecode,
        )
        assert_equal(
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_dusd)
            ),
            self.reserved_bytecode,
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
            != self.reserved_bytecode
        )
        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(self.contract_address_dusd)
            )
            != self.reserved_bytecode
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
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )

        # test totalSupply variable
        assert_equal(
            self.btc.functions.totalSupply().call()
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
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
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
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0.5),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0.5),
        )
        [amountBTC] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(amountBTC, "9.50000000@BTC")

    def test_multiple_dvm_evm_bridge(self):
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.erc55_address)
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "nonce": nonce,
                    "singlekeycheck": False,
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
                    "nonce": nonce + 1,
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(3.5),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
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
                    "singlekeycheck": False,
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
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        [afterAmount] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(
            self.btc.functions.balanceOf(self.key_pair2.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(3.5),
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
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(self.key_pair2.address).call(), Decimal(0)
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(3.5),
        )
        [afterAmount] = [x for x in self.node.getaccount(self.address) if "BTC" in x]
        assert_equal(beforeAmount, afterAmount)

        assert_equal(
            len(self.node.getrawmempool()), 1
        )  # failed tx should be in mempool
        self.node.clearmempool()

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
                    "singlekeycheck": False,
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
                    "singlekeycheck": False,
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
                    "singlekeycheck": False,
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
        self.nodes[0].generate(10)  # activate prices

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
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)
        [TSLAAmount] = [x for x in self.node.getaccount(self.address) if "TSLA" in x]
        assert_equal(TSLAAmount, "98.00000000@TSLA")
        assert_equal(
            self.tsla.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.tsla.functions.decimals().call()),
            Decimal(2),
        )
        assert_equal(
            self.tsla.functions.totalSupply().call()
            / math.pow(10, self.tsla.functions.decimals().call()),
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
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)
        [TSLAAmount] = [x for x in self.node.getaccount(self.address) if "TSLA" in x]
        assert_equal(TSLAAmount, "99.00000000@TSLA")
        assert_equal(
            self.tsla.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.tsla.functions.decimals().call()),
            Decimal(1),
        )
        assert_equal(
            self.tsla.functions.totalSupply().call()
            / math.pow(10, self.tsla.functions.decimals().call()),
            Decimal(1),
        )

    def test_dst20_back_and_forth(self):
        self.rollback_to(self.start_height)

        self.node.transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)
        block = self.nodes[0].eth_getBlockByNumber("latest", True)
        sender_address = Web3.to_checksum_address(block["transactions"][0]["from"])

        assert_equal(
            self.btc.functions.balanceOf(sender_address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )
        assert_equal(
            self.btc.functions.balanceOf(self.contract_address_transfer_domain).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(sender_address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
        )
        assert_equal(
            self.btc.functions.balanceOf(self.contract_address_transfer_domain).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
        )

        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.address,
                        "amount": "1@BTC",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(sender_address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )
        assert_equal(
            self.btc.functions.balanceOf(self.contract_address_transfer_domain).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )

        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.key_pair.address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.address,
                        "amount": "1@BTC",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        assert_equal(
            self.btc.functions.balanceOf(sender_address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.balanceOf(self.key_pair.address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.balanceOf(self.contract_address_transfer_domain).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )

    def test_rename_dst20(self):
        # Reset test
        self.rollback_to(self.start_height)

        # Move to fork height
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        # Update token name
        self.nodes[0].updatetoken(
            "BTC",
            {
                "name": "Litecoin",
                "symbol": "LTC",
            },
        )
        self.nodes[0].generate(1)

        # Check that update has associated EVM TX and receipt
        update_tx = self.nodes[0].eth_getBlockByNumber("latest")["transactions"][0]
        receipt = self.nodes[0].eth_getTransactionReceipt(update_tx)
        tx = self.nodes[0].eth_getTransactionByHash(update_tx)
        assert_equal(
            self.w0.to_checksum_address(receipt["contractAddress"]),
            self.contract_address_btc,
        )
        assert_equal(receipt["from"], tx["from"])
        assert_equal(receipt["gasUsed"], "0x0")
        assert_equal(receipt["logs"], [])
        assert_equal(receipt["status"], "0x1")
        assert_equal(receipt["to"], None)

        # Check contract variables
        self.btc = self.nodes[0].w3.eth.contract(
            address=self.contract_address_btc, abi=self.abi
        )
        assert_equal(self.btc.functions.name().call(), "Litecoin")
        assert_equal(self.btc.functions.symbol().call(), "LTC")

        assert_raises_rpc_error(
            -32600,
            "Invalid token symbol",
            self.nodes[0].updatetoken,
            "LTC",
            {"symbol": "LT#C"},
        )

        # Move back to fork height
        self.rollback_to(200)

        # Check contract variables
        self.btc = self.nodes[0].w3.eth.contract(
            address=self.contract_address_btc, abi=self.abi
        )
        assert_equal(self.btc.functions.name().call(), "BTC token")
        assert_equal(self.btc.functions.symbol().call(), "BTC")

        # Setup oracle
        address = self.nodes[0].getnewaddress("", "legacy")
        prices = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "TSLA"},
        ]
        self.nodes[0].appointoracle(address, prices, 10)
        self.nodes[0].generate(1)

        # Create loan token
        self.nodes[0].setloantoken(
            {
                "symbol": "TSLA",
                "name": "Tesla Token",
                "fixedIntervalPriceId": "TSLA/USD",
                "mintable": True,
                "interest": 0.01,
            }
        )
        self.nodes[0].generate(1)

        # Check DST token
        self.tsla = self.nodes[0].w3.eth.contract(
            address=self.contract_address_tsla, abi=self.abi
        )

        assert_equal(self.tsla.functions.name().call(), "Tesla Token")
        assert_equal(self.tsla.functions.symbol().call(), "TSLA")

        # Update via loan token
        self.nodes[0].updateloantoken(
            "TSLA",
            {
                "symbol": "META",
                "name": "Meta",
            },
        )
        self.nodes[0].generate(1)

        # Check contract variables
        self.tsla = self.nodes[0].w3.eth.contract(
            address=self.contract_address_tsla, abi=self.abi
        )

        assert_equal(self.tsla.functions.name().call(), "Meta")
        assert_equal(self.tsla.functions.symbol().call(), "META")

        # Check invalid symbol
        assert_raises_rpc_error(
            -32600,
            "Invalid token symbol",
            self.nodes[0].updateloantoken,
            "META",
            {
                "symbol": "ME#/TA",
                "name": "Me#/ta",
            },
        )

        # Update again loan token
        self.nodes[0].updateloantoken(
            "META",
            {
                "symbol": "FB",
                "name": "Facebook",
            },
        )
        self.nodes[0].generate(1)

        # Check contract variables
        self.tsla = self.nodes[0].w3.eth.contract(
            address=self.contract_address_tsla, abi=self.abi
        )

        assert_equal(self.tsla.functions.name().call(), "Facebook")
        assert_equal(self.tsla.functions.symbol().call(), "FB")

    def run_test(self):
        self.node = self.nodes[0]
        self.w0 = self.node.w3
        self.address = self.node.get_genesis_keys().ownerAuthAddress
        self.erc55_address = self.node.addressmap(self.address, 1)["format"]["erc55"]

        # Contract addresses
        self.contract_address_transfer_domain = self.w0.to_checksum_address(
            "0xdf00000000000000000000000000000000000001"
        )
        self.contract_address_usdt = self.w0.to_checksum_address(
            "0xff00000000000000000000000000000000000001"
        )
        self.contract_address_btc = self.w0.to_checksum_address(
            "0xff00000000000000000000000000000000000002"
        )
        self.contract_address_eth = self.w0.to_checksum_address(
            "0xff00000000000000000000000000000000000003"
        )
        self.contract_address_dusd = self.w0.to_checksum_address(
            "0xff00000000000000000000000000000000000004"
        )
        self.contract_address_tsla = self.w0.to_checksum_address(
            "0xff00000000000000000000000000000000000005"
        )
        self.contract_address_unused = self.w0.to_checksum_address(
            "0xff000000000000000000000000000000000000aa"
        )

        # Contract ABI
        # Implementation ABI since we want to call functions from the implementation
        self.abi = open(
            get_solc_artifact_path("dst20_v1", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        # Proxy bytecode since we want to check proxy deployment
        self.bytecode = json.loads(
            open(
                get_solc_artifact_path("dst20", "deployed_bytecode.json"),
                "r",
                encoding="utf8",
            ).read()
        )["object"]

        self.reserved_bytecode = json.loads(
            open(
                get_solc_artifact_path("dfi_reserved", "deployed_bytecode.json"),
                "r",
                encoding="utf8",
            ).read()
        )["object"]

        # Generate chain
        self.node.generate(150)
        self.nodes[0].utxostoaccount({self.address: "1000@DFI"})

        # pre-metachain fork height
        self.nodes[0].generate(1)

        # Create token and check DST20 migration pre EVM activation
        self.test_invalid_too_long_token_name_dst20_migration_tx()
        self.test_invalid_utf8_encoding_token_name_dst20_migration_tx()
        self.test_dst20_migration_txs()

        # Create token before EVM
        self.node.createtoken(
            {
                "symbol": "USDT",
                "name": "USDT token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.node.generate(1)
        self.node.minttokens("10@USDT")
        self.node.generate(1)

        self.key_pair = EvmKeyPair.from_node(self.node)
        self.key_pair2 = EvmKeyPair.from_node(self.node)

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
        self.nodes[0].generate(2)

        self.test_pre_evm_token()

        self.test_deploy_token()

        self.test_deploy_multiple_tokens()

        self.node.minttokens("10@BTC")
        self.node.generate(1)
        self.start_height = self.nodes[
            0
        ].getblockcount()  # Use post-token deployment as start height

        self.test_dst20_dvm_to_evm_bridge()

        self.test_dst20_evm_to_dvm_bridge()

        self.test_multiple_dvm_evm_bridge()

        self.test_conflicting_bridge()

        self.test_invalid_token()

        self.test_bridge_when_no_balance()

        self.test_negative_transfer()

        self.test_different_tokens()

        self.test_loan_token()

        self.test_dst20_back_and_forth()

        self.test_rename_dst20()


if __name__ == "__main__":
    DST20().main()
