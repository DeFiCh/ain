#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256,
    get_solc_artifact_path,
)
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair

import json
import math
from decimal import Decimal


def transfer_domain(node, fromAddr, toAddr, amount, fromDomain, toDomain):
    return node.transferdomain(
        [
            {
                "src": {"address": fromAddr, "amount": amount, "domain": fromDomain},
                "dst": {
                    "address": toAddr,
                    "amount": amount,
                    "domain": toDomain,
                },
                "singlekeycheck": False,
            }
        ]
    )


class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        node_args = [
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
            "-metachainheight=150",
            "-subsidytest=1",
        ]
        self.extra_args = [node_args, node_args]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address_2nd = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        self.eth_address = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.eth_address_bech32 = "bcrt1qta8meuczw0mhqupzjl5wplz47xajz0dn0wxxr8"
        self.eth_address_privkey = (
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )
        self.eth_address1 = self.nodes[0].getnewaddress("", "erc55")
        self.no_auth_eth_address = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.address_erc55 = self.nodes[0].addressmap(self.address, 1)["format"][
            "erc55"
        ]
        self.contract_address = "0xdF00000000000000000000000000000000000001"

        # Implementation ABI since we want to call functions from the implementation
        self.abi = open(
            get_solc_artifact_path("transfer_domain_v1", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        symbolDFI = "DFI"
        symbolBTC = "BTC"
        self.symbolBTCDFI = "BTC-DFI"
        symbolUSER = "USER"

        # Import eth_address and validate Bech32 eqivilent is part of the wallet
        self.nodes[0].importprivkey(self.eth_address_privkey)
        result = self.nodes[0].getaddressinfo(self.eth_address_bech32)
        assert_equal(
            result["scriptPubKey"], "00145f4fbcf30273f770702297e8e0fc55f1bb213db3"
        )
        assert_equal(
            result["pubkey"],
            "021286647f7440111ab928bdea4daa42533639c4567d81eca0fff622fb6438eae3",
        )
        assert_equal(result["ismine"], True)
        assert_equal(result["iswitness"], True)

        # Generate chain
        self.nodes[0].generate(145)

        # Create DAT token to be used in tests
        self.nodes[0].createtoken(
            {
                "symbol": symbolBTC,
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )

        # Create non-DAT token to be used in tests
        userTx = self.nodes[0].createtoken(
            {
                "symbol": symbolUSER,
                "name": "Non-DAT token",
                "isDAT": False,
                "collateralAddress": self.address,
            }
        )

        # Fund DFI address
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].generate(1)

        self.nodes[0].minttokens("100@BTC")
        self.nodes[0].minttokens("100@USER#128")

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        idUSER = list(self.nodes[0].gettoken(userTx).keys())[0]
        self.symbolUSER = self.nodes[0].gettoken(idUSER)[idUSER]["symbolKey"]

        # create pool
        self.nodes[0].createpoolpair(
            {
                "tokenA": idBTC,
                "tokenB": idDFI,
                "commission": 1,
                "status": True,
                "ownerAddress": self.address,
                "pairSymbol": self.symbolBTCDFI,
            },
            [],
        )
        self.nodes[0].generate(1)

        # check tokens id
        pool = self.nodes[0].getpoolpair(self.symbolBTCDFI)
        idDFIBTC = list(self.nodes[0].gettoken(self.symbolBTCDFI).keys())[0]
        assert pool[idDFIBTC]["idTokenA"] == idBTC
        assert pool[idDFIBTC]["idTokenB"] == idDFI

        # transfer
        self.nodes[0].addpoolliquidity(
            {self.address: ["1@" + symbolBTC, "100@" + symbolDFI]}, self.address, []
        )
        self.nodes[0].generate(1)

    def invalid_before_fork_and_disabled(self):
        assert_raises_rpc_error(
            -32600,
            "called before Metachain height",
            lambda: transfer_domain(
                self.nodes[0], self.address, self.eth_address, "100@DFI", 2, 3
            ),
        )

        # Move to fork height
        self.nodes[0].generate(2)

        assert_raises_rpc_error(
            -32600,
            "Cannot create tx, transfer domain is not enabled",
            lambda: transfer_domain(
                self.nodes[0], self.address, self.eth_address, "100@DFI", 2, 3
            ),
        )

        # Activate EVM
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/feature/evm": "true"}})
        # TODO: Check EVM disabled on gen +1
        # TODO: Check EVM enabled on gen +2
        self.nodes[0].generate(2)

        # Check error before transferdomain enabled
        assert_raises_rpc_error(
            -32600,
            "Cannot create tx, transfer domain is not enabled",
            lambda: transfer_domain(
                self.nodes[0], self.address, self.eth_address, "100@DFI", 2, 3
            ),
        )

        # Activate transferdomain
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "false",
                    "v0/transferdomain/dvm-evm/src-formats": ["p2pkh", "bech32"],
                    "v0/transferdomain/dvm-evm/dest-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/enabled": "false",
                    "v0/transferdomain/evm-dvm/src-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/auth-formats": ["bech32-erc55"],
                    "v0/transferdomain/evm-dvm/dest-formats": ["p2pkh", "bech32"],
                }
            }
        )
        self.nodes[0].generate(2)

        assert_raises_rpc_error(
            -32600,
            "DVM to EVM is not currently enabled",
            lambda: transfer_domain(
                self.nodes[0], self.address, self.eth_address, "100@DFI", 2, 3
            ),
        )
        assert_raises_rpc_error(
            -32600,
            "EVM to DVM is not currently enabled",
            lambda: transfer_domain(
                self.nodes[0], self.address, self.eth_address, "100@DFI", 3, 2
            ),
        )

        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/evm-dvm/enabled": "true",
                }
            }
        )
        self.nodes[0].generate(1)

        assert_raises_rpc_error(
            -32600,
            "transferdomain for DST20 from DVM to EVM is not enabled",
            lambda: transfer_domain(
                self.nodes[0], self.address, self.eth_address, "1@BTC", 2, 3
            ),
        )
        assert_raises_rpc_error(
            -32600,
            "transferdomain for DST20 from EVM to DVM is not enabled",
            lambda: transfer_domain(
                self.nodes[0], self.address, self.eth_address, "1@BTC", 3, 2
            ),
        )

        # Activate DAT transferdomain
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                }
            }
        )
        self.nodes[0].generate(1)

        self.start_height = self.nodes[0].getblockcount()

    def setup_after_evm_activation(self):
        self.rollback_to(self.start_height)

        # Check initial balances
        self.dfi_balance = self.nodes[0].getaccount(self.address, {}, True)["0"]
        self.eth_balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(self.dfi_balance, Decimal("101"))
        assert_equal(self.eth_balance, int_to_eth_u256(0))
        assert_equal(len(self.nodes[0].getaccount(self.eth_address, {}, True)), 0)

        self.contract_address_btc = self.nodes[0].w3.to_checksum_address(
            "0xff00000000000000000000000000000000000001"
        )
        self.btc = self.nodes[0].w3.eth.contract(
            address=self.contract_address_btc, abi=self.abi
        )
        self.bytecode = json.loads(
            open(
                get_solc_artifact_path("dst20", "deployed_bytecode.json"),
                "r",
                encoding="utf8",
            ).read()
        )["object"]
        assert_equal(self.btc.functions.name().call(), "BTC token")
        assert_equal(self.btc.functions.symbol().call(), "BTC")

        # check BTC migration
        genesis_block = self.nodes[0].eth_getBlockByNumber("0x0")
        btc_tx = genesis_block["transactions"][5]
        receipt = self.nodes[0].eth_getTransactionReceipt(btc_tx)
        tx = self.nodes[0].eth_getTransactionByHash(btc_tx)
        assert_equal(
            self.nodes[0].w3.to_checksum_address(receipt["contractAddress"]),
            self.contract_address_btc,
        )
        assert_equal(receipt["from"], tx["from"])
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

    def invalid_parameters(self):
        self.rollback_to(self.start_height)

        # Check for invalid parameters in transferdomain rpc
        assert_raises_rpc_error(
            -8,
            'Invalid parameters, src argument "address" must not be null',
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -8,
            'Invalid parameters, src argument "amount" must not be null',
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "domain": 2},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -8,
            'Invalid parameters, src argument "domain" must not be null',
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI"},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -1,
            "JSON value is not an integer as expected",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.address,
                        "amount": "100@DFI",
                        "domain": "dvm",
                    },
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -1,
            "JSON value is not an integer as expected",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": "evm",
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -8,
            'Invalid parameters, src argument "domain" must be either 2 (DFI token to EVM) or 3 (EVM to DFI token)',
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 0},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -32600,
            "Unknown transfer domain aspect",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 4,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -8,
            "Invalid src address provided",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": "blablabla", "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -8,
            "Invalid dst address provided",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {"address": "blablabla", "amount": "100@DFI", "domain": 3},
                    "singlekeycheck": False,
                }
            ],
        )

    def invalid_values_dvm_evm(self):
        self.rollback_to(self.start_height)

        assert_raises_rpc_error(
            -32600,
            'Dst address must be an ERC55 address in case of "EVM" domain',
            lambda: transfer_domain(
                self.nodes[0], self.address, self.address, "100@DFI", 2, 3
            ),
        )
        assert_raises_rpc_error(
            -32600,
            "Cannot transfer inside same domain",
            lambda: transfer_domain(
                self.nodes[0], self.address, self.eth_address, "100@DFI", 2, 2
            ),
        )
        assert_raises_rpc_error(
            -32600,
            "Source amount must be equal to destination amount",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "101@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -32600,
            "Non-DAT or LP tokens are not supported for transferdomain",
            lambda: transfer_domain(
                self.nodes[0],
                self.address,
                self.eth_address,
                "1@" + self.symbolUSER,
                2,
                3,
            ),
        )
        assert_raises_rpc_error(
            -32600,
            "Non-DAT or LP tokens are not supported for transferdomain",
            lambda: transfer_domain(
                self.nodes[0],
                self.address,
                self.eth_address,
                "1@" + self.symbolBTCDFI,
                2,
                3,
            ),
        )

    def valid_transfer_dvm_evm(self):
        self.rollback_to(self.start_height)

        # Transfer 100 DFI from DVM to EVM
        tx1 = transfer_domain(
            self.nodes[0], self.address, self.eth_address, "100@DFI", 2, 3
        )
        self.nodes[0].generate(1)

        # Check tx1 fields
        result = self.nodes[0].getcustomtx(tx1)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.address)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "DVM")
        assert_equal(result["dst"]["address"], self.eth_address)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "EVM")

        # Check that EVM balance shows in gettokenbalances
        assert_equal(
            self.nodes[0].gettokenbalances({}, False, False, True),
            ["101.00000000@0", "99.00000000@1", "9.99999000@2", "100.00000000@128"],
        )

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(self.address, {}, True)["0"]
        new_eth_balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(new_dfi_balance, self.dfi_balance - Decimal("100"))
        assert_equal(new_eth_balance, int_to_eth_u256(100))
        assert_equal(len(self.nodes[0].getaccount(self.eth_address, {}, True)), 1)
        assert_equal(self.nodes[0].getaccount(self.eth_address)[0], "100.00000000@DFI")

        # Check accounting of DVM->EVM transfer
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm-evm/0/total"],
            Decimal("100.00000000"),
        )
        # assert_equal(attributes['v0/live/economy/transferdomain/dvm/0/current'], Decimal('-100.00000000'))
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/out"],
            Decimal("100.00000000"),
        )
        # assert_equal(attributes['v0/live/economy/transferdomain/evm/0/current'], Decimal('100.00000000'))
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/in"],
            Decimal("100.00000000"),
        )

        # Check accounting of EVM fees
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], Decimal("0E-8"))
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], Decimal("0E-8")
        )

    def invalid_values_evm_dvm(self):
        self.rollback_to(self.start_height)

        # Check for valid values EVM->DVM in transferdomain rpc
        assert_raises_rpc_error(
            -32600,
            'Src address must be an ERC55 address in case of "EVM" domain',
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 3},
                    "dst": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -32600,
            'Dst address must be a legacy or Bech32 address in case of "DVM" domain',
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -32600,
            "Cannot transfer inside same domain",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "100@DFI", "domain": 3},
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -32600,
            "Source amount must be equal to destination amount",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.eth_address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "101@DFI", "domain": 2},
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -32600,
            "TransferDomain currently only supports a single transfer per transaction",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.eth_address1,
                        "amount": "10@DFI",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "10@DFI", "domain": 2},
                    "singlekeycheck": False,
                },
                {
                    "src": {
                        "address": self.eth_address1,
                        "amount": "10@DFI",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.address_2nd,
                        "amount": "10@DFI",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                },
            ],
        )
        assert_raises_rpc_error(
            -32600,
            "Non-DAT or LP tokens are not supported for transferdomain",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.address,
                        "amount": "1@" + self.symbolUSER,
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.eth_address,
                        "amount": "1@" + self.symbolUSER,
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -32600,
            "Non-DAT or LP tokens are not supported for transferdomain",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.address,
                        "amount": "1@" + self.symbolBTCDFI,
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.eth_address,
                        "amount": "1@" + self.symbolBTCDFI,
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ],
        )

    def valid_transfer_evm_dvm(self):
        self.rollback_to(self.start_height)

        # Transfer 100 DFI from DVM to EVM
        self.valid_transfer_dvm_evm()

        # Transfer 100 DFI from EVM to DVM
        tx = transfer_domain(
            self.nodes[0], self.eth_address, self.address, "100@DFI", 3, 2
        )
        self.nodes[0].generate(1)

        # Check tx fields
        result = self.nodes[0].getcustomtx(tx)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.eth_address)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "EVM")
        assert_equal(result["dst"]["address"], self.address)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "DVM")

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(self.address, {}, True)["0"]
        new_eth_balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(new_dfi_balance, self.dfi_balance)
        assert_equal(new_eth_balance, self.eth_balance)
        assert_equal(len(self.nodes[0].getaccount(self.eth_address, {}, True)), 0)

        # Check accounting of DVM->EVM transfer
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm-evm/0/total"],
            Decimal("100.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/current"],
            Decimal("0.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/out"],
            Decimal("100.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/in"],
            Decimal("100.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm-dvm/0/total"],
            Decimal("100.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/current"],
            Decimal("0.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/in"],
            Decimal("100.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/out"],
            Decimal("100.00000000"),
        )

        # Check accounting of EVM fees
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], Decimal("0E-8"))
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], Decimal("0E-8")
        )

    def valid_single_key_transfer(self):
        self.rollback_to(self.start_height)

        # Valid DVM legacy address to EVM address with same key transfer
        legacy_address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].accounttoaccount(self.address, {legacy_address: "1@DFI"})
        self.nodes[0].accounttoaccount(self.address, {legacy_address: "1@BTC"})
        self.nodes[0].generate(1)

        # Native transfer
        legacy_erc55_address = self.nodes[0].addressmap(legacy_address, 1)["format"][
            "erc55"
        ]
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": legacy_address, "amount": "1@DFI", "domain": 2},
                    "dst": {
                        "address": legacy_erc55_address,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)
        balance = self.nodes[0].eth_getBalance(legacy_erc55_address)
        assert_equal(balance, int_to_eth_u256(1))

        # Token transfer
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": legacy_address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": legacy_erc55_address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)
        assert_equal(
            self.btc.functions.balanceOf(legacy_erc55_address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )

        # Valid DVM bech32 address to EVM address with same key transfer
        bech32_address = self.nodes[0].getnewaddress("", "bech32")
        self.nodes[0].accounttoaccount(self.address, {bech32_address: "1@DFI"})
        self.nodes[0].accounttoaccount(self.address, {bech32_address: "1@BTC"})
        self.nodes[0].generate(1)

        # Native transfer
        bech32_erc55_address = self.nodes[0].addressmap(bech32_address, 1)["format"][
            "erc55"
        ]
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": bech32_address, "amount": "1@DFI", "domain": 2},
                    "dst": {
                        "address": bech32_erc55_address,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)
        balance = self.nodes[0].eth_getBalance(bech32_erc55_address)
        assert_equal(balance, int_to_eth_u256(1))

        # Token transfer
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": bech32_address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": bech32_erc55_address,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)
        assert_equal(
            self.btc.functions.balanceOf(bech32_erc55_address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(1),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
        )

        # Valid ERC55 address to DVM bech32 address with same key transfer
        erc55_address = self.nodes[0].getnewaddress("", "erc55")
        transfer_domain(self.nodes[0], self.address, erc55_address, "2@DFI", 2, 3)
        transfer_domain(self.nodes[0], self.address, erc55_address, "2@BTC", 2, 3)
        self.nodes[0].generate(1)
        balance = self.nodes[0].eth_getBalance(erc55_address)
        assert_equal(balance, int_to_eth_u256(2))
        assert_equal(
            self.btc.functions.balanceOf(erc55_address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(4),
        )

        # Native transfer
        erc55_bech32_address = self.nodes[0].addressmap(erc55_address, 2)["format"][
            "bech32"
        ]
        erc55_legacy_address = self.nodes[0].addressmap(erc55_address, 2)["format"][
            "legacy"
        ]
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": erc55_address, "amount": "1@DFI", "domain": 3},
                    "dst": {
                        "address": erc55_bech32_address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                }
            ]
        )
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": erc55_address, "amount": "1@DFI", "domain": 3},
                    "dst": {
                        "address": erc55_legacy_address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)
        balance = self.nodes[0].eth_getBalance(erc55_address)
        assert_equal(balance, int_to_eth_u256(0))
        balance = self.nodes[0].getaccount(erc55_bech32_address, {}, True)["0"]
        assert_equal(balance, Decimal("1"))
        balance = self.nodes[0].getaccount(erc55_legacy_address, {}, True)["0"]
        assert_equal(balance, Decimal("1"))

        # Token transfer
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": erc55_address, "amount": "1@BTC", "domain": 3},
                    "dst": {
                        "address": erc55_bech32_address,
                        "amount": "1@BTC",
                        "domain": 2,
                    },
                }
            ]
        )
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": erc55_address, "amount": "1@BTC", "domain": 3},
                    "dst": {
                        "address": erc55_legacy_address,
                        "amount": "1@BTC",
                        "domain": 2,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)
        assert_equal(
            self.btc.functions.balanceOf(erc55_address).call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(0),
        )
        assert_equal(
            self.btc.functions.totalSupply().call()
            / math.pow(10, self.btc.functions.decimals().call()),
            Decimal(2),
        )
        balance = self.nodes[0].getaccount(erc55_bech32_address, {}, True)["1"]
        assert_equal(balance, Decimal("1"))
        balance = self.nodes[0].getaccount(erc55_legacy_address, {}, True)["1"]
        assert_equal(balance, Decimal("1"))

    def invalid_single_key_transfer(self):
        self.rollback_to(self.start_height)

        # Invalid DVM legacy address to EVM address with different key transfer
        legacy_address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].accounttoaccount(self.address, {legacy_address: "1@DFI"})
        self.nodes[0].generate(1)
        assert_raises_rpc_error(
            -5,
            "Dst address does not match source key",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": legacy_address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                    "dst": {
                        "address": self.eth_address,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                }
            ],
        )

        # Invalid DVM bech32 address to EVM address with different key transfer
        bech32_address = self.nodes[0].getnewaddress("", "bech32")
        self.nodes[0].accounttoaccount(self.address, {bech32_address: "1@DFI"})
        self.nodes[0].generate(1)
        assert_raises_rpc_error(
            -5,
            "Dst address does not match source key",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": bech32_address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                    "dst": {
                        "address": self.eth_address,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                }
            ],
        )

        # Invalid ERC55 address to DVM bech32 address with different key transfer
        erc55_address = self.nodes[0].getnewaddress("", "erc55")
        transfer_domain(self.nodes[0], self.address, erc55_address, "1@DFI", 2, 3)
        self.nodes[0].generate(1)
        balance = self.nodes[0].eth_getBalance(erc55_address)
        assert_equal(balance, int_to_eth_u256(1))
        assert_raises_rpc_error(
            -5,
            "Dst address does not match source key",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": erc55_address,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                }
            ],
        )

    def invalid_transfer_sc(self):
        self.rollback_to(self.start_height)

        self.evm_key_pair = EvmKeyPair.from_node(self.nodes[0])
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        abi, bytecode, _ = EVMContract.from_file("Reverter.sol", "Reverter").compile()
        compiled = self.nodes[0].w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
        contract_address = receipt["contractAddress"]

        assert_raises_rpc_error(
            -32600,
            "EVM destination is a smart contract",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                    "dst": {
                        "address": contract_address,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ],
        )

    def invalid_transfer_no_auth(self):
        self.rollback_to(self.start_height)

        assert_raises_rpc_error(
            -5,
            "no full public key for address " + self.address1,
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address1, "amount": "1@DFI", "domain": 2},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ],
        )
        assert_raises_rpc_error(
            -5,
            "no full public key for address",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {
                        "address": self.no_auth_eth_address,
                        "amount": "1@DFI",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "1@DFI", "domain": 2},
                    "singlekeycheck": False,
                }
            ],
        )

    def invalid_transfer_dvm_evm_insufficient_balance(self):
        self.rollback_to(self.start_height)

        # drain account balance
        balance_before = self.nodes[0].getaccount(self.address, {}, True)["0"]
        transfer_amount = str(int(balance_before - 1)) + "@DFI"
        self.nodes[0].accounttoutxos(self.address, {self.address: transfer_amount})
        self.nodes[0].generate(1)
        balance_after = Decimal(self.nodes[0].getaccount(self.address, {}, True)["0"])
        assert_equal(balance_after, Decimal(1.00000000))

        # Invalid transfer domain, insufficient account balance
        assert_raises_rpc_error(
            -32600,
            "amount 1.00000000 is less than 100.00000000",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ],
        )

    def valid_transfer_to_evm_then_move_then_back_to_dvm(self):
        self.rollback_to(self.start_height)

        # Transfer 100 DFI from DVM to EVM
        tx1 = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "101@DFI", "domain": 2},
                    "dst": {
                        "address": self.eth_address,
                        "amount": "101@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        # Check tx1 fields
        result = self.nodes[0].getcustomtx(tx1)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.address)
        assert_equal(result["src"]["amount"], "101.00000000@0")
        assert_equal(result["src"]["domain"], "DVM")
        assert_equal(result["dst"]["address"], self.eth_address)
        assert_equal(result["dst"]["amount"], "101.00000000@0")
        assert_equal(result["dst"]["domain"], "EVM")

        # Check that EVM balance shows in gettokenbalances
        assert_equal(
            self.nodes[0].gettokenbalances({}, False, False, True),
            ["101.00000000@0", "99.00000000@1", "9.99999000@2", "100.00000000@128"],
        )

        # Check new balances
        new_eth_balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(new_eth_balance, int_to_eth_u256(101))
        assert_equal(len(self.nodes[0].getaccount(self.eth_address, {}, True)), 1)
        assert_equal(self.nodes[0].getaccount(self.eth_address)[0], "101.00000000@DFI")

        # Check accounting of DVM->EVM transfer
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm-evm/0/total"],
            Decimal("101.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/current"],
            Decimal("-101.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/out"],
            Decimal("101.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/current"],
            Decimal("101.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/in"],
            Decimal("101.00000000"),
        )

        # Move from one EVM address to another
        self.nodes[0].evmtx(self.eth_address, 0, 21, 21001, self.eth_address1, 100)
        self.nodes[0].generate(1)
        blockHash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())

        new_eth1_balance = self.nodes[0].eth_getBalance(self.eth_address1)
        assert_equal(new_eth1_balance, int_to_eth_u256(100))

        # Check accounting of EVM fees 21 Gwei * 21000 = 44100 sat, burnt 21000, paid 44100 - 21000 = 23100
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        self.burnt_fee = Decimal("0.00021000")
        self.priority_fee = Decimal("0.00023100")
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burnt_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burnt_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"], blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"], blockHash
        )

        dfi_balance = self.nodes[0].getaccount(self.address, {}, True).get("0", 0)

        # Transfer 100 DFI from EVM to DVM
        tx = self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.eth_address1,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "dst": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        # Check tx fields
        result = self.nodes[0].getcustomtx(tx)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.eth_address1)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "EVM")
        assert_equal(result["dst"]["address"], self.address)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "DVM")

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(self.address, {}, True)["0"]
        assert_equal(new_dfi_balance, dfi_balance + Decimal("100"))
        new_eth1_balance = self.nodes[0].eth_getBalance(self.eth_address1)
        assert_equal(new_eth1_balance, "0x0")
        assert_equal(len(self.nodes[0].getaccount(self.eth_address1, {}, True)), 0)

        # Check accounting of DVM->EVM transfer
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm-evm/0/total"],
            Decimal("101.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/current"],
            Decimal("-1.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/out"],
            Decimal("101.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/dvm/0/in"],
            Decimal("100.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm-dvm/0/total"],
            Decimal("100.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/current"],
            Decimal("1.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/in"],
            Decimal("101.00000000"),
        )
        assert_equal(
            attributes["v0/live/economy/transferdomain/evm/0/out"],
            Decimal("100.00000000"),
        )

    def invalid_transfer_evm_dvm_after_evm_tx(self):
        self.rollback_to(self.start_height)

        # Transfer 100 DFI from DVM to EVM
        self.valid_transfer_dvm_evm()

        balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(balance, "0x56bc75e2d63100000")  # 100 DFI
        erc55_address = self.nodes[0].getnewaddress("", "erc55")

        tx1 = self.nodes[0].evmtx(
            self.eth_address, 0, 21, 21001, erc55_address, 50
        )  # Spend half balance
        self.nodes[0].generate(1)

        block = self.nodes[0].eth_getBlockByNumber("latest")
        assert_equal(len(block["transactions"]), 1)
        evm_tx = self.nodes[0].vmmap(tx1, 0)["output"]
        assert_equal(block["transactions"][0], evm_tx)

    def conflicting_transferdomain_evmtx(self):
        self.rollback_to(self.start_height)

        self.nodes[0].utxostoaccount({self.address: "200@DFI"})
        transfer_domain(
            self.nodes[0], self.address, self.address_erc55, "100@DFI", 2, 3
        )
        self.nodes[0].generate(1)

        burn_address = self.nodes[0].w3.to_checksum_address(
            "0x0000000000000000000000000000000000000000"
        )
        self.nodes[0].eth_sendTransaction(
            {
                "nonce": self.nodes[0].w3.to_hex(
                    self.nodes[0].w3.eth.get_transaction_count(self.address_erc55)
                ),
                "from": self.address_erc55,
                "to": burn_address,
                "value": "0x1",
                "gas": "0x100000",
                "gasPrice": "0x4e3b29200",
            }
        )
        self.nodes[0].generate(1)

        burn_balance = Decimal(self.nodes[0].w3.eth.get_balance(burn_address))
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.address_erc55)
        sender_nonce = self.nodes[0].w3.eth.get_transaction_count(self.address_erc55)

        self.nodes[0].eth_sendTransaction(
            {
                "nonce": self.nodes[0].w3.to_hex(nonce),
                "from": self.address_erc55,
                "to": "0x0000000000000000000000000000000000000000",
                "value": "0x1",
                "gas": "0x100000",
                "gasPrice": "0x4e3b29200",
            }
        )
        tx = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "nonce": nonce,
                }
            ]
        )
        self.nodes[0].generate(1)

        burn_balance_after = Decimal(self.nodes[0].w3.eth.get_balance(burn_address))
        sender_nonce_after = self.nodes[0].w3.eth.get_transaction_count(
            self.address_erc55
        )

        # transferdomain should take precedence, evmtx should be discarded
        assert_equal(sender_nonce + 1, sender_nonce_after)
        assert_equal(burn_balance_after, burn_balance)
        result = self.nodes[0].getcustomtx(tx)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.address)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "DVM")
        assert_equal(result["dst"]["address"], self.address_erc55)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "EVM")

    def should_find_empty_nonce(self):
        self.rollback_to(self.start_height)

        self.nodes[0].utxostoaccount({self.address: "200@DFI"})
        transfer_domain(
            self.nodes[0], self.address, self.address_erc55, "100@DFI", 2, 3
        )
        self.nodes[0].generate(1)

        nonce = self.nodes[0].w3.eth.get_transaction_count(self.address_erc55)
        evm_nonces = [nonce, nonce + 2, nonce + 3]

        # send evmtxs with nonces
        for nonce in evm_nonces:
            self.nodes[0].eth_sendTransaction(
                {
                    "nonce": self.nodes[0].w3.to_hex(nonce),
                    "from": self.address_erc55,
                    "to": "0x0000000000000000000000000000000000000000",
                    "value": "0x1",
                    "gas": "0x100000",
                    "gasPrice": "0x4e3b29200",
                }
            )

        # send transferdomain without specifying nonce
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        balance_before = self.nodes[0].w3.eth.get_balance(self.address_erc55)
        self.nodes[0].generate(1)
        balance_after = self.nodes[0].w3.eth.get_balance(self.address_erc55)

        assert_equal(
            len(self.nodes[0].getrawmempool()), 0
        )  # all TXs should make it through
        assert balance_after > balance_before  # transferdomain should succeed

    def invalid_transfer_invalid_nonce(self):
        self.rollback_to(self.start_height)

        self.nodes[0].utxostoaccount({self.address: "200@DFI"})
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.address_erc55)
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "nonce": nonce,
                }
            ]
        )
        self.nodes[0].generate(1)

        assert_raises_rpc_error(
            -32600,
            "transferdomain evm tx failed to pre-validate : Invalid nonce. Account nonce 1, signed_tx nonce 0",
            self.nodes[0].transferdomain,
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "nonce": nonce,
                }
            ],
        )

    def test_contract_methods(self):
        self.rollback_to(self.start_height)

        contract = self.nodes[0].w3.eth.contract(
            address=self.contract_address, abi=self.abi
        )

        assert_equal(contract.functions.name().call(), "TransferDomain")
        assert_equal(contract.functions.symbol().call(), "DFI")
        assert_equal(contract.functions.totalSupply().call(), 0)
        assert_equal(contract.functions.balanceOf(self.address_erc55).call(), 0)

        self.nodes[0].utxostoaccount({self.address: "200@DFI"})
        self.nodes[0].generate(1)

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)

        assert_equal(
            contract.functions.totalSupply().call(),
            self.nodes[0].w3.to_wei(100, "ether"),
        )
        assert_equal(
            contract.functions.balanceOf(self.address_erc55).call(),
            self.nodes[0].w3.to_wei(0, "ether"),
        )  # Don't track balance per address

        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.address,
                        "amount": "100@DFI",
                        "domain": 2,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)

        assert_equal(contract.functions.totalSupply().call(), 0)
        assert_equal(contract.functions.balanceOf(self.address_erc55).call(), 0)

    def run_test(self):
        self.setup()
        self.invalid_before_fork_and_disabled()

        self.setup_after_evm_activation()
        self.invalid_parameters()

        # Transfer DVM->EVM
        self.invalid_values_dvm_evm()
        self.valid_transfer_dvm_evm()

        # Transfer EVM->DVM
        self.invalid_values_evm_dvm()
        self.valid_transfer_evm_dvm()

        # Single key transfer
        self.valid_single_key_transfer()
        self.invalid_single_key_transfer()

        # Transfer to smart contract
        self.invalid_transfer_sc()

        # Invalid authorisation
        self.invalid_transfer_no_auth()

        # Invalid DVM account insufficient balance
        self.invalid_transfer_dvm_evm_insufficient_balance()

        self.valid_transfer_to_evm_then_move_then_back_to_dvm()

        self.invalid_transfer_evm_dvm_after_evm_tx()  # TODO assert behaviour here. transferdomain shouldn't be kept in mempool since its nonce will never be valid

        self.conflicting_transferdomain_evmtx()

        self.should_find_empty_nonce()

        self.invalid_transfer_invalid_nonce()

        self.test_contract_methods()


if __name__ == "__main__":
    EVMTest().main()
